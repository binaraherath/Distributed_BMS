package io.github.binaraherath.dbms.alarm;

import java.time.Clock;
import java.time.Instant;

import org.springframework.data.domain.Pageable;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import io.github.binaraherath.dbms.common.ApiExceptions;
import io.github.binaraherath.dbms.common.PageResponse;
import io.github.binaraherath.dbms.pack.PackService;
import io.github.binaraherath.dbms.user.AppUser;
import io.github.binaraherath.dbms.user.UserService;

@Service
public class AlarmService {

    public enum StatusFilter { ACTIVE, CLEARED, ALL }

    public enum SeverityFilter { CRITICAL, WARNING, ALL }

    public record AlarmDto(long id, String source, String code, String description, String severity,
                           Instant raisedAt, Instant clearedAt, boolean active,
                           Instant acknowledgedAt, String acknowledgedBy) {

        static AlarmDto of(Alarm a) {
            FaultCode f = a.getFaultCode();
            AppUser by = a.getAcknowledgedBy();
            return new AlarmDto(a.getId(), f.getSource(), f.getCode(), f.getDescription(), f.getSeverity(),
                    a.getRaisedAt(), a.getClearedAt(), a.getClearedAt() == null,
                    a.getAcknowledgedAt(), by == null ? null : by.getUsername());
        }
    }

    private final AlarmRepository alarms;
    private final PackService packs;
    private final UserService users;
    private final Clock clock;

    AlarmService(AlarmRepository alarms, PackService packs, UserService users, Clock clock) {
        this.alarms = alarms;
        this.packs = packs;
        this.users = users;
        this.clock = clock;
    }

    @Transactional(readOnly = true)
    public PageResponse<AlarmDto> search(long packId, StatusFilter status, SeverityFilter severity,
                                         Pageable pageable) {
        packs.find(packId);
        return PageResponse.of(alarms.search(packId, status.name(), severity.name(), pageable), AlarmDto::of);
    }

    /** 404 if the alarm does not exist, 409 if somebody already acknowledged it. */
    @Transactional
    public AlarmDto acknowledge(long alarmId, String username) {
        Alarm alarm = alarms.findById(alarmId)
                .orElseThrow(() -> new ApiExceptions.NotFound("Alarm " + alarmId + " not found"));
        int updated = alarms.acknowledge(alarmId, clock.instant(), users.findByUsername(username));
        if (updated == 0) {
            throw new ApiExceptions.Conflict("Alarm " + alarmId + " is already acknowledged");
        }
        return alarms.findById(alarm.getId()).map(AlarmDto::of).orElseThrow();   // fresh: context was cleared
    }
}
