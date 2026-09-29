"""Stack sprite frames into one vertical strip and register it in the layer manifest.

    python3 tools/art/assemble_strip.py build/art/knob   build/art/layers --name knobStrip
    python3 tools/art/assemble_strip.py build/art/toggle build/art/layers --name toggleStrip
"""

import glob
import json
import os
import sys

from PIL import Image

DESCRIPTIONS = {
    "knobStrip": {
        "value": "frame = round(value * (frames - 1)); value 0 = 7 o'clock, 1 = 5 o'clock",
        "anchor": "sprite centre = knob centre, board px (x + 26, y + 17) for board.json knobs, * scale",
    },
    "toggleStrip": {
        "value": "frames 0/1/2 = up/centre/down. 3-way: option index = frame; 2-way: option 0 = up (0), "
                 "option 1 = down (2); DIP-style toggles: on = up, off = down",
        "anchor": "sprite centre = toggle pivot, board px (x + 16, y + 38) for jumpers and "
                  "(x + 16 + 38 * i, y + 38) for dips, * scale",
    },
}


def main():
    frames_dir, layers_dir = sys.argv[1], sys.argv[2]
    name = sys.argv[sys.argv.index("--name") + 1] if "--name" in sys.argv else "knobStrip"
    file = {"knobStrip": "knob_strip.png", "toggleStrip": "toggle_strip.png"}[name]
    files = sorted(glob.glob(os.path.join(frames_dir, "frame_*.png")))
    first = Image.open(files[0])
    w, h = first.size
    strip = Image.new(first.mode, (w, h * len(files)))
    for i, f in enumerate(files):
        strip.paste(Image.open(f), (0, i * h))
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
