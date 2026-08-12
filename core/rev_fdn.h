#ifndef REV_FDN_H
#define REV_FDN_H
#include <stdint.h>
#include "rev_delay.h"

#define REV_FDN_LINES 8

/* 8-line Feedback Delay Network.
   Feedback uses the Householder matrix (2/N * (sum x) - x) with N=8,
   implemented as multiply/add only (0.25 == 2/8 is a compile-time constant).
   Even lines feed L, odd lines feed R (decorrelated -> stereo width).
   A slow triangle LFO dithers the read delay for a "living" tail. */
typedef struct {
    RevDelay line[REV_FDN_LINES];
    float state[REV_FDN_LINES];
    uint32_t base_delay[REV_FDN_LINES];
    float fb_gain;
    float damp_coef;
    float lfo_phase;
    float lfo_inc;
    float mod_depth;
    float out_gain;
} RevFdn;

/* len_pow2[] entries must each be a power of two and >= 32 (headroom for the LFO read-delay shift). */
void rev_fdn_init(RevFdn* f, float* mem, const uint32_t* len_pow2, float sample_rate);
void rev_fdn_clear(RevFdn* f);
/* decay/tone/mod in [0,1] */
void rev_fdn_set(RevFdn* f, float decay, float tone, float mod);
void rev_fdn_process(RevFdn* f, float in, float* out_l, float* out_r);
#endif
