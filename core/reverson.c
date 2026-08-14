#include "reverson.h"
#include "rev_util.h"
#include "rev_env.h"
#include "rev_fdn.h"
#include "rev_swell.h"
#include <string.h>

#if REVERSON_ENABLE_FDN
#define REV_FDN_TOTAL_SAMPLES 61440u   /* 2048+4096+8192+16384 x2 (L/R sets) */
#endif

struct Reverson {
    float sample_rate;
    ReversonParams target;
    ReversonParams cur;
    float smooth_coef;
    RevEnv env;
#if REVERSON_ENABLE_FDN
    RevFdn fdn;
#endif
    RevSwell swell;
    float bed;                /* FDN bed mix: 1 = bed+swell, 0 = pure swell */
    float duck_gain_sm;
    float env_peak;             /* slow peak of input env for level-independent duck/gate */
    float wet_lp_l, wet_lp_r;
    float wet_bl_l, wet_bl_r;   /* bass low-shelf one-pole state */
    /* reverse-gate envelope (classic SPX90/Alesis reverse reverb):
       the wet amplitude swells then cuts with ZERO predelay. Attack is
       ACCELERATING (quadratic, DP/4-style): starts slow, rushes to the peak.
       `shape` blends linear (0) -> accelerating (1). Value-based so a
       re-trigger during the fall resumes rising from the current level
       (no dip -> no "wobble"). */
    float rev_env;
    uint32_t rev_state;         /* 0=idle(floor), 1=rising, 2=settling */
    float rev_env_inc;          /* current rise increment (grows each sample) */
    float rev_env_acc;          /* rise acceleration (shape-controlled) */
    float rev_fall_inc;
    uint32_t rev_last_trigger;
    uint32_t sample_count;
};

uint32_t Reverson_state_size(float sample_rate) {
    (void)sample_rate;   /* fixed-size state (ZDL-style caller memory) */
    uint32_t extra = REV_SWELL_BUF_LEN + REV_SWELL_DIFF_TOTAL;
#if REVERSON_ENABLE_FDN
    extra += REV_FDN_TOTAL_SAMPLES;
#endif
    return (uint32_t)(sizeof(Reverson) + extra * sizeof(float) + 256u);
}

Reverson* Reverson_init(void* mem, uint32_t mem_size, float sample_rate) {
    uint32_t need = Reverson_state_size(sample_rate);
    if (mem == NULL || mem_size < need) return NULL;
    Reverson* r = (Reverson*)mem;
    memset(r, 0, sizeof(Reverson));
    r->sample_rate = sample_rate;
    float* p = (float*)((uint8_t*)mem + sizeof(Reverson));
    rev_env_init(&r->env, sample_rate);
#if REVERSON_ENABLE_FDN
    {
        static const uint32_t fdn_pow2[REV_FDN_LINES] = {
            2048, 4096, 8192, 16384,
            2048, 4096, 8192, 16384
        };
        rev_fdn_init(&r->fdn, p, fdn_pow2, sample_rate);
    }
    rev_swell_init(&r->swell, p + REV_FDN_TOTAL_SAMPLES, REV_SWELL_BUF_LEN,
                   p + REV_FDN_TOTAL_SAMPLES + REV_SWELL_BUF_LEN, REV_SWELL_DIFF_LEN, sample_rate);
#else
    rev_swell_init(&r->swell, p, REV_SWELL_BUF_LEN,
                   p + REV_SWELL_BUF_LEN, REV_SWELL_DIFF_LEN, sample_rate);
#endif

    /* defaults tuned toward DIIV-style clean+spacious */
    r->target.mix = 0.55f;
    r->target.decay = 0.60f;
    r->target.tone = 0.60f;
    r->target.revlen = 0.40f;
    r->target.duck = 0.50f;
    r->target.gate = 0.00f;
    r->target.shape = 0.33f;
    r->target.mod = 0.35f;
    r->target.sat = 0.10f;
    r->target.width = 0.80f;
    r->target.density = 0.75f;   /* swell hold time */
    r->target.bass = 0.55f;      /* slight low-mid body */
    r->target.diffusion = 0.30f; /* swell diffuser feedback: 0 = sharp reverse, ~0.7 = dense/smooth */
    r->target.trig = 0.5f;
    r->target.predelay = 0.0f;
    r->cur = r->target;
    r->smooth_coef = rev_coeff_from_tc(0.005f * sample_rate);
    r->duck_gain_sm = 1.0f;
    r->bed = 0.0f;      /* pure reverse swell by default; bed adds the FDN bed */
    r->env_peak = 0.0f;
    r->rev_env = 1.0f;
    r->rev_state = 0u;
    r->rev_env_inc = 0.0f;
    r->rev_env_acc = 0.0f;
    r->rev_last_trigger = 0u;
    r->sample_count = 0u;
    Reverson_reset(r);
    return r;
}

