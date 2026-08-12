/* rev_swell.h - SPX90-style reverse swell engine (part of Reverson). */
#ifndef REV_SWELL_H
#define REV_SWELL_H
#include <stdint.h>
#include "rev_delay.h"

#define REV_SWELL_TAPS 13u
/* Power-of-two delay line. Max tap at scale 2.0 (revlen=1) is 2*~224 ms
   ~= 19744 samples @44k1 / 21491 @48k, so 32768 leaves headroom. */
#define REV_SWELL_BUF_LEN 32768u

/* Classic reverse reverb, no non-causal buffering:
   a delay line with 13 read heads at exponentially increasing delays
   (8 ms .. ~224 ms at scale 1.0) and exponentially increasing gains, then
   3 cascaded one-pole allpass diffusers per channel. A note is answered
   immediately by the near taps (ZERO predelay), the far louder taps build a
   crescendo, then the line runs dry -> the natural "reverse" swell/gate.
   Multiplicative gain -> no amplitude pumping, unlike a single wet-signal
   envelope ramp. */
typedef struct {
    RevDelay line;
    float sample_rate;
    float samples_per_ms;
    uint32_t base_delay[REV_SWELL_TAPS];   /* samples at scale 1.0 */
    float base_gain_l[REV_SWELL_TAPS];
    float base_gain_r[REV_SWELL_TAPS];
    float scale;             /* revlen -> tap span (0.5x..2.0x) */
    float amount;            /* 0..1 swell gain (driven by gate) */
    /* one-pole allpass states [channel][stage][0=prev_in,1=prev_out] */
    float ap[2][3][2];
    float ap_g[3];
    float out_gain;
} RevSwell;

void rev_swell_init(RevSwell* s, float* mem, uint32_t len_pow2, float sample_rate);
void rev_swell_clear(RevSwell* s);
/* revlen/amount in [0,1]: revlen scales the tap span, amount scales tap gain */
void rev_swell_set(RevSwell* s, float revlen, float amount);
void rev_swell_process(RevSwell* s, float in, float* out_l, float* out_r);
#endif
