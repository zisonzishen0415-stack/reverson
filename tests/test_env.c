#include "rev_env.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    RevEnv e;
    rev_env_init(&e, 44100.0f);

    /* silence -> envelope stays 0 */
    for (int i = 0; i < 100; ++i) rev_env_process(&e, 0.0f);
    CHECK(rev_env_value(&e) == 0.0f);
    CHECK(rev_env_onset(&e) == 0);

    /* a step to 1.0 must raise the envelope and fire exactly one onset */
    int onsets = 0;
    for (int i = 0; i < 4410; ++i) {
        rev_env_process(&e, 1.0f);
        onsets += rev_env_onset(&e);
    }
    CHECK(rev_env_value(&e) > 0.9f);
    CHECK(onsets == 1);

    /* input drops to 0 -> envelope releases (slowly) */
    rev_env_process(&e, 0.0f);
    CHECK(rev_env_value(&e) < 1.0f);

    if (fails == 0) { printf("test_env PASS\n"); return 0; }
    printf("test_env FAILED (%d)\n", fails);
    return 1;
}