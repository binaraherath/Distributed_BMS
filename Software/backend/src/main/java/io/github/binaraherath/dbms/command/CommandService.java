package io.github.binaraherath.dbms.command;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.ApplicationEventPublisher;
import org.springframework.context.event.EventListener;
import org.springframework.data.domain.Pageable;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.TransactionDefinition;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.transaction.event.TransactionPhase;
import org.springframework.transaction.event.TransactionalEventListener;
import org.springframework.transaction.support.TransactionTemplate;

import io.github.binaraherath.dbms.command.CommandDefinitions.Catalog;
import io.github.binaraherath.dbms.command.CommandDefinitions.CommandDefinition;
import io.github.binaraherath.dbms.common.ApiExceptions;
import io.github.binaraherath.dbms.common.PageResponse;
import io.github.binaraherath.dbms.config.BmsProperties;
import io.github.binaraherath.dbms.ingest.GatewayCommandPublisher;
import io.github.binaraherath.dbms.ingest.GatewayCommandPublisher.GatewayUnavailableException;
import io.github.binaraherath.dbms.ingest.GatewayEvents.CommandAcknowledged;
import io.github.binaraherath.dbms.pack.BatteryPack;
import io.github.binaraherath.dbms.pack.PackService;
import io.github.binaraherath.dbms.telemetry.LiveTelemetryService;
import io.github.binaraherath.dbms.user.UserService;
import jakarta.validation.constraints.NotNull;
import tools.jackson.databind.ObjectMapper;

/**
 * Remote commands. The command row is saved first and published to MQTT only after
 * the database transaction commits, so a command is never sent without a record.
 */
@Service
public class CommandService {

    private static final Logger log = LoggerFactory.getLogger(CommandService.class);

    public record IssueCommandRequest(@NotNull CommandType type, Integer argument) {
    }

    public record CommandDto(long id, long packId, CommandType type, Integer argument, CommandStatus status,
                             String issuedBy, Instant issuedAt, Instant completedAt, String resultMessage) {

        static CommandDto of(RemoteCommand c) {
            return new CommandDto(c.getId(), c.getPackId(), c.getCommandType(), c.getArgument(), c.getStatus(),
                    c.getIssuedBy().getUsername(), c.getIssuedAt(), c.getCompletedAt(), c.getResultMessage());
        }
    }

    /** Internal event: a command was saved and must be published once the transaction commits. */
    record CommandSaved(long commandId) {
    }

    private final RemoteCommandRepository commands;
    private final Catalog catalog;
    private final PackService packs;
    private final UserService users;
    private final LiveTelemetryService live;
    private final GatewayCommandPublisher publisher;
    private final ApplicationEventPublisher events;
    private final ObjectMapper mapper;
    private final Clock clock;
    private final TransactionTemplate newTx;
    private final Duration ackTimeout;
    private final String gatewayToken;

    CommandService(RemoteCommandRepository commands, Catalog catalog, PackService packs, UserService users,
                   LiveTelemetryService live, GatewayCommandPublisher publisher, ApplicationEventPublisher events,
                   ObjectMapper mapper, Clock clock, PlatformTransactionManager txManager, BmsProperties props) {
        this.commands = commands;
        this.catalog = catalog;
        this.packs = packs;
        this.users = users;
        this.live = live;
        this.publisher = publisher;
        this.events = events;
        this.mapper = mapper;
        this.clock = clock;
        this.newTx = new TransactionTemplate(txManager);
        this.newTx.setPropagationBehavior(TransactionDefinition.PROPAGATION_REQUIRES_NEW);
        this.ackTimeout = props.commands().ackTimeout();
        this.gatewayToken = props.mqtt().commandToken();
    }

    // ------------------------------------------------------------ issue

    @Transactional
    public CommandDto issue(long packId, IssueCommandRequest req, String username) {
        packs.find(packId);
        CommandDefinition definition = catalog.get(req.type());
        definition.validate(req.argument());
        if (!live.isGatewayOnline(packId)) {
            throw new ApiExceptions.Conflict("The pack's gateway is offline - the command cannot be delivered");
        }
        RemoteCommand saved = commands.save(new RemoteCommand(packId, req.type(), req.argument(),
                users.findByUsername(username), clock.instant()));
        events.publishEvent(new CommandSaved(saved.getId()));
        return CommandDto.of(saved);
    }

    @TransactionalEventListener(phase = TransactionPhase.AFTER_COMMIT)
    void publishAfterCommit(CommandSaved e) {
        newTx.executeWithoutResult(status -> {
            RemoteCommand c = commands.findById(e.commandId()).orElseThrow();
            BatteryPack pack = packs.find(c.getPackId());
            Map<String, Object> json = new LinkedHashMap<>();
            catalog.get(c.getCommandType()).writePayload(json, c.getArgument());
            json.put("token", gatewayToken);
            json.put("id", c.getCorrelationId().toString());
            try {
                publisher.publishCommand(pack.getDeviceId(), mapper.writeValueAsString(json));
                c.markPublished();
            } catch (GatewayUnavailableException ex) {
                log.warn("Command {} not delivered: {}", c.getId(), ex.getMessage());
                c.complete(CommandStatus.FAILED, clock.instant(), ex.getMessage());
            }
        });
    }

    // ------------------------------------------------------------ outcome

    @EventListener
    public void onAck(CommandAcknowledged ack) {
        newTx.executeWithoutResult(status -> findCommandFor(ack).ifPresent(c -> c.complete(
                ack.accepted() ? CommandStatus.ACCEPTED : CommandStatus.REJECTED, ack.at(), ack.reason())));
    }

    private Optional<RemoteCommand> findCommandFor(CommandAcknowledged ack) {
        if (ack.correlationId() != null) {
            try {
                return commands.findByCorrelationId(UUID.fromString(ack.correlationId()));
            } catch (IllegalArgumentException ex) {
                return Optional.empty();
            }
        }
        CommandType type = CommandType.fromGatewayName(ack.command());
        if (type == null) {
            return Optional.empty();
        }
        return commands.findFirstByPackIdAndCommandTypeAndStatusAndIssuedAtAfterOrderByIssuedAtDesc(
                ack.packId(), type, CommandStatus.PUBLISHED, ack.at().minus(ackTimeout));
    }

    @Scheduled(fixedDelay = 2000)
    @Transactional
    public void timeOutUnanswered() {
        Instant now = clock.instant();
        int n = commands.timeOutWaiting(now.minus(ackTimeout), now);
        if (n > 0) {
            log.info("{} command(s) timed out without an answer", n);
        }
    }

    // ------------------------------------------------------------ queries

    @Transactional(readOnly = true)
    public PageResponse<CommandDto> list(long packId, Pageable pageable) {
        packs.find(packId);
        return PageResponse.of(commands.findByPackId(packId, pageable), CommandDto::of);
    }

    @Transactional(readOnly = true)
    public CommandDto get(long commandId) {
        return commands.findWithUserById(commandId).map(CommandDto::of)
                .orElseThrow(() -> new ApiExceptions.NotFound("Command " + commandId + " not found"));
    }
}
