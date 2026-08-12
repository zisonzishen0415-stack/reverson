#ifndef REV_REV_H
#define REV_REV_H
#include <stdint.h>

/* Reverse engine: circular record buffer + backwards read head.
   Trigger mode: on each trigger a fresh segment of `seg_len` samples is
   replayed in reverse with a swell envelope (shape 1..4) and a crossfade
   into the segment head near the wrap to avoid clicks. Output is normalized
   against the last recorded peak so swells stay audible without clipping.
   All audio-path math is multiply/add only (division only at trigger time).

   Per-sample ordering contract: call rev_rev_write to record the incoming
   sample, then rev_rev_process to produce the output for that sample.

   rev_rev_trigger clamps seg_len to buf_len, so seg_len <= buf_len always
   holds and an oversized segment cannot alias the buffer.

   The swell envelope rises toward 1 over the segment body and falls to 0
   across the crossfade window, so the output at the segment seam is ~0 on
   both sides (click-free wrap).

   The read head is anchored at trigger time (anchor = write_idx), so live
   recording does not move it; the segment replays the material ending at
   anchor-1 in reverse and loops that anchored material until the next
   trigger re-anchors. */

typedef struct {
    float* buf;
    uint32_t buf_len;
    uint32_t mask;
    uint32_t write_idx;
    uint32_t anchor;  /* read-head anchor captured at trigger; the segment replays material ending at anchor-1 */
    uint32_t seg_pos;
    uint32_t seg_len;
    float seg_peak;
    float norm_gain;
    float norm_target;
    float norm_coef;
    float env;
    float env_inc;
    uint32_t cross_len;
    float cross_pos;
    float cross_inc;
    int shape;
} RevRev;

void rev_rev_init(RevRev* r, float* mem, uint32_t buf_len_pow2, float sample_rate);
void rev_rev_clear(RevRev* r);
void rev_rev_trigger(RevRev* r, uint32_t seg_len, uint32_t cross_samples, int shape);
void rev_rev_write(RevRev* r, float x);
float rev_rev_process(RevRev* r);
#endif
