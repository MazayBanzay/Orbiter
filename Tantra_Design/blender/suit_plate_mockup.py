# The chest armour plate (Cuirass) of the user's suit, five refinements - renders only, nothing for the game
# (the user, 2026-10-16: the plate and the arcs are armour and stay - better visuals and engineering, not removed; the
# legs are not to be touched). The arcs (ShoulderArcs), the lugs (PlateLugs) and the hip ring keep the load path as it
# is: a fall on the plate goes into the frame, not into her sternum.
#   blender -b astronavigator_suit.blend --python suit_plate_mockup.py   -> Tantra_Design/suit_mockups/plate_*.png
#   0 now          the plate as it is
#   1 Доработка    the same outline, curved both ways, the top laid back (her chin sees her hands), rolled edges,
#                  the upper corners chamfered for the arms' swing
#   2 Ламели       three overlapping bands (she bends at the waist), each with a rolled edge, on two vertical straps
#   3 Кассета      the layers shown: ceramic face tiles in a clamping frame over the dark polyethylene backing,
#                  hinge pins at the lugs (it swings down to reach the chest connectors), the latch under the arcs
#   4 Рёбра        one piece with a raised spine and two stiffening ribs, the apron lower over the upper belly
#   5 Каркас-рама  the arcs run on round the plate as one tube (a roll cage), the panel thinner inside it
import bpy, bmesh, math, os, traceback
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "..", "suit_mockups"); os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
try:
    PLATE = bpy.data.materials["SuitPlate"]; FRAME = bpy.data.materials["Frame"]; REDM = bpy.data.materials["MarkingRed"]
    def mk(name, col, rough):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; return m
    BACKING = mk("pm_backing", (0.07, 0.07, 0.08), 0.8)            # the polyethylene behind the ceramic: dark
    CERAMIC = mk("pm_ceramic", (0.80, 0.80, 0.78), 0.45)

    cu = bpy.data.objects["Cuirass"]; mark = bpy.data.objects["MarkChest"]
    Z0, Z1, HW = 1.036, 1.436, 0.188                                 # the plate now: bottom, top, half width
    def y_now(u, v): return -0.215 + (-0.286 + 0.215) * v + 0.047 * u * u      # its front: leans forward to the top

    def link(o, coll): coll.objects.link(o); return o
    def surface(name, coll, m, f, nu=40, nv=30, thick=0.022, umin=-1, umax=1, vmin=0, vmax=1):
        """a plate from f(u, v) -> point (u across -1..1, v up 0..1), given a thickness towards her"""
        bm = bmesh.new(); grid = []
        for j in range(nv + 1):
            row = []
            for i in range(nu + 1):
                u = umin + (umax - umin) * i / nu; v = vmin + (vmax - vmin) * j / nv
                row.append(bm.verts.new(f(u, v)))
            grid.append(row)
        for j in range(nv):
            for i in range(nu): bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for p in me.polygons: p.use_smooth = True
        o = bpy.data.objects.new(name, me); me.materials.append(m); link(o, coll)
        s = o.modifiers.new("t", 'SOLIDIFY'); s.thickness = thick; s.offset = -1.0; s.use_even_offset = True   # towards her: f() is the front face
        b = o.modifiers.new("b", 'BEVEL'); b.width = 0.004; b.segments = 2; b.limit_method = 'ANGLE'
        return o
    def tube(name, pts, r, coll, m, closed=False):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 4; cd.use_fill_caps = True
        sp = cd.splines.new('POLY'); sp.points.add(len(pts) - 1)
        for p, q in zip(sp.points, pts): p.co = (*q, 1)
        sp.use_cyclic_u = closed; cd.materials.append(m)
        return link(bpy.data.objects.new(name, cd), coll)
    def outline(f, hw_of_v, n=60):
        """the plate's rim as points on its front face, round the outline (bottom, right, top, left)"""
        pts = []
        for k in range(n): pts.append(f(-1 + 2 * k / n, 0.0))
        for k in range(n): pts.append(f(1.0, k / n))
        for k in range(n): pts.append(f(1 - 2 * k / n, 1.0))
        for k in range(n): pts.append(f(-1.0, 1 - k / n))
        return pts
    def mark_on(coll, f):
        """the chest marking moved onto the new face"""
        m2 = mark.copy(); m2.data = mark.data; link(m2, coll)
        m2.location.y += f(0, (1.37 - Z0) / (Z1 - Z0))[1] - (-0.283)
        mark.hide_render = True

    # the shapes ------------------------------------------------------------------------------------------------
    def chamfer(u, v, top=0.72, cut=0.08):                       # the upper corners cut away for the arms
        k = max(0.0, (v - top) / (1 - top)); return HW - cut * k * k
    def f1(u, v, z0=Z0, z1=Z1):
        hw = chamfer(u, v); z = z0 + (z1 - z0) * v
        y = -0.212 + (-0.252 + 0.212) * v - 0.018 * 4 * v * (1 - v) + 0.050 * u * u   # laid back 3 cm, bulged 2 cm
        return (u * hw, y, z)
    def build1(coll):
        surface("p1", coll, PLATE, f1)
        tube("rim1", outline(f1, None), 0.007, coll, FRAME, closed=True)
        mark_on(coll, f1)
    def build2(coll):
        bands = [(0.0, 0.38), (0.31, 0.69), (0.62, 1.0)]            # overlapping bands, the upper over the lower
        for k, (a, b) in enumerate(bands):
            off = -0.006 * k                                         # each higher band a little out: it laps the lower
            fk = lambda u, v, a=a, b=b, off=off: (lambda p: (p[0], p[1] + off, p[2]))(f1(u, a + (b - a) * v))
            surface("band%d" % k, coll, PLATE, fk, nv=12)
            tube("bandrim%d" % k, [fk(-1 + 2 * i / 60, 0.0) for i in range(61)], 0.006, coll, FRAME)
        for sx in (-0.55, 0.55):                                     # the straps the bands ride on
            tube("strap%+.0f" % sx, [(lambda p: (p[0], p[1] - 0.026, p[2]))(f1(sx, i / 20)) for i in range(21)], 0.009, coll, FRAME)
        mark_on(coll, lambda u, v: (lambda p: (p[0], p[1] - 0.012, p[2]))(f1(u, v)))
    def build3(coll):
        surface("back3", coll, BACKING, f1, thick=0.026)
        # the ceramic face: tiles 7 x 6 with 4 mm joints, each a small plate standing 6 mm proud of the backing
        nx, nz = 6, 6
        for i in range(nx):
            for j in range(nz):
                ua, ub = -1 + 2 * i / nx + 0.012, -1 + 2 * (i + 1) / nx - 0.012
                va, vb = j / nz + 0.012, (j + 1) / nz - 0.012
                ft = lambda u, v, ua=ua, ub=ub, va=va, vb=vb: (lambda p: (p[0], p[1] - 0.006, p[2]))(f1(ua + (ub - ua) * (u + 1) / 2, va + (vb - va) * v))
                c = f1((ua + ub) / 2, (va + vb) / 2)
                if abs(c[0]) > chamfer(0, (va + vb) / 2) - 0.01: continue
                surface("tile%d_%d" % (i, j), coll, CERAMIC, ft, nu=4, nv=4, thick=0.006)
        tube("frame3", outline(lambda u, v: (lambda p: (p[0], p[1] - 0.004, p[2]))(f1(u, v)), None), 0.009, coll, FRAME, closed=True)
        for sx in (-0.07, 0.07):                                      # the hinge pins down at the lugs
            tube("pin%+.2f" % sx, [(sx - 0.03, -0.19, 1.04), (sx + 0.03, -0.19, 1.04)], 0.011, coll, FRAME)
        tube("latch", [(-0.04, -0.262, 1.44), (0.04, -0.262, 1.44)], 0.010, coll, REDM)    # the latch under the arcs
        mark_on(coll, lambda u, v: (lambda p: (p[0], p[1] - 0.008, p[2]))(f1(u, v)))
    def f4(u, v):                                                  # lower: an apron to 0.99 over the upper belly
        return f1(u, v, z0=0.995)
    def build4(coll):
        surface("p4", coll, PLATE, f4)
        tube("rim4", outline(f4, None), 0.007, coll, FRAME, closed=True)
        spine = [(lambda p: (p[0], p[1] - 0.006, p[2]))(f4(0, v / 30)) for v in range(31)]
        tube("spine4", spine, 0.012, coll, PLATE)
        for su in (-0.62, 0.62):
            tube("rib%+.1f" % su, [(lambda p: (p[0], p[1] - 0.004, p[2]))(f4(su, 0.08 + 0.84 * v / 30)) for v in range(31)], 0.007, coll, PLATE)
        mark_on(coll, lambda u, v: (lambda p: (p[0], p[1] - 0.010, p[2]))(f1(u, v)))
    def build5(coll):
        # the panel inside the frame, thinner; the frame a tube that comes from the arcs' ends (z 1.44 at the plate's
        # top corners) and goes round the plate down to the lugs
        surface("p5", coll, PLATE, lambda u, v: f1(u * 0.93, 0.03 + 0.94 * v), thick=0.014)
        tube("cage5", outline(lambda u, v: (lambda p: (p[0], p[1] + 0.004, p[2]))(f1(u, v)), None), 0.012, coll, FRAME, closed=True)
        for sx in (-1, 1):                                            # the arcs' ends brought down into the cage's corners
            a = f1(sx * 0.70, 1.0)
            tube("arcjoin%d" % sx, [(a[0], a[1] + 0.004, a[2]), (a[0] * 0.85, a[1] + 0.02, a[2] + 0.03), (sx * 0.12, -0.215, 1.46)], 0.012, coll, FRAME)
            tube("diag%d" % sx, [f1(sx * 0.93, 0.0), f1(0.0, 0.5), f1(-sx * 0.93, 1.0)] if sx > 0 else [f1(-0.93, 0.0), f1(0.0, 0.5), f1(0.93, 1.0)], 0.005, coll, FRAME)
        mark_on(coll, lambda u, v: (lambda p: (p[0], p[1] - 0.004, p[2]))(f1(u, v)))

    VARIANTS = [("0_now", "Сейчас", None), ("1_dorabotka", "1 Доработка", build1), ("2_lameli", "2 Ламели", build2),
                ("3_kasseta", "3 Кассета", build3), ("4_ryobra", "4 Рёбра", build4), ("5_rama", "5 Каркас-рама", build5)]

    # the stage: soft, as the earlier suit renders ------------------------------------------------------------
    for o in list(bpy.data.objects):
        if o.type in ('LIGHT', 'CAMERA'): bpy.data.objects.remove(o)
    for eng in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE', 'CYCLES'):
        try: sc.render.engine = eng; break
        except Exception: pass
    sc.render.resolution_x, sc.render.resolution_y = 900, 1100
    sc.world = sc.world or bpy.data.worlds.new("w"); sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None) or sc.world.node_tree.nodes.new("ShaderNodeBackground")
    bg.inputs[0].default_value = (0.20, 0.21, 0.23, 1); bg.inputs[1].default_value = 0.5
    def light(name, loc, energy, size):
        ld = bpy.data.lights.new(name, 'AREA'); ld.energy = energy; ld.size = size
        o = bpy.data.objects.new(name, ld); o.location = loc; sc.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (Vector((0, 0, 1.2)) - Vector(loc)).to_track_quat('-Z', 'Y')
    light("key", (-2.0, -2.6, 2.8), 220, 2.0); light("fill", (2.4, -1.6, 1.8), 70, 2.0); light("rim", (0.5, 2.8, 2.4), 140, 1.5)
    cd = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    def shoot(path, loc, tgt, lens):
        cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = path; bpy.ops.render.render(write_still=True)

    for key, title, build in VARIANTS:
        coll = bpy.data.collections.new(key); sc.collection.children.link(coll)
        cu.hide_render = build is not None; mark.hide_render = False
        if build: build(coll)
        shoot(os.path.join(OUT, "plate_%s_34.png" % key), (-0.95, -1.55, 1.42), (0, -0.16, 1.22), 50)
        shoot(os.path.join(OUT, "plate_%s_side.png" % key), (1.55, -0.55, 1.30), (0, -0.12, 1.22), 50)
        for o in list(coll.objects): bpy.data.objects.remove(o)
        bpy.data.collections.remove(coll)
    open(os.path.join(OUT, "plate_done.txt"), "w").write("ok")
except Exception:
    open(os.path.join(OUT, "plate_error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
