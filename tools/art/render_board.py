"""Build the LILI-4 circuit board in Blender from art/board.json and render it.

    blender -b -P tools/art/render_board.py -- --out build/art/board.png [--lit] [--samples 128] [--scale 2]

Vintage green look: glossy green solder mask over raised copper (the flower,
green on green), gold touch pads, off-white mono silkscreen with reversed-out
section tabs, cream knobs and bat toggles, a TO-5 metal can in the flower's centre. `--lit` adds warm emission to the traces named in the JSON's
"litPreview" block plus a bloom pass, to preview the "playing" state.

Board px map to millimetres (1 px = 1 mm, y flipped); the camera is an
orthographic top-down view that frames the board exactly.
"""

import json
import math
import os
import shutil
import sys

import bpy
from mathutils import Vector

PX = 0.001  # metres per board px


# ----------------------------------------------------------------------------- args

def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    args = {"out": "build/art/board.png", "lit": False, "bake": False, "no_knobs": False, "samples": 128,
            "scale": 2, "tilt": 0.0}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--lit":
            args["lit"] = True
        elif a == "--bake":
            args["bake"] = True
        elif a in ("--no-controls", "--no-knobs"):
            # knob and toggle bodies come from sprite strips at runtime; labels/scales stay
            args["no_knobs"] = True
        elif a in ("--out", "--samples", "--scale", "--tilt"):
            args[a[2:]] = argv[i + 1]
            i += 1
        i += 1
    args["samples"] = int(args["samples"])
    args["scale"] = int(args["scale"])
    args["tilt"] = float(args["tilt"])
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


# ----------------------------------------------------------------------------- surface texture
# Procedural, and deliberately quiet: legibility of the silkscreen wins. Noise
# scales are per metre, and 1 board px = 1 mm, so scale 100 ~ 10 px features.

def _link(nt, a, b):
    nt.links.new(a, b)


def _coords(nt, space="world", stretch=None):
    """World position (shared by every object, so separate parts read as one board) or object space."""
    if space == "world":
        out = nt.nodes.new("ShaderNodeNewGeometry").outputs["Position"]
    else:
        out = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    if stretch:
        mp = nt.nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = stretch
        _link(nt, out, mp.inputs["Vector"])
        out = mp.outputs["Vector"]
    return out


def _noise(nt, vec, scale, detail=2.0, rough=0.5):
    n = nt.nodes.new("ShaderNodeTexNoise")
    n.inputs["Scale"].default_value = scale
    n.inputs["Detail"].default_value = detail
    n.inputs["Roughness"].default_value = rough
    _link(nt, vec, n.inputs["Vector"])
    return n.outputs["Fac"]


def _map(nt, fac, lo_in, hi_in, lo_out, hi_out):
    n = nt.nodes.new("ShaderNodeMapRange")
    n.clamp = True
    for k, v in (("From Min", lo_in), ("From Max", hi_in), ("To Min", lo_out), ("To Max", hi_out)):
        n.inputs[k].default_value = v
    _link(nt, fac, n.inputs["Value"])
    return n.outputs["Result"]


def _math(nt, op, a, b):
    n = nt.nodes.new("ShaderNodeMath")
    n.operation = op
    for i, v in enumerate((a, b)):
        if isinstance(v, (int, float)):
            n.inputs[i].default_value = v
        else:
            _link(nt, v, n.inputs[i])
    return n.outputs[0]


def _scale_color(nt, color, fac):
    """color * fac (fac a float socket), as an RGB socket."""
    n = nt.nodes.new("ShaderNodeVectorMath")
    n.operation = "SCALE"
    if isinstance(color, tuple):
        n.inputs[0].default_value = color
    else:
        _link(nt, color, n.inputs[0])
    _link(nt, fac, n.inputs["Scale"])
    return n.outputs["Vector"]


def _mix_color(nt, fac, a, b):
    n = nt.nodes.new("ShaderNodeMix")
    n.data_type = "RGBA"
    for sock, v in ((n.inputs[6], a), (n.inputs[7], b)):
        if isinstance(v, tuple):
            sock.default_value = (*v, 1.0)
        else:
            _link(nt, v, sock)
    if isinstance(fac, (int, float)):
        n.inputs[0].default_value = fac
    else:
        _link(nt, fac, n.inputs[0])
    return n.outputs[2]


