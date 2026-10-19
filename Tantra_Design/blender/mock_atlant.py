# «Атлант» - the cargo exoskeleton (Orbitersdk/OrbiterCrew/EXO_CARGO.md, variant E) as a jointed model: every joint an
# angle, the legs by inverse kinematics, the arms, the crane and the counterweight by their own parameters - so the
# poses and the walk are computed, not placed by hand. Looks only, nothing for the game.
# blender --background astronavigator_suit.blend --python mock_atlant.py
#   ATL_SHOT = mesh -> Meshes\OrbiterCrew\Atlant.msh + Config\OrbiterCrew\Atlant.geo (the rest pose, a group per part)
#              lowgrip -> renderstlant_lowgrip.png (the grip from below)
#   ATL_SHOT = poses (default) -> renders\atlant_stand / _pull / _tow / _lift .png
#              walk            -> renders\atlant_walk\f00..f15.png (one gait cycle, empty, 6 km/h; a GIF from them outside)
# Blender: x across (+x her left), y along (front -y), z up. Her legs are inside the leg shells: the machine's thigh and
# shin are hers (the same lengths, the knees on her knees), her boots on stirrups at the machine's raised heel; below,
# the reverse-knee metatarsus and the articulated foot.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
SHOT = os.environ.get("ATL_SHOT", "poses"); LOG = os.path.join(HERE, "mock_atlant.log"); log = []

# ---- her legs (the figure's bones) and the machine's ----
H_HIP = Vector((0.099, -0.016, 0.92))            # her hip joint (the figure, rest, +x side)
L1, L2 = 0.414, 0.440                            # thigh, shin: hers = the machine's
L3, HA = 0.30, 0.26                              # the metatarsus (the heel joint -> the ankle), the ankle over the sole
M_ANG = 0.55                                     # the metatarsus' lean (rad from vertical, the ankle ahead of the heel)

