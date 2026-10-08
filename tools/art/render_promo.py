"""Promotional renders of the LILI-4 board (quick first pass).

    blender -b -P tools/art/render_promo.py -- [--shot exploded|knobs|hero|all] [--samples 64] [--pct 100]

Reuses render_board.build() for the whole scene (lit "playing" state, as in
`render_board.py --lit`), then swaps the orthographic UI camera for a
perspective one and stages three shots:

  exploded  1920x1080  the board pulled apart into layers (substrate, copper,
                       silkscreen, components, controls), 3/4 view
  knobs     1920x1080  low-angle macro across the PETAL 1 knobs, shallow focus,
                       the glowing flower behind
  hero      1080x1080  the whole board on standoffs over a dark desk, angled,
                       with depth of field

Writes to promo/<shot>.png. Does not touch the shipping assets.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import render_board as rb  # noqa: E402  (parses its own args from argv; ours are ignored by it)

PX = rb.PX
W, H = rb.W, rb.H


def parse():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    a = {"shot": "all", "samples": 64, "pct": 100}
    for i, k in enumerate(argv):
        if k in ("--shot", "--samples", "--pct"):
            a[k[2:]] = argv[i + 1]
    a["samples"], a["pct"] = int(a["samples"]), int(a["pct"])
    return a


ARGS = parse()
OUTDIR = os.path.join(rb.ROOT, "promo")


# ----------------------------------------------------------------------------- scene

def base_scene(res):
    rb.ARGS["lit"] = True          # glowing petals, stem, meters: the "playing" look
    rb.ARGS["no_knobs"] = False
    rb.ARGS["samples"] = ARGS["samples"]
    # Factory reset between shots frees cached datablocks; drop the helpers' caches.
    rb.FONTS.clear()
    rb._INK.clear()
    rb.LIGHTGROUPS.clear()
    rb.OUTMODE_TEXTS.clear()
    if "parts" in sys.modules:
        sys.modules["parts"]._MATS.clear()
    scene = rb.reset_scene()
    rb.build(scene)
    rb.lights_and_camera(scene)
    rb.compositor(scene)            # bloom + a whisper of vignette
    # The UI's key light sits where an angled camera sees it mirrored in the glossy
    # mask as a big hot spot; keep its light, drop its reflection.
    bpy.data.objects["key"].visible_glossy = False
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = ARGS["pct"]
    scene.cycles.samples = ARGS["samples"]
    scene.cycles.use_denoising = True
    scene.cycles.max_bounces = 6
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_depth = "8"
    return scene


def perspective(scene, loc, target, lens=50.0, focus=None, fstop=None):
    cam = scene.camera
    cam.data.type = "PERSP"
    cam.data.lens = lens
    cam.data.clip_start = 0.005
    cam.data.clip_end = 50.0
    cam.location = Vector(loc)
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    if fstop:
        cam.data.dof.use_dof = True
        cam.data.dof.focus_distance = ((Vector(focus or target) - Vector(loc)).length)
        cam.data.dof.aperture_fstop = fstop
    return cam


def B(x, y, z=0.0):
    """Board px (+ height in metres) -> world."""
    return (x * PX, -y * PX, z)


def area(name, loc, target, size, energy, color=(1, 1, 1)):
    data = bpy.data.lights.new(name, "AREA")
    data.shape = "DISK"
    data.size = size
    data.energy = energy
    data.color = color
    obj = rb.link(bpy.data.objects.new(name, data))
    obj.location = loc
    obj.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    return obj


def floor(z, color=(0.012, 0.013, 0.015), rough=0.55, size=30.0):
    bpy.ops.mesh.primitive_plane_add(size=size, location=(W * PX / 2, -H * PX / 2, z))
    f = bpy.context.active_object
    f.name = "floor"
    f.data.materials.append(rb.material("floor", color, rough=rough))
    return f


def world_gradient(scene, top=(0.05, 0.06, 0.07), strength=1.0):
    """Swap the near-black world for a dim cool backdrop (reflections + background)."""
    nt = scene.world.node_tree
    bg = nt.nodes["Background"]
    bg.inputs["Color"].default_value = (*top, 1)
    bg.inputs["Strength"].default_value = strength


def all_objects():
    return [o for o in bpy.context.scene.objects if o.type in ("MESH", "CURVE", "FONT")]


# ----------------------------------------------------------------------------- exploded view

COPPER_MATS = {"trace", "trace_bottom", "trace_lit", "trace_bottom_lit", "enig", "enig_lit", "tin", "hole",
               "bare_copper", "bare_copper_lit"}
SILK_MATS = {"silk", "silk_dim", "silk2"}
CONTROL_MATS = {"knob_cream", "nickel", "chrome2", "paint_black"}


def classify(obj):
    name = obj.name
    mats = {m.name.split(".")[0] for m in obj.data.materials if m}
    if name == "board":
        return "substrate"
    if name.startswith(("u1", "u2", "jack", "stamen", "smd_", "led_")):
        return "components"
    if name.startswith(("cream_", "tog_")) or mats & CONTROL_MATS:
        return "controls"
    if mats & SILK_MATS or any(m.startswith("silk_") for m in mats):
        return "silk"
    if mats & COPPER_MATS:
        return "copper"
    return "components"


def copperize():
    """Show the copper layer as bare copper (the real board hides it under the mask)."""
    # A faint self-glow so unlit copper still reads against the dark backdrop.
    cu = rb.material("bare_copper", (0.93, 0.50, 0.30), rough=0.22, metal=1.0,
                     emit=(1.0, 0.45, 0.2), emit_strength=0.35)
    cu_lit = rb.material("bare_copper_lit", (0.93, 0.50, 0.30), rough=0.22, metal=1.0,
                         emit=(1.0, 0.48, 0.12), emit_strength=3.0)
    for o in all_objects():
        if classify(o) != "copper":
            continue
        for i, m in enumerate(o.data.materials):
            if m is None:
                continue
            n = m.name.split(".")[0]
            if n in ("trace", "trace_bottom"):
                o.data.materials[i] = cu
            elif n in ("trace_lit", "trace_bottom_lit"):
                o.data.materials[i] = cu_lit


def shot_exploded():
    scene = base_scene((1920, 1080))
    gap = 0.18
    order = ["substrate", "copper", "silk", "components", "controls"]
    lift = {k: i * gap for i, k in enumerate(order)}
    copperize()
    for o in all_objects():
        if o.parent is None:
            o.location.z += lift[classify(o)]
    # An FR-4 core under the mask, and thin guide rods through the mounting holes,
    # the classic exploded-view cue that the layers belong together.
    core = rb.material("fr4", (0.55, 0.47, 0.25), rough=0.6, bump=0.08, bump_scale=400)
    rb.box("fr4_core", 6, 6, W - 12, H - 12, 0.0012, core, z=-gap, bevel=2.0)
    rod = rb.material("rod", (0.9, 0.9, 0.9), rough=0.3, emit=(0.8, 0.85, 1.0), emit_strength=0.6)
    top = lift["controls"] + 0.02
    for x, y in mount_points():
        rb.cylinder("rod", x, y, 1.2, top + gap + 0.03, rod, z=-gap - 0.03, verts=16)
    world_gradient(scene, (0.035, 0.04, 0.05), 1.0)
    floor(-gap - 0.25, color=(0.02, 0.022, 0.026), rough=0.7, size=400.0)
    cx, cy = W / 2, H / 2
    # Extra light from the front so the lifted layers read against the dark.
    area("promo_front", B(cx - 200, cy + 1300, 1.2), B(cx, cy, 0.15), 1.5, 60, (1.0, 0.95, 0.9))
    area("promo_back", B(cx + 600, cy - 1400, 0.5), B(cx, cy, 0.15), 1.5, 45,
         (0.7, 0.8, 1.0)).visible_glossy = False
    # Fairly low (about 18 degrees) so the sparse upper layers separate instead of stacking up.
    target = B(cx + 20, cy, lift["silk"] - 0.08)
    perspective(scene, B(cx - 1500, cy + 2600, 1.29), target, lens=45, fstop=5.6)
    return scene


# ----------------------------------------------------------------------------- knob macro

def mount_points(inset=16):
    """Standoff/rod positions: the board has no mounting holes any more, so use the corners."""
    return [(inset, inset), (W - inset, inset), (inset, H - inset), (W - inset, H - inset)]


def shot_knobs():
    scene = base_scene((1920, 1080))
    floor(-0.0017)
    world_gradient(scene, (0.03, 0.035, 0.045), 1.0)
    # Camera low off the board's front-left corner, looking along the ECHO knob row
    # toward the glowing stem and flower.
    cam_at = B(5, 790, 0.035)
    focus = B(122, 715, 0.008)  # TIME 2
    target = B(420, 560, -0.012)
    perspective(scene, cam_at, target, lens=35, focus=focus, fstop=4.0)
    scene.view_settings.exposure = -0.4  # keep the cream knobs cream, not white
    bpy.data.objects["fill"].visible_glossy = False  # its mirror image washed out the right side
    # A low warm grazing light along the row, for relief on the flutes.
    area("promo_graze", B(-300, 600, 0.08), B(200, 715, 0.0), 0.3, 4, (1.0, 0.85, 0.65))
    return scene


# ----------------------------------------------------------------------------- hero

def shot_hero():
    scene = base_scene((1080, 1080))
    stand = 0.012
    # Lift the board onto brass standoffs above a dark desk.
    for o in all_objects():
        if o.parent is None:
            o.location.z += stand
    brass = rb.material("standoff", (0.86, 0.66, 0.36), rough=0.3, metal=1.0)
    for x, y in mount_points():
        bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=5.5 * PX, depth=stand - 0.0016)
        s = bpy.context.active_object
        s.location = B(x, y, (stand - 0.0016) / 2)
        s.data.materials.append(brass)
    floor(0.0, color=(0.012, 0.012, 0.014), rough=0.6)
    world_gradient(scene, (0.03, 0.035, 0.045), 1.0)
    cx, cy = W / 2, H / 2
    area("promo_front", B(cx + 300, cy + 1300, 1.0), B(cx, cy, 0.0), 1.5, 30, (1.0, 0.95, 0.9))
    # A long soft strip behind the board, placed so the glossy mask mirrors it as a
    # sheen across the far half.
    strip = area("promo_strip", B(cx - 750, cy - 1700, 1.6), B(cx, cy, 0.0), 1.0, 20, (0.9, 0.95, 1.0))
    strip.visible_diffuse = False
    strip.data.shape = "RECTANGLE"
    strip.data.size, strip.data.size_y = 3.0, 0.5
    focus = B(560, 300, 0.01)  # the U1 can at the flower's heart
    perspective(scene, B(cx + 650, cy + 1150, 0.8), B(cx + 10, cy - 30, 0.0), lens=50,
                focus=focus, fstop=2.0)
    return scene


# ----------------------------------------------------------------------------- physical close-ups
#
# The UI parts are modelled for a straight-down view: the cream knob is a 35 mm
# skirt with a 24 mm cap only 10.4 mm tall, and toggles lean 34 degrees so the
# throw reads from above. Up close and at an angle they look like squat discs.
# For these shots the controls are rebuilt at real-hardware proportions.
#
# Scale: the board is 1 px = 1 mm (a 1120 x 800 mm board). Anchoring the 35 mm
# UI skirt to a real ~22 mm Davies-1900H-style skirt gives REAL = 1.6 scene mm
# per real mm, so the board stands for a ~700 x 500 mm panel with ~35-39 mm knob
# pitch. All the real dimensions below are multiplied by REAL.

REAL = 1.6


def mm(real_mm):
    """Real millimetres -> scene metres."""
    return real_mm * REAL * PX


def r_px(real_mm):
    """Real millimetres -> scene px (for the board-px helpers)."""
    return real_mm * REAL


# Real dimensions (mm).
PCB_T = 1.6                                   # FR-4 thickness
NUT_AF, NUT_T, WASHER_D, WASHER_T = 10.0, 2.0, 13.0, 0.5   # 9 mm pot: M7 nut, washer
BUSH_D = 7.0                                  # pot bushing (M7 thread)
SKIRT_GAP = 3.0                               # skirt bottom above the panel (clears the nut)
SKIRT_D0, SKIRT_D1, SKIRT_H = 22.0, 20.0, 2.6  # flared skirt, bottom/top diameter
BODY_D0, BODY_D1, BODY_H = 14.5, 12.8, 11.0   # tapered fluted body
FLUTES, FLUTE_DEPTH = 24, 0.55
KNOB_H = SKIRT_GAP + SKIRT_H + BODY_H         # 16.6 mm above the panel

TOG_NUT_AF, TOG_NUT_T = 9.5, 2.2              # 1/4-40 bushing hex nut
TOG_BUSH_D, TOG_BUSH_H = 6.35, 9.0            # threaded bushing, height above panel
TOG_LEVER, TOG_LEVER_D0, TOG_LEVER_D1 = 12.0, 2.4, 3.6  # bat lever above the pivot
TOG_TIP_D, TOG_THROW = 4.2, 18.0              # bat tip, throw angle (degrees)


def _hex(name, cx, cy, af, h, z, mat):
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=mm(af / 2 / math.cos(math.pi / 6)), depth=mm(h))
    o = bpy.context.active_object
    o.name = name
    o.location = (cx * PX, -cy * PX, z + mm(h) / 2)
    o.rotation_euler.z = math.radians(30)
    bev = o.modifiers.new("bevel", "BEVEL")
    bev.width = mm(0.25)
    bev.segments = 2
    o.data.materials.append(mat)
    return o


def _taper_top(obj, ratio):
    """Scale the top ring of a radial_prism (local z > half height) toward the axis."""
    zs = [v.co.z for v in obj.data.vertices]
    mid = (min(zs) + max(zs)) / 2
    for v in obj.data.vertices:
        if v.co.z > mid:
            v.co.x *= ratio
            v.co.y *= ratio


def physical_knob(cx, cy, value, m):
    import parts
    steel = m.setdefault("steel", rb.material("steel", (0.8, 0.8, 0.78), rough=0.28, metal=1.0,
                                              bump=0.05, bump_scale=3000))
    cream = m["knob_cream"]
    # Pot hardware: washer, hex nut, threaded bushing.
    rb.cylinder("pk_washer", cx, cy, r_px(WASHER_D / 2), mm(WASHER_T), steel, verts=48)
    _hex("pk_nut", cx, cy, NUT_AF, NUT_T, mm(WASHER_T), steel)
    parts.radial_prism("pk_bush", cx, cy, lambda th: r_px(BUSH_D / 2) - 0.25 * abs(math.sin(14 * th)),
                       mm(SKIRT_GAP - WASHER_T - NUT_T + 0.3), mm(WASHER_T + NUT_T), steel, segments=96)
    # Flared skirt.
    z = mm(SKIRT_GAP)
    bpy.ops.mesh.primitive_cone_add(vertices=128, radius1=mm(SKIRT_D0 / 2), radius2=mm(SKIRT_D1 / 2),
                                    depth=mm(SKIRT_H), location=(cx * PX, -cy * PX, z + mm(SKIRT_H) / 2))
    skirt = bpy.context.active_object
    skirt.name = "pk_skirt"
    skirt.data.materials.append(cream)
    bev = skirt.modifiers.new("bevel", "BEVEL")
    bev.width, bev.segments, bev.limit_method = mm(0.5), 3, "ANGLE"
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40))
    # Fluted, tapered grip body.
    z += mm(SKIRT_H)
    r0 = r_px(BODY_D0 / 2)
    body = parts.radial_prism("pk_body", cx, cy,
                              lambda th: r0 - r_px(FLUTE_DEPTH) * (0.5 - 0.5 * math.cos(FLUTES * th)) ** 0.6,
                              mm(BODY_H), z, cream, segments=384, top_bevel_px=r_px(0.9), smooth_deg=30)
    _taper_top(body, BODY_D1 / BODY_D0)
    top = z + mm(BODY_H)
    # Pointer: painted line across the top, and the index down onto the skirt.
    parts.pointer("pk_line", cx, cy, value, r_px(0.8), r_px(BODY_D1 / 2 - 0.9), r_px(0.9), top - mm(0.02),
                  m["paint_black"], height=mm(0.08))
    parts.pointer("pk_index", cx, cy, value, r_px(BODY_D0 / 2 - 0.3), r_px(SKIRT_D1 / 2 - 0.3), r_px(0.9),
                  z - mm(0.02), m["paint_black"], height=mm(0.08))


def physical_toggle(cx, cy, position, m):
    import parts
    _hex("pt_nut", cx, cy, TOG_NUT_AF, TOG_NUT_T, 0.0, m["nickel"])
    parts.radial_prism("pt_bush", cx, cy, lambda th: r_px(TOG_BUSH_D / 2) - 0.3 * abs(math.sin(12 * th)),
                       mm(TOG_BUSH_H - TOG_NUT_T), mm(TOG_NUT_T), m["nickel"], segments=160, top_bevel_px=0.6)
    tilt = math.radians(TOG_THROW) * position
    length, pivot = mm(TOG_LEVER), mm(TOG_BUSH_H - 1.0)
    bpy.ops.mesh.primitive_cone_add(vertices=48, radius1=mm(TOG_LEVER_D0 / 2), radius2=mm(TOG_LEVER_D1 / 2),
                                    depth=length)
    bat = bpy.context.active_object
    bat.name = "pt_bat"
    bat.data.materials.append(m["chrome"])
    bpy.ops.object.shade_smooth()
    bat.location = (cx * PX, -cy * PX + math.sin(tilt) * length / 2, pivot + math.cos(tilt) * length / 2)
    bat.rotation_euler.x = -tilt
    bpy.ops.mesh.primitive_uv_sphere_add(radius=mm(TOG_TIP_D / 2), location=(
        cx * PX, -cy * PX + math.sin(tilt) * length, pivot + math.cos(tilt) * length))
    tip = bpy.context.active_object
    tip.name = "pt_tip"
    tip.scale.z = 0.85
    tip.data.materials.append(m["chrome"])
    bpy.ops.object.shade_smooth()


def physicalize(scene):
    """Swap the UI's knobs/toggles for real-proportion ones; thicken the PCB to 1.6 mm."""
    import parts
    m = parts.mats()
    for o in list(scene.objects):
        if o.type != "MESH":
            continue
        names = {s.name.split(".")[0] for s in o.data.materials if s}
        if o.name.startswith("cream_") or names & {"nickel", "chrome2", "knob_cream"}:
            bpy.data.objects.remove(o, do_unlink=True)
    L = rb.LAYOUT
    ks = L.get("knobScale", 1.0)

    def scaled(build, cx, cy):
        # Build at full size, then shrink about the control centre like the board's own parts.
        before = set(bpy.data.objects)
        build()
        parts.scale_about([o for o in bpy.data.objects if o not in before], cx, cy, ks)

    # Layout coordinates are control centres.
    for (pid, cx, cy, label, value, *_rest) in L["knobs"]:
        scaled(lambda: physical_knob(cx, cy, value, m), cx, cy)
    for (pid, cx, cy, title, names, sel, *_rest) in L["jumpers"]:
        throws = (1, 0, -1) if len(names) == 3 else (1, -1)
        scaled(lambda: physical_toggle(cx, cy, throws[sel], m), cx, cy)
    for (pid, cx, cy, title, names, on, *_rest) in L["dips"]:
        scaled(lambda: physical_toggle(cx, cy, 1 if on else -1, m), cx, cy)
    board = bpy.data.objects["board"]
    board.data.extrude = mm(PCB_T) / 2
    board.location.z = -mm(PCB_T) / 2
    # A touch more solder-mask texture so the board reads as a surface up close.
    for node in bpy.data.materials["mask"].node_tree.nodes:
        if node.type == "BUMP":
            node.inputs["Strength"].default_value = 0.12
    print("PHYSICAL knob: skirt %.1f/%.1f mm, body %.1f-%.1f mm, height %.1f mm above panel (scene x%.1f)"
          % (SKIRT_D0, SKIRT_D1, BODY_D0, BODY_D1, KNOB_H, REAL))


