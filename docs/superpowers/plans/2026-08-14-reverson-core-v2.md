# Reverson Core v2 Implementation Plan (M1 + M2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline execution; the user authorized autonomous execution and waived review gates).

**Goal:** Upgrade the Reverson core per `docs/superpowers/specs/2026-08-14-reverson-core-v2-design.md` (M1: core DSP + tests; M2: VST + render presets), with the Zoom G1on/ZDL platform as the acceptance standard.

**Architecture:** The dormant, already-tested `core/rev_rev.c` (RevRev) reverse-playback engine is integrated into `Reverson_process` as the "reverse layer" — its buffer shares memory with the existing 32768-sample swell line (same write cadence + same content, so the sharing is safe and adds **zero** large-buffer memory). `rev_mix = clamp((gate-0.12)*1.282, 0, 1)` fades the forward 13-tap layer out as the reverse layer takes over (high `Rev` = pure reverse + gate, and the tap loop early-outs = CPU win). The diffuser gains L/R crossfeed, the wet path gains a fixed ~110 Hz HPF, the reverse envelope becomes a 5-state machine (rise->1+over, settle->1, hold, fall->floor), triggering gains sensitivity + predelay, all params smooth on an 8-sample grid, and a stereo API preserves the dry stereo image.

**Tech stack:** C99 core (no deps, ZDL-safe: mul/add only in the audio path), CTest, JUCE 7.0.12 VST3, MSVC 2022 + CMake.

**Spec refinement (documented deviation):** the spec named a new `core/rev_reverse.c` module; the codebase already contains the dormant, unit-tested `RevRev` engine in `core/rev_rev.c`, so the plan integrates it instead (better reuse, honors the "lighter" constraint). The `Over` param is fully curve-owned (`over = 0.16*shape`), so `REVERSON_NUM_PARAMS` becomes 15 (13 + Trig + Predelay), not 16.

---

## File map

| File | Action | Responsibility |
|---|---|---|
| `core/rev_rev.h/.c` | modify | add `pre_off` skip (attack-transient skip) |
| `core/rev_swell.h/.c` | modify | split taps/diffuse API, mod=0 fast path, amount early-out, L/R crossfeed |
| `core/rev_env.h/.c` | modify | `rev_env_set_thresh` |
| `core/reverson.h` | modify | 15 params, `Reverson_process_stereo`, `Reverson_mode`, test hook |
| `core/reverson.c` | modify (large) | v2 process body: 8-sample smoothing grid, 5-state envelope, trigger sensitivity+predelay, RevRev integration, HPF, stereo |
| `tests/test_rev.c` | modify | pre_off tests |
| `tests/test_swell.c` | modify | taps/diffuse split equivalence, mod fast path |
| `tests/test_env.c` | modify | set_thresh tests |
| `tests/test_core.c` | modify | 15-param loops, mode table tests, env state-machine checks |
| `tests/test_crossfeed.c` | create | crossfeed bounds/decorrelation |
| `tests/test_hpf.c` | create | wet HPF behavioral test |
| `tests/test_stereo.c` | create | stereo API exactness |
| `tests/test_trigger.c` | create | sensitivity/predelay/hold/grid determinism |
| `tests/test_v2reverse.c` | create | integrated reverse layer causality/boundedness |
| `CMakeLists.txt` | modify | register new tests |
| `tools/render_demo.c` | modify | trig/predelay keys, mbv/diiv/slowdive knob presets, `Reverson_mode` |
| `vst/PluginProcessor.{h,cpp}` | modify | mode/trig/predelay params, stereo process |
| `vst/PluginEditor.{h,cpp}` | modify | 3-page editor |
| `README.md` | modify | v2 docs |

Build/test commands (run from repo root, Debug):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

---

## Task 1: RevRev pre-offset (`pre_off`)

