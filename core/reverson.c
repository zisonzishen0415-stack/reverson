#include "reverson.h"
#include "rev_util.h"
#include "rev_env.h"
#include "rev_fdn.h"
#include <string.h>

struct Reverson {
    float sample_rate;
    ReversonParams target;
    ReversonParams cur;
    float smooth_coef;
    RevEnv env;
    RevFdn fdn;
    float duck_gain_sm;
    float env_peak;             /* slow peak of input env for level-independent duck/gate */
    float wet_lp_l, wet_lp_r;
    float wet_bl_l, wet_bl_r;   /* bass low-shelf one-pole state */
    /* reverse-gate envelope (classic SPX90/Alesis reverse reverb):
       the FDN tail's amplitude envelope is reversed - on each onset the wet
       swells 0->1 over revlen, holds, then cuts. Soft->loud->cut with ZERO
       predelay (unlike reversed-audio playback, which lags a whole segment). */
    float rev_env;
    uint32_t rev_state;         /* 0=idle(floor), 1=rising, 2=settling */
    float rev_env_inc;
    float rev_fall_inc;
    uint32_t rev_last_trigger;
    uint32_t sample_count;
};

uint32_t Reverson_state_size(float sample_rate) {
    uint32_t fdn_total = 2048u + 4096u + 8192u + 16384u +
                         2048u + 4096u + 8192u + 16384u;   /* = 61440 */
    return (uint32_t)(sizeof(Reverson) + fdn_total * sizeof(float) + 64u);
}

Reverson* Reverson_init(void* mem, uint32_t mem_size, float sample_rate) {
    uint32_t need = Reverson_state_size(sample_rate);
    if (mem == NULL || mem_size < need) return NULL;
    Reverson* r = (Reverson*)mem;
    memset(r, 0, sizeof(Reverson));
    r->sample_rate = sample_rate;
    float* p = (float*)((uint8_t*)mem + sizeof(Reverson));
    rev_env_init(&r->env, sample_rate);
    {
        static const uint32_t fdn_pow2[REV_FDN_LINES] = {
            2048, 4096, 8192, 16384,
            2048, 4096, 8192, 16384
        };
        rev_fdn_init(&r->fdn, p, fdn_pow2, sample_rate);
    }

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
    r->cur = r->target;
    r->smooth_coef = rev_coeff_from_tc(0.005f * sample_rate);
    r->duck_gain_sm = 1.0f;
    r->env_peak = 0.0f;
    r->rev_env = 1.0f;
    r->rev_state = 0u;
    r->rev_last_trigger = 0u;
    r->sample_count = 0u;
    Reverson_reset(r);
    return r;
}

void Reverson_reset(Reverson* r) {
    rev_fdn_clear(&r->fdn);
    r->env.env = 0.0f;
    r->env.onset_env = 0.0f;
    r->env.onset = 0;
    r->env.was_playing = 0;
    r->duck_gain_sm = 1.0f;
    r->env_peak = 0.0f;
    r->rev_env = 1.0f;
    r->rev_state = 0u;
    r->wet_lp_l = 0.0f;
    r->wet_lp_r = 0.0f;
    r->wet_bl_l = 0.0f;
    r->wet_bl_r = 0.0f;
}

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
    }
    return 0.0f;
}

void Reverson_process(Reverson* r, float in, float* out_l, float* out_r) {
    float c = r->smooth_coef;
    r->cur.mix += (r->target.mix - r->cur.mix) * c;
    r->cur.decay += (r->target.decay - r->cur.decay) * c;
    r->cur.tone += (r->target.tone - r->cur.tone) * c;
    r->cur.revlen += (r->target.revlen - r->cur.revlen) * c;
    r->cur.duck += (r->target.duck - r->cur.duck) * c;
    r->cur.gate += (r->target.gate - r->cur.gate) * c;
    r->cur.shape += (r->target.shape - r->cur.shape) * c;
    r->cur.mod += (r->target.mod - r->cur.mod) * c;
    r->cur.sat += (r->target.sat - r->cur.sat) * c;
    r->cur.width += (r->target.width - r->cur.width) * c;
    r->cur.density += (r->target.density - r->cur.density) * c;
    r->cur.bass += (r->target.bass - r->cur.bass) * c;

    r->sample_count++;
    rev_env_process(&r->env, in);

    /* reverse swell envelope with a FLOOR: the reverb is always present
       (floor), and each onset blooms it 0->1 over revlen then settles back to
       the floor. No hard gate -> no 'reverb absent' gaps, no sudden blasts. */
    float rev_floor = 1.0f - 0.65f * r->cur.gate;
    if (rev_floor < 0.2f) rev_floor = 0.2f;
    if (rev_env_onset(&r->env) && r->cur.gate > 0.01f) {
        uint32_t min_gap = (uint32_t)(0.02f * r->sample_rate);
        if (r->sample_count - r->rev_last_trigger >= min_gap) {
            if (r->rev_state == 0u) {
                uint32_t rise = (uint32_t)((0.05f + 1.95f * r->cur.revlen) * r->sample_rate);
                if (rise < 2u) rise = 2u;
                uint32_t release = (uint32_t)((0.30f + 0.70f * r->cur.density) * r->sample_rate);
                if (release < 2u) release = 2u;
                r->rev_env_inc = (1.0f - rev_floor) / (float)rise;
                r->rev_fall_inc = (1.0f - rev_floor) / (float)release;
                r->rev_state = 1u;
            } else if (r->rev_state == 2u) {
                /* settling: resume rising from the current level (no dip) */
                r->rev_state = 1u;
            }
            r->rev_last_trigger = r->sample_count;
        }
    }

    /* step the swell envelope */
    if (r->cur.gate > 0.01f) {
        if (r->rev_state == 1u) {
            r->rev_env += r->rev_env_inc;
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

    rev_fdn_set(&r->fdn, r->cur.decay, r->cur.tone, r->cur.mod);
    float wet_l, wet_r;
    rev_fdn_process(&r->fdn, wet_in, &wet_l, &wet_r);

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
