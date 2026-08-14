#include "rev_rev.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
#define CLOSE(a,b,tol) CHECK(((a) > (b) - (tol)) && ((a) < (b) + (tol)))
/* NaN and infinities fail this check; normal floats pass. Takes a value, not a
   stateful expression, so it is evaluated exactly once. */
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

int main(void) {
    float mem[64];
    RevRev r;
    rev_rev_init(&r, mem, 64u, 44100.0f);
    rev_rev_clear(&r);

    /* record a rising ramp 0..9 */
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);

    /* trigger a 10-sample reverse segment, linear swell, minimal crossfade */
    rev_rev_trigger(&r, 10u, 1u, 1);

    float out[24];
    for (int i = 0; i < 24; ++i) out[i] = rev_rev_process(&r);

    /* first sample is the newest (9) at swell env 0.1, norm gain ~1.0 (peak ~9 -> target 0.1, still smoothing) */
    CLOSE(out[0], 0.9f, 0.15f);
    /* the swell falls to 0 across the crossfade window: the last sample of the
       segment is silent so the wrap seam is click-free */
    CLOSE(out[9], 0.0f, 0.05f);
    /* the segment wrapped: second segment starts like the first */
    CLOSE(out[10], out[0], 0.2f);
    /* squared swell is smaller early on */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 10u, 1u, 2);
    float first_sq = rev_rev_process(&r); /* 9 * 0.1^2 = 0.09 */
    CLOSE(first_sq, 0.09f, 0.05f);

    /* oversized segment clamps to the 64-sample buffer: 128 samples stay finite */
    rev_rev_clear(&r);
    for (int i = 0; i < 64; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 200u, 1u, 1);
    for (int i = 0; i < 128; ++i) CHECK(is_finite_f(rev_rev_process(&r)));

    /* cross_len clamps to seg_len=2 and shape clamps to 4: finite, no crash */
    rev_rev_clear(&r);
    for (int i = 0; i < 4; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 2u, 5u, 9);
    for (int i = 0; i < 8; ++i) CHECK(is_finite_f(rev_rev_process(&r)));

    /* peak-0 trigger clamps norm to 3.0: with a 0.5 recorded signal the output
       stays within 3.0 * 0.5 = 1.5 and finite */
    rev_rev_clear(&r);
    rev_rev_trigger(&r, 10u, 1u, 1);            /* seg_peak == 0 -> norm_target 3.0 */
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, 0.5f);
    for (int i = 0; i < 2; ++i) {
        float v = rev_rev_process(&r);
        CHECK(is_finite_f(v));
        CHECK(v > -1.5f && v < 1.5f);
    }

    /* interleaved write/process (recording while playing back): stays finite */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 10u, 1u, 1);
    for (int i = 0; i < 10; ++i) {
        CHECK(is_finite_f(rev_rev_process(&r)));
        rev_rev_write(&r, (float)i);
    }

    /* live interleaved: writes continue while reading; the anchored read head must
       still replay the recorded ramp reversed (a frozen head would fail this) */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);   /* record 0..9 */
    rev_rev_trigger(&r, 10u, 1u, 1);                            /* anchor = write_idx (10) */
    float live[24];
    for (int i = 0; i < 24; ++i) {
        rev_rev_write(&r, (float)(100 + i));                    /* writes keep advancing write_idx */
        live[i] = rev_rev_process(&r);
    }
    CLOSE(live[0], 0.9f, 0.15f);   /* buf[anchor-1] = 9, env 0.1 */
    CLOSE(live[9], 0.0f, 0.05f);   /* segment end: env falls to 0 at the seam */
    CLOSE(live[10], live[0], 0.2f);/* wrap: segment loops, starts again */

    /* multi-voice: default is 1 voice (existing single-voice behavior) */
    CHECK(r.n_voices == 1u);
    CHECK(r.voice_scale == 1.0f);
    rev_rev_set_voices(&r, 9u);            /* clamps to max */
    CHECK(r.n_voices == REV_REV_MAX_VOICES);
    rev_rev_set_voices(&r, 0u);            /* clamps to min */
    CHECK(r.n_voices == 1u);

    /* 3-voice reverse: staggered read heads, averaged, stay finite and in
       the same ballpark as the single voice (voice 0 reads buf[anchor-1]=9
       at env 0.1; voices 1/2 read 6/3 at env 0.3/0.6 -> sum 4.5/3 = 1.5) */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_set_voices(&r, 3u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float mv0 = rev_rev_process(&r);
    CHECK(is_finite_f(mv0));
    CHECK(mv0 > 0.5f && mv0 < 3.0f);
    float mv_peak = 0.0f;
    for (int i = 0; i < 32; ++i) {
        float v = rev_rev_process(&r);
        CHECK(is_finite_f(v));
        if (v < 0.0f) v = -v;
        if (v > mv_peak) mv_peak = v;
    }
    CHECK(mv_peak < 4.0f);  /* averaged voices stay bounded */

    /* back to single voice: identical first sample as the original test */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_set_voices(&r, 1u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float single0 = rev_rev_process(&r);
    CLOSE(single0, 0.9f, 0.15f);

    /* pre_off: the read head skips the newest pre_off samples (the attack
       transient); content comes from the pre-onset tail instead. */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);   /* record 0..9 */
    rev_rev_set_preoff(&r, 2u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float po0 = rev_rev_process(&r);   /* buf[anchor-1-2] = 7, env 0.1 */
    CLOSE(po0, 0.7f, 0.15f);
    /* pre_off clamps into the segment: oversized value stays finite */
    rev_rev_set_preoff(&r, 100u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float po1 = rev_rev_process(&r);
    CHECK(is_finite_f(po1));
    /* pre_off back to 0: original first-sample behavior is unchanged */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_set_preoff(&r, 0u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float po2 = rev_rev_process(&r);
    CLOSE(po2, 0.9f, 0.15f);

    /* live-overwrite guard: the segment must fit in HALF the ring. The
       write head keeps recording live input while the anchored read head
       sweeps the frozen segment; with a segment longer than buf_len/2 the
       live writes clobber the not-yet-read material (crackle). The engine
       must clamp seg_len so this cannot happen. */
    {
        float mem2[1024];
        RevRev r2;
        rev_rev_init(&r2, mem2, 1024u, 44100.0f);
        rev_rev_clear(&r2);
        for (int i = 0; i < 600; ++i) rev_rev_write(&r2, (float)i);  /* frozen ramp 0..599 */
        rev_rev_trigger(&r2, 700u, 10u, 1);   /* 700 > half (512): must clamp */
        CHECK(r2.seg_len <= 510u);
        /* and the clamped playback must keep reading the FROZEN ramp while
           live writes continue (no clobber -> body values stay ramp-sized;
           the seam crossfade may blend the live head, so allow that) */
        for (int i = 0; i < 400; ++i) {
            rev_rev_write(&r2, 1000.0f + (float)i);   /* live writes */
            float v = rev_rev_process(&r2);
            CHECK(is_finite_f(v));
            CHECK(v > -300.0f && v < 300.0f);         /* normalized ramp, not raw live junk */
        }
    }

    if (fails == 0) { printf("test_rev PASS\n"); return 0; }
    printf("test_rev FAILED (%d)\n", fails);
    return 1;
}