void Reverson_reset(Reverson* r) {
#if REVERSON_ENABLE_FDN
    rev_fdn_clear(&r->fdn);
#endif
    rev_swell_clear(&r->swell);
    r->env.env = 0.0f;
    r->env.onset_env = 0.0f;
    r->env.onset = 0;
    r->env.was_playing = 0;
    r->duck_gain_sm = 1.0f;
    r->bed = 0.0f;      /* pure reverse swell by default; bed adds the FDN bed */
    r->env_peak = 0.0f;
    r->rev_env = 1.0f;
    r->rev_state = 0u;
    r->rev_env_inc = 0.0f;
    r->rev_env_acc = 0.0f;
    r->wet_lp_l = 0.0f;
    r->wet_lp_r = 0.0f;
    r->wet_bl_l = 0.0f;
    r->wet_bl_r = 0.0f;
}

void Reverson_set_bed(Reverson* r, float bed) {
#if REVERSON_ENABLE_FDN
    r->bed = rev_clampf(bed, 0.0f, 1.0f);
#else
    (void)bed;   /* FDN compiled out: the bed is always 0 */
    r->bed = 0.0f;
#endif
}

/* 5-position mode switch: the same tables the pedal page-3 switch uses.
   Order: mix decay tone revlen duck gate shape mod sat width density bass diffusion */
static const float REV_MODE_TABLES[5][13] = {
    /* Wash */
    { 0.60f, 0.85f, 0.45f, 0.45f, 0.35f, 0.25f, 0.50f, 0.30f, 0.15f, 0.90f, 0.95f, 0.55f, 0.35f },
    /* Reverse */
    { 0.80f, 0.75f, 0.40f, 0.40f, 0.30f, 0.65f, 0.70f, 0.30f, 0.15f, 0.90f, 0.80f, 0.55f, 0.15f },
    /* Gated */
    { 0.70f, 0.70f, 0.45f, 0.25f, 0.20f, 0.90f, 0.50f, 0.30f, 0.15f, 0.85f, 0.35f, 0.55f, 0.20f },
    /* Shoegaze */
    { 0.80f, 0.85f, 0.40f, 0.45f, 0.45f, 0.45f, 0.60f, 0.30f, 0.15f, 0.90f, 0.85f, 0.55f, 0.30f },
    /* Space */
    { 0.85f, 1.00f, 0.35f, 0.60f, 0.10f, 0.20f, 0.50f, 0.35f, 0.12f, 0.95f, 1.00f, 0.60f, 0.50f }
};

void Reverson_mode(int mode, ReversonParams* p) {
    if (mode < 1) return;
    if (mode > 5) mode = 5;
    const float* t = REV_MODE_TABLES[mode - 1];
    p->mix = t[0]; p->decay = t[1]; p->tone = t[2]; p->revlen = t[3];
    p->duck = t[4]; p->gate = t[5]; p->shape = t[6]; p->mod = t[7];
    p->sat = t[8]; p->width = t[9]; p->density = t[10]; p->bass = t[11];
    p->diffusion = t[12];
}

