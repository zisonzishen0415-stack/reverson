#include "reverson.h"
#include "rev_util.h"
#include "rev_env.h"
#include "rev_rev.h"
#include "rev_fdn.h"
#include <string.h>

struct Reverson {
    float sample_rate;
    ReversonParams target;
    ReversonParams cur;
    float smooth_coef;
    RevEnv env;
    RevRev rev;
    RevFdn fdn;
    float duck_gain_sm;
    float gate_gain_sm;         /* output gate smoother (gated reverse) */
    float env_peak;             /* slow peak of input env for level-independent duck/gate */
    float wet_lp_l, wet_lp_r;
    float wet_bl_l, wet_bl_r;   /* bass low-shelf one-pole state */
    uint32_t rev_len_max;
    uint32_t rev_cross_samples;
};

uint32_t Reverson_state_size(float sample_rate) {
    uint32_t rev_pow2 = rev_next_pow2((uint32_t)(sample_rate * 2.2f));
    uint32_t fdn_total = 2048u + 4096u + 8192u + 16384u +
                         2048u + 4096u + 8192u + 16384u;   /* = 61440 */
    return (uint32_t)(sizeof(Reverson) + rev_pow2 * sizeof(float) + fdn_total * sizeof(float) + 64u);
}

Reverson* Reverson_init(void* mem, uint32_t mem_size, float sample_rate) {
    uint32_t need = Reverson_state_size(sample_rate);
    if (mem == NULL || mem_size < need) return NULL;
    Reverson* r = (Reverson*)mem;
    memset(r, 0, sizeof(Reverson));
    r->sample_rate = sample_rate;
    r->rev_len_max = (uint32_t)(sample_rate * REVERSON_MAX_REV_S);
    uint32_t rev_pow2 = rev_next_pow2((uint32_t)(sample_rate * 2.2f));
    float* p = (float*)((uint8_t*)mem + sizeof(Reverson));
    rev_env_init(&r->env, sample_rate);
    rev_rev_init(&r->rev, p, rev_pow2, sample_rate);
    p += rev_pow2;
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
    r->target.density = 0.75f;   /* 3-voice reverse wash */
    r->target.bass = 0.55f;      /* slight low-mid body */
    r->cur = r->target;
    r->smooth_coef = rev_coeff_from_tc(0.005f * sample_rate);
    r->duck_gain_sm = 1.0f;
    r->gate_gain_sm = 1.0f;
    r->rev_cross_samples = (uint32_t)(0.004f * sample_rate);
    Reverson_reset(r);
    return r;
}

void Reverson_reset(Reverson* r) {
    rev_rev_clear(&r->rev);
    rev_fdn_clear(&r->fdn);
    r->env.env = 0.0f;
    r->env.onset_env = 0.0f;
    r->env.onset = 0;
    r->env.was_playing = 0;
    r->duck_gain_sm = 1.0f;
    r->gate_gain_sm = 1.0f;
    r->env_peak = 0.0f;
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

    rev_env_process(&r->env, in);
    if (rev_env_onset(&r->env)) {
        uint32_t seg_len = (uint32_t)((0.05f + 1.95f * r->cur.revlen) * r->sample_rate);
        if (seg_len > r->rev_len_max) seg_len = r->rev_len_max;
        int sh = 1 + (int)(3.99f * r->cur.shape);
        uint32_t voices = 1u + (uint32_t)(3.99f * r->cur.density);   /* 1..4 */
        rev_rev_set_voices(&r->rev, voices);
        rev_rev_trigger(&r->rev, seg_len, r->rev_cross_samples, sh);
    }
    rev_rev_write(&r->rev, in);
    float rev_sig = rev_rev_process(&r->rev);

    float env = rev_env_value(&r->env);
    /* level-independent dynamics: duck/gate use env relative to a slow peak,
       so the same settings behave the same at any input level (pedal reality) */
    if (env > r->env_peak) r->env_peak = env;
    else r->env_peak *= 0.99997f;   /* ~0.65 s decay @44k1 */
    if (r->env_peak < 1e-4f) r->env_peak = 1e-4f;
    float env_n = env / r->env_peak;
    if (env_n > 1.0f) env_n = 1.0f;
    float duck_gain = 1.0f - r->cur.duck * env_n;
    if (duck_gain < 0.0f) duck_gain = 0.0f;
    if (duck_gain > 1.0f) duck_gain = 1.0f;
    r->duck_gain_sm += (duck_gain - r->duck_gain_sm) * c;
    float wet_in = rev_sig * r->duck_gain_sm;

    rev_fdn_set(&r->fdn, r->cur.decay, r->cur.tone, r->cur.mod);
    float wet_l, wet_r;
    rev_fdn_process(&r->fdn, wet_in, &wet_l, &wet_r);

    float tc = 0.05f + 0.9f * r->cur.tone;
    r->wet_lp_l += (wet_l - r->wet_lp_l) * tc;
    r->wet_lp_r += (wet_r - r->wet_lp_r) * tc;
    wet_l = r->wet_lp_l;
    wet_r = r->wet_lp_r;

    /* bass low-shelf: y = x + g*lp(x); shelf gain g = (bass-0.5)*1.2 in [-0.6,+0.6].
       One-pole lp at tc=0.04 (~300 Hz @44k1) puts body in the low-mid region. */
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

    /* gate on the reverb OUTPUT: the whole wet (reverse swell + FDN tail)
       sounds only in the gaps, and is cut while the source is playing.
       That is the classic gated-reverse character (input-side gating only
       stopped new feed and let the tail ring over the notes -> smear). */
    {
        float gate_gain = 1.0f;
        if (r->cur.gate > 0.01f) {
            float th = 0.05f + 0.5f * r->cur.gate;
            gate_gain = (env_n < th) ? 1.0f : 0.0f;
        }
        float gtc = rev_coeff_from_tc(0.02f * r->sample_rate); /* ~20 ms, click-free */
        r->gate_gain_sm += (gate_gain - r->gate_gain_sm) * gtc;
        wet_l *= r->gate_gain_sm;
        wet_r *= r->gate_gain_sm;
    }

    float mix = r->cur.mix;
    *out_l = in * (1.0f - mix) + wet_l * mix;
    *out_r = in * (1.0f - mix) + wet_r * mix;
}
