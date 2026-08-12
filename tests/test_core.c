#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);

    CHECK(Reverson_get_param(r, REVERSON_PARAM_MIX) > 0.5f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_DECAY) == 0.6f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_GATE) == 0.0f);

    Reverson_set_param(r, REVERSON_PARAM_MIX, 2.0f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_MIX) == 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 0.55f);

    float l = 1.0f, rr = 1.0f;
    for (int i = 0; i < 1000; ++i) {
        Reverson_process(r, 0.0f, &l, &rr);
        CHECK(l == 0.0f && rr == 0.0f);
    }

    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.6f);
    float diff_sum = 0.0f;
    for (int i = 0; i < 8820; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr);
        diff_sum += rev_absf(l - rr);
    }
    CHECK(diff_sum > 0.01f);

    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.2f);
    for (int i = 0; i < 5000; ++i) Reverson_process(r, 1.0f, &l, &rr); /* settle duck */
    float peak = 0.0f;
    for (int i = 0; i < 5000; ++i) {
        Reverson_process(r, 1.0f, &l, &rr);
        if (rev_absf(l) > peak) peak = rev_absf(l);
    }
    CHECK(peak < 0.05f);

    for (int i = 0; i < 44100; ++i) {
        float in = (float)((i * 7919) % 1000) / 500.0f - 1.0f;
        Reverson_process(r, in, &l, &rr);
        CHECK(l == l && rr == rr);
    }

    free(mem);
    if (fails == 0) { printf("test_core PASS\n"); return 0; }
    printf("test_core FAILED (%d)\n", fails);
    return 1;
}
