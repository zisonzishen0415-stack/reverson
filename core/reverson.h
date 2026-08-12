#ifndef REVERSON_H
#define REVERSON_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REVERSON_NUM_PARAMS 13u
#define REVERSON_MAX_REV_S 2.0f

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
     rev   -> gate, shape, density      (wash -> gated reverse)
     space -> decay, revlen, mod, width (small -> huge)
     tone  -> tone, bass, sat           (dark -> bright)
     grain -> diffusion                 (grainy -> smooth)
     duck  -> duck (direct)
   All in [0,1]; curves shaped so any combination stays musical.
   ZDL-safe (polynomials only, no div/sin/pow). */
void Reverson_map6(float mix, float rev, float space, float tone,
                   float grain, float duck, ReversonParams* p);
#ifdef __cplusplus
}
#endif
#endif
