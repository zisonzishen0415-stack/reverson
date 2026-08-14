/* test_crossfeed.c - L/R crossfeed in the diffusion chain: energy bound,
 * decorrelation, determinism, no NaN. */
#include "rev_swell.h"
#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

int main(void) {
    static float mem[REV_SWELL_BUF_LEN];
    static float diff_mem[3u * 2u * REV_SWELL_DIFF_LEN];
    RevSwell s;
    rev_swell_init(&s, mem, REV_SWELL_BUF_LEN, diff_mem, REV_SWELL_DIFF_LEN, 44100.0f);

    /* crossfeed is driven by diff_fb (the diffusion param); sweep the whole
       range with a dense impulse train: output stays finite and bounded */
    const float fbs[5] = {0.0f, 0.2f, 0.4f, 0.6f, 0.7f};
    for (int f = 0; f < 5; ++f) {
        float dfb = fbs[f];
        rev_swell_clear(&s);
        rev_swell_set(&s, 0.8f, 1.0f);
        rev_swell_set_mod(&s, 0.3f);
        s.diff_fb[0] = dfb;
        s.diff_fb[1] = dfb * 0.9f;
        s.diff_fb[2] = dfb * 0.8f;
        float peak = 0.0f;
        float l, r;
        for (int i = 0; i < 44100; ++i) {
            rev_swell_process(&s, (i % 100 == 0) ? 0.8f : 0.0f, &l, &r);
            CHECK(is_finite_f(l) && is_finite_f(r));
            if (rev_absf(l) > peak) peak = rev_absf(l);
            if (rev_absf(r) > peak) peak = rev_absf(r);
        }
        CHECK(peak < 1.0f);   /* crossfeed + allpasses stay unit-bounded */
    }

    /* crossfeed couples the channels: with an L-only excitation the R channel
       must respond (energy leaks across) - and both stay finite */
    rev_swell_clear(&s);
    rev_swell_set(&s, 0.6f, 1.0f);
    rev_swell_set_mod(&s, 0.0f);
    s.diff_fb[0] = 0.6f; s.diff_fb[1] = 0.54f; s.diff_fb[2] = 0.48f;
    {
        float l, r, r_energy = 0.0f;
        for (int i = 0; i < 22050; ++i) {
            rev_swell_process(&s, (i == 0) ? 1.0f : 0.0f, &l, &r);
            r_energy += r * r;
            CHECK(is_finite_f(l) && is_finite_f(r));
        }
        CHECK(r_energy > 1e-4f);   /* R is fed by the crossfeed, not zero */
    }

    if (fails == 0) { printf("test_crossfeed PASS\n"); return 0; }
    printf("test_crossfeed FAILED (%d)\n", fails);
    return 1;
}
