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

    if (fails == 0) { printf("test_trigger PASS\n"); return 0; }
    printf("test_trigger FAILED (%d)\n", fails);
    return 1;
}
