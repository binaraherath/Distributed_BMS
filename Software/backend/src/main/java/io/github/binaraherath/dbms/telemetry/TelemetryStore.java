package io.github.binaraherath.dbms.telemetry;

import java.sql.Timestamp;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.function.IntToLongFunction;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Repository;
import org.springframework.transaction.annotation.Transactional;

import io.github.binaraherath.dbms.config.BmsProperties;
import io.github.binaraherath.dbms.ingest.PackSnapshot;
import io.github.binaraherath.dbms.ingest.PackSnapshot.DataSource;
import io.github.binaraherath.dbms.ingest.PackSnapshot.ModuleSnapshot;

/**
 * Writes telemetry with plain JDBC: one sample row plus a row per cell and per NTC.
 * JPA would add nothing here (no entity life cycle, append-only rows) and batch
 * inserts are simpler to reason about.
 */
@Repository
public class TelemetryStore {

    private static final Logger log = LoggerFactory.getLogger(TelemetryStore.class);

    private static final String INSERT_SAMPLE = """
            INSERT INTO telemetry_sample (pack_id, recorded_at, received_at, pack_voltage_mv, current_ma,
                soc_pct, soh_pct, cell_min_mv, cell_max_mv, temp_max_dc, bms_state, bms_fault_bits, bms_source,
                ctrl_state, ctrl_fault_bits, contactor_bits, fan_duty_pct, ctrl_source)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT (pack_id, recorded_at) DO NOTHING
            RETURNING id""";

    private static final String INSERT_CELL =
            "INSERT INTO cell_reading (sample_id, module_id, cell_no, voltage_mv, balancing) VALUES (?, ?, ?, ?, ?)";

    private static final String INSERT_TEMP =
            "INSERT INTO temperature_reading (sample_id, module_id, sensor_no, temp_dc) VALUES (?, ?, ?, ?)";

    private final JdbcTemplate jdbc;
    private final Clock clock;
    private final int retentionDays;

    public TelemetryStore(JdbcTemplate jdbc, Clock clock, BmsProperties props) {
        this.jdbc = jdbc;
        this.clock = clock;
        this.retentionDays = props.storage().retentionDays();
    }

    /**
     * Saves one snapshot - sample, cells and temperatures - as a single transaction:
     * either every row is written or none is.
     *
     * @param moduleIdOf maps a node id to its bms_module id
     * @return the new sample id, or empty if this (pack, time) was already stored (a replay)
     */
    @Transactional
    public Optional<Long> save(long packId, PackSnapshot s, IntToLongFunction moduleIdOf) {
        List<ModuleSnapshot> withData = s.modules().stream()
                .filter(m -> m.source() != DataSource.OFFLINE)
                .toList();
        if (withData.isEmpty()) {
            return Optional.empty();
        }
        var ctrl = s.controller();
        boolean ctrlKnown = ctrl.source() != DataSource.OFFLINE;

        List<Long> ids = jdbc.query(INSERT_SAMPLE, (rs, n) -> rs.getLong(1),
                packId,
                Timestamp.from(s.recordedAt()),
                Timestamp.from(s.receivedAt()),
                s.pack().voltageMv(),
                s.pack().currentMa(),
                s.pack().socPct(),
                s.pack().sohPct(),
                withData.stream().mapToInt(ModuleSnapshot::cellMinMv).min().orElse(0),
                withData.stream().mapToInt(ModuleSnapshot::cellMaxMv).max().orElse(0),
                withData.stream().mapToInt(ModuleSnapshot::tempMaxDc).max().orElse(0),
                worstState(withData),
                withData.stream().mapToInt(ModuleSnapshot::faultBits).reduce(0, (a, b) -> a | b),
                withData.stream().anyMatch(m -> m.source() == DataSource.LIVE) ? "LIVE" : "SIMULATED",
                ctrlKnown ? ctrl.state() : null,
                ctrlKnown ? ctrl.faultBits() : 0,
                ctrlKnown ? ctrl.contactors().bits() : 0,
                ctrlKnown ? ctrl.fanDutyPct() : null,
                ctrl.source().name());
        if (ids.isEmpty()) {
            return Optional.empty();
        }
        long sampleId = ids.getFirst();

        List<Object[]> cells = new ArrayList<>();
        List<Object[]> temps = new ArrayList<>();
        for (ModuleSnapshot m : withData) {
            long moduleId = moduleIdOf.applyAsLong(m.nodeId());
            for (int i = 0; i < m.cellsMv().size(); i++) {
                int localNo = i + 1;
                cells.add(new Object[] {sampleId, moduleId, m.firstCellNo() + i, m.cellsMv().get(i),
                        m.balancing().cells().contains(localNo)});
            }
            for (int t = 0; t < m.tempsDc().size(); t++) {
                temps.add(new Object[] {sampleId, moduleId, t + 1, m.tempsDc().get(t)});
            }
        }
        jdbc.batchUpdate(INSERT_CELL, cells);
        jdbc.batchUpdate(INSERT_TEMP, temps);
        jdbc.update("UPDATE battery_pack SET last_seen_at = GREATEST(COALESCE(last_seen_at, ?), ?) WHERE id = ?",
                Timestamp.from(s.recordedAt()), Timestamp.from(s.recordedAt()), packId);
        return Optional.of(sampleId);
    }

    /** Daily clean-up; readings go with their sample through ON DELETE CASCADE. */
    @Scheduled(cron = "0 30 3 * * *")
    @Transactional
    public void purgeOldSamples() {
        Instant cutoff = clock.instant().minus(Duration.ofDays(retentionDays));
        int deleted = jdbc.update("DELETE FROM telemetry_sample WHERE recorded_at < ?", Timestamp.from(cutoff));
        if (deleted > 0) {
            log.info("Retention: deleted {} samples older than {} days", deleted, retentionDays);
        }
    }

    private static String worstState(List<ModuleSnapshot> modules) {
        return modules.stream().map(ModuleSnapshot::state)
                .filter("FAULT"::equals).findFirst()
                .orElse(modules.getFirst().state());
    }
}