def knob_rig(scene, cam, focus, key_side=1.0, key_elev=20.0, key_w=45.0, rim_w=8.0):
    """Raking key from the side and a little behind the subject (soft shadows run toward
    the camera), a cool rim behind for edge separation and board sheen, a dim fill."""
    for n in ("key", "fill", "rim"):
        bpy.data.objects.remove(bpy.data.objects[n], do_unlink=True)
    f, c = Vector(focus), Vector(cam)
    fwd = f - c
    fwd.z = 0
    fwd.normalize()
    side = Vector((-fwd.y, fwd.x, 0.0))

    def at(direction, elev, dist):
        d = direction.normalized() * math.cos(math.radians(elev)) + Vector((0, 0, math.sin(math.radians(elev))))
        return f + d * dist

    area("pk_key", at(side * key_side + fwd * 0.7, key_elev, 1.0), f, 0.09, key_w, (1.0, 0.9, 0.78))
    rim = area("pk_rim", at(fwd - side * key_side * 0.4, 38, 1.2), f, 0.5, rim_w, (0.75, 0.86, 1.0))
    rim.data.shape = "RECTANGLE"
    rim.data.size, rim.data.size_y = 0.9, 0.2
    fill = area("pk_fill", at(-fwd - side * key_side * 0.6, 45, 1.2), f, 1.2, 6, (0.9, 0.95, 1.0))
    fill.visible_glossy = False


