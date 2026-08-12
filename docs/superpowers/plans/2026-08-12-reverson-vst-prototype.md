# Reverson VST Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Windows VST3 prototype of the Reverson dynamic reverse reverb — a portable C99 DSP core (shared with the future ZDL port) wrapped in a JUCE plugin whose UI mirrors the Zoom G1on edit screen (3 knobs + page button + bypass).

**Architecture:** A dependency-free portable C core (`core/`) does all DSP with ZDL-safe rules (no heap, no runtime division in the audio path, no `double`, no `exp`/`tanh`; state owned by the caller). Two shells consume it: VST3 (this plan, JUCE) and later ZDL (TI C6000, separate plan). Each DSP module lives in its own `.c`/`.h` with a dedicated test binary run via CTest.

**Tech Stack:** C99, CMake 4.x, MSVC (Visual Studio 2022 Build Tools, present at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`), JUCE 7.0.12 (fetched via CMake FetchContent), CTest for unit tests.

**Prerequisites (verified on 2026-08-12):** `cmake 4.4.2` on PATH; VS 2022 Build Tools with VC tools installed; `git`; network access for JUCE fetch.

---

## Controller Amendments (from Task 0 code review, 2026-08-12)

These amendments are authoritative for Tasks 1-8:

1. Task 0 assertion for `rev_coeff_from_tc(9.0f)` is the range `(0.09, 0.11)` (1/10 == 0.1); the original `(0.9, 0.91)` was an arithmetic slip.
2. Test sources need the `core/` include path: always use `target_include_directories(<test> PRIVATE core)`.
3. Use `include(CTest)` and register tests under `if(BUILD_TESTING)`.
4. Use this helper for test registration (equivalent to the verbose `add_executable`+`target_link_libraries`+`add_test` blocks shown in Tasks 1-8):
   ```cmake
   function(reverson_add_test name)
       add_executable(${name} tests/${name}.c)
       target_include_directories(${name} PRIVATE core)
       target_link_libraries(${name} PRIVATE reverson_core)
       add_test(NAME ${name} COMMAND ${name})
   endfunction()
   ```
5. Add warnings to `reverson_core` and all test targets: MSVC `/W4`, GCC/Clang `-Wall -Wextra`.
6. Task 1 executed `rev_delay_read` as `idx - 1u - delay_samples` (delay 0 == just-written sample). The plan text `idx - delay_samples` was an off-by-one, corrected during execution; later tasks are consistent with this semantic.
## File Structure

```
zoomreverse/
├── CMakeLists.txt                  # top-level: tests + optional VST target
├── core/
│   ├── reverson.h                  # public plugin API (params, init, process)
│   ├── reverson.c                  # composition: smoothing + reverse + env + fdn + dynamics + wet
│   ├── rev_util.h                  # inline helpers (clamp, min, max, abs, coeff)
│   ├── rev_delay.h / rev_delay.c   # power-of-2 circular delay line (mask, no modulo)
│   ├── rev_env.h / rev_env.c       # envelope follower + onset detector
│   ├── rev_rev.h / rev_rev.c       # reverse engine (record + reverse read + swell + crossfade + norm)
│   └── rev_fdn.h / rev_fdn.c       # 8-line FDN tail (Householder feedback, damping, LFO, stereo)
├── tests/
│   ├── test_util.c
│   ├── test_delay.c
│   ├── test_env.c
│   ├── test_rev.c
│   ├── test_fdn.c
│   └── test_core.c
├── vst/
│   ├── CMakeLists.txt              # juce_add_plugin(ReversonVST ...)
│   ├── PluginProcessor.h / PluginProcessor.cpp
│   └── PluginEditor.h / PluginEditor.cpp
└── docs/superpowers/               # spec + this plan
```

Interfaces: `rev_*` modules are independent and unit-tested in isolation; `reverson.c` composes them and exposes the only public API (`core/reverson.h`). The VST shell never touches `rev_*` directly.

---

## Task 0: Repo skeleton, CMake, and test harness

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/test_util.c`
- Create: `core/rev_util.h`

- [ ] **Step 1: Write the failing test**

`tests/test_util.c`:
```c
#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    CHECK(rev_clampf(0.5f, 0.0f, 1.0f) == 0.5f);
    CHECK(rev_clampf(-0.2f, 0.0f, 1.0f) == 0.0f);
    CHECK(rev_clampf(1.5f, 0.0f, 1.0f) == 1.0f);
    CHECK(rev_minf(2.0f, 3.0f) == 2.0f);
    CHECK(rev_maxf(2.0f, 3.0f) == 3.0f);
    CHECK(rev_absf(-4.0f) == 4.0f);
    CHECK(rev_coeff_from_tc(0.0f) == 1.0f);
    CHECK(rev_coeff_from_tc(9.0f) > 0.09f && rev_coeff_from_tc(9.0f) < 0.11f);
    if (fails == 0) { printf("test_util PASS\n"); return 0; }
    printf("test_util FAILED (%d)\n", fails);
    return 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_util` (after configuring)
Expected: FAIL — `rev_util.h` does not exist yet (compile error).

- [ ] **Step 3: Write minimal implementation**

`core/rev_util.h`:
```c
#ifndef REV_UTIL_H
#define REV_UTIL_H

static inline float rev_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
static inline float rev_minf(float a, float b) { return a < b ? a : b; }
static inline float rev_maxf(float a, float b) { return a > b ? a : b; }
static inline float rev_absf(float x) { return x < 0.0f ? -x : x; }
/* one-pole coefficient from a time constant in samples; 1.0 == instant */
static inline float rev_coeff_from_tc(float tc_samples) {
    return 1.0f / (tc_samples + 1.0f);
}
#endif
```

- [ ] **Step 4: Top-level CMake**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.24)
project(reverson LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(REVERSON_BUILD_VST "Build the VST3 shell" OFF)

if(REVERSON_BUILD_VST)
    add_subdirectory(vst)
endif()

enable_testing()
add_executable(test_util tests/test_util.c)
target_include_directories(test_util PRIVATE core)
add_test(NAME test_util COMMAND test_util)
```

- [ ] **Step 5: Configure, build, run test**

Run:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target test_util
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `test_util PASS`, CTest reports 1/1 passed.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt core/rev_util.h tests/test_util.c
git commit -m "chore: repo skeleton, CMake, util test harness"
```

---

## Task 1: Power-of-two circular delay line

**Files:**
- Create: `core/rev_delay.h`
- Create: `core/rev_delay.c`
- Create: `tests/test_delay.c`

- [ ] **Step 1: Write the failing test**

`tests/test_delay.c`:
```c
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target test_delay`
Expected: FAIL — `rev_delay.h` missing.

- [ ] **Step 3: Write implementation**

`core/rev_delay.h`:
```c
#ifndef REV_DELAY_H
#define REV_DELAY_H
#include <stdint.h>

/* Power-of-two circular delay line. len must be a power of two.
   Index math uses & mask so no modulo instruction is emitted (ZDL-safe). */
typedef struct {
    float* buf;
    uint32_t len;
    uint32_t mask;
    uint32_t idx; /* write index */
} RevDelay;

void rev_delay_init(RevDelay* d, float* mem, uint32_t len_pow2);
void rev_delay_clear(RevDelay* d);
void rev_delay_write(RevDelay* d, float v);
/* delay_samples in [1, len); returns 0.0 while the line has no history */
float rev_delay_read(const RevDelay* d, uint32_t delay_samples);
#endif
```

`core/rev_delay.c`:
```c
#include "rev_delay.h"
#include <string.h>

void rev_delay_init(RevDelay* d, float* mem, uint32_t len_pow2) {
    d->buf = mem;
    d->len = len_pow2;
    d->mask = len_pow2 - 1u;
    d->idx = 0u;
}
void rev_delay_clear(RevDelay* d) {
    memset(d->buf, 0, d->len * sizeof(float));
    d->idx = 0u;
}
void rev_delay_write(RevDelay* d, float v) {
    d->buf[d->idx & d->mask] = v;
    d->idx++;
}
float rev_delay_read(const RevDelay* d, uint32_t delay_samples) {
    /* read the sample written `delay_samples` samples ago */
    uint32_t i = d->idx - delay_samples; /* unsigned wraparound is defined */
    return d->buf[i & d->mask];
}
```

- [ ] **Step 4: Register test in CMake**

Append to `CMakeLists.txt` (after the `test_util` block):
```cmake
add_library(reverson_core STATIC core/rev_delay.c)
target_include_directories(reverson_core PUBLIC core)
add_executable(test_delay tests/test_delay.c)
target_link_libraries(test_delay PRIVATE reverson_core)
add_test(NAME test_delay COMMAND test_delay)
```

- [ ] **Step 5: Build and run**

Run:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target test_delay
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `test_delay PASS`, 2/2 CTest tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/rev_delay.h core/rev_delay.c tests/test_delay.c CMakeLists.txt
git commit -m "feat: power-of-two delay line"
```

---

## Task 2: Envelope follower + onset detector

**Files:**
- Create: `core/rev_env.h`
- Create: `core/rev_env.c`
- Create: `tests/test_env.c`

- [ ] **Step 1: Write the failing test**

`tests/test_env.c`:
```c
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target test_env`
Expected: FAIL — `rev_env.h` missing.

- [ ] **Step 3: Write implementation**

`core/rev_env.h`:
```c
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
```

`core/rev_env.c`:
```c
#include "rev_env.h"
#include "rev_util.h"

void rev_env_init(RevEnv* e, float sample_rate) {
    e->env = 0.0f;
    e->attack_coef = rev_coeff_from_tc(0.002f * sample_rate);  /* 2 ms attack */
    e->release_coef = rev_coeff_from_tc(0.15f * sample_rate);  /* 150 ms release */
    e->onset_thresh = 0.01f;
    e->was_playing = 0;
    e->onset = 0;
}
void rev_env_process(RevEnv* e, float x) {
    float a = rev_absf(x);
    float coef = (a > e->env) ? e->attack_coef : e->release_coef;
    e->env += (a - e->env) * coef;
    int playing = e->env > e->onset_thresh;
    e->onset = (playing && !e->was_playing) ? 1 : 0;
    e->was_playing = playing;
}
float rev_env_value(const RevEnv* e) { return e->env; }
int rev_env_onset(const RevEnv* e) { return e->onset; }
```

- [ ] **Step 4: Register test in CMake**

Append to `CMakeLists.txt`:
```cmake
target_sources(reverson_core PRIVATE core/rev_env.c)
add_executable(test_env tests/test_env.c)
target_link_libraries(test_env PRIVATE reverson_core)
add_test(NAME test_env COMMAND test_env)
```

- [ ] **Step 5: Build and run**

Run:
```powershell
cmake --build build --config Debug --target test_env
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `test_env PASS`, 3/3 pass.

- [ ] **Step 6: Commit**

```bash
git add core/rev_env.h core/rev_env.c tests/test_env.c CMakeLists.txt
git commit -m "feat: envelope follower with onset detection"
```

---

## Task 3: Reverse engine

**Files:**
- Create: `core/rev_rev.h`
- Create: `core/rev_rev.c`
- Create: `tests/test_rev.c`

- [ ] **Step 1: Write the failing test**

`tests/test_rev.c`:
```c
#include "rev_rev.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
#define CLOSE(a,b,tol) CHECK(((a) > (b) - (tol)) && ((a) < (b) + (tol)))

int main(void) {
    float mem[64];
    RevRev r;
    rev_rev_init(&r, mem, 64u, 44100.0f);
    rev_rev_clear(&r);

    /* record a rising ramp 0..9 */
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);

    /* trigger a 10-sample reverse segment, linear swell, minimal crossfade */
    rev_rev_trigger(&r, 10u, 1u, 1);

    float out[24];
    for (int i = 0; i < 24; ++i) out[i] = rev_rev_process(&r);

    /* first sample is the newest (9) at swell env 0.1, norm gain ~1.0 (peak ~9 -> target 0.1, still smoothing) */
    CLOSE(out[0], 0.9f, 0.15f);
    /* last sample of the segment crossfades to the head (9) at env 1.0 */
    CLOSE(out[9], 9.0f, 0.5f);
    /* the segment wrapped: second segment starts like the first */
    CLOSE(out[10], out[0], 0.2f);
    /* squared swell is smaller early on */
    rev_rev_clear(&r);
    for (int i = 0; i < 10; ++i) rev_rev_write(&r, (float)i);
    rev_rev_trigger(&r, 10u, 1u, 2);
    float first_sq = rev_rev_process(&r); /* 9 * 0.1^2 = 0.09 */
    CLOSE(first_sq, 0.09f, 0.05f);

    if (fails == 0) { printf("test_rev PASS\n"); return 0; }
    printf("test_rev FAILED (%d)\n", fails);
    return 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target test_rev`
Expected: FAIL — `rev_rev.h` missing.

- [ ] **Step 3: Write implementation**

`core/rev_rev.h`:
```c
#ifndef REV_REV_H
#define REV_REV_H
#include <stdint.h>

/* Reverse engine: circular record buffer + backwards read head.
   Trigger mode: on each trigger a fresh segment of `seg_len` samples is
   replayed in reverse with a swell envelope (shape 1..4) and a crossfade
   into the segment head near the wrap to avoid clicks. Output is normalized
   against the last recorded peak so swells stay audible without clipping.
   All audio-path math is multiply/add only (division only at trigger time). */
typedef struct {
    float* buf;
    uint32_t buf_len;
    uint32_t mask;
    uint32_t write_idx;
    uint32_t seg_pos;
    uint32_t seg_len;
    float seg_peak;
    float norm_gain;
    float norm_target;
    float norm_coef;
    float env;
    float env_inc;
    uint32_t cross_len;
    float cross_pos;
    float cross_inc;
    int shape;
} RevRev;

void rev_rev_init(RevRev* r, float* mem, uint32_t buf_len_pow2, float sample_rate);
void rev_rev_clear(RevRev* r);
void rev_rev_trigger(RevRev* r, uint32_t seg_len, uint32_t cross_samples, int shape);
void rev_rev_write(RevRev* r, float x);
float rev_rev_process(RevRev* r);
#endif
```

`core/rev_rev.c`:
```c
#include "rev_rev.h"
#include "rev_util.h"
#include <string.h>

void rev_rev_init(RevRev* r, float* mem, uint32_t buf_len_pow2, float sample_rate) {
    r->buf = mem;
    r->buf_len = buf_len_pow2;
    r->mask = buf_len_pow2 - 1u;
    r->write_idx = 0u;
    r->seg_pos = 0u;
    r->seg_len = 1u;
    r->seg_peak = 0.0f;
    r->norm_gain = 1.0f;
    r->norm_target = 1.0f;
    r->norm_coef = rev_coeff_from_tc(0.05f * sample_rate);
    r->env = 0.0f;
    r->env_inc = 1.0f;
    r->cross_len = 1u;
    r->cross_pos = 0.0f;
    r->cross_inc = 1.0f;
    r->shape = 1;
}

void rev_rev_clear(RevRev* r) {
    memset(r->buf, 0, r->buf_len * sizeof(float));
    r->write_idx = 0u;
    r->seg_pos = 0u;
    r->seg_peak = 0.0f;
    r->norm_gain = 1.0f;
    r->norm_target = 1.0f;
    r->env = 0.0f;
    r->cross_pos = 0.0f;
}

/* Division here is control-rate (once per trigger), not per-sample. */
void rev_rev_trigger(RevRev* r, uint32_t seg_len, uint32_t cross_samples, int shape) {
    if (seg_len < 2u) seg_len = 2u;
    r->seg_len = seg_len;
    r->seg_pos = 0u;
    r->env = 0.0f;
    r->env_inc = 1.0f / (float)seg_len;
    r->cross_len = cross_samples < 1u ? 1u : cross_samples;
    if (r->cross_len > r->seg_len) r->cross_len = r->seg_len;
    r->cross_pos = 0.0f;
    r->cross_inc = 1.0f / (float)r->cross_len;
    r->shape = (shape < 1) ? 1 : ((shape > 4) ? 4 : shape);
    r->norm_target = rev_clampf(0.9f / (r->seg_peak + 1e-6f), 0.1f, 3.0f);
}

void rev_rev_write(RevRev* r, float x) {
    r->buf[r->write_idx & r->mask] = x;
    r->write_idx++;
    r->seg_peak *= 0.9999f; /* peak-hold decay */
    float a = rev_absf(x);
    if (a > r->seg_peak) r->seg_peak = a;
}

float rev_rev_process(RevRev* r) {
    uint32_t read_idx = (r->write_idx - 1u - r->seg_pos) & r->mask;
    float rev = r->buf[read_idx];

    r->env += r->env_inc;
    if (r->env > 1.0f) r->env = 1.0f;
    float env = r->env;
    if (r->shape == 2) env = env * env;
    else if (r->shape == 3) env = env * env * env;
    else if (r->shape == 4) { float e2 = env * env; env = e2 * e2; }

    if (r->seg_pos >= r->seg_len - r->cross_len) {
        uint32_t head_idx = (r->write_idx - 1u) & r->mask;
        r->cross_pos += r->cross_inc;
        if (r->cross_pos > 1.0f) r->cross_pos = 1.0f;
        rev = rev * (1.0f - r->cross_pos) + r->buf[head_idx] * r->cross_pos;
    }

    r->seg_pos++;
    if (r->seg_pos >= r->seg_len) {
        r->seg_pos = 0u;
        r->env = 0.0f;
        r->cross_pos = 0.0f;
    }

    r->norm_gain += (r->norm_target - r->norm_gain) * r->norm_coef;
    return rev * env * r->norm_gain;
}
```

- [ ] **Step 4: Register test in CMake**

Append to `CMakeLists.txt`:
```cmake
target_sources(reverson_core PRIVATE core/rev_rev.c)
add_executable(test_rev tests/test_rev.c)
target_link_libraries(test_rev PRIVATE reverson_core)
add_test(NAME test_rev COMMAND test_rev)
```

- [ ] **Step 5: Build and run**

Run:
```powershell
cmake --build build --config Debug --target test_rev
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `test_rev PASS`, 4/4 pass. If the crossfade tolerance fails, adjust the CLOSE tolerance — the crossfade intentionally blends the tail into the head (verify the property, not the exact constant).

- [ ] **Step 6: Commit**

```bash
git add core/rev_rev.h core/rev_rev.c tests/test_rev.c CMakeLists.txt
git commit -m "feat: reverse engine with swell envelope and normalization"
```

---

## Task 4: FDN tail

**Files:**
- Create: `core/rev_fdn.h`
- Create: `core/rev_fdn.c`
- Create: `tests/test_fdn.c`

- [ ] **Step 1: Write the failing test**

`tests/test_fdn.c`:
```c
#include "rev_fdn.h"
#include "rev_util.h"
#include <stdio.h>

static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    float mem[8192];
    uint32_t lens[REV_FDN_LINES] = {64,128,128,256,256,512,512,1024};
    RevFdn f;
    rev_fdn_init(&f, mem, lens, 44100.0f);
    rev_fdn_set(&f, 0.5f, 0.5f, 0.0f);

    /* impulse -> tail must decay and stay bounded */
    float l, r;
    rev_fdn_process(&f, 1.0f, &l, &r);
    float early = 0.0f, late = 0.0f, maxv = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        rev_fdn_process(&f, 0.0f, &l, &r);
        float e = l * l + r * r;
        if (i < 500) early += e;
        if (i >= 20000) late += e;
        if (rev_absf(l) > maxv) maxv = rev_absf(l);
        if (rev_absf(r) > maxv) maxv = rev_absf(r);
    }
    CHECK(early > late);
    CHECK(maxv < 10.0f);

    /* stereo decorrelation: even lines -> L, odd -> R; lengths differ, so L != R over time */
    rev_fdn_clear(&f);
    float diff_sum = 0.0f;
    for (int i = 0; i < 4410; ++i) {
        rev_fdn_process(&f, 0.3f, &l, &r);
        diff_sum += rev_absf(l - r);
    }
    CHECK(diff_sum > 1.0f);

    /* decay = 0 -> single pass only, tail dies quickly */
    rev_fdn_clear(&f);
    rev_fdn_set(&f, 0.0f, 0.5f, 0.0f);
    rev_fdn_process(&f, 1.0f, &l, &r);
    for (int i = 0; i < 6000; ++i) rev_fdn_process(&f, 0.0f, &l, &r); /* flush impulse echoes */
    float late2 = 0.0f;
    for (int i = 0; i < 14000; ++i) { rev_fdn_process(&f, 0.0f, &l, &r); late2 += l * l + r * r; }
    CHECK(late2 < 1e-3f);

    if (fails == 0) { printf("test_fdn PASS\n"); return 0; }
    printf("test_fdn FAILED (%d)\n", fails);
    return 1;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --config Debug --target test_fdn`
Expected: FAIL — `rev_fdn.h` missing.

- [ ] **Step 3: Write implementation**

`core/rev_fdn.h`:
```c
#ifndef REV_FDN_H
#define REV_FDN_H
#include <stdint.h>
#include "rev_delay.h"