The reverse layer must skip the newest `pre_off` samples (the new note's attack transient) so the reversed playback starts on the pre-onset tail.

**Files:**
- Modify: `core/rev_rev.h:63` (struct), `core/rev_rev.c`
- Test: `tests/test_rev.c`

- [ ] **Step 1: Write the failing tests** — append to `tests/test_rev.c` before the final `if (fails == 0)` block:

```c
    /* pre_off: the read head skips the newest pre_off samples (the attack
       transient); content comes from the pre-onset tail instead. */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);   /* record 0..9 */
    rev_rev_set_preoff(&r, 2u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float po0 = rev_rev_process(&r);   /* buf[anchor-1-2] = 7, env 0.1 */
    CLOSE(po0, 0.7f, 0.15f);
    /* pre_off clamps into the segment: oversized value stays finite */
    rev_rev_set_preoff(&r, 100u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float po1 = rev_rev_process(&r);
    CHECK(is_finite_f(po1));
    /* pre_off back to 0: original first-sample behavior is unchanged */
    rev_rev_set_preoff(&r, 0u);
    rev_rev_trigger(&r, 10u, 1u, 1);
    float po2 = rev_rev_process(&r);
    CLOSE(po2, 0.9f, 0.15f);
```

- [ ] **Step 2: Run the failing tests**

Run: `cmake --build build --config Debug` then `ctest --test-dir build -C Debug -R test_rev --output-on-failure`
Expected: compile FAIL — `rev_rev_set_preoff` not declared.

- [ ] **Step 3: Implement** — in `core/rev_rev.h` add the field after `cross_inc`:

```c
    float cross_inc;
    uint32_t pre_off;  /* newest samples to skip at the segment head (attack transient) */
    float voice_scale;
```

add the declaration after `rev_rev_set_voices`:

```c
/* samples to skip at the segment head (0..seg_len-2, clamped in trigger);
   applied at the next rev_rev_trigger. Default 0 (original behavior). */
void rev_rev_set_preoff(RevRev* r, uint32_t samples);
```

in `core/rev_rev.c`:
- in `rev_rev_init`, add after `r->cross_inc = 1.0f;`: `r->pre_off = 0u;`
- add after `rev_rev_set_voices`:

```c
void rev_rev_set_preoff(RevRev* r, uint32_t samples) {
    r->pre_off = samples;
}
```

- in `rev_rev_trigger`, after the `cross_len` clamping add:

```c
    if (r->pre_off > r->seg_len - 2u) r->pre_off = r->seg_len > 2u ? r->seg_len - 2u : 0u;
```

- in `rev_rev_process`, change the read-index line from

```c
        uint32_t read_idx = (r->anchor - 1u - pos) & r->mask;
```

to

```c
        uint32_t read_idx = (r->anchor - 1u - r->pre_off - pos) & r->mask;
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug -R "test_rev|test_swell|test_core" --output-on-failure`
Expected: all PASS (existing RevRev behavior is unchanged at pre_off=0).

- [ ] **Step 5: Commit**

```
git add core/rev_rev.h core/rev_rev.c tests/test_rev.c
git commit -m "core: RevRev pre_off skip (attack transient) for the reverse layer"
```

---

## Task 2: rev_swell split taps/diffuse + mod fast path + early-out + crossfeed

**Files:**
- Modify: `core/rev_swell.h`, `core/rev_swell.c`
- Test: `tests/test_swell.c`, create `tests/test_crossfeed.c`, modify `CMakeLists.txt`

- [ ] **Step 1: Write the failing tests** — append to `tests/test_swell.c` before the final block:

```c
    /* split API: taps + diffuse composition equals the single-call process
       on twin instances (bit-exact: deterministic engines, same inputs) */
    {
        float mem2[REV_SWELL_BUF_LEN];
        float diff_mem2[3u * 2u * REV_SWELL_DIFF_LEN];
        RevSwell a, b;
        rev_swell_init(&a, mem, REV_SWELL_BUF_LEN, diff_mem, REV_SWELL_DIFF_LEN, 44100.0f);
        rev_swell_init(&b, mem2, REV_SWELL_BUF_LEN, diff_mem2, REV_SWELL_DIFF_LEN, 44100.0f);
        rev_swell_set(&a, 0.7f, 0.8f);
        rev_swell_set(&b, 0.7f, 0.8f);
        rev_swell_set_mod(&a, 0.6f);
        rev_swell_set_mod(&b, 0.6f);
        float exact_diff = 0.0f;
        for (int i = 0; i < 5000; ++i) {
            float x = (i % 100 == 0) ? 0.5f : 0.0f;
            float al, ar, bl, br;
            rev_swell_taps(&a, x, &al, &ar);
            rev_swell_diffuse(&a, al, ar, &al, &ar);
            rev_swell_process(&b, x, &bl, &br);
            exact_diff += rev_absf(al - bl) + rev_absf(ar - br);
            CHECK(is_finite_f(al) && is_finite_f(ar));
        }
        CHECK(exact_diff == 0.0f);
    }

    /* mod=0 fast path: the LFO phase must not advance, so mod=0 twice with an
       intervening mod=1 run is bit-identical to two pure mod=0 runs */
    {
        enum { M2 = 8000 };
        float a[M2], b[M2];
        for (int pass = 0; pass < 2; ++pass) {
            float* o = (pass == 0) ? a : b;
            rev_swell_clear(&s);
            rev_swell_set(&s, 1.0f, 1.0f);
            rev_swell_set_mod(&s, 0.0f);
            rev_swell_process(&s, 1.0f, &l, &r);
            o[0] = l + r;
            for (int i = 1; i < M2; ++i) {
                rev_swell_process(&s, 0.0f, &l, &r);
                o[i] = l + r;
            }
            if (pass == 0) {   /* exercise mod=1 in between; phase must not leak */
                rev_swell_clear(&s);
                rev_swell_set_mod(&s, 1.0f);
                for (int i = 0; i < 100; ++i) rev_swell_process(&s, 0.0f, &l, &r);
            }
        }
        float md = 0.0f;
        for (int i = 0; i < M2; ++i) md += rev_absf(a[i] - b[i]);
        CHECK(md == 0.0f);
    }
```

Create `tests/test_crossfeed.c`:

```c
/* test_crossfeed.c - L/R crossfeed in the diffusion chain: energy bound,
 * decorrelation, determinism, no NaN. */
#include "rev_swell.h"
#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

int main(void) {
    static float mem[REV_SWELL_BUF_LEN];
    static float diff_mem[3u * 2u * REV_SWELL_DIFF_LEN];
    RevSwell s;
    rev_swell_init(&s, mem, REV_SWELL_BUF_LEN, diff_mem, REV_SWELL_DIFF_LEN, 44100.0f);

    /* crossfeed is driven by diff_fb (the diffusion param); sweep the whole
       range with a dense impulse train: output stays finite and bounded */
    const float fbs[5] = {0.0f, 0.2f, 0.4f, 0.6f, 0.7f};
    for (int f = 0; f < 5; ++f) {
        float dfb = fbs[f];
        rev_swell_clear(&s);
        rev_swell_set(&s, 0.8f, 1.0f);
        rev_swell_set_mod(&s, 0.3f);
        s.diff_fb[0] = dfb;
        s.diff_fb[1] = dfb * 0.9f;
        s.diff_fb[2] = dfb * 0.8f;
        float peak = 0.0f;
        float l, r;
        for (int i = 0; i < 44100; ++i) {
            rev_swell_process(&s, (i % 100 == 0) ? 0.8f : 0.0f, &l, &r);
            CHECK(is_finite_f(l) && is_finite_f(r));
            if (rev_absf(l) > peak) peak = rev_absf(l);
            if (rev_absf(r) > peak) peak = rev_absf(r);
        }
        CHECK(peak < 1.0f);   /* crossfeed + allpasses stay unit-bounded */
    }

    /* crossfeed couples the channels: with an L-only excitation the R channel
       must respond (energy leaks across) - and both stay finite */
    rev_swell_clear(&s);
    rev_swell_set(&s, 0.6f, 1.0f);
    rev_swell_set_mod(&s, 0.0f);
    s.diff_fb[0] = 0.6f; s.diff_fb[1] = 0.54f; s.diff_fb[2] = 0.48f;
    {
        float l, r, r_energy = 0.0f;
        for (int i = 0; i < 22050; ++i) {
            rev_swell_process(&s, (i == 0) ? 1.0f : 0.0f, &l, &r);
            r_energy += r * r;
            CHECK(is_finite_f(l) && is_finite_f(r));
        }
        CHECK(r_energy > 1e-4f);   /* R is fed by the crossfeed, not zero */
    }

    if (fails == 0) { printf("test_crossfeed PASS\n"); return 0; }
    printf("test_crossfeed FAILED (%d)\n", fails);
    return 1;
}
```

- [ ] **Step 2: Register the new test** — in `CMakeLists.txt` add `reverson_add_test(test_crossfeed)` after the `reverson_add_test(test_swell)` line.

- [ ] **Step 3: Run to verify failure**

Run: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64; cmake --build build --config Debug`
Expected: compile FAIL — `rev_swell_taps` / `rev_swell_diffuse` not declared.

- [ ] **Step 4: Implement** — in `core/rev_swell.h`, replace the `rev_swell_process` declaration with:

```c
/* Split API: taps (writes the line, sums the heads) and diffuse (allpass
   chain + L/R crossfeed) are separate so callers can blend another source
   (the reverse layer) into the diffuser input. rev_swell_process remains as
   the composed single call. */
void rev_swell_taps(RevSwell* s, float in, float* out_l, float* out_r);
void rev_swell_diffuse(RevSwell* s, float l, float r, float* out_l, float* out_r);
void rev_swell_process(RevSwell* s, float in, float* out_l, float* out_r);
```

In `core/rev_swell.c`, replace the body of `rev_swell_process` (lines 102-145) with:

```c
void rev_swell_taps(RevSwell* s, float in, float* out_l, float* out_r) {
    rev_delay_write(&s->line, in);
    float l = 0.0f, r = 0.0f;
    if (s->amount >= 1e-3f) {
        if (s->mod_depth < 1e-3f) {
            /* static taps: no per-tap LFO math (CPU fast path) */
            for (int i = 0; i < REV_SWELL_TAPS; ++i) {
                float d = (float)s->base_delay[i] * s->scale;
                if (d >= (float)(s->line.len - 1u)) d = (float)(s->line.len - 1u);
                float v = rev_delay_read_frac(&s->line, d);
                l += v * (s->base_gain_l[i] * s->amount);
                r += v * (s->base_gain_r[i] * s->amount);
            }
        } else {
            for (int i = 0; i < REV_SWELL_TAPS; ++i) {
                float ph = s->lfo_phase + s->lfo_off[i];
                if (ph > 1.0f) ph -= 1.0f;
                float tri = 1.0f - 4.0f * rev_absf(ph - 0.5f);
                float t = tri * 0.5f + 0.5f;
                float shift = s->mod_depth * t * (float)s->base_delay[i];
                float d = (float)s->base_delay[i] * s->scale + shift;
                if (d >= (float)(s->line.len - 1u)) d = (float)(s->line.len - 1u);
                if (d < 0.0f) d = 0.0f;
                float v = rev_delay_read_frac(&s->line, d);
                l += v * (s->base_gain_l[i] * s->amount);
                r += v * (s->base_gain_r[i] * s->amount);
            }
            s->lfo_phase += s->lfo_inc;
            if (s->lfo_phase > 1.0f) s->lfo_phase -= 1.0f;
        }
    }
    *out_l = l;
    *out_r = r;
}

void rev_swell_diffuse(RevSwell* s, float l, float r, float* out_l, float* out_r) {
    /* feedback diffusion (Freeverb-style allpass) with per-stage L/R
       crossfeed: the channels smear against each other -> wider, denser tail */
    for (int st = 0; st < 3; ++st) {
        for (int ch = 0; ch < 2; ++ch) {
            RevDelay* d = &s->diff[st][ch];
            float x = (ch == 0) ? l : r;
            float bufout = rev_delay_read(d, s->diff_d[st]);
            float y = -x + bufout;
            rev_delay_write(d, x + s->diff_fb[st] * bufout);
            if (ch == 0) l = y; else r = y;
        }
        float cf = 0.35f * s->diff_fb[st];
        float l2 = l + cf * r;
        float r2 = r + cf * l;
        l = l2;
        r = r2;
    }
    /* cascaded one-pole allpass smears (unit magnitude, no boost) */
    for (int ch = 0; ch < 2; ++ch) {
        float x = (ch == 0) ? l : r;
        for (int st = 0; st < 3; ++st) {
            float g = s->ap_g[st];
            float y = -g * x + s->ap[ch][st][0] + g * s->ap[ch][st][1];
            s->ap[ch][st][0] = x;
            s->ap[ch][st][1] = y;
            x = y;
        }
        if (ch == 0) l = x; else r = x;
    }
    *out_l = l * s->out_gain;
    *out_r = r * s->out_gain;
}

void rev_swell_process(RevSwell* s, float in, float* out_l, float* out_r) {
    float l, r;
    rev_swell_taps(s, in, &l, &r);
    rev_swell_diffuse(s, l, r, out_l, out_r);
}
```

- [ ] **Step 5: Run tests**

Run: `cmake --build build --config Debug; ctest --test-dir build -C Debug -R "test_swell|test_crossfeed|test_core|test_rev|test_nofdn" --output-on-failure`
Expected: PASS. (The existing `test_swell` bounds/impulse checks must still hold — crossfeed keeps peaks < 1.0 as analyzed.)

- [ ] **Step 6: Commit**

```
git add core/rev_swell.h core/rev_swell.c tests/test_swell.c tests/test_crossfeed.c CMakeLists.txt
git commit -m "core: split swell taps/diffuse, mod=0 fast path, amount early-out, L/R crossfeed"
```

---

## Task 3: `rev_env_set_thresh` (trigger sensitivity)

**Files:** Modify `core/rev_env.h`, `core/rev_env.c`; Test `tests/test_env.c`

- [ ] **Step 1: Failing tests** — append to `tests/test_env.c` before the final block:

```c
    /* settable relative threshold (trigger sensitivity) */
    rev_env_init(&e, 44100.0f);
    rev_env_set_thresh(&e, 0.5f);
    CHECK(e.onset_thresh == 0.5f);
    rev_env_set_thresh(&e, 2.0f);   /* clamps to 1 */
    CHECK(e.onset_thresh == 1.0f);
    rev_env_set_thresh(&e, -1.0f);  /* clamps to 0 */
    CHECK(e.onset_thresh == 0.0f);
    rev_env_set_thresh(&e, 0.35f);
    CHECK(e.onset_thresh == 0.35f);
```

- [ ] **Step 2: Verify failure** — build + `ctest -R test_env` → compile FAIL.

- [ ] **Step 3: Implement** — in `core/rev_env.h` add after `rev_env_init`:

```c
/* relative onset threshold in [0,1] (fraction of the slow onset peak);
   higher = harder to trigger. Clamped internally. */
void rev_env_set_thresh(RevEnv* e, float rel);
```

in `core/rev_env.c` add:

```c
void rev_env_set_thresh(RevEnv* e, float rel) {
    e->onset_thresh = rev_clampf(rel, 0.0f, 1.0f);
}
```

- [ ] **Step 4: Run tests** — `ctest -R test_env` → PASS.

- [ ] **Step 5: Commit**

```
git add core/rev_env.h core/rev_env.c tests/test_env.c
git commit -m "core: settable relative onset threshold (trigger sensitivity)"
```

---

## Task 4: `reverson.h` API — 15 params, stereo API, `Reverson_mode`

**Files:** Modify `core/reverson.h`, `core/reverson.c` (param plumbing only — the process body is still the old one until Task 6); Test `tests/test_core.c`

- [ ] **Step 1: Failing tests** — in `tests/test_core.c`:
  - update the two `float vals[13] = {...}` lines (there are two occurrences of the map6 invariant loop pattern; change `[13]` to `[15]`, add `mp.trig, mp.predelay` to the initializer, and loop `k < 15`).
  - append before the 48 kHz block:

```c
    /* 15-param model: trig/predelay exist, clamp, and default sane */
    CHECK(Reverson_get_param(r, REVERSON_PARAM_TRIG) == 0.5f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_PREDELAY) == 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_TRIG, 1.5f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_TRIG) == 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_TRIG, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_PREDELAY, -1.0f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_PREDELAY) == 0.0f);

    /* mode tables: 1..5 fill the 13 shared params in range; 0 is a no-op;
       out-of-range clamps to 5 */
    {
        ReversonParams m0, m1, m5, m9;
        Reverson_mode(0, &m0);   /* untouched (m0 stays garbage-free? fill first) */
        m0.mix = -1.0f;
        Reverson_mode(0, &m0);
        CHECK(m0.mix == -1.0f);  /* mode 0 leaves the struct alone */
        Reverson_mode(1, &m1);
        Reverson_mode(5, &m5);
        Reverson_mode(9, &m9);   /* clamps to 5 */
        const float* a1 = (const float*)&m1;
        const float* a5 = (const float*)&m5;
        const float* a9 = (const float*)&m9;
        for (int k = 0; k < 13; ++k) {
            CHECK(a1[k] >= 0.0f && a1[k] <= 1.0f);
            CHECK(a5[k] >= 0.0f && a5[k] <= 1.0f);
            CHECK(a9[k] == a5[k]);
        }
    }
