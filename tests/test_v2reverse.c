/* test_v2reverse.c - integrated reverse layer (RevRev):
 * (A) at full reverse the forward taps are silent (discriminator);
 * (B) the layer plays pre-onset material backwards after the trigger;
 * (C) boundedness. */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

static void configure(Reverson* r) {
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);   /* pure reverse layer */
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.4f);
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_SHAPE, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_MOD, 0.3f);
    Reverson_set_param(r, REVERSON_PARAM_SAT, 0.1f);
}

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);
    configure(r);
    float l, rr;

    /* before ANY trigger: the reverse buffer is silent -> wet is exactly 0 */
    for (int i = 0; i < 100; ++i) {
        Reverson_process(r, 0.0f, &l, &rr);
        CHECK(l == 0.0f && rr == 0.0f);
    }
    for (int i = 0; i < 44000; ++i) Reverson_process(r, 0.0f, &l, &rr); /* converge the 8-sample param grid */

    /* (A) discriminator: at full reverse the forward 13-tap engine is OFF,
       and with an EMPTY pre-onset buffer the reverse layer has nothing to
       play. An isolated onset therefore produces (near-)silence in the tap
       echo window [400, 10000] - a forward-tap engine echoes loudly there. */
    {
        float ewin = 0.0f;
        for (int i = 0; i < 50; ++i) Reverson_process(r, 0.9f, &l, &rr);  /* isolated onset */
        for (int i = 0; i < 10000; ++i) {
            Reverson_process(r, 0.0f, &l, &rr);
            if (i >= 400) ewin += rev_absf(l) + rev_absf(rr);
            CHECK(is_finite_f(l) && is_finite_f(rr));
        }
        CHECK(ewin < 0.001f);
    }

    /* (B) positive: with a pre-onset tail in the buffer, the layer plays it
       back after the trigger. The window [12500, 13500] after the onset is
       past the longest forward tap (~10933 samples at scale 1.1) and past
       the diffuser comb tail, yet inside the reverse segment span (~14112
       samples), where the pre-onset tail sits (positions ~4460..13280). */
    Reverson_reset(r);
    configure(r);
    for (int i = 0; i < 8820; ++i) {                       /* pre-onset tail burst */
        float x = (float)sin(2.0 * 3.14159265358979323846 * 220.0 * (double)i / 44100.0);
        Reverson_process(r, 0.4f * x * (1.0f - (float)i / 8820.0f), &l, &rr);
    }
    for (int i = 0; i < 4410; ++i) Reverson_process(r, 0.0f, &l, &rr);  /* gap */
    for (int i = 0; i < 50; ++i) Reverson_process(r, 0.9f, &l, &rr);    /* onset */
    for (int i = 0; i < 12500; ++i) Reverson_process(r, 0.0f, &l, &rr); /* skip past taps + tail */
    float tail_win = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        Reverson_process(r, 0.0f, &l, &rr);
        tail_win += rev_absf(l) + rev_absf(rr);
    }
    CHECK(tail_win > 0.001f);  /* the reverse layer plays the pre-onset tail */

    /* (C) boundedness with sustained pulse input at full reverse */
    float peak = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        Reverson_process(r, (i % 220 == 0) ? 0.8f : 0.0f, &l, &rr);
        CHECK(is_finite_f(l) && is_finite_f(rr));
        if (rev_absf(l) > peak) peak = rev_absf(l);
        if (rev_absf(rr) > peak) peak = rev_absf(rr);
    }
    CHECK(peak < 1.2f);

    /* (D) retrigger clicks: a mid-swell onset must NOT step the output.
       The reverse layer re-anchors only while its gain is ~0 (at the gate
       floor); mid-swell onsets re-plan the envelope (continuous) and leave
       the read head alone. Measured as max per-sample |delta| in the wet. */
    {
        Reverson_reset(r);
        configure(r);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 0.3f);   /* mechanism check: mid mix (content transients stay under threshold) */
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.8f);  /* long hold: mid-swell retriggers */
        float prev = 0.0f, maxd = 0.0f;
        /* sustained low note (content in the buffer), then two onsets close
           together while the swell is up */
        for (int i = 0; i < 8820; ++i) {
            float x = 0.4f * (float)sin(2.0 * 3.14159265358979323846 * 110.0 * (double)i / 44100.0);
            Reverson_process(r, x, &l, &rr);
        }
        for (int i = 0; i < 4410; ++i) Reverson_process(r, 0.0f, &l, &rr);
        for (int i = 0; i < 50; ++i) Reverson_process(r, 0.4f, &l, &rr);  /* onset 1 (realistic level) */
        for (int i = 0; i < 4410; ++i) Reverson_process(r, 0.0f, &l, &rr); /* mid-swell */
        for (int i = 0; i < 50; ++i) {
            float a = 0.4f;
            if (i < 10) a = 0.4f * (float)i / 10.0f;              /* smooth rise (still an onset) */
            if (i >= 30) a = 0.4f * (float)(50 - i) / 20.0f;      /* release ramp: no dry-path step at the measurement start */
            Reverson_process(r, a, &l, &rr);
        }  /* onset 2 (mid-swell) */
        for (int i = 0; i < 22050; ++i) {
            Reverson_process(r, 0.0f, &l, &rr);
            if (i < 64) { prev = l; continue; }   /* let the limiter lookahead fill */
            float d = rev_absf(l - prev);
            if (d > maxd) maxd = d;
            prev = l;
            CHECK(is_finite_f(l) && is_finite_f(rr));
        }
        CHECK(maxd < 0.05f);   /* no steps: the re-anchor was silent */
    }

    /* (E) fast-picking crackle: rapid onsets (every 120 ms) in gated mode
       must not produce TRUE clicks - a single-sample spike (this sample
       jumps > 0.08 while the previous step was < 0.02). The reversed
       attack tail of each re-anchored segment used to slam through the
       diffuser as a broadband spike (fixed by the retrigger env rise +
       the fixed ~6 kHz wet smoothing). */
    {
        Reverson_reset(r);
        configure(r);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);   /* wet only */
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 0.9f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.3f);
        Reverson_set_param(r, REVERSON_PARAM_TRIG, 0.35f);
        {
            ReversonParams mp;
            Reverson_mode(3, &mp);
            Reverson_set_param(r, REVERSON_PARAM_DECAY, mp.decay);
            Reverson_set_param(r, REVERSON_PARAM_TONE, mp.tone);
            Reverson_set_param(r, REVERSON_PARAM_REVLEN, mp.revlen);
            Reverson_set_param(r, REVERSON_PARAM_GATE, mp.gate);
            Reverson_set_param(r, REVERSON_PARAM_SHAPE, mp.shape);
            Reverson_set_param(r, REVERSON_PARAM_MOD, mp.mod);
            Reverson_set_param(r, REVERSON_PARAM_SAT, mp.sat);
            Reverson_set_param(r, REVERSON_PARAM_WIDTH, mp.width);
            Reverson_set_param(r, REVERSON_PARAM_DENSITY, mp.density);
            Reverson_set_param(r, REVERSON_PARAM_BASS, mp.bass);
            Reverson_set_param(r, REVERSON_PARAM_DIFFUSION, mp.diffusion);
        }
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        float prev2 = 0.0f, prev = 0.0f;
        int clicks = 0;
        for (int i = 0; i < 352800; ++i) {   /* 8 s of fast picking */
            double t = (double)i / 44100.0;
            double pos = fmod(t, 0.12);
            float x;
            if (pos < 0.025) {
                float e = (float)exp(-pos * 120.0);
                x = e * (float)(sin(2.0 * 3.14159265358979323846 * 220.0 * t)
                                + 0.5 * sin(2.0 * 3.14159265358979323846 * 330.0 * t)
                                + 0.3 * sin(2.0 * 3.14159265358979323846 * 440.0 * t));
            } else {
                x = 0.0f;
            }
            x += 0.08f * (float)sin(2.0 * 3.14159265358979323846 * 110.0 * t);
            x *= 0.6f;
            Reverson_process(r, x, &l, &rr);
            float d1 = (float)fabs(l - prev);
            float d2 = (float)fabs(prev - prev2);
            if (d1 > 0.08f && d2 < 0.02f) clicks++;   /* true single-sample spike */
            prev2 = prev;
            prev = l;
            CHECK(is_finite_f(l) && is_finite_f(rr));
        }
        CHECK(clicks == 0);   /* no broadband spikes from re-anchoring */
    }

    free(mem);
    if (fails == 0) { printf("test_v2reverse PASS\n"); return 0; }
    printf("test_v2reverse FAILED (%d)\n", fails);
    return 1;
}
