# «Усилитель» - the powered frame over the coverall or the light suit (Orbitersdk/OrbiterCrew/AMPLIFIER.md) on Niza's
# figure: the frame is built in her rest pose, a part per bone, and rides her bones' pose (its joint axes on hers).
# blender --background astronavigator_coverall.blend --python mock_amp.py
#   AMP_SHOT = poses (default) -> renders\amp_stand / amp_squat / amp_carry .png
#              hands           -> renders\amp_hands.png (the exchangeable end effectors in a row)
# Blender: x her left, front -y, z up. Look: "the USSR 500 years on" - massive faceted ceramic shells, graphite joint
# housings and belt, orange borazon caps, MR gel in teal.
import bpy, bmesh, math, os, traceback
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
SHOT = os.environ.get("AMP_SHOT", "poses"); LOG = os.path.join(HERE, "mock_amp.log"); log = []
PLATE = 0.045                                   # the frame's sole under her shoe: the ground is this far below z = 0

# ================= materials and primitives =================
M = {}
def mat(name, col, rough=0.45, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    return m
def mats():
    M["shell"] = mat("AmShell", (0.055, 0.058, 0.062), 0.42, 0.25); M["shell2"] = mat("AmShell2", (0.70, 0.33, 0.06), 0.55, 0.05)
    M["cap"] = mat("AmCap", (0.86, 0.85, 0.81), 0.45)
    M["frame"] = mat("AmFrame", (0.11, 0.115, 0.12), 0.4, 0.35); M["joint"] = mat("AmJoint", (0.30, 0.31, 0.32), 0.3, 0.8)
    M["orange"] = mat("AmOrange", (0.80, 0.42, 0.10), 0.5, 0.1); M["gel"] = mat("AmGel", (0.10, 0.55, 0.45), 0.25)
    M["steel"] = mat("AmSteel", (0.45, 0.46, 0.48), 0.3, 0.9); M["dark"] = mat("AmDark", (0.02, 0.02, 0.025), 0.7)
    M["crate"] = mat("AmCrate", (0.42, 0.36, 0.26), 0.7); M["ground"] = mat("AmGround", (0.30, 0.29, 0.27), 0.95)

parts = {}; PART = ["Hips"]
def part(name): PART[0] = name
def bm_of(k):
    key = (PART[0], k)
    if key not in parts: parts[key] = bmesh.new()
    return parts[key]

def octa(c, e1, e2, w, d, ch):
    k = ch * min(w, d); hw, hd = w / 2, d / 2
    pts = [(-hw + k, -hd), (hw - k, -hd), (hw, -hd + k), (hw, hd - k), (hw - k, hd), (-hw + k, hd), (-hw, hd - k), (-hw, -hd + k)]
    return [c + e1 * x + e2 * y for x, y in pts]
def obox(k, a, b, w, d, up=(1, 0, 0), w1=None, d1=None, ch=0.3, mid=None):
    """a chamfered (octagonal) prism a -> b: w across `up`, d across the other way; tapers to w1, d1"""
    a, b = Vector(a), Vector(b); ax = b - a
    if ax.length < 1e-6: return
    ax.normalize(); e1 = Vector(up).normalized(); e1 = e1 - ax * e1.dot(ax)
    if e1.length < 1e-4: e1 = ax.orthogonal()
    e1.normalize(); e2 = ax.cross(e1)
    w1 = w if w1 is None else w1; d1 = d if d1 is None else d1
    secs = [(0.0, w, d)]
    if mid: t, sc = mid; secs.append((t, (w + (w1 - w) * t) * sc, (d + (d1 - d) * t) * sc))
    secs.append((1.0, w1, d1))
    bm = bm_of(k); rings = [[bm.verts.new(p) for p in octa(a + (b - a) * t, e1, e2, ww, dd, ch)] for t, ww, dd in secs]
    for A, B in zip(rings, rings[1:]):
        for j in range(8): bm.faces.new((A[j], A[(j + 1) % 8], B[(j + 1) % 8], B[j]))
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
def cbox(k, lo, hi, ch=0.25):
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    obox(k, ((x0 + x1) / 2, (y0 + y1) / 2, z0), ((x0 + x1) / 2, (y0 + y1) / 2, z1), x1 - x0, y1 - y0, ch=ch)
def cyl(k, a, b, r, seg=24, cap=True):
    a, b = Vector(a), Vector(b); ax = b - a
    if ax.length < 1e-6: return
    ax.normalize(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1); bm = bm_of(k)
    ra = [bm.verts.new(a + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r) for j in range(seg)]
    rb = [bm.verts.new(b + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r) for j in range(seg)]
    for j in range(seg): bm.faces.new((ra[j], ra[(j + 1) % seg], rb[(j + 1) % seg], rb[j]))
    if cap: bm.faces.new(ra[::-1]); bm.faces.new(rb)
def octcyl(k, c, ax, r, half):
    """an octagonal housing: a joint's drum with flat faces"""
    c, ax = Vector(c), Vector(ax).normalized(); e1 = ax.orthogonal().normalized()
    obox(k, c - ax * half, c + ax * half, 2 * r, 2 * r, e1, ch=0.29)
def arc_plate(k, c, axis, ref, r, a0, a1, half, thick=0.02, seg=10):
    """a curved shell plate: the arc a0..a1 (rad, from `ref` about `axis`) of radius r, `half` along the axis each way"""
    c, axis = Vector(c), Vector(axis).normalized(); ref = Vector(ref); ref = (ref - axis * ref.dot(axis)).normalized(); e2 = axis.cross(ref)
    bm = bm_of(k); rows = []
    for rr in (r, r + thick):
        for s in (-half, half):
            rows.append([bm.verts.new(c + axis * s + (ref * math.cos(a0 + (a1 - a0) * j / seg) + e2 * math.sin(a0 + (a1 - a0) * j / seg)) * rr) for j in range(seg + 1)])
    iA, iB, oA, oB = rows
    for j in range(seg):
        bm.faces.new((iA[j], iA[j + 1], iB[j + 1], iB[j])); bm.faces.new((oA[j], oB[j], oB[j + 1], oA[j + 1]))
        bm.faces.new((iA[j], oA[j], oA[j + 1], iA[j + 1])); bm.faces.new((iB[j], iB[j + 1], oB[j + 1], oB[j]))
    bm.faces.new((iA[0], iB[0], oB[0], oA[0])); bm.faces.new((iA[-1], oA[-1], oB[-1], iB[-1]))
def ring(k, c, ax, r, w=0.02, t=0.012):
    """a cuff band round a limb"""
    c, ax = Vector(c), Vector(ax).normalized(); bm = bm_of(k); seg = 20; e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    vs = [[bm.verts.new(c + ax * s + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * rr) for j in range(seg)] for s, rr in ((-w / 2, r), (w / 2, r), (w / 2, r + t), (-w / 2, r + t))]
    for q in range(4):
        A, B = vs[q], vs[(q + 1) % 4]
        for j in range(seg): bm.faces.new((A[j], A[(j + 1) % seg], B[(j + 1) % seg], B[j]))

# ================= the figure =================
ARM = None; REST = {}
def bone_rest(n):
    b = ARM.data.bones[n]; return ARM.matrix_world @ b.head_local, ARM.matrix_world @ b.tail_local
def reset_pose():
    for pb in ARM.pose.bones: pb.rotation_mode = 'QUATERNION'; pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0); pb.scale = (1, 1, 1)
    bpy.context.view_layer.update()
