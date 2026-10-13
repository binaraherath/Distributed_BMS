package io.github.binaraherath.dbms.pack;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;

/** One master monitoring node of a pack and the block of cells it watches. */
@Entity
@Table(name = "bms_module")
public class BmsModule {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "pack_id")
    private BatteryPack pack;

    @Column(name = "node_id", nullable = false)
    private short nodeId;

    @Column(name = "first_cell_no", nullable = false)
    private short firstCellNo;

    @Column(name = "cell_count", nullable = false)
    private short cellCount;

    protected BmsModule() {
    }

    public BmsModule(BatteryPack pack, int nodeId, int firstCellNo, int cellCount) {
        this.pack = pack;
        this.nodeId = (short) nodeId;
        this.firstCellNo = (short) firstCellNo;
        this.cellCount = (short) cellCount;
    }

    public Long getId() { return id; }
    public BatteryPack getPack() { return pack; }
    public int getNodeId() { return nodeId; }
    public int getFirstCellNo() { return firstCellNo; }
    public int getCellCount() { return cellCount; }
}
