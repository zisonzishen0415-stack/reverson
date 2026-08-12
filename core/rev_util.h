#ifndef REV_UTIL_H
#define REV_UTIL_H
#include <stdint.h>

static inline float rev_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
static inline float rev_minf(float a, float b) { return a < b ? a : b; }
static inline float rev_maxf(float a, float b) { return a > b ? a : b; }
static inline float rev_absf(float x) { return x < 0.0f ? -x : x; }
/* one-pole coefficient from a time constant in samples; 1.0 == instant */
static inline float rev_coeff_from_tc(float tc_samples) {
    return 1.0f / (tc_samples + 1.0f);
}
static inline uint32_t rev_next_pow2(uint32_t v) {
    if (v == 0u) return 1u;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u;
}
/* cubic soft clip (no exp/tanh; ZDL-safe). Maps [-1,1] -> [-1,1], gentle boost
   then saturate. The final clamp absorbs float rounding so the endpoints land
   on exactly +/-1 (analytic range of the cubic on [-1,1] is exactly [-1,1]). */
static inline float rev_softclip(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    float y = 1.5f * (x - x * x * x * 0.333333f);
    if (y > 1.0f) return 1.0f;
    if (y < -1.0f) return -1.0f;
    return y;
}
#endif
