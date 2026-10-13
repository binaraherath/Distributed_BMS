package io.github.binaraherath.dbms.alarm;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Set;

import org.junit.jupiter.api.Test;

class AlarmEvaluatorDiffTest {

    @Test
    void raisesNewBitsAndClearsBitsThatWentAway() {
        int bits = (1 << 4) | (1 << 9);           // OVER_TEMP, IMBALANCE now set

        AlarmEvaluator.BitDiff d = AlarmEvaluator.diff(bits, Set.of(2, 9));   // CELL_OV, IMBALANCE open

        assertThat(d.raise()).containsExactly(4);
        assertThat(d.clear()).containsExactly(2);
    }

    @Test
    void nothingChangesWhenBitsMatchOpenAlarms() {
        AlarmEvaluator.BitDiff d = AlarmEvaluator.diff(1 << 3, Set.of(3));

        assertThat(d.raise()).isEmpty();
        assertThat(d.clear()).isEmpty();
    }

    @Test
    void zeroBitsClearsEverything() {
        AlarmEvaluator.BitDiff d = AlarmEvaluator.diff(0, Set.of(0, 13));

        assertThat(d.raise()).isEmpty();
        assertThat(d.clear()).containsExactlyInAnyOrder(0, 13);
    }

    @Test
    void handlesTheHighestBmsBit() {
        AlarmEvaluator.BitDiff d = AlarmEvaluator.diff(1 << 23, Set.of());

        assertThat(d.raise()).containsExactly(23);
    }
}
