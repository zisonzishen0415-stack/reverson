> **项目状态（2026-08-16 修订）：失败。**
> 经 G1on 实机多次验证，自制 ZDL **无法在 G1on 上稳定工作**（表现为：无 UI、bypass、或切换死机）。
> 此前文档中的“已验证 / 可用 / 可运行 / 已对齐 / 可收口”等表述，均不代表 G1on 实机可用。
> 本项目的 ZDL 目标当前**未达成**，现状为失败；任何后续结论不得以“G1on 可用”为前提。

# Reverson - dynamic reverse reverb

A from-scratch reverse reverb for shoegaze / indie (DIIV, MBV-style) tones,
built first as a **VST3** to tune in a DAW, then ported to the **Zoom
G1on / MS-series pedal** as a custom ZDL effect. The DSP is fully original - no extracted factory algorithms.

The signature sound: a **dense, always-present reverse swell** (SPX90-style
multi-head, no continuous FDN bed by default) - each note is answered
immediately (zero predelay), builds an accelerating crescendo, then the line
runs dry into a natural gate. The `Rev` knob is a full continuum: low = the
forward multi-head wash, high = a **true backward-read reverse layer** (the
recent input is replayed in reverse on each onset, MBV "Sometimes"-style),
top = gated reverse.

## Features

- **Sample-by-sample causal DSP** - no whole-file / non-causal buffering,
  so it works as a live pedal effect.
- **True reverse playback layer** - on each onset a backwards read head
  replays the pre-onset tail from the shared ring buffer (RevRev engine:
  anchored segments, 1..3 staggered voices, click-free crossfade, peak
  normalization). It shares memory with the swell line (zero extra large
  buffers) and fades the forward taps out as `Rev` rises - at full reverse
  the 13-tap loop is skipped entirely (CPU win).
- **SPX90-style multi-head swell** - 13 delay taps with exponentially
  increasing delays (8 ms..224 ms) and gains, diffused by cascaded allpass
  filters with per-stage L/R **crossfeed** plus a per-tap LFO; an 8-line FDN
  bed is optional (default off).
- **5-state reverse envelope** - rise -> overshoot (`0.16 * shape`) -> settle
  to 1 -> hold -> fall to a floor; value-based re-planning keeps retriggers
  wobble-free.
- **Accelerating reverse attack** (DP/4-style) - the `Shape` knob blends a
  linear attack into a quadratic "slow start, rush to peak" curve.
- **Living tail (Mod)** - a slow triangle LFO dithers each tap's read position
  with per-tap phase offsets, so the echo comb slowly decorrelates: reads as
  reverb instead of delay, with far taps breathing more than near taps
  (a mod=0 fast path skips the per-tap LFO entirely).
- **Level-independent onset triggering** with adjustable **sensitivity**
  (`Trig` knob: threshold + hold linked) and trigger **predelay**
  (0..120 ms) that delays only the swell, never the wet floor.
- **Wet high-pass (~110 Hz)** after the tone LP - palm-mute mud stays out of
  the smear.
- **Stereo input** - the mono sum drives the wet engine, the dry stereo image
  passes through intact (`Reverson_process_stereo`).
- **8-sample parameter smoothing grid** - all 15 params smooth once per 8
  samples with an equivalent coefficient (1/8 the per-sample cost; the ZDL
  callback is already 8 samples).
- **ZDL-safe core** - no heap, no `double`, no division, no `sinf`/`powf` in
  the audio path (init-time coefficients use mul/add series); all memory is
  caller-provided.

## Parameters (15 internal; the UI exposes 9 knobs + bypass)

| Param | Range | What it does |
|---|---|---|
| Mix | 0..1 | dry/wet balance (knob: Mix) |
| Gate | 0..1 | reverse amount: 0 = dry / bed-only, 1 = full reverse gate (knob: Rev) |
| Shape | 0..1 | attack curve: 0 = linear, 1 = accelerating + overshoot (knob: Rev) |
| Density | 0..1 | swell hold time + reverse-layer voices (knob: Rev) |
| RevLen | 0..1 | reverse swell length + reverse segment span (knob: Space) |
| Decay | 0..1 | FDN bed tail, used when the bed is on (knob: Space) |
| Width | 0..1 | stereo width (knob: Space) |
| Tone | 0..1 | wet low-pass, dark..bright (knob: Tone) |
| Bass | 0..1 | low-mid body shelf (knob: Tone) |
| Sat | 0..1 | soft-clip saturation (knob: Tone) |
| Diffusion | 0..1 | diffuser feedback + crossfeed: sharp echo -> dense smear (knob: Grain) |
| Mod | 0..1 | swell-tap LFO, living tail (knob: Grain) |
| Duck | 0..1 | wet rides down while you play (knob: Duck) |
| Trig | 0..1 | trigger sensitivity + hold (low = easy trigger/short hold, high = forgiving/long hold) |
| Predelay | 0..1 | trigger predelay 0..120 ms (delays only the swell) |

`Over` (the envelope overshoot) is curve-owned: `over = 0.16 * shape`, not a
user knob.

## 9-knob ergonomic mapping (three pages x 3)

The 13 shared internal params are driven by 6 linked knobs; each internal
param is owned by exactly one knob so they never fight. Page 3 carries the
mode switch plus the two trigger knobs:

