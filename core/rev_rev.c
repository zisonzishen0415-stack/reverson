#include "rev_rev.h"
#include "rev_util.h"

void rev_rev_init(RevRev* r, float* mem, uint32_t buf_len_pow2, float sample_rate) {
    r->buf = mem;
    r->sample_rate = sample_rate;
    r->buf_len = buf_len_pow2;
    r->mask = buf_len_pow2 - 1u;
    r->write_idx = 0u;
    r->anchor = 0u;
    r->seg_len = 1u;
    r->n_voices = 1u;
    r->voice_scale = 1.0f;
    r->seg_peak = 0.0f;
    r->norm_gain = 1.0f;
    r->norm_target = 1.0f;
    r->norm_coef = rev_coeff_from_tc(0.05f * sample_rate);
    r->env_inc = 1.0f;
    r->cross_len = 1u;
    r->cross_inc = 1.0f;
    r->pre_off = 0u;
    r->g_fade = 0.0f;
    r->g_fade_inc = 1.0f / (0.012f * sample_rate);   /* ~12 ms retrigger crossfade */
    r->g_anchor = 0u;
    r->shape = 1;
    for (uint32_t v = 0; v < REV_REV_MAX_VOICES; ++v) {
        r->v_pos[v] = 0u;
        r->v_env[v] = 0.0f;
        r->v_cross[v] = 0.0f;
        r->g_pos[v] = 0u;
        r->g_env[v] = 0.0f;
    }
}

void rev_rev_clear(RevRev* r) {
    rev_zero32((uint32_t*)r->buf, r->buf_len);   /* buf_len floats == words */
    r->write_idx = 0u;
    r->anchor = 0u;
    r->seg_peak = 0.0f;
    r->norm_gain = 1.0f;
    r->norm_target = 1.0f;
    r->g_fade = 0.0f;
    r->g_anchor = 0u;
    for (uint32_t v = 0; v < REV_REV_MAX_VOICES; ++v) {
        r->v_pos[v] = 0u;
        r->v_env[v] = 0.0f;
        r->v_cross[v] = 0.0f;
        r->g_pos[v] = 0u;
        r->g_env[v] = 0.0f;
    }
    /* seg_len/cross_len/env_inc/voice_scale are intentionally not reset here:
       the next trigger (re)sets them. */
}

void rev_rev_set_voices(RevRev* r, uint32_t voices) {
    if (voices < 1u) voices = 1u;
    if (voices > REV_REV_MAX_VOICES) voices = REV_REV_MAX_VOICES;
    r->n_voices = voices;
    r->voice_scale = 1.0f / (float)voices;
}

void rev_rev_set_preoff(RevRev* r, uint32_t samples) {
    r->pre_off = samples;
}

/* Division here is control-rate (once per trigger), not per-sample. */
void rev_rev_trigger(RevRev* r, uint32_t seg_len, uint32_t cross_samples, int shape) {
    /* retrigger ghost: mask the anchor jump with a TRUE CONTENT crossfade.
       Only armed on a MID-SWELL retrigger (fresh trigger: nothing playing,
       nothing to mask). The OLD read heads (anchor + per-voice positions +
       envelope levels) are saved so the old segment KEEPS PLAYING while it
       fades out over ~12 ms as the new segment fades in - a scalar-level
       ghost would leave a decaying DC thump on the audio (crackle). */
    int fresh = (r->v_env[0] == 0.0f && r->v_cross[0] == 0.0f);
    if (fresh) {
        r->g_fade = 0.0f;
    } else {
        r->g_fade = 1.0f;
        r->g_anchor = r->anchor;
        for (uint32_t v = 0; v < REV_REV_MAX_VOICES; ++v) {
            r->g_pos[v] = r->v_pos[v];
            r->g_env[v] = r->v_env[v];
        }
    }
    if (seg_len < 2u) seg_len = 2u;
    if (seg_len > r->buf_len) seg_len = r->buf_len; /* oversized segment cannot alias the buffer */
    /* live-overwrite guard: the write head keeps recording while the
       anchored read head sweeps the frozen segment. With seg_len beyond
       HALF the ring, live writes reach a not-yet-read position before the
       read head does (p > buf_len/2: overwrite at t = buf_len-p < p) and
       the playback picks up live junk = crackle. Clamp so it cannot happen. */
    if (seg_len > r->buf_len / 2u - 2u) seg_len = r->buf_len / 2u - 2u;
    r->seg_len = seg_len;
    r->cross_len = cross_samples < 1u ? 1u : cross_samples;
    if (r->cross_len > r->seg_len) r->cross_len = r->seg_len;
    r->cross_inc = 1.0f / (float)r->cross_len;
    if (r->pre_off > r->seg_len - 2u) r->pre_off = r->seg_len > 2u ? r->seg_len - 2u : 0u;
    /* swell rise bounded to ~0.35 s: long segments do not create a long
       pre-delay; env holds at 1.0 for the rest of the body, then falls in
       the crossfade */
    {
        uint32_t body = r->seg_len - r->cross_len; /* seg_len >= 2 and cross_len <= seg_len */
        uint32_t rise_time = (uint32_t)(int)(0.35f * r->sample_rate);
        if (rise_time < 1u) rise_time = 1u;
        uint32_t rise = body < rise_time ? body : rise_time;
        if (rise < 1u) rise = 1u;
        r->env_inc = 1.0f / (float)rise;
    }
    r->shape = (shape < 1) ? 1 : ((shape > 4) ? 4 : shape);
    /* normalization target 0.5 (was 0.9): the reverse layer + rev_gain
       restores the level; keeping the target lower bounds the worst-case
       wet (diffuser x drive x env) below the clip rail - loudness comes
       from the gain structure, not from hard clipping */
    r->norm_target = rev_clampf(0.5f / (r->seg_peak + 1e-6f), 0.1f, 3.0f);
    r->anchor = r->write_idx;
    uint32_t body = r->seg_len - r->cross_len;
    /* smooth retrigger: keep the envelope if a swell is already playing so a
       new onset re-anchors without a hard level reset (kills the wobble);
       the anchor content jump is masked by the retrigger ghost (armed above
       only when NOT fresh) */
    for (uint32_t v = 0; v < REV_REV_MAX_VOICES; ++v) {
        if (v < r->n_voices) {
            /* stagger via float math (v*seg_len < 2^24: exact in float32; the
               ZDL build has no 64-bit integer divide helper) */
            uint32_t pos = (uint32_t)(int)(((float)v * (float)r->seg_len) / (float)r->n_voices);
            r->v_pos[v] = pos;
            if (fresh) {
                if (pos >= body) {
                    uint32_t k = pos - body;
                    r->v_cross[v] = (float)k * r->cross_inc;
                    if (r->v_cross[v] > 1.0f) r->v_cross[v] = 1.0f;
                    r->v_env[v] = 0.0f;
                } else {
                    r->v_cross[v] = 0.0f;
                    r->v_env[v] = (float)pos * r->env_inc;
                    if (r->v_env[v] > 1.0f) r->v_env[v] = 1.0f;
                }
            }
            /* not fresh: keep v_env/v_cross and just re-anchor (smooth retrigger) */
        } else {
            r->v_pos[v] = 0u;
            r->v_env[v] = 0.0f;
            r->v_cross[v] = 0.0f;
        }
    }
}

