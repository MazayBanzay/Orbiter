"""Generate the Tantra mesh (Orbiter .msh, MSHX1) and the animation layout header.

Layout "T9" (Tantra_Design/DESIGN_LOCAL.md 2026-10-02, docs/T9_PLAN.md): the T8 hull «Б» of rounded-polygon sections
(flat flanks, lower chines, top slopes, flat bottom) with a 9 m insert at s 88 (argon charges), iridium nose on a blunted
ogive with two retro anamezon cups, armoured shoulder with the dorsal spine, telescopic dorsal fin, two-panel wings with
elevons, body flap, stern clover of four fixed anamezon cups with irises and the marching planetary cup on a sliding
mount in the central well, four planetary pods in flank bays at CG height on telescopic arms, two blade legs («лопасти»)
on trunnion carriages with umbrella feet, four stern legs of four sections with umbrella feet on rails, one kangaroo
leg out of the hangar floor (the third support while lying).

Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane,
z = s + STERN_Z (mesh origin at s = 60). Hull axis 31.4 m above ground while resting level (the shortest blade).

Every opening in the skin is cut from the same (s, u) surface its door or cover is built from,
so closed doors, stowed pads and folded crests are flush by construction (u = perimeter
fraction of a section, 0 = bottom centre, increasing towards starboard).

Every movable part is built in ONE reference pose and moved by Orbiter animations; the rig
(pivots, axes, angles, parents) is written to orbiter2016/MeshLayout.h so the C++ code and this
mesh can never disagree. `--preview` renders poses by applying the same transforms (Orbiter
convention: a positive angle about +z turns +x towards +y; about +x turns +y towards +z;
children are transformed by their parents).
"""
import math
import os
import sys

import numpy as np

STERN_Z = -60.0
INSERT_S, INSERT_L = 88.0, 9.0                 # T9: hull insert (argon charges); everything ahead moves forward
AXIS_H = 31.4                                  # lying on the blades: shortest blade 30 m + ankle 0.5 + hub 0.9
L_SHIP = 168.968 + INSERT_L
SEG = 48


def zs(s):
    return s + STERN_Z


def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


def sm5(t):
    t = min(1.0, max(0.0, t))
    return t * t * t * (t * (6 * t - 15) + 10)


class Group:
    def __init__(self, name, material):
        self.name, self.material = name, material
        self.v, self.n, self.t = [], [], []

    def vert(self, p, n):
        self.v.append(np.asarray(p, float))
        nn = np.asarray(n, float)
        self.n.append(nn / (np.linalg.norm(nn) or 1.0))
        return len(self.v) - 1

    def tri(self, a, b, c, outward):
        """Add a triangle facing `outward` (Orbiter: clockwise = front)."""
        p0, p1, p2 = self.v[a], self.v[b], self.v[c]
        if np.dot(np.cross(p1 - p0, p2 - p0), outward) < 0:
            b, c = c, b
        self.t.append((a, b, c))

    def quad(self, a, b, c, d, outward):
        self.tri(a, b, c, outward)
        self.tri(a, c, d, outward)


# ---------------------------------------------------------------------------
# Primitives


def lathe(g, profile, center=(0.0, 0.0), inward=False, seg=SEG):
    """Surface of revolution about an axis parallel to z through `center`; profile (s, r)."""
    cx, cy = center
    prof = [(zs(s), r) for s, r in profile]
    rings = []
    for i, (z, r) in enumerate(prof):
        z0, r0 = prof[max(i - 1, 0)]
        z1, r1 = prof[min(i + 1, len(prof) - 1)]
        tz, tr = z1 - z0, r1 - r0
        nr, nz = tz, -tr
        if inward:
            nr, nz = -nr, -nz
        ring = []
        for k in range(seg):
            a = 2 * math.pi * k / seg
            c, s = math.cos(a), math.sin(a)
            ring.append(g.vert((cx + r * c, cy + r * s, z), (nr * c, nr * s, nz)))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(seg):
            k1 = (k + 1) % seg
            a, b, c, d = rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]
            mid = (g.v[a] + g.v[b] + g.v[c] + g.v[d]) / 4
            radial = np.array([mid[0] - cx, mid[1] - cy, 0.0])
            nrm = g.n[a] + g.n[b] + g.n[c] + g.n[d]
            out = nrm if np.linalg.norm(nrm) > 1e-9 else radial
            g.quad(a, b, c, d, out)


def disc_n(g, centre, normal, radius, seg=24):
    """Flat disc with an arbitrary normal."""
    c = np.asarray(centre, float)
    n = unit(normal)
    ref = np.array([0, 1, 0]) if abs(n[1]) < 0.9 else np.array([1, 0, 0])
    u = unit(np.cross(n, ref))
    v = np.cross(n, u)
    c0 = g.vert(c, n)
    ring = [g.vert(c + radius * (math.cos(2 * math.pi * k / seg) * u + math.sin(2 * math.pi * k / seg) * v), n)
            for k in range(seg)]
    for k in range(seg):
        g.tri(c0, ring[k], ring[(k + 1) % seg], n)


def prism(g, poly2d, axis_a, axis_b, axis_n, offset_n, thickness, fan=0):
    """Flat plate: simple polygon in the (axis_a, axis_b) plane, extruded along axis_n.
    Faces are a fan from vertex `fan` (pass the reflex vertex of an L-shaped polygon)."""
    A, B, N = (np.array(x, float) for x in (axis_a, axis_b, axis_n))
    h = thickness / 2
    m = len(poly2d)
    area = sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(poly2d, poly2d[1:] + poly2d[:1]))
    orient = np.sign(area) * np.sign(np.dot(np.cross(A, B), N))
    pts = [p[0] * A + p[1] * B for p in poly2d]
    order = [(fan + k) % m for k in range(m)]
    for side in (+1, -1):
        nrm = side * N
        idx = [g.vert(p + (offset_n + side * h) * N, nrm) for p in pts]
        for i in range(1, m - 1):
            g.tri(idx[order[0]], idx[order[i]], idx[order[i + 1]], nrm)
    for i in range(m):
        p, q = pts[i], pts[(i + 1) % m]
        out = np.cross(q - p, N) * orient
        quad = [g.vert(p + (offset_n + h) * N, out), g.vert(q + (offset_n + h) * N, out),
                g.vert(q + (offset_n - h) * N, out), g.vert(p + (offset_n - h) * N, out)]
        g.quad(*quad, out)


def tube(g, p0, p1, r, n=16, caps=True, r1=None):
    """Closed n-gon cylinder between two points (n=4 gives a square beam)."""
    p0, p1 = np.array(p0, float), np.array(p1, float)
    r1 = r if r1 is None else r1
    d = unit(p1 - p0)
    ref = np.array([0, 1, 0]) if abs(d[1]) < 0.9 else np.array([1, 0, 0])
    u = unit(np.cross(d, ref))
    v = np.cross(d, u)
    ph = math.pi / n if n == 4 else 0.0
    dirs = [math.cos(2 * math.pi * k / n + ph) * u + math.sin(2 * math.pi * k / n + ph) * v for k in range(n)]
    ring0 = [g.vert(p0 + r * e, e) for e in dirs]
    ring1 = [g.vert(p1 + r1 * e, e) for e in dirs]
    for k in range(n):
        k1 = (k + 1) % n
        g.quad(ring0[k], ring0[k1], ring1[k1], ring1[k], dirs[k] + dirs[k1])
    if caps:
        for p, rr, sgn in ((p0, r, -1), (p1, r1, 1)):
            nn = sgn * d
            c0 = g.vert(p, nn)
            ring = [g.vert(p + rr * e, nn) for e in dirs]
            for k in range(n):
                g.tri(c0, ring[k], ring[(k + 1) % n], nn)


def box(g, lo, hi):
    lo, hi = np.array(lo, float), np.array(hi, float)
    c = (lo + hi) / 2
    obox(g, c, (1, 0, 0), (0, 1, 0), (0, 0, 1), *((hi - lo) / 2))


def obox(g, centre, ax, ay, az, hx, hy, hz):
    """Oriented box: half sizes along three orthonormal axes."""
    c = np.asarray(centre, float)
    A = [unit(ax), unit(ay), unit(az)]
    H = [hx, hy, hz]
    for i in range(3):
        for sgn in (-1, 1):
            n = sgn * A[i]
            j, k = [q for q in range(3) if q != i]
            pts = [g.vert(c + n * H[i] + a * H[j] * A[j] + b * H[k] * A[k], n) for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            g.quad(*pts, n)


def cup(g, cx, cy, s_rim, depth, radius, seg=24):
    """Concave reflector cup opening aft; the back face sits 3 cm behind (no z-fighting)."""
    prof = [(s_rim + depth * (1 - t * t), radius * t) for t in np.linspace(0.02, 1.0, 8)]
    lathe(g, prof, center=(cx, cy), seg=seg)
    lathe(g, [(s + 0.03, r) for s, r in prof], center=(cx, cy), inward=True, seg=seg)


def cup_fwd(g, cx, cy, s_rim, depth, radius, seg=24):
    """Concave reflector cup opening forward (retro cups in the nose)."""
    prof = [(s_rim - depth * (1 - t * t), radius * t) for t in np.linspace(0.02, 1.0, 8)]
    lathe(g, prof, center=(cx, cy), seg=seg)
    lathe(g, [(s - 0.03, r) for s, r in prof], center=(cx, cy), inward=True, seg=seg)


def rounded_rect(a, b, r, n=4):
    """Convex rounded rectangle (half sizes a, b; corner radius r) as a 2D polygon."""
    pts = []
    for cx, cy, a0 in ((a - r, b - r, 0), (-a + r, b - r, 90), (-a + r, -b + r, 180), (a - r, -b + r, 270)):
        for k in range(n + 1):
            t = math.radians(a0 + 90.0 * k / n)
            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


def plate(g, centre, long_ax, short_ax, normal, half_l, half_w, t, rc):
    """Rounded flat plate centred at `centre` (long/short axes in its plane, `normal` across)."""
    A, B, N = unit(long_ax), unit(short_ax), unit(normal)
    c = np.asarray(centre, float)
    poly = [(np.dot(c, A) + u, np.dot(c, B) + v) for u, v in rounded_rect(half_l, half_w, rc)]
    prism(g, poly, A, B, N, np.dot(c, N), t)


def rot(axis, ang):
    """Orbiter/right-hand rotation matrix (angle rad)."""
    a = unit(axis)
    x, y, z = a
    c, s = math.cos(ang), math.sin(ang)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def axis_angle(R):
    ang = math.acos(max(-1.0, min(1.0, (np.trace(R) - 1) / 2)))
    if ang < 1e-9:
        return np.array([1.0, 0, 0]), 0.0
    ax = np.array([R[2, 1] - R[1, 2], R[0, 2] - R[2, 0], R[1, 0] - R[0, 1]])
    if np.linalg.norm(ax) < 1e-9:  # 180 deg
        w, V = np.linalg.eigh((R + np.eye(3)) / 2)
        ax = V[:, np.argmax(w)]
    return unit(ax), ang


# ---------------------------------------------------------------------------
# Materials: name, diffuse, specular (r, g, b, power), emissive

MATERIALS = [
    ("hull_lacquer", (0.62, 0.64, 0.68), (0.30, 0.30, 0.32, 20), (0, 0, 0)),     # boron-zirconium lacquer
    ("nose_iridium", (0.78, 0.76, 0.70), (0.95, 0.95, 0.95, 80), (0, 0, 0)),     # crystalline iridium shields
    ("crest_radiator", (0.46, 0.26, 0.18), (0.15, 0.15, 0.15, 10), (0, 0, 0)),
    ("structure", (0.26, 0.27, 0.29), (0.20, 0.20, 0.20, 15), (0, 0, 0)),
    ("boron_nitride", (0.62, 0.58, 0.92), (0.60, 0.60, 0.70, 40), (0.08, 0.06, 0.20)),
    ("engine_metal", (0.40, 0.40, 0.42), (0.50, 0.50, 0.50, 40), (0, 0, 0)),
    ("mechanism", (0.54, 0.56, 0.60), (0.70, 0.70, 0.72, 50), (0, 0, 0)),
    ("dark", (0.07, 0.07, 0.09), (0.05, 0.05, 0.05, 5), (0, 0, 0)),
    ("band", (0.28, 0.36, 0.50), (0.50, 0.55, 0.65, 40), (0, 0, 0)),               # CNT band mast
    ("planetary_cup", (0.60, 0.70, 0.85), (0.70, 0.70, 0.80, 50), (0.03, 0.06, 0.10)),
    ("rover", (0.55, 0.48, 0.32), (0.30, 0.30, 0.30, 20), (0, 0, 0)),
    ("shuttle", (0.85, 0.84, 0.81), (0.40, 0.40, 0.40, 30), (0, 0, 0)),
    ("trap_shell", (0.44, 0.47, 0.56), (0.45, 0.45, 0.50, 30), (0, 0, 0)),         # anamezon trap containers
]
MAT = {m[0]: i + 1 for i, m in enumerate(MATERIALS)}  # .msh material indices are 1-based


# ---------------------------------------------------------------------------
# Hull T8: the smallest smooth envelope over what must be inside (Tantra_Design/DESIGN_LOCAL.md, «форма из содержимого»):
# stern frame with four nacelles (s 0-18), lift shafts and the 2x2 trap columns D 8 (s 19-89.6), fairings of the carriage
# legs and pods (s 40-80), the XR2 hangar (s 90.6-111.6), crew (s 112.6-135), the water/charges screen, the nose.
# Sections: flat bottom, lower chine, flank, rounded top (the C-148 section family scaled by half-width W and height H);
# the axis sits at 40 % of the height above the bottom. The stern blends into the «clover» around the nacelles.

# (s, half-width at the chine, height) every metre up to the nose root
PROFILE = [
    (0, 8.9, 18.2), (1, 8.9, 18.2), (2, 8.9, 18.2), (3, 8.9, 18.2), (4, 8.9, 18.2), (5, 8.9, 18.2), (6, 8.9, 18.2),
    (7, 8.9, 18.2), (8, 8.9, 18.2), (9, 8.902, 18.2), (10, 8.92, 18.2), (11, 8.955, 18.2), (12, 8.994, 18.2),
    (13, 9.034, 18.2), (14, 9.073, 18.2), (15, 9.113, 18.2), (16, 9.152, 18.2), (17, 9.191, 18.2), (18, 9.263, 18.2),
    (19, 9.55, 18.2), (20, 9.55, 18.2), (21, 9.55, 18.2), (22, 9.55, 18.2), (23, 9.662, 18.2), (24, 9.801, 18.2),
    (25, 9.957, 18.2), (26, 10.121, 18.2), (27, 10.285, 18.2), (28, 10.454, 18.2), (29, 10.64, 18.2),
    (30, 10.843, 18.2), (31, 11.064, 18.2), (32, 11.302, 18.2), (33, 11.544, 18.2), (34, 11.83, 18.2),
    (35, 12.116, 18.2), (36, 12.403, 18.2), (37, 12.69, 18.2), (38, 12.977, 18.2), (39, 13.263, 18.2),
    (40, 13.55, 18.2), (41, 13.55, 18.2), (42, 13.55, 18.2), (43, 13.55, 18.2), (44, 13.55, 18.2), (45, 13.55, 18.2),
    (46, 13.55, 18.2), (47, 13.55, 18.2), (48, 13.55, 18.2), (49, 13.55, 18.2), (50, 13.55, 18.2), (51, 13.55, 18.2),
    (52, 13.55, 18.2), (53, 13.55, 18.2), (54, 13.55, 18.2), (55, 13.55, 18.2), (56, 13.55, 18.2), (57, 13.55, 18.2),
    (58, 13.55, 18.2), (59, 13.55, 18.2), (60, 13.55, 18.2), (61, 13.55, 18.2), (62, 13.55, 18.2), (63, 13.55, 18.2),
    (64, 13.55, 18.2), (65, 13.55, 18.2), (66, 13.55, 18.2), (67, 13.55, 18.2), (68, 13.55, 18.2), (69, 13.55, 18.2),
    (70, 13.55, 18.2), (71, 13.55, 18.2), (72, 13.55, 18.2), (73, 13.55, 18.2), (74, 13.55, 18.2), (75, 13.55, 18.2),
    (76, 13.55, 18.2), (77, 13.55, 18.2), (78, 13.55, 18.2), (79, 13.55, 18.2), (80, 13.55, 18.2),
    (81, 13.443, 18.2), (82, 13.431, 18.2), (83, 13.418, 18.2), (84, 13.406, 18.2), (85, 13.394, 18.2),
    (86, 13.382, 18.2), (87, 13.37, 18.2), (88, 13.358, 18.2),
    # T9 insert s 88..97: the fairing section runs on (argon tanks)
    (89, 13.358, 18.2), (90, 13.358, 18.2), (91, 13.358, 18.2), (92, 13.358, 18.2), (93, 13.358, 18.2), (94, 13.358, 18.2),
    (95, 13.358, 18.2), (96, 13.358, 18.2), (97, 13.358, 18.2), (98, 13.35, 18.2), (99, 13.35, 18.2),
    (100, 13.35, 17.627), (101, 13.35, 17.053), (102, 13.35, 16.48), (103, 13.35, 15.906), (104, 13.35, 15.624),
    (105, 13.35, 15.346), (106, 13.35, 15.067), (107, 13.35, 14.788), (108, 13.35, 14.518), (109, 13.35, 14.283),
    (110, 13.35, 14.082), (111, 13.35, 13.916), (112, 13.35, 13.785), (113, 13.35, 13.689), (114, 13.35, 13.627),
    (115, 13.35, 13.6), (116, 13.35, 13.6), (117, 13.35, 13.6), (118, 13.35, 13.6), (119, 13.35, 13.6),
    (120, 13.35, 13.6), (121, 13.35, 13.6), (122, 13.063, 13.6), (123, 12.777, 13.6), (124, 12.49, 13.6),
    (125, 12.203, 13.6), (126, 11.916, 13.6), (127, 11.63, 13.6), (128, 11.343, 13.6), (129, 11.056, 13.6),
    (130, 10.769, 13.6), (131, 10.483, 13.6), (132, 10.196, 13.6), (133, 9.916, 13.6), (134, 9.653, 13.6),
    (135, 9.408, 13.6), (136, 9.181, 13.6), (137, 8.97, 13.6), (138, 8.777, 13.6), (139, 8.602, 13.6),
    (140, 8.443, 13.6), (141, 8.302, 13.6), (142, 8.179, 13.6), (143, 8.073, 13.6), (144, 7.984, 13.6),
    (145, 7.9, 13.6), (146, 7.815, 13.6), (147, 7.8, 13.6), (148, 7.8, 13.6), (149, 7.8, 13.6), (150, 7.8, 13.6),
    (151, 7.8, 13.6),
]

FL = 0.4                                 # bottom at -FL*H, top at (1-FL)*H
NB = 142.968 + INSERT_L                  # nose root
NOSE_LN, NOSE_UP, NOSE_NW, NOSE_BEXP, NOSE_TEXP, NOSE_RN = L_SHIP - NB, 0.4, 0.9, 0.25, 0.55, 3.0
_PROF = np.array(PROFILE, float)


def wh_at(s):
    """Half-width and height of the body section at s (s <= NB)."""
    s = min(max(s, 0.0), NB)
    return float(np.interp(s, _PROF[:, 0], _PROF[:, 1])), float(np.interp(s, _PROF[:, 0], _PROF[:, 2]))


def _key(B, C, T, ya, rb, rc, rt, ra):
    return dict(B=np.array(B, float), C=np.array(C, float), T=np.array(T, float), ya=ya, rb=rb, rc=rc, rt=rt, ra=ra)


def key_wh(W, H):
    k = H / 24.0
    return _key((0.767 * W, -FL * H), (W, -(FL - 0.233) * H), (0.753 * W, (1 - FL) * H - 0.1 * H), (1 - FL) * H,
                3.0 * k, 0.8, 3.0 * k, 6.0 * k)


def fillet(pts, rad, n=10):
    out, m = [], len(pts)
    for i in range(m):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % m]
        a, b = p0 - p1, p2 - p1
        la, lb = np.linalg.norm(a), np.linalg.norm(b)
        a, b = a / la, b / lb
        ang = math.acos(max(-1.0, min(1.0, float(a @ b))))
        r = rad[i]
        if r <= 0 or ang > math.pi - 1e-3:
            out.append(p1)
            continue
        t = min(r / math.tan(ang / 2), 0.49 * la, 0.49 * lb)
        r = t * math.tan(ang / 2)
        q0, q1 = p1 + a * t, p1 + b * t
        c = p1 + unit(a + b) * (r / math.sin(ang / 2))
        t0, t1 = math.atan2(*(q0 - c)[::-1]), math.atan2(*(q1 - c)[::-1])
        dt = t1 - t0
        while dt > math.pi:
            dt -= 2 * math.pi
        while dt < -math.pi:
            dt += 2 * math.pi
        for k in range(n + 1):
            th = t0 + dt * k / n
            out.append(c + r * np.array([math.cos(th), math.sin(th)]))
    return np.array(out)


