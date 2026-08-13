/* test_nofdn.c - core compiled with REVERSON_ENABLE_FDN=0 (the ZDL shape):
 * the FDN bed memory is not reserved and set_bed is inert, so the pure
 * reverse path is the only path and the output is identical with/without
 * trying to enable the bed. */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    /* the full build reserves ~61440 more floats (~240 KB) for the FDN bed;
       the trimmed build must stay well under that. */
    CHECK(need < 200000u);

    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);

    enum { N = 44100 };
    float* out[2];
    out[0] = (float*)malloc(N * sizeof(float));
    out[1] = (float*)malloc(N * sizeof(float));

    /* two fresh instances, one with the bed "enabled": with FDN compiled out
       the bed stays 0, so the outputs must be bit-identical. */
    for (int variant = 0; variant < 2; ++variant) {
        void* mem2 = malloc(need);
        CHECK(mem2 != NULL);
        Reverson* rr = Reverson_init(mem2, need, 44100.0f);
        CHECK(rr != NULL);
        Reverson_set_6knob(rr, 0.65f, 0.5f, 0.6f, 0.5f, 0.6f, 0.4f);
        Reverson_set_param(rr, REVERSON_PARAM_REVLEN, 1.0f);
        if (variant == 1) Reverson_set_bed(rr, 1.0f);
        float l = 0.0f, rr2 = 0.0f;
        for (int i = 0; i < 5000; ++i) Reverson_process(rr, 0.0f, &l, &rr2);
        unsigned s = 999u;
        for (int i = 0; i < N; ++i) {
            s = s * 1664525u + 1013904223u;
            float in = ((s >> 8) & 0xffff) / 32768.0f - 1.0f;
            in = in * 0.25f + ((i % 220 == 0) ? 0.4f : 0.0f);
            Reverson_process(rr, in, &l, &rr2);
            out[variant][i] = l;
            CHECK(l == l && rr2 == rr2);
        }
        free(mem2);
    }
    float diff = 0.0f;
    for (int i = 0; i < N; ++i) diff += rev_absf(out[0][i] - out[1][i]);
    CHECK(diff == 0.0f);

    free(out[0]); free(out[1]); free(mem);
    if (fails == 0) { printf("test_nofdn PASS\n"); return 0; }
    printf("test_nofdn FAILED (%d)\n", fails);
    return 1;
}
