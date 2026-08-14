/* test_trigger.c - v2 trigger path: 8-sample smoothing grid (determinism +
 * convergence), trigger sensitivity, predelay timing, hold behavior.
 * Uses the Reverson_test_env hook. */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

static Reverson* new_core(void** memout) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    *memout = mem;
    return Reverson_init(mem, need, 44100.0f);
}

/* a single 1.0 sample cannot fire the onset detector (onset_env needs to
   clear the 0.02 absolute floor), so onsets are played as a short burst */
static void play_onset(Reverson* r, float* l, float* rr, int n) {
    for (int i = 0; i < n; ++i) Reverson_process(r, 1.0f, l, rr);
}

int main(void) {
    /* --- 8-sample grid: deterministic + convergent -------------------- */
    void* mem1; void* mem2;
    Reverson* a = new_core(&mem1);
    Reverson* b = new_core(&mem2);
    CHECK(a != NULL && b != NULL);
    Reverson_set_param(a, REVERSON_PARAM_GATE, 0.9f);
    Reverson_set_param(b, REVERSON_PARAM_GATE, 0.9f);
    Reverson_set_param(a, REVERSON_PARAM_REVLEN, 0.3f);
    Reverson_set_param(b, REVERSON_PARAM_REVLEN, 0.3f);
    Reverson_set_param(a, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(b, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(a, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(b, REVERSON_PARAM_MIX, 1.0f);
    float l1, r1, l2, r2, dsum = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        float x = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(a, x, &l1, &r1);
        Reverson_process(b, x, &l2, &r2);
        dsum += rev_absf(l1 - l2) + rev_absf(r1 - r2);
        CHECK(is_finite_f(l1) && is_finite_f(r1));
    }
    CHECK(dsum == 0.0f);   /* two identical instances stay bit-identical */
    /* params converge to target within 1 s even on the 8-sample grid */
    Reverson_set_param(a, REVERSON_PARAM_GATE, 0.2f);
    Reverson_set_param(a, REVERSON_PARAM_GATE, 0.9f);
    for (int i = 0; i < 44100; ++i) Reverson_process(a, 0.0f, &l1, &r1);
    CHECK(Reverson_get_param(a, REVERSON_PARAM_GATE) > 0.899f);
    free(mem1); free(mem2);

    /* --- overshoot: shape=1 blooms above 1 then settles to exactly 1; ---- */
    /* --- shape=0 never exceeds 1 ----------------------------------------- */
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.2f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.5f);
        Reverson_set_param(r, REVERSON_PARAM_SHAPE, 1.0f);
        float l, rr;
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        play_onset(r, &l, &rr, 50);
        float over_peak = 0.0f;
        int saw_over = 0;
        for (int i = 0; i < 22050; ++i) {     /* rise (19404) + settle (1764) window */
            Reverson_process(r, 0.0f, &l, &rr);
            float e = Reverson_test_env(r);
            if (e > over_peak) over_peak = e;
            if (e > 1.0f + 0.05f) saw_over = 1;
        }
        CHECK(saw_over == 1);                 /* the overshoot happened */
        /* settle lands back on 1 during the hold phase */
        Reverson_process(r, 0.0f, &l, &rr);
        for (int i = 0; i < 4410; ++i) Reverson_process(r, 0.0f, &l, &rr);
        float hold_env = Reverson_test_env(r);
        CHECK(hold_env > 0.99f && hold_env < 1.01f);
        free(mem);
    }
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.2f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.5f);
        Reverson_set_param(r, REVERSON_PARAM_SHAPE, 0.0f);
        float l, rr;
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        play_onset(r, &l, &rr, 50);
        float lin_peak = 0.0f;
        for (int i = 0; i < 22050; ++i) {
            Reverson_process(r, 0.0f, &l, &rr);
            float e = Reverson_test_env(r);
            if (e > lin_peak) lin_peak = e;
        }
        CHECK(lin_peak <= 1.0001f);           /* linear attack: no overshoot */
        free(mem);
    }

    /* --- hold: density lengthens the time above the floor's midpoint ----- */
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.2f);
        Reverson_set_param(r, REVERSON_PARAM_SHAPE, 0.0f);
        float l, rr;
        float above_mid[2];
        for (int d = 0; d < 2; ++d) {
            Reverson_set_param(r, REVERSON_PARAM_DENSITY, d == 0 ? 0.0f : 1.0f);
            for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);  /* settle to floor */
            play_onset(r, &l, &rr, 50);
            float floor = Reverson_test_env(r);
            float mid = floor + (1.0f - floor) * 0.5f;
            int above = 0;
            for (int i = 0; i < 88200; ++i) {     /* 2 s window */
                Reverson_process(r, 0.0f, &l, &rr);
                if (Reverson_test_env(r) > mid) above++;
            }
            above_mid[d] = (float)above;
        }
        CHECK(above_mid[1] > above_mid[0] * 1.5f);  /* long hold keeps it up */
        free(mem);
    }

    if (fails == 0) { printf("test_trigger PASS\n"); return 0; }
    printf("test_trigger FAILED (%d)\n", fails);
    return 1;
}
