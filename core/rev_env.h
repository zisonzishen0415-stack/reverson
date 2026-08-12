#ifndef REV_ENV_H
#define REV_ENV_H

typedef struct {
    float env;
    float attack_coef;
    float release_coef;
    float onset_thresh;
    int was_playing;
    int onset;
} RevEnv;

void rev_env_init(RevEnv* e, float sample_rate);
/* feed one input sample; updates envelope and onset flag */
void rev_env_process(RevEnv* e, float x);
float rev_env_value(const RevEnv* e);
int rev_env_onset(const RevEnv* e);
#endif