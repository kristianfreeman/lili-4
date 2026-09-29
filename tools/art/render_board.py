"""Build the LILI-8 circuit board in Blender from art/board.json and render it.

    blender -b -P tools/art/render_board.py -- --out build/art/style_frame.png [--lit] [--samples 128] [--scale 2]

Vintage green look: glossy green solder mask over raised copper, tinned (HASL)
pads, white silkscreen, blue trimmers and DIP switches, a TO-5 metal can in the
flower's centre. `--lit` adds warm emission to the traces named in the JSON's
"styleFrameLit" block plus a bloom pass, to preview the "playing" state.

Board px map to millimetres (1 px = 1 mm, y flipped); the camera is an
orthographic top-down view that frames the board exactly.
"""

import json
import math
import os
import sys

import bpy
from mathutils import Vector

PX = 0.001  # metres per board px


# ----------------------------------------------------------------------------- args

def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = {"out": "build/art/style_frame.png", "lit": False, "samples": 128, "scale": 2}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--lit":
            args["lit"] = True
        elif a in ("--out", "--samples", "--scale"):
            args[a[2:]] = argv[i + 1]
            i += 1
        i += 1
    args["samples"] = int(args["samples"])
    args["scale"] = int(args["scale"])
    return args


ARGS = parse_args()
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAYOUT = json.load(open(os.path.join(ROOT, "art", "board.json")))
W, H = LAYOUT["size"]


def P(x, y, z=0.0):
    """Board px -> world metres."""
    return Vector((x * PX, -y * PX, z))


# ----------------------------------------------------------------------------- svg path sampling

def tokenize(d):
    out, num = [], ""
    for ch in d:
        if ch.isalpha():
            if num:
                out.append(float(num))
                num = ""
            out.append(ch)
        elif ch in " ,":
            if num:
                out.append(float(num))
                num = ""
        elif ch == "-" and num and num[-1] not in "eE":
            out.append(float(num))
            num = ch
        else:
            num += ch
    if num:
        out.append(float(num))
    return out


def sample_path(d, steps=20):
    """Returns a list of (points, closed) polylines. Absolute commands only."""
    toks = tokenize(d)
    polys, cur, pos, start, i, cmd = [], [], (0.0, 0.0), (0.0, 0.0), 0, None
    closed = False

    def take(n):
        nonlocal i
        vals = toks[i:i + n]
        i += n
        return vals

    while i < len(toks):
        if isinstance(toks[i], str):
            cmd = toks[i]
            i += 1
        if cmd == "M":
            if len(cur) > 1:
                polys.append((cur, closed))
            x, y = take(2)
            pos = start = (x, y)
            cur, closed = [pos], False
            cmd = "L"
        elif cmd == "L":
            pos = tuple(take(2))
            cur.append(pos)
        elif cmd == "H":
            pos = (take(1)[0], pos[1])
            cur.append(pos)
        elif cmd == "V":
            pos = (pos[0], take(1)[0])
            cur.append(pos)
        elif cmd == "Q":
            cx, cy, x, y = take(4)
            x0, y0 = pos
            for s in range(1, steps + 1):
                t = s / steps
                u = 1 - t
                cur.append((u * u * x0 + 2 * u * t * cx + t * t * x, u * u * y0 + 2 * u * t * cy + t * t * y))
            pos = (x, y)
        elif cmd == "C":
            c1x, c1y, c2x, c2y, x, y = take(6)
            x0, y0 = pos
            for s in range(1, steps + 1):
                t = s / steps
                u = 1 - t
                cur.append((u ** 3 * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t ** 3 * x,
                            u ** 3 * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t ** 3 * y))
            pos = (x, y)
        elif cmd == "Z":
            closed = True
            pos = start
        else:
            raise ValueError("unsupported path command " + str(cmd))
    if len(cur) > 1:
        polys.append((cur, closed))
    return polys


# ----------------------------------------------------------------------------- scene helpers

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "METAL"
    prefs.get_devices()
    for dev in prefs.devices:
        dev.use = dev.type == "METAL"
    scene.cycles.device = "GPU"
    scene.cycles.samples = ARGS["samples"]
    scene.cycles.use_denoising = True
    scene.render.resolution_x = W * ARGS["scale"]
    scene.render.resolution_y = H * ARGS["scale"]
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    return scene


