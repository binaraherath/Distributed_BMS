package io.github.binaraherath.dbms.command;

import java.time.Instant;
import java.util.UUID;

import io.github.binaraherath.dbms.user.AppUser;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;

/** A command sent from the dashboard, kept as an audit trail with its outcome. */
@Entity
@Table(name = "remote_command")
public class RemoteCommand {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "pack_id", nullable = false)
    private Long packId;

    @Enumerated(EnumType.STRING)
    @Column(name = "command_type", nullable = false, length = 20)
    private CommandType commandType;

    private Short argument;

    @Enumerated(EnumType.STRING)
    @Column(nullable = false, length = 12)
    private CommandStatus status;

    /** Sent to the gateway as "id" and echoed back in its ack. */
    @Column(name = "correlation_id", nullable = false, unique = true)
    private UUID correlationId;

    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "issued_by")
    private AppUser issuedBy;

    @Column(name = "issued_at", nullable = false)
    private Instant issuedAt;

    @Column(name = "completed_at")
    private Instant completedAt;

    @Column(name = "result_message", length = 200)
    private String resultMessage;

    protected RemoteCommand() {
    }

    public RemoteCommand(long packId, CommandType type, Integer argument, AppUser issuedBy, Instant issuedAt) {
        this.packId = packId;
        this.commandType = type;
        this.argument = argument == null ? null : argument.shortValue();
        this.status = CommandStatus.PENDING;
        this.correlationId = UUID.randomUUID();
        this.issuedBy = issuedBy;
        this.issuedAt = issuedAt;
    }

    public void markPublished() {
        if (status == CommandStatus.PENDING) {
            status = CommandStatus.PUBLISHED;
        }
    }

    public void complete(CommandStatus result, Instant at, String message) {
        if (status.isFinal()) {
            return;
        }
        this.status = result;
        this.completedAt = at;
        this.resultMessage = message == null || message.isBlank() ? null
                : message.substring(0, Math.min(200, message.length()));
    }

    public Long getId() { return id; }
    public Long getPackId() { return packId; }
    public CommandType getCommandType() { return commandType; }
    public Integer getArgument() { return argument == null ? null : argument.intValue(); }
    public CommandStatus getStatus() { return status; }
    public UUID getCorrelationId() { return correlationId; }
    public AppUser getIssuedBy() { return issuedBy; }
    public Instant getIssuedAt() { return issuedAt; }
    public Instant getCompletedAt() { return completedAt; }
    public String getResultMessage() { return resultMessage; }
}
