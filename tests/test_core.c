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
    /* boundary: undersized caller buffer must be rejected, not overrun */
    CHECK(Reverson_init(mem, need - 1u, 44100.0f) == NULL);

    CHECK(Reverson_get_param(r, REVERSON_PARAM_MIX) > 0.5f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_DECAY) == 0.6f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_GATE) == 0.0f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_DENSITY) == 0.75f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_BASS) == 0.55f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_SHAPE) == 0.33f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_MOD) == 0.35f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_WIDTH) == 0.80f);
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, 2.0f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_DENSITY) == 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.75f);
    Reverson_set_param(r, REVERSON_PARAM_BASS, -1.0f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_BASS) == 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_BASS, 0.55f);

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
    Reverson_set_bed(r, 1.0f);   /* FDN bed path: stereo spread test */
    /* warm up the reverse buffer: with the anchored read head the segment only
       replays pre-trigger material, so feed the pulse pattern long enough that
       the anchor region holds recorded audio before measuring */
    for (int i = 0; i < 95000; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr);
    }
    float diff_sum = 0.0f;
    /* Each onset re-triggers a fresh reverse segment, so the swell envelope only
       reaches ~0.6% of full scale before the next pulse re-triggers; accumulate
       decorrelation over 2x pulse periods so the FDN stereo spread clears the
       floor (with the warm-up above, the corrected engine measures ~21, vs ~0
       with an empty buffer). */
    for (int i = 0; i < 17640; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr);
        diff_sum += rev_absf(l - rr);
    }
    CHECK(diff_sum > 0.01f);

    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.2f);
    Reverson_set_bed(r, 1.0f);   /* bed on: duck rides the FDN bed down */
    for (int i = 0; i < 30000; ++i) Reverson_process(r, 1.0f, &l, &rr); /* settle duck (5ms TC converges slowly) */
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

    /* wet bus stays bounded even at sat=0 / decay=1 (always-on limiter) */
    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_SAT, 0.0f);
    Reverson_set_bed(r, 1.0f);   /* bed on: wet bus stays bounded at decay=1 */
    float wpeak = 0.0f;
    for (int blk = 0; blk < 8; ++blk) {
        float dc = (blk & 1u) ? -1.0f : 1.0f;
        for (int i = 0; i < 8820; ++i) {
            Reverson_process(r, dc, &l, &rr);
            if (rev_absf(l) > wpeak) wpeak = rev_absf(l);
            if (rev_absf(rr) > wpeak) wpeak = rev_absf(rr);
        }
    }
    CHECK(wpeak < 1.01f);

    /* width=0 collapses to mono exactly (settle the width smoothing first) */
    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_WIDTH, 0.0f);
    Reverson_set_bed(r, 1.0f);   /* bed on: width collapse applies to the bed */
    for (int i = 0; i < 8820; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr); /* settle: cur.width -> 0 */
    }
    float wdiff = 0.0f;
    for (int i = 0; i < 8820; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr);
        if (rev_absf(l - rr) > wdiff) wdiff = rev_absf(l - rr);
    }
    CHECK(wdiff == 0.0f);

    /* reset -> silence stays silent */
    Reverson_reset(r);
    for (int i = 0; i < 100; ++i) {
        Reverson_process(r, 0.0f, &l, &rr);
        CHECK(l == 0.0f && rr == 0.0f);
    }

    /* reverse swell: an isolated onset blooms the wet from a floor up to full
       then settles back to the floor - always present (no hard gate, no
       sudden blast). The wet responds immediately (no predelay). */
    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.3f);  /* ~0.64 s swell */
    Reverson_set_bed(r, 0.0f);   /* default: pure reverse swell, no bed */
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.2f); /* short hold */
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.7f);
    for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr); /* silence */
    Reverson_process(r, 1.0f, &l, &rr);  /* the onset */
    float gpeak = 0.0f;
    for (int i = 0; i < 88200; ++i) {    /* 2 s: swell rises, holds, cuts */
        Reverson_process(r, 0.0f, &l, &rr);
        if (rev_absf(l) > gpeak) gpeak = rev_absf(l);
        if (rev_absf(rr) > gpeak) gpeak = rev_absf(rr);
    }
    CHECK(gpeak > 0.002f);               /* the swell is audible after the onset */
    float gtail = 0.0f;
    for (int i = 0; i < 44100; ++i) {    /* after the swell settles */
        Reverson_process(r, 0.0f, &l, &rr);
        gtail += rev_absf(l) + rev_absf(rr);
    }
    CHECK(gtail < gpeak);                /* settled back below the swell peak */

    /* max density (4 voices) + bass boost stays bounded and finite */
    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.95f);
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_BASS, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_SAT, 0.0f);
    Reverson_set_bed(r, 1.0f);   /* bed on: max density + bass stays bounded */
    for (int i = 0; i < 95000; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr);
    }
    float dpeak = 0.0f;
    for (int i = 0; i < 17640; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr);
        CHECK(l == l && rr == rr);
        if (rev_absf(l) > dpeak) dpeak = rev_absf(l);
        if (rev_absf(rr) > dpeak) dpeak = rev_absf(rr);
    }
    CHECK(dpeak < 1.01f);

    free(mem);
    if (fails == 0) { printf("test_core PASS\n"); return 0; }
    printf("test_core FAILED (%d)\n", fails);
    return 1;
}
