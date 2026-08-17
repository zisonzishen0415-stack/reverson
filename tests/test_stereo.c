/* test_stereo.c - stereo API: dry image preserved, mono wrapper equivalent. */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static void configure(Reverson* r) {
    Reverson_set_param(r, REVERSON_PARAM_MIX, 0.55f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.4f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_MOD, 0.4f);
}

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);

    /* mix=0: the dry stereo image passes through bit-exactly (after the
       8-sample param grid has converged mix to exactly 0). The output
       limiter's lookahead delays the path by REV_LOOK_LEN samples, so
       compare against the delayed input. */
    Reverson_set_param(r, REVERSON_PARAM_MIX, 0.0f);
    float ol, orr;
    for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &ol, &orr);
    {
        enum { DL = 64 };
        static float hl[DL], hr[DL];
        for (int i = 0; i < 1000; ++i) {
            float xl = (float)((i * 13) % 17) / 17.0f - 0.5f;
            float xr = (float)((i * 7) % 19) / 19.0f - 0.5f;
            Reverson_process_stereo(r, xl, xr, &ol, &orr);
            if (i >= DL) {
                CHECK(ol == hl[i % DL]);
                CHECK(orr == hr[i % DL]);
            } else {
                CHECK(ol == 0.0f && orr == 0.0f);   /* lookahead prefill */
            }
            hl[i % DL] = xl;
            hr[i % DL] = xr;
        }
    }

    /* L==R input: the mono wrapper is bit-identical to the stereo entry.
       Two twin instances (one per API) so the interleaved grid ticks of a
       single instance cannot differ between the calls. */
    {
        uint32_t need2 = Reverson_state_size(44100.0f);
        void* mem_a = malloc(need);
        void* mem2 = malloc(need2);
        CHECK(mem_a != NULL && mem2 != NULL);
        Reverson* a = Reverson_init(mem_a, need, 44100.0f);
        Reverson* b = Reverson_init(mem2, need2, 44100.0f);
        CHECK(a != NULL && b != NULL);
        configure(a);
        configure(b);
        float l1, r1, l2, r2, dsum = 0.0f;
        for (int i = 0; i < 44100; ++i) {       /* converge + settle, identical on both */
            float x = (i % 220 == 0) ? 0.7f : 0.0f;
            Reverson_process(a, x, &l1, &r1);
            Reverson_process_stereo(b, x, x, &l2, &r2);
            dsum += rev_absf(l1 - l2) + rev_absf(r1 - r2);
        }
        CHECK(dsum == 0.0f);
        free(mem_a);
        free(mem2);
    }

    /* stereo input with mix=1: the WET path is a mono engine (L==R wet), the
       dry is silent at mix=1 - outputs stay finite and bounded */
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &ol, &orr); /* converge */
    float peak = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        float xl = (i % 220 == 0) ? 0.8f : 0.0f;
        float xr = (i % 220 == 55) ? -0.6f : 0.0f;
        Reverson_process_stereo(r, xl, xr, &ol, &orr);
        if (ol == ol && orr == orr) {
            if (rev_absf(ol) > peak) peak = rev_absf(ol);
            if (rev_absf(orr) > peak) peak = rev_absf(orr);
        } else {
            CHECK(0);   /* NaN */
        }
    }
    CHECK(peak < 1.2f);

    free(mem);
    if (fails == 0) { printf("test_stereo PASS\n"); return 0; }
    printf("test_stereo FAILED (%d)\n", fails);
    return 1;
}
