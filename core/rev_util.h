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
/* One-pole parameter smoothing with a convergence SNAP. Pure one-pole
   smoothing stalls in float near the target (the increment drops below the
   ULP): mix would sit at 0.9999934 instead of 1.0 (a residual dry leak at
   wet=100%), and gate would sit at a denormal instead of 0. Once within
   1e-4 the value lands exactly on the target; the snap step is far below
   audibility and only happens when the knob has (nearly) stopped moving. */
static inline float rev_smooth(float cur, float target, float c) {
    cur += (target - cur) * c;
    float d = target - cur;
    if (d > 1e-4f || d < -1e-4f) return cur;
    return target;
}
static inline uint32_t rev_next_pow2(uint32_t v) {
    if (v == 0u) return 1u;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u;
}
/* ZDL-safe zero fill: the C6000 build has no runtime memset, so clears are
   a plain word loop instead of a library call. n is the count of uint32
   words. */
static inline void rev_zero32(uint32_t* dst, uint32_t n) {
    uint32_t i;
    for (i = 0u; i < n; ++i) dst[i] = 0u;
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
