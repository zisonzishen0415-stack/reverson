# Reverson Core v2 — Design Spec

Date: 2026-08-14
Status: approved (user authorized autonomous execution, no further review gates)

## Goal

Upgrade the Reverson reverse-reverb core so it sounds better **and** costs less
(memory + CPU), with the Zoom G1on / ZDL platform as the acceptance standard.
The existing signature (dense always-present reverse swell, SPX90 multi-head,
zero predelay) is preserved at the low end of the `Rev` knob and extended into
a true backward-read "reverse playback" layer at the high end.

All DSP work lands in `core/` first (desktop ctest-verified), then the VST
exposes it, then a 9-knob ZDL is built for the G1on.

## Constraints (hard)

1. **ZDL-safe audio path**: no division, no `sinf`/`powf`/`expf`, no heap, no
   `double`, all memory caller-provided. Init-time helpers may use transcendentals.
2. **Memory**: no new large buffers. The reverse layer must reuse the existing
   32768-sample swell delay line (`REV_SWELL_BUF_LEN`). Target total state
   stays ~160 KB (`REVERSON_ENABLE_FDN=0`).
3. **CPU**: per-sample op count must go *down* overall. Wins come from
   8-sample block smoothing, a mod=0 fast path, and an early-out of the tap
   loop at high `Rev` (where the reverse layer takes over).
4. **Pedal is the acceptance standard**: 8-sample callbacks, params[5..13] =
   9 user knobs, 3 pages x 3.
5. Causal, sample-by-sample: no non-causal buffering, ever.

## Signal flow (v2)

```
inL,inR (stereo API) --(mono sum)---> onset env (sensitivity-thresholded)
      |                                   |
      |                                   v
      |                           trigger logic (predelay counter)
      |                                   |
      v                                   v
   duck (env_peak-normalized)      reverse envelope state machine
      |                             (rise->1+over, settle->1, hold, fall->floor)
      v                                   |
   mono wet input -------------------------+
      |
      +--> rev_swell: write line, 13 forward taps (amount *= 1-rev_mix)
      |        [mod=0 fast path; early-out when amount ~ 0]
      +--> rev_reverse: backward read of the SAME line (gain = rev_mix)
      |
      +--> blend taps/reverse -> 3-stage diffuser WITH crossfeed (L/R)
      |
      +--> tone LP -> wet HPF (~110 Hz fixed) -> bass shelf -> sat
      |
      +--> width -> * rev_env (output gate) -> mix
out_l = inL*(1-mix) + wet_l*mix      out_r = inR*(1-mix) + wet_r*mix
```

## Component specs

### 1. `core/rev_reverse.c/h` — backward-read layer (NEW)

The true "reverse" content: on trigger, a read head starts just behind the
current write position and moves **backward** through the shared line, so it
plays the pre-onset content (previous note tail) reversed — the classic
reverse-gate mechanism. Causal because it only reads samples already written.

State (all floats, ~24 B total): `rpos` (fractional read position), `rinc`
(= 1.0 samples/sample, fixed), `active` flag, small entry fade.

- **Trigger**: `rev_reverse_trigger(rr, line)` sets `rpos = write_pos - pre_off`,
  `pre_off = 0.004 * sample_rate` (4 ms, skips the attack transient click),
  `active = 1`.
- **Per sample**: `rev_reverse_process(rr, line, in_rev_env, gain, &out_l, &out_r)`:
  - if `!active` or `gain < 1e-4`: output 0, return.
  - `v = rev_delay_read_frac(line, rpos)`; `rpos -= rinc`.
  - Guard: `min_dist = 4` samples behind write head — if `rpos >= wpos - min_dist`
    or `rpos < wpos - (BUF_LEN-2)`, clamp to `wpos - min_dist` (wrap the span).
  - `out_l = out_r = v * gain * fade`, where `fade` eases in over 5 ms.
- **Gain staging**: `gain = rev_mix * reverse_out` where `reverse_out ≈ 0.8` so
  the layer's peak matches the forward tap sum's peak; final levels are tuned
  in the render A/B (M2) and are a named constant, not a user knob.
- **Span cap**: the layer is driven by `rev_env` (it only outputs while the
  envelope is above the floor), so effective reverse spans stay ≤ ~600 ms and
  always inside the 32768-sample buffer at 48 kHz.
- The line write remains in `rev_swell` (single writer); `rev_reverse` is
  read-only. Blend happens in `reverson.c`, before diffusion.

### 2. `core/rev_swell.c` — taps lightening + crossfeed diffusion

- **Split API**: `rev_swell_process` is refactored into
  `rev_swell_taps(s, in, &l, &r)` (writes line, sums taps) and
  `rev_swell_diffuse(s, l, r, &l, &r)` (diffusion chain). `reverson.c` calls
  taps → blends reverse layer → diffuses. The old single-call wrapper stays for
  the tests that use it.
