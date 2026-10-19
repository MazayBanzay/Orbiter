# BORAZON ZONE PROTECTION - the layout from the zone calculation (the user, 2026-10-18: «правильно физически сделанные,
# по правильному расчету»; «с ранцем и без него»). Zones by threat (ICRP tissue weights: chest + abdomen hold over half the
# radiation risk; falls forward; micrometeoroids from above on a surface, all round in orbit; loads against belly and
# thighs; regolith abrasion):
#   chest plate (lungs, breasts, sternum, oesophagus) 6 mm, on a stand-off, NO cups: a taut surface over her front;
#   abdomen 4 lames 6 mm on sliding links (stomach, colon, liver; she bends), nesting under each other upwards;
#   lower-abdomen shield on the hip ring (gonads, bladder) 6 mm; shoulders two lames 6 mm (from above, side falls);
#   helmet: nape shield and two crown plates either side of the rib, cBN 6 mm; forearms, thighs, shins 4 mm; knees 6 mm.
#   Nothing over the flex lines: armpits, elbow pits, groin, knee pits. The back: the life-support pack (and the jet pack
#   M1 «Гекса» when worn, its 13 kg of hydrogen moderating fast neutrons for the plates' boron-10 to catch).
# Material: binderless nanotwinned cBN, boron-10 enriched, honey-amber, polished; plates solid (no coatings), each framed in
# light metal, seated on 6 mm BN aerogel. Rigid plates are TAUT: the concave envelope of her surface (rows, then columns),
# so a plate never sags into a hollow; only the abdomen lames follow her in steps.
# Variants of the chest: A monolith; B two tiers (breathing, reaching); C halves with a sternum strip (twist).
#   blender -b astronavigator_suit.blend --python boron_layout.py   (env BL_OUT, BL_ONLY "A,B")
import bpy, bmesh, math, os, traceback
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.environ.get("BL_OUT") or os.path.join(HERE, "..", "suit_mockups", "boron_layout"); os.makedirs(OUT, exist_ok=True)
ONLY = [s for s in os.environ.get("BL_ONLY", "").split(",") if s]
# the game build is lean: the figure is skinned on the CPU every frame and its skin read at the start - rigid plates do
# not need render density (plates: 4 rings, 20 mm outline steps, 1 bevel segment; rims and hoses 1 bevel ring)
GAME = bool(os.environ.get("BL_GAME"))
LOGF = open(os.path.join(OUT, "bl_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
try:
    def mk(name, col, metal, rough, spec=0.5, coat=0.0):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
        for k, v in (("Specular IOR Level", spec), ("Coat Weight", coat), ("IOR", 2.12), ("Coat Roughness", 0.10), ("Coat IOR", 2.12)):
            if k in b.inputs and (k not in ("IOR", "Coat Roughness", "Coat IOR") or coat > 0): b.inputs[k].default_value = v
        if coat > 0 and "Coat Tint" in b.inputs: b.inputs["Coat Tint"].default_value = (1.0, 0.62, 0.30, 1)
        m.diffuse_color = (*col, 1); return m
    BORAZON = mk("BL_borazon", (0.30, 0.068, 0.0), 0.0, 0.16, 0.8, 0.6)
    FRAME = mk("BL_frame", (0.62, 0.63, 0.65), 1.0, 0.32)
    LINK = mk("BL_link", (0.30, 0.31, 0.32), 0.6, 0.45)

    # ---- the helmet: helmet_cheek.py's k1 (glass b, hex ports, nape shield), the nape shield in cBN ----
    src = open(os.path.join(HERE, "helmet_cheek.py"), encoding="utf-8").read()
    src = src[:src.index("REST = {n:")].replace('LOG = open(os.path.join(OUT, "hk_log.txt"), "w", encoding="utf-8")', "LOG = LOGF")
    HK = {"LOGF": LOGF, "__name__": "hk"}; exec(compile(src, "helmet_cheek.py", "exec"), HK)
    # the visor (visor_fit.py, 2026-10-18): a clean cut, the glass under the shell's edge, sapphire with a light blue cast
    VF = {"HK": HK, "bpy": bpy, "bmesh": bmesh, "math": math, "Vector": Vector, "BVHTree": BVHTree}
    exec(compile(open(os.path.join(HERE, "visor_fit.py"), encoding="utf-8").read(), "visor_fit.py", "exec"), VF)
    _shellb = VF["shell_bvh"](); VF["cut_fine"](HK["W"], passes=1 if GAME else 3, band=0.015 if GAME else 0.03)
    HK["parts"].append(VF["fitted_glass"]("glass", VF["sapphire_blue"](), HK["W"], _shellb))
    HK["parts"].append(VF["chin_fill"](HK["W"], _shellb))   # the old opening under the new window: closed with shell
    HK["rim"](HK["W"]); HK["hexport"](1); HK["hexport"](-1); HK["shield"]()
    bpy.data.objects["shield"].data.materials[0] = BORAZON
    for sg in (1, -1):                                       # the crown plates: either side of the rib, from above
        p0, p1 = (-math.pi / 2 + 0.32, math.pi / 2 - 0.32) if sg > 0 else (math.pi / 2 + 0.32, 3 * math.pi / 2 - 0.32)
        HK["patch2"]("crown%d" % sg, p0, p1, 1.752, 1.792, lambda p, t, p0=p0, p1=p1: 0.006 * HK["ease"](min(p - p0, p1 - p) / 0.10), BORAZON, n=40, m=6)
    FR = HK["FRONT"]; BA = math.radians(70)
    HK["patch2"]("brow", FR - BA, FR + BA, 1.722, 1.746,
                 lambda p, t: (0.004 + 0.005 * math.cos((p - FR) / BA * math.pi / 2)) * HK["ease"](min(p - FR + BA, FR + BA - p) / 0.12), BORAZON, n=60, m=6)
    for n in ("HelmPods", "Cuirass", "MarkChest", "PlateLugs", "ShoulderArcs"):
        if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
    # ---- the jet pack M1 «Гекса» (session «Анимации»): built per shot ----
    HX = {}; exec(open(os.path.join(HERE, "jetpack_hexa_build.py"), encoding="utf-8").read(), HX)

    arm = bpy.data.objects["Astronavigator"]; arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    if bpy.data.objects["SuitBody"].matrix_world != Matrix.Identity(4): raise RuntimeError("SuitBody not at the origin")
    BODY = BVHTree.FromObject(bpy.data.objects["SuitBody"], dg)
    parts = []; CARRY = {}; CUR = ["Spine"]
    def link(o):
        sc.collection.objects.link(o); parts.append(o)
        b = CUR[0]
        if "{S}" in b:                                       # LR: the part's own side from its name (..L / ..R, ..L_rim)
            base = o.name.split(".")[0].replace("_rim", ""); b = b.replace("{S}", "Left" if base.endswith("L") else "Right")
        CARRY[o.name] = b; return o
    def mesh_obj(name, bm, m, smooth=True):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        me.materials.append(m); return link(bpy.data.objects.new(name, me))
    def tube(name, pts, r, m, closed=True):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 1 if GAME else 2; cd.use_fill_caps = True
        sp = cd.splines.new('POLY'); sp.points.add(len(pts) - 1); sp.use_cyclic_u = closed
        for q, c in zip(sp.points, pts): q.co = (*c, 1)
        cd.materials.append(m); return link(bpy.data.objects.new(name, cd))

    TGT = [None]
    def hit(p, n0):
        T_ = TGT[0] or BODY
        loc, nrm, i, d = T_.ray_cast(p + n0 * 0.25, -n0, 0.6)
        if loc is None: loc, nrm, i, d = T_.find_nearest(p)
        return loc
    def bvh_of(names):
        V, P = [], []
        for n in names:
            o = bpy.data.objects[n]; e = o.evaluated_get(dg); m = e.to_mesh(); k = len(V)
            V += [o.matrix_world @ v.co for v in m.vertices]; P += [[k + i for i in f.vertices] for f in m.polygons]; e.to_mesh_clear()
        return BVHTree.FromPolygons(V, P)
    class Zone:
        def __init__(s, c, n0, up=(0, 0, 1)):
            s.n = Vector(n0).normalized(); up = Vector(up)
            s.a = up.cross(s.n).normalized(); s.b = s.n.cross(s.a).normalized(); s.c = Vector(c)
        def h(s, u, v): return (hit(s.c + s.a * u + s.b * v, s.n) - s.c).dot(s.n)          # her height along n
        def P(s, u, v, hh): return s.c + s.a * u + s.b * v + s.n * hh
        def mirror(s): return Zone((-s.c.x, s.c.y, s.c.z), (-s.n.x, s.n.y, s.n.z), (-s.b.x, s.b.y, s.b.z))
    def upper_hull(ts, ys):
        """the smallest concave function over the points: a rigid sheet laid on them from outside"""
        H = []
        for p in zip(ts, ys):
            while len(H) >= 2 and (H[-1][0] - H[-2][0]) * (p[1] - H[-2][1]) - (H[-1][1] - H[-2][1]) * (p[0] - H[-2][0]) >= 0: H.pop()
            H.append(p)
        out = []
        for t in ts:
            for k in range(len(H) - 1):
                if H[k][0] - 1e-9 <= t <= H[k + 1][0] + 1e-9:
                    w = (t - H[k][0]) / max(1e-9, H[k + 1][0] - H[k][0]); out.append(H[k][1] + (H[k + 1][1] - H[k][1]) * w); break
            else: out.append(ys[ts.index(t)])
        return out
    class Field:
        """her height over the zone's (u, v) box, made taut (rigid plate) or kept (follow)"""
        def __init__(s, z, u0, u1, v0, v1, taut=True, nu=18, nv=18):
            s.z, s.u0, s.u1, s.v0, s.v1, s.nu, s.nv = z, u0, u1, v0, v1, nu, nv
            us = [u0 + (u1 - u0) * i / nu for i in range(nu + 1)]; vs = [v0 + (v1 - v0) * j / nv for j in range(nv + 1)]
            Hh = [[z.h(u, v) for u in us] for v in vs]
            if taut:
                for _ in range(2):
                    Hh = [upper_hull(us, row) for row in Hh]
                    cols = [upper_hull(vs, [Hh[j][i] for j in range(nv + 1)]) for i in range(nu + 1)]
                    Hh = [[cols[i][j] for i in range(nu + 1)] for j in range(nv + 1)]
            s.H = Hh
        def __call__(s, u, v):
            x = (u - s.u0) / (s.u1 - s.u0) * s.nu; y = (v - s.v0) / (s.v1 - s.v0) * s.nv
            i = min(max(int(x), 0), s.nu - 1); j = min(max(int(y), 0), s.nv - 1); a = min(max(x - i, 0), 1); b = min(max(y - j, 0), 1)
            H = s.H; return (H[j][i] * (1 - a) + H[j][i + 1] * a) * (1 - b) + (H[j + 1][i] * (1 - a) + H[j + 1][i + 1] * a) * b

    def plate(name, z, poly, th=0.006, gap=0.006, taut=True, rim=True, field=None):
        """a solid cBN plate: outline poly (u, v), on a taut (or following) surface gap above her, th thick, framed"""
        us = [p[0] for p in poly]; vs = [p[1] for p in poly]; m_ = 0.01
        F = field or Field(z, min(us) - m_, max(us) + m_, min(vs) - m_, max(vs) + m_, taut)
        B = []
        for k in range(len(poly)):
            (ua, va), (ub, vb) = poly[k], poly[(k + 1) % len(poly)]
            n_ = max(2, int(math.hypot(ub - ua, vb - va) / (0.02 if GAME else 0.008)))
            for t in range(n_): B.append((ua + (ub - ua) * t / n_, va + (vb - va) * t / n_))
        cu, cv = sum(us) / len(us), sum(vs) / len(vs); R = 4 if GAME else 10
        pos = lambda u, v, e: z.P(u, v, F(u, v) + gap + e)
        bm = bmesh.new(); ctr = bm.verts.new(pos(cu, cv, th))
        rings = [[bm.verts.new(pos(cu + (u - cu) * r / R, cv + (v - cv) * r / R, th)) for u, v in B] for r in range(1, R + 1)]
        nb = len(B)
        for i in range(nb): bm.faces.new((ctr, rings[0][i], rings[0][(i + 1) % nb]))
        for a_, b_ in zip(rings, rings[1:]):
            for i in range(nb): bm.faces.new((a_[i], b_[i], b_[(i + 1) % nb], a_[(i + 1) % nb]))
        o = mesh_obj(name, bm, BORAZON)
        s = o.modifiers.new("t", 'SOLIDIFY'); s.thickness = th; s.offset = 1.0; s.use_even_offset = False   # (even offset spiked on the fan's thin triangles)
        b = o.modifiers.new("b", 'BEVEL'); b.width = 0.002; b.segments = 1 if GAME else 2; b.limit_method = 'ANGLE'
        if rim: tube(name + "_rim", [pos(u, v, th * 0.5) for u, v in B], 0.0032, FRAME)
        return o, F, pos
    def crect(w, h, ch, cx=0.0, cy=0.0):
        w, h = w / 2, h / 2; return [(cx + x, cy + y) for x, y in [(-w + ch, -h), (w - ch, -h), (w, -h + ch), (w, h - ch), (w - ch, h), (-w + ch, h), (-w, h - ch), (-w, -h + ch)]]
    def LR(fn, base, z, *a, **k): return [fn(base + "L", z, *a, **k), fn(base + "R", z.mirror(), *a, **k)]
    def rivet(p, n):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=0.0045, radius2=0.0035, depth=0.003)
        R_ = Vector((0, 0, 1)).rotation_difference(n).to_matrix().to_4x4(); bmesh.ops.transform(bm, matrix=Matrix.Translation(p) @ R_, verts=bm.verts)
        return mesh_obj("rivet", bm, LINK)

    # ---- her zones (SuitBody, REST; front -y) ----
    Z_CH = Zone((0.0, -0.17, 1.33), (0, -1, 0.06))        # the chest: u across, v up (v 0 at z ~1.33)
    Z_AB = Zone((0.0, -0.15, 1.11), (0, -1, 0.0))         # the abdomen
    Z_LA = Zone((0.0, -0.13, 0.90), (0, -1, -0.15))       # below the hip ring, front
    Z_SH = Zone((0.185, -0.02, 1.40), (0.55, 0.0, 0.83), (0, 1, 0))
    Z_SH2 = Zone((0.215, -0.02, 1.33), (0.95, 0.0, 0.30), (0, 0, 1))    # the shoulder's side, under the cap
    Z_TH = Zone((0.115, -0.15, 0.74), (0.12, -1, 0.10))
    Z_KN = Zone((0.140, -0.13, 0.505), (0.05, -1, 0.0))
    Z_SN = Zone((0.155, -0.09, 0.32), (0.05, -1, 0.0))
    Z_FA = Zone((0.39, -0.10, 1.13), (0.55, -0.55, 0.62), (0.70, -0.65, -0.32))

    # the chest outline: neck curve on top (the neck ring), off the armpits, down to the under-bust line
    CH_TOP, CH_BOT = 0.105, -0.115                         # z ~1.435 .. 1.215
    def chest_poly(x0=-0.15, x1=0.15):
        P = [(-0.148, CH_BOT), (0.148, CH_BOT), (0.150, 0.0), (0.128, 0.065), (0.112, CH_TOP), (0.055, CH_TOP - 0.008),
             (0.0, CH_TOP - 0.030), (-0.055, CH_TOP - 0.008), (-0.112, CH_TOP), (-0.128, 0.065), (-0.150, 0.0)]
        return [(max(x0, min(x1, u)), v) for u, v in P]
    CHF = []                                                   # one taut chest form: every chest piece cut from it
    def chest_field():
        if not CHF: CHF.append(Field(Z_CH, -0.165, 0.165, CH_BOT - 0.012, CH_TOP + 0.012, True, 22, 22))
        return CHF[0]
    def chest_A():
        plate("chest", Z_CH, chest_poly(), field=chest_field())
    def chest_B():                                             # two tiers: the upper over the lower (breathing, reaching)
        plate("chestU", Z_CH, [(-0.150, -0.012), (0.150, -0.012), (0.150, 0.0), (0.128, 0.065), (0.112, CH_TOP), (0.055, CH_TOP - 0.008), (0.0, CH_TOP - 0.030),
                               (-0.055, CH_TOP - 0.008), (-0.112, CH_TOP), (-0.128, 0.065), (-0.150, 0.0)], gap=0.006 + 0.008, field=chest_field())
        plate("chestL", Z_CH, [(-0.148, CH_BOT), (0.148, CH_BOT), (0.150, 0.012), (-0.150, 0.012)], field=chest_field())
    def chest_C():                                             # halves on a sternum strip (twist)
        plate("chestR", Z_CH, [(0.012, CH_BOT), (0.148, CH_BOT), (0.150, 0.0), (0.128, 0.065), (0.112, CH_TOP), (0.055, CH_TOP - 0.008), (0.012, CH_TOP - 0.026)], field=chest_field())
        plate("chestLf", Z_CH, [(-0.012, CH_BOT), (-0.012, CH_TOP - 0.026), (-0.055, CH_TOP - 0.008), (-0.112, CH_TOP), (-0.128, 0.065), (-0.150, 0.0), (-0.148, CH_BOT)], field=chest_field())
        plate("stern", Z_CH, [(-0.022, CH_BOT + 0.01), (0.022, CH_BOT + 0.01), (0.022, CH_TOP - 0.04), (0.0, CH_TOP - 0.03), (-0.022, CH_TOP - 0.04)], gap=0.006 + 0.008, field=chest_field())
    # ---- from the strain study (suit_strain.py, 2026-10-18): seams at the under-bust fold (z 1.20, compression) and the
    # upper-chest band (z ~1.40, compression); nothing rigid where the pectoral stretches with the arm (|x| > 0.11 above
    # z 1.29); the chest carried by Spine (skin moves <= 12 mm towards a plate), the collar by Spine1 (<= 8 mm); breathing
    # +10 mm; the abdomen front shortens by up to a third -> lames overlap 35 %
    LAME_TOP, LAME_OV, LAME_GAP = 1.232, 0.014, 0.006
    LAME_BOT = [1.015]
    G_COL = 0.006 + 0.008               # the collar: aerogel + its skin motion
    G_MAIN = 0.006 + 0.012 + 0.010      # the chest: aerogel + skin motion + breathing
    VS, VC = -0.128, 0.062              # the under-bust seam and the collar seam (v on Z_CH)
    def collar(name="collar", x=0.108, parts_=1):
        top = [(x, 0.118), (0.05, 0.112), (0.0, 0.092), (-0.05, 0.112), (-x, 0.118)]
        CUR[0] = "Spine1"
        if parts_ == 1: plate(name, Z_CH, [(-x, VC - 0.012), (x, VC - 0.012)] + top, gap=G_COL + 0.008)
        else:
            plate(name + "R", Z_CH, [(0.022, VC - 0.012), (x, VC - 0.012), (x, 0.118), (0.05, 0.112), (0.022, 0.098)], gap=G_COL + 0.008)
            plate(name + "L", Z_CH, [(-0.022, VC - 0.012), (-0.022, 0.098), (-0.05, 0.112), (-x, 0.118), (-x, VC - 0.012)], gap=G_COL + 0.008)
        CUR[0] = "Spine"
    def side_lames():                   # where the pectoral stretches: a lame hung from the shoulder, it moves with the arm
        CUR[0] = "{S}Shoulder"
        z = Zone((0.140, -0.115, 1.355), (0.55, -0.83, 0.10)); LR(plate, "pect", z, crect(0.05, 0.10, 0.015), th=0.004, gap=0.008)
        CUR[0] = "Spine"
    def chest_D():                      # a sternum rail; the breast segments adjustable on it (angle, offset) - one set fits many
        side_lames(); collar()
        F = chest_field()
        plate("railS", Z_CH, [(-0.024, VS + 0.004), (0.024, VS + 0.004), (0.024, VC + 0.002), (-0.024, VC + 0.002)], gap=G_MAIN + 0.009, field=F)
        plate("brR", Z_CH, [(0.018, VS), (0.128, VS), (0.134, -0.04), (0.112, VC), (0.018, VC)], gap=G_MAIN, field=F)
        plate("brL", Z_CH, [(-0.018, VS), (-0.018, VC), (-0.112, VC), (-0.134, -0.04), (-0.128, VS)], gap=G_MAIN, field=F)
        for vv in (VS + 0.04, VC - 0.04):
            for sx in (-1, 1): rivet(Z_CH.P(sx * 0.014, vv, F(sx * 0.014, vv) + G_MAIN + 0.009 + 0.0065), Z_CH.n)
    def chest_E():                      # two belts: the collar (Spine1) over the chest band (Spine)
        side_lames(); collar()
        plate("band", Z_CH, [(-0.128, VS), (0.128, VS), (0.134, -0.04), (0.112, VC), (-0.112, VC), (-0.134, -0.04)], gap=G_MAIN, field=chest_field())
    def chest_F():                      # five segments on the low-strain islands: collar halves, sternum, breasts
        side_lames(); collar(parts_=2)
        F = chest_field()
        plate("stern", Z_CH, [(-0.026, VS + 0.004), (0.026, VS + 0.004), (0.026, 0.096), (0.0, 0.088), (-0.026, 0.096)], gap=G_MAIN + 0.009, field=F)
        plate("brR", Z_CH, [(0.020, VS), (0.128, VS), (0.134, -0.04), (0.112, VC - 0.004), (0.020, VC - 0.004)], gap=G_MAIN, field=F)
        plate("brL", Z_CH, [(-0.020, VS), (-0.020, VC - 0.004), (-0.112, VC - 0.004), (-0.134, -0.04), (-0.128, VS)], gap=G_MAIN, field=F)
    CHESTS = {"A": ("монолит", chest_A), "B": ("два яруса", chest_B), "C": ("половины и грудинная полоса", chest_C),
              "D": ("грудинный рельс", chest_D), "E": ("два пояса", chest_E), "F": ("пять сегментов", chest_F)}

    def body_common(lowab=True):
        # the abdomen: 4 lames from the under-bust down to the hip ring; each one tucks under the one below it (bending
        # forward they slide up under each other); their tops under the chest plate's lower edge
        N = 4; top, bot = LAME_TOP, LAME_BOT[0]; hh = (top - bot) / N; ov = LAME_OV
        for k in range(N):
            CUR[0] = "Spine" if k < 2 else "LowerBack"
            zt = top - k * hh + (ov if k else 0.0); zb = top - (k + 1) * hh - (0.0 if k == N - 1 else 0.0)
            v1, v0 = zt - Z_AB.c.z, zb - Z_AB.c.z; w = 0.142 - 0.006 * k
            g = LAME_GAP + 0.0075 * k                                    # nesting: the lower lame over the upper
            o, F, pos = plate("lame%d" % k, Z_AB, crect(2 * w, v1 - v0, 0.012, 0.0, (v0 + v1) / 2), gap=g)
            for sx in (-1, 1): rivet(pos(sx * (w - 0.018), (v0 + v1) / 2, 0.0065), Z_AB.n)       # the sliding link pins
        # the lower-abdomen shield, hinged on the hip ring's front
        CUR[0] = "Hips"
        if lowab: plate("lowab", Z_LA, [(-0.085, 0.045), (0.085, 0.045), (0.06, -0.035), (-0.06, -0.035)], gap=0.016)
        # shoulders: the cap and a lame under it
        CUR[0] = "{S}Shoulder"; LR(plate, "shcap", Z_SH, crect(0.12, 0.10, 0.03), gap=0.006)
        CUR[0] = "{S}Arm"; LR(plate, "shlam", Z_SH2, crect(0.07, 0.06, 0.018, 0.0, -0.01), gap=0.006)
        CUR[0] = "{S}ForeArm"; LR(plate, "fore", Z_FA, crect(0.06, 0.17, 0.02), th=0.004)
        CUR[0] = "{S}UpLeg"; LR(plate, "thigh", Z_TH, crect(0.12, 0.19, 0.03), th=0.004, gap=0.022)
        CUR[0] = "{S}Leg"; LR(plate, "knee", Z_KN, crect(0.10, 0.12, 0.035), gap=0.022)
        LR(plate, "shin", Z_SN, crect(0.07, 0.19, 0.02), th=0.004, gap=0.016)
        CUR[0] = "Spine"

    # ---- the groin (the user, 2026-10-18: the separate shield reads as an accent; the thigh must pass): the lowest lame
    # carried by the pelvis (Hips), its lower edge arched along the groin creases; the hip ring's front with a cBN insert
    def ring_insert(h=0.026):
        CUR[0] = "Hips"; TGT[0] = bvh_of(["HipRing"])
        plate("ringins", Zone((0.0, -0.20, 1.000), (0, -1, 0)), crect(0.22, min(h, 0.046), 0.008), th=0.004, gap=0.0015, taut=False)   # the ring: z 0.972..1.028
        TGT[0] = None; CUR[0] = "Spine"
    def arch(x, x0, zc, zs):                      # the arch: zc at x0 rising to zs at the side
        t = min(1.0, abs(x - x0) / 0.12); return zc + (zs - zc) * t * t
    def pelvic(name, xs, x0, zc=0.935, zs=1.000, top=1.040):
        CUR[0] = "Hips"; TGT[0] = bvh_of(["SuitBody", "HipRing"])
        z = Zone((0.0, -0.14, 0.98), (0, -1, -0.10))
        bot = [(x, arch(x, x0, zc, zs) - z.c.z) for x in xs]
        poly = [(xs[-1], top - z.c.z), (xs[0], top - z.c.z)] + bot
        plate(name, z, poly, gap=LAME_GAP + 0.0075 * 4)
        TGT[0] = None; CUR[0] = "Spine"
    def g1():                                     # one arched pelvic lame over the ring
        ring_insert(); pelvic("pelv", [-0.13 + 0.26 * i / 12 for i in range(13)], 0.0)
    def g2():                                     # split: each half arches over its own groin crease
        ring_insert()
        pelvic("pelvR", [0.008 + 0.124 * i / 8 for i in range(9)], 0.010, zc=0.930, zs=1.005)
        pelvic("pelvL", [-0.132 + 0.124 * i / 8 for i in range(9)], -0.010, zc=0.930, zs=1.005)
    def g3():                                     # the ring itself as the belt: no plate below it, a taller insert
        ring_insert(h=0.040)
    def g4():                                     # under the belt: the lame behind the hip ring, its arch just showing below it
        ring_insert(h=0.040)
        CUR[0] = "Hips"
        z = Zone((0.0, -0.145, 1.00), (0, -1, -0.05))
        xs = [-0.135 + 0.27 * i / 14 for i in range(15)]
        bot = [(x, (0.962 + (0.990 - 0.962) * min(1.0, abs(x) / 0.12) ** 2) - z.c.z) for x in xs]
        o, F_, pos_ = plate("pelv", z, [(xs[-1], 1.068 - z.c.z), (xs[0], 1.068 - z.c.z)] + bot, gap=0.002)
        bpy.context.view_layer.update(); W_ = [o.matrix_world @ v.co for v in o.data.vertices]
        print("pelv bounds x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f  centre-front y at z1.00: %.3f" % (min(v.x for v in W_), max(v.x for v in W_), min(v.y for v in W_), max(v.y for v in W_),
              min(v.z for v in W_), max(v.z for v in W_), pos_(0.0, 0.0, 0.006).y), file=LOGF)
        CUR[0] = "Spine"
    GROINS = {"G4": ("ламель под поясом, арка из-под кольца", g4), "G1": ("тазовая ламель-арка над кольцом", g1), "G2": ("раздельная арка по паховым складкам", g2), "G3": ("кольцо-пояс, без пластины ниже", g3)}
    def carry_all():
        arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
        for o in list(parts) + list(HK["parts"]):
            if o.name not in bpy.data.objects or o.parent: continue
            b = CARRY.get(o.name, "Head")
            mw = o.matrix_world.copy(); o.parent = arm; o.parent_type = 'BONE'; o.parent_bone = b; bpy.context.view_layer.update(); o.matrix_world = mw
        bpy.context.view_layer.update()
    def rotb(bone, axis, deg):
        pb = arm.pose.bones[bone]; h = pb.head.copy(); R = Matrix.Rotation(math.radians(deg), 4, Vector(axis))
        pb.matrix = Matrix.Translation(h) @ R @ Matrix.Translation(-h) @ pb.matrix; bpy.context.view_layer.update()

    # ---- the game build (BL_GAME=1): the new suit type with the set «Бор» of Suit_T10 as groups "Set*" the game shows or hides
    # (the user, 2026-10-18: one suit, the kit put on / taken off later in a compartment or the MPU when a kit is there).
    # Everything rigid on its bone (one vertex group, weight 1, the armature): the export takes visible meshes with groups.
    def game_build():
        GAMEN = {}
        def to_bone(o, bone, name):
            dg_ = bpy.context.evaluated_depsgraph_get()
            if o.type == 'CURVE':
                me = bpy.data.meshes.new_from_object(o.evaluated_get(dg_)); mats = list(o.data.materials)
                n = bpy.data.objects.new("tmp", me); sc.collection.objects.link(n); bpy.data.objects.remove(o, do_unlink=True); o = n
                me.materials.clear()
                for m_ in mats: me.materials.append(m_)
            else:
                for md in list(o.modifiers):
                    if md.type == 'ARMATURE': o.modifiers.remove(md)
                dg_ = bpy.context.evaluated_depsgraph_get()
                me = bpy.data.meshes.new_from_object(o.evaluated_get(dg_)); o.modifiers.clear(); o.data = me
            mw_ = o.matrix_world.copy(); o.parent = None; o.matrix_world = mw_; o.vertex_groups.clear()   # (keep its place: MarkPack hung on Pack)
            vg = o.vertex_groups.new(name=bone); vg.add(list(range(len(o.data.vertices))), 1.0, 'REPLACE')
            md = o.modifiers.new("Armature", 'ARMATURE'); md.object = arm
            k = GAMEN.get(name, 0); GAMEN[name] = k + 1; o.name = "%s%d" % (name, k) if k else name
            return o
        # the helmet's parts: named as the game hides them (Helm* in first person, Visor* with the front open)
        HELMN = {"HelmChin": "HelmChin", "glass": "VisorB", "rim": "HelmRim", "hexport": "HelmPort", "shield": "HelmShield", "shieldrim": "HelmShRim",
                 "connframe": "HelmConn", "crown": "SetCrown", "brow": "SetBrow"}
        for o in list(HK["parts"]):
            if o.name not in bpy.data.objects: continue
            base = "".join(ch for ch in o.name.split(".")[0] if ch.isalpha())
            nm = next((v for k_, v in HELMN.items() if base == k_ or base.startswith(k_)), "Helm" + base[:6])
            if base == "shield": o.data.materials[0] = HK["GREY"]      # the nape shield as chosen (grey); the kit adds crown + brow
            to_bone(o, "Spine1", nm)                   # as the shell: the helmet sits on the suit, her head turns inside it
        # the kit: every plate, its frame and its pins
        for o in list(parts):
            if o.name not in bpy.data.objects: continue
            b = CARRY.get(o.name, "Spine"); base = "".join(ch for ch in o.name.split(".")[0] if ch.isalpha())[:7]
            to_bone(o, b, "Set" + base)
        # the suit's back block (suit_back_block.py, 2026-10-18): the old Pack, its rails and cables off; each part rigid
        # on the bone under it - the thoracic part and its lid on Spine1, the lumbar part on LowerBack, the strut on Hips
        SBG = {"__name__": "sbb", "SB_BUILD_ONLY": True, "SB_SUBSURF": 0, "SB_LOD": 1}
        exec(compile(open(os.path.join(HERE, "suit_back_block.py"), encoding="utf-8").read(), "suit_back_block.py", "exec"), SBG)
        for o in list(SBG["inside"]): bpy.data.objects.remove(o, do_unlink=True)
        blk = list(SBG["shells"]) + list(SBG["outside"]) + ([bpy.data.objects["MarkPack"]] if "MarkPack" in bpy.data.objects else [])
        for o in blk:
            bpy.context.view_layer.update()
            zc = (o.matrix_world @ (sum((Vector(c) for c in o.bound_box), Vector()) / 8)).z
            bone = "Spine1" if zc > 1.25 else ("LowerBack" if zc > 1.075 else "Hips")
            base = "".join(ch for ch in o.name.split(".")[0] if ch.isalpha())
            nm = {"blockupper": "BlkUpper", "blocklower": "BlkLower", "lid": "BlkLid", "MarkPack": "BlkMark"}.get(base, "Blk" + base[:1].upper() + base[1:6])
            to_bone(o, bone, nm)
        print("GAME block: %d parts" % len(blk), file=LOGF)
        # what the game needs back: the old jet pack's groups (the fan pack), the sun shade; the old visor and pods stay off
        for o in bpy.data.objects:
            if o.name.startswith("Jet") or o.name in ("SunShade", "ShadeArms"): o.hide_render = False; o.hide_viewport = False
        arm.data.pose_position = 'REST'
        dst = os.path.join(HERE, "astronavigator_suit2.blend")
        bpy.ops.wm.save_as_mainfile(filepath=dst, copy=True)
        names = sorted(o.name for o in bpy.data.objects if o.type == 'MESH' and not o.hide_render)
        open(os.path.join(OUT, "game_parts.txt"), "w", encoding="utf-8").write(chr(10).join(names))
        print("GAME saved", dst, len(names), file=LOGF)

    # ---- the render ----
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 600, 860
    if sc.world is None: sc.world = bpy.data.worlds.new("BL_world")
    sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None)
    if bg: bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("BL_key", 3.0, (42, 0, 145)), ("BL_fill", 1.4, (65, 0, -40)), ("BL_front", 1.6, (55, 0, 20)), ("BL_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("BL_cam"); cd.sensor_fit = 'VERTICAL'; cd.lens = 36
    cam = bpy.data.objects.new("BL_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0.0, -0.02, 0.95))
    VIEWS = {"front": T + Vector((0.0, -2.75, 0.10)), "q34": T + Vector((-1.55, -2.25, 0.25)), "side": T + Vector((2.75, 0.0, 0.08)), "back": T + Vector((1.2, 2.5, 0.35))}
    if os.environ.get("BL_GROIN") or (ONLY and all(k in "DEF" for k in ONLY)):
        LAME_TOP, LAME_OV, LAME_GAP = 1.205, 0.019, 0.010     # the study's lames: under the seam, 35 % overlap, motion gap
    if os.environ.get("BL_GAME"):
        LAME_TOP, LAME_OV, LAME_GAP = 1.205, 0.019, 0.010
        LAME_BOT[0] = 1.036
        body_common(lowab=False); chest_E(); g4(); game_build(); CHESTS = {}
    elif os.environ.get("BL_GROIN"):
        if not ONLY or "G4" in ONLY: LAME_BOT[0] = 1.036            # the lames end above the belt; the pelvic lame goes under it
        body_common(lowab=False); chest_E(); carry_all(); common = list(parts)
        TS = Vector((0.0, -0.05, 0.98))
        GV = {"front": TS + Vector((0.0, -2.1, 0.05)), "q34": TS + Vector((-1.2, -1.75, 0.15)), "side": TS + Vector((2.1, 0.0, 0.05))}
        SV = {"stepside": TS + Vector((2.1, -0.2, 0.05)), "stepq34": TS + Vector((1.25, -1.75, 0.15)),
              "stepclose": TS + Vector((0.75, -0.55, 0.02)), "stepcloselow": TS + Vector((0.35, -0.80, -0.22))}
        GV["helm"] = (Vector((0.45, -0.95, 1.80)), Vector((0.0, -0.04, 1.66)))       # the brow arc
        for key, (title, fn) in GROINS.items():
            if ONLY and key not in ONLY: continue
            fn(); carry_all(); mine = [o for o in parts if o not in common]
            for poseset, views in (("rest", GV), ("step", SV)):
                arm.data.pose_position = 'POSE'
                for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
                bpy.context.view_layer.update()
                if poseset == "step": rotb("LeftUpLeg", (1, 0, 0), -90); rotb("LeftLeg", (1, 0, 0), 90)
                for vn, loc in views.items():
                    loc, tg = loc if isinstance(loc, tuple) else (loc, TS)
                    cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tg - loc).to_track_quat('-Z', 'Y')
                    sc.render.filepath = os.path.join(OUT, "bg_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
            for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
            arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
            for o in mine: bpy.data.objects.remove(o, do_unlink=True); parts.remove(o)
            print("rendered", key, file=LOGF); LOGF.flush()
        CHESTS = {}
    else:
        body_common(); common = list(parts)
    for key, (title, fn) in CHESTS.items():
        if ONLY and key not in ONLY: continue
        fn(); chest = [o for o in parts if o not in common]
        for pk in ("pack", "nopack"):
            hx = HX["build_hexa"](folded=False) if pk == "pack" else []
            for vn, loc in VIEWS.items():
                cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
                sc.render.filepath = os.path.join(OUT, "bl_%s_%s_%s.png" % (key, pk, vn)); bpy.ops.render.render(write_still=True)
            if hx: HX["remove_hexa"](hx)
            print("rendered", key, pk, file=LOGF); LOGF.flush()
        for o in chest: bpy.data.objects.remove(o, do_unlink=True); parts.remove(o)
    print("DONE", file=LOGF)
except Exception:
    traceback.print_exc(file=LOGF); print("FAILED", file=LOGF)
LOGF.close()