def knob_scene():
    scene = base_scene((1920, 1080))
    physicalize(scene)
    floor(-mm(PCB_T) - 0.0001)
    world_gradient(scene, (0.02, 0.023, 0.03), 1.0)
    scene.cycles.samples = max(ARGS["samples"], 96)
    scene.view_settings.exposure = -0.35
    # Up close the glow has to survive the board's sheen: double the emissive parts.
    for mat in bpy.data.materials:
        if mat.node_tree and "Principled BSDF" in mat.node_tree.nodes:
            es = mat.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"]
            if es.default_value > 0 and mat.name != "softbox":
                es.default_value *= 4.0 if mat.name.startswith("enig") else 2.0
    return scene


def shot_knobs_a():
    """~30 degrees down the ECHO row from its left end."""
    scene = knob_scene()
    focus = B(122, 715, mm(10))      # TIME 2
    cam = B(-60, 815, 0.0)
    flat = (Vector(focus) - Vector(cam)).xy.length
    cam = (cam[0], cam[1], focus[2] + flat * math.tan(math.radians(30)))
    perspective(scene, cam, B(205, 712, mm(4)), lens=50, focus=focus, fstop=11)
    knob_rig(scene, cam, B(200, 715, 0.0), key_side=1.0)
    return scene


def shot_knobs_b():
    """~45 degree three-quarter view of the PETAL 1 cluster, labels legible."""
    scene = knob_scene()
    focus = B(150, 150, mm(8))       # between SPREAD and TIMBRE
    tgt = B(165, 178, 0.0)
    flat = 330
    az = math.radians(25)            # swing left of straight-on
    cam_xy = (165 - flat * math.sin(az), 178 + flat * math.cos(az))
    cam = B(*cam_xy, flat * PX * math.tan(math.radians(45)))
    perspective(scene, cam, tgt, lens=40, focus=focus, fstop=11)
    knob_rig(scene, cam, tgt, key_side=-1.0, key_elev=24)
    return scene