class Section:
    """Closed CCW outline starting at the bottom centre; u = arc-length fraction."""

    def __init__(self, P=None, poly=None):
        if poly is None:
            m = lambda p: np.array([-p[0], p[1]])
            pts = [P["B"], P["C"], P["T"], np.array([0.0, P["ya"]]), m(P["T"]), m(P["C"]), m(P["B"])]
            poly = fillet(pts, [P["rb"], P["rc"], P["rt"], P["ra"], P["rt"], P["rc"], P["rb"]])
            n = len(poly)
            for k in range(n):
                p, q = poly[k - 1], poly[k]
                if p[1] < 0 and q[1] < 0 and p[0] < 0 <= q[0]:
                    y0 = p[1] + (q[1] - p[1]) * (-p[0]) / (q[0] - p[0])
                    poly = np.vstack([[0.0, y0], poly[k:], poly[:k]])
                    break
        self.p = np.asarray(poly, float)
        self.q = np.vstack([self.p[1:], self.p[:1]])
        l = np.hypot(*(self.q - self.p).T)
        self.cum = np.concatenate([[0.0], np.cumsum(l)])
        self.L = self.cum[-1]

    def at(self, u):
        d = (u % 1.0) * self.L
        j = min(len(self.p) - 1, max(0, int(np.searchsorted(self.cum, d, "right")) - 1))
        f = (d - self.cum[j]) / max(1e-12, self.cum[j + 1] - self.cum[j])
        return self.p[j] + (self.q[j] - self.p[j]) * f

    def u_of(self, pt):
        pt = np.asarray(pt, float)
        e = self.q - self.p
        t = np.clip(((pt - self.p) * e).sum(1) / (e * e).sum(1), 0, 1)
        c = self.p + e * t[:, None]
        j = int(np.argmin(((c - pt) ** 2).sum(1)))
        return (self.cum[j] + t[j] * (self.cum[j + 1] - self.cum[j])) / self.L

    def dist(self, pt):
        """Signed distance of a 2D point to the outline (+ outside)."""
        pt = np.asarray(pt, float)
        e = self.q - self.p
        t = np.clip(((pt - self.p) * e).sum(1) / (e * e).sum(1), 0, 1)
        d = np.sqrt(((self.p + e * t[:, None] - pt) ** 2).sum(1)).min()
        y = pt[1]
        cross = ((self.p[:, 1] > y) != (self.q[:, 1] > y))
        xs = self.p[cross, 0] + (y - self.p[cross, 1]) * e[cross, 0] / e[cross, 1]
        inside = (np.sum(xs > pt[0]) % 2) == 1
        return -d if inside else d


# Stern: four nacelles on the diagonals (centres +-A_N about the stern axis YC), radius R_N; the skin wraps them
# with webs (radius RW about YC); an axial slot between the two upper (and the two lower) nacelles takes the fin.
A_N, R_N, RW = 4.8, 4.1, 7.3
YC = (0.5 - FL) * wh_at(0.0)[1]                 # stern axis: mid-height of the stern section
NAC = [(A_N, YC + A_N), (-A_N, YC + A_N), (-A_N, YC - A_N), (A_N, YC - A_N)]   # upper stbd, upper port, lower port, lower stbd
CLOVER_S0, CLOVER_S1 = 8.0, 16.0                 # pure clover aft of s 8, body section from s 16
NU = 240


def clover_poly():
    pts = []
    for i in range(NU):
        th = -math.pi / 2 + 2 * math.pi * i / NU
        ux, uy = math.cos(th), math.sin(th)
        r = RW
        for cx, cy in NAC:
            cx, cy = cx, cy - YC
            b = ux * cx + uy * cy
            d = b * b - (cx * cx + cy * cy - (R_N - 0.15) ** 2)
            if d >= 0:
                r = max(r, b + math.sqrt(d))
        pts.append((r * ux, YC + r * uy))
    return np.array(pts)


_CLOVER = Section(poly=clover_poly())
_SEC = {}


def sec(s):
    s = min(max(s, 0.0), NB)
    k = round(s, 6)
    if k not in _SEC:
        body = Section(key_wh(*wh_at(s)))
        if s >= CLOVER_S1:
            _SEC[k] = body
        else:
            f = sm5((CLOVER_S1 - s) / (CLOVER_S1 - CLOVER_S0))
            us = np.linspace(0.0, 1.0, NU, endpoint=False)
            poly = np.array([(1 - f) * body.at(u) + f * _CLOVER.at(u) for u in us])
            _SEC[k] = Section(poly=poly)
    return _SEC[k]


# Nose: blunted (tip radius 3 m), flat bottom up to a tip at 40 % of the height, rounded top, no kink at the root.
_WF, _HF = wh_at(NB)
_BASE = sec(NB)
_YB0 = -FL * _HF
_YTIP = -FL * _HF + NOSE_UP * _HF
_Y0 = _YTIP
TIP_S = L_SHIP - 0.35


def nose_shape(s):
    x = max(0.0, (L_SHIP - s) / NOSE_LN)
    g = 1 - (1 - x) ** 2
    w = _WF * g ** NOSE_NW
    yb = _YTIP + (_YB0 - _YTIP) * g ** NOSE_BEXP
    yt = _YTIP + ((1 - FL) * _HF - _YTIP) * g ** NOSE_TEXP
    return w, yb, yt


def hull_xy(s, u):
    if s <= NB:
        return sec(s).at(u)
    w, yb, yt = nose_shape(s)
    p = _BASE.at(u)
    return np.array([p[0] * w / _WF, yb + (p[1] - _YB0) * (yt - yb) / _HF])


def hull_pt(s, u, off=0.0):
    x, y = hull_xy(s, u)
    p = np.array([x, y, zs(s)])
    return p + off * hull_n(s, u) if off else p


def hull_n(s, u):
    du, ds = 2e-4, 0.25
    a = hull_xy(s, u + du) - hull_xy(s, u - du)
    s2 = s + ds if s + ds < TIP_S - 0.05 else s - ds
    b = hull_xy(s2, u) - hull_xy(s, u)
    Pu = np.array([a[0], a[1], 0.0])
    Ps = np.array([b[0], b[1], s2 - s])
    n = np.cross(Pu, Ps) * (1 if s2 > s else -1)
    return unit(n)


def top_y(s):
    return hull_xy(s, 0.5)[1]


def outside(p):
    """Signed distance of a 3D point outside the skin at its station (None beyond the hull)."""
    s = p[2] - STERN_Z
    if s < 0.0 or s > TIP_S - 0.5:
        return None
    if s <= NB:
        return sec(s).dist(p[:2])
    w, yb, yt = nose_shape(s)
    kx, ky = w / _WF, (yt - yb) / _HF
    q = (p[0] / kx, _YB0 + (p[1] - yb) / ky)
    return _BASE.dist(q) * min(kx, ky)


# Reference faces of the fairing (s 40-80; carriage legs and pods), starboard; port mirrors x and u -> 1 - u.
S_FAIR = 60.0
KEY_FAIR = key_wh(*wh_at(S_FAIR))
_SA = sec(S_FAIR)
_C, _T, _B = KEY_FAIR["C"], KEY_FAIR["T"], KEY_FAIR["B"]
E_F = unit(_T - _C)                           # flank, up
N_F = np.array([E_F[1], -E_F[0]])             # flank outward normal
E_C = unit(_C - _B)                           # lower chine, up
N_C = np.array([E_C[1], -E_C[0]])
_APEX = np.array([0.0, KEY_FAIR["ya"]])
E_T = unit(_APEX - _T)                        # top slope, inboard
N_T = np.array([E_T[1], -E_T[0]])


def flank_pt(t):
    return _C + t * E_F


def flank_t(u):
    return float((_SA.at(u) - _C) @ E_F)


def chine_pt(w, key=KEY_FAIR):
    return key["B"] + w * unit(key["C"] - key["B"])


def top_pt(v):
    return _T + v * E_T


def mir(u):
    return 1.0 - u


# ---------------------------------------------------------------------------
# Geometry of the moving parts (mirrors core/Spec.h)