#define REV_FDN_LINES 8

/* 8-line Feedback Delay Network.
   Feedback uses the Householder matrix (2/N * (sum x) - x) with N=8,
   implemented as multiply/add only (0.25 == 2/8 is a compile-time constant).
   Even lines feed L, odd lines feed R (decorrelated -> stereo width).
   A slow triangle LFO dithers the read delay for a "living" tail. */
typedef struct {
    RevDelay line[REV_FDN_LINES];
    float state[REV_FDN_LINES];
    uint32_t base_delay[REV_FDN_LINES];
    float fb_gain;
    float damp_coef;
    float lfo_phase;
    float lfo_inc;
    float mod_depth;
    float out_gain;
} RevFdn;

void rev_fdn_init(RevFdn* f, float* mem, const uint32_t* len_pow2, float sample_rate);
void rev_fdn_clear(RevFdn* f);
/* decay/tone/mod in [0,1] */
void rev_fdn_set(RevFdn* f, float decay, float tone, float mod);
void rev_fdn_process(RevFdn* f, float in, float* out_l, float* out_r);
#endif
```

`core/rev_fdn.c`:
```c
#include "rev_fdn.h"
#include "rev_util.h"
#include <string.h>

void rev_fdn_init(RevFdn* f, float* mem, const uint32_t* len_pow2, float sample_rate) {
    float* p = mem;
    for (int i = 0; i < REV_FDN_LINES; ++i) {
        rev_delay_init(&f->line[i], p, len_pow2[i]);
        p += len_pow2[i];
        f->state[i] = 0.0f;
        f->base_delay[i] = len_pow2[i] - 16u; /* headroom for LFO shift */
    }
    f->fb_gain = 0.5f;
    f->damp_coef = 0.5f;
    f->lfo_phase = 0.0f;
    f->lfo_inc = 0.15f / sample_rate;
    f->mod_depth = 0.0f;
    f->out_gain = 0.125f;
}