```

- [ ] **Step 2: Verify failure** — build → compile FAIL (`REVERSON_PARAM_TRIG` / `Reverson_mode` / `Reverson_process_stereo` missing).

- [ ] **Step 3: Implement** — `core/reverson.h`:

```c
#define REVERSON_NUM_PARAMS 15u
```
append to the enum (after `REVERSON_PARAM_DIFFUSION`):
```c
    REVERSON_PARAM_TRIG,
    REVERSON_PARAM_PREDELAY
```
extend the struct:
```c
typedef struct {
    float mix, decay, tone, revlen, duck, gate, shape, mod, sat, width, density, bass, diffusion, trig, predelay;
} ReversonParams;
```
add after the `Reverson_process` declaration:
```c
/* stereo-in variant: mono = 0.5*(in_l+in_r) drives the envelope/duck/wet
   engine (single wet path), the DRY stereo image is preserved. The ZDL
   callback buffers are already LLLLLLLL RRRRRRRR, so both hosts call this. */
void Reverson_process_stereo(Reverson* r, float in_l, float in_r, float* out_l, float* out_r);
/* 5-position mode switch (page 3 on the pedal): fills the 13 shared params.
   mode 0 = leave the struct untouched (custom knobs); 1..5 = Wash, Reverse,
   Gated, Shoegaze, Space; out-of-range clamps to 5. Trig/predelay are NOT
   touched (they stay user-owned). */
void Reverson_mode(int mode, ReversonParams* p);
/* test hook: current reverse-gate envelope value (v2 state machine). */
float Reverson_test_env(const Reverson* r);
```

`core/reverson.c` plumbing:
- `Reverson_state_size`: change `+ 64u` to `+ 256u` (the RevRev struct grows `sizeof(Reverson)`).
- `Reverson_set_param`: add
```c
        case REVERSON_PARAM_TRIG:     r->target.trig = v; break;
        case REVERSON_PARAM_PREDELAY: r->target.predelay = v; break;
```
- `Reverson_get_param`: add
```c
        case REVERSON_PARAM_TRIG:     return r->target.trig;
        case REVERSON_PARAM_PREDELAY: return r->target.predelay;
```
- defaults in `Reverson_init` (after the diffusion default):
```c
    r->target.trig = 0.5f;
    r->target.predelay = 0.0f;
```
- `Reverson_map6` and `Reverson_map3`: add before `p->mix = mix;`... actually append at the end (after `p->diffusion`):
```c
    p->trig = 0.5f;
    p->predelay = 0.0f;
```
- add the mode tables + function (place after `Reverson_set_bed`):

```c
/* 5-position mode switch: the same tables the pedal page-3 switch uses.
   Order: mix decay tone revlen duck gate shape mod sat width density bass diffusion */
static const float REV_MODE_TABLES[5][13] = {
    /* Wash */
    { 0.60f, 0.85f, 0.45f, 0.45f, 0.35f, 0.25f, 0.50f, 0.30f, 0.15f, 0.90f, 0.95f, 0.55f, 0.35f },
    /* Reverse */
    { 0.80f, 0.75f, 0.40f, 0.40f, 0.30f, 0.65f, 0.70f, 0.30f, 0.15f, 0.90f, 0.80f, 0.55f, 0.15f },
    /* Gated */
    { 0.70f, 0.70f, 0.45f, 0.25f, 0.20f, 0.90f, 0.50f, 0.30f, 0.15f, 0.85f, 0.35f, 0.55f, 0.20f },
    /* Shoegaze */
    { 0.80f, 0.85f, 0.40f, 0.45f, 0.45f, 0.45f, 0.60f, 0.30f, 0.15f, 0.90f, 0.85f, 0.55f, 0.30f },
    /* Space */
    { 0.85f, 1.00f, 0.35f, 0.60f, 0.10f, 0.20f, 0.50f, 0.35f, 0.12f, 0.95f, 1.00f, 0.60f, 0.50f }
};

void Reverson_mode(int mode, ReversonParams* p) {
    if (mode < 1) return;
    if (mode > 5) mode = 5;
    const float* t = REV_MODE_TABLES[mode - 1];
    p->mix = t[0]; p->decay = t[1]; p->tone = t[2]; p->revlen = t[3];
    p->duck = t[4]; p->gate = t[5]; p->shape = t[6]; p->mod = t[7];
    p->sat = t[8]; p->width = t[9]; p->density = t[10]; p->bass = t[11];
    p->diffusion = t[12];
}
```

- [ ] **Step 4: Run tests** — build + `ctest -R "test_core|test_nofdn"` → PASS.

- [ ] **Step 5: Commit**

```
git add core/reverson.h core/reverson.c tests/test_core.c
git commit -m "core: 15-param model (trig/predelay), Reverson_mode tables, stereo API decl, test env hook"
```

---

## Task 5: 8-sample parameter smoothing grid

**Files:** Modify `core/reverson.c`; Test `tests/test_trigger.c` (created here; more tests land in later tasks)

- [ ] **Step 1: Failing tests** — create `tests/test_trigger.c`:

```c
/* test_trigger.c - v2 trigger path: 8-sample smoothing grid, sensitivity,
 * predelay timing, hold behavior. Uses the Reverson_test_env hook. */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

static Reverson* new_core(void** memout) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    *memout = mem;
    return Reverson_init(mem, need, 44100.0f);
}