# ================= geometry helpers =================
def mat(name, col, rough=0.45, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    return m

M = {}
def mats():
    if M: return
    M["frame"] = mat("AtFrame", (0.075, 0.08, 0.09), 0.35, 0.25); M["armor"] = mat("AtArmor", (0.20, 0.21, 0.23), 0.4, 0.3)
    M["warn"] = mat("AtWarn", (0.88, 0.62, 0.06), 0.45); M["chrome"] = mat("AtChrome", (0.80, 0.82, 0.85), 0.12, 1.0)
    M["steel"] = mat("AtSteel", (0.42, 0.43, 0.45), 0.35, 0.9); M["dark"] = mat("AtDark", (0.025, 0.025, 0.03), 0.6)
    M["cell"] = mat("AtCell", (0.60, 0.63, 0.66), 0.4, 0.5); M["crate"] = mat("AtCrate", (0.55, 0.36, 0.18), 0.7)
    M["lamp"] = mat("AtLamp", (0.2, 0.9, 0.4), 0.3); M["rope"] = mat("AtRope", (0.85, 0.55, 0.10), 0.6)
    M["ground"] = mat("AtGround", (0.32, 0.30, 0.27), 0.9); M["line"] = mat("AtLine", (0.18, 0.17, 0.15), 0.9)

parts = {}
PART = "Pelvis"                                  # the moving part the geometry goes to (the game mesh's groups)
PIV = {}                                         # the joints' pivots and axes in the pose built last (for the module)
def part(name):
    global PART; PART = name
def bm_of(k):
    key = (PART, k)
    if key not in parts: parts[key] = bmesh.new()
    return parts[key]

def cyl(k, a, b, r, seg=16, cap=True):
    a, b = Vector(a), Vector(b); bm = bm_of(k); ax = (b - a)
    if ax.length < 1e-6: return
    ax.normalize(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    ra = [bm.verts.new(a + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r) for j in range(seg)]
    rb = [bm.verts.new(b + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r) for j in range(seg)]
    for j in range(seg): bm.faces.new((ra[j], ra[(j + 1) % seg], rb[(j + 1) % seg], rb[j]))
    if cap: bm.faces.new(ra[::-1]); bm.faces.new(rb)

def box(k, lo, hi):
    bm = bm_of(k); (x0, y0, z0), (x1, y1, z1) = lo, hi
    v = [bm.verts.new(p) for p in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
    for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)): bm.faces.new([v[i] for i in f])

def sphere(k, c, r):
    bmesh.ops.create_uvsphere(bm_of(k), u_segments=16, v_segments=10, radius=r, matrix=Matrix.Translation(Vector(c)))

def octa(c, e1, e2, w, d, ch):
    k = ch * min(w, d); hw, hd = w / 2, d / 2
    pts = [(-hw + k, -hd), (hw - k, -hd), (hw, -hd + k), (hw, hd - k), (hw - k, hd), (-hw + k, hd), (-hw, hd - k), (-hw, -hd + k)]
    return [c + e1 * x + e2 * y for x, y in pts]

def obox(k, a, b, w, d, up=(1, 0, 0), w1=None, d1=None, ch=0.28, mid=None):
    a, b = Vector(a), Vector(b); ax = (b - a)
    if ax.length < 1e-6: return
    ax.normalize(); e1 = Vector(up).normalized(); e1 = (e1 - ax * e1.dot(ax))
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

def arc_plate(k, c, axis, ref, r, a0, a1, half, thick=0.025, seg=12):
    """a curved armour plate: the arc a0..a1 (rad, from `ref` about `axis`) of radius r, `half` along the axis each way"""
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

def ram(a, b, r=0.022, rod=None):
    """a hydraulic cylinder: the barrel and its eye on the current part, the rod and its eye on `rod` (the part it
    pushes); the module stretches them between the two"""
    a, b = Vector(a), Vector(b); m = a + (b - a) * 0.55; keep = PART
    cyl("frame", a, m, r * 1.45); pin(a, b, r * 1.4)
    if rod: part(rod)
    cyl("chrome", a + (b - a) * 0.25, b, r * 0.7); pin(b, a, r * 1.3); part(keep)

def pin(c, other, r):
    """a clevis eye: a short pin across the ram's line (horizontal where it can be), not a ball"""
    c = Vector(c); ax = (Vector(other) - c).normalized(); p = ax.cross(Vector((0, 0, 1)))
    if p.length < 0.3: p = ax.cross(Vector((0, 1, 0)))
    p.normalize(); cyl("steel", c - p * r * 1.25, c + p * r * 1.25, r * 0.45, 12)
    obox("frame", c - ax * r * 0.5, c + ax * r * 0.9, r * 2.1, r * 1.2, p, r * 1.8, r * 1.0, ch=0.35)

def mr_ram(a, b, r, rod=None):
    """a magnetorheological ram: the fat barrel with the field coils' jacket and the cooling fins on the current part,
    the rod (from deep inside the barrel, so it never gaps out) and its eye on `rod`"""
    a, b = Vector(a), Vector(b); ax = (b - a).normalized(); m = a + (b - a) * 0.58; keep = PART
    cyl("frame", a, m, r * 1.5, 20); pin(a, b, r * 1.25)
    for t in (0.14, 0.24, 0.34): p0 = a + (b - a) * t; cyl("dark", p0, p0 + ax * 0.022, r * 1.78, 20)
    cyl("armor", a + (b - a) * 0.44, m, r * 1.64, 20)                                    # the coil jacket and the gland
    if rod: part(rod)
    cyl("chrome", a + (b - a) * 0.22, b, r * 0.78, 16); pin(b, a, r * 1.15); part(keep)

def joint(c, ax, r=0.055, w=0.05):
    c, ax = Vector(c), Vector(ax).normalized(); cyl("frame", c - ax * w, c + ax * w, r, 20); cyl("warn", c - ax * (w + 0.004), c - ax * (w - 0.006), r * 0.75, 20)

def rope(a, b, r=0.007):
    cyl("rope", a, b, r, 6)

# ================= kinematics =================
def leg_ik(Hp, A, m_ang=M_ANG):
    """the hip Hp, the ankle A (world) -> the knee, the heel joint and the angles: the metatarsus leans m_ang
    (the ankle ahead), the thigh and the shin solve the rest with the knee forward (hers)"""
    Hl = A + Vector((0, math.sin(m_ang), math.cos(m_ang))) * L3
    d = Hl - Hp; D = min(math.hypot(d.y, d.z), L1 + L2 - 1e-4)
    base = math.atan2(-d.y, -d.z)                                  # angle of hip->heel from straight down, + forward (-y)
    c1 = (L1 * L1 + D * D - L2 * L2) / (2 * L1 * D); a_in = math.acos(max(-1, min(1, c1)))
    th = base + a_in                                                # the thigh: forward of the line (the knee ahead)
    Kn = Hp + Vector((0, -math.sin(th), -math.cos(th))) * L1
    dd = Hl - Kn; sh = math.atan2(-dd.y, -dd.z)
    Hl = Kn + Vector((0, -math.sin(sh), -math.cos(sh))) * L2       # (exact when the leg is at full stretch)
    return Kn, Hl, th, sh

def arm_dirs(s, flex, abd, elbow):
    def rot(v, ang):                                                # flex about x (forward = -y), then abduct about y (out)
        v = Vector((v.x, v.y * math.cos(ang) - v.z * math.sin(ang), v.y * math.sin(ang) + v.z * math.cos(ang)))
        return v
    def ab(v):
        th = -s * abd; return Vector((v.x * math.cos(th) + v.z * math.sin(th), v.y, -v.x * math.sin(th) + v.z * math.cos(th)))
    up = ab(rot(Vector((0, 0, -1)), -flex)); fo = ab(rot(Vector((0, 0, -1)), -(flex + elbow)))
    return up.normalized(), fo.normalized()

# ================= the machine =================
def foot(s, A, pitch=0.0, toe=0.0, heel=0.0, L="L"):
    """the articulated foot at the ankle A: pitch (+ toes up) about the ankle's axle, the toes and the heel on their
    own hinges (+ bent up)"""
    x = A.x
    def P(v, piv, ang):                                             # rotate a point about the x axis through piv
        v, piv = Vector(v), Vector(piv); d = v - piv
        return piv + Vector((d.x, d.y * math.cos(ang) - d.z * math.sin(ang), d.y * math.sin(ang) + d.z * math.cos(ang)))
    F = lambda v: P(v, A, -pitch)                                    # the foot's frame: toes (-y) up for pitch > 0
    part("Foot" + L); PIV["ankle" + L] = A.copy()
    cyl("steel", A + Vector((-0.20, 0, 0)), A + Vector((0.20, 0, 0)), 0.05, 20)
    for dx in (-0.17, 0.17):
        obox("frame", A + Vector((dx, 0, 0.02)), F(Vector((x + dx, A.y - 0.04, A.z - 0.15))), 0.05, 0.20, (0, 1, 0), 0.05, 0.30, ch=0.35)
    g = A.z - HA                                                     # the sole's level in the foot's frame
    M0, M1 = F(Vector((x, A.y + 0.12, g + 0.10))), F(Vector((x, A.y - 0.32, g + 0.09)))
    obox("frame", M0, M1, 0.40, 0.13, (1, 0, 0), 0.44, 0.11, ch=0.4)
    for yy in (0.06, -0.08, -0.22):
        for dx in (-0.11, 0.11):
            c = F(Vector((x + dx, A.y + yy, g + 0.02))); cbox("dark", (c.x - 0.085, c.y - 0.055, c.z - 0.02), (c.x + 0.085, c.y + 0.055, c.z + 0.02), 0.3)
    Hh = F(Vector((x, A.y + 0.14, g + 0.10)))                        # the heel's hinge
    cyl("steel", Hh + Vector((-0.17, 0, 0)), Hh + Vector((0.17, 0, 0)), 0.035, 16)
    part("Heel" + L); PIV["heel" + L] = Hh.copy()
    Hf = lambda v: P(F(v), Hh, heel)                                # (+heel: its back end up; - digs the spur in)
    obox("frame", Hh, Hf(Vector((x, A.y + 0.46, g + 0.12))), 0.42, 0.15, (1, 0, 0), 0.36, 0.16, ch=0.4, mid=(0.5, 1.04))
    obox("frame", Hf(Vector((x, A.y + 0.44, g + 0.11))), Hf(Vector((x, A.y + 0.58, g + 0.02))), 0.24, 0.10, (1, 0, 0), 0.14, 0.05, ch=0.4)
    for dx in (-0.10, 0.10):
        c = Hf(Vector((x + dx, A.y + 0.32, g + 0.02))); cbox("dark", (c.x - 0.095, c.y - 0.13, c.z - 0.02), (c.x + 0.095, c.y + 0.13, c.z + 0.02), 0.35)
    Tp = F(Vector((x, A.y - 0.33, g + 0.09)))                        # the toes' pin
    part("Foot" + L); cyl("steel", Tp + Vector((-0.21, 0, 0)), Tp + Vector((0.21, 0, 0)), 0.035, 16); PIV["toe" + L] = Tp.copy()
    for dx, sp in ((-0.11, -0.10), (0.11, 0.10)):
        T = lambda v: P(F(v), Tp, -toe)
        P0 = T(Vector((x + dx, A.y - 0.34, g + 0.085))); P1 = T(Vector((x + dx + sp * 0.06, A.y - 0.51, g + 0.06)))
        P2 = P(T(Vector((x + dx + sp * 0.10, A.y - 0.64, g + 0.03))), P1, -toe * 0.6)
        part("Toe1" + L); obox("frame", P0, P1, 0.17, 0.11, (1, 0, 0), 0.15, 0.10, ch=0.4); cyl("steel", P1 + Vector((-0.08, 0, 0)), P1 + Vector((0.08, 0, 0)), 0.03, 14)
        PIV["toe2" + L] = P1.copy()
        part("Toe2" + L); obox("frame", P1, P2, 0.14, 0.09, (1, 0, 0), 0.11, 0.06, ch=0.4)
        obox("dark", P2, P2 + (P2 - P1).normalized() * 0.05 + Vector((0, 0, -0.04)), 0.11, 0.05, (1, 0, 0), 0.08, 0.03, ch=0.3)
        part("Foot" + L); ram(F(Vector((x + dx, A.y - 0.18, g + 0.17))), P1 + Vector((0, 0.05, 0.05)), 0.016, rod="Toe1" + L)

def machine(pose):
    """build the whole machine for a pose (dict); returns her pose (the figure's hip and leg angles)"""
    hz, hy = pose.get("hip_z", 1.37), pose.get("hip_y", 0.0)
    her = {}
    for s in (1, -1):
        L = "L" if s > 0 else "R"
        Hp = Vector((s * 0.19, hy - 0.016, hz))
        A = Vector((s * 0.30, hy + pose[L + "_ay"], pose[L + "_az"]))
        Kn, Hl, th, sh = leg_ik(Hp, A, pose.get(L + "_m", M_ANG))
        her[L] = (th, sh); PIV["hip" + L] = Hp.copy(); PIV["knee" + L] = Kn.copy(); PIV["heelj" + L] = Hl.copy()
        # (the user: the legs carry massive MR hydraulics) the hip's rotary MR pack, the shells round her legs with
        # the valve packs on them, the fat MR rams of the hip, the knee and the heel joint
        X = Vector((1, 0, 0))
        part("Pelvis")
        cyl("frame", Hp + X * s * 0.10, Hp + X * s * 0.30, 0.17, 32)                                         # the hip's MR pack
        cyl("dark", Hp + X * s * 0.30, Hp + X * s * 0.315, 0.125, 32); cyl("steel", Hp + X * s * 0.315, Hp + X * s * 0.33, 0.07, 24)
        part("Thigh" + L); td = (Kn - Hp).normalized(); tb = X.cross(td).normalized()                       # (tb: behind)
        obox("frame", Hp + td * 0.05, Kn, 0.34, 0.38, X, 0.28, 0.32, mid=(0.35, 1.07), ch=0.32)
        obox("armor", Hp + td * 0.12 - tb * 0.185, Kn - td * 0.10 - tb * 0.155, 0.30, 0.05, X, 0.24, 0.045, ch=0.4)   # the front plate
        obox("dark", Hp + td * 0.12 + X * s * 0.185, Kn - td * 0.14 + X * s * 0.16, 0.07, 0.25, X, 0.06, 0.21, ch=0.35)  # the valve pack
        cyl("frame", Kn + X * s * 0.02, Kn + X * s * 0.22, 0.145, 28)                                            # the knee's drum
        cyl("dark", Kn + X * s * 0.22, Kn + X * s * 0.235, 0.095, 24)
        part("Shin" + L); sd = (Hl - Kn).normalized(); sb = X.cross(sd).normalized()
        obox("frame", Kn, Hl + sd * 0.05, 0.31, 0.35, X, 0.25, 0.28, mid=(0.3, 1.06), ch=0.32)
        obox("armor", Kn - sb * 0.20 - sd * 0.08, Kn - sb * 0.205 + sd * 0.11, 0.30, 0.06, X, 0.26, 0.05, ch=0.4)   # the knee guard
        obox("armor", Kn - sb * 0.185 + sd * 0.14, Hl - sb * 0.15 - sd * 0.06, 0.26, 0.045, X, 0.20, 0.04, ch=0.4)  # the shin plate
        cyl("frame", Hl - X * s * 0.04, Hl + X * s * 0.20, 0.13, 28); cyl("dark", Hl + X * s * 0.20, Hl + X * s * 0.215, 0.085, 24)   # the heel joint's drum
        st = Hl + Vector((0, -0.10, -0.02)); cbox("steel", (st.x - 0.11, st.y - 0.14, st.z - 0.045), (st.x + 0.11, st.y + 0.12, st.z), 0.3)   # her stirrup
        mr_ram(Kn + sb * 0.25 + sd * 0.04, Hl + sb * 0.20 + Vector((0, 0, 0.17)), 0.062, rod="Meta" + L)        # the heel joint's ram (the jump's)
        part("Meta" + L)
        obox("frame", Hl + Vector((s * 0.05, 0, 0)), A + Vector((0, 0.03, 0.04)), 0.26, 0.28, (1, 0, 0), 0.27, 0.20, mid=(0.45, 0.95), ch=0.34)
        obox("armor", Hl + Vector((0, -0.13, -0.05)), A + Vector((0, -0.08, 0.10)), 0.22, 0.04, (1, 0, 0), 0.20, 0.04, ch=0.4)
        mr_ram(Hl + Vector((0, 0.19, -0.03)), A + Vector((0, 0.25, 0.07)), 0.045, rod="Foot" + L)              # the ankle's ram
        part("Pelvis"); mr_ram(Hp + Vector((0, -0.24, -0.05)), Kn - tb * 0.19 + Vector((0, 0, 0.17)), 0.058, rod="Thigh" + L)   # the hip's ram
        part("Thigh" + L); mr_ram(Hp + Vector((0, 0.23, -0.08)), Kn + tb * 0.20 + Vector((0, 0, 0.10)), 0.058, rod="Shin" + L)  # the knee's ram
        foot(s, A, pose.get(L + "_pitch", 0.0), pose.get(L + "_toe", 0.0), pose.get(L + "_heel", 0.0), L)
    # ---- the pelvis, the spine, the torso's protection ----
    part("Pelvis"); P0 = Vector((0, hy, hz))
    cbox("frame", (-0.38, hy + 0.10, hz - 0.06), (0.38, hy + 0.36, hz + 0.24), 0.3)
    box("warn", (-0.33, hy + 0.361, hz + 0.18), (0.33, hy + 0.363, hz + 0.22))
    cbox("frame", (-0.32, hy + 0.12, hz - 0.26), (0.32, hy + 0.34, hz - 0.06), 0.3)                        # the pump
    for x in (0.12, -0.12): cyl("frame", (x, hy + 0.30, hz + 0.17), (x, hy + 0.30, hz + 0.82), 0.04)
    cbox("frame", (-0.16, hy + 0.30, hz + 0.24), (0.16, hy + 0.44, hz + 0.74), 0.3)                         # the spine's spar
    yoke = [Vector((0.40 * math.cos(a), hy + 0.06 + 0.25 * math.sin(a), hz + 0.80)) for a in [math.pi * k / 12 for k in range(13)]]
    for p, q in zip(yoke, yoke[1:]): cyl("frame", p, q, 0.034, 10)
    # the chest hoop round her (behind her arms' swing, before the chest), the flank plates, the roll bar
    hoop = [Vector((0.36 * math.cos(a), hy + 0.02 + 0.30 * math.sin(a), hz + 0.42)) for a in [math.pi * (1 + k / 16) for k in range(17)]]
    hoop = [Vector((p.x, hy + 0.02 - abs(p.y - hy - 0.02), p.z)) for p in hoop]
    for p, q in zip(hoop, hoop[1:]): cyl("frame", p, q, 0.028, 10)
    for x in (0.36, -0.36): cyl("frame", (x, hy + 0.02, hz + 0.42), (x, hy + 0.30, hz + 0.42), 0.028)
    for s in (1, -1):
        arc_plate("armor", (s * 0.30, hy + 0.10, hz + 0.40), (0, 0, 1), (s, 0, 0), 0.10, -1.0, 1.0, 0.18, 0.03)   # the flank plates
    bar = [Vector((0.34 * math.cos(a), hy + 0.22, hz + 0.80 + 0.36 * math.sin(a))) for a in [math.pi * k / 14 for k in range(15)]]
    for p, q in zip(bar, bar[1:]): cyl("warn" if abs(p.x) < 0.1 else "frame", p, q, 0.03, 10)
    sphere("lamp", (0.0, hy + 0.22, hz + 1.18), 0.03)
    # (the user: the controls are in her hands) a grip under each of her hands - the machine's arms copy what her
    # hands do with them (the grip on a 3-axis gimbal, a trigger, a thumb pad, the tool switch), on an arm from the flank
    for s in (1, -1):
        Hd = Vector((s * 0.49, hy - 0.22, hz + 0.02))                                                          # her hand (rest)
        obox("frame", (s * 0.38, hy + 0.08, hz + 0.32), Hd + Vector((0, 0.10, 0.02)), 0.05, 0.05, ch=0.35)
        sphere("steel", Hd + Vector((0, 0.04, -0.06)), 0.035)                                                   # the gimbal
        obox("dark", Hd + Vector((0, 0.03, -0.07)), Hd + Vector((0, -0.01, 0.07)), 0.04, 0.05, (1, 0, 0), 0.035, 0.045, ch=0.4)   # the grip
        box("warn", (Hd.x - 0.012, Hd.y - 0.04, Hd.z + 0.02), (Hd.x + 0.012, Hd.y - 0.025, Hd.z + 0.04))      # the trigger
        sphere("lamp", Hd + Vector((0, 0.0, 0.075)), 0.012)                                                     # the thumb pad
    # ---- the counterweight: the cells on the slide, the ballast plates ----
    cw = pose.get("cw", 0.55); nb = int(round(pose.get("ballast", 0)))
    for x in (0.20, -0.20):
        obox("steel", (x, hy + 0.30, hz - 0.01), (x, hy + 1.22, hz - 0.01), 0.05, 0.06, (0, 0, 1), ch=0.3)
        obox("frame", (x, hy + 0.34, hz + 0.25), (x, hy + 1.10, hz + 0.03), 0.04, 0.05, (1, 0, 0), ch=0.3)
    Y0 = hy + cw; PIV["slide"] = Vector((0, Y0, hz + 0.04))
    part("Carriage"); cbox("frame", (-0.30, Y0 - 0.05, hz + 0.01), (0.30, Y0 + 0.32, hz + 0.07), 0.2)
    for x in (-0.20, 0.20):
        cbox("cell", (x - 0.19, Y0, hz + 0.07), (x + 0.19, Y0 + 0.27, hz + 0.44), 0.15); box("warn", (x - 0.17, Y0 + 0.27, hz + 0.37), (x + 0.17, Y0 + 0.272, hz + 0.41))
    part("Ballast")
    for k in range(nb): cbox("dark", (-0.32, Y0 - 0.03, hz - 0.01 - 0.11 * (k + 1)), (0.32, Y0 + 0.30, hz - 0.01 - 0.11 * k - 0.01), 0.15)
    part("Pelvis")
    # the tow hitch under the slide's root, at the back
    cbox("steel", (-0.06, hy + 0.36, hz - 0.30), (0.06, hy + 0.50, hz - 0.22), 0.3); cyl("steel", (0, hy + 0.50, hz - 0.26), (0, hy + 0.56, hz - 0.26), 0.035, 12)
    pose["_hitch"] = Vector((0, hy + 0.58, hz - 0.26))
    # ---- the arms: shoulder (flex, abduction), elbow, the telescopic stage, the wrist, the tool; the pauldrons ----
    for s in (1, -1):
        L = "L" if s > 0 else "R"
        f, a, e, t = pose.get(L + "_flex", 0.5), pose.get(L + "_abd", 0.25), pose.get(L + "_elbow", 0.6), pose.get(L + "_tele", 0.15)
        Sh = Vector((s * 0.44, hy + 0.06, hz + 0.71))
        ud, fd = arm_dirs(s, f, a, e)
        El = Sh + ud * 0.40; Wr = El + fd * 0.40; Ex = Wr + fd * t
        PIV["shoulder" + L] = Sh.copy(); PIV["elbow" + L] = El.copy(); PIV["wrist" + L] = Wr.copy(); PIV["fdir" + L] = fd.copy()
        X = Vector((1, 0, 0))
        part("Pelvis")                                                                                          # the shoulder's flex actuator
        cyl("armor", Sh - X * s * 0.13, Sh + X * s * 0.02, 0.135, 32); cyl("dark", Sh - X * s * 0.135, Sh - X * s * 0.13, 0.10, 32)
        part("UpperArm" + L); ub = X.cross(ud).normalized()                                                    # (ub: behind the arm)
        cyl("frame", Sh + X * s * 0.02, Sh + X * s * 0.11, 0.125, 32)                                            # the abduction actuator
        arc_plate("armor", Sh, (0, 1, 0), (0, 0, 1), 0.17, -0.3 * s - 0.2, 1.4 * s if s > 0 else -1.4, 0.15, 0.03, 16)   # the pauldron, two layers
        arc_plate("armor", Sh + Vector((0, 0.02, 0)), (0, 1, 0), (0, 0, 1), 0.205, -0.1 * s - 0.1, 1.2 * s if s > 0 else -1.2, 0.11, 0.025, 16)
        obox("frame", Sh + ud * 0.07, El - ud * 0.04, 0.16, 0.19, X, 0.14, 0.16, mid=(0.4, 1.06), ch=0.38)
        obox("armor", Sh + ud * 0.12 + ub * 0.09, El - ud * 0.10 + ub * 0.075, 0.12, 0.08, X, 0.10, 0.06, ch=0.4)   # the elbow's actuator, enclosed
        ae = ud.cross(fd); ae = ae.normalized() if ae.length > 1e-4 else X.copy()
        if ae.x * s < 0: ae = -ae
        part("Forearm" + L); tf = fd.cross(ae).normalized()                                                   # (tf: the forearm's top)
        cyl("armor", El - ae * 0.10, El + ae * 0.10, 0.105, 32); cyl("dark", El + ae * 0.10, El + ae * 0.106, 0.07, 28)   # the elbow, flush
        obox("frame", El + fd * 0.05, Wr, 0.19, 0.20, ae, 0.23, 0.24, mid=(0.55, 1.08), ch=0.36)
        obox("armor", El + fd * 0.10 + tf * 0.105, Wr + tf * 0.125 - fd * 0.02, 0.17, 0.035, ae, 0.21, 0.035, ch=0.4)   # the top plate
        part("Tele" + L); obox("steel", Wr - fd * 0.38, Ex + fd * 0.001, 0.13, 0.14, ae, ch=0.3)   # the stage: a sleeve inside the forearm
        part("Tool" + L); cyl("frame", Ex, Ex + fd * 0.06, 0.085, 24); cyl("dark", Ex + fd * 0.025, Ex + fd * 0.035, 0.09, 24)   # the wrist's rotator
        tool = pose.get(L + "_tool", "pad")
        T0 = Ex + fd * 0.06; obox("frame", T0, T0 + fd * 0.05, 0.22, 0.22, ae, 0.24, 0.24, ch=0.4)               # the tool's quick coupler
        T1 = T0 + fd * 0.05
        if tool == "claw":                                                                                      # three jointed fingers in armour
            obox("frame", T1, T1 + fd * 0.07, 0.24, 0.22, ae, 0.20, 0.18, ch=0.4)
            T1 = T1 + fd * 0.07
            for dx, dz in ((-0.065, 0.06), (0.065, 0.06), (0.0, -0.07)):
                o = ae * dx + tf * dz; f0 = T1 + o; f1 = f0 + fd * 0.17 + o * 0.4; f2 = f1 + fd * 0.10 - o * 0.9
                obox("armor", f0, f1, 0.06, 0.055, ae, 0.05, 0.045, ch=0.4); cyl("dark", f1 - ae * 0.028, f1 + ae * 0.028, 0.026, 12)
                obox("armor", f1, f2, 0.045, 0.045, ae, 0.03, 0.03, ch=0.4)
        elif tool == "forks":                                                                                   # tines under the load
            hd = Vector((fd.x, fd.y, 0)).normalized(); acr = Vector((-hd.y, hd.x, 0))
            for k2 in (-0.08, 0.08):
                b0 = T1 + acr * k2; b1 = b0 + Vector((0, 0, -0.14))
                obox("steel", b0, b1, 0.07, 0.04, (0, 0, 1), ch=0.3); obox("steel", b1, b1 + hd * 0.62, 0.07, 0.03, acr, 0.06, 0.012, ch=0.25)
            pose["_" + L + "_tine"] = T1 + Vector((0, 0, -0.14))
        elif tool == "hook":
            cyl("steel", T1, T1 + fd * 0.16, 0.03); cyl("steel", T1 + fd * 0.16, T1 + fd * 0.16 + Vector((0, 0, -0.09)), 0.025)
        else:                                                                                                   # the clamp pad, facing in
            n = Vector((-s, 0, 0))
            obox("steel", T1, T1 + n * 0.10, 0.10, 0.10, ch=0.35); obox("dark", T1 + n * 0.10, T1 + n * 0.13, 0.46, 0.40, (0, 1, 0), ch=0.25)
        pose["_" + L + "_tool"] = T1
    # ---- the crane: the slewing ring, the luffing boom on its ram, the telescopic stage, the winch, the rope ----
    slew, luff, tele = pose.get("slew", 0.0), pose.get("luff", 0.55), pose.get("tele", 0.3)
    T = Vector((0.0, hy + 0.40, hz + 0.84)); PIV["slew"] = T.copy()
    part("Pelvis"); cyl("steel", T - Vector((0, 0, 0.02)), T + Vector((0, 0, 0.02)), 0.15, 28)                  # the slewing ring
    part("Turret"); cyl("frame", T + Vector((0, 0, 0.02)), T + Vector((0, 0, 0.14)), 0.12, 24)
    fo = Vector((math.sin(slew), -math.cos(slew), 0)); side = Vector((math.cos(slew), math.sin(slew), 0))
    Pv = T + Vector((0, 0, 0.24)) + fo * -0.05                                                                 # the boom's foot pin
    bd = (fo * math.cos(luff) + Vector((0, 0, 1)) * math.sin(luff)).normalized()
    B1 = Pv + bd * 0.95; Tip = B1 + bd * (0.10 + tele); PIV["boom"] = Pv.copy(); PIV["bdir"] = bd.copy()
    for sx in (-0.07, 0.07): obox("frame", T + Vector((0, 0, 0.12)) + side * sx, Pv + side * sx, 0.03, 0.14, (0, 0, 1), ch=0.3)   # the turret's cheeks
    cyl("steel", Pv - side * 0.09, Pv + side * 0.09, 0.035, 14)
    part("Boom"); obox("frame", Pv, B1, 0.14, 0.16, side, 0.12, 0.13, ch=0.3)
    part("BoomTele"); obox("steel", B1 - bd * 0.80, Tip, 0.09, 0.10, side, 0.08, 0.09, ch=0.3)   # the stage slides out of the boom
    cyl("steel", Tip - side * 0.06, Tip + side * 0.06, 0.055, 18)                                              # the tip sheave
    rb = Pv + bd * 0.42; ra = T + Vector((0, 0, 0.06)) + fo * 0.14
    part("Turret"); ram(ra, rb + Vector((0, 0, -0.07)), 0.035, rod="Boom")                                    # the luffing ram
    W = T + fo * -0.18 + Vector((0, 0, 0.10))                                                                  # the winch drum behind the foot
    cyl("dark", W - side * 0.11, W + side * 0.11, 0.08, 20); cyl("rope", W - side * 0.09, W + side * 0.09, 0.083, 20, cap=False)
    obox("frame", W - side * 0.13 + Vector((0, 0, -0.10)), W - side * 0.13, 0.03, 0.18, ch=0.3); obox("frame", W + side * 0.13 + Vector((0, 0, -0.10)), W + side * 0.13, 0.03, 0.18, ch=0.3)
    part("Rope"); up = bd.cross(side).normalized()
    if up.z < 0: up = -up
    rope(W + Vector((0, 0, 0.08)), Pv + up * 0.09)                                                             # the rope: drum -> foot -> tip
    rope(Pv + up * 0.09, Tip + up * 0.06)
    tgt = pose.get("rope_to")
    if tgt is None: tgt = Tip + Vector((0, 0, -pose.get("hook_drop", 0.5)))
    rope(Tip + up * 0.0, tgt)
    part("Hook")
    if pose.get("hook", True):
        cyl("steel", tgt + Vector((0, 0, 0.0)), tgt + Vector((0, 0, -0.10)), 0.035, 12); cyl("steel", tgt + Vector((0, 0, -0.10)), tgt + Vector((0, 0.04, -0.15)), 0.015, 8)
    pose["_tip"] = Tip; part("Scene")
    return her

def ground(y0, y1, lines=True):
    box("ground", (-3.5, y0, -0.02), (3.5, y1, 0.0))
    if lines:
        y = math.floor(y0)
        while y < y1:
            box("line", (-3.5, y, 0.0), (3.5, y + 0.03, 0.002)); y += 0.5

def crate(c, size, k="crate", eye=True):
    c = Vector(c); hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    box(k, (c.x - hx, c.y - hy, c.z - hz), (c.x + hx, c.y + hy, c.z + hz))
    for t in (-0.6, 0.6): box("dark", (c.x - hx - 0.005, c.y - hy - 0.005, c.z + t * hz - 0.02), (c.x + hx + 0.005, c.y + hy + 0.005, c.z + t * hz + 0.02))
    if eye: cyl("steel", c + Vector((-0.06, 0, hz + 0.03)), c + Vector((0.06, 0, hz + 0.03)), 0.03, 12)
    return c + Vector((0, 0, hz + 0.05))

# ================= the scene =================
REST = {}
def place_her(her, hz, hy):
    arm = bpy.data.objects.get("Astronavigator")
    for o in bpy.data.objects:
        if o.parent is None and o.type in ("MESH", "ARMATURE", "EMPTY") and not o.name.startswith(("At_", "Floor", "Cam", "Key", "Fill", "Rim")):
            if o.name not in REST: REST[o.name] = o.location.copy()
            o.location = REST[o.name] + Vector((0, hy, hz - H_HIP.z))
    if not arm: return
    if arm.data.pose_position != 'POSE':                             # (the file keeps the figure at rest: no pose would show)
        arm.data.pose_position = 'POSE'
        for pb in arm.pose.bones: pb.rotation_mode = 'XYZ'; pb.rotation_euler = (0, 0, 0); pb.location = (0, 0, 0); pb.scale = (1, 1, 1)
    for side, L in (("Left", "L"), ("Right", "R")):
        th, sh = her[L]
        for nm, val in ((side + "UpLeg", -th), (side + "Leg", th - sh), (side + "Foot", sh * 0.6)):
            pb = arm.pose.bones.get(nm)
            if pb: pb.rotation_mode = 'XYZ'; pb.rotation_euler = (val, 0, 0)
    bpy.context.view_layer.update()

def clear_machine():
    for o in [o for o in bpy.data.objects if o.name.startswith("At_")]: bpy.data.objects.remove(o, do_unlink=True)
    for k in list(parts): parts[k].free(); del parts[k]

def flush():
    for (pn, k), bm in list(parts.items()):
        me = bpy.data.meshes.new("At_%s_%s" % (pn, k)); bm.to_mesh(me); bm.free(); me.materials.append(M[k])
        for p in me.polygons: p.use_smooth = k in ("chrome", "steel", "lamp", "rope")
        o = bpy.data.objects.new("At_%s_%s" % (pn, k), me); bpy.context.scene.collection.objects.link(o)
    parts.clear()

def stance(pose, ay_l=-0.12, ay_r=-0.12):
    pose.update(L_ay=ay_l, L_az=HA, R_ay=ay_r, R_az=HA); return pose

def render(cam, name, loc, tgt, lens=50):
    sc = bpy.context.scene; cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y'); bpy.context.view_layer.update()
    sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); log.append("rendered " + name)

