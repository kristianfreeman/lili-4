# LILI-8 art direction and pipeline

The UI is pre-rendered artwork (Blender), composited natively at runtime and
driven by engine telemetry. There is no web view or JavaScript in the plugin.

## Pipeline

| File | Role |
|------|------|
| `art/board.json` | Layout: every path, part and control position in board px (1120×800). Also read by the future native editor for hit areas. |
| `tools/art/render_board.py` | Builds the board in Cycles from the layout and renders it. `--lit` previews the playing state. |
| `tools/art/parts.py` | Detailed control models (knobs, toggles, slide switch, tact button). |
| `tools/art/render_parts.py` | Comparison sheet of control styles (`--tilt`, `--scale`). |

```sh
blender -b -P tools/art/render_board.py -- --out build/art/board.png [--lit] [--samples 128] [--scale 2]
blender -b -P tools/art/render_parts.py -- --out build/art/parts.png [--tilt 28] [--scale 4]
blender -b -P tools/art/render_board.py -- --bake --out build/art/layers    # runtime layers
python3 tools/art/crop_layers.py build/art/layers --threshold 0.02          # crop to where light lands
python3 tools/art/composite.py build/art/layers out.png voice0=1 mix=0.8 delay0=0.5   # preview a state
```

### Layer bake (what the plugin composites)

`--bake` renders once with **Cycles light groups**: every animatable element gets its own light group, and the studio lights and world are in `base`. The single render writes:
- `base.png`: the resting board.
- 37 glow layers: `voice0..7` (petal, rib and touch pad), `mix`, `delay0/1`, `xmod0/1`, `totalfb`, `lfo0/1`, `stamens`, `meter{pair}_{led}`. Each layer includes the light it spills onto the board around it.
- `manifest.json`: the layer list, the encoding and the tone-map parameters.

Layers are **linear light**: sRGB-encoded 16-bit PNG, rendered at −2 EV for headroom. The runtime (and `composite.py`) does:

```
linear = decode(base) + Σ level_i · decode(layer_i)      # decode = srgb→linear × 4
out    = sRGB( AgX(bloom(linear)) )                     # AgX minimal approx + look (power 1.40, sat 1.05)
```

The first bake saved tone-mapped layers, and adding those washed the glow out to cream. Linear layers add physically. The AgX look parameters were fitted against Blender's "Medium High Contrast" render of the same scene (MSE 0.018 → 0.0008), so the composited plugin UI matches the style frames. It takes about 30 s for all 38 layers at 2240×1600.

**Knobs: one shared filmstrip.** `render_knob_strip.py` renders the cream knob alone at 64 positions (value 0 = 7 o'clock to 1 = 5 o'clock) on a Cycles **shadow catcher** over a transparent film. Each frame is the knob plus its real soft shadow as alpha, under the board's own studio lights, so highlights stay put while the knob turns. It takes about 23 s. `assemble_strip.py` stacks the frames into `knob_strip.png` (128×8192, same linear 16-bit encoding, straight alpha) and registers it in the manifest. The board is then baked with `--no-knobs`: the base keeps each knob's silkscreen scale and label, and the runtime draws every knob from the strip:

```
linear = base;  for each knob: linear = sprite·α + linear·(1−α)   # frame = round(value·63)
linear += Σ level_i · glow_i;  out = sRGB(AgX(bloom(linear)))
```

One strip serves all 31 knobs (the light direction barely changes across the board), about 4 MB as RGBA16F.

```sh
blender -b -P tools/art/render_board.py -- --bake --no-knobs --out build/art/layers
blender -b -P tools/art/render_knob_strip.py -- --out build/art/knob
python3 tools/art/crop_layers.py build/art/layers --threshold 0.02
python3 tools/art/assemble_strip.py build/art/knob build/art/layers
python3 tools/art/composite.py build/art/layers out.png voice0=1 knob:tune1=0.2
```

**Cropping.** `crop_layers.py` trims each glow layer to where its decoded light exceeds 0.02 (plus 12 px padding) and records `rect` in the manifest. That's 13% of the full-frame area, so GPU memory for the layers (RGBA16F) drops from about 1,061 MB to 141 MB. It's visually lossless: against the uncropped composite the maximum error is 5/255 on 9 pixels. Some voice layers stay wide on purpose: lit traces reflect in glossy parts (U1 can, chrome toggles) across the board.

A 2240×1600 render takes about 12 s on an M4 Max (Metal).

## Decisions

- **Vintage green board.** Mottled glossy green solder mask over raised copper, HASL (tinned) pads, off-white silkscreen.
- **Vintage cream knobs.** A fluted cap on a wide skirt, with a printed index and a silkscreen scale (11 ticks over 270°). Chosen over refined trimmers, fluted black and machined aluminium (see `render_parts.py`). The knob is about 1.5× the old trimmer's footprint, so it reads at plugin size.
- **Bat toggles replace jumpers and DIP switches.** 3-position toggles for Source (up = partner pair, centre = off, down = LFO/FB) and the delay Mod Source; 2-position toggles for Fast, Wave, LFO logic and the global switches. Option 0 is always "up". The lever leans 34° so its throw reads from straight above.
- **Touch pads are interdigitated gold (ENIG) combs.** They're two electrodes your finger bridges, the way the Lyra's touch plates work. Each comb has its spine on one half-ring and fingers at a 4 px pitch. They read as sensors, not as flat grey discs (`parts.touch_pad`, `"pad": "comb"`).
- **Type: IBM Plex Mono Medium** for silkscreen, **Instrument Serif** for the logo. Both are SIL OFL 1.1, vendored in `art/fonts/` with their licence texts, so they're safe to bake into shipped artwork.
- **Pair level meters.** Each pair module's second row has a 5-LED SMD meter (4 amber plus 1 pink "hot") between Source and Speed. It fills the dead space with something functional; at runtime it's driven by that pair's telemetry `pairPeak`. Emission uses deep hues, because AgX desaturates small bright emitters toward cream; the final tint can be adjusted in the compositor.
- **Top-down orthographic camera, kept after a test.** A 15° tilt (`render_board.py --tilt 15`) gives the knobs a little volume and the toggle levers read slightly better. But the gain is modest, and it foreshortens and shifts the board framing and makes hit-testing non-trivial. The larger cream knobs and 34° toggle levers already read from straight above.
- **Glow is amber, modest.** Emission above about 3 clips to white under AgX.

## Open items

- Toggle states (2 or 3 positions): same trick as the knobs, a small sprite per throw. Toggles are still baked at their style-frame positions.
- The native JUCE compositor: GPU sum of layers, bloom, AgX, driven by telemetry.

## Log

- **Style frame v1:** placeholder primitives, blue trimmers, DIPs, jumpers.
- **Parts sheet:** four knob styles and three switch styles compared.
- **Style frame v2:** cream knobs, bat toggles, headings and bottom row re-spaced so scales don't collide.
- **Style frame v3:** gold interdigitated touch pads.
- **Style frame v4:** OFL type (Plex Mono and Instrument Serif) replaces Apple system fonts.
- **Lit preview v5:** a sounding voice also lights its touch pad (the combs glow amber along with the petal), so the "touched" sensor reads at a glance. In the runtime bake this becomes one more lit layer per voice.
- **v6:** pair level meters (lit preview shows pairs 12, 34 and 56 playing).
- **Tilt test:** 15° vs top-down compared (`build/art/tilt_compare.png`); stayed top-down.
- **Layer bake:** light-group bake, linear encoding, reference compositor with fitted AgX look (`build/art/composite_play.png`).