int main(void) {
    /* --- 8-sample grid: deterministic + convergent -------------------- */
    void* mem1; void* mem2;
    Reverson* a = new_core(&mem1);
    Reverson* b = new_core(&mem2);
    CHECK(a != NULL && b != NULL);
    Reverson_set_param(a, REVERSON_PARAM_GATE, 0.9f);
    Reverson_set_param(b, REVERSON_PARAM_GATE, 0.9f);
    Reverson_set_param(a, REVERSON_PARAM_REVLEN, 0.3f);
    Reverson_set_param(b, REVERSON_PARAM_REVLEN, 0.3f);
    Reverson_set_param(a, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(b, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(a, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(b, REVERSON_PARAM_MIX, 1.0f);
    float l1, r1, l2, r2, dsum = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        float x = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(a, x, &l1, &r1);
        Reverson_process(b, x, &l2, &r2);
        dsum += rev_absf(l1 - l2) + rev_absf(r1 - r2);
        CHECK(is_finite_f(l1) && is_finite_f(r1));
    }
    CHECK(dsum == 0.0f);   /* two identical instances stay bit-identical */
    /* params converge to target within 1 s even on the 8-sample grid */
    Reverson_set_param(a, REVERSON_PARAM_GATE, 0.2f);
    Reverson_set_param(a, REVERSON_PARAM_GATE, 0.9f);
    for (int i = 0; i < 44100; ++i) Reverson_process(a, 0.0f, &l1, &r1);
    CHECK(Reverson_get_param(a, REVERSON_PARAM_GATE) > 0.899f);
    free(mem1); free(mem2);

    if (fails == 0) { printf("test_trigger PASS\n"); return 0; }
    printf("test_trigger FAILED (%d)\n", fails);
    return 1;
}
```

(Task 5's file contains only the grid tests; the predelay + sensitivity blocks are appended in Task 7, right before their implementation.)

- [ ] **Step 2: Register** — in `CMakeLists.txt` add `reverson_add_test(test_trigger)` after `reverson_add_test(test_crossfeed)`.

- [ ] **Step 3: Verify current state passes grid tests** — the old code smooths per sample: determinism holds, convergence holds → this task's test should already PASS (it's a characterization test, TDD-green-now, red-on-regression). Run to confirm.

- [ ] **Step 4: Implement the grid** — in `core/reverson.c`:

struct additions (inside `struct Reverson`):
```c
    float smooth_coef_b;      /* per-8-sample smoothing coef */
    uint32_t smooth_timer;
```
`Reverson_init` additions (after `r->smooth_coef` line):
```c
    {
        float t = 1.0f - r->smooth_coef;
        float t2 = t * t;
        float t4 = t2 * t2;
        float t8 = t4 * t4;
        r->smooth_coef_b = 1.0f - t8;   /* (1-c)^8 via 3 multiplies (ZDL-safe) */
    }
    r->smooth_timer = 0u;
```
Add the smoothing helper above `Reverson_process`:
```c
static void rev_smooth_params(Reverson* r) {
    float c = r->smooth_coef_b;
    r->cur.mix       = rev_smooth(r->cur.mix,       r->target.mix,       c);
    r->cur.decay     = rev_smooth(r->cur.decay,     r->target.decay,     c);
    r->cur.tone      = rev_smooth(r->cur.tone,      r->target.tone,      c);
    r->cur.revlen    = rev_smooth(r->cur.revlen,    r->target.revlen,    c);
    r->cur.duck      = rev_smooth(r->cur.duck,      r->target.duck,      c);
    r->cur.gate      = rev_smooth(r->cur.gate,      r->target.gate,      c);
    r->cur.shape     = rev_smooth(r->cur.shape,     r->target.shape,     c);
    r->cur.mod       = rev_smooth(r->cur.mod,       r->target.mod,       c);
    r->cur.sat       = rev_smooth(r->cur.sat,       r->target.sat,       c);
    r->cur.width     = rev_smooth(r->cur.width,     r->target.width,     c);
    r->cur.density   = rev_smooth(r->cur.density,   r->target.density,   c);
    r->cur.bass      = rev_smooth(r->cur.bass,      r->target.bass,      c);
    r->cur.diffusion = rev_smooth(r->cur.diffusion, r->target.diffusion, c);
    r->cur.trig      = rev_smooth(r->cur.trig,      r->target.trig,      c);
    r->cur.predelay  = rev_smooth(r->cur.predelay,  r->target.predelay,  c);
}
```
In `Reverson_process`, DELETE the 13-line per-sample smoothing block at the top (lines 279-291) and replace with, at the top of the function:
```c
    r->sample_count++;
    r->smooth_timer++;
    if (r->smooth_timer == 8u) {
        r->smooth_timer = 0u;
        rev_smooth_params(r);
    }
```
(Keep everything else in the old process body for now — the remaining old code reads `r->cur` which is now grid-smoothed; the per-sample `rev_swell_set`/`set_mod`/diff_fb updates still use `r->cur` per sample — fine for this task, they move to the grid in Task 8.)

- [ ] **Step 5: Run tests** — build + `ctest -R "test_trigger|test_core|test_nofdn|test_swell|test_env|test_rev"` → PASS.

- [ ] **Step 6: Commit**

```
git add core/reverson.c tests/test_trigger.c CMakeLists.txt
git commit -m "core: 8-sample parameter smoothing grid (1/8 the per-sample cost)"
```

---

## Task 6: 5-state reverse envelope (overshoot + settle + hold + fall)

**Files:** Modify `core/reverson.c`; Test `tests/test_trigger.c` (append), `tests/test_core.c` (append)

- [ ] **Step 1: Failing tests** — append to `tests/test_trigger.c` before the final block:

```c
    /* --- overshoot: shape=1 blooms above 1 then settles to exactly 1; ---- */
    /* --- shape=0 never exceeds 1 ----------------------------------------- */
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.3f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.5f);
        Reverson_set_param(r, REVERSON_PARAM_SHAPE, 1.0f);
        float l, rr;
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        Reverson_process(r, 1.0f, &l, &rr);   /* onset */
        float over_peak = 0.0f;
        int saw_over = 0;
        for (int i = 0; i < 22050; ++i) {     /* rise + settle window */
            Reverson_process(r, 0.0f, &l, &rr);
            float e = Reverson_test_env(r);
            if (e > over_peak) over_peak = e;
            if (e > 1.0f + 0.05f) saw_over = 1;
        }
        CHECK(saw_over == 1);                 /* the overshoot happened */
        /* settle lands back on 1 during the hold phase */
        Reverson_process(r, 0.0f, &l, &rr);
        for (int i = 0; i < 4410; ++i) Reverson_process(r, 0.0f, &l, &rr);
        float hold_env = Reverson_test_env(r);
        CHECK(hold_env > 0.99f && hold_env < 1.01f);
        free(mem);
    }
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.3f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.5f);
        Reverson_set_param(r, REVERSON_PARAM_SHAPE, 0.0f);
        float l, rr;
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        Reverson_process(r, 1.0f, &l, &rr);
        float lin_peak = 0.0f;
        for (int i = 0; i < 22050; ++i) {
            Reverson_process(r, 0.0f, &l, &rr);
            float e = Reverson_test_env(r);
            if (e > lin_peak) lin_peak = e;
        }
        CHECK(lin_peak <= 1.0001f);           /* linear attack: no overshoot */
        free(mem);
    }

    /* --- hold: density lengthens the time above the floor's midpoint ----- */
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.2f);
        Reverson_set_param(r, REVERSON_PARAM_SHAPE, 0.0f);
        float l, rr;
        float above_mid[2];
        for (int d = 0; d < 2; ++d) {
            Reverson_set_param(r, REVERSON_PARAM_DENSITY, d == 0 ? 0.0f : 1.0f);
            for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);  /* settle to floor */
            Reverson_process(r, 1.0f, &l, &rr);   /* onset */
            float floor = Reverson_test_env(r);
            float mid = floor + (1.0f - floor) * 0.5f;
            int above = 0;
            for (int i = 0; i < 88200; ++i) {     /* 2 s window */
                Reverson_process(r, 0.0f, &l, &rr);
                if (Reverson_test_env(r) > mid) above++;
            }
            above_mid[d] = (float)above;
        }
        CHECK(above_mid[1] > above_mid[0] * 1.5f);  /* long hold keeps it up */
        free(mem);
    }
```

- [ ] **Step 2: Verify failure** — build + `ctest -R test_trigger` → the overshoot/hold checks FAIL (old 2-state machine has no overshoot, and old density affects release not hold-at-peak... the hold test may accidentally pass; the overshoot one will fail deterministically).

- [ ] **Step 3: Implement** — in `core/reverson.c`:

struct additions:
```c
    float rev_settle_inc;
    float rev_over;
    uint32_t rev_hold_left;
    float hold_add;           /* trig knob -> extra hold samples (Task 7 wires it) */
```
Add helpers above `Reverson_process`:
```c
static float rev_floor_of(const Reverson* r) {
    float f = 1.0f - 0.65f * r->cur.gate;
    if (f < 0.2f) f = 0.2f;
    return f;
}

static void rev_fire_trigger(Reverson* r) {
    float sr = r->sample_rate;
    uint32_t rise = (uint32_t)((0.05f + 1.95f * r->cur.revlen) * sr);
    if (rise < 2u) rise = 2u;
    r->rev_over = 0.16f * r->cur.shape;
    float target = 1.0f + r->rev_over;
    float span = target - r->rev_env;
    if (span < 1e-4f) span = 1e-4f;
    float inv_rise = 1.0f / (float)rise;
    float a = r->cur.shape;
    r->rev_env_inc = span * inv_rise * (1.0f - a);
    r->rev_env_acc = 2.0f * span * inv_rise * inv_rise * a;
    uint32_t settle_n = (uint32_t)(0.04f * sr);
    if (settle_n < 1u) settle_n = 1u;
    r->rev_settle_inc = r->rev_over / (float)settle_n;
    float hold_s = 0.30f + 0.70f * r->cur.density;
    uint32_t hold = (uint32_t)(hold_s * sr + r->hold_add);
    if (hold < 2u) hold = 2u;
    r->rev_hold_left = hold;
    uint32_t fall_n = hold >> 1u;
    if (fall_n < 2u) fall_n = 2u;
    r->rev_fall_inc = (target - rev_floor_of(r)) / (float)fall_n;
    r->rev_state = 1u;
    r->rev_last_trigger = r->sample_count;
}
```
Replace the whole old envelope block in `Reverson_process` — from the comment `/* reverse swell envelope with a FLOOR...` through the end of the `/* state 0: env stays at the floor...*/ } else {...}` block — with:
```c
    /* reverse swell envelope v2: rise -> (1+over) -> settle to 1 -> hold ->
       fall to the floor. Value-based, so a retrigger re-plans from the
       current level (no dip -> no wobble). */
    {
        float floor = rev_floor_of(r);
        if (rev_env_onset(&r->env) && r->cur.gate > 0.01f) {
            uint32_t min_gap = (uint32_t)(0.02f * r->sample_rate);
            if (r->sample_count - r->rev_last_trigger >= min_gap) {
                rev_fire_trigger(r);   /* predelay wiring lands in Task 7 */
            }
        }
        if (r->cur.gate > 0.01f) {
            float target = 1.0f + r->rev_over;
            if (r->rev_state == 1u) {
                r->rev_env += r->rev_env_inc;
                r->rev_env_inc += r->rev_env_acc;
                if (r->rev_env >= target) { r->rev_env = target; r->rev_state = 2u; }
            } else if (r->rev_state == 2u) {
                r->rev_env -= r->rev_settle_inc;
                if (r->rev_env <= 1.0f) { r->rev_env = 1.0f; r->rev_state = 3u; }
            } else if (r->rev_state == 3u) {
                if (r->rev_hold_left > 0u) r->rev_hold_left--;
                if (r->rev_hold_left == 0u) r->rev_state = 4u;
            } else if (r->rev_state == 4u) {
                r->rev_env -= r->rev_fall_inc;
                if (r->rev_env <= floor) { r->rev_env = floor; r->rev_state = 0u; }
            } else {
                r->rev_env = floor;   /* state 0: stay at the floor */
            }
        } else {
            r->rev_env = 1.0f;
            r->rev_state = 0u;
        }
    }
