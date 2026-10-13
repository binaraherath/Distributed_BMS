package io.github.binaraherath.dbms.telemetry;

import java.io.PrintWriter;
import java.sql.Array;
import java.sql.Timestamp;
import java.time.Duration;
import java.time.Instant;
import java.util.List;

import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import io.github.binaraherath.dbms.common.ApiExceptions;
import io.github.binaraherath.dbms.pack.PackService;

/** Read side of telemetry: bucketed history, per-cell history and CSV export. */
@Service
public class TelemetryQueryService {

    public record HistoryPoint(Instant t, double voltageV, double currentA, double socPct, double tempMaxC,
                               int cellDeltaMv, int cellMinMv, int cellMaxMv, boolean simulated) {
    }

    public record HistoryDto(long packId, Instant from, Instant to, HistoryResolution resolution,
                             long bucketSeconds, List<HistoryPoint> points) {
    }

    public record CellPoint(Instant t, double voltageMv) {
    }

    public record CellHistoryDto(long packId, int cellNo, HistoryResolution resolution, List<CellPoint> points) {
    }

    private static final String BUCKET = "date_bin(CAST(? AS interval), %s, TIMESTAMPTZ '2000-01-01 00:00:00+00')";

    private static final String HISTORY_SQL = """
            SELECT %s AS t,
                   AVG(pack_voltage_mv)            AS v,
                   AVG(current_ma)                 AS i,
                   AVG(soc_pct)                    AS soc,
                   MAX(temp_max_dc)                AS tmax,
                   MAX(cell_max_mv - cell_min_mv)  AS delta,
                   MIN(cell_min_mv)                AS cmin,
                   MAX(cell_max_mv)                AS cmax,
                   BOOL_OR(bms_source = 'SIMULATED') AS simulated
            FROM telemetry_sample
            WHERE pack_id = ? AND recorded_at >= ? AND recorded_at < ?
            GROUP BY t
            ORDER BY t""".formatted(BUCKET.formatted("recorded_at"));

    private static final String CELL_SQL = """
            SELECT %s AS t, AVG(c.voltage_mv) AS mv
            FROM telemetry_sample s
            JOIN cell_reading c ON c.sample_id = s.id
            WHERE s.pack_id = ? AND s.recorded_at >= ? AND s.recorded_at < ? AND c.cell_no = ?
            GROUP BY t
            ORDER BY t""".formatted(BUCKET.formatted("s.recorded_at"));

    private static final String EXPORT_SQL = """
            SELECT s.recorded_at, s.pack_voltage_mv, s.current_ma, s.soc_pct, s.temp_max_dc,
                   s.bms_state, s.ctrl_state, s.bms_source,
                   array_agg(c.voltage_mv ORDER BY c.cell_no) AS cells
            FROM telemetry_sample s
            LEFT JOIN cell_reading c ON c.sample_id = s.id
            WHERE s.pack_id = ? AND s.recorded_at >= ? AND s.recorded_at < ?
            GROUP BY s.id
            ORDER BY s.recorded_at""";

    private final JdbcTemplate jdbc;
    private final PackService packs;

    TelemetryQueryService(JdbcTemplate jdbc, PackService packs) {
        this.jdbc = jdbc;
        this.packs = packs;
    }

    @Transactional(readOnly = true)
    public HistoryDto history(long packId, Instant from, Instant to) {
        packs.find(packId);
        HistoryResolution res = resolutionFor(from, to);
        List<HistoryPoint> points = jdbc.query(HISTORY_SQL, (rs, n) -> new HistoryPoint(
                rs.getTimestamp("t").toInstant(),
                rs.getDouble("v") / 1000.0,
                rs.getDouble("i") / 1000.0,
                rs.getDouble("soc"),
                rs.getInt("tmax") / 10.0,
                rs.getInt("delta"),
                rs.getInt("cmin"),
                rs.getInt("cmax"),
                rs.getBoolean("simulated")),
                res.sqlInterval(), packId, Timestamp.from(from), Timestamp.from(to));
        return new HistoryDto(packId, from, to, res, res.bucket().toSeconds(), points);
    }

    @Transactional(readOnly = true)
    public CellHistoryDto cellHistory(long packId, int cellNo, Instant from, Instant to) {
        int series = packs.find(packId).getSeriesCells();
        if (cellNo < 1 || cellNo > series) {
            throw new ApiExceptions.BadRequest("cellNo must be between 1 and " + series);
        }
        HistoryResolution res = resolutionFor(from, to);
        List<CellPoint> points = jdbc.query(CELL_SQL, (rs, n) -> new CellPoint(
                rs.getTimestamp("t").toInstant(), rs.getDouble("mv")),
                res.sqlInterval(), packId, Timestamp.from(from), Timestamp.from(to), cellNo);
        return new CellHistoryDto(packId, cellNo, res, points);
    }

    /** Streams raw stored samples (with every cell voltage) as CSV. */
    @Transactional(readOnly = true)
    public void exportCsv(long packId, Instant from, Instant to, PrintWriter out) {
        int series = packs.find(packId).getSeriesCells();
        resolutionFor(from, to);
        StringBuilder header = new StringBuilder(
                "recorded_at_utc,pack_voltage_v,current_a,soc_pct,temp_max_c,bms_state,controller_state,data_source");
        for (int c = 1; c <= series; c++) {
            header.append(",cell_").append(c).append("_mv");
        }
        out.println(header);
        jdbc.query(EXPORT_SQL, rs -> {
            StringBuilder line = new StringBuilder()
                    .append(rs.getTimestamp("recorded_at").toInstant()).append(',')
                    .append(rs.getInt("pack_voltage_mv") / 1000.0).append(',')
                    .append(rs.getInt("current_ma") / 1000.0).append(',')
                    .append(rs.getBigDecimal("soc_pct")).append(',')
                    .append(rs.getInt("temp_max_dc") / 10.0).append(',')
                    .append(rs.getString("bms_state")).append(',')
                    .append(rs.getString("ctrl_state") == null ? "" : rs.getString("ctrl_state")).append(',')
                    .append(rs.getString("bms_source"));
            Array cells = rs.getArray("cells");
            Object[] mv = cells == null ? new Object[0] : (Object[]) cells.getArray();
            for (int c = 0; c < series; c++) {
                line.append(',').append(c < mv.length && mv[c] != null ? mv[c] : "");
            }
            out.println(line);
        }, packId, Timestamp.from(from), Timestamp.from(to));
    }

    static HistoryResolution resolutionFor(Instant from, Instant to) {
        if (!from.isBefore(to)) {
            throw new ApiExceptions.BadRequest("'from' must be before 'to'");
        }
        Duration range = Duration.between(from, to);
        if (range.compareTo(HistoryResolution.MAX_RANGE) > 0) {
            throw new ApiExceptions.BadRequest("Range must not exceed 31 days");
        }
        return HistoryResolution.forRange(range);
    }
}
