# Reverson - dynamic reverse reverb

A from-scratch reverse reverb for shoegaze / indie (DIIV, MBV-style) tones,
built first as a **VST3** to tune in a DAW, then ported to the **Zoom
G1on / MS-series pedal** as a custom ZDL effect. The DSP is fully original - no extracted factory algorithms.

The signature sound: a **dense, always-present reverse swell** (SPX90-style
multi-head, no continuous FDN bed by default) - each note is answered
immediately (zero predelay), builds an accelerating crescendo, then the line
runs dry into a natural gate.

## Features

- **Sample-by-sample causal DSP** - no whole-file / non-causal buffering,
  so it works as a live pedal effect.
- **SPX90-style multi-head swell** - 13 delay taps with exponentially
  increasing delays (8 ms..224 ms) and gains, diffused by cascaded allpass
  filters plus a per-tap LFO; an 8-line FDN bed is optional (default off).
- **Accelerating reverse attack** (DP/4-style) - the `Shape` knob blends a
  linear attack into a quadratic "slow start, rush to peak" curve.
- **Living tail (Mod)** - a slow triangle LFO dithers each tap's read position
  with per-tap phase offsets, so the echo comb slowly decorrelates: reads as
  reverb instead of delay, with far taps breathing more than near taps.
- **Level-independent onset triggering** - triggers the same way whether you
  pick quietly or loudly.
- **Zero predelay, no amplitude pumping** - a floor keeps the wet always
  present; the swell comes from the tap structure, not from gating the whole
  wet signal.
- **ZDL-safe core** - no heap, no `double`, no division, no `sinf` in the
  audio path; all memory is caller-provided.

## Parameters (13 internal; the UI exposes 6 linked knobs)

| Param | Range | What it does |
|---|---|---|
| Mix | 0..1 | dry/wet balance (knob: Mix) |
| Gate | 0..1 | reverse amount: 0 = dry / bed-only, 1 = full reverse gate (knob: Rev) |
| Shape | 0..1 | attack curve: 0 = linear, 1 = accelerating (knob: Rev) |
| Density | 0..1 | swell hold time after the peak (knob: Rev) |
| RevLen | 0..1 | reverse swell length (knob: Space) |
| Decay | 0..1 | FDN bed tail, used when the bed is on (knob: Space) |
| Width | 0..1 | stereo width (knob: Space) |
| Tone | 0..1 | wet low-pass, dark..bright (knob: Tone) |
| Bass | 0..1 | low-mid body shelf (knob: Tone) |
| Sat | 0..1 | soft-clip saturation (knob: Tone) |
| Diffusion | 0..1 | diffuser feedback: sharp echo -> dense smear (knob: Grain) |
| Mod | 0..1 | swell-tap LFO, living tail (knob: Grain) |
| Duck | 0..1 | wet rides down while you play (knob: Duck) |

## 6-knob ergonomic mapping (two pages x 3)

The 13 internal params are driven by 6 linked knobs; each internal param is
owned by exactly one knob so they never fight:

| Page | Knob | Owns |
|---|---|---|
| P1 | Mix | dry/wet (direct) |
| P1 | Rev | gate / shape / density (wash -> gated reverse) |
| P1 | Space | revlen / decay / width (small -> huge) |
| P2 | Tone | tone / bass / sat (dark -> bright) |
| P2 | Grain | diffusion / mod (grainy-static -> smooth-flowing) |
| P2 | Duck | duck (direct) |

Curves are shaped so any combination stays musical: `Rev` keeps a floor
(rev=0 is a subtle wash, never a dead dry patch), `Space` grows the swell
span + bed tail + width together, `Tone` adds saturation as it brightens,
and `Grain` pairs the diffuser with the tap LFO - the "delay vs reverb"
axis lives entirely on that knob.

## DSP architecture

```
in -> duck -> multi-head swell (13 taps, exp delays/gains)
              -> per-tap LFO (Mod: living tail, decorrelates the comb)
              -> 3-stage aperiodic feedback diffusion (fills the gaps)
         -> reverse gate envelope (accelerating attack, floor -> 1 -> cut)
         -> tone LP -> bass shelf -> sat -> width -> mix
         [optional 8-line FDN bed, default off]
```

Pure reverse-swell engine (SPX90-style), modernised: the 3-stage
mutually-prime diffuser (277/449/613 sample delays) turns the discrete
echo grains into dense aperiodic smear, so it reads as reverb without
losing the reverse crescendo/gate. An FDN bed is still in the code but
defaults off (pure reverse was chosen as the sound).

## Build

Windows + MSVC + CMake:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### Build the VST3

Requires JUCE 7.0.12 (fetched via FetchContent, or point at a local mirror):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DREVERSON_BUILD_VST=ON
cmake --build build --config Release --target ReversonVST_VST3
```

Install the VST3 to a DAW-scanned path - on Windows the standard per-user
location is `%LOCALAPPDATA%\Programs\Common\VST3` (Reaper scans it by
default); the system-wide `C:\Program Files\Common Files\VST3` needs admin.

The editor is a faithful replica of the Zoom MS-series LCD UI: effect name +
page, focused parameter with a big value and bar, K1/K2/K3 slots, and three
pedal-style knobs below (PAGE cycles the 2 pages of 3 parameters).

### Offline render tool (A/B sweeps without a DAW)

```
cmake --build build --config Release --target reverson_render
reverson_render in.wav out 2 rev cold 0.25 gate=0.6 shape=0.8
```

`reverson_render <in.wav> <out_prefix> [loops] [preset] [cold] [input_peak]
[key=value ...]` renders dry + presets through the exact same core as the
VST, with optional per-param overrides for quick sweeps.

## Tests

`ctest` runs 8 suites covering the delay lines, FDN, reverse swell engine,
onset envelope, the full core (boundedness, no NaN, stereo decorrelation,
duck/gate behavior, map6 invariants, 48 kHz), and the no-FDN ZDL-shaped build.

## Zoom G1on / ZDL status

- **K9Probe** (`ZoomMultistompZDL/src/hardware_probes/k9probe/`) is a
  9-knob hardware probe that validates the synthesized LineSel-cloned edit
  handlers for knobs 4..9 (pages 2/3) on the pedal - the last open piece
  before a full 9-knob Reverson ZDL.
- The pedal exposes up to **9 user knobs** (3 pages x 3): pages 1-2 carry the
  6-knob mapping above; page 3 is reserved for a 5-position mode switch
  (Wash / Reverse / Gated / Shoegaze / Space - the `MODES` tables in
  `tools/render_demo.c`) plus 2 spare knobs.
- The ZDL build compiles the core with `REVERSON_ENABLE_FDN=0`: the FDN bed
  is compiled out entirely and ~240 KB of `ctx[3]` memory is not reserved
  (the pure-reverse path is the shipped sound anyway).
- See `research_docs/` and `ZoomMultistompZDL/docs/` for the reverse
  engineering notes (edit-handler ABI, safe DSP rules, ZDL status).

## Repository layout

```
core/            DSP engine (C99, no dependencies, ZDL-safe)
vst/             JUCE VST3 shell + Zoom-style editor
tools/           offline renderer (reverson_render)
tests/           CTest suites
research_docs/   ZDL reverse-engineering notes
docs/            plans and design specs
```

## License

Original DSP and code. No Zoom factory algorithms are included or derived.
The third-party research repos (ZoomMultistompZDL etc.) are re-cloned on
demand and are not part of this repository.
