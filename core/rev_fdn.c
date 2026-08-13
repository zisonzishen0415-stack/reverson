#include "rev_fdn.h"
#include "rev_util.h"
#include <string.h>

void rev_fdn_init(RevFdn* f, float* mem, const uint32_t* len_pow2, float sample_rate) {
    float* p = mem;
    for (int i = 0; i < REV_FDN_LINES; ++i) {
        rev_delay_init(&f->line[i], p, len_pow2[i]);
        rev_delay_clear(&f->line[i]);   /* controller amendment: zero caller memory */
        p += len_pow2[i];
        f->state[i] = 0.0f;
        f->base_delay[i] = len_pow2[i] - 16u; /* headroom for LFO shift */
    }
    f->fb_gain = 0.5f;
    f->damp_coef = 0.5f;
    f->lfo_phase = 0.0f;
    f->lfo_inc = 0.15f / sample_rate;
    f->mod_depth = 0.0f;
    f->out_gain = 0.125f;
}

void rev_fdn_clear(RevFdn* f) {
    for (int i = 0; i < REV_FDN_LINES; ++i) {
        rev_delay_clear(&f->line[i]);
        f->state[i] = 0.0f;
    }
    f->lfo_phase = 0.0f;
}

void rev_fdn_set(RevFdn* f, float decay, float tone, float mod) {
    decay = rev_clampf(decay, 0.0f, 1.0f);
    tone = rev_clampf(tone, 0.0f, 1.0f);
    mod = rev_clampf(mod, 0.0f, 1.0f);
    f->fb_gain = 0.92f * decay * decay;  /* musical taper */
    f->damp_coef = 0.05f + 0.9f * tone;  /* 0=dark, 1=bright */
    f->mod_depth = 8.0f * mod;
}

void rev_fdn_process(RevFdn* f, float in, float* out_l, float* out_r) {
    float sum = 0.0f;
    for (int i = 0; i < REV_FDN_LINES; ++i) sum += f->state[i];
    float common = 0.25f * f->fb_gain * sum;  /* Householder: 2/8 * g * sum */
    float fb[REV_FDN_LINES];
    for (int i = 0; i < REV_FDN_LINES; ++i) fb[i] = common - f->fb_gain * f->state[i];

    f->lfo_phase += f->lfo_inc;
    if (f->lfo_phase > 1.0f) f->lfo_phase -= 1.0f;
    float tri = 1.0f - 4.0f * rev_absf(f->lfo_phase - 0.5f);  /* true triangle, no jump at wrap */

    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < REV_FDN_LINES; ++i) {
        rev_delay_write(&f->line[i], in + fb[i]);
        /* interpolated fractional read: the LFO slides the read position
           smoothly instead of stepping whole samples (no zipper clicks) */
        float delay = (float)f->base_delay[i] + f->mod_depth * tri;
        if (delay < 0.0f) delay = 0.0f;
        float v = rev_delay_read_frac(&f->line[i], delay);
        f->state[i] += (v - f->state[i]) * f->damp_coef;
        if ((i & 1u) == 0u) l += f->state[i]; else r += f->state[i];
    }
    *out_l = l * f->out_gain;
    *out_r = r * f->out_gain;
}
