package io.github.binaraherath.dbms.ingest;

import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;

import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.ingest.PackSnapshot.Balancing;
import io.github.binaraherath.dbms.ingest.PackSnapshot.Contactors;
import io.github.binaraherath.dbms.ingest.PackSnapshot.ControllerSnapshot;
import io.github.binaraherath.dbms.ingest.PackSnapshot.DataSource;
import io.github.binaraherath.dbms.ingest.PackSnapshot.GatewaySnapshot;
import io.github.binaraherath.dbms.ingest.PackSnapshot.Limits;
import io.github.binaraherath.dbms.ingest.PackSnapshot.ModuleSnapshot;
import io.github.binaraherath.dbms.ingest.PackSnapshot.PackMetrics;
import tools.jackson.core.JacksonException;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;

/**
 * Adapter between the gateway firmware's telemetry JSON (protocol v1, see
 * Firmware/Node3_IoT_Gateway/src/bms_model.cpp) and the {@link PackSnapshot} domain model.
 * <p>
 * This is the only class that knows the firmware's field names and units. The v1
 * payload carries a single master node; it becomes a pack with one module, so when
 * multi-master firmware arrives only a new adapter is needed.
 */
@Component
public class GatewayJsonV1Adapter {

    private static final int MAX_CELLS_PER_MODULE = 16;

    private final ObjectMapper mapper;
    private final Clock clock;

    public GatewayJsonV1Adapter(ObjectMapper mapper, Clock clock) {
        this.mapper = mapper;
        this.clock = clock;
    }

    public PackSnapshot adapt(String deviceId, byte[] payload) {
        JsonNode root;
        try {
            root = mapper.readTree(payload);
        } catch (JacksonException ex) {
            throw new InvalidGatewayMessageException("Telemetry is not valid JSON", ex);
        }
        JsonNode bms = root.path("bms");
        JsonNode cellsNode = bms.path("cells_mv");
        if (!cellsNode.isArray() || cellsNode.isEmpty() || cellsNode.size() > MAX_CELLS_PER_MODULE) {
            throw new InvalidGatewayMessageException("Telemetry has no valid bms.cells_mv array");
        }

        Instant receivedAt = clock.instant();
        long ts = root.path("ts").asLong(0);
        boolean timeFromGateway = ts > 0;
        Instant recordedAt = timeFromGateway ? Instant.ofEpochMilli(ts) : receivedAt;

        return new PackSnapshot(
                deviceId,
                recordedAt,
                receivedAt,
                timeFromGateway,
                root.path("seq").asLong(0),
                root.path("replay").asBoolean(false),
                packMetrics(bms),
                List.of(module(bms, cellsNode)),
                controller(root.path("ctrl")),
                gateway(root.path("gateway")));
    }

    private static PackMetrics packMetrics(JsonNode bms) {
        JsonNode lim = bms.path("limits");
        return new PackMetrics(
                (int) Math.round(bms.path("pack_v").asDouble() * 1000),
                (int) Math.round(bms.path("current_a").asDouble() * 1000),
                clamp(bms.path("soc").asDouble(), 0, 100),
                clamp(bms.path("soh").asDouble(), 0, 100),
                bms.path("remaining_ah").asDouble(),
                bms.path("capacity_ah").asDouble(),
                bms.path("discharged_ah").asDouble(),
                new Limits(
                        lim.path("charge_a").asDouble(),
                        lim.path("discharge_a").asDouble(),
                        lim.path("charge_allowed").asBoolean(),
                        lim.path("discharge_allowed").asBoolean(),
                        lim.path("full").asBoolean(),
                        lim.path("empty").asBoolean()));
    }

    private static ModuleSnapshot module(JsonNode bms, JsonNode cellsNode) {
        List<Integer> cells = new ArrayList<>();
        cellsNode.forEach(c -> cells.add(c.asInt()));

        List<Integer> temps = new ArrayList<>();
        bms.path("temps_c").forEach(t -> temps.add((int) Math.round(t.asDouble() * 10)));

        JsonNode bal = bms.path("balancing");
        List<Integer> balCells = new ArrayList<>();
        bal.path("cells").forEach(c -> balCells.add(c.asInt()));

        return new ModuleSnapshot(
                1,
                source(bms),
                bms.path("state").asString("INIT"),
                1,
                List.copyOf(cells),
                bms.path("cell_min_mv").asInt(),
                bms.path("cell_max_mv").asInt(),
                bms.path("cell_avg_mv").asInt(),
                bms.path("cell_min_no").asInt(),
                bms.path("cell_max_no").asInt(),
                List.copyOf(temps),
                bms.path("bq_die_c").asInt(),
                new Balancing(
                        bal.path("enabled").asBoolean(),
                        bal.path("active").asBoolean(),
                        bal.path("mask").asInt(),
                        List.copyOf(balCells),
                        bal.path("minutes").asInt()),
                bms.path("fault_bits").asInt(),
                strings(bms.path("faults")),
                bms.path("warning_bits").asInt(),
                strings(bms.path("warnings")),
                bms.path("uptime_s").asLong(),
                bms.path("fw").asString(""));
    }

    private static ControllerSnapshot controller(JsonNode ctrl) {
        JsonNode k = ctrl.path("contactors");
        JsonNode fan = ctrl.path("fan");
        return new ControllerSnapshot(
                source(ctrl),
                ctrl.path("state").asString("INIT"),
                new Contactors(
                        k.path("main_neg").asBoolean(),
                        k.path("precharge").asBoolean(),
                        k.path("main_pos").asBoolean(),
                        k.path("charge_en").asBoolean()),
                (int) clamp(fan.path("duty").asInt(), 0, 100),
                fan.path("rpm").asInt(),
                ctrl.path("key_on").asBoolean(),
                ctrl.path("estop_ok").asBoolean(),
                ctrl.path("fault_bits").asInt(),
                strings(ctrl.path("faults")),
                ctrl.path("warning_bits").asInt(),
                strings(ctrl.path("warnings")),
                ctrl.path("uptime_s").asLong(),
                ctrl.path("fw").asString(""));
    }

    private static GatewaySnapshot gateway(JsonNode gw) {
        return new GatewaySnapshot(
                gw.path("fw").asString(""),
                gw.path("rssi_dbm").asInt(),
                gw.path("uptime_s").asLong(),
                gw.path("can_rx").asLong(),
                gw.path("can_e2e_errors").asLong(),
                gw.path("can_bus_off").asLong(),
                gw.path("buffered").asInt());
    }

    /**
     * The exhibition firmware tags each node with "source": "live" | "simulated".
     * The official firmware has no tag, so "online" decides between LIVE and OFFLINE.
     */
    static DataSource source(JsonNode node) {
        String tag = node.path("source").asString("");
        if ("simulated".equalsIgnoreCase(tag)) {
            return DataSource.SIMULATED;
        }
        return node.path("online").asBoolean(false) ? DataSource.LIVE : DataSource.OFFLINE;
    }

    private static List<String> strings(JsonNode array) {
        List<String> out = new ArrayList<>();
        array.forEach(n -> out.add(n.asString()));
        return List.copyOf(out);
    }

    private static double clamp(double v, double lo, double hi) {
        return Math.max(lo, Math.min(hi, v));
    }
}