def scene_poses(cam):
    # 1. stand: the arms holding a crate before her, the crane up, the slide in
    p = stance(dict(hip_z=1.36, cw=0.55, L_flex=0.75, R_flex=0.75, L_abd=0.35, R_abd=0.35, L_elbow=0.55, R_elbow=0.55, L_tele=0.10, R_tele=0.10,
                    slew=0.0, luff=0.95, tele=0.10, hook_drop=0.35))
    her = machine(p); crate((0, -0.95, 1.30), (1.00, 0.70, 0.62)); ground(-4, 3); flush(); place_her(her, 1.36, 0.0)
    render(cam, "atlant_stand", (3.9, -5.2, 2.3), (0, -0.3, 1.5), 38); clear_machine()
    # 2. pull out: crouched, heels' spurs dug in, the left foot back, the boom down forward, the rope to a crate stuck
    #    in the ground 3 m ahead (its front sunk)
    p = dict(hip_z=1.16, cw=1.05, ballast=2, L_ay=0.18, L_az=HA, R_ay=-0.30, R_az=HA, L_heel=-0.25, R_heel=-0.25, L_m=0.75, R_m=0.6,
             L_flex=0.3, R_flex=0.3, L_abd=0.25, R_abd=0.25, L_elbow=0.9, R_elbow=0.9, L_tele=0.0, R_tele=0.0,
             slew=0.0, luff=0.10, tele=0.6, hook=False)
    stuck = Vector((0, -3.6, 0.22)); p["rope_to"] = stuck + Vector((0, 0.45, 0.20))
    her = machine(p); b = bm_of("crate")
    crate(stuck, (1.0, 0.9, 0.7)); box("ground", (-0.8, -4.3, -0.02), (0.8, -3.0, 0.08))              # the sand round its foot
    ground(-6, 3); flush(); place_her(her, 1.16, 0.0)
    render(cam, "atlant_pull", (4.6, -3.4, 1.6), (0, -1.6, 0.8), 40); clear_machine()
    # 3. tow: mid-stride forward, the rope from the hitch behind to a crate on a sled 2.5 m back
    p = dict(hip_z=1.33, cw=0.55, L_ay=-0.45, L_az=HA, R_ay=0.30, R_az=HA + 0.03, R_pitch=0.25, R_toe=0.35,
             L_flex=0.35, R_flex=0.65, L_abd=0.25, R_abd=0.25, L_elbow=0.5, R_elbow=0.5, L_tele=0.0, R_tele=0.0, L_tool="hook", R_tool="claw",
             slew=0.0, luff=1.1, tele=0.0, hook_drop=0.25)
    her = machine(p); sled = Vector((0, 2.9, 0.40))
    box("steel", (-0.55, sled.y - 0.60, 0.0), (0.55, sled.y + 0.60, 0.05)); crate(sled + Vector((0, 0, 0.0)), (0.9, 1.0, 0.62), eye=False)
    rope(p["_hitch"], Vector((0, sled.y - 0.62, 0.05)))
    ground(-3, 5); flush(); place_her(her, 1.33, 0.0)
    render(cam, "atlant_tow", (5.8, 0.6, 2.2), (0, 0.8, 1.3), 34); clear_machine()
    # 4. lift: the boom up and out to her left, the crate lifted to 1.9 m, her arms guiding it, the slide far back
    p = stance(dict(hip_z=1.30, cw=1.05, ballast=1, L_flex=1.05, R_flex=0.9, L_abd=0.55, R_abd=0.2, L_elbow=0.25, R_elbow=0.35,
                    L_tele=0.35, R_tele=0.3, R_tool="claw", slew=1.25, luff=0.72, tele=0.55, hook=True), -0.05, -0.18)
    her = machine(p); tip = p["_tip"]
    top = crate(Vector((tip.x, tip.y, 1.90)), (0.9, 0.8, 0.6))
    p2 = dict(p); clear_machine(); p2["rope_to"] = top + Vector((0, 0, 0.12)); her = machine(p2); crate(Vector((tip.x, tip.y, 1.90)), (0.9, 0.8, 0.6))
    ground(-4, 3); flush(); place_her(her, 1.30, 0.0)
    render(cam, "atlant_lift", (4.8, -3.6, 2.7), (0.7, -0.2, 1.7), 36); clear_machine()

