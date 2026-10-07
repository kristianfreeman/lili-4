"""Stack sprite frames into one vertical strip and register it in the layer manifest.

    python3 tools/art/assemble_strip.py build/art/knob   build/art/layers --name knobStrip
    python3 tools/art/assemble_strip.py build/art/toggle build/art/layers --name toggleStrip
"""

import glob
import json
import os
import sys

import numpy as np
from PIL import Image

FEATHER = 0.12  # fraction of the frame over which alpha fades to 0 at each edge


def feather(img):
    """Fade alpha to zero toward the frame edges (smoothstep), so shadows never end in a seam."""
    px = np.asarray(img.convert("RGBA")).astype(np.float64)
    h, w = px.shape[:2]

    def ramp(n):
        d = np.minimum(np.arange(n), np.arange(n)[::-1]) / (FEATHER * n)
        d = np.clip(d, 0.0, 1.0)
        return d * d * (3 - 2 * d)

    px[..., 3] *= np.outer(ramp(h), ramp(w))
    return Image.fromarray(np.clip(px + 0.5, 0, 255).astype(np.uint8))  # HxWx4 uint8 -> RGBA

DESCRIPTIONS = {
    "knobStrip": {
        "value": "frame = round(value * (frames - 1)); value 0 = 7 o'clock, 1 = 5 o'clock",
        "anchor": "sprite centre = knob centre, board px (x, y) for board.json knobs, * scale",
    },
    "toggleStrip": {
        "value": "frames 0/1/2 = up/centre/down. 3-way: option index = frame; 2-way: option 0 = up (0), "
                 "option 1 = down (2); DIP-style toggles: on = up, off = down",
        "anchor": "sprite centre = toggle pivot, board px (x, y) for board.json jumpers and dips, * scale",
    },
}


def main():
    frames_dir, layers_dir = sys.argv[1], sys.argv[2]
    name = sys.argv[sys.argv.index("--name") + 1] if "--name" in sys.argv else "knobStrip"
    file = {"knobStrip": "knob_strip.png", "toggleStrip": "toggle_strip.png"}[name]
    files = sorted(glob.glob(os.path.join(frames_dir, "frame_*.png")))
    # Note: Pillow reads these 16-bit PNGs as 8-bit; the strip is 8-bit sRGB-encoded linear.
    first = Image.open(files[0])
    w, h = first.size
    strip = Image.new("RGBA", (w, h * len(files)))
    for i, f in enumerate(files):
        strip.paste(feather(Image.open(f)), (0, i * h))
    strip.save(os.path.join(layers_dir, file))

    path = os.path.join(layers_dir, "manifest.json")
    manifest = json.load(open(path))
    manifest[name] = {
        "file": file,
        "frames": len(files),
        "frameSize": [w, h],
        "alpha": "straight",
        **DESCRIPTIONS[name],
        "composite": "linear: base = sprite * a + base * (1 - a), before glow layers are added",
    }
    json.dump(manifest, open(path, "w"), indent=1)
    print(f"{name}: {w}x{h * len(files)} ({len(files)} frames) -> {layers_dir}/{file}")


main()
