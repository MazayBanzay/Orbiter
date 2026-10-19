# THE SUIT'S BACK BLOCK, designed from the systems (not the old box): the 6 layouts A-F in 3D on her (the user,
# 2026-10-18: «проектируй с нуля»; «визуал в 3d»). Suit_T10's systems: battery 6 kWh at the MPU cells' density
# (~3.1 kWh/kg: 1.9 kg, ~1.2 L), O2 1.0 kg + N2 1.5 kg at 70 MPa (~1.4 + 2.7 L, ~5 L with the bottles), CO2 swing beds
# (2 x 1.5 L), heat pump + fans + valves + the field's and drives' power units (~2.5 L); ~930 W of heat to reject at
# the peak (radiators silver, finned). Drinking water stays in the suit's front. The lumbar stays free (she bends:
# suit_strain.py), the scapulae ride with the arms. Render only, on astronavigator_suit2.blend (Suit_T10 + «Бор»).
#   blender -b astronavigator_suit2.blend --python back_layouts3d.py   (env BL3_OUT, BL3_ONLY "A,C")
import bpy, bmesh, math, os, traceback
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.environ.get("BL3_OUT") or os.path.join(HERE, "..", "suit_mockups", "back_block3d"); os.makedirs(OUT, exist_ok=True)
ONLY = [s for s in os.environ.get("BL3_ONLY", "").split(",") if s]
LOG = open(os.path.join(OUT, "bl3_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
try:
    bpy.data.objects["Astronavigator"].data.pose_position = 'REST'
    for o in bpy.data.objects:
        if o.name in ("Pack", "MarkPack", "BackRails", "CableL", "CableR") or o.name.startswith("Jet"): o.hide_render = True
    def mk(name, col, metal, rough):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
        m.diffuse_color = (*col, 1); return m
    SHELL = mk("B3_shell", (0.86, 0.87, 0.88), 0.0, 0.40)
    FRAME = mk("B3_frame", (0.60, 0.61, 0.63), 1.0, 0.30)
    DARK = mk("B3_dark", (0.12, 0.125, 0.13), 0.3, 0.5)
    RAD = mk("B3_rad", (0.80, 0.82, 0.84), 1.0, 0.18)       # radiators: polished light metal
    RED = mk("B3_red", (0.78, 0.09, 0.07), 0.1, 0.35)
    O2C = mk("B3_o2", (0.12, 0.35, 0.72), 0.2, 0.35)        # oxygen blue
    N2C = mk("B3_n2", (0.06, 0.06, 0.06), 0.2, 0.40)        # nitrogen black
    SORB = mk("B3_sorb", (0.20, 0.42, 0.40), 0.1, 0.5)      # the CO2 beds: dark teal
    HOSE = mk("B3_hose", (0.30, 0.31, 0.32), 0.0, 0.7)
    parts = []
    def link(o): sc.collection.objects.link(o); parts.append(o); return o
    def obj(name, bm, m, smooth=False, bevel=0.0):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        me.materials.append(m); o = link(bpy.data.objects.new(name, me))
        if bevel:
            b = o.modifiers.new("b", 'BEVEL'); b.width = bevel; b.segments = 3; b.limit_method = 'ANGLE'
        return o
    def box(name, c, s, m, bevel=0.006, rot=None):
        bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0); bmesh.ops.scale(bm, vec=Vector(s), verts=bm.verts)
        if rot is not None: bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=rot)
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, bevel=bevel)
    def cyl(name, c, r, L, axis, m, seg=28, bevel=0.003):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r, depth=L)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix())
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, smooth=True, bevel=bevel)
    def bottle(c, r, L, axis, band):
        ax = Vector(axis).normalized(); c = Vector(c)
        cyl("btl", c, r, L - 1.2 * r, ax, FRAME)
        for sg in (-1, 1):
            bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=r)
            bmesh.ops.scale(bm, vec=(1, 1, 0.6), verts=bm.verts)
            bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(ax).to_matrix())
            bmesh.ops.translate(bm, vec=c + ax * sg * (L / 2 - 0.6 * r), verts=bm.verts); obj("btle", bm, FRAME, smooth=True)
        cyl("band", c, r * 1.03, L * 0.30, ax, band, bevel=0.001)
        cyl("valve", c + ax * (L / 2 + 0.006), r * 0.35, 0.02, ax, DARK)
    def tube(name, pts, r, m, closed=False):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 3; cd.use_fill_caps = True
        sp = cd.splines.new('BEZIER'); sp.bezier_points.add(len(pts) - 1)
        for q, p in zip(sp.bezier_points, pts): q.co = p; q.handle_left_type = q.handle_right_type = 'AUTO'
        cd.materials.append(m); return link(bpy.data.objects.new(name, cd))
    def fins_panel(c, w, h, n_axis, up, m=RAD, pitch=0.02, depth=0.014, t=0.0025):
        """a radiator: a base plate with fins across it; n_axis the outward normal, up its long direction"""
        n = Vector(n_axis).normalized(); u = Vector(up).normalized(); a = u.cross(n).normalized(); c = Vector(c)
        R = Matrix((a, u, n)).transposed()
        box("radp", c, (w, h, 0.004), m, bevel=0.001, rot=R)
        k = int(w / pitch)
        for i in range(k + 1):
            x = -w / 2 + i * w / k
            box("fin", c + a * x + n * (depth / 2 + 0.002), (t, h - 0.01, depth), m, bevel=0.0006, rot=R)
    def stripe(c, w, h=0.010, n=(0, 1, 0)):
        box("stripe", Vector(c) + Vector(n) * 0.001, (w, 0.002, h), RED, bevel=0.0004)
    def handle(c, w=0.05):
        c = Vector(c); cyl("hdl", c + Vector((0, 0.016, 0)), 0.0045, w, (1, 0, 0), FRAME)
        for sx in (-1, 1): cyl("hdl", c + Vector((sx * w / 2, 0.008, 0)), 0.0045, 0.016, (0, 1, 0), FRAME)
    def ring_back(x, out=0.0):            # the hip ring's back arc (x +-0.22, y -0.183..0.126, centre y -0.028)
        a, b, yc = 0.222 + out, 0.154 + out, -0.0285
        return Vector((x, yc + b * math.sqrt(max(0.0, 1 - (x / a) ** 2)), 0.0))

    LAYOUTS = []
    def layout(k, title):
        def d(f): LAYOUTS.append((k, title, f)); return f
        return d

    @layout("A", "Хребет")
    def A():
        # a segmented column along the spine (vertebra-like links, each on its bone), the bottles paired behind it,
        # the radiator panels hinged on its sides - open in the shot (folded flat along it when the heat is low)
        for z, h in ((1.44, 0.07), (1.36, 0.07), (1.28, 0.07)):
            box("vert", (0, 0.135, z), (0.10, 0.08, h - 0.008), SHELL, bevel=0.012)
            box("vlink", (0, 0.098, z), (0.06, 0.01, h - 0.02), FRAME, bevel=0.002)
        for sx, band in ((-1, O2C), (1, N2C)): bottle((sx * 0.042, 0.205, 1.36), 0.034, 0.24, (0, 0, 1), band)
        box("cap", (0, 0.17, 1.495), (0.14, 0.13, 0.02), FRAME, bevel=0.004)
        for sx in (-1, 1):
            cyl("hinge", (sx * 0.055, 0.17, 1.36), 0.007, 0.24, (0, 0, 1), FRAME)
            ang = math.radians(28) * sx
            nrm = Vector((math.sin(ang) * 0.0 + 0.0, 1, 0));
            d = Vector((sx * math.cos(math.radians(28)), math.sin(math.radians(28)), 0))
            ctr = Vector((sx * 0.055, 0.17, 1.36)) + d * 0.15
            fins_panel(ctr, 0.28, 0.22, Vector((0, 0, 1)).cross(d) * sx, (0, 0, 1), pitch=0.025)
        box("battp", (0, 0.135, 0.985), (0.14, 0.05, 0.10), SHELL, bevel=0.01); handle((0, 0.16, 0.985), 0.04)
        tube("hose", [Vector((0, 0.12, 1.035)), Vector((0, 0.115, 1.14)), Vector((0, 0.12, 1.245))], 0.009, HOSE)
        stripe((0, 0.176, 1.455), 0.06)

    @layout("B", "Пояс и лопатки")
    def B():
        # the masses on the hip ring the frame already carries: two arc bottles along its back (O2 over N2), the
        # battery cassette between them; on the back only the CO2 beds and the pump on the spine, and two radiator
        # plates on the scapulae (they ride with the shoulders)
        for zz, r, band in ((1.075, 0.026, O2C), (1.05, 0.032, N2C)):
            pts = [ring_back(x, 0.04 + r) + Vector((0, 0, zz - 0.0)) for x in (-0.19, -0.12, -0.05, 0.05, 0.12, 0.19)]
            tube("arc", pts, r, FRAME)
            mid = [ring_back(x, 0.04 + r) + Vector((0, 0, zz)) for x in (-0.05, 0.0, 0.05)]
            tube("arcband", mid, r * 1.04, band)
        box("batt", (0, 0.205, 0.975), (0.15, 0.045, 0.09), SHELL, bevel=0.01); handle((0, 0.228, 0.975), 0.04)
        box("spine", (0, 0.125, 1.36), (0.09, 0.07, 0.17), SHELL, bevel=0.014)
        box("beds", (0, 0.162, 1.36), (0.07, 0.006, 0.13), SORB, bevel=0.002)
        for sx in (-1, 1):
            fins_panel((sx * 0.11, 0.115, 1.36), 0.11, 0.17, (sx * 0.25, 1, 0.08), (0, 0, 1), pitch=0.018)
        tube("hose", [Vector((0, 0.12, 1.27)), Vector((0, 0.10, 1.15)), Vector((0, 0.15, 1.04))], 0.009, HOSE)
        stripe((0, 0.161, 1.43), 0.05)

    @layout("C", "Люк")
    def C():
        # the Soviet lineage (Orlan): she enters from behind; the life support is the hatch. Hinge on her right,
        # two latches on the left; the bottles in its sides, the beds, pump and battery in the middle; outer face finned
        box("collar", (0, 0.098, 1.285), (0.38, 0.016, 0.40), FRAME, bevel=0.004)
        box("hatch", (0, 0.155, 1.285), (0.36, 0.10, 0.38), SHELL, bevel=0.03)
        fins_panel((0, 0.208, 1.30), 0.30, 0.30, (0, 1, 0), (0, 0, 1), pitch=0.02)
        for sx in (-1, 1): cyl("hinge" if sx < 0 else "latch", (sx * 0.185, 0.14, 1.20), 0.010, 0.06, (0, 0, 1), FRAME)
        for z in (1.12, 1.30, 1.44): cyl("hinge", (-0.185, 0.14, z), 0.010, 0.05, (0, 0, 1), FRAME)
        for z in (1.18, 1.40):
            box("latch", (0.19, 0.16, z), (0.03, 0.03, 0.04), DARK, bevel=0.004)
        for sx, band in ((-1, O2C), (1, N2C)): cyl("sideband", (sx * 0.176, 0.16, 1.30), 0.018, 0.10, (0, 0, 1), band)
        stripe((0, 0.215, 1.468), 0.20)
        handle((0, 0.206, 1.12), 0.08)

    @layout("D", "Броня-радиатор")
    def D():
        # a thin unit on the upper back (beds, pump, battery); the gas bottles in two pods on the hip ring's sides;
        # the heat goes by coolant lines over the shoulders into the borazon plates of the chest (cBN spreads it,
        # the plates radiate) - the set «Бор» becomes the radiator; small side strips for the rest
        box("unit", (0, 0.118, 1.38), (0.24, 0.05, 0.16), SHELL, bevel=0.014)
        box("beds", (0, 0.145, 1.38), (0.18, 0.004, 0.11), SORB, bevel=0.002)
        for sx, band in ((-1, O2C), (1, N2C)):
            bottle((sx * 0.235, 0.01, 1.00), 0.032, 0.17, (0, 0, 1), band)
            box("pod", (sx * 0.235, 0.01, 0.905), (0.05, 0.05, 0.02), FRAME, bevel=0.003)
        for sx in (-1, 1):
            tube("cool", [Vector((sx * 0.09, 0.12, 1.45)), Vector((sx * 0.10, 0.06, 1.50)), Vector((sx * 0.10, -0.06, 1.48)),
                          Vector((sx * 0.08, -0.14, 1.42))], 0.006, RED)
            tube("coolr", [Vector((sx * 0.06, 0.12, 1.45)), Vector((sx * 0.07, 0.05, 1.505)), Vector((sx * 0.07, -0.07, 1.485)),
                           Vector((sx * 0.055, -0.15, 1.42))], 0.006, O2C)
            fins_panel((sx * 0.16, 0.06, 1.30), 0.06, 0.16, (sx, 0.7, 0), (0, 0, 1), pitch=0.015)
        stripe((0, 0.144, 1.455), 0.12)

    @layout("E", "Два этажа")
    def E():
        # the upper unit on the shoulder blades' line (gases across its top, the beds, its back a radiator), the lower
        # unit on the hip ring (battery, the field's and drives' power); the lumbar between them free - two flexible
        # lines cross it
        box("upper", (0, 0.135, 1.37), (0.32, 0.09, 0.18), SHELL, bevel=0.018)
        bottle((0, 0.15, 1.475), 0.034, 0.30, (1, 0, 0), O2C)
        bottle((0, 0.20, 1.475), 0.034, 0.30, (1, 0, 0), N2C)
        fins_panel((0, 0.183, 1.355), 0.28, 0.14, (0, 1, 0), (0, 0, 1), pitch=0.02)
        box("lower", (0, 0.165, 0.995), (0.30, 0.07, 0.10), SHELL, bevel=0.014)
        handle((0, 0.20, 0.995), 0.06); stripe((0, 0.201, 1.035), 0.20, 0.008)
        for sx in (-1, 1):
            tube("hose", [Vector((sx * 0.07, 0.15, 1.28)), Vector((sx * 0.075, 0.12, 1.16)), Vector((sx * 0.07, 0.17, 1.045))], 0.011, HOSE)

    @layout("F", "Кассеты")
    def F():
        # a frame with four cassettes put in from behind (gases, CO2 beds, power), swapped at a station or the MPU;
        # the radiator panels at its sides, open in the shot
        box("back", (0, 0.10, 1.29), (0.30, 0.012, 0.34), FRAME, bevel=0.003)
        for x in (-0.15, 0.15): box("post", (x, 0.155, 1.29), (0.012, 0.10, 0.34), FRAME, bevel=0.002)
        for z in (1.12, 1.29, 1.46): box("shelf", (0, 0.155, z), (0.30, 0.10, 0.010), FRAME, bevel=0.002)
        cells = [(-0.072, 1.375, O2C), (0.072, 1.375, N2C), (-0.072, 1.205, SORB), (0.072, 1.205, None)]
        for x, z, band in cells:
            box("cas", (x, 0.155, z), (0.13, 0.09, 0.15), SHELL, bevel=0.008)
            if band is not None: box("cband", (x, 0.2005, z + 0.05), (0.11, 0.002, 0.016), band, bevel=0.0004)
            handle((x, 0.200, z - 0.03), 0.05)
        for sx in (-1, 1):
            d = Vector((sx * math.cos(math.radians(20)), math.sin(math.radians(20)), 0))
            fins_panel(Vector((sx * 0.156, 0.17, 1.29)) + d * 0.12, 0.22, 0.30, Vector((0, 0, 1)).cross(d) * sx, (0, 0, 1), pitch=0.022)
        stripe((0, 0.106, 1.48), 0.16)

    # ---- the render ----
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 560, 640
    if sc.world is None: sc.world = bpy.data.worlds.new("B3_world")
    sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None)
    if bg: bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("B3_key", 3.0, (42, 0, 145)), ("B3_fill", 1.4, (65, 0, -40)), ("B3_back", 2.2, (55, 0, 200)), ("B3_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("B3_cam"); cd.sensor_fit = 'VERTICAL'; cd.lens = 45
    cam = bpy.data.objects.new("B3_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0.0, 0.08, 1.20))
    VIEWS = {"q34": T + Vector((1.35, 1.85, 0.40)), "back": T + Vector((0.0, 2.30, 0.18)), "side": T + Vector((2.30, -0.05, 0.12))}
    for key, title, fn in LAYOUTS:
        if ONLY and key not in ONLY: continue
        fn()
        for vn, loc in VIEWS.items():
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "b3_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear(); print("rendered", key, title, file=LOG); LOG.flush()
    print("DONE", file=LOG)
except Exception:
    traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
