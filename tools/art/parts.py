"""Detailed control models for the LILI-8 board (knobs, switches, buttons).

All positions are board px (see render_board.PX); each builder places one part
centred on (cx, cy) with its base on the board surface (z = 0). `value` is the
normalised control position (knobs sweep 270 degrees, 0 at 7 o'clock).
"""

import math

import bmesh
import bpy

from render_board import PX, box, cylinder, link, material, poly_curve

# ----------------------------------------------------------------------------- materials

_MATS = {}


def mats():
    if _MATS:
        return _MATS
    _MATS.update(
        knob_black=material("knob_black", (0.012, 0.012, 0.013), rough=0.32, coat=0.35, coat_rough=0.2),
        knob_cream=material("knob_cream", (0.80, 0.74, 0.60), rough=0.38, coat=0.25, sss=0.05),
        knob_alu=material("knob_alu", (0.86, 0.86, 0.85), rough=0.24, metal=1.0),
        paint_white=material("paint_white", (0.9, 0.89, 0.85), rough=0.5),
        paint_black=material("paint_black", (0.02, 0.02, 0.02), rough=0.6),
        trim_blue=material("trim_blue2", (0.03, 0.10, 0.38), rough=0.38, coat=0.3, bump=0.12, bump_scale=900),
        trim_ink=material("trim_ink", (0.55, 0.62, 0.78), rough=0.6),
        brass=material("brass2", (0.86, 0.64, 0.33), rough=0.22, metal=1.0),
        chrome=material("chrome2", (0.92, 0.92, 0.93), rough=0.08, metal=1.0),
        nickel=material("nickel", (0.78, 0.78, 0.76), rough=0.3, metal=1.0),
        tin=material("tin2", (0.72, 0.72, 0.70), rough=0.3, metal=1.0),
        black_plastic=material("black_plastic2", (0.02, 0.02, 0.022), rough=0.45),
        cap_cream=material("cap_cream", (0.83, 0.78, 0.66), rough=0.4),
        led_amber=material("led_amber_on", (1.0, 0.5, 0.15), emit=(1.0, 0.45, 0.1), emit_strength=3.0,
                           rough=0.1),
        silk=material("silk2", (0.86, 0.85, 0.80), rough=0.75),
    )
    return _MATS


# ----------------------------------------------------------------------------- geometry helpers

def radial_prism(name, cx, cy, radius_fn, depth, z, mat, segments=192, top_bevel_px=0.0, smooth_deg=35):
    """Extruded outline r(theta) -> a fluted/knurled body."""
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, segments=segments, radius=1.0)
    for v in bm.verts:
        th = math.atan2(v.co.y, v.co.x)
        r = radius_fn(th) * PX
        v.co.x, v.co.y = r * math.cos(th), r * math.sin(th)
    bm.faces.ensure_lookup_table()
    face = bm.faces[0]
    ext = bmesh.ops.extrude_face_region(bm, geom=[face])
    top = [e for e in ext["geom"] if isinstance(e, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=top, vec=(0.0, 0.0, depth))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = link(bpy.data.objects.new(name, mesh))
    obj.location = (cx * PX, -cy * PX, z)
    obj.data.materials.append(mat)
    if top_bevel_px > 0:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = top_bevel_px * PX
        mod.segments = 4
        mod.limit_method = "ANGLE"
        mod.angle_limit = math.radians(60)
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(smooth_deg))
    obj.select_set(False)
    return obj


def pointer(name, cx, cy, value, r_in, r_out, width, z, mat, height=0.00015):
    """A painted/engraved indicator line from r_in to r_out at the knob angle."""
    a = math.radians(-135 + 270 * value)  # 0 = straight up, clockwise
    length = r_out - r_in
    mid = (r_in + r_out) / 2
    x = cx + mid * math.sin(a)
    y = cy - mid * math.cos(a)
    obj = box(name, x - width / 2, y - length / 2, width, length, height, mat, z=z, bevel=width * 0.3)
    obj.rotation_euler.z = -a
    return obj


