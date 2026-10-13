package io.github.binaraherath.dbms.telemetry;

import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Component;

import io.github.binaraherath.dbms.config.BmsProperties;
import io.github.binaraherath.dbms.ingest.GatewayEvents.TelemetryReceived;
import io.github.binaraherath.dbms.pack.PackRegistry;

/**
 * Observer: stores one snapshot per storage interval. Live data is 1 Hz, but history
 * at 10 s resolution is plenty for a battery and keeps the database small.
 */
@Component
class TelemetryPersistenceListener {

    private final StorageSampler sampler;
    private final TelemetryStore store;
    private final PackRegistry packs;

    TelemetryPersistenceListener(BmsProperties props, TelemetryStore store, PackRegistry packs) {
        this.sampler = new StorageSampler(props.storage().sampleInterval());
        this.store = store;
        this.packs = packs;
    }

    @EventListener
    public void onTelemetry(TelemetryReceived e) {
        if (sampler.shouldStore(e.packId(), e.snapshot().recordedAt())) {
            store.save(e.packId(), e.snapshot(), nodeId -> packs.moduleId(e.packId(), nodeId));
        }
    }
}
