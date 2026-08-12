/* render_demo.c - offline Reverson demo renderer.
 * Reads a 16-bit PCM WAV (mono or stereo), normalizes, pre-warms the
 * reverse buffer with one full pass, then renders N loop passes through the
 * Reverson core. Writes stereo 16-bit WAVs:
 *   <prefix>_dry.wav
 *   <prefix>_<preset>.wav   for each preset (or one named preset)
 *
 * usage: reverson_render <in.wav> <out_prefix> [loops] [preset] [cold]
 *   loops  - number of loop passes (default 2)
 *   preset - one of: wet, big, only, wash, wash_wet, rev_fat, dense, steady (default: all)
 *
 * Build: linked against reverson_core (see tools/CMakeLists.txt).
 */
#include "reverson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_SAMPLES (16u * 1024u * 1024u) /* 16M samples headroom (~6 min at 44k1) */

typedef struct {
    unsigned rate;
    float* mono;
    unsigned n;
} Audio;

typedef struct {
    const char* name;
    float mix, decay, tone, revlen, duck, gate, shape, mod, sat, width, density, bass;
} Preset;

static const Preset PRESETS[] = {
    { "wet",      0.55f, 0.60f, 0.60f, 0.40f, 0.50f, 0.00f, 0.33f, 0.35f, 0.10f, 0.80f, 0.75f, 0.55f },
    { "big",      0.75f, 0.75f, 0.55f, 0.85f, 0.10f, 0.00f, 0.60f, 0.50f, 0.15f, 0.95f, 0.80f, 0.60f },
    { "only",     1.00f, 0.80f, 0.50f, 0.80f, 0.10f, 0.00f, 0.60f, 0.50f, 0.15f, 0.90f, 0.85f, 0.60f },
    { "wash",     0.60f, 0.80f, 0.40f, 0.28f, 0.75f, 0.00f, 0.66f, 0.60f, 0.08f, 0.85f, 0.90f, 0.65f },
    { "wash_wet", 0.80f, 0.85f, 0.40f, 0.35f, 0.35f, 0.00f, 0.60f, 0.60f, 0.12f, 0.90f, 0.90f, 0.65f },
    { "rev_fat",  0.85f, 0.92f, 0.35f, 0.50f, 0.40f, 0.00f, 0.50f, 0.75f, 0.25f, 0.90f, 0.95f, 0.75f },
    { "dense",    0.85f, 0.90f, 0.35f, 0.45f, 0.40f, 0.00f, 0.50f, 0.75f, 0.25f, 0.90f, 1.00f, 0.75f },
    { "steady",  0.85f, 0.88f, 0.38f, 0.45f, 0.10f, 0.00f, 0.50f, 0.60f, 0.18f, 0.90f, 1.00f, 0.70f },
};
#define NUM_PRESETS ((unsigned)(sizeof(PRESETS) / sizeof(PRESETS[0])))

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
            if (tag != 1 || bits != 16) { fprintf(stderr, "%s: only PCM16 supported (tag=%u bits=%u)\n", path, tag, bits); fclose(f); return -1; }
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
    if (n == 0 || n > MAX_SAMPLES) { fprintf(stderr, "%s: too many frames (%u)\n", path, n); fclose(f); return -1; }
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

static int write_wav(const char* path, const float* l, const float* r, unsigned n, unsigned rate) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return -1; }
    unsigned data_bytes = n * 4u;
    unsigned riff = 36u + data_bytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    unsigned fmt_sz = 16, tag = 1, ch = 2, bits = 16;
    unsigned block = ch * (bits / 8u), bps = rate * block;
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_sz, 4, 1, f);
    fwrite(&tag, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&bps, 4, 1, f);
    fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data_bytes, 4, 1, f);
    for (unsigned i = 0; i < n; ++i) {
        short sl = (short)(l[i] * 32767.0f);
        short sr = (short)(r[i] * 32767.0f);
        fwrite(&sl, 2, 1, f); fwrite(&sr, 2, 1, f);
    }
    fclose(f);
    return 0;
}