```
In `Reverson_init`: add `r->rev_settle_inc = 0.0f; r->rev_over = 0.0f; r->rev_hold_left = 0u; r->hold_add = 0.0f;` next to the other rev_env fields. Same in `Reverson_reset`.
Add the test hook at the end of the file:
```c
float Reverson_test_env(const Reverson* r) { return r->rev_env; }
```

Note: the OLD trigger also had the retrigger `span` re-plan with `if (r->rev_state == 0u)` vs rising — the new version re-plans `span = target - rev_env` unconditionally, which is exactly the value-based behavior (kept simple and correct for all states).

- [ ] **Step 4: Run tests** — build + `ctest -R "test_trigger|test_core|test_nofdn|test_swell|test_env|test_rev"` → PASS. (test_core's existing swell test: gate=1, shape default 0.33 → over≈0.05, still blooms then settles below peak. The `gtail < gpeak` check compares the settled tail to the peak — with hold+fall the tail settles by +2s. PASS expected; verify empirically.)

- [ ] **Step 5: Commit**

```
git add core/reverson.c tests/test_trigger.c
git commit -m "core: 5-state reverse envelope with overshoot, settle, hold, fall"
```

---

## Task 7: Trigger sensitivity + predelay wiring

**Files:** Modify `core/reverson.c`; Test `tests/test_trigger.c` (append)

- [ ] **Step 1: Failing tests** — append to `tests/test_trigger.c` before the final block:

```c
    /* --- predelay: env must stay at the floor for exactly pd samples ------ */
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.3f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.5f);
        Reverson_set_param(r, REVERSON_PARAM_PREDELAY, 0.10f);  /* 4410 samples @44k1 */
        float l, rr;
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        float floor_before = Reverson_test_env(r);
        Reverson_process(r, 1.0f, &l, &rr);   /* the onset */
        int stayed = 1;
        for (int i = 0; i < 4400; ++i) {
            Reverson_process(r, 0.0f, &l, &rr);
            if (Reverson_test_env(r) != floor_before) stayed = 0;
        }
        CHECK(stayed == 1);                     /* env held at floor during predelay */
        for (int i = 0; i < 200; ++i) Reverson_process(r, 0.0f, &l, &rr);
        CHECK(Reverson_test_env(r) > floor_before);   /* then it rises */
        free(mem);
    }

    /* --- sensitivity: hard trigger ignores a quiet re-note, easy triggers -- */
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.2f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.3f);
        float l, rr;
        Reverson_set_param(r, REVERSON_PARAM_TRIG, 1.0f);   /* rel 0.50: hard */
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        Reverson_process(r, 1.0f, &l, &rr);                 /* big note: fires */
        for (int i = 0; i < 22050; ++i) Reverson_process(r, 0.0f, &l, &rr);
        float hard_floor = Reverson_test_env(r);
        for (int i = 0; i < 11025; ++i) {                   /* silence gap */
            Reverson_process(r, 0.0f, &l, &rr);
            if (i < 11024) Reverson_process(r, 0.0f, &l, &rr);
        }
        for (int i = 0; i < 200; ++i) Reverson_process(r, 0.08f, &l, &rr);  /* quiet: 8% of peak */
        for (int i = 0; i < 5000; ++i) Reverson_process(r, 0.0f, &l, &rr);
        CHECK(Reverson_test_env(r) <= hard_floor + 0.05f);  /* never re-fired */
        free(mem);
    }
    {
        void* mem;
        Reverson* r = new_core(&mem);
        CHECK(r != NULL);
        Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
        Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);
        Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.2f);
        Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.3f);
        Reverson_set_param(r, REVERSON_PARAM_TRIG, 0.0f);   /* rel 0.12: easy */
        float l, rr;
        for (int i = 0; i < 44100; ++i) Reverson_process(r, 0.0f, &l, &rr);
        Reverson_process(r, 1.0f, &l, &rr);
        for (int i = 0; i < 22050; ++i) Reverson_process(r, 0.0f, &l, &rr);
        for (int i = 0; i < 11025; ++i) {                   /* silence gap */
            Reverson_process(r, 0.0f, &l, &rr);
            if (i < 11024) Reverson_process(r, 0.0f, &l, &rr);
        }
        float before_quiet = Reverson_test_env(r);
        for (int i = 0; i < 200; ++i) Reverson_process(r, 0.2f, &l, &rr);  /* 20% note */
        for (int i = 0; i < 5000; ++i) Reverson_process(r, 0.0f, &l, &rr);
        CHECK(Reverson_test_env(r) > before_quiet + 0.1f); /* re-fired: env bloomed */
        free(mem);
    }
```

- [ ] **Step 2: Verify failure** — build + `ctest -R test_trigger` → the predelay + sensitivity checks FAIL.

- [ ] **Step 3: Implement** — in `core/reverson.c`:

struct additions:
```c
    float pd_samples;         /* predelay length in samples (from the knob) */
    uint32_t pd_counter;      /* pending predelay countdown (0 = none) */
```
In the trigger block of the process body, replace
```c
            if (r->sample_count - r->rev_last_trigger >= min_gap) {
                rev_fire_trigger(r);   /* predelay wiring lands in Task 7 */
            }
```
with
```c
            if (r->sample_count - r->rev_last_trigger >= min_gap) {
                uint32_t pd = (uint32_t)r->pd_samples;
                if (pd > 0u) r->pd_counter = pd;
                else rev_fire_trigger(r);
            }
        }
        if (r->pd_counter > 0u) {
            r->pd_counter--;
            if (r->pd_counter == 0u) rev_fire_trigger(r);
        }
```
(also delete the now-duplicated closing brace of the old `if` — the replacement block above keeps brace balance: old was `if (onset && gate) { if (min_gap) { fire; } }`; new is `if (onset && gate) { if (min_gap) { pd... } } if (pd_counter) {...}`.)

Add an `update_derived` step — for this task only the trigger parts, minimal (the full function lands in Task 8; here add a stub that only does what Task 7 needs):
```c
static void rev_update_derived(Reverson* r) {
    /* trigger settings from the trig/predelay knobs (extended in Task 8) */
    rev_env_set_thresh(&r->env, 0.12f + 0.38f * r->cur.trig);
    r->hold_add = (0.05f + 0.75f * r->cur.trig) * r->sample_rate;
    r->pd_samples = 0.120f * r->cur.predelay * r->sample_rate;
}
```
Call it inside the 8-sample grid branch, right after `rev_smooth_params(r);`, and once at the end of `Reverson_init` (after defaults set) and in `Reverson_reset` (after `r->cur = r->target` is already true — reset does not touch cur/target, so call it after the field clears).
In `Reverson_init` add `r->pd_samples = 0.0f; r->pd_counter = 0u;` near the other trigger fields; same in `Reverson_reset`.

- [ ] **Step 4: Run tests** — build + `ctest -R "test_trigger|test_core|test_nofdn|test_env"` → PASS.

- [ ] **Step 5: Commit**

```
git add core/reverson.c tests/test_trigger.c
git commit -m "core: trigger sensitivity (trig knob) + predelay counter"
```

---

## Task 8: RevRev integration — the reverse layer takes over at high Rev

**Files:** Modify `core/reverson.c`; Test `tests/test_v2reverse.c` (create + register)

- [ ] **Step 1: Failing tests** — create `tests/test_v2reverse.c`:

```c
/* test_v2reverse.c - integrated reverse layer (RevRev): zero before the
 * first trigger, plays pre-onset material after it, stays bounded. */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
static int is_finite_f(float v) {
    return (v == v) && (v > -3.0e38f) && (v < 3.0e38f);
}

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 1.0f);   /* pure reverse layer */
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.4f);
    Reverson_set_param(r, REVERSON_PARAM_DENSITY, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_SHAPE, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_MOD, 0.3f);
    Reverson_set_param(r, REVERSON_PARAM_SAT, 0.1f);
    float l, rr;

    /* before ANY trigger: the reverse buffer is silent -> wet is exactly 0 */
    for (int i = 0; i < 100; ++i) {
        Reverson_process(r, 0.0f, &l, &rr);
        CHECK(l == 0.0f && rr == 0.0f);
    }

    /* feed a decaying burst (pre-onset material), then a short gap, then the
       onset impulse: the swell after the trigger must be non-zero even though
       the input AFTER the trigger is silence (the layer plays the pre-onset
       tail backwards) */
    for (int i = 0; i < 8820; ++i) {                       /* 200 ms burst */
        float x = (float)sinf(2.0f * 3.14159265f * 220.0f * (float)i / 44100.0f);
        Reverson_process(r, 0.4f * x * (1.0f - (float)i / 8820.0f), &l, &rr);
    }
    for (int i = 0; i < 4410; ++i) Reverson_process(r, 0.0f, &l, &rr);  /* 100 ms gap */
    Reverson_process(r, 0.9f, &l, &rr);                   /* the onset */
    float post = 0.0f;
    for (int i = 0; i < 22050; ++i) {
        Reverson_process(r, 0.0f, &l, &rr);
        post += rev_absf(l) + rev_absf(rr);
        CHECK(is_finite_f(l) && is_finite_f(rr));
    }
    CHECK(post > 0.01f);   /* the reverse layer plays back the pre-onset tail */

    /* boundedness with sustained pulse input at full reverse */
    float peak = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        Reverson_process(r, (i % 220 == 0) ? 0.8f : 0.0f, &l, &rr);
        CHECK(is_finite_f(l) && is_finite_f(rr));
        if (rev_absf(l) > peak) peak = rev_absf(l);
        if (rev_absf(rr) > peak) peak = rev_absf(rr);
    }
    CHECK(peak < 1.2f);

    free(mem);
    if (fails == 0) { printf("test_v2reverse PASS\n"); return 0; }
    printf("test_v2reverse FAILED (%d)\n", fails);
    return 1;
}
```

Note: this test uses `sinf` (a TEST, not the audio path — allowed). Register: `reverson_add_test(test_v2reverse)` in `CMakeLists.txt`.

- [ ] **Step 2: Verify failure** — build + `ctest -R test_v2reverse` → FAIL (old core at gate=1 uses forward taps; the "wet is exactly 0 before trigger" holds only until the first tap reaches the line... actually forward taps DO output after ~8ms even before any onset trigger, so the exact-zero check fails → red confirmed).

- [ ] **Step 3: Implement** — in `core/reverson.c`:

include: add `#include "rev_rev.h"`.
struct additions:
```c
    RevRev rev;
    float rev_mix;            /* reverse-layer mix derived from gate */
    float rev_gain;           /* reverse-layer wet gain */
    uint32_t rev_seg_len, rev_cross, rev_preoff;
    uint32_t rev_voices;
    int rev_rr_shape;
```
`Reverson_init` — after the swell init block:
```c
#if REVERSON_ENABLE_FDN
    rev_rev_init(&r->rev, p + REV_FDN_TOTAL_SAMPLES, REV_SWELL_BUF_LEN, sample_rate);
#else
    rev_rev_init(&r->rev, p, REV_SWELL_BUF_LEN, sample_rate);
#endif
```
The RevRev buffer SHARES the swell line memory (same 32768 length, same write cadence, same wet_in value each sample — the double write is benign). Defaults: `r->rev_mix = 0.0f; r->rev_gain = 0.6f; r->rev_seg_len = 2u; r->rev_cross = 1u; r->rev_preoff = 0u; r->rev_voices = 1u; r->rev_rr_shape = 1;`
`Reverson_reset`: add `rev_rev_clear(&r->rev);`

