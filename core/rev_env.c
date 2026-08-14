#include "rev_env.h"
#include "rev_util.h"

void rev_env_init(RevEnv* e, float sample_rate) {
    e->env = 0.0f;
    e->attack_coef = rev_coeff_from_tc(0.002f * sample_rate);
    e->release_coef = rev_coeff_from_tc(0.15f * sample_rate);
    e->onset_env = 0.0f;
    e->onset_attack_coef = rev_coeff_from_tc(0.002f * sample_rate);
    e->onset_release_coef = rev_coeff_from_tc(0.01f * sample_rate);
    e->onset_thresh = 0.35f;      /* relative: 35% of the slow onset peak */
    e->onset_peak = 0.0f;
    e->onset_floor = 0.02f;       /* absolute floor (~ -34 dBFS): silence never triggers */
    e->was_playing = 0;
    e->onset = 0;
}

void rev_env_set_thresh(RevEnv* e, float rel) {
    e->onset_thresh = rev_clampf(rel, 0.0f, 1.0f);
}
void rev_env_process(RevEnv* e, float x) {
    float a = rev_absf(x);
    float coef = (a > e->env) ? e->attack_coef : e->release_coef;
    e->env += (a - e->env) * coef;
    float ocoef = (a > e->onset_env) ? e->onset_attack_coef : e->onset_release_coef;
    e->onset_env += (a - e->onset_env) * ocoef;
    /* slow peak of the onset envelope: the trigger threshold is RELATIVE to it,
       so the same notes trigger the same way at any input level (a pedal must
       behave identically for quiet and loud guitars) */
    if (e->onset_env > e->onset_peak) e->onset_peak = e->onset_env;
    else e->onset_peak *= 0.99999f;   /* ~1.6 s decay @44k1 */
    if (e->onset_peak < 1e-4f) e->onset_peak = 1e-4f;
    float th = e->onset_thresh * e->onset_peak;
    if (th < e->onset_floor) th = e->onset_floor;
    int playing = e->onset_env > th;
    e->onset = (playing && !e->was_playing) ? 1 : 0;
    e->was_playing = playing;
}
float rev_env_value(const RevEnv* e) { return e->env; }
int rev_env_onset(const RevEnv* e) { return e->onset; }
