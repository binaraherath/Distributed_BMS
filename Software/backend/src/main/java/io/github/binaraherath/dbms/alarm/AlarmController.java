package io.github.binaraherath.dbms.alarm;

import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.data.web.PageableDefault;
import org.springframework.security.access.prepost.PreAuthorize;
import org.springframework.security.core.Authentication;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import io.github.binaraherath.dbms.alarm.AlarmService.AlarmDto;
import io.github.binaraherath.dbms.alarm.AlarmService.SeverityFilter;
import io.github.binaraherath.dbms.alarm.AlarmService.StatusFilter;
import io.github.binaraherath.dbms.common.ApiExceptions;
import io.github.binaraherath.dbms.common.PageResponse;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import jakarta.validation.constraints.NotNull;

@RestController
@Tag(name = "Alarms", description = "Faults raised and cleared by the BMS and the controller")
public class AlarmController {

    public record AlarmUpdateRequest(@NotNull Boolean acknowledged) {
    }

    private final AlarmService alarms;

    AlarmController(AlarmService alarms) {
        this.alarms = alarms;
    }

    @GetMapping("/api/v1/packs/{packId}/alarms")
    @Operation(summary = "Alarms of a pack, newest first (paginated, filter by status and severity)")
    public PageResponse<AlarmDto> list(@PathVariable long packId,
                                       @RequestParam(defaultValue = "ALL") StatusFilter status,
                                       @RequestParam(defaultValue = "ALL") SeverityFilter severity,
                                       @PageableDefault(size = 20, sort = "raisedAt", direction = Sort.Direction.DESC)
                                       Pageable pageable) {
        return alarms.search(packId, status, severity, pageable);
    }

    @PatchMapping("/api/v1/alarms/{alarmId}")
    @PreAuthorize("hasRole('ADMIN')")
    @Operation(summary = "Acknowledge an alarm (409 if already acknowledged)")
    public AlarmDto update(@PathVariable long alarmId, @Valid @RequestBody AlarmUpdateRequest request,
                           Authentication auth) {
        if (!request.acknowledged()) {
            throw new ApiExceptions.BadRequest("An acknowledgement cannot be undone");
        }
        return alarms.acknowledge(alarmId, auth.getName());
    }
}