# Carriage («лафет») legs, one per side in the fairing: trunnion shoe on rails in a flank pocket, telescopic trunnion
# pin (four stages nesting into the hip block), hip block with the pitch bearing, flat thigh (band drums inside),
# band mast (interlocking bands rolled off the drums into a rigid box section; 13 visual sections), two-axis ankle,
# pad 16 x 6 x 1.2 m on a spreader fork. T8 erects the loaded ship (CG s 54.4) on 70 m columns and the light one
# (CG s 70.6-72) on 86 m: mast up to 94 m, section 6.2 x 2.0 m (I 0.66 m^4; tests: t8 carriage check).
# Stowed: pitched aft along the hull, pad folded against the thigh ground face out, rolled with the hip to the
# flank lean so the pad is the flank skin over the pocket.
CAR_S0, CAR_S1 = 53.4, 71.6                    # hip (trunnion) track: loaded .. landing CG; the pin slot runs on
CAR_SREF = CAR_S0                              # legs built with the hip at the track start
STOW_S = 66.0                                  # hip station with the leg folded aft into the pocket
HIP_X_OUT = 19.0
THIGH_L, THIGH_W, THIGH_T = 8.0, 6.8, 2.3      # blade: 6.8 m fore-aft when hanging, 2.3 m across (band mast 6.2 inside)
SHIN_N, SHIN_SEG, SHIN_OVL, SHIN_TOP = 13, 7.4, 0.3, 0.6
SHIN_W, SHIN_T = 6.2, 2.0                      # band mast
SHIN_STEP = SHIN_SEG - SHIN_OVL
ANKLE_R, PAD_OFF = 0.8, 1.6                    # ankle; pad mid-plane below the ankle centre
PAD_L, PAD_W, PAD_T, PAD_RC = 16.0, 7.0, 1.2, 0.3      # pad as wide as the thigh: the pocket takes both
LEG_LMIN = THIGH_L + ANKLE_R                   # hip -> ankle, mast rolled in
LEG_LMAX = SHIN_TOP + (SHIN_N - 1) * SHIN_STEP + SHIN_SEG + ANKLE_R
FOOT_H = PAD_OFF + PAD_T / 2                   # ankle centre above the ground = leg axis to pad face
ROLL = math.atan2(N_F[1], N_F[0])
PIN_N, PIN_X0, PIN_STEP, PIN_LEN = 4, 9.75, 1.8, 1.9   # pin stages (deployed: x0 + k*step .. + len); roll pivot at x0
ROLL_P = np.array([PIN_X0, 0.0])
TRAVEL = HIP_X_OUT - PIN_X0 + FOOT_H - float(N_F @ _C - N_F @ ROLL_P)   # stowed pad face lies in the flank plane
HIP_X_IN = HIP_X_OUT - TRAVEL
SLIDE_ROLL = 0.3
HIP_BLOCK = (0.9, 1.3, 2.7, 1.7)               # hip block: inboard, outboard, half height, half length
POCKET_T = float((ROLL_P + (HIP_X_IN - PIN_X0 + FOOT_H) * N_F - _C) @ E_F)   # pad centre on the flank
LAF_S0 = STOW_S - LEG_LMIN - PAD_L / 2 - 0.05
LAF_S1 = STOW_S + HIP_BLOCK[3] + 0.2
LAF_DEPTH = 3.8
PIN_SLOT = (LAF_S1, CAR_S1 + 1.6)              # lip-sealed slot for the pin beyond the pocket

# Stern legs: on the outer sides of the four nacelles, 30 deg off the horizontal (in the shadow of the body at
# sub-light: inside |x| <= 13.5, under the top line). Hinge forward (s 15), leg folded aft along the nacelle on
# heat shields (10 ceramic screens: ~820 K next to a 1500 K nacelle), pad folded against the thigh. Each leg swings
# in its own radial plane; standing feet on R 26 m behind the stern (s -12): tip-over 8.9 deg at the landing CG.
LEG_ANG = math.radians(30.0)                         # lower legs (and every standing foot)
LEG_ANG_UP = math.radians(23.0)                      # upper legs stowed lower: under the top line at sub-light
LEG_S_H = 30.0                                       # hinge on a pylon ahead of the nacelles (splay friction <= 0.55)
LEG_THIGH_L, LEG_THIGH_W, LEG_THIGH_T = 8.0, 2.6, 2.0
LEG_SHIN_W, LEG_SHIN_T, LEG_SHIN_N = 2.0, 1.5, 6
LEG_LMIN_S = LEG_THIGH_L + ANKLE_R
LEG_EXT_MAX = LEG_SHIN_N * SHIN_STEP
LEG_STANDOFF = 1.7                                   # heat-shield stack between the nacelle and the leg (clears the widening body ahead)
LEG_PAD_OFF = 1.1
LEG_FOOT_H = LEG_PAD_OFF + PAD_T / 2
LEG_PAD_L, LEG_PAD_W = 9.0, 2.6
STAND_R, STAND_GROUND_S = 33.0, -12.0                # tip-over >= 10 deg at the landing CG
REST_GROUND_Y = -AXIS_H
REST_EXT = 13.0
LEG_EXT_DELAY = 0.6
# Leg systems (Tantra_Design/DESIGN_LOCAL.md «Ноги Т8: передовая механика», core/Legs.h): the ankle is a gas-hydraulic
# strut with an MR valve (stroke 1.5 m carriage / 1.0 m stern, 30 % taken by the weight: the mesh is built at that
# static sag, unloaded the rod shows STRUT_EXT more); the lower SOLE_T of every pad is the jamming sole; anchors on
# CNT muscles sit in the pad and run ANCHOR_OUT into the ground; catcher rings on the magnetic-bearing drums.
STRUT_EXT_C, STRUT_EXT_S = 0.45, 0.3
SOLE_T, ANCHOR_OUT, ANCHOR_L = 0.45, 0.8, 1.0
# name, nacelle index, side (x sign), up (y sign)
STERN_LEGS = [("upper_stbd", 0, 1, 1), ("upper_port", 1, -1, 1), ("lower_port", 2, -1, -1), ("lower_stbd", 3, 1, -1)]

# Wings: two panels on two hinges. Root hinge along z in the side trough between the nacelles (x 7.6 on the stern
# axis), root chord s 0-24; inner panel 6 m, outer panel 10 m (span 16 m from the hinge); elevons on the outer
# panel trailing edge (30 % chord). Modes: 90 = deployed, 30 = raised 30 deg, folded = inner panel up 85 deg and the
# outer panel folded back down along it (in the shadow of the fairings at sub-light).
WING_X0, WING_Y = 9.4, YC                      # hinge outside the body side (s 16-24); at the stern a longeron carries it
WING_T = 0.9
WING_B1, WING_B = 5.5, 16.0                    # folded: inner panel clear of the upper legs, outer of the lower
WING_LE0, WING_TE0, WING_LE1, WING_TE1 = 24.0, 0.0, 10.0, 4.0
WING_FOLD = math.radians(85.0)
WING_RAISE = math.radians(30.0)
ELEV_UP, ELEV_DOWN = 30.0, 40.0
FLAP_HALF, FLAP_S0, FLAP_S1, FLAP_T, FLAP_DOWN = 7.0, -1.0, 6.0, 0.25, 25.0
FLAP_Y = YC - A_N - R_N - 0.45


def wing_le(r):
    return WING_LE0 + (WING_LE1 - WING_LE0) * r / WING_B


def wing_te(r):
    return WING_TE0 + (WING_TE1 - WING_TE0) * r / WING_B


def wing_h(r):
    return wing_te(r) + 0.3 * (wing_le(r) - wing_te(r))


# Dorsal fin: telescopic, two stages of 11 m (726 m^2, yaw margin +7 %), root s 3-48; retracts down into the axial
# slot between the upper nacelles and the trap columns (columns at x +-4.75: a 1.5 m slot).
FIN_LOWER = [(3.0, 0.0), (48.0, 0.0), (37.5, 11.0), (4.5, 11.0)]
FIN_UPPER = [(4.5, 11.0), (37.5, 11.0), (27.0, 22.0), (6.0, 22.0)]
FIN_T, FIN_T2 = 1.1, 0.8
FIN_BASE = YC + RW + 0.2                      # root at the top web between the upper nacelles
FIN_DROP = 11.5                               # each stage
FIN_RETRACT = 2 * FIN_DROP
SLOT_HALF = 0.75

# Planetary pods: 4 x 3 cups in the fairing on its lower chines (s 43.6 aft pair, s 78 fore pair). The bay door is the
# pod's swing arm: it swings down past vertical about its lower edge, the pod hangs outboard under the chine and turns on
# a trunnion normal to the door (0 = thrust aft along the hull, 90 = thrust up).
POD_L, POD_W, POD_T, POD_DEPTH = 5.6, 3.0, 1.8, 1.4
POD_DEFS = [(43.6, KEY_FAIR, (0.4, 4.6), 2.5, -1), (43.6, KEY_FAIR, (0.4, 4.6), 2.5, 1),
            (78.0, KEY_FAIR, (0.4, 4.6), 2.5, -1), (78.0, KEY_FAIR, (0.4, 4.6), 2.5, 1)]
POD_DOOR_L = 6.4
POD_SWIVEL_MAX = math.radians(100.0)
POD_CANT = {43.6: math.radians(15.0), 78.0: math.radians(15.0)}

# Engines in the nacelles: fixed anamezon cups (aperture R 3.0 m, focus in the rim plane s -2: the hull stands in
# their shadow). T9: the 12 planetary cups on the rims are gone. The marching planetary cup (R 2.2, 12.1 T, 886 MN)
# sits in the central well between the nacelles (free circle R 2.69) on a sliding mount: stowed under the well iris
# 0.6 m below the rim plane, run out MARCH_TRAVEL so its lip stands 4.5 m beyond the anamezon rims (jet >= 2 radii
# from the nacelle lips). Interlocks (code): anamezon irises only with the mount home and its iris shut; the mount
# only with the anamezon irises shut.
RIM_S = -2.0
ANA_R, ANA_DEPTH = 3.0, 1.5
ANA_CUPS = [(x, y) for x, y in NAC]
WELL_R = A_N * math.sqrt(2.0) + R_N
WELL_C_R = A_N * math.sqrt(2.0) - R_N              # central well (2.69)
WELL_C_DEPTH_S = 14.0                              # well floor station
MARCH_R, MARCH_D = 2.2, 1.5                        # marching cup aperture, depth
MARCH_BODY_R, MARCH_BODY_L = 2.5, 8.0              # coil housing behind the cup (slides in the well)
MARCH_LIP_S = -0.3                                 # cup lip, stowed
MARCH_IRIS_S = -0.6                                # well iris (shut over the stowed cup)
MARCH_TRAVEL = 4.5 + MARCH_LIP_S - RIM_S           # lip from s -0.3 to s -6.5 (6.2 m)
# Nose: two retro anamezon cups (R 2.2) at x +-3.0, y -1.0, lips at s 165 (156 before the insert), 0.5 m under the
# skin; the jet leaves forward through a skin opening s 165-171.5 whose cover is the iris (petals = nose skin).
NOSE_CUP_X, NOSE_CUP_Y, NOSE_CUP_R, NOSE_CUP_D = 3.0, -1.0, 2.2, 1.5
NOSE_CUP_S = 156.0 + INSERT_L
NOSE_CUP_S1 = NOSE_CUP_S + 6.5

# Anamezon port and trap columns: 2x2 cylinders D 8 m in octagonal armour, s 21-87.6, lifted by their trunnions on
# telescopic masts in the shafts at both column ends.
CASS_W, CASS_CH = 8.3, 2.4                     # armour across the flats: the fin slot between the columns stays 1.2 m
CASS_S0, CASS_S1 = 21.0, 87.6
_YB_C = -FL * wh_at(50.0)[1]
TRAP_XY = [(4.75, _YB_C + 0.8 + 4.3), (-4.75, _YB_C + 0.8 + 4.3), (4.75, _YB_C + 0.8 + 4.3 + 8.6), (-4.75, _YB_C + 0.8 + 4.3 + 8.6)]
BAY_S0, BAY_S1 = 20.4, 88.2
BAY_X0, BAY_X1 = 0.75, 9.05
LIFT_Y0, LIFT_TRAVEL, LIFT_CEIL = TRAP_XY[2][1], 36.0, (1 - FL) * wh_at(50.0)[1] - 0.8
LIFT_N, LIFT_SEG = 8, 5.2
HEAD_S = (20.0, 88.6)
TRAP_MOUTH_Y = _YB_C - 0.2
HANGAR_S = (90.6 + INSERT_L, 111.6 + INSERT_L)
AIRLOCK_S = 120.0 + INSERT_L
HANGAR_REF_S = 101.0 + INSERT_L            # reference section of the hangar doors


def octagon(cx, cy, w, ch):
    h = w / 2
    return [(cx - h + ch, cy - h), (cx + h - ch, cy - h), (cx + h, cy - h + ch), (cx + h, cy + h - ch),
            (cx + h - ch, cy + h), (cx - h + ch, cy + h), (cx - h, cy + h - ch), (cx - h, cy - h + ch)]


def trap_geom(g, cx, cy, s0, s1):
    """Trap cassette: octagonal armour with hoop bands, end blocks and axial trunnions (s0..s1 = body)."""
    L = s1 - s0
    zc = zs((s0 + s1) / 2)
    prism(g, octagon(cx, cy, CASS_W, CASS_CH), (1, 0, 0), (0, 1, 0), (0, 0, 1), zc, L)
    for sb in (s0 + 3.0, s0 + 8.5, s1 - 8.5, s1 - 3.0):
        prism(g, octagon(cx, cy, CASS_W + 0.3, CASS_CH + 0.06), (1, 0, 0), (0, 1, 0), (0, 0, 1), zs(sb), 0.6)
    for se, sg in ((s0, -1), (s1, 1)):
        prism(g, octagon(cx, cy, CASS_W - 1.2, CASS_CH - 0.4), (1, 0, 0), (0, 1, 0), (0, 0, 1), zs(se + sg * 0.15), 0.3)
        tube(g, (cx, cy, zs(se)), (cx, cy, zs(se + sg * 0.6)), 1.0, n=16)
        tube(g, (cx, cy, zs(se + sg * 0.45)), (cx, cy, zs(se + sg * 0.6)), 1.5, n=16)
        box(g, (cx - 2.2, cy + 1.6, zs(se) - 0.2 if sg < 0 else zs(se)), (cx - 0.8, cy + 3.0, zs(se) if sg < 0 else zs(se) + 0.2))


def nose_u_range(s, cx, cy, r):
    """u-range of the nose skin at station s within r of the axis (cx, cy): the retro-cup opening."""
    us = np.linspace(0.0, 1.0, 2001)
    xy = np.array([hull_xy(s, u) for u in us])
    m = np.hypot(xy[:, 0] - cx, xy[:, 1] - cy) <= r
    sel = us[m]
    return float(sel.min()), float(sel.max())


# ---------------------------------------------------------------------------
# Layout of pods and stern legs (computed once; used by the mesh, the rig and the header)


def pod_layout():
    out = []
    for i, (s_c, key, (w0, w1), wc, sgn) in enumerate(POD_DEFS):
        S = sec(s_c)
        e = unit(key["C"] - key["B"])
        n2 = np.array([e[1], -e[0]])
        u0, u1 = S.u_of(chine_pt(w0, key)), S.u_of(chine_pt(w1, key))
        if sgn < 0:
            u0, u1 = mir(u1), mir(u0)
        m = np.array([sgn, 1.0])
        n3 = np.array([*(n2 * m), 0.0])
        e3 = np.array([*(e * m), 0.0])
        hinge = np.array([*(chine_pt(w0, key) * m), zs(s_c)])
        centre = np.array([*(chine_pt(wc, key) * m), zs(s_c)]) - n3 * POD_DEPTH
        target = np.array([-sgn, 0.0])
        a0 = math.atan2(n3[1], n3[0])
        a1 = math.atan2(target[1], target[0])
        swing = a1 - a0
        if sgn > 0 and swing > 0:
            swing -= 2 * math.pi
        if sgn < 0 and swing < 0:
            swing += 2 * math.pi
        swing -= sgn * POD_CANT[s_c]
        cups = [centre + np.array([0, 0, -POD_L / 2]) + e3 * dy for dy in (-0.9, 0.0, 0.9)]
        out.append(dict(s=s_c, sgn=sgn, u=(u0, u1), n=n3, e=e3, hinge=hinge, swing=swing, centre=centre,
                        swivel_axis=n3 * sgn, cups=cups, s0=s_c - POD_DOOR_L / 2, s1=s_c + POD_DOOR_L / 2))
    return out


