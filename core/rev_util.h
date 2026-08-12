#ifndef REV_UTIL_H
#define REV_UTIL_H

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
#endif
