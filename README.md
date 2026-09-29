# LILI-8

A native (C++/JUCE) rewrite of [LIRA•8](https://github.com/MikeMorenoDSP/LIRA-8),
Mike Moreno's Pure Data emulation of the SOMA Laboratory Lyra-8 "organismic"
drone synth. It builds as a universal (Apple Silicon + Intel) AU, VST3 and
standalone app.

Not affiliated with SOMA Laboratory or Mike Moreno DSP.

## Status

- Full signal path ported: 8 voices in 4 FM pairs, sensor envelopes with the
  touch "thump", hold, vibrato, the cross-mod source matrix, Hyper LFO, dual mod
  delay (including the reference's odd compressor/expander), drive and
  distortion, total feedback. See [`docs/SPEC.md`](docs/SPEC.md).
- **Rendered circuit-board UI.** A vintage green PCB whose signal flow is drawn as a lily: the petals are the 8 voices, the stem is the mix → delay → drive path, and the leaves are the two LFOs. It's rendered in Blender and composited natively. Cream knobs and bat toggles are drawn from sprite strips at the live parameter values. Petals, pads, stem, delay lines, cross-mod arcs, the LFO LEDs and per-pair LED meters glow with the actual audio. See [`docs/ART.md`](docs/ART.md).
- Validated with `auval` and pluginval (strictness 10, including its GUI tests).

## Playing it

MIDI notes **C1–G1** (36–43) are sensors 1–8. **Hold 1234/5678** makes a group
drone continuously. On the board:

- **Knobs:** drag up/down (Shift for fine), double-click to reset, scroll wheel.
- **Toggles:** click to throw; on 3-way toggles, click above or below the pivot.
- **Touch pads (S1–S8):** click to latch a voice on (the **Sensor N** parameters).
- **Hover** any control for its value in real units (Hz, ms, ×, %).

Every control is a host parameter, so Live's MIDI Map and automation work on all of them.

## Building

Requires CMake ≥ 3.25 and Xcode (macOS). CMake fetches JUCE 9.0.3 at
configure time; pass `-DLILI_JUCE_DIR=/path/to/JUCE` to use a local checkout.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# install for Live / other hosts
ditto build/plugin/LILI8_artefacts/Release/AU/LILI-8.component ~/Library/Audio/Plug-Ins/Components/LILI-8.component
ditto build/plugin/LILI8_artefacts/Release/VST3/LILI-8.vst3   ~/Library/Audio/Plug-Ins/VST3/LILI-8.vst3
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
./build/tests/lili_render out.wav 10 gates=1357 delMix=0.6 source12=0 mod12=0.7
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
