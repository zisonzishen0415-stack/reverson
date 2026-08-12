#include "rev_rev.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
#define CLOSE(a,b,tol) CHECK(((a) > (b) - (tol)) && ((a) < (b) + (tol)))

int main(void) {
    float mem[64];
    RevRev r;
    rev_rev_init(&r, mem, 64u, 44100.0f);
    rev_rev_clear(&r);

    /* record a rising ramp 0..9 */
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);

    /* trigger a 10-sample reverse segment, linear swell, minimal crossfade */
    rev_rev_trigger(&r, 10u, 1u, 1);

    float out[24];
    for (int i = 0; i < 24; ++i) out[i] = rev_rev_process(&r);

    /* first sample is the newest (9) at swell env 0.1, norm gain ~1.0 (peak ~9 -> target 0.1, still smoothing) */
    CLOSE(out[0], 0.9f, 0.15f);
    /* last sample of the segment crossfades to the head (9) at env 1.0 */
    CLOSE(out[9], 9.0f, 0.5f);
    /* the segment wrapped: second segment starts like the first */
    CLOSE(out[10], out[0], 0.2f);
    /* squared swell is smaller early on */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 10u, 1u, 2);
    float first_sq = rev_rev_process(&r); /* 9 * 0.1^2 = 0.09 */
    CLOSE(first_sq, 0.09f, 0.05f);

    if (fails == 0) { printf("test_rev PASS\n"); return 0; }
    printf("test_rev FAILED (%d)\n", fails);
    return 1;
}