def stern_legs():
    """Per stern leg: hinge, swing axis, stand/rest angles and extensions, pad rotations. The leg lies on the outer
    side of its nacelle (30 deg off the horizontal), stowed along -z; it swings in its radial plane."""
    out = []
    mz = np.array([0.0, 0.0, -1.0])
    for name, ni, sx, sy in STERN_LEGS:
        cx, cy = NAC[ni]
        a_st = LEG_ANG_UP if sy > 0 else LEG_ANG
        n2 = np.array([sx * math.cos(a_st), sy * math.sin(a_st)])             # radial (outward) of the stowed leg
        P2 = np.array([cx, cy]) + R_N * n2                                     # nacelle surface under the leg
        H2 = P2 + (LEG_STANDOFF + LEG_THIGH_T / 2) * n2                       # thigh axis
        n3 = np.array([*n2, 0.0])
        e3 = np.array([-n2[1], n2[0], 0.0]) * sx * sy                          # across the leg (tangential)
        H = np.array([*H2, zs(LEG_S_H)])
        # standing foot: on the leg's radial line from the stern axis, at R, behind the stern
        F2 = np.array([0.0, YC]) + STAND_R * np.array([sx * math.cos(LEG_ANG), sy * math.sin(LEG_ANG)])  # legs outside the skin: the swing plane need not be radial
        F = np.array([F2[0], F2[1], zs(STAND_GROUND_S) + LEG_FOOT_H])
        d = F - H
        axis = unit(np.cross(mz, d))
        phi_stand = math.acos(float(mz @ unit(d)))
        e_stand = np.linalg.norm(d) - LEG_LMIN_S
        lower = sy < 0
        phi_rest = e_rest = 0.0
        if False:                      # T9: lying, the ship stands on the blades and the kangaroo leg - no stern-leg rest pose
            e_of = lambda ph: (H[1] - (REST_GROUND_Y + LEG_FOOT_H)) / -(rot(axis, ph) @ mz)[1] - LEG_LMIN_S
            lo, hi = math.radians(15.0), math.radians(90.0)
            for _ in range(60):
                mid = (lo + hi) / 2
                lo, hi = (mid, hi) if e_of(mid) > REST_EXT else (lo, mid)
            phi_rest = (lo + hi) / 2
            e_rest = e_of(phi_rest)
        B0 = np.column_stack([np.array([0, 0, 1.0]), -n3, np.cross(np.array([0, 0, 1.0]), -n3)])

        def pad_child(phi, long_t, up_t):
            Bt = np.column_stack([long_t, up_t, np.cross(long_t, up_t)])
            return axis_angle(rot(axis, phi).T @ (Bt @ B0.T))

        fd = np.array([F2[0], F2[1] - YC, 0.0]) / STAND_R
        fs_ax, fs_ang = pad_child(phi_stand, unit(fd), np.array([0, 0, 1.0]))
        fr_ax, fr_ang = pad_child(phi_rest, np.array([0, 0, 1.0]), np.array([0, 1.0, 0])) if lower else (np.array([1.0, 0, 0]), 0.0)
        assert 0.0 <= e_stand <= LEG_EXT_MAX and 0.0 <= e_rest <= LEG_EXT_MAX, (name, e_stand, e_rest)
        pad_s0 = LEG_S_H - LEG_LMIN_S - LEG_PAD_L / 2
        out.append(dict(name=name, H=H, axis=axis, phi_stand=phi_stand, e_stand=e_stand, phi_rest=phi_rest, e_rest=e_rest,
                        phi_max=max(phi_stand, phi_rest), lower=lower, rad=fd, n=n3, e=e3, pw=LEG_PAD_W,
                        fs_ax=fs_ax, fs_ang=fs_ang, fr_ax=fr_ax, fr_ang=fr_ang, nac=ni,
                        s0=pad_s0, s1=LEG_S_H + 1.0, pad_s1=pad_s0 + LEG_PAD_L))
    return out


PODS = pod_layout()
SLEGS = stern_legs()




# ---------------------------------------------------------------------------
# Openings in the skin (starboard definitions; port mirrored). s-limits may depend on u.


def _open(name, u0, u1, a, b, **kw):
    d = dict(name=name, u=(u0, u1), a=a, b=b)
    d.update(kw)
    return d


def _mirror(o, name):
    f = lambda x: (lambda u: x(mir(u))) if callable(x) else x
    d = dict(o)
    d.update(name=name, u=(mir(o["u"][1]), mir(o["u"][0])), a=f(o["a"]), b=f(o["b"]))
    return d


def ev(x, u):
    return x(u) if callable(x) else x


def openings():
    O = []
    lafet = _open("carriage_starboard", _SA.u_of(flank_pt(POCKET_T - PAD_W / 2 - 0.05)), _SA.u_of(flank_pt(POCKET_T + PAD_W / 2 + 0.05)),
                  LAF_S0, LAF_S1, depth=LAF_DEPTH)
    yb = -FL * wh_at(S_FAIR)[1]
    bay = _open("bay_starboard", _SA.u_of((BAY_X0, yb)), _SA.u_of((BAY_X1, yb + 0.15)), BAY_S0, BAY_S1)
    for o, n in ((lafet, "carriage_port"), (bay, "bay_port")):
        O += [o, _mirror(o, n)]
    for i, p in enumerate(PODS):
        O.append(_open(f"pod_{i}", p["u"][0], p["u"][1], p["s0"], p["s1"], depth=POD_DEPTH + POD_T / 2 + 0.1))
    S30 = sec(30.0)
    us = S30.u_of((SLOT_HALF, top_y(30.0)))
    O.append(_open("fin_slot", us, mir(us), 3.0, 48.5, depth=3.0))
    SF = sec(HANGAR_REF_S)
    W, H = wh_at(HANGAR_REF_S)
    K = key_wh(W, H)
    uh = SF.u_of((0.62 * W, (1 - FL) * H - 0.12 * H))
    O.append(_open("hangar_top", uh, mir(uh), HANGAR_S[0] + 0.5, HANGAR_S[1] - 0.5, depth=3.0))
    ub = SF.u_of(K["C"])                                   # bottom doors run up to the chine: the XR2 span is 23.9 m
    O.append(_open("hangar_bottom_starboard", 0.0, ub, HANGAR_S[0] + 0.5, HANGAR_S[1] - 0.5, depth=5.0))
    O.append(_open("hangar_bottom_port", mir(ub), 1.0, HANGAR_S[0] + 0.5, HANGAR_S[1] - 0.5, depth=5.0))
    SA = sec(AIRLOCK_S)
    KA = key_wh(*wh_at(AIRLOCK_S))
    fe = unit(KA["T"] - KA["C"])
    fp = lambda y: KA["C"] + fe * (y - KA["C"][1]) / fe[1]
    O.append(_open("airlock_door", mir(SA.u_of(fp(2.9))), mir(SA.u_of(fp(-1.9))), AIRLOCK_S - 1.4, AIRLOCK_S + 1.4))
    u0, u1 = nose_u_range(NOSE_CUP_S + 3.0, NOSE_CUP_X, NOSE_CUP_Y, NOSE_CUP_R + 0.4)
    nose_cup = _open("nose_cup_starboard", u0, u1, NOSE_CUP_S, NOSE_CUP_S1, depth=3.0)
    O += [nose_cup, _mirror(nose_cup, "nose_cup_port")]
    return O


OPENINGS = openings()
OPEN = {o["name"]: o for o in OPENINGS}


def overlays():
    """Flush dark patches (seals, hatches): name, u0, u1, s0, s1."""
    t_pin = (0.0 - _C[1]) / E_F[1]
    u0, u1 = _SA.u_of(flank_pt(t_pin - 2.5)), _SA.u_of(flank_pt(t_pin + 2.5))
    return [("hatches", u0, u1, *PIN_SLOT), ("hatches", mir(u1), mir(u0), *PIN_SLOT)]


# ---------------------------------------------------------------------------
# Surface builder: strips between u-grid lines, zipped between station lists, holes cut exactly


def _stations():
    st = list(np.arange(0.0, CLOVER_S1, 0.5)) + list(np.arange(CLOVER_S1, NB, 1.0)) + [NB]
    st += list(np.arange(NB, TIP_S - 2.0, 0.75)) + [TIP_S - 2.0 * (1 - k / 12) for k in range(12)] + [TIP_S - 0.03]
    st += [*PIN_SLOT]
    for o in OPENINGS:
        for x in (o["a"], o["b"]):
            if not callable(x):
                st.append(x)
    st = sorted(set(round(s, 6) for s in st))
    return [s for s in st if s <= TIP_S - 0.03]


STATIONS = _stations()
S_END = STATIONS[-1]


def _ugrid():
    us = list(np.linspace(0.0, 1.0, 145))
    edges = [u for o in OPENINGS for u in o["u"]] + [u for ov in overlays() for u in ov[1:3]] + [0.5]
    for e in edges:
        us = [u for u in us if abs(u - e) > 2e-3 or u in (0.0, 1.0)]
    us = sorted(set(round(u, 9) for u in us + edges))
    return us


UGRID = _ugrid()


def edge_points(u):
    pts = set(STATIONS)
    for o in OPENINGS:
        if o["u"][0] - 1e-9 <= u <= o["u"][1] + 1e-9:
            pts.add(round(ev(o["a"], u), 6))
            pts.add(round(ev(o["b"], u), 6))
    return sorted(p for p in pts if 0.0 <= p <= S_END)


def hull_keep(u, um):
    """Kept s-intervals of the skin at edge u of the strip whose middle is um."""
    iv = [(0.0, S_END)]
    for o in OPENINGS:
        if o["u"][0] < um < o["u"][1]:
            a, b = ev(o["a"], u), ev(o["b"], u)
            nxt = []
            for lo, hi in iv:
                if b <= lo or a >= hi:
                    nxt.append((lo, hi))
                    continue
                if a > lo:
                    nxt.append((lo, a))
                if b < hi:
                    nxt.append((b, hi))
            iv = nxt
    return iv


def surface(group_of, u_lo, u_hi, keep, off=0.0, inward=False, tip=False):
    """Skin (or an offset of it) over u_lo..u_hi, s-intervals from keep(u, um)."""
    grid = [u for u in UGRID if u_lo - 1e-9 <= u <= u_hi + 1e-9]
    for uL, uR in zip(grid, grid[1:]):
        um = (uL + uR) / 2
        IL, IR = keep(uL, um), keep(uR, um)
        assert len(IL) == len(IR), (uL, uR, IL, IR)
        eL, eR = edge_points(uL), edge_points(uR)
        for (aL, bL), (aR, bR) in zip(IL, IR):
            L = [s for s in eL if aL - 1e-6 <= s <= bL + 1e-6]
            R = [s for s in eR if aR - 1e-6 <= s <= bR + 1e-6]
            if len(L) < 1 or len(R) < 1 or (len(L) < 2 and len(R) < 2):
                continue
            i = j = 0
            cache = {}

            def V(g, s, u):
                key = (id(g), s, u)
                if key not in cache:
                    n = hull_n(s, u)
                    cache[key] = g.vert(hull_pt(s, u) + off * n, -n if inward else n)
                return cache[key]

            while i < len(L) - 1 or j < len(R) - 1:
                if j >= len(R) - 1 or (i < len(L) - 1 and L[i + 1] <= R[j + 1]):
                    tri = ((L[i], uL), (R[j], uR), (L[i + 1], uL))
                    i += 1
                else:
                    tri = ((L[i], uL), (R[j], uR), (R[j + 1], uR))
                    j += 1
                g = group_of(sum(t[0] for t in tri) / 3)
                idx = [V(g, s, u) for s, u in tri]
                out = sum(g.n[k] for k in idx)
                g.tri(*idx, out)
            if tip and abs(L[-1] - S_END) < 1e-6 and abs(R[-1] - S_END) < 1e-6:
                g = group_of(S_END)
                t = g.vert((0.0, _Y0, zs(TIP_S)), (0, 0, 1))
                idx = [V(g, L[-1], uL), V(g, R[-1], uR), t]
                g.tri(*idx, g.n[idx[0]] + g.n[idx[1]] + np.array([0, 0, 1.0]))


def opening_keep(o):
    return lambda u, um: [(ev(o["a"], u), ev(o["b"], u))]


def rect_keep(s0, s1):
    return lambda u, um: [(s0, s1)]


def patch(g, o, off=0.0, back=None):
    """Door/cover over opening o (flush); optional back face `back` m inside."""
    surface(lambda s: g, o["u"][0], o["u"][1], opening_keep(o), off=off)
    if back:
        surface(lambda s: g, o["u"][0], o["u"][1], opening_keep(o), off=-back, inward=True)


def liner(g, o, depth):
    """Dark recess behind opening o: back wall and side walls, all facing into the opening."""
    surface(lambda s: g, o["u"][0], o["u"][1], opening_keep(o), off=-depth)
    for u, sg in ((o["u"][0], 1), (o["u"][1], -1)):          # walls along the u-edges
        ss = [s for s in edge_points(u) if ev(o["a"], u) - 1e-6 <= s <= ev(o["b"], u) + 1e-6]
        for s0, s1 in zip(ss, ss[1:]):
            inn = hull_pt((s0 + s1) / 2, u + sg * 2e-3) - hull_pt((s0 + s1) / 2, u)
            p = [hull_pt(s0, u), hull_pt(s1, u), hull_pt(s1, u, -depth), hull_pt(s0, u, -depth)]
            g.quad(*[g.vert(q, inn) for q in p], inn)
    grid = [u for u in UGRID if o["u"][0] - 1e-9 <= u <= o["u"][1] + 1e-9]
    for end, sg in (("a", 1), ("b", -1)):
        if end == "a" and o.get("open_aft"):
            continue
        for u0, u1 in zip(grid, grid[1:]):
            s0, s1 = ev(o[end], u0), ev(o[end], u1)
            nrm = np.array([0, 0, sg * 1.0])
            p = [hull_pt(s0, u0), hull_pt(s1, u1), hull_pt(s1, u1, -depth), hull_pt(s0, u0, -depth)]
            g.quad(*[g.vert(q, nrm) for q in p], nrm)


# ---------------------------------------------------------------------------
# Groups (stable order = module contract, written to MeshLayout.h)

SIDES = ("port", "starboard")
GROUPS = (["hull", "shoulder", "nose", "spine", "fin", "fin_upper", "crest_port", "crest_starboard", "wing_outer_port",
           "wing_outer_starboard", "elevon_port", "elevon_starboard",
           "body_flap", "well", "baffle", "cups_anamezon"] + [f"iris_ana_{i}" for i in range(4)]
          + ["well_centre", "march_unit", "iris_march", "cups_nose", "iris_nose_0", "iris_nose_1"]
          + [f"door_pod_{i}" for i in range(4)] + [f"pod_{i}" for i in range(4)]
          + ["door_top_port", "door_top_starboard", "door_bottom_port", "door_bottom_starboard",
             "hangar_inner", "shuttle", "rover_platform", "hatches", "airlock", "pocket_liner"]
          + [f"{part}_{side}" for side in SIDES for part in ("carriage", "hip", "thigh", "ankle", "pad")]
          + [f"pin_{side}_{k}" for side in SIDES for k in range(PIN_N)]
          + [f"shin_{side}_{i}" for side in SIDES for i in range(SHIN_N)]
          + ["leg_hinges"]
          + [f"leg{i}_{part}" for i in range(4) for part in ["thigh"] + [f"shin{k}" for k in range(LEG_SHIN_N)] + ["ankle", "pad"]]
          + ["bay_liner", "bay_door_port", "bay_door_starboard"] + [f"trap_{i}" for i in range(4)]
          + [f"lift{c}_heads" for c in range(2)] + [f"lift{c}_m{i}" for c in range(2) for i in range(LIFT_N)]
          + [f"{part}_{side}" for side in SIDES for part in ("sole", "anchor")]
          + [f"leg{i}_{part}" for i in range(4) for part in ("sole", "anchor")])


