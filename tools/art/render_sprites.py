"""Render control sprite strips: one part, several states, alpha + real shadow.

    blender -b -P tools/art/render_sprites.py -- --part knob   --out build/art/knob   [--frames 64]
    blender -b -P tools/art/render_sprites.py -- --part toggle --out build/art/toggle
    python3 tools/art/assemble_strip.py build/art/knob   build/art/layers --name knobStrip
    python3 tools/art/assemble_strip.py build/art/toggle build/art/layers --name toggleStrip

knob:   frame f shows value f / (N - 1) (0 = 7 o'clock, 1 = 5 o'clock).
toggle: frames 0, 1, 2 = lever up, centre, down.

The part sits on a shadow catcher over a transparent film, lit by the board's
own lights from a mid-board position, so one strip serves every instance.
Encoding matches the bake: linear light, sRGB-encoded 16-bit RGBA at the bake
exposure, straight alpha.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402

import parts  # noqa: E402
import render_board as rb  # noqa: E402

CX, CY = rb.W // 2, rb.H // 2  # render position on the board (lighting reference): its centre
KNOB_R = 12.0                   # knob_cream(r=12.0), then scaled by the layout's knobScale
KNOB_S = rb.LAYOUT["knobScale"]
TOGGLE_S = rb.TOGGLE_S          # must match render_board's toggle_switch(s=TOGGLE_S)
# Room for the long soft shadows the low key light throws to the lower right;
# assemble_strip.py feathers the last margin so nothing shows a seam.
SPRITE_PX = {"knob": 96, "toggle": 128}


def setup(scene, size_px, samples):
    scene.cycles.samples = samples
    scene.render.resolution_x = scene.render.resolution_y = size_px * 2
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = rb.BAKE_EXPOSURE_EV
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "16"
    # Shadow catcher floor: contributes only the part's shadow, as alpha.
    bpy.ops.mesh.primitive_plane_add(size=0.2, location=(CX * rb.PX, -CY * rb.PX, 0.0))
    bpy.context.active_object.is_shadow_catcher = True
    rb.lights_and_camera(scene)
    scene.camera.location = (CX * rb.PX, -CY * rb.PX, 1.0)
    scene.camera.data.ortho_scale = size_px * rb.PX


def render_knob(scene, path, frames):
    objs = parts.scale_about(parts.knob_cream(CX, CY, 0.5, r=KNOB_R, ticks=False), CX, CY, KNOB_S)  # pointing up
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
        # clockwise on the board is -z
        pivot.rotation_euler.z = -math.radians(270 * (value - 0.5))
        pivot.keyframe_insert("rotation_euler", index=2, frame=f + 1)
    scene.render.filepath = os.path.join(path, "frame_")
    bpy.ops.render.render(animation=True)
    return frames


def render_toggle(scene, path):
    for k, position in enumerate((1, 0, -1)):  # up, centre, down
        objs = parts.toggle_switch(CX, CY, position, s=TOGGLE_S)
        scene.render.filepath = os.path.join(path, f"frame_{k + 1:04d}.png")
        bpy.ops.render.render(write_still=True)
        for o in objs:
            bpy.data.objects.remove(o, do_unlink=True)
    return 3


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    part, out, frames, samples = "knob", None, 64, 64
    for i, a in enumerate(argv):
        if a == "--part":
            part = argv[i + 1]
        if a == "--out":
            out = argv[i + 1]
        if a == "--frames":
            frames = int(argv[i + 1])
        if a == "--samples":
            samples = int(argv[i + 1])
    out = out or f"build/art/{part}"

    scene = rb.reset_scene()
    setup(scene, SPRITE_PX[part], samples)
    path = os.path.join(rb.ROOT, out)
    os.makedirs(path, exist_ok=True)
    n = render_knob(scene, path, frames) if part == "knob" else render_toggle(scene, path)
    print("RENDERED", n, part, "frames to", path)


main()