def world(n): return ARM.matrix_world @ ARM.pose.bones[n].matrix
def head(n): return world(n).translation.copy()
def set_world(n, Mw):
    ARM.pose.bones[n].matrix = ARM.matrix_world.inverted() @ Mw; bpy.context.view_layer.update()
def aim(n, d, twist_ref=None, twist_want=None):
    """turn bone n about its head so that its axis points along d; optionally twist so that the rest-pose direction
    twist_ref (world, rest) goes as near to twist_want as it can"""
    Mw = world(n); y = (Mw.to_3x3() @ Vector((0, 1, 0))).normalized(); d = Vector(d).normalized()
    q = y.rotation_difference(d); p = Mw.translation
    R = Matrix.Translation(p) @ q.to_matrix().to_4x4() @ Matrix.Translation(-p)
    set_world(n, R @ Mw)
    if twist_ref is not None:
        cur = delta(n).to_3x3() @ Vector(twist_ref)
        a = (cur - d * cur.dot(d)).normalized(); b = (Vector(twist_want) - d * Vector(twist_want).dot(d)).normalized()
        ang = math.atan2(a.cross(b).dot(d), a.dot(b)); Mw = world(n)
        set_world(n, Matrix.Translation(p) @ Matrix.Rotation(ang, 4, d) @ Matrix.Translation(-p) @ Mw)
def delta(n):
    """rest pose -> posed, for bone n (world)"""
    return world(n) @ (ARM.matrix_world @ ARM.data.bones[n].matrix_local).inverted()
def ik2(S, T, l1, l2, pole):
    d = T - S; D = min(d.length, l1 + l2 - 1e-4); u = d.normalized()
    a = (l1 * l1 + D * D - l2 * l2) / (2 * D); h = math.sqrt(max(0.0, l1 * l1 - a * a))
    p = Vector(pole); p = (p - u * p.dot(u)).normalized()
    return S + u * a + p * h
def blen(n): h, t = bone_rest(n); return (t - h).length
def rdir(n): h, t = bone_rest(n); return (t - h).normalized()

def stilts():
    """the stilt columns under the soles: the base frame (the extensions, the wheels locked) on the ground, two
    telescopic columns per foot up to the raised sole"""
    v = STILT[0]
    if v <= 0: return
    part("World")
    for s, L in ((1, "Left"), (-1, "Right")):
        A, T = bone_rest(L + "Foot"); fx = (A.x + T.x) / 2; toe_y = T.y - 0.03; heel_y = A.y + 0.10; g0 = -PLATE
        cbox("frame", (fx - 0.09, toe_y - 0.33, g0), (fx + 0.09, heel_y + 0.27, g0 + 0.04), 0.3)
        cbox("dark", (fx - 0.085, toe_y - 0.32, g0 - 0.004), (fx + 0.085, heel_y + 0.26, g0 + 0.004), 0.3)
        for cy in (toe_y + 0.06, heel_y - 0.06):
            cyl("frame", (fx, cy, g0 + 0.04), (fx, cy, g0 + 0.04 + v * 0.55), 0.042, 16)
            cyl("steel", (fx, cy, g0 + 0.04 + v * 0.50), (fx, cy, g0 + v - 0.002), 0.032, 16)
            cyl("orange", (fx, cy, g0 + 0.04 + v * 0.55), (fx, cy, g0 + 0.055 + v * 0.55), 0.046, 16)

def pose_her(hip=None, pelvis=0.0, lean=0.0, palms=None, back=0.0):
    """hip: the pelvis centre (world) or None (stands); pelvis: the pelvis tilted forward (rad); lean: the trunk
    forward (rad); palms: {+1: P, -1: P} the palm centres (world) or None (arms as at rest); back: lean back (rad)"""
    reset_pose()
    ARM.location = Vector((0, 0, STILT[0])); bpy.context.view_layer.update()
    if hip is not None or pelvis:
        Mw = world("Hips"); h0 = Mw.translation.copy(); tgt = Vector(hip) if hip is not None else h0
        set_world("Hips", Matrix.Translation(tgt) @ Matrix.Rotation(pelvis, 4, 'X') @ Matrix.Translation(-h0) @ Mw)
        for s, L in ((1, "Left"), (-1, "Right")):
            H = head(L + "UpLeg"); A = bone_rest(L + "Foot")[0] + Vector((0, 0, STILT[0]))
            K = ik2(H, A, blen(L + "UpLeg"), blen(L + "Leg"), Vector((0.15 * s, -1, 0)))
            aim(L + "UpLeg", K - H); aim(L + "Leg", A - head(L + "Leg"))
            aim(L + "Foot", rdir(L + "Foot")); aim(L + "ToeBase", rdir(L + "ToeBase"))
    if lean or back:
        a = lean - back
        for n, k in (("LowerBack", 0.8), ("Spine", 1.0), ("Spine1", 1.0), ("Neck", 0.5), ("Neck1", 0.3), ("Head", 0.2)):
            r = rdir(n); d = Matrix.Rotation(a * k, 3, 'X') @ r
            if n in ("Neck", "Neck1", "Head"): d = Matrix.Rotation(a * 0.55, 3, 'X') @ r
            aim(n, d)
    if palms:
        for s, L in ((1, "Left"), (-1, "Right")):
            P = Vector(palms[s]); S = head(L + "Arm"); l1, l2 = blen(L + "Arm"), blen(L + "ForeArm")
            f = (P - S).normalized()
            for _ in range(6):
                Wt = P - f * (REACH + TELE[0]); E = ik2(S, Wt, l1, l2, Vector((0.9 * s, 0.5, -0.2)))
                f = (Wt - E).normalized()
            aim(L + "Arm", E - S)
            aim(L + "ForeArm", Wt - head(L + "ForeArm"), PALM_N[s], Vector((-s, 0, 0)))
            aim(L + "Hand", f)
    bpy.context.view_layer.update()

