"""Renders the plug-in's knob and switch filmstrips with Blender (Cycles).

    /Applications/Blender.app/Contents/MacOS/Blender -b -P render_controls.py -- OUT_DIR [--preview] [names...]

names: knob_black knob_green knob_white switch (default: all).
Each control is written to OUT_DIR/<name>.png as a vertical strip of square (knobs) or 2:1
(switches) frames, top to bottom. Knob frame i is the knob at normalised position i / (N - 1)
over the plug-in's 288 degree travel; switch frames are [off, on].
--preview renders only a few frames per control into OUT_DIR/preview_<name>.png.
"""

import bpy, bmesh, math, os, sys, struct, zlib
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(argv[0] if argv else "out")
PREVIEW = "--preview" in argv
NAMES = [a for a in argv[1:] if not a.startswith("--")] or ["knob_black", "knob_green", "knob_white", "switch"]
os.makedirs(OUT, exist_ok=True)

KNOB_FRAMES = 101
KNOB_PX = 200          # frame edge; the frame spans KNOB_SPAN scene units at z = 0
KNOB_SPAN = 3.2        # knob skirt radius is 1.0
TRAVEL = math.radians(288.0)
SWITCH_PX = (336, 168)
SWITCH_SPAN = 5.6      # switch plate is 4.6 x 2.0 units


# ---------------------------------------------------------------------------------------------
def write_png(path, rgba):
    """rgba: uint8 array (h, w, 4), top row first."""
    h, w, _ = rgba.shape
    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def read_png(path):
    img = bpy.data.images.load(path)
    img.colorspace_settings.name = "Non-Color"
    w, h = img.size
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1]
    bpy.data.images.remove(img)
    # drop the faint film the shadow catcher leaves over the whole frame
    px[..., 3] = np.clip((px[..., 3] - 0.03) / 0.97, 0.0, 1.0)
    # fade the shadow out before the frame edge so it never shows a hard border
    yy, xx = np.mgrid[0:h, 0:w]
    edge = np.minimum(np.minimum(xx, w - 1 - xx), np.minimum(yy, h - 1 - yy)) / (0.12 * min(w, h))
    px[..., 3] *= np.clip(edge, 0.0, 1.0) ** 1.5
    return np.clip(np.round(px * 255.0), 0, 255).astype(np.uint8)


# ---------------------------------------------------------------------------------------------
def reset_scene(res, span):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for d in prefs.devices: d.use = True
        sc.cycles.device = "GPU"
    except Exception as e:
        print("GPU unavailable, using CPU:", e)
    sc.cycles.samples = 48 if PREVIEW else 160
    sc.cycles.use_denoising = True
    sc.render.film_transparent = True
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.render.image_settings.color_depth = "8"
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.render.filter_size = 1.2

    world = bpy.data.worlds.new("World")
    sc.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.20, 0.20, 0.21, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.08

    # camera straight down, long lens so the sides barely show
    lens = 85.0
    cam_d = bpy.data.cameras.new("Cam")
    cam_d.lens = lens
    cam_d.sensor_fit = "HORIZONTAL"
    cam_d.sensor_width = 36.0
    cam = bpy.data.objects.new("Cam", cam_d)
    cam.location = (0, 0, span * lens / 36.0)
    sc.collection.objects.link(cam)
    sc.camera = cam

    def area(name, loc, size, energy, color=(1, 1, 1), size_y=None, glossy=True):
        ld = bpy.data.lights.new(name, "AREA")
        ld.energy = energy
        ld.color = color
        ld.shape = "RECTANGLE"
        ld.size = size
        ld.size_y = size_y or size
        ob = bpy.data.objects.new(name, ld)
        ob.location = loc
        ob.visible_glossy = glossy
        sc.collection.objects.link(ob)
        con = ob.constraints.new("TRACK_TO")
        tgt = bpy.data.objects.get("Target") or bpy.data.objects.new("Target", None)
        if tgt.name not in sc.collection.objects: sc.collection.objects.link(tgt)
        con.target = tgt
        con.track_axis = "TRACK_NEGATIVE_Z"
        con.up_axis = "UP_Y"
        return ob

    area("Key", (-2.6, 3.4, 6.2), 3.0, 620.0, (1.0, 0.97, 0.92))
    area("Fill", (4.5, -1.0, 3.0), 4.0, 90.0, (0.9, 0.95, 1.0))
    area("Top", (0.0, 0.5, 9.0), 6.0, 160.0, glossy=False)
    area("Rim", (0.0, -5.0, 2.0), 5.0, 70.0, size_y=1.0)
    # soft strip reflected in flat tops: gives the glossy faces a gradient instead of pure black
    area("Strip", (-1.5, 2.5, 10.0), 14.0, 25.0, size_y=2.0)

    # shadow catcher
    bpy.ops.mesh.primitive_plane_add(size=40)
    plane = bpy.context.active_object
    plane.is_shadow_catcher = True
    return sc


