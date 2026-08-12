#include "rev_rev.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
#define CLOSE(a,b,tol) CHECK(((a) > (b) - (tol)) && ((a) < (b) + (tol)))
/* NaN and infinities fail this check; normal floats pass. Takes a value, not a
   stateful expression, so it is evaluated exactly once. */
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

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
    /* the swell falls to 0 across the crossfade window: the last sample of the
       segment is silent so the wrap seam is click-free */
    CLOSE(out[9], 0.0f, 0.05f);
    /* the segment wrapped: second segment starts like the first */
    CLOSE(out[10], out[0], 0.2f);
    /* squared swell is smaller early on */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 10u, 1u, 2);
    float first_sq = rev_rev_process(&r); /* 9 * 0.1^2 = 0.09 */
    CLOSE(first_sq, 0.09f, 0.05f);

    /* oversized segment clamps to the 64-sample buffer: 128 samples stay finite */
    rev_rev_clear(&r);
    for (int i = 0; i < 64; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 200u, 1u, 1);
    for (int i = 0; i < 128; ++i) CHECK(is_finite_f(rev_rev_process(&r)));

    /* cross_len clamps to seg_len=2 and shape clamps to 4: finite, no crash */
    rev_rev_clear(&r);
    for (int i = 0; i < 4; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 2u, 5u, 9);
    for (int i = 0; i < 8; ++i) CHECK(is_finite_f(rev_rev_process(&r)));

    /* peak-0 trigger clamps norm to 3.0: with a 0.5 recorded signal the output
       stays within 3.0 * 0.5 = 1.5 and finite */
    rev_rev_clear(&r);
    rev_rev_trigger(&r, 10u, 1u, 1);            /* seg_peak == 0 -> norm_target 3.0 */
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, 0.5f);
    for (int i = 0; i < 2; ++i) {
        float v = rev_rev_process(&r);
        CHECK(is_finite_f(v));
        CHECK(v > -1.5f && v < 1.5f);
    }

    /* interleaved write/process (recording while playing back): stays finite */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 10u, 1u, 1);
    for (int i = 0; i < 10; ++i) {
        CHECK(is_finite_f(rev_rev_process(&r)));
        rev_rev_write(&r, (float)i);
    }

    /* live interleaved: writes continue while reading; the anchored read head must
       still replay the recorded ramp reversed (a frozen head would fail this) */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);   /* record 0..9 */
    rev_rev_trigger(&r, 10u, 1u, 1);                            /* anchor = write_idx (10) */
    float live[24];
    for (int i = 0; i < 24; ++i) {
        rev_rev_write(&r, (float)(100 + i));                    /* writes keep advancing write_idx */
        live[i] = rev_rev_process(&r);
    }
    CLOSE(live[0], 0.9f, 0.15f);   /* buf[anchor-1] = 9, env 0.1 */
    CLOSE(live[9], 0.0f, 0.05f);   /* segment end: env falls to 0 at the seam */
    CLOSE(live[10], live[0], 0.2f);/* wrap: segment loops, starts again */

    if (fails == 0) { printf("test_rev PASS\n"); return 0; }
    printf("test_rev FAILED (%d)\n", fails);
    return 1;
}
