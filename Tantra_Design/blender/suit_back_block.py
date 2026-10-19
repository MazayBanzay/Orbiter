# THE SUIT'S BACK BLOCK of Suit_T10 - one design, calculated (the user, 2026-10-18: «посчитать и аппаратуру и газ внутри
# и питание... зачем открытые баллоны? 30 век, век материаловедения»). Everything inside a closed shell.
#
# Systems (the game's model: LifeSupport.h / Suit_T10.cfg) and their hardware:
#   O2 1.0 kg at 30 MPa (O2-safe pressure): 2.54 L -> a conformal 4-cell tank (cells D52 x 300 mm), BNNT overwrap 0.16 kg.
#     The suit's gas: 0.12 m3 of O2 at 30 kPa = 0.047 kg. Use: rest 26 g/h, work (300 W) 77 g/h, heavy (500 W) 128 g/h
#     -> 1.0 kg lasts 33 / 12 / 7.5 h.
#   N2 1.5 kg at 70 MPa: 2.98 L -> a conformal 3-cell tank (D64 x 300 mm), 0.43 kg; cold-gas RCS (Isp ~73 s): dv ~10 m/s
#     for her + the suit (104 kg) - self-rescue; 4 thruster quads at the block's corners.
#   CO2: a regenerable MOF cartridge pair, 1.6 kg CO2 at ~45 wt% (3.6 kg, ~2.8 L), heated clean at a station;
#     work 90 g/h -> 18 h (O2 runs out first).
#   Power: 6 kWh at the MPU cells' density (3.1 kWh/kg): 1.9 kg, ~2.4 L, a cassette. Loads: life 60 W, heat pump
#     Q/3 (up to 233 W), drives 50-300 W, lamps 6-24 W, the field 600 W; supply 1 kW. Walk + work 272 W -> 22 h;
#     with the field 872 W -> 7 h; the peak (1.2 kW) over the supply - the game sheds loads.
#   Heat: cooling up to 700 W + the pump's 233 W = 933 W out. The shell is the radiator (white h-BN/BNNT, eps 0.9,
#     alpha 0.2, ~0.26 m2 effective): 223 W at 360 K. The rest by a sublimator (water 2.83 MJ/kg): work 0.23 kg/h,
#     peak 0.9 kg/h from a 2 kg feedwater bladder - laid as the block's outer layer over the spine (2 g/cm2 of water:
#     hydrogen over the spinal marrow). (The set «Бор»'s plates add ~0.39 m2 of radiator if piped: +290 W.)
#   Pump, heat pump, fans, valves, regulators ~2.1 L; electronics (computer, radio, the field's and drives' power) ~1 L.
#   Hardware ~18 L in ~21 L of shell; ~19 kg with the shell. The shell: white BN-nanotube composite 4 mm (structure,
#   micrometeoroid bumper; h-BN is white by nature), nothing outside it but the radiator channels, the ports and pads.
# Form: the rigid thoracic part (z 1.21-1.48, on Spine) and an articulated lumbar part (z 1.06-1.24, on LowerBack) that
#   slides under the upper lip when she bends; inner faces taut over her back (no sag into the spine's groove); its
#   weight to the hip ring through a strut; two vent lines into the helmet ring; the jet pack's dock (4 pads + a
#   connector) on its back. Render only, on astronavigator_suit2.blend (Suit_T10 + «Бор»).
#   blender -b astronavigator_suit2.blend --python suit_back_block.py   (env SB_OUT)
import bpy, bmesh, math, os, traceback
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.environ.get("SB_OUT") or os.path.join(HERE, "..", "suit_mockups", "back_block_one"); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, "sb_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
# exec'd by another script with SB_BUILD_ONLY = True: the block is built (lists shells / outside / inside, the lid LID,
# outer_y(), the interface json) and nothing rendered; SB_SUBSURF sets the shells' subdivision (the game mesh: 1)
BUILD_ONLY = globals().get("SB_BUILD_ONLY", False); SUBSURF = globals().get("SB_SUBSURF", 2); LOD = globals().get("SB_LOD", 0)
class _BuildOnly(Exception): pass
try:
    arm = bpy.data.objects["Astronavigator"]; arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    for o in bpy.data.objects:
        if o.name in ("Pack", "MarkPack", "BackRails", "CableL", "CableR") or o.name.startswith("Jet"): o.hide_render = True
    dg = bpy.context.evaluated_depsgraph_get()
    BODY = BVHTree.FromObject(bpy.data.objects["SuitBody"], dg)
    def mk(name, col, metal, rough, alpha=1.0):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
        if alpha < 1:
            b.inputs["Alpha"].default_value = alpha
            try: m.surface_render_method = 'BLENDED'
            except Exception: pass
        m.diffuse_color = (*col, alpha); return m
    SHELL = mk("SB_shell", (0.88, 0.885, 0.89), 0.0, 0.45)      # h-BN / BNNT composite: white
    LINER = mk("SB_liner", (0.70, 0.71, 0.72), 0.0, 0.6)
    FRAME = mk("SB_frame", (0.60, 0.61, 0.63), 1.0, 0.30)
    DARK = mk("SB_dark", (0.12, 0.125, 0.13), 0.3, 0.5)
    RIB = mk("SB_rib", (0.80, 0.81, 0.82), 0.6, 0.25)
    RED = mk("SB_red", (0.78, 0.09, 0.07), 0.1, 0.35)
    O2C = mk("SB_o2", (0.12, 0.35, 0.72), 0.2, 0.35)
    N2C = mk("SB_n2", (0.06, 0.06, 0.06), 0.2, 0.4)
    SORB = mk("SB_sorb", (0.20, 0.42, 0.40), 0.1, 0.5)
    H2O = mk("SB_h2o", (0.25, 0.50, 0.32), 0.0, 0.35)
    BATT = mk("SB_batt", (0.30, 0.31, 0.33), 0.4, 0.4)
    HOSE = mk("SB_hose", (0.32, 0.33, 0.34), 0.0, 0.7)
    shells, inside, outside = [], [], []
    def link(o, group): sc.collection.objects.link(o); group.append(o); return o
    def obj(name, bm, m, group, smooth=False, bevel=0.0):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        me.materials.append(m); o = link(bpy.data.objects.new(name, me), group)
        if bevel:
            b = o.modifiers.new("b", 'BEVEL'); b.width = bevel; b.segments = 1 if LOD else 3; b.limit_method = 'ANGLE'
        return o
    def box(name, c, s, m, group, bevel=0.005):
        bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0); bmesh.ops.scale(bm, vec=Vector(s), verts=bm.verts)
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, group, bevel=bevel)
    def cyl(name, c, r, L, axis, m, group, seg=28):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=(min(seg, 12) if LOD else seg), radius1=r, radius2=r, depth=L)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix())
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, group, smooth=True, bevel=0.002)
    def sphere(name, c, r, m, group, sz=1.0):
        bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=r)
        bmesh.ops.scale(bm, vec=(sz, 1, 1), verts=bm.verts)
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, group, smooth=True)
    def tube(name, pts, r, m, group):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 1 if LOD else 3; cd.use_fill_caps = True
        sp = cd.splines.new('BEZIER'); sp.bezier_points.add(len(pts) - 1)
        for q, p in zip(sp.bezier_points, pts): q.co = p; q.handle_left_type = q.handle_right_type = 'AUTO'
        cd.materials.append(m); return link(bpy.data.objects.new(name, cd), group)

    # ---- her back: the surface under the block, made taut (the shell's inner face spans the spine's groove) ----
    def back_y(x, z):
        loc, nrm, i, d = BODY.ray_cast(Vector((x, 0.6, z)), Vector((0, -1, 0)), 1.0)
        return loc.y if loc else 0.06
    def upper_hull(ts, ys):
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
    GX = [-0.23 + 0.46 * i / 23 for i in range(24)]; GZ = [1.02 + 0.50 * j / 25 for j in range(26)]
    YB = [[back_y(x, z) for x in GX] for z in GZ]
    for _ in range(2):
        YB = [upper_hull(GX, row) for row in YB]
        cols = [upper_hull(GZ, [YB[j][i] for j in range(len(GZ))]) for i in range(len(GX))]
        YB = [[cols[i][j] for i in range(len(GX))] for j in range(len(GZ))]
    def Y(x, z):
        fx = (x - GX[0]) / (GX[1] - GX[0]); fz = (z - GZ[0]) / (GZ[1] - GZ[0])
        i = min(max(int(fx), 0), len(GX) - 2); j = min(max(int(fz), 0), len(GZ) - 2); a = min(max(fx - i, 0), 1); b = min(max(fz - j, 0), 1)
        return (YB[j][i] * (1 - a) + YB[j][i + 1] * a) * (1 - b) + (YB[j + 1][i] * (1 - a) + YB[j + 1][i + 1] * a) * b

    def shell(name, W, z0, z1, tmax, gap, round_top, round_bot, p=5.0, nu=36, nv=28, tz=None, dip=0.0, hole=None):
        """a closed shell over her back: inner face on the taut back + gap, outer = inner + thickness (round at the
        rounded edges, a flat lip at the others); its outline a superellipse where rounded"""
        bm = bmesh.new(); zc = 0.5 * (z0 + z1); H = 0.5 * (z1 - z0)
        def XZ(s, t):
            v = 2 * t - 1
            shrink = (1 - abs(v) ** p) ** (1 / p) if ((v > 0 and round_top) or (v < 0 and round_bot)) else 1.0
            return W * (2 * s - 1) * max(shrink, 0.02), zc + H * v - dip * (1 - (2 * s - 1) ** 2) * max(0.0, v) ** 3
        def thick(s, t):
            u = abs(2 * s - 1); v = 2 * t - 1
            fx = (1 - u ** 8) ** 0.5
            fz = (1 - abs(v) ** 8) ** 0.5 if ((v > 0 and round_top) or (v < 0 and round_bot)) else 1.0
            return 0.006 + (tmax - 0.006) * fx * fz * (tz(t) if tz else 1.0)
        I = [[None] * (nu + 1) for _ in range(nv + 1)]; O = [[None] * (nu + 1) for _ in range(nv + 1)]
        for j in range(nv + 1):
            for i in range(nu + 1):
                s, t = i / nu, j / nv; x, z = XZ(s, t); yi = Y(x, z) + gap
                I[j][i] = bm.verts.new((x, yi, z)); O[j][i] = bm.verts.new((x, yi + thick(s, t), z))
        inhole = lambda i, j: hole and hole[0] <= i < hole[1] and hole[2] <= j < hole[3]
        for j in range(nv):
            for i in range(nu):
                bm.faces.new((I[j][i], I[j + 1][i], I[j + 1][i + 1], I[j][i + 1]))
                if not inhole(i, j): bm.faces.new((O[j][i], O[j][i + 1], O[j + 1][i + 1], O[j + 1][i]))
        if hole:                                  # the opening's walls, down to the floor (the inner face)
            i0, i1, j0, j1 = hole
            loop = [(j0, i) for i in range(i0, i1)] + [(j, i1) for j in range(j0, j1)] + [(j1, i) for i in range(i1, i0, -1)] + [(j, i0) for j in range(j1, j0, -1)]
            for k in range(len(loop)):
                (ja, ia), (jb, ib) = loop[k], loop[(k + 1) % len(loop)]
                try: bm.faces.new((O[ja][ia], O[jb][ib], I[jb][ib], I[ja][ia]))
                except ValueError: pass
        ring = [(0, i) for i in range(nu)] + [(j, nu) for j in range(nv)] + [(nv, i) for i in range(nu, 0, -1)] + [(j, 0) for j in range(nv, 0, -1)]
        for k in range(len(ring)):
            (ja, ia), (jb, ib) = ring[k], ring[(k + 1) % len(ring)]
            try: bm.faces.new((I[ja][ia], I[jb][ib], O[jb][ib], O[ja][ia]))
            except ValueError: pass
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        o = obj(name, bm, SHELL, shells, smooth=True)
        sub = o.modifiers.new("s", 'SUBSURF'); sub.levels = SUBSURF; sub.render_levels = SUBSURF
        o["grid"] = (nu, nv)
        return o, XZ, thick
    # the thoracic part (rigid) and the lumbar part (articulated, under the upper lip)
    UPW, UZ0, UZ1, UT, UG = 0.19, 1.21, 1.48, 0.150, 0.008
    LOW, LZ0, LZ1, LT, LG = 0.16, 1.06, 1.245, 0.100, 0.008
    # the wedge (the user, 2026-10-18: «дорабатываем форму»): full depth low, thin under the helmet (she looks up),
    # the mass lower; a dip at the top's middle for the helmet ring; the service lid's opening (rows/columns of the grid)
    WEDGE = lambda t: 1.0 if t <= 0.45 else 0.43 + 0.285 * (1 + math.cos(math.pi * (t - 0.45) / 0.55))
    HOLE = (4, 32, 2, 25)
    up_o, up_xz, up_t = shell("block_upper", UPW, UZ0, UZ1, UT, UG, True, False, tz=WEDGE, dip=0.022, hole=HOLE)
    lo_o, lo_xz, lo_t = shell("block_lower", LOW, LZ0, LZ1, LT, LG, False, True)
    def lid_build():
        nu, nv = 36, 28; i0, i1, j0, j1 = HOLE
        bm = bmesh.new(); V = {}
        for j in range(j0, j1 + 1):
            for i in range(i0, i1 + 1):
                ss, tt = i / nu, j / nv; x, z = up_xz(ss, tt)
                V[j, i] = bm.verts.new((x, Y(x, z) + UG + up_t(ss, tt) + 0.0008, z))
        for j in range(j0, j1):
            for i in range(i0, i1): bm.faces.new((V[j, i], V[j, i + 1], V[j + 1, i + 1], V[j + 1, i]))
        o = obj("lid", bm, SHELL, shells, smooth=True)
        so = o.modifiers.new("t", 'SOLIDIFY'); so.thickness = 0.006; so.offset = 1.0   # (its faces look inward: along them = into the block)
        b = o.modifiers.new("b", 'BEVEL'); b.width = 0.003; b.segments = 1 if LOD else 3; b.limit_method = 'ANGLE'
        xa, za = up_xz(i0 / nu, 0.5); xb, zb = up_xz(i1 / nu, 0.5)
        return o, Vector((xa, Y(xa, za) + UG + up_t(i0 / nu, 0.5) - 0.004, 0)), xb
    LID, HINGE, LATCH_X = lid_build()
    def outer_y(x, z, which="up"):
        if which == "up":
            s = 0.5 + x / (2 * UPW); t = (z - UZ0) / (UZ1 - UZ0); return Y(x, z) + UG + up_t(min(max(s, 0), 1), min(max(t, 0), 1))
        s = 0.5 + x / (2 * LOW); t = (z - LZ0) / (LZ1 - LZ0); return Y(x, z) + LG + lo_t(min(max(s, 0), 1), min(max(t, 0), 1))

    # ---- outside: radiator channels in the white skin, the ports, the RCS quads, the dock, the vent lines, the strut ----
    lidparts = []
    for k in list(range(-6, -1)) + list(range(2, 7)):   # radiator channels in the lid: two fields, the middle left for the dock
        x = k * 0.022
        pts = [Vector((x, outer_y(x, z) + 0.0023, z)) for z in (1.245, 1.28, 1.32, 1.365)]   # (they stop under the marking)
        lidparts.append(tube("chan", pts, 0.0022, RIB, outside))
    for z in (1.255, 1.335, 1.415):                      # the hinge on her right (-x): three knuckles
        cyl("hinge", (HINGE.x - 0.004, HINGE.y + 0.004, z), 0.0075, 0.04, (0, 0, 1), FRAME, outside)
    for z in (1.29, 1.39):                               # two latches on her left
        x = LATCH_X - 0.004; box("latch", (x, outer_y(x, z) + 0.001, z), (0.016, 0.012, 0.03), DARK, outside, bevel=0.003)
    for k in range(-4, 5):
        x = k * 0.026
        pts = [Vector((x, outer_y(x, z, "lo") + 0.0015, z)) for z in (1.085, 1.12, 1.16)]
        tube("chanl", pts, 0.0020, RIB, outside)
    # the sublimator: its porous plate on the lumbar part's back (the water boils off into vacuum)
    box("sublim", (0, outer_y(0, 1.20, "lo") + 0.002, 1.205), (0.16, 0.004, 0.035), DARK, outside, bevel=0.0015)
    for sx in (-1, 1):                           # RCS quads at the corners: recessed, three nozzles each
        for (x, z, w) in ((sx * 0.165, 1.43, "up"), (sx * 0.125, 1.085, "lo")):
            y = outer_y(x, z, w) - 0.004
            box("rcs", (x, y, z), (0.035, 0.016, 0.03), DARK, outside, bevel=0.003)
            for dx, dz, ax in ((sx * 0.018, 0, (sx, 0, 0)), (0, 0.016 if z > 1.3 else -0.016, (0, 0, 1 if z > 1.3 else -1)), (0, 0, (0, 1, 0))):
                cyl("noz", (x + dx, y + (0.009 if ax == (0, 1, 0) else 0), z + dz), 0.004, 0.008, ax, FRAME, outside, seg=12)
    for sx in (-1, 1):                           # the jet pack's dock: four pads and the power/coolant connector
        for z in (1.28, 1.42):
            x = sx * 0.11; lidparts.append(cyl("pad", (x, outer_y(x, z) + 0.003, z), 0.014, 0.005, (0, 1, 0), FRAME, outside))
    lidparts.append(box("dockconn", (0, outer_y(0, 1.33) + 0.004, 1.33), (0.05, 0.006, 0.03), DARK, outside, bevel=0.002))
    # the service port (power, O2/N2/water fill, coolant) on her left: the station's or the MPU's umbilical
    x = -0.150; z = 1.15
    cyl("port", (x, outer_y(x, z, "lo") - 0.01, z), 0.016, 0.02, (0, 1, 0), FRAME, outside)
    cyl("portc", (x, outer_y(x, z, "lo") + 0.001, z), 0.010, 0.004, (0, 1, 0), DARK, outside)
    # vent lines to the helmet ring (O2 in, out to the beds) and the strut to the hip ring
    for sx in (-1, 1):
        tube("vent", [Vector((sx * 0.05, Y(sx * 0.05, 1.47) + 0.05, 1.475)), Vector((sx * 0.05, 0.10, 1.50)), Vector((sx * 0.055, 0.06, 1.475))], 0.011, HOSE, outside)
    tube("strut", [Vector((0, Y(0, 1.06) + 0.04, 1.07)), Vector((0, 0.115, 1.045)), Vector((0, 0.13, 1.03))], 0.010, FRAME, outside)
    # the marking: «37 ЗВЁЗДНАЯ» on the upper part, a red line over it
    mp = bpy.data.objects.get("MarkPack")
    if mp:
        mp.hide_render = False
        if not mp.get("on_lid"):                 # laid on the lid point by point (the lid is sloped: the wedge)
            M = mp.matrix_world.copy(); Mi = M.inverted()
            for v in mp.data.vertices:
                w = M @ v.co; w.z += 1.398 - 1.412; w.y = outer_y(w.x, w.z) + 0.0012; v.co = Mi @ w
            mp["on_lid"] = 1
        lidparts.append(mp)
    lidparts.append(box("stripe", (0, outer_y(0, 1.370) + 0.0012, 1.370), (0.14, 0.002, 0.007), RED, outside, bevel=0.0004))

    # ---- inside: the hardware where the calculation puts it ----
    ycell = lambda x, z: Y(x, z) + UG + 0.006
    for k, z in enumerate((1.290, 1.343, 1.396, 1.449 - 0.004)):   # O2: 4 cells D52 x 300, against the back
        y = ycell(0, z) + 0.026
        cyl("o2cell", (0, y, z), 0.026, 0.27, (1, 0, 0), FRAME, inside)
        for sx in (-1, 1): sphere("o2dome", (sx * 0.135, y, z), 0.026, FRAME, inside, sz=0.55)
        cyl("o2band", (0, y, z), 0.0266, 0.08, (1, 0, 0), O2C, inside)
    for sx in (-1, 1): cyl("o2man", (sx * 0.152, ycell(0, 1.37) + 0.026, 1.37), 0.007, 0.17, (0, 0, 1), FRAME, inside)
    yb = ycell(0, 1.31) + 0.058
    box("battery", (0, yb + 0.022, 1.315), (0.15, 0.044, 0.17), BATT, inside, bevel=0.004)
    box("batlab", (0, yb + 0.045, 1.37), (0.10, 0.002, 0.012), RED, inside, bevel=0.0004)
    handle_y = yb + 0.046
    for sx in (-1, 1):
        box("sorbent", (sx * 0.122, yb + 0.03, 1.305), (0.085, 0.060, 0.15), SORB, inside, bevel=0.006)
        cyl("sgrip", (sx * 0.122, yb + 0.062, 1.37), 0.004, 0.05, (1, 0, 0), FRAME, inside)
    cyl("bgrip", (0, handle_y + 0.008, 1.385), 0.004, 0.07, (1, 0, 0), FRAME, inside)
    box("heatpump", (0, yb + 0.025, 1.222), (0.26, 0.05, 0.022), DARK, inside, bevel=0.004)
    cyl("compr", (-0.08, yb + 0.025, 1.222), 0.016, 0.05, (1, 0, 0), FRAME, inside)
    for sx in (-1, 1): cyl("fan", (sx * 0.05, ycell(0, 1.45) + 0.06, 1.452), 0.014, 0.01, (0, 1, 0), FRAME, inside)
    box("electronics", (0, ycell(0, 1.425) + 0.062, 1.425), (0.24, 0.010, 0.035), DARK, inside, bevel=0.002)
    for k, z in enumerate((1.095, 1.160, 1.225)):                    # N2: 3 cells D64 x 300 in the lumbar part
        y = Y(0, z) + LG + 0.006 + 0.032
        cyl("n2cell", (0, y, z), 0.032, 0.25, (1, 0, 0), FRAME, inside)
        for sx in (-1, 1): sphere("n2dome", (sx * 0.125, y, z), 0.032, FRAME, inside, sz=0.55)
        cyl("n2band", (0, y, z), 0.0326, 0.07, (1, 0, 0), N2C, inside)
    yw = Y(0, 1.15) + LG + 0.072
    box("feedwater", (0, yw + 0.012, 1.15), (0.26, 0.022, 0.16), H2O, inside, bevel=0.009)  # its outer layer: water over the spine
    # the inner liner (cutaway shots show it instead of the shell)
    def liner(name, W, z0, z1, gap, xz):
        bm = bmesh.new(); nu, nv = 24, 16; V = [[None] * (nu + 1) for _ in range(nv + 1)]
        for j in range(nv + 1):
            for i in range(nu + 1):
                x, z = xz(i / nu, j / nv); V[j][i] = bm.verts.new((x, Y(x, z) + gap + 0.003, z))
        for j in range(nv):
            for i in range(nu): bm.faces.new((V[j][i], V[j][i + 1], V[j + 1][i + 1], V[j + 1][i]))
        return obj(name, bm, LINER, inside, smooth=True)
    liner("liner_up", UPW, UZ0, UZ1, UG, up_xz); liner("liner_lo", LOW, LZ0, LZ1, LG, lo_xz)
    print("built: shells %d, outside %d, inside %d" % (len(shells), len(outside), len(inside)), file=LOG)
    # ---- the interface (for the jet pack and the game): Blender frame, metres; the game frame is (-x, z - 0.93, -y) ----
    import json
    V3 = lambda v: [round(v[0], 4), round(v[1], 4), round(v[2], 4)]
    IFACE = {
        "frame": "Blender (x her left, y her back, z up), m; game/Orbiter model frame = (-x, z - 0.93, -y)",
        "upper": {"bone": "Spine1", "x": [-UPW, UPW], "z": [UZ0, UZ1], "top_centre_z": round(UZ1 - 0.022, 4),
                  "inner_y_centre": {str(z): round(Y(0, z) + UG, 4) for z in (1.22, 1.30, 1.38, 1.45)},
                  "outer_y_centre": {str(z): round(outer_y(0, z), 4) for z in (1.22, 1.30, 1.38, 1.45)},
                  "outer_y_max": round(max(outer_y(x, z) for x in (-0.1, 0, 0.1) for z in (1.22, 1.26, 1.30, 1.34)), 4)},
        "lower": {"bone": "LowerBack", "x": [-LOW, LOW], "z": [LZ0, LZ1],
                  "outer_y_centre": {str(z): round(outer_y(0, z, "lo"), 4) for z in (1.08, 1.15, 1.22)}},
        "dock_pads": [V3((sx * 0.11, outer_y(sx * 0.11, z) + 0.0055, z)) for sx in (-1, 1) for z in (1.28, 1.42)],
        "dock_pad_radius": 0.014, "dock_normal": [0, 1, 0],
        "dock_connector": {"centre": V3((0, outer_y(0, 1.33) + 0.007, 1.33)), "size_xz": [0.05, 0.03], "carries": "power 1 kW DC, coolant loop, data"},
        "service_lid": {"hinge_axis_xy": V3((HINGE.x, HINGE.y, 0))[:2], "hinge_z": [1.235, 1.435], "opens_deg": 105,
                        "latch_x": round(LATCH_X, 4), "note": "the dock pads sit on the lid: the jet pack comes off before the lid opens"},
        "service_port": V3((-0.150, outer_y(-0.150, 1.15, "lo"), 1.15)),
        "rcs_quads": [V3((sx * 0.165, outer_y(sx * 0.165, 1.43), 1.43)) for sx in (-1, 1)] + [V3((sx * 0.125, outer_y(sx * 0.125, 1.085, "lo"), 1.085)) for sx in (-1, 1)],
        "vents_to_helmet_ring": [[sx * 0.055, 0.06, 1.475] for sx in (-1, 1)],
        "strut_to_hip_ring": {"bone": "Hips", "end": [0, 0.13, 1.03]},
        "hide": ["Pack", "BackRails", "CableL", "CableR", "Jet*"], "moved": {"MarkPack": "onto the lid, z 1.398"},
        "build": "exec(open(r'Tantra_Design/blender/suit_back_block.py', encoding='utf-8').read(), G) with G = {'__name__': 'x', 'SB_BUILD_ONLY': True, 'SB_SUBSURF': 1}; then G['shells'], G['outside'], G['inside'] (remove 'inside' for the outside look)",
    }
    json.dump(IFACE, open(os.path.join(HERE, "back_block_interface.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    if BUILD_ONLY: raise _BuildOnly()

    # ---- the render ----
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 720, 820
    if sc.world is None: sc.world = bpy.data.worlds.new("SB_world")
    sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None)
    if bg: bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("SB_key", 3.0, (42, 0, 145)), ("SB_fill", 1.4, (65, 0, -40)), ("SB_back", 2.4, (50, 0, 200)), ("SB_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("SB_cam"); cd.sensor_fit = 'VERTICAL'; cd.lens = 50
    cam = bpy.data.objects.new("SB_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0.0, 0.10, 1.26))
    VIEWS = {"q34": T + Vector((0.95, 1.35, 0.30)), "back": T + Vector((0.0, 1.70, 0.12)), "side": T + Vector((1.70, -0.02, 0.06)),
             "top": T + Vector((0.55, 1.05, 0.95))}
    def shoot(tag, views):
        for vn, loc in views.items():
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "sb_%s_%s.png" % (tag, vn)); bpy.ops.render.render(write_still=True)
    for o in lidparts:                             # the lid carries its channels, the dock, the marking
        mw = o.matrix_world.copy(); o.parent = LID; o.matrix_parent_inverse = LID.matrix_world.inverted(); o.matrix_world = mw
    for o in inside: o.hide_render = True
    shoot("ext", VIEWS)
    # the look up: the neck back 2 x 18 deg - the helmet's rear clears the block's thin top
    arm.data.pose_position = 'POSE'
    def rotb(bone, deg):
        pb = arm.pose.bones[bone]; h = pb.head.copy(); R = Matrix.Rotation(math.radians(deg), 4, 'X')
        pb.matrix = Matrix.Translation(h) @ R @ Matrix.Translation(-h) @ pb.matrix; bpy.context.view_layer.update()
    rotb("Neck", -18); rotb("Neck1", -18)
    shoot("lookup", {"side": VIEWS["side"]})
    for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    # open on the service stand
    LID.matrix_world = Matrix.Translation(HINGE) @ Matrix.Rotation(math.radians(105), 4, 'Z') @ Matrix.Translation(-HINGE) @ LID.matrix_world
    lower_inside = ("n2cell", "n2dome", "n2band", "feedwater")       # the lumbar part stays closed (filled through the port)
    for o in inside:
        if not o.name.startswith("liner") and o.name.split(".")[0] not in lower_inside: o.hide_render = False
    shoot("open", {"q34": VIEWS["q34"], "back": VIEWS["back"], "top": VIEWS["top"]})
    for o in inside: o.hide_render = True
    LID.matrix_world = Matrix.Translation(HINGE) @ Matrix.Rotation(math.radians(-105), 4, 'Z') @ Matrix.Translation(-HINGE) @ LID.matrix_world
    for o in inside: o.hide_render = False
    for o in shells + outside: o.hide_render = True
    if mp: mp.hide_render = True
    shoot("cut", {"q34": VIEWS["q34"], "back": VIEWS["back"], "side": VIEWS["side"]})
    outer_layer = ("battery", "batlab", "sorbent", "heatpump", "compr", "fan", "electronics", "feedwater")
    for o in inside:
        if o.name.split(".")[0] in outer_layer: o.hide_render = True
    shoot("cut2", {"q34": VIEWS["q34"], "back": VIEWS["back"]})
    print("DONE", file=LOG)
except _BuildOnly:
    print("BUILT (build only)", file=LOG)
except Exception:
    traceback.print_exc(file=LOG); print("FAILED", file=LOG)
    if BUILD_ONLY: LOG.close(); raise
LOG.close()