def material(name, color, rough=0.5, metal=0.0, coat=0.0, coat_rough=0.1, emit=None, emit_strength=0.0,
             bump=0.0, bump_scale=300.0, transmission=0.0, sss=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    bsdf.inputs["Coat Weight"].default_value = coat
    bsdf.inputs["Coat Roughness"].default_value = coat_rough
    bsdf.inputs["Transmission Weight"].default_value = transmission
    bsdf.inputs["Subsurface Weight"].default_value = sss
    if emit is not None:
        bsdf.inputs["Emission Color"].default_value = (*emit, 1.0)
        bsdf.inputs["Emission Strength"].default_value = emit_strength
    if bump > 0:
        noise = nt.nodes.new("ShaderNodeTexNoise")
        noise.inputs["Scale"].default_value = bump_scale
        noise.inputs["Detail"].default_value = 6.0
        bmp = nt.nodes.new("ShaderNodeBump")
        bmp.inputs["Strength"].default_value = bump
        bmp.inputs["Distance"].default_value = 0.0002
        nt.links.new(noise.outputs["Fac"], bmp.inputs["Height"])
        nt.links.new(bmp.outputs["Normal"], bsdf.inputs["Normal"])
    return m


def mottled_mask(name, color_a, color_b):
    """Solder mask: glossy coat, with the slight uneven tone of old boards."""
    m = material(name, color_a, rough=0.5, coat=0.5, coat_rough=0.18, bump=0.04, bump_scale=900)
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 6.0
    noise.inputs["Detail"].default_value = 3.0
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (*color_a, 1)
    ramp.color_ramp.elements[1].color = (*color_b, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    return m


def link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def poly_curve(name, polys, width_px, mat, z=0.0, flatten=0.3, cyclic_ok=True):
    """Open/closed polylines as a flattened tube (a raised trace)."""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = width_px * PX / 2
    cu.bevel_resolution = 3
    cu.use_fill_caps = True
    for pts, closed in polys:
        sp = cu.splines.new("POLY")
        sp.points.add(len(pts) - 1)
        for k, (x, y) in enumerate(pts):
            sp.points[k].co = (x * PX, -y * PX, 0.0, 1.0)
        sp.use_cyclic_u = closed and cyclic_ok
    obj = link(bpy.data.objects.new(name, cu))
    obj.data.materials.append(mat)
    obj.scale = (1, 1, flatten)
    obj.location.z = z
    return obj


def box(name, x, y, w, h, depth, mat, z=0.0, bevel=0.3):
    """Axis-aligned block from a board-px rect, sitting on z."""
    bpy.ops.mesh.primitive_cube_add(size=1)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = (w * PX, h * PX, depth)
    obj.location = (x * PX + w * PX / 2, -(y * PX + h * PX / 2), z + depth / 2)
    # Scale only: the defaults also bake location, moving the pivot to the world origin.
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel > 0:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = bevel * PX
        mod.segments = 3
    obj.data.materials.append(mat)
    return obj


def cylinder(name, cx, cy, r_px, depth, mat, z=0.0, verts=48, bevel_px=0.0):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r_px * PX, depth=depth)
    obj = bpy.context.active_object
    obj.name = name
    obj.location = (cx * PX, -cy * PX, z + depth / 2)
    obj.data.materials.append(mat)
    if bevel_px > 0:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = min(bevel_px * PX, depth * 0.95)
        mod.segments = 5
        mod.limit_method = "ANGLE"
    # Smooth sides, flat caps (plain shade_smooth domes the top face).
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40))
    return obj


FONTS = {}


def font(path):
    if path not in FONTS:
        FONTS[path] = bpy.data.fonts.load(path) if os.path.exists(path) else None
    return FONTS[path]


MONO = "/System/Library/Fonts/Menlo.ttc"
SERIF = "/System/Library/Fonts/Supplemental/Georgia.ttf"


def text(body, x, y, size_px, mat, align="left", font_path=MONO, z=0.00045, spacing=1.12):
    cu = bpy.data.curves.new("txt", "FONT")
    cu.body = body
    f = font(font_path)
    if f:
        cu.font = f
    cu.size = size_px * PX * 1.3
    cu.space_character = spacing
    cu.align_x = {"left": "LEFT", "center": "CENTER", "right": "RIGHT"}[align]
    cu.extrude = 0.00003
    obj = link(bpy.data.objects.new("txt_" + body[:12], cu))
    obj.location = (x * PX, -y * PX, z)
    obj.data.materials.append(mat)
    return obj