def material(name, color, rough=0.35, coat=0.0, coat_rough=0.08, metal=0.0, spec=0.5,
             emit=None, emit_strength=0.0, sss=0.0, transmission=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    b.inputs["Specular IOR Level"].default_value = spec
    b.inputs["Coat Weight"].default_value = coat
    b.inputs["Coat Roughness"].default_value = coat_rough
    b.inputs["Subsurface Weight"].default_value = sss
    b.inputs["Transmission Weight"].default_value = transmission
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1)
        b.inputs["Emission Strength"].default_value = emit_strength
    return m


def add_pointer_paint(m, paint, half_width, r_min, y_min=0.0, z_min=-1.0):
    """Paints a radial line along object +Y into material m (moves with the knob)."""
    nt = m.node_tree
    N = nt.nodes
    L = nt.links
    b = N["Principled BSDF"]
    tc = N.new("ShaderNodeTexCoord")
    sep = N.new("ShaderNodeSeparateXYZ")
    L.new(tc.outputs["Object"], sep.inputs[0])

    def math_node(op, a, bval=None):
        n = N.new("ShaderNodeMath")
        n.operation = op
        if isinstance(a, float): n.inputs[0].default_value = a
        else: L.new(a, n.inputs[0])
        if bval is not None:
            if isinstance(bval, float): n.inputs[1].default_value = bval
            else: L.new(bval, n.inputs[1])
        return n.outputs[0]

    ax = math_node("ABSOLUTE", sep.outputs["X"])
    in_w = math_node("LESS_THAN", ax, half_width)
    in_y = math_node("GREATER_THAN", sep.outputs["Y"], max(r_min, y_min))
    in_z = math_node("GREATER_THAN", sep.outputs["Z"], z_min)
    mask = math_node("MULTIPLY", math_node("MULTIPLY", in_w, in_y), in_z)

    mix = N.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    L.new(mask, mix.inputs["Factor"])
    mix.inputs[6].default_value = b.inputs["Base Color"].default_value[:]
    mix.inputs[7].default_value = (*paint[:3], 1)
    L.new(mix.outputs[2], b.inputs["Base Color"])
    # paint is matte-ish
    rmix = N.new("ShaderNodeMix")
    rmix.data_type = "FLOAT"
    L.new(mask, rmix.inputs["Factor"])
    rmix.inputs[2].default_value = b.inputs["Roughness"].default_value
    rmix.inputs[3].default_value = paint[3]
    L.new(rmix.outputs[0], b.inputs["Roughness"])