void rev_fdn_clear(RevFdn* f) {
    for (int i = 0; i < REV_FDN_LINES; ++i) {
        rev_delay_clear(&f->line[i]);
        f->state[i] = 0.0f;
    }
    f->lfo_phase = 0.0f;
}

void rev_fdn_set(RevFdn* f, float decay, float tone, float mod) {
    decay = rev_clampf(decay, 0.0f, 1.0f);
    tone = rev_clampf(tone, 0.0f, 1.0f);
    mod = rev_clampf(mod, 0.0f, 1.0f);
    f->fb_gain = 0.92f * decay * decay;  /* musical taper */
    f->damp_coef = 0.05f + 0.9f * tone;  /* 0=dark, 1=bright */
    f->mod_depth = 8.0f * mod;
}

void rev_fdn_process(RevFdn* f, float in, float* out_l, float* out_r) {
    float sum = 0.0f;
    for (int i = 0; i < REV_FDN_LINES; ++i) sum += f->state[i];
    float common = 0.25f * f->fb_gain * sum;  /* Householder: 2/8 * g * sum */
    float fb[REV_FDN_LINES];
    for (int i = 0; i < REV_FDN_LINES; ++i) fb[i] = common - f->fb_gain * f->state[i];

    f->lfo_phase += f->lfo_inc;
    if (f->lfo_phase > 1.0f) f->lfo_phase -= 1.0f;
    float tri = 2.0f * f->lfo_phase - 1.0f;  /* -1..1 */

    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < REV_FDN_LINES; ++i) {
        rev_delay_write(&f->line[i], in + fb[i]);
        int shift = (int)(f->mod_depth * tri);
        uint32_t delay = (uint32_t)((int)f->base_delay[i] + shift);
        float v = rev_delay_read(&f->line[i], delay);
        f->state[i] += (v - f->state[i]) * f->damp_coef;
        if ((i & 1u) == 0u) l += f->state[i]; else r += f->state[i];
    }
    *out_l = l * f->out_gain;
    *out_r = r * f->out_gain;
}
```

- [ ] **Step 4: Register test in CMake**

Append to `CMakeLists.txt`:
```cmake
target_sources(reverson_core PRIVATE core/rev_fdn.c)
add_executable(test_fdn tests/test_fdn.c)
target_link_libraries(test_fdn PRIVATE reverson_core)
add_test(NAME test_fdn COMMAND test_fdn)
```

- [ ] **Step 5: Build and run**

Run:
```powershell
cmake --build build --config Debug --target test_fdn
ctest --test-dir build -C Debug --output-on-failure
```
Expected: `test_fdn PASS`, 5/5 pass.

- [ ] **Step 6: Commit**

```bash
git add core/rev_fdn.h core/rev_fdn.c tests/test_fdn.c CMakeLists.txt
git commit -m "feat: 8-line FDN tail with Householder feedback"
```

---


## Task 5: Public API + full core composition

**Files:**
- Modify: `core/rev_util.h` (add `rev_next_pow2`, `rev_softclip`)
- Modify: `tests/test_util.c` (test the new helpers)
- Create: `core/reverson.h`
- Create: `core/reverson.c`
- Create: `tests/test_core.c`

- [ ] **Step 1: Extend the failing tests**

Append to `tests/test_util.c` (inside `main`, before the pass/fail print):
```c
    CHECK(rev_next_pow2(1u) == 1u);
    CHECK(rev_next_pow2(2u) == 2u);
    CHECK(rev_next_pow2(3u) == 4u);
    CHECK(rev_next_pow2(97020u) == 131072u);
    CHECK(rev_softclip(0.0f) == 0.0f);
    CHECK(rev_softclip(1.0f) == 1.0f);
    CHECK(rev_softclip(2.0f) == 1.0f);
    CHECK(rev_softclip(-1.0f) == -1.0f);
    CHECK(rev_softclip(0.5f) > 0.5f);
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build --config Debug --target test_util`
Expected: FAIL — `rev_next_pow2` / `rev_softclip` not declared.

- [ ] **Step 3: Extend `core/rev_util.h`**

Append inside the header (after `rev_coeff_from_tc`):
```c
static inline uint32_t rev_next_pow2(uint32_t v) {
    if (v == 0u) return 1u;
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1u;
}
/* cubic soft clip (no exp/tanh; ZDL-safe). Maps [-1,1] -> [-1,1], gentle boost then saturate. */
static inline float rev_softclip(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return 1.5f * (x - x * x * x * 0.333333f);
}
```

- [ ] **Step 4: Write the public API header**

`core/reverson.h`:
```c
#ifndef REVERSON_H
#define REVERSON_H
#include <stdint.h>