def gait(phase, stride=1.50, duty=0.62, lift=0.18):
    """the ankle's place along the walk (body frame, from its neutral point) and the foot's angles for one leg"""
    if phase < duty:                                                # stance: planted, the body passes over it
        u = phase / duty; y = -stride / 2 + stride * u; z = 0.0
        pitch = 0.0; toe = 0.0; heel = 0.0
        if u > 0.75: k = (u - 0.75) / 0.25; pitch = -0.35 * k; toe = 0.45 * k     # heel off, toes bend
        if u < 0.10: pitch = 0.12 * (1 - u / 0.10)                   # heel strike: toes still up
    else:                                                           # swing: forward, lifted
        u = (phase - duty) / (1 - duty); c = 0.5 - 0.5 * math.cos(math.pi * u)
        y = stride / 2 - stride * c; z = lift * math.sin(math.pi * u)
        pitch = -0.35 * (1 - u) * (1 - u) + 0.15 * u * u; toe = 0.45 * (1 - u) ** 2; heel = 0.0
    return y, z, pitch, toe, heel

def scene_walk(cam):
    v = 6.0 / 3.6; stride = 1.50; T = stride / v; N = 16
    os.makedirs(os.path.join(OUT, "atlant_walk"), exist_ok=True)
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 900, 760
    for i in range(N):
        ph = i / N; t = ph * T; by = -v * t
        p = dict(hip_y=by, cw=0.55, L_flex=0.45, R_flex=0.45, L_abd=0.25, R_abd=0.25, L_elbow=0.6, R_elbow=0.6, L_tele=0.0, R_tele=0.0,
                 slew=0.0, luff=1.15, tele=0.0, hook_drop=0.25, R_tool="claw")
        for L, off in (("L", 0.0), ("R", 0.5)):
            y, z, pitch, toe, heel = gait((ph + off) % 1.0, stride)
            p[L + "_ay"] = -0.12 + y; p[L + "_az"] = HA + z; p[L + "_pitch"] = pitch; p[L + "_toe"] = toe; p[L + "_heel"] = heel
            p[L + "_flex"] = 0.45 - 0.35 * (y / stride)                 # the arms swing against the legs (a little)
        hz = 1.33 + 0.02 * math.cos(4 * math.pi * ph)
        p["hip_z"] = hz
        her = machine(p); ground(-4.5, 3.0); flush(); place_her(her, hz, by)
        render(cam, "atlant_walk\\f%02d" % i, (5.6, by - 0.4, 1.5), (0, by - 0.4, 1.1), 40); clear_machine()