# ================= the frame (built in her rest pose, a part per bone) =================
TELE = [0.0]                                    # the forearm's telescopic stage out, m (pushing a crate home)
STILT = [0.0]                                   # the stilt columns under the soles out, m (lifting to a deck)
EXT = [False]                                   # the foot's toe and heel extensions out (heavy loads)
REACH = 0.15                                    # the wrist -> the palm's centre (the coupler and the effector)
PALM_N = {}                                     # the palm's normal in the rest pose (the twist reference)
def frame(effector="palm"):
    X = Vector((1, 0, 0)); Z = Vector((0, 0, 1)); Y = Vector((0, 1, 0))
    for s, L in ((1, "Left"), (-1, "Right")):
        H, K = bone_rest(L + "UpLeg"); K, A = bone_rest(L + "Leg"); A, T = bone_rest(L + "Foot")
        td = (K - H).normalized(); sd = (A - K).normalized(); out = X * s
        # ---- the thigh: an outer spar and a shell over the outside and the front, a cuff ----
        part(L + "UpLeg")
        obox("frame", H + out * 0.125, K + out * 0.11 + td * 0.02, 0.065, 0.16, out, 0.055, 0.13, ch=0.32)
        a0, a1 = (-0.5, 1.9) if s > 0 else (-1.9, 0.5)                    # the outside and the front
        arc_plate("shell", (H + K) / 2 + td * 0.02, td, out, 0.112, a0, a1, (H - K).length * 0.40, 0.05, 5)
        arc_plate("shell2", (H + K) / 2 + td * 0.05, td, out, 0.164, a0 + (0.3 if s > 0 else 0.9), a1 - (0.9 if s > 0 else 0.3), (H - K).length * 0.30, 0.022, 3)
        ring("frame", H + (K - H) * 0.55, td, 0.082)
        octcyl("frame", K + out * 0.125, X, 0.09, 0.04); octcyl("joint", K + out * 0.17, X, 0.06, 0.008); octcyl("cap", K + out * 0.18, X, 0.035, 0.004)
        # ---- the shin: the outer spar, the shell, the knee guard, the cuff, the ankle housing ----
        part(L + "Leg")
        obox("frame", K + out * 0.11 + sd * 0.04, A + out * 0.105 - sd * 0.03, 0.06, 0.14, out, 0.05, 0.11, ch=0.32)
        arc_plate("shell", (K + A) / 2 - sd * 0.02, sd, out, 0.092, a0, a1, (K - A).length * 0.36, 0.045, 5)
        arc_plate("shell2", (K + A) / 2 - sd * 0.04, sd, out, 0.138, a0 + (0.3 if s > 0 else 0.9), a1 - (0.9 if s > 0 else 0.3), (K - A).length * 0.26, 0.02, 3)
        arc_plate("cap", K + sd * 0.02, sd, -Y, 0.082, -0.8, 0.8, 0.075, 0.04, 4)                 # the knee cap
        ring("frame", K + (A - K) * 0.55, sd, 0.062)
        octcyl("frame", A + out * 0.105, X, 0.064, 0.035); octcyl("joint", A + out * 0.142, X, 0.04, 0.006)
        # ---- the foot: the sole under her shoe, the toe on its hinge, side lips, the heel's MR damper, the strap ----
        part(L + "Foot")
        fx = (A.x + T.x) / 2; heel_y = A.y + 0.10; toe_y = T.y - 0.03
        cbox("frame", (fx - 0.085, toe_y, -PLATE), (fx + 0.085, heel_y, -0.004), 0.35)
        cbox("dark", (fx - 0.08, toe_y + 0.01, -PLATE - 0.006), (fx + 0.08, heel_y - 0.01, -PLATE + 0.004), 0.35)
        cyl("steel", (fx - 0.075, toe_y - 0.004, -PLATE * 0.5), (fx + 0.075, toe_y - 0.004, -PLATE * 0.5), 0.012, 12)
        cbox("frame", (fx - 0.075, toe_y - 0.075, -PLATE), (fx + 0.075, toe_y - 0.012, -0.010), 0.4)
        for sx in (-1, 1):
            obox("joint", (fx + sx * 0.088, toe_y + 0.03, -0.012), (fx + sx * 0.088, heel_y - 0.03, -0.012), 0.012, 0.03, Z, ch=0.3)
            obox("frame", (fx + sx * 0.096, toe_y + 0.03, -0.014), (fx + sx * 0.096, heel_y - 0.03, -0.014), 0.04, 0.012, (sx, 0, -0.4), ch=0.3)
        obox("frame", A + out * 0.10 - Z * 0.02, Vector((A.x + s * 0.10, A.y + 0.02, -0.004)), 0.05, 0.07, out, ch=0.3)   # the ankle strut down to the sole
        cyl("gel", (A.x + s * 0.04, heel_y - 0.015, 0.0), (A.x + s * 0.06, heel_y - 0.02, A.z - 0.01), 0.018, 16)
        if EXT[0]:                                                                 # the toe and heel extensions, 0.25 m each
            for sx in (-0.045, 0.045):
                cbox("steel", (fx + sx - 0.025, toe_y - 0.075 - 0.25, -PLATE), (fx + sx + 0.025, toe_y - 0.06, -PLATE + 0.018), 0.3)
                cbox("steel", (fx + sx - 0.025, heel_y - 0.02, -PLATE), (fx + sx + 0.025, heel_y + 0.25, -PLATE + 0.018), 0.3)
            for wy in (toe_y - 0.075 - 0.24, heel_y + 0.22):                  # the wheels: hub motors, MR brakes
                cbox("shell2", (fx - 0.10, wy - 0.03, -PLATE + 0.02), (fx + 0.10, wy + 0.03, -PLATE + 0.06), 0.3)
                for sx in (-1, 1):
                    cyl("dark", (fx + sx * 0.075, wy, -PLATE + 0.045), (fx + sx * 0.115, wy, -PLATE + 0.045), 0.045, 20)
                    cyl("joint", (fx + sx * 0.07, wy, -PLATE + 0.045), (fx + sx * 0.12, wy, -PLATE + 0.045), 0.02, 12)
        obox("frame", Vector((fx - 0.075, A.y - 0.05, 0.05)), Vector((fx + 0.075, A.y - 0.05, 0.05)), 0.03, 0.012, Z, ch=0.3)
        # the foot guard (the user: «ступни закрыть — травмоопасно, просто щитком»): a faceted hood over the instep and
        # the toes on the sole's edges, the toe cap in safety orange
        t_mid = toe_y + 0.13
        arc_plate("shell", Vector((fx, (t_mid + A.y - 0.01) / 2, 0.0)), Y, Z, 0.118, -1.45, 1.45, (A.y - 0.01 - t_mid) / 2, 0.016, 5)
        arc_plate("shell", Vector((fx, (toe_y + t_mid) / 2, 0.0)), Y, Z, 0.092, -1.45, 1.45, (t_mid - toe_y) / 2, 0.018, 5)
        arc_plate("shell2", Vector((fx, toe_y + 0.035, 0.0)), Y, Z, 0.112, -1.3, 1.3, 0.035, 0.012, 5)
        obox("shell2", Vector((fx, toe_y + 0.004, -0.004)), Vector((fx, toe_y + 0.004, 0.09)), 0.17, 0.016, X, ch=0.4)
        # ---- the arm: the upper arm's spar and shell, the elbow housing, the forearm's casing, the coupler, the effector ----
        S, E = bone_rest(L + "Arm"); E, W = bone_rest(L + "ForeArm")
        ud = (E - S).normalized(); fd = (W - E).normalized(); eax = ud.cross(fd).normalized()
        if eax.dot(out) < 0: eax = -eax
        part(L + "Arm")
        obox("frame", S + ud * 0.06 + out * 0.075, E - ud * 0.02 + out * 0.07, 0.04, 0.05, out, ch=0.3)   # the outer spar
        arc_plate("shell", (S + E) / 2 + ud * 0.01, ud, out, 0.078, -1.4, 1.4, (E - S).length * 0.38, 0.045, 5)
        arc_plate("shell2", (S + E) / 2 + ud * 0.02, ud, out, 0.125, -0.55, 0.55, (E - S).length * 0.26, 0.02, 2)
        ring("frame", S + (E - S) * 0.6, ud, 0.058)
        octcyl("frame", E + eax * 0.08, eax, 0.07, 0.035); octcyl("cap", E + eax * 0.118, eax, 0.035, 0.004)
        part(L + "ForeArm")
        PALM_N[s] = (-out - fd * (-out).dot(fd)).normalized()
        up = fd.cross(PALM_N[s]).normalized()
        Wx = W + fd * (0.02 + TELE[0])
        if TELE[0] > 0: obox("steel", W - fd * 0.05, Wx + fd * 0.001, 0.11, 0.10, PALM_N[s], ch=0.3)   # the stage out of the casing
        obox("shell", E + fd * 0.05, Wx, 0.15, 0.14, PALM_N[s], 0.18, 0.155, ch=0.36, mid=(0.6, 1.06))
        obox("shell2", E + fd * 0.10 - PALM_N[s] * 0.082, Wx - fd * 0.03 - PALM_N[s] * 0.09, 0.012, 0.11, PALM_N[s], 0.012, 0.12, ch=0.3)
        obox("frame", E + fd * 0.08 + up * 0.06, Wx + up * 0.07, 0.07, 0.02, PALM_N[s], ch=0.3)
        cyl("joint", Wx, Wx + fd * 0.035, 0.07, 24); cyl("orange", Wx + fd * 0.035, Wx + fd * 0.045, 0.072, 24)
        effector_at(effector, Wx + fd * 0.045, fd, PALM_N[s], up)
    # ---- the pelvis belt, the hip housings ----
    part("Hips"); hc = bone_rest("LHipJoint")[0]
    ring_pts = [Vector((0.215 * math.cos(a), 0.15 * math.sin(a) + 0.01, hc.z + 0.03)) for a in [math.pi * (-0.15 + 1.3 * k / 12) for k in range(13)]]
    for p, q in zip(ring_pts, ring_pts[1:]): obox("frame", p, q, 0.13, 0.05, Z, ch=0.35)
    for s in (1, -1):
        H = bone_rest(("Left" if s > 0 else "Right") + "UpLeg")[0]
        octcyl("frame", H + X * s * 0.135, X, 0.105, 0.045); octcyl("shell2", H + X * s * 0.185, X, 0.075, 0.008); octcyl("cap", H + X * s * 0.195, X, 0.04, 0.004)
        obox("frame", Vector((s * 0.20, 0.05, hc.z + 0.03)), H + X * s * 0.115, 0.07, 0.06, Z, ch=0.3)
    # (no buckle in front: the belt is open there - she steps in from the front; the user: «что это» at a floating plate)
    # ---- the spine column, the back unit with the cassette, the shoulder housings ----
    part("Spine1"); b0 = hc.z + 0.08
    cbox("shell", (-0.18, 0.11, b0 + 0.04), (0.18, 0.28, 1.47), 0.28)
    cbox("shell2", (-0.15, 0.275, b0 + 0.30), (0.15, 0.29, 1.40), 0.25)
    cbox("cap", (-0.17, 0.115, 1.455), (0.17, 0.275, 1.49), 0.3)
    cbox("dark", (-0.075, 0.288, 1.10), (0.075, 0.296, 1.30), 0.2)
    cbox("cap", (-0.065, 0.29, 1.14), (0.065, 0.302, 1.26), 0.2)
    for z in (1.33, 1.36, 1.39): obox("frame", (-0.14, 0.294, z), (0.14, 0.294, z), 0.006, 0.012, Z, ch=0.2)
    part("Spine"); obox("frame", (0, 0.10, b0), (0, 0.13, b0 + 0.12), 0.09, 0.06, X, ch=0.3)
    part("Spine1")
    for s in (1, -1):
        Sh = bone_rest(("Left" if s > 0 else "Right") + "Arm")[0]
        cbox("shell", (s * 0.12 - 0.085, -0.09, 1.35), (s * 0.12 + 0.085 + s * 0.045, 0.17, 1.48), 0.32)
        cbox("shell2", (s * 0.12 - 0.075, -0.08, 1.475), (s * 0.12 + 0.075 + s * 0.045, 0.16, 1.497), 0.3)
        octcyl("joint", Sh + X * s * 0.075 + Vector((0, 0.0, -0.01)), X, 0.07, 0.035)
        obox("frame", Vector((s * 0.12, 0.13, 1.40)), Vector((s * 0.15, 0.13, 1.20)), 0.06, 0.03, X, ch=0.3)

