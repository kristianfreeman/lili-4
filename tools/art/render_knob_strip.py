"""Render the cream knob filmstrip: one knob, N rotations, alpha + real shadow.

    blender -b -P tools/art/render_knob_strip.py -- --out build/art/knob [--frames 64] [--samples 64]
    python3 tools/art/assemble_strip.py build/art/knob build/art/layers

Frame f shows value f / (N - 1) (0 = 7 o'clock, 1 = 5 o'clock). The knob sits
on a shadow catcher over a transparent film, lit by the board's own lights from
a mid-board position, so one strip serves all knobs. Encoding matches the bake:
linear light, sRGB-encoded 16-bit RGBA at the bake exposure, straight alpha.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402

import parts  # noqa: E402
import render_board as rb  # noqa: E402

CX, CY = 560, 400      # render position on the board (lighting reference)
SIZE_PX = 64           # sprite size in board px (knob Ø35 + shadow)
KNOB_R = 12.0          # must match render_board's knob_cream(r=12.0)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out, frames, samples = "build/art/knob", 64, 64
    for i, a in enumerate(argv):
        if a == "--out":
            out = argv[i + 1]
        if a == "--frames":
            frames = int(argv[i + 1])
        if a == "--samples":
            samples = int(argv[i + 1])

    scene = rb.reset_scene()
    scene.cycles.samples = samples
    scale = 2
    scene.render.resolution_x = scene.render.resolution_y = SIZE_PX * scale
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = rb.BAKE_EXPOSURE_EV
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "16"

    # The knob, built pointing straight up (value 0.5), parented to a pivot.
    objs = parts.knob_cream(CX, CY, 0.5, r=KNOB_R, ticks=False)
    pivot = bpy.data.objects.new("pivot", None)
    scene.collection.objects.link(pivot)
    pivot.location = (CX * rb.PX, -CY * rb.PX, 0.0)
    bpy.context.view_layer.update()
    for o in objs:
        o.parent = pivot
        o.matrix_parent_inverse = pivot.matrix_world.inverted()

    scene.frame_start, scene.frame_end = 1, frames
    for f in range(frames):
        value = f / (frames - 1)
        # built at value 0.5 (pointing up); clockwise on the board is -z
        pivot.rotation_euler.z = -math.radians(270 * (value - 0.5))
        pivot.keyframe_insert("rotation_euler", index=2, frame=f + 1)

    # Shadow catcher floor: contributes only the knob's shadow, as alpha.
    bpy.ops.mesh.primitive_plane_add(size=0.2, location=(CX * rb.PX, -CY * rb.PX, 0.0))
    floor = bpy.context.active_object
    floor.is_shadow_catcher = True

    rb.lights_and_camera(scene)
    cam = scene.camera
    cam.location = (CX * rb.PX, -CY * rb.PX, 1.0)
    cam.data.ortho_scale = SIZE_PX * rb.PX

    path = os.path.join(rb.ROOT, out)
    os.makedirs(path, exist_ok=True)
    scene.render.filepath = os.path.join(path, "frame_")
    bpy.ops.render.render(animation=True)
    print("RENDERED", frames, "frames to", path)


main()