def shot_knobs_c():
    """Along PETAL 1's TUNE / SPREAD / TIMBRE row toward the glowing S1 pad."""
    scene = knob_scene()
    focus = B(132, 125, mm(10))      # SPREAD
    cam = B(-15, 285, 0.0)
    flat = (Vector(focus) - Vector(cam)).xy.length
    cam = (cam[0], cam[1], focus[2] + flat * math.tan(math.radians(24)))
    perspective(scene, cam, B(300, 130, -mm(4)), lens=40, focus=focus, fstop=8)
    knob_rig(scene, cam, B(200, 125, 0.0), key_side=1.0, key_elev=18)
    return scene


# ----------------------------------------------------------------------------- wide 3/4 product views

def board_rig(scene):
    """One big soft key from the board's upper left at a moderate height (short, soft
    shadows), a gentle front fill and a subtle back rim. Fill and rim are hidden from
    reflections so the glossy mask doesn't glare over the silkscreen."""
    for n in ("key", "fill", "rim"):
        bpy.data.objects.remove(bpy.data.objects[n], do_unlink=True)
    c = Vector(B(W / 2, H / 2, 0.0))

    def at(dx, dy, elev, dist):
        d = Vector((dx, dy, 0)).normalized() * math.cos(math.radians(elev)) + Vector(
            (0, 0, math.sin(math.radians(elev))))
        return c + d * dist

    area("pv_key", at(-1.0, 0.6, 55, 2.6), c, 1.6, 120, (1.0, 0.96, 0.9))
    fill = area("pv_fill", at(0.3, -1.0, 35, 2.6), c, 2.0, 30, (0.9, 0.95, 1.0))
    fill.visible_glossy = False
    rim = area("pv_rim", at(0.6, 1.0, 22, 2.4), c, 1.5, 35, (0.8, 0.88, 1.0))
    rim.visible_glossy = False
    # The overhead reflection softbox stays (metal reads as metal) but dimmer.
    sb = bpy.data.materials["softbox"].node_tree.nodes["Emission"]
    sb.inputs["Strength"].default_value = 0.2


