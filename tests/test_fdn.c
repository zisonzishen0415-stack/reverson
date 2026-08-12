#include "rev_fdn.h"
#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
/* NaN and infinities fail this check; normal floats pass. Takes a value, not a
   stateful expression, so it is evaluated exactly once. */
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

int main(void) {
    float mem[20000];
    uint32_t lens[REV_FDN_LINES] = {64,128,256,512,1024,2048,4096,8192};
    RevFdn f;
    rev_fdn_init(&f, mem, lens, 44100.0f);
    rev_fdn_set(&f, 0.5f, 0.5f, 0.0f);

    /* impulse -> tail must decay and stay bounded */
    float l, r;
    rev_fdn_process(&f, 1.0f, &l, &r);
    float early = 0.0f, late = 0.0f, maxv = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        rev_fdn_process(&f, 0.0f, &l, &r);
        float e = l * l + r * r;
        if (i < 500) early += e;
        if (i >= 20000) late += e;
        if (rev_absf(l) > maxv) maxv = rev_absf(l);
        if (rev_absf(r) > maxv) maxv = rev_absf(r);
    }
    CHECK(early > late);
    CHECK(maxv < 1.0f);

    /* feedback lengthens the tail: decay=1 vs decay=0.2, late-window energy.
       With the feedback loop disconnected both runs only see the direct
       echoes, so this check requires working feedback to pass. */
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 1.0f, 0.5f, 0.0f);
    rev_fdn_process(&f, 1.0f, &l, &r);
    float e_dec1 = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        rev_fdn_process(&f, 0.0f, &l, &r);
        if (i >= 2205 && i < 17640) e_dec1 += l * l + r * r; /* 50 ms..400 ms */
    }
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 0.2f, 0.5f, 0.0f);
    rev_fdn_process(&f, 1.0f, &l, &r);
    float e_dec02 = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        rev_fdn_process(&f, 0.0f, &l, &r);
        if (i >= 2205 && i < 17640) e_dec02 += l * l + r * r;
    }
    CHECK(e_dec1 > e_dec02);

    /* max settings (decay=1, tone=1, mod=1) stay stable: one impulse then
       silence must stay finite and bounded */
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 1.0f, 1.0f, 1.0f);
    rev_fdn_process(&f, 1.0f, &l, &r);
    float maxv2 = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        rev_fdn_process(&f, 0.0f, &l, &r);
        CHECK(is_finite_f(l));
        CHECK(is_finite_f(r));
        if (rev_absf(l) > maxv2) maxv2 = rev_absf(l);
        if (rev_absf(r) > maxv2) maxv2 = rev_absf(r);
    }
    CHECK(maxv2 < 10.0f);

    /* mod>0 dithers read positions: same constant input must produce output
       that differs from a mod=0 run, exercising the LFO path */
    float out0_l[4410], out0_r[4410];
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 0.5f, 0.5f, 0.0f);
    for (int i = 0; i < 4410; ++i) {
        rev_fdn_process(&f, 0.3f, &l, &r);
        out0_l[i] = l;
        out0_r[i] = r;
    }
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 0.5f, 0.5f, 1.0f);
    float mod_diff = 0.0f;
    for (int i = 0; i < 4410; ++i) {
        rev_fdn_process(&f, 0.3f, &l, &r);
        CHECK(is_finite_f(l));
        CHECK(is_finite_f(r));
        mod_diff += rev_absf(l - out0_l[i]) + rev_absf(r - out0_r[i]);
    }
    CHECK(mod_diff > 0.0f);

    /* stereo decorrelation: even lines -> L, odd -> R; lengths differ, so L != R over time */
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 0.5f, 0.5f, 0.0f);
    float diff_sum = 0.0f;
    for (int i = 0; i < 4410; ++i) {
        rev_fdn_process(&f, 0.3f, &l, &r);
        diff_sum += rev_absf(l - r);
    }
    CHECK(diff_sum > 1.0f);

    /* decay = 0 -> single pass only, tail dies quickly */
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 0.0f, 0.5f, 0.0f);
    rev_fdn_process(&f, 1.0f, &l, &r);
    for (int i = 0; i < 9000; ++i) rev_fdn_process(&f, 0.0f, &l, &r); /* flush longest echo (base 8176) */
    float late2 = 0.0f;
    for (int i = 0; i < 14000; ++i) { rev_fdn_process(&f, 0.0f, &l, &r); late2 += l * l + r * r; }
    CHECK(late2 < 1e-3f);

    if (fails == 0) { printf("test_fdn PASS\n"); return 0; }
    printf("test_fdn FAILED (%d)\n", fails);
    return 1;
}