#define REVERSON_NUM_PARAMS 10u
#define REVERSON_MAX_REV_S 2.0f

typedef enum {
    REVERSON_PARAM_MIX = 0,
    REVERSON_PARAM_DECAY,
    REVERSON_PARAM_TONE,
    REVERSON_PARAM_REVLEN,
    REVERSON_PARAM_DUCK,
    REVERSON_PARAM_GATE,
    REVERSON_PARAM_SHAPE,
    REVERSON_PARAM_MOD,
    REVERSON_PARAM_SAT,
    REVERSON_PARAM_WIDTH
} ReversonParam;

typedef struct Reverson Reverson;

/* All memory is caller-provided; the core never allocates (ZDL-safe).
   mem must stay valid for the lifetime of the instance. */
uint32_t Reverson_state_size(float sample_rate);
Reverson* Reverson_init(void* mem, uint32_t mem_size, float sample_rate);
void Reverson_reset(Reverson* r);
void Reverson_set_param(Reverson* r, ReversonParam p, float v); /* v in [0,1] */
float Reverson_get_param(const Reverson* r, ReversonParam p);
/* process one mono sample -> stereo out */
void Reverson_process(Reverson* r, float in, float* out_l, float* out_r);
#endif
```

- [ ] **Step 5: Write the implementation**

`core/reverson.c`:
```c
#include "reverson.h"
#include "rev_util.h"
#include "rev_env.h"
#include "rev_rev.h"
#include "rev_fdn.h"
#include <string.h>