def dramatic_rig(scene, style):
    """Moodier take on board_rig: a small, low, warm key so every knob throws a long shadow,
    almost no fill, and a cool back rim. 'rake' keys from the side; 'pool' is a tight
    overhead spot that falls off into darkness."""
    for n in ("pv_key", "pv_fill", "pv_rim"):
        bpy.data.objects.remove(bpy.data.objects[n], do_unlink=True)
    c = Vector(B(W / 2, H / 2, 0.0))

    def at(dx, dy, elev, dist):
        d = Vector((dx, dy, 0)).normalized() * math.cos(math.radians(elev)) + Vector(
            (0, 0, math.sin(math.radians(elev))))
        return c + d * dist

    if style == "rake":
        area("dr_key", at(-1.0, 0.35, 16, 2.2), c, 0.35, 260, (1.0, 0.86, 0.68))
    elif style == "soft":
        # Same side as 'rake' but a big window-sized source a bit higher: shadows still fall
        # across the board, with soft penumbras instead of hard spikes.
        # Close enough that the light visibly falls off across the board, like a real window.
        # From the front-left (the camera's side) so its reflection goes away from the lens
        # instead of glaring in the far corner; shadows fall back and to the right.
        key = area("dr_key", at(-1.0, -0.55, 30, 1.2), c, 0.9, 110, (1.0, 0.9, 0.78))
        # A satin solder mask instead of a gloss one, so this big source doesn't glare across it
        # (and the metal still gets a real reflection).
        for node in bpy.data.materials["mask"].node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                node.inputs["Roughness"].default_value = 0.38
                node.inputs["Coat Weight"].default_value = 0.0
            if node.type == "VALTORGB":  # the mottled base colour: a deeper green
                for el in node.color_ramp.elements:
                    el.color = (el.color[0] * 0.6, el.color[1] * 0.7, el.color[2] * 0.6, 1.0)
        # Text pops: brighter, whiter silkscreen; the dim section titles come up too.
        # The board's silk inks (silk_<level>) tint a constant (0.86, 0.85, 0.80) by their tone noise.
        for mat in bpy.data.materials:
            if not mat.name.startswith("silk_") or not mat.node_tree:
                continue
            for node in mat.node_tree.nodes:
                for inp in node.inputs:
                    v = getattr(inp, "default_value", None)
                    if hasattr(v, "__len__") and len(v) == 4 and abs(v[0] - 0.86) < 1e-3 and abs(v[1] - 0.85) < 1e-3:
                        inp.default_value = (0.95, 0.94, 0.90, 1.0)
    else:
        data = bpy.data.lights.new("dr_spot", "SPOT")
        data.energy, data.spot_size, data.spot_blend = 260, math.radians(38), 0.6
        data.shadow_soft_size, data.color = 0.06, (1.0, 0.9, 0.76)
        spot = rb.link(bpy.data.objects.new("dr_spot", data))
        spot.location = at(-0.5, 0.4, 62, 1.5)
        spot.rotation_euler = (c - spot.location).to_track_quat("-Z", "Y").to_euler()
    fill = area("dr_fill", at(0.3, -1.0, 35, 2.6), c, 2.0, 14 if style == "soft" else 4, (0.8, 0.9, 1.0))
    fill.visible_glossy = False
    rim = area("dr_rim", at(0.8, 1.0, 14, 2.0), c, 0.8, 60, (0.65, 0.8, 1.0))
    rim.visible_glossy = False
    # Metal needs something to reflect, or the toggles go black.
    bpy.data.materials["softbox"].node_tree.nodes["Emission"].inputs["Strength"].default_value = (
        0.3 if style == "soft" else 0.05)
    world_gradient(scene, (0.004, 0.005, 0.007), 0.3)
    scene.view_settings.look = "None" if style == "soft" else "AgX - Medium High Contrast"
    scene.view_settings.exposure = -0.3 if style == "soft" else 0.0


