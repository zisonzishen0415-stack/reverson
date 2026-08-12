# Reverson ? dynamic reverse reverb

A from-scratch reverse reverb for shoegaze / indie (DIIV, MBV-style) tones,
built first as a **VST3** to tune in a DAW, then ported to the **Zoom
G1on / MS-series pedal** as a custom ZDL effect. The DSP is fully original ?
no extracted factory algorithms.

The signature sound: a **dense, continuous reverb bed** with an SPX90-style
**multi-head reverse swell** on top ? each note is answered immediately
(zero predelay), builds an accelerating crescendo, then the line runs dry
into a natural gate.

## Features

- **Sample-by-sample causal DSP** ? no whole-file / non-causal buffering,
  so it works as a live pedal effect.
- **SPX90-style multi-head swell** ? 13 delay taps with exponentially
  increasing delays (8 ms..224 ms) and gains, diffused by cascaded allpass
  filters, layered over an 8-line FDN reverb bed.
- **Accelerating reverse attack** (DP/4-style) ? the `Shape` knob blends a
  linear attack into a quadratic "slow start, rush to peak" curve.
- **Level-independent onset triggering** ? triggers the same way whether you
  pick quietly or loudly.
- **Zero predelay, no amplitude pumping** ? a floor keeps the bed always
  present; the swell comes from the tap structure, not from gating the whole
  wet signal.
- **ZDL-safe core** ? no heap, no `double`, no division, no `sinf` in the
  audio path; all memory is caller-provided.

## Parameters (12, all exposed in the VST)

| Param | Range | What it does |
|---|---|---|
| Mix | 0..1 | dry/wet balance |
| Decay | 0..1 | FDN tail length |
| Tone | 0..1 | wet low-pass (dark..bright) |
| RevLen | 0..1 | reverse swell length |
| Duck | 0..1 | wet rides down while you play |
| Gate | 0..1 | reverse amount: 0 = dense bed, 1 = full reverse gate |
| Shape | 0..1 | attack curve: 0 = linear, 1 = accelerating |
| Mod | 0..1 | FDN read-delay LFO (living tail) |
| Sat | 0..1 | soft-clip saturation |
| Width | 0..1 | stereo width |
| Density | 0..1 | swell hold time after the peak |
| Bass | 0..1 | low-mid body shelf |

## DSP architecture

```
in -> duck -> FDN bed (8-line, dense continuous tail)
           +-> multi-head swell (13 taps, exp delays/gains, allpass smear)
         -> reverse gate envelope (accelerating attack, floor -> 1 -> cut)
         -> tone LP -> bass shelf -> sat -> width -> mix
```

The reverse character comes from the SPX90-style tap structure (classic
reverse reverb, improved: a continuous FDN bed keeps it dense instead of
the hard empty gate of vintage units).

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

Install the VST3 to a DAW-scanned path ? on Windows the standard per-user
location is `%LOCALAPPDATA%\Programs\Common\VST3` (Reaper scans it by
default); the system-wide `C:\Program Files\Common Files\VST3` needs admin.

The editor is a faithful replica of the Zoom MS-series LCD UI: effect name +
page, focused parameter with a big value and bar, K1/K2/K3 slots, and three
pedal-style knobs below (PAGE cycles the 4 pages of 3 parameters).

### Offline render tool (A/B sweeps without a DAW)

```
cmake --build build --config Release --target reverson_render
reverson_render in.wav out 2 rev cold 0.25 gate=0.6 shape=0.8
```

`reverson_render <in.wav> <out_prefix> [loops] [preset] [cold] [input_peak]
[key=value ...]` renders dry + presets through the exact same core as the
VST, with optional per-param overrides for quick sweeps.

## Tests

`ctest` runs 7 suites covering the delay lines, FDN, reverse swell engine,
onset envelope, and the full core (boundedness, no NaN, stereo decorrelation,
duck/gate behavior).

## Zoom G1on / ZDL status

- **K9Probe** (`ZoomMultistompZDL/src/hardware_probes/k9probe/`) is a
  9-knob hardware probe that validates the synthesized LineSel-cloned edit
  handlers for knobs 4..9 (pages 2/3) on the pedal ? the last open piece
  before a full 9-knob Reverson ZDL.
- The pedal exposes up to **9 user knobs** (3 pages x 3), so the ZDL port
  maps the 9 core params to knobs; the rest ship as VST-tuned defaults.
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
