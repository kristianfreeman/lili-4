"""Photographic post for the promo renders: bloom on the glowing parts, a filmic grade
(warm highlights, cool lifted shadows), vignette, faint lateral chromatic aberration and
grain. Runs on a rendered PNG in a second or two, so the grade can be iterated
without re-rendering.

    python3 tools/art/promo_post.py promo/preview-8.png promo/preview-8-post.png
"""

import sys

import numpy as np
from PIL import Image, ImageFilter

BLOOM = 0.25        # strength of the glow halo
BLOOM_THRESH = 0.85  # only pixels brighter than this bloom
VIGNETTE = 0.5     # darkening at the corners
CA_PX = 1.2         # red/blue offset at the frame edge, in pixels
GRAIN = 0.018
SHADOW_TINT = np.array([0.92, 1.0, 1.06])    # cool shadows
HIGHLIGHT_TINT = np.array([1.05, 1.0, 0.93])  # warm highlights
LIFT = 0.012        # raise the blacks slightly, film-style
CONTRAST = 0.45     # blend toward a smoothstep S-curve


def bloom(img):
    lum = img.mean(axis=2, keepdims=True)
    bright = img * np.clip((lum - BLOOM_THRESH) / (1 - BLOOM_THRESH), 0, 1)
    pil = Image.fromarray((bright * 255).astype(np.uint8))
    h = img.shape[0]
    halo = sum(np.asarray(pil.filter(ImageFilter.GaussianBlur(h * r)), np.float32) / 255
               for r in (0.006, 0.02, 0.05)) / 3
    return 1 - (1 - img) * (1 - BLOOM * halo)  # screen blend


def grade(img):
    lum = img.mean(axis=2, keepdims=True)
    tint = SHADOW_TINT * (1 - lum) + HIGHLIGHT_TINT * lum
    img = img * tint
    # gentle S-curve: a quarter of the way toward smoothstep
    img = np.clip(img, 0, 1)
    img = img + CONTRAST * (img * img * (3 - 2 * img) - img)
    return LIFT + (1 - LIFT) * img


def vignette(img):
    h, w = img.shape[:2]
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.sqrt(((x - w / 2) / (w / 2))**2 + ((y - h / 2) / (h / 2))**2) / np.sqrt(2)
    return img * (1 - VIGNETTE * r[..., None]**2.2)


def aberration(img):
    h, w = img.shape[:2]
    out = img.copy()
    for ch, s in ((0, 1.0), (2, -1.0)):
        scale = 1 + s * CA_PX / (w / 2)
        pil = Image.fromarray((img[..., ch] * 255).astype(np.uint8))
        nw, nh = round(w * scale), round(h * scale)
        res = np.asarray(pil.resize((nw, nh), Image.BICUBIC), np.float32) / 255
        if scale > 1:
            x0, y0 = (nw - w) // 2, (nh - h) // 2
            out[..., ch] = res[y0:y0 + h, x0:x0 + w]
        else:
            pad = np.pad(res, (((h - nh) // 2, h - nh - (h - nh) // 2), ((w - nw) // 2, w - nw - (w - nw) // 2)),
                         mode="edge")
            out[..., ch] = pad
    return out


def grain(img):
    rng = np.random.default_rng(7)
    n = rng.normal(0, GRAIN, img.shape[:2])[..., None]
    lum = img.mean(axis=2, keepdims=True)
    return img + n * (0.4 + 0.6 * (1 - lum))  # grain shows more in the shadows


def main(src, dst):
    img = np.asarray(Image.open(src).convert("RGB"), np.float32) / 255
    img = bloom(img)
    img = grade(img)
    img = vignette(img)
    img = aberration(np.clip(img, 0, 1))
    img = grain(img)
    Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8)).save(dst)
    print("WROTE", dst)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
