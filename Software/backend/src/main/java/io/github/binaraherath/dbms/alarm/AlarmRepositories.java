package io.github.binaraherath.dbms.alarm;

import java.time.Instant;
import java.util.List;

import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import io.github.binaraherath.dbms.user.AppUser;

interface FaultCodeRepository extends JpaRepository<FaultCode, Short> {
}

interface AlarmRepository extends JpaRepository<Alarm, Long> {

    /** Uses the partial index ux_alarm_open. */
    @Query("select a from Alarm a join fetch a.faultCode where a.packId = :packId and a.clearedAt is null")
    List<Alarm> findOpenByPack(@Param("packId") long packId);

    @Query(value = """
            select a from Alarm a join fetch a.faultCode f left join fetch a.acknowledgedBy
            where a.packId = :packId
              and (:status = 'ALL'
                   or (:status = 'ACTIVE' and a.clearedAt is null)
                   or (:status = 'CLEARED' and a.clearedAt is not null))
              and (:severity = 'ALL' or f.severity = :severity)""",
            countQuery = """
            select count(a) from Alarm a join a.faultCode f
            where a.packId = :packId
              and (:status = 'ALL'
                   or (:status = 'ACTIVE' and a.clearedAt is null)
                   or (:status = 'CLEARED' and a.clearedAt is not null))
              and (:severity = 'ALL' or f.severity = :severity)""")
    Page<Alarm> search(@Param("packId") long packId, @Param("status") String status,
                       @Param("severity") String severity, Pageable pageable);

    /** Conditional update: only the first of two admins acknowledging at the same time wins. */
    /** Conditional update: two admins acknowledging at once cannot both succeed. */
    @Modifying(flushAutomatically = true, clearAutomatically = true)
    @Query("update Alarm a set a.acknowledgedAt = :at, a.acknowledgedBy = :user "
            + "where a.id = :id and a.acknowledgedAt is null")
    int acknowledge(@Param("id") long id, @Param("at") Instant at, @Param("user") AppUser user);
}
