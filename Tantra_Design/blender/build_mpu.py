# МПУ - the test sample (open platform, a control post with a joystick up front, no cabin): mesh for Orbiter + geometry header.
# Э.МПУ (VARIANT=EMPU) - the same chassis with the crew cabin (lined inside, windows, six seats, the console with a yoke
# as the Tantra's) and the four vectored thrust pods (static here).
# blender --background astronavigator_coverall.blend --python build_mpu.py      (VARIANT = MPU | EMPU)
#   -> Meshes\MPU\MPU.msh (or EMPU.msh), Orbitersdk\samples\MPU\MpuGeo.h (MPU only), renders\<variant>_test_side.png / _34.png
# Blender axes: x across, y along (front -y), z up. Orbiter: x right, y up, z forward  =>  (x, y, z)_orb = (-x, z - CG_H, -y).
# Groups: one per (part, material); the header lists the groups of each arm and each wheel for the animations.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
ORB = os.path.abspath(os.path.join(HERE, "..", ".."))
VAR = os.environ.get("VARIANT", "MPU").upper()
MESH_OUT = os.path.join(ORB, "Meshes", "MPU", VAR + ".msh"); GEO_OUT = os.path.join(ORB, "Orbitersdk", "samples", "MPU", "MpuGeo.h")
HV = int(os.environ.get("MPU_HATCH", "0"))         # the hatch mock-ups (1..3): rendered only, written to renders\ not the game
HSTYLE = HV or 1                                   # the game's hatch: 1, the spacecraft hatch (the user's pick 2026-10-07)
if HV: MESH_OUT = os.path.join(HERE, "renders", "mock_hatch.msh"); GEO_OUT = os.path.join(HERE, "renders", "mock_geo.h")
LOG = os.path.join(HERE, "build_mpu_%s.log" % VAR.lower()); log = []
CG_H = 1.0                                        # the vessel's origin (centre of gravity) above the ground at rest
WHEEL_R, WHEEL_W = 0.80, 0.65                     # chassis v2: D1.6 x 0.65 m airless wheels
DZD = 0.20                                        # the deck raised 1.42 -> 1.62: the post / cabin (built for 1.42) go up by this
P_LOW, P_UP = (0.50, 0.58), (0.55, 1.18)          # the wishbones' inner pivots on the backbone (x, z), blender
K_LOW, K_UP = (1.57, 0.44), (1.54, 1.10)          # their outer ball joints on the knuckle at rest
AXLES = (-3.30, -1.65, 1.65, 3.30)                # blender y of the axles (front first)
HUB_X = 1.85

MATS = [("Hull", (0.11, 0.12, 0.135), 0.0), ("Deck", (0.20, 0.21, 0.23), 0.0), ("Metal", (0.32, 0.33, 0.35), 0.0),
        ("Glass", (0.05, 0.09, 0.13), 0.0), ("Tread", (0.05, 0.05, 0.055), 0.0), ("Red", (0.72, 0.12, 0.10), 0.0),
        ("Lamp", (1.0, 0.96, 0.85), 1.0), ("Seam", (0.04, 0.04, 0.045), 0.0), ("Blade", (0.20, 0.22, 0.26), 0.0),
        ("White", (0.85, 0.86, 0.87), 0.0), ("Window", (0.55, 0.68, 0.72), 0.0), ("Lining", (0.58, 0.59, 0.58), 0.0),
        ("Seat", (0.15, 0.16, 0.18), 0.0), ("Ceramic", (0.78, 0.76, 0.72), 0.0), ("Coil", (0.55, 0.30, 0.15), 0.0),
        ("Warn", (0.86, 0.64, 0.08), 0.0), ("LampIn", (1.0, 0.25, 0.18), 1.0), ("Term", (1.0, 1.0, 1.0), 1.0), ("YScr", (1.0, 1.0, 1.0), 1.0)]
ALPHA = {"Window": 0.12}
MI = {n: i for i, (n, _, _) in enumerate(MATS)}
parts = {}                                        # part name -> list of (verts, faces, material index) triangles collected

class Part:
    def __init__(self): self.v = []; self.f = []      # per material: v list [(co, n)], faces
    pass

def part(name):
    if name not in parts: parts[name] = {}
    return parts[name]

def add_poly(pn, pts, mi):
    """a planar polygon (list of Vectors, CCW seen from outside) to part pn, material mi"""
    d = part(pn).setdefault(mi, ([], []))
    n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
    n = n.normalized() if n.length > 1e-9 else Vector((0, 0, 1))
    base = len(d[0])
    for p in pts: d[0].append((p.copy(), n))
    for k in range(1, len(pts) - 1): d[1].append((base, base + k, base + k + 1))

def ring(c, ax, r, seg):
    ax = ax.normalized(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    return [c + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r for j in range(seg)]

def loft(pn, rings, mi, cap=True, closed=False, smooth=True):
    d = part(pn).setdefault(mi, ([], [])); n = len(rings[0]); k = len(rings)
    cen = [sum(r, Vector()) / n for r in rings]
    if smooth:
        base = len(d[0])
        for i, r in enumerate(rings):
            for p in r:
                nn = (p - cen[i]); nn = nn.normalized() if nn.length > 1e-9 else Vector((0, 0, 1)); d[0].append((p.copy(), nn))
        for i in range(k if closed else k - 1):
            a, b = base + i * n, base + ((i + 1) % k) * n
            for j in range(n):
                j1 = (j + 1) % n
                d[1].append((a + j, a + j1, b + j1)); d[1].append((a + j, b + j1, b + j))
    else:
        for i in range(k if closed else k - 1):
            A, B = rings[i], rings[(i + 1) % k]
            for j in range(n):
                j1 = (j + 1) % n; add_poly(pn, [A[j], A[j1], B[j1], B[j]], mi)
    if cap and not closed:
        add_poly(pn, list(reversed(rings[0])), mi); add_poly(pn, list(rings[-1]), mi)

def cyl(pn, a, b, r, mi, seg=24):
    a, b = Vector(a), Vector(b); loft(pn, [ring(a, b - a, r, seg), ring(b, b - a, r, seg)], mi)

def box(pn, lo, hi, mi):
    lo, hi = Vector(lo), Vector(hi)
    lo, hi = Vector((min(lo.x, hi.x), min(lo.y, hi.y), min(lo.z, hi.z))), Vector((max(lo.x, hi.x), max(lo.y, hi.y), max(lo.z, hi.z)))
    P = [Vector((x, y, z)) for z in (lo.z, hi.z) for y in (lo.y, hi.y) for x in (lo.x, hi.x)]
    for q in ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)):
        add_poly(pn, [P[i] for i in q], mi)

def oct_ring(y, hw, z0, z1, ch):
    return [Vector(p) for p in ((-hw + ch, y, z0), (hw - ch, y, z0), (hw, y, z0 + ch), (hw, y, z1 - ch), (hw - ch, y, z1), (-hw + ch, y, z1), (-hw, y, z1 - ch), (-hw, y, z0 + ch))]

def wheel(pn, c, sx):
    c = Vector(c); ax = Vector((1, 0, 0)); w, r = WHEEL_W, WHEEL_R
    a, b = c - ax * w / 2, c + ax * w / 2
    loft(pn, [ring(a, ax, r - 0.09, 48), ring(a, ax, r, 48), ring(b, ax, r, 48), ring(b, ax, r - 0.09, 48)], MI["Tread"], cap=False, closed=True, smooth=False)
    for k in range(20):
        t = 2 * math.pi * k / 20
        for s in (-1, 1):
            p0 = c + Vector((0, math.cos(t), math.sin(t))) * (r + 0.01)
            p1 = c + Vector((s * w * 0.46, math.cos(t + 0.10) * (r + 0.01), math.sin(t + 0.10) * (r + 0.01)))
            cyl(pn, p0, p1, 0.018, MI["Tread"], 4)
    for k in range(20):
        t0 = 2 * math.pi * k / 20; pts = []
        for i in range(7):
            u = i / 6; rr = 0.30 + (r - 0.40) * u; tt = t0 + 0.35 * math.sin(math.pi * u); pts.append((rr, tt))
        for i in range(6):
            (r0, t0_), (r1, t1_) = pts[i], pts[i + 1]
            q = [c + Vector((-w * 0.38, math.cos(t0_) * r0, math.sin(t0_) * r0)), c + Vector((-w * 0.38, math.cos(t1_) * r1, math.sin(t1_) * r1)),
                 c + Vector((w * 0.38, math.cos(t1_) * r1, math.sin(t1_) * r1)), c + Vector((w * 0.38, math.cos(t0_) * r0, math.sin(t0_) * r0))]
            add_poly(pn, q, MI["Blade"]); add_poly(pn, q[::-1], MI["Blade"])
    cyl(pn, c - ax * 0.20, c + ax * 0.20, 0.31, MI["Metal"], 40)
    cyl(pn, c + ax * sx * 0.20, c + ax * sx * 0.235, 0.22, MI["Hull"], 40)
    cyl(pn, c + ax * sx * 0.235, c + ax * sx * 0.245, 0.08, MI["Red"], 20)

def to_orb(p): return Vector((-p.x, p.z - CG_H, -p.y))
def bl(x, y, z): return Vector((-x, -z, y + CG_H))          # orbiter vessel frame -> blender

def quad4(pn, a, b, c, d, mi, both=False):
    add_poly(pn, [a, b, c, d], mi)
    if both: add_poly(pn, [d, c, b, a], mi)

def panel(pn, x, y0, y1, za, zb, holes, mi, out):
    """a flat wall in the plane x = const from y0..y1, za..zb, with rectangular holes (ya, yb, z0, z1); normal +x if out"""
    def rect(ya, yb, z0, z1):
        if yb - ya < 1e-4 or z1 - z0 < 1e-4: return
        P = [Vector((x, ya, z0)), Vector((x, yb, z0)), Vector((x, yb, z1)), Vector((x, ya, z1))]
        add_poly(pn, P if out else P[::-1], mi)
    y = y0
    for ya, yb, z0, z1 in sorted(holes):
        rect(y, ya, za, zb); rect(ya, yb, za, z0); rect(ya, yb, z1, zb); y = yb
    rect(y, y1, za, zb)

def slab(pn, b0, b1, hw, th, mi):
    """a board from the middle of its bottom edge b0 to the middle of its top edge b1, half width hw (along x), thickness th"""
    b0, b1 = Vector(b0), Vector(b1); up = (b1 - b0).normalized(); n = Vector((1, 0, 0)).cross(up).normalized() * th / 2
    P = [b0 + Vector((-hw, 0, 0)) - n, b0 + Vector((hw, 0, 0)) - n, b0 + Vector((hw, 0, 0)) + n, b0 + Vector((-hw, 0, 0)) + n]
    Q = [p + (b1 - b0) for p in P]
    for a, b, c, d in ((P[0], P[1], P[2], P[3]), (Q[3], Q[2], Q[1], Q[0])):
        add_poly(pn, [d, c, b, a], mi); add_poly(pn, [a, b, c, d], mi)
    for k in range(4):
        add_poly(pn, [P[k], P[(k + 1) % 4], Q[(k + 1) % 4], Q[k]], mi); add_poly(pn, [Q[k], Q[(k + 1) % 4], P[(k + 1) % 4], P[k]], mi)

# ---- Э.МПУ: the cabin, its inside, the seats, the console with the yoke, the thrust pods ----
CAB = [(-3.35, 1.30, 1.42, 2.05, 0.25), (-2.85, 1.62, 1.40, 2.80, 0.40), (-1.95, 1.72, 1.38, 3.28, 0.50),
       (2.20, 1.72, 1.38, 3.28, 0.50), (3.05, 1.55, 1.40, 3.05, 0.45), (3.35, 1.25, 1.42, 2.55, 0.35)]
