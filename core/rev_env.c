#include "rev_env.h"
#include "rev_util.h"

void rev_env_init(RevEnv* e, float sample_rate) {
    e->env = 0.0f;
    e->attack_coef = rev_coeff_from_tc(0.002f * sample_rate);
    e->release_coef = rev_coeff_from_tc(0.15f * sample_rate);
    e->onset_env = 0.0f;
    e->onset_attack_coef = rev_coeff_from_tc(0.002f * sample_rate);
    e->onset_release_coef = rev_coeff_from_tc(0.01f * sample_rate);
    e->onset_thresh = 0.01f;
    e->was_playing = 0;
    e->onset = 0;
}
void rev_env_process(RevEnv* e, float x) {
    float a = rev_absf(x);
    float coef = (a > e->env) ? e->attack_coef : e->release_coef;
    e->env += (a - e->env) * coef;
    float ocoef = (a > e->onset_env) ? e->onset_attack_coef : e->onset_release_coef;
    e->onset_env += (a - e->onset_env) * ocoef;
    int playing = e->onset_env > e->onset_thresh;
    e->onset = (playing && !e->was_playing) ? 1 : 0;
    e->was_playing = playing;
}
float rev_env_value(const RevEnv* e) { return e->env; }
int rev_env_onset(const RevEnv* e) { return e->onset; }
