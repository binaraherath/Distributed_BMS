package io.github.binaraherath.dbms.ingest;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;

import org.junit.jupiter.api.Test;

import io.github.binaraherath.dbms.ingest.PackSnapshot.DataSource;
import io.github.binaraherath.dbms.ingest.PackSnapshot.ModuleSnapshot;
import tools.jackson.databind.json.JsonMapper;

class GatewayJsonV1AdapterTest {

    private static final Instant NOW = Instant.parse("2026-10-07T10:00:00Z");
    private final GatewayJsonV1Adapter adapter =
            new GatewayJsonV1Adapter(JsonMapper.builder().build(), Clock.fixed(NOW, ZoneOffset.UTC));

    private static byte[] fixture() throws IOException {
        try (InputStream in = GatewayJsonV1AdapterTest.class.getResourceAsStream("/fixtures/telemetry-v1.json")) {
            return in.readAllBytes();
        }
    }

    @Test
    void mapsTheFirmwarePayloadToSiUnitsInIntegers() throws IOException {
        PackSnapshot s = adapter.adapt("dbms-01", fixture());

        assertThat(s.recordedAt()).isEqualTo(Instant.ofEpochMilli(1791316800000L));
        assertThat(s.timeFromGateway()).isTrue();
        assertThat(s.seq()).isEqualTo(1287);
        assertThat(s.pack().voltageMv()).isEqualTo(59_380);
        assertThat(s.pack().currentMa()).isZero();
        assertThat(s.pack().socPct()).isEqualTo(52.4);

        ModuleSnapshot m = s.primaryModule();
        assertThat(m.nodeId()).isEqualTo(1);
        assertThat(m.firstCellNo()).isEqualTo(1);
        assertThat(m.cellsMv()).hasSize(16).startsWith(3712, 3710);
        assertThat(m.cellDeltaMv()).isEqualTo(6);
        assertThat(m.tempsDc()).containsExactly(298, 301, 296, 300);   // 0.1 °C
        assertThat(m.tempMaxDc()).isEqualTo(301);
        assertThat(m.balancing().active()).isFalse();
        assertThat(s.gateway().firmware()).isEqualTo("1.0.0-exh");
    }

    @Test
    void keepsTheProvenanceOfEachNode() throws IOException {
        PackSnapshot s = adapter.adapt("dbms-01", fixture());

        assertThat(s.primaryModule().source()).isEqualTo(DataSource.LIVE);
        assertThat(s.controller().source()).isEqualTo(DataSource.SIMULATED);
    }

    @Test
    void officialFirmwareWithoutTagIsLiveWhenOnlineElseOffline() {
        String json = """
                {"ts":0,"bms":{"online":true,"cells_mv":[3700,3701]},"ctrl":{"online":false}}""";

        PackSnapshot s = adapter.adapt("dbms-01", json.getBytes(StandardCharsets.UTF_8));

        assertThat(s.primaryModule().source()).isEqualTo(DataSource.LIVE);
        assertThat(s.controller().source()).isEqualTo(DataSource.OFFLINE);
    }

    @Test
    void usesTheServerClockWhenTheGatewayHasNoTime() {
        String json = """
                {"ts":0,"bms":{"online":true,"cells_mv":[3700]}}""";

        PackSnapshot s = adapter.adapt("dbms-01", json.getBytes(StandardCharsets.UTF_8));

        assertThat(s.recordedAt()).isEqualTo(NOW);
        assertThat(s.timeFromGateway()).isFalse();
    }

    @Test
    void rejectsMessagesWithoutCells() {
        assertThatThrownBy(() -> adapter.adapt("dbms-01", "{\"bms\":{}}".getBytes(StandardCharsets.UTF_8)))
                .isInstanceOf(InvalidGatewayMessageException.class);
        assertThatThrownBy(() -> adapter.adapt("dbms-01", "not json".getBytes(StandardCharsets.UTF_8)))
                .isInstanceOf(InvalidGatewayMessageException.class);
    }
}