def wing_panel_poly(sgn, r0, r1, x_of):
    """Planform of a wing part between spans r0 .. r1 (x, z) in the deployed pose."""
    return [(x_of(r0), zs(wing_te(r0))), (x_of(r0), zs(wing_le(r0))), (x_of(r1), zs(wing_le(r1))), (x_of(r1), zs(wing_te(r1)))]


def build():
    comps = rig(SLEGS)
    G = {name: None for name in GROUPS}

    def grp(name, mat):
        if G[name] is None:
            G[name] = Group(name, MAT[mat])
        return G[name]

    # ---- skin: stern and body, fairings (carriage legs and pods), nose
    hull, fairing, nose = grp("hull", "hull_lacquer"), grp("shoulder", "hull_lacquer"), grp("nose", "nose_iridium")
    zone = lambda s: fairing if 40.0 <= s <= 112.0 + INSERT_L else (nose if s >= NB else hull)
    surface(zone, 0.0, 1.0, hull_keep, tip=True)

    # ---- recesses behind every opening
    pl = grp("pocket_liner", "dark")
    for o in OPENINGS:
        if o.get("depth") and not o["name"].startswith("hangar"):
            liner(pl, o, o["depth"])
    hi = grp("hangar_inner", "structure")
    for n in ("hangar_top", "hangar_bottom_starboard", "hangar_bottom_port"):
        liner(hi, OPEN[n], OPEN[n]["depth"])

    # ---- fin slot lips (the spine): two rails along the slot, seals over the retracted fin
    g = grp("spine", "structure")
    for sgn in (-1, 1):
        for s0 in np.arange(3.0, 48.0, 3.0):
            a, b = hull_pt(s0, 0.5), hull_pt(s0 + 3.0, 0.5)
            x = sgn * (SLOT_HALF + 0.25)
            box(g, (x - 0.2, a[1] - 0.05, a[2]), (x + 0.2, a[1] + 0.25, b[2]))

    # ---- telescopic dorsal fin (reference: extended); the upper stage is thinner and nests into the lower one
    prism(grp("fin", "crest_radiator"), [(zs(s), FIN_BASE + h) for s, h in FIN_LOWER], (0, 0, 1), (0, 1, 0), (1, 0, 0), 0.0, FIN_T)
    prism(grp("fin_upper", "crest_radiator"), [(zs(s), FIN_BASE + h) for s, h in FIN_UPPER], (0, 0, 1), (0, 1, 0), (1, 0, 0), 0.0, FIN_T2)

    # ---- wings (reference: deployed, elevons neutral): inner panel, outer panel, elevons on the outer trailing edge
    for side, sgn in (("port", -1), ("starboard", 1)):
        X = lambda r, sgn=sgn: sgn * (WING_X0 + r)
        prism(grp(f"crest_{side}", "crest_radiator"), wing_panel_poly(sgn, 0.0, WING_B1 - 0.05, X), (1, 0, 0), (0, 0, 1), (0, 1, 0),
              WING_Y, WING_T)
        ra = WING_B1 + 0.05
        poly = [(X(ra), zs(wing_h(ra))), (X(ra), zs(wing_le(ra))), (X(WING_B), zs(wing_le(WING_B))), (X(WING_B), zs(wing_h(WING_B)))]
        prism(grp(f"wing_outer_{side}", "crest_radiator"), poly, (1, 0, 0), (0, 0, 1), (0, 1, 0), WING_Y, WING_T)
        el = [(X(ra), zs(wing_te(ra))), (X(ra), zs(wing_h(ra) - 0.08)), (X(WING_B), zs(wing_h(WING_B) - 0.08)), (X(WING_B), zs(wing_te(WING_B)))]
        prism(grp(f"elevon_{side}", "mechanism"), el, (1, 0, 0), (0, 0, 1), (0, 1, 0), WING_Y, WING_T * 0.7)
        # hinge fairing along the root (in the side trough)
        tube(grp(f"crest_{side}", "crest_radiator"), (sgn * WING_X0, WING_Y, zs(0.3)), (sgn * WING_X0, WING_Y, zs(WING_LE0 - 0.3)), 0.6, n=12)
        g = grp("spine", "structure")                         # hinge longeron on the side web between the nacelles
        x0, x1 = sorted((sgn * (RW - 0.3), sgn * (WING_X0 - 0.5)))
        box(g, (x0, WING_Y - 0.5, zs(0.5)), (x1, WING_Y + 0.5, zs(CLOVER_S1 + 2.0)))
    box(grp("body_flap", "nose_iridium"), (-FLAP_HALF + 0.05, FLAP_Y, zs(FLAP_S0)), (FLAP_HALF - 0.05, FLAP_Y + FLAP_T, zs(FLAP_S1 - 0.05)))
    g = grp("body_flap", "nose_iridium")
    gs = grp("spine", "structure")
    for x in (-FLAP_HALF + 1.0, 0.0, FLAP_HALF - 1.0):        # hinge brackets on the lower web (fixed; the flap turns below)
        box(gs, (x - 0.3, FLAP_Y + FLAP_T + 0.35, zs(FLAP_S1 - 0.9)), (x + 0.3, YC - RW + 0.3, zs(FLAP_S1 + 0.3)))

    # ---- four nacelles: shell, rim, anamezon cups with irises, planetary cups with irises
    g = grp("well", "engine_metal")
    for cx, cy in NAC:
        lathe(g, [(RIM_S, R_N), (CLOVER_S1 + 2.0, R_N)], center=(cx, cy))
    g = grp("baffle", "nose_iridium")
    for cx, cy in NAC:
        lathe(g, [(RIM_S - 0.02, ANA_R + 0.05), (RIM_S - 0.02, R_N), (RIM_S + 0.3, R_N + 0.03)], center=(cx, cy))
    g = grp("cups_anamezon", "boron_nitride")
    for x, y in ANA_CUPS:
        cup(g, x, y, RIM_S + 0.05, ANA_DEPTH, ANA_R)
    for i, (x, y) in enumerate(ANA_CUPS):
        g = grp(f"iris_ana_{i}", "nose_iridium")
        disc_n(g, (x, y, zs(RIM_S - 0.05)), (0, 0, -1), ANA_R + 0.1, seg=24)
        disc_n(g, (x, y, zs(RIM_S)), (0, 0, 1), ANA_R + 0.1, seg=24)
    # central well: collar (cup ceramic, it sees the reaction zones at grazing angles) and the deep metal well
    g = grp("baffle", "nose_iridium")
    lathe(g, [(RIM_S - 0.02, WELL_C_R - 0.05), (RIM_S - 0.02, WELL_C_R + 0.3)], center=(0.0, YC))   # rim ring
    g = grp("well_centre", "engine_metal")
    lathe(g, [(RIM_S, WELL_C_R), (WELL_C_DEPTH_S, WELL_C_R), (WELL_C_DEPTH_S, 0.3)], center=(0.0, YC), inward=True)
    # marching planetary cup on its sliding mount (reference: stowed, lip at MARCH_LIP_S under the shut well iris)
    g = grp("march_unit", "engine_metal")
    cup(g, 0.0, YC, MARCH_LIP_S, MARCH_D, MARCH_R)
    b0, b1 = MARCH_LIP_S + MARCH_D, MARCH_LIP_S + MARCH_D + MARCH_BODY_L
    lathe(g, [(b0 - 0.4, MARCH_R + 0.1), (b0 - 0.4, MARCH_R + 0.45), (b0, MARCH_R + 0.45), (b0, MARCH_BODY_R), (b1, MARCH_BODY_R), (b1, 0.0)],
          center=(0.0, YC))                                                       # coil ring, housing, end cap
    for k in range(4):                                                            # slide shoes on the well wall
        a = math.pi / 4 + k * math.pi / 2
        c = np.array([0.0, YC, 0.0]) + np.array([math.cos(a), math.sin(a), 0.0]) * (WELL_C_R - 0.12)
        obox(g, c + np.array([0, 0, zs(b0 + 2.0)]), (math.cos(a), math.sin(a), 0), (-math.sin(a), math.cos(a), 0), (0, 0, 1), 0.1, 0.5, 1.5)
    g = grp("iris_march", "nose_iridium")
    disc_n(g, (0.0, YC, zs(MARCH_IRIS_S)), (0, 0, -1), WELL_C_R + 0.02, seg=32)
    disc_n(g, (0.0, YC, zs(MARCH_IRIS_S + 0.05)), (0, 0, 1), WELL_C_R + 0.02, seg=32)
    # nose retro cups behind their skin openings; the covers (petals) are the irises
    g = grp("cups_nose", "boron_nitride")
    for sgn in (1, -1):
        cup_fwd(g, sgn * NOSE_CUP_X, NOSE_CUP_Y, NOSE_CUP_S, NOSE_CUP_D, NOSE_CUP_R)
        lathe(g, [(NOSE_CUP_S, NOSE_CUP_R + 0.05), (NOSE_CUP_S + 1.2, NOSE_CUP_R + 0.05)], center=(sgn * NOSE_CUP_X, NOSE_CUP_Y), inward=True)
    patch(grp("iris_nose_0", "nose_iridium"), OPEN["nose_cup_starboard"])
    patch(grp("iris_nose_1", "nose_iridium"), OPEN["nose_cup_port"])

    # ---- planetary pods (reference: stowed in the bays, door closed, cups aft)
    for i, p in enumerate(PODS):
        patch(grp(f"door_pod_{i}", "hull_lacquer"), OPEN[f"pod_{i}"])
        g = grp(f"pod_{i}", "engine_metal")
        c, e, nn = p["centre"], p["e"], p["n"]
        sec2 = [(-1.5, -0.9), (0.9, -0.9), (1.5, -0.3), (1.5, 0.9), (-1.5, 0.9)]
        prism(g, [(float(c @ e) + a, float(c @ nn) + b) for a, b in sec2], e, nn, (0, 0, 1), c[2], POD_L)
        for c in p["cups"]:
            cup(g, c[0], c[1], c[2] - STERN_Z - 0.02, 0.4, 0.42, seg=10)
        tube(g, p["centre"] + p["n"] * (POD_T / 2 - 0.05), p["centre"] + p["n"] * (POD_DEPTH - 0.2), 0.9, n=12)

    # ---- hangar (closed port for an XR2 / DG-IV class craft): doors, floor, cradle, lowering platform.
    # The visiting craft itself is not part of this mesh.
    ht = OPEN["hangar_top"]
    half = lambda o, lo, hi: dict(o, u=(lo, hi))
    patch(grp("door_top_starboard", "hull_lacquer"), half(ht, ht["u"][0], 0.5))
    patch(grp("door_top_port", "hull_lacquer"), half(ht, 0.5, ht["u"][1]))
    patch(grp("door_bottom_starboard", "hull_lacquer"), OPEN["hangar_bottom_starboard"])
    patch(grp("door_bottom_port", "hull_lacquer"), OPEN["hangar_bottom_port"])
    hs0, hs1 = HANGAR_S
    Wh, Hh = wh_at(HANGAR_REF_S)
    y_ch = -(FL - 0.233) * Hh
    box(hi, (-Wh + 1.2, y_ch + 5.6, zs(hs0 + 0.5)), (Wh - 1.2, y_ch + 6.0, zs(hs1 - 0.5)))          # ceiling frame
    g = grp("shuttle", "mechanism")                                                                 # docking cradle
    for z0 in (hs0 + 3.0, hs1 - 5.0):
        box(g, (-2.5, y_ch - 1.6, zs(z0)), (2.5, y_ch - 1.2, zs(z0 + 2.0)))
        box(g, (-0.4, y_ch - 1.2, zs(z0 + 0.6)), (0.4, y_ch + 0.2, zs(z0 + 1.4)))
    g = grp("rover_platform", "mechanism")
    yb = -FL * Hh
    box(g, (-4.5, yb + 0.25, zs(hs0 + 1.5)), (4.5, yb + 0.5, zs(hs1 - 1.5)))
    for s0 in (hs0 + 3.0, hs0 + 11.0):
        box(g, (-2.35, yb + 0.5, zs(s0)), (-1.35, yb + 1.8, zs(s0 + 7)))
        box(g, (1.35, yb + 0.5, zs(s0)), (2.35, yb + 1.8, zs(s0 + 7)))

    # ---- flush patches: pin-slot seals; airlock door
    for name, u0, u1, s0, s1 in overlays():
        surface(lambda s, n=name: grp(n, "hull_lacquer"), u0, u1, rect_keep(s0, s1), off=0.01)
    patch(grp("airlock", "hull_lacquer"), OPEN["airlock_door"])

    # ---- carriage legs (reference: hip out at the track start, leg hanging, shin out, pad open)
    for side, sgn in (("port", -1), ("starboard", 1)):
        T = np.array([sgn * HIP_X_OUT, 0.0, zs(CAR_SREF)])
        xs = lambda a, b: (min(sgn * a, sgn * b), max(sgn * a, sgn * b))
        g = grp(f"carriage_{side}", "mechanism")                 # shoe on the rails with the roll trunnion
        x0, x1 = xs(10.5, PIN_X0)
        box(g, (x0, -2.2, T[2] - 2.2), (x1, 2.2, T[2] + 2.2))
        tube(g, (sgn * PIN_X0, 0, T[2] - 2.4), (sgn * PIN_X0, 0, T[2] + 2.4), 0.5, n=12)
        for k in range(PIN_N):
            gk = grp(f"pin_{side}_{k}", "mechanism")
            x0, x1 = xs(PIN_X0 + k * PIN_STEP, PIN_X0 + k * PIN_STEP + PIN_LEN)
            hy, hz = 2.5 - 0.15 * k, 1.5 - 0.15 * k
            box(gk, (x0, -hy, T[2] - hz), (x1, hy, T[2] + hz))
        g = grp(f"hip_{side}", "mechanism")                     # hip block with the pitch bearing
        bi, bo, bh, bl = HIP_BLOCK
        x0, x1 = xs(HIP_X_OUT - bi, HIP_X_OUT + bo)
        box(g, (x0, -bh, T[2] - bl), (x1, bh, T[2] + bl))
        tube(g, T - np.array([sgn * 1.0, 0, 0]), T + np.array([sgn * 1.45, 0, 0]), 1.3, n=20)
        for xr in (-1.0, 1.2):                                  # catcher (sliding) bearing rings of the magnetic bearing
            tube(g, T + np.array([sgn * xr, 0, 0]), T + np.array([sgn * (xr + 0.25), 0, 0]), 1.42, n=20)
        # cap: closes the pocket ahead of the stowed pad; built in the flank plane at the stowed pose
        M = comp_matrix(comps, stowed_states(), comp_index(comps, f"slide_{side}", "hip"))
        Minv = np.linalg.inv(M)
        e3 = np.array([sgn * E_F[0], E_F[1], 0.0])
        n3 = np.array([sgn * N_F[0], N_F[1], 0.0])
        c2 = flank_pt(POCKET_T)
        s_c0, s_c1 = STOW_S - LEG_LMIN + PAD_L / 2 + 0.05, LAF_S1 - 0.05
        c = np.array([sgn * c2[0], c2[1], zs((s_c0 + s_c1) / 2)]) - n3 * 0.15
        corners = []
        for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            q = c + e3 * a * PAD_W / 2 + np.array([0, 0, b * (s_c1 - s_c0) / 2])
            corners.append((Minv @ np.append(q, 1.0))[:3])
        cc = sum(corners) / 4
        nn = Minv[:3, :3] @ n3
        ee = Minv[:3, :3] @ e3
        obox(g, cc, ee, nn, Minv[:3, :3] @ np.array([0, 0, 1.0]), PAD_W / 2, 0.15, (s_c1 - s_c0) / 2)
        obox(g, cc - nn * 0.9, ee, nn, Minv[:3, :3] @ np.array([0, 0, 1.0]), 0.6, 0.75, (s_c1 - s_c0) / 2 - 0.2)
        g = grp(f"thigh_{side}", "hull_lacquer")
        obox(g, T + np.array([0, -THIGH_L / 2, 0]), (1, 0, 0), (0, 1, 0), (0, 0, 1), THIGH_T / 2, THIGH_L / 2, THIGH_W / 2)
        for i in range(SHIN_N):
            g = grp(f"shin_{side}_{i}", "band")
            k = 1.0 - 0.012 * i
            yc = -(SHIN_TOP + i * SHIN_STEP + SHIN_SEG / 2)
            obox(g, T + np.array([0, yc, 0]), (1, 0, 0), (0, 1, 0), (0, 0, 1), SHIN_T / 2 * k, SHIN_SEG / 2, SHIN_W / 2 * k)
        A = T + np.array([0, -LEG_LMAX, 0])
        g = grp(f"ankle_{side}", "mechanism")                   # two-axis ankle: fore-aft and lateral axles
        tube(g, A - np.array([0, 0, 0.9]), A + np.array([0, 0, 0.9]), ANKLE_R, n=12)
        tube(g, A - np.array([0.8, 0, 0]), A + np.array([0.8, 0, 0]), ANKLE_R * 0.8, n=12)
        tube(g, A, A + np.array([0, ANKLE_R + STRUT_EXT_C + 1.0, 0]), 0.55, n=12)   # strut rod into the mast end
        g = grp(f"pad_{side}", "mechanism")                     # pad on a spreader fork (supports at +-4 m)
        plate(g, A + np.array([0, -PAD_OFF + SOLE_T / 2, 0]), (0, 0, 1), (1, 0, 0), (0, 1, 0), PAD_L / 2, PAD_W / 2,
              PAD_T - SOLE_T, PAD_RC)
        yb = A[1] - PAD_OFF - PAD_T / 2                         # ground face
        plate(grp(f"sole_{side}", "dark"), A + np.array([0, -PAD_OFF - (PAD_T - SOLE_T) / 2, 0]), (0, 0, 1), (1, 0, 0),
              (0, 1, 0), PAD_L / 2 - 0.15, PAD_W / 2 - 0.15, SOLE_T, PAD_RC + 0.15)
        ga = grp(f"anchor_{side}", "nose_iridium")              # 6 anchors, retracted inside the pad
        for dz in (-5.5, 0.0, 5.5):
            for dx in (-2.0, 2.0):
                tube(ga, (A[0] + dx, yb + 0.02, A[2] + dz), (A[0] + dx, yb + 0.02 + ANCHOR_L, A[2] + dz), 0.06, n=6, r1=0.22)
        top = A[1] - PAD_OFF + PAD_T / 2
        box(g, (A[0] - 0.5, top, A[2] - 4.4), (A[0] + 0.5, top + 0.2, A[2] + 4.4))
        box(g, (A[0] - 0.5, top, A[2] - 0.7), (A[0] + 0.5, A[1] - ANKLE_R * 0.6, A[2] + 0.7))

    # ---- anamezon port: two belly bays under the trap columns, armoured doors, liner walls
    g = grp("bay_liner", "dark")
    yb = -FL * wh_at(S_FAIR)[1]
    for sgn in (-1, 1):
        for xw in (BAY_X0, BAY_X1 - 0.15):
            box(g, (sgn * xw - 0.15, yb, zs(BAY_S0)), (sgn * xw + 0.15, yb + 6.0, zs(BAY_S1)))
        for s_end in (BAY_S0, BAY_S1):
            box(g, (min(sgn * BAY_X0, sgn * BAY_X1), yb + 0.1, zs(s_end) - 0.15), (max(sgn * BAY_X0, sgn * BAY_X1), yb + 6.0, zs(s_end) + 0.15))
    for side in SIDES:
        patch(grp(f"bay_door_{side}", "nose_iridium"), OPEN[f"bay_{side}"], back=0.3)
    for i, (x, y) in enumerate(TRAP_XY):
        trap_geom(grp(f"trap_{i}", "trap_shell"), x, y, CASS_S0, CASS_S1)
    for c, xc in enumerate((4.75, -4.75)):
        g = grp(f"lift{c}_heads", "mechanism")
        for sh, sg in zip(HEAD_S, (-1, 1)):
            z = zs(sh)
            box(g, (xc - 1.5, LIFT_Y0 - 1.9, z - 0.6), (xc + 1.5, LIFT_Y0 - 0.9, z + 0.6))
            box(g, (xc - 1.5, LIFT_Y0 + 0.9, z - 0.6), (xc + 1.5, LIFT_Y0 + 1.9, z + 0.6))
            box(g, (xc - 1.5, LIFT_Y0 - 1.9, z - sg * 0.6 - (0.4 if sg > 0 else 0)),
                (xc + 1.5, LIFT_Y0 + 1.9, z - sg * 0.6 + (0.4 if sg < 0 else 0)))
        for i in range(LIFT_N):
            g = grp(f"lift{c}_m{i}", "band")
            w = 1.1 - 0.06 * i
            for sh in HEAD_S:
                tube(g, (xc, LIFT_CEIL, zs(sh)), (xc, LIFT_CEIL - LIFT_SEG, zs(sh)), w / 2 * math.sqrt(2), n=4)

    # ---- stern legs (reference: stowed aft on the nacelle sides over their heat shields, pads folded ground face out)
    g = grp("leg_hinges", "nose_iridium")
    for L in SLEGS:
        tube(g, L["H"] - 1.4 * L["axis"], L["H"] + 1.4 * L["axis"], 1.2, n=16)
        for a0 in (-1.4, 1.15):                                        # catcher rings of the magnetic bearing
            tube(g, L["H"] + a0 * L["axis"], L["H"] + (a0 + 0.25) * L["axis"], 1.32, n=16)
        n3 = L["n"]                                                    # pylon from the body to the hinge
        r_h = float(np.linalg.norm(L["H"][:2] - np.array([0.0, YC])))
        c = np.array([0.0, YC, L["H"][2]]) + n3 * (r_h - 2.4) / 1.0 * 0.5 + n3 * 3.6
        obox(g, np.array([0.0, YC, 0.0]) + n3 * (r_h - 1.5) + np.array([0, 0, L["H"][2] - 0.5]), L["e"], n3, (0, 0, 1),
             1.2, 2.2, 2.5)
        cx, cy = NAC[L["nac"]]                                        # heat-shield stack under the stowed leg
        n3 = L["n"]
        base = np.array([cx, cy, 0.0]) + n3 * (R_N + LEG_STANDOFF / 2)
        obox(g, base + np.array([0, 0, zs((L["s0"] + LEG_S_H) / 2)]), L["e"], n3, (0, 0, 1), LEG_PAD_W / 2, LEG_STANDOFF / 2,
             (LEG_S_H - L["s0"]) / 2)
    mz = np.array([0, 0, -1.0])
    for i, L in enumerate(SLEGS):
        H, ax = L["H"], L["axis"]
        th = unit(np.cross(ax, mz))
        obox(grp(f"leg{i}_thigh", "hull_lacquer"), H + mz * LEG_THIGH_L / 2, ax, th, mz,
             LEG_THIGH_W / 2, LEG_THIGH_T / 2, LEG_THIGH_L / 2)
        for k in range(LEG_SHIN_N):
            f = 1 - 0.04 * k
            obox(grp(f"leg{i}_shin{k}", "band"), H + mz * (SHIN_TOP + SHIN_SEG / 2), ax, th, mz,
                 LEG_SHIN_W / 2 * f, LEG_SHIN_T / 2 * f, SHIN_SEG / 2)
        fc = H + mz * LEG_LMIN_S
        g = grp(f"leg{i}_ankle", "mechanism")
        tube(g, fc - ax * 0.9, fc + ax * 0.9, ANKLE_R, n=12)
        tube(g, fc, fc - mz * (ANKLE_R + STRUT_EXT_S + 0.9), 0.4, n=12)        # strut rod into the shin end
        g = grp(f"leg{i}_pad", "mechanism")
        pc = fc + L["n"] * LEG_PAD_OFF
        n3 = L["n"]
        # rigid part; the sole on the ground side (+n)
        prof = [(PAD_T / 2 - SOLE_T, -LEG_PAD_L / 2), (PAD_T / 2 - SOLE_T, LEG_PAD_L / 2), (-PAD_T / 2, LEG_PAD_L / 2 - 0.7),
                (-PAD_T / 2, -LEG_PAD_L / 2 + 0.7)]
        prism(g, [(float(pc @ n3) + a, pc[2] + b) for a, b in prof], n3, (0, 0, 1), L["e"], float(pc @ L["e"]), L["pw"])
        prof = [(PAD_T / 2, -LEG_PAD_L / 2 + 0.15), (PAD_T / 2, LEG_PAD_L / 2 - 0.15), (PAD_T / 2 - SOLE_T, LEG_PAD_L / 2 - 0.15),
                (PAD_T / 2 - SOLE_T, -LEG_PAD_L / 2 + 0.15)]
        prism(grp(f"leg{i}_sole", "dark"), [(float(pc @ n3) + a, pc[2] + b) for a, b in prof], n3, (0, 0, 1), L["e"],
              float(pc @ L["e"]), L["pw"] - 0.3)
        ga = grp(f"leg{i}_anchor", "nose_iridium")                             # 4 anchors, retracted inside the pad
        face = pc + n3 * (PAD_T / 2 - 0.02)
        for dz in (-3.0, 3.0):
            for de in (-0.6, 0.6):
                q = face + np.array([0, 0, dz]) + L["e"] * de
                tube(ga, q, q - n3 * ANCHOR_L, 0.06, n=6, r1=0.2)
        tube(g, fc + L["n"] * 0.3, fc + L["n"] * (LEG_PAD_OFF - PAD_T / 2), 0.5, n=12)
    missing = [n for n in GROUPS if G[n] is None]
    assert not missing, missing
    return [G[name] for name in GROUPS], SLEGS