WIN_Z = (2.10, 2.65)                                        # side windows' sill and head (blender z)
WIN_L = [(-1.80, -1.05), (0.70, 1.45)]                      # blender +x (the orbiter's left, the hatch side)
WIN_R = [(-1.80, -1.05), (-0.55, 0.20), (0.70, 1.45)]       # blender -x
HATCH_Y = (-0.45, 0.45)
SEATS = [(-0.70, 2.25), (0.70, 2.25), (-0.70, 1.20), (0.70, 1.20), (-0.70, -1.35), (0.70, -1.35)]   # orbiter (x, z) of the hips
FLOOR = 0.42                                                 # orbiter y of the cabin floor (the deck)
HIPS = 0.72
TOP = "Top"
HUB = (-0.70, FLOOR + 1.02, 2.70)                            # the yoke's hub (orbiter); its axes as the Tantra's yoke
YX, YY, YZ = Vector((1, 0, 0)), Vector((0, 0.7960, 0.6053)), Vector((0, 0.6053, -0.7960))

def cabin():
    R = [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in CAB]
    for s_ in range(len(R) - 1):                                                  # the shell, face by face
        A, B = R[s_], R[s_ + 1]
        for j in range(8):
            j1 = (j + 1) % 8
            if s_ == 2 and j in (2, 6): continue                                  # the straight sides: panels with holes
            mi = MI["Window"] if s_ in (0, 1) and j == 4 else MI["White"]        # windscreen and the roof glazing ahead
            add_poly(TOP, [A[j], A[j1], B[j1], B[j]], mi)
    add_poly(TOP, list(reversed(R[0])), MI["White"]); add_poly(TOP, list(R[-1]), MI["White"])
    y2, y3 = CAB[2][0], CAB[3][0]; za, zb = CAB[2][3] + CAB[2][4], CAB[2][3] - CAB[2][4] + (CAB[2][3] - CAB[2][3])
    za, zb = CAB[2][2] + CAB[2][4], CAB[2][3] - CAB[2][4]
    holesL = [(a, b, *WIN_Z) for a, b in WIN_L]; holesR = [(a, b, *WIN_Z) for a, b in WIN_R]
    panel(TOP, 1.72, y2, y3, za, zb, holesL, MI["White"], True)
    panel(TOP, -1.72, y2, y3, za, zb, holesR, MI["White"], False)
    for sx, holes in ((1, holesL), (-1, holesR)):                                 # the panes, seen from both sides
        for ya, yb, z0, z1 in holes:
            x = sx * 1.70
            quad4(TOP, Vector((x, ya, z0)), Vector((x, yb, z0)), Vector((x, yb, z1)), Vector((x, ya, z1)), MI["Window"], True)
    # the lining inside (faces inwards); the floor is the deck. No lining where the glass is
    L = [oct_ring(y, hw - 0.06, z0 + 0.04, z1 - 0.06, ch) for y, hw, z0, z1, ch in CAB]
    for s_ in range(len(L) - 1):
        A, B = L[s_], L[s_ + 1]
        for j in range(1, 8):                                                     # j = 0: the bottom (the deck is the floor)
            j1 = (j + 1) % 8
            if s_ == 2 and j in (2, 6): continue
            if s_ in (0, 1) and j == 4:
                add_poly(TOP, [B[j], B[j1], A[j1], A[j]], MI["Window"]); continue
            add_poly(TOP, [B[j], B[j1], A[j1], A[j]], MI["Lining"])
    add_poly(TOP, list(L[0]), MI["Lining"]); add_poly(TOP, list(reversed(L[-1])), MI["Lining"])
    holesLi = holesL + [(HATCH_Y[0], HATCH_Y[1], 1.45, 2.85)]
    panel(TOP, 1.66, y2, y3, za + 0.04, zb - 0.04, holesLi, MI["Lining"], False)
    panel(TOP, -1.66, y2, y3, za + 0.04, zb - 0.04, holesR, MI["Lining"], True)
    panel(TOP, 1.66, HATCH_Y[0], HATCH_Y[1], 1.45, 2.85, [], MI["Metal"], False)          # the hatch from inside
    for sx in (-1, 1): box(TOP, (sx * 1.722 - 0.01, -1.6, 2.00), (sx * 1.722 + 0.01, 3.0, 2.03), MI["Red"])
    box(TOP, (1.72, HATCH_Y[0], 1.50), (1.75, HATCH_Y[1], 2.80), MI["Metal"])                # the hatch outside
    box(TOP, (1.75, -0.05, 2.05), (1.80, 0.05, 2.25), MI["Hull"])                            # its handle
    for sx in (-0.7, 0.7): box(TOP, (sx - 0.35, -2.05, 3.27), (sx + 0.35, -1.85, 3.31), MI["Lamp"])
    box(TOP, (-0.5, 1.8, 3.27), (0.5, 2.6, 3.37), MI["Hull"])
    box(TOP, (-1.62, 1.6, 3.18), (1.62, 3.0, 3.22), MI["Lamp"])                              # ceiling light strip (inside)

def seat(ox, oz, driver):
    f = FLOOR
    box(TOP, tuple(bl(ox + 0.18, f, oz - 0.12)), tuple(bl(ox - 0.18, f + 0.55, oz + 0.12)), MI["Metal"])           # pedestal
    box(TOP, tuple(bl(ox + 0.27, f + 0.55, oz - 0.20)), tuple(bl(ox - 0.27, f + 0.635, oz + 0.30)), MI["Seat"])    # cushion
    slab(TOP, bl(ox, f + 0.635, oz - 0.24), bl(ox, f + 1.30, oz - 0.38), 0.26, 0.09, MI["Seat"])                 # back
    slab(TOP, bl(ox, f + 1.34, oz - 0.39), bl(ox, f + 1.55, oz - 0.42), 0.13, 0.08, MI["Seat"])                  # headrest
    for sx in (-1, 1):                                                                                               # armrests
        box(TOP, tuple(bl(ox + sx * 0.27, f + 0.80, oz - 0.18)), tuple(bl(ox + sx * 0.33, f + 0.86, oz + 0.22)), MI["Hull"])
        box(TOP, tuple(bl(ox + sx * 0.29, f + 0.635, oz - 0.10)), tuple(bl(ox + sx * 0.31, f + 0.80, oz - 0.04)), MI["Metal"])
    box(TOP, tuple(bl(ox + 0.27, f + 0.40, oz - 0.21)), tuple(bl(ox - 0.27, f + 0.42, oz + 0.31)), MI["Red"])      # seat number band

def console_and_yoke():
    f = FLOOR
    # the console across the front: a sloped desk with screens, a foot well under it
    box(TOP, tuple(bl(1.50, f, 3.05)), tuple(bl(-1.50, f + 0.55, 3.25)), MI["Hull"])
    P = [bl(-1.50, f + 0.78, 2.82), bl(1.50, f + 0.78, 2.82), bl(1.50, f + 0.98, 3.20), bl(-1.50, f + 0.98, 3.20)]
    quad4(TOP, *P, MI["Hull"], True)
    for x0 in (-1.25, -0.30, 0.65):
        quad4(TOP, bl(x0 + 0.55, f + 0.795, 2.86), bl(x0, f + 0.795, 2.86), bl(x0, f + 0.965, 3.17), bl(x0 + 0.55, f + 0.965, 3.17), MI["Glass"], True)
    box(TOP, tuple(bl(1.50, f + 0.55, 3.05)), tuple(bl(-1.50, f + 0.80, 3.22)), MI["Hull"])
    # the yoke: a column out of the console, the hub, a bar, two horns - the Tantra's yoke geometry (hub frame X, Y, Z)
    H = Vector(HUB)
    def B(v): return bl(v.x, v.y, v.z)
    cyl(TOP, B(H - YZ * 0.06), B(H - YZ * 0.50), 0.035, MI["Metal"], 12)
    cyl(TOP, B(H + YZ * 0.03), B(H - YZ * 0.05), 0.07, MI["Hull"], 20)
    for sx in (-1, 1):
        gt = H + YX * (sx * 0.243) + YY * -0.058; gb = H + YX * (sx * 0.268) + YY * -0.168
        cyl(TOP, B(H), B(H + YX * (sx * 0.20)), 0.022, MI["Hull"], 10)
        cyl(TOP, B(H + YX * (sx * 0.20)), B(gt), 0.022, MI["Hull"], 10)
        cyl(TOP, B(gt), B(gb), 0.021, MI["Seat"], 12)
        cyl(TOP, B(gt - (gb - gt) * 0.08), B(gt), 0.024, MI["Red"], 12)

def pods():
    """four vectored thrust pods on their arms at the corners (static here: down)"""
    for sx in (-1, 1):
        for sy in (-1, 1):
            yr0, yr1 = sy * 3.55, sy * 4.30
            box(TOP, (sx * 0.15, min(yr0, yr1), 1.36), (sx * 1.25, max(yr0, yr1), 1.50), MI["Hull"])
            c = Vector((sx * 1.62, sy * 5.05, 1.55)); hinge = Vector((sx * 0.9, sy * 4.15, 1.62))
            cyl(TOP, Vector((sx * 0.9, sy * 4.15, 1.45)), hinge, 0.11, MI["Metal"], 12)
            cyl(TOP, hinge, c - Vector((sx * 0.48, 0, 0)), 0.09, MI["Metal"], 12)
            for side in (-1, 1):
                q = c + Vector((side * 0.52, 0, 0))
                box(TOP, (q.x - 0.04, q.y - 0.11, q.z - 0.11), (q.x + 0.04, q.y + 0.11, q.z + 0.32), MI["Metal"])
            box(TOP, (c.x - 0.56, c.y - 0.11, c.z + 0.24), (c.x + 0.56, c.y + 0.11, c.z + 0.32), MI["Metal"])
            cyl(TOP, c - Vector((0.52, 0, 0)), c + Vector((0.52, 0, 0)), 0.07, MI["Metal"], 12)
            cyl(TOP, c + Vector((0, 0, 0.22)), c + Vector((0, 0, -0.16)), 0.38, MI["Hull"], 32)
            cyl(TOP, c + Vector((0, 0, 0.02)), c + Vector((0, 0, -0.02)), 0.39, MI["Red"], 32)
            cyl(TOP, c + Vector((0, 0, -0.16)), c + Vector((0, 0, -0.22)), 0.36, MI["Coil"], 32)
            cyl(TOP, c + Vector((0, 0, -0.13)), c + Vector((0, 0, -0.221)), 0.29, MI["Seam"], 32)
            cyl(TOP, c + Vector((0, 0, -0.13)), c + Vector((0, 0, -0.16)), 0.395, MI["Ceramic"], 32)


# ---- Э.МПУ «C — экзоскелет» (the user's pick, 2026-10-07): built at the deck 1.62, into "Body" (no shift) ----
# Blender frame (front -y). The hull: a framed nose (lower glazing - the wheels in view), the habitat, the stern airlock; an
# external load frame (ribs, rails) carrying the system bays over the fenders, the radiators (stowed flat), the panel, the mast.
# Inside: the lining, two seats at the console (the driver's on the left, orbiter -x = blender +x) with the yoke, four bunks
# on the right, a table and two benches and the galley on the left, the bulkhead and its door, the airlock with two suits.
DE = 1.62
E_NOSE = [(-4.30, 1.00, DE + 0.10, 2.70, 0.30), (-3.85, 1.42, DE + 0.02, 3.30, 0.45), (-3.05, 1.47, DE, 3.55, 0.50)]
E_HAB = [(-3.05, 1.47, DE, 3.55, 0.50), (2.45, 1.47, DE, 3.55, 0.50)]
E_LOCK = [(2.45, 1.47, DE, 3.55, 0.50), (4.25, 1.47, DE, 3.55, 0.50)]     # as high and wide as the habitat (1.86 m clear)
E_GLASS = {0: (1, 2, 3, 4, 5, 6, 7), 1: (1, 2, 3, 4, 5, 6, 7)}       # the nose's faces that are panes
E_SEATS = [(0.65, -3.15), (-0.65, -3.15)]                              # blender (x, y) of the hips: the driver's first (left)
E_HUB = (0.65, -3.62, DE + 1.02)                                        # the yoke (blender); orbiter x = -0.65, z = 3.62

