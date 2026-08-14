/* test_swell.c - multi-head reverse swell engine */
#include "rev_swell.h"
#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

static float peak_window(const float* out_l, const float* out_r, int lo, int hi) {
    float p = 0.0f;
    for (int i = lo; i <= hi; ++i) {
        if (out_l[i] < 0.0f) { if (-out_l[i] > p) p = -out_l[i]; }
        else if (out_l[i] > p) p = out_l[i];
        if (out_r[i] < 0.0f) { if (-out_r[i] > p) p = -out_r[i]; }
        else if (out_r[i] > p) p = out_r[i];
    }
    return p;
}

int main(void) {
    float mem[REV_SWELL_BUF_LEN];
    float diff_mem[3u * 2u * REV_SWELL_DIFF_LEN];
    RevSwell s;
    rev_swell_init(&s, mem, REV_SWELL_BUF_LEN, diff_mem, REV_SWELL_DIFF_LEN, 44100.0f);
    rev_swell_set(&s, 0.3333333f, 1.0f);   /* scale 1.0 */

    /* impulse -> ZERO predelay: silence until the first tap (~8 ms = 353 smp) */
    float l, r;
    enum { N = 22000 };
    float out_l[N], out_r[N];
    rev_swell_clear(&s);
    rev_swell_process(&s, 1.0f, &l, &r);
    out_l[0] = l; out_r[0] = r;
    int first_nz = -1;
    for (int i = 1; i < N; ++i) {
        rev_swell_process(&s, 0.0f, &l, &r);
        out_l[i] = l; out_r[i] = r;
        if (first_nz < 0 && (l != 0.0f || r != 0.0f)) first_nz = i;
    }
    CHECK(first_nz >= 340 && first_nz <= 380);   /* 8 ms tap @44k1 */

    /* crescendo: the loud far taps exceed the quiet near taps */
    float early_peak = peak_window(out_l, out_r, 300, 700);
    float late_peak  = peak_window(out_l, out_r, 9800, 10050);
    CHECK(late_peak > early_peak * 3.0f);
    /* loudest tap is the last (exponential gain curve) */
    float near_last = peak_window(out_l, out_r, 7300, 7900);   /* tap 11 ~7479 */
    CHECK(late_peak > near_last * 1.1f);

    /* stereo panning: L and R responses differ */
    float ldiff = 0.0f;
    for (int i = 340; i < 10100; ++i) ldiff += rev_absf(out_l[i] - out_r[i]);
    CHECK(ldiff > 0.01f);

    /* feedback diffusion: after the last tap (~9878) the echoes decay over
       ~100 ms instead of stopping abruptly -> the swell reads as reverb */
    float tail = 0.0f;
    for (int i = 10100; i < 15000; ++i) tail += rev_absf(out_l[i]) + rev_absf(out_r[i]);
    CHECK(tail > 0.01f);
    /* and the tail decays: late half is quieter than the early half */
    float tailA = 0.0f, tailB = 0.0f;
    for (int i = 10100; i < 12500; ++i) tailA += rev_absf(out_l[i]) + rev_absf(out_r[i]);
    for (int i = 12500; i < 15000; ++i) tailB += rev_absf(out_l[i]) + rev_absf(out_r[i]);
    CHECK(tailA > tailB);

    /* amount=0 -> silence */
    rev_swell_clear(&s);
    rev_swell_set(&s, 0.5f, 0.0f);
    rev_swell_process(&s, 1.0f, &l, &r);
    CHECK(l == 0.0f && r == 0.0f);
    for (int i = 0; i < 1000; ++i) {
        rev_swell_process(&s, 0.0f, &l, &r);
        CHECK(l == 0.0f && r == 0.0f);
    }

    /* revlen=1 doubles the tap span (scale 2.0): first tap ~706, last ~19756 */
    rev_swell_clear(&s);
    rev_swell_set(&s, 1.0f, 1.0f);
    rev_swell_process(&s, 1.0f, &l, &r);
    out_l[0] = l; out_r[0] = r;
    int first_nz2 = -1;
    float far_peak = 0.0f;
    for (int i = 1; i < N; ++i) {
        rev_swell_process(&s, 0.0f, &l, &r);
        out_l[i] = l; out_r[i] = r;
        CHECK(is_finite_f(l));
        CHECK(is_finite_f(r));
        if (first_nz2 < 0 && (l != 0.0f || r != 0.0f)) first_nz2 = i;
        if (i >= 19600 && i < 20000) {
            if (rev_absf(l) > far_peak) far_peak = rev_absf(l);
            if (rev_absf(r) > far_peak) far_peak = rev_absf(r);
        }
    }
    CHECK(first_nz2 >= 690 && first_nz2 <= 730);
    CHECK(far_peak > 0.05f);   /* the doubled last tap is clearly present */

    /* continuous input stays bounded (allpass is unit magnitude, no growth) */
    rev_swell_clear(&s);
    rev_swell_set(&s, 1.0f, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        rev_swell_process(&s, 0.3f, &l, &r);
        CHECK(is_finite_f(l));
        CHECK(is_finite_f(r));
        if (rev_absf(l) > peak) peak = rev_absf(l);
        if (rev_absf(r) > peak) peak = rev_absf(r);
    }
    CHECK(peak < 1.0f);

    /* set clamps out-of-range args */
    rev_swell_set(&s, 2.0f, -1.0f);
    CHECK(s.scale == 2.0f && s.amount == 0.0f);

    /* Mod knob: the tap LFO must change the impulse response (living tail).
       mod=0 is static, mod=1 max movement; per-tap phase offsets decorrelate
       the taps so the response differs sample-by-sample, and the LFO start
       is deterministic (mod=0 twice is identical). */
    {
        enum { M = 22000 };
        float a[M], b[M], c[M];
        const float mods[3] = {0.0f, 1.0f, 0.0f};
        float* dst[3] = {a, b, c};
        for (int pass = 0; pass < 3; ++pass) {
            float* o = dst[pass];
            rev_swell_clear(&s);
            rev_swell_set(&s, 1.0f, 1.0f);
            rev_swell_set_mod(&s, mods[pass]);
            rev_swell_process(&s, 1.0f, &l, &r);
            o[0] = l + r;
            for (int i = 1; i < M; ++i) {
                rev_swell_process(&s, 0.0f, &l, &r);
                o[i] = l + r;
                CHECK(is_finite_f(l) && is_finite_f(r));
            }
        }
        float d01 = 0.0f, d02 = 0.0f;
        for (int i = 0; i < M; ++i) {
            d01 += rev_absf(a[i] - b[i]);   /* mod 0 vs mod 1 */
            d02 += rev_absf(a[i] - c[i]);   /* mod 0 twice: deterministic */
        }
        CHECK(d01 > 1e-3f);
        CHECK(d02 == 0.0f);
        float mp = 0.0f;
        for (int i = 0; i < M; ++i) {
            if (rev_absf(a[i]) > mp) mp = rev_absf(a[i]);
            if (rev_absf(b[i]) > mp) mp = rev_absf(b[i]);
        }
        CHECK(mp < 1.0f);   /* modulation stays bounded */
    }

    /* split API: taps + diffuse composition equals the single-call process
       on twin instances (bit-exact: deterministic engines, same inputs) */
    {
        float mem2[REV_SWELL_BUF_LEN];
        float diff_mem2[3u * 2u * REV_SWELL_DIFF_LEN];
        RevSwell a, b;
        rev_swell_init(&a, mem, REV_SWELL_BUF_LEN, diff_mem, REV_SWELL_DIFF_LEN, 44100.0f);
        rev_swell_init(&b, mem2, REV_SWELL_BUF_LEN, diff_mem2, REV_SWELL_DIFF_LEN, 44100.0f);
        rev_swell_set(&a, 0.7f, 0.8f);
        rev_swell_set(&b, 0.7f, 0.8f);
        rev_swell_set_mod(&a, 0.6f);
        rev_swell_set_mod(&b, 0.6f);
        float exact_diff = 0.0f;
        for (int i = 0; i < 5000; ++i) {
            float x = (i % 100 == 0) ? 0.5f : 0.0f;
            float al, ar, bl, br;
            rev_swell_taps(&a, x, &al, &ar);
            rev_swell_diffuse(&a, al, ar, &al, &ar);
            rev_swell_process(&b, x, &bl, &br);
            exact_diff += rev_absf(al - bl) + rev_absf(ar - br);
            CHECK(is_finite_f(al) && is_finite_f(ar));
        }
        CHECK(exact_diff == 0.0f);
    }

    /* mod=0 fast path: the LFO phase must not advance, so a mod=0 run after
       an intervening mod=1 run is bit-identical to a fresh mod=0 run */
    {
        enum { M2 = 8000 };
        float a[M2], b[M2];
        for (int pass = 0; pass < 2; ++pass) {
            float* o = (pass == 0) ? a : b;
            rev_swell_clear(&s);
            rev_swell_set(&s, 1.0f, 1.0f);
            rev_swell_set_mod(&s, 0.0f);
            rev_swell_process(&s, 1.0f, &l, &r);
            o[0] = l + r;
            for (int i = 1; i < M2; ++i) {
                rev_swell_process(&s, 0.0f, &l, &r);
                o[i] = l + r;
            }
            if (pass == 0) {   /* exercise mod=1 in between; phase must not leak */
                rev_swell_clear(&s);
                rev_swell_set_mod(&s, 1.0f);
                for (int i = 0; i < 100; ++i) rev_swell_process(&s, 0.0f, &l, &r);
            }
        }
        float md = 0.0f;
        for (int i = 0; i < M2; ++i) md += rev_absf(a[i] - b[i]);
        CHECK(md == 0.0f);
    }

    if (fails == 0) { printf("test_swell PASS\n"); return 0; }
    printf("test_swell FAILED (%d)\n", fails);
    return 1;
}