# ================= the game mesh =================
ORB = os.path.abspath(os.path.join(HERE, "..", ".."))
def to_orb(v): return Vector((-v.x, v.z, -v.y))                    # Blender (x her left, front -y, z up) -> Orbiter (x right, y up, z fwd)

def export_msh(path, geo):
    """the parts built last -> an Orbiter mesh: one group per (part, material), in the pose built (the rest pose); the
    joints' pivots into the .geo file for the module (Orbiter frame, the origin on the ground under the hips)"""
    cols = {}
    for k, m in M.items():
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'); cols[k] = tuple(b.inputs["Base Color"].default_value)[:3]
    names = sorted(cols)
    groups = []
    for (pn, k), bm in parts.items():
        if pn == "Scene": continue
        bmesh.ops.triangulate(bm, faces=bm.faces[:]); bm.normal_update()
        smooth = k in ("chrome", "steel", "lamp", "rope")
        vs, fs = [], []
        if smooth:
            idx = {}
            for v in bm.verts: idx[v.index] = len(vs); vs.append((to_orb(v.co), to_orb(v.normal)))
            for f in bm.faces: a, b2, c = (idx[v.index] for v in f.verts); fs.append((a, c, b2))
        else:
            for f in bm.faces:
                n = to_orb(f.normal); base = len(vs)
                for v in f.verts: vs.append((to_orb(v.co), n))
                fs.append((base, base + 2, base + 1))
        groups.append(("%s_%s" % (pn, k), names.index(k), vs, fs))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(groups))
        for lab, mi, vs, fs in groups:
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE 0\nGEOM %d %d\n" % (lab, mi + 1, len(vs), len(fs)))
            for q, n in vs: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (q.x, q.y, q.z, n.x, n.y, n.z))
            for a, b2, c in fs: f.write("%d %d %d\n" % (a, b2, c))
        f.write("MATERIALS %d\n" % len(names))
        for k in names: f.write("ATL_%s\n" % k)
        for k in names:
            c = cols[k]; sp = 0.8 if k in ("chrome", "steel") else 0.25; em = 1.0 if k == "lamp" else 0.0
            f.write("MATERIAL ATL_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 %d\n%.3f %.3f %.3f 1\n" % (k, *c, *c, sp, sp, sp, 60 if sp > 0.5 else 15, *(x * em for x in c)))
    os.makedirs(os.path.dirname(geo), exist_ok=True)
    with open(geo, "w", newline="\n") as f:
        f.write("# Atlant cargo exoskeleton - the joints in the rest pose (Orbiter frame: x right, y up, z forward; origin on the\n")
        f.write("# ground under the hips). Written by Tantra_Design/blender/mock_atlant.py (ATL_SHOT=mesh). Legs, arms (flex),\n")
        f.write("# feet, heels, toes, the boom: about the x axis; the slew: about y; the slide and the tele stages: along their dirs.\n")
        for k in sorted(PIV): v = to_orb(PIV[k]); f.write("PIVOT %s %.4f %.4f %.4f\n" % (k, v.x, v.y, v.z))
        for i, (lab, mi, vs, fs) in enumerate(groups): f.write("GROUP %d %s %d\n" % (i, lab, len(vs)))
    log.append("mesh %s: %d groups, %d verts, %d tris" % (path, len(groups), sum(len(g[2]) for g in groups), sum(len(g[3]) for g in groups)))

