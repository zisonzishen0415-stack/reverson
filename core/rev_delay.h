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

void rev_delay_init(RevDelay* d, float* mem, uint32_t len_pow2);
void rev_delay_clear(RevDelay* d);
void rev_delay_write(RevDelay* d, float v);
/* delay_samples in [1, len); returns 0.0 while the line has no history */
float rev_delay_read(const RevDelay* d, uint32_t delay_samples);
#endif