# ----------------------------------------------------------------------------- build

def build(scene):
    lit = ARGS["lit"]
    lit_spec = LAYOUT.get("styleFrameLit", {}) if lit else {}

    # Materials ---------------------------------------------------------------
    mask = mottled_mask("mask", (0.030, 0.150, 0.072), (0.042, 0.178, 0.085))
    trace = material("trace", (0.075, 0.300, 0.130), rough=0.35, coat=0.8, coat_rough=0.06)
    bottom = material("trace_bottom", (0.040, 0.130, 0.068), rough=0.5, coat=0.5)
    # Amber, and modest: hot emission clips to white under AgX.
    glow_col = (1.0, 0.48, 0.12)
    trace_lit = material("trace_lit", (0.30, 0.42, 0.18), rough=0.3, coat=0.8, emit=glow_col, emit_strength=2.2)
    tin = material("tin", (0.72, 0.72, 0.70), rough=0.3, metal=1.0, bump=0.06, bump_scale=700)
    silk = material("silk", (0.86, 0.85, 0.80), rough=0.75)
    silk_dim = material("silk_dim", (0.55, 0.60, 0.55), rough=0.8)
    blue = material("trimmer_blue", (0.035, 0.12, 0.42), rough=0.32, coat=0.4)
    brass = material("brass", (0.85, 0.62, 0.32), rough=0.28, metal=1.0)
    rotor_white = material("rotor_white", (0.82, 0.80, 0.74), rough=0.4)
    black = material("black_plastic", (0.018, 0.018, 0.02), rough=0.45)
    epoxy = material("epoxy", (0.025, 0.025, 0.027), rough=0.6, bump=0.15, bump_scale=500)
    chrome = material("chrome", (0.9, 0.9, 0.92), rough=0.12, metal=1.0)
    gold = material("gold", (1.0, 0.76, 0.35), rough=0.2, metal=1.0)
    dip_white = material("dip_white", (0.93, 0.92, 0.88), rough=0.35)
    hole = material("hole", (0.004, 0.004, 0.004), rough=1.0)
    led_off = material("led", (0.55, 0.06, 0.12), rough=0.1, transmission=0.6)
    led_on = material("led_on", (1.0, 0.2, 0.32), rough=0.1, emit=(1.0, 0.12, 0.28), emit_strength=4.0)
    smd_pink = material("smd_led", (0.75, 0.30, 0.38), rough=0.25)
    smd_pink_on = material("smd_led_on", (1.0, 0.3, 0.45), emit=(1.0, 0.18, 0.4), emit_strength=3.0)

    # Board -------------------------------------------------------------------
    r = LAYOUT["cornerRadius"]
    corners = []
    for cx, cy, a0 in ((W - r, r, -90), (W - r, H - r, 0), (r, H - r, 90), (r, r, 180)):
        for s in range(9):
            a = math.radians(a0 + s * 90 / 8)
            corners.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    cu = bpy.data.curves.new("board", "CURVE")
    cu.dimensions = "2D"
    cu.fill_mode = "BOTH"
    cu.extrude = 0.0008
    sp = cu.splines.new("POLY")
    sp.points.add(len(corners) - 1)
    for k, (x, y) in enumerate(corners):
        sp.points[k].co = (x * PX, -y * PX, 0, 1)
    sp.use_cyclic_u = True
    board = link(bpy.data.objects.new("board", cu))
    board.location.z = -0.0008
    board.data.materials.append(mask)

    # Copper under the mask ------------------------------------------------------
    for idx, d in enumerate(LAYOUT["petals"]):
        poly_curve(f"petal{idx}", sample_path(d), 2.4,
                   trace_lit if idx in lit_spec.get("petals", []) else trace)
    for idx, d in enumerate(LAYOUT["ribs"]):
        poly_curve(f"rib{idx}", sample_path(d), 1.5,
                   trace_lit if idx in lit_spec.get("petals", []) else trace)
    for d in LAYOUT["traces"]:
        poly_curve("trace", sample_path(d), 2.2, trace)
    poly_curve("stem", sample_path(LAYOUT["stem"]), 3.2, trace_lit if lit_spec.get("stem") else trace)
    for idx, d in enumerate(LAYOUT["meanders"]):
        poly_curve(f"meander{idx}", sample_path(d), 1.7,
                   trace_lit if idx in lit_spec.get("meanders", []) else trace)
    for d in LAYOUT["leaves"]:
        poly_curve("leaf", sample_path(d), 2.2, trace)
    for d in LAYOUT["leafRibs"]:
        poly_curve("leafrib", sample_path(d), 1.2, trace)
    # Bottom-layer routes: seen dimly through the board.
    for idx, d in enumerate(LAYOUT["xmod"]):
        poly_curve(f"xmod{idx}", sample_path(d), 1.4,
                   trace_lit if idx in lit_spec.get("xmod", []) else bottom, flatten=0.1)
    poly_curve("totalfb", sample_path(LAYOUT["totalFb"]), 1.4, bottom, flatten=0.1)

    # Exposed metal ---------------------------------------------------------------
    for i, (x, y) in enumerate(LAYOUT["pads"]):
        # HASL pads: a slightly domed solder coat, rounded at the edge
        cylinder(f"pad{i}", x, y, LAYOUT["padRadius"], 0.0009, tin, verts=96, bevel_px=3)
        # silkscreen ring + label, offset outward from the flower centre
        ring = bpy.data.curves.new("padring", "CURVE")
        ring.dimensions = "3D"
        ring.bevel_depth = 0.6 * PX
        spr = ring.splines.new("POLY")
        spr.points.add(63)
        for k in range(64):
            a = 2 * math.pi * k / 64
            spr.points[k].co = ((x + 25 * math.cos(a)) * PX, -(y + 25 * math.sin(a)) * PX, 0.0003, 1)
        spr.use_cyclic_u = True
        link(bpy.data.objects.new("padring", ring)).data.materials.append(silk)
        dx, dy = x - 560, y - 300
        n = math.hypot(dx, dy)
        text(f"S{i + 1}", x + dx / n * 38, y + dy / n * 38 + 4, 10, silk, "center")
    for x, y in LAYOUT["vias"]:
        cylinder("via", x, y, 5, 0.00035, tin)
        cylinder("via_hole", x, y, 2.2, 0.0005, hole)
    for x, y in LAYOUT["holes"]:
        cylinder("mount_ring", x, y, 13, 0.0004, tin)
        cylinder("mount_hole", x, y, 8, 0.0006, hole)

    # Silkscreen ------------------------------------------------------------------
    for (x, y, w, h) in LAYOUT["boxes"]:
        pts = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
        poly_curve("silkbox", [(pts, True)], 1.0, silk_dim, z=0.00035, flatten=0.2)
    for (x, y, body, align, strong) in LAYOUT["labels"]:
        text(body, x, y, 9, silk if strong else silk_dim, align)
    lx, ly, logo = LAYOUT["logo"]
    text(logo, lx, ly, 34, silk, "left", SERIF, spacing=1.0)

    # Trimmers (Bourns 3386-style: blue body, white rotor with a slot) -----------
    for (pid, x, y, label, value) in LAYOUT["knobs"]:
        cx, cy = x + 26, y + 17
        box(f"trim_{pid}", cx - 17, cy - 17, 34, 34, 0.0048, blue, bevel=1.6)
        rot = cylinder(f"rotor_{pid}", cx, cy, 12.5, 0.0012, rotor_white, z=0.0048)
        slot = box(f"slot_{pid}", cx - 9, cy - 1.6, 18, 3.2, 0.0006, black, z=0.0056, bevel=0.4)
        angle = math.radians(-(-135 + 270 * value))
        slot.select_set(True)
        bpy.context.view_layer.objects.active = slot
        bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY")
        slot.rotation_euler.z = angle
        # three legs peeking out (vintage through-hole)
        for k in (-1, 0, 1):
            cylinder("leg", cx + k * 9, cy + 20, 1.5, 0.0004, tin)
        text(label, cx, cy + 35, 9, silk, "center")

    # Jumpers: pin headers, a black cap on the selected position -----------------
    pitch = LAYOUT.get("jumperPitch", 40)
    for (pid, x, y, title, labels, sel) in LAYOUT["jumpers"]:
        text(title, x, y + 8, 9, silk_dim, "left")
        for j, lab in enumerate(labels):
            px_, py_ = x + 17 + pitch * j, y + 26
            box("hdr_base", px_ - 7, py_ - 7, 14, 14, 0.0025, black, bevel=0.8)
            box("hdr_pin", px_ - 2, py_ - 2, 4, 4, 0.006, gold, bevel=0.3)
            if j == sel:
                box("jumper_cap", px_ - 8, py_ - 9, 16, 18, 0.0075, black, bevel=1.5)
            text(lab, px_, py_ + 22, 9, silk, "center")

    # DIP switches --------------------------------------------------------------------
    for (x, y, title, items) in LAYOUT["dips"]:
        text(title, x, y + 8, 9, silk_dim, "left")
        n = len(items)
        bw = 12 + 30 * n + 6 * (n - 1)
        box("dip_body", x, y + 16, bw, 50, 0.004, blue, bevel=1.2)
        for j, (pid, lab, on) in enumerate(items):
            sx = x + 6 + 15 + 36 * j
            box("dip_well", sx - 7, y + 22, 14, 26, 0.0005, black, z=0.004, bevel=0.3)
            ly_ = y + 24 if on else y + 36
            box("dip_lever", sx - 5, ly_, 10, 10, 0.0012, dip_white, z=0.004, bevel=0.6)
            text(lab, sx, y + 60, 8, silk, "center", z=0.0041)

    # Parts in the flower and the stem -------------------------------------------------
    ux, uy = LAYOUT["u1"]
    cylinder("u1_flange", ux, uy, 19, 0.0006, chrome, verts=96)
    cylinder("u1_can", ux, uy, 16.5, 0.0055, chrome, z=0.0006, verts=96, bevel_px=2.5)
    box("u1_tab", ux + 13, uy + 11, 7, 4, 0.0006, chrome, bevel=0.3).rotation_euler.z = math.radians(-45)
    for k, (x, y) in enumerate(LAYOUT["stamenLeds"]):
        box("stamen_led", x - 4, y - 6, 8, 12, 0.0012,
            smd_pink_on if k in lit_spec.get("stamenLeds", []) else smd_pink, bevel=0.6)
    x, y, w, h = LAYOUT["u2"]
    box("u2", x, y, w, h, 0.0035, epoxy, bevel=0.8)
    for k in range(4):
        for side in (-1, 1):
            box("u2_leg", x + 4 + k * 8.5, y + (h if side > 0 else -3), 3, 3, 0.001, tin, bevel=0.2)
    cylinder("u2_dot", x + 5, y + 5, 1.6, 0.0036, material("dot", (0.05, 0.05, 0.05), rough=0.9))
    jx, jy = LAYOUT["jack"]
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=22 * PX, depth=0.003)
    nut = bpy.context.active_object
    nut.location = (jx * PX, -jy * PX, 0.0015)
    nut.data.materials.append(chrome)
    cylinder("jack_barrel", jx, jy, 14, 0.006, chrome, z=0.003)
    cylinder("jack_hole", jx, jy, 8, 0.0065, hole, z=0.003)
    for k, (x, y) in enumerate(LAYOUT["leds"]):
        on = k in lit_spec.get("leds", [])
        m = led_on if on else led_off
        cylinder("led_rim", x, y, 7, 0.001, m)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=6 * PX, location=(x * PX, -y * PX, 0.001))
        dome = bpy.context.active_object
        dome.data.materials.append(m)
        bpy.ops.object.shade_smooth()