def effector_at(kind, c, f, n, up):
    """the exchangeable end effector on the quick coupler at c: f forward (along the forearm), n the palm's normal
    (towards the load), up across"""
    c, f, n, up = Vector(c), Vector(f), Vector(n), Vector(up)
    if kind == "palm":                         # the MR palm: a broad pad, MR gel on its face
        obox("frame", c, c + f * 0.05, 0.10, 0.10, n, 0.12, 0.11, ch=0.35)
        P = c + f * 0.13
        obox("frame", P - f * 0.09 - n * 0.01, P + f * 0.10 - n * 0.01, 0.045, 0.17, n, 0.04, 0.15, ch=0.3)
        obox("gel", P - f * 0.08 + n * 0.022, P + f * 0.09 + n * 0.022, 0.018, 0.16, n, 0.018, 0.14, ch=0.4)
        obox("orange", P - f * 0.08 - n * 0.036, P - f * 0.02 - n * 0.036, 0.006, 0.10, n, ch=0.3)
    elif kind == "three":                      # three fingers with MR pads: two against the thumb
        obox("frame", c, c + f * 0.07, 0.11, 0.12, n, 0.10, 0.11, ch=0.35)
        b = c + f * 0.07
        for k, (du, dn, ang) in enumerate(((0.035, -0.02, 0.35), (-0.035, -0.02, 0.35), (0.0, 0.05, -0.6))):
            base = b + up * du + n * dn; d1 = (f + n * (0.35 if dn < 0 else -0.35)).normalized()
            p1 = base + d1 * 0.07; d2 = (f + n * (0.9 if dn < 0 else -0.9)).normalized(); p2 = p1 + d2 * 0.055
            octcyl("joint", base, up, 0.018, 0.016); obox("shell", base, p1, 0.03, 0.03, up, 0.028, 0.028, ch=0.35)
            octcyl("joint", p1, up, 0.015, 0.014); obox("shell", p1, p2, 0.026, 0.026, up, 0.02, 0.022, ch=0.35)
            obox("gel", p1 + d2 * 0.005 - (n if dn < 0 else -n) * -0.012, p2 - (n if dn < 0 else -n) * -0.012, 0.008, 0.02, up, ch=0.4)
    elif kind == "five":                       # the classic: five fingers, her own scaled 1.3x
        obox("frame", c, c + f * 0.10, 0.09, 0.115, n, 0.035, 0.12, ch=0.3)
        for k in range(4):
            du = (k - 1.5) * 0.028; base = c + f * 0.10 + up * du
            p = base; d = f
            for j, (ln, w) in enumerate(((0.05, 0.022), (0.035, 0.019), (0.026, 0.016))):
                d = (d + n * 0.35).normalized(); q = p + d * ln
                obox("shell" if j < 2 else "frame", p, q, w, w, up, w * 0.9, w * 0.9, ch=0.35); p = q
        tb = c + f * 0.04 - up * 0.06 + n * 0.03; d = (f * 0.6 + n * 0.6 - up * 0.3).normalized()
        obox("shell", tb, tb + d * 0.05, 0.026, 0.026, n, ch=0.35); obox("frame", tb + d * 0.05, tb + d * 0.09, 0.022, 0.022, n, ch=0.35)
    elif kind == "forks":                      # tines: pallets, MPU cells on their skids
        obox("frame", c, c + f * 0.06, 0.12, 0.12, n, ch=0.35)
        for du in (-0.04, 0.04):
            p0 = c + f * 0.06 + up * du; p1 = p0 - Vector((0, 0, 0.0)) + f * 0.28
            obox("steel", p0, p1, 0.022, 0.05, up, 0.012, 0.04, ch=0.3)
    elif kind == "clamp":                      # the C-clamp: cylinders - cells, bottles, pipes
        obox("frame", c, c + f * 0.06, 0.12, 0.12, n, ch=0.35)
        ctr = c + f * 0.17
        for sgn in (1, -1):
            pts = [ctr + (f * math.cos(a) + n * sgn * math.sin(a)) * 0.10 for a in [math.pi * (0.95 - 0.7 * k / 6) for k in range(7)]]
            for p, q in zip(pts, pts[1:]): obox("shell", p, q, 0.05, 0.03, up, ch=0.3)
            for p, q in zip(pts[1:-1], pts[2:]): obox("gel", p - (p - ctr).normalized() * 0.018, q - (q - ctr).normalized() * 0.018, 0.04, 0.008, up, ch=0.4)
    elif kind == "hook":                       # the sling hook, retracting into the casing
        obox("frame", c, c + f * 0.05, 0.10, 0.10, n, ch=0.35)
        p0 = c + f * 0.05; pts = [p0 + f * 0.10]
        for a in [math.pi * k / 6 for k in range(7)]: pts.append(p0 + f * 0.13 + (f * math.sin(a) + n * (1 - math.cos(a))) * 0.045)
        cyl("steel", p0, pts[0], 0.016, 12)
        for p, q in zip(pts, pts[1:]): cyl("steel", p, q, 0.014, 12)

