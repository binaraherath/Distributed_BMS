package io.github.binaraherath.dbms.telemetry;

import java.time.Duration;
import java.time.Instant;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Decides which 1 Hz snapshots are written to the database: the first one in each
 * time bucket (default 10 s). Buckets are based on the gateway's own timestamp, so
 * snapshots replayed after a Wi-Fi outage still fill their old buckets correctly,
 * even though they arrive mixed with live data.
 */
public class StorageSampler {

    private static final int BUCKETS_REMEMBERED = 4096;   // ~11 h at 10 s

    private final long bucketMs;
    private final Map<Long, Set<Long>> storedBuckets = new ConcurrentHashMap<>();

    public StorageSampler(Duration interval) {
        if (interval.isZero() || interval.isNegative()) {
            throw new IllegalArgumentException("Sample interval must be positive");
        }
        this.bucketMs = interval.toMillis();
    }

    /** True exactly once per pack and time bucket. */
    public boolean shouldStore(long packId, Instant recordedAt) {
        long bucket = Math.floorDiv(recordedAt.toEpochMilli(), bucketMs);
        Set<Long> seen = storedBuckets.computeIfAbsent(packId, id -> new LinkedHashSet<>());
        synchronized (seen) {
            if (!seen.add(bucket)) {
                return false;
            }
            if (seen.size() > BUCKETS_REMEMBERED) {
                Iterator<Long> oldest = seen.iterator();
                oldest.next();
                oldest.remove();
            }
            return true;
        }
    }
}
