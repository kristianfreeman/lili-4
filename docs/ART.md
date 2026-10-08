# Art pipeline

The UI is pre-rendered in Blender (Cycles), then composited natively by the plugin and driven by engine telemetry.

## Files

| File | Role |
|------|------|
| `art/board.json` | The layout: every path, part and control centre in board px (1000×424, which are also the editor's points), plus the type levels. The renderer and the plugin editor both read it, so hit areas match the render exactly. |
| `art/fonts/` | IBM Plex Mono, the board's only face (silkscreen and logo), and Instrument Serif (not used by the board). Both SIL OFL 1.1. |
| `tools/art/render_board.py` | Builds the board in Blender from the layout and renders it, or bakes the runtime layers. |
| `tools/art/parts.py` | Control models: knobs, toggles, touch pads. |
| `tools/art/render_sprites.py` | Renders one control in each of its states, for a sprite strip. |
| `tools/art/bloom_layers.py` | Adds the soft halo of light under the mask to each glow layer. |
| `tools/art/crop_layers.py` | Crops each glow layer to where its light lands. |
| `tools/art/assemble_strip.py` | Stacks sprite frames into a strip and feathers their edges. |
| `tools/art/composite.py` | Reference compositor, for previewing any state offline. |
| `tools/art/export_ui.py` | Exports the display-ready assets in `plugin/assets/`. |

## The layout

The board is a compact horizontal panel in three bands: four petal sections either side of GARDEN, then GROUP 1·2, LFO and GROUP 3·4, then ECHO, the output jack and OUTPUT. Knobs and toggles are 88% of their original size (`knobScale`).

The silkscreen is set entirely in IBM Plex Mono, at three levels (`type` in the JSON):
- section titles, 9 px, reversed out of solid silkscreen tabs on square frames;
- control labels, 8.5 px, centred below every control on the baseline `y + labelOffset`;
- legends, 8 px at 74% ink: toggle positions (right of the throw, with tick marks), bracket names and small print.

Baselines sit on a 4 px grid. Words that repeat are folded into brackets (TIME, MOD, DIST, GLOBAL). The logo is a solid silkscreen block with LILI-4 knocked out of the ink, so the mask shows through the letters. Reversed-out text is a real boolean cut in the ink.

The flower is copper under the mask, green on green: a small lily in GARDEN around U1, with the stem running down through LFO to U2 and the jack, and an echo meander on either side of the jack. `padRuns` connect each touch pad to its petal. Every route is right-angled and runs in a channel between sections (the side margins, the gaps between the bands, the column gaps either side of GARDEN and LFO), never under a control. The cross-mod and total-feedback routes are on the bottom layer.

The top-right "MONO OUT" / "STEREO OUT" silkscreen is a control (`outMode`): clicking it toggles the `stereo` parameter.

**Surface texture.** Every material carries a little procedural texture, kept quiet so the silkscreen stays legible: mottling, a faint fibreglass weave and fine grain in the solder mask (in world space, so separate parts read as one board), uneven tone and the odd pinhole in the silkscreen ink, a brushed grain with some tarnish on the gold pads, matte grain on the knob plastic, and brushed metal on the toggles and jack nut.

## Rebuilding the assets

```sh
blender -b -P tools/art/render_board.py -- --bake --no-controls --out build/art/layers
python3 tools/art/bloom_layers.py build/art/layers
blender -b -P tools/art/render_sprites.py -- --part knob
blender -b -P tools/art/render_sprites.py -- --part toggle
python3 tools/art/crop_layers.py build/art/layers --threshold 0.02
python3 tools/art/assemble_strip.py build/art/knob build/art/layers --name knobStrip
python3 tools/art/assemble_strip.py build/art/toggle build/art/layers --name toggleStrip
python3 tools/art/export_ui.py build/art/layers plugin/assets
```

To preview a state offline:

```sh
python3 tools/art/composite.py build/art/layers out.png petal0=1 mix=0.8 knob:tune1=0.2 toggle:bee=1 outmode=0
```

A 2000×848 bake, with the two output-mode patches, takes about 75 s on an M-series Mac (Metal).

## How it works

**Layer bake.** One render uses Cycles light groups: every element that can light up gets its own group, and the studio lights sit in `base`. The layers are:
- the resting board;
- one glow layer each for the petals (with their touch pads and pad runs), mix, echo lines, cross-mod routes, total feedback, LFO LEDs, stamens and each meter LED, including the light each spills onto the board.

`bloom_layers.py` then adds to each layer a soft halo of its own light: a sum of Gaussians, wider for the LEDs, masked to where the base shows bare mask. Light bleeds sideways under the mask, but full-strength silkscreen, gold and parts stay unlit, so labels remain legible. In the editor, knobs and toggles are drawn over the glow, so they cover it too.

Layers are linear light, stored as sRGB-encoded 16-bit PNGs rendered at −2 EV for headroom, and composited as:

```
linear = decode(base) + Σ level_i · decode(layer_i)
out    = sRGB(AgX(bloom(linear)))
```

The AgX look (power 1.40, saturation 1.05) is fitted to Blender's own render of the same scene.

**Controls.** Each control type is rendered alone on a shadow catcher, so every frame carries the part plus its real soft shadow as alpha, under the board's lighting. Knobs have 64 frames (7 o'clock to 5 o'clock) and toggles have 3. One strip serves every instance. The board itself is baked without controls, and the editor draws each knob and toggle from its strip at the live parameter value.

**Output mode.** The bake leaves the MONO OUT / STEREO OUT text off the board, then re-renders a region around it once per state. `export_ui.py` crops the `outMode.patch` rectangle from each, feathers its edge and stacks them into `outmode_strip.png`. The editor draws frame 0 (mono) or 1 (stereo) over the board.

**Glow in the plugin.** `export_ui.py` turns each glow layer into a display-space delta, `AgX(base + layer) − AgX(base)`. The editor keeps only the pixel runs that actually light up. Each frame it restores last frame's runs from the clean board, adds `level × delta` for every lit layer, and repaints only the changed areas. Layer levels follow the telemetry:

| Layer | Driven by |
|-------|-----------|
| Petals | voice gain |
| Mix | summed petal peaks |
| Echo lines | delay peaks |
| Cross-mod arcs | FM depth, when a petal takes its partner as source |
| Total FB | its loop level, while the switch is on |
| LFO LEDs | the LFO square wave |
| Stamens | output peak |

Glow releases over 150 ms.

## Snapshots

The editor has environment hooks for rendering documentation images from the standalone app:

| Variable | Effect |
|----------|--------|
| `LILI_SNAPSHOT=/path/out.png` | Renders the editor offscreen about 1 s after it opens, saves it as a 2x PNG and quits the standalone app. The window stays invisible. |
| `LILI_SNAPSHOT_SENSORS=13` | Latches petals 1 and 3, and mutes the output. |
| `LILI_SNAPSHOT_PARAMS="hold12=1,bloom=0.5"` | Sets normalised parameter values. |
| `LILI_SNAPSHOT_READOUT=tune1` | Shows that control's hover readout. |
| `LILI_SNAPSHOT_SEED=/path.wav` | Loads a Seed sample into group 3·4. |

## Promo renders

`tools/art/render_promo.py` renders marketing shots of the same board. It builds the scene with `render_board.build()` in the lit state, swaps in real-proportion knobs and toggles, and adds a perspective camera and studio lights. Output goes to `promo/`, which is gitignored.

```sh
blender -b -P tools/art/render_promo.py -- --shot preview-8 --samples 24
python3 tools/art/promo_post.py promo/preview-8.png promo/preview-8-post.png
```

`--shot` takes a comma-separated list: `hero`, `exploded`, `knobs`, `knobs-a`…`knobs-c` or `preview-1`…`preview-8`. Use low samples (16–24) to iterate on an angle, and 128 or more for a final render. `promo_post.py` adds bloom, a filmic grade, a vignette, slight chromatic aberration and grain; its constants are at the top of the file. The `preview-*` shots are framed for the compact board. The older shots still use cameras framed for the previous 1120×800 board.