def flush(posed=True):
    for (pn, k), bm in list(parts.items()):
        if posed and ARM and pn in ARM.pose.bones: bm.transform(delta(pn))
        me = bpy.data.meshes.new("Am_%s_%s" % (pn, k)); bm.to_mesh(me); bm.free(); me.materials.append(M[k])
        for p in me.polygons: p.use_smooth = k in ("steel", "gel")
        o = bpy.data.objects.new("Am_%s_%s" % (pn, k), me); bpy.context.scene.collection.objects.link(o)
    parts.clear()
def clear():
    for o in [o for o in bpy.data.objects if o.name.startswith("Am_")]: bpy.data.objects.remove(o, do_unlink=True)

def crate(c, size):
    part("World"); c = Vector(c); hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    cbox("crate", (c.x - hx, c.y - hy, c.z - hz), (c.x + hx, c.y + hy, c.z + hz), 0.08)
    for t in (-0.55, 0.55): cbox("dark", (c.x - hx - 0.004, c.y - hy - 0.004, c.z + t * hz - 0.015), (c.x + hx + 0.004, c.y + hy + 0.004, c.z + t * hz + 0.015), 0.1)
def ground():
    part("World"); cbox("ground", (-4, -4, -PLATE - 0.02), (4, 4, -PLATE), 0.01)

def render(cam, name, loc, tgt, lens=50):
    sc = bpy.context.scene; cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y'); bpy.context.view_layer.update()
    sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); log.append("rendered " + name)

def scene_poses(cam):
    # 1. standing, arms as at rest
    reset_pose(); frame("palm"); ground(); flush(); render(cam, "amp_stand", (2.6, -3.4, 1.35), (0, 0, 0.88), 42); clear()
    # 2. the squat lift: the crate at her shins, the palms on its sides
    size = (0.90, 0.90, 0.60); box = Vector((0, -0.66, size[2] / 2))                      # the standard crate «С»
    palms = {1: Vector((0.45 + 0.04, -0.36, 0.40)), -1: Vector((-0.45 - 0.04, -0.36, 0.40))}
    pose_her(hip=(0, 0.10, 0.50), pelvis=0.35, lean=0.70, palms=palms)
    frame("palm"); crate(box + Vector((0, 0, -PLATE)), size); ground(); flush()
    render(cam, "amp_squat", (2.9, -2.6, 1.15), (0, -0.15, 0.55), 40); clear()
    # 3. carrying at the waist
    box = Vector((0, -0.62, 1.04))
    palms = {1: Vector((0.45 + 0.04, -0.36, 1.04)), -1: Vector((-0.45 - 0.04, -0.36, 1.04))}
    pose_her(palms=palms, back=0.08)
    frame("palm"); crate(box, size); ground(); flush()
    render(cam, "amp_carry", (2.7, -3.1, 1.30), (0, -0.15, 0.85), 40); clear()

