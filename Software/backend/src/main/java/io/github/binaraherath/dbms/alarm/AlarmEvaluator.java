package io.github.binaraherath.dbms.alarm;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Component;
import org.springframework.transaction.support.TransactionTemplate;

import io.github.binaraherath.dbms.ingest.GatewayEvents.TelemetryReceived;
import io.github.binaraherath.dbms.ingest.PackSnapshot;
import io.github.binaraherath.dbms.ingest.PackSnapshot.DataSource;
import io.github.binaraherath.dbms.ingest.PackSnapshot.ModuleSnapshot;
import io.github.binaraherath.dbms.pack.PackRegistry;

/**
 * Observer: opens an alarm when a fault bit sets and closes it when the bit clears.
 * <p>
 * Alarms are derived from the telemetry fault bits, not from the gateway's event
 * topic, because the gateway buffers telemetry while offline but not events.
 * Only LIVE data is evaluated: a node that is offline or simulated keeps its
 * alarms as they were, rather than "clearing" them because data stopped.
 */
@Component
class AlarmEvaluator {

    private static final Logger log = LoggerFactory.getLogger(AlarmEvaluator.class);

    /** Fault bits that must be raised / cleared to move from the open alarms to the current bits. */
    record BitDiff(List<Integer> raise, List<Integer> clear) {
        boolean isEmpty() {
            return raise.isEmpty() && clear.isEmpty();
        }
    }

    private final AlarmRepository alarms;
    private final FaultCodeRepository codes;
    private final PackRegistry packs;
    private final TransactionTemplate tx;

    /** Last evaluated bits per (pack, source, module): the database is touched only on change. */
    private final Map<String, Integer> lastBits = new ConcurrentHashMap<>();
    private volatile Map<String, FaultCode> codeByKey;

    AlarmEvaluator(AlarmRepository alarms, FaultCodeRepository codes, PackRegistry packs, TransactionTemplate tx) {
        this.alarms = alarms;
        this.codes = codes;
        this.packs = packs;
        this.tx = tx;
    }

    @EventListener
    public void onTelemetry(TelemetryReceived e) {
        PackSnapshot s = e.snapshot();
        if (s.replay()) {
            return;   // old data must not reopen or close today's alarms
        }
        for (ModuleSnapshot m : s.modules()) {
            if (m.source() == DataSource.LIVE) {
                evaluate(e.packId(), "BMS", packs.moduleId(e.packId(), m.nodeId()), m.faultBits(), s.recordedAt());
            }
        }
        if (s.controller().source() == DataSource.LIVE) {
            evaluate(e.packId(), "CTRL", null, s.controller().faultBits(), s.recordedAt());
        }
    }

    void evaluate(long packId, String source, Long moduleId, int bits, Instant at) {
        String key = packId + ":" + source + ":" + moduleId;
        Integer previous = lastBits.get(key);
        if (previous != null && previous == bits) {
            return;
        }
        tx.executeWithoutResult(status -> reconcile(packId, source, moduleId, bits, at));
        lastBits.put(key, bits);
    }

    private void reconcile(long packId, String source, Long moduleId, int bits, Instant at) {
        List<Alarm> open = alarms.findOpenByPack(packId).stream()
                .filter(a -> a.getFaultCode().getSource().equals(source) && Objects.equals(a.getModuleId(), moduleId))
                .toList();
        Set<Integer> openBits = open.stream().map(a -> a.getFaultCode().getBit()).collect(Collectors.toSet());
        BitDiff diff = diff(bits, openBits);

        for (int bit : diff.raise()) {
            FaultCode code = codes().get(source + ":" + bit);
            if (code != null) {
                alarms.save(new Alarm(packId, moduleId, code, at));
                log.info("Alarm raised: pack {} {} {}", packId, source, code.getCode());
            }
        }
        for (Alarm a : open) {
            if (diff.clear().contains(a.getFaultCode().getBit())) {
                a.clear(at);
                log.info("Alarm cleared: pack {} {} {}", packId, source, a.getFaultCode().getCode());
            }
        }
    }

    static BitDiff diff(int currentBits, Set<Integer> openBits) {
        List<Integer> raise = new ArrayList<>();
        List<Integer> clear = new ArrayList<>();
        for (int bit = 0; bit < 24; bit++) {
            boolean set = (currentBits & (1 << bit)) != 0;
            if (set && !openBits.contains(bit)) {
                raise.add(bit);
            } else if (!set && openBits.contains(bit)) {
                clear.add(bit);
            }
        }
        return new BitDiff(raise, clear);
    }

    private Map<String, FaultCode> codes() {
        Map<String, FaultCode> c = codeByKey;
        if (c == null) {
            c = codes.findAll().stream()
                    .collect(Collectors.toUnmodifiableMap(f -> f.getSource() + ":" + f.getBit(), f -> f));
            codeByKey = c;
        }
        return c;
    }
}