struct Reverson {
    float sample_rate;
    ReversonParams target;
    ReversonParams cur;
    float smooth_coef;
    RevEnv env;
    RevRev rev;
    RevFdn fdn;
    float duck_gain_sm;
    float wet_lp_l, wet_lp_r;
    uint32_t rev_len_max;
    uint32_t rev_cross_samples;
};

uint32_t Reverson_state_size(float sample_rate) {
    uint32_t rev_pow2 = rev_next_pow2((uint32_t)(sample_rate * 2.2f));
    uint32_t fdn_total = 64u + 128u + 128u + 256u + 256u + 512u + 512u + 1024u;
    return (uint32_t)(sizeof(Reverson) + rev_pow2 * sizeof(float) + fdn_total * sizeof(float) + 64u);
}

Reverson* Reverson_init(void* mem, uint32_t mem_size, float sample_rate) {
    uint32_t need = Reverson_state_size(sample_rate);
    if (mem == NULL || mem_size < need) return NULL;
    Reverson* r = (Reverson*)mem;
    memset(r, 0, sizeof(Reverson));
    r->sample_rate = sample_rate;
    r->rev_len_max = (uint32_t)(sample_rate * REVERSON_MAX_REV_S);
    uint32_t rev_pow2 = rev_next_pow2((uint32_t)(sample_rate * 2.2f));
    float* p = (float*)((uint8_t*)mem + sizeof(Reverson));
    rev_env_init(&r->env, sample_rate);
    rev_rev_init(&r->rev, p, rev_pow2, sample_rate);
    p += rev_pow2;
    {
        static const uint32_t fdn_pow2[REV_FDN_LINES] = {64,128,128,256,256,512,512,1024};
        rev_fdn_init(&r->fdn, p, fdn_pow2, sample_rate);
    }

    /* defaults tuned toward DIIV-style clean+spacious */
    r->target.mix = 0.55f;
    r->target.decay = 0.60f;
    r->target.tone = 0.60f;
    r->target.revlen = 0.40f;
    r->target.duck = 0.50f;
    r->target.gate = 0.00f;
    r->target.shape = 0.33f;
    r->target.mod = 0.35f;
    r->target.sat = 0.10f;
    r->target.width = 0.80f;
    r->cur = r->target;
    r->smooth_coef = rev_coeff_from_tc(0.005f * sample_rate);
    r->duck_gain_sm = 1.0f;
    r->rev_cross_samples = (uint32_t)(0.004f * sample_rate);
    Reverson_reset(r);
    return r;
}