def scene_hands(cam):
    """the six effectors in a row, each on a forearm casing standing on a stand (no figure)"""
    for o in bpy.data.objects:
        if o.parent == ARM or o == ARM: o.hide_render = True
    kinds = ["palm", "three", "five", "forks", "clamp", "hook"]
    for i, k in enumerate(kinds):
        x = (i - 2.5) * 0.36; part("World")
        c0 = Vector((x, 0, 0.25)); f = Vector((0, 0, 1)); n = Vector((-1, 0, 0)); up = Vector((0, -1, 0))
        cbox("frame", (x - 0.07, -0.07, -PLATE), (x + 0.07, 0.07, 0.02), 0.3)
        obox("shell", c0 - f * 0.23, c0, 0.13, 0.12, n, 0.15, 0.13, ch=0.36)
        cyl("joint", c0, c0 + f * 0.035, 0.07, 24); cyl("orange", c0 + f * 0.035, c0 + f * 0.045, 0.072, 24)
        effector_at(k, c0 + f * 0.045, f, n, up)
    ground(); flush(posed=False)
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 800
    render(cam, "amp_hands", (0.35, -2.4, 0.95), (0, 0, 0.42), 40)

# ================= the cargo system (Tantra_Design/CARGO_STANDARD.md): crates on a 0.5 m grid, the containers =================
CLASS = {"M": ("cap", "AmBandG", (0.15, 0.55, 0.25)), "S": ("shell2b", "AmBandB", (0.12, 0.35, 0.70)), "B": ("frame", "AmBandO", (0.80, 0.42, 0.10))}
def band(name, col):
    if name not in M: M[name] = mat(name, col, 0.5)
    return name
def crate_std(kind, o, dims=None, on="World"):
    """a standard crate: M 0.5^3, S 1.0 x 1.0 x 0.5, B 1.0^3; o = the centre of its base on the ground"""
    part(on); o = Vector(o)
    sx, sy, sz = dims or {"M": (0.5, 0.5, 0.5), "S": (0.75, 0.75, 0.75), "B": (1.0, 1.0, 1.0)}[kind]
    if "shell2b" not in M: M["shell2b"] = mat("AmShell2b", (0.42, 0.43, 0.44), 0.45, 0.2)
    body = CLASS[kind][0]; bnd = band(CLASS[kind][1], CLASS[kind][2]); hx, hy = sx / 2, sy / 2; z0 = o.z
    cbox(body, (o.x - hx + 0.02, o.y - hy + 0.02, z0 + 0.03), (o.x + hx - 0.02, o.y + hy - 0.02, z0 + sz - 0.03), 0.06)
    for cx in (-1, 1):                                                             # the corner posts with stacking cones
        for cy in (-1, 1):
            c = Vector((o.x + cx * (hx - 0.03), o.y + cy * (hy - 0.03), z0))
            cbox("frame", (c.x - 0.03, c.y - 0.03, z0), (c.x + 0.03, c.y + 0.03, z0 + sz), 0.35)
            cyl("orange", c + Vector((0, 0, sz)), c + Vector((0, 0, sz + 0.015)), 0.015, 8)
    for z in (z0 + 0.03, z0 + sz - 0.03):                                        # the edge frames
        for (a, b) in (((-hx, -hy), (hx, -hy)), ((hx, -hy), (hx, hy)), ((hx, hy), (-hx, hy)), ((-hx, hy), (-hx, -hy))):
            obox("frame", (o.x + a[0], o.y + a[1], z), (o.x + b[0], o.y + b[1], z), 0.035, 0.035, (0, 0, 1), ch=0.3)
    part(on); zb = z0 + sz * 0.62                                                 # the class band and the MR-palm fields
    cbox(bnd, (o.x - hx + 0.015, o.y - hy + 0.015, zb - 0.025), (o.x + hx - 0.015, o.y + hy - 0.015, zb + 0.025), 0.1)
    for sgn in (-1, 1):
        cbox("dark", (o.x + sgn * (hx - 0.012) - 0.004, o.y - min(0.15, hy * 0.5), z0 + sz * 0.22), (o.x + sgn * (hx - 0.012) + 0.004, o.y + min(0.15, hy * 0.5), z0 + sz * 0.48), 0.1)
        cbox("frame", (o.x - 0.07, o.y + sgn * (hy - 0.012) - 0.006, z0 + sz * 0.80), (o.x + 0.07, o.y + sgn * (hy - 0.012) + 0.006, z0 + sz * 0.86), 0.2)   # a recessed handle
    if kind in ("S", "B"):                                                        # fork pockets
        for sgn in (-1, 1): cbox("dark", (o.x + sgn * 0.25 - 0.09, o.y - hy - 0.002, z0 + 0.04), (o.x + sgn * 0.25 + 0.09, o.y + hy + 0.002, z0 + 0.12), 0.1)
    return o + Vector((0, 0, sz))
def container(kind, o):
    """K: inside 6.0 x 2.0 x 2.0 (24 B), ISO-type corner castings; KG (for the lander «Грань»): inside 3.0 x 2.0 x 1.0"""
    part("World"); o = Vector(o)
    L, W, H = {"K": (6.20, 2.24, 2.26), "KG": (3.20, 2.10, 1.20)}[kind]; hx, hy = L / 2, W / 2
    if "cont" not in M: M["cont"] = mat("AmCont", (0.84, 0.83, 0.79), 0.5)
    cbox("cont", (o.x - hx + 0.06, o.y - hy + 0.04, o.z + 0.06), (o.x + hx - 0.06, o.y + hy - 0.04, o.z + H - 0.06), 0.02)
    n = int(L / 0.30)
    for i in range(1, n):                                                         # corrugation of the side walls
        x = o.x - hx + i * L / n
        for sgn in (-1, 1): cbox("cont", (x - 0.05, o.y + sgn * (hy - 0.02) - 0.02, o.z + 0.12), (x + 0.05, o.y + sgn * (hy - 0.02) + 0.02, o.z + H - 0.12), 0.3)
    for a, b, w, d in (((-hx, -hy, 0.06), (hx, -hy, 0.06), 0.12, 0.12), ((-hx, hy, 0.06), (hx, hy, 0.06), 0.12, 0.12),
                       ((-hx, -hy, H - 0.06), (hx, -hy, H - 0.06), 0.12, 0.12), ((-hx, hy, H - 0.06), (hx, hy, H - 0.06), 0.12, 0.12)):
        obox("frame", (o.x + a[0], o.y + a[1], o.z + a[2]), (o.x + b[0], o.y + b[1], o.z + b[2]), w, d, (0, 0, 1), ch=0.25)
    for cx in (-1, 1):
        for cy in (-1, 1):
            c = Vector((o.x + cx * (hx - 0.08), o.y + cy * (hy - 0.08), o.z))
            cbox("frame", (c.x - 0.08, c.y - 0.08, o.z), (c.x + 0.08, c.y + 0.08, o.z + H), 0.2)
            for z in (o.z, o.z + H - 0.12): cbox("orange", (c.x - 0.09, c.y - 0.09, z), (c.x + 0.09, c.y + 0.09, z + 0.12), 0.15)   # the castings
    for sgn in (-1, 1):                                                           # the door end, and the class band
        cbox("frame", (o.x + sgn * (hx - 0.03) - 0.02, o.y - hy + 0.1, o.z + 0.1), (o.x + sgn * (hx - 0.03) + 0.02, o.y + hy - 0.1, o.z + H - 0.1), 0.1)
    cbox(band("AmBandO", (0.80, 0.42, 0.10)), (o.x - hx + 0.15, o.y - hy - 0.03, o.z + H * 0.72), (o.x + hx - 0.15, o.y - hy + 0.01, o.z + H * 0.80), 0.1)