# ---------------------------------------------------------------------------
# Animation rig (the same one the C++ code builds; used here for previews and checks)


def rig(legs):
    """Orbiter rig. Each group is owned by exactly ONE component; parents move their children
    (groups and pivots) automatically, so a parent never lists a child's groups. Components with
    no groups are pure pivots (C++: a LOCALVERTEXLIST dummy). d = the state the mesh is built at.
    Entry: anim, kind (rot: ref, axis, ang | tr: shift | sc: ref, scale), groups, parent."""
    C = []

    def add(anim, kind, groups, par, parent=None, s0=0.0, s1=1.0, d=0.0):
        C.append(dict(anim=anim, kind=kind, groups=groups, par=par, parent=parent, s0=s0, s1=s1, d=d))
        return len(C) - 1

    fl = add("crest_dorsal", "tr", ["fin"], np.array([0, -FIN_DROP, 0]))
    add("crest_dorsal", "tr", ["fin_upper"], np.array([0, -FIN_DROP, 0]), parent=fl)      # nests into the lower stage
    ed = ELEV_UP / (ELEV_UP + ELEV_DOWN)
    for side, sgn in (("port", -1), ("starboard", 1)):
        f = add("crest_lateral", "rot", [f"crest_{side}"], (np.array([sgn * WING_X0, WING_Y, 0]), np.array([0, 0, 1.0]), sgn * WING_FOLD))
        # outer panel: hinge on the lower face at the end of the inner panel; folds back under it (outboard when the
        # inner panel stands up)
        o = add("wing_outer", "rot", [f"wing_outer_{side}"], (np.array([sgn * (WING_X0 + WING_B1), WING_Y - WING_T / 2, 0]),
                                                               np.array([0, 0, 1.0]), -sgn * math.pi * 0.985), parent=f)
        ra = WING_B1 + 0.05
        h0 = np.array([sgn * (WING_X0 + ra), WING_Y, zs(wing_h(ra))])
        h1 = np.array([sgn * (WING_X0 + WING_B), WING_Y, zs(wing_h(WING_B))])
        ax = unit(h1 - h0)
        down = -1.0 if (np.cross(ax, [0, 0, -1.0])[1] > 0) else 1.0
        add(f"elevon_{side}", "rot", [f"elevon_{side}"], (h0, ax, down * math.radians(ELEV_UP + ELEV_DOWN)), parent=o, d=ed)
    add("body_flap", "rot", ["body_flap"], (np.array([0, FLAP_Y + FLAP_T / 2, zs(FLAP_S1)]), np.array([1.0, 0, 0]), -math.radians(FLAP_DOWN)))
    # pods: 1 = stowed (mesh pose); the swing opens the door and hangs the pod out
    for i, p in enumerate(PODS):
        sw = add("pod_retract", "rot", [f"door_pod_{i}"], (p["hinge"], np.array([0, 0, 1.0]), -p["swing"]), d=1.0)
        add("pod_swivel", "rot", [f"pod_{i}"], (p["centre"], p["swivel_axis"], POD_SWIVEL_MAX), parent=sw)
    for i, (x, y) in enumerate(ANA_CUPS):
        add("iris_ana", "sc", [f"iris_ana_{i}"], (np.array([x, y, zs(RIM_S - 0.05)]), np.array([0.001, 0.001, 1])))
    add("iris_march", "sc", ["iris_march"], (np.array([0.0, YC, zs(MARCH_IRIS_S)]), np.array([0.001, 0.001, 1])))
    add("march_slide", "tr", ["march_unit"], np.array([0, 0, -MARCH_TRAVEL]))            # 1 = run out past the rims
    for i, sgn in enumerate((1, -1)):                                                     # petals draw into the cup axis
        add("iris_nose", "sc", [f"iris_nose_{i}"], (np.array([sgn * NOSE_CUP_X, NOSE_CUP_Y, zs(NOSE_CUP_S + 1.0)]), np.array([0.001, 0.001, 0.2])))
    ht = OPEN["hangar_top"]
    for name, u, ang in (("door_top_starboard", ht["u"][0], -105), ("door_top_port", ht["u"][1], 105),
                         ("door_bottom_starboard", OPEN["hangar_bottom_starboard"]["u"][1], 95),
                         ("door_bottom_port", OPEN["hangar_bottom_port"]["u"][0], -95)):
        hp = hull_pt(HANGAR_REF_S, u)
        add("hangar", "rot", [name], (np.array([hp[0], hp[1], 0.0]), np.array([0, 0, 1.0]), math.radians(ang)))
    add("rover_lift", "tr", ["rover_platform"], np.array([0, (-AXIS_H + 0.25) - (-FL * wh_at(HANGAR_REF_S)[1] + 0.25), 0]))
    for side, sgn in (("port", -1), ("starboard", 1)):
        T = np.array([sgn * HIP_X_OUT, 0.0, zs(CAR_SREF)])
        run = np.array([0, 0, CAR_S1 - CAR_S0])
        tr = add(f"track_{side}", "tr", [], run)                                              # pivot only
        add(f"track_{side}", "tr", [f"carriage_{side}"], run)
        P = np.array([sgn * PIN_X0, 0.0, T[2]])
        ro = add(f"slide_{side}", "rot", [f"pin_{side}_0"], (P, np.array([0, 0, 1.0]), sgn * ROLL), parent=tr, s0=0.0, s1=SLIDE_ROLL)
        for k in range(1, PIN_N):                                                              # pin stages nest
            add(f"slide_{side}", "tr", [f"pin_{side}_{k}"], np.array([-sgn * k * PIN_STEP, 0, 0]), parent=ro, s0=SLIDE_ROLL)
        sl = add(f"slide_{side}", "tr", [f"hip_{side}"], np.array([-sgn * TRAVEL, 0, 0]), parent=ro, s0=SLIDE_ROLL)
        pi = add(f"pitch_{side}", "rot", [f"thigh_{side}"], (T, np.array([1.0, 0, 0]), math.pi / 2), parent=sl)  # 1 = aft
        for i in range(SHIN_N):
            add(f"shin_len_{side}", "tr", [f"shin_{side}_{i}"], np.array([0, i * SHIN_STEP, 0]), parent=pi)
        ank = add(f"shin_len_{side}", "tr", [], np.array([0, (SHIN_N - 1) * SHIN_STEP, 0]), parent=pi)   # pivot only
        st = add("strut_carriage", "tr", [f"ankle_{side}"], np.array([0, -STRUT_EXT_C, 0]), parent=ank)  # 1 = unloaded
        A = T + np.array([0, -LEG_LMAX, 0])
        fb = add(f"pad_fold_{side}", "rot", [], (A, np.array([0, 1.0, 0]), -sgn * math.pi / 2), parent=st)  # pivot only
        pd = add(f"pad_fold_{side}", "rot", [f"pad_{side}"], (A, np.array([1.0, 0, 0]), math.pi / 2), parent=fb)
        add("anchor_carriage", "tr", [f"sole_{side}"], np.zeros(3), parent=pd)                 # the sole rides on the pad
        add("anchor_carriage", "tr", [f"anchor_{side}"], np.array([0, -ANCHOR_OUT, 0]), parent=pd)
    mz = np.array([0, 0, -1.0])
    for i, L in enumerate(legs):
        sw = add(f"leg{i}_swing", "rot", [f"leg{i}_thigh"], (L["H"], L["axis"], L["phi_max"]))
        for k in range(LEG_SHIN_N):
            add(f"leg{i}_ext", "tr", [f"leg{i}_shin{k}"], mz * SHIN_STEP * (k + 1), parent=sw)
        ex = add(f"leg{i}_ext", "tr", [], mz * LEG_EXT_MAX, parent=sw)                              # pivot only
        st = add(f"leg{i}_strut", "tr", [f"leg{i}_ankle"], mz * STRUT_EXT_S, parent=ex)           # 1 = unloaded
        fc = L["H"] + mz * LEG_LMIN_S
        # the pad turns to the ground only once the ankle is clear of the hull (rest: half way; stand: last quarter,
        # the ankle is then behind the stern and beyond the well skirt)
        fr = add(f"leg{i}_foot_rest", "rot", [], (fc, L["fr_ax"], L["fr_ang"]), parent=st, s0=0.5)   # pivot only
        pd = add(f"leg{i}_foot_stand", "rot", [f"leg{i}_pad"], (fc, L["fs_ax"], L["fs_ang"]), parent=fr, s0=0.75)
        add(f"leg{i}_anchor", "tr", [f"leg{i}_sole"], np.zeros(3), parent=pd)
        add(f"leg{i}_anchor", "tr", [f"leg{i}_anchor"], L["n"] * ANCHOR_OUT, parent=pd)
    add("bay_doors", "rot", ["bay_door_starboard"], (bay_hinge(1), np.array([0, 0, 1.0]), math.radians(100)))
    add("bay_doors", "rot", ["bay_door_port"], (bay_hinge(-1), np.array([0, 0, 1.0]), -math.radians(100)))
    step = LIFT_TRAVEL / (LIFT_N - 1)
    for c in range(2):
        add(f"lift{c}", "tr", [f"lift{c}_heads"], np.array([0, -LIFT_TRAVEL, 0]))
        for i in range(1, LIFT_N):
            add(f"lift{c}", "tr", [f"lift{c}_m{i}"], np.array([0, -i * step, 0]))
    for i, (x, y) in enumerate(TRAP_XY):
        lf = add(f"trap{i}_lift", "tr", [], np.array([0, (LIFT_Y0 - LIFT_TRAVEL) - y, 0]))
        add(f"trap{i}_hide", "sc", [f"trap_{i}"], (np.array([x, y, zs((CASS_S0 + CASS_S1) / 2)]), np.array([0.001, 0.001, 0.001])),
            parent=lf)
    return C


