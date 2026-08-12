#ifndef REV_ENV_H
#define REV_ENV_H

typedef struct {
    float env;                /* main envelope: duck/gate level (2 ms attack / 150 ms release) */
    float attack_coef;
    float release_coef;
    float onset_env;          /* fast envelope: onset detection (2 ms attack / 10 ms release) */
    float onset_attack_coef;
    float onset_release_coef;
    float onset_thresh;
    int was_playing;
    int onset;                /* one-sample flag: consume per sample */
} RevEnv;

void rev_env_init(RevEnv* e, float sample_rate);
/* feed one input sample; updates envelope and onset flag */
void rev_env_process(RevEnv* e, float x);
float rev_env_value(const RevEnv* e);
/* one-sample flag, recomputed on every rev_env_process call: the caller must
   read it each sample (it is not latched until cleared) */
int rev_env_onset(const RevEnv* e);
#endif