def scene_cargo(cam):
    reset_pose(); frame("palm")
    z = -PLATE
    crate_std("M", (1.0, -0.8, z)); crate_std("S", (2.1, -0.8, z)); crate_std("B", (3.5, -0.8, z))
    t = crate_std("B", (5.0, -0.8, z)); t = crate_std("S", t); crate_std("M", t + Vector((0.25, 0.25, 0)))    # a stack
    container("KG", (2.6, 2.6, z)); container("K", (7.2, 2.6, z))
    part("World"); cbox("ground", (-3, -4, -PLATE - 0.02), (12, 6, -PLATE), 0.01)
    flush()
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1800, 900
    for o in bpy.data.objects:
        if o.type == 'LIGHT': o.data.energy *= 2.2
    render(cam, "cargo_system", (3.8, -9.5, 4.2), (4.0, 1.2, 0.9), 30)
    render(cam, "cargo_crates", (3.4, -4.6, 2.2), (2.6, -0.6, 0.55), 38)

def scene_carry2(cam):
    """carrying in her arms, no forks (the user): С edge-on at the chest; Б hugged, the toe extensions out"""
    if "shell2b" not in M: M["shell2b"] = mat("AmShell2b", (0.42, 0.43, 0.44), 0.45, 0.2)
    reset_pose(); frame("palm")                                  # (the palms' rest normals, then throw the geometry away)
    for bm in parts.values(): bm.free()
    parts.clear()
    EXT[0] = False; by = -0.47; bz = 0.36                       # С: 1.0 wide, 0.5 deep, 1.0 tall
    palms = {1: Vector((0.50 + 0.04, by + 0.05, 1.04)), -1: Vector((-0.50 - 0.04, by + 0.05, 1.04))}
    pose_her(palms=palms, back=0.10)
    frame("palm"); crate_std("S", (0, by, bz), (1.0, 0.5, 1.0)); ground(); flush()
    render(cam, "amp_carry_S", (2.8, -3.0, 1.45), (0, -0.25, 0.9), 40); clear()
    EXT[0] = True; by = -0.72; bz = 0.30                        # Б 1.0^3 hugged against her, lifted 0.3 m
    palms = {1: Vector((0.50 + 0.04, by + 0.22, 1.02)), -1: Vector((-0.50 - 0.04, by + 0.22, 1.02))}
    pose_her(palms=palms, back=0.16)
    frame("palm"); crate_std("B", (0, by, bz)); ground(); flush()
    render(cam, "amp_carry_B", (3.0, -3.3, 1.5), (0, -0.4, 0.85), 38); clear()
    EXT[0] = False

def scene_deck(cam):
    """with Б: rolling on the toe/heel wheels; lifting it onto the MPU's deck (1.50 m) on the stilts"""
    if "shell2b" not in M: M["shell2b"] = mat("AmShell2b", (0.42, 0.43, 0.44), 0.45, 0.2)
    reset_pose(); frame("palm")
    for bm in parts.values(): bm.free()
    parts.clear()
    # 1. rolling with Б (wheels down, the feet do not leave the floor)
    EXT[0] = True; STILT[0] = 0.0; TELE[0] = 0.0; by = -0.72; bz = 0.30
    palms = {1: Vector((0.54, by + 0.22, 1.02)), -1: Vector((-0.54, by + 0.22, 1.02))}
    pose_her(palms=palms, back=0.16)
    frame("palm"); crate_std("B", (0, by, bz)); ground(); flush()
    render(cam, "amp_roll_B", (3.2, -2.6, 1.0), (0, -0.4, 0.6), 36); clear()
    # 2. at the MPU's deck: stilts out 0.5 m, Б's bottom above the deck top, its front edge already on the deck lip,
    #    the forearms' stages pushing it home
    EXT[0] = False; STILT[0] = 0.50; TELE[0] = 0.18
    deck = 1.50; face = -1.13; bz = deck + 0.02 - PLATE; by = -0.70
    palms = {1: Vector((0.54, by + 0.20, bz + 0.45)), -1: Vector((-0.54, by + 0.20, bz + 0.45))}
    pose_her(palms=palms, back=0.12)
    frame("palm"); stilts(); crate_std("B", (0, by, bz))
    part("World")
    if "deck" not in M: M["deck"] = mat("AmDeck", (0.30, 0.31, 0.33), 0.6, 0.3)
    cbox("deck", (-1.6, face - 3.0, -PLATE + 0.6), (1.6, face, deck - PLATE), 0.02)          # the MPU's deck (a block)
    cbox("frame", (-1.6, face - 0.06, deck - PLATE - 0.06), (1.6, face + 0.004, deck - PLATE + 0.004), 0.2)
    for x in (-1.1, 0.0, 1.1): cyl("dark", (x - 0.32, face - 0.8, 0.8 - PLATE), (x + 0.32, face - 0.8, 0.8 - PLATE), 0.8, 32)   # its wheels
    ground(); flush()
    render(cam, "amp_deck_B", (3.6, 0.2, 2.2), (0, -0.9, 1.3), 34)
    render(cam, "amp_deck_B2", (3.0, -2.2, 2.4), (0, -0.6, 1.4), 36); clear()
    EXT[0] = False; STILT[0] = 0.0; TELE[0] = 0.0; ARM.location = Vector((0, 0, 0))