def e_pane(pn, P, depth=0.05, rim=0.09):
    """a pane set into a frame face (both sides: seen from inside and out), the reveal round it"""
    c = sum(P, Vector()) / len(P); n = (P[1] - P[0]).cross(P[2] - P[0]).normalized()
    Q = [q + (c - q) * min(rim / max((c - q).length, 1e-6), 0.45) - n * depth for q in P]
    for a in range(len(P)):
        b = (a + 1) % len(P); add_poly(pn, [P[a], P[b], Q[b], Q[a]], MI["Hull"])

def e_shell(pn, R, glass, inset=0.07):
    """the outer skin (outwards), panes where 'glass' says, and the lining inside (inwards; none at the panes, none at the
    bottom - the deck is the floor)"""
    rings = [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in R]
    inner = [oct_ring(y, hw - inset, z0 + 0.02, z1 - inset, ch) for y, hw, z0, z1, ch in R]
    for s_, (A, B) in enumerate(zip(rings, rings[1:])):
        for j in range(8):
            j1 = (j + 1) % 8
            if s_ in glass and j in glass[s_]: e_pane(pn, [A[j], A[j1], B[j1], B[j]]); continue
            if j == 0 and max(A[0].z, B[0].z) < DE + 0.005: continue          # the bottom on the deck: the deck is it (no flicker)
            add_poly(pn, [A[j], A[j1], B[j1], B[j]], MI["White"])
            if j != 0: add_poly(pn, [inner[s_ + 1][j], inner[s_ + 1][j1], inner[s_][j1], inner[s_][j]], MI["Lining"])
    return rings, inner

def e_joint(pn, p, r, mi):
    """a node where tubes meet: a block a little fatter than the tubes, the open ends inside it"""
    p = Vector(p); box(pn, p - Vector((r, r, r)) * 1.35, p + Vector((r, r, r)) * 1.35, mi)

def e_beams(pn, rings, ks, r=0.07):
    for k in ks:
        for a, b in zip(rings[k], rings[k][1:] + rings[k][:1]): cyl(pn, a, b, r, MI["White"], 6)
        for q in rings[k]: e_joint(pn, q, r, MI["White"])
    for k in range(len(rings) - 1):
        if k in ks and k + 1 in ks:
            for j in range(8): cyl(pn, rings[k][j], rings[k + 1][j], r, MI["White"], 6)

def e_bay(pn, sx, y0, y1, z0, z1, hatches=3):
    xi, xo = sx * 1.47, sx * 2.02
    box(pn, (min(xi, xo), y0, z0), (max(xi, xo), y1, z1), MI["White"])
    L = (y1 - y0) / hatches
    for k in range(hatches):
        ya, yb = y0 + k * L + 0.05, y0 + (k + 1) * L - 0.05
        box(pn, (xo - 0.004, ya, z0 + 0.15), (xo + 0.004, yb, z1 - 0.15), MI["Seam"])
        box(pn, (xo - 0.012 * sx, ya + 0.04, z0 + 0.19), (xo + 0.010 * sx, yb - 0.04, z1 - 0.19), MI["White"])   # the hatch stands proud of its groove
        box(pn, (xo - 0.03, 0.5 * (ya + yb) - 0.08, 0.5 * (z0 + z1) - 0.03), (xo + 0.03, 0.5 * (ya + yb) + 0.08, 0.5 * (z0 + z1) + 0.03), MI["Red"])
    for k in range(6): box(pn, (min(xi, xo) + 0.12, y0 + 0.3 + k * 0.08, z1), (max(xi, xo) - 0.18, y0 + 0.34 + k * 0.08, z1 + 0.02), MI["Seam"])

def e_seat(pn, x, y, face=-1):
    """a seat with its back towards -face along y (face -1: looking forward)"""
    f = DE
    box(pn, (x - 0.18, y - 0.12, f), (x + 0.18, y + 0.12, f + 0.55), MI["Metal"])
    box(pn, (x - 0.27, y - 0.30 * -face - 0.0, f + 0.55), (x + 0.27, y + 0.20 * -face, f + 0.635), MI["Seat"])
    yb = y - 0.24 * face
    slab(pn, Vector((x, yb, f + 0.635)), Vector((x, yb - 0.14 * face, f + 1.30)), 0.26, 0.09, MI["Seat"])
    slab(pn, Vector((x, yb - 0.15 * face, f + 1.34)), Vector((x, yb - 0.18 * face, f + 1.55)), 0.13, 0.08, MI["Seat"])
    for sx in (-1, 1): box(pn, (x + sx * 0.27, y - 0.18, f + 0.80), (x + sx * 0.33, y + 0.22, f + 0.86), MI["Hull"])

UVMAP = {}                                      # part -> (blender point -> u, v): faces showing a whole drawn texture

def obox(pn, c, r, u, n, sx, sy, sz, mi):
    """a box oriented by its axes r (across), u (up), n (out), centred at c"""
    c = Vector(c); r, u, n = Vector(r), Vector(u), Vector(n)
    P = [c + r * (i * sx / 2) + u * (j * sy / 2) + n * (k * sz / 2) for i in (-1, 1) for j in (-1, 1) for k in (-1, 1)]
    for F in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
        Q = [P[f] for f in F]
        nn = (Q[1] - Q[0]).cross(Q[2] - Q[0]); fc = sum(Q, Vector()) / 4
        add_poly(pn, Q if nn.dot(fc - c) > 0 else Q[::-1], mi)

# the terminal (the user's variant 2, 2026-10-07): a rugged instrument box on the console, tilted 25 deg to the driver, a sun
# hood, carrying handles, five hard keys each side of the screen; the screen set 6 cm in (MPU.cpp draws it, 512 x 384)
TERM_C, TERM_A = Vector((0.0, -3.74, DE + 0.98)), 30.0   # low, under the driver's line of sight
TERM_SW, TERM_SH = 0.52, 0.39
def term_axes():
    a = math.radians(TERM_A)
    return Vector((-1, 0, 0)), Vector((0, -math.sin(a), math.cos(a))), Vector((0, math.cos(a), math.sin(a)))   # her right, up, out

def e_terminal(pn):
    r, u, n = term_axes(); c = TERM_C
    W, H, D = 0.78, 0.49, 0.16
    obox(pn, c - n * (D - 0.01), r, u, n, W, H, 0.02, MI["Hull"])                          # the back
    for sgn in (-1, 1):
        obox(pn, c + r * sgn * (W / 2 - 0.065) - n * D / 2, r, u, n, 0.13, H, D, MI["Hull"])   # the side cheeks (the keys)
        obox(pn, c + u * sgn * (H / 2 - 0.025) - n * D / 2, r, u, n, W, 0.05, D, MI["Hull"])   # top and bottom
    for sgn in (-1, 1):                                                                         # carrying handles
        h0 = c + r * sgn * (W / 2 + 0.03); cyl(pn, h0 + u * 0.18, h0 - u * 0.18, 0.013, MI["Metal"], 10)
        for e in (-1, 1): cyl(pn, h0 + u * e * 0.18, h0 + u * e * 0.18 - r * sgn * 0.04, 0.013, MI["Metal"], 10)
        for k in range(5):                                                                      # the hard keys
            kc = c + r * sgn * (TERM_SW / 2 + 0.065) + u * (0.16 - k * 0.08) + n * 0.006
            obox(pn, kc, r, u, n, 0.075, 0.05, 0.014, MI["Seat"])
    # the screen, recessed
    sc = c - n * 0.06
    Q = [sc - r * TERM_SW / 2 - u * TERM_SH / 2, sc + r * TERM_SW / 2 - u * TERM_SH / 2, sc + r * TERM_SW / 2 + u * TERM_SH / 2, sc - r * TERM_SW / 2 + u * TERM_SH / 2]
    add_poly("TermScr", Q, MI["Term"])
    UVMAP["TermScr"] = lambda q: ((q - sc).dot(r) / TERM_SW + 0.5, 0.5 - (q - sc).dot(u) / TERM_SH)
    for sgn in (-1, 1):                                                                         # the recess' walls
        obox(pn, sc + r * sgn * (TERM_SW / 2 + 0.005) + n * 0.03, r, u, n, 0.01, TERM_SH, 0.06, MI["Seat"])
        obox(pn, sc + u * sgn * (TERM_SH / 2 + 0.005) + n * 0.03, r, u, n, TERM_SW + 0.02, 0.01, 0.06, MI["Seat"])
    # the keyboard on its pulled-out tray
    box(pn, (-0.25, -3.60, DE + 0.76), (0.25, -3.40, DE + 0.79), MI["Hull"])
    box(pn, (-0.23, -3.585, DE + 0.79), (0.23, -3.415, DE + 0.81), MI["Seat"])
    for j in range(14):
        for i in range(4):
            x = -0.20 + j * 0.0308; y = -3.57 + i * 0.038
            box(pn, (x - 0.012, y, DE + 0.81), (x + 0.012, y + 0.026, DE + 0.822), MI["Hull"])

# the projection on the windscreen: a film in the nose's top-front pane before the driver's eyes (where her level gaze meets
# the glass), 0.50 x 0.34 m; MPU.cpp draws it (512 x 352), photonic light written into the glass
PROJ_X0, PROJ_X1 = 0.68, -0.68                                  # blender x: the pane's left edge, its right (the whole top pane)
def e_projection():
    a, b = Vector((0, -4.30, 2.70)), Vector((0, -3.85, 3.30))    # the top face's edge along its slope (rings 0 -> 1)
    sdir = (b - a).normalized(); out = Vector((0, -sdir.z, sdir.y))
    c = a + (b - a) * 0.5 - out * 0.02                            # the whole pane between the two cross beams
    L = (b - a).length * 0.92
    top, bot = c + sdir * (L / 2), c - sdir * (L / 2)
    Q = [Vector((PROJ_X0, bot.y, bot.z)), Vector((PROJ_X1, bot.y, bot.z)), Vector((PROJ_X1, top.y, top.z)), Vector((PROJ_X0, top.y, top.z))]
    nn = (Q[1] - Q[0]).cross(Q[2] - Q[0])
    add_poly("Proj", Q if nn.dot(-out) > 0 else Q[::-1], MI["Proj"])
    UVMAP["Proj"] = lambda q: ((PROJ_X0 - q.x) / (PROJ_X0 - PROJ_X1), (top - q).dot(sdir) / L)

def rr(w, h, r, z0, x0=0.0, seg=5):
    """a rounded rectangle in (x, z), counter-clockwise"""
    P = []
    for cx, cz, a0 in ((x0 + w / 2 - r, z0 + r, -90), (x0 + w / 2 - r, z0 + h - r, 0), (x0 - w / 2 + r, z0 + h - r, 90), (x0 - w / 2 + r, z0 + r, 180)):
        for k in range(seg + 1):
            a = math.radians(a0 + 90 * k / seg); P.append((cx + r * math.cos(a), cz + r * math.sin(a)))
    return P

def slab_xz(pn, P, y, f, t, mi, rim_mi=None):
    """a flat plate of outline P (x, z) on a wall at y, t thick towards f; the face fanned from its centre"""
    yf = y + f * t; c = (sum(x for x, _ in P) / len(P), sum(z for _, z in P) / len(P))
    for k in range(len(P)):
        a, b = P[k], P[(k + 1) % len(P)]
        T = [Vector((c[0], yf, c[1])), Vector((a[0], yf, a[1])), Vector((b[0], yf, b[1]))]
        add_poly(pn, T if f < 0 else T[::-1], mi)
        S = [Vector((a[0], y, a[1])), Vector((b[0], y, b[1])), Vector((b[0], yf, b[1])), Vector((a[0], yf, a[1]))]
        add_poly(pn, S, rim_mi or mi); add_poly(pn, S[::-1], rim_mi or mi)