def lights_and_camera(scene, w=None, h=None, tilt_deg=0.0):
    w = W if w is None else w
    h = H if h is None else h
    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.02, 0.022, 0.025, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.25
    scene.world = world

    def area(name, loc, size, energy, color=(1, 1, 1)):
        data = bpy.data.lights.new(name, "AREA")
        data.shape = "DISK"
        data.size = size
        data.energy = energy
        data.color = color
        obj = link(bpy.data.objects.new(name, data))
        obj.location = loc
        direction = Vector((w * PX / 2, -h * PX / 2, 0)) - Vector(loc)
        obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        return obj

    # Soft key from upper left, cool fill from the right, low warm rim for relief.
    # Kept off-axis so the glossy mask shows a gradient sheen, not a mirror.
    area("key", (-0.7, 0.55, 0.9), 1.0, 55, (1.0, 0.96, 0.9))
    area("fill", (1.9, -0.6, 0.7), 1.0, 14, (0.85, 0.92, 1.0))
    area("rim", (0.56, -1.6, 0.25), 0.6, 9, (1.0, 0.85, 0.7))

    # A big dim softbox overhead that only reflections see, so polished metal
    # (U1 can, jack, tinned pads) reads as metal instead of reflecting black.
    bpy.ops.mesh.primitive_plane_add(size=3.0, location=(w * PX / 2, -h * PX / 2, 1.6))
    softbox = bpy.context.active_object
    softbox.name = "softbox"
    sb_mat = bpy.data.materials.new("softbox")
    sb_mat.use_nodes = True
    nt = sb_mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Strength"].default_value = 0.5
    grad = nt.nodes.new("ShaderNodeTexGradient")
    grad.gradient_type = "SPHERICAL"
    coord = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(coord.outputs["Object"], grad.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.05, 0.05, 0.06, 1)
    ramp.color_ramp.elements[1].color = (1.0, 0.98, 0.95, 1)
    nt.links.new(grad.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], emit.inputs["Color"])
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    softbox.data.materials.append(sb_mat)
    softbox.visible_camera = False
    softbox.visible_diffuse = False
    softbox.visible_shadow = False

    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = w * PX
    cam = link(bpy.data.objects.new("cam", cam_data))
    centre = Vector((w * PX / 2, -h * PX / 2, 0.0))
    if tilt_deg:
        # Preview only: tip the view back so part heights show.
        t = math.radians(tilt_deg)
        cam.rotation_euler = (t, 0.0, 0.0)
        cam.location = centre + Vector((0.0, -math.sin(t), math.cos(t)))
    else:
        cam.location = centre + Vector((0.0, 0.0, 1.0))
    scene.camera = cam


