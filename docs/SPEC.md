# LILI-4 DSP specification

This is a transcription of the reference Pure Data patch
([LIRA•8](https://github.com/MikeMorenoDSP/LIRA-8/tree/6a6b6cb) by Mike Moreno
DSP, upstream commit `6a6b6cb`) into plain math. It is the contract the C++ engine in `dsp/`
implements. Where the reference does something odd, the oddity is recorded
under **Quirk** and the native decision under **LILI-4**.

Conventions:

- `SR` = sample rate. `x` = a knob's normalized value, `v / 127` of the
  reference's 0–127 range. All plugin parameters are stored as `x ∈ [0,1]`
  (or an integer choice).
- `smooth(·)` = the reference's `[$1 23.22( → line~`, a linear ramp to the new
  value over 23.22 ms. LILI-4 uses a one-pole smoother with a comparable
  settling time. The difference is inaudible and it doesn't zipper.
- `mtof(n) = 440 · 2^((n − 69) / 12)`.
- `dbtorms(d) = 10^((d − 100) / 20)` for `d > 0`, else 0 (Pd's 100 dB = unity convention).
- `tanhP(x)` = the reference's `ma.tanh~`, the Padé approximation
  `c · (27 + c²) / (27 + 9c²)` with `c = clip(x, −3, 3)`. LILI-4 uses it
  everywhere the reference does, because its knee is audible.

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

The reference engine is mono and sends the same signal to both outputs. LILI-4
does the same with Stereo off, and adds a stereo image on top of it (see **Stereo**).

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
| (LILI-4 only)   | `stereo`      | 0 = MONO OUT, 1 = STEREO OUT        | 1                    |

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

**LILI-4:** the reference's group Pitch is a continuous 0.01–2.0 multiplier. LILI-4 snaps it to whole semitones (−80 to +12 st). The default x = 64/127 is exactly 0 st, and group transpositions stay in tune with each other. The per-voice Tune knobs stay continuous. The 5 ms frequency smoother turns each step into a short glide.

## Pair vibrato

Each pair has a sine LFO. Its rate is picked at random when the plugin loads:
`rate = 0.5 + 3 · U{0..1000}/1000` Hz (0.5–3.5 Hz). The reference seeds the
choice from wall-clock ms plus the pair's first voice index, so every instance
differs. LILI-4 takes a seed in `EngineConfig` so renders can be reproduced.

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
cross-mod through. **LILI-4:** kept, since it's part of the sound.

**Quirk:** in Pd, `s~`/`r~` feedback lags by one 64-sample block whenever the
receiver runs before the sender in Pd's DSP sort order (0 samples otherwise),
so the FM paths between pairs, and from total feedback, lag 0 or 64 samples
depending on sort order.
**LILI-4:** these loops run with a 1-sample delay by default, which is closer
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
**LILI-4:** reproduce the curve exactly, and add a transparent safety
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

## LILI-4 structure (supersedes the 8-voice layout above)

LILI-4 keeps the reference's 8 oscillators in 4 FM pairs, but presents each pair as one **petal**:

- **One pad, gate, sensor envelope and thump per petal**, shared by both oscillators. The thump is carried by oscillator A only.
- **Tuning:**
  ```
  f_A = mtof(24 + 72 · x_tune) · pitchMul_G        (C1..C7)
  f_B = f_A · 2^(spread / 12),  spread = 12 · u³,  u = 2 · x_spread − 1
  ```
  Spread is fine detune near the centre, ±1 octave at the ends; the default x = 0.56 is about +2 cents. Quantize applies to both.
- **Partners:** 1 ↔ 2 and 3 ↔ 4 with Switch off; 1 ↔ 4, 2 ↔ 1, 3 ↔ 2, 4 ↔ 3 with Switch on (the reference's pair matrix).
- **MIDI:** notes 36, 38, 40, 41 (C1 D1 E1 F1) gate petals 1–4.
- **Groups** (1·2, 3·4) keep Pitch, Hold, Engine and Table.

Everything else in this document applies per petal where it said per pair.

## Garden engine (not in the reference)

Everything here is opt-in. With Engine = Classic, Bloom = 0, BEE off and Stereo off, the
engine is bit-identical to the reference port above (tested).

**Petal engine per group** (`engine12`, `engine34`): Classic / Wave / Seed.
Pitch, FM (`f_osc = f · (1 + modSig)`), vibrato, the sensor envelope, the thump
and Hold are shared by all three. Only the waveform source changes, and the
per-pair **Sharp** becomes "timbre":

- **Classic:** `sq · s − tri · (1 − s)` as above.
- **Wave:** `w = 0.7 · WT(family, morph = √s, phase)`, then a body shelf: `out = w + (√17 − 1) · LP1(w, 100 Hz)`.
  - The body gives Wave the Classic triangle's low-register weight. The triangle is the pulse through a one-pole low-pass at `max(f/4, 100 Hz)`, so below about C4 the 100 Hz floor lets up to √17 (+12.3 dB) more fundamental through than higher up. The shelf has the same corner and the same √17 DC gain, and is transparent from about C5 up. Per petal, Wave measures within about 4 dB of Classic at Timbre 0 from C1 to C5 (tested).
  - `WavetableBank` has 4 procedural families, 8 frames × 2048 samples each, every frame RMS-normalised to a unit sine. With `t` the frame's morph (0..1) and `n` the harmonic:
    - **Stem:** `1/n`, even harmonics fading out with `t` (saw to hollow square).
    - **Reed:** a formant centred on harmonic `2 + 30t`, width `w = 1.5 + 4t`, peak `0.5 · √(1.5/w)`, over a body of `1/n^1.5`.
    - **Glass:** the fundamental, the octave and the primes, `1/n^(1.6 − 0.9t)`.
    - **Moss:** the fundamental at 1, overtones `2r²/n^0.85` with `r` seeded per frame and harmonic. Phases are seeded per harmonic and shared by every frame, so morphing never cancels a partial.
  - Every frame, and every crossfade between frames or families, keeps its fundamental at 0.55 or more of a unit sine (tested).
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

## Stereo (not in the reference)

The Lyra-8 is a mono instrument: its main output is mono (the headphone jack
carries the same signal to both ears), and its MOD DELAY is two lines run in
parallel. LIRA•8 likewise wires one `master_output` to both `dac~` channels.
`stereo` (default on) spreads LILI-4 across L and R without changing what it is.
Every part of the image is built as **mid ± side**, so `(L + R) / 2` is the mono
signal up to the drive's saturation, and every side signal passes a 4th-order
Linkwitz–Riley high-pass `HPLR(·)` at 150 Hz (two Butterworth sections:
−15 dB at 100 Hz, −29 dB at 65 Hz) so the bass stays centred.

With `w` the width (0..1):

```
pan_v   = −0.65 −0.35 | −0.35 −0.05 | +0.35 +0.05 | +0.65 +0.35   (osc A, B of petals 1 | 2 | 3 | 4)
side    = w · 0.16 · HPLR(Σ_v pan_v · voice_v)          (voice_v = the oscillator's (shaped + thump) · gain)
in_L,R  = in ± side
write_1 = in_L + 0.001 · noise + fb · loop_1           (line 1 is fed from the left,
write_2 = in_R + 0.001 · noise + fb · loop_2            line 2 from the right)
wet     = tanhP((loop_1 + loop_2) / max(fb, 1.5))      (the reference's wet, unchanged)
wetS    = w · tanhP(0.6 · HPLR(loop_2 − loop_1) / max(fb, 1.5))
wet_L,R = wet ± wetS                                   (the echoes return crosswise: line 2 left, line 1 right)
delayed_L,R = 0.7 · in_L,R · (1 − mix) + mix · wet_L,R
mixed_L,R   = the drive/distortion section, run per channel (its own 20 Hz and 10 Hz high-passes)
out_L,R     = mixed_L,R · vol
totalFeedback = HP1(tanhP((mixed_L + mixed_R) / 2), 3 Hz)   (stays one mono FM source)
```

- **Petals** sit where they are on the board: group 1·2 left and 3·4 right, at
  −0.5, −0.2, +0.2 and +0.5. A petal's two oscillators are 0.3 apart (A outside,
  B inside), so the slow beating of a detuned pair drifts across the image, and
  Bloom's per-oscillator breathing moves in space. Pans are linear
  (`L = 1 + pan`, `R = 1 − pan`), so nothing is lost in a fold-down, and nothing
  is hard-panned: the widest oscillator is 13.5 dB down on the far side.
- **ECHO** is the natural L/R pair. Each line hears its own side of the
  petals and returns on the other, so a sound on the left echoes on the right.
  The cross-feed has its own saturator, outside the reference's, because inside
  it the cross-feed's highs intermodulate with the bass of the mid and leak low
  end into the sides. 0.6 puts each line 12 dB lower on its own side.
- **DRIVE** runs per channel, like a stereo pair of the same circuit. When it
  saturates, the image narrows with it.
- **The LFOs and the Pollinator** aren't routed to panning. They already move
  the image through the two echo lines (Mod 1 and Mod 2 set separate depths),
  and the instrument's motion stays where the reference puts it.
- **Mono is exact.** With `w = 0` the engine runs the reference section above,
  bit for bit, with `L = R`. `process(left, nullptr)` (a mono output) always
  takes that path. Checked against renders of the engine from before stereo
  existed (`lili_render … stereo=0 channels=2`), and in `lili_tests`.
- **Switching** ramps `w` linearly over 40 ms, so MONO/STEREO OUT never clicks.
  Entering stereo starts the right channel's drive filters from the left
  channel's state.
- **Measured** (6–14 s renders of default, Wave Stem, a heavy echo, FM with the
  Pollinator, and Bloom with full drive): `(L + R) / 2` against Stereo off is
  within 0.2 dB below 150 Hz and within 0.3 dB overall, and the mean power of L
  and R is within 0.25 dB of mono. Side/mid is about −17 dB on the default patch,
  where most of the energy is in centred fundamentals, and −11 dB with echo.
  Below 80 Hz it is about −27 dB with echo, and is tested to stay under −20 dB.
- **State:** a saved state with no `stereo` loads with the default, Stereo on.