- **Mod=0 fast path**: when `mod_depth < 1e-3`, skip the per-tap triangle LFO
  entirely; taps read at `base_delay * scale` directly (still interpolated —
  the 2026-08-12 zipper fix stays).
- **Early-out**: when `amount < 1e-3` skip the whole tap loop (this is the
  steady state at high `Rev`, where the reverse layer owns the sound).
- **Crossfeed**: between channels after each feedback-allpass stage:
  `l' = l + cf*r; r' = r + cf*l` with `cf = 0.35 * diff_fb[st]`
  (≈ 0.18/0.16/0.14 per stage). Two mul + two add per stage. Keeps the
  mutually-prime allpass delays; boundedness is covered by the existing
  boundedness suite plus a new crossfeed gain test.

### 3. Reverse envelope state machine (in `core/reverson.c`)

States: `0 idle(floor)`, `1 rising->(1+over)`, `2 settle->1`, `3 hold@1`,
`4 fall->floor`. Value-based re-planning on retrigger is preserved (no dip,
no wobble).

- `rise = (0.05 + 1.95*revlen) * sr` (unchanged)
- `over = 0.16 * shape` (accelerating attacks overshoot; linear do not)
- `settle`: fixed 40 ms linear ramp `(1+over) -> 1`
- `hold = (0.30 + 0.70*density) * sr + hold_add` (see Trig knob)
- `fall = hold * 0.5`, then floor (floor logic unchanged)

### 4. Trigger sensitivity + predelay (`core/rev_env.c`, `core/reverson.c`)

- `rev_env_set_thresh(e, rel)`: onset threshold = `max(rel * onset_peak, floor)`
  replacing the fixed 0.35 relative threshold.
- **Trig knob** (page 3, knob 2): `rel = 0.12 + 0.38*trig`
  (0.12 = easy trigger → 0.50 = forgiving), `hold_add = (0.05 + 0.75*trig) * sr`
  (0.05 s → 0.8 s). One knob, linked, as approved.
- **Predelay knob** (page 3, knob 3): `pd = 0.120 * predelay * sr` (0..120 ms).
  Onset detection starts a one-shot counter; the trigger (envelope re-plan +
  reverse-head reset) fires when the counter expires. The wet floor is
  unaffected — only the swell/trigger and the reverse read start are delayed.

### 5. Block smoothing (`core/reverson.c`)

- `smooth_timer` counts 0..7; every 8th sample the 13 `cur` params take one
  smoothing step with `coef_b = 1 - (1-c)^8`, computed at init by repeated
  squaring (3 multiplies, ZDL-safe). Equivalent tracking behavior, 1/8 the
  per-sample cost.
- Derived values (`rev_swell_set`, `set_mod`, diffuser `diff_fb`, HPF/LP
  coefficients) also recompute on the same 8-sample grid.

### 6. Wet high-pass (`core/reverson.c`)

One-pole HPF, fixed 110 Hz, per channel:
`y = a*(y + x - x1); x1 = x;  a = exp(-2*pi*110/sr)` (precomputed at init).
Placed after the tone LP, before the bass shelf. 1 state + 2 mul + 2 add per
channel. Not user-exposed.

### 7. Stereo API (`core/reverson.h`)

- `void Reverson_process_stereo(Reverson*, float in_l, float in_r, float *out_l, float *out_r)`
- `mono = 0.5*(in_l+in_r)` drives env/duck/trigger/wet (single wet engine,
  unchanged). Dry stays stereo: `out_l = in_l*(1-mix) + wet_l*mix`,
  `out_r = in_r*(1-mix) + wet_r*mix`. The wet width synthesis is unchanged.
- `Reverson_process` becomes a wrapper (`in_l = in_r = in`).
- VST and ZDL both call the stereo entry point. On ZDL the callback buffers
  are already `LLLLLLLL RRRRRRRR`, so the dry stereo image is preserved even
  though the stock stereo-routing declaration is still unmapped.

### 8. `Rev` continuum (mapping, `map6`/`map3` in `core/reverson.c`)

- `rev_mix = rev * rev` (0 at wash, 1 at full reverse).
- Forward taps: `amount = gate * (1 - rev_mix)` (replaces `amount = gate`).
- Reverse layer gain: `rev_mix` (see §1).
- The output reverse-gate envelope applies to the whole wet as today; at high
  `rev_mix` the reverse layer's natural shape dominates, which is the goal.
- `map3` gets the same treatment so the old 3-knob path stays consistent.

## Parameter model

16 internal params: the existing 13 + `Trig`, `Predelay`, `Over`
(`Over` is curve-owned: `over = 0.16*shape`, not a knob).