float Reverson_test_env(const Reverson* r) { return r->rev_env; }

void Reverson_set_param(Reverson* r, ReversonParam p, float v) {
    v = rev_clampf(v, 0.0f, 1.0f);
    switch (p) {
        case REVERSON_PARAM_MIX:   r->target.mix = v; break;
        case REVERSON_PARAM_DECAY: r->target.decay = v; break;
        case REVERSON_PARAM_TONE:  r->target.tone = v; break;
        case REVERSON_PARAM_REVLEN:r->target.revlen = v; break;
        case REVERSON_PARAM_DUCK:  r->target.duck = v; break;
        case REVERSON_PARAM_GATE:  r->target.gate = v; break;
        case REVERSON_PARAM_SHAPE: r->target.shape = v; break;
        case REVERSON_PARAM_MOD:   r->target.mod = v; break;
        case REVERSON_PARAM_SAT:   r->target.sat = v; break;
        case REVERSON_PARAM_WIDTH: r->target.width = v; break;
        case REVERSON_PARAM_DENSITY:r->target.density = v; break;
        case REVERSON_PARAM_BASS:   r->target.bass = v; break;
        case REVERSON_PARAM_DIFFUSION:r->target.diffusion = v; break;
        case REVERSON_PARAM_TRIG:     r->target.trig = v; break;
        case REVERSON_PARAM_PREDELAY: r->target.predelay = v; break;
    }
}

float Reverson_get_param(const Reverson* r, ReversonParam p) {
    switch (p) {
        case REVERSON_PARAM_MIX:   return r->target.mix;
        case REVERSON_PARAM_DECAY: return r->target.decay;
        case REVERSON_PARAM_TONE:  return r->target.tone;
        case REVERSON_PARAM_REVLEN:return r->target.revlen;
        case REVERSON_PARAM_DUCK:  return r->target.duck;
        case REVERSON_PARAM_GATE:  return r->target.gate;
        case REVERSON_PARAM_SHAPE: return r->target.shape;
        case REVERSON_PARAM_MOD:   return r->target.mod;
        case REVERSON_PARAM_SAT:   return r->target.sat;
        case REVERSON_PARAM_WIDTH: return r->target.width;
        case REVERSON_PARAM_DENSITY:return r->target.density;
        case REVERSON_PARAM_BASS:   return r->target.bass;
        case REVERSON_PARAM_DIFFUSION:return r->target.diffusion;
        case REVERSON_PARAM_TRIG:     return r->target.trig;
        case REVERSON_PARAM_PREDELAY: return r->target.predelay;
    }
    return 0.0f;
}

void Reverson_map6(float mix, float rev, float space, float tone,
                   float grain, float duck, ReversonParams* p) {
    mix = rev_clampf(mix, 0.0f, 1.0f);
    rev = rev_clampf(rev, 0.0f, 1.0f);
    space = rev_clampf(space, 0.0f, 1.0f);
    tone = rev_clampf(tone, 0.0f, 1.0f);
    grain = rev_clampf(grain, 0.0f, 1.0f);
    duck = rev_clampf(duck, 0.0f, 1.0f);

    /* REV knob: wash -> gated reverse. gate eases in with a FLOOR so rev=0
       is a subtle always-present wash (never a dead dry patch in pure mode);
       shape accelerates the attack, density (swell hold) shortens as the
       reverse gets harder. */
    float gate    = 0.12f + 0.78f * (0.35f * rev + 0.65f * rev * rev);
    float shape   = 0.45f + 0.30f * rev;
    float dens    = 0.95f - 0.50f * rev;

    /* SPACE knob: small -> huge. revlen is the swell span (pure mode), decay
       is the FDN bed tail (used when the bed is on), width opens up. */
    float revlen  = 0.25f + 0.50f * space;
    float decay   = 0.45f + 0.55f * space;
    float width   = 0.55f + 0.45f * space;

    /* TONE knob: dark -> bright; bass shelf drops and saturation creeps in
       as it brightens so it never gets harsh-thin. */
    float tparam  = 0.12f + 0.88f * tone;
    float bass    = 0.70f - 0.42f * tone;
    float sat     = 0.08f + 0.22f * tone;

    /* GRAIN knob: grainy/sharp/static -> smooth/dense/flowing. diffusion is
       the main smear, mod adds a slow living LFO to the swell taps (the
       "delay vs reverb" axis - both belong together here). */
    float diff    = 0.10f + 0.60f * grain;
    float mod     = 0.10f + 0.40f * grain;

    p->mix = mix;
    p->decay = decay;
    p->tone = tparam;
    p->revlen = revlen;
    p->duck = duck;
    p->gate = gate;
    p->shape = shape;
    p->mod = mod;
    p->sat = sat;
    p->width = width;
    p->density = dens;
    p->bass = bass;
    p->diffusion = diff;
    p->trig = 0.5f;
    p->predelay = 0.0f;
}

