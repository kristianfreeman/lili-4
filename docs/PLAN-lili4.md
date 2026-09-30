# LILI-4: four petals, two oscillators each

Goal: a smaller, clearer instrument that stops reading as a Lyra-8, while keeping
the sound's density, beating and FM web.

## Model

- **4 petals, each two oscillators** (8 oscillators, as before), grouped **1·2** and **3·4**.
- Each petal has **one** pad, **one** sensor envelope and **one** touch thump. The pad gates both oscillators.
- **TUNE** sets the petal's pitch over C1–C7 (MIDI 24–96, continuous).
- **SPREAD** sets oscillator B relative to A: `12 · u³` semitones with `u = 2x − 1`. The centre is unison with fine detune (default about +2 cents, gentle beating); the ends are ±1 octave.
- **TIMBRE** (was Sharp): pulse shape, wavetable morph, or Seed position.
- **MOD**, **SOURCE** and **FAST**: per petal, unchanged in behaviour. Cross-mod partners: 1 ↔ 2, 3 ↔ 4; Switch crosses 1 ↔ 4 and 3 ↔ 2.
- **Per group** (1·2, 3·4): PITCH (semitones), HOLD, ENGINE, TABLE.
- **MIDI:** C1, D1, E1, F1 → petals 1–4.
- **Global:** unchanged (LFO A/B, echo, drive, Bloom, Drift, BEE).

## Identity (away from the Lyra)

- Product **LILI-4**, plugin code `Lil4`, bundle `com.kristianfreeman.lili4`. It's a new plugin to hosts.
- Parameter ids are rewritten per petal (`tune1..4`, `spread1..4`, `timbre1..4`, `mod1..4`, `source1..4`, `fast1..4`, `sensor1..4`, `pitch12/34`, `hold12/34`, `engine12/34`, `table12/34`).
- **Board:** 4 petals fanning up from the centre on a long stem; petal modules "PETAL 1–4"; "GROUP 1·2 / 3·4"; "LFO A/B"; "ECHO"; logo LILI-4; subtitle "A GARDEN DRONE · REV B".

## Steps

1. Engine and params (petal sensors, Tune/Spread, new ids) + tests.
2. Plugin: identity, MIDI map, editor references (glow per petal, readouts).
3. Board layout and art: new flower geometry, re-render, bake, export (glow layers `petal0..3`).
4. Docs, CI paths, install LILI-4 (remove the old LILI-8 bundles), pluginval, auval.
