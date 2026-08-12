/* rev_swell.c - SPX90-style reverse swell engine (part of Reverson).
 * Tap table: 13 heads, exponentially spaced delays (8..~224 ms, ratio 1.32)
 * and exponentially increasing gains (ratio 1.25). Per-tap stereo pan
 * (L/R alternate) gives natural width. 3 one-pole allpasses diffuse the taps
 * so it reads as "reverb" not "delay"; allpass is unit magnitude (no boost).
 */
#include "rev_swell.h"
#include "rev_util.h"
#include <string.h>

static const float TAP_MS[REV_SWELL_TAPS] = {
    8.00f, 10.56f, 13.94f, 18.40f, 24.29f, 32.06f, 42.32f,
    55.86f, 73.74f, 97.33f, 128.48f, 169.59f, 223.86f
};
static const float TAP_GAIN[REV_SWELL_TAPS] = {
    0.0320f, 0.0400f, 0.0500f, 0.0625f, 0.0781f, 0.0977f, 0.1221f,
    0.1526f, 0.1907f, 0.2384f, 0.2980f, 0.3725f, 0.4657f
};

void rev_swell_init(RevSwell* s, float* mem, uint32_t len_pow2,
                    float* diff_mem, uint32_t diff_len_pow2, float sample_rate) {
    rev_delay_init(&s->line, mem, len_pow2);
    rev_delay_clear(&s->line);           /* zero caller memory */
    for (int st = 0; st < 2; ++st) {
        for (int ch = 0; ch < 2; ++ch) {
            rev_delay_init(&s->diff[st][ch], diff_mem, diff_len_pow2);
            rev_delay_clear(&s->diff[st][ch]);
            diff_mem += diff_len_pow2;
        }
    }
    /* mutually-prime delays so the feedback echo train never locks into a
       single periodic comb; feedback ~0.5 fills the gaps between taps with
       a ~100 ms diffusion tail (0.5^9 ~ -54 dB). */
    s->diff_d[0] = 331u;
    s->diff_d[1] = 463u;
    s->diff_fb[0] = 0.50f;
    s->diff_fb[1] = 0.45f;
    s->sample_rate = sample_rate;
    s->samples_per_ms = sample_rate * 0.001f;
    for (int i = 0; i < REV_SWELL_TAPS; ++i) {
        uint32_t d = (uint32_t)(TAP_MS[i] * s->samples_per_ms);
        if (d < 2u) d = 2u;
        if (d >= len_pow2 - 1u) d = len_pow2 - 1u;
        s->base_delay[i] = d;
        /* alternate pan: even heads full L, odd heads full R (0.4 bleed) */
        float gl = (i & 1u) ? 0.4f : 1.0f;
        float gr = (i & 1u) ? 1.0f : 0.4f;
        s->base_gain_l[i] = TAP_GAIN[i] * gl;
        s->base_gain_r[i] = TAP_GAIN[i] * gr;
    }
    s->scale = 1.0f;
    s->amount = 0.0f;
    s->ap_g[0] = 0.40f;
    s->ap_g[1] = 0.50f;
    s->ap_g[2] = 0.60f;
    s->out_gain = 0.60f;
    rev_swell_clear(s);
}

void rev_swell_clear(RevSwell* s) {
    rev_delay_clear(&s->line);
    for (int st = 0; st < 2; ++st)
        for (int ch = 0; ch < 2; ++ch)
            rev_delay_clear(&s->diff[st][ch]);
    memset(s->ap, 0, sizeof(s->ap));
}

void rev_swell_set(RevSwell* s, float revlen, float amount) {
    revlen = rev_clampf(revlen, 0.0f, 1.0f);
    amount = rev_clampf(amount, 0.0f, 1.0f);
    s->scale = 0.5f + 1.5f * revlen;  /* 0.5x .. 2.0x tap span */
    s->amount = amount;
}

void rev_swell_process(RevSwell* s, float in, float* out_l, float* out_r) {
    rev_delay_write(&s->line, in);
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < REV_SWELL_TAPS; ++i) {
        uint32_t d = (uint32_t)((float)s->base_delay[i] * s->scale);
        if (d >= s->line.len) d = s->line.len - 1u;   /* guard: never alias */
        float v = rev_delay_read(&s->line, d);
        l += v * (s->base_gain_l[i] * s->amount);
        r += v * (s->base_gain_r[i] * s->amount);
    }
    /* Feedback diffusion (Freeverb-style allpass): fills the gaps between
       taps so the swell reads as reverb, not a discrete echo line. Each tap
       becomes a decaying echo train; y = -x + buf[n-D], buf[n] = x + fb*buf[n-D]. */
    for (int ch = 0; ch < 2; ++ch) {
        float x = (ch == 0) ? l : r;
        for (int st = 0; st < 2; ++st) {
            RevDelay* d = &s->diff[st][ch];
            float bufout = rev_delay_read(d, s->diff_d[st]);   /* buf[n-D] */
            float y = -x + bufout;
            rev_delay_write(d, x + s->diff_fb[st] * bufout);   /* buf[n] */
            x = y;
        }
        /* cascaded one-pole allpass smears (unit magnitude, no boost) */
        for (int st = 0; st < 3; ++st) {
            float g = s->ap_g[st];
            float y = -g * x + s->ap[ch][st][0] + g * s->ap[ch][st][1];
            s->ap[ch][st][0] = x;   /* prev input */
            s->ap[ch][st][1] = y;   /* prev output */
            x = y;
        }
        if (ch == 0) l = x; else r = x;
    }
    *out_l = l * s->out_gain;
    *out_r = r * s->out_gain;
}
