"""Render a comparison sheet of control styles on the LILI-8 board material.

    blender -b -P tools/art/render_parts.py -- --out build/art/parts.png [--tilt 30]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402

import parts  # noqa: E402
import render_board as rb  # noqa: E402

SW, SH = 1120, 560


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = "build/art/parts.png"
    tilt = 0.0
    scale = 2
    for i, a in enumerate(argv):
        if a == "--out":
            out = argv[i + 1]
        if a == "--tilt":
            tilt = float(argv[i + 1])
        if a == "--scale":
            scale = int(argv[i + 1])

    scene = rb.reset_scene()
    scene.render.resolution_x = SW * scale
    scene.render.resolution_y = SH * scale

    mask = rb.mottled_mask("mask", (0.030, 0.150, 0.072), (0.042, 0.178, 0.085))
    rb.box("board", -40, -40, SW + 80, SH + 80, 0.0016, mask, z=-0.0016, bevel=0)
    silk = parts.mats()["silk"]
    dim = rb.material("silk_dim", (0.55, 0.60, 0.55), rough=0.8)
    rb.text("LILI-8 · CONTROL STYLES", 40, 50, 12, silk)

    row1, row2 = 150, 400
    knobs = [
        (170, "A  TRIMMER, REFINED", parts.knob_trimmer),
        (420, "B  FLUTED BLACK", parts.knob_davies),
        (670, "C  MACHINED ALUMINIUM", parts.knob_alu),
        (920, "D  VINTAGE CREAM", parts.knob_cream),
    ]
    for x, label, fn in knobs:
        fn(x, row1, 0.62)
        rb.text(label, x, row1 + 62, 9, silk, "center")

    for k, pos in enumerate((1, 0, -1)):
        parts.toggle_switch(120 + k * 50, row2, pos)
    rb.text("E  BAT TOGGLE  UP · CTR · DN", 170, row2 + 62, 9, silk, "center")
    rb.text("3-POS = SOURCE: PAIR / OFF / LFO", 170, row2 + 78, 8, dim, "center")

    parts.slide_switch(390, row2, False)
    parts.slide_switch(450, row2, True)
    rb.text("F  SLIDE SWITCH", 420, row2 + 62, 9, silk, "center")

    parts.tact_button(640, row2, True)
    parts.tact_button(710, row2, False)
    rb.text("G  TACT BUTTON + LED", 675, row2 + 62, 9, silk, "center")

    rb.text("CURRENT FOR COMPARISON", 920, row2 - 40, 8, dim, "center")
    blue = rb.material("dip_blue", (0.035, 0.12, 0.42), rough=0.32, coat=0.4)
    white = rb.material("dip_white", (0.93, 0.92, 0.88), rough=0.35)
    black = rb.material("blk", (0.018, 0.018, 0.02), rough=0.45)
    rb.box("dip_body", 880, row2 - 25, 42, 50, 0.004, blue, bevel=1.2)
    rb.box("dip_well", 894, row2 - 19, 14, 26, 0.0005, black, z=0.004, bevel=0.3)
    rb.box("dip_lever", 896, row2 - 17, 10, 10, 0.0012, white, z=0.004, bevel=0.6)
    for j in range(2):
        rb.box("hdr_base", 940 + 26 * j - 7, row2 - 7, 14, 14, 0.0025, black, bevel=0.8)
    rb.box("cap", 940 - 8, row2 - 9, 16, 18, 0.0075, black, bevel=1.5)
    rb.text("DIP · JUMPER", 920, row2 + 62, 9, dim, "center")

    rb.lights_and_camera(scene, w=SW, h=SH, tilt_deg=tilt)
    rb.compositor(scene)
    path = os.path.join(rb.ROOT, out)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("WROTE", path)


main()
