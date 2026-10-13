package io.github.binaraherath.dbms.telemetry;

import java.io.IOException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.event.EventListener;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import io.github.binaraherath.dbms.config.BmsProperties;
import io.github.binaraherath.dbms.ingest.GatewayEvents.GatewayStatusChanged;
import io.github.binaraherath.dbms.ingest.GatewayEvents.TelemetryReceived;
import io.github.binaraherath.dbms.ingest.PackSnapshot;

/**
 * Observer: keeps the newest snapshot of every pack in memory and pushes it to
 * browsers over Server-Sent Events. The live view is never read from the database.
 */
@Service
public class LiveTelemetryService {

    private static final Logger log = LoggerFactory.getLogger(LiveTelemetryService.class);
    private static final long EMITTER_TIMEOUT_MS = Duration.ofMinutes(30).toMillis();

    /** What the dashboard receives on every update. */
    public record LiveTelemetryDto(long packId, boolean gatewayOnline, Instant lastMessageAt,
                                   Instant serverTime, PackSnapshot snapshot) {
    }

    /** Sent every few seconds so the dashboard notices a silent gateway. */
    public record LiveHeartbeatDto(long packId, boolean gatewayOnline, Instant lastMessageAt, Instant serverTime) {
    }

    private record PackState(PackSnapshot snapshot, Instant lastMessageAt, Boolean statusOnline) {
    }

    private final Clock clock;
    private final Duration gatewayStaleAfter;
    private final Map<Long, PackState> state = new ConcurrentHashMap<>();
    private final Map<Long, List<SseEmitter>> emitters = new ConcurrentHashMap<>();

    public LiveTelemetryService(Clock clock, BmsProperties props) {
        this.clock = clock;
        this.gatewayStaleAfter = props.live().gatewayStaleAfter();
    }

    // ------------------------------------------------------------ observers

    @EventListener
    public void onTelemetry(TelemetryReceived e) {
        Instant now = clock.instant();
        state.compute(e.packId(), (id, old) -> {
            Boolean status = old == null ? null : old.statusOnline();
            // a replayed (buffered) snapshot proves the gateway is alive but is older than what we show
            if (e.snapshot().replay() && old != null && old.snapshot() != null) {
                return new PackState(old.snapshot(), now, status);
            }
            return new PackState(e.snapshot(), now, status);
        });
        if (!e.snapshot().replay()) {
            latest(e.packId()).ifPresent(dto -> broadcast(e.packId(), "telemetry", dto));
        }
    }

    @EventListener
    public void onGatewayStatus(GatewayStatusChanged e) {
        state.compute(e.packId(), (id, old) -> old == null
                ? new PackState(null, null, e.online())
                : new PackState(old.snapshot(), old.lastMessageAt(), e.online()));
        broadcast(e.packId(), "heartbeat", heartbeat(e.packId()));
    }

    // ------------------------------------------------------------ queries

    public Optional<LiveTelemetryDto> latest(long packId) {
        PackState s = state.get(packId);
        if (s == null || s.snapshot() == null) {
            return Optional.empty();
        }
        return Optional.of(new LiveTelemetryDto(packId, isGatewayOnline(packId), s.lastMessageAt(),
                clock.instant(), s.snapshot()));
    }

    /** Online = the gateway has not announced "offline" and has spoken in the last few seconds. */
    public boolean isGatewayOnline(long packId) {
        PackState s = state.get(packId);
        if (s == null || s.lastMessageAt() == null || Boolean.FALSE.equals(s.statusOnline())) {
            return false;
        }
        return Duration.between(s.lastMessageAt(), clock.instant()).compareTo(gatewayStaleAfter) <= 0;
    }

    private LiveHeartbeatDto heartbeat(long packId) {
        PackState s = state.get(packId);
        return new LiveHeartbeatDto(packId, isGatewayOnline(packId),
                s == null ? null : s.lastMessageAt(), clock.instant());
    }

    // ------------------------------------------------------------ SSE

    public SseEmitter subscribe(long packId) {
        SseEmitter emitter = new SseEmitter(EMITTER_TIMEOUT_MS);
        List<SseEmitter> list = emitters.computeIfAbsent(packId, id -> new CopyOnWriteArrayList<>());
        list.add(emitter);
        emitter.onCompletion(() -> list.remove(emitter));
        emitter.onTimeout(() -> list.remove(emitter));
        emitter.onError(ex -> list.remove(emitter));
        try {
            Optional<LiveTelemetryDto> now = latest(packId);
            if (now.isPresent()) {
                emitter.send(SseEmitter.event().name("telemetry").data(now.get()));
            } else {
                emitter.send(SseEmitter.event().name("heartbeat").data(heartbeat(packId)));
            }
        } catch (IOException ex) {
            list.remove(emitter);
        }
        return emitter;
    }

    @Scheduled(fixedRate = 3000)
    void sendHeartbeats() {
        emitters.forEach((packId, list) -> {
            if (!list.isEmpty()) {
                broadcast(packId, "heartbeat", heartbeat(packId));
            }
        });
    }

    private void broadcast(long packId, String eventName, Object data) {
        List<SseEmitter> list = emitters.get(packId);
        if (list == null) {
            return;
        }
        for (SseEmitter emitter : list) {
            try {
                emitter.send(SseEmitter.event().name(eventName).data(data));
            } catch (IOException | IllegalStateException ex) {
                // browser tab closed or connection dropped - the client reconnects by itself
                list.remove(emitter);
                log.debug("SSE client removed: {}", ex.getMessage());
            }
        }
    }
}
