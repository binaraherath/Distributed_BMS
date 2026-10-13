package io.github.binaraherath.dbms.alarm;

import java.time.Instant;

import io.github.binaraherath.dbms.user.AppUser;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;

/** One occurrence of a fault: raised, later cleared, and acknowledged by an admin. */
@Entity
@Table(name = "alarm")
public class Alarm {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "pack_id", nullable = false)
    private Long packId;

    /** The master node that raised it; null for controller faults. */
    @Column(name = "module_id")
    private Long moduleId;

    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "fault_code_id")
    private FaultCode faultCode;

    @Column(name = "raised_at", nullable = false)
    private Instant raisedAt;

    @Column(name = "cleared_at")
    private Instant clearedAt;

    @Column(name = "acknowledged_at")
    private Instant acknowledgedAt;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "acknowledged_by")
    private AppUser acknowledgedBy;

    protected Alarm() {
    }

    public Alarm(long packId, Long moduleId, FaultCode faultCode, Instant raisedAt) {
        this.packId = packId;
        this.moduleId = moduleId;
        this.faultCode = faultCode;
        this.raisedAt = raisedAt;
    }

    public void clear(Instant at) {
        this.clearedAt = at.isBefore(raisedAt) ? raisedAt : at;
    }

    public Long getId() { return id; }
    public Long getPackId() { return packId; }
    public Long getModuleId() { return moduleId; }
    public FaultCode getFaultCode() { return faultCode; }
    public Instant getRaisedAt() { return raisedAt; }
    public Instant getClearedAt() { return clearedAt; }
    public Instant getAcknowledgedAt() { return acknowledgedAt; }
    public AppUser getAcknowledgedBy() { return acknowledgedBy; }
}
