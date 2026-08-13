# Asset sources

## strum_arp_cc0.wav
- Source: Freesound, "Clean Strumming & Arpeggio" by loudernoises
  https://freesound.org/s/453384/
- License: Creative Commons 0 (public domain, no attribution required)
- Content: clean electric guitar, strumming + arpeggio in one take,
  10.8 s, 44.1 kHz, 16-bit stereo (converted from preview mp3).
- Note: the source take is strongly low-passed (little energy above 2 kHz),
  so it reads as a dark / DI-ish tone rather than an amp-in-the-room tone.

## cleanarp_by40.wav
- Source: Freesound, "R_Holliday - ElectricGuitarCleanArp (Bpm 81.43).wav"
  by ReyHolliday
  https://freesound.org/s/352246/
- License: CC BY 4.0 (attribution required: ReyHolliday via Freesound)
- Content: clean electric guitar E-major arpeggio, 29.5 s,
  44.1 kHz, 16-bit stereo (converted from preview mp3).
- Kept for reference / alternate take; prefer strum_arp_cc0.wav for
  repo-committed demo renders so no attribution is required.

## surf85_amp.wav / surf100_amp.wav (local, generated)
- Source: tosound mirror of Freesound 316990 / 316988
  ("冲浪摇滚吉他 85 bpm" / "冲浪摇滚吉他 100 bpm A调")
  https://freesound.org/s/316990/ (original page now deleted)
- License: not verifiable (Freesound page removed); local demo use only.
- Content: clean surf-rock electric guitar strumming, 46 s / 40 s.
- Generated with tools/amp_sim.py (drive 0.35, cab 0.85, neve 0.6, pos pre)
  so the take reads as a clean amp + 1x12 cab with Neve-style console
  coloration. Neve is placed BEFORE the cab by default: the cab's high
  rolloff tames the saturation harmonics so they never sound harsh.

## Pre-existing assets (not from this project's search)
- PIXIES.wav, DC120.wav, loop1.wav, Loop2C.wav, candidates.png: from the
  M3Lab hacked-guitar project (see assets/readme.md).
