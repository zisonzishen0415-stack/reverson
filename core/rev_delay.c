#include "rev_delay.h"
#include "rev_util.h"

void rev_delay_init(RevDelay* d, float* mem, uint32_t len_pow2) {
    /* contract: len_pow2 must be > 0 and a power of two */
    d->buf = mem;
    d->len = len_pow2;
    d->mask = len_pow2 - 1u;
    d->idx = 0u;
}
void rev_delay_clear(RevDelay* d) {
    rev_zero32((uint32_t*)d->buf, d->len);   /* len floats == len words */
    d->idx = 0u;
}
void rev_delay_write(RevDelay* d, float v) {
    d->buf[d->idx & d->mask] = v;
    d->idx++;
}
float rev_delay_read(const RevDelay* d, uint32_t delay_samples) {
    /* read the sample written `delay_samples` samples ago (idx-1 is the
       just-written sample; unsigned wraparound is defined) */
    uint32_t i = d->idx - 1u - delay_samples;
    return d->buf[i & d->mask];
}

float rev_delay_read_frac(const RevDelay* d, float delay_samples) {
    if (delay_samples < 0.0f) delay_samples = 0.0f;
    uint32_t i0 = (uint32_t)(int)delay_samples;   /* int conv is native; no RTS */
    float frac = delay_samples - (float)i0;
    uint32_t i1 = i0 + 1u;
    float v0 = d->buf[(d->idx - 1u - i0) & d->mask];
    float v1 = d->buf[(d->idx - 1u - i1) & d->mask];
    return v0 + (v1 - v0) * frac;
}
