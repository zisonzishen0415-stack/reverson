/* test_hpf.c - wet high-pass: low-band (40..100 Hz) wet energy is well below
 * high-band (250..800 Hz) wet energy. The 13-tap comb response varies wildly
 * per frequency, so the comparison averages four well-spaced tones per band
 * with mod=1 (the LFO sweeps the comb notches) and resets between tones (the
 * reverse layer's segment is then silence, so no cross-tone leakage). */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static float wet_rms(Reverson* r, float freq) {
    float l = 0.0f, rr = 0.0f, acc = 0.0f;
    for (int i = 0; i < 44100; ++i) {         /* 1 s steady state */
        float x = 0.2f * (float)sin(2.0 * 3.14159265358979323846 * freq * (double)i / 44100.0);
        Reverson_process(r, x, &l, &rr);
        acc += l * l + rr * rr;
    }
    return (float)sqrt(acc / 88200.0f);
}

static void configure(Reverson* r) {
    /* taps on (gate>0.12), reverse layer low, LP wide open (tone=1),
       neutral shelf, no sat, full mod (comb notches sweep), mono width */
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.3f);
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_TONE, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_BASS, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_SAT, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_MOD, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_WIDTH, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DIFFUSION, 0.1f);
}

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);
    configure(r);
    float l, rr;
    for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr); /* converge the param grid */

    const float lows[4]  = {40.0f, 60.0f, 80.0f, 100.0f};
    const float highs[4] = {250.0f, 350.0f, 500.0f, 800.0f};
    float low_sum = 0.0f, high_sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        Reverson_reset(r);                    /* empty reverse segment: no cross-tone leak */
        low_sum += wet_rms(r, lows[i]);
    }
    for (int i = 0; i < 4; ++i) {
        Reverson_reset(r);
        high_sum += wet_rms(r, highs[i]);
    }
    CHECK(high_sum > 1e-3f);                  /* high band passes */
    CHECK(low_sum < high_sum * 0.7f);         /* low band is cut by the HPF */
    free(mem);
    if (fails == 0) { printf("test_hpf PASS\n"); return 0; }
    printf("test_hpf FAILED (%d)\n", fails);
    return 1;
}
