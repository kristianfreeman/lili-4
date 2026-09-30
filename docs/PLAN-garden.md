# Garden engine: making LILI-8 its own instrument

Checkpoint before this work: tag `lyra-port-v1` (the faithful LIRA-8 port).

Goal: keep the organismic feel (slow swells, voices modulating each other,
touch pads) but move away from the Lyra's recipe. There are two big moves,
each opt-in so the classic sound stays available:

1. **Sound: new petal sources.** Each group (1234, 5678) chooses its engine:
   **Classic** (the current pulse/triangle), **Wave** (wavetables) or
   **Seed** (a granular sampler of your own audio file).
2. **Behaviour: Bloom and Pollinator.** Bloom slowly grows and fades voices
   and drifts their tuning and timbre over minutes. Pollinator is a chaotic
   (Lorenz) modulator that can replace the Hyper LFO.

## Design

**One knob, three meanings.** The per-pair **Sharp** knob becomes "timbre":
- Classic: triangle → square (unchanged).
- Wave: morph position within the table.
- Seed: grain position within the sample.

FM (Mod plus Source) keeps working in every engine, as a frequency multiplier.

**Wavetables.** Four procedural "botanical" families, generated at start-up,
so there are no sample assets to license:

| # | Name | Character across Sharp |
|---|------|------------------------|
| 0 | Stem | saw → hollow pulse (odd/even balance) |
| 1 | Reed | a formant sweeping up the spectrum (vocal-ish) |
| 2 | Glass | sparse bell-like partials, darker → brighter |
| 3 | Moss | seeded random spectra, frame to frame (organic, noisy) |

- Each family has 8 frames of 2048 samples.
- Each frame is band-limited per octave (11 mip levels via an inverse FFT), so FM doesn't alias.
- A per-group **Table** knob scans continuously across the 4 families, crossfading neighbours.

**Seed sampler.** One mono sample per group, loaded by dropping an audio file
on the left (1234) or right (5678) half of the board. Each voice runs a small
granular cloud:
- 2 overlapping Hann grains of 90 ms;
- **Tune** sets the playback rate against a C3 root;
- **Sharp** sets the read position;
- FM also jitters the grain position, so cross-modulation smears the sample.

The engine receives samples from the plugin through a lock-free pointer swap.
The plugin keeps every loaded sample alive, so the audio thread never frees
memory. File paths are saved in the plugin state.

**Bloom.**
- Per voice: a slow smoothed random walk. **Drift** sets its period, from 5 minutes (0) to 8 s (1).
- **Bloom** (0 = off) scales three things:
  - an extra "breath" gain that wakes and sleeps voices without notes;
  - ±40 cents of tune drift;
  - ±0.3 of timbre drift.
- At Bloom 0 the engine is bit-for-bit the classic one.

**Pollinator (BEE switch).** A Lorenz attractor replaces the Hyper LFO outputs:
- LFO Freq A sets flight speed; LFO Freq B sets chaos (ρ from 20 to 45).
- x feeds the voices' LFO FM source; y and z feed the delay modulation.
- The leaf LEDs flicker with the sign of x and y ("wings").

**New parameters** (appended; ids are stable): `engine1234`, `engine5678`
(Classic/Wave/Seed), `table1234`, `table5678`, `bloom`, `drift`, `bee`.

**Board layout** (`art/board.json`, then re-render):
- Each group module gains an ENGINE 3-way toggle and a TABLE knob in its empty right half.
- BLOOM and DRIFT knobs plus the BEE toggle sit above the flower, between S2 and S5.

The editor builds its controls from `board.json`, so no UI code changes are
needed for layout.

## Phases (each ends green: tests, lint, commit)

1. **Wavetable engine:** table generator (small FFT), per-group engine and table, Sharp as morph. Tests: pitch, band-limiting (no energy above Nyquist at high notes), morph continuity. Benchmark.
2. **Seed sampler:** sample handoff, granular voice. Tests: rate follows Tune, silence without a sample, no allocation on the audio thread.
3. **Bloom and Pollinator.** Tests: Bloom 0 is identical to the classic engine; Bloom > 0 wakes voices without gates; Lorenz stays bounded.
4. **Plugin:**
   - parameters;
   - drag-and-drop seed loading and state save/restore;
   - readouts (engine, table family, seed file name, bloom, drift);
   - board layout and re-rendered art;
   - snapshots.
5. **Validation:** pluginval (GUI), auval, docs (SPEC, ART, README), install.

## Out of scope for now

- Per-voice engine choice, user wavetable import, MPE, new effects
  (Dew/Roots), free cross-mod patching. They're good follow-ups once this
  core lands.
