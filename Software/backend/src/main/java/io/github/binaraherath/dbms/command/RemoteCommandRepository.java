package io.github.binaraherath.dbms.command;

import java.time.Instant;
import java.util.Optional;
import java.util.UUID;

import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.EntityGraph;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

interface RemoteCommandRepository extends JpaRepository<RemoteCommand, Long> {

    @EntityGraph(attributePaths = "issuedBy")
    Page<RemoteCommand> findByPackId(Long packId, Pageable pageable);

    @EntityGraph(attributePaths = "issuedBy")
    Optional<RemoteCommand> findWithUserById(Long id);

    Optional<RemoteCommand> findByCorrelationId(UUID correlationId);

    /** Fallback match for firmware that does not echo the command id in its ack. */
    Optional<RemoteCommand> findFirstByPackIdAndCommandTypeAndStatusAndIssuedAtAfterOrderByIssuedAtDesc(
            Long packId, CommandType type, CommandStatus status, Instant after);

    /** Uses the partial index ix_command_waiting. */
    @Modifying
    @Query("""
            update RemoteCommand c
               set c.status = io.github.binaraherath.dbms.command.CommandStatus.TIMED_OUT,
                   c.completedAt = :now,
                   c.resultMessage = 'No answer from the gateway'
             where c.status in (io.github.binaraherath.dbms.command.CommandStatus.PENDING,
                                io.github.binaraherath.dbms.command.CommandStatus.PUBLISHED)
               and c.issuedAt < :cutoff""")
    int timeOutWaiting(@Param("cutoff") Instant cutoff, @Param("now") Instant now);
}