def solder_mask(name, color_a, color_b):
    """Glossy green mask: slow mottling, a faint fibreglass weave and a fine grain."""
    m = material(name, color_a, rough=0.5, coat=0.5, coat_rough=0.18, bump=0.04, bump_scale=900)
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    pos = _coords(nt)
    mottle = _map(nt, _noise(nt, pos, 7.0, detail=4.0), 0.3, 0.7, 0.0, 1.0)
    col = _mix_color(nt, mottle, color_a, color_b)
    # Weave: 8 px basket of 2 px bundles, warp/weft alternating per 4 px cell.
    checker = nt.nodes.new("ShaderNodeTexChecker")
    checker.inputs["Scale"].default_value = 250.0
    checker.inputs["Color1"].default_value = (1, 1, 1, 1)
    checker.inputs["Color2"].default_value = (0, 0, 0, 1)
    _link(nt, pos, checker.inputs["Vector"])
    bands = []
    for axis in ("X", "Y"):
        w = nt.nodes.new("ShaderNodeTexWave")
        w.wave_type = "BANDS"
        w.bands_direction = axis
        w.inputs["Scale"].default_value = 78.5  # 4 px period
        _link(nt, pos, w.inputs["Vector"])
        bands.append(w.outputs["Fac"])
    weave = nt.nodes.new("ShaderNodeMix")
    weave.data_type = "FLOAT"
    _link(nt, checker.outputs["Fac"], weave.inputs[0])
    _link(nt, bands[0], weave.inputs[2])
    _link(nt, bands[1], weave.inputs[3])
    weave_f = _map(nt, weave.outputs[0], 0.0, 1.0, 0.975, 1.025)
    grain_f = _map(nt, _noise(nt, pos, 700.0, detail=1.0), 0.3, 0.7, 0.97, 1.03)
    col = _scale_color(nt, col, _math(nt, "MULTIPLY", weave_f, grain_f))
    _link(nt, col, bsdf.inputs["Base Color"])
    _link(nt, _map(nt, mottle, 0.0, 1.0, 0.44, 0.56), bsdf.inputs["Roughness"])
    return m


mottled_mask = solder_mask  # old name


_INK = {}


def silk_ink(ink=1.0):
    """Silkscreen ink: slightly uneven coverage and the odd pinhole; `ink` < 1 dims a whole level."""
    key = round(ink, 3)
    if key in _INK:
        return _INK[key]
    m = material(f"silk_{key}", (0.86, 0.85, 0.80), rough=0.75)
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    out = nt.nodes["Material Output"]
    pos = _coords(nt)
    # Uneven coverage shows as tone, not transparency, so full-strength ink still
    # hides the glowing copper under it; only pinholes (and dimmer levels) let it through.
    cover = _map(nt, _noise(nt, pos, 50.0, detail=2.0), 0.3, 0.7, 0.92, 1.0)
    pin = _map(nt, _noise(nt, pos, 900.0, detail=0.0), 0.77, 0.80, 1.0, 0.0)
    fac = _math(nt, "MULTIPLY", pin, ink)
    tone = _math(nt, "MULTIPLY", cover, _map(nt, _noise(nt, pos, 120.0), 0.3, 0.7, 0.97, 1.02))
    _link(nt, _scale_color(nt, (0.86, 0.85, 0.80), tone), bsdf.inputs["Base Color"])
    clear = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix = nt.nodes.new("ShaderNodeMixShader")
    _link(nt, fac, mix.inputs["Fac"])
    _link(nt, clear.outputs["BSDF"], mix.inputs[1])
    _link(nt, bsdf.outputs["BSDF"], mix.inputs[2])
    _link(nt, mix.outputs["Shader"], out.inputs["Surface"])
    _INK[key] = m
    return m


def brushed_gold(name, emit=None, emit_strength=0.0, metal=1.0):
    """ENIG gold: a fine horizontal brush grain and a little tarnish."""
    gold_col = (1.0, 0.74, 0.34)
    m = material(name, gold_col, rough=0.26, metal=metal, emit=emit, emit_strength=emit_strength)
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    brush = _noise(nt, _coords(nt, stretch=(0.05, 1.0, 1.0)), 1500.0, detail=3.0)
    tarnish = _map(nt, _noise(nt, _coords(nt), 30.0, detail=3.0), 0.30, 0.42, 0.22, 0.0)
    col = _scale_color(nt, gold_col, _map(nt, brush, 0.3, 0.7, 0.88, 1.0))
    _link(nt, _mix_color(nt, tarnish, col, (0.40, 0.27, 0.15)), bsdf.inputs["Base Color"])
    _link(nt, _math(nt, "ADD", _map(nt, brush, 0.3, 0.7, 0.18, 0.30), _math(nt, "MULTIPLY", tarnish, 0.5)),
          bsdf.inputs["Roughness"])
    return m


