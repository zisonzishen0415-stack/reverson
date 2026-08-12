#ifndef REV_REV_H
#define REV_REV_H
#include <stdint.h>

/* Reverse engine: circular record buffer + backwards read head.
   Trigger mode: on each trigger a fresh segment of `seg_len` samples is
   replayed in reverse with a swell envelope (shape 1..4) and a crossfade
   into the segment head near the wrap to avoid clicks. Output is normalized
   against the last recorded peak so swells stay audible without clipping.
   All audio-path math is multiply/add only (division only at trigger time). */
typedef struct {
    float* buf;
    uint32_t buf_len;
    uint32_t mask;
    uint32_t write_idx;
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