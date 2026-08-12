#include "rev_delay.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    float mem[64];
    RevDelay d;
    rev_delay_init(&d, mem, 64u);
    rev_delay_clear(&d);

    /* write a ramp 0..9, read back with delay 4 */
    float in[10] = {0,1,2,3,4,5,6,7,8,9};
    float out[10];
    for (int i = 0; i < 10; ++i) {
        rev_delay_write(&d, in[i]);
        out[i] = rev_delay_read(&d, 4u);
    }
    CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f && out[3] == 0.0f);
    CHECK(out[4] == 0.0f && out[5] == 1.0f && out[6] == 2.0f && out[7] == 3.0f && out[8] == 4.0f && out[9] == 5.0f);

    /* wrap: delay larger than what we wrote so far reads zeros */
    rev_delay_clear(&d);
    rev_delay_write(&d, 7.0f);
    CHECK(rev_delay_read(&d, 1u) == 0.0f);

    if (fails == 0) { printf("test_delay PASS\n"); return 0; }
    printf("test_delay FAILED (%d)\n", fails);
    return 1;
}