static void apply_preset(Reverson* r, const Preset* p) {
    Reverson_set_param(r, REVERSON_PARAM_MIX, p->mix);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, p->decay);
    Reverson_set_param(r, REVERSON_PARAM_TONE, p->tone);
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, p->revlen);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, p->duck);
    Reverson_set_param(r, REVERSON_PARAM_GATE, p->gate);
    Reverson_set_param(r, REVERSON_PARAM_SHAPE, p->shape);
    Reverson_set_param(r, REVERSON_PARAM_MOD, p->mod);
    Reverson_set_param(r, REVERSON_PARAM_SAT, p->sat);
    Reverson_set_param(r, REVERSON_PARAM_WIDTH, p->width);
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, p->density);
    Reverson_set_param(r, REVERSON_PARAM_BASS, p->bass);
}

static float peak_of(const float* x, unsigned n) {
    float p = 0.0f;
    for (unsigned i = 0; i < n; ++i) { float v = x[i] < 0 ? -x[i] : x[i]; if (v > p) p = v; }
    return p;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: reverson_render <in.wav> <out_prefix> [loops] [preset] [cold]\n");
        return 1;
    }
    Audio a;
    if (read_wav(argv[1], &a) != 0) return 1;
    unsigned loops = argc > 3 ? (unsigned)atoi(argv[3]) : 2u;
    if (loops < 1) loops = 1;
    const char* filter = argc > 4 ? argv[4] : NULL;
    int cold = (argc > 5 && strcmp(argv[5], "cold") == 0);
    if (cold) loops = 1;

    /* normalize to peak 0.25 */
    float src_peak = peak_of(a.mono, a.n);
    if (src_peak > 1e-6f) {
        float g = 0.25f / src_peak;
        for (unsigned i = 0; i < a.n; ++i) a.mono[i] *= g;
    }

    uint32_t need = Reverson_state_size((float)a.rate);
    unsigned total = a.n * loops;
    float* dryL = (float*)malloc(total * sizeof(float));
    float* L = (float*)malloc(total * sizeof(float));
    float* R = (float*)malloc(total * sizeof(float));
    if (!dryL || !L || !R) { fprintf(stderr, "alloc failed\n"); return 1; }
    for (unsigned p = 0; p < loops; ++p)
        for (unsigned i = 0; i < a.n; ++i) dryL[p * a.n + i] = a.mono[i];
    float dry_peak = peak_of(dryL, total);
    float norm = 0.89f / dry_peak;

    char path[1024];
    snprintf(path, sizeof(path), "%s_dry.wav", argv[2]);
    write_wav(path, dryL, dryL, total, a.rate);
    {
        float drms = 0.0f;
        for (unsigned i = 0; i < total; ++i) drms += dryL[i] * dryL[i];
        drms = (float)sqrt(drms / (float)total);
        printf("dry: peak=%.3f rms=%.3f -> %s\n", dry_peak * norm, drms, path);
    }

    for (unsigned pi = 0; pi < NUM_PRESETS; ++pi) {
        const Preset* pr = &PRESETS[pi];
        if (filter && strcmp(filter, pr->name) != 0) continue;
        void* mem = malloc(need);
        if (!mem) { fprintf(stderr, "alloc failed\n"); return 1; }
        Reverson* core = Reverson_init(mem, need, (float)a.rate);
        if (!core) { fprintf(stderr, "init failed\n"); return 1; }
        apply_preset(core, pr);

        /* pre-warm reverse buffer (discard output) unless cold (live-take
           simulation starts from an empty buffer) */
        if (!cold) {
            for (unsigned i = 0; i < a.n; ++i) {
                float l, r; Reverson_process(core, a.mono[i], &l, &r);
            }
        }
        /* render */
        unsigned out = 0;
        for (unsigned p = 0; p < loops; ++p)
            for (unsigned i = 0; i < a.n; ++i) {
                float l, r;
                Reverson_process(core, a.mono[i], &l, &r);
                L[out] = l; R[out] = r; ++out;
            }
        for (unsigned i = 0; i < out; ++i) { L[i] *= norm; R[i] *= norm; }
        snprintf(path, sizeof(path), "%s_%s.wav", argv[2], pr->name);
        write_wav(path, L, R, out, a.rate);
        float rms = 0.0f;
        for (unsigned i = 0; i < out; ++i) { float m = (L[i] + R[i]) * 0.5f; rms += m * m; }
        rms = (float)sqrt(rms / (float)out);
        printf("%-8s: peak=%.3f rms=%.3f -> %s\n", pr->name, peak_of(L, out), rms, path);
        free(mem);
    }

    free(dryL); free(L); free(R); free(a.mono);
    return 0;
}
