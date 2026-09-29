"""Reference compositor for baked layers (mirrors what the plugin's shader will do).

    python3 tools/art/composite.py build/art/layers out.png voice0=1 voice5=1 voice2=0.5 mix=0.8 ...

Decodes each 16-bit sRGB layer to linear light, undoes the bake exposure,
sums base + level * layer, tone-maps with an AgX approximation, encodes sRGB.
"""

import json
import os
import sys

import numpy as np
from PIL import Image

# AgX approximation (Benjamin Wrensch, "Minimal AgX implementation", MIT licence).
AGX_IN = np.array([[0.842479062253094, 0.0423282422610123, 0.0423756549057051],
                   [0.0784335999999992, 0.878468636469772, 0.0784336],
                   [0.0792237451477643, 0.0791661274605434, 0.879142973793104]])
AGX_OUT = np.array([[1.19687900512017, -0.0528968517574562, -0.0529716355144438],
                    [-0.0980208811401368, 1.15190312990417, -0.0980434501171241],
                    [-0.0990297440797205, -0.0989611768448433, 1.15107367264116]])
MIN_EV, MAX_EV = -12.47393, 4.026069
# ASC-CDL "look" fitted to Blender's "AgX - Medium High Contrast" on our base
# render (MSE 0.018 -> 0.0008 against style_frame_v4.png).
LOOK_POWER, LOOK_SAT = 1.40, 1.05
LUMA = np.array([0.2126, 0.7152, 0.0722])


def agx(linear, power=LOOK_POWER, sat=LOOK_SAT):
    v = linear @ AGX_IN  # rows of the matrices above are GLSL columns
    v = np.clip(np.log2(np.maximum(v, 1e-10)), MIN_EV, MAX_EV)
    x = (v - MIN_EV) / (MAX_EV - MIN_EV)
    x2 = x * x
    x4 = x2 * x2
    x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232
    luma = (x @ LUMA)[..., None]
    x = np.maximum(x, 0.0) ** power
    x = luma + sat * (x - luma)
    x = x @ AGX_OUT
    return np.clip(x, 0, 1) ** 2.2  # back to linear display light


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def load(path, gain):
    im = np.asarray(Image.open(path)).astype(np.float64)
    im = im[..., :3] / 65535.0 if im.max() > 255 else im[..., :3] / 255.0
    return srgb_to_linear(im) * gain


def main():
    layers_dir, out = sys.argv[1], sys.argv[2]
    manifest = json.load(open(os.path.join(layers_dir, "manifest.json")))
    gain = 2.0 ** -manifest["encoding"]["exposureEV"]
    acc = load(os.path.join(layers_dir, manifest["base"]), gain)
    rects = {layer["name"]: layer.get("rect") for layer in manifest["layers"]}
    for arg in sys.argv[3:]:
        name, level = arg.split("=")
        img = float(level) * load(os.path.join(layers_dir, name + ".png"), gain)
        x, y, w, h = rects.get(name) or (0, 0, img.shape[1], img.shape[0])
        acc[y:y + h, x:x + w] += img  # cropped layers sit at their rect (crop_layers.py)
    rgb = linear_to_srgb(agx(acc))
    Image.fromarray((rgb * 255 + 0.5).astype(np.uint8)).save(out)
    print("wrote", out)


main()
