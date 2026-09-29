"""Crop baked glow layers to where their light actually lands.

    python3 tools/art/crop_layers.py build/art/layers [--threshold 0.003] [--pad 12]

Each layer's PNG is replaced by its cropped region and the manifest gains
"rect": [x, y, w, h] in layer pixels (the base stays full-frame). Pixels whose
decoded linear light is below the threshold on every channel count as black;
at that level the contribution is invisible after tone mapping.
"""

import json
import os
import sys

import numpy as np
from PIL import Image


def main():
    layers_dir = sys.argv[1]
    threshold = 0.003
    pad = 12
    args = sys.argv[2:]
    for i, a in enumerate(args):
        if a == "--threshold":
            threshold = float(args[i + 1])
        if a == "--pad":
            pad = int(args[i + 1])

    path = os.path.join(layers_dir, "manifest.json")
    manifest = json.load(open(path))
    gain = 2.0 ** -manifest["encoding"]["exposureEV"]
    before = after = 0
    for layer in manifest["layers"]:
        file = os.path.join(layers_dir, layer["file"])
        before += os.path.getsize(file)
        img = Image.open(file)
        px = np.asarray(img).astype(np.float64)
        scale = 65535.0 if px.max() > 255 else 255.0
        c = px[..., :3] / scale
        lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4) * gain
        ys, xs = np.nonzero(lin.max(axis=2) > threshold)
        if len(xs) == 0:
            x0 = y0 = 0
            x1 = y1 = 1
        else:
            x0 = max(int(xs.min()) - pad, 0)
            y0 = max(int(ys.min()) - pad, 0)
            x1 = min(int(xs.max()) + pad + 1, img.width)
            y1 = min(int(ys.max()) + pad + 1, img.height)
        if "rect" in layer:  # already cropped: rect is relative to the original frame
            ox, oy = layer["rect"][0], layer["rect"][1]
        else:
            ox, oy = 0, 0
        img.crop((x0, y0, x1, y1)).save(file)
        layer["rect"] = [ox + x0, oy + y0, x1 - x0, y1 - y0]
        after += os.path.getsize(file)
    json.dump(manifest, open(path, "w"), indent=1)
    print(f"cropped {len(manifest['layers'])} layers: {before / 1e6:.1f} MB -> {after / 1e6:.1f} MB")


main()
