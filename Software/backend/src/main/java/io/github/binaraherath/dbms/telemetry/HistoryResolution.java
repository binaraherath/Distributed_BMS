package io.github.binaraherath.dbms.telemetry;

import java.time.Duration;

/**
 * Time bucket used for history charts. A browser chart needs a few hundred points,
 * not tens of thousands, so longer ranges are averaged into wider buckets.
 */
public enum HistoryResolution {

    /** The stored samples themselves (one every 10 s). */
    RAW(Duration.ofSeconds(10)),
    ONE_MINUTE(Duration.ofMinutes(1)),
    FIFTEEN_MINUTES(Duration.ofMinutes(15));

    public static final Duration MAX_RANGE = Duration.ofDays(31);

    private final Duration bucket;

    HistoryResolution(Duration bucket) {
        this.bucket = bucket;
    }

    public Duration bucket() {
        return bucket;
    }

    /** PostgreSQL interval literal for date_bin(). */
    public String sqlInterval() {
        return bucket.toSeconds() + " seconds";
    }

    /** At most ~720 points for any range up to the 31-day limit. */
    public static HistoryResolution forRange(Duration range) {
        if (range.compareTo(Duration.ofHours(2)) <= 0) {
            return RAW;
        }
        if (range.compareTo(Duration.ofDays(2)) <= 0) {
            return ONE_MINUTE;
        }
        return FIFTEEN_MINUTES;
    }
}