def plastic_grain(m, color, scale=2500.0):
    """Matte moulded-plastic grain on a part material (object space, so it turns with the part)."""
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    vec = _coords(nt, "object")
    g = _noise(nt, vec, scale, detail=1.0)
    _link(nt, _scale_color(nt, color, _map(nt, g, 0.3, 0.7, 0.975, 1.02)), bsdf.inputs["Base Color"])
    _link(nt, _map(nt, _noise(nt, vec, scale / 6), 0.3, 0.7, 0.34, 0.46), bsdf.inputs["Roughness"])
    bmp = nt.nodes.new("ShaderNodeBump")
    bmp.inputs["Strength"].default_value = 0.08
    bmp.inputs["Distance"].default_value = 0.00005
    _link(nt, g, bmp.inputs["Height"])
    _link(nt, bmp.outputs["Normal"], bsdf.inputs["Normal"])
    return m


def brushed_metal(name, color, rough_lo, rough_hi):
    """Brushed nickel/steel for the toggles and jack: grain along object x."""
    m = material(name, color, rough=rough_lo, metal=1.0)
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    brush = _noise(nt, _coords(nt, "object", stretch=(0.04, 1.0, 1.0)), 2500.0, detail=3.0)
    _link(nt, _scale_color(nt, color, _map(nt, brush, 0.3, 0.7, 0.88, 1.0)), bsdf.inputs["Base Color"])
    _link(nt, _map(nt, brush, 0.3, 0.7, rough_lo, rough_hi), bsdf.inputs["Roughness"])
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


# Vendored OFL fonts (licences alongside), safe to bake into shipped art.
MONO = os.path.join(ROOT, "art", "fonts", "IBMPlexMono-Medium.ttf")
SERIF = os.path.join(ROOT, "art", "fonts", "InstrumentSerif-Regular.ttf")


# Blender's font size maps to 0.675 em for Plex Mono (measured), and
# space_character adds (s - 1) * 0.5 * size per glyph: these turn the layout's
# em sizes and em tracking into Blender units, so text matches the layout's metrics.
EM_PER_SIZE = 0.675
MONO_ADVANCE = 0.6  # Plex Mono advance, em


def text(body, x, y, size_px, mat, align="left", font_path=MONO, z=0.00045, tracking=0.0):
    """Text on a baseline at (x, y); size_px is the em size, tracking in em."""
    cu = bpy.data.curves.new("txt", "FONT")
    cu.body = body
    f = font(font_path)
    if f:
        cu.font = f
    cu.size = size_px / EM_PER_SIZE * PX
    cu.space_character = 1.0 + tracking * EM_PER_SIZE / 0.5
    cu.align_x = {"left": "LEFT", "center": "CENTER", "right": "RIGHT"}[align]
    cu.extrude = 0.00003
    obj = link(bpy.data.objects.new("txt_" + body[:12], cu))
    obj.location = (x * PX, -y * PX, z)
    obj.data.materials.append(mat)
    return obj


def type_level(name):
    """A typographic level from board.json: {"size", "tracking", ["ink"]}."""
    return LAYOUT["type"][name]


def measure(body, level):
    """Advance width of mono text at a type level, in board px (as the layout mockup measures it)."""
    t = type_level(level) if isinstance(level, str) else level
    n = len(body)
    return n * MONO_ADVANCE * t["size"] + t["tracking"] * t["size"] * max(0, n - 1)


def flat_poly(name, pts, mat, z=0.0003, thick=0.00004):
    """A filled flat shape (silk fills, tabs, the logo block) from a closed board-px polygon."""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "2D"
    cu.fill_mode = "BOTH"
    cu.extrude = thick / 2
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for k, (x, y) in enumerate(pts):
        sp.points[k].co = (x * PX, -y * PX, 0, 1)
    sp.use_cyclic_u = True
    obj = link(bpy.data.objects.new(name, cu))
    obj.location.z = z + thick / 2
    obj.data.materials.append(mat)
    return obj