Extend `rev_update_derived` with the reverse-layer geometry (append after the trigger settings):
```c
    /* Rev continuum: forward taps fade out as the reverse layer takes over */
    float rm = (r->cur.gate - 0.12f) * 1.282f;
    rm = rev_clampf(rm, 0.0f, 1.0f);
    r->rev_mix = rm;
    rev_swell_set(&r->swell, r->cur.revlen, r->cur.gate * (1.0f - rm));
    rev_swell_set_mod(&r->swell, r->cur.mod);
    {
        float dfb = r->cur.diffusion;
        if (dfb > 0.7f) dfb = 0.7f;
        r->swell.diff_fb[0] = dfb;
        r->swell.diff_fb[1] = dfb * 0.9f;
        r->swell.diff_fb[2] = dfb * 0.8f;
    }
    float sr = r->sample_rate;
    float span_s = 0.10f + 0.55f * r->cur.revlen;           /* 0.10..0.65 s */
    uint32_t seg = (uint32_t)(span_s * sr);
    if (seg < 2u) seg = 2u;
    if (seg > REV_SWELL_BUF_LEN - 2u) seg = REV_SWELL_BUF_LEN - 2u;
    r->rev_seg_len = seg;
    uint32_t cross = (uint32_t)(0.03f * sr);
    if (cross > seg / 2u) cross = seg / 2u;
    if (cross < 1u) cross = 1u;
    r->rev_cross = cross;
    r->rev_preoff = (uint32_t)(0.004f * sr);
    if (r->rev_preoff > seg - 2u) r->rev_preoff = seg > 2u ? seg - 2u : 0u;
    r->rev_voices = 1u + (uint32_t)(2.0f * r->cur.density);  /* 1..3 */
    r->rev_rr_shape = 1 + (int)(3.0f * r->cur.shape);        /* 1..4 */
    if (r->rev_rr_shape > 4) r->rev_rr_shape = 4;
    r->rev_gain = 0.6f;
```
In `rev_fire_trigger`, before `r->rev_state = 1u;` add:
```c
    rev_rev_set_voices(&r->rev, r->rev_voices);
    rev_rev_set_preoff(&r->rev, r->rev_preoff);
    rev_rev_trigger(&r->rev, r->rev_seg_len, r->rev_cross, r->rev_rr_shape);
```
In the process body, replace the swell-only block
```c
    /* SPX90-style multi-head swell: gate = swell amount ... */
    float sw_l, sw_r;
    rev_swell_set(&r->swell, r->cur.revlen, r->cur.gate);
    rev_swell_set_mod(&r->swell, r->cur.mod);   /* living tail on the taps */
    {
        float dfb = r->cur.diffusion;
        if (dfb > 0.7f) dfb = 0.7f;
        r->swell.diff_fb[0] = dfb;
        r->swell.diff_fb[1] = dfb * 0.9f;
        r->swell.diff_fb[2] = dfb * 0.8f;
    }
    rev_swell_process(&r->swell, wet_in, &sw_l, &sw_r);
    wet_l += sw_l;
    wet_r += sw_r;
```
with
```c
    /* forward taps + reverse layer -> shared diffuser */
    {
        float sw_l, sw_r;
        rev_swell_taps(&r->swell, wet_in, &sw_l, &sw_r);
        rev_rev_write(&r->rev, wet_in);
        float rv = rev_rev_process(&r->rev);
        float rg = r->rev_gain * r->rev_mix;
        sw_l += rv * rg;
        sw_r += rv * rg;
        rev_swell_diffuse(&r->swell, sw_l, sw_r, &sw_l, &sw_r);
        wet_l += sw_l;
        wet_r += sw_r;
    }
```

