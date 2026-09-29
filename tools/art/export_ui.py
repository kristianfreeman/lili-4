"""Export display-ready (8-bit sRGB, tone-mapped) UI assets for the plugin.

    python3 tools/art/export_ui.py build/art/layers plugin/assets

The first native editor draws with JUCE's software renderer, which loads PNGs
as 8-bit, so this bakes the AgX look in: board.png (base, controls removed)
plus knob_strip.png and toggle_strip.png (straight alpha). The linear 16-bit
layers stay the source for the later GPU compositor with glow.
"""

import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from composite import agx, linear_to_srgb, load, srgb_to_linear  # noqa: E402


def to8(x):
    return (np.clip(x, 0, 1) * 255 + 0.5).astype(np.uint8)


def main():
    layers_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    manifest = json.load(open(os.path.join(layers_dir, "manifest.json")))
    gain = 2.0 ** -manifest["encoding"]["exposureEV"]

    base = linear_to_srgb(agx(load(os.path.join(layers_dir, manifest["base"]), gain)))
    Image.fromarray(to8(base)).save(os.path.join(out_dir, "board.png"), optimize=True)

    for key in ("knobStrip", "toggleStrip"):
        info = manifest[key]
        raw = np.asarray(Image.open(os.path.join(layers_dir, info["file"]))).astype(np.float64)
        raw /= 65535.0 if raw.max() > 255 else 255.0  # Pillow reads 16-bit PNGs as 8-bit
        rgb = linear_to_srgb(agx(srgb_to_linear(raw[..., :3]) * gain))
        rgba = np.concatenate([to8(rgb), to8(raw[..., 3:4])], axis=2)
        Image.fromarray(rgba, "RGBA").save(os.path.join(out_dir, info["file"]), optimize=True)

    # Glow deltas: how much each element brightens the *displayed* board at full
    # level, AgX(base + layer) - AgX(base), 8-bit RGB cropped to the layer rect.
    # The editor adds level * delta (a display-space approximation of the
    # linear sum; the later GPU pass will do it in linear light). Meter LEDs are
    # drawn by the editor, so their layers are skipped.
    base_lin = load(os.path.join(layers_dir, manifest["base"]), gain)
    glow_dir = os.path.join(out_dir, "glow")
    os.makedirs(glow_dir, exist_ok=True)
    rects = {}
    for layer in manifest["layers"]:
        if layer["kind"] == "meter":
            continue
        x, y, w, h = layer["rect"]
        region = base_lin[y:y + h, x:x + w]
        lit = region + load(os.path.join(layers_dir, layer["file"]), gain)
        delta = linear_to_srgb(agx(lit)) - linear_to_srgb(agx(region))
        Image.fromarray(to8(np.clip(delta, 0, 1))).save(os.path.join(glow_dir, layer["name"] + ".png"), optimize=True)
        rects[layer["name"]] = [x, y, w, h]
    with open(os.path.join(glow_dir, "glow.json"), "w") as f:
        json.dump({"scale": manifest["scale"], "rects": rects}, f, indent=1)

    for f in sorted(os.listdir(out_dir)):
        path = os.path.join(out_dir, f)
        if os.path.isfile(path):
            print(f"{f}: {os.path.getsize(path) / 1e6:.2f} MB")
    total = sum(os.path.getsize(os.path.join(glow_dir, f)) for f in os.listdir(glow_dir))
    print(f"glow/: {len(rects)} deltas, {total / 1e6:.2f} MB")


main()
