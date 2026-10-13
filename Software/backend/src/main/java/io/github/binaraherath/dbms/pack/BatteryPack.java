package io.github.binaraherath.dbms.pack;

import java.math.BigDecimal;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.OneToMany;
import jakarta.persistence.OrderBy;
import jakarta.persistence.Table;

/** A battery pack, identified by the device id of its IoT gateway. */
@Entity
@Table(name = "battery_pack")
public class BatteryPack {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "device_id", nullable = false, unique = true, length = 64)
    private String deviceId;

    @Column(nullable = false, length = 100)
    private String name;

    @Column(length = 120)
    private String location;

    @Column(nullable = false, length = 40)
    private String chemistry;

    @Column(name = "series_cells", nullable = false)
    private short seriesCells;

    @Column(name = "rated_capacity_ah", precision = 6, scale = 1)
    private BigDecimal ratedCapacityAh;

    @Column(name = "gateway_online", nullable = false)
    private boolean gatewayOnline;

    @Column(name = "gateway_status_at")
    private Instant gatewayStatusAt;

    @Column(name = "last_seen_at")
    private Instant lastSeenAt;

    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    @OneToMany(mappedBy = "pack")
    @OrderBy("nodeId")
    private List<BmsModule> modules = new ArrayList<>();

    protected BatteryPack() {
    }

    public BatteryPack(String deviceId, String name, int seriesCells, Instant createdAt) {
        this.deviceId = deviceId;
        this.name = name;
        this.chemistry = "Li-ion (LMO/NMC)";
        this.seriesCells = (short) seriesCells;
        this.createdAt = createdAt;
    }

    public Long getId() { return id; }
    public String getDeviceId() { return deviceId; }
    public String getName() { return name; }
    public void setName(String name) { this.name = name; }
    public String getLocation() { return location; }
    public void setLocation(String location) { this.location = location; }
    public String getChemistry() { return chemistry; }
    public int getSeriesCells() { return seriesCells; }
    public void setSeriesCells(int seriesCells) { this.seriesCells = (short) seriesCells; }
    public BigDecimal getRatedCapacityAh() { return ratedCapacityAh; }
    public void setRatedCapacityAh(BigDecimal ratedCapacityAh) { this.ratedCapacityAh = ratedCapacityAh; }
    public boolean isGatewayOnline() { return gatewayOnline; }
    public Instant getGatewayStatusAt() { return gatewayStatusAt; }
    public Instant getLastSeenAt() { return lastSeenAt; }
    public Instant getCreatedAt() { return createdAt; }
    public List<BmsModule> getModules() { return modules; }

    public void setGatewayStatus(boolean online, Instant at) {
        this.gatewayOnline = online;
        this.gatewayStatusAt = at;
    }
}
