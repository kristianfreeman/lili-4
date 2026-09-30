# LILI-4

A four-petal "garden drone" synthesizer. It builds as a universal (Apple Silicon + Intel) AU, VST3 and standalone app.

It began as a native (C++/JUCE) rewrite of [LIRA•8](https://github.com/MikeMorenoDSP/LIRA-8), Mike Moreno's Pure Data emulation of the SOMA Laboratory Lyra-8. That faithful port is tagged `lyra-port-v1`. It has since grown into its own instrument. Not affiliated with SOMA Laboratory or Mike Moreno DSP.

## The instrument

- **Four petals, two oscillators each** (grouped 1·2 and 3·4). Each petal has:
  - **TUNE**: C1–C7.
  - **SPREAD**: its second oscillator's offset. Fine detune near the centre, up to ±1 octave at the ends.
  - **TIMBRE**, **MOD**, **SOURCE**: FM from the partner petal, or from the LFOs / total feedback.
  - **SPEED**: fast or slow swell.

  Cross-modulation between petals, the touch "thump", vibrato and total feedback carry the organismic character. See [`docs/SPEC.md`](docs/SPEC.md).
- **ENGINE** per group:
  - **Classic**: pulse/triangle.
  - **Wave**: four procedural wavetable families (Stem, Reed, Glass, Moss), scanned by **TABLE**.
  - **Seed**: a granular sampler; drop an audio file on the board.

  TIMBRE is the pulse shape, wavetable morph, or position in the sample.
- **BLOOM / DRIFT**: slow generative growth. Petals breathe awake without notes and drift in tune and timbre, over cycles from minutes to seconds.
- **BEE**: the Pollinator, a chaotic Lorenz modulator in place of the LFO pair. LFO A sets flight speed and LFO B sets chaos.
- **ECHO** (two modulated delay lines) into **DRIVE**.
- **Rendered circuit-board UI.** A vintage green PCB whose signal flow is drawn as a lily: four petals fan up from the centre on a long stem, through the echo lines and drive to the output jack, with the two LFOs as leaves. It's rendered in Blender and composited natively. Cream knobs and bat toggles are drawn at the live parameter values, and petals, pads, stem, echo lines, cross-mod arcs, LEDs and per-petal meters glow with the actual audio. See [`docs/ART.md`](docs/ART.md).
- Validated with `auval` and pluginval (strictness 10, including its GUI tests).

## Playing it

MIDI notes **C1, D1, E1, F1** (36, 38, 40, 41) play petals 1–4. **HOLD** makes a group drone continuously. On the board:

- **Knobs:** drag up/down (Shift for fine), double-click to reset, scroll wheel.
- **Toggles:** click to throw; on 3-way toggles, click above or below the pivot.
- **Petal pads (S1–S4):** click to latch a petal on (the **Petal N** parameters).
- **Hover** any control for its value in real units (Hz, cents/st, ms, %, wavetable family, loaded sample).
- **Drop an audio file** on the left or right half of the board to load a Seed sample for group 1·2 or 3·4. That group switches to Seed, and the file path is saved with your set.

Every control is a host parameter, so Live's MIDI Map and automation work on all of them.

## Building

Requires CMake ≥ 3.25 and Xcode (macOS). CMake fetches JUCE 9.0.3 at
configure time; pass `-DLILI_JUCE_DIR=/path/to/JUCE` to use a local checkout.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# install for Live / other hosts
ditto build/plugin/LILI4_artefacts/Release/AU/LILI-4.component ~/Library/Audio/Plug-Ins/Components/LILI-4.component
ditto build/plugin/LILI4_artefacts/Release/VST3/LILI-4.vst3   ~/Library/Audio/Plug-Ins/VST3/LILI-4.vst3
```

Or configure with `-DLILI_COPY_PLUGIN=ON` to install after every build.
Engine-only builds (no JUCE download) use `-DLILI_BUILD_PLUGIN=OFF`.

## Layout

| Path                 | What                                                     |
|----------------------|----------------------------------------------------------|
| `dsp/`               | Framework-free engine (`lili::Engine`), the source of truth |
| `plugin/`            | Thin JUCE wrapper: parameters, MIDI, state               |
| `tests/`             | `lili_tests`, `lili_render` (offline WAV), `lili_bench`  |
| `docs/SPEC.md`       | The reference patch transcribed to math, with quirks     |
| `reference/lira-8-pd`| Vendored upstream Pd patch (BSD), used as the reference  |
| `tools/pdview.py`    | Dumps a Pd patch as readable objects + connections       |
| `tools/lint.sh`      | clang-format check + clang-tidy                          |
| `art/`               | Board layout (`board.json`) and vendored OFL fonts         |
| `tools/art/`         | Blender render, layer bake, sprite strips, reference compositor, UI export |
| `plugin/assets/`     | Exported UI art the plugin embeds (board, sprite strips, glow deltas) |

```sh
./build/tests/lili_render out.wav 10 gates=13 delMix=0.6 source1=0 mod1=0.7
python3 tools/pdview.py reference/lira-8-pd/abs/lira.voice.pd
```

## Lint and CI

`tools/lint.sh <build-dir>` runs clang-format (check) and clang-tidy over
`dsp/`, `plugin/` and `tests/` (CI pins LLVM 19 from PyPI). GitHub Actions
(`.github/workflows/ci.yml`) runs:

- lint on Linux
- engine tests on Linux, macOS and Windows
- a universal macOS plugin build with `auval` and pluginval, uploading the
  AU/VST3/app as artifacts

## Performance

On an M-series Mac at 48 kHz, the engine uses about 0.55 % of one core with
all eight voices, FM and both delays running (`lili_bench`). The oscillator
bank is SIMD-vectorised, and the per-sample `exp`/`cos` calls are replaced by
exact or ~1e-6-accurate fast paths. Renders match the pre-optimisation engine
to within 3e-6.

## License

BSD 3-Clause, see [`LICENSE`](LICENSE). Contains a port of LIRA•8
(© Miguel Moreno, BSD); its notice is kept in `LICENSE` and in
`reference/lira-8-pd/LICENSE.txt`.
