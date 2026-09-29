"""Stack knob frames into one vertical filmstrip and register it in the layer manifest.

    python3 tools/art/assemble_strip.py build/art/knob build/art/layers
"""

import glob
import json
import os
import sys

from PIL import Image


def main():
    frames_dir, layers_dir = sys.argv[1], sys.argv[2]
    files = sorted(glob.glob(os.path.join(frames_dir, "frame_*.png")))
    first = Image.open(files[0])
    w, h = first.size
    strip = Image.new(first.mode, (w, h * len(files)))
    for i, f in enumerate(files):
        strip.paste(Image.open(f), (0, i * h))
    strip.save(os.path.join(layers_dir, "knob_strip.png"))

    path = os.path.join(layers_dir, "manifest.json")
    manifest = json.load(open(path))
    manifest["knobStrip"] = {
        "file": "knob_strip.png",
        "frames": len(files),
        "frameSize": [w, h],
        "alpha": "straight",
        "value": "frame = round(value * (frames - 1)); value 0 = 7 o'clock, 1 = 5 o'clock",
        "anchor": "sprite centre = knob centre (board px (x + 26, y + 17) for board.json knobs) * scale",
        "composite": "linear: base = sprite * a + base * (1 - a), before glow layers are added",
    }
    json.dump(manifest, open(path, "w"), indent=1)
    print(f"strip {w}x{h * len(files)} ({len(files)} frames) -> {layers_dir}/knob_strip.png")


main()