def compositor(scene):
    scene.use_nodes = True
    nt = scene.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    rl = nt.nodes.new("CompositorNodeRLayers")
    out = nt.nodes.new("CompositorNodeComposite")
    src = rl.outputs["Image"]
    if ARGS["lit"]:
        glare = nt.nodes.new("CompositorNodeGlare")
        glare.glare_type = "BLOOM"
        glare.quality = "HIGH"
        glare.threshold = 0.9
        glare.size = 8
        glare.mix = 0.15
        nt.links.new(src, glare.inputs["Image"])
        src = glare.outputs["Image"]
    # a whisper of vignette
    ellipse = nt.nodes.new("CompositorNodeEllipseMask")
    ellipse.width, ellipse.height = 1.25, 1.25
    blur = nt.nodes.new("CompositorNodeBlur")
    blur.size_x = blur.size_y = 400
    mix = nt.nodes.new("CompositorNodeMixRGB")
    mix.blend_type = "MULTIPLY"
    mix.inputs["Fac"].default_value = 1.0
    ramp = nt.nodes.new("CompositorNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.72, 0.72, 0.72, 1)
    nt.links.new(ellipse.outputs["Mask"], blur.inputs["Image"])
    nt.links.new(blur.outputs["Image"], ramp.inputs["Fac"])
    nt.links.new(src, mix.inputs[1])
    nt.links.new(ramp.outputs["Image"], mix.inputs[2])
    nt.links.new(mix.outputs["Image"], out.inputs["Image"])


def probe(x, y, radius=30):
    """Debug: list objects whose bounds come within `radius` px of (x, y)."""
    target = Vector((x * PX, -y * PX))
    for obj in bpy.context.scene.objects:
        corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
        cx = sum(c.x for c in corners) / 8
        cy = sum(c.y for c in corners) / 8
        if (Vector((cx, cy)) - target).length < radius * PX:
            print("PROBE", obj.name, round(cx / PX, 1), round(-cy / PX, 1))


def main():
    scene = reset_scene()
    build(scene)
    if "--probe" in sys.argv:
        i = sys.argv.index("--probe")
        probe(float(sys.argv[i + 1]), float(sys.argv[i + 2]))
        return
    lights_and_camera(scene)
    compositor(scene)
    out = os.path.join(ROOT, ARGS["out"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print("WROTE", out)


if __name__ == "__main__":  # also imported as a helper library by render_parts.py
    main()