| Page | Knob | Owns |
|---|---|---|
| P1 | Mix | dry/wet (direct) |
| P1 | Rev | gate / shape / density + reverse-layer mix (wash -> reverse -> gated) |
| P1 | Space | revlen / decay / width (small -> huge) |
| P2 | Tone | tone / bass / sat (dark -> bright) |
| P2 | Grain | diffusion / mod (grainy-static -> smooth-flowing) |
| P2 | Duck | duck (direct) |
| P3 | Mode | 5-position switch: Wash / Reverse / Gated / Shoegaze / Space (`Reverson_mode`) |
| P3 | Trig | trigger sensitivity + hold (linked) |
| P3 | Predelay | trigger predelay 0..120 ms |

Curves are shaped so any combination stays musical: `Rev` keeps a floor
(rev=0 is a subtle wash, never a dead dry patch), `Space` grows the swell
span + bed tail + width together, `Tone` adds saturation as it brightens,
and `Grain` pairs the diffuser with the tap LFO - the "delay vs reverb"
axis lives entirely on that knob.

## DSP architecture

```
inL,inR --(mono sum)--> onset env (sensitivity) -> trigger (predelay counter)
      |                                          |
      v                                          v
   duck (env_peak-normalized)        5-state reverse envelope
      |                              (rise->1+over, settle->1, hold, fall->floor)
      v                                          |
   mono wet input --------------------------------+
      |
      +--> multi-head swell: 13 taps (fade out as Rev rises)
      +--> reverse layer: backward read of the SAME line (fades in, RevRev)
      |
      +--> 3-stage aperiodic feedback diffusion WITH L/R crossfeed
      |
      +--> tone LP -> wet HPF (~110 Hz) -> bass shelf -> sat -> width
      |
      +--> * reverse gate env -> mix
out_l = inL*(1-mix) + wet_l*mix    out_r = inR*(1-mix) + wet_r*mix
[optional 8-line FDN bed, default off]
```

The `Rev` continuum: at low gate the 13 forward taps own the sound; as gate
rises the reverse layer (the dormant RevRev engine, sharing the swell line
memory) takes over and the tap loop early-outs; at the top the reverse
segment is gated by the 5-state envelope. The 3-stage mutually-prime
diffuser (277/449/613 sample delays, now cross-coupled) turns the discrete
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

The build copies the finished plugin to `out/vst3/Reverson.vst3`. Install it
to a DAW-scanned path - on Windows the standard per-user location is
`%LOCALAPPDATA%\Programs\Common\VST3` (Reaper scans it by default); the
system-wide `C:\Program Files\Common Files\VST3` needs admin.

The editor is a faithful replica of the Zoom MS-series LCD UI: effect name +
page, focused parameter with a big value and bar, K1/K2/K3 slots, and three
pedal-style knobs below (PAGE cycles the 3 pages of 3 parameters;
P3 = Mode/Trig/Predelay).

### Offline render tool (A/B sweeps without a DAW)

```
cmake --build build --config Release --target reverson_render
reverson_render in.wav out 2 mbv cold 0.25 gate=0.6 shape=0.8
```

`reverson_render <in.wav> <out_prefix> [loops] [preset] [cold] [input_peak]
[key=value ...]` renders dry + presets through the exact same core as the
VST, with optional per-param overrides for quick sweeps. Acceptance presets:
`mbv` (gated reverse), `diiv` (clean spacious wash), `slowdive` (ambient
wash) - expressed as 6-knob + trig/predelay values, exactly like the pedal.

## Tests

`ctest` runs 13 suites covering the delay lines, FDN, reverse swell engine
(split taps/diffuse API, crossfeed, mod fast path), the RevRev reverse layer
(pre_off, voices, causality), onset envelope (settable threshold), the full
core (boundedness, no NaN, 5-state envelope, trigger sensitivity/predelay,
8-sample grid, stereo API, wet HPF), and the no-FDN ZDL-shaped build.

## Zoom G1on / ZDL status

- **K9Probe** (`ZoomMultistompZDL/src/hardware_probes/k9probe/`) is a
  9-knob hardware probe that validates the synthesized LineSel-cloned edit
  handlers for knobs 4..9 (pages 2/3) on the pedal - the last open piece
  before a full 9-knob Reverson ZDL. Flash it first when the pedal arrives,
  before the real effect. **Built**: `build/probes/K9Probe.ZDL`.
- **Reverson.ZDL is built** (`ZoomMultistompZDL/dist/Reverson.ZDL`, Reverb
  category, fxid 0x019B): 9 knobs (P1 Mix/Rev/Space, P2 Tone/Grain/Duck,
  P3 Mode/Trig/Predelay), `diiv` as the shipped default via manifest
  fallbacks, `.fardata` 0 bytes, state in `ctx[3]` (~160 KB), the only
  external symbol is the float-divide RTS (linker-resolved). The core was
  made ZDL-toolchain-clean for this build: no `memset`/struct-assign/
  float-to-uint RTS helpers, no `switch` jump tables, float-stagger math
  instead of 64-bit division - all behavior-identical (13 desktop suites
  still green).
- The pedal exposes up to **9 user knobs** (3 pages x 3): pages 1-2 carry the
  6-knob mapping above; page 3 = the 5-position mode switch
  (Wash / Reverse / Gated / Shoegaze / Space - the `Reverson_mode` tables in
  `core/reverson.c`) + Trig + Predelay.
- The ZDL build compiles the core with `REVERSON_ENABLE_FDN=0`: the FDN bed
  is compiled out entirely and ~240 KB of `ctx[3]` memory is not reserved.
- The G1on hardware steps (K9Probe -> Reverson flash -> 3-page knob check)
  are the remaining work; the full procedure + pass criteria are in
  `research_docs/docs_REVERSON-G1ON-FLASH.md`.
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