REST_POSE = dict(hip_z=1.37, cw=0.55, L_flex=0.35, R_flex=0.35, L_abd=0.20, R_abd=0.20, L_elbow=0.55, R_elbow=0.55, L_tele=0.0, R_tele=0.0,
                 L_tool="pad", R_tool="claw", slew=0.0, luff=1.35, tele=0.0, hook_drop=0.30)

def scene_mesh(cam):
    p = stance(dict(REST_POSE)); her = machine(p)
    export_msh(os.path.join(ORB, "Meshes", "OrbiterCrew", "Atlant.msh"), os.path.join(ORB, "Config", "OrbiterCrew", "Atlant.geo"))
    parts.clear()

def scene_lowgrip(cam):
    """the grip from below: deep crouch (the reverse knee lets the hips down), the arms forward and down, fork tines
    on both tool plates slid under the load on its pallet at the floor"""
    p = dict(hip_z=0.95, cw=1.0, ballast=1, L_ay=-0.02, L_az=HA, R_ay=-0.02, R_az=HA, L_m=0.95, R_m=0.95, L_heel=-0.1, R_heel=-0.1,
             L_flex=0.50, R_flex=0.50, L_abd=0.14, R_abd=0.14, L_elbow=0.10, R_elbow=0.10, L_tele=0.40, R_tele=0.40,
             L_tool="forks", R_tool="forks", slew=0.0, luff=1.10, tele=0.0, hook_drop=0.3)
    for it in range(5):                                             # let the hips down until the tines are at the pallet's slot
        her = machine(p); zt = min(p["_L_tine"].z, p["_R_tine"].z)
        for k in list(parts): parts[k].free(); del parts[k]
        if abs(zt - 0.07) < 0.01: break
        p["hip_z"] = max(0.70, p["hip_z"] + (0.07 - zt))
    her = machine(p); tL, tR = p["_L_tine"], p["_R_tine"]
    yc = (tL.y + tR.y) / 2 - 0.36; top = max(tL.z, tR.z) + 0.03
    box("steel", (-0.82, yc - 0.48, top), (0.82, yc + 0.48, top + 0.03))                             # the pallet's deck
    for x in (-0.80, 0.0, 0.80): box("dark", (x - 0.06, yc - 0.48, 0.0), (x + 0.06, yc + 0.48, top))   # its runners (the tines between)
    crate(Vector((0, yc, top + 0.03 + 0.36)), (1.40, 0.90, 0.72))
    ground(-4, 3); flush(); place_her(her, p["hip_z"], 0.0)
    render(cam, "atlant_lowgrip", (3.6, -3.9, 1.2), (0, -0.8, 0.7), 34); clear_machine()
    log.append("lowgrip hip_z %.2f tines z %.3f / %.3f" % (p["hip_z"], tL.z, tR.z))


