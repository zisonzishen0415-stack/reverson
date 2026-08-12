#include "rev_fdn.h"
#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    float mem[8192];
    uint32_t lens[REV_FDN_LINES] = {64,128,128,256,256,512,512,1024};
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
    CHECK(maxv < 10.0f);

    /* stereo decorrelation: even lines -> L, odd -> R; lengths differ, so L != R over time */
    rev_fdn_clear(&f);
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
    for (int i = 0; i < 6000; ++i) rev_fdn_process(&f, 0.0f, &l, &r); /* flush impulse echoes */
    float late2 = 0.0f;
    for (int i = 0; i < 14000; ++i) { rev_fdn_process(&f, 0.0f, &l, &r); late2 += l * l + r * r; }
    CHECK(late2 < 1e-3f);

    if (fails == 0) { printf("test_fdn PASS\n"); return 0; }
    printf("test_fdn FAILED (%d)\n", fails);
    return 1;
}