package io.github.binaraherath.dbms.telemetry;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.dao.DataIntegrityViolationException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.test.context.ActiveProfiles;

import io.github.binaraherath.dbms.TestcontainersConfiguration;
import io.github.binaraherath.dbms.ingest.GatewayJsonV1Adapter;
import io.github.binaraherath.dbms.ingest.PackSnapshot;
import io.github.binaraherath.dbms.ingest.PackSnapshot.ModuleSnapshot;
import io.github.binaraherath.dbms.pack.PackRegistry;

/** The one multi-table transaction: a sample, its 16 cells and 4 temperatures, all or nothing. */
@SpringBootTest
@ActiveProfiles("test")
@Import(TestcontainersConfiguration.class)
class TelemetryStoreIntegrationTest {

    @Autowired GatewayJsonV1Adapter adapter;
    @Autowired PackRegistry packs;
    @Autowired TelemetryStore store;
    @Autowired JdbcTemplate jdbc;

    private PackSnapshot snapshot(String device, Instant ts) throws IOException {
        try (InputStream in = getClass().getResourceAsStream("/fixtures/telemetry-v1.json")) {
            String json = new String(in.readAllBytes(), StandardCharsets.UTF_8)
                    .replace("\"ts\":1791316800000", "\"ts\":" + ts.toEpochMilli());
            return adapter.adapt(device, json.getBytes(StandardCharsets.UTF_8));
        }
    }

    private long count(String table, long packId) {
        String sql = table.equals("telemetry_sample")
                ? "select count(*) from telemetry_sample where pack_id = ?"
                : "select count(*) from " + table + " r join telemetry_sample s on s.id = r.sample_id where s.pack_id = ?";
        return jdbc.queryForObject(sql, Long.class, packId);
    }

    @Test
    void savesOneSampleWithSixteenCellsAndFourTemperatures() throws IOException {
        PackSnapshot s = snapshot("store-a", Instant.parse("2026-10-07T08:00:00Z"));
        long packId = packs.resolve(s);

        Optional<Long> id = store.save(packId, s, node -> packs.moduleId(packId, node));

        assertThat(id).isPresent();
        assertThat(count("telemetry_sample", packId)).isEqualTo(1);
        assertThat(count("cell_reading", packId)).isEqualTo(16);
        assertThat(count("temperature_reading", packId)).isEqualTo(4);
        assertThat(jdbc.queryForObject("select ctrl_source from telemetry_sample where id = ?", String.class, id.get()))
                .isEqualTo("SIMULATED");
    }

    @Test
    void ignoresAReplayOfASampleItAlreadyHas() throws IOException {
        PackSnapshot s = snapshot("store-b", Instant.parse("2026-10-07T08:00:10Z"));
        long packId = packs.resolve(s);

        Optional<Long> first = store.save(packId, s, node -> packs.moduleId(packId, node));
        Optional<Long> again = store.save(packId, s, node -> packs.moduleId(packId, node));

        assertThat(first).isPresent();
        assertThat(again).isEmpty();
        assertThat(count("cell_reading", packId)).isEqualTo(16);
    }

    @Test
    void deletingAPackRemovesItsWholeHistory() throws IOException {
        PackSnapshot s = snapshot("store-d", Instant.parse("2026-10-07T08:00:30Z"));
        long packId = packs.resolve(s);
        store.save(packId, s, node -> packs.moduleId(packId, node));

        jdbc.update("delete from battery_pack where id = ?", packId);

        assertThat(jdbc.queryForObject("select count(*) from bms_module where pack_id = ?", Long.class, packId)).isZero();
        assertThat(count("telemetry_sample", packId)).isZero();
        assertThat(jdbc.queryForObject("""
                select count(*) from cell_reading c left join telemetry_sample s on s.id = c.sample_id
                where s.id is null""", Long.class)).as("no orphan readings").isZero();
    }

    @Test
    void rollsBackTheSampleWhenACellReadingIsRejected() throws IOException {
        PackSnapshot good = snapshot("store-c", Instant.parse("2026-10-07T08:00:20Z"));
        long packId = packs.resolve(good);
        ModuleSnapshot m = good.primaryModule();
        List<Integer> cells = new ArrayList<>(m.cellsMv());
        cells.set(15, 6000);   // violates CHECK (voltage_mv BETWEEN 0 AND 5000), after the sample row was inserted
        ModuleSnapshot broken = new ModuleSnapshot(m.nodeId(), m.source(), m.state(), m.firstCellNo(), cells,
                m.cellMinMv(), m.cellMaxMv(), m.cellAvgMv(), m.cellMinNo(), m.cellMaxNo(), m.tempsDc(), m.dieTempC(),
                m.balancing(), m.faultBits(), m.faults(), m.warningBits(), m.warnings(), m.uptimeS(), m.firmware());
        PackSnapshot bad = new PackSnapshot(good.deviceId(), good.recordedAt(), good.receivedAt(), good.timeFromGateway(),
                good.seq(), good.replay(), good.pack(), List.of(broken), good.controller(), good.gateway());

        assertThatThrownBy(() -> store.save(packId, bad, node -> packs.moduleId(packId, node)))
                .isInstanceOf(DataIntegrityViolationException.class);

        assertThat(count("telemetry_sample", packId)).as("no half-written sample").isZero();
    }
}
