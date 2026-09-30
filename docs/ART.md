# Art pipeline

The UI is pre-rendered in Blender (Cycles), then composited natively by the plugin and driven by engine telemetry.

## Files

| File | Role |
|------|------|
| `art/board.json` | The layout: every path, part and control position in board px (1120×800). The renderer and the plugin editor both read it, so hit areas match the render exactly. |
| `art/fonts/` | IBM Plex Mono (silkscreen) and Instrument Serif (logo), both SIL OFL 1.1. |
| `tools/art/render_board.py` | Builds the board in Blender from the layout and renders it, or bakes the runtime layers. |
| `tools/art/parts.py` | Control models: knobs, toggles, touch pads. |
| `tools/art/render_sprites.py` | Renders one control in each of its states, for a sprite strip. |
| `tools/art/crop_layers.py` | Crops each glow layer to where its light lands. |
| `tools/art/assemble_strip.py` | Stacks sprite frames into a strip and feathers their edges. |
| `tools/art/composite.py` | Reference compositor, for previewing any state offline. |
| `tools/art/export_ui.py` | Exports the display-ready assets in `plugin/assets/`. |

## Rebuilding the assets

```sh
blender -b -P tools/art/render_board.py -- --bake --no-controls --out build/art/layers
blender -b -P tools/art/render_sprites.py -- --part knob
blender -b -P tools/art/render_sprites.py -- --part toggle
python3 tools/art/crop_layers.py build/art/layers --threshold 0.02
python3 tools/art/assemble_strip.py build/art/knob build/art/layers --name knobStrip
python3 tools/art/assemble_strip.py build/art/toggle build/art/layers --name toggleStrip
python3 tools/art/export_ui.py build/art/layers plugin/assets
```

To preview a state offline:

```sh
python3 tools/art/composite.py build/art/layers out.png petal0=1 mix=0.8 knob:tune1=0.2
```

A 2240×1600 bake takes about 30 s on an M-series Mac (Metal).

## How it works

**Layer bake.** One render uses Cycles light groups: every element that can light up gets its own group, and the studio lights sit in `base`. The layers are:
- the resting board;
- one glow layer each for the petals, mix, echo lines, cross-mod arcs, total feedback, LFO LEDs, stamens and each meter LED, including the light each spills onto the board.

Layers are linear light, stored as sRGB-encoded 16-bit PNGs rendered at −2 EV for headroom, and composited as:

```
linear = decode(base) + Σ level_i · decode(layer_i)
out    = sRGB(AgX(bloom(linear)))
```

The AgX look (power 1.40, saturation 1.05) is fitted to Blender's own render of the same scene.

**Controls.** Each control type is rendered alone on a shadow catcher, so every frame carries the part plus its real soft shadow as alpha, under the board's lighting. Knobs have 64 frames (7 o'clock to 5 o'clock) and toggles have 3. One strip serves every instance. The board itself is baked without controls, and the editor draws each knob and toggle from its strip at the live parameter value.

**Glow in the plugin.** `export_ui.py` turns each glow layer into a display-space delta, `AgX(base + layer) − AgX(base)`. The editor keeps only the pixel runs that actually light up. Each frame it restores last frame's runs from the clean board, adds `level × delta` for every lit layer, and repaints only the changed areas. Layer levels follow the telemetry:

| Layer | Driven by |
|-------|-----------|
| Petals | voice gain |
| Mix | summed petal peaks |
| Echo lines | delay peaks |
| Cross-mod arcs | FM depth, when a petal takes its partner as source |
| Total FB | its loop level, while the switch is on |
| Leaf LEDs | the LFO square wave |
| Stamens | output peak |

Glow releases over 150 ms.

## Snapshots

The editor has environment hooks for rendering documentation images from the standalone app:

| Variable | Effect |
|----------|--------|
| `LILI_SNAPSHOT=/path/out.png` | Saves the editor as a PNG about 1 s after it opens. |
| `LILI_SNAPSHOT_SENSORS=13` | Latches petals 1 and 3, and mutes the output. |
| `LILI_SNAPSHOT_PARAMS="hold12=1,bloom=0.5"` | Sets normalised parameter values. |
| `LILI_SNAPSHOT_READOUT=tune1` | Shows that control's hover readout. |
| `LILI_SNAPSHOT_SEED=/path.wav` | Loads a Seed sample into group 3·4. |
