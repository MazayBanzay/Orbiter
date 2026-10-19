# BORON ZONE PROTECTION, 10 concepts (the user, 2026-10-18): no breastplate; the epoch of new materials - zones protected by
# borazon (cubic boron nitride) tiles and plates: the chest, the shoulders, the hips/thighs, the knees (the back is the pack's).
# The zones from the impact and marrow analysis (sternum, iliac crests, femoral and humeral heads, knees). The user's suit
# without the cuirass, its marking, lugs and arcs; the helmet = the chosen «щиток затылка» (helmet_cheek.py: glass b,
# hex ports, nape shield). Everything projected onto HER mesh (SuitBody), nothing of hers deformed. Render only.
#   blender -b astronavigator_suit.blend --python boron_mockups.py   (env BM_OUT: the output folder; BM_ONLY: "1,4")
import bpy, bmesh, math, os, traceback
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.environ.get("BM_OUT") or os.path.join(HERE, "..", "suit_mockups", "boron"); os.makedirs(OUT, exist_ok=True)
ONLY = [s for s in os.environ.get("BM_ONLY", "").split(",") if s]
LOGF = open(os.path.join(OUT, "bm_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
try:
    # ---- the helmet: helmet_cheek.py's parts, variant k1 (its definitions only; its own render loop not run) ----
    src = open(os.path.join(HERE, "helmet_cheek.py"), encoding="utf-8").read()
    src = src[:src.index("REST = {n:")].replace('LOG = open(os.path.join(OUT, "hk_log.txt"), "w", encoding="utf-8")', "LOG = LOGF")
    HK = {"LOGF": LOGF, "__name__": "hk"}; exec(compile(src, "helmet_cheek.py", "exec"), HK)
    HK["cut"](HK["W"]); HK["glass"](HK["W"]); HK["rim"](HK["W"]); HK["hexport"](1); HK["hexport"](-1); HK["shield"]()
    for n in ("HelmPods", "Cuirass", "MarkChest", "PlateLugs", "ShoulderArcs"):
        if n in bpy.data.objects: bpy.data.objects[n].hide_render = True

    arm = bpy.data.objects["Astronavigator"]; arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    BODY = BVHTree.FromObject(bpy.data.objects["SuitBody"], dg)       # world space: SuitBody has an identity matrix
    MW = bpy.data.objects["SuitBody"].matrix_world
    if MW != Matrix.Identity(4): raise RuntimeError("SuitBody is not at the origin: transform the BVH")

    def mk(name, col, metal, rough, spec=0.5, coat=0.0):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
        for k, v in (("Specular IOR Level", spec), ("Coat Weight", coat)):
            if k in b.inputs: b.inputs[k].default_value = v
        m.diffuse_color = (*col, 1); return m
    # the 30th-century plate: binderless nanotwinned cBN, boron-10 enriched - honey-amber, translucent, polished
    # (the user, 2026-10-18: the colour of cBN crystals; not today's black binder-sintered PCBN)
    BORAZON = mk("BM_borazon", (0.30, 0.068, 0.0), 0.0, 0.16, 0.8, 0.6)
    _b = next(n for n in BORAZON.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    for k, v in (("Transmission Weight", 0.0), ("IOR", 2.12), ("Subsurface Weight", 0.0), ("Coat Roughness", 0.10), ("Coat IOR", 2.12)):
        if k in _b.inputs: _b.inputs[k].default_value = v
    if "Coat Tint" in _b.inputs: _b.inputs["Coat Tint"].default_value = (1.0, 0.62, 0.30, 1)   # the depth: amber under the polish
    try: BORAZON.surface_render_method = 'DITHERED'; BORAZON.use_screen_refraction = True
    except Exception: pass
    FRAME = mk("BM_frame", (0.62, 0.63, 0.65), 1.0, 0.32)                       # light metal (the arcs' colour)
    STRAP = mk("BM_strap", (0.30, 0.31, 0.32), 0.0, 0.8)                         # webbing
    POCKET = mk("BM_pocket", (0.80, 0.80, 0.78), 0.0, 0.85)                      # the suit's cloth, a shade off
    LBL = mk("BM_label", (0.05, 0.05, 0.05), 0.0, 0.6)
    parts = []
    def link(o): sc.collection.objects.link(o); parts.append(o); return o
    def mesh_obj(name, bm, m, smooth=True):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        me.materials.append(m); return link(bpy.data.objects.new(name, me))

    # ---- on her surface ----
    def hit(p, n0):
        """the body point under p looking along -n0 (a ray from outside); the closest point if the ray misses"""
        loc, nrm, i, d = BODY.ray_cast(p + n0 * 0.25, -n0, 0.6)
        if loc is None: loc, nrm, i, d = BODY.find_nearest(p)
        return loc
    class Zone:
        """a zone: its centre near her surface, its outward direction n0 and its 'up' along her (the frame a, b)"""
        def __init__(s, c, n0, up=(0, 0, 1)):
            s.n = Vector(n0).normalized(); up = Vector(up)
            s.a = up.cross(s.n).normalized()                      # across
            if s.a.length < 1e-6: s.a = Vector((1, 0, 0))
            s.b = s.n.cross(s.a).normalized(); s.c = Vector(c)    # along
        def at(s, u, v, gap): return hit(s.c + s.a * u + s.b * v, s.n) + s.n * gap
        def mirror(s):
            z = Zone((-s.c.x, s.c.y, s.c.z), (-s.n.x, s.n.y, s.n.z), (-s.b.x, s.b.y, s.b.z)); return z
    def normal_at(z, u, v, gap, e=0.008):
        p0 = z.at(u, v, gap); pa = z.at(u + e, v, gap); pb = z.at(u, v + e, gap)
        n = (pa - p0).cross(pb - p0).normalized()
        return n if n.dot(z.n) > 0 else -n
    def inside(poly, u, v):
        c = False; j = len(poly) - 1
        for i in range(len(poly)):
            (ui, vi), (uj, vj) = poly[i], poly[j]
            if (vi > v) != (vj > v) and u < (uj - ui) * (v - vi) / (vj - vi + 1e-12) + ui: c = not c
            j = i
        return c
    def chamfer_rect(w, h, ch):
        w, h = w / 2, h / 2; return [(-w + ch, -h), (w - ch, -h), (w, -h + ch), (w, h - ch), (w - ch, h), (-w + ch, h), (-w, h - ch), (-w, -h + ch)]
    def hexagon(r, rot=0.0): return [(r * math.cos(rot + k * math.pi / 3), r * math.sin(rot + k * math.pi / 3)) for k in range(6)]
    def tube(name, pts, r, m, closed=True, ns=8):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 2; cd.use_fill_caps = True
        sp = cd.splines.new('POLY'); sp.points.add(len(pts) - 1); sp.use_cyclic_u = closed
        for q, c in zip(sp.points, pts): q.co = (*c, 1)
        cd.materials.append(m); return link(bpy.data.objects.new(name, cd))

    def plate(name, z, poly, th=0.007, gap=0.006, rim=True, n=24, mat=None):
        """one plate over the zone: the outline poly (u, v in m) on her surface, raised by gap, thickness th, a light rim"""
        B = []
        for k in range(len(poly)):                                  # the outline, evenly walked
            (ua, va), (ub, vb) = poly[k], poly[(k + 1) % len(poly)]
            m_ = max(2, int(math.hypot(ub - ua, vb - va) / 0.008))
            for t in range(m_): B.append((ua + (ub - ua) * t / m_, va + (vb - va) * t / m_))
        cu_ = sum(p[0] for p in poly) / len(poly); cv_ = sum(p[1] for p in poly) / len(poly); R_ = 10
        bm = bmesh.new()
        ctr = bm.verts.new(z.at(cu_, cv_, gap + th))
        rings = [[bm.verts.new(z.at(cu_ + (u - cu_) * r / R_, cv_ + (v - cv_) * r / R_, gap + th)) for u, v in B] for r in range(1, R_ + 1)]
        nb = len(B)
        for i in range(nb): bm.faces.new((ctr, rings[0][i], rings[0][(i + 1) % nb]))
        for a_, b_ in zip(rings, rings[1:]):
            for i in range(nb): bm.faces.new((a_[i], b_[i], b_[(i + 1) % nb], a_[(i + 1) % nb]))
        o = mesh_obj(name, bm, mat or BORAZON)
        s = o.modifiers.new("t", 'SOLIDIFY'); s.thickness = th; s.offset = 1.0; s.use_even_offset = True
        b = o.modifiers.new("b", 'BEVEL'); b.width = 0.0025; b.segments = 2; b.limit_method = 'ANGLE'
        if rim:                                                     # the frame: the outline walked on her surface
            pts = []
            for k in range(len(poly)):
                (ua, va), (ub, vb) = poly[k], poly[(k + 1) % len(poly)]
                for t in range(6): pts.append(z.at(ua + (ub - ua) * t / 6, va + (vb - va) * t / 6, gap + th * 0.35))
            tube(name + "_rim", pts, 0.0035, FRAME)
        return o
    def mosaic(name, z, poly, r=0.020, gap=0.006, th=0.006, joint=0.0025):
        """hex tiles (flat to flat 2r*cos30) over the outline, each laid flat on her surface at its centre"""
        bm = bmesh.new(); dx = r * math.sqrt(3); dy = r * 1.5; cnt = 0
        us = [p[0] for p in poly]; vs = [p[1] for p in poly]
        j = 0; v = min(vs)
        while v <= max(vs) + 1e-6:
            u = min(us) + (dx / 2 if j % 2 else 0.0)
            while u <= max(us) + 1e-6:
                if inside(poly, u, v):
                    c = z.at(u, v, gap); nn = normal_at(z, u, v, gap)
                    a = z.b.cross(nn).normalized(); b = nn.cross(a).normalized()
                    ring_lo = [c + a * x + b * y for x, y in hexagon(r - joint, math.pi / 6)]
                    lo = [bm.verts.new(p) for p in ring_lo]; hi = [bm.verts.new(p + nn * th) for p in ring_lo]
                    bm.faces.new(lo[::-1]); bm.faces.new(hi)
                    for k in range(6): bm.faces.new((lo[k], lo[(k + 1) % 6], hi[(k + 1) % 6], hi[k]))
                    cnt += 1
                u += dx
            v += dy; j += 1
        o = mesh_obj(name, bm, BORAZON, smooth=False)
        b = o.modifiers.new("b", 'BEVEL'); b.width = 0.0015; b.segments = 2; b.limit_method = 'ANGLE'
        return o, cnt
    def bands(name, z, v_list, half_w, h, gap=0.006, th=0.006, curve=0.0):
        """horizontal bands (narrow plates), each its own frame"""
        out = []
        for k, v in enumerate(v_list):
            poly = [(-half_w, v - h / 2), (0, v - h / 2 - curve), (half_w, v - h / 2), (half_w, v + h / 2), (0, v + h / 2 - curve), (-half_w, v + h / 2)]
            out.append(plate("%s%d" % (name, k), z, poly, th=th, gap=gap, n=20))
        return out
    def strap(name, pts_zone, w=0.032, gap=0.004):
        """webbing on her surface through the points (zone, u, v): a flat band"""
        P = [z.at(u, v, gap) for z, u, v in pts_zone]; Q = []
        for i in range(len(P) - 1):
            for t in range(8): Q.append(P[i].lerp(P[i + 1], t / 8))
        Q.append(P[-1])
        bm = bmesh.new(); rings = []
        for i, p in enumerate(Q):
            t = (Q[min(i + 1, len(Q) - 1)] - Q[max(i - 1, 0)]).normalized()
            loc, nrm, _, _ = BODY.find_nearest(p); nn = nrm.normalized(); side = t.cross(nn).normalized()
            base = loc + nn * gap
            rings.append([bm.verts.new(base + side * s_ * w / 2 + nn * h_) for s_, h_ in ((-1, 0), (1, 0), (1, 0.003), (-1, 0.003))])
        for a, b in zip(rings, rings[1:]):
            for k in range(4): bm.faces.new((a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]))
        return mesh_obj(name, bm, STRAP)

    # ---- her zones (measured on SuitBody, REST): front is -y ----
    Z_STERN = Zone((0.0, -0.16, 1.33), (0, -1, 0.10))                 # the sternum, between the breasts and above
    Z_UPPER = Zone((0.0, -0.13, 1.405), (0, -1, 0.55))                # the upper chest under the collarbones (faces up-front)
    Z_SHL = Zone((0.185, -0.02, 1.40), (0.55, 0.0, 0.83), (0, 1, 0))  # the shoulder cap over the humeral head (faces up-out)
    Z_ILI = Zone((0.12, -0.12, 1.06), (0.45, -0.89, 0.0))             # the iliac crest, front-side (above the hip ring)
    Z_TRO = Zone((0.20, -0.03, 0.87), (1, -0.15, 0.0))                # the hip's side over the greater trochanter
    Z_THI = Zone((0.115, -0.15, 0.74), (0.12, -1, 0.10))              # the thigh's front
    Z_KNE = Zone((0.140, -0.13, 0.505), (0.05, -1, 0.0))              # the knee (its plates on her knee pad: gap 22 mm)
    Z_SHIN = Zone((0.155, -0.09, 0.32), (0.05, -1, 0.0))              # the shin's front
    Z_FORE = Zone((0.39, -0.10, 1.13), (0.55, -0.55, 0.62), (0.70, -0.65, -0.32))   # the forearm, its outer-upper face
    def LR(fn, base, z, *a, **k):
        return [fn(base + "L", z, *a, **k), fn(base + "R", z.mirror(), *a, **k)]

    CONCEPTS = []
    def concept(key, title):
        def deco(f): CONCEPTS.append((key, title, f)); return f
        return deco
    @concept("1", "Костный мозг")
    def c1():
        plate("stern", Z_STERN, chamfer_rect(0.085, 0.20, 0.018))
        LR(plate, "shl", Z_SHL, chamfer_rect(0.11, 0.12, 0.025))
        LR(plate, "ili", Z_ILI, chamfer_rect(0.10, 0.07, 0.015))
        LR(plate, "tro", Z_TRO, chamfer_rect(0.10, 0.13, 0.025))
    @concept("2", "Чешуя")
    def c2():
        mosaic("chest", Z_UPPER, [(-0.13, -0.035), (0.13, -0.035), (0.11, 0.035), (-0.11, 0.035)])
        mosaic("stern", Z_STERN, [(-0.045, -0.11), (0.045, -0.11), (0.045, 0.05), (-0.045, 0.05)])
        for zz, nm in ((Z_SHL, "shl"), (Z_THI, "thi"), (Z_KNE, "kne")):
            poly = {"shl": hexagon(0.065, math.pi / 6), "thi": chamfer_rect(0.12, 0.20, 0.03), "kne": hexagon(0.055, math.pi / 6)}[nm]
            g_ = 0.022 if nm == "kne" else 0.006           # the knee tiles on her knee pad
            mosaic(nm + "L", zz, poly, gap=g_); mosaic(nm + "R", zz.mirror(), poly, gap=g_)
    @concept("3", "Пояса")
    def c3():
        bands("chestb", Z_UPPER, [-0.03, 0.0, 0.03], 0.12, 0.024, curve=0.006)
        for z in (Z_THI, Z_THI.mirror()):
            bands("thib%s" % ("L" if z is Z_THI else "R"), z, [-0.06, -0.02, 0.02, 0.06], 0.06, 0.03, curve=0.004)
        LR(plate, "kne", Z_KNE, chamfer_rect(0.10, 0.11, 0.03), gap=0.022)
    @concept("4", "Погоны")
    def c4():
        LR(plate, "shl", Z_SHL, chamfer_rect(0.14, 0.15, 0.035))
        plate("gorget", Z_UPPER, [(-0.12, 0.03), (-0.06, -0.035), (0.06, -0.035), (0.12, 0.03), (0.07, 0.045), (-0.07, 0.045)])
        LR(plate, "kne", Z_KNE, chamfer_rect(0.10, 0.11, 0.03), gap=0.022)
    @concept("5", "Портупея")
    def c5():
        for sg, z in ((1, Z_SHL), (-1, Z_SHL.mirror())):
            strap("strapX%+d" % sg, [(z, 0.0, 0.0), (Z_UPPER, sg * 0.07, 0.02), (Z_STERN, 0.0, -0.03), (Z_STERN, -sg * 0.06, -0.15), (Z_ILI if sg < 0 else Z_ILI.mirror(), 0.0, 0.0)])
        plate("stern", Z_STERN, hexagon(0.055, math.pi / 6))
        LR(plate, "ili", Z_ILI, hexagon(0.045, math.pi / 6))
        LR(plate, "tro", Z_TRO, hexagon(0.055, math.pi / 6))
        LR(plate, "kne", Z_KNE, hexagon(0.055, math.pi / 6), gap=0.022)
    @concept("6", "Падение")
    def c6():
        LR(plate, "kne", Z_KNE, chamfer_rect(0.10, 0.12, 0.03), gap=0.022)
        LR(plate, "shin", Z_SHIN, chamfer_rect(0.07, 0.20, 0.02))
        LR(plate, "fore", Z_FORE, chamfer_rect(0.065, 0.17, 0.02))
        plate("stern", Z_STERN, chamfer_rect(0.085, 0.20, 0.018))
        LR(plate, "ili", Z_ILI, chamfer_rect(0.10, 0.07, 0.015))
    @concept("7", "Орбита")
    def c7():
        LR(plate, "shl", Z_SHL, chamfer_rect(0.13, 0.14, 0.03))
        plate("upper", Z_UPPER, chamfer_rect(0.25, 0.07, 0.02))
        LR(plate, "thi", Z_THI, chamfer_rect(0.12, 0.10, 0.025), )
    @concept("8", "Укладка")
    def c8():
        # cloth pockets (the stock is shared: tiles where the task needs them, some pockets empty)
        for z, poly, filled in ((Z_STERN, chamfer_rect(0.10, 0.21, 0.02), True), (Z_THI, chamfer_rect(0.13, 0.20, 0.025), True),
                                (Z_THI.mirror(), chamfer_rect(0.13, 0.20, 0.025), False), (Z_SHL, chamfer_rect(0.12, 0.12, 0.03), True),
                                (Z_SHL.mirror(), chamfer_rect(0.12, 0.12, 0.03), False)):
            plate("pocket", z, poly, th=0.004, gap=0.002, rim=False, mat=POCKET)
            if filled:
                sub = [(x * 0.82, y * 0.82) for x, y in poly]
                plate("tile", z, sub, th=0.004, gap=0.0065, rim=False)
            strap("flap", [(z, -0.04, max(p[1] for p in poly) - 0.01), (z, 0.04, max(p[1] for p in poly) - 0.01)], w=0.018, gap=0.007)
        LR(plate, "kne", Z_KNE, chamfer_rect(0.10, 0.11, 0.03), gap=0.022)
    @concept("9", "На каркасе")
    def c9():
        # the plates on the exoframe she already wears: the hip ring carries the hip plates, the thigh cuffs the thigh plates,
        # the knee drives the knee caps; the load goes into the frame
        LR(plate, "tro", Z_TRO, chamfer_rect(0.11, 0.14, 0.03), gap=0.03)
        LR(plate, "thi", Z_THI, chamfer_rect(0.12, 0.19, 0.03), gap=0.022)
        LR(plate, "kne", Z_KNE, hexagon(0.06, math.pi / 6), gap=0.022)
        plate("stern", Z_STERN, chamfer_rect(0.085, 0.20, 0.018))
    @concept("10", "Минимум")
    def c10():
        plate("stern", Z_STERN, chamfer_rect(0.085, 0.20, 0.018))
        LR(plate, "kne", Z_KNE, chamfer_rect(0.10, 0.11, 0.03), gap=0.022)

    # ---- the render: the helmet sheet's light, the whole figure ----
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 560, 800; sc.render.film_transparent = False
    try: sc.eevee.use_raytracing = True; sc.eevee.ray_tracing_options.resolution_scale = '1'
    except Exception: pass
    if sc.world is None: sc.world = bpy.data.worlds.new("BM_world")
    sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None)
    if bg: bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("BM_key", 3.0, (42, 0, 145)), ("BM_fill", 1.4, (65, 0, -40)), ("BM_front", 1.6, (55, 0, 20)), ("BM_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("BM_cam"); cd.sensor_fit = 'VERTICAL'; cd.lens = 36
    cam = bpy.data.objects.new("BM_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0.0, -0.02, 0.92))
    VIEWS = {"front": T + Vector((0.0, -2.75, 0.10)), "q34": T + Vector((-1.55, -2.25, 0.25)), "side": T + Vector((2.75, 0.0, 0.08))}
    for key, title, fn in CONCEPTS:
        if ONLY and key not in ONLY: continue
        fn(); print("built", key, title, len(parts), file=LOGF); LOGF.flush()
        for vn, loc in VIEWS.items():
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "bm_%s_%s.png" % (key.zfill(2), vn)); bpy.ops.render.render(write_still=True)
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
    print("DONE", file=LOGF)
except Exception:
    traceback.print_exc(file=LOGF); print("FAILED", file=LOGF)
LOGF.close()