def ring_xz(pn, O, I, y, f, t, mi):
    """a frame between two outlines with the same point count, t proud towards f"""
    yf = y + f * t
    for k in range(len(O)):
        k1 = (k + 1) % len(O)
        Q = [Vector((O[k][0], yf, O[k][1])), Vector((O[k1][0], yf, O[k1][1])), Vector((I[k1][0], yf, I[k1][1])), Vector((I[k][0], yf, I[k][1]))]
        add_poly(pn, Q if f < 0 else Q[::-1], mi)
        S = [Vector((O[k][0], y, O[k][1])), Vector((O[k1][0], y, O[k1][1])), Vector((O[k1][0], yf, O[k1][1])), Vector((O[k][0], yf, O[k][1]))]
        add_poly(pn, S, mi); add_poly(pn, S[::-1], mi)

def e_outer_hatch(B, yd):
    if HSTYLE == 1:     # a spacecraft hatch: flush, rounded, a latch lever and an equalising valve, a small square port; no wheel
        for f in (1, -1):
            y0 = yd if f > 0 else yd - 0.002
            ring_xz(B, rr(1.04, 1.80, 0.24, DE + 0.04), rr(0.90, 1.66, 0.17, DE + 0.11), y0, f, 0.03, MI["Hull"])
            slab_xz(B, rr(0.88, 1.64, 0.16, DE + 0.12), y0, f, 0.025, MI["White"], MI["Seat"])
            box(B, (-0.13, y0 + f * 0.025, DE + 1.30), (0.13, y0 + f * 0.03, DE + 1.56), MI["Metal"])
            box(B, (-0.10, y0 + f * 0.03, DE + 1.33), (0.10, y0 + f * 0.032, DE + 1.53), MI["Window"])
            hz = DE + 0.98
            cyl(B, (0.28, y0 + f * 0.025, hz), (0.28, y0 + f * 0.06, hz), 0.035, MI["Metal"], 12)          # the lever's pivot
            cyl(B, (0.28, y0 + f * 0.06, hz), (0.00, y0 + f * 0.06, hz - 0.08), 0.018, MI["Red"] if f > 0 else MI["Metal"], 10)
            cyl(B, (-0.30, y0 + f * 0.025, hz + 0.20), (-0.30, y0 + f * 0.05, hz + 0.20), 0.03, MI["Metal"], 12)   # the valve
            cyl(B, (-0.30, y0 + f * 0.05, hz + 0.20), (-0.30, y0 + f * 0.06, hz + 0.20), 0.045, MI["Seat"], 12)
            box(B, (0.18, y0 + f * 0.025, hz - 0.22), (0.40, y0 + f * 0.027, hz - 0.17), MI["Warn"])           # its label plate
        for z in (DE + 0.35, DE + 1.45): cyl(B, (0.47, yd + 0.04, z - 0.10), (0.47, yd + 0.04, z + 0.10), 0.03, MI["Metal"], 10)
    elif HSTYLE == 2:   # an armoured vehicle's rear door: thick, heavy knuckle hinges full height, a window, a recessed handle
        for f in (1, -1):
            y0 = yd if f > 0 else yd - 0.002
            slab_xz(B, rr(0.96, 1.74, 0.10, DE + 0.06), y0, f, 0.012, MI["Seat"])                          # the seal
            slab_xz(B, rr(0.92, 1.70, 0.08, DE + 0.08), y0 + f * 0.012, f, 0.06, MI["Hull"])
            box(B, (-0.20, y0 + f * 0.07, DE + 1.20), (0.20, y0 + f * 0.075, DE + 1.55), MI["Metal"])
            box(B, (-0.17, y0 + f * 0.075, DE + 1.23), (0.17, y0 + f * 0.077, DE + 1.52), MI["Window"])
            box(B, (-0.36, y0 + f * 0.07, DE + 0.85), (-0.24, y0 + f * 0.075, DE + 1.05), MI["Seat"])        # the handle pocket
            cyl(B, (-0.33, y0 + f * 0.09, DE + 0.88), (-0.33, y0 + f * 0.09, DE + 1.02), 0.012, MI["Metal"], 8)
            for z in (DE + 0.30, DE + 0.95, DE + 1.60):
                box(B, (-0.40, y0 + f * 0.072, z - 0.03), (0.35, y0 + f * 0.078, z + 0.03), MI["Hull"])      # the stiffeners
        cyl(B, (0.50, yd + 0.05, DE + 0.10), (0.50, yd + 0.05, DE + 1.70), 0.04, MI["Metal"], 12)           # the hinge
        for z in (DE + 0.25, DE + 0.90, DE + 1.55): box(B, (0.38, yd + 0.02, z - 0.08), (0.52, yd + 0.09, z + 0.08), MI["Metal"])
        cyl(B, (-0.62, yd + 0.10, DE + 0.40), (-0.62, yd + 0.10, DE + 1.50), 0.02, MI["Metal"], 8)           # a grab bar on the hull
        for z in (DE + 0.40, DE + 1.50): cyl(B, (-0.62, yd, z), (-0.62, yd + 0.10, z), 0.02, MI["Metal"], 8)
    else:           # a ramp hatch: hinged at its foot, open - it is the stairs; the opening's frame rounded at the top
        O = rr(1.04, 1.82, 0.22, DE + 0.0); I = rr(0.92, 1.72, 0.17, DE + 0.05)
        ring_xz(B, O, I, yd, 1, 0.05, MI["Hull"]); ring_xz(B, O, I, yd - 0.002, -1, 0.05, MI["Hull"])
        L = 1.85; ang = math.radians(48); d = Vector((0, math.cos(ang), -math.sin(ang)))
        foot = Vector((0, yd + 0.05, DE + 0.02)); top = foot + d * L
        side = Vector((1, 0, 0)); nrm = Vector((0, math.sin(ang), math.cos(ang)))
        P = [foot - side * 0.46, foot + side * 0.46, top + side * 0.46, top - side * 0.46]
        for q in (P, [v - nrm * 0.06 for v in P][::-1]): add_poly(B, q, MI["Hull"])
        for sx in (-1, 1):
            add_poly(B, [P[0 if sx < 0 else 1], P[3 if sx < 0 else 2], P[3 if sx < 0 else 2] - nrm * 0.06, P[0 if sx < 0 else 1] - nrm * 0.06], MI["Hull"])
            cyl(B, foot + side * sx * 0.47 + Vector((0, 0, 0.75)), top + side * sx * 0.47 + nrm * 0.75, 0.02, MI["Metal"], 8)   # the rails
            cyl(B, Vector((sx * 0.47, yd + 0.03, DE + 1.45)), foot + d * 1.0 + side * sx * 0.47 + nrm * 0.02, 0.008, MI["Metal"], 6)   # the cables
        for k in range(1, 7):
            c = foot + d * (L * k / 7.0) + nrm * 0.005
            box(B, (c.x - 0.44, c.y - 0.03, c.z), (c.x + 0.44, c.y + 0.03, c.z + 0.025), MI["Metal"])           # the treads
        box(B, (-0.10, top.y - 0.10, 0.0), (0.10, top.y + 0.10, top.z), MI["Metal"])                           # its foot plate

def e_inner_door(B, dt):
    """the inner door: a pressure door that slides into the bulkhead - open, only its rounded frame"""
    for y, f in ((2.43, -1), (2.57, 1)):
        O = rr(1.04, 1.86, 0.20, DE - 0.02); I = rr(0.90, 1.80, 0.16, DE + 0.0)
        ring_xz(B, O, I, y, f, 0.025, MI["Hull"])
    box(B, (-0.52, 2.40, DE + 0.95), (-0.48, 2.43, DE + 1.10), MI["Metal"])                                   # the door's pull

