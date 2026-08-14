#ifndef REV_ENV_H
#define REV_ENV_H

typedef struct {
    float env;                /* main envelope: duck level (2 ms attack / 150 ms release) */
    float attack_coef;
    float release_coef;
    float onset_env;          /* fast envelope: onset detection (2 ms attack / 10 ms release) */
    float onset_attack_coef;
    float onset_release_coef;
    float onset_thresh;       /* RELATIVE: fraction of the slow onset peak */
    float onset_peak;         /* slow peak of onset_env (level-independent trigger) */
    float onset_floor;        /* absolute floor so silence never triggers */
    int was_playing;
    int onset;                /* one-sample flag: consume per sample */
} RevEnv;

void rev_env_init(RevEnv* e, float sample_rate);
/* relative onset threshold in [0,1] (fraction of the slow onset peak);
   higher = harder to trigger. Clamped internally. */
void rev_env_set_thresh(RevEnv* e, float rel);
/* feed one input sample; updates envelope and onset flag */
void rev_env_process(RevEnv* e, float x);
float rev_env_value(const RevEnv* e);
/* one-sample flag, recomputed on every rev_env_process call: the caller must
   read it each sample (it is not latched until cleared) */
int rev_env_onset(const RevEnv* e);
#endif