def to_mesh(obj):
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.convert(target="MESH")
    return bpy.context.view_layer.objects.active


def knockout(block, cutters):
    """Cut text out of a silk fill (a real gap in the ink: the board's own mask shows through)."""
    block = to_mesh(block)
    for c in cutters:
        c.data.extrude = 0.0003
        c.location.z = block.location.z
        c = to_mesh(c)
        mod = block.modifiers.new("cut", "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.solver = "EXACT"
        mod.object = c
        bpy.context.view_layer.objects.active = block
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(c, do_unlink=True)
    return block


def rounded_rect(x, y, w, h, r, seg=6):
    if r <= 0:
        return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    pts = []
    for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for s in range(seg + 1):
            a = math.radians(a0 + s * 90 / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


# ----------------------------------------------------------------------------- build

LIGHTGROUPS = []  # bake mode: one Cycles light group per animatable element
BAKE_EXPOSURE_EV = -2.0  # headroom for bright emission in 16-bit PNG layers
OUTMODE_TEXTS = []  # (state index, text object) for the clickable output-mode silkscreen
TOGGLE_S = 1.35 * 0.88  # toggle model scale (parts.toggle_switch), knobs-and-toggles at 88%


def silk_line(polys, ink, width=0.8, z=0.00035):
    return poly_curve("silk", polys, width, silk_ink(ink), z=z, flatten=0.2)


def label(body, x, y, level, align="center", mat=None):
    t = type_level(level)
    return text(body, x, y, t["size"], mat or silk_ink(t.get("ink", 1.0)), align, tracking=t["tracking"])


def build(scene):
    lit = ARGS["lit"]
    lit_spec = LAYOUT.get("litPreview", {}) if lit else {}
    bake = ARGS["bake"]
    if bake:
        # Everything that can light up is emissive, each in its own light group.
        lit_spec = {"petals": list(range(len(LAYOUT["petals"]))), "stem": True, "meanders": [0, 1],
                    "xmod": [0, 1], "totalFb": True, "leds": [0, 1], "stamenLeds": [0, 1, 2],
                    "meters": [5] * len(LAYOUT.get("meters", []))}

    def lg(objs, name):
        if not bake:
            return
        for o in objs if isinstance(objs, list) else [objs]:
            o.lightgroup = name
        if name not in LIGHTGROUPS:
            LIGHTGROUPS.append(name)

    # Lazy import (parts imports this module for its helpers); blender -P
    # doesn't put the script's folder on sys.path.
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import parts

    LBL = LAYOUT["labelOffset"]
    KS = LAYOUT["knobScale"]

    # Materials ---------------------------------------------------------------
    mask_a, mask_b = (0.028, 0.142, 0.068), (0.044, 0.182, 0.088)
    mask = solder_mask("mask", mask_a, mask_b)
    # Mask pools a little thicker and lighter along the edges of tented copper.
    pool = solder_mask("mask_pool", tuple(c * 1.1 for c in mask_a), tuple(c * 1.1 for c in mask_b))
    trace = material("trace", (0.075, 0.300, 0.130), rough=0.35, coat=0.8, coat_rough=0.06)
    trace_fine = material("trace_fine", (0.056, 0.225, 0.100), rough=0.38, coat=0.8, coat_rough=0.06)
    bottom = material("trace_bottom", (0.040, 0.130, 0.068), rough=0.5, coat=0.5)
    # Amber, and modest: hot emission clips to white under AgX.
    glow_col = (1.0, 0.48, 0.12)
    # In a bake, lit materials keep the unlit base so the base pass matches the resting board.
    # A soft glow under the mask, not a neon overlay: the flower is big on this board.
    trace_lit = material("trace_lit", (0.075, 0.300, 0.130) if bake else (0.30, 0.42, 0.18), rough=0.3, coat=0.8,
                         emit=glow_col, emit_strength=0.65)
    # Pad runs carry the touch signal, not the voice: lit, they stay dimmer than the petals.
    run_lit = material("run_lit", (0.075, 0.300, 0.130) if bake else (0.22, 0.38, 0.16), rough=0.3, coat=0.8,
                       emit=glow_col, emit_strength=0.35)
    # Bottom-layer routes glow dimly through the board.
    bottom_lit = material("trace_bottom_lit", (0.040, 0.130, 0.068), rough=0.5, coat=0.5, emit=glow_col,
                          emit_strength=0.8)
    tin = material("tin", (0.72, 0.72, 0.70), rough=0.3, metal=1.0, bump=0.06, bump_scale=700)
    epoxy = material("epoxy", (0.025, 0.025, 0.027), rough=0.6, bump=0.15, bump_scale=500)
    chrome = material("chrome", (0.9, 0.9, 0.92), rough=0.12, metal=1.0)
    nut_metal = brushed_metal("jack_nut", (0.80, 0.80, 0.80), 0.22, 0.36)
    hole = material("hole", (0.004, 0.004, 0.004), rough=1.0)
    led_off = material("led", (0.55, 0.06, 0.12), rough=0.1, transmission=0.6)
    if bake:
        led_on = material("led_on", (0.55, 0.06, 0.12), rough=0.1, transmission=0.6, emit=(1.0, 0.12, 0.28),
                          emit_strength=4.0)
    else:
        led_on = material("led_on", (1.0, 0.2, 0.32), rough=0.1, emit=(1.0, 0.12, 0.28), emit_strength=4.0)
    smd_pink = material("smd_led", (0.75, 0.30, 0.38), rough=0.25)
    smd_pink_on = material("smd_led_on", (0.75, 0.30, 0.38) if bake else (1.0, 0.3, 0.45), rough=0.25,
                           emit=(1.0, 0.18, 0.4), emit_strength=3.0)

    # Board -------------------------------------------------------------------
    cu = bpy.data.curves.new("board", "CURVE")
    cu.dimensions = "2D"
    cu.fill_mode = "BOTH"
    cu.extrude = 0.0008
    corners = rounded_rect(0, 0, W, H, LAYOUT["cornerRadius"], seg=8)
    sp = cu.splines.new("POLY")
    sp.points.add(len(corners) - 1)
    for k, (x, y) in enumerate(corners):
        sp.points[k].co = (x * PX, -y * PX, 0, 1)
    sp.use_cyclic_u = True
    board = link(bpy.data.objects.new("board", cu))
    board.location.z = -0.0008
    board.data.materials.append(mask)

    # Copper under the mask: the flower, green on green ----------------------------
    def copper(name, d, width, mat, group=None, flatten=0.3, halo=True):
        polys = sample_path(d)
        obj = poly_curve(name, polys, width, mat, flatten=flatten)
        if halo:
            poly_curve(name + "_pool", polys, width + 4.0, pool, z=0.00001, flatten=0.03)
        if group:
            lg(obj, group)
        return obj

    on = lit_spec.get("petals", [])
    for idx, d in enumerate(LAYOUT["petals"]):
        copper(f"petal{idx}", d, 2.4, trace_lit if idx in on else trace, f"petal{idx}")
    for idx, d in enumerate(LAYOUT["ribs"]):
        copper(f"rib{idx}", d, 1.6, trace_lit if idx in on else trace, f"petal{idx}")
    for idx, d in enumerate(LAYOUT["padRuns"]):
        copper(f"run{idx}", d, 1.5, run_lit if idx in on else trace, f"petal{idx}")
    for d in LAYOUT.get("traces", []):
        copper("trace", d, 1.4, trace_fine)
    copper("stem", LAYOUT["stem"], 3.0, trace_lit if lit_spec.get("stem") else trace, "mix")
    for idx, d in enumerate(LAYOUT["meanders"]):
        copper(f"meander{idx}", d, 1.8, trace_lit if idx in lit_spec.get("meanders", []) else trace, f"delay{idx}")
    for d in LAYOUT.get("leaves", []):
        copper("leaf", d, 1.4, trace_fine)
    for d in LAYOUT.get("leafRibs", []):
        copper("leafrib", d, 1.0, trace_fine)
    # Bottom-layer routes: seen dimly through the board.
    for idx, d in enumerate(LAYOUT["xmod"]):
        copper(f"xmod{idx}", d, 1.3,
               (bottom_lit if bake else trace_lit) if idx in lit_spec.get("xmod", []) else bottom,
               f"xmod{idx}", flatten=0.1, halo=False)
    copper("totalfb", LAYOUT["totalFb"], 1.3, bottom_lit if lit_spec.get("totalFb") else bottom, "totalfb",
           flatten=0.1, halo=False)

    # Touch pads: exposed gold combs in the group boxes ----------------------------
    pr = LAYOUT["padRadius"]
    for i, (x, y, name) in enumerate(LAYOUT["pads"]):
        lg(parts.touch_pad(x, y, pr, pitch=3.6, gap=1.8, width=2.3, lit=i in on), f"petal{i}")
        ring = [(x + (pr + 3.5) * math.cos(2 * math.pi * k / 96), y + (pr + 3.5) * math.sin(2 * math.pi * k / 96))
                for k in range(96)]
        silk_line([(ring, True)], 1.0)
        label(name, x, y + LBL, "label")

    # Silkscreen: frames with reversed-out title tabs, brackets, free labels ---------
    title = type_level("title")
    for (x, y, w, h, name) in LAYOUT["boxes"]:
        silk_line([([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], True)], 0.75)
        tw = measure(name, title) + 14
        knockout(flat_poly("tab", rounded_rect(x - 0.4, y - 0.4, tw + 0.4, 16.4, 0), silk_ink(1.0)),
                 [text(name, x + 7, y + 12, title["size"], mask, "left", tracking=title["tracking"])])
    legend = type_level("legend")
    for (x1, x2, y, name) in LAYOUT["brackets"]:
        xm = (x1 + x2) / 2
        gap = measure(name, legend) + 8
        silk_line([([(x1 - 16, y - 4), (x1 - 16, y), (xm - gap / 2, y)], False),
                   ([(xm + gap / 2, y), (x2 + 16, y), (x2 + 16, y - 4)], False)], 1.0, width=0.9)
        label(name, xm, y + 3, "legend")
    for (x, y, body, align, level) in LAYOUT["labels"]:
        label(body, x, y, level, align)

    # Logo: a solid silk block with LILI-4 knocked out of the ink, and the subtitle.
    logo = LAYOUT["logo"]
    bx, by, bw, bh = logo["block"]
    knockout(flat_poly("logo_block", rounded_rect(bx, by, bw, bh, 2), silk_ink(1.0)),
             [text(logo["text"], bx + bw / 2, logo["baseline"], logo["size"], mask, "center",
                   tracking=logo["tracking"])])
    for (x, y, body, level) in logo["subtitle"]:
        label(body, x, y, level, "left")

    # Output mode: clickable silkscreen, one text per state (baked as a patch strip).
    om = LAYOUT.get("outMode")
    if om:
        for k, body in enumerate(om["texts"]):
            obj = label(body, om["x"], om["y"], om["level"], om["align"])
            obj.hide_render = bake or k != om.get("default", 0)
            OUTMODE_TEXTS.append((k, obj))

    # Knobs: cream bodies come from the knob strip at runtime; scales and labels are silk.
    for (pid, cx, cy, body, value, *_rest) in LAYOUT["knobs"]:
        parts.knob_ticks(cx, cy, r_in=19 * KS, r_out=21.5 * KS, r_long=23 * KS)
        if not ARGS["no_knobs"]:
            parts.scale_about(parts.knob_cream(cx, cy, value, r=12.0, ticks=False), cx, cy, KS)
        label(body, cx, cy + LBL, "label")

    # Toggles: option 0 is "up"; 3-way ones centre on option 1. Legends sit right of
    # the throw, one per position, with a tick from the bat position.
    def legends(cx, cy, names):
        rows = (-12, 0, 12) if len(names) == 3 else (-12, 12)
        for name, r in zip(names, rows):
            silk_line([([(cx + 8, cy + r), (cx + 11, cy + r)], False)], 0.7)
            label(name, cx + 14, cy + r + 3, "legend", "left")

    for (pid, cx, cy, body, names, sel, *_rest) in LAYOUT["jumpers"]:
        legends(cx, cy, names)
        label(body, cx, cy + LBL, "label")
        if not ARGS["no_knobs"]:
            throws = (1, 0, -1) if len(names) == 3 else (1, -1)
            parts.toggle_switch(cx, cy, throws[sel], s=TOGGLE_S)
    for (pid, cx, cy, body, names, on_, *_rest) in LAYOUT["dips"]:
        if names:
            legends(cx, cy, names)
        label(body, cx, cy + LBL, "label")
        if not ARGS["no_knobs"]:
            parts.toggle_switch(cx, cy, 1 if on_ else -1, s=TOGGLE_S)

    # Pair level meters (driven by telemetry pairPeak at runtime) -----------------
    lit_levels = lit_spec.get("meters", [])
    for k, (mx, my) in enumerate(LAYOUT.get("meters", [])):
        lenses = parts.led_meter(mx, my, lit_count=lit_levels[k] if k < len(lit_levels) else 0,
                                 pitch=LAYOUT.get("meterPitch", 10))
        for j, lens in enumerate(lenses):
            lg(lens, f"meter{k}_{j}")
        silk_line([([(mx - 27, my - 7), (mx + 27, my - 7), (mx + 27, my + 7), (mx - 27, my + 7)], True)], 0.7)
        label("LEVEL", mx, my + LBL, "legend")

    # Parts in the flower and the stem -------------------------------------------------
    ux, uy = LAYOUT["u1"]
    cylinder("u1_flange", ux, uy, 14.5, 0.0005, chrome, verts=96)
    cylinder("u1_can", ux, uy, 12.5, 0.0045, chrome, z=0.0005, verts=96, bevel_px=2.0)
    box("u1_tab", ux + 9.5, uy + 8, 5.5, 3, 0.0005, chrome, bevel=0.3).rotation_euler.z = math.radians(-45)
    for k, (x, y) in enumerate(LAYOUT["stamenLeds"]):
        lg(box("stamen_led", x - 1.5, y - 2.5, 3, 5, 0.0007,
               smd_pink_on if k in lit_spec.get("stamenLeds", []) else smd_pink, bevel=0.4), "stamens")
    x, y, w, h = LAYOUT["u2"]
    box("u2", x, y, w, h, 0.003, epoxy, bevel=0.7)
    for k in range(4):
        for top in (True, False):
            box("u2_leg", x + 3 + k * 7.5, y - 2 if top else y + h, 3, 2, 0.0008, tin, bevel=0.2)
    cylinder("u2_dot", x + 4, y + 4, 1.3, 0.0031, material("dot", (0.05, 0.05, 0.05), rough=0.9))
    jx, jy = LAYOUT["jack"]
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=18 * PX, depth=0.0025)
    nut = bpy.context.active_object
    nut.location = (jx * PX, -jy * PX, 0.00125)
    nut.data.materials.append(nut_metal)
    cylinder("jack_barrel", jx, jy, 11.5, 0.005, chrome, z=0.0025)
    cylinder("jack_hole", jx, jy, 6.5, 0.0055, hole, z=0.0025)
    for k, (x, y) in enumerate(LAYOUT["leds"]):
        m = led_on if k in lit_spec.get("leds", []) else led_off
        rim = cylinder("led_rim", x, y, 3.6, 0.0006, m)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=3.0 * PX, location=(x * PX, -y * PX, 0.0006))
        dome = bpy.context.active_object
        dome.data.materials.append(m)
        bpy.ops.object.shade_smooth()
        lg([rim, dome], f"lfo{k}")


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
    # Placed relative to the board centre (tuned on the 1120x800 board, whose centre was (0.56, -0.4)).
    c = Vector((w * PX / 2, -h * PX / 2, 0.0))
    area("key", c + Vector((-1.26, 0.95, 0.9)), 1.0, 55, (1.0, 0.96, 0.9))
    area("fill", c + Vector((1.34, -0.2, 0.7)), 1.0, 14, (0.85, 0.92, 1.0))
    area("rim", c + Vector((0.0, -1.2, 0.25)), 0.6, 9, (1.0, 0.85, 0.7))

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