void rev_rev_write(RevRev* r, float x) {
    r->buf[r->write_idx & r->mask] = x;
    r->write_idx++;
    r->seg_peak *= 0.9999f; /* peak-hold decay */
    float a = rev_absf(x);
    if (a > r->seg_peak) r->seg_peak = a;
}

float rev_rev_process(RevRev* r) {
    /* retrigger ghost: the OLD playback stream continues from its saved
       read heads (g_anchor + g_pos) and fades to 0 over ~12 ms while the
       freshly re-anchored segment fades in - a true content crossfade, so
       the anchor jump cannot step the output and no DC thump remains */
    float gf = 0.0f;
    if (r->g_fade > 0.0f) {
        gf = r->g_fade;
        r->g_fade -= r->g_fade_inc;
        if (r->g_fade < 0.0f) r->g_fade = 0.0f;
    }
    /* Read heads are anchored at trigger time (r->anchor = write_idx then), so
       live recording does not move them; at wrap each v_pos resets to 0 and the
       segment LOOPS the same anchored material until the next trigger
       re-anchors. */
    float sum = 0.0f;
    uint32_t body = r->seg_len - r->cross_len;
    for (uint32_t v = 0; v < r->n_voices; ++v) {
        uint32_t pos = r->v_pos[v];
        uint32_t read_idx = (r->anchor - 1u - r->pre_off - pos) & r->mask;
        float rev = r->buf[read_idx];

        float env = r->v_env[v];
        if (pos >= body) {
            /* crossfade window: blend toward the segment head and let the swell
               fall to 0 so the seam is click-free */
            uint32_t head_idx = (r->write_idx - 1u) & r->mask;
            float cp = r->v_cross[v];
            r->v_cross[v] += r->cross_inc;
            if (r->v_cross[v] > 1.0f) r->v_cross[v] = 1.0f;
            float head = r->buf[head_idx];
            rev = rev * (1.0f - cp) + head * cp;
            env = env * (1.0f - cp); /* fall to 0 at the seam */
        } else {
            /* segment body: swell rises toward 1 */
            r->v_env[v] += r->env_inc;
            if (r->v_env[v] > 1.0f) r->v_env[v] = 1.0f;
            env = r->v_env[v];
        }

        if (r->shape == 2) env = env * env;
        else if (r->shape == 3) env = env * env * env;
        else if (r->shape == 4) { float e2 = env * env; env = e2 * e2; }

        sum += rev * env;

        r->v_pos[v]++;
        if (r->v_pos[v] >= r->seg_len) {
            r->v_pos[v] = 0u;
            r->v_env[v] = 0.0f;
            r->v_cross[v] = 0.0f;
        }
    }

    r->norm_gain += (r->norm_target - r->norm_gain) * r->norm_coef;
    float out = sum * r->voice_scale * r->norm_gain;
    /* true content crossfade with the old stream: at the retrigger sample
       (gf=1) the output IS the old playback (continuous), then the new
       segment takes over linearly over ~12 ms */
    if (gf > 0.0f) {
        float ghost = 0.0f;
        for (uint32_t v = 0; v < r->n_voices; ++v) {
            uint32_t gidx = (r->g_anchor - 1u - r->pre_off - r->g_pos[v]) & r->mask;
            float grev = r->buf[gidx];
            float genv = r->g_env[v];
            if (r->shape == 2) genv = genv * genv;
            else if (r->shape == 3) genv = genv * genv * genv;
            else if (r->shape == 4) { float e2 = genv * genv; genv = e2 * e2; }
            ghost += grev * genv;
            r->g_pos[v]++;
            if (r->g_pos[v] >= r->seg_len) r->g_pos[v] = 0u;
        }
        out = out * (1.0f - gf) + (ghost * r->voice_scale) * gf;
    }
    return out;
}
