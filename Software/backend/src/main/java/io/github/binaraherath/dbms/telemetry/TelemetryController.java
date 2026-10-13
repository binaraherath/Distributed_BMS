package io.github.binaraherath.dbms.telemetry;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;

import org.springframework.format.annotation.DateTimeFormat;
import org.springframework.format.annotation.DateTimeFormat.ISO;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.servlet.mvc.method.annotation.SseEmitter;

import io.github.binaraherath.dbms.pack.PackService;
import io.github.binaraherath.dbms.telemetry.LiveTelemetryService.LiveTelemetryDto;
import io.github.binaraherath.dbms.telemetry.TelemetryQueryService.CellHistoryDto;
import io.github.binaraherath.dbms.telemetry.TelemetryQueryService.HistoryDto;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.servlet.http.HttpServletResponse;

@RestController
@RequestMapping("/api/v1/packs/{packId}")
@Tag(name = "Telemetry", description = "Live data, history and export")
public class TelemetryController {

    private static final Duration DEFAULT_RANGE = Duration.ofHours(1);
    private static final DateTimeFormatter FILE_TIME =
            DateTimeFormatter.ofPattern("yyyyMMdd-HHmm").withZone(ZoneOffset.UTC);

    private final LiveTelemetryService live;
    private final TelemetryQueryService query;
    private final PackService packs;
    private final Clock clock;

    TelemetryController(LiveTelemetryService live, TelemetryQueryService query, PackService packs, Clock clock) {
        this.live = live;
        this.query = query;
        this.packs = packs;
        this.clock = clock;
    }

    @GetMapping("/telemetry/latest")
    @Operation(summary = "Newest snapshot (204 if the gateway has not reported since the server started)")
    public ResponseEntity<LiveTelemetryDto> latest(@PathVariable long packId) {
        packs.find(packId);
        return live.latest(packId).map(ResponseEntity::ok).orElse(ResponseEntity.noContent().build());
    }

    @GetMapping(path = "/stream", produces = MediaType.TEXT_EVENT_STREAM_VALUE)
    @Operation(summary = "Live updates as Server-Sent Events ('telemetry' and 'heartbeat' events)")
    public SseEmitter stream(@PathVariable long packId) {
        packs.find(packId);
        return live.subscribe(packId);
    }

    @GetMapping("/telemetry")
    @Operation(summary = "Pack history; resolution is chosen from the range (max 31 days, default last hour)")
    public HistoryDto history(@PathVariable long packId,
                              @RequestParam(required = false) @DateTimeFormat(iso = ISO.DATE_TIME) Instant from,
                              @RequestParam(required = false) @DateTimeFormat(iso = ISO.DATE_TIME) Instant to) {
        Instant end = to != null ? to : clock.instant();
        Instant start = from != null ? from : end.minus(DEFAULT_RANGE);
        return query.history(packId, start, end);
    }

    @GetMapping("/cells/{cellNo}/history")
    @Operation(summary = "Voltage history of one cell")
    public CellHistoryDto cellHistory(@PathVariable long packId, @PathVariable int cellNo,
                                      @RequestParam(required = false) @DateTimeFormat(iso = ISO.DATE_TIME) Instant from,
                                      @RequestParam(required = false) @DateTimeFormat(iso = ISO.DATE_TIME) Instant to) {
        Instant end = to != null ? to : clock.instant();
        Instant start = from != null ? from : end.minus(DEFAULT_RANGE);
        return query.cellHistory(packId, cellNo, start, end);
    }

    @GetMapping(path = "/telemetry/export", produces = "text/csv")
    @Operation(summary = "Download stored samples with every cell voltage as CSV")
    public void export(@PathVariable long packId,
                       @RequestParam(required = false) @DateTimeFormat(iso = ISO.DATE_TIME) Instant from,
                       @RequestParam(required = false) @DateTimeFormat(iso = ISO.DATE_TIME) Instant to,
                       HttpServletResponse response) throws IOException {
        Instant end = to != null ? to : clock.instant();
        Instant start = from != null ? from : end.minus(Duration.ofHours(24));
        TelemetryQueryService.resolutionFor(start, end);   // validate before any byte is written
        String file = "pack-%d_%s_%s.csv".formatted(packId, FILE_TIME.format(start), FILE_TIME.format(end));
        response.setContentType("text/csv");
        response.setCharacterEncoding(StandardCharsets.UTF_8.name());
        response.setHeader(HttpHeaders.CONTENT_DISPOSITION, "attachment; filename=\"" + file + "\"");
        query.exportCsv(packId, start, end, response.getWriter());
    }
}
