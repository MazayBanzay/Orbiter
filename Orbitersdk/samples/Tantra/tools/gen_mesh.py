"""Generate the Tantra mesh (Orbiter .msh, MSHX1) and the animation layout header.

Layout "C-148" (docs/DESIGN.md, Tantra_Design/tantra_c148.html): hull «Б» of rounded-polygon
sections (flat flanks, lower chines, top slopes, flat bottom), iridium nose on a blunted ogive,
armoured shoulder with the dorsal spine, dorsal fin 7 retracting into a slot between the trap
columns, one-piece lateral crests with elevons folding into flank recesses, body flap, stern well
with iris-shuttered anamezon cups and a ring of 12 planetary cups, four planetary pods on the lower
chines (the bay door is the pod's swing arm), carriage («лафет») legs with a telescopic trunnion
pin, four stern legs hinged forward and folded aft.

Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane,
z = s + STERN_Z (mesh origin at s = 38). Hull axis 14 m above ground while resting level.

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

STERN_Z = -38.0
AXIS_H = 14.0
L_SHIP = 146.0
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
# Hull «Б»: rounded-polygon sections (the same construction as the mockup)

def _key(B, C, T, ya, rb, rc, rt, ra):
    return dict(B=np.array(B, float), C=np.array(C, float), T=np.array(T, float), ya=ya, rb=rb, rc=rc, rt=rt, ra=ra)


KEY_AFT = _key((11.2, -9.6), (14.6, -4.0), (11.0, 12.0), 14.4, 3.0, 0.8, 3.0, 6.0)
KEY_FORE = _key((5.4, -8.7), (9.0, -3.8), (6.8, 5.0), 8.9, 1.6, 0.8, 4.5, 5.6)
# The aft body is a prism from the stern plane to s 47 (flat faces carry the pockets of the stern
# legs right up to the stern), then blends into the fore body; ogive nose from s 122.
HULL_KEYS = [(0.0, KEY_AFT), (47.0, KEY_AFT), (75.0, KEY_FORE), (122.0, KEY_FORE)]
NB = 122.0


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

    def __init__(self, P):
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
        self.p = poly
        self.q = np.vstack([poly[1:], poly[:1]])
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


_SEC = {}


def sec(s):
    s = min(max(s, 0.0), NB)
    k = round(s, 6)
    if k not in _SEC:
        for (a, pa), (b, pb) in zip(HULL_KEYS, HULL_KEYS[1:]):
            if s <= b:
                t = sm5((s - a) / (b - a)) if b > a else 0.0
                P = {key: pa[key] + (pb[key] - pa[key]) * t for key in pa}
                break
        _SEC[k] = Section(P)
    return _SEC[k]


# Ogive nose with a spherical tip (radius 1.8), as in the mockup.
_BASE = sec(NB)
_Y0 = (_BASE.p[:, 1].min() + _BASE.p[:, 1].max()) / 2
_RN = (_BASE.p[:, 1].max() - _BASE.p[:, 1].min()) / 2
_NL = L_SHIP - NB
_RHO = (_RN ** 2 + _NL ** 2) / (2 * _RN)
_rn = 1.8
_XS = _NL - math.sqrt((_RHO - _rn) ** 2 - (_RHO - _RN) ** 2)
_XT = _XS - _rn * (_NL - _XS) / (_RHO - _rn)
_TIP = _XS - _rn
TIP_S = L_SHIP - _TIP


def nose_k(x):
    if x <= _TIP:
        return 0.0
    if x < _XT:
        return math.sqrt(max(0.0, _rn ** 2 - (x - _XS) ** 2)) / _RN
    return (math.sqrt(_RHO ** 2 - (_NL - x) ** 2) + _RN - _RHO) / _RN


def hull_xy(s, u):
    if s <= NB:
        return sec(s).at(u)
    k = nose_k(L_SHIP - s)
    p = _BASE.at(u)
    return np.array([p[0] * k, _Y0 + (p[1] - _Y0) * k])


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
    k = nose_k(L_SHIP - s)
    return _BASE.dist(((p[0]) / k, _Y0 + (p[1] - _Y0) / k)) * k


# Reference faces of the aft body (starboard; port mirrors x and u -> 1 - u).
_SA = sec(30.0)
_C, _T, _B = KEY_AFT["C"], KEY_AFT["T"], KEY_AFT["B"]
E_F = unit(_T - _C)                           # flank, up
N_F = np.array([E_F[1], -E_F[0]])             # flank outward normal
E_C = unit(_C - _B)                           # lower chine, up
N_C = np.array([E_C[1], -E_C[0]])
_APEX = np.array([0.0, KEY_AFT["ya"]])
E_T = unit(_APEX - _T)                        # top slope, inboard
N_T = np.array([E_T[1], -E_T[0]])


def flank_pt(t):
    return _C + t * E_F


def flank_t(u):
    return float((_SA.at(u) - _C) @ E_F)


def chine_pt(w, key=KEY_AFT):
    return key["B"] + w * unit(key["C"] - key["B"])


def top_pt(v):
    return _T + v * E_T


def mir(u):
    return 1.0 - u


# ---------------------------------------------------------------------------
# Geometry of the moving parts (mirrors core/Spec.h)

# Carriage («лафет») legs, one per flank: trunnion shoe on rails in a flank pocket, telescopic
# trunnion pin (box 5 x 3 m, four stages nesting into the hip block), hip block with the pitch
# bearing, flat thigh blade (band drums inside), band-mast shin of nested sections collapsing into
# the thigh, two-axis ankle (lateral axis lockable), pad 16 x 6 x 1.2 m on a spreader fork.
# Stowed: pitched aft along the hull, pad folded against the thigh ground face out, rolled with the
# hip to the flank lean so the pad is the flank skin over the pocket.
CAR_S0, CAR_S1 = 32.0, 53.0                    # hip (trunnion) track; the pin slot runs on to s 54.6
CAR_SREF = CAR_S0                              # legs built with the hip at the track start
STOW_S = 46.0                                  # hip station with the leg folded aft into the pocket
HIP_X_OUT = 19.0
THIGH_L, THIGH_W, THIGH_T = 8.0, 6.0, 2.0      # blade: 6 m fore-aft when hanging, 2 m across
SHIN_N, SHIN_SEG, SHIN_OVL, SHIN_TOP = 10, 7.4, 0.3, 0.6
SHIN_W, SHIN_T = 5.4, 1.7                      # band mast, CNT wall 5 cm
SHIN_STEP = SHIN_SEG - SHIN_OVL
ANKLE_R, PAD_OFF = 0.8, 1.6                    # ankle; pad mid-plane below the ankle centre
PAD_L, PAD_W, PAD_T, PAD_RC = 16.0, 6.0, 1.2, 0.3
LEG_LMIN = THIGH_L + ANKLE_R                   # hip -> ankle, shin collapsed
LEG_LMAX = SHIN_TOP + (SHIN_N - 1) * SHIN_STEP + SHIN_SEG + ANKLE_R
FOOT_H = PAD_OFF + PAD_T / 2                   # ankle centre above the ground = leg axis to pad face
# Stowing: the pin retracts along its axis, then the whole pin/hip/leg assembly rolls about a trunnion on
# the shoe by the flank lean. Deploying is the reverse: the pin pushes the package straight out along the
# flank normal, and it unrolls outside the hull.
ROLL = math.atan2(N_F[1], N_F[0])
PIN_N, PIN_X0, PIN_STEP, PIN_LEN = 4, 10.8, 1.8, 1.9   # pin stages (deployed: x0 + k*step .. + len); roll pivot at x0
ROLL_P = np.array([PIN_X0, 0.0])
TRAVEL = HIP_X_OUT - PIN_X0 + FOOT_H - float(N_F @ _C - N_F @ ROLL_P)   # stowed pad face lies in the flank plane
HIP_X_IN = HIP_X_OUT - TRAVEL                 # (in the rolled frame)
SLIDE_ROLL = 0.3                               # slide state below which the assembly unrolls (outside the hull)
HIP_BLOCK = (0.9, 1.3, 2.7, 1.7)               # hip block: inboard, outboard, half height, half length
POCKET_T = float((ROLL_P + (HIP_X_IN - PIN_X0 + FOOT_H) * N_F - _C) @ E_F)   # pad centre on the flank
LAF_S0 = STOW_S - LEG_LMIN - PAD_L / 2 - 0.05
LAF_S1 = STOW_S + HIP_BLOCK[3] + 0.2
LAF_DEPTH = 3.3
PIN_SLOT = (LAF_S1, CAR_S1 + 1.6)              # lip-sealed slot for the pin beyond the pocket

# Stern legs: hinge forward (s 15.5) under the skin, leg folded aft, pad folded against the thigh
# ground face out = the skin over the pocket. Upper pair on the top slopes, lower pair on the lower
# chines. Swing about one axis; the lower pair also rests the ship level (feet out at +-18.7 m).
LEG_S_H = 14.35                                     # pad from s 0.05: the pocket starts at the stern plane
LEG_THIGH_L, LEG_THIGH_W, LEG_THIGH_T = 8.0, 2.6, 2.0
LEG_SHIN_W, LEG_SHIN_T, LEG_SHIN_N = 2.0, 1.5, 3     # CNT wall 8 cm
LEG_LMIN_S = LEG_THIGH_L + ANKLE_R
LEG_EXT_MAX = LEG_SHIN_N * SHIN_STEP
LEG_PAD_OFF = 2.1                                    # room for the swing tilt of the blade under the pad
LEG_FOOT_H = LEG_PAD_OFF + PAD_T / 2
LEG_PAD_L = 11.0
STAND_R, STAND_GROUND_S = 19.0, -12.0
REST_GROUND_Y = -AXIS_H
REST_EXT = 13.0                                      # shin out at rest: lower legs lie ~35 deg out (the thigh root stays under the cap)
LEG_EXT_DELAY = 0.6                                  # the shin runs out only after 60 % of the swing (clear of the stern)
STERN_POCKET_DEPTH = 4.0
# name, side, face, pad centre coordinate on the face, pad width, foot direction when standing (deg)
# Each leg swings in the plane of its face normal: any other single-axis swing slides the leg sideways under
# the skin. The foot distance along the normal sets the stance (upper feet 17 m up, lower feet out to y -11.7:
# tip-over 10 deg at the landing CG).
STERN_LEGS = [("upper_stbd", 1, "top", 5.12, 5.0, ("y", 17.0)), ("upper_port", -1, "top", 5.12, 5.0, ("y", 17.0)),
              ("lower_port", -1, "chine", 3.96, 4.4, ("y", -11.55)), ("lower_stbd", 1, "chine", 3.96, 4.4, ("y", -11.55))]
LEG_CAP_BACK = 1.2           # lower legs: opening ends 1.2 m aft of the hinge; a cap on the thigh closes it up to the pad (front edge stays over the skin up to 48 deg swing)

# Lateral crests: one-piece radiator wings, span 55 m, hinge on the chine; elevons on the outer 60 %,
# 30 % chord. Folded up by 102.7 deg they lie in a flank recess, windward face out, flush.
CREST_T = 0.9
CREST_Y = -4.0
CREST_X0 = (float(N_F @ _C) - CREST_T / 2 - 0.04 - N_F[1] * CREST_Y) / N_F[0]   # 4 cm under the flank: the root lies over the chine fillet
CREST_X1 = 27.4
CREST_R = CREST_X1 - CREST_X0
CREST_TH = float((np.array([CREST_X0, CREST_Y]) - _C) @ E_F)   # hinge on the flank line
FOLD = math.pi - math.atan2(E_F[1], -E_F[0])
ELEV_RA = 0.4 * CREST_R
ELEV_UP, ELEV_DOWN = 30.0, 40.0
FLAP_HALF, FLAP_S0, FLAP_S1, FLAP_T, FLAP_DOWN = 9.0, -3.0, 2.0, 0.25, 25.0


def crest_te(r):
    return 6.0 + 5.0 * r / CREST_R


def crest_le(r):
    return 28.0 - 7.0 * r / CREST_R


def crest_te_c(r):
    """Trailing edge with the root corner chamfered (0.4 x 0.6 m): while folding, the plate thickness
    projects up to 0.45 m along the flank and the swept edges would catch the recess edge."""
    return max(crest_te(r), crest_te(0) + 0.4 + r * (crest_te(0.6) - crest_te(0) - 0.4) / 0.6) if r < 0.6 else crest_te(r)


def crest_le_c(r):
    return min(crest_le(r), crest_le(0) - 0.4 + r * (crest_le(0.6) - crest_le(0) + 0.4) / 0.6) if r < 0.6 else crest_le(r)


def crest_h(r):
    return crest_te(r) + 0.3 * (crest_le(r) - crest_te(r))


# Dorsal fin 7 (radiator): retracts into a 1.3 m slot between the trap columns; its top chord then
# closes the slot flush. Spine over the shoulder.
FIN = [(3.0, 0.0), (46.0, 0.0), (36.0, 6.0), (27.0, 20.5), (6.0, 20.5)]
FIN_T, FIN_BASE = 1.1, 14.0
FIN_RETRACT = FIN_BASE + 20.5 - top_y(30.0) + 0.04   # top chord 4 cm under the apex (slot edges on the apex fillet)
SLOT_HALF = 0.65

# Planetary pods: 4 x 3 cups, pairs at s 30 and s 74 on the lower chines. The bay door is the pod's
# swing arm: it swings down 144..149 deg about its lower edge and the pod hangs outboard under the
# chine; the pod turns on a trunnion normal to the door (0 = thrust forward, 90 = thrust up).
POD_L, POD_W, POD_T, POD_DEPTH = 5.6, 3.0, 1.8, 1.4
POD_DEFS = [(30.0, KEY_AFT, (2.4, 6.35), 4.1, -1), (30.0, KEY_AFT, (2.4, 6.35), 4.1, 1),      # door w0..w1, pod centre w
            (76.0, KEY_FORE, (1.6, 5.75), 3.35, -1), (76.0, KEY_FORE, (1.6, 5.75), 3.35, 1)]
POD_DOOR_L = 6.4
POD_SWIVEL_MAX = math.radians(100.0)
POD_CANT = {30.0: math.radians(13.0), 76.0: math.radians(17.0)}   # door swings past vertical: pod top clears the chine

PLAN_R, PLAN_CUP_R = 10.75, 0.75
WELL_Y, WELL_R = 2.2, 11.5
ANA_CUPS = [(-4.4, 4.4 + WELL_Y), (4.4, 4.4 + WELL_Y), (-4.4, -4.4 + WELL_Y), (4.4, -4.4 + WELL_Y)]

# Anamezon port and trap columns (mirrors core/Spec.h). Trap cassettes: octagonal armour around the
# magnetic trap, lifted by their trunnions on telescopic masts in shafts beyond both column ends.
CASS_W, CASS_CH = 9.8, 3.2
CASS_S0, CASS_S1 = 21.5, 46.3
TRAP_XY = [(5.55, -3.15), (-5.55, -3.15), (5.55, 7.0), (-5.55, 7.0)]  # lower stbd, lower port, upper stbd, upper port
BAY_S0, BAY_S1 = 19.7, 48.1
BAY_X0, BAY_X1 = 0.5, 10.6
LIFT_Y0, LIFT_TRAVEL, LIFT_CEIL = 7.0, 36.0, 11.6
LIFT_N, LIFT_SEG = 8, 5.2
HEAD_S = (20.4, 47.4)
TRAP_MOUTH_Y = -13.6
HANGAR_S = (80.0, 100.0)
AIRLOCK_S = 104.0
# Port airlock: a flush door for space (docking, EVA). On the ground the crew uses the hangar floor platform,
# which already lowers to the ground: no separate lift.


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


def plan_cups():
    return [(PLAN_R * math.cos(2 * math.pi * (k + 0.5) / 12), WELL_Y + PLAN_R * math.sin(2 * math.pi * (k + 0.5) / 12)) for k in range(12)]


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
        target = np.array([-sgn, 0.0])                             # door normal after the swing: inboard
        a0 = math.atan2(n3[1], n3[0])
        a1 = math.atan2(target[1], target[0])
        swing = a1 - a0                                            # swing down (through straight down)
        if sgn > 0 and swing > 0:
            swing -= 2 * math.pi
        if sgn < 0 and swing < 0:
            swing += 2 * math.pi
        swing -= sgn * POD_CANT[s_c]                               # past vertical (swing is negative for starboard)
        cups = [centre + np.array([0, 0, -POD_L / 2]) + e3 * dy for dy in (-0.9, 0.0, 0.9)]
        out.append(dict(s=s_c, sgn=sgn, u=(u0, u1), n=n3, e=e3, hinge=hinge, swing=swing, centre=centre,
                        swivel_axis=n3 * sgn, cups=cups, s0=s_c - POD_DOOR_L / 2, s1=s_c + POD_DOOR_L / 2))
    return out


def stern_legs():
    """Per stern leg: hinge, swing axis, stand/rest angles and extensions, pad rotations, pocket."""
    out = []
    mz = np.array([0.0, 0.0, -1.0])                     # stowed leg direction (aft)
    for name, sgn, face, c, pw, foot in STERN_LEGS:
        m = np.array([sgn, 1.0])
        if face == "top":
            P2, n2, e2 = top_pt(c), N_T, E_T
            u0, u1 = _SA.u_of(top_pt(c - pw / 2 - 0.05)), _SA.u_of(top_pt(c + pw / 2 + 0.05))
        else:
            P2, n2, e2 = chine_pt(c), N_C, E_C
            u0, u1 = _SA.u_of(chine_pt(c - pw / 2 - 0.05)), _SA.u_of(chine_pt(c + pw / 2 + 0.05))
        if sgn < 0:
            u0, u1 = mir(u1), mir(u0)
        n3 = np.array([*(n2 * m), 0.0])
        e3 = np.array([*(e2 * m), 0.0])
        H = np.array([*((P2 - LEG_FOOT_H * n2) * m), zs(LEG_S_H)])
        H2 = P2 - LEG_FOOT_H * n2
        k = (foot[1] - H2[1]) / n2[1]
        F2 = (H2 + k * n2) * m
        fd = np.array([F2[0], F2[1], 0.0]) / STAND_R           # foot / kStandR (the adapter multiplies back)
        F = np.array([F2[0], F2[1], zs(STAND_GROUND_S) + LEG_FOOT_H])
        d = F - H
        axis = unit(np.cross(mz, d))
        phi_stand = math.acos(float(mz @ unit(d)))
        e_stand = np.linalg.norm(d) - LEG_LMIN_S
        lower = face == "chine"
        phi_rest = e_rest = 0.0
        if lower:
            e_of = lambda ph: (H[1] - (REST_GROUND_Y + LEG_FOOT_H)) / -(rot(axis, ph) @ mz)[1] - LEG_LMIN_S
            lo, hi = math.radians(15.0), math.radians(90.0)       # swing until the shin is REST_EXT out
            for _ in range(60):
                mid = (lo + hi) / 2
                lo, hi = (mid, hi) if e_of(mid) > REST_EXT else (lo, mid)
            phi_rest = (lo + hi) / 2
            e_rest = e_of(phi_rest)
        # pad built stowed: long axis along z, leg-side ("up") normal pointing inboard (-n3)
        B0 = np.column_stack([np.array([0, 0, 1.0]), -n3, np.cross(np.array([0, 0, 1.0]), -n3)])

        def pad_child(phi, long_t, up_t):
            Bt = np.column_stack([long_t, up_t, np.cross(long_t, up_t)])
            return axis_angle(rot(axis, phi).T @ (Bt @ B0.T))

        fs_ax, fs_ang = pad_child(phi_stand, unit(fd), np.array([0, 0, 1.0]))
        fr_ax, fr_ang = pad_child(phi_rest, np.array([0, 0, 1.0]), np.array([0, 1.0, 0])) if lower else (np.array([1.0, 0, 0]), 0.0)
        assert 0.0 <= e_stand <= LEG_EXT_MAX and 0.0 <= e_rest <= LEG_EXT_MAX, (name, e_stand, e_rest)
        pad_s0 = LEG_S_H - LEG_LMIN_S - LEG_PAD_L / 2
        out.append(dict(name=name, H=H, axis=axis, phi_stand=phi_stand, e_stand=e_stand, phi_rest=phi_rest, e_rest=e_rest,
                        phi_max=max(phi_stand, phi_rest), lower=lower, rad=fd, n=n3, e=e3, pw=pw, u=(u0, u1),
                        fs_ax=fs_ax, fs_ang=fs_ang, fr_ax=fr_ax, fr_ang=fr_ang,
                        s0=0.0, s1=LEG_S_H - LEG_CAP_BACK if lower else pad_s0 + LEG_PAD_L + 0.1, pad_s1=pad_s0 + LEG_PAD_L))
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
    # crest recess: the folded planform (t on the flank = hinge + span)
    r_of = lambda u: min(CREST_R, max(0.0, flank_t(u) - CREST_TH))
    crest = _open("crest_starboard", _SA.u_of((CREST_X0, CREST_Y)) - 0.002, _SA.u_of(flank_pt(CREST_TH + CREST_R + 0.05)),
                  lambda u: crest_te_c(r_of(u)) - 0.05, lambda u: crest_le_c(r_of(u)) + 0.05, depth=CREST_T + 0.05)
    lafet = _open("carriage_starboard", _SA.u_of(flank_pt(POCKET_T - PAD_W / 2 - 0.05)), _SA.u_of(flank_pt(POCKET_T + PAD_W / 2 + 0.05)),
                  LAF_S0, LAF_S1, depth=LAF_DEPTH)
    bay = _open("bay_starboard", _SA.u_of((BAY_X0, -9.6)), _SA.u_of((BAY_X1, -9.45)), BAY_S0, BAY_S1)
    flap = _open("flap_starboard", 0.0, _SA.u_of((FLAP_HALF, -9.6)), 0.0, FLAP_S1, depth=FLAP_T + 0.05, open_aft=True)
    for o, n in ((crest, "crest_port"), (lafet, "carriage_port"), (bay, "bay_port"), (flap, "flap_port")):
        O += [o, _mirror(o, n)]
    for i, p in enumerate(PODS):
        O.append(_open(f"pod_{i}", p["u"][0], p["u"][1], p["s0"], p["s1"], depth=POD_DEPTH + POD_T / 2 + 0.1))
    for i, L in enumerate(SLEGS):
        O.append(_open(f"leg{i}", L["u"][0], L["u"][1], L["s0"], L["s1"], depth=STERN_POCKET_DEPTH, open_aft=True))
    us = _SA.u_of((SLOT_HALF, top_y(30.0)))
    O.append(_open("fin_slot", us, mir(us), 3.0, 46.0, depth=3.0))
    SF = sec(90.0)
    uh = SF.u_of((6.6, 5.7))
    O.append(_open("hangar_top", uh, mir(uh), *HANGAR_S, depth=3.4))
    ub = SF.u_of((4.0, -8.7))
    O.append(_open("hangar_bottom_starboard", 0.0, ub, *HANGAR_S, depth=5.0))
    O.append(_open("hangar_bottom_port", mir(ub), 1.0, *HANGAR_S, depth=5.0))
    SA = sec(AIRLOCK_S)
    fe = unit(KEY_FORE["T"] - KEY_FORE["C"])
    fp = lambda y: KEY_FORE["C"] + fe * (y - KEY_FORE["C"][1]) / fe[1]
    O.append(_open("airlock_door", mir(SA.u_of(fp(2.9))), mir(SA.u_of(fp(-1.9))), AIRLOCK_S - 1.4, AIRLOCK_S + 1.4))
    return O


OPENINGS = openings()
OPEN = {o["name"]: o for o in OPENINGS}


def overlays():
    """Flush dark patches (seals, hatches): name, u0, u1, s0, s1."""
    t_pin = (0.0 - _C[1]) / E_F[1]
    u0, u1 = _SA.u_of(flank_pt(t_pin - 2.5)), _SA.u_of(flank_pt(t_pin + 2.5))
    SF = sec(104.0)
    fe = unit(KEY_FORE["T"] - KEY_FORE["C"])
    fp = lambda y: KEY_FORE["C"] + fe * (y - KEY_FORE["C"][1]) / fe[1]
    uk = sec(57.0).u_of((1.2, -9.5))
    return [("hatches", u0, u1, *PIN_SLOT), ("hatches", mir(u1), mir(u0), *PIN_SLOT),
            ("hatches", 0.0, uk, 56.0, 58.5), ("hatches", mir(uk), 1.0, 56.0, 58.5)]   # skin-coloured, seams only


# ---------------------------------------------------------------------------
# Surface builder: strips between u-grid lines, zipped between station lists, holes cut exactly


def _stations():
    st = list(np.arange(0.0, 47.0, 1.0)) + list(np.arange(47.0, NB, 0.75)) + [NB]
    st += list(np.arange(NB, TIP_S - 2.0, 0.75)) + [TIP_S - 2.0 * (1 - k / 12) for k in range(12)] + [TIP_S - 0.03]
    st += [48.0, 63.0, 56.0, 58.5, *PIN_SLOT]
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
GROUPS = (["hull", "shoulder", "nose", "spine", "fin", "crest_port", "crest_starboard", "elevon_port", "elevon_starboard",
           "body_flap", "well", "baffle", "cups_anamezon"] + [f"iris_ana_{i}" for i in range(4)]
          + ["cups_planetary"] + [f"iris_plan_{i}" for i in range(12)]
          + [f"door_pod_{i}" for i in range(4)] + [f"pod_{i}" for i in range(4)]
          + ["door_top_port", "door_top_starboard", "door_bottom_port", "door_bottom_starboard",
             "hangar_inner", "shuttle", "rover_platform", "hatches", "airlock", "pocket_liner"]
          + [f"{part}_{side}" for side in SIDES for part in ("carriage", "hip", "thigh", "ankle", "pad")]
          + [f"pin_{side}_{k}" for side in SIDES for k in range(PIN_N)]
          + [f"shin_{side}_{i}" for side in SIDES for i in range(SHIN_N)]
          + ["leg_hinges"]
          + [f"leg{i}_{part}" for i in range(4) for part in ("thigh", "shin0", "shin1", "shin2", "ankle", "pad")]

          + ["bay_liner", "bay_door_port", "bay_door_starboard"] + [f"trap_{i}" for i in range(4)]
          + [f"lift{c}_heads" for c in range(2)] + [f"lift{c}_m{i}" for c in range(2) for i in range(LIFT_N)])


def build():
    comps = rig(SLEGS)
    G = {name: None for name in GROUPS}

    def grp(name, mat):
        if G[name] is None:
            G[name] = Group(name, MAT[mat])
        return G[name]

    ground = -AXIS_H
    # ---- skin: aft body, shoulder armour (s 48..63), fore body, iridium nose
    hull, shoulder, nose = grp("hull", "hull_lacquer"), grp("shoulder", "nose_iridium"), grp("nose", "nose_iridium")
    zone = lambda s: shoulder if 48.0 <= s <= 63.0 else (nose if s >= NB else hull)
    surface(zone, 0.0, 1.0, hull_keep, tip=True)

    # ---- recesses behind every opening
    pl = grp("pocket_liner", "dark")
    for o in OPENINGS:
        if o.get("depth") and not o["name"].startswith("hangar"):
            liner(pl, o, o["depth"])
    hi = grp("hangar_inner", "structure")
    for n in ("hangar_top", "hangar_bottom_starboard", "hangar_bottom_port"):
        liner(hi, OPEN[n], OPEN[n]["depth"])

    # ---- spine over the shoulder (armour ridge, carries the mains and the periscope run)
    g = grp("spine", "nose_iridium")
    N = 40
    rows = []
    for i in range(N + 1):
        t = i / N
        s = 46.0 + 34.0 * t
        w, h, y = 1.2 * (1 - t) + 0.3, 2.4 * (1 - t) ** 1.3, top_y(s)
        rows.append([np.array([-w, y - 0.3, zs(s)]), np.array([0.0, y + h, zs(s)]), np.array([w, y - 0.3, zs(s)])])
    for i in range(N):
        for k in range(2):
            a, b, c, d = rows[i][k], rows[i][k + 1], rows[i + 1][k + 1], rows[i + 1][k]
            out = np.cross(b - a, d - a)
            if out[1] < 0:
                out = -out
            g.quad(*[g.vert(p, out) for p in (a, b, c, d)], out)
    r0 = rows[0]
    g.tri(*[g.vert(p, (0, 0, -1)) for p in r0], np.array([0, 0, -1.0]))

    # ---- dorsal fin 7 (reference: extended)
    prism(grp("fin", "crest_radiator"), [(zs(s), FIN_BASE + h) for s, h in FIN], (0, 0, 1), (0, 1, 0), (1, 0, 0), 0.0, FIN_T)

    # ---- lateral crests with elevons (reference: deployed, elevon neutral) and body flap
    for side, sgn in (("port", -1), ("starboard", 1)):
        X = lambda r: sgn * (CREST_X0 + r)
        ra, rm = ELEV_RA, CREST_R
        poly = [(X(0.6), zs(crest_te(0.6))), (X(0), zs(crest_te_c(0))), (X(0), zs(crest_le_c(0))), (X(0.6), zs(crest_le(0.6))),
                (X(rm), zs(crest_le(rm))), (X(rm), zs(crest_h(rm))), (X(ra), zs(crest_h(ra))), (X(ra), zs(crest_te(ra)))]
        prism(grp(f"crest_{side}", "crest_radiator"), poly, (1, 0, 0), (0, 0, 1), (0, 1, 0), CREST_Y, CREST_T, fan=6)
        el = [(X(ra + 0.05), zs(crest_te(ra + 0.05))), (X(ra + 0.05), zs(crest_h(ra + 0.05) - 0.08)),
              (X(rm), zs(crest_h(rm) - 0.08)), (X(rm), zs(crest_te(rm)))]
        prism(grp(f"elevon_{side}", "mechanism"), el, (1, 0, 0), (0, 0, 1), (0, 1, 0), CREST_Y, CREST_T * 0.7)
    yb = -9.6
    box(grp("body_flap", "nose_iridium"), (-FLAP_HALF + 0.05, yb, zs(FLAP_S0)), (FLAP_HALF - 0.05, yb + FLAP_T, zs(FLAP_S1 - 0.05)))

    # ---- stern: armoured well, back plate, bulkhead, anamezon cups with irises, baffle, planetary ring
    g = grp("well", "structure")
    lathe(g, [(-4.0, WELL_R), (0.2, WELL_R)], center=(0.0, WELL_Y))
    lathe(g, [(-4.0, WELL_R - 0.2), (0.2, WELL_R - 0.2)], center=(0.0, WELL_Y), inward=True)
    lathe(g, [(-4.0, WELL_R - 0.2), (-4.0, WELL_R)], center=(0.0, WELL_Y))
    disc_n(g, (0, WELL_Y, zs(0.25)), (0, 0, -1), WELL_R, seg=48)
    flap_u = [OPEN["flap_starboard"]["u"], OPEN["flap_port"]["u"]]
    ring = []
    for u in UGRID[:-1]:
        inflap = any(a - 1e-9 <= u <= b + 1e-9 for a, b in flap_u)
        o = hull_pt(0.0, u, -(FLAP_T + 0.05) if inflap else 0.0)
        d = o[:2] - np.array([0.0, WELL_Y])
        inner = np.array([0.0, WELL_Y]) + d * min(1.0, WELL_R / np.linalg.norm(d))
        ring.append((g.vert((inner[0], inner[1], zs(0.0)), (0, 0, -1)), g.vert(o, (0, 0, -1))))
    for k in range(len(ring)):
        a, b = ring[k], ring[(k + 1) % len(ring)]
        g.quad(a[0], b[0], b[1], a[1], np.array([0, 0, -1.0]))
    g = grp("baffle", "nose_iridium")
    lathe(g, [(-1.8, 10.2), (0.3, 10.2)], center=(0.0, WELL_Y))
    lathe(g, [(-1.8, 10.0), (0.3, 10.0)], center=(0.0, WELL_Y), inward=True)
    g = grp("cups_anamezon", "boron_nitride")
    for x, y in ANA_CUPS:
        cup(g, x, y, -1.5, 1.8, 3.2)
    for i, (x, y) in enumerate(ANA_CUPS):
        g = grp(f"iris_ana_{i}", "nose_iridium")
        disc_n(g, (x, y, zs(-1.55)), (0, 0, -1), 3.35, seg=24)
        disc_n(g, (x, y, zs(-1.50)), (0, 0, 1), 3.35, seg=24)
    g = grp("cups_planetary", "planetary_cup")
    for x, y in plan_cups():
        cup(g, x, y, -0.6, 0.5, PLAN_CUP_R, seg=12)
    for i, (x, y) in enumerate(plan_cups()):
        g = grp(f"iris_plan_{i}", "nose_iridium")
        disc_n(g, (x, y, zs(-0.65)), (0, 0, -1), 0.8, seg=12)
        disc_n(g, (x, y, zs(-0.62)), (0, 0, 1), 0.8, seg=12)

    # ---- planetary pods (reference: stowed in the bays, door closed, cups aft)
    for i, p in enumerate(PODS):
        patch(grp(f"door_pod_{i}", "hull_lacquer"), OPEN[f"pod_{i}"])
        g = grp(f"pod_{i}", "engine_metal")
        c, e, nn = p["centre"], p["e"], p["n"]
        sec2 = [(-1.5, -0.9), (0.9, -0.9), (1.5, -0.3), (1.5, 0.9), (-1.5, 0.9)]   # upper-inner edge chamfered (swing clearance)
        prism(g, [(float(c @ e) + a, float(c @ nn) + b) for a, b in sec2], e, nn, (0, 0, 1), c[2], POD_L)
        for c in p["cups"]:
            cup(g, c[0], c[1], c[2] - STERN_Z - 0.02, 0.4, 0.42, seg=10)
        tube(g, p["centre"] + p["n"] * (POD_T / 2 - 0.05), p["centre"] + p["n"] * (POD_DEPTH - 0.2), 0.9, n=12)  # trunnion to the door

    # ---- hangar doors s 80..100, shuttle in the top niche, rover platform in the bottom bay
    ht = OPEN["hangar_top"]
    half = lambda o, lo, hi: dict(o, u=(lo, hi))
    patch(grp("door_top_starboard", "hull_lacquer"), half(ht, ht["u"][0], 0.5))
    patch(grp("door_top_port", "hull_lacquer"), half(ht, 0.5, ht["u"][1]))
    patch(grp("door_bottom_starboard", "hull_lacquer"), OPEN["hangar_bottom_starboard"])
    patch(grp("door_bottom_port", "hull_lacquer"), OPEN["hangar_bottom_port"])
    box(hi, (-7.2, 1.2, zs(80.5)), (7.2, 1.6, zs(99.5)))
    box(hi, (-4.0, -3.3, zs(80.5)), (4.0, -2.9, zs(99.5)))
    g = grp("shuttle", "shuttle")
    lathe(g, [(81.5, 0.3), (84.5, 1.9), (92.5, 2.0), (96.5, 1.2), (99.0, 0.2)], center=(0.0, 3.9), seg=20)
    prism(g, [(zs(83.5), 0.0), (zs(91.5), 0.0), (zs(88.5), 6.3), (zs(85.5), 6.3)], (0, 0, 1), (1, 0, 0), (0, 1, 0), 3.2, 0.35)
    prism(g, [(zs(83.5), 0.0), (zs(91.5), 0.0), (zs(88.5), -6.3), (zs(85.5), -6.3)], (0, 0, 1), (1, 0, 0), (0, 1, 0), 3.2, 0.35)
    g = grp("rover_platform", "mechanism")
    box(g, (-3.5, -8.35, zs(81.5)), (3.5, -8.1, zs(98.5)))
    for s0 in (84.0, 93.3):
        box(g, (-2.35, -8.1, zs(s0)), (-1.35, -6.8, zs(s0 + 9)))
        box(g, (1.35, -8.1, zs(s0)), (2.35, -6.8, zs(s0 + 9)))
        box(g, (-1.5, -7.0, zs(s0 + 0.5)), (1.5, -5.5, zs(s0 + 8.5)))

    # ---- flush patches: pin-slot seals, keel hatch, airlock door; periscope; airlock lift
    for name, u0, u1, s0, s1 in overlays():
        surface(lambda s, n=name: grp(n, "hull_lacquer"), u0, u1, rect_keep(s0, s1), off=0.01)
    patch(grp("airlock", "hull_lacquer"), OPEN["airlock_door"])   # flush door, skin-coloured

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
        g = grp(f"pad_{side}", "mechanism")                     # pad on a spreader fork (supports at +-4 m)
        plate(g, A + np.array([0, -PAD_OFF, 0]), (0, 0, 1), (1, 0, 0), (0, 1, 0), PAD_L / 2, PAD_W / 2, PAD_T, PAD_RC)
        top = A[1] - PAD_OFF + PAD_T / 2
        box(g, (A[0] - 0.5, top, A[2] - 4.4), (A[0] + 0.5, top + 0.2, A[2] + 4.4))
        box(g, (A[0] - 0.5, top, A[2] - 0.7), (A[0] + 0.5, A[1] - ANKLE_R * 0.6, A[2] + 0.7))

    # ---- anamezon port: two belly bays under the trap columns, armoured doors, liner walls
    g = grp("bay_liner", "dark")
    for sgn in (-1, 1):
        for xw in (BAY_X0, BAY_X1 - 0.15):
            box(g, (sgn * xw - 0.15, -9.6, zs(BAY_S0)), (sgn * xw + 0.15, -3.0, zs(BAY_S1)))
        box(g, (min(sgn * BAY_X0, sgn * BAY_X1), -3.3, zs(BAY_S0)), (max(sgn * BAY_X0, sgn * BAY_X1), -3.0, zs(BAY_S1)))
        for s_end in (BAY_S0, BAY_S1):
            box(g, (min(sgn * BAY_X0, sgn * BAY_X1), -9.5, zs(s_end) - 0.15), (max(sgn * BAY_X0, sgn * BAY_X1), -3.0, zs(s_end) + 0.15))
    for side in SIDES:
        patch(grp(f"bay_door_{side}", "nose_iridium"), OPEN[f"bay_{side}"], back=0.3)
    for i, (x, y) in enumerate(TRAP_XY):
        trap_geom(grp(f"trap_{i}", "trap_shell"), x, y, CASS_S0, CASS_S1)
    for c, xc in enumerate((5.55, -5.55)):
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

    # ---- stern legs (reference: stowed aft in their pockets, pads folded ground face out)
    g = grp("leg_hinges", "nose_iridium")
    for L in SLEGS:
        tube(g, L["H"] - 1.4 * L["axis"], L["H"] + 1.4 * L["axis"], 1.2, n=16)
    mz = np.array([0, 0, -1.0])
    for i, L in enumerate(SLEGS):
        H, ax = L["H"], L["axis"]
        th = unit(np.cross(ax, mz))                                # blade thickness direction (~ outward)
        obox(grp(f"leg{i}_thigh", "hull_lacquer"), H + mz * LEG_THIGH_L / 2, ax, th, mz,
             LEG_THIGH_W / 2, LEG_THIGH_T / 2, LEG_THIGH_L / 2)
        # cap (lower legs): skin over the pocket between the pad and the thigh-root exit, on the thigh
        s_c0, s_c1 = L["pad_s1"] + 0.05, L["s1"] - 0.05
        if not L["lower"]:
            s_c1 = s_c0
        cc = H + L["n"] * (LEG_FOOT_H - 0.15)
        cc[2] = zs((s_c0 + s_c1) / 2)
        if s_c1 > s_c0:   # flush plate, its front edge bevelled (it rises over the skin as the thigh swings)
            n3 = L["n"]
            top = H + n3 * LEG_FOOT_H
            poly = [(float(top @ n3) + a, z) for a, z in ((0.0, zs(s_c0)), (0.0, zs(s_c1)), (-0.3, zs(s_c1 - 0.8)), (-0.3, zs(s_c0)))]
            gth = grp(f"leg{i}_thigh", "hull_lacquer")
            prism(gth, poly, n3, (0, 0, 1), L["e"], float(top @ L["e"]), L["pw"])
            web_h = LEG_FOOT_H - 0.3 - LEG_THIGH_T / 2
            obox(gth, H + n3 * (LEG_THIGH_T / 2 + web_h / 2) + np.array([0, 0, zs((s_c0 + s_c1 - 2.0) / 2) - H[2]]), L["e"], n3,
                 (0, 0, 1), 0.5, web_h / 2 + 0.05, (s_c1 - s_c0 - 2.0) / 2)
        for k in range(LEG_SHIN_N):
            f = 1 - 0.04 * k
            obox(grp(f"leg{i}_shin{k}", "band"), H + mz * (SHIN_TOP + SHIN_SEG / 2), ax, th, mz,
                 LEG_SHIN_W / 2 * f, LEG_SHIN_T / 2 * f, SHIN_SEG / 2)
        fc = H + mz * LEG_LMIN_S
        g = grp(f"leg{i}_ankle", "mechanism")
        tube(g, fc - ax * 0.9, fc + ax * 0.9, ANKLE_R, n=12)
        g = grp(f"leg{i}_pad", "mechanism")
        # sled pad: both ends bevelled on the leg side (they rise out of the pocket first while the leg swings)
        pc = fc + L["n"] * LEG_PAD_OFF
        n3 = L["n"]
        prof = [(PAD_T / 2, -LEG_PAD_L / 2), (PAD_T / 2, LEG_PAD_L / 2), (-PAD_T / 2, LEG_PAD_L / 2 - 0.7), (-PAD_T / 2, -LEG_PAD_L / 2 + 0.7)]
        prism(g, [(float(pc @ n3) + a, pc[2] + b) for a, b in prof], n3, (0, 0, 1), L["e"], float(pc @ L["e"]), L["pw"])
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

    add("crest_dorsal", "tr", ["fin"], np.array([0, -FIN_RETRACT, 0]))
    ed = ELEV_UP / (ELEV_UP + ELEV_DOWN)
    for side, sgn in (("port", -1), ("starboard", 1)):
        f = add("crest_lateral", "rot", [f"crest_{side}"], (np.array([sgn * CREST_X0, CREST_Y, 0]), np.array([0, 0, 1.0]), sgn * FOLD))
        h0 = np.array([sgn * (CREST_X0 + ELEV_RA), CREST_Y, zs(crest_h(ELEV_RA))])
        h1 = np.array([sgn * CREST_X1, CREST_Y, zs(crest_h(CREST_R))])
        ax = unit(h1 - h0)
        down = -1.0 if (np.cross(ax, [0, 0, -1.0])[1] > 0) else 1.0      # sign that moves the trailing edge down
        add(f"elevon_{side}", "rot", [f"elevon_{side}"], (h0, ax, down * math.radians(ELEV_UP + ELEV_DOWN)), parent=f, d=ed)
    add("body_flap", "rot", ["body_flap"], (np.array([0, -9.6 + FLAP_T / 2, zs(FLAP_S1)]), np.array([1.0, 0, 0]), -math.radians(FLAP_DOWN)))
    # pods: 1 = stowed (mesh pose); the swing opens the door and hangs the pod out
    for i, p in enumerate(PODS):
        sw = add("pod_retract", "rot", [f"door_pod_{i}"], (p["hinge"], np.array([0, 0, 1.0]), -p["swing"]), d=1.0)
        add("pod_swivel", "rot", [f"pod_{i}"], (p["centre"], p["swivel_axis"], POD_SWIVEL_MAX), parent=sw)
    for i, (x, y) in enumerate(ANA_CUPS):
        add("iris_ana", "sc", [f"iris_ana_{i}"], (np.array([x, y, zs(-1.55)]), np.array([0.001, 0.001, 1])))
    for i, (x, y) in enumerate(plan_cups()):
        add("iris_plan", "sc", [f"iris_plan_{i}"], (np.array([x, y, zs(-0.65)]), np.array([0.001, 0.001, 1])))
    ht = OPEN["hangar_top"]
    for name, u, ang in (("door_top_starboard", ht["u"][0], -105), ("door_top_port", ht["u"][1], 105),
                         ("door_bottom_starboard", OPEN["hangar_bottom_starboard"]["u"][1], 95),
                         ("door_bottom_port", OPEN["hangar_bottom_port"]["u"][0], -95)):
        hp = hull_pt(90.0, u)
        add("hangar", "rot", [name], (np.array([hp[0], hp[1], 0.0]), np.array([0, 0, 1.0]), math.radians(ang)))
    add("rover_lift", "tr", ["rover_platform"], np.array([0, (-AXIS_H + 0.25) - (-8.35), 0]))
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
        ank = add(f"shin_len_{side}", "tr", [f"ankle_{side}"], np.array([0, (SHIN_N - 1) * SHIN_STEP, 0]), parent=pi)
        A = T + np.array([0, -LEG_LMAX, 0])
        fb = add(f"pad_fold_{side}", "rot", [], (A, np.array([0, 1.0, 0]), -sgn * math.pi / 2), parent=ank)  # pivot only
        add(f"pad_fold_{side}", "rot", [f"pad_{side}"], (A, np.array([1.0, 0, 0]), math.pi / 2), parent=fb)
    mz = np.array([0, 0, -1.0])
    for i, L in enumerate(legs):
        sw = add(f"leg{i}_swing", "rot", [f"leg{i}_thigh"], (L["H"], L["axis"], L["phi_max"]))
        for k in range(LEG_SHIN_N):
            add(f"leg{i}_ext", "tr", [f"leg{i}_shin{k}"], mz * SHIN_STEP * (k + 1), parent=sw)
        ex = add(f"leg{i}_ext", "tr", [f"leg{i}_ankle"], mz * LEG_EXT_MAX, parent=sw)
        fc = L["H"] + mz * LEG_LMIN_S
        # the pad turns to the ground only once the ankle is clear of the hull (rest: half way; stand: last quarter,
        # the ankle is then behind the stern and beyond the well skirt)
        fr = add(f"leg{i}_foot_rest", "rot", [], (fc, L["fr_ax"], L["fr_ang"]), parent=ex, s0=0.5)   # pivot only
        add(f"leg{i}_foot_stand", "rot", [f"leg{i}_pad"], (fc, L["fs_ax"], L["fs_ang"]), parent=fr, s0=0.75)
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
    p = hull_pt(36.0, o["u"][1] if sgn > 0 else o["u"][0])
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
    L.append(f"constexpr double kLegExtDelay = {LEG_EXT_DELAY};  // stern shins run out over the last {1 - LEG_EXT_DELAY:.0%} of the swing")
    L.append(f"constexpr double kStandR = {STAND_R}, kStandGroundS = {STAND_GROUND_S};")
    L.append(f"constexpr double kPlanR = {PLAN_R}, kWellY = {WELL_Y};")
    L.append(f"constexpr double kFinRetract = {FIN_RETRACT:.3f};")
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
    L.append("};\n\n}  // namespace tantra::mesh\n")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L))


def stowed_states():
    """Flight at 0.9 c: everything folded, doors shut, fin down."""
    st = {"crest_lateral": 1, "crest_dorsal": 1, "pod_retract": 1}
    for s in SIDES:
        st.update({f"track_{s}": (STOW_S - CAR_S0) / (CAR_S1 - CAR_S0), f"slide_{s}": 1.0, f"pitch_{s}": 1.0,
                   f"shin_len_{s}": 1.0, f"pad_fold_{s}": 1.0})
    return st


def preview_poses(legs):
    """Named poses: flight (stowed), resting level (pods hovering), turning 45 deg, standing, hangar open."""
    mid = (53.0 - CAR_S0) / (CAR_S1 - CAR_S0)
    shin = lambda L: (LEG_LMAX - L) / (LEG_LMAX - LEG_LMIN)
    stowed = stowed_states()
    rest = {"pod_retract": 0, "pod_swivel": 90 / 100}
    for s in SIDES:
        rest.update({f"track_{s}": mid, f"shin_len_{s}": shin(AXIS_H - FOOT_H)})
    for i, g in enumerate(legs):
        if g["lower"]:
            rest.update({f"leg{i}_swing": g["phi_rest"] / g["phi_max"], f"leg{i}_ext": g["e_rest"] / LEG_EXT_MAX, f"leg{i}_foot_rest": 1})
    turn_h = 53.0 + 16.0
    turn = {"crest_lateral": 1, "crest_dorsal": 1, "pod_retract": 0, "pod_swivel": 0.45}
    for s in SIDES:
        turn.update({f"track_{s}": mid, f"pitch_{s}": 0.5, f"shin_len_{s}": shin(turn_h - FOOT_H)})
    stand = dict(stowed)
    for i, g in enumerate(legs):
        stand.update({f"leg{i}_swing": g["phi_stand"] / g["phi_max"], f"leg{i}_ext": g["e_stand"] / LEG_EXT_MAX, f"leg{i}_foot_stand": 1})
    hang = dict(rest)
    hang.update({"hangar": 1, "rover_lift": 1, "bay_doors": 1, "elevon_port": 1, "elevon_starboard": 0, "body_flap": 1})
    # ship pose: pitch about the hip (trunnion) at s 53, lift of that point above its level height
    return [("flight (stowed)", stowed, 0, 0, (15, -60)), ("resting level", rest, 0, 0, (10, -120)),
            ("turning 45 deg", turn, 45, turn_h - AXIS_H, (8, -80)), ("standing", stand, 90, 12 + 53 - AXIS_H, (8, -60)),
            ("hangar open", hang, 0, 0, (-15, -60))]


def pose_world(P, pitch, lift, pivot_s=53.0):
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
    groups, legs = build()
    out = os.path.join(root, "Meshes", "Tantra")
    os.makedirs(out, exist_ok=True)
    write_msh(groups, os.path.join(out, "Tantra.msh"))
    trap = Group("trap", MAT["trap_shell"])
    trap_geom(trap, 0.0, 0.0, -12.4 - STERN_Z, 12.4 - STERN_Z)
    write_msh([trap], os.path.join(out, "TantraTrap.msh"))
    raw_dir = os.path.join(here, "..", "build", "mesh")
    os.makedirs(raw_dir, exist_ok=True)
    write_json(groups, os.path.join(raw_dir, "tantra_raw.json"))
    write_layout(legs, rig(legs), os.path.join(here, "..", "orbiter2016", "MeshLayout.h"))
    nv = sum(len(g.v) for g in groups)
    nt = sum(len(g.t) for g in groups)
    print(f"Tantra.msh: {len(groups)} groups, {nv} vertices, {nt} triangles; MeshLayout.h written")