def bay_hinge(sgn):
    o = OPEN["bay_starboard" if sgn > 0 else "bay_port"]
    p = hull_pt(55.0, o["u"][1] if sgn > 0 else o["u"][0])
    return np.array([p[0], p[1], 0.0])


def comp_index(comps, anim, group):
    for i, c in enumerate(comps):
        if c["anim"] == anim and group in [g.split("_")[0] for g in c["groups"]]:
            return i
    raise KeyError((anim, group))


def comp_matrices(comps, states):
    M = {}

    def mat(i):
        if i in M:
            return M[i]
        c = comps[i]
        d = c.get("d", 0.0)
        st = states.get(c["anim"], d)
        f = min(1.0, max(0.0, (st - c["s0"]) / (c["s1"] - c["s0"])))
        f -= min(1.0, max(0.0, (d - c["s0"]) / (c["s1"] - c["s0"])))  # the mesh sits at the default state
        T = np.eye(4)
        if c["kind"] == "rot":
            ref, ax, ang = c["par"]
            R = rot(ax, ang * f)
            T[:3, :3] = R
            T[:3, 3] = ref - R @ ref
        elif c["kind"] == "tr":
            T[:3, 3] = c["par"] * f
        else:
            ref, sc = c["par"]
            S = np.diag(1 + (sc - 1) * f)
            T[:3, :3] = S
            T[:3, 3] = ref - S @ ref
        M[i] = (mat(c["parent"]) @ T) if c["parent"] is not None else T
        return M[i]

    for i in range(len(comps)):
        mat(i)
    return M


def comp_matrix(comps, states, i):
    return comp_matrices(comps, states)[i]


def apply_pose(groups, comps, states):
    """Vertices after the rig at the given states: each group gets parent chain @ own transform."""
    V = {g.name: np.array(g.v, float) for g in groups}
    M = comp_matrices(comps, states)
    for i, c in enumerate(comps):
        for gname in c["groups"]:
            m = M[i]
            V[gname] = (m[:3, :3] @ V[gname].T).T + m[:3, 3]
    return V


# ---------------------------------------------------------------------------
# Output


def write_json(groups, path):
    """Raw groups for the Blender refinement pass (tools/blender_refine.py)."""
    import json
    data = {"materials": [{"name": n, "diffuse": d, "specular": sp, "emissive": e} for n, d, sp, e in MATERIALS],
            "groups": [{"name": g.name, "material": g.material,
                        "v": np.round(np.array(g.v), 4).tolist(), "t": [list(t) for t in g.t]} for g in groups]}
    with open(path, "w") as f:
        json.dump(data, f)


def write_msh(groups, path):
    with open(path, "w", newline="\r\n") as f:
        f.write("MSHX1\n")
        f.write(f"GROUPS {len(groups)}\n")
        for g in groups:
            f.write(f"LABEL {g.name}\nMATERIAL {g.material}\nTEXTURE 0\n")
            f.write(f"GEOM {len(g.v)} {len(g.t)} ;{g.name}\n")
            for p, n in zip(g.v, g.n):
                f.write(f"{p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}\n")
            for a, b, c in g.t:
                f.write(f"{a} {b} {c}\n")
        f.write(f"MATERIALS {len(MATERIALS)}\n")
        for m in MATERIALS:
            f.write(m[0] + "\n")
        for name, dif, spec, emi in MATERIALS:
            f.write(f"MATERIAL {name}\n")
            f.write("{:.3f} {:.3f} {:.3f} 1\n".format(*dif))
            f.write("{:.3f} {:.3f} {:.3f} 1\n".format(*dif))
            f.write("{:.3f} {:.3f} {:.3f} 1 {:.0f}\n".format(*spec))
            f.write("{:.3f} {:.3f} {:.3f} 1\n".format(*emi))
        f.write("TEXTURES 0\n")


def v3(a):
    return "{%.5f, %.5f, %.5f}" % tuple(a)


# ---------------------------------------------------------------------------
# Debris: every part that can break off becomes a vessel of its own (Config/Vessels/Tantra/Debris_*.cfg,
# Meshes/Tantra/Debris/*.msh), built from the same groups, centred on its own centroid. The hull breaks into
# four chunks by station. name, groups (or station range of the hull for a chunk), mass [t]
_HULL_GROUPS = ("hull", "shoulder", "nose", "spine", "pocket_liner", "hatches", "airlock", "cups_nose", "iris_nose_0", "iris_nose_1")
DEBRIS_DEFS = (
    [("crest_port", ["crest_port", "wing_outer_port", "elevon_port"], 22.0),
     ("crest_starboard", ["crest_starboard", "wing_outer_starboard", "elevon_starboard"], 22.0),
     ("fin", ["fin", "fin_upper"], 24.0)]
    + [(f"pod_{i}", [f"pod_{i}", f"door_pod_{i}"], 19.0) for i in range(4)]
    + [(f"leg_{side}", [f"shin_{side}_{i}" for i in range(SHIN_N)] + [f"ankle_{side}", f"pad_{side}", f"sole_{side}", f"anchor_{side}"], 80.0)
       for side in SIDES]
    + [(f"sternleg_{i}", [f"leg{i}_shin{k}" for k in range(LEG_SHIN_N)] + [f"leg{i}_ankle", f"leg{i}_pad", f"leg{i}_sole", f"leg{i}_anchor"], 30.0)
       for i in range(4)]
    + [(n, [n], 6.0) for n in ("door_top_port", "door_top_starboard", "door_bottom_port", "door_bottom_starboard")]
    + [(n, [n], 10.0) for n in ("bay_door_port", "bay_door_starboard")]
    + [("hull_aft", (-10.0, 40.0), 900.0), ("hull_mid", (40.0, 99.0), 700.0), ("hull_fore", (99.0, 144.0), 420.0),
       ("hull_nose", (144.0, 185.0), 300.0)])
DEBRIS = []  # filled by write_debris: (name, class, centroid (mesh frame), mass)


def write_debris(groups, root):
    G = {g.name: g for g in groups}
    mdir = os.path.join(root, "Meshes", "Tantra", "Debris")
    cdir = os.path.join(root, "Config", "Vessels", "Tantra")
    os.makedirs(mdir, exist_ok=True)
    os.makedirs(cdir, exist_ok=True)
    DEBRIS.clear()
    chunk_extra = {"hull_aft": ["well", "baffle", "cups_anamezon", "well_centre", "march_unit", "iris_march", "body_flap"],
                   "hull_mid": ["bay_liner"] + [f"trap_{i}" for i in range(4)],
                   "hull_fore": ["hangar_inner", "shuttle", "rover_platform"]}
    for name, spec, mass in DEBRIS_DEFS:
        parts = []
        if isinstance(spec, tuple):  # hull chunk: triangles of the skin groups by station, whole inner groups
            s0, s1 = spec
            for gname in _HULL_GROUPS:
                g = G[gname]
                V = np.array(g.v)
                ng = Group(gname, g.material)
                for t in g.t:
                    s = V[list(t), 2].mean() - STERN_Z
                    if s0 <= s < s1:
                        idx = [ng.vert(V[k], g.n[k]) for k in t]
                        ng.t.append(tuple(idx))
                if ng.t:
                    parts.append(ng)
            parts += [G[n] for n in chunk_extra.get(name, [])]
        else:
            parts = [G[n] for n in spec]
        P = np.vstack([np.array(p.v) for p in parts])
        c = (P.min(0) + P.max(0)) / 2
        out = []
        for p in parts:
            q = Group(p.name, p.material)
            q.v = [np.asarray(v) - c for v in p.v]
            q.n = list(p.n)
            q.t = list(p.t)
            out.append(q)
        write_msh(out, os.path.join(mdir, name + ".msh"))
        lo, hi = P.min(0) - c, P.max(0) - c
        ext = hi - lo
        size = float(np.linalg.norm(ext) / 2)
        ixx, iyy, izz = (ext[1] ** 2 + ext[2] ** 2) / 12, (ext[0] ** 2 + ext[2] ** 2) / 12, (ext[0] ** 2 + ext[1] ** 2) / 12
        with open(os.path.join(cdir, f"Debris_{name}.cfg"), "w", newline="\r\n") as f:
            f.write(f"; Tantra debris: {name} (generated by tools/gen_mesh.py)\n")
            f.write(f"ClassName = Tantra\\Debris_{name}\nMeshName = Tantra\\Debris\\{name}\n")
            f.write(f"Size = {max(1.0, size):.1f}\nMass = {mass * 1e3:.0f}\n")
            f.write(f"Inertia = {ixx:.2f} {iyy:.2f} {izz:.2f}\n")
            f.write(f"CrossSections = {ext[1] * ext[2] * 0.6:.1f} {ext[0] * ext[2] * 0.6:.1f} {ext[0] * ext[1] * 0.6:.1f}\n")
            f.write("CW = 1.0 1.0 1.2\nLiftFactor = 0.0\n")
            f.write(f"TouchdownPoints = 0 {lo[1]:.2f} {hi[2] * 0.8:.2f}  {lo[0] * 0.8:.2f} {lo[1]:.2f} {lo[2] * 0.8:.2f}  "
                    f"{hi[0] * 0.8:.2f} {lo[1]:.2f} {lo[2] * 0.8:.2f}\n")
            f.write(f"COG_OverGround = {-lo[1]:.2f}\nEnableFocus = TRUE\n")
        DEBRIS.append((name, f"Tantra\\Debris_{name}", c, mass * 1e3))


