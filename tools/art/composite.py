"""Reference compositor for baked layers (mirrors what the plugin's shader will do).

    python3 tools/art/composite.py build/art/layers out.png petal0=1 mix=0.8 knob:tune1=0.2 toggle:bee=1 outmode=0 ...

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
# ASC-CDL "look" fitted to Blender's "AgX - Medium High Contrast" render of the board
# (MSE 0.018 -> 0.0008).
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
    args = sys.argv[3:]

    # Glow first: the lit copper is under the mask, so knobs and toggles (drawn next) cover it,
    # as in the editor.
    rects = {layer["name"]: layer.get("rect") for layer in manifest["layers"]}
    for arg in args:
        if arg.startswith(("knob:", "toggle:", "outmode")):
            continue
        name, level = arg.split("=")
        img = float(level) * load(os.path.join(layers_dir, name + ".png"), gain)
        x, y, w, h = rects.get(name) or (0, 0, img.shape[1], img.shape[0])
        acc[y:y + h, x:x + w] += img  # cropped layers sit at their rect (crop_layers.py)

    # Control bodies from sprite strips (a base baked with --no-controls has only labels/scales).
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    board = json.load(open(os.path.join(root, "art", "board.json")))
    s = manifest["scale"]

    def overrides(prefix):
        return {a[len(prefix):].split("=")[0]: float(a.split("=")[1]) for a in args if a.startswith(prefix)}

    def strip(key):
        info = manifest.get(key)
        if not info:
            return None, None
        raw = np.asarray(Image.open(os.path.join(layers_dir, info["file"]))).astype(np.float64)
        return info, raw / (65535.0 if raw.max() > 255 else 255.0)

    def draw(info, raw, frame_index, cx, cy):
        fw, fh = info["frameSize"]
        frame = raw[frame_index * fh:(frame_index + 1) * fh]
        rgb = srgb_to_linear(frame[..., :3]) * gain
        a = frame[..., 3:4]
        x0, y0 = int(cx * s - fw / 2), int(cy * s - fh / 2)
        # Clip to the board: sprites near the edge (shadow margins) hang over it.
        cx0, cy0 = max(x0, 0), max(y0, 0)
        cx1, cy1 = min(x0 + fw, acc.shape[1]), min(y0 + fh, acc.shape[0])
        rgb = rgb[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0]
        a = a[cy0 - y0:cy1 - y0, cx0 - x0:cx1 - x0]
        acc[cy0:cy1, cx0:cx1] = rgb * a + acc[cy0:cy1, cx0:cx1] * (1 - a)

    info, raw = strip("knobStrip")
    if info:
        knob_values = overrides("knob:")
        for pid, x, y, _label, value, *_ in board["knobs"]:
            value = knob_values.get(pid, value)
            draw(info, raw, round(value * (info["frames"] - 1)), x, y)

    info, raw = strip("toggleStrip")
    if info:
        states = overrides("toggle:")
        for pid, x, y, _title, labels, sel, *_ in board["jumpers"]:
            sel = int(states.get(pid, sel))
            draw(info, raw, sel if len(labels) == 3 else (0 if sel == 0 else 2), x, y)
        for pid, x, y, _title, _legends, on, *_ in board["dips"]:
            on = states.get(pid, on)
            draw(info, raw, 0 if on else 2, x, y)

    # Output-mode silkscreen (outmode=0 mono, 1 stereo): its re-rendered board patch.
    om = manifest.get("outMode")
    if om:
        k = int(overrides("outmode").get("", board["outMode"].get("default", 0)))
        px, py, pw, ph = (v * s for v in om["patch"])
        patch = load(os.path.join(layers_dir, om["files"][k]), gain)
        acc[py:py + ph, px:px + pw] = patch[py:py + ph, px:px + pw]

    rgb = linear_to_srgb(agx(acc))
    Image.fromarray((rgb * 255 + 0.5).astype(np.uint8)).save(out)
    print("wrote", out)


if __name__ == "__main__":  # also imported by export_ui.py
    main()
