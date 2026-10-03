# Mock-up of the «прыгающий скелет» (suit class 3) over the astronavigator suit - looks only, nothing for the game.
# blender --background astronavigator_suit.blend --python mock_skeleton.py   (SK_VARIANT = A | B)
#   A «Пружины»: coil springs across the knee, a leaf spring at the ankle
#   B «Клинок»:  a blade spring behind the shin down under the forefoot (the kangaroo's tendon)
# Both: pelvis frame with a seat under the buttocks (her weight goes into the frame), two spine rails behind the pack up
# to a yoke over the shoulders, powered arms along the outside of hers with a gripper beside each hand, foot plates.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
V = os.environ.get("SK_VARIANT", "A"); LOG = os.path.join(HERE, "mock_skeleton.log"); log = []

def mat(name, col, rough=0.4, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    return m

def obj(name, bm, mats):
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = True
    for m in mats: me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); return o

def ring(bm, c, ax, r, seg, mat_i=0):
    ax = ax.normalized(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    return [c + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r for j in range(seg)]

def loft(bm, rings, mat_i=0, cap=True, closed=False):
    Vs = [[bm.verts.new(p) for p in r] for r in rings]; n = len(rings[0])
    for i in range(len(Vs) if closed else len(Vs) - 1):
        A, B = Vs[i], Vs[(i + 1) % len(Vs)]
        for j in range(n): bm.faces.new((A[j], A[(j + 1) % n], B[(j + 1) % n], B[j])).material_index = mat_i
    if cap and not closed:
        bm.faces.new(Vs[0][::-1]).material_index = mat_i; bm.faces.new(Vs[-1]).material_index = mat_i

def tube(bm, ctrl, r, mat_i=0, seg=12, per=8, flat=1.0):
    P = [ctrl[0] * 2 - ctrl[1]] + list(ctrl) + [ctrl[-1] * 2 - ctrl[-2]]; pts = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        for k in range(per):
            t = k / per; t2 = t * t; t3 = t2 * t
            pts.append(0.5 * ((2 * p1) + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (3 * p1 - p0 - 3 * p2 + p3) * t3))
    pts.append(ctrl[-1].copy())
    T = [(pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized() for i in range(len(pts))]
    n = T[0].cross(Vector((1, 0, 0))) if abs(T[0].x) < 0.9 else T[0].orthogonal(); n.normalize(); rings = []
    for i, p in enumerate(pts):
        n = (n - T[i] * n.dot(T[i])).normalized(); bv = T[i].cross(n)
        rings.append([p + (n * math.cos(2 * math.pi * j / seg) * flat + bv * math.sin(2 * math.pi * j / seg)) * r for j in range(seg)])
    loft(bm, rings, mat_i)

def disc(bm, c, ax, r, w, mat_rim, mat_face):
    ax = ax.normalized()
    loft(bm, [ring(bm, c - ax * w / 2, ax, r, 24), ring(bm, c + ax * w / 2, ax, r, 24)], mat_rim)
    loft(bm, [ring(bm, c + ax * (w / 2 + 0.003), ax, r * 0.62, 24), ring(bm, c + ax * (w / 2 + 0.006), ax, r * 0.55, 24)], mat_face)

def box(bm, c, size, mat_i=0, bev=0.006):
    r = bmesh.ops.create_cube(bm, size=1.0); vs = r['verts']
    for v in vs: v.co = Vector((c.x + v.co.x * size.x, c.y + v.co.y * size.y, c.z + v.co.z * size.z))
    for f in {f for v in vs for f in v.link_faces}: f.material_index = mat_i
    res = bmesh.ops.bevel(bm, geom=list({e for v in vs for e in v.link_edges}), offset=bev, segments=2, profile=0.5, affect='EDGES')
    for f in res['faces']: f.material_index = mat_i

def helix(c0, c1, r, turns, side):
    ax = c1 - c0; L = ax.length; ax.normalize(); e1 = side.normalized(); e2 = ax.cross(e1); n = int(turns * 16)
    return [c0 + ax * (L * k / n) + (e1 * math.cos(2 * math.pi * turns * k / n) + e2 * math.sin(2 * math.pi * turns * k / n)) * r for k in range(n + 1)]

try:
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    B = lambda n, tail=False: arm.matrix_world @ (arm.data.bones[n].tail_local if tail else arm.data.bones[n].head_local)
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True          # the jet pack is not worn with the skeleton
    FR = mat("SkFrame", (0.09, 0.10, 0.115), 0.30, 0.65); MUS = mat("SkMuscle", (0.16, 0.17, 0.19), 0.45, 0.25)
    TEN = mat("SkTendon", (0.52, 0.54, 0.57), 0.25, 0.8); PAD = mat("SkPad", (0.20, 0.21, 0.23), 0.8)
    CELL = mat("SkCell", (0.05, 0.055, 0.06), 0.2, 0.4); RED = mat("SkRed", (0.70, 0.12, 0.10), 0.4)
    b = bmesh.new()   # materials: 0 frame, 1 muscle, 2 tendon, 3 pad, 4 storage cells, 5 red
    def pin(p, ax, r=0.019, L=0.050):                       # a hinge: an axle in the frame
        ax = ax.normalized(); loft(b, [ring(b, p - ax * L / 2, ax, r, 20), ring(b, p + ax * L / 2, ax, r, 20)], 0)
        for k in (-1, 1): loft(b, [ring(b, p + ax * k * (L / 2), ax, r * 0.5, 16), ring(b, p + ax * k * (L / 2 + 0.004), ax, r * 0.5, 16)], 5)
    def muscle(p0, p1, r=0.024, tendon=0.30):
        """an artificial muscle: a sheathed bundle of contractile fibre, swelling in the middle, running into a flat
        composite tendon that stores the landing and gives it back; anchored at both ends"""
        d = p1 - p0; L = d.length; u = d / L; e = p0 + u * (L * (1 - tendon))
        side = u.cross(Vector((1, 0, 0))); side = side if side.length > 0.2 else u.orthogonal(); side.normalize(); up = u.cross(side)
        rings = []
        for k in range(15):
            t = k / 14; c = p0 + (e - p0) * t; rr = r * (0.38 + 0.62 * math.sin(math.pi * min(1.0, t * 1.08)) ** 0.8)
            rings.append([c + (side * math.cos(2 * math.pi * j / 18) * 1.15 + up * math.sin(2 * math.pi * j / 18) * 0.85) * rr for j in range(18)])
        loft(b, rings, 1)
        if tendon > 0: tube(b, [e - u * 0.01, p1], r * 0.55, 2, seg=12, per=1, flat=0.28)
        loft(b, [ring(b, p0 - u * 0.004, u, r * 0.45, 12), ring(b, p0 + u * 0.010, u, r * 0.45, 12)], 0)
    def blade(pts, w=0.016, flat=0.55): tube(b, pts, w, 0, seg=16, per=8, flat=flat)
    # ---- pelvis frame close round the suit's hip ring; the energy store is in it; a seat bar under the buttocks ----
    PZ = 0.985; AX, AY, CY = 0.272, 0.205, 0.018
    ov = lambda t, dr=0.0, dz=0.0: Vector(((AX + dr) * math.sin(t), CY - (AY + dr) * math.cos(t), PZ + dz))
    loft(b, [[ov(2 * math.pi * j / 64, dr, dz) for j in range(64)] for dz, dr in ((-0.026, 0.0), (-0.030, 0.012), (-0.026, 0.024), (0.026, 0.024), (0.030, 0.012), (0.026, 0.0))], 0, cap=False, closed=True)
    for j in range(64):                                      # storage cells: a dark band round the outside of the frame
        if 0.6 < abs(math.pi - 2 * math.pi * j / 64) < 2.6: pass
    loft(b, [[ov(2 * math.pi * j / 64, 0.0245, dz) for j in range(64)] for dz in (-0.012, 0.012)], 4, cap=False, closed=False)
    for sx in (-1, 1):
        blade([ov(sx * 1.75), Vector((sx * 0.20, 0.16, 0.90)), Vector((sx * 0.09, 0.168, 0.866)), Vector((0, 0.168, 0.862))], 0.015)
    box(b, Vector((0, 0.160, 0.874)), Vector((0.20, 0.06, 0.02)), 3, 0.008)
    # ---- spine: two flat blades behind the life-support pack, each carrying a column of storage cells, into a yoke ----
    PB = 0.262
    for sx in (-1, 1):
        blade([ov(sx * 2.45), Vector((sx * 0.215, PB, 1.12)), Vector((sx * 0.215, PB, 1.45)), Vector((sx * 0.20, PB - 0.01, 1.565))], 0.022, 0.45)
        for k in range(6):
            z = 1.12 + k * 0.055
            box(b, Vector((sx * 0.215, PB + 0.012, z)), Vector((0.030, 0.006, 0.044)), 4, 0.002)
    blade([Vector((-0.20, PB - 0.01, 1.565)), Vector((0, PB - 0.02, 1.582)), Vector((0.20, PB - 0.01, 1.565))], 0.020, 0.5)
    # ---- arms: hinges at shoulder and elbow, slim beams outside her arms; muscles over them; a gripper ----
    for side, sx in (("Left", 1), ("Right", -1)):
        Sh, El, Wr = B(side + "Arm"), B(side + "ForeArm"), B(side + "Hand")
        d1 = (El - Sh).normalized(); out = Vector((sx, 0, 0)); n1 = (out - d1 * out.dot(d1)).normalized()
        n1 = (n1 + Vector((0, 0, 0.6))).normalized(); n1 = (n1 - d1 * n1.dot(d1)).normalized()
        d2 = (Wr - El).normalized(); n2 = (out - d2 * out.dot(d2)).normalized()
        S = Sh + n1 * 0.085; E = El + (n1 + n2).normalized() * 0.075; W = Wr + n2 * 0.060
        Y = Vector((sx * 0.20, PB - 0.01, 1.565))
        blade([Y, Vector((sx * 0.25, 0.15, 1.54)), S + Vector((0, 0.06, 0.035)), S], 0.017)
        pin(S, Vector((1, 0, 0)), 0.023)
        blade([S, E], 0.014); ax_e = d1.cross(d2); ax_e = ax_e.normalized() if ax_e.length > 1e-4 else Vector((0, 1, 0)); pin(E, ax_e, 0.020)
        blade([E - d2 * 0.07, W], 0.013)
        muscle(Y + Vector((sx * 0.03, -0.03, -0.02)), S + (E - S) * 0.32 + Vector((0, 0.03, 0)), 0.022, 0.25)    # shoulder
        muscle(S + (E - S) * 0.15 + n1 * 0.035, E - d2 * 0.07, 0.020, 0.30)                                      # elbow (triceps line)
        muscle(S + (E - S) * 0.20 - n1 * 0.0 + Vector((0, -0.035, 0)), E + d2 * 0.08 + Vector((0, -0.03, 0)), 0.017, 0.30)   # elbow (biceps line)
        loft(b, [ring(b, Wr - d2 * 0.075, d2, 0.062, 28), ring(b, Wr - d2 * 0.050, d2, 0.062, 28)], 0)
        g = W + d2 * 0.03; sv = d2.cross(n2).normalized()
        box(b, g, Vector((0.040, 0.040, 0.040)), 0, 0.010)
        for k in (-1, 1): blade([g + sv * 0.016 * k, g + d2 * 0.08 + sv * 0.028 * k, g + d2 * 0.16 + sv * 0.012 * k - n2 * 0.02], 0.009, 0.6)
        muscle(E + (W - E) * 0.25 + n2 * 0.022, g - d2 * 0.012 + n2 * 0.02, 0.013, 0.35)                        # grip
    # ---- legs: hinges outside hip, knee, ankle; slim beams; muscles as glutes, quadriceps, calf; tendons ----
    for side, sx in (("Left", 1), ("Right", -1)):
        Hj, Kj, Aj = B(side + "UpLeg"), B(side + "Leg"), B(side + "Foot")
        Ft = B(side + "ToeBase") if (side + "ToeBase") in arm.data.bones else B(side + "Foot", True)
        Hd = Vector((sx * 0.296, Hj.y, PZ - 0.045)); Kd = Vector((sx * 0.282, Kj.y - 0.02, Kj.z)); Ad = Vector((sx * 0.262, Aj.y + 0.01, Aj.z - 0.01))
        for p in (Hd, Kd, Ad): pin(p, Vector((1, 0, 0)))
        blade([ov(sx * 1.57, 0.024), Hd], 0.016)
        blade([Hd, Kd], 0.019); blade([Kd, Ad], 0.016)
        KL = Kd + Vector((0, -0.070, 0.010)); blade([Kd, KL], 0.012, 0.5)                                       # knee lever on the shin
        HL = Hd + Vector((0, 0.075, -0.035)); blade([Hd, HL], 0.012, 0.5)                                       # hip lever behind
        fx = Aj.x; heel = Aj.y + 0.075; toe = Ft.y - 0.07
        box(b, Vector((fx, 0.5 * (heel + toe), -0.004)), Vector((0.125, heel - toe, 0.020)), 0, 0.008)          # foot plate
        blade([Ad, Vector((sx * 0.24, Aj.y + 0.01, 0.05)), Vector((fx + sx * 0.062, Aj.y + 0.01, 0.010))], 0.013)
        AL = Vector((sx * 0.25, heel + 0.04, 0.035)); blade([Vector((sx * 0.245, Aj.y + 0.01, 0.045)), AL], 0.011, 0.5)   # heel lever
        muscle(ov(sx * 2.05, 0.024) + Vector((0, 0, -0.012)), HL, 0.026, 0.20)                                  # hip: glute line
        muscle(Hd + (Kd - Hd) * 0.10 + Vector((0, -0.05, 0)), KL, 0.026, 0.28)                                  # knee: quadriceps line
        blade([Hd + (Kd - Hd) * 0.10, Hd + (Kd - Hd) * 0.10 + Vector((0, -0.05, 0))], 0.010, 0.6)
        muscle(Hd + (Kd - Hd) * 0.12 + Vector((0, 0.05, 0)), Kd + (Ad - Kd) * 0.12 + Vector((0, 0.05, 0)), 0.020, 0.25)   # knee: hamstring line
        muscle(Kd + (Ad - Kd) * 0.06 + Vector((0, 0.055, 0)), AL, 0.025, 0.45)                                  # ankle: calf and its long tendon
        blade([Kd + (Ad - Kd) * 0.06, Kd + (Ad - Kd) * 0.06 + Vector((0, 0.055, 0))], 0.010, 0.6)
        for t_, base, tip, rr in ((0.55, Hj, Kj, 0.098), (0.45, Kj, Aj, 0.078)):
            c = base + (tip - base) * t_; ax = (tip - base).normalized()
            loft(b, [ring(b, c - ax * 0.020, ax, rr, 32), ring(b, c + ax * 0.020, ax, rr, 32)], 0)
    o = obj("Skeleton" + V, b, [FR, MUS, TEN, PAD, CELL, RED])
    log.append("skeleton %s: %d verts" % (V, len(o.data.vertices)))
    sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage()
    render_util.shoot(cam, OUT, [("skeleton_%s_threequarter" % V, (3.3, -3.9, 1.38), 0.99, 70), ("skeleton_%s_side" % V, (-4.2, -0.4, 1.1), 0.95, 70),
                                 ("skeleton_%s_back" % V, (-2.6, 4.2, 1.35), 0.99, 70)], log)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