def lathe(name, profile, ridges=0, ridge_depth=0.0, seg_per_ridge=8, segments=128, mats=None):
    """profile: list of (z, r, ridge_weight, mat_index); last point may have r == 0 (closed top).
    Ridges modulate the radius around the circumference (knurling)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    n = ridges * seg_per_ridge if ridges else segments
    rings = []
    for (z, r, rw, mi) in profile:
        if r <= 1e-6:
            rings.append([bm.verts.new((0, 0, z))])
            continue
        ring = []
        for i in range(n):
            t = 2 * math.pi * i / n
            rr = r
            if ridges and rw > 0:
                ph = (i % seg_per_ridge) / seg_per_ridge          # 0..1 across one ridge
                s = 0.5 - 0.5 * math.cos(2 * math.pi * ph)        # 0 at ridge top, 1 in groove
                rr = r - ridge_depth * rw * s ** 0.7
            ring.append(bm.verts.new((rr * math.sin(t), rr * math.cos(t), z)))
        rings.append(ring)
    # bottom cap
    if len(rings[0]) > 1:
        f = bm.faces.new(list(reversed(rings[0])))
        f.material_index = profile[0][3]
    for k in range(len(rings) - 1):
        a, b = rings[k], rings[k + 1]
        mi = profile[k + 1][3]
        if len(b) == 1:
            for i in range(n):
                f = bm.faces.new((a[i], a[(i + 1) % n], b[0]))
                f.material_index = mi
        else:
            for i in range(n):
                f = bm.faces.new((a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]))
                f.material_index = mi
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons: p.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for m in (mats or []): me.materials.append(m)
    es = ob.modifiers.new("split", "EDGE_SPLIT")
    es.split_angle = math.radians(55)
    es.use_edge_sharp = False
    return ob


def bevel_corner(z0, r0, z1, r1, steps=3, mi=0, rw=0.0, inward=True):
    """Quarter-round from (z0,r0) to (z1,r1) (excludes start point)."""
    pts = []
    for s in range(1, steps + 1):
        a = (math.pi / 2) * s / steps
        if inward:   # going up a wall then turning in: r stays, then z stays
            z = z0 + (z1 - z0) * math.sin(a)
            r = r1 + (r0 - r1) * math.cos(a)
        else:
            z = z1 + (z0 - z1) * math.cos(a)
            r = r0 + (r1 - r0) * math.sin(a)
        pts.append((z, r, rw, mi))
    return pts


# ---------------------------------------------------------------------------------------------
def build_knob_dark(green):
    if green:
        body = material("GreenPlastic", (0.012, 0.18, 0.115), rough=0.32, coat=0.6, coat_rough=0.12, sss=0.0)
    else:
        body = material("BlackPlastic", (0.018, 0.018, 0.02), rough=0.38, coat=0.35, coat_rough=0.18)
    top = material("TopFace", (0.02, 0.24, 0.16) if green else (0.028, 0.028, 0.03),
                   rough=0.22 if green else 0.28, coat=0.8, coat_rough=0.06)
    for m in (body, top):
        add_pointer_paint(m, (0.93, 0.93, 0.9, 0.55), 0.042, 0.16, z_min=0.12)

    P = []
    P.append((0.0, 0.99, 0, 0))
    P.append((0.12, 1.0, 0, 0))
    P += bevel_corner(0.12, 1.0, 0.18, 0.94, 3, 0)           # skirt top edge
    P.append((0.25, 0.84, 0, 0))                               # conical flange
    P.append((0.27, 0.79, 0, 0))
    P.append((0.29, 0.77, 1, 0))                               # knurled body
    P.append((0.78, 0.765, 1, 0))
    P += bevel_corner(0.78, 0.765, 0.86, 0.69, 3, 1, 0.4)
    P.append((0.845, 0.35, 0, 1))                              # slightly dished top
    P.append((0.838, 0.0, 0, 1))
    ob = lathe("Knob", P, ridges=36, ridge_depth=0.045, seg_per_ridge=8, mats=[body, top])
    return ob


def build_knob_white():
    cream = material("Cream", (0.60, 0.58, 0.52), rough=0.34, coat=0.4, coat_rough=0.12, sss=0.08)
    add_pointer_paint(cream, (0.03, 0.03, 0.03, 0.45), 0.045, 0.14, z_min=0.1)
    P = []
    P.append((0.0, 0.97, 0, 0))
    P.append((0.04, 1.0, 0, 0))
    P.append((0.36, 1.0, 1, 0))                                # finely ribbed wall
    P += bevel_corner(0.36, 1.0, 0.50, 0.86, 4, 0, 0.3)
    # dome
    for s in range(1, 9):
        a = (math.pi / 2) * s / 8
        P.append((0.50 + 0.17 * math.sin(a), 0.86 * math.cos(a) if s < 8 else 0.0, 0, 0))
    ob = lathe("Knob", P, ridges=48, ridge_depth=0.018, seg_per_ridge=6, mats=[cream])
    return ob


def render_knob(name, builder):
    sc = reset_scene((KNOB_PX, KNOB_PX), KNOB_SPAN)
    ob = builder()
    frames = [0, 50, 100] if PREVIEW else list(range(KNOB_FRAMES))
    tiles = []
    tmp = os.path.join(OUT, "_frame.png")
    for i in frames:
        pos = i / (KNOB_FRAMES - 1)
        ob.rotation_euler = (0, 0, -(-TRAVEL / 2 + TRAVEL * pos))   # clockwise from 12 o'clock
        sc.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        tiles.append(read_png(tmp))
        print(f"{name}: frame {i}", flush=True)
    os.remove(tmp)
    out = os.path.join(OUT, ("preview_" if PREVIEW else "") + name + ".png")
    write_png(out, np.concatenate(tiles, axis=0))
    print("wrote", out)


# ---------------------------------------------------------------------------------------------
def rounded_box(name, size, loc, radius, mat, segments=4):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = size
    bpy.ops.object.transform_apply(scale=True)
    bv = ob.modifiers.new("bevel", "BEVEL")
    bv.width = radius
    bv.segments = segments
    bv.limit_method = "ANGLE"
    ob.data.materials.append(mat)
    for p in ob.data.polygons: p.use_smooth = True
    return ob


def cut(target, cutter):
    bo = target.modifiers.new("cut", "BOOLEAN")
    bo.operation = "DIFFERENCE"
    bo.object = cutter
    bo.solver = "EXACT"
    cutter.hide_render = True
    cutter.hide_viewport = True
    # boolean before the bevel so the slot edge gets rounded too
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_move_to_index(modifier="cut", index=0)


def rrect_prism(name, w, h, r, z0, z1, mat, seg=10, bevel=0.03):
    """Rounded-rectangle outline (corner radius r) extruded from z0 to z1, rims softly bevelled."""
    bm = bmesh.new()
    pts = []
    for cx, cy, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90),
                       (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for s in range(seg + 1):
            a = math.radians(a0 + 90 * s / seg)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    face = bm.faces.new([bm.verts.new((x, y, z0)) for x, y in pts])
    ext = bmesh.ops.extrude_face_region(bm, geom=[face])
    bmesh.ops.translate(bm, vec=(0, 0, z1 - z0), verts=[v for v in ext["geom"] if isinstance(v, bmesh.types.BMVert)])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.materials.append(mat)
    for p in me.polygons: p.use_smooth = True
    if bevel > 0:
        bv = ob.modifiers.new("bevel", "BEVEL")
        bv.width = bevel
        bv.segments = 3
        bv.limit_method = "ANGLE"
        bv.angle_limit = math.radians(40)
        bv.harden_normals = True
    return ob


def emission_gradient(m, stops, scale, offset=(0, 0, 0), strength=1.0):
    """Drives m's emission colour with an elliptical gradient (object space) through `stops`
    [(pos, rgb)], pos 0 = rim, 1 = centre."""
    nt = m.node_tree
    N, L = nt.nodes, nt.links
    tc = N.new("ShaderNodeTexCoord")
    mp = N.new("ShaderNodeMapping")
    mp.inputs["Location"].default_value = offset
    mp.inputs["Scale"].default_value = scale
    L.new(tc.outputs["Object"], mp.inputs[0])
    grad = N.new("ShaderNodeTexGradient")
    grad.gradient_type = "SPHERICAL"
    L.new(mp.outputs[0], grad.inputs[0])
    ramp = N.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    while len(els) < len(stops): els.new(0.5)
    for e, (pos, rgb) in zip(els, stops):
        e.position = pos
        e.color = (*rgb, 1)
    L.new(grad.outputs[0], ramp.inputs[0])
    b = N["Principled BSDF"]
    L.new(ramp.outputs[0], b.inputs["Emission Color"])
    b.inputs["Emission Strength"].default_value = strength


SLOT_W, SLOT_H = 3.55, 1.36


def bezel():
    """Thick black frame with generously rounded corners around a recessed window (as in the
    original ensemble's switch graphic)."""
    plate = material("Bezel", (0.005, 0.005, 0.006), rough=0.55, spec=0.3, coat=0.1, coat_rough=0.3)
    b = rrect_prism("Bezel", 4.6, 2.0, 0.34, 0.0, 0.26, plate, bevel=0.07)
    c = rrect_prism("Slot", SLOT_W, SLOT_H, 0.1, -0.5, 1.0, plate, bevel=0.0)
    cut(b, c)
    floor = material("SlotFloor", (0.004, 0.002, 0.002), rough=0.8)
    rounded_box("Floor", (SLOT_W, SLOT_H, 0.04), (0, 0, 0.0), 0.01, floor, 1)
    return b


def rocker(name, on, mats):
    """Translucent rocker cap filling the window: a flat face with one raised end whose inclined
    face catches the light (right end when off, left end when on; the on state keeps a narrow
    flat lip beyond the ramp). mats = [face, ramp, lip]."""
    hw = SLOT_W / 2 - 0.03
    hy = SLOT_H / 2 - 0.035
    lo, hi = 0.07, 0.22
    lip = 0.3 if on else 0.04
    r0, r1 = hw - lip - 0.46, hw - lip     # ramp start / end (right-hand version)
    prof = [(-hw, 0.0), (hw, 0.0), (hw, hi), (r1, hi), (r0, lo), (-hw, lo)]
    if on: prof = [(-x, z) for x, z in reversed(prof)]
    bm = bmesh.new()
    face = bm.faces.new([bm.verts.new((x, -hy, z)) for x, z in prof])
    ext = bmesh.ops.extrude_face_region(bm, geom=[face])
    bmesh.ops.translate(bm, vec=(0, 2 * hy, 0), verts=[v for v in ext["geom"] if isinstance(v, bmesh.types.BMVert)])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    for f in bm.faces:
        n = f.normal
        if abs(n.x) > 0.2 and n.z > 0.2: f.material_index = 1
        elif on and n.z > 0.9 and f.calc_center_median().x < -hw + lip + 0.01: f.material_index = 2
        else: f.material_index = 0
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    for m in mats: me.materials.append(m)
    bv = ob.modifiers.new("bevel", "BEVEL")
    bv.width = 0.03
    bv.segments = 3
    bv.limit_method = "ANGLE"
    bv.angle_limit = math.radians(10)
    bv.harden_normals = True
    return ob


def render_switch(name, build):
    sc = reset_scene(SWITCH_PX, SWITCH_SPAN)
    tiles = []
    tmp = os.path.join(OUT, "_frame.png")
    for on in (False, True):
        # rebuild per state (materials differ)
        for ob in list(bpy.data.objects):
            if ob.get("ctl"): bpy.data.objects.remove(ob, do_unlink=True)
        before = set(bpy.data.objects)
        build(on)
        for ob in set(bpy.data.objects) - before: ob["ctl"] = 1
        sc.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        tiles.append(read_png(tmp))
    os.remove(tmp)
    out = os.path.join(OUT, ("preview_" if PREVIEW else "") + name + ".png")
    write_png(out, np.concatenate(tiles, axis=0))
    print("wrote", out)


def build_switch(on):
    """Illuminated rocker. Off: dark red window, lit edge of the raised right end.
    On: the cap glows from inside (hot yellow centre, orange to deep red at the rim) and the
    raised left end reads as a pale band."""
    bezel()
    if on:
        face = material("FaceOn", (0.5, 0.08, 0.01), rough=0.25, coat=1.0, coat_rough=0.04)
        emission_gradient(face, [(0.0, (0.16, 0.012, 0.0)), (0.3, (0.55, 0.07, 0.005)),
                                 (0.65, (1.0, 0.36, 0.03)), (0.93, (1.0, 0.75, 0.15))],
                          scale=(0.55, 1.2, 1.0), offset=(0.05, 0, 0), strength=1.15)
        ramp = material("RampOn", (0.5, 0.25, 0.15), rough=0.3, coat=1.0, coat_rough=0.04,
                        emit=(1.0, 0.6, 0.4), emit_strength=0.78)
        lipm = material("LipOn", (0.2, 0.02, 0.0), rough=0.3, coat=1.0, coat_rough=0.04,
                        emit=(0.55, 0.06, 0.01), emit_strength=0.6)
    else:
        face = material("FaceOff", (0.012, 0.002, 0.001), rough=0.4, spec=0.3, coat=0.25, coat_rough=0.12)
        emission_gradient(face, [(0.0, (0.022, 0.003, 0.001)), (1.0, (0.008, 0.001, 0.0))],
                          scale=(0.55, 1.4, 1.0), strength=1.0)
        ramp = material("RampOff", (0.4, 0.13, 0.08), rough=0.5, coat=0.5, coat_rough=0.1,
                        emit=(0.4, 0.12, 0.07), emit_strength=0.3)
        lipm = ramp
    rocker("Rocker", on, [face, ramp, lipm])
    if on:
        ld = bpy.data.lights.new("Glow", "POINT")
        ld.energy = 3.0
        ld.color = (1.0, 0.4, 0.1)
        ld.shadow_soft_size = 0.5
        lo = bpy.data.objects.new("Glow", ld)
        lo.location = (0.0, 0, 0.35)
        lo.visible_glossy = False
        bpy.context.scene.collection.objects.link(lo)


# ---------------------------------------------------------------------------------------------
for n in NAMES:
    if n == "knob_black": render_knob(n, lambda: build_knob_dark(False))
    elif n == "knob_green": render_knob(n, lambda: build_knob_dark(True))
    elif n == "knob_white": render_knob(n, build_knob_white)
    elif n == "switch": render_switch(n, build_switch)