void Reverson_map3(float c, float s, float t, ReversonParams* p) {
    c = rev_clampf(c, 0.0f, 1.0f);
    s = rev_clampf(s, 0.0f, 1.0f);
    t = rev_clampf(t, 0.0f, 1.0f);
    /* CHAR axis: wash -> shoegaze -> gated reverse */
    float gate    = 0.90f * (0.35f * c + 0.65f * c * c);
    float diff    = 0.55f - 0.40f * c;
    float shape   = 0.45f + 0.30f * c;
    float dens    = 0.95f - 0.50f * c;
    float duck    = 0.15f + 0.55f * (4.0f * c * (1.0f - c));
    /* SPACE axis */
    float decay   = 0.45f + 0.55f * s;
    float revlen  = 0.25f + 0.50f * s;
    float mod     = 0.20f + 0.25f * s;
    float width   = 0.75f + 0.20f * s;
    float mix     = 0.55f + 0.35f * s;
    /* TONE axis */
    float tone    = 0.12f + 0.88f * t;
    float bass    = 0.70f - 0.42f * t;
    float sat     = 0.08f + 0.22f * t;

    p->mix = mix;
    p->decay = decay;
    p->tone = tone;
    p->revlen = revlen;
    p->duck = duck;
    p->gate = gate;
    p->shape = shape;
    p->mod = mod;
    p->sat = sat;
    p->width = width;
    p->density = dens;
    p->bass = bass;
    p->diffusion = diff;
    p->trig = 0.5f;
    p->predelay = 0.0f;
}

void Reverson_set_6knob(Reverson* r, float mix, float rev, float space,
                        float tone, float grain, float duck) {
    ReversonParams p;
    Reverson_map6(mix, rev, space, tone, grain, duck, &p);
    Reverson_set_param(r, REVERSON_PARAM_MIX, p.mix);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, p.decay);
    Reverson_set_param(r, REVERSON_PARAM_TONE, p.tone);
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, p.revlen);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, p.duck);
    Reverson_set_param(r, REVERSON_PARAM_GATE, p.gate);
    Reverson_set_param(r, REVERSON_PARAM_SHAPE, p.shape);
    Reverson_set_param(r, REVERSON_PARAM_MOD, p.mod);
    Reverson_set_param(r, REVERSON_PARAM_SAT, p.sat);
    Reverson_set_param(r, REVERSON_PARAM_WIDTH, p.width);
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, p.density);
    Reverson_set_param(r, REVERSON_PARAM_BASS, p.bass);
    Reverson_set_param(r, REVERSON_PARAM_DIFFUSION, p.diffusion);
}