def empu_c():
    B = "Body"
    nose, nin = e_shell(B, E_NOSE, E_GLASS)
    hab, hin = e_shell(B, E_HAB, {})
    lock, lin = e_shell(B, E_LOCK, {})
    add_poly(B, list(reversed(nose[0])), MI["White"])                         # the nose's tip
    e_beams(B, nose, (0, 1, 2), 0.08)                                          # the nose's load frame
    # the bulkhead between the habitat and the airlock, its door (a dark opening frame, the passage itself stays open)
    # (the wall follows the lining's octagon; the door 0.90 x 1.80 m, its top just under the ceiling)
    zc, dt = 3.48, DE + 1.80
    for y, f in ((2.45, 1), (2.55, -1)):
        for sx in (-1, 1):
            Q = [Vector((sx * x, y, z)) for x, z in ((0.45, DE), (0.90, DE), (1.40, DE + 0.50), (1.40, zc - 0.50), (0.90, zc), (0.45, zc))]
            add_poly(B, Q if sx * f > 0 else Q[::-1], MI["Lining"])
        Q = [Vector(p) for p in ((-0.45, y, dt), (0.45, y, dt), (0.45, y, zc), (-0.45, y, zc))]
        add_poly(B, Q if f > 0 else Q[::-1], MI["Lining"])
    for sx in (-1, 1): box(B, (sx * 0.45 - 0.04, 2.43, DE), (sx * 0.45 + 0.04, 2.57, dt), MI["Hull"])
    box(B, (-0.49, 2.43, dt - 0.04), (0.49, 2.57, dt + 0.04), MI["Hull"])
    e_inner_door(B, dt)
    box(B, (-0.76, 2.38, DE + 1.20), (-0.54, 2.43, DE + 1.40), MI["Hull"])                # the cabin light's switch box (MPU.cpp)
    box(B, (0.54, 2.33, DE + 1.12), (0.80, 2.43, DE + 1.48), MI["Red"])                     # the emergency kit (hull patches)
    box(B, (0.58, 2.325, DE + 1.38), (0.76, 2.33, DE + 1.44), MI["White"])
    cyl(B, (-0.65, 2.38, DE + 1.30), (-0.65, 2.33, DE + 1.33), 0.012, MI["Metal"], 8)
    box(B, (-0.73, 2.375, DE + 1.36), (-0.57, 2.38, DE + 1.38), MI["Red"])
    # the stern: its end wall with the outer door (opens inwards), a port, the handle; the stairs below (stowed under the deck
    # would be the chassis' business - here out, parked)
    yd = 4.25
    add_poly(B, list(lock[-1]), MI["White"]); add_poly(B, list(reversed(lin[-1])), MI["Lining"])
    # the outer hatch, both faces: an octagonal coaming in warning yellow, the leaf with its bolt ring, the locking wheel,
    # the port, two heavy hinges (left from outside); a lamp and a small control box beside it outside
    e_outer_hatch(B, yd)
    box(B, (-0.15, yd, DE + 1.86), (0.15, yd + 0.06, DE + 1.91), MI["Lamp"])
    box(B, (-0.78, yd, DE + 0.95), (-0.60, yd + 0.07, DE + 1.25), MI["Hull"]); box(B, (-0.75, yd + 0.07, DE + 1.15), (-0.63, yd + 0.075, DE + 1.22), MI["Red"])
    for k in (range(5) if HSTYLE != 3 else ()):
        z = DE - 0.05 - k * 0.30; yy = yd + 0.10 + k * 0.27
        box("Ladder", (-0.45, yy, z - 0.04), (0.45, yy + 0.28, z), MI["Metal"])
    for sx in ((-0.48, 0.48) if HSTYLE != 3 else ()):
        a, b = Vector((sx, yd + 0.05, DE)), Vector((sx, yd + 1.45, 0.10))
        cyl("Ladder", a, b, 0.03, MI["Metal"], 8)
        for q in (a, b): e_joint("Ladder", q, 0.03, MI["Metal"])                          # the rails' ends capped
    # the exoskeleton: ribs over the hull every 1.2 m, rails along; the bays hang on it over the fenders
    for yy in (-2.40, -1.20, 0.0, 1.20, 2.40):
        pts = [Vector((-2.06, yy, 2.18)), Vector((-1.60, yy, 3.38)), Vector((-1.05, yy, 3.70)), Vector((1.05, yy, 3.70)), Vector((1.60, yy, 3.38)), Vector((2.06, yy, 2.18))]
        for a, b in zip(pts, pts[1:]): cyl(B, a, b, 0.07, MI["Hull"], 8)
        for q in pts: e_joint(B, q, 0.07, MI["Hull"])
        for sx in (-1, 1): cyl(B, (sx * 2.06, yy, 2.18), (sx * 1.47, yy, DE + 0.02), 0.06, MI["Hull"], 8)
    for x, z in ((-1.05, 3.70), (1.05, 3.70), (-2.06, 2.18), (2.06, 2.18)): cyl(B, (x, -2.6, z), (x, 2.6, z), 0.055, MI["Hull"], 8)
    for sx in (-1, 1): e_bay(B, sx, -2.45, 2.35, 2.20, 2.95)
    # the roof: the radiators stowed flat on the rails (they rise to the sky when parked - later), the solar panel, the mast
    for sx in (-1, 1): box(B, (min(sx * 0.15, sx * 1.40), -2.3, 3.74), (max(sx * 0.15, sx * 1.40), 2.2, 3.77), MI["Lining"])
    box(B, (-0.95, -2.2, 3.78), (0.95, 2.1, 3.81), MI["Glass"])
    cyl(B, (0.6, -2.0, 3.72), (0.6, -2.0, 4.25), 0.035, MI["Metal"], 8); cyl(B, (0.6, -2.0, 4.25), (0.6, -2.12, 4.50), 0.24, MI["White"], 20)
    box(B, (-0.9, -3.30, 3.55), (0.9, -3.18, 3.65), MI["Hull"])
    for x in (-0.75, -0.25, 0.25, 0.75): box(B, (x - 0.12, -3.31, 3.57), (x + 0.12, -3.30, 3.63), MI["Lamp"])
    # ---- inside ----
    box(B, (-0.88, -3.88, DE + 0.04), (0.88, -3.62, DE + 0.40), MI["Hull"]); box(B, (-1.18, -3.88, DE + 0.40), (1.18, -3.62, DE + 0.78), MI["Hull"])   # the console
    P = [Vector((-1.25, -3.62, DE + 0.78)), Vector((1.25, -3.62, DE + 0.78)), Vector((1.15, -3.90, DE + 1.00)), Vector((-1.15, -3.90, DE + 1.00))]
    add_poly(B, P, MI["Hull"]); add_poly(B, P[::-1], MI["Hull"])
    e_terminal(B)
    for x, y in E_SEATS: e_seat(B, x, y)
    H = Vector(E_HUB); YXb, YYb, YZb = Vector((-1, 0, 0)), Vector((0, -0.6053, 0.7960)), Vector((0, 0.7960, 0.6053))   # the Tantra yoke's axes, blender
    cyl(B, H, H + Vector((0, -0.36, -0.27)), 0.035, MI["Metal"], 12)                         # the yoke's column into the console
    Y = "Yoke"                                                                                # the turning part: hub, arms, horns
    cyl(Y, H + YZb * 0.03, H - YZb * 0.05, 0.07, MI["Metal"], 20)
    for sxx in (-1, 1):
        gt = H + YXb * (sxx * 0.243) + YYb * -0.058; gb = H + YXb * (sxx * 0.268) + YYb * -0.168
        cyl(Y, H, H + YXb * (sxx * 0.20), 0.022, MI["Metal"], 10); cyl(Y, H + YXb * (sxx * 0.20), gt, 0.022, MI["Metal"], 10)
        cyl(Y, gt, gb, 0.021, MI["Seat"], 12)
    # the yoke's screen (MPU.cpp draws it, 256 x 160) in a bezel on the hub, four buttons under it: headlights, the platform,
    # the ladder, the parking brake - all turning with the yoke
    sc = H + YZb * 0.045
    obox(Y, H + YZb * 0.03, YXb, YYb, YZb, 0.25, 0.20, 0.03, MI["Hull"])
    Q = [sc - YXb * 0.10 + YYb * 0.065 - YYb * 0.12, sc + YXb * 0.10 + YYb * 0.065 - YYb * 0.12, sc + YXb * 0.10 + YYb * 0.065, sc - YXb * 0.10 + YYb * 0.065]
    nn = (Q[1] - Q[0]).cross(Q[2] - Q[0])
    add_poly("YokeScr", Q if nn.dot(YZb) > 0 else Q[::-1], MI["YScr"])
    top_ = sc + YYb * 0.065
    UVMAP["YokeScr"] = lambda q: ((q - (top_ - YXb * 0.10)).dot(YXb) / 0.20, (top_ - q).dot(YYb) / 0.12)
    for i in range(4):
        obox(Y, H + YYb * -0.08 + YXb * (-0.075 + i * 0.05) + YZb * 0.05, YXb, YYb, YZb, 0.038, 0.026, 0.016, MI["Red"] if i == 3 else MI["Metal"])
    for y0, y1 in ((-2.30, -0.20), (0.00, 2.10)):                                          # bunks, right (blender -x), two tiers
        for z in (DE + 0.25, DE + 1.05):
            box(B, (-1.38, y0, z), (-0.80, y1, z + 0.10), MI["Lining"]); box(B, (-1.36, y0 + 0.02, z + 0.10), (-0.82, y1 - 0.02, z + 0.22), MI["Seat"])
        for y in (y0, y1): box(B, (-0.84, y - 0.03, DE), (-0.78, y + 0.03, DE + 1.40), MI["Metal"])
    box(B, (0.80, -2.20, DE), (1.38, -1.40, DE + 0.48), MI["Seat"]); box(B, (0.80, -0.30, DE), (1.38, 0.50, DE + 0.48), MI["Seat"])   # benches, left
    box(B, (0.75, -1.20, DE + 0.70), (1.38, -0.50, DE + 0.76), MI["Lining"]); cyl(B, (1.05, -0.85, DE), (1.05, -0.85, DE + 0.70), 0.05, MI["Metal"], 10)   # the table
    box(B, (0.85, 1.00, DE), (1.38, 2.35, DE + 0.92), MI["Lining"]); box(B, (0.85, 1.00, DE + 1.45), (1.38, 2.35, DE + 1.80), MI["Lining"])   # the galley
    for k in range(3): box(B, (0.845, 1.05 + k * 0.43, DE + 0.10), (0.85, 1.42 + k * 0.43, DE + 0.85), MI["Seam"])
    box(B, (-0.25, -2.9, 3.43), (0.25, 2.3, 3.46), MI["LampIn"])                            # the ceiling light strip
    box(B, (-0.20, 2.9, 3.43), (0.20, 3.9, 3.46), MI["LampIn"])                              # the airlock's
    x = -1.05
    for y in (3.15, 3.80):                                                                   # the suits on stands along the right wall
        cyl(B, (x - 0.22, y, DE), (x - 0.22, y, DE + 1.60), 0.04, MI["Metal"], 8)
        cyl(B, (x, y, DE + 0.85), (x, y, DE + 1.50), 0.24, MI["White"], 16); cyl(B, (x, y, DE + 1.50), (x, y, DE + 1.72), 0.16, MI["White"], 16)
        for dy in (-0.1, 0.1): cyl(B, (x, y + dy, DE + 0.08), (x, y + dy, DE + 0.85), 0.08, MI["White"], 10)

def post():
    box(TOP, (-0.45, -4.15, 1.42), (0.45, -3.85, 2.30), MI["Hull"])
    add_poly(TOP, [Vector((-0.42, -3.85, 2.30)), Vector((0.42, -3.85, 2.30)), Vector((0.42, -4.12, 2.42)), Vector((-0.42, -4.12, 2.42))], MI["Glass"])
    box(TOP, (-0.45, -4.15, 2.30), (0.45, -4.12, 2.44), MI["Hull"])
    cyl(TOP, (0.22, -3.78, 2.20), (0.22, -3.62, 2.42), 0.018, MI["Metal"], 10)                          # joystick
    cyl(TOP, (0.22, -3.62, 2.42), (0.22, -3.60, 2.50), 0.035, MI["Hull"], 12)
    box(TOP, (0.12, -3.85, 2.12), (0.32, -3.62, 2.20), MI["Hull"])
    hoop = [Vector((-0.75 * math.cos(math.pi * k / 12), -3.25 - 0.55 * math.sin(math.pi * k / 12), 2.35)) for k in range(13)]
    for p, q in zip(hoop, hoop[1:]): cyl(TOP, p, q, 0.03, MI["Metal"], 8)
    for sx in (-1, 1): cyl(TOP, (sx * 0.75, -3.25, 1.42), (sx * 0.75, -3.25, 2.35), 0.03, MI["Metal"], 8)
    cyl(TOP, (0.9, -3.9, 1.42), (0.9, -3.9, 2.9), 0.02, MI["Metal"], 8); cyl(TOP, (0.9, -3.9, 2.9), (0.9, -3.9, 2.97), 0.07, MI["Lamp"], 12)   # beacon mast