def preview_scene():
    scene = base_scene((960, 540))
    physicalize(scene)
    floor(-mm(PCB_T) - 0.0001, color=(0.014, 0.014, 0.016), rough=0.7)
    world_gradient(scene, (0.02, 0.022, 0.026), 1.0)
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = -0.2
    board_rig(scene)
    return scene


def preview_cam(scene, side, flat=1.12, elev_deg=35.0):
    """3/4 view from the lower-left (side=-1) or lower-right (+1) corner, ~35 deg up."""
    tgt = Vector(B(W / 2 + side * 20, H / 2 + 40, 0.0))
    az = math.radians(28) * side
    elev = math.radians(elev_deg)
    cam = tgt + Vector((math.sin(az) * flat, -math.cos(az) * flat, flat * math.tan(elev)))
    focus = Vector(B(W / 2 + side * 200, H / 2 + 180, 0.01))  # the near knob rows
    perspective(scene, cam, tgt, lens=35, focus=focus, fstop=16)


def shot_preview_1():
    scene = preview_scene()
    preview_cam(scene, -1)
    return scene


def shot_preview_2():
    scene = preview_scene()
    preview_cam(scene, 1)
    return scene


def shot_preview_3():
    scene = preview_scene()
    preview_cam(scene, 1, flat=0.98)  # preview-2's angle, a little closer
    return scene