def scale_ticks(cx, cy, r_in, r_out, count=11, mat=None, width=1.1):
    """Silkscreen scale around a knob (270 degrees, long ticks at ends and middle)."""
    mat = mat or mats()["silk"]
    polys = []
    for i in range(count):
        a = math.radians(-135 + 270 * i / (count - 1))
        ro = r_out + (3 if i in (0, count // 2, count - 1) else 0)
        polys.append(([(cx + r_in * math.sin(a), cy - r_in * math.cos(a)),
                       (cx + ro * math.sin(a), cy - ro * math.cos(a))], False))
    return poly_curve("ticks", polys, width, mat, z=0.0003, flatten=0.2)


# ----------------------------------------------------------------------------- knobs

def knob_trimmer(cx, cy, value):
    """Refined Bourns 3386-style cermet trimmer: textured blue body, brass cross-slot rotor."""
    m = mats()
    box("trim_body", cx - 17, cy - 17, 34, 34, 0.0046, m["trim_blue"], bevel=2.2)
    cylinder("trim_well", cx, cy, 14, 0.0004, m["black_plastic"], z=0.0044)
    rotor = cylinder("trim_rotor", cx, cy, 12.5, 0.0014, m["brass"], z=0.0044, bevel_px=1.2)
    rotor.rotation_euler.z = 0
    for k, (w, h) in enumerate(((19, 3.2), (3.2, 19))):
        slot = box("trim_slot", cx - w / 2, cy - h / 2, w, h, 0.0005, m["paint_black"], z=0.0055, bevel=0.3)
        slot.rotation_euler.z = -math.radians(-135 + 270 * value)
    pointer("trim_notch", cx, cy, value, 9.5, 12.5, 2.2, 0.0058, m["paint_white"])
    for k in (-1, 0, 1):
        cylinder("trim_leg", cx + k * 9, cy + 21, 1.6, 0.0004, m["tin"])


def knob_davies(cx, cy, value, r=15.0):
    """Black fluted knob with a skirt, 1900H style."""
    m = mats()
    scale_ticks(cx, cy, r + 5, r + 9)
    cylinder("dav_skirt", cx, cy, r + 2.5, 0.0022, m["knob_black"], bevel_px=1.0, verts=128)
    flutes = 18
    radial_prism("dav_body", cx, cy,
                 lambda th: r - 1.3 * (0.5 - 0.5 * math.cos(flutes * th)) ** 2,
                 0.0085, 0.0022, m["knob_black"], top_bevel_px=2.2)
    pointer("dav_line", cx, cy, value, 3.0, r - 0.5, 1.8, 0.0107, m["paint_white"])
    pointer("dav_skirt_line", cx, cy, value, r + 0.2, r + 2.3, 1.8, 0.0022, m["paint_white"])


def knob_alu(cx, cy, value, r=14.0):
    """Machined aluminium knob, fine knurl, engraved line."""
    m = mats()
    scale_ticks(cx, cy, r + 4, r + 8)
    knurl = 72
    radial_prism("alu_body", cx, cy,
                 lambda th: r - 0.35 * abs(math.sin(knurl * th / 2)),
                 0.0095, 0.0, m["knob_alu"], segments=432, top_bevel_px=1.4, smooth_deg=25)
    cylinder("alu_face", cx, cy, r - 2.2, 0.0002, material("alu_face", (0.8, 0.8, 0.8), rough=0.18, metal=1.0,
                                                            bump=0.2, bump_scale=1600),
             z=0.0095, verts=128)
    pointer("alu_line", cx, cy, value, 4.0, r - 2.5, 1.6, 0.0097, m["paint_black"], height=0.0001)


def knob_ticks(cx, cy, r=13.0):
    """The silkscreen scale that goes with knob_cream (printed on the board)."""
    return scale_ticks(cx, cy, r + 7, r + 10)


def knob_cream(cx, cy, value, r=13.0, ticks=True):
    """Vintage cream knob: wide skirt with a printed index, fluted cap. Returns its objects."""
    m = mats()
    if ticks:
        knob_ticks(cx, cy, r)
    flutes = 12
    return [
        cylinder("cream_skirt", cx, cy, r + 5.5, 0.0024, m["knob_cream"], bevel_px=1.2, verts=128),
        radial_prism("cream_cap", cx, cy,
                     lambda th: r - 1.8 * (0.5 - 0.5 * math.cos(flutes * th)),
                     0.008, 0.0024, m["knob_cream"], top_bevel_px=2.6),
        pointer("cream_index", cx, cy, value, r - 0.5, r + 5.0, 1.8, 0.0024, m["paint_black"]),
        pointer("cream_dot", cx, cy, value, r - 5.0, r - 2.5, 2.2, 0.0104, m["paint_black"]),
    ]


# ----------------------------------------------------------------------------- touch pads

def touch_pad(cx, cy, r=20.0, pitch=4.0, gap=2.2, lit=False):
    """Interdigitated touch sensor: two gold combs, each spined on a half ring.

    A fingertip bridges the two electrodes, like the Lyra's touch plates.
    `lit` gives the combs a warm glow (the voice is sounding).
    """
    if lit:
        gold = mats().setdefault("enig_lit", material("enig_lit", (1.0, 0.74, 0.34), rough=0.26, metal=0.6,
                                                      emit=(1.0, 0.55, 0.18), emit_strength=1.6))
    else:
        gold = mats().setdefault("enig", material("enig", (1.0, 0.74, 0.34), rough=0.26, metal=1.0,
                                                  bump=0.05, bump_scale=1200))
    polys_a, polys_b = [], []
    steps = 40
    for side, polys in ((-1, polys_a), (1, polys_b)):
        # half ring spine, stopping short of the top/bottom so the combs don't touch
        a0 = math.asin(gap / r)
        arc = []
        for s in range(steps + 1):
            a = a0 + (math.pi - 2 * a0) * s / steps  # from top to bottom around one side
            arc.append((cx + side * r * math.sin(a), cy - r * math.cos(a)))
        polys.append((arc, False))
    n = int((2 * r - 2 * pitch) // pitch)
    for k in range(n + 1):
        y = -r + pitch + k * pitch
        half = math.sqrt(max(r * r - y * y, 0.0))
        if half < pitch:
            continue
        side = -1 if k % 2 == 0 else 1
        start = cx + side * half
        end = cx - side * (half - gap - 1.0)
        (polys_a if side < 0 else polys_b).append(([(start, cy + y), (end, cy + y)], False))
    return [poly_curve("pad_comb_a", polys_a, 1.6, gold, z=0.0, flatten=0.35),
            poly_curve("pad_comb_b", polys_b, 1.6, gold, z=0.0, flatten=0.35)]


# ----------------------------------------------------------------------------- indicators

def led_meter(cx, cy, n=5, lit_count=0, pitch=12.0):
    """A row of 0805 SMD LEDs (amber, the last one pink for 'hot'), centred on (cx, cy).

    Returns the lens objects, left to right.
    """
    m = mats()
    lenses = []
    body = m.setdefault("smd_body", material("smd_body", (0.9, 0.88, 0.82), rough=0.4))
    for k in range(n):
        x = cx + (k - (n - 1) / 2) * pitch
        hot = k == n - 1
        on = k < lit_count
        key = ("led_hot" if hot else "led_amb") + ("_on" if on else "_off")
        if key not in m:
            # Deep hues: AgX desaturates bright emission on small parts toward cream.
            col = (1.0, 0.12, 0.3) if hot else (1.0, 0.34, 0.04)
            m[key] = (material(key, col, rough=0.15, emit=col, emit_strength=1.3) if on
                      else material(key, tuple(c * 0.28 for c in col), rough=0.15))
        box("smd_led_body", x - 3.5, cy - 5, 7, 10, 0.0006, body, bevel=0.4)
        lenses.append(box("smd_led_lens", x - 2.6, cy - 3.2, 5.2, 6.4, 0.0009, m[key], bevel=0.8))
    return lenses


# ----------------------------------------------------------------------------- switches & buttons

def toggle_switch(cx, cy, position, s=1.0):
    """Bat-handle toggle. position: +1 up, 0 centre, -1 down (in board view); s scales it."""
    m = mats()
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=9 * s * PX, depth=0.0022 * s)
    nut = bpy.context.active_object
    nut.location = (cx * PX, -cy * PX, 0.0011 * s)
    nut.data.materials.append(m["nickel"])
    bush = radial_prism("tog_bush", cx, cy, lambda th: (5.6 - 0.35 * abs(math.sin(10 * th))) * s, 0.0045 * s,
                        0.0022 * s, m["nickel"], segments=160, top_bevel_px=0.6)
    # A steep lean and a long bat so the throw reads from straight above.
    tilt = math.radians(34) * position
    length = 0.016 * s
    pivot = 0.0067 * s
    bpy.ops.mesh.primitive_cone_add(vertices=48, radius1=2.0 * s * PX, radius2=3.3 * s * PX, depth=length)
    bat = bpy.context.active_object
    bat.data.materials.append(m["chrome"])
    bpy.ops.object.shade_smooth()
    # pivot at the bushing top, lean toward -y (board "up") for position +1
    bat.location = (cx * PX, -cy * PX + math.sin(tilt) * length / 2, pivot + math.cos(tilt) * length / 2)
    bat.rotation_euler.x = -tilt
    bpy.ops.mesh.primitive_uv_sphere_add(radius=3.4 * s * PX, location=(
        cx * PX, -cy * PX + math.sin(tilt) * length, pivot + math.cos(tilt) * length))
    tip = bpy.context.active_object
    tip.scale.z = 0.8
    tip.data.materials.append(m["chrome"])
    bpy.ops.object.shade_smooth()
    return [nut, bush, bat, tip]


def slide_switch(cx, cy, on):
    """SPDT slide switch: tin frame, black actuator."""
    m = mats()
    box("slide_frame", cx - 14, cy - 6.5, 28, 13, 0.0035, m["tin"], bevel=0.8)
    box("slide_slot", cx - 11, cy - 3.5, 22, 7, 0.0003, m["paint_black"], z=0.0035, bevel=0.3)
    x = cx + (3 if on else -11)
    box("slide_actuator", x, cy - 3, 8, 6, 0.0032, m["black_plastic"], z=0.0035, bevel=0.8)
    for k in (-1, 0, 1):
        box("slide_leg", cx + k * 8 - 1.5, cy + 6.5, 3, 3, 0.0006, m["tin"], bevel=0.2)


def tact_button(cx, cy, lit):
    """12 mm tactile switch with a cream round cap and an LED beside it."""
    m = mats()
    box("tact_base", cx - 12, cy - 12, 24, 24, 0.0035, m["black_plastic"], bevel=1.2)
    for sx in (-1, 1):
        for sy in (-1, 1):
            cylinder("tact_pin", cx + sx * 9.5, cy + sy * 9.5, 1.3, 0.0036, m["tin"])
    cylinder("tact_cap", cx, cy, 8.5, 0.0045, m["cap_cream"], z=0.0035, bevel_px=2.2, verts=96)
    cylinder("tact_led", cx + 20, cy - 12, 3.0, 0.002,
             m["led_amber"] if lit else material("led_amber_off", (0.45, 0.22, 0.06), rough=0.15))