def scene_back(cam):
    """on the ground, the porter's way: С edge-on on the back carrier (its tines in the crate's fork pockets, the
    corner cones locked), another С edge-on at the chest; the trunk leaned forward, the centre of mass over the feet"""
    if "shell2b" not in M: M["shell2b"] = mat("AmShell2b", (0.42, 0.43, 0.44), 0.45, 0.2)
    reset_pose(); frame("palm")
    for bm in parts.values(): bm.free()
    parts.clear()
    lean = 0.56
    palms = {1: Vector((0.54, -0.50, 1.00)), -1: Vector((-0.54, -0.50, 1.00))}
    pose_her(lean=lean, palms=palms)
    frame("palm")
    part("Spine1")                                                    # the back carrier: a shelf and two rails off the back unit
    cbox("frame", (-0.42, 0.29, 0.86), (0.42, 0.86, 0.90), 0.3)
    for x in (-0.36, 0.36):
        obox("frame", (x, 0.30, 0.88), (x, 0.30, 1.62), 0.05, 0.04, (1, 0, 0), ch=0.3)
        obox("shell2", (x, 0.31, 1.62), (x, 0.31, 1.70), 0.06, 0.05, (1, 0, 0), ch=0.3)
    crate_std("S", (0, 0.56, 0.90), (1.0, 0.5, 1.0), on="Spine1")
    crate_std("S", (0, -0.60, 0.35), (1.0, 0.5, 1.0))
    ground(); flush()
    render(cam, "amp_back_S", (3.4, -1.2, 1.5), (0, 0, 0.95), 34)
    render(cam, "amp_back_S2", (2.6, 2.8, 1.8), (0, 0.1, 1.0), 34); clear()

def to_orb(v): return Vector((-v.x, v.z, -v.y))
def export_msh(path, prefix):
    """the parts built -> an Orbiter mesh, a group per (part, material), materials from the Blender colours"""
    cols = {}
    for k, m in M.items():
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'); cols[k] = tuple(b.inputs["Base Color"].default_value)[:3]
    used = sorted({k for (_, k) in parts}); groups = []
    for (pn, k), bm in parts.items():
        bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.normal_update()
        vs, fs = [], []
        for f in bm.faces:
            n = to_orb(f.normal); base = len(vs)
            for v in f.verts: vs.append((to_orb(v.co), n))
            fs.append((base, base + 2, base + 1))
        groups.append(("%s_%s" % (pn, k), used.index(k), vs, fs))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(groups))
        for lab, mi, vs, fs in groups:
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE 0\nGEOM %d %d\n" % (lab, mi + 1, len(vs), len(fs)))
            for q, n in vs: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (q.x, q.y, q.z, n.x, n.y, n.z))
            for a, b2, c in fs: f.write("%d %d %d\n" % (a, b2, c))
        f.write("MATERIALS %d\n" % len(used))
        for k in used: f.write("%s_%s\n" % (prefix, k))
        for k in used:
            c = cols[k]; sp = 0.8 if k in ("steel",) else 0.25
            f.write("MATERIAL %s_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 %d\n0 0 0 1\n" % (prefix, k, *c, *c, sp, sp, sp, 60 if sp > 0.5 else 15))
    log.append("mesh %s: %d groups, %d verts" % (path, len(groups), sum(len(g[2]) for g in groups)))
    for bm in parts.values(): bm.free()
    parts.clear()

def scene_framemesh(cam):
    """the frame as the worn mesh for OrbiterCrew: built in her rest pose, a group per (bone, material), in her model
    frame (export_skin.py: Orbiter = (-x, z - 0.93, -y) of Blender); Config/OrbiterCrew/Usilitel.geo maps the groups
    to the bones (the module carries each group with its bone, as the impeller pack's PoseFan)"""
    ORB = os.path.normpath(os.path.join(HERE, "..", ".."))
    reset_pose(); frame("palm")
    for k in [k for k in parts if k[0] == "World"]: parts.pop(k).free()
    cols = {}
    for k, m in M.items():
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'); cols[k] = tuple(b.inputs["Base Color"].default_value)[:3]
    C = lambda v: Vector((-v.x, v.z - 0.93, -v.y))
    used = sorted({k for (_, k) in parts}); groups = []
    for (pn, k), bm in sorted(parts.items()):
        bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.normal_update()
        vs, fs = [], []
        smooth = k in ("steel", "gel")
        for f in bm.faces:
            base = len(vs)
            for v in f.verts: vs.append((C(v.co), C(v.normal if smooth else f.normal) + Vector((0, 0.93, 0))))
            fs.append((base, base + 2, base + 1))
        groups.append((pn, k, vs, fs))
    path = os.path.join(ORB, "Meshes", "OrbiterCrew", "Usilitel.msh")
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(groups))
        for pn, k, vs, fs in groups:
            f.write("LABEL %s_%s\nMATERIAL %d\nTEXTURE 0\nGEOM %d %d\n" % (pn, k, used.index(k) + 1, len(vs), len(fs)))
            for q, n in vs: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (q.x, q.y, q.z, n.x, n.y, n.z))
            for a, b2, c in fs: f.write("%d %d %d\n" % (a, b2, c))
        f.write("MATERIALS %d\n" % len(used))
        for k in used: f.write("USL_%s\n" % k)
        for k in used:
            c = cols[k]; sp = 0.8 if k in ("steel", "joint") else 0.25
            f.write("MATERIAL USL_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 %d\n0 0 0 1\n" % (k, *c, *c, sp, sp, sp, 60 if sp > 0.5 else 15))
    geo = os.path.join(ORB, "Config", "OrbiterCrew", "Usilitel.geo")
    with open(geo, "w", newline="\n") as f:
        f.write("# The amplifier frame (Orbitersdk/OrbiterCrew/AMPLIFIER.md): mesh group -> the figure's bone that carries it.\n")
        f.write("# Written by Tantra_Design/blender/mock_amp.py (AMP_SHOT=framemesh); her rest model frame, as Usilitel.msh.\n")
        for i, (pn, k, vs, fs) in enumerate(groups): f.write("GROUP %d %s\n" % (i, pn))
    log.append("frame mesh %s: %d groups, %d verts" % (path, len(groups), sum(len(g[2]) for g in groups)))
    for bm in parts.values(): bm.free()
    parts.clear()

def scene_cratemesh(cam):
    """the standard crates as Orbiter meshes for OrbiterCrew items: centred on the box's centre (the item's origin)"""
    if "shell2b" not in M: M["shell2b"] = mat("AmShell2b", (0.42, 0.43, 0.44), 0.45, 0.2)
    ORB = os.path.normpath(os.path.join(HERE, "..", ".."))
    for kind, sz in (("M", 0.5), ("S", 0.75)):
        crate_std(kind, (0, 0, -sz / 2))
        export_msh(os.path.join(ORB, "Meshes", "OrbiterCrew", "Crate_%s.msh" % kind), "CRT")

def main():
    global ARM
    import sys; sys.path.insert(0, HERE); import render_util
    mats(); cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.hide_render = True
    ARM = bpy.data.objects.get("Astronavigator"); ARM.data.pose_position = 'POSE'
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1100, 1250
    if SHOT == "hands": scene_hands(cam)
    elif SHOT == "cargo": scene_cargo(cam)
    elif SHOT == "carry2": scene_carry2(cam)
    elif SHOT == "deck": scene_deck(cam)
    elif SHOT == "back": scene_back(cam)
    elif SHOT == "cratemesh": scene_cratemesh(cam)
    elif SHOT == "framemesh": scene_framemesh(cam)
    else: scene_poses(cam)

try:
    main()
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