void Reverson_reset(Reverson* r) {
    rev_rev_clear(&r->rev);
    rev_fdn_clear(&r->fdn);
    r->env.env = 0.0f;
    r->env.was_playing = 0;
    r->duck_gain_sm = 1.0f;
    r->wet_lp_l = 0.0f;
    r->wet_lp_r = 0.0f;
}

void Reverson_set_param(Reverson* r, ReversonParam p, float v) {
    v = rev_clampf(v, 0.0f, 1.0f);
    switch (p) {
        case REVERSON_PARAM_MIX:   r->target.mix = v; break;
        case REVERSON_PARAM_DECAY: r->target.decay = v; break;
        case REVERSON_PARAM_TONE:  r->target.tone = v; break;
        case REVERSON_PARAM_REVLEN:r->target.revlen = v; break;
        case REVERSON_PARAM_DUCK:  r->target.duck = v; break;
        case REVERSON_PARAM_GATE:  r->target.gate = v; break;
        case REVERSON_PARAM_SHAPE: r->target.shape = v; break;
        case REVERSON_PARAM_MOD:   r->target.mod = v; break;
        case REVERSON_PARAM_SAT:   r->target.sat = v; break;
        case REVERSON_PARAM_WIDTH: r->target.width = v; break;
    }
}

float Reverson_get_param(const Reverson* r, ReversonParam p) {
    switch (p) {
        case REVERSON_PARAM_MIX:   return r->target.mix;
        case REVERSON_PARAM_DECAY: return r->target.decay;
        case REVERSON_PARAM_TONE:  return r->target.tone;
        case REVERSON_PARAM_REVLEN:return r->target.revlen;
        case REVERSON_PARAM_DUCK:  return r->target.duck;
        case REVERSON_PARAM_GATE:  return r->target.gate;
        case REVERSON_PARAM_SHAPE: return r->target.shape;
        case REVERSON_PARAM_MOD:   return r->target.mod;
        case REVERSON_PARAM_SAT:   return r->target.sat;
        case REVERSON_PARAM_WIDTH: return r->target.width;
    }
    return 0.0f;
}

void Reverson_process(Reverson* r, float in, float* out_l, float* out_r) {
    float c = r->smooth_coef;
    r->cur.mix += (r->target.mix - r->cur.mix) * c;
    r->cur.decay += (r->target.decay - r->cur.decay) * c;
    r->cur.tone += (r->target.tone - r->cur.tone) * c;
    r->cur.revlen += (r->target.revlen - r->cur.revlen) * c;
    r->cur.duck += (r->target.duck - r->cur.duck) * c;
    r->cur.gate += (r->target.gate - r->cur.gate) * c;
    r->cur.shape += (r->target.shape - r->cur.shape) * c;
    r->cur.mod += (r->target.mod - r->cur.mod) * c;
    r->cur.sat += (r->target.sat - r->cur.sat) * c;
    r->cur.width += (r->target.width - r->cur.width) * c;

    rev_env_process(&r->env, in);
    if (rev_env_onset(&r->env)) {
        uint32_t seg_len = (uint32_t)((0.05f + 1.95f * r->cur.revlen) * r->sample_rate);
        if (seg_len > r->rev_len_max) seg_len = r->rev_len_max;
        int sh = 1 + (int)(3.99f * r->cur.shape);
        rev_rev_trigger(&r->rev, seg_len, r->rev_cross_samples, sh);
    }
    rev_rev_write(&r->rev, in);
    float rev_sig = rev_rev_process(&r->rev);

    float env = rev_env_value(&r->env);
    float duck_gain = 1.0f - r->cur.duck * env;
    if (r->cur.gate > 0.01f) {
        float th = 0.05f + 0.5f * r->cur.gate;
        float g = (env < th) ? 1.0f : 0.0f;
        duck_gain *= g;
    }
    r->duck_gain_sm += (duck_gain - r->duck_gain_sm) * c;
    float wet_in = rev_sig * r->duck_gain_sm;

    rev_fdn_set(&r->fdn, r->cur.decay, r->cur.tone, r->cur.mod);
    float wet_l, wet_r;
    rev_fdn_process(&r->fdn, wet_in, &wet_l, &wet_r);

    float tc = 0.05f + 0.9f * r->cur.tone;
    r->wet_lp_l += (wet_l - r->wet_lp_l) * tc;
    r->wet_lp_r += (wet_r - r->wet_lp_r) * tc;
    wet_l = r->wet_lp_l;
    wet_r = r->wet_lp_r;

    float s = r->cur.sat;
    if (s > 0.001f) {
        float drive = 1.0f + 3.0f * s;
        wet_l = rev_softclip(wet_l * drive);
        wet_r = rev_softclip(wet_r * drive);
    }

    float mid = (wet_l + wet_r) * 0.5f;
    float side = (wet_l - wet_r) * 0.5f;
    wet_l = mid + side * r->cur.width;
    wet_r = mid - side * r->cur.width;

    float mix = r->cur.mix;
    *out_l = in * (1.0f - mix) + wet_l * mix;
    *out_r = in * (1.0f - mix) + wet_r * mix;
}
```

- [ ] **Step 6: Write the integration test**

`tests/test_core.c`:
```c
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

    CHECK(Reverson_get_param(r, REVERSON_PARAM_MIX) > 0.5f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_DECAY) == 0.6f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_GATE) == 0.0f);

    Reverson_set_param(r, REVERSON_PARAM_MIX, 2.0f);
    CHECK(Reverson_get_param(r, REVERSON_PARAM_MIX) == 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 0.55f);

    float l = 1.0f, rr = 1.0f;
    for (int i = 0; i < 1000; ++i) {
        Reverson_process(r, 0.0f, &l, &rr);
        CHECK(l == 0.0f && rr == 0.0f);
    }

    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_GATE, 0.0f);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.6f);
    float diff_sum = 0.0f;
    for (int i = 0; i < 8820; ++i) {
        float in = (i % 220 == 0) ? 0.8f : 0.0f;
        Reverson_process(r, in, &l, &rr);
        diff_sum += rev_absf(l - rr);
    }
    CHECK(diff_sum > 0.01f);

    Reverson_reset(r);
    Reverson_set_param(r, REVERSON_PARAM_MIX, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DUCK, 1.0f);
    Reverson_set_param(r, REVERSON_PARAM_DECAY, 0.2f);
    for (int i = 0; i < 5000; ++i) Reverson_process(r, 1.0f, &l, &rr); /* settle duck */
    float peak = 0.0f;
    for (int i = 0; i < 5000; ++i) {
        Reverson_process(r, 1.0f, &l, &rr);
        if (rev_absf(l) > peak) peak = rev_absf(l);
    }
    CHECK(peak < 0.05f);

    for (int i = 0; i < 44100; ++i) {
        float in = (float)((i * 7919) % 1000) / 500.0f - 1.0f;
        Reverson_process(r, in, &l, &rr);
        CHECK(l == l && rr == rr);
    }

    free(mem);
    if (fails == 0) { printf("test_core PASS\n"); return 0; }
    printf("test_core FAILED (%d)\n", fails);
    return 1;
}
```

- [ ] **Step 7: Register in CMake**

Append to `CMakeLists.txt`:
```cmake
target_sources(reverson_core PRIVATE core/reverson.c)
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE reverson_core)
add_test(NAME test_core COMMAND test_core)
```

- [ ] **Step 8: Build and run all tests**

Run:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```
Expected: 6/6 tests pass (`test_util`, `test_delay`, `test_env`, `test_rev`, `test_fdn`, `test_core`). If a property test is flaky, verify the property by inspection before adjusting the threshold — do not weaken a real constraint.