try:
    # ---------------- body (chassis v2): backbone, deck frame, locks, battery trays, bumpers ----------------
    B = "Body"
    st = [(-4.25, 0.32, 0.72, 1.20, 0.12), (-3.9, 0.45, 0.65, 1.30, 0.14), (3.9, 0.45, 0.65, 1.30, 0.14), (4.25, 0.32, 0.72, 1.20, 0.12)]
    loft(B, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in st], MI["Hull"], smooth=False)
    for yy in (-3.0, -1.0, 1.0, 3.0):
        loft(B, [oct_ring(yy - 0.01, 0.455, 0.645, 1.305, 0.14), oct_ring(yy + 0.01, 0.455, 0.645, 1.305, 0.14)], MI["Seam"], smooth=False)
    box(B, (-0.50, -4.0, 0.62), (0.50, 4.0, 0.65), MI["Metal"])                                    # skid plate
    for y0 in AXLES:                                                                                 # pivot brackets
        for sx in (-1, 1): box(B, (sx * 0.45, y0 - 0.36, 0.52), (sx * 0.62, y0 + 0.36, 1.26), MI["Metal"])
    for y in (-4.25, -2.45, -0.85, 0.85, 2.45, 4.25):                                                # deck frame cross tubes, posts
        box(B, (-1.40, y - 0.06, 1.47), (1.40, y + 0.06, 1.59), MI["Hull"])
        for sx in (-1, 1): cyl(B, (sx * 0.42, y, 1.25), (sx * 1.30, y, 1.47), 0.05, MI["Hull"], 10)
    for sx in (-1, 1): box(B, (sx * 1.40 - 0.07, -4.30, 1.45), (sx * 1.40 + 0.07, 4.30, 1.60), MI["Hull"])
    for y0, y1 in ((-4.2, -2.55), (-2.35, -0.95), (-0.75, 0.75), (0.95, 2.35), (2.55, 4.2)):
        box(B, (-1.30, y0, 1.60), (1.30, y1, 1.62), MI["Deck"])
    for sx in (-1, 1):
        for y in (-4.2, -2.45, 0.0, 2.45, 4.2): box(B, (sx * 1.40 - 0.09, y - 0.09, 1.60), (sx * 1.40 + 0.09, y + 0.09, 1.70), MI["Red"])   # twist-locks
        box(B, (sx * 0.47, -0.95, 0.75), (sx * 1.25, 0.95, 1.30), MI["Hull"])                     # battery trays
        for k in range(4): box(B, (sx * 1.25, -0.85 + k * 0.45, 0.80), (sx * 1.26, -0.50 + k * 0.45, 1.26), MI["Seam"])
        if sx < 0:                                                    # the right side (orbiter +x): the four energy cells' hatches
            for k in range(4):
                yc = -0.675 + k * 0.45
                box(B, (-1.265, yc - 0.15, 0.82), (-1.255, yc + 0.15, 1.24), MI["Hull"])           # the hatch
                box(B, (-1.30, yc - 0.06, 1.08), (-1.265, yc + 0.06, 1.12), MI["Red"])             # its handle
    for sy in (-1, 1):                                                                               # bumpers, folded couplings, lamps
        y = sy * 4.35
        box(B, (-1.25, y - 0.10, 0.95), (1.25, y + 0.10, 1.40), MI["Hull"])
        for sx in (-1, 1):
            cyl(B, (sx * 1.25, y, 1.15), (sx * 1.38, sy * 4.10, 1.50), 0.06, MI["Hull"], 10)
            box(B, (sx * 0.95 - 0.22, y + sy * 0.10, 1.22), (sx * 0.95 + 0.22, y + sy * 0.12, 1.30), MI["Lamp"])
        for dx in (-0.16, 0.16): box(B, (dx - 0.03, y + sy * 0.10, 1.00), (dx + 0.03, y + sy * 0.26, 1.12), MI["Metal"])
        pin = Vector((0, y + sy * 0.20, 1.06)); dd = Vector((0, sy * 0.15, 1.0)).normalized()
        cyl(B, pin - Vector((0.2, 0, 0)), pin + Vector((0.2, 0, 0)), 0.04, MI["Metal"], 10)
        cyl(B, pin, pin + dd * 0.32, 0.07, MI["Red"], 14); cyl(B, pin + dd * 0.32, pin + dd * 0.44, 0.15, MI["Red"], 20)
        for dx in (-0.55, 0.55): cyl(B, (dx, y + sy * 0.10, 1.15), (dx, y + sy * 0.24, 1.15), 0.07, MI["Metal"], 14)
    cyl(B, (0, -4.35, 1.40), (0, -4.35, 1.58), 0.05, MI["Metal"], 10); cyl(B, (0, -4.35, 1.58), (0, -4.35, 1.70), 0.10, MI["Hull"], 20)   # lidar
    cyl(B, (0.75, -4.45, 1.10), (0.75, -4.67, 1.10), 0.10, MI["Red"], 16)                           # the winch's fairlead
    if VAR == "MPU":
        for sx in (-1, 1): box(TOP, (sx * 1.50 - 0.05, -3.0, 1.42), (sx * 1.50 + 0.05, 4.1, 1.62), MI["Hull"])   # low side coaming
        box(TOP, (-1.0, -4.2, 1.35), (1.0, -3.0, 1.42), MI["Deck"])                                 # the post's floor plate
        post()
    else:
        empu_c()
    # fenders: fixed to the frame, one over each pair of wheels (front pair, rear pair), clear of the wheels at full bump
    # (top of the tyre up to 2.05 m) and at full lock; brackets down to the deck frame; rubber mud flaps behind each pair
    FZ = 2.14                                                         # the fender's underside
    for sx in (-1, 1):
        for ya, yb in ((-4.30, -0.70), (0.70, 4.30)):                 # the pairs' extent along the chassis
            xi, xo = sx * 1.74, sx * 2.32                              # inner / outer edges
            prof = [(ya, FZ - 0.42), (ya + 0.45, FZ), (yb - 0.45, FZ), (yb, FZ - 0.42)]   # sloped ends, flat over the wheels
            T = 0.07                                                   # a composite panel 7 cm thick
            for (y0, z0), (y1, z1) in zip(prof, prof[1:]):
                for zz in (0.0, T):                                    # top and under faces
                    Q = [Vector((xi, y0, z0 + zz)), Vector((xo, y0, z0 + zz)), Vector((xo, y1, z1 + zz)), Vector((xi, y1, z1 + zz))]
                    add_poly(B, Q if (zz > 0) == (sx > 0) else Q[::-1], MI["Hull"])
                Q = [Vector((xi, y0, z0)), Vector((xi, y1, z1)), Vector((xi, y1, z1 + T)), Vector((xi, y0, z0 + T))]           # inner edge
                add_poly(B, Q, MI["Hull"]); add_poly(B, Q[::-1], MI["Hull"])
                # the outer edge rolled down: a turned-down lip with a round bead along its bottom
                Q = [Vector((xo, y0, z0 - 0.14)), Vector((xo, y1, z1 - 0.14)), Vector((xo, y1, z1 + T)), Vector((xo, y0, z0 + T))]
                add_poly(B, Q, MI["Hull"]); add_poly(B, Q[::-1], MI["Hull"])
                Q = [Vector((xo - sx * 0.05, y0, z0 - 0.14)), Vector((xo - sx * 0.05, y1, z1 - 0.14)), Vector((xo - sx * 0.05, y1, z1)), Vector((xo - sx * 0.05, y0, z0))]
                add_poly(B, Q, MI["Hull"]); add_poly(B, Q[::-1], MI["Hull"])
                cyl(B, Vector((xo - sx * 0.025, y0, z0 - 0.14)), Vector((xo - sx * 0.025, y1, z1 - 0.14)), 0.035, MI["Seam"], 8)
            for yr in [ya + 0.45 + k * (yb - ya - 0.9) / 4 for k in range(5)]:   # ribs under the flat top (stiffness)
                box(B, (min(xi, xo) + 0.04, yr - 0.025, FZ - 0.09), (max(xi, xo) - 0.06, yr + 0.025, FZ), MI["Metal"])
            for yy in (ya + 0.55, 0.5 * (ya + yb), yb - 0.55):        # brackets from the deck frame
                cyl(B, (sx * 1.42, yy, 1.55), (sx * 1.90, yy, FZ - 0.01), 0.04, MI["Metal"], 8)
                box(B, (sx * 1.40 - 0.05, yy - 0.05, 1.50), (sx * 1.47 + 0.0, yy + 0.05, 1.60), MI["Metal"])
        for yf in (-0.66, 4.34):                                       # mud flaps behind each pair
            Q = [Vector((sx * 1.60, yf, 0.42)), Vector((sx * 2.28, yf, 0.42)), Vector((sx * 2.28, yf, FZ - 0.40)), Vector((sx * 1.60, yf, FZ - 0.40))]
            add_poly(B, Q, MI["Tread"]); add_poly(B, Q[::-1], MI["Tread"])
            box(B, (sx * 1.60 - 0.01, yf - 0.03, FZ - 0.48), (sx * 2.28 + 0.01, yf + 0.03, FZ - 0.40), MI["Metal"])
    # steps on the left (orbiter -x), between the axles: their own part, built deployed (treads 0.36 .. 1.32, the deck at
    # 1.62); driving, they slide up 0.45 m and in 0.20 m under the deck's side (STEPS_STOW, in the header)
    for k in range(4):
        box("Steps", (1.55, -0.4, 0.36 + k * 0.32), (1.90, 0.4, 0.41 + k * 0.32), MI["Metal"])
    for yy in (-0.42, 0.42): box("Steps", (1.55, yy - 0.03, 0.30), (1.90, yy + 0.03, 1.42), MI["Metal"])
    # the tow bar (drawn by the module from this vehicle's rear hitch to the follower's front hitch): two tubes along t = 0..1,
    # stored with t in blender -y (orbiter z) and the tube's offset in x / z; collapsed to a point while uncoupled
    SEGB = 8
    for dx in (-0.12, 0.12):
        rings = [[Vector((dx + 0.045 * math.cos(2 * math.pi * j / SEGB), -t, 0.045 * math.sin(2 * math.pi * j / SEGB))) for j in range(SEGB)] for t in (0.0, 1.0)]
        loft("TowBar", rings, MI["Red"], cap=False, smooth=True)
    # ---------------- the wheel modules: double wishbones, knuckle (with the hub motor and the fender), wheel ----------------
    geo = []
    for i, yw in enumerate(AXLES):
        for sx in (-1, 1):
            idx = len(geo)
            hub = Vector((sx * HUB_X, yw, WHEEL_R))
            Pl = Vector((sx * P_LOW[0], yw, P_LOW[1])); Pu = Vector((sx * P_UP[0], yw, P_UP[1]))
            kL = Vector((sx * K_LOW[0], yw, K_LOW[1])); kU = Vector((sx * K_UP[0], yw, K_UP[1]))
            lo, up, kn, wn = "Low%d" % idx, "Up%d" % idx, "Kn%d" % idx, "Wheel%d" % idx
            for dy in (-0.32, 0.32): cyl(lo, Pl + Vector((0, dy, 0)), kL, 0.048, MI["Metal"], 10)
            cyl(lo, Pl + Vector((0, -0.32, 0)), Pl + Vector((0, 0.32, 0)), 0.05, MI["Metal"], 10)
            s0 = Pl + (kL - Pl) * 0.66 + Vector((0, 0.20, 0.05)); s1 = Vector((sx * 0.95, yw + 0.20, 1.45))
            cyl(lo, s0, s0 + (s1 - s0) * 0.55, 0.04, MI["Metal"], 10)                               # the strut's rod (with the arm)
            cyl(B, s0 + (s1 - s0) * 0.45, s1, 0.065, MI["Hull"], 12)                                 # its body (on the frame)
            box(B, (s1.x - 0.09, s1.y - 0.09, s1.z - 0.10), (s1.x + 0.09, s1.y + 0.09, s1.z), MI["Red"])
            for dy in (-0.24, 0.24): cyl(up, Pu + Vector((0, dy, 0)), kU, 0.04, MI["Metal"], 10)
            for q in (kL, kU): cyl(kn, q - Vector((0, 0.05, 0)), q + Vector((0, 0.05, 0)), 0.06, MI["Hull"], 12)
            box(kn, (kL.x - 0.06, yw - 0.09, kL.z), (kL.x + 0.06, yw + 0.09, kU.z), MI["Hull"])
            cyl(kn, kU + Vector((0, -0.08, -0.12)), kU + Vector((0, -0.28, -0.12)), 0.03, MI["Hull"], 8)   # steering arm
            cyl(B, Pu + Vector((0, -0.30, -0.06)), Pu + Vector((sx * 0.35, -0.30, -0.10)), 0.05, MI["Hull"], 12)   # steering actuator
            cyl(kn, hub + Vector((sx * -0.30, 0, 0)), hub + Vector((sx * -0.20, 0, 0)), 0.37, MI["Hull"], 32)   # motor flange
            box(kn, (hub.x - 0.05 - sx * 0.26, yw - 0.10, WHEEL_R + 0.27), (hub.x + 0.05 - sx * 0.26, yw + 0.10, WHEEL_R + 0.40), MI["Red"])   # brake
            wheel(wn, hub, sx)
            geo.append(tuple(to_orb(v) for v in (hub, Pl, Pu, kL, kU)))
    # ---------------- export: groups per (part, material) ----------------
    for mi_ in parts.get("Top", {}):                                                                 # the post / cabin up with the deck
        vs_, fs_ = parts["Top"][mi_]
        parts["Top"][mi_] = ([(pp + Vector((0, 0, DZD)), nn) for pp, nn in vs_], fs_)
    names = (["Low%d" % i for i in range(8)] + ["Up%d" % i for i in range(8)] + ["Kn%d" % i for i in range(8)] +
             ["Wheel%d" % i for i in range(8)] + ["Steps", "TowBar", "Body", "Top", "Yoke", "TermScr", "YokeScr", "Ladder"])
    groups, gidx = [], {}
    for pn in names:
        for mi in sorted(parts.get(pn, {})):
            gidx.setdefault(pn, []).append(len(groups)); groups.append((pn, mi, parts[pn][mi]))
    os.makedirs(os.path.dirname(MESH_OUT), exist_ok=True)
    # textures (Tantra_Design/tools/make_mpu_textures.py; D3D9Client adds the _norm / _spec maps itself): material -> (file, metres
    # per tile); a planar projection on each vertex's normal (the dominant axis), in the rest pose - moving parts keep theirs
    TEX = {"Hull": ("MPU\\mpu_composite.dds", 1.0), "Deck": ("MPU\\mpu_deck.dds", 1.0), "Metal": ("MPU\\mpu_metal.dds", 0.5),
           "Blade": ("MPU\\mpu_metal.dds", 0.5), "Tread": ("MPU\\mpu_rubber.dds", 0.5), "White": ("MPU\\mpu_white.dds", 1.0),
           "Lining": ("MPU\\mpu_lining.dds", 1.0), "Seat": ("MPU\\mpu_seat.dds", 0.5),
           "Term": ("MPU\\empu_term.dds", 0.0), "YScr": ("MPU\\empu_yoke.dds", 0.0)}   # drawn by MPU.cpp at run time
    TEXLIST = sorted({t for t, _ in TEX.values()})
    def uv(p, n, tile):
        a = max(range(3), key=lambda k: abs(n[k]))
        u, v = (p.y, p.z) if a == 0 else (p.x, p.z) if a == 1 else (p.x, p.y)
        return u / tile, -v / tile
    with open(MESH_OUT, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(groups))
        for pn, mi, (vs, fs) in groups:
            tx = TEX.get(MATS[mi][0])
            f.write("LABEL %s_%s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (pn, MATS[mi][0], mi + 1, TEXLIST.index(tx[0]) + 1 if tx else 0, len(vs), len(fs)))
            for p, n in vs:
                q = to_orb(p); m = Vector((-n.x, n.z, -n.y))
                if tx and pn in UVMAP:
                    tu, tv = UVMAP[pn](p)
                    f.write("%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n" % (q.x, q.y, q.z, m.x, m.y, m.z, tu, tv))
                elif tx:
                    tu, tv = uv(p, n, tx[1])
                    f.write("%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n" % (q.x, q.y, q.z, m.x, m.y, m.z, tu, tv))
                else:
                    f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (q.x, q.y, q.z, m.x, m.y, m.z))
            for a, b, c in fs: f.write("%d %d %d\n" % (a, c, b))
        f.write("MATERIALS %d\n" % len(MATS))
        for n, _, _ in MATS: f.write("MPU_%s\n" % n)
        for n, col, em in MATS:
            spec = 0.95 if n == "Window" else 0.6 if n in ("Metal", "Glass", "Blade") else 0.15
            a_ = ALPHA.get(n, 1.0)
            if n in TEX: col = (1.0, 1.0, 1.0)                       # the texture carries the colour
            f.write("MATERIAL MPU_%s\n%.3f %.3f %.3f %.2f\n%.3f %.3f %.3f %.2f\n%.2f %.2f %.2f 1 %d\n%.3f %.3f %.3f 1\n" %
                    (n, *col, a_, *col, a_, spec, spec, spec, 90 if n == "Window" else 40 if spec > 0.3 else 10, *(c * em for c in col)))
        f.write("TEXTURES %d\n" % len(TEXLIST))
        for t in TEXLIST: f.write("%s\n" % t)
    # ---------------- header for the module ----------------
    os.makedirs(os.path.dirname(GEO_OUT), exist_ok=True)
    if VAR == "EMPU":                                       # Э.МПУ: the yoke's groups (it turns with the steering, MPU.cpp)
        with open(os.path.join(os.path.dirname(GEO_OUT), "EmpuGeo.h" if not HV else "mock_empugeo.h"), "w", encoding="utf-8", newline="\n") as f:
            yg = gidx["Yoke"] + gidx.get("YokeScr", [])
            f.write("// Written by Tantra_Design/blender/build_mpu.py (VARIANT=EMPU) - do not edit.\n#pragma once\n\nnamespace empu {\n")
            f.write("constexpr int kLampInMat = %d;   // the cabin's lamps (0-based material): off / standby red / full\n" % MI["LampIn"])
            f.write("constexpr int kTermTex = %d, kYokeScrTex = %d;   // 1-based mesh textures replaced by the drawn surfaces\n" % (TEXLIST.index("MPU\\empu_term.dds") + 1, TEXLIST.index("MPU\\empu_yoke.dds") + 1))
            tr, tu_, tn = term_axes(); to = lambda v: (-v.x, v.z, -v.y)
            f.write("constexpr double kTermC[3] = {%.4f, %.4f, %.4f}, kTermR[3] = {%.4f, %.4f, %.4f}, kTermU[3] = {%.4f, %.4f, %.4f}, kTermN[3] = {%.4f, %.4f, %.4f};\n" %
                    (*to_orb(TERM_C), *to(tr), *to(tu_), *to(tn)))
            f.write("constexpr double kTermSW = %.3f, kTermSH = %.3f, kTermRecess = 0.06;   // the screen; the keys at +-(SW/2 + 0.065) across, 0.16 - k*0.08 up\n" % (TERM_SW, TERM_SH))
            lg = gidx.get("Ladder", [])
            f.write("constexpr int kLadderGrp[4] = {%s};\nconstexpr int kLadderN = %d;\nconstexpr double kLadderHinge[3] = {0.0, %.3f, %.3f};   // folds about x\n" %
                    (", ".join(map(str, lg + [0] * (4 - len(lg)))), len(lg), DE - 0.03 - CG_H, -(4.25 + 0.05)))
            f.write("constexpr int kYokeGrp[8] = {%s};\nconstexpr int kYokeN = %d;\n}  // namespace empu\n" % (", ".join(map(str, yg + [0] * (8 - len(yg)))), len(yg)))
    with open(GEO_OUT if VAR == "MPU" else os.devnull, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Written by Tantra_Design/blender/build_mpu.py - do not edit. Orbiter vessel frame (x right, y up, z forward).\n#pragma once\n\n")
        f.write("namespace mpu {\nconstexpr double kCgH = %.3f, kWheelR = %.3f, kWheelW = %.3f;\nconstexpr int kWheels = 8;\n" % (CG_H, WHEEL_R, WHEEL_W))
        f.write("// hub, the wishbones' inner pivots (lower, upper) and outer ball joints (lower, upper); the groups of each moving part\n")
        f.write("struct WheelGeo { double hub[3], pl[3], pu[3], kl[3], ku[3]; int lowGrp[4]; int nLow; int upGrp[4]; int nUp; int knGrp[8]; int nKn; int wheelGrp[8]; int nWheel; };\n")
        f.write("constexpr WheelGeo kWheel[kWheels] = {\n")
        def g_(nm, n): g = gidx[nm]; return "{%s}, %d" % (", ".join(map(str, g + [0] * (n - len(g)))), len(g))
        for i, vv in enumerate(geo):
            f.write("    {" + ", ".join("{%.4f, %.4f, %.4f}" % (v.x, v.y, v.z) for v in vv) + ", %s, %s, %s, %s},\n" %
                    (g_("Low%d" % i, 4), g_("Up%d" % i, 4), g_("Kn%d" % i, 8), g_("Wheel%d" % i, 8)))
        f.write("};\nconstexpr double kDriverEye[3] = {%.3f, %.3f, %.3f};\n" % tuple(to_orb(Vector((0.0, -3.30, 1.62 + 1.62)))))
        sg = gidx["Steps"]
        f.write("constexpr int kStepsGrp[4] = {%s};\nconstexpr int kStepsN = %d;\n" % (", ".join(map(str, sg + [0] * (4 - len(sg)))), len(sg)))
        f.write("constexpr double kStepsStow[3] = {0.200, 0.450, 0.000};   // stowed: their offset from the deployed pose\n")
        f.write("constexpr int kTowBarGrp = %d;   // the tow bar: vertex z (orbiter) = -t (0..1), x / y the tube's offset\n" % gidx["TowBar"][0])
        f.write("constexpr double kHitchF[3] = {0.0, 0.060, 4.550}, kHitchR[3] = {0.0, 0.060, -4.550};   // the coupling pins\n}  // namespace mpu\n")
    log.append("groups %d, verts %d" % (len(groups), sum(len(v) for _, _, (v, f) in groups)))
    # ---------------- preview with her for scale ----------------
    if os.environ.get("MPU_PREVIEW", "1") == "1":
        for o in bpy.data.objects:
            if o.name.startswith("Jet"): o.hide_render = True
        bm = bmesh.new(); mats = []
        for n, col, em in MATS:
            m = bpy.data.materials.new("MPUp_" + n); m.use_nodes = True
            bs = next(x for x in m.node_tree.nodes if x.type == 'BSDF_PRINCIPLED')
            bs.inputs["Base Color"].default_value = (*col, 1); bs.inputs["Roughness"].default_value = 0.4 if n in ("Metal", "Glass") else 0.6
            bs.inputs["Metallic"].default_value = 0.7 if n in ("Metal", "Blade") else 0.2; mats.append(m)
        OFF = Vector((3.0, 6.9, 0))
        for pn, mi, (vs, fs) in groups:
            base = [bm.verts.new(p + OFF) for p, n in vs]
            for a, b, c in fs:
                try: bm.faces.new((base[a], base[b], base[c])).material_index = mi
                except ValueError: pass
        me = bpy.data.meshes.new("MPU"); bm.to_mesh(me); bm.free()
        for m in mats: me.materials.append(m)
        ob = bpy.data.objects.new("MPU", me); bpy.context.scene.collection.objects.link(ob)
        sys.path.insert(0, HERE); import render_util
        cam = render_util.setup_stage()
        fl = bpy.data.objects.get("Floor")
        if fl: fl.scale = (8, 8, 1)
        sun = bpy.data.lights.new("MSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
        so = bpy.data.objects.new("MSun", sun); bpy.context.scene.collection.objects.link(so)
        so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
        sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 300
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = 13.0
        cam.location = (-60, 5.9, 1.9); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "%s_test_side.png" % VAR.lower()); bpy.ops.render.render(write_still=True)
        cam.data.type = 'PERSP'; cam.data.lens = 35
        loc = Vector((-9.5, -6.5, 5.0)); tgt = Vector((OFF.x, OFF.y - 1.0, 1.4))
        cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "%s_test_34.png" % VAR.lower()); bpy.ops.render.render(write_still=True)
        if VAR == "EMPU":                                       # close-ups: the nose outside, the cockpit, the door to the airlock
            pl = bpy.data.lights.new("MIn", 'POINT'); pl.energy = 400; pl.shadow_soft_size = 1.0
            po = bpy.data.objects.new("MIn", pl); bpy.context.scene.collection.objects.link(po); po.location = OFF + Vector((0, -1.0, 3.2))
            shots = ()
            if os.environ.get("MPU_SCREENS") == "1":                # the screens' placement mock-up (renders only, not in the mesh)
                sm = bpy.data.materials.new("Scr"); sm.use_nodes = True
                bs = next(x for x in sm.node_tree.nodes if x.type == 'BSDF_PRINCIPLED')
                bs.inputs["Base Color"].default_value = (0.02, 0.06, 0.05, 1)
                bs.inputs["Emission Color"].default_value = (0.25, 0.85, 0.65, 1); bs.inputs["Emission Strength"].default_value = 1.5
                tm = bpy.data.materials.new("ScrT"); tm.use_nodes = True
                bt = next(x for x in tm.node_tree.nodes if x.type == 'BSDF_PRINCIPLED')
                bt.inputs["Emission Color"].default_value = (1.0, 0.75, 0.2, 1); bt.inputs["Emission Strength"].default_value = 4.0
                def screen(nm, P, label):
                    P = [Vector(p) + OFF for p in P]
                    me = bpy.data.meshes.new(nm); me.from_pydata(P, [], [(0, 1, 2, 3)]); me.materials.append(sm)
                    o = bpy.data.objects.new(nm, me); bpy.context.scene.collection.objects.link(o)
                    n = (P[1] - P[0]).cross(P[2] - P[0]).normalized(); c = sum(P, Vector()) / 4
                    cu = bpy.data.curves.new(nm + "T", 'FONT'); cu.body = label; cu.size = 0.07; cu.align_x = 'CENTER'; cu.materials.append(tm)
                    to = bpy.data.objects.new(nm + "T", cu); bpy.context.scene.collection.objects.link(to)
                    to.rotation_mode = 'QUATERNION'; to.rotation_quaternion = n.to_track_quat('Z', 'Y'); to.location = c + n * 0.01
                # А: the driver's navigation, upright on the console between the yoke and the middle; Б: the navigator's
                # (comms, systems); В: the big systems screen on the wall at the table; Г: the airlock's, on the bulkhead
                # the console as the user asked (2026-10-07, «Марсианин»): plain instruments with dials, toggle switches and a
                # terminal (a rugged screen on an arm, a keyboard); the trip's data on the person's screen row
                def mat(nm, col, em=0.0, emc=None):
                    m_ = bpy.data.materials.new(nm); m_.use_nodes = True
                    b_ = next(x for x in m_.node_tree.nodes if x.type == 'BSDF_PRINCIPLED'); b_.inputs["Base Color"].default_value = (*col, 1)
                    if em: b_.inputs["Emission Color"].default_value = (*(emc or col), 1); b_.inputs["Emission Strength"].default_value = em
                    return m_
                mFace, mRim, mNeedle = mat("DialF", (0.92, 0.90, 0.84), 0.6), mat("DialR", (0.04, 0.04, 0.045)), mat("Needle", (0.9, 0.25, 0.1), 1.0)
                mPlate, mLever, mTerm = mat("Plate", (0.10, 0.11, 0.12)), mat("Lever", (0.75, 0.76, 0.78)), mat("Term", (0.05, 0.12, 0.06), 1.2, (0.25, 0.55, 0.18))
                mKey, mAmber = mat("Keys", (0.16, 0.16, 0.17)), mat("Amber", (1.0, 0.6, 0.1), 2.0)
                def put(o, at, n, m_):
                    o.location = Vector(at) + OFF; o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector(n).normalized().to_track_quat('Z', 'Y'); o.data.materials.append(m_)
                def cylo(r, d, at, n, m_, v=24):
                    bpy.ops.mesh.primitive_cylinder_add(vertices=v, radius=r, depth=d); put(bpy.context.active_object, at, n, m_); return bpy.context.active_object
                def boxo(sx, sy, sz, at, n, m_):
                    bpy.ops.mesh.primitive_cube_add(size=1.0); o = bpy.context.active_object; o.scale = (sx, sy, sz); put(o, at, n, m_); return o
                sl = Vector((0, 0.618, 0.786))                                           # the console's slope, its normal
                def onslope(x, t, lift): return Vector((x, -3.62 - 0.28 * t, DE + 0.78 + 0.22 * t)) + sl * lift
                # the driver's six dials behind the yoke: speed, power (+/-), charge, cabin pressure, cabin temperature, the inclinometer
                for k, x in enumerate((1.02, 0.86, 0.70, 0.54, 0.38, 0.22)):
                    c_ = onslope(x, 0.5, 0.0)
                    cylo(0.062, 0.03, c_ + sl * 0.015, sl, mRim); cylo(0.052, 0.01, c_ + sl * 0.032, sl, mFace)
                    nd = Vector((math.cos(0.6 + k), 0, math.sin(0.6 + k))); nd = (nd - sl * nd.dot(sl)).normalized()
                    boxo(0.006, 0.04, 0.004, c_ + sl * 0.04 + nd * 0.018, sl, mNeedle).rotation_quaternion = sl.to_track_quat('Z', 'Y') @ __import__('mathutils').Quaternion((0, 0, 1), 0.6 + k)
                # the navigator's side: toggle switch rows with their plates, warning lamps
                for row, t in enumerate((0.25, 0.55, 0.85)):
                    boxo(0.62, 0.07, 0.01, onslope(-0.68, t, 0.006), sl, mPlate)
                    for j in range(9):
                        x = -0.94 + j * 0.065
                        cylo(0.008, 0.035, onslope(x, t, 0.025), sl + Vector((0, -0.4, 0.3)), mLever, 8)
                    for j in range(3): cylo(0.012, 0.01, onslope(-0.30 - j * 0.04, t, 0.012), sl, mAmber, 12)
                # the terminal in the middle, in a box frame, tilted to the driver, the screen set in (MPU_TERM = 1, 2, 3)
                TV = int(os.environ.get("MPU_TERM", "1"))
                def tilt(a): return Vector((0, math.cos(math.radians(a)), math.sin(math.radians(a))))
                def upv(a): return Vector((0, -math.sin(math.radians(a)), math.cos(math.radians(a))))
                def framed(c, a, w, h, depth, rim, recess, hood=0.0, handles=False):
                    """a box frame (rim bars round the opening, a back), the screen recessed into it"""
                    n, u, r_ = tilt(a), upv(a), Vector((1, 0, 0)); c = Vector(c)
                    boxo(w, h, 0.02, c - n * (depth - 0.01), n, mRim)                          # the back
                    for s in (-1, 1):
                        boxo(rim, h, depth, c + r_ * s * (w - rim) / 2 - n * depth / 2, n, mPlate)   # the sides
                        boxo(w, rim, depth, c + u * s * (h - rim) / 2 - n * depth / 2, n, mPlate)    # top and bottom
                    boxo(w - 2 * rim, h - 2 * rim, 0.004, c - n * recess, n, mTerm)            # the screen, set in
                    if hood: boxo(w + 0.02, hood, 0.012, c + u * (h / 2) + n * (hood / 2 - 0.01), u, mPlate)
                    if handles:
                        for s in (-1, 1):
                            for e in (-1, 1): cylo(0.012, 0.06, c + r_ * s * (w / 2 + 0.03) + u * e * (h / 2 - 0.06) + n * 0.02, n, mLever, 10)
                            cylo(0.012, h - 0.12, c + r_ * s * (w / 2 + 0.03) + n * 0.05, u, mLever, 10)
                def keyboard(c, n, w=0.42, d=0.15):
                    n = Vector(n).normalized(); c = Vector(c)
                    boxo(w, d, 0.025, c, n, mKey)
                    u = Vector((0, 0, 1)) - n * n.z; u.normalize(); r_ = Vector((1, 0, 0))
                    for j in range(14):
                        for i in range(4): boxo(0.022, 0.022, 0.01, c + r_ * (-w / 2 + 0.035 + j * (w - 0.07) / 13) + u * (-d / 2 + 0.03 + i * (d - 0.06) / 3) + n * 0.017, n, mPlate)
                if TV == 1:     # set into the console: a low frame box on its front edge, 40 deg; the keyboard on a tray below
                    framed((0.0, -3.72, DE + 0.98), 40, 0.58, 0.40, 0.10, 0.04, 0.04)
                    boxo(0.48, 0.18, 0.02, (0.0, -3.53, DE + 0.70), (0, 0.15, 1), mPlate); keyboard((0.0, -3.53, DE + 0.725), (0, 0.15, 1))
                elif TV == 2:   # a rack box standing on the console, steeper (25 deg), a sun hood, carrying handles; the keyboard pulled out
                    framed((0.0, -3.76, DE + 1.06), 25, 0.62, 0.46, 0.16, 0.05, 0.06, hood=0.10, handles=True)
                    boxo(0.50, 0.20, 0.03, (0.0, -3.50, DE + 0.78), (0, 0.0, 1), mPlate); keyboard((0.0, -3.50, DE + 0.80), (0, 0.0, 1), 0.46, 0.17)
                else:           # a clamshell in a cradle: the keyboard half sunk into the console's slope, the screen half up at 35 deg
                    framed((0.0, -3.82, DE + 1.10), 35, 0.52, 0.34, 0.07, 0.035, 0.03)
                    boxo(0.58, 0.30, 0.06, onslope(0.0, 0.45, -0.01), sl, mPlate); keyboard(onslope(0.0, 0.45, 0.025), sl, 0.46, 0.20)
                    for s in (-1, 1):
                        for t in (0.0, 0.9): boxo(0.05, 0.05, 0.08, onslope(s * 0.29, t, 0.02), sl, mRim)
                # a side panel by the driver's left knee: lights, heating, the radiators, the steps (toggles)
                boxo(0.40, 0.30, 0.02, (1.37, -3.05, DE + 0.95), (-1, 0, 0), mPlate)
                for j in range(5):
                    for i in range(2): cylo(0.008, 0.035, (1.355, -3.20 + j * 0.075, DE + 0.88 + i * 0.13), (-1, 0, 0.6), mLever, 8)
                screen("ScrC", [(1.37, -1.25, DE + 0.85), (1.37, -0.45, DE + 0.85), (1.37, -0.45, DE + 1.33), (1.37, -1.25, DE + 1.33)], "C")
                screen("ScrD", [(0.95, 2.57, DE + 1.05), (0.55, 2.57, DE + 1.05), (0.55, 2.57, DE + 1.45), (0.95, 2.57, DE + 1.45)], "D")
                pl2 = bpy.data.lights.new("MIn2", 'POINT'); pl2.energy = 150
                po2 = bpy.data.objects.new("MIn2", pl2); bpy.context.scene.collection.objects.link(po2); po2.location = OFF + Vector((0, 3.4, 3.2))
                shots = (("scr_cockpit", (0.0, -2.2, 3.15), (0.0, -3.8, 2.45), 20), ("scr_term", (0.25, -2.95, 3.05), (0.0, -3.78, 2.55), 24), ("scr_driver", (0.65, -3.05, 3.12), (0.35, -4.2, 2.45), 16), ("scr_nav", (-0.65, -3.05, 3.12), (-0.4, -4.0, 2.4), 16),
                         ("scr_table", (-0.7, -0.9, 2.95), (1.38, -0.85, 2.65), 16), ("scr_lock", (-0.3, 4.05, 3.05), (0.6, 2.5, 2.7), 14))
            for nm, loc, tgt, lens in shots + (("nose", (-3.0, -6.6, 1.2), (0.0, -4.0, 2.2), 30), ("front", (1.6, -7.5, 3.6), (0.0, -3.6, 2.4), 40), ("cockpit", (0.65, -2.9, 3.20), (0.4, -4.4, 2.6), 16),
                                       ("door", (0.0, -0.6, 2.9), (0.0, 3.0, 2.4), 22), ("lock", (0.4, 2.2, 3.0), (-0.2, 4.2, 2.2), 14), ("rear", (2.6, 9.5, 2.6), (0.0, 4.3, 1.9), 30), ("rearin", (0.0, 2.7, 3.1), (0.0, 4.3, 2.4), 18)):
                loc = Vector(loc) + OFF; tgt = Vector(tgt) + OFF; cam.data.lens = lens; cam.data.clip_start = 0.05
                cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
                bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "empu_%s%s.png" % (nm, ("_t" + os.environ["MPU_TERM"]) if nm.startswith("scr") and "MPU_TERM" in os.environ else ""))
                if os.environ.get("MPU_TERM") and nm not in ("scr_cockpit", "scr_term", "scr_driver"): continue
                if HV and nm not in ("rear", "rearin", "door", "lock"): continue
                if HV: sc.render.filepath = os.path.join(OUT, "empu_hatch%d_%s.png" % (HV, nm))
                bpy.ops.render.render(write_still=True)
        log.append("rendered")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
