#include "rev_env.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    RevEnv e;
    rev_env_init(&e, 44100.0f);

    /* silence -> envelope stays 0 */
    for (int i = 0; i < 100; ++i) rev_env_process(&e, 0.0f);
    CHECK(rev_env_value(&e) == 0.0f);
    CHECK(rev_env_onset(&e) == 0);

    /* a step to 1.0 must raise the envelope and fire exactly one onset */
    int onsets = 0;
    for (int i = 0; i < 4410; ++i) {
        rev_env_process(&e, 1.0f);
        onsets += rev_env_onset(&e);
    }
    CHECK(rev_env_value(&e) > 0.9f);
    CHECK(onsets == 1);

    /* input drops to 0 -> envelope releases (slowly) */
    rev_env_process(&e, 0.0f);
    CHECK(rev_env_value(&e) < 1.0f);

    /* onset re-arms after release: the fast onset envelope must decay below
       threshold between notes (16ths at 120 BPM are 125 ms apart) */
    rev_env_init(&e, 44100.0f);
    int rearm_onsets = 0;
    for (int i = 0; i < 500; ++i) {
        rev_env_process(&e, 1.0f);
        rearm_onsets += rev_env_onset(&e);
    }
    for (int i = 0; i < 4410; ++i) rev_env_process(&e, 0.0f);  /* ~100 ms gap */
    for (int i = 0; i < 500; ++i) {
        rev_env_process(&e, 1.0f);
        rearm_onsets += rev_env_onset(&e);
    }
    CHECK(rearm_onsets == 2);

    /* release to silence: after a full-scale step, sustained silence must
       bring the main envelope down to the noise floor */
    rev_env_init(&e, 44100.0f);
    for (int i = 0; i < 4410; ++i) rev_env_process(&e, 1.0f);
    for (int i = 0; i < 88200; ++i) rev_env_process(&e, 0.0f);  /* 2 s */
    CHECK(rev_env_value(&e) < 0.001f);

    /* absolute floor: sub-floor level never triggers an onset */
    rev_env_init(&e, 44100.0f);
    for (int i = 0; i < 1000; ++i) rev_env_process(&e, 0.005f);
    CHECK(rev_env_onset(&e) == 0);

    /* above the floor triggers exactly one onset */
    rev_env_init(&e, 44100.0f);
    int boundary_onsets = 0;
    for (int i = 0; i < 1000; ++i) {
        rev_env_process(&e, 0.05f);
        boundary_onsets += rev_env_onset(&e);
    }
    CHECK(boundary_onsets == 1);

    /* relative threshold: a note far below 35% of the recent peak does NOT
       re-trigger (level-independent trigger) */
    rev_env_init(&e, 44100.0f);
    for (int i = 0; i < 1000; ++i) rev_env_process(&e, 1.0f);
    for (int i = 0; i < 4410; ++i) rev_env_process(&e, 0.0f);  /* release */
    int quiet_onsets = 0;
    for (int i = 0; i < 1000; ++i) {
        rev_env_process(&e, 0.1f);   /* ~10% of the 1.0 peak, below 35% */
        quiet_onsets += rev_env_onset(&e);
    }
    CHECK(quiet_onsets == 0);

    /* settable relative threshold (trigger sensitivity) */
    rev_env_init(&e, 44100.0f);
    rev_env_set_thresh(&e, 0.5f);
    CHECK(e.onset_thresh == 0.5f);
    rev_env_set_thresh(&e, 2.0f);   /* clamps to 1 */
    CHECK(e.onset_thresh == 1.0f);
    rev_env_set_thresh(&e, -1.0f);  /* clamps to 0 */
    CHECK(e.onset_thresh == 0.0f);
    rev_env_set_thresh(&e, 0.35f);
    CHECK(e.onset_thresh == 0.35f);

    /* rev_env_playing: 1 while above threshold, 0 on silence */
    rev_env_init(&e, 44100.0f);
    CHECK(rev_env_playing(&e) == 0);
    for (int i = 0; i < 200; ++i) rev_env_process(&e, 1.0f);
    CHECK(rev_env_playing(&e) == 1);
    for (int i = 0; i < 4410; ++i) rev_env_process(&e, 0.0f);   /* ~100 ms gap */
    CHECK(rev_env_playing(&e) == 0);
    /* re-trigger after the gap */
    for (int i = 0; i < 200; ++i) rev_env_process(&e, 1.0f);
    CHECK(rev_env_playing(&e) == 1);

    if (fails == 0) { printf("test_env PASS\n"); return 0; }
    printf("test_env FAILED (%d)\n", fails);
    return 1;
}
