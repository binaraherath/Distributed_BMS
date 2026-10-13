package io.github.binaraherath.dbms.alarm;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;

/** Lookup row for one fault bit of the firmware (seeded by Flyway V2). Read-only. */
@Entity
@Table(name = "fault_code")
public class FaultCode {

    @Id
    private Short id;

    @Column(nullable = false, length = 4)
    private String source;

    @Column(nullable = false)
    private short bit;

    @Column(nullable = false, length = 40)
    private String code;

    @Column(nullable = false, length = 8)
    private String severity;

    @Column(nullable = false, length = 200)
    private String description;

    protected FaultCode() {
    }

    public Short getId() { return id; }
    public String getSource() { return source; }
    public int getBit() { return bit; }
    public String getCode() { return code; }
    public String getSeverity() { return severity; }
    public String getDescription() { return description; }
}