- [ ] **Step 4: Run tests** — build + full `ctest --output-on-failure` → PASS. (test_core's "pure mode + gate=0 is dry": rm=0, amount=0 → wet stays exactly 0. The old swell test at gate=1 now runs the reverse layer — verified green in the analysis; watch `gpeak > 0.002` and the 48 kHz bound.)

- [ ] **Step 5: Commit**

```
git add core/reverson.c tests/test_v2reverse.c CMakeLists.txt
git commit -m "core: integrate RevRev reverse layer (Rev continuum, buffer-shared, tap early-out)"
```

---

## Task 9: Wet high-pass + stereo API

**Files:** Modify `core/reverson.c`; Test `tests/test_hpf.c`, `tests/test_stereo.c` (create + register)

- [ ] **Step 1: Failing tests** — create `tests/test_hpf.c`:

```c
/* test_hpf.c - wet high-pass: sustained 50 Hz wet energy is well below
 * 300 Hz wet energy (fixed ~110 Hz HPF cuts palm-mute mud). */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static float wet_rms(Reverson* r, float freq) {
    float l = 0.0f, rr = 0.0f, acc = 0.0f;
    for (int i = 0; i < 44100; ++i) {         /* 1 s steady state */
        float x = 0.2f * (float)sinf(2.0f * 3.14159265f * freq * (float)i / 44100.0f);
        Reverson_process(r, x, &l, &rr);
        acc += l * l + rr * rr;
    }
    return (float)sqrt(acc / 88200.0f);
}

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);
    /* taps on (gate>0.12), reverse layer low, LP wide open (tone=1),
       neutral shelf, no sat, no mod, mono width */
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.3f);
    Reverson_set_param(r, REVERSON_PARAM_REVLEN, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_TONE, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_BASS, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_SAT, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_MOD, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_WIDTH, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DIFFUSION, 0.1f);
    float l, rr;
    for (int i = 0; i < 22050; ++i) Reverson_process(r, 0.0f, &l, &rr); /* settle */
    float rms50 = wet_rms(r, 50.0f);
    float rms300 = wet_rms(r, 300.0f);
    CHECK(rms300 > 1e-4f);                 /* 300 Hz passes */
    CHECK(rms50 < rms300 * 0.6f);          /* 50 Hz is cut by the HPF */
    free(mem);
    if (fails == 0) { printf("test_hpf PASS\n"); return 0; }
    printf("test_hpf FAILED (%d)\n", fails);
    return 1;
}
```

create `tests/test_stereo.c`:

```c
/* test_stereo.c - stereo API: dry image preserved, mono wrapper equivalent. */
#include "reverson.h"
#include "rev_util.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    uint32_t need = Reverson_state_size(44100.0f);
    void* mem = malloc(need);
    CHECK(mem != NULL);
    Reverson* r = Reverson_init(mem, need, 44100.0f);
    CHECK(r != NULL);

    /* mix=0: the dry stereo image passes through bit-exactly */
    Reverson_set_param(r, REVERSON_PARAM_MIX, 0.0f);
    float ol, orr;
    for (int i = 0; i < 1000; ++i) {
        float xl = (float)((i * 13) % 17) / 17.0f - 0.5f;
        float xr = (float)((i * 7) % 19) / 19.0f - 0.5f;
        Reverson_process_stereo(r, xl, xr, &ol, &orr);
        CHECK(ol == xl);
        CHECK(orr == xr);
    }

    /* L==R input: mono wrapper is bit-identical to the stereo entry */
    Reverson_set_param(r, REVERSON_PARAM_MIX, 0.55f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.4f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.5f);
    Reverson_set_param(r, REVERSON_PARAM_MOD, 0.4f);
    float l1, r1, l2, r2, dsum = 0.0f;
    for (int i = 0; i < 22050; ++i) {
        float x = (i % 220 == 0) ? 0.7f : 0.0f;
        Reverson_process(r, x, &l1, &r1);
        Reverson_process_stereo(r, x, x, &l2, &r2);
        dsum += rev_absf(l1 - l2) + rev_absf(r1 - r2);
    }
    CHECK(dsum == 0.0f);

    /* stereo input with mix=1: the WET path is a mono engine (L==R wet), the
       dry is silent at mix=1 - so outputs stay finite and bounded */
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        float xl = (i % 220 == 0) ? 0.8f : 0.0f;
        float xr = (i % 220 == 55) ? -0.6f : 0.0f;
        Reverson_process_stereo(r, xl, xr, &ol, &orr);
        if (ol == ol && orr == orr) {
            if (rev_absf(ol) > peak) peak = rev_absf(ol);
            if (rev_absf(orr) > peak) peak = rev_absf(orr);
        } else {
            CHECK(0);   /* NaN */
        }
    }
    CHECK(peak < 1.2f);

    free(mem);
    if (fails == 0) { printf("test_stereo PASS\n"); return 0; }
    printf("test_stereo FAILED (%d)\n", fails);
    return 1;
}
```

Register both in `CMakeLists.txt` (`reverson_add_test(test_hpf)`, `reverson_add_test(test_stereo)`).

- [ ] **Step 2: Verify failure** — build + `ctest -R "test_hpf|test_stereo"` → FAIL (no stereo API / no HPF).

- [ ] **Step 3: Implement** — in `core/reverson.c`:

struct additions:
```c
    float wet_hp_l, wet_hp_r;        /* one-pole HPF state */
    float wet_hp_x1_l, wet_hp_x1_r;  /* HPF previous input */
    float hp_a;                      /* HPF coefficient (init-time) */
```
static helper above `Reverson_init`:
```c
/* one-pole HPF coefficient a = exp(-2*pi*fc/sr); 5-term series so the
   audio path (and init) stay mul/add-only (ZDL-safe) */
static float rev_hp_coeff(float sr) {
    float x = -6.283185307f * 110.0f / sr;
    return 1.0f + x * (1.0f + x * (0.5f + x * (0.1666667f + x * 0.04166667f)));
}
```
`Reverson_init`: after the smooth_coef_b block add:
```c
    r->hp_a = rev_hp_coeff(sample_rate);
    r->wet_hp_l = 0.0f; r->wet_hp_r = 0.0f;
    r->wet_hp_x1_l = 0.0f; r->wet_hp_x1_r = 0.0f;
```
`Reverson_reset`: add the same four zero-clears (after the wet_lp clears).
In the process body, insert between the tone-LP block and the bass-shelf block:
```c
    /* fixed wet high-pass (~110 Hz): cuts palm-mute mud from the smear */
    {
        float a = r->hp_a;
        float yl = a * (r->wet_hp_l + wet_l - r->wet_hp_x1_l);
        r->wet_hp_x1_l = wet_l;
        r->wet_hp_l = yl;
        wet_l = yl;
        float yr = a * (r->wet_hp_r + wet_r - r->wet_hp_x1_r);
        r->wet_hp_x1_r = wet_r;
        r->wet_hp_r = yr;
        wet_r = yr;
    }
```
Replace `Reverson_process` with the stereo entry + wrapper (the dry path changes from `in` to `in_l`/`in_r`):
```c
void Reverson_process_stereo(Reverson* r, float in_l, float in_r, float* out_l, float* out_r) {
    /* ... the whole existing per-sample body, with the final mix lines: ... */
    float mix = r->cur.mix;
    *out_l = in_l * (1.0f - mix) + wet_l * mix;
    *out_r = in_r * (1.0f - mix) + wet_r * mix;
}

void Reverson_process(Reverson* r, float in, float* out_l, float* out_r) {
    Reverson_process_stereo(r, in, in, out_l, out_r);
}
```
Also the first line of the body feeds the envelopes with the mono sum: change `rev_env_process(&r->env, in);` to use the downmix:
```c
    float mono = 0.5f * (in_l + in_r);
```
and use `mono` for `rev_env_process`, the duck path, and `wet_in`.

- [ ] **Step 4: Run tests** — build + full `ctest --output-on-failure` → PASS.

- [ ] **Step 5: Commit**

```
git add core/reverson.c tests/test_hpf.c tests/test_stereo.c CMakeLists.txt
git commit -m "core: wet high-pass (~110 Hz) + stereo API with preserved dry image"
```

---

## Task 10: Full regression + tune fallout

- [ ] **Step 1:** Build everything and run the complete suite:
`cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
Expected: all suites PASS (test_util, test_delay, test_env, test_rev, test_fdn, test_swell, test_crossfeed, test_hpf, test_stereo, test_trigger, test_v2reverse, test_core, test_nofdn).

- [ ] **Step 2:** If any existing assertion fails (the likely suspects: test_swell peak bounds with crossfeed, test_core duck/gate timing after the 5-state change, the 48 kHz bound), fix per `docs/superpowers/specs/2026-08-14-reverson-core-v2-design.md` intent:
  - crossfeed-related peak: clamp `cf` harder (e.g. `0.30f * s->diff_fb[st]`) — the bound `(1+cf)^3` must stay under the test ceiling;
  - timing-related: re-check the state-machine arithmetic against the test windows before touching the tests themselves.
  If a test is genuinely obsolete (behavior intentionally changed), update the test with a comment referencing the spec — never silently delete a failing assertion.

- [ ] **Step 3: Commit** any fixes:

```
git add -A
git commit -m "core: regression fixes after v2 process body"
```

---

## Task 11: Render tool — trig/predelay keys, acceptance presets, Reverson_mode

**Files:** Modify `tools/render_demo.c`

- [ ] **Step 1: Extend the Preset struct and tables** — change the struct to:
```c
typedef struct {
    const char* name;
    float mix, decay, tone, revlen, duck, gate, shape, mod, sat, width, density, bass, diffusion, trig, predelay;
} Preset;
```
Append `, 0.5f, 0.0f` to each of the 12 existing PRESETS rows.
Add a knob-level acceptance preset table after `PRESETS`:
```c
/* Acceptance presets (spec): expressed as 6-knob + trig/predelay values so
   they exercise the same map6 curves the VST and pedal use. */
typedef struct {
    const char* name;
    float mix, rev, space, tone, grain, duck, trig, predelay;
} KnobPreset;

static const KnobPreset KNOB_PRESETS[3] = {
    { "mbv",      0.60f, 0.85f, 0.55f, 0.50f, 0.60f, 0.10f, 0.35f, 0.10f },
    { "diiv",     0.55f, 0.25f, 0.60f, 0.50f, 0.40f, 0.50f, 0.55f, 0.00f },
    { "slowdive", 0.70f, 0.35f, 0.85f, 0.45f, 0.55f, 0.30f, 0.70f, 0.00f }
};
```
`apply_preset` gains two lines:
```c
    Reverson_set_param(r, REVERSON_PARAM_TRIG, p->trig);
    Reverson_set_param(r, REVERSON_PARAM_PREDELAY, p->predelay);
```
Add an `apply_params(Reverson*, const ReversonParams*)` helper (same body as `apply_preset` but from a `ReversonParams`), and re-implement `apply_preset` as a `ReversonParams` fill + `apply_params` call (keeps one path).

- [ ] **Step 2: Override keys** — in the key parsing block add before the `else { fprintf... }`:
```c
            else if (strcmp(k, "trig") == 0)     par = REVERSON_PARAM_TRIG;
            else if (strcmp(k, "predelay") == 0) par = REVERSON_PARAM_PREDELAY;
```
Replace the `mode` override body (the `apply_preset(core, &MODES[mi-1])` branch) with:
```c
            if (strcmp(k, "mode") == 0) {
                int mi = (int)v;
                if (mi < 1) mi = 1;
                if (mi > (int)NUM_MODES) mi = (int)NUM_MODES;
                ReversonParams mp;
                Reverson_mode(mi, &mp);
                apply_params(core, &mp);
                continue;
            }
```
Delete the local `MODES` array + `NUM_MODES` (replaced by `Reverson_mode`) — keep the `NUM_MODES` constant as `5u` where needed... simplest: replace `MODES[5]` block with nothing and change the mode override to clamp `mi` to `1..5` directly:
```c
                int mi = (int)v;
                if (mi < 1) mi = 1;
                if (mi > 5) mi = 5;
```
Update the usage text: keys now `mix decay tone revlen duck gate shape mod sat width density bass diffusion trig predelay`; presets list gains `mbv diiv slowdive`.

- [ ] **Step 3: Render the knob presets** — in `main`, after the existing `PRESETS` loop, add a second loop:
```c
    for (unsigned ki = 0; ki < 3u; ++ki) {
        const KnobPreset* kp = &KNOB_PRESETS[ki];
        if (filter && strcmp(filter, kp->name) != 0) continue;
        void* mem = malloc(need);
        if (!mem) { fprintf(stderr, "alloc failed\n"); return 1; }
        Reverson* core = Reverson_init(mem, need, (float)a.rate);
        if (!core) { fprintf(stderr, "init failed\n"); return 1; }
        Reverson_set_6knob(core, kp->mix, kp->rev, kp->space, kp->tone, kp->grain, kp->duck);
        Reverson_set_param(core, REVERSON_PARAM_TRIG, kp->trig);
        Reverson_set_param(core, REVERSON_PARAM_PREDELAY, kp->predelay);
        for (int oi = 7; oi < argc; ++oi) {   /* per-preset overrides */
            char k[32]; float v = 0.0f;
            if (sscanf(argv[oi], "%31[^=]=%f", k, &v) != 2) continue;
            if (strcmp(k, "trig") == 0)      { Reverson_set_param(core, REVERSON_PARAM_TRIG, v); continue; }
            if (strcmp(k, "predelay") == 0)  { Reverson_set_param(core, REVERSON_PARAM_PREDELAY, v); continue; }
        }
        if (!cold) {
            for (unsigned i = 0; i < a.n; ++i) {
                float l, r; Reverson_process(core, a.mono[i], &l, &r);
            }
        }
        unsigned out = 0;
        for (unsigned p = 0; p < loops; ++p)
            for (unsigned i = 0; i < a.n; ++i) {
                float l, r;
                Reverson_process(core, a.mono[i], &l, &r);
                L[out] = l; R[out] = r; ++out;
            }
        for (unsigned i = 0; i < out; ++i) { L[i] *= norm; R[i] *= norm; }
        {
            float pk = peak_of(L, out);
            float pk2 = peak_of(R, out);
            if (pk2 > pk) pk = pk2;
            if (pk > 0.95f) {
                float g = 0.95f / pk;
                for (unsigned i = 0; i < out; ++i) { L[i] *= g; R[i] *= g; }
            }
        }
        snprintf(path, sizeof(path), "%s_%s.wav", argv[2], kp->name);
        write_wav(path, L, R, out, a.rate);
        float rms = 0.0f;
        for (unsigned i = 0; i < out; ++i) { float m = (L[i] + R[i]) * 0.5f; rms += m * m; }
        rms = (float)sqrt(rms / (float)out);
        printf("%-8s: peak=%.3f rms=%.3f -> %s\n", kp->name, peak_of(L, out), rms, path);
        free(mem);
    }
```

- [ ] **Step 4: Build + smoke render** — `cmake --build build --config Debug` then generate a test tone and render (any short wav works; use the renderer's own output as input):
```
build\Debug\reverson_render.exe build\Debug\reverson_render.exe x 2>nul
```
Expected: it prints an error (input is an exe) — instead use a real wav: `build\Debug\reverson_render.exe out\somefile_dry.wav out\v2 2 mbv` after a first successful run of any existing preset, or generate one with PowerShell (16-bit PCM wav with a sine burst is enough for a smoke test). Simplest smoke: render with an existing repo wav if present in `zdl_samples`/`out` (check `Get-ChildItem out, zdl_samples -Filter *.wav`; if none, generate `smoke.wav` via PowerShell). Expected: `_dry.wav`, `_mbv.wav`, `_diiv.wav`, `_slowdive.wav` written with sane peak/rms prints.

- [ ] **Step 5: Commit**

```
git add tools/render_demo.c
git commit -m "tools: trig/predelay overrides, mbv/diiv/slowdive acceptance presets, Reverson_mode"
```

---

## Task 12: VST — mode/trig/predelay params, 3-page editor, stereo path

**Files:** Modify `vst/PluginProcessor.h`, `vst/PluginProcessor.cpp`, `vst/PluginEditor.h`, `vst/PluginEditor.cpp`

- [ ] **Step 1: Processor params** — in `createParameterLayout`, after `add("duck", ...)` add:
```cpp
    add("mode", "Mode", 0.0f);
    add("trig", "Trig", 0.35f);
    add("predelay", "Predelay", 0.0f);
```
and change the mode param's range after the adds by replacing the `add` lambda body for mode with a direct construction instead: simplest — replace the `add("mode", ...)` line with:
```cpp
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mode", "Mode", juce::NormalisableRange<float>(0.0f, 1.0f, 0.2f), 0.0f));
```
(keep `add(...)` for trig/predelay). Remove the `add("mode", ...)` lambda call accordingly.

- [ ] **Step 2: Processor processBlock** — replace the body from the `Reverson_set_6knob` line through the sample loop with:
```cpp
    auto* pMode = apvts.getRawParameterValue("mode");
    auto* pTrig = apvts.getRawParameterValue("trig");
    auto* pPredelay = apvts.getRawParameterValue("predelay");

    Reverson_set_6knob(core, *pMix, *pRev, *pSpace, *pTone, *pGrain, *pDuck);
    Reverson_set_param(core, REVERSON_PARAM_TRIG, *pTrig);
    Reverson_set_param(core, REVERSON_PARAM_PREDELAY, *pPredelay);
    {
        int mode = (int)(*pMode * 5.0f + 0.5f);   /* 0 = custom knobs */
        if (mode >= 1) {
            ReversonParams mp;
            Reverson_mode(mode, &mp);
            Reverson_set_param(core, REVERSON_PARAM_MIX, mp.mix);
            Reverson_set_param(core, REVERSON_PARAM_DECAY, mp.decay);
            Reverson_set_param(core, REVERSON_PARAM_TONE, mp.tone);
            Reverson_set_param(core, REVERSON_PARAM_REVLEN, mp.revlen);
            Reverson_set_param(core, REVERSON_PARAM_DUCK, mp.duck);
            Reverson_set_param(core, REVERSON_PARAM_GATE, mp.gate);
            Reverson_set_param(core, REVERSON_PARAM_SHAPE, mp.shape);
            Reverson_set_param(core, REVERSON_PARAM_MOD, mp.mod);
            Reverson_set_param(core, REVERSON_PARAM_SAT, mp.sat);
            Reverson_set_param(core, REVERSON_PARAM_WIDTH, mp.width);
            Reverson_set_param(core, REVERSON_PARAM_DENSITY, mp.density);
            Reverson_set_param(core, REVERSON_PARAM_BASS, mp.bass);
            Reverson_set_param(core, REVERSON_PARAM_DIFFUSION, mp.diffusion);
        }
    }

    const int numSamples = buffer.getNumSamples();
    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float l = 0.0f, r = 0.0f;
        float xl = inL[i];
        float xr = (inR != nullptr) ? inR[i] : xl;
        Reverson_process_stereo(core, xl, xr, &l, &r);
        if (outR != nullptr) {
            outL[i] = l;
            outR[i] = r;
        } else {
            outL[i] = 0.5f * (l + r);
        }
    }
