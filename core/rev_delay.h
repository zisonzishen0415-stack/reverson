#ifndef REV_DELAY_H
#define REV_DELAY_H
#include <stdint.h>

/* Power-of-two circular delay line. len must be a power of two.
   Index math uses & mask so no modulo instruction is emitted (ZDL-safe). */
typedef struct {
    float* buf;
    uint32_t len;
    uint32_t mask;
    uint32_t idx; /* write index */
} RevDelay;

/* len_pow2 must be > 0 and a power of two. */
void rev_delay_init(RevDelay* d, float* mem, uint32_t len_pow2);
void rev_delay_clear(RevDelay* d);
void rev_delay_write(RevDelay* d, float v);
/* delay_samples in [0, len-1]; 0 is the sample most recently written by
   rev_delay_write, len-1 is the oldest sample in the line.
   Reads with delay_samples >= len alias to older slots; the caller must
   guarantee history by clearing or pre-filling the buffer, the function
   does not auto-zero-pad. */
float rev_delay_read(const RevDelay* d, uint32_t delay_samples);
/* Fractional read with linear interpolation (ZDL-safe: mul/add only).
   Callers modulate delay positions with an LFO; stepping whole samples
   would inject a click every time the read position jumps, so the
   modulated reads (FDN bed, swell taps) go through this. */
float rev_delay_read_frac(const RevDelay* d, float delay_samples);
#endif
