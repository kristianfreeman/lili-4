"""Give each baked glow layer the soft halo of light under the solder mask.

    python3 tools/art/bloom_layers.py build/art/layers

Run it on the full-frame bake, before crop_layers.py. Lit copper is under the
mask, so its light bleeds sideways through the mask and lights the board around
it. Each layer gains a wide soft halo (a sum of Gaussians of its own light, in
linear light), masked to where the board shows bare mask: full-strength silk,
gold, chips and metal in the base occlude it, so labels stay legible. Knobs and
toggles are drawn over the glow at runtime, so they occlude it too. The halo is
part of the layer, so the editor still drives each one by its own level.
"""

import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from composite import linear_to_srgb, load  # noqa: E402

# (sigma in board px, weight): a tight bloom, a soft spill and a faint wide wash.
HALO = [(1.5, 0.55), (5.0, 0.45), (12.0, 0.30)]
# Point lights (LEDs) are small and bright: they bloom wider.
HALO_POINT = [(2.0, 0.6), (7.0, 0.5), (18.0, 0.35)]
POINT_KINDS = ("lfo", "stamens")


def gaussian_bank(img, sigmas_weights):
    """Sum of weighted Gaussian blurs of an HxWxC image, via one FFT (zero padded, no wrap)."""
    h, w = img.shape[:2]
    pad = int(3 * max(s for s, _ in sigmas_weights)) + 2
    hp, wp = h + 2 * pad, w + 2 * pad
    padded = np.zeros((hp, wp, img.shape[2]))
    padded[pad:pad + h, pad:pad + w] = img
    spec = np.fft.rfft2(padded, axes=(0, 1))
    fy = np.fft.fftfreq(hp)[:, None]
    fx = np.fft.rfftfreq(wp)[None, :]
    f2 = fy * fy + fx * fx
    kernel = sum(wt * np.exp(-2.0 * np.pi ** 2 * s * s * f2) for s, wt in sigmas_weights)
    out = np.fft.irfft2(spec * kernel[..., None], s=(hp, wp), axes=(0, 1))
    return np.maximum(out[pad:pad + h, pad:pad + w], 0.0)


def bare_mask(base):
    """1 where the base shows plain green mask (or copper under it), 0 on silk, gold, metal and parts."""
    g = base[..., 1]
    greenness = (g - np.maximum(base[..., 0], base[..., 2])) / np.maximum(g, 1e-4)
    m = np.clip((greenness - 0.2) / 0.25, 0.0, 1.0)
    return gaussian_bank(m[..., None], [(0.6, 1.0)])[..., 0]


def save16(path, linear, gain):
    """Same encoding as the bake: sRGB-encoded 16-bit RGB at the bake exposure."""
    enc = linear_to_srgb(np.clip(linear / gain, 0.0, 1.0))
    _write_png16(path, (enc * 65535.0 + 0.5).astype(">u2"))


def _write_png16(path, rgb16):
    """Minimal 16-bit RGB PNG writer (Pillow can read these, but not write them)."""
    import struct
    import zlib

    h, w, _ = rgb16.shape
    raw = b"".join(b"\x00" + rgb16[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main():
    layers_dir = sys.argv[1]
    path = os.path.join(layers_dir, "manifest.json")
    manifest = json.load(open(path))
    gain = 2.0 ** -manifest["encoding"]["exposureEV"]
    scale = manifest["scale"]
    base = load(os.path.join(layers_dir, manifest["base"]), gain)
    occl = bare_mask(base)[..., None]
    done = 0
    for layer in manifest["layers"]:
        if layer["kind"] == "meter" or layer.get("halo") or "rect" in layer:
            continue  # meters are drawn by the editor; cropped or haloed layers are already processed
        file = os.path.join(layers_dir, layer["file"])
        lin = load(file, gain)
        bank = HALO_POINT if layer["kind"] in POINT_KINDS else HALO
        halo = gaussian_bank(lin, [(s * scale, wt) for s, wt in bank])
        save16(file, lin + halo * occl, gain)
        layer["halo"] = True
        done += 1
    manifest["halo"] = {"under_mask": [list(x) for x in HALO], "point": [list(x) for x in HALO_POINT],
                        "comment": "sigma (board px), weight; masked to bare solder mask in the base"}
    json.dump(manifest, open(path, "w"), indent=1)
    print(f"haloed {done} glow layers")


main()