# ================= animation: key poses -> frames (the same kinematics every frame) =================
def ease(u): u = max(0.0, min(1.0, u)); return u * u * (3 - 2 * u)

def blend(keys, t):
    """keys: [(time, pose dict)]; numbers eased between neighbours, the rest from the earlier key"""
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if t0 <= t <= t1:
            u = ease((t - t0) / (t1 - t0) if t1 > t0 else 1.0); out = dict(a)
            for k, v in b.items():
                if isinstance(v, (int, float)) and isinstance(a.get(k), (int, float)): out[k] = a[k] + (v - a[k]) * u
                elif k not in a: out[k] = v
            return out
    return dict(keys[-1][1] if t > keys[-1][0] else keys[0][1])

def solve_reach(p, want=0.07):
    """the hips down until the fork tines are at `want` over the ground (the reverse knee lets them that low)"""
    for it in range(8):
        machine(p); zt = min(p["_L_tine"].z, p["_R_tine"].z)
        for k in list(parts): parts[k].free(); del parts[k]
        if abs(zt - want) < 0.008: break
        p["hip_z"] = max(0.62, min(1.37, p["hip_z"] + (want - zt)))
    return p

def run_anim(cam, name, keys, nfr, props, camloc, camtgt, lens=34):
    d = os.path.join(OUT, name); os.makedirs(d, exist_ok=True)
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 960, 800
    T = keys[-1][0]
    for i in range(nfr):
        t = T * i / (nfr - 1); p = blend(keys, t)
        her = machine(p); props(p, t); ground(-5, 4); flush(); place_her(her, p["hip_z"], p.get("hip_y", 0.0))
        render(cam, os.path.join(name, "f%03d" % i), camloc, camtgt, lens); clear_machine()


