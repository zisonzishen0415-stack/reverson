# Reverson — dynamic reverse reverb

From-scratch reverse reverb for Zoom G1on (ZDL) — developed first as a VST3 so
the sound can be tuned in a DAW, then ported to the pedal.

## Build (Windows, MSVC + CMake)

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Debug
    ctest --test-dir build -C Debug --output-on-failure

## Build the VST3

JUCE 7.0.12 is fetched automatically via FetchContent when GitHub is reachable.
If your network cannot clone the JUCE repo (large), provision it once locally
(e.g. from a mirror) and point at it:

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DREVERSON_BUILD_VST=ON -DJUCE_ROOT=C:/path/to/juce
    cmake --build build --config Release --target ReversonVST_VST3

Output: `build/vst/ReversonVST_artefacts/Release/VST3/Reverson.vst3`

## Test in a DAW

1. Copy `Reverson.vst3` into your DAW's VST3 folder (e.g. `C:\Program Files\Common Files\VST3`) and rescan.
2. Prefer a 44.1 kHz project (matches the G1on).
3. Load Reverson on a clean guitar track. Knobs P1: Mix/Decay/Tone, P2: RevLen/Duck/Gate; BYPASS button for wet/dry A/B.
4. The signature moves: trigger-based reverse swell per note; Duck rides the wet
   down while you play; Gate only lets the tail ring in the gaps.

## DSP architecture (from scratch, ZDL-safe: no heap / double / division / sinf
in the audio path)

    in -> duck -> FDN bed (8-line, dense continuous tail)
               +-> multi-head swell (13 taps, exp delays/gains, allpass smear)
             -> reverse gate envelope (accelerating attack, floor -> 1 -> cut)
             -> tone LP -> bass shelf -> sat -> width -> mix

- The reverse character is the SPX90-style multi-head swell: each note is
  answered immediately by near taps (zero predelay) and the far louder taps
  build a crescendo, then the line runs dry -> natural reverse gate. No
  non-causal / whole-file buffering; fully sample-by-sample causal.
- `gate` blends dense bed (0) -> full reverse (1); `shape` blends a linear
  attack (0) -> accelerating (1); `revlen` scales the swell span; `density`
  sets the hold time after the peak.