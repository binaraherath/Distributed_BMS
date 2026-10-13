package io.github.binaraherath.dbms.telemetry;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Duration;
import java.time.Instant;

import org.junit.jupiter.api.Test;

import io.github.binaraherath.dbms.common.ApiExceptions;

class StorageAndResolutionTest {

    private static final Instant T0 = Instant.parse("2026-10-07T10:00:00Z");

    @Test
    void storesTheFirstSnapshotOfEachTenSecondBucket() {
        StorageSampler sampler = new StorageSampler(Duration.ofSeconds(10));

        assertThat(sampler.shouldStore(1, T0)).isTrue();
        assertThat(sampler.shouldStore(1, T0.plusSeconds(9))).isFalse();
        assertThat(sampler.shouldStore(1, T0.plusSeconds(10))).isTrue();
        assertThat(sampler.shouldStore(2, T0)).as("buckets are per pack").isTrue();
    }

    @Test
    void replayedSnapshotsFillTheirOwnOldBuckets() {
        StorageSampler sampler = new StorageSampler(Duration.ofSeconds(10));
        sampler.shouldStore(1, T0.plusSeconds(600));               // live data after a Wi-Fi outage

        assertThat(sampler.shouldStore(1, T0.plusSeconds(120))).isTrue();   // replay from the gateway buffer
        assertThat(sampler.shouldStore(1, T0.plusSeconds(121))).isFalse();
    }

    @Test
    void picksACoarserResolutionForLongerRanges() {
        assertThat(HistoryResolution.forRange(Duration.ofHours(1))).isEqualTo(HistoryResolution.RAW);
        assertThat(HistoryResolution.forRange(Duration.ofHours(2))).isEqualTo(HistoryResolution.RAW);
        assertThat(HistoryResolution.forRange(Duration.ofHours(24))).isEqualTo(HistoryResolution.ONE_MINUTE);
        assertThat(HistoryResolution.forRange(Duration.ofDays(7))).isEqualTo(HistoryResolution.FIFTEEN_MINUTES);
    }

    @Test
    void rejectsBackwardsOrTooLongRanges() {
        assertThatThrownBy(() -> TelemetryQueryService.resolutionFor(T0, T0.minusSeconds(1)))
                .isInstanceOf(ApiExceptions.BadRequest.class);
        assertThatThrownBy(() -> TelemetryQueryService.resolutionFor(T0, T0.plus(Duration.ofDays(32))))
                .isInstanceOf(ApiExceptions.BadRequest.class);
        assertThat(TelemetryQueryService.resolutionFor(T0, T0.plus(Duration.ofDays(31))))
                .isEqualTo(HistoryResolution.FIFTEEN_MINUTES);
    }
}