| Page | Knob | Owns | Notes |
|---|---|---|---|
| P1 | Mix | mix | unchanged |
| P1 | Rev | gate/shape/density + rev_mix | wash -> reverse -> gated continuum |
| P1 | Space | revlen/decay/width | unchanged |
| P2 | Tone | tone/bass/sat | unchanged |
| P2 | Grain | diffusion/mod | unchanged |
| P2 | Duck | duck | unchanged |
| P3 | Mode | 5-position enum | Wash/Reverse/Gated/Shoegaze/Space -> preset param sets |
| P3 | Trig | trigger rel + hold_add | linked (approved) |
| P3 | Predelay | trigger predelay 0..120 ms | |

ZDL params[5..13] carry P1..P3 in order; the Mode switch is an integer-valued
param (params[11]) with 5 table entries in the ZDL body.

## Presets (acceptance)

| Preset | Feel | Key settings (internal) |
|---|---|---|
| `mbv` | "Sometimes"-style gated reverse | rev 0.85, shape 0.8, trig 0.35, predelay 0.1, space 0.55, mix 0.6 |
| `diiv` | clean spacious wash (current default direction) | rev 0.25, mod 0.4, diffusion 0.35, space 0.6, mix 0.55 |
| `slowdive` | big ambient wash | rev 0.35, mod 0.55, diffusion 0.55, space 0.85, mix 0.7 |

Exact values tuned during M2 render A/B; `diiv` becomes the shipped default
(ZDL hardcoded param table, since ZDL init materialization is still unsolved).

## CPU/memory budget (v2 vs current)

- Large buffers: **+0** (reverse layer reuses the swell line; HPF adds 2 floats).
- Per-sample ops, typical mix: current ~250 → v2 ~90-120 (block smoothing
  ÷8, mod=0 fast path −52, tap early-out −143 replaced by +8 reverse read,
  crossfeed +6, HPF +4). Worst case (low Rev, mod on, bed on) ≈ current.
- ZDL total state: ≈ 160 KB, `REVERSON_ENABLE_FDN=0` (unchanged policy).

## Testing plan (M1)

New ctest suites (all desktop, no hardware):

1. `rev_reverse`: causality (read pos < write pos always), trigger->output
   correlation vs reversed input buffer, boundedness, no NaN, span clamp.
2. `crossfeed`: energy bound across cf range, L/R decorrelation improves vs
   crossfeed off, no NaN.
3. `wet_hpf`: sine at 50/110/300 Hz — attenuation order correct.
4. `env_state`: overshoot peak > 1, settle lands on 1, hold/fall timing,
   retrigger re-planning.
5. `trigger`: predelay shifts trigger by exactly N samples; sensitivity
   threshold: same input triggers at low rel, not at high rel.
6. `block_smooth`: per-sample vs 8-sample smoothing equivalence within bound.
7. `stereo_api`: L≠R dry input stays separated in dry path; wet path equals
   mono-engine output; mono wrapper equals stereo with L=R.
8. Full regression of the existing 8 suites.

## G1on port (M3/M4)

1. Build the 9-knob **Reverson ZDL** under `ZoomMultistompZDL/src/custom/reverson/`
   following the StChorus/ToTape9 pattern: `ctx[3]` state, synthesized
   LineSel-clone edit handlers for knobs 4..9, `REVERSON_ENABLE_FDN=0`, page-3
   mode switch + Trig + Predelay, `diiv` hardcoded default param table.
2. **K9Probe first** (already built): flash `build/probes/K9Probe.ZDL` and
   follow its README pass criteria (9 knobs audible per page, no freeze).
   This de-risks the only unproven ABI surface before the real effect.
3. Flash Reverson ZDL: load, bypass, all 3 pages, audio, no freeze.
4. Known quirks to check on G1on: category visibility (may need a stock effect
   of the same category installed), param normalization on knobs 4..9.
5. Update README + `research_docs/` with G1on results.

## Milestones

- **M1** core v2 DSP + all tests green (done before hardware arrives).
- **M2** VST: 16 params, page-3 editor (Mode/Trig/Predelay), stereo input;
  render tool gains `trig/predelay/over` overrides + `mbv/diiv/slowdive`
  presets; render A/B outputs in `out/` for listening.
- **M3** ZDL build (Reverson + re-verify K9Probe) + flash checklist ready.
- **M4** G1on hardware validation, preset hardcoding, docs.

## Risks

- K9Probe page 2/3 freeze → triage path in its README (EDIT-HANDLER-ABI §7/§8).
- `rev_mix` gain staging (reverse layer vs tap sum loudness) → render A/B tune.
- Reverse span ≤ ~600 ms @48k by buffer size — accepted (MBV-style swells are
  200-500 ms); documenting, not blocking.
- G1on category-visibility quirk → checklist item, workaround known.

## Out of scope (this pass)

- FDN bed: stays compiled out for ZDL (VST keeps it, unchanged).
- Stock stereo-routing declaration reverse-engineering.
- Tempo sync.
- ZDL init-time parameter materialization (presets hardcoded instead).