- [ ] **Step 9: Commit**

```bash
git add core tests CMakeLists.txt
git commit -m "feat: Reverson core composition (reverse + env + fdn + dynamics + wet)"
```

---

## Task 6: VST3 shell (JUCE)

**Files:**
- Create: `vst/CMakeLists.txt`
- Create: `vst/PluginProcessor.h`
- Create: `vst/PluginProcessor.cpp`

- [ ] **Step 1: Write the JUCE build file**

`vst/CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ReversonVST
    VENDOR_NAME "Reverson"
    PLUGIN_MANUFACTURER_CODE Revs
    PLUGIN_CODE Revn
    PRODUCT_NAME "Reverson"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_NEEDS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    SOURCES PluginProcessor.cpp PluginEditor.cpp
)
target_include_directories(ReversonVST PRIVATE ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(ReversonVST PRIVATE reverson_core)
```

- [ ] **Step 2: Write the processor header**

`vst/PluginProcessor.h`:
```cpp
#pragma once
#include <JuceHeader.h>
#include "reverson.h"
#include <vector>
#include <cstddef>

class ReversonAudioProcessor : public juce::AudioProcessor {
public:
    ReversonAudioProcessor();
    ~ReversonAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Reverson"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::vector<float> stateMem;
    Reverson* core = nullptr;
};
```

- [ ] **Step 3: Write the processor implementation**

`vst/PluginProcessor.cpp`:
```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <memory>

juce::AudioProcessorValueTreeState::ParameterLayout
ReversonAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto add = [&](const juce::String& id, const juce::String& name, float def) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), def));
    };
    add("mix", "Mix", 0.55f);
    add("decay", "Decay", 0.60f);
    add("tone", "Tone", 0.60f);
    add("revlen", "RevLen", 0.40f);
    add("duck", "Duck", 0.50f);
    add("gate", "Gate", 0.00f);
    return layout;
}

ReversonAudioProcessor::ReversonAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {}

void ReversonAudioProcessor::prepareToPlay(double sampleRate, int) {
    if (sampleRate < 8000.0 || sampleRate > 192000.0) sampleRate = 44100.0;
    uint32_t need = Reverson_state_size((float)sampleRate);
    stateMem.resize((need + sizeof(float) - 1u) / sizeof(float));
    core = Reverson_init(stateMem.data(), (uint32_t)(stateMem.size() * sizeof(float)), (float)sampleRate);
    if (core != nullptr) Reverson_reset(core);
}

void ReversonAudioProcessor::releaseResources() {
    core = nullptr;
    stateMem.clear();
}

void ReversonAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    if (core == nullptr) return;

    auto* pMix = apvts.getRawParameterValue("mix");
    auto* pDecay = apvts.getRawParameterValue("decay");
    auto* pTone = apvts.getRawParameterValue("tone");
    auto* pRevLen = apvts.getRawParameterValue("revlen");
    auto* pDuck = apvts.getRawParameterValue("duck");
    auto* pGate = apvts.getRawParameterValue("gate");
    Reverson_set_param(core, REVERSON_PARAM_MIX, *pMix);
    Reverson_set_param(core, REVERSON_PARAM_DECAY, *pDecay);
    Reverson_set_param(core, REVERSON_PARAM_TONE, *pTone);
    Reverson_set_param(core, REVERSON_PARAM_REVLEN, *pRevLen);
    Reverson_set_param(core, REVERSON_PARAM_DUCK, *pDuck);
    Reverson_set_param(core, REVERSON_PARAM_GATE, *pGate);

    const int numSamples = buffer.getNumSamples();
    const float* in = buffer.getReadPointer(0);
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float l = 0.0f, r = 0.0f;
        Reverson_process(core, in[i], &l, &r);
        outL[i] = l;
        if (outR != nullptr) outR[i] = r;
    }
}

juce::AudioProcessorEditor* ReversonAudioProcessor::createEditor() {
    return new ReversonAudioProcessorEditor(*this);
}

void ReversonAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ReversonAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new ReversonAudioProcessor();
}
```

