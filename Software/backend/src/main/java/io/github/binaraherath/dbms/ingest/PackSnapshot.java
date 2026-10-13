package io.github.binaraherath.dbms.ingest;

import java.time.Instant;
import java.util.List;

/**
 * One moment in the life of a pack, independent of the gateway's JSON format.
 * Built by {@link GatewayJsonV1Adapter}; consumed by the live stream, storage and alarms.
 *
 * @param recordedAt      when the gateway took the snapshot (its NTP clock), or the
 *                        server receive time if the gateway had no time sync
 * @param timeFromGateway false when {@code recordedAt} had to fall back to server time
 * @param replay          true when the gateway is re-sending data it buffered while offline
 */
public record PackSnapshot(
        String deviceId,
        Instant recordedAt,
        Instant receivedAt,
        boolean timeFromGateway,
        long seq,
        boolean replay,
        PackMetrics pack,
        List<ModuleSnapshot> modules,
        ControllerSnapshot controller,
        GatewaySnapshot gateway) {

    /** Data provenance of a node's values. */
    public enum DataSource { LIVE, SIMULATED, OFFLINE }

    public record PackMetrics(int voltageMv, int currentMa, double socPct, double sohPct,
                              double remainingAh, double capacityAh, double dischargedAh,
                              Limits limits) {

        public double powerW() {
            return voltageMv / 1000.0 * currentMa / 1000.0;
        }
    }

    public record Limits(double chargeA, double dischargeA, boolean chargeAllowed,
                         boolean dischargeAllowed, boolean full, boolean empty) {
    }

    /** One master monitoring node (BQ76952 + STM32) and its cells. */
    public record ModuleSnapshot(
            int nodeId,
            DataSource source,
            String state,
            int firstCellNo,
            List<Integer> cellsMv,
            int cellMinMv, int cellMaxMv, int cellAvgMv,
            int cellMinNo, int cellMaxNo,
            List<Integer> tempsDc,
            int dieTempC,
            Balancing balancing,
            int faultBits, List<String> faults,
            int warningBits, List<String> warnings,
            long uptimeS,
            String firmware) {

        public int cellDeltaMv() {
            return cellMaxMv - cellMinMv;
        }

        /** Highest NTC reading in 0.1 degC. */
        public int tempMaxDc() {
            return tempsDc.stream().mapToInt(Integer::intValue).max().orElse(0);
        }
    }

    public record Balancing(boolean enabled, boolean active, int mask, List<Integer> cells, int minutes) {
    }

    public record ControllerSnapshot(
            DataSource source,
            String state,
            Contactors contactors,
            int fanDutyPct, int fanRpm,
            boolean keyOn, boolean estopOk,
            int faultBits, List<String> faults,
            int warningBits, List<String> warnings,
            long uptimeS,
            String firmware) {
    }

    public record Contactors(boolean mainNeg, boolean precharge, boolean mainPos, boolean chargeEnable) {

        /** Same bit order as CTRL_OUT_* in bms_can_protocol.h. */
        public int bits() {
            return (mainNeg ? 1 : 0) | (precharge ? 2 : 0) | (mainPos ? 4 : 0) | (chargeEnable ? 8 : 0);
        }
    }

    public record GatewaySnapshot(String firmware, int rssiDbm, long uptimeS, long canRxFrames,
                                  long canE2eErrors, long canBusOff, int buffered) {
    }

    /** The first (and, with v1 firmware, only) master node. */
    public ModuleSnapshot primaryModule() {
        return modules.getFirst();
    }
}
