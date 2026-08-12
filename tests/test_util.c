#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    CHECK(rev_clampf(0.5f, 0.0f, 1.0f) == 0.5f);
    CHECK(rev_clampf(-0.2f, 0.0f, 1.0f) == 0.0f);
    CHECK(rev_clampf(1.5f, 0.0f, 1.0f) == 1.0f);
    CHECK(rev_minf(2.0f, 3.0f) == 2.0f);
    CHECK(rev_maxf(2.0f, 3.0f) == 3.0f);
    CHECK(rev_absf(-4.0f) == 4.0f);
    CHECK(rev_coeff_from_tc(0.0f) == 1.0f);
    CHECK(rev_coeff_from_tc(9.0f) > 0.09f && rev_coeff_from_tc(9.0f) < 0.11f);
    if (fails == 0) { printf("test_util PASS\n"); return 0; }
    printf("test_util FAILED (%d)\n", fails);
    return 1;
}