```
Remove the `monoIn` member from `PluginProcessor.h` and its uses.

- [ ] **Step 3: Editor 3 pages** — in `PluginEditor.h` change `static const char* ids[2][3];` → `[3][3]` and `names[2][3]` → `[3][3]`.
In `PluginEditor.cpp`:
```cpp
const char* ReversonAudioProcessorEditor::ids[3][3] = {
    {"mix", "rev", "space"},
    {"tone", "grain", "duck"},
    {"mode", "trig", "predelay"}
};
const char* ReversonAudioProcessorEditor::names[3][3] = {
    {"Mix", "Rev", "Space"},
    {"Tone", "Grain", "Duck"},
    {"Mode", "Trig", "Predelay"}
};
```
`setPage`: `setPage((currentPage + 1) % 3);` (the `onClick` lambda). In `paint`, for the Mode slot show the mode name instead of a percent: in the knob-slot loop, replace the value string computation with:
```cpp
        juce::String sv("--");
        if (auto* v = processor.apvts.getRawParameterValue(ids[currentPage][i])) {
            if (juce::String(ids[currentPage][i]) == "mode") {
                static const char* MODE_NAMES[6] = {"Off", "Wash", "Reverse", "Gated", "Shoegaze", "Space"};
                int mi = (int)(v->load() * 5.0f + 0.5f);
                if (mi < 0) mi = 0;
                if (mi > 5) mi = 5;
                sv = MODE_NAMES[mi];
            } else {
                sv = juce::String((int)(v->load() * 100.0f + 0.5f));
            }
        }
        g.drawText(sv, r, juce::Justification::centred, false);
```
and likewise for the focused value readout (mid section): if the focused id is "mode", draw the mode name instead of the number:
```cpp
    juce::String focusText;
    if (juce::String(ids[currentPage][focusedSlot]) == "mode") {
        static const char* MODE_NAMES[6] = {"Off", "Wash", "Reverse", "Gated", "Shoegaze", "Space"};
        int mi = (int)(focusVal * 5.0f + 0.5f);
        if (mi < 0) mi = 0;
        if (mi > 5) mi = 5;
        focusText = MODE_NAMES[mi];
    } else {
        focusText = juce::String((int)(focusVal * 100.0f + 0.5f));
    }
```
and draw `focusText` where the numeric string was drawn. Update the class header comment ("4 pages" line) to "3 pages x 3 knobs (P3 = Mode/Trig/Predelay)".

- [ ] **Step 4: Build the VST3** — `cmake -S . -B build -A x64 -DREVERSON_BUILD_VST=ON` (reconfigure with the option; JUCE 7.0.12 is fetched) then `cmake --build build --config Release --target ReversonVST_VST3`.
Expected: `build\vst\ReversonVST_artefacts\Release\VST3\Reverson.vst3` produced with no errors. (If the FetchContent download fails in the sandbox, note it and retry once with escalation; the core work is unaffected.)

- [ ] **Step 5: Commit**

```
git add vst/PluginProcessor.h vst/PluginProcessor.cpp vst/PluginEditor.h vst/PluginEditor.cpp
git commit -m "vst: mode/trig/predelay params, 3-page editor, stereo input path"
```

---

## Task 13: Docs + final sweep

- [ ] **Step 1:** Update `README.md`:
  - Features: add the true reverse-playback layer (Rev continuum), trigger sensitivity + predelay, wet HPF, crossfeed diffusion, 8-sample smoothing, stereo input.
  - Parameter table: 15 params, add `Trig` / `Predelay` rows; note `Over` is curve-owned.
  - 6-knob mapping section: add the P3 row (Mode / Trig / Predelay).
  - DSP architecture diagram: add the reverse layer, crossfeed, HPF.
  - ZDL status section: note the K9Probe flash-first plan for the G1on and that the ZDL build compiles with `REVERSON_ENABLE_FDN=0` (unchanged), state ≈160 KB.
  - Render tool usage line: add `trig predelay` keys and `mbv diiv slowdive` presets.
- [ ] **Step 2:** Update `core/reverson.h` doc comments (process-stereo, mode, param list) and `core/rev_swell.h` header comment (crossfeed).
- [ ] **Step 3:** Full final verification: rebuild Debug + run `ctest --output-on-failure`; rebuild Release VST3; render the three acceptance presets to `out/` for the user's listening pass.
- [ ] **Step 4: Commit**

```
git add README.md core/reverson.h core/rev_swell.h
git commit -m "docs: Reverson v2 core features, params, and G1on status"
```

---

## M3/M4 note (out of this plan's scope)

The 9-knob ZDL build, K9Probe flash run, and G1on hardware validation (spec milestones M3/M4) get their own plan after M1/M2 land, following `ZoomMultistompZDL/src/custom/*` patterns and `research_docs/docs_INSTALLING-ZDLS.md`. The pedal hardware arrives tonight; the flash checklist will be ready before it does.

## Self-review notes

- Spec coverage: reverse layer (§1 -> T8), crossfeed (§2 -> T2), HPF (§6 -> T9), envelope v2 (§3 -> T6), trigger+predelay (§4 -> T7), block smoothing (§5 -> T5), stereo API (§7 -> T9), Rev continuum (§8 -> T8), 15-param model + page 3 (§param -> T4/T12), presets (§presets -> T11), render tool (T11), VST (T12), tests (§testing -> T1-T10), README (T13). CPU/memory budget: buffer sharing + early-out + grid = T8/T2/T5.
- Placeholder scan: none — all code is concrete; the one empirical risk (crossfeed vs existing test_swell bounds) has an explicit triage step in Task 10 with a concrete fallback (`cf` clamp).
- Type consistency: `rev_swell_taps/diffuse` signatures match T2's tests and T8's call sites; `Reverson_test_env`, `Reverson_mode`, `Reverson_process_stereo`, `rev_env_set_thresh`, `rev_rev_set_preoff` declarations land in T4/T3/T1 and are used identically in later tasks.