def shot_preview_4():
    scene = preview_scene()
    preview_cam(scene, 1, flat=0.42, elev_deg=50)  # right up against the panel, from higher
    return scene


def shot_preview_5():
    scene = preview_scene()
    preview_cam(scene, 1, flat=0.32, elev_deg=60)
    return scene


def shot_preview_6():
    scene = preview_scene()
    dramatic_rig(scene, "rake")
    preview_cam(scene, 1, flat=0.42, elev_deg=50)
    return scene


def shot_preview_7():
    scene = preview_scene()
    dramatic_rig(scene, "pool")
    preview_cam(scene, 1, flat=0.42, elev_deg=50)
    return scene


def shot_preview_8():
    scene = preview_scene()
    dramatic_rig(scene, "soft")
    preview_cam(scene, 1, flat=0.42, elev_deg=50)
    return scene


SHOTS = {"exploded": shot_exploded, "knobs": shot_knobs, "hero": shot_hero,
         "preview-1": shot_preview_1, "preview-2": shot_preview_2, "preview-3": shot_preview_3, "preview-4": shot_preview_4, "preview-5": shot_preview_5, "preview-6": shot_preview_6, "preview-7": shot_preview_7, "preview-8": shot_preview_8,
         "knobs-a": shot_knobs_a, "knobs-b": shot_knobs_b, "knobs-c": shot_knobs_c}


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    names = ["exploded", "knobs", "hero"] if ARGS["shot"] == "all" else ARGS["shot"].split(",")
    if ARGS["shot"] == "closeups":
        names = ["knobs-a", "knobs-b", "knobs-c"]
    for n in names:
        scene = SHOTS[n]()
        out = os.path.join(OUTDIR, n + ".png")
        scene.render.filepath = out
        bpy.ops.render.render(write_still=True)
        print("WROTE", out)


main()
