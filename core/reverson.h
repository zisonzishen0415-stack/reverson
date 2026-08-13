#ifndef REVERSON_H
#define REVERSON_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REVERSON_NUM_PARAMS 13u
#define REVERSON_MAX_REV_S 2.0f

/* Include the 8-line FDN bed in the state block? Default on (VST / A/B).
   The ZDL port builds with REVERSON_ENABLE_FDN=0: the bed is compiled out
   entirely and ~240 KB of ctx[3] memory is not reserved (the pure-reverse
   path is the shipped sound anyway). */
#ifndef REVERSON_ENABLE_FDN
#define REVERSON_ENABLE_FDN 1
#endif

typedef enum {
    REVERSON_PARAM_MIX = 0,
    REVERSON_PARAM_DECAY,
    REVERSON_PARAM_TONE,
    REVERSON_PARAM_REVLEN,
    REVERSON_PARAM_DUCK,
    REVERSON_PARAM_GATE,
    REVERSON_PARAM_SHAPE,
    REVERSON_PARAM_MOD,
    REVERSON_PARAM_SAT,
    REVERSON_PARAM_WIDTH,
    REVERSON_PARAM_DENSITY,
    REVERSON_PARAM_BASS,
    REVERSON_PARAM_DIFFUSION
} ReversonParam;

typedef struct {
    float mix, decay, tone, revlen, duck, gate, shape, mod, sat, width, density, bass, diffusion;
} ReversonParams;

typedef struct Reverson Reverson;

/* All memory is caller-provided; the core never allocates (ZDL-safe).
   mem must stay valid for the lifetime of the instance. */
uint32_t Reverson_state_size(float sample_rate);
Reverson* Reverson_init(void* mem, uint32_t mem_size, float sample_rate);
void Reverson_reset(Reverson* r);
void Reverson_set_param(Reverson* r, ReversonParam p, float v); /* v in [0,1] */
float Reverson_get_param(const Reverson* r, ReversonParam p);
/* process one mono sample -> stereo out */
void Reverson_process(Reverson* r, float in, float* out_l, float* out_r);

/* 6-knob mapping (two pages x 3). Every internal param is owned by exactly
   one knob so the knobs never fight:
     mix   -> wet (direct)
     rev   -> gate, shape, density      (wash -> gated reverse; a floor keeps
                                         the reverse alive even at rev=0)
     space -> revlen, decay, width      (small -> huge)
     tone  -> tone, bass, sat           (dark -> bright)
     grain -> diffusion, mod            (grainy/static -> smooth/flowing)
     duck  -> duck (direct)
   All in [0,1]; curves shaped so any combination stays musical.
   ZDL-safe (polynomials only, no div/sin/pow). */
void Reverson_map6(float mix, float rev, float space, float tone,
                   float grain, float duck, ReversonParams* p);
/* Convenience: map the 6 knobs and apply the whole set to an instance
   (smoothing applies). Shared by the VST and the ZDL port. */
void Reverson_set_6knob(Reverson* r, float mix, float rev, float space,
                        float tone, float grain, float duck);
/* FDN bed mix in [0,1]: 1 = continuous bed + reverse swell (current),
   0 = pure reverse swell (no continuous bed). A/B tuning aid; not in the
   6-knob UI yet. */
void Reverson_set_bed(Reverson* r, float bed);
#ifdef __cplusplus
}
#endif
#endif