def layer_kind(name):
    for prefix in ("petal", "delay", "xmod", "lfo", "meter"):
        if name.startswith(prefix):
            return prefix, name[len(prefix):]
    return name, None


def bake_layers(scene):
    """One render -> base.png + one additive glow layer per light group + manifest.json.

    Runtime: out = base + sum(level_i * layer_i), then bloom. With --bake, --out is a directory.
    """
    outdir = os.path.join(ROOT, ARGS["out"])
    os.makedirs(outdir, exist_ok=True)
    # Linear light, not tone-mapped: layers must add physically. sRGB-encoded
    # 16-bit PNG at -2 EV keeps headroom for glow cores; the runtime decodes,
    # multiplies by 4, sums, then tone-maps (AgX approximation).
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = BAKE_EXPOSURE_EV
    for o in scene.objects:
        if o.type == "LIGHT" or o.name == "softbox":
            o.lightgroup = "base"
    scene.world.lightgroup = "base"
    names = ["base"] + LIGHTGROUPS
    vl = scene.view_layers[0]
    for n in names:
        vl.lightgroups.add(name=n)

    scene.use_nodes = True
    nt = scene.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    rl = nt.nodes.new("CompositorNodeRLayers")
    comp = nt.nodes.new("CompositorNodeComposite")
    nt.links.new(rl.outputs["Image"], comp.inputs["Image"])
    fo = nt.nodes.new("CompositorNodeOutputFile")
    fo.base_path = outdir
    fo.format.file_format = "PNG"
    fo.format.color_mode = "RGB"
    fo.format.color_depth = "16"
    fo.file_slots.remove(fo.inputs[0])
    written = []
    for n in names:
        sock = rl.outputs.get("Combined_" + n)
        if sock is None:
            print("MISSING PASS", n)
            continue
        dn = nt.nodes.new("CompositorNodeDenoise")
        nt.links.new(sock, dn.inputs["Image"])
        fo.file_slots.new(n)
        nt.links.new(dn.outputs["Image"], fo.inputs[n])
        written.append(n)

    scene.render.filepath = os.path.join(outdir, "_all_lit.png")
    bpy.ops.render.render(write_still=True)
    for n in written:  # the file output node appends the frame number
        src = os.path.join(outdir, f"{n}{scene.frame_current:04d}.png")
        if os.path.exists(src):
            os.replace(src, os.path.join(outdir, n + ".png"))

    # The output-mode silkscreen: re-render a region around it once per text state.
    # The border keeps the full frame (cropping is export_ui's job), so the patch
    # lines up with the base pixel for pixel; the margin keeps the denoiser's
    # region edges out of the patch.
    outmode = None
    om = LAYOUT.get("outMode")
    if om and OUTMODE_TEXTS:
        px_, py_, pw, ph = om["patch"]
        m = 12
        scene.render.use_border = True
        scene.render.use_crop_to_border = False
        scene.render.border_min_x = max(0.0, (px_ - m) / W)
        scene.render.border_max_x = min(1.0, (px_ + pw + m) / W)
        scene.render.border_min_y = max(0.0, 1.0 - (py_ + ph + m) / H)
        scene.render.border_max_y = min(1.0, 1.0 - (py_ - m) / H)
        files = []
        for k in range(len(om["texts"])):
            for j, obj in OUTMODE_TEXTS:
                obj.hide_render = j != k
            sub = os.path.join(outdir, f"_outmode{k}")
            fo.base_path = sub
            scene.render.filepath = os.path.join(sub, "_all_lit.png")
            bpy.ops.render.render(write_still=True)
            os.replace(os.path.join(sub, f"base{scene.frame_current:04d}.png"), os.path.join(outdir, f"outmode{k}.png"))
            shutil.rmtree(sub)
            files.append(f"outmode{k}.png")
        scene.render.use_border = False
        outmode = {"files": files, "patch": om["patch"],
                   "comment": "base pass with each output-mode text, full frame; export crops patch * scale"}

    manifest = {
        "size": [W, H],
        "scale": ARGS["scale"],
        "base": "base.png",
        "encoding": {"transfer": "sRGB", "exposureEV": BAKE_EXPOSURE_EV, "bits": 16,
                     "decode": "linear = srgb_to_linear(px) * 2^(-exposureEV)"},
        "blend": "linear = base + sum(level_i * layer_i); bloom; tonemap (AgX approx); encode sRGB",
        "tonemap": {"curve": "AgX minimal approximation (see tools/art/composite.py)",
                    "lookPower": 1.40, "lookSaturation": 1.05},
        "layers": [{"name": n, "file": n + ".png", "kind": layer_kind(n)[0], "index": layer_kind(n)[1]}
                   for n in written if n != "base"],
    }
    if outmode:
        manifest["outMode"] = outmode
    with open(os.path.join(outdir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("BAKED", len(written), "layers to", outdir)


def main():
    scene = reset_scene()
    build(scene)
    if "--probe" in sys.argv:
        i = sys.argv.index("--probe")
        probe(float(sys.argv[i + 1]), float(sys.argv[i + 2]))
        return
    lights_and_camera(scene, tilt_deg=ARGS["tilt"])
    if ARGS["bake"]:
        bake_layers(scene)
        return
    compositor(scene)
    out = os.path.join(ROOT, ARGS["out"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print("WROTE", out)


if __name__ == "__main__":  # also imported as a helper library by parts.py and render_sprites.py
    main()
