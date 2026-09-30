# LILI-8 DSP specification

This is a transcription of the reference Pure Data patch
(`reference/lira-8-pd`, LIRA•8 by Mike Moreno DSP, upstream commit
`6a6b6cb`) into plain math. It is the contract the C++ engine in `dsp/`
implements. Where the reference does something odd, the oddity is recorded
under **Quirk** and the native decision under **LILI-8**.

Conventions:

- `SR` = sample rate. `x` = a knob's normalized value, `v / 127` of the
  reference's 0–127 range. All plugin parameters are stored as `x ∈ [0,1]`
  (or an integer choice).
- `smooth(·)` = the reference's `[$1 23.22( → line~`, a linear ramp to the new
  value over 23.22 ms. LILI-8 uses a one-pole smoother with a comparable
  settling time. The difference is inaudible and it doesn't zipper.
- `mtof(n) = 440 · 2^((n − 69) / 12)`.
- `dbtorms(d) = 10^((d − 100) / 20)` for `d > 0`, else 0 (Pd's 100 dB = unity convention).
- `tanhP(x)` = the reference's `ma.tanh~`, the Padé approximation
  `c · (27 + c²) / (27 + 9c²)` with `c = clip(x, −3, 3)`. LILI-8 uses it
  everywhere the reference does, because its knee is audible.
- To see the flow for yourself, dump any reference patch with
  `python3 tools/pdview.py reference/lira-8-pd/<file>.pd`.

## Topology

```
          ┌──────── group 1234 ────────┐   ┌──────── group 5678 ────────┐
 sensors  │ pair 12        pair 34     │   │ pair 56        pair 78     │
 1..8  ──▶│ (voices 1,2)   (voices 3,4)│   │ (voices 5,6)   (voices 7,8)│
          └─────┬──────────────┬───────┘   └─────┬──────────────┬───────┘
                └──────────────┴────── Σ × 0.16 ─┴──────────────┘
                                          │
                              ┌───────────▼────────────┐
          hyper LFO ─────────▶│ dual mod delay (mono)  │
                              └───────────┬────────────┘
                              ┌───────────▼────────────┐
                              │ drive / dist / volume  │──▶ out L = out R
                              └───────────┬────────────┘
                                          └─▶ "total feedback" (FM source)
```

The engine is mono; the reference sends the same signal to both outputs.

Each **pair** has two voices that share Sharp, Mod, Source, Fast and a vibrato
LFO. Each **group** (1234, 5678) shares Pitch and Hold.

## Parameters

| Reference name  | Internal key  | Range / choices                     | Default (ref, 0–127) |
|-----------------|---------------|-------------------------------------|----------------------|
| Fast 12..78     | `fast-XY`     | 0/1                                 | 1                    |
| Tune 1..8       | `tune-N`      | x                                   | 32, 64, 32, 64 …     |
| Sharp 12..78    | `sharp-XY`    | x                                   | 0                    |
| Mod 12..78      | `mod-XY`      | x                                   | 0                    |
| Source 12..78   | `source-XY`   | 0 = other pair, 1 = OFF, 2 = LFO/FB | 1                    |
| Pitch 1234/5678 | `pitch-G`     | x                                   | 64                   |
| Hold 1234/5678  | `hold-G`      | x                                   | 0                    |
| Switch          | `switch`      | 0/1                                 | 0                    |
| Total FB        | `total-fb`    | 0/1                                 | 0                    |
| Vibrato         | `vibrato`     | 0/1                                 | 0                    |
| LFO:Freq A/B    | `f-a`, `f-b`  | x                                   | 64                   |
| LFO:AND/OR      | `andor`       | 0 = AND, 1 = OR                     | 0                    |
| LFO:Link        | `link`        | 0/1                                 | 0                    |
| Del:Time 1/2    | `time-1/2`    | x                                   | 64                   |
| Del:Feedback    | `feedback`    | x                                   | 64                   |
| Del:Mix         | `del-mix`     | x                                   | 0                    |
| Del:Mod 1/2     | `mod-1/2`     | x                                   | 0                    |
| Del:Source      | `del-mod`     | 0 = SELF, 1 = OFF, 2 = LFO          | 2                    |
| Del:Waveform    | `lfo-wav`     | 0 = TRIANGLE, 1 = SQUARE            | 0                    |
| Dist:Drive      | `drv`         | x                                   | 64                   |
| Dist:Mix        | `dst-mix`     | x                                   | 64                   |
| Volume          | `vol`         | x                                   | 127                  |
| (not exposed)   | `quantize`    | 0/1: snap voice pitch to semitones  | 0                    |

## Sensors (MIDI)

MIDI notes 36–43 (C1–G1 in Live's naming) gate voices 1–8. Velocity > 0 is on,
velocity 0 or note-off is off, and velocity is otherwise ignored.
The GUI's sensor pads latch (click = hold).

## Voice pitch (`prm.tune`, `prm.pitch`)

Per-voice table `[lo, hi]` in MIDI note numbers (index = voice − 1):

| voice | 1   | 2   | 3   | 4   | 5      | 6      | 7      | 8      |
|-------|-----|-----|-----|-----|--------|--------|--------|--------|
| lo    | −16 | −16 | 7   | 9   | 20     | 20     | 33     | 33     |
| hi    | 93  | 93  | 109 | 107 | 116.54 | 116.54 | 126.24 | 131.22 |

```
pitchMul_G = 2^(round(12 · log2(0.01 + 1.99 · x_pitch-G)) / 12)   (group multiplier, semitone steps)
f_N        = mtof(lo_N + x_tune-N · (hi_N − lo_N)) · pitchMul_G
if quantize: f_N = mtof(round(ftom(f_N)))
f_N        = smooth(f_N)
```

**LILI-8:** the reference's group Pitch is a continuous 0.01–2.0 multiplier. LILI-8 snaps it to whole semitones (−80 to +12 st). The default x = 64/127 is exactly 0 st, and group transpositions stay in tune with each other. The per-voice Tune knobs stay continuous. The 5 ms frequency smoother turns each step into a short glide.

## Pair vibrato

Each pair has a sine LFO. Its rate is picked at random when the plugin loads:
`rate = 0.5 + 3 · U{0..1000}/1000` Hz (0.5–3.5 Hz). The reference seeds the
choice from wall-clock ms plus the pair's first voice index, so every instance
differs. LILI-8 takes a seed in `EngineConfig` so renders can be reproduced.

```
vib = cos(2π · rate · t) · vibratoSwitch
```

It modulates both voices in the pair:

- frequency: `f ← f · (1 + 0.005 · vib)` (±0.5 %, about ±8.6 cents)
- pulse width: `pw = 0.53125 + 0.025 · vib`

## Oscillator (`os.triangle~`)

A phase accumulator `p ∈ [0,1)` advancing by `dt = |f_osc| / SR` per sample.
FM can push `f_osc` negative; the reference takes `|·|`, so the phase always
runs forward.

- **Pulse:** a PolyBLEP pulse built from two band-limited saws,
  `saw(p) − saw(wrap(p + pw_held)) + 2·pw_held − 1`, with
  `saw(t) = 2t − 1 − blep(t, dt)` and the standard polyBLEP residual.
  `pw_held = clip(pw, 0, 1)`, latched when the phase wraps (`samphold~`).
- **Square out** = `0.59 · pulse`.
- **Triangle out** = `2.65 · LP1z(pulse, fc) − 0.145`, where `LP1z` is the
  one-pole `y = (1 − a)·x + a·y`, `a = exp(−2π · fc / SR)`,
  `fc = min(max(|f_osc| / 4, 100), SR/2)` Hz. The 2.65 gain restores amplitude,
  and −0.145 cancels the DC offset that comes from `pw ≈ 0.53`.

## Sensor envelope (`prm.sensor`)

A linear ramp `l` moves toward 1 when the gate opens and toward 0 when it
closes:

| Fast | attack | release |
|------|--------|---------|
| 1    | 100 ms | 100 ms  |
| 0    | 200 ms | 8000 ms |

The ramp times are full-scale segment durations: `line~` reaches the target in
exactly that time from wherever it is. Changing Fast re-sends the current gate
state, so a ramp in progress restarts with the new time from its current value.

Outputs:

- `amp   = l²`
- `thump = 0.5 · sin(2π · l)`, a one-cycle low bump added to the audio
  *before* the amplitude gain. It gives a touch transient on attack and release.

## Pair (`lira.voice`)

For the pair's voices A and B, with shared `s = smooth(x_sharp²)` and
`m = smooth(2 · x_mod⁴)`:

```
modSig  = sourceSignal · m                    (see Source matrix; shared by A and B)
fA_osc  = fA_vib · (1 + modSig)
voiceA  = sq_A · s − tri_A · (1 − s)           (note the triangle's polarity flip)
gainA   = min(amp_A + hold_G, 1)
outA    = (voiceA + thump_A) · gainA
pairOut = outA + outB
pairTap = HP1(pairOut, 3 Hz)                   (the signal that FMs other pairs)
```

`hold_G = smooth(x_hold²)`. At `hold = 1` every voice in the group drones.

## Source matrix (`prm.source`)

Each pair `P` has one "cross" partner for Switch = 0 and one for Switch = 1:

| pair | Switch = 0 | Switch = 1 |
|------|------------|------------|
| 12   | 34         | 78         |
| 34   | 12         | 12         |
| 56   | 78         | 34         |
| 78   | 56         | 56         |

`leak(b) = 0.001 + 0.999 · b` (the reference never fully gates a path to zero).

```
cross        = tap[partner0] · leak(switch == 0) + tap[partner1] · leak(switch == 1)
lfo          = sqrLfo · leak(totalFb == 0) + totalFeedback · leak(totalFb == 1)
sourceSignal = cross · leak(source == 0) + lfo · leak(source == 2)
```

**Quirk:** because of the 0.001 leak, Source = OFF still lets −60 dB of
cross-mod through. **LILI-8:** kept, since it's part of the sound.

**Quirk:** in Pd, `s~`/`r~` feedback lags by one 64-sample block whenever the
receiver runs before the sender in Pd's DSP sort order (0 samples otherwise),
so the FM paths between pairs, and from total feedback, lag 0 or 64 samples
depending on sort order.
**LILI-8:** these loops run with a 1-sample delay by default, which is closer
to the analog hardware. `EngineConfig::legacyBlockFeedback = true` restores a
64-sample delay on those paths for A/B comparison against reference renders.

## Hyper LFO

```
fA   = smooth(mtof(127 · x_fa² − 75))           (≈ 0.107 Hz .. 164.8 Hz)
fB   = smooth(mtof(127 · x_fb² − 75))
pA   : phase at fA
sqA  = clip(1000 · cos(2π pA), −1, 1)
pB   : phase at fB · (1 + link · sqA / 2)       (Link: A's square FMs B by ±50 %)
sqB  = clip(1000 · cos(2π pB), −1, 1)
triX = 4 · min(pX, 1 − pX) − 1

sqrLfo = andor == AND ? sqA · sqB : (sqA + sqB) / 2      → voice FM source
delSqr = (sqA + sqB) / 2                                 → delay mod
delTri = (triA + triB) / 2                               → delay mod
```

**Quirk:** "AND" is really an XOR-like product of the two ±1 squares.

## Dual mod delay

Input `in` is the voice sum × 0.16. Noise at −60 dB is added to what the delays
see: `inN = in + 0.001 · noise` (uniform white noise in [−1, 1]).

Two independent delay lines (not ping-pong), each up to 5944 ms, both fed from
`inN`:

```
time_k    = smooth(1.45125 · 2^(12 · x_time-k)) ms    (1.45 ms .. 5944 ms)
depth_k   = smooth(10 · x_mod-k²) ms
fb        = smooth(x · 2^(2x)), x = x_feedback        (0 .. 4)

lfoMod    = (lfo-wav == TRI ? delTri : delSqr) · [del-mod == LFO]
selfMod_k = LP1(write_k / 2, 689 Hz) · [del-mod == SELF]
read_k    = delay_k.read4(time_k + depth_k · (lfoMod + selfMod_k))   (4-pt interp, as vd~)
loop_k    = expander(compressor(LP1(HP1(read_k, 1 Hz), 4000 Hz)))
write_k   = inN + fb · loop_k
wet       = tanhP((loop_1 + loop_2) / max(fb, 1.5))
out       = 0.7 · in · (1 − mix) + mix · wet,   mix = smooth(x_del-mix)
```

### "Compressor" (attack 2.5 ms, release 2.5 ms, threshold −12 dB, ratio 5)

```
env : peak follower on |x|; coeff = exp(ln(0.01) / max(1, SR · ms / 1000)),
      attack coeff when |x| > env, else release; env = |x| + coeff · (env − |x|)
T   = dbtorms(−12 + 100)
g   = tanhP(clip(env / (T + (env − T) / 5), 0, 1))
y   = x · g
```

**Quirk:** the gain is inverted relative to a textbook compressor
(`env/level_out` instead of `level_out/env`). Above threshold, `g` saturates at
`tanhP(1) ≈ 0.777`, a fixed gain. Below threshold it attenuates, like a soft
gate. It never limits. With `fb > 1/0.777 ≈ 1.29` (`x_feedback ≳ 0.55`) the
loop gain exceeds 1 and the loop runs away. Pd floats let it grow until only
the `tanhP` on the wet output hides it, and eventually it reaches inf.
**LILI-8:** reproduce the curve exactly, and add a transparent safety
saturator on `write_k` (`4 · tanh(x / 4)`). It does nothing below about ±1
and stops runaway. High feedback still gives the intended hot,
self-oscillating wash.

### Expander (threshold −60 dB, ratio 5)

```
L   = RMS level (dB, 0 dBFS = 0) over the last 512 samples, updated every 256 samples (env~ 512)
gdB = min(0, (L + 60) · (1 − 1/5))
g   = dbtorms(ramp(gdB, 11 ms) + 100)
y   = x · g
```

## Master output: drive, distortion, volume

```
dG      = smooth(dbtorms(3^(2x+1) + 3 + 100)),  x = x_drv       (≈ 2 .. 31.6 linear)
t       = tanhP(HP1(in · dG, 20 Hz))
shaped  = HP1(t + t³¹ / 4, 10 Hz) / clip(dG, 1, 4)
wetDist = shaped + 0.1 · in
mixed   = in · (1 − d) + wetDist · d,     d = smooth(x_dst-mix)
out     = mixed · smooth(x_vol²)

totalFeedback = HP1(tanhP(mixed), 3 Hz)   (taken before volume; an FM source)
```

`t³¹` is an odd power, so it keeps the sign. It only matters where `t` is
close to ±1, where it sharpens the clipped edges.

## Filters

- `HP1(x, fc)`: Pd's `hip~`, where `c = clamp(1 − 2π·fc/SR, 0, 1)`,
  `w = x + c·w₋₁`, `y = (1 + c)/2 · (w − w₋₁)`.
- `LP1(x, fc)`: Pd's `lop~`, where `k = clamp(2π·fc/SR, 0, 1)`, `y += k·(x − y)`.

## Garden engine (LILI-8's own; not in the reference)

Everything here is opt-in. With Engine = Classic, Bloom = 0 and BEE off, the
engine is bit-identical to the reference port above (tested). See
`docs/PLAN-garden.md` for the design rationale.

**Petal engine per group** (`engine1234`, `engine5678`): Classic / Wave / Seed.
Pitch, FM (`f_osc = f · (1 + modSig)`), vibrato, the sensor envelope, the thump
and Hold are shared by all three. Only the waveform source changes, and the
per-pair **Sharp** becomes "timbre":

- **Classic:** `sq · s − tri · (1 − s)` as above.
- **Wave:** `0.6 · WT(family, morph = √s, phase)`.
  - `WavetableBank` has 4 procedural families (Stem, Reed, Glass, Moss), 8 frames × 2048 samples each.
  - Each frame is band-limited to 11 per-octave mip levels (level `L` keeps harmonics ≤ 1024 ≫ L, chosen so the top harmonic stays below Nyquist for the current phase increment).
  - `table` (0..1) scans the families; frames and families crossfade linearly.
- **Seed:** a granular read of the group's sample, scaled by 0.8.
  - Two Hann grains of 90 ms at 50% overlap (unity sum).
  - Rate `= |f_osc| / 130.81 Hz × sr_sample / sr`, so a voice at C3 plays the recording as-is.
  - Grain start `= wrap(√s + 0.004 · noise + 0.02 · clip(modSig, ±1)) · length`, and the sample loops.
  - No sample means silence.

**Bloom** (`bloom`, `drift`). Each voice runs two smoothstepped value-noise walks (`walk`, `breath` ∈ [−1, 1]):
- Segment period `= 300 s · (8/300)^drift`; updated every 32 samples.
- With `d = bloom`:
  ```
  f_osc     ·= 2^(d · 40/1200 · walk)        (±40 cents)
  sharp_v    = clip(s_pair + 0.3 · d · walk, 0, 1)
  gain_v     = min(l² + hold_G + 0.6 · d · smoothstep(wake), 1),
               wake = clip((0.5 + 0.5 · breath − 0.35) / 0.4, 0, 1)
  ```

**Pollinator** (`bee`). A Lorenz system replaces the Hyper LFO outputs:
- σ = 10, β = 8/3, ρ = 20 + 25 · x_fb; Euler steps with `dt = max(fA, 0.05) / sr · 0.8`.
- Outputs:
  ```
  sqrLfo = tanh(x / 12)       (voice FM source)
  delTri = tanh(y / 15)
  delSqr = clip(z / 25 − 1, ±1)
  ```
- Freq A is flight speed, Freq B is chaos. The leaf LEDs follow sign(x) and sign(y).
- The state resets if it ever goes non-finite.

## Future (beyond the reference)

- MPE / poly pressure: continuous sensor amount (`l` slews toward the pressure
  value at the attack/release rates, instead of toward 0/1).
- Oversampling (2–4×) for the FM oscillators and the distortion stage.
- Audio input into the delay/distortion section (effect mode).
- SIMD: the four pairs are uniform, so they can run as 4-wide lanes.
