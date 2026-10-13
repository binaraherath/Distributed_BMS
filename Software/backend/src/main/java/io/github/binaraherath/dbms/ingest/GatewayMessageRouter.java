package io.github.binaraherath.dbms.ingest;

import java.nio.charset.StandardCharsets;
import java.time.Clock;
import java.util.Optional;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.ApplicationEventPublisher;
import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.config.BmsProperties;
import io.github.binaraherath.dbms.ingest.GatewayEvents.CommandAcknowledged;
import io.github.binaraherath.dbms.ingest.GatewayEvents.GatewayStatusChanged;
import io.github.binaraherath.dbms.ingest.GatewayEvents.TelemetryReceived;
import io.github.binaraherath.dbms.pack.PackRegistry;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;

/**
 * Entry point for every MQTT message from a gateway: works out what it is, converts
 * it, and publishes the matching {@link GatewayEvents} event.
 */
@Component
public class GatewayMessageRouter {

    private static final Logger log = LoggerFactory.getLogger(GatewayMessageRouter.class);

    private final String topicRoot;
    private final GatewayJsonV1Adapter adapter;
    private final PackRegistry packs;
    private final ApplicationEventPublisher events;
    private final ObjectMapper mapper;
    private final Clock clock;

    public GatewayMessageRouter(BmsProperties props, GatewayJsonV1Adapter adapter, PackRegistry packs,
                                ApplicationEventPublisher events, ObjectMapper mapper, Clock clock) {
        this.topicRoot = props.mqtt().topicRoot();
        this.adapter = adapter;
        this.packs = packs;
        this.events = events;
        this.mapper = mapper;
        this.clock = clock;
    }

    public void route(String topic, byte[] payload) {
        GatewayTopic t = GatewayTopic.parse(topicRoot, topic);
        switch (t.kind()) {
            case TELEMETRY -> onTelemetry(t.deviceId(), payload);
            case STATUS -> onStatus(t.deviceId(), payload);
            case COMMAND_ACK -> onCommandAck(t.deviceId(), payload);
            default -> { /* events topic is informational; alarms come from telemetry bits */ }
        }
    }

    private void onTelemetry(String deviceId, byte[] payload) {
        PackSnapshot snapshot = adapter.adapt(deviceId, payload);
        long packId = packs.resolve(snapshot);
        events.publishEvent(new TelemetryReceived(packId, snapshot));
    }

    private void onStatus(String deviceId, byte[] payload) {
        Optional<Long> packId = packs.findPackId(deviceId);
        if (packId.isEmpty()) {
            log.debug("Status from unknown gateway {} ignored until its first telemetry", deviceId);
            return;
        }
        boolean online = "online".equalsIgnoreCase(new String(payload, StandardCharsets.UTF_8).trim());
        events.publishEvent(new GatewayStatusChanged(packId.get(), online, clock.instant()));
    }

    private void onCommandAck(String deviceId, byte[] payload) {
        Optional<Long> packId = packs.findPackId(deviceId);
        if (packId.isEmpty()) {
            return;
        }
        JsonNode ack = mapper.readTree(payload);
        String id = ack.path("id").asString("");
        events.publishEvent(new CommandAcknowledged(
                packId.get(),
                ack.path("cmd").asString(""),
                "sent".equalsIgnoreCase(ack.path("result").asString("")),
                ack.path("reason").asString(""),
                id.isBlank() ? null : id,
                clock.instant()));
    }
}