def write_layout(legs, comps, path):
    L = []
    L.append("// Generated by tools/gen_mesh.py - do not edit. Mesh group indices and animation rig of Tantra.msh.")
    L.append("#pragma once\n\nnamespace tantra::mesh {\n")
    L.append("enum Group : unsigned {")
    for i, name in enumerate(GROUPS):
        L.append(f"    GRP_{name.upper()} = {i},")
    L.append(f"    GRP_COUNT = {len(GROUPS)}\n}};\n")
    L.append("struct V { double x, y, z; };\n")
    L.append(f"constexpr double kAxisH = {AXIS_H};")
    L.append(f"constexpr double kCarS0 = {CAR_S0}, kCarS1 = {CAR_S1}, kCarSRef = {CAR_SREF}, kStowS = {STOW_S};  // hip track, stowed station")
    L.append(f"constexpr double kHipXOut = {HIP_X_OUT}, kHipXIn = {HIP_X_IN:.4f};")
    L.append(f"constexpr double kLegLMin = {LEG_LMIN:.3f}, kLegLMax = {LEG_LMAX:.3f};  // hip -> ankle, shin in / out")
    L.append(f"constexpr double kFootH = {FOOT_H:.3f}, kPadHalfL = {PAD_L / 2}, kPadHalfW = {PAD_W / 2};  // ankle above ground; pad")
    L.append(f"constexpr double kLegLMinS = {LEG_LMIN_S:.3f}, kLegExtMax = {LEG_EXT_MAX:.3f}, kLegFootH = {LEG_FOOT_H:.3f};  // stern legs (stowed along -z)")
    L.append(f"constexpr double kStrutExtC = {STRUT_EXT_C}, kStrutExtS = {STRUT_EXT_S}, kAnchorOut = {ANCHOR_OUT};  // unloaded strut rod; anchors")
    L.append(f"constexpr double kLegExtDelay = {LEG_EXT_DELAY};  // stern shins run out over the last {1 - LEG_EXT_DELAY:.0%} of the swing")
    L.append(f"constexpr double kStandR = {STAND_R}, kStandGroundS = {STAND_GROUND_S};")
    L.append(f"constexpr double kWellCentreR = {WELL_C_R:.3f}, kMarchCupR = {MARCH_R}, kMarchLipS = {MARCH_LIP_S}, kMarchTravel = {MARCH_TRAVEL:.2f}, kMarchIrisS = {MARCH_IRIS_S};  // marching cup: lip station stowed, run-out")
    L.append(f"constexpr double kNoseCupX = {NOSE_CUP_X}, kNoseCupY = {NOSE_CUP_Y}, kNoseCupR = {NOSE_CUP_R}, kNoseCupS = {NOSE_CUP_S};  // retro cups (lips)")
    L.append(f"constexpr double kFinRetract = {FIN_RETRACT:.3f}, kFinDrop = {FIN_DROP:.3f};  // telescopic fin: two stages")
    L.append(f"constexpr double kSternAxisY = {YC:.4f}, kRimS = {RIM_S}, kAnaCupR = {ANA_R};")
    L.append("constexpr double kAnaCup[4][2] = {" + ", ".join("{%.4f, %.4f}" % c for c in ANA_CUPS) + "};  // x, y of the anamezon cups (nacelles)")
    L.append(f"constexpr double kWingX0 = {WING_X0}, kWingY = {WING_Y:.4f}, kWingB1 = {WING_B1}, kWingB = {WING_B}, kWingFoldDeg = {math.degrees(WING_FOLD):.1f}, kWingRaiseDeg = {math.degrees(WING_RAISE):.1f};")
    L.append(f"constexpr double kWingLe0 = {WING_LE0}, kWingTe0 = {WING_TE0}, kWingLe1 = {WING_LE1}, kWingTe1 = {WING_TE1};")
    L.append(f"constexpr double kFlapY = {FLAP_Y:.4f}, kFlapHalf = {FLAP_HALF}, kFlapS0 = {FLAP_S0}, kFlapS1 = {FLAP_S1};")
    L.append(f"constexpr double kHangarS0 = {HANGAR_S[0]}, kHangarS1 = {HANGAR_S[1]}, kAirlockS = {AIRLOCK_S}, kShipLength = {L_SHIP};")
    L.append(f"constexpr double kElevonUpDeg = {ELEV_UP}, kElevonDownDeg = {ELEV_DOWN}, kBodyFlapDownDeg = {FLAP_DOWN};")
    L.append("constexpr double kTrapXY[4][2] = {" + ", ".join("{%.2f, %.2f}" % t for t in TRAP_XY) + "};  // lower stbd, lower port, upper stbd, upper port")
    L.append(f"constexpr double kTrapZ = {zs((CASS_S0 + CASS_S1) / 2):.3f}, kCassW = {CASS_W}, kCassLen = {CASS_S1 - CASS_S0:.1f}, kTrapMouthY = {TRAP_MOUTH_Y};")
    L.append(f"constexpr double kLiftY0 = {LIFT_Y0}, kLiftTravel = {LIFT_TRAVEL};  // head (= cassette centre) at rest, travel down")
    L.append("// Planetary pods (reference = stowed): swing of the door arm about `hinge` (axis z) by `swing`,")
    L.append("// then the pod turns about `swivelAxis` through `pivot` (0 = cups aft .. kPodSwivelMax).")
    L.append(f"constexpr int kPodCount = {len(PODS)};")
    L.append(f"constexpr double kPodSwivelMax = {POD_SWIVEL_MAX:.6f};")
    L.append("struct PodRig { double s; V hinge; double swing; V pivot, swivelAxis, cup[3]; };")
    L.append("constexpr PodRig kPods[kPodCount] = {")
    for p in PODS:
        L.append(f"    {{{p['s']}, {v3(p['hinge'])}, {p['swing']:.6f}, {v3(p['centre'])}, {v3(p['swivel_axis'])}, "
                 f"{{{', '.join(v3(c) for c in p['cups'])}}}}},")
    L.append("};\n")
    L.append("// Stern legs: stowed along -z from the hinge; one swing axis. Swing state = angle / phiMax.")
    L.append("struct LegRig { V hinge, axis; double phiStand, extStand, phiRest, extRest, phiMax; bool lower;")
    L.append("               V footStandAxis; double footStandAng; V footRestAxis; double footRestAng; V radial; };")
    L.append("constexpr LegRig kLegs[4] = {")
    for g in legs:
        L.append(f"    {{{v3(g['H'])}, {v3(g['axis'])}, {g['phi_stand']:.6f}, {g['e_stand']:.4f}, {g['phi_rest']:.6f}, "
                 f"{g['e_rest']:.4f}, {g['phi_max']:.6f}, {'true' if g['lower'] else 'false'},")
        L.append(f"     {v3(g['fs_ax'])}, {g['fs_ang']:.6f}, {v3(g['fr_ax'])}, {g['fr_ang']:.6f}, {v3(g['rad'])}}},")
    L.append("};\n")
    anims = []
    for c in comps:
        if c["anim"] not in anims:
            anims.append(c["anim"])
    L.append("enum Anim : int {")
    for i, a in enumerate(anims):
        L.append(f"    ANIM_{a.upper()} = {i},")
    L.append(f"    ANIM_COUNT = {len(anims)}\n}};\n")
    defs = {}
    for c in comps:
        assert defs.setdefault(c["anim"], c.get("d", 0.0)) == c.get("d", 0.0), c["anim"]
    L.append("constexpr double kAnimDef[ANIM_COUNT] = {" + ", ".join(f"{defs[a]:.4f}" for a in anims) + "};  // mesh pose")
    L.append("enum RigKind : int { RIG_ROTATE = 0, RIG_TRANSLATE = 1, RIG_SCALE = 2 };")
    L.append("struct RigComp { int anim, kind, group; V ref, vec; double angle; int parent; double s0, s1; };  // group -1 = pivot only")
    L.append(f"constexpr int kRigCount = {len(comps)};")
    L.append("constexpr RigComp kRig[kRigCount] = {")
    for c in comps:
        kind = {"rot": 0, "tr": 1, "sc": 2}[c["kind"]]
        assert len(c["groups"]) <= 1
        grp = GROUPS.index(c["groups"][0]) if c["groups"] else -1
        if c["kind"] == "rot":
            ref, vec, ang = c["par"]
        elif c["kind"] == "tr":
            ref, vec, ang = np.zeros(3), c["par"], 0.0
        else:
            (ref, vec), ang = c["par"], 0.0
        par = -1 if c["parent"] is None else c["parent"]
        L.append(f"    {{ANIM_{c['anim'].upper()}, {kind}, {grp}, {v3(ref)}, {v3(vec)}, {ang:.6f}, {par}, {c['s0']:.3f}, {c['s1']:.3f}}},")
    L.append("};\n")
    L.append("// Debris vessels (tools/gen_mesh.py write_debris): class, spawn point = centroid in the mesh frame.")
    L.append("struct DebrisDef { const char* name; const char* cls; V centre; double mass; };")
    L.append(f"constexpr int kDebrisCount = {len(DEBRIS)};")
    L.append("constexpr DebrisDef kDebris[kDebrisCount] = {")
    for name, cls, c, mass in DEBRIS:
        L.append(f'    {{"{name}", "{cls.replace(chr(92), chr(92) * 2)}", {v3(c)}, {mass:.0f}}},')
    L.append("};\n\n}  // namespace tantra::mesh\n")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L))


def stowed_states():
    """Flight at 0.9 c: everything folded, doors shut, fin down."""
    st = {"crest_lateral": 1, "wing_outer": 1, "crest_dorsal": 1, "pod_retract": 1}
    for s in SIDES:
        st.update({f"track_{s}": (STOW_S - CAR_S0) / (CAR_S1 - CAR_S0), f"slide_{s}": 1.0, f"pitch_{s}": 1.0,
                   f"shin_len_{s}": 1.0, f"pad_fold_{s}": 1.0})
    return st


def preview_poses(legs):
    """Named poses: flight (stowed), resting level (pods hovering), turning 45 deg, standing, hangar open."""
    mid = (60.0 - CAR_S0) / (CAR_S1 - CAR_S0)
    shin = lambda L: (LEG_LMAX - L) / (LEG_LMAX - LEG_LMIN)
    stowed = stowed_states()
    rest = {"pod_retract": 0, "pod_swivel": 90 / 100}
    for s in SIDES:
        rest.update({f"track_{s}": mid, f"shin_len_{s}": shin(AXIS_H - FOOT_H)})
    for i, g in enumerate(legs):
        if g["lower"]:
            rest.update({f"leg{i}_swing": g["phi_rest"] / g["phi_max"], f"leg{i}_ext": g["e_rest"] / LEG_EXT_MAX, f"leg{i}_foot_rest": 1})
    turn_h = 60.0 + 16.0
    turn = {"crest_lateral": 1, "wing_outer": 1, "crest_dorsal": 1, "pod_retract": 0, "pod_swivel": 0.45}
    for s in SIDES:
        turn.update({f"track_{s}": mid, f"pitch_{s}": 0.5, f"shin_len_{s}": shin(turn_h - FOOT_H)})
    stand = dict(stowed)
    for i, g in enumerate(legs):
        stand.update({f"leg{i}_swing": g["phi_stand"] / g["phi_max"], f"leg{i}_ext": g["e_stand"] / LEG_EXT_MAX, f"leg{i}_foot_stand": 1})
    hang = dict(rest)
    hang.update({"hangar": 1, "rover_lift": 1, "bay_doors": 1, "elevon_port": 1, "elevon_starboard": 0, "body_flap": 1})
    # ship pose: pitch about the hip (trunnion) at s 53, lift of that point above its level height
    return [("flight (stowed)", stowed, 0, 0, (15, -60)), ("resting level", rest, 0, 0, (10, -120)),
            ("turning 45 deg", turn, 45, turn_h - AXIS_H, (8, -80)), ("standing", stand, 90, 12 + 60 - AXIS_H, (8, -60)),
            ("hangar open", hang, 0, 0, (-15, -60))]


def pose_world(P, pitch, lift, pivot_s=60.0):
    """Ship vertices -> world (ground frame): pitch nose-up about the trunnion at pivot_s, raise by lift."""
    R = rot([1, 0, 0], -math.radians(pitch))
    c = np.array([0.0, 0.0, zs(pivot_s)])
    return ((R @ (P - c).T).T + c) + np.array([0, lift, 0])


def preview(groups, comps, path, poses):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    colors = {i + 1: m[1] for i, m in enumerate(MATERIALS)}
    light = unit([0.4, 0.8, -0.4])
    fig = plt.figure(figsize=(6 * len(poses), 6))
    for k, (title, states, pitch, lift, view) in enumerate(poses):
        V = apply_pose(groups, comps, states)
        ax = fig.add_subplot(1, len(poses), k + 1, projection="3d")
        for g in groups:
            P = pose_world(V[g.name], pitch, lift)
            polys, fc = [], []
            for a, b, c in g.t:
                tri = P[[a, b, c]]
                nrm = np.cross(tri[1] - tri[0], tri[2] - tri[0])
                ln = np.linalg.norm(nrm)
                shade = 0.35 + 0.65 * max(0.0, float(np.dot(nrm / ln, light))) if ln > 0 else 0.5
                polys.append(tri[:, [2, 0, 1]])
                fc.append(tuple(min(1.0, ch * shade + 0.05) for ch in colors[g.material]))
            ax.add_collection3d(Poly3DCollection(polys, facecolors=fc, edgecolors="none"))
        gz = -AXIS_H
        ax.plot([-110, 110, 110, -110, -110], [-40, -40, 40, 40, -40], [gz] * 5, color="#6b4f3a")
        ax.set_xlim(-100, 110)
        ax.set_ylim(-60, 60)
        ax.set_zlim(gz, gz + 170 if pitch > 45 else gz + 90)
        ax.set_box_aspect((210, 120, 170 if pitch > 45 else 90))
        ax.view_init(elev=view[0], azim=view[1])
        ax.set_title(title)
        ax.set_axis_off()
    fig.tight_layout()
    fig.savefig(path, dpi=70)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "..", "..")
    install = "--install" in sys.argv            # otherwise everything goes to build/mesh_t9 (the installed DLL matches the old mesh)
    groups, legs = build()
    work = os.path.join(here, "..", "build", "mesh_t9")
    out = os.path.join(root, "Meshes", "Tantra") if install else os.path.join(work, "Meshes", "Tantra")
    os.makedirs(out, exist_ok=True)
    write_msh(groups, os.path.join(out, "Tantra.msh"))
    trap = Group("trap", MAT["trap_shell"])
    trap_geom(trap, 0.0, 0.0, -12.4 - STERN_Z, 12.4 - STERN_Z)
    write_msh([trap], os.path.join(out, "TantraTrap.msh"))
    raw_dir = os.path.join(here, "..", "build", "mesh")
    os.makedirs(raw_dir, exist_ok=True)
    write_json(groups, os.path.join(raw_dir, "tantra_raw.json"))
    write_debris(groups, root if install else work)
    write_layout(legs, rig(legs), os.path.join(here, "..", "orbiter2016", "MeshLayout.h") if install else os.path.join(work, "MeshLayout.h"))
    nv = sum(len(g.v) for g in groups)
    nt = sum(len(g.t) for g in groups)
    print(f"Tantra.msh: {len(groups)} groups, {nv} vertices, {nt} triangles; MeshLayout.h written")
