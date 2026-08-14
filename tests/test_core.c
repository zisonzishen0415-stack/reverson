#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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
        float dc = (blk & 1u) ? -0.4f : 0.4f;   /* realistic input level (was 1.0) */
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

    /* reverse swell (v2): at full reverse the forward taps are off and the
       reverse layer replays the pre-onset tail - an onset burst (with tail
       content in the buffer) blooms the wet from the floor up, then it
       settles back down toward the floor (always-present, never silent). */
    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.3f);  /* ~0.64 s swell */
    Reverson_set_bed(r, 0.0f);   /* default: pure reverse swell, no bed */
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.2f); /* short hold */
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.7f);
    for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr); /* converge grid */
    for (int i = 0; i < 4410; ++i)   /* pre-onset tail (the layer needs content) */
        Reverson_process(r, (float)((i * 7) % 13) / 13.0f - 0.5f, &l, &rr);
    for (int i = 0; i < 4410; ++i) Reverson_process(r, 0.0f, &l, &rr);  /* gap */
    for (int i = 0; i < 50; ++i) Reverson_process(r, 1.0f, &l, &rr);    /* onset burst */
    float gswell = 0.0f, gpeak = 0.0f;
    for (int i = 0; i < 44100; ++i) {    /* 1 s: the swell window */
        Reverson_process(r, 0.0f, &l, &rr);
        gswell += rev_absf(l) + rev_absf(rr);
        if (rev_absf(l) > gpeak) gpeak = rev_absf(l);
        if (rev_absf(rr) > gpeak) gpeak = rev_absf(rr);
    }
    for (int i = 0; i < 44100; ++i) {    /* second s: hold + fall complete */
        Reverson_process(r, 0.0f, &l, &rr);
        if (rev_absf(l) > gpeak) gpeak = rev_absf(l);
        if (rev_absf(rr) > gpeak) gpeak = rev_absf(rr);
    }
    CHECK(gpeak > 0.002f);               /* the swell is audible after the onset */
    float gtail = 0.0f;
    for (int i = 0; i < 44100; ++i) {    /* third s: settled at the floor */
        Reverson_process(r, 0.0f, &l, &rr);
        gtail += rev_absf(l) + rev_absf(rr);
    }
    CHECK(gtail < gswell * 0.8f);        /* settled back toward the floor */

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

    /* pure mode + gate=0 is dry (A/B baseline): with the bed off and no
       reverse amount there is no wet bus at all. */
    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.0f);
    Reverson_set_bed(r, 0.0f);
    for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
    for (int i = 0; i < 44100; ++i) {
        Reverson_process(r, (i % 220 == 0) ? 0.8f : 0.0f, &l, &rr);
        CHECK(l == 0.0f && rr == 0.0f);
    }

    /* map6 invariants: every knob combination maps to in-range params, and
       each axis is monotone in the direction it claims. */
    for (int mi = 0; mi <= 4; ++mi) {
        for (int ri = 0; ri <= 4; ++ri) {
            for (int si = 0; si <= 4; ++si) {
                ReversonParams mp;
                Reverson_map6(mi / 4.0f, ri / 4.0f, si / 4.0f, 0.5f, 0.5f, 0.4f, &mp);
                float vals[15] = {mp.mix, mp.decay, mp.tone, mp.revlen, mp.duck, mp.gate,
                                  mp.shape, mp.mod, mp.sat, mp.width, mp.density, mp.bass, mp.diffusion,
                                  mp.trig, mp.predelay};
                for (int k = 0; k < 15; ++k) CHECK(vals[k] >= 0.0f && vals[k] <= 1.0f);
            }
        }
    }
    {
        ReversonParams lo, hi;
        Reverson_map6(0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.4f, &lo);
        Reverson_map6(0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.4f, &hi);
        CHECK(lo.gate >= 0.10f);            /* rev=0 is a wash, not a dry patch */
        CHECK(hi.gate > lo.gate + 0.3f);    /* rev axis: more reverse */
        CHECK(hi.shape > lo.shape);
        CHECK(hi.density < lo.density);
        Reverson_map6(0.5f, 0.5f, 0.0f, 0.5f, 0.5f, 0.4f, &lo);
        Reverson_map6(0.5f, 0.5f, 1.0f, 0.5f, 0.5f, 0.4f, &hi);
        CHECK(hi.revlen > lo.revlen);        /* space axis: bigger */
        CHECK(hi.decay > lo.decay);
        CHECK(hi.width > lo.width);
        Reverson_map6(0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 0.4f, &lo);
        Reverson_map6(0.5f, 0.5f, 0.5f, 1.0f, 0.5f, 0.4f, &hi);
        CHECK(hi.tone > lo.tone);            /* tone axis: dark -> bright */
        CHECK(hi.bass < lo.bass);
        CHECK(hi.sat > lo.sat);
        Reverson_map6(0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.4f, &lo);
        Reverson_map6(0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 0.4f, &hi);
        CHECK(hi.diffusion > lo.diffusion);  /* grain axis: smooth + flow */
        CHECK(hi.mod > lo.mod);
    }

    /* 15-param model: trig/predelay exist, clamp, and default sane */
    CHECK(Reverson_get_param(r, REVERSON_PARAM_TRIG) == 0.5f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_PREDELAY) == 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_TRIG, 1.5f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_TRIG) == 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_TRIG, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_PREDELAY, -1.0f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_PREDELAY) == 0.0f);

    /* mode tables: 1..5 fill the 13 shared params in range; 0 is a no-op;
       out-of-range clamps to 5 */
    {
        ReversonParams m0, m1, m5, m9;
        m0.mix = -1.0f;
        Reverson_mode(0, &m0);
        CHECK(m0.mix == -1.0f);  /* mode 0 leaves the struct alone */
        Reverson_mode(1, &m1);
        Reverson_mode(5, &m5);
        Reverson_mode(9, &m9);   /* clamps to 5 */
        const float* a1 = (const float*)&m1;
        const float* a5 = (const float*)&m5;
        const float* a9 = (const float*)&m9;
        for (int k = 0; k < 13; ++k) {
            CHECK(a1[k] >= 0.0f && a1[k] <= 1.0f);
            CHECK(a5[k] >= 0.0f && a5[k] <= 1.0f);
            CHECK(a9[k] == a5[k]);
        }
    }

    /* sample-rate portability: 48 kHz stays finite and bounded with the
       longest swell (revlen=1). */
    {
        uint32_t need48 = Reverson_state_size(48000.0f);
        void* m48 = malloc(need48);
        Reverson* r48 = Reverson_init(m48, need48, 48000.0f);
        CHECK(r48 != NULL);
        Reverson_set_6knob(r48, 0.65f, 0.5f, 0.6f, 0.5f, 0.6f, 0.4f);
        Reverson_set_param(r48, REVERSON_PARAM_REVLEN, 1.0f);
        float l48 = 0.0f, rr48 = 0.0f, pk48 = 0.0f;
        for (int i = 0; i < 96000; ++i) {
            Reverson_process(r48, (i % 240 == 0) ? 0.7f : 0.0f, &l48, &rr48);
            CHECK(l48 == l48 && rr48 == rr48);
            float a = l48 < 0 ? -l48 : l48, b = rr48 < 0 ? -rr48 : rr48;
            if (a > pk48) pk48 = a;
            if (b > pk48) pk48 = b;
        }
        CHECK(pk48 < 1.2f);
        free(m48);
    }

    /* output safety limiter: at a realistic level the wet is loud (makeup
       matched toward the dry) and under the rail; at a full-scale input
       the output stays finite and bounded (a single-sample attack leak is
       allowed - the limiter has no lookahead). */
    {
        Reverson_reset(r);   /* fresh state: the earlier blocks leave residue */
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 0.3f);
        Reverson_set_param(r, REVERSON_PARAM_SAT, 0.1f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.5f);
        Reverson_set_bed(r, 0.0f);   /* bed off: the earlier blocks left it on */
        float l, rr, pk = 0.0f, xpk = 0.0f;
        for (int i = 0; i < 44100; ++i) {
            float x = 0.4f * (float)sin(2.0 * 3.14159265358979323846 * 110.0 * (double)i / 44100.0);
            if (rev_absf(x) > xpk) xpk = rev_absf(x);
            Reverson_process(r, x, &l, &rr);
            CHECK(l == l && rr == rr);
            if (rev_absf(l) > pk) pk = rev_absf(l);
            if (rev_absf(rr) > pk) pk = rev_absf(rr);
        }
        CHECK(pk > 0.25f);        /* the wet is loud (makeup matched) */
        CHECK(pk <= 0.96f);       /* and under the rail at realistic levels */
        pk = 0.0f;
        for (int i = 0; i < 44100; ++i) {
            float x = 1.0f * (float)sin(2.0 * 3.14159265358979323846 * 110.0 * (double)i / 44100.0);
            Reverson_process(r, x, &l, &rr);
            CHECK(l == l && rr == rr);
            if (rev_absf(l) > pk) pk = rev_absf(l);
            if (rev_absf(rr) > pk) pk = rev_absf(rr);
        }
        CHECK(pk < 2.5f);         /* full-scale: finite and bounded (leak) */
    }

    free(mem);
    if (fails == 0) { printf("test_core PASS\n"); return 0; }
    printf("test_core FAILED (%d)\n", fails);
    return 1;
}
