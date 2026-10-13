package io.github.binaraherath.dbms.pack;

import java.math.BigDecimal;
import java.time.Instant;
import java.util.List;

import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import io.github.binaraherath.dbms.common.ApiExceptions;
import io.github.binaraherath.dbms.ingest.GatewayEvents.GatewayStatusChanged;
import io.github.binaraherath.dbms.telemetry.LiveTelemetryService;
import jakarta.validation.constraints.DecimalMax;
import jakarta.validation.constraints.DecimalMin;
import jakarta.validation.constraints.Size;

@Service
public class PackService {

    private final BatteryPackRepository packs;
    private final LiveTelemetryService live;

    PackService(BatteryPackRepository packs, LiveTelemetryService live) {
        this.packs = packs;
        this.live = live;
    }

    public record ModuleDto(int nodeId, int firstCellNo, int cellCount) {
    }

    public record PackDto(long id, String deviceId, String name, String location, String chemistry,
                          int seriesCells, BigDecimal ratedCapacityAh, boolean gatewayOnline,
                          Instant lastSeenAt, Instant createdAt, List<ModuleDto> modules) {
    }

    /** PATCH body: a null field means "leave unchanged". */
    public record PackUpdateRequest(
            @Size(min = 1, max = 100) String name,
            @Size(max = 120) String location,
            @DecimalMin("0.1") @DecimalMax("9999.9") BigDecimal ratedCapacityAh) {
    }

    @Transactional(readOnly = true)
    public List<PackDto> list() {
        return packs.findAllWithModules().stream().map(this::toDto).toList();
    }

    @Transactional(readOnly = true)
    public PackDto get(long id) {
        return toDto(find(id));
    }

    @Transactional(readOnly = true)
    public BatteryPack find(long id) {
        return packs.findById(id).orElseThrow(() -> new ApiExceptions.NotFound("Pack " + id + " not found"));
    }

    @Transactional
    public PackDto update(long id, PackUpdateRequest req) {
        BatteryPack pack = find(id);
        if (req.name() != null) {
            pack.setName(req.name().trim());
        }
        if (req.location() != null) {
            pack.setLocation(req.location().isBlank() ? null : req.location().trim());
        }
        if (req.ratedCapacityAh() != null) {
            pack.setRatedCapacityAh(req.ratedCapacityAh());
        }
        return toDto(pack);
    }

    @EventListener
    @Transactional
    public void onGatewayStatus(GatewayStatusChanged e) {
        packs.findById(e.packId()).ifPresent(p -> p.setGatewayStatus(e.online(), e.at()));
    }

    private PackDto toDto(BatteryPack p) {
        return new PackDto(p.getId(), p.getDeviceId(), p.getName(), p.getLocation(), p.getChemistry(),
                p.getSeriesCells(), p.getRatedCapacityAh(), live.isGatewayOnline(p.getId()),
                p.getLastSeenAt(), p.getCreatedAt(),
                p.getModules().stream()
                        .map(m -> new ModuleDto(m.getNodeId(), m.getFirstCellNo(), m.getCellCount()))
                        .toList());
    }
}
