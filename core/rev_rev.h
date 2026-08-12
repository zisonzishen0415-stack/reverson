#ifndef REV_REV_H
#define REV_REV_H
#include <stdint.h>

/* Reverse engine: circular record buffer + backwards read heads.
   Trigger mode: on each trigger a fresh segment of `seg_len` samples is
   replayed in reverse with a swell envelope (shape 1..4) and a crossfade
   into the segment head near the wrap to avoid clicks. Output is normalized
   against the last recorded peak so swells stay audible without clipping.
   All audio-path math is multiply/add only (division only at trigger time).

   Multi-voice: `n_voices` (1..REV_REV_MAX_VOICES) read heads replay the same
   anchored segment with staggered phase offsets (voice v starts at
   v*seg_len/n_voices), each with its own swell envelope and crossfade. Voices
   are averaged (voice_scale = 1/n_voices) so density thickens the wash without
   raising loudness. n_voices == 1 reproduces the original single-voice engine
   sample-for-sample. Call rev_rev_set_voices before rev_rev_trigger so the
   stagger is computed from the current count (take effect at the next trigger).

   Per-sample ordering contract: call rev_rev_write to record the incoming
   sample, then rev_rev_process to produce the output for that sample.

   rev_rev_trigger clamps seg_len to buf_len, so seg_len <= buf_len always
   holds and an oversized segment cannot alias the buffer.

   The swell envelope rises toward 1 over the segment body and falls to 0
   across the crossfade window, so the output at the segment seam is ~0 on
   both sides (click-free wrap).

   The read heads are anchored at trigger time (anchor = write_idx), so live
   recording does not move them; each voice replays the material ending at
   anchor-1 in reverse and loops that anchored material until the next
   trigger re-anchors.

   Smooth retrigger: if a swell is already playing when a new trigger arrives,
   the envelope is kept (re-anchor only), so rapid onsets do not hard-reset the
   level and pump ('wobble'). The swell rise is bounded to ~0.35 s regardless
   of segment length, so long reverse segments do not create a long pre-delay
   before the wash blooms. */

#define REV_REV_MAX_VOICES 4u

typedef struct {
    float* buf;
    float sample_rate;
    uint32_t buf_len;
    uint32_t mask;
    uint32_t write_idx;
    uint32_t anchor;  /* read-head anchor captured at trigger; the segment replays material ending at anchor-1 */
    uint32_t seg_len;
    uint32_t n_voices;
    uint32_t v_pos[REV_REV_MAX_VOICES];
    float v_env[REV_REV_MAX_VOICES];
    float v_cross[REV_REV_MAX_VOICES];
    float seg_peak;
    float norm_gain;
    float norm_target;
    float norm_coef;
    float env_inc;
    uint32_t cross_len;
    float cross_inc;
    float voice_scale;
    int shape;
} RevRev;

void rev_rev_init(RevRev* r, float* mem, uint32_t buf_len_pow2, float sample_rate);
void rev_rev_clear(RevRev* r);
/* voices in [1, REV_REV_MAX_VOICES]; takes effect at the next trigger */
void rev_rev_set_voices(RevRev* r, uint32_t voices);
void rev_rev_trigger(RevRev* r, uint32_t seg_len, uint32_t cross_samples, int shape);
void rev_rev_write(RevRev* r, float x);
float rev_rev_process(RevRev* r);
#endif