void Reverson_process(Reverson* r, float in, float* out_l, float* out_r) {
    float c = r->smooth_coef;
    r->cur.mix       = rev_smooth(r->cur.mix,       r->target.mix,       c);
    r->cur.decay     = rev_smooth(r->cur.decay,     r->target.decay,     c);
    r->cur.tone      = rev_smooth(r->cur.tone,      r->target.tone,      c);
    r->cur.revlen    = rev_smooth(r->cur.revlen,    r->target.revlen,    c);
    r->cur.duck      = rev_smooth(r->cur.duck,      r->target.duck,      c);
    r->cur.gate      = rev_smooth(r->cur.gate,      r->target.gate,      c);
    r->cur.shape     = rev_smooth(r->cur.shape,     r->target.shape,     c);
    r->cur.mod       = rev_smooth(r->cur.mod,       r->target.mod,       c);
    r->cur.sat       = rev_smooth(r->cur.sat,       r->target.sat,       c);
    r->cur.width     = rev_smooth(r->cur.width,     r->target.width,     c);
    r->cur.density   = rev_smooth(r->cur.density,   r->target.density,   c);
    r->cur.bass      = rev_smooth(r->cur.bass,      r->target.bass,      c);
    r->cur.diffusion = rev_smooth(r->cur.diffusion, r->target.diffusion, c);

    r->sample_count++;
    rev_env_process(&r->env, in);

    /* reverse swell envelope with a FLOOR: the reverb is always present
       (floor), and each onset blooms it to 1 over revlen then settles back to
       the floor. No hard gate -> no 'reverb absent' gaps, no sudden blasts. */
    float rev_floor = 1.0f - 0.65f * r->cur.gate;
    if (rev_floor < 0.2f) rev_floor = 0.2f;
    if (rev_env_onset(&r->env) && r->cur.gate > 0.01f) {
        uint32_t min_gap = (uint32_t)(0.02f * r->sample_rate);
        if (r->sample_count - r->rev_last_trigger >= min_gap) {
            uint32_t rise = (uint32_t)((0.05f + 1.95f * r->cur.revlen) * r->sample_rate);
            if (rise < 2u) rise = 2u;
            uint32_t release = (uint32_t)((0.30f + 0.70f * r->cur.density) * r->sample_rate);
            if (release < 2u) release = 2u;
            float inv_rise = 1.0f / (float)rise;
            float a = r->cur.shape;   /* 0 linear, 1 accelerating (quadratic) */
            float span;
            if (r->rev_state == 0u) {
                span = 1.0f - rev_floor;
            } else {
                /* already rising or settling: re-plan the remaining rise from
                   the current level, so every note gets a full swell and a
                   fast strum never dips back to the floor (no wobble). */
                span = 1.0f - r->rev_env;
                if (span < 1e-4f) span = 1e-4f;
            }
            r->rev_env_inc = span * inv_rise * (1.0f - a);
            r->rev_env_acc = 2.0f * span * inv_rise * inv_rise * a;
            r->rev_fall_inc = (1.0f - rev_floor) / (float)release;
            r->rev_state = 1u;
            r->rev_last_trigger = r->sample_count;
        }
    }

    /* step the swell envelope (accelerating attack: inc grows every sample) */
    if (r->cur.gate > 0.01f) {
        if (r->rev_state == 1u) {
            r->rev_env += r->rev_env_inc;
            r->rev_env_inc += r->rev_env_acc;
            if (r->rev_env >= 1.0f) { r->rev_env = 1.0f; r->rev_state = 2u; }
        } else if (r->rev_state == 2u) {
            r->rev_env -= r->rev_fall_inc;
            if (r->rev_env <= rev_floor) { r->rev_env = rev_floor; r->rev_state = 0u; }
        }
        /* state 0: env stays at the floor (reverb always present) */
    } else {
        r->rev_env = 1.0f;
        r->rev_state = 0u;
    }

    float env = rev_env_value(&r->env);
    /* level-independent dynamics: duck uses env relative to a slow peak */
    if (env > r->env_peak) r->env_peak = env;
    else r->env_peak *= 0.99997f;   /* ~0.65 s decay @44k1 */
    if (r->env_peak < 1e-4f) r->env_peak = 1e-4f;
    float env_n = env / r->env_peak;
    if (env_n > 1.0f) env_n = 1.0f;
    float duck_gain = 1.0f - r->cur.duck * env_n;
    if (duck_gain < 0.0f) duck_gain = 0.0f;
    if (duck_gain > 1.0f) duck_gain = 1.0f;
    r->duck_gain_sm += (duck_gain - r->duck_gain_sm) * c;
    float wet_in = in * r->duck_gain_sm;

    /* FDN bed (optional, default off): when bed is ~0 the 8-line network is
       skipped entirely (CPU/memory saver - the pure reverse path is the
       default sound). With REVERSON_ENABLE_FDN=0 this whole block compiles
       out and the bed is always 0. */
    float wet_l = 0.0f, wet_r = 0.0f;
#if REVERSON_ENABLE_FDN
    if (r->bed > 0.001f) {
        rev_fdn_set(&r->fdn, r->cur.decay, r->cur.tone, r->cur.mod);
        float fdn_l, fdn_r;
        rev_fdn_process(&r->fdn, wet_in, &fdn_l, &fdn_r);
        wet_l = fdn_l * r->bed;
        wet_r = fdn_r * r->bed;
    }
#endif

    /* SPX90-style multi-head swell: gate = swell amount
       (0 -> dry pass-through in pure-reverse mode, 1 -> full reverse). */
    float sw_l, sw_r;
    rev_swell_set(&r->swell, r->cur.revlen, r->cur.gate);
    rev_swell_set_mod(&r->swell, r->cur.mod);   /* living tail on the taps */
    /* Diffusion knob: diffuser feedback, 0 = sharp reverse gate .. ~0.7 = dense */
    {
        float dfb = r->cur.diffusion;
        if (dfb > 0.7f) dfb = 0.7f;
        r->swell.diff_fb[0] = dfb;
        r->swell.diff_fb[1] = dfb * 0.9f;
        r->swell.diff_fb[2] = dfb * 0.8f;
    }
    rev_swell_process(&r->swell, wet_in, &sw_l, &sw_r);
    wet_l += sw_l;
    wet_r += sw_r;

    float tc = 0.05f + 0.9f * r->cur.tone;
    r->wet_lp_l += (wet_l - r->wet_lp_l) * tc;
    r->wet_lp_r += (wet_r - r->wet_lp_r) * tc;
    wet_l = r->wet_lp_l;
    wet_r = r->wet_lp_r;

    /* bass low-shelf: y = x + g*lp(x); g = (bass-0.5)*1.2 in [-0.6,+0.6], ~300 Hz */
    {
        float btc = 0.04f;
        r->wet_bl_l += (wet_l - r->wet_bl_l) * btc;
        r->wet_bl_r += (wet_r - r->wet_bl_r) * btc;
        float shelf_g = (r->cur.bass - 0.5f) * 1.2f;
        wet_l += shelf_g * r->wet_bl_l;
        wet_r += shelf_g * r->wet_bl_r;
    }

    float drive = 1.0f + 3.0f * r->cur.sat;
    wet_l = rev_softclip(wet_l * drive);
    wet_r = rev_softclip(wet_r * drive);

    float mid = (wet_l + wet_r) * 0.5f;
    float side = (wet_l - wet_r) * 0.5f;
    wet_l = mid + side * r->cur.width;
    wet_r = mid - side * r->cur.width;

    /* reverse gate on the reverb OUTPUT: the whole wet swells then cuts */
    wet_l *= r->rev_env;
    wet_r *= r->rev_env;

    float mix = r->cur.mix;
    *out_l = in * (1.0f - mix) + wet_l * mix;
    *out_r = in * (1.0f - mix) + wet_r * mix;
}