def tine_heel(s, f, a, e, t, hz, hy=0.0):
    """where the fork tine's heel is for these arm angles (the same chain as machine(): shoulder, 0.40 + 0.40 + tele,
    the wrist 0.06, the plate 0.05, the tine's shank 0.14 down)"""
    Sh = Vector((s * 0.44, hy + 0.06, hz + 0.71)); ud, fd = arm_dirs(s, f, a, e)
    return Sh + ud * 0.40 + fd * (0.40 + t + 0.11) + Vector((0, 0, -0.14))

def fit_arms(p, y_want, z_want, tele=0.40):
    """the arms' flex and elbow (both sides alike) that put the tines' heel at (y_want, z_want) - a search"""
    best = None
    for i in range(61):
        f = -0.2 + 1.8 * i / 60
        for j in range(41):
            e = 1.4 * j / 40
            q = tine_heel(1, f, p.get("L_abd", 0.14), e, tele, p["hip_z"], p.get("hip_y", 0.0))
            err = (q.y - y_want) ** 2 + (q.z - z_want) ** 2
            if best is None or err < best[0]: best = (err, f, e)
    _, f, e = best
    p.update(L_flex=f, R_flex=f, L_elbow=e, R_elbow=e, L_tele=tele, R_tele=tele)
    return math.sqrt(best[0])

def scene_anims(cam):
    base = dict(REST_POSE); base.update(L_tool="forks", R_tool="forks", cw=0.55, ballast=0)
    # ---- 1. the grip from below: crouch, reach, tines under the pallet, stand up with the load ----
    k0 = stance(dict(base))
    TOE_Y = -0.02 - 0.86                                                  # the toes' tips (the ankle -0.02, the toes 0.86 ahead)
    k1 = stance(dict(base, hip_z=0.80, L_m=0.95, R_m=0.95, L_heel=-0.1, R_heel=-0.1, L_abd=0.14, R_abd=0.14, cw=1.05, ballast=1, luff=1.20), -0.02, -0.02)
    for it in range(4):                                                    # the hips and the arms together: the tines at the slot
        err = fit_arms(k1, TOE_Y - 0.06, 0.09, 0.45)
        if err < 0.02: break
        k1["hip_z"] = max(0.62, k1["hip_z"] - 0.05)
    k3 = stance(dict(base, hip_z=1.30, L_abd=0.14, R_abd=0.14, cw=1.0, ballast=1, luff=1.20), -0.05, -0.05)
    fit_arms(k3, TOE_Y - 0.10, 0.62, 0.40)                                 # the carry: the load low, before her chest, over the toes' tips
    machine(k1); tl = (k1["_L_tine"] + k1["_R_tine"]) / 2
    for k in list(parts): parts[k].free(); del parts[k]
    top = tl.z + 0.03; yc = tl.y - 0.62 + 0.36                              # the pallet on the tines' length, its back at their heel
    log.append("lowgrip anim: hip %.2f flex %.2f elbow %.2f tines (%.2f, %.2f); carry flex %.2f elbow %.2f" % (k1["hip_z"], k1["L_flex"], k1["L_elbow"], tl.y, tl.z, k3["L_flex"], k3["L_elbow"]))
    keys = [(0, k0), (1.0, k1), (1.4, k1), (2.6, k3), (3.0, k3)]
    def props(p, t):
        tm = (p["_L_tine"] + p["_R_tine"]) / 2
        dz = (tm.z - tl.z) if t > 1.4 else 0.0; dy = (tm.y - tl.y) if t > 1.4 else 0.0
        for x in (-0.80, 0.0, 0.80): box("dark", (x - 0.06, yc - 0.36 + dy, dz), (x + 0.06, yc + 0.36 + dy, top + dz))
        box("steel", (-0.82, yc - 0.36 + dy, top + dz), (0.82, yc + 0.36 + dy, top + 0.03 + dz))
        crate(Vector((0, yc + dy, top + 0.03 + 0.30 + dz)), (1.40, 0.70, 0.60))
    run_anim(cam, "anim_lowgrip", keys, 40, props, (3.8, -4.2, 1.5), (0, -0.8, 0.9))
    # ---- 2. the crane: out to her left and down, hook on, lift, swing round before her, set on the arms ----
    c0 = stance(dict(base, L_tool="pad", R_tool="claw", luff=1.35, tele=0.0, hook_drop=0.30))
    c1 = stance(dict(c0, slew=1.30, luff=0.45, tele=0.70, cw=1.05, ballast=1))
    c1["hook"] = True
    machine(c1); tip1 = c1["_tip"].copy()
    for k in list(parts): parts[k].free(); del parts[k]
    ground_crate = Vector((tip1.x, tip1.y, 0.31))
    c1["hook_drop"] = tip1.z - (ground_crate.z + 0.31 + 0.12)
    c2 = dict(c1, hook_drop=c1["hook_drop"] - 0.45)                          # lifted 0.45 m clear of the ground only: it never
                                                                          # swings over her head (the slew is done low)
    c3 = dict(c2, slew=0.0, luff=0.30, tele=0.75)
    machine(c3); tip3 = c3["_tip"].copy()
    for k in list(parts): parts[k].free(); del parts[k]
    c3["hook_drop"] = tip3.z - (1.05 + 0.43)                                  # the crate's centre at 1.05 m, before her
    keys = [(0, c0), (1.2, c1), (1.5, c1), (2.4, c2), (3.6, c3), (4.0, c3)]
    def props2(p, t):
        tip = p["_tip"]; hook = tip - Vector((0, 0, p.get("hook_drop", 0.3)))
        if t < 1.5: crate(ground_crate, (0.9, 0.8, 0.62))
        else: crate(hook - Vector((0, 0, 0.12 + 0.31)), (0.9, 0.8, 0.62))
    run_anim(cam, "anim_crane", keys, 44, props2, (4.6, -4.6, 2.4), (0.6, -0.4, 1.4))

def main():
    sys.path.insert(0, HERE); import render_util
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    mats(); cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.hide_render = True                                     # our own ground with lines (the walk shows on it)
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1300, 1100
    if SHOT == "walk": scene_walk(cam)
    elif SHOT == "mesh": scene_mesh(cam)
    elif SHOT == "lowgrip": scene_lowgrip(cam)
    elif SHOT == "anims": scene_anims(cam)
    else: scene_poses(cam)

try:
    main()
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