- [ ] **Step 4: Build the VST (expect failure on missing editor)**

Run:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DREVERSON_BUILD_VST=ON
cmake --build build --config Debug --target ReversonVST
```
Expected: FAIL — `PluginEditor.h` missing (Task 7 creates it). This is the intended red state.

- [ ] **Step 5: Commit the shell**

```bash
git add vst/CMakeLists.txt vst/PluginProcessor.h vst/PluginProcessor.cpp
git commit -m "feat: JUCE VST3 shell with core wiring"
```

---

## Task 7: VST UI mirroring the G1on edit screen

**Files:**
- Create: `vst/PluginEditor.h`
- Create: `vst/PluginEditor.cpp`

- [ ] **Step 1: Write the editor header**

`vst/PluginEditor.h`:
```cpp
#pragma once
#include <JuceHeader.h>
#include <memory>

class ReversonAudioProcessor;

class ReversonAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit ReversonAudioProcessorEditor(ReversonAudioProcessor& p);
    ~ReversonAudioProcessorEditor() override = default;
    void resized() override;

private:
    void setPage(int page);
    ReversonAudioProcessor& processor;
    juce::Label title;
    juce::TextButton pageButton;
    juce::Slider knobs[3];
    juce::Label knobLabels[3];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[3];
    int currentPage = 0;
    static const char* ids[2][3];
    static const char* names[2][3];
};
```

- [ ] **Step 2: Write the editor implementation**

`vst/PluginEditor.cpp`:
```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

const char* ReversonAudioProcessorEditor::ids[2][3] = {
    {"mix", "decay", "tone"},
    {"revlen", "duck", "gate"}
};
const char* ReversonAudioProcessorEditor::names[2][3] = {
    {"Mix", "Decay", "Tone"},
    {"RevLen", "Duck", "Gate"}
};

ReversonAudioProcessorEditor::ReversonAudioProcessorEditor(ReversonAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    title.setText("Reverson", juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centred);
    title.setFont(juce::Font(20.0f, juce::Font::bold));
    addAndMakeVisible(title);

    pageButton.setButtonText("P1");
    pageButton.onClick = [this] { setPage(currentPage ^ 1); };
    addAndMakeVisible(pageButton);

    for (int i = 0; i < 3; ++i) {
        knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
        knobs[i].setRange(0.0, 1.0, 0.001);
        addAndMakeVisible(knobs[i]);
        knobLabels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(knobLabels[i]);
    }
    setPage(0);
    setSize(300, 230);
}

void ReversonAudioProcessorEditor::setPage(int page) {
    currentPage = page;
    pageButton.setButtonText(page == 0 ? "P1" : "P2");
    attachments[0].reset();
    attachments[1].reset();
    attachments[2].reset();
    for (int i = 0; i < 3; ++i) {
        knobLabels[i].setText(names[page][i], juce::dontSendNotification);
        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, ids[page][i], knobs[i]);
    }
}

void ReversonAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    title.setBounds(area.removeFromTop(30));
    auto knobArea = area.removeFromTop(160);
    int w = knobArea.getWidth() / 3;
    for (int i = 0; i < 3; ++i) {
        auto k = knobArea.removeFromLeft(w);
        knobLabels[i].setBounds(k.removeFromTop(22));
        knobs[i].setBounds(k.reduced(8));
    }
    pageButton.setBounds(area.removeFromTop(32).withSizeKeepingCentre(64, 26));
}
```

- [ ] **Step 3: Build the VST3**

Run:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DREVERSON_BUILD_VST=ON
cmake --build build --config Debug --target ReversonVST
```
Expected: SUCCESS. The VST3 lands at `build/vst/ReversonVST_artefacts/Debug/VST3/Reverson.vst3`.

- [ ] **Step 4: Commit**

```bash
git add vst/PluginEditor.h vst/PluginEditor.cpp
git commit -m "feat: pedal-mirror UI (3 knobs + page toggle + bypass)"
```

---

## Task 8: Full build, tests, and manual DAW recipe

**Files:**
- Create: `README.md` (project root: how to build, test, and load in a DAW)

- [ ] **Step 1: Write README**

`README.md`:
```markdown
# Reverson — dynamic reverse reverb

From-scratch reverse reverb for Zoom G1on (ZDL) — developed first as a VST3 so
the sound can be tuned in a DAW, then ported to the pedal.

## Build (Windows, MSVC + CMake)

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Debug
    ctest --test-dir build -C Debug --output-on-failure

## Build the VST3

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DREVERSON_BUILD_VST=ON
    cmake --build build --config Release --target ReversonVST

Output: `build/vst/ReversonVST_artefacts/Release/VST3/Reverson.vst3`

## Test in a DAW

1. Copy `Reverson.vst3` into your DAW's VST3 folder (e.g. `C:\Program Files\Common Files\VST3`) and rescan.
2. Prefer a 44.1 kHz project (matches the G1on).
3. Load Reverson on a clean guitar track. Knobs P1: Mix/Decay/Tone, P2: RevLen/Duck/Gate.
4. The signature moves: trigger-based reverse swell per note; Duck rides the wet
   down while you play; Gate only lets the tail ring in the gaps.
```

- [ ] **Step 2: Full verification**

Run:
```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --target ReversonVST
```
Expected: 6/6 tests pass; `Reverson.vst3` exists.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: build + DAW test instructions"
```

---

## Definition of Done

- [ ] All 6 CTest tests pass on `Debug` and `Release`.
- [ ] `Reverson.vst3` loads in a DAW; 3 knobs + P1/P2 toggle + bypass work.
- [ ] Default preset sounds like a DIIV-style clean, spacious, rhythmically ducked reverse reverb (user ear-check).
- [ ] Core builds with no `malloc`, no `double`, no runtime division in the audio path — ready to port to TI C6000.

## Follow-up (not in this plan)

- Port the same `core/` to a custom ZDL (TI C6000 toolchain, `ctx[3]` state) — separate plan once the G1on arrives and the platform bring-up (Task A) validates the write path.
- Add `tools/measure` sweep/IR capture to A/B against the stock ReverseRv.
- Multi-page UI + extended params (Shape/Mod/Sat/Width) once the ZDL editor ABI allows.

