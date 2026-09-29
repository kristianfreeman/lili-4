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
```

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

- The layer bake (base plus a lit layer per animated element, knob rotor frames) and the native compositor.

## Log

- **Style frame v1:** placeholder primitives, blue trimmers, DIPs, jumpers.
- **Parts sheet:** four knob styles and three switch styles compared.
- **Style frame v2:** cream knobs, bat toggles, headings and bottom row re-spaced so scales don't collide.
- **Style frame v3:** gold interdigitated touch pads.
- **Style frame v4:** OFL type (Plex Mono and Instrument Serif) replaces Apple system fonts.
- **Lit preview v5:** a sounding voice also lights its touch pad (the combs glow amber along with the petal), so the "touched" sensor reads at a glance. In the runtime bake this becomes one more lit layer per voice.
- **v6:** pair level meters (lit preview shows pairs 12, 34 and 56 playing).
- **Tilt test:** 15° vs top-down compared (`build/art/tilt_compare.png`); stayed top-down.
