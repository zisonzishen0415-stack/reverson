#include "rev_rev.h"
#include "rev_util.h"
#include <string.h>

void rev_rev_init(RevRev* r, float* mem, uint32_t buf_len_pow2, float sample_rate) {
    r->buf = mem;
    r->buf_len = buf_len_pow2;
    r->mask = buf_len_pow2 - 1u;
    r->write_idx = 0u;
    r->seg_pos = 0u;
    r->seg_len = 1u;
    r->seg_peak = 0.0f;
    r->norm_gain = 1.0f;
    r->norm_target = 1.0f;
    r->norm_coef = rev_coeff_from_tc(0.05f * sample_rate);
    r->env = 0.0f;
    r->env_inc = 1.0f;
    r->cross_len = 1u;
    r->cross_pos = 0.0f;
    r->cross_inc = 1.0f;
    r->shape = 1;
}

void rev_rev_clear(RevRev* r) {
    memset(r->buf, 0, r->buf_len * sizeof(float));
    r->write_idx = 0u;
    r->seg_pos = 0u;
    r->seg_peak = 0.0f;
    r->norm_gain = 1.0f;
    r->norm_target = 1.0f;
    r->env = 0.0f;
    r->cross_pos = 0.0f;
    /* seg_len/cross_len/env_inc are intentionally not reset here: the next
       trigger (re)sets them. */
}

/* Division here is control-rate (once per trigger), not per-sample. */
void rev_rev_trigger(RevRev* r, uint32_t seg_len, uint32_t cross_samples, int shape) {
    if (seg_len < 2u) seg_len = 2u;
    if (seg_len > r->buf_len) seg_len = r->buf_len; /* oversized segment cannot alias the buffer */
    r->seg_len = seg_len;
    r->seg_pos = 0u;
    r->env = 0.0f;
    r->cross_len = cross_samples < 1u ? 1u : cross_samples;
    if (r->cross_len > r->seg_len) r->cross_len = r->seg_len;
    r->cross_pos = 0.0f;
    r->cross_inc = 1.0f / (float)r->cross_len;
    r->env_inc = 1.0f / (float)(r->seg_len - r->cross_len + 1u); /* rise over rise-window */
    r->shape = (shape < 1) ? 1 : ((shape > 4) ? 4 : shape);
    r->norm_target = rev_clampf(0.9f / (r->seg_peak + 1e-6f), 0.1f, 3.0f);
}

void rev_rev_write(RevRev* r, float x) {
    r->buf[r->write_idx & r->mask] = x;
    r->write_idx++;
    r->seg_peak *= 0.9999f; /* peak-hold decay */
    float a = rev_absf(x);
    if (a > r->seg_peak) r->seg_peak = a;
}

float rev_rev_process(RevRev* r) {
    uint32_t read_idx = (r->write_idx - 1u - r->seg_pos) & r->mask;
    float rev = r->buf[read_idx];

    float env = r->env;
    if (r->seg_pos >= r->seg_len - r->cross_len) {
        /* crossfade window: blend toward the segment head and let the swell
           fall to 0 so the seam is click-free */
        uint32_t head_idx = (r->write_idx - 1u) & r->mask;
        r->cross_pos += r->cross_inc;
        if (r->cross_pos > 1.0f) r->cross_pos = 1.0f;
        float head = r->buf[head_idx];
        rev = rev * (1.0f - r->cross_pos) + head * r->cross_pos;
        env = env * (1.0f - r->cross_pos); /* fall to 0 at the seam */
    } else {
        /* segment body: swell rises toward 1 */
        r->env += r->env_inc;
        if (r->env > 1.0f) r->env = 1.0f;
        env = r->env;
    }

    if (r->shape == 2) env = env * env;
    else if (r->shape == 3) env = env * env * env;
    else if (r->shape == 4) { float e2 = env * env; env = e2 * e2; }

    r->seg_pos++;
    if (r->seg_pos >= r->seg_len) {
        r->seg_pos = 0u;
        r->env = 0.0f;
        r->cross_pos = 0.0f;
    }

    r->norm_gain += (r->norm_target - r->norm_gain) * r->norm_coef;
    return rev * env * r->norm_gain;
}
