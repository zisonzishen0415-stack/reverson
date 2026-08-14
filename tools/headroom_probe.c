/* headroom_probe.c - raw headroom + discontinuity probe for Reverson.
 * Feeds a WAV through the core with NO output normalization and reports:
 *   - true output peak (|L|,|R|) and how many samples exceed 0.999 (0 dBFS)
 *   - max per-sample delta and how many deltas exceed 0.05 (click detector)
 *   - RMS
 * Optional 'sweep': ramps the rev knob 0->1->0 over the file while the
 * space knob ramps the other way - exposes 8-sample-grid gain stepping.
 * Optional 'mode=N' (1..5): applies the mode's character params after the
 * knobs (mix/duck stay knob-owned, like the plugin).
 *
 * usage: headroom_probe <in.wav> <mix> <rev> <space> <tone> <grain> <duck>
 *         [trig] [predelay] [sweep|mode=N]
 * Build: linked against reverson_core (see tools/CMakeLists.txt).
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_SAMPLES (16u * 1024u * 1024u)

typedef struct {
    unsigned rate;
    float* mono;
    unsigned n;
} Audio;

static int read_wav(const char* path, Audio* a) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "%s: not a RIFF/WAVE file\n", path); fclose(f); return -1;
    }
    unsigned rate = 0, ch = 0, bits = 0, data_len = 0;
    int found = 0;
    for (;;) {
        char ck[8];
        if (fread(ck, 1, 8, f) != 8) break;
        unsigned sz = (unsigned char)ck[4] | ((unsigned char)ck[5] << 8) | ((unsigned char)ck[6] << 16) | ((unsigned char)ck[7] << 24);
        if (memcmp(ck, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            unsigned got = (unsigned)fread(fmt, 1, sz < 16 ? sz : 16, f);
            if (sz < 16 || got < 16) break;
            unsigned tag = fmt[0] | (fmt[1] << 8);
            ch = fmt[2] | (fmt[3] << 8);
            rate = (unsigned)fmt[4] | ((unsigned)fmt[5] << 8) | ((unsigned)fmt[6] << 16) | ((unsigned)fmt[7] << 24);
            bits = fmt[14] | (fmt[15] << 8);
            if (tag != 1 || bits != 16) { fprintf(stderr, "%s: only PCM16 supported\n", path); fclose(f); return -1; }
            if (sz > 16 && fseek(f, (long)(sz - 16), SEEK_CUR) != 0) break;
        } else if (memcmp(ck, "data", 4) == 0) {
            data_len = sz; found = 1; break;
        } else {
            if (fseek(f, (long)sz, SEEK_CUR) != 0) break;
        }
    }
    if (!found || ch == 0 || ch > 2 || rate == 0) {
        fprintf(stderr, "%s: bad WAV structure\n", path); fclose(f); return -1;
    }
    unsigned n = data_len / (ch * 2u);
    if (n == 0 || n > MAX_SAMPLES) { fprintf(stderr, "%s: too many frames\n", path); fclose(f); return -1; }
    float* mono = (float*)malloc(n * sizeof(float));
    if (!mono) { fclose(f); return -1; }
    for (unsigned i = 0; i < n; ++i) {
        short l, r;
        if (fread(&l, 2, 1, f) != 1) { n = i; break; }
        if (ch == 2) {
            if (fread(&r, 2, 1, f) != 1) { n = i; break; }
            mono[i] = ((float)l + (float)r) * 0.5f / 32768.0f;
        } else {
            mono[i] = (float)l / 32768.0f;
        }
    }
    fclose(f);
    a->rate = rate; a->mono = mono; a->n = n;
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 8) {
        fprintf(stderr, "usage: headroom_probe <in.wav> <mix> <rev> <space> <tone> <grain> <duck> [trig] [predelay] [sweep]\n");
        return 1;
    }
    Audio a;
    if (read_wav(argv[1], &a) != 0) return 1;
    float mix = (float)atof(argv[2]), rev = (float)atof(argv[3]), space = (float)atof(argv[4]);
    float tone = (float)atof(argv[5]), grain = (float)atof(argv[6]), duck = (float)atof(argv[7]);
    float trig = argc > 8 ? (float)atof(argv[8]) : 0.35f;
    float predelay = argc > 9 ? (float)atof(argv[9]) : 0.0f;
    int sweep = 0;
    int mode = 0;
    if (argc > 10) {
        if (strcmp(argv[10], "sweep") == 0) sweep = 1;
        else if (sscanf(argv[10], "mode=%d", &mode) == 1) { /* mode 1..5 */ }
    }

    uint32_t need = Reverson_state_size((float)a.rate);
    void* mem = malloc(need);
    if (!mem) { fprintf(stderr, "alloc failed\n"); return 1; }
    Reverson* core = Reverson_init(mem, need, (float)a.rate);
    if (!core) { fprintf(stderr, "init failed\n"); return 1; }
    Reverson_set_6knob(core, mix, rev, space, tone, grain, duck);
    Reverson_set_param(core, REVERSON_PARAM_TRIG, trig);
    Reverson_set_param(core, REVERSON_PARAM_PREDELAY, predelay);
    if (mode >= 1) {   /* mode owns the 11 character params (mix/duck knob-owned) */
        ReversonParams mp;
        Reverson_mode(mode, &mp);
        Reverson_set_param(core, REVERSON_PARAM_DECAY, mp.decay);
        Reverson_set_param(core, REVERSON_PARAM_TONE, mp.tone);
        Reverson_set_param(core, REVERSON_PARAM_REVLEN, mp.revlen);
        Reverson_set_param(core, REVERSON_PARAM_GATE, mp.gate);
        Reverson_set_param(core, REVERSON_PARAM_SHAPE, mp.shape);
        Reverson_set_param(core, REVERSON_PARAM_MOD, mp.mod);
        Reverson_set_param(core, REVERSON_PARAM_SAT, mp.sat);
        Reverson_set_param(core, REVERSON_PARAM_WIDTH, mp.width);
        Reverson_set_param(core, REVERSON_PARAM_DENSITY, mp.density);
        Reverson_set_param(core, REVERSON_PARAM_BASS, mp.bass);
        Reverson_set_param(core, REVERSON_PARAM_DIFFUSION, mp.diffusion);
    }

    /* converge the param grid before measuring */
    {
        float l, r;
        for (unsigned i = 0; i < a.rate; ++i) Reverson_process(core, 0.0f, &l, &r);
    }

    float pkL = 0.0f, pkR = 0.0f, rms = 0.0f;
    unsigned over = 0, first_over = 0;
    float maxD = 0.0f;
    unsigned maxd_pos = 0u;
    unsigned clicks = 0, first_click = 0;
    float prevL = 0.0f, prevR = 0.0f;
    unsigned click_pos[20];
    float click_d[20];
    unsigned n_pos = 0u;
    static float ring[64];   /* 40 samples around the first click */
    static float ring_in[64];
    float maxd_ring[64];
    unsigned ring_i = 0u;
    int dumped = 0;
    for (unsigned i = 0; i < a.n; ++i) {
        float l, r;
        if (sweep) {
            float ph = (float)i / (float)a.n;            /* 0..1 over the file */
            float rv = ph < 0.5f ? 2.0f * ph : 2.0f * (1.0f - ph);  /* 0->1->0 */
            Reverson_set_6knob(core, mix, rv, 1.0f - ph, tone, grain, duck);
        }
        Reverson_process_stereo(core, a.mono[i], a.mono[i], &l, &r);
        ring[ring_i & 63u] = l;
        ring_in[ring_i & 63u] = a.mono[i];
        ring_i++;
        float al = rev_absf(l), ar = rev_absf(r);
        if (al > pkL) pkL = al;
        if (ar > pkR) pkR = ar;
        if ((al > 0.999f || ar > 0.999f) && over == 0u) first_over = i;
        if (al > 0.999f) over++;
        if (ar > 0.999f) over++;
        float dL = rev_absf(l - prevL), dR = rev_absf(r - prevR);
        if (dL > maxD) { maxD = dL; maxd_pos = i; for (int k = 0; k < 64; ++k) maxd_ring[k] = ring[k]; }
        if (dR > maxD) { maxD = dR; maxd_pos = i; for (int k = 0; k < 64; ++k) maxd_ring[k] = ring[k]; }
        if ((dL > 0.05f || dR > 0.05f) && clicks == 0u) first_click = i;
        if (dL > 0.05f) clicks++;
        if (dR > 0.05f) clicks++;
        if (n_pos < 20u && (dL > 0.05f || dR > 0.05f)) {
            click_pos[n_pos] = i;
            click_d[n_pos] = dL > dR ? dL : dR;
            n_pos++;
            if (!dumped) {
                dumped = 1;
                printf("waveform around click #1 (i=%u):\n", i);
                for (int k = -30; k <= 10; ++k) {
                    unsigned idx = (i + (unsigned)k) & 63u;
                    printf("  %+5d in=%.4f out=%.4f\n", k, ring_in[idx], ring[idx]);
                }
            }
        }
        prevL = l; prevR = r;
        float m = (l + r) * 0.5f;
        rms += m * m;
    }
    rms = (float)sqrt(rms / (float)a.n);
    {
        printf("waveform around max delta (i=%u):\n", maxd_pos);
        for (int k = -30; k <= 10; ++k) {
            unsigned idx = (maxd_pos + (unsigned)k) & 63u;
            printf("  %+5d in=%.4f out=%.4f\n", k, ring_in[idx], maxd_ring[idx]);
        }
    }
    printf("in=%s n=%u rate=%u sweep=%s\n", argv[1], a.n, a.rate, sweep ? "yes" : "no");
    printf("peak  L=%.4f R=%.4f  (0 dBFS = 1.0000)\n", pkL, pkR);
    printf("over  0dBFS: %u samples (first at %u)\n", over, first_over);
    printf("max |delta|: %.4f at sample %u  clicks(>0.05): %u (first at %u)\n", maxD, maxd_pos, clicks, first_click);
    printf("rms: %.4f\n", rms);
    if (n_pos > 0u) {
        printf("first clicks: ");
        for (unsigned k = 0; k < n_pos; ++k) printf("%u(%.3f) ", click_pos[k], click_d[k]);
        printf("\n");
    }
    free(mem); free(a.mono);
    return 0;
}
