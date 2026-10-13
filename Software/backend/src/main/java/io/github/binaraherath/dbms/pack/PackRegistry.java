package io.github.binaraherath.dbms.pack;

import java.time.Clock;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.transaction.support.TransactionTemplate;

import io.github.binaraherath.dbms.ingest.PackSnapshot;
import io.github.binaraherath.dbms.ingest.PackSnapshot.ModuleSnapshot;

/**
 * Maps a gateway device id to its pack and module ids. A pack is registered
 * automatically the first time its gateway sends telemetry. Results are cached
 * because telemetry arrives every second and the mapping almost never changes.
 */
@Component
public class PackRegistry {

    private static final Logger log = LoggerFactory.getLogger(PackRegistry.class);

    private final BatteryPackRepository packs;
    private final BmsModuleRepository modules;
    private final TransactionTemplate tx;
    private final Clock clock;

    private final Map<String, Long> packIdByDevice = new ConcurrentHashMap<>();
    /** packId -> (nodeId -> moduleId) */
    private final Map<Long, Map<Integer, Long>> moduleIds = new ConcurrentHashMap<>();

    PackRegistry(BatteryPackRepository packs, BmsModuleRepository modules,
                 TransactionTemplate tx, Clock clock) {
        this.packs = packs;
        this.modules = modules;
        this.tx = tx;
        this.clock = clock;
    }

    /** Pack id for this snapshot's gateway; creates the pack and any new modules if needed. */
    public long resolve(PackSnapshot s) {
        Long cached = packIdByDevice.get(s.deviceId());
        if (cached != null && hasAllModules(cached, s)) {
            return cached;
        }
        Long id = tx.execute(status -> register(s));
        return id;
    }

    public Optional<Long> findPackId(String deviceId) {
        Long cached = packIdByDevice.get(deviceId);
        if (cached != null) {
            return Optional.of(cached);
        }
        return packs.findByDeviceId(deviceId).map(BatteryPack::getId);
    }

    /** Module id of a node in a pack that has already been resolved. */
    public long moduleId(long packId, int nodeId) {
        Long id = moduleIds.getOrDefault(packId, Map.of()).get(nodeId);
        if (id == null) {
            throw new IllegalStateException("Module " + nodeId + " of pack " + packId + " is not registered");
        }
        return id;
    }

    private boolean hasAllModules(long packId, PackSnapshot s) {
        Map<Integer, Long> known = moduleIds.getOrDefault(packId, Map.of());
        return s.modules().stream().allMatch(m -> known.containsKey(m.nodeId()));
    }

    private long register(PackSnapshot s) {
        int cells = s.modules().stream().mapToInt(m -> m.cellsMv().size()).sum();
        BatteryPack pack = packs.findByDeviceId(s.deviceId()).orElseGet(() -> {
            log.info("New gateway {} - registering pack with {} cells", s.deviceId(), cells);
            return packs.save(new BatteryPack(s.deviceId(), "Pack " + s.deviceId(), cells, clock.instant()));
        });
        if (pack.getSeriesCells() < cells) {
            pack.setSeriesCells(cells);
        }

        Map<Integer, Long> nodeToModule = new ConcurrentHashMap<>();
        for (BmsModule m : modules.findByPackId(pack.getId())) {
            nodeToModule.put(m.getNodeId(), m.getId());
        }
        for (ModuleSnapshot m : s.modules()) {
            if (!nodeToModule.containsKey(m.nodeId())) {
                BmsModule saved = modules.save(new BmsModule(pack, m.nodeId(), m.firstCellNo(), m.cellsMv().size()));
                nodeToModule.put(m.nodeId(), saved.getId());
            }
        }
        moduleIds.put(pack.getId(), nodeToModule);
        packIdByDevice.put(s.deviceId(), pack.getId());
        return pack.getId();
    }
}
