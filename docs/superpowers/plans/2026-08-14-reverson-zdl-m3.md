# Reverson G1on ZDL Port — Implementation Plan (M3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans.

**Goal:** Build the 9-knob Reverson ZDL (`dist/Reverson.ZDL`) for the Zoom G1on /
MS-series pedal from the v2 core, following the K9Probe-proven 9-knob
handler config, and prepare the hardware flash checklist (M4).

**Architecture:** The core compiles into one translation unit
(`reverson_zdl.c` #includes `core/reverson.c` with `REVERSON_ENABLE_FDN=0`).
State is carved lazily from the host `ctx[3]` arena (header magic + version +
`Reverson_init` block ≈160 KB, well under the 512 KB arena floor). Knobs 1..3
use the hardware-proven stock blobs (LineSel 1/2 + AIR knob3); knobs 4..9 use
the linker's synthesized LineSel clones (`synth_edit_start_index=3`) — the
exact config K9Probe exists to validate.

**Tech stack:** TI C6000 compiler 8.5.0.LTS at
`C:\Users\34723\Downloads\ti-cgt-c6000_8.5.0.LTS` (`TI_CGT_ROOT`), the repo
Python linker (`ZoomMultistompZDL/build/linker.py`).

**Key identifiers:** effect name `Reverson` (8 chars, safe), audio func
`Fx_REV_Reverson`, gid 9 (Reverb), fxid 411 / 0x019B (next free after OTT
0x019A). Shipped default = the `diiv` acceptance preset via manifest defaults
+ `DEFAULT_NORM` fallbacks (ZDL init materialization is unsolved, so the
manifest defaults ARE the shipped preset).

## Tasks

### Task 1: Create the custom effect source

**Files:**
- Create: `ZoomMultistompZDL/src/custom/reverson/manifest.json`
- Create: `ZoomMultistompZDL/src/custom/reverson/reverson_zdl.c`
- Create: `ZoomMultistompZDL/src/custom/reverson/build.py`
- Modify: `ZoomMultistompZDL/build_all.py` (register in RELEASE_PLUGINS)

manifest.json — 9 params (P1 Mix/Rev/Space, P2 Tone/Grain/Duck,
P3 Mode/Trig/Predelay), defaults = diiv, mode max 5:

```json
{
  "effect_name": "Reverson",
  "comment": "Reverson v2 reverse reverb for G1on/MS-series. 9 knobs: P1 Mix/Rev/Space, P2 Tone/Grain/Duck, P3 Mode/Trig/Predelay. Core compiled with REVERSON_ENABLE_FDN=0; state carved from ctx[3] (~160 KB). Defaults are the 'diiv' acceptance preset.",
  "audio_func_name": "Fx_REV_Reverson",
  "gid": 9,
  "gid_comment": "0x09 = Reverb category.",
  "fxid": 411,
  "fxid_comment": "0x019B - next free in the custom effect range (after OTT 0x019A).",
  "fxid_version": "1.00",
  "flags_byte": 1,
  "params": [
    {"name": "Mix",      "type": "knob", "max": 100, "default": 55, "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.55, "comment": "P1 K1: dry/wet."},
    {"name": "Rev",      "type": "knob", "max": 100, "default": 25, "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.25, "comment": "P1 K2: wash -> reverse layer -> gated."},
    {"name": "Space",    "type": "knob", "max": 100, "default": 60, "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.60, "comment": "P1 K3: swell span / decay / width."},
    {"name": "Tone",     "type": "knob", "max": 100, "default": 50, "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.50, "comment": "P2 K1: dark -> bright."},
    {"name": "Grain",    "type": "knob", "max": 100, "default": 40, "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.40, "comment": "P2 K2: diffusion / mod."},
    {"name": "Duck",     "type": "knob", "max": 100, "default": 50, "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.50, "comment": "P2 K3: wet rides down while playing."},
    {"name": "Mode",     "type": "knob", "max": 5,   "default": 0,  "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.00, "comment": "P3 K1: 0=Off(custom) 1=Wash 2=Reverse 3=Gated 4=Shoegaze 5=Space."},
    {"name": "Trig",     "type": "knob", "max": 100, "default": 55, "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.55, "comment": "P3 K2: trigger sensitivity + hold."},
    {"name": "Predelay", "type": "knob", "max": 100, "default": 0,  "audio_min": 0.0, "audio_max": 1.0, "audio_default": 0.00, "comment": "P3 K3: trigger predelay 0..120 ms."}
  ],
  "audio_nop": false
}
```

reverson_zdl.c:

```c
/*
 * Reverson ZDL body - 9-knob reverse reverb for Zoom G1on / MS-series.
 * The v2 core is pulled into this translation unit with the FDN bed
 * compiled out; state is carved lazily from the host ctx[3] arena.
 * Knob mapping: P1 Mix/Rev/Space, P2 Tone/Grain/Duck, P3 Mode/Trig/Predelay.
 */
#include <stdint.h>
#include "../common/zoom_params.h"
#include "reverson_params.h"

#define REVERSON_ENABLE_FDN 0
#include "../../../../core/reverson.c"

#ifndef REVERSON_AUDIO_FUNC
#define REVERSON_AUDIO_FUNC Fx_REV_Reverson
#endif

#define REVERSON_DO_PRAGMA(x) _Pragma(#x)
#define REVERSON_EXPAND_PRAGMA(x) REVERSON_DO_PRAGMA(x)
#define REVERSON_CODE_SECTION(func) REVERSON_EXPAND_PRAGMA(CODE_SECTION(func, ".audio"))

#define ZDL_PTR(type, word) ((type)(uintptr_t)(word))

#define REVERSON_ZDL_MAGIC 0x52565731u   /* "RVW1" */
#define REVERSON_ZDL_VERSION 1u
#define REVERSON_ARENA_FLOOR 524288u

static inline float revzdl_abs(float x) { return x < 0.0f ? -x : x; }

REVERSON_CODE_SECTION(REVERSON_AUDIO_FUNC)
void REVERSON_AUDIO_FUNC(unsigned int *ctx)
{
    float *params = ZDL_PTR(float *, ctx[1]);
    float *fxBuf = ZDL_PTR(float *, ctx[5]);

    /* stock magic shuttle: preserve it first, always */
    unsigned int *magicSrc = ZDL_PTR(unsigned int *, ctx[12]);
    unsigned int *magicDst = ZDL_PTR(unsigned int *, *(unsigned int *)ZDL_PTR(unsigned int *, ctx[11]));
    *magicDst = *magicSrc;

    if (params[0] < 0.5f) return;   /* bypass: dry passthrough */

    volatile unsigned int *desc = ZDL_PTR(volatile unsigned int *, ctx[3]);
    if (!desc) return;
    uintptr_t base = (uintptr_t)desc[0];
    uintptr_t end = (uintptr_t)desc[1];
    unsigned int span = desc[2];
    uintptr_t bytes = end - base;
    if (base == 0u || end <= base) return;
    if ((base & 3u) != 0u || (end & 3u) != 0u || (span & 3u) != 0u) return;
    if (bytes < REVERSON_ARENA_FLOOR || span < bytes) return;
    if (bytes > 0x00800000u || span > 0x00800000u) return;

    /* header (magic/version + denormal seeds) then the core state block */
    uintptr_t hdr = (base + 3u) & ~(uintptr_t)3u;
    uintptr_t coreBase = hdr + 16u;
    uint32_t need = Reverson_state_size(44100.0f);
    if (coreBase + (uintptr_t)need > end) return;

    uint32_t *mag = (uint32_t *)hdr;
    Reverson *core = (Reverson *)coreBase;
    if (mag[0] != REVERSON_ZDL_MAGIC || mag[1] != REVERSON_ZDL_VERSION) {
        Reverson *r = Reverson_init((void *)core, need, 44100.0f);
        if (!r) return;
        mag[0] = REVERSON_ZDL_MAGIC;
        mag[1] = REVERSON_ZDL_VERSION;
        mag[2] = 0x1234567u;   /* denormal dither seeds */
        mag[3] = 0x89ABCDFu;
    }

    /* 9 knobs on the stock 0..0.14 raw rail, with the manifest defaults as
       fallbacks (untouched knobs ship the 'diiv' preset) */
    float mix      = zoom_param_norm(params[REVERSON_MIX_SLOT],      REVERSON_MIX_DEFAULT_NORM);
    float rev      = zoom_param_norm(params[REVERSON_REV_SLOT],      REVERSON_REV_DEFAULT_NORM);
    float space    = zoom_param_norm(params[REVERSON_SPACE_SLOT],    REVERSON_SPACE_DEFAULT_NORM);
    float tone     = zoom_param_norm(params[REVERSON_TONE_SLOT],     REVERSON_TONE_DEFAULT_NORM);
    float grain    = zoom_param_norm(params[REVERSON_GRAIN_SLOT],    REVERSON_GRAIN_DEFAULT_NORM);
    float duck     = zoom_param_norm(params[REVERSON_DUCK_SLOT],     REVERSON_DUCK_DEFAULT_NORM);
    float modev    = zoom_param_norm(params[REVERSON_MODE_SLOT],     REVERSON_MODE_DEFAULT_NORM);
    float trig     = zoom_param_norm(params[REVERSON_TRIG_SLOT],     REVERSON_TRIG_DEFAULT_NORM);
    float predelay = zoom_param_norm(params[REVERSON_PREDELAY_SLOT], REVERSON_PREDELAY_DEFAULT_NORM);

    Reverson_set_6knob(core, mix, rev, space, tone, grain, duck);
    Reverson_set_param(core, REVERSON_PARAM_TRIG, trig);
    Reverson_set_param(core, REVERSON_PARAM_PREDELAY, predelay);
    {
        int mode = (int)(modev * 5.0f + 0.5f);
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

    int i;
    for (i = 0; i < 8; i++) {
        float xl = fxBuf[i];
        float xr = fxBuf[i + 8];
        /* denormal dither (same trick as StChorus): keep the delay lines
           from filling with denormals during silence */
        if (revzdl_abs(xl) < 1.18e-23f) xl = (float)mag[2] * 1.18e-17f;
        if (revzdl_abs(xr) < 1.18e-23f) xr = (float)mag[3] * 1.18e-17f;
        mag[2] ^= mag[2] << 13; mag[2] ^= mag[2] >> 17; mag[2] ^= mag[2] << 5;
        mag[3] ^= mag[3] << 13; mag[3] ^= mag[3] >> 17; mag[3] ^= mag[3] << 5;

        float ol, orr;
        Reverson_process_stereo(core, xl, xr, &ol, &orr);
        fxBuf[i] = ol;
        fxBuf[i + 8] = orr;
    }
}
```

build.py — clone of k9probe's with reverson paths, `TI_CGT_ROOT` override,
output `dist/Reverson.ZDL`, screen `make_airwindows_reverb_screen("Reverson")`,
the proven knob-blob config:

```python
#!/usr/bin/env python3
"""Build Reverson.ZDL from reverson_zdl.c + manifest.json."""
from __future__ import annotations
import json, os, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))

from airwindows_image import make_airwindows_reverb_screen  # noqa: E402
from linker import LinkerConfig, link, params_from_manifest  # noqa: E402
from manifest_params import write_param_header  # noqa: E402

TI_ROOT = Path(os.environ.get(
    "TI_CGT_ROOT",
    "C:/Users/34723/Downloads/ti-cgt-c6000_8.5.0.LTS",
))
CL6X = TI_ROOT / "bin" / "cl6x"

CFLAGS = [
    "--c99", "--opt_level=2", "-mv6740", "--abi=eabi",
    "--mem_model:data=far",
    f"--include_path={TI_ROOT}/include",
]

def main() -> None:
    manifest = json.loads((HERE / "manifest.json").read_text())
    write_param_header(manifest, HERE / "reverson_params.h", "REVERSON")

    src_c = HERE / "reverson_zdl.c"
    obj = HERE / "reverson_zdl.obj"
    out_zdl = ROOT / "dist" / f"{manifest['effect_name']}.ZDL"
    out_zdl.parent.mkdir(exist_ok=True)

    print(f"[reverson] compiling {src_c.name} -> {obj.name} with {CL6X}")
    subprocess.run([str(CL6X), *CFLAGS, "-c", str(src_c), f"--output_file={obj}"],
                   check=True, cwd=HERE)

    for junk in ("compiler.opt", "linker.cmd"):
        p = HERE / junk
        if p.exists():
            p.unlink()

    cfg = LinkerConfig(
        effect_name=manifest["effect_name"],
        audio_func_name=manifest.get("audio_func_name"),
        gid=manifest["gid"],
        fxid=manifest["fxid"],
        params=params_from_manifest(manifest["params"]),
        obj_path=obj,
        output_path=out_zdl,
        fxid_version=manifest.get("fxid_version", "1.00").encode("ascii"),
        flags_byte=manifest.get("flags_byte", 0x01),
        screen_image=make_airwindows_reverb_screen("Reverson"),
        handler_blob_path=ROOT / "build" / "linesel_handlers.bin",
        knob3_blob_path=ROOT / "build" / "air_knob3_edit.bin",
        synthesize_linesel_edit_handlers=True,
        synth_edit_start_index=3,
        use_object_edit_handlers=False,
        audio_nop=manifest.get("audio_nop", False),
    )
    link(cfg)

    print(f"\n[reverson] done -> {out_zdl}")

if __name__ == "__main__":
    main()
```

build_all.py: add `("reverson", CUSTOM_DIR / "reverson" / "build.py"),` to
RELEASE_PLUGINS.

### Task 2: Build and verify the artifact

```
$env:TI_CGT_ROOT = "C:\Users\34723\Downloads\ti-cgt-c6000_8.5.0.LTS"
python -B build_all.py reverson
```

Expected: `dist/Reverson.ZDL` produced; linker prints `.fardata: 0 bytes`
and no unexpected relocations. Then:

```
python -B build/dump_zdl_descriptor.py dist/Reverson.ZDL
```

Expected: descriptor dump shows 9 knobs, Reverb category, sane imageInfo.

### Task 3: Verify K9Probe builds

```
python -B build_all.py k9probe
```

Expected: `build/probes/K9Probe.ZDL` (the flash-first artifact).

### Task 4: Flash checklist doc

Create `research_docs/docs_REVERSON-G1ON-FLASH.md` with the M4 procedure:
backup effect list -> flash K9Probe -> 9-knob page test (pass criteria from
k9probe README) -> flash Reverson.ZDL -> load in Reverb category -> bypass /
3 pages / audio checks -> report results (category visibility quirk note).

## Self-review

- K9Probe-proven config reused verbatim (stock blobs knobs 1-3, synthesized
  clones 4-9) — the only hardware-risky surface is exactly what K9Probe tests.
- `.fardata` stays 0 (all core tables are `const`; linker packs `.const:*`).
- State ≈160 KB < 512 KB arena floor; lazy init via magic/version header.
- `fxid` 0x019B is free; `Reverson` is 8 chars (no truncation collision).
