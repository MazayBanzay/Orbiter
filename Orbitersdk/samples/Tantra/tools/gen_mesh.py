"""Generate the Tantra mesh (Orbiter .msh, MSHX1) and the animation layout header.

Layout "Spear C-146", final design (docs/DESIGN.md, Tantra_Design/mockup_spear_c146.html):
armoured shoulder with the deflector coil, folding crests, stern well with iris-shuttered
anamezon cups and a ring of 12 planetary cups behind a baffle, small auxiliary pods on the
shoulder, carriage («лафет») columns formed from band drums, four stern legs on long feet.

Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane,
z = s + STERN_Z (mesh origin at s = 38). Hull axis 14 m above ground while resting level.

Every movable part is built in ONE reference pose and moved by Orbiter animations.
The reference pose, pivots, axes and angles are written to orbiter2016/MeshLayout.h so
the C++ animation code and this mesh can never disagree. `--preview` renders the mesh in
several poses by applying exactly the same transforms (Orbiter convention: a positive
angle about +z turns +x towards +y; about +x turns +y towards +z; children are
transformed by their parents).
"""
import math
import os
import sys

import numpy as np

STERN_Z = -38.0
AXIS_H = 14.0
SEG = 48


def zs(s):
    return s + STERN_Z


def unit(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


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


def disc_n(g, centre, normal, radius, seg=24, both=False):
    """Flat disc with an arbitrary normal."""
    c = np.asarray(centre, float)
    n = unit(normal)
    ref = np.array([0, 1, 0]) if abs(n[1]) < 0.9 else np.array([1, 0, 0])
    u = unit(np.cross(n, ref))
    v = np.cross(n, u)
    for side in ((1, -1) if both else (1,)):
        nn = side * n
        c0 = g.vert(c, nn)
        ring = [g.vert(c + radius * (math.cos(2 * math.pi * k / seg) * u + math.sin(2 * math.pi * k / seg) * v), nn)
                for k in range(seg)]
        for k in range(seg):
            g.tri(c0, ring[k], ring[(k + 1) % seg], nn)


def prism(g, poly2d, axis_a, axis_b, axis_n, offset_n, thickness):
    """Flat plate: convex polygon in the (axis_a, axis_b) plane, extruded along axis_n."""
    A, B, N = (np.array(x, float) for x in (axis_a, axis_b, axis_n))
    h = thickness / 2
    pts = [p[0] * A + p[1] * B for p in poly2d]
    center = sum(pts) / len(pts) + offset_n * N
    for side in (+1, -1):
        nrm = side * N
        idx = [g.vert(p + (offset_n + side * h) * N, nrm) for p in pts]
        for i in range(1, len(idx) - 1):
            g.tri(idx[0], idx[i], idx[i + 1], nrm)
    m = len(pts)
    for i in range(m):
        p, q = pts[i], pts[(i + 1) % m]
        out = np.cross(q - p, N)
        mid = (p + q) / 2 + offset_n * N
        if np.dot(out, mid - center) < 0:
            out = -out
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
        out = dirs[k] + dirs[k1]
        g.quad(ring0[k], ring0[k1], ring1[k1], ring1[k], out)
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
    for axis in range(3):
        for sgn in (-1, 1):
            n = np.zeros(3)
            n[axis] = sgn
            a, b = [i for i in range(3) if i != axis]
            pts = []
            for ua, ub in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = c.copy()
                p[axis] = hi[axis] if sgn > 0 else lo[axis]
                p[a] = hi[a] if ua else lo[a]
                p[b] = hi[b] if ub else lo[b]
                pts.append(g.vert(p, n))
            g.quad(*pts, n)


def obox(g, centre, ax, ay, az, hx, hy, hz):
    """Oriented box: half sizes along three orthonormal axes."""
    c = np.asarray(centre, float)
    A = [unit(ax), unit(ay), unit(az)]
    H = [hx, hy, hz]
    for i in range(3):
        for sgn in (-1, 1):
            n = sgn * A[i]
            j, k = [q for q in range(3) if q != i]
            pts = []
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                pts.append(g.vert(c + n * H[i] + a * H[j] * A[j] + b * H[k] * A[k], n))
            g.quad(*pts, n)


def cup(g, cx, cy, s_rim, depth, radius, seg=24):
    """Concave reflector cup opening aft; the back face sits 3 cm behind (no z-fighting)."""
    prof = [(s_rim + depth * (1 - t * t), radius * t) for t in np.linspace(0.02, 1.0, 8)]
    lathe(g, prof, center=(cx, cy), seg=seg)
    lathe(g, [(s + 0.03, r) for s, r in prof], center=(cx, cy), inward=True, seg=seg)


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
]
MATERIALS.append(("trap_shell", (0.44, 0.47, 0.56), (0.45, 0.45, 0.50, 30), (0, 0, 0)))   # anamezon trap containers
MAT = {m[0]: i + 1 for i, m in enumerate(MATERIALS)}  # .msh material indices are 1-based

HULL_KEYS = [
    (0.0, 12.0, 13.0, -12.2), (8.0, 13.5, 14.0, -9.5), (29.0, 14.0, 14.0, -9.5), (47.0, 14.0, 14.0, -9.5), (53.0, 11.6, 12.0, -9.0),
    (58.0, 9.5, 9.5, -8.8), (62.0, 8.6, 8.6, -8.6), (122.0, 8.6, 8.6, -8.6),
]
NOSE_KEYS = [(122.0, 8.6, 8.6, -8.6), (130.0, 7.4, 7.4, -7.4), (138.0, 4.8, 4.8, -4.8),
             (143.0, 2.2, 2.2, -2.2), (146.0, 0.15, 0.15, -0.15)]


def section(keys, sv):
    for (s0, *a), (s1, *b) in zip(keys, keys[1:]):
        if s0 <= sv <= s1:
            t = (sv - s0) / (s1 - s0) if s1 > s0 else 0.0
            t = t * t * (3 - 2 * t)
            return [x + (y - x) * t for x, y in zip(a, b)]
    return list(keys[-1][1:]) if sv > keys[-1][0] else list(keys[0][1:])


def loft(g, keys, s0, s1, n, seg=SEG, arc=None, grow=0.0):
    a0, a1 = (0.0, 360.0) if arc is None else arc
    closed = arc is None
    na = seg if closed else max(2, int(seg * (a1 - a0) / 360.0))
    rows = []
    for i in range(n + 1):
        sv = s0 + (s1 - s0) * i / n
        w, top, bot = section(keys, sv)
        yc = (top + bot) / 2
        row = []
        for j in range(na if closed else na + 1):
            th = math.radians(a0 + (a1 - a0) * j / na)
            c, sn = math.cos(th), math.sin(th)
            x = (w + grow) * c
            y = (top + grow) * sn if sn >= 0 else (abs(bot) + grow) * sn
            row.append(g.vert((x, y, zs(sv)), (x, y - yc, 0.0)))
        rows.append(row)
    for i in range(n):
        for j in range(na):
            j1 = (j + 1) % na if closed else j + 1
            a, b, c2, d = rows[i][j], rows[i][j1], rows[i + 1][j1], rows[i + 1][j]
            mid = (g.v[a] + g.v[b] + g.v[c2] + g.v[d]) / 4
            w, top, bot = section(keys, mid[2] - STERN_Z)
            out = np.array([mid[0], mid[1] - (top + bot) / 2, 0.0])
            if np.linalg.norm(out) < 1e-6:
                out = np.array([0.0, 0.0, 1.0])
            g.quad(a, b, c2, d, out)


def hull_half(s):
    return section(HULL_KEYS, s)[0]


# ---------------------------------------------------------------------------
# Geometry of the moving parts (mirrors core/Spec.h)

# Carriage («лафет») legs, one per flank. Hip on a trunnion carriage running in a flank pocket; flat
# thigh blade (band drums inside); band-mast shin of nested sections that collapse into the thigh;
# two-axis ankle; flat pad that folds against the thigh with its ground face out and closes the pocket.
# The pocket is boxed in by the trap columns (x <= 10.45), the lateral crests (s < 28) and the
# deflector coil in the shoulder (s > 48); the leg sizes follow from that.
CAR_S0, CAR_S1 = 32.0, 46.0                    # hip (trunnion) track
CAR_SREF = CAR_S0                              # legs built with the hip at the track start
STOW_S = 46.0                                  # hip station with the leg folded aft into the pocket
POCKET_S0, POCKET_S1 = 29.0, 48.0             # flank pocket along the ship
POCKET_D = 3.4                                 # pocket depth at the flank (back wall x ~ 10.6)
HIP_X_OUT, HIP_X_IN, HIP_R = 19.0, 11.65, 2.0  # hip deployed / stowed; stowed thigh inner face 10.65 (trap hoops 10.6)
THIGH_L, THIGH_W, THIGH_T = 8.0, 6.0, 2.0      # blade: 6 m fore-aft when hanging, 2 m across (band drums 1.9 m)
SHIN_N, SHIN_SEG, SHIN_OVL, SHIN_TOP = 10, 7.4, 0.3, 0.6
SHIN_W, SHIN_T = 5.4, 1.7
SHIN_STEP = SHIN_SEG - SHIN_OVL
ANKLE_R, PAD_OFF = 0.8, 1.3                    # ankle ball; pad mid-plane below the ankle centre
PAD_L, PAD_W, PAD_T, PAD_RC = 16.0, 6.0, 0.6, 0.6   # stowed, its ground face is the skin over the pocket
LEG_LMIN = THIGH_L + ANKLE_R                    # hip -> ankle, shin collapsed
LEG_LMAX = SHIN_TOP + (SHIN_N - 1) * SHIN_STEP + SHIN_SEG + ANKLE_R
FOOT_H = PAD_OFF + PAD_T / 2                   # ankle centre above the ground
# Stern legs: the same parts; hinges sunk under the skin at the aft corners, pads 11 x 6.5 m.
LEG_S, LEG_THIGH_L, LEG_THIGH_W, LEG_SHIN_N = 5.0, 8.0, 5.0, 3
LEG_LMIN_S = LEG_THIGH_L + ANKLE_R
LEG_EXT_MAX = LEG_SHIN_N * SHIN_STEP
LEG_PAD_L, LEG_PAD_W = 11.0, 5.0
LEG_CORNERS = [45.0, 135.0, 225.0, 315.0]      # hull (loft) angles of the aft corners
CORNER_S0, CORNER_S1, CORNER_D = 2.5, 19.5, 3.3
STAND_R, STAND_GROUND_S = 26.0, -12.0
REST_GROUND_Y = -AXIS_H
POD_S, POD_X_OUT, POD_X_IN = 60.0, 15.0, 10.6
CREST_X0, CREST_X1, CREST_Y = 13.5, 27.4, -1.0  # lateral crest root line on the skin and tip; s 6..28 root, 11..21 tip
CREST_DX = (CREST_X1 - CREST_X0) / 4


def crest_front(x):
    return 6.0 + (11.0 - 6.0) * (x - CREST_X0) / (CREST_X1 - CREST_X0)


def crest_rear(x):
    return 28.0 + (21.0 - 28.0) * (x - CREST_X0) / (CREST_X1 - CREST_X0)


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


def skin_pt(s, deg, grow=0.0):
    w, top, bot = section(HULL_KEYS, s)
    th = math.radians(deg)
    c, sn = math.cos(th), math.sin(th)
    return np.array([(w + grow) * c, ((top + grow) * sn if sn >= 0 else (abs(bot) + grow) * sn), zs(s)])


def inside(p, tol=0.0):
    """Is point p (x, y, z) inside the hull outline at its station (grown by tol)?"""
    w, top, bot = section(HULL_KEYS, p[2] - STERN_Z)
    b = top if p[1] >= 0 else abs(bot)
    return (p[0] / (w + tol)) ** 2 + (p[1] / (b + tol)) ** 2 <= 1.0


def loft_deg(s, direction, target):
    """Loft angle whose skin point lies `target` along `direction` (2D unit), searched near it."""
    base = math.degrees(math.atan2(direction[1], direction[0]))
    best, err = base, 1e9
    for k in range(-900, 901):
        d = base + k * 0.05
        e = abs(np.dot(skin_pt(s, d)[:2], direction) - target)
        if e < err:
            best, err = d, e
    return best


def flank_opening():
    """Loft angles of the flank pocket edges (starboard, about 0 deg) for a pad of half width PAD_W/2."""
    s = 38.0
    up = math.degrees(math.asin(PAD_W / 2 / section(HULL_KEYS, s)[1]))
    dn = math.degrees(math.asin(PAD_W / 2 / abs(section(HULL_KEYS, s)[2])))
    return -dn, up


def leg_hinges():
    """Stern leg hinges: as far out as the folded pad (11 x 5 m, ground face out) stays under the skin."""
    out = []
    dz = np.array([0, 0, 1.0])
    for deg in LEG_CORNERS:
        u = unit(skin_pt(12.0, deg)[:2])
        side = np.array([-u[1], u[0]])
        r = np.linalg.norm(skin_pt(12.0, deg)[:2])
        while True:   # outer face at radius r: all pad corners inside the skin
            ok = True
            for sv in (LEG_S + LEG_LMIN_S - LEG_PAD_L / 2, LEG_S + LEG_LMIN_S + LEG_PAD_L / 2):
                for e in (-1, 1):
                    p2 = u * r + side * e * LEG_PAD_W / 2
                    ok = ok and inside(np.array([p2[0], p2[1], zs(sv)]))
            if ok:
                break
            r -= 0.02
        out.append(u * (r - (PAD_OFF + PAD_T / 2)))
    return out


def corner_opening(u, r_out):
    """Loft angles of a corner pocket opening that just fits the folded pad."""
    side = np.array([-u[1], u[0]])
    return (loft_deg(12.0, side, -LEG_PAD_W / 2 - 0.05), loft_deg(12.0, side, LEG_PAD_W / 2 + 0.05))


def leg_layout():
    """Per stern leg: hinge, swing axis/angles, extensions and pad rotations (rest/stand)."""
    out = []
    for i, (hx, hy) in enumerate(leg_hinges()):
        H = np.array([hx, hy, zs(LEG_S)])
        rad = unit([hx, hy, 0.0])
        F_stand = np.array([STAND_R * rad[0], STAND_R * rad[1], zs(STAND_GROUND_S) + FOOT_H])  # ankle over its pad
        d_stand = F_stand - H
        L_stand = np.linalg.norm(d_stand)
        dz = np.array([0, 0, 1.0])
        axis = unit(np.cross(dz, d_stand))
        phi_stand = math.acos(np.dot(dz, unit(d_stand)))
        perp = unit(d_stand - np.dot(d_stand, dz) * dz)          # in-plane, perpendicular to the stowed leg
        lower = hy < 0
        phi_rest = e_rest = 0.0
        if lower:  # level rest: swing straight across (90 deg) until the pad meets the ground
            phi_rest = math.pi / 2
            L_rest = (REST_GROUND_Y + FOOT_H - H[1]) / perp[1]
            e_rest = L_rest - LEG_LMIN_S
        # pad built stowed: long axis along +z, leg-side ("up") normal pointing inboard (-rad)
        B0 = np.column_stack([dz, -rad, np.cross(dz, -rad)])

        def pad_child(phi, long_t, up_t):
            Bt = np.column_stack([long_t, up_t, np.cross(long_t, up_t)])
            return axis_angle(rot(axis, phi).T @ (Bt @ B0.T))

        fs_ax, fs_ang = pad_child(phi_stand, rad, dz)                       # radial on the ground, up = ship axis
        fr_ax, fr_ang = pad_child(phi_rest, np.array([np.sign(hx), 0, 0]), np.array([0, 1.0, 0])) if lower \
            else (np.array([1.0, 0, 0]), 0.0)
        e_stand = L_stand - LEG_LMIN_S
        assert 0.0 <= e_stand <= LEG_EXT_MAX and 0.0 <= e_rest <= LEG_EXT_MAX, (i, e_stand, e_rest)
        out.append(dict(H=H, axis=axis, phi_stand=phi_stand, e_stand=e_stand, phi_rest=phi_rest,
                        e_rest=e_rest, lower=lower, rad=rad, fs_ax=fs_ax, fs_ang=fs_ang, fr_ax=fr_ax, fr_ang=fr_ang))
    return out


def rounded_rect(a, b, r, n=4):
    """Convex rounded rectangle (half sizes a, b; corner radius r) as a 2D polygon."""
    pts = []
    for cx, cy, a0 in ((a - r, b - r, 0), (-a + r, b - r, 90), (-a + r, -b + r, 180), (a - r, -b + r, 270)):
        for k in range(n + 1):
            t = math.radians(a0 + 90.0 * k / n)
            pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return pts


def plate(g, centre, long_ax, short_ax, normal, half_l, half_w, t, rc):
    """Rounded flat pad centred at `centre` (long/short axes in its plane, `normal` across)."""
    A, B, N = unit(long_ax), unit(short_ax), unit(normal)
    c = np.asarray(centre, float)
    poly = [(np.dot(c, A) + u, np.dot(c, B) + v) for u, v in rounded_rect(half_l, half_w, rc)]
    prism(g, poly, A, B, N, np.dot(c, N), t)


def pocket(g, s0, s1, a0, a1, depth, n):
    """Dark recess behind an opening in the skin: back wall, side walls, end walls."""
    loft(g, HULL_KEYS, s0, s1, n, arc=(a0, a1), grow=-depth)
    ss = np.linspace(s0, s1, n + 1)
    for deg, sg in ((a0, 1), (a1, -1)):
        for k in range(n):
            p = [skin_pt(ss[k], deg), skin_pt(ss[k + 1], deg), skin_pt(ss[k + 1], deg, -depth), skin_pt(ss[k], deg, -depth)]
            inward = skin_pt((ss[k] + ss[k + 1]) / 2, deg + sg * 2.0) - skin_pt((ss[k] + ss[k + 1]) / 2, deg)
            g.quad(*[g.vert(q, inward) for q in p], inward)
    m = max(2, int(abs(a1 - a0) / 4))
    ths = np.linspace(a0, a1, m + 1)
    for s_end, sg in ((s0, 1), (s1, -1)):
        nrm = np.array([0, 0, sg * 1.0])
        for k in range(m):
            p = [skin_pt(s_end, ths[k]), skin_pt(s_end, ths[k + 1]), skin_pt(s_end, ths[k + 1], -depth), skin_pt(s_end, ths[k], -depth)]
            g.quad(*[g.vert(q, nrm) for q in p], nrm)


# ---------------------------------------------------------------------------
# Groups (stable order = module contract, written to MeshLayout.h)

GROUPS = (["hull", "shoulder", "nose", "crest_root", "crest_dorsal"] + [f"crest_{side}_{k}" for side in ("port", "starboard") for k in range(4)] + [
           "well", "baffle", "cups_anamezon"]
          + [f"iris_ana_{i}" for i in range(4)]
          + ["cups_planetary"] + [f"iris_plan_{i}" for i in range(12)]
          + ["pod_port", "pod_starboard", "pylon_port", "pylon_starboard",
             "door_top_port", "door_top_starboard", "door_bottom_port", "door_bottom_starboard",
             "hangar_inner", "shuttle", "rover_platform", "hatches", "airlock", "pocket_liner"]
          + [f"{part}_{side}" for side in ("port", "starboard") for part in ("carriage", "hip", "thigh", "ankle", "pad")]
          + [f"shin_{side}_{i}" for side in ("port", "starboard") for i in range(10)]
          + ["leg_hinges"]
          + [f"leg{i}_{part}" for i in range(4) for part in ("thigh", "shin0", "shin1", "shin2", "ankle", "pad")]
          + ["airlock_lift"]
          + ["bay_liner", "bay_door_port", "bay_door_starboard"] + [f"trap_{i}" for i in range(4)]
          + [f"lift{c}_heads" for c in range(2)] + [f"lift{c}_m{i}" for c in range(2) for i in range(8)])

PLAN_R, PLAN_CUP_R = 11.0, 0.75
ANA_CUPS = [(-4.4, 4.4), (4.4, 4.4), (-4.4, -4.4), (4.4, -4.4)]


# Anamezon port and trap columns (mirrors core/Spec.h: traps 10 x 25 m, s 24..49).
# Trap cassettes: an octagonal armoured cassette around the cylindrical magnetic trap (ISO-tank
# principle): flat bottom, flat faces for the slot guides, end blocks with the field store, the
# connector and an axial trunnion at each end. They are lifted by their trunnions by fork heads
# on telescopic masts in shafts beyond both ends of each column (like spent-fuel casks).
CASS_W, CASS_CH = 9.8, 3.2                                         # across flats, corner chamfer
CASS_S0, CASS_S1 = 21.5, 46.3                                       # cassette body
TRAP_S0, TRAP_S1, TRAP_R = CASS_S0, CASS_S1, CASS_W / 2             # (names kept for the container mesh)
TRUN_S0, TRUN_S1 = 20.9, 46.9                                       # trunnion ends
TRAP_XY = [(5.55, -3.15), (-5.55, -3.15), (5.55, 7.0), (-5.55, 7.0)]  # lower stbd, lower port, upper stbd, upper port
BAY_S0, BAY_S1 = 19.7, 48.1                                         # bays include the end shafts
BAY_ARC = [(220.8, 267.95), (272.05, 319.2)]                       # hull angles of the port / starboard door
LIFT_Y0, LIFT_TRAVEL, LIFT_CEIL = 7.0, 36.0, 11.6                  # head centre at rest, travel, mast top
LIFT_N, LIFT_SEG = 8, 5.2
HEAD_S = (20.4, 47.4)
TRAP_MOUTH_Y = -13.6                                               # centre of a cassette just clear of the belly


def octagon(cx, cy, w, ch):
    h = w / 2
    return [(cx - h + ch, cy - h), (cx + h - ch, cy - h), (cx + h, cy - h + ch), (cx + h, cy + h - ch),
            (cx + h - ch, cy + h), (cx - h + ch, cy + h), (cx - h, cy + h - ch), (cx - h, cy - h + ch)]


def trap_geom(g, cx, cy, s0, s1):
    """Trap cassette: octagonal armour with hoop bands, end blocks and axial trunnions (s0..s1 = body)."""
    L = s1 - s0
    zc = zs((s0 + s1) / 2)
    prism(g, octagon(cx, cy, CASS_W, CASS_CH), (1, 0, 0), (0, 1, 0), (0, 0, 1), zc, L)
    for sb in (s0 + 3.0, s0 + 8.5, s1 - 8.5, s1 - 3.0):             # hoop bands (they carry the field pressure)
        prism(g, octagon(cx, cy, CASS_W + 0.3, CASS_CH + 0.06), (1, 0, 0), (0, 1, 0), (0, 0, 1), zs(sb), 0.6)
    for se, sg in ((s0, -1), (s1, 1)):                               # end blocks: field store, connector, trunnion
        prism(g, octagon(cx, cy, CASS_W - 1.2, CASS_CH - 0.4), (1, 0, 0), (0, 1, 0), (0, 0, 1), zs(se + sg * 0.15), 0.3)
        tube(g, (cx, cy, zs(se)), (cx, cy, zs(se + sg * 0.6)), 1.0, n=16)
        tube(g, (cx, cy, zs(se + sg * 0.45)), (cx, cy, zs(se + sg * 0.6)), 1.5, n=16)
        box(g, (cx - 2.2, cy + 1.6, zs(se) - 0.2 if sg < 0 else zs(se)), (cx - 0.8, cy + 3.0, zs(se) if sg < 0 else zs(se) + 0.2))


def plan_cups():
    return [(PLAN_R * math.cos(2 * math.pi * (k + 0.5) / 12), PLAN_R * math.sin(2 * math.pi * (k + 0.5) / 12)) for k in range(12)]


def build():
    G = {name: None for name in GROUPS}

    def grp(name, mat):
        G[name] = Group(name, MAT[mat])
        return G[name]

    ground = -AXIS_H
    # Hull, hangar cut out s 79..101 (doors are separate groups).
    g = grp("hull", "hull_lacquer")
    loft(g, HULL_KEYS, 0.0, CORNER_S0, 2)
    corners = [corner_opening(unit(h), 0.0) for h in leg_hinges()]
    for k in range(4):                                                                  # skin between the corner pockets
        a0 = corners[k][1]
        a1 = corners[(k + 1) % 4][0] + (360.0 if k == 3 else 0.0)
        loft(g, HULL_KEYS, CORNER_S0, CORNER_S1, 8, arc=(a0, a1))
    loft(g, HULL_KEYS, CORNER_S1, BAY_S0, 1)
    fdn, fup = flank_opening()
    for a, b, n in ((BAY_S0, POCKET_S0, 5), (POCKET_S1, BAY_S1, 1)):
        loft(g, HULL_KEYS, a, b, n, arc=(BAY_ARC[1][1], BAY_ARC[0][0] + 360.0))         # all but the bays and keel
        loft(g, HULL_KEYS, a, b, n, arc=(BAY_ARC[0][1], BAY_ARC[1][0]))                 # keel strip between them
    for arc in ((fup, 180.0 - fup), (180.0 - fdn, BAY_ARC[0][0]),                         # flank pockets open
                (BAY_ARC[0][1], BAY_ARC[1][0]), (BAY_ARC[1][1], 360.0 + fdn)):
        loft(g, HULL_KEYS, POCKET_S0, POCKET_S1, 10, arc=arc)
    loft(g, HULL_KEYS, BAY_S1, 79.0, 28)
    loft(g, HULL_KEYS, 79.0, 101.0, 11, arc=(150.0, 240.0))
    loft(g, HULL_KEYS, 79.0, 101.0, 11, arc=(300.0, 390.0))
    loft(g, HULL_KEYS, 101.0, 122.0, 12)
    g = grp("shoulder", "nose_iridium")          # second shield: armoured belt with the deflector coil inside
    loft(g, HULL_KEYS, 48.0, 63.0, 10, grow=0.35)
    g = grp("nose", "nose_iridium")
    loft(g, NOSE_KEYS, 122.0, 146.0, 16)

    # Crests: dorsal blade telescopes into its root fairing; lateral fins fold up against the sides.
    g = grp("crest_root", "crest_radiator")
    box(g, (-0.9, 13.3, zs(2.0)), (0.9, 14.6, zs(44.0)))
    g = grp("crest_dorsal", "crest_radiator")
    prism(g, [(zs(2), 13.5), (zs(44), 13.5), (zs(32), 36.0), (zs(10), 36.0)], (0, 0, 1), (0, 1, 0), (1, 0, 0), 0.0, 1.2)
    # Lateral crests: four telescopic spanwise sections; the shorter outer ones slide into the root one.
    for side, sgn in (("port", -1), ("starboard", 1)):
        for k in range(4):
            xa, xb = CREST_X0 + k * CREST_DX, CREST_X0 + (k + 1) * CREST_DX
            pts = [(zs(crest_front(xa)), sgn * xa), (zs(crest_rear(xa)), sgn * xa),
                   (zs(crest_rear(xb)), sgn * xb), (zs(crest_front(xb)), sgn * xb)]
            prism(grp(f"crest_{side}_{k}", "crest_radiator"), pts, (0, 0, 1), (1, 0, 0), (0, 1, 0), CREST_Y, 0.8 - 0.08 * k)

    # Stern: armoured well, back plate, anamezon cups with irises, baffle lip, planetary ring with irises.
    g = grp("well", "structure")
    lathe(g, [(-4.0, 11.8), (0.2, 11.8)])
    lathe(g, [(-4.0, 11.6), (0.2, 11.6)], inward=True)
    disc_n(g, (0, 0, zs(0.25)), (0, 0, -1), 11.8, seg=48)
    # Stern bulkhead: closes the annulus between the well rim and the hull outline at s = 0.
    w0, t0, b0 = section(HULL_KEYS, 0.0)
    ring = []
    for k in range(SEG):
        th = 2 * math.pi * k / SEG
        c, sn = math.cos(th), math.sin(th)
        ox, oy = w0 * c, (t0 if sn >= 0 else abs(b0)) * sn
        ro = math.hypot(ox, oy)
        f = 11.8 / ro if ro > 11.8 else 1.0
        inner = (ox * f, oy * f) if ro > 11.8 else (ox, oy)
        ring.append((g.vert((inner[0], inner[1], zs(0.0)), (0, 0, -1)), g.vert((ox, oy, zs(0.0)), (0, 0, -1))))
    for k in range(SEG):
        a, b = ring[k], ring[(k + 1) % SEG]
        g.quad(a[0], b[0], b[1], a[1], np.array([0, 0, -1.0]))
    g = grp("baffle", "nose_iridium")
    lathe(g, [(-1.8, 10.2), (0.3, 10.2)])
    lathe(g, [(-1.8, 10.0), (0.3, 10.0)], inward=True)
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

    # Auxiliary pods on the shoulder (reference: deployed, swivel 0 = thrust forward).
    for side, sgn in (("port", -1), ("starboard", 1)):
        g = grp(f"pod_{side}", "engine_metal")
        cx = sgn * POD_X_OUT
        box(g, (cx - 1.5, -1.5, zs(POD_S - 2.8)), (cx + 1.5, 1.5, zs(POD_S + 2.8)))
        for dy in (-0.9, 0.0, 0.9):
            cup(g, cx, dy, POD_S - 3.1, 0.4, 0.45, seg=10)
        g = grp(f"pylon_{side}", "nose_iridium")
        box(g, (min(sgn * 9.0, sgn * 13.5), -0.5, zs(POD_S - 1.5)), (max(sgn * 9.0, sgn * 13.5), 0.5, zs(POD_S + 1.5)))

    # Hangar doors s 79..101, interior, shuttle in the top niche, rover platform in the bottom bay.
    for name, arc in (("door_top_port", (90.0, 150.0)), ("door_top_starboard", (30.0, 90.0)),
                      ("door_bottom_port", (240.0, 270.0)), ("door_bottom_starboard", (270.0, 300.0))):
        loft(grp(name, "hull_lacquer"), HULL_KEYS, 81.0, 99.0, 9, arc=arc, grow=0.05)
    g = grp("hangar_inner", "structure")
    loft(g, HULL_KEYS, 79.0, 81.0, 1, arc=(30.0, 150.0))
    loft(g, HULL_KEYS, 99.0, 101.0, 1, arc=(30.0, 150.0))
    loft(g, HULL_KEYS, 79.0, 81.0, 1, arc=(240.0, 300.0))
    loft(g, HULL_KEYS, 99.0, 101.0, 1, arc=(240.0, 300.0))
    box(g, (-7.4, 1.2, zs(81)), (7.4, 1.6, zs(99)))
    box(g, (-4.0, -3.3, zs(81)), (4.0, -2.9, zs(99)))
    g = grp("shuttle", "shuttle")
    lathe(g, [(81.5, 0.3), (84.5, 1.9), (92.5, 2.0), (96.5, 1.2), (99.0, 0.2)], center=(0.0, 3.9), seg=20)
    prism(g, [(zs(83.5), 0.0), (zs(91.5), 0.0), (zs(88.5), 6.3), (zs(85.5), 6.3)], (0, 0, 1), (1, 0, 0), (0, 1, 0), 3.2, 0.35)
    prism(g, [(zs(83.5), 0.0), (zs(91.5), 0.0), (zs(88.5), -6.3), (zs(85.5), -6.3)], (0, 0, 1), (1, 0, 0), (0, 1, 0), 3.2, 0.35)
    g = grp("rover_platform", "mechanism")
    box(g, (-3.5, -8.25, zs(81.5)), (3.5, -8.0, zs(98.5)))
    g2 = Group("tmp", 0)
    for s0 in (84.0, 93.3):
        box(g, (-2.35, -8.0, zs(s0)), (-1.35, -6.7, zs(s0 + 9)))
        box(g, (1.35, -8.0, zs(s0)), (2.35, -6.7, zs(s0 + 9)))
        box(g, (-1.5, -6.9, zs(s0 + 0.5)), (1.5, -5.4, zs(s0 + 8.5)))
    del g2

    # Keel hatch, port airlock with its lift, periscope.
    g = grp("hatches", "dark")
    box(g, (-1.2, -9.25, zs(56.0)), (1.2, -8.75, zs(58.5)))
    g = grp("airlock", "dark")
    dz = zs(104.0)
    box(g, (-8.75, -2.0, dz - 1.5), (-8.45, 3.0, dz + 1.5))
    box(g, (-13.0, -2.3, dz - 1.6), (-8.5, -2.0, dz + 1.6))
    box(g, (-0.4, 8.4, zs(119) - 0.4), (0.4, 10.6, zs(119) + 0.4))
    g = grp("airlock_lift", "mechanism")          # lift column to the ground (reference: extended, level)
    box(g, (-13.0, ground, dz - 0.5), (-12.2, -2.0, dz + 0.5))
    # Pockets: carriage legs in the flanks, stern legs in the aft corners.
    g = grp("pocket_liner", "dark")
    pocket(g, POCKET_S0, POCKET_S1, fdn, fup, POCKET_D, 10)
    pocket(g, POCKET_S0, POCKET_S1, 180.0 - fup, 180.0 - fdn, POCKET_D, 10)
    for a0, a1 in corners:
        pocket(g, CORNER_S0, CORNER_S1, a0, a1, CORNER_D, 8)
    for sgn in (-1, 1):                              # rails of the hip carriage on the pocket back wall
        for y in (-1.5, 1.5):
            box(g, (min(sgn * 10.55, sgn * 10.85), y - 0.25, zs(POCKET_S0 + 0.3)), (max(sgn * 10.55, sgn * 10.85), y + 0.25, zs(POCKET_S1 - 0.3)))

    # Carriage legs (reference: hip out at the track start, leg hanging, shin fully out, pad open).
    for side, sgn in (("port", -1), ("starboard", 1)):
        T = np.array([sgn * HIP_X_OUT, 0.0, zs(CAR_SREF)])
        g = grp(f"carriage_{side}", "mechanism")          # shoe on the rails
        box(g, (min(sgn * 10.85, sgn * 11.5), -2.0, T[2] - 2.2), (max(sgn * 10.85, sgn * 11.5), 2.0, T[2] + 2.2))
        g = grp(f"hip_{side}", "mechanism")                # trunnion pin (telescopes into the shoe) + hip housing
        tube(g, np.array([sgn * 11.3, 0, T[2]]), T - np.array([sgn * 0.9, 0, 0]), 1.1, n=16)
        tube(g, T - np.array([sgn * 0.95, 0, 0]), T + np.array([sgn * 1.3, 0, 0]), HIP_R, n=20)
        g = grp(f"thigh_{side}", "hull_lacquer")          # flat blade, band drums inside
        obox(g, T + np.array([0, -THIGH_L / 2, 0]), (1, 0, 0), (0, 1, 0), (0, 0, 1), THIGH_T / 2, THIGH_L / 2, THIGH_W / 2)
        for i in range(SHIN_N):                            # nested band-mast sections, thinner downwards
            g = grp(f"shin_{side}_{i}", "band")
            k = 1.0 - 0.012 * i
            yc = -(SHIN_TOP + i * SHIN_STEP + SHIN_SEG / 2)
            obox(g, T + np.array([0, yc, 0]), (1, 0, 0), (0, 1, 0), (0, 0, 1), SHIN_T / 2 * k, SHIN_SEG / 2, SHIN_W / 2 * k)
        A = T + np.array([0, -LEG_LMAX, 0])
        g = grp(f"ankle_{side}", "mechanism")              # two-axis ankle
        tube(g, A - np.array([0, 0, 0.9]), A + np.array([0, 0, 0.9]), ANKLE_R, n=12)
        g = grp(f"pad_{side}", "mechanism")                # pad: long axis fore-aft, ground face down
        plate(g, A + np.array([0, -PAD_OFF, 0]), (0, 0, 1), (1, 0, 0), (0, 1, 0), PAD_L / 2, PAD_W / 2, PAD_T, PAD_RC)
        tube(g, A + np.array([0, -0.3, 0]), A + np.array([0, -PAD_OFF + PAD_T / 2, 0]), 0.6, n=12)

    # Anamezon port: two belly bays under the trap columns, armoured doors, dark liner.
    g = grp("bay_liner", "dark")                  # walls follow the belly curve so nothing shows outside
    belly = lambda x: -9.5 * math.sqrt(max(0.0, 1.0 - (x / 14.0) ** 2)) + 0.25
    for sgn in (-1, 1):
        for xw in (0.5, 10.6):
            box(g, (sgn * xw - 0.15, belly(xw), zs(BAY_S0)), (sgn * xw + 0.15, -3.0, zs(BAY_S1)))
        xs = np.linspace(0.5, 10.6, 9)
        poly = [(sgn * x, belly(x)) for x in xs] + [(sgn * 10.6, -3.0), (sgn * 0.5, -3.0)]
        if sgn < 0:
            poly = poly[::-1]
        for s_end in (BAY_S0, BAY_S1):
            prism(g, poly, (1, 0, 0), (0, 1, 0), (0, 0, 1), zs(s_end), 0.3)
    for side, arc in (("port", BAY_ARC[0]), ("starboard", BAY_ARC[1])):
        loft(grp(f"bay_door_{side}", "nose_iridium"), HULL_KEYS, BAY_S0, BAY_S1, 12, arc=arc, grow=0.05)
    # Four trap cassettes in two columns (reference: in their slots).
    for i, (x, y) in enumerate(TRAP_XY):
        trap_geom(grp(f"trap_{i}", "trap_shell"), x, y, CASS_S0, CASS_S1)
    # Column lifts: fork heads at both trunnions, telescopic masts from the shaft ceilings
    # (reference: heads at the upper slot, masts collapsed).
    for c, xc in enumerate((5.55, -5.55)):
        g = grp(f"lift{c}_heads", "mechanism")
        for sh, sg in zip(HEAD_S, (-1, 1)):
            z = zs(sh)
            box(g, (xc - 1.5, LIFT_Y0 - 1.9, z - 0.6), (xc + 1.5, LIFT_Y0 - 0.9, z + 0.6))      # fork jaws
            box(g, (xc - 1.5, LIFT_Y0 + 0.9, z - 0.6), (xc + 1.5, LIFT_Y0 + 1.9, z + 0.6))
            box(g, (xc - 1.5, LIFT_Y0 - 1.9, z - sg * 0.6 - (0.4 if sg > 0 else 0)),
                (xc + 1.5, LIFT_Y0 + 1.9, z - sg * 0.6 + (0.4 if sg < 0 else 0)))             # back plate
        for i in range(LIFT_N):
            g = grp(f"lift{c}_m{i}", "band")
            w = 1.1 - 0.06 * i
            for sh in HEAD_S:
                tube(g, (xc, LIFT_CEIL, zs(sh)), (xc, LIFT_CEIL - LIFT_SEG, zs(sh)), w / 2 * math.sqrt(2), n=4)

    # Stern legs (reference: stowed in the corner pockets, pads folded ground face out).
    legs = leg_layout()
    g = grp("leg_hinges", "nose_iridium")
    for L in legs:
        tube(g, L["H"] - 1.3 * L["axis"], L["H"] + 1.3 * L["axis"], HIP_R * 0.9, n=16)
    for i, L in enumerate(legs):
        H, rad = L["H"], L["rad"]
        dz = np.array([0, 0, 1.0])
        side = np.cross(dz, rad)
        obox(grp(f"leg{i}_thigh", "hull_lacquer"), H + dz * LEG_THIGH_L / 2, dz, rad, side, LEG_THIGH_L / 2, THIGH_T / 2, LEG_THIGH_W / 2)
        for k in range(LEG_SHIN_N):
            obox(grp(f"leg{i}_shin{k}", "band"), H + dz * (SHIN_TOP + SHIN_SEG / 2), dz, rad, side,
                 SHIN_SEG / 2, SHIN_T / 2 * (1 - 0.03 * k), (LEG_THIGH_W - 0.6) / 2 * (1 - 0.03 * k))
        fc = H + dz * LEG_LMIN_S
        tube(grp(f"leg{i}_ankle", "mechanism"), fc - side * 0.9, fc + side * 0.9, ANKLE_R, n=12)
        g = grp(f"leg{i}_pad", "mechanism")
        plate(g, fc + rad * PAD_OFF, dz, side, rad, LEG_PAD_L / 2, LEG_PAD_W / 2, PAD_T, PAD_RC)
        tube(g, fc + rad * 0.3, fc + rad * (PAD_OFF - PAD_T / 2), 0.5, n=12)
    return [G[name] for name in GROUPS], legs


# ---------------------------------------------------------------------------
# Animation rig (the same one the C++ code builds; used here for previews)


def rig(legs):
    """Orbiter rig. Each group is owned by exactly ONE component; parents move their children
    (groups and pivots) automatically, so a parent never lists a child's groups. Components with
    no groups are pure pivots (C++: a LOCALVERTEXLIST dummy).
    Entry: anim, kind (rot: ref, axis, ang | tr: shift | sc: ref, scale), groups, parent."""
    C = []

    def add(anim, kind, groups, par, parent=None, s0=0.0, s1=1.0, d=0.0):
        C.append(dict(anim=anim, kind=kind, groups=groups, par=par, parent=parent, s0=s0, s1=s1, d=d))
        return len(C) - 1

    add("crest_dorsal", "sc", ["crest_dorsal"], (np.array([0, 13.5, 0]), np.array([1, 0.06, 1])))
    # Lateral crests: first the sections telescope in (0 .. 0.5), then the root folds up against the flank.
    for side, sgn in (("port", -1), ("starboard", 1)):
        f = add("crest_lateral", "rot", [f"crest_{side}_0"], (np.array([sgn * CREST_X0, CREST_Y, 0]), np.array([0, 0, 1.0]),
                                                                sgn * math.pi / 2), s0=0.5, s1=1.0)
        for k in range(1, 4):
            add("crest_lateral", "tr", [f"crest_{side}_{k}"], np.array([-sgn * k * CREST_DX, 0, 0]), parent=f, s0=0.0, s1=0.5)
    for side, sgn in (("port", -1), ("starboard", 1)):
        r = add("pod_retract", "tr", [f"pylon_{side}"], np.array([-sgn * (POD_X_OUT - POD_X_IN), 0, 0]))
        add("pod_swivel", "rot", [f"pod_{side}"], (np.array([sgn * POD_X_OUT, 0, zs(POD_S)]), np.array([1.0, 0, 0]),
                                                    -math.radians(100)), parent=r)
    for i, (x, y) in enumerate(ANA_CUPS):
        add("iris_ana", "sc", [f"iris_ana_{i}"], (np.array([x, y, zs(-1.55)]), np.array([0.001, 0.001, 1])))
    for i, (x, y) in enumerate(plan_cups()):
        add("iris_plan", "sc", [f"iris_plan_{i}"], (np.array([x, y, zs(-0.65)]), np.array([0.001, 0.001, 1])))
    c30, s30 = 8.6 * math.cos(math.radians(30)), 8.6 * math.sin(math.radians(30))
    add("hangar", "rot", ["door_top_starboard"], (np.array([c30, s30, 0]), np.array([0, 0, 1.0]), -math.radians(105)))
    add("hangar", "rot", ["door_top_port"], (np.array([-c30, s30, 0]), np.array([0, 0, 1.0]), math.radians(105)))
    add("hangar", "rot", ["door_bottom_starboard"], (np.array([s30, -c30, 0]), np.array([0, 0, 1.0]), -math.radians(95)))
    add("hangar", "rot", ["door_bottom_port"], (np.array([-s30, -c30, 0]), np.array([0, 0, 1.0]), math.radians(95)))
    add("rover_lift", "tr", ["rover_platform"], np.array([0, (-AXIS_H + 0.25) - (-8.25), 0]))
    add("airlock_lift", "sc", ["airlock_lift"], (np.array([-12.6, -2.0, zs(104.0)]), np.array([1, 0.02, 1])))  # 1 = up
    for side, sgn in (("port", -1), ("starboard", 1)):
        T = np.array([sgn * HIP_X_OUT, 0.0, zs(CAR_SREF)])
        run = np.array([0, 0, CAR_S1 - CAR_S0])
        tr = add(f"track_{side}", "tr", [], run)                                              # pivot only
        add(f"track_{side}", "tr", [f"carriage_{side}"], run)
        sl = add(f"slide_{side}", "tr", [f"hip_{side}"], np.array([-sgn * (HIP_X_OUT - HIP_X_IN), 0, 0]), parent=tr)
        pi = add(f"pitch_{side}", "rot", [f"thigh_{side}"], (T, np.array([1.0, 0, 0]), math.pi / 2), parent=sl)  # 1 = along the hull, aft
        # Shin: state 1 = collapsed into the thigh (translations only under the pitch pivot).
        for i in range(SHIN_N):
            add(f"shin_len_{side}", "tr", [f"shin_{side}_{i}"], np.array([0, i * SHIN_STEP, 0]), parent=pi)
        ank = add(f"shin_len_{side}", "tr", [f"ankle_{side}"], np.array([0, (SHIN_N - 1) * SHIN_STEP, 0]), parent=pi)
        # Pad fold (1 = folded against the thigh, ground face out): about the leg axis, then across it.
        A = T + np.array([0, -LEG_LMAX, 0])
        fb = add(f"pad_fold_{side}", "rot", [], (A, np.array([0, 1.0, 0]), -sgn * math.pi / 2), parent=ank)  # pivot only
        add(f"pad_fold_{side}", "rot", [f"pad_{side}"], (A, np.array([1.0, 0, 0]), math.pi / 2), parent=fb)
    for i, L in enumerate(legs):
        dz = np.array([0, 0, 1.0])
        sw = add(f"leg{i}_swing", "rot", [f"leg{i}_thigh"], (L["H"], L["axis"], L["phi_stand"]))
        for k in range(LEG_SHIN_N):
            add(f"leg{i}_ext", "tr", [f"leg{i}_shin{k}"], dz * SHIN_STEP * (k + 1), parent=sw)
        ex = add(f"leg{i}_ext", "tr", [f"leg{i}_ankle"], dz * LEG_EXT_MAX, parent=sw)
        fc = L["H"] + dz * LEG_LMIN_S
        fr = add(f"leg{i}_foot_rest", "rot", [], (fc, L["fr_ax"], L["fr_ang"]), parent=ex)        # pivot only
        add(f"leg{i}_foot_stand", "rot", [f"leg{i}_pad"], (fc, L["fs_ax"], L["fs_ang"]), parent=fr)
    # Anamezon port: doors; column lifts (heads + telescopic masts); cassettes riding with the heads; empty slots.
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
    w, top, bot = section(HULL_KEYS, 36.0)
    a = math.radians(BAY_ARC[1][1] if sgn > 0 else BAY_ARC[0][0])
    return np.array([(w + 0.05) * math.cos(a), (abs(bot) + 0.05) * math.sin(a), 0.0])


def apply_pose(groups, comps, states):
    """Vertices after the rig at the given states: each group gets parent chain @ own transform."""
    V = {g.name: np.array(g.v, float) for g in groups}
    M = {}

    def mat(i):
        if i in M:
            return M[i]
        c = comps[i]
        st = states.get(c["anim"], c.get("d", 0.0))
        f = min(1.0, max(0.0, (st - c["s0"]) / (c["s1"] - c["s0"])))
        f -= min(1.0, max(0.0, (c.get("d", 0.0) - c["s0"]) / (c["s1"] - c["s0"])))  # the mesh sits at the default state
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

    for i, c in enumerate(comps):
        for gname in c["groups"]:
            m = mat(i)
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
    L.append(f"constexpr double kHipXOut = {HIP_X_OUT}, kHipXIn = {HIP_X_IN};")
    L.append(f"constexpr double kLegLMin = {LEG_LMIN:.3f}, kLegLMax = {LEG_LMAX:.3f};  // hip -> ankle, shin in / out")
    L.append(f"constexpr double kFootH = {FOOT_H:.3f}, kPadHalfL = {PAD_L / 2}, kPadHalfW = {PAD_W / 2};  // ankle above ground; pad")
    L.append(f"constexpr double kPodS = {POD_S}, kPodXOut = {POD_X_OUT}, kPodXIn = {POD_X_IN}, kPodSwivelMax = {math.radians(100):.6f};")
    L.append(f"constexpr double kLegS = {LEG_S}, kLegLMinS = {LEG_LMIN_S:.3f}, kLegExtMax = {LEG_EXT_MAX:.3f};")
    L.append(f"constexpr double kStandR = {STAND_R}, kStandGroundS = {STAND_GROUND_S};")
    L.append(f"constexpr double kPlanR = {PLAN_R};")
    L.append("constexpr double kTrapXY[4][2] = {" + ", ".join("{%.2f, %.2f}" % t for t in TRAP_XY) + "};  // lower stbd, lower port, upper stbd, upper port")
    L.append(f"constexpr double kTrapZ = {zs((CASS_S0 + CASS_S1) / 2):.3f}, kCassW = {CASS_W}, kCassLen = {CASS_S1 - CASS_S0:.1f}, kTrapMouthY = {TRAP_MOUTH_Y};")
    L.append(f"constexpr double kLiftY0 = {LIFT_Y0}, kLiftTravel = {LIFT_TRAVEL};  // head (= cassette centre) at rest, travel down")
    L.append("struct LegRig { V hinge, axis; double phiStand, extStand, phiRest, extRest; bool lower;")
    L.append("               V footStandAxis; double footStandAng; V footRestAxis; double footRestAng; V radial; };")
    L.append("constexpr LegRig kLegs[4] = {")
    for g in legs:
        L.append(f"    {{{v3(g['H'])}, {v3(g['axis'])}, {g['phi_stand']:.6f}, {g['e_stand']:.4f}, {g['phi_rest']:.6f}, "
                 f"{g['e_rest']:.4f}, {'true' if g['lower'] else 'false'},")
        L.append(f"     {v3(g['fs_ax'])}, {g['fs_ang']:.6f}, {v3(g['fr_ax'])}, {g['fr_ang']:.6f}, {v3(g['rad'])}}},")
    L.append("};\n")
    # Animation rig: one entry per component, parents first (same data the preview uses).
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
        defs.setdefault(c["anim"], c.get("d", 0.0))
    L.append("constexpr double kAnimDef[ANIM_COUNT] = {" + ", ".join(f"{defs[a]:.3f}" for a in anims) + "};  // mesh pose")
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


def preview_poses(legs):
    """Named poses for the preview: flight (stowed), resting level, turning 45 deg, standing, hangar open."""
    both = ("port", "starboard")
    mid = (38.0 - CAR_S0) / (CAR_S1 - CAR_S0)
    shin = lambda L: (LEG_LMAX - L) / (LEG_LMAX - LEG_LMIN)
    stowed = {}
    for s in both:
        stowed.update({f"track_{s}": 1.0, f"slide_{s}": 1.0, f"pitch_{s}": 1.0, f"shin_len_{s}": 1.0, f"pad_fold_{s}": 1.0})
    rest = {}
    for s in both:
        rest.update({f"track_{s}": mid, f"shin_len_{s}": shin(AXIS_H - FOOT_H)})
    for i, g in enumerate(legs):
        if g["lower"]:
            rest.update({f"leg{i}_swing": g["phi_rest"] / g["phi_stand"], f"leg{i}_ext": g["e_rest"] / LEG_EXT_MAX,
                         f"leg{i}_foot_rest": 1})
    turn_h = 38.0 + 16.0
    turn = {"crest_lateral": 1, "crest_dorsal": 1, "pod_retract": 1}
    for s in both:
        turn.update({f"track_{s}": mid, f"pitch_{s}": 0.5, f"shin_len_{s}": shin(turn_h - FOOT_H)})
    stand = dict(stowed)
    stand.update({"crest_lateral": 1, "crest_dorsal": 1, "pod_retract": 1})
    for i, g in enumerate(legs):
        stand.update({f"leg{i}_swing": 1, f"leg{i}_ext": g["e_stand"] / LEG_EXT_MAX, f"leg{i}_foot_stand": 1})
    hang = dict(rest)
    hang.update({"hangar": 1, "rover_lift": 1})
    return [("flight (stowed)", stowed, 0, 0, (15, -60)), ("resting level", rest, 0, 0, (10, -120)),
            ("turning 45 deg", turn, 45, turn_h - AXIS_H, (8, -80)), ("standing", stand, 90, 12 + 38 - AXIS_H, (8, -60)),
            ("hangar open", hang, 0, 0, (-15, -60))]


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
        R = rot([1, 0, 0], -math.radians(pitch))  # ship pitch nose-up, about the origin (trunnion = CG)
        ax = fig.add_subplot(1, len(poses), k + 1, projection="3d")
        for g in groups:
            P = (R @ V[g.name].T).T + np.array([0, lift, 0])
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
    trap = Group("trap", MAT["trap_shell"])            # stand-alone container vessel (TantraTrap), centred
    trap_geom(trap, 0.0, 0.0, -12.4 - STERN_Z, 12.4 - STERN_Z)
    write_msh([trap], os.path.join(out, "TantraTrap.msh"))
    raw_dir = os.path.join(here, "..", "build", "mesh")
    os.makedirs(raw_dir, exist_ok=True)
    write_json(groups, os.path.join(raw_dir, "tantra_raw.json"))
    write_layout(legs, rig(legs), os.path.join(here, "..", "orbiter2016", "MeshLayout.h"))
    nv = sum(len(g.v) for g in groups)
    nt = sum(len(g.t) for g in groups)
    print(f"Tantra.msh: {len(groups)} groups, {nv} vertices, {nt} triangles; MeshLayout.h written")
    for i, g in enumerate(legs):
        print(f"leg{i}: phi_stand {math.degrees(g['phi_stand']):.1f} ext {g['e_stand']:.2f} | "
              f"phi_rest {math.degrees(g['phi_rest']):.1f} ext {g['e_rest']:.2f} | foot stand {math.degrees(g['fs_ang']):.1f} rest {math.degrees(g['fr_ang']):.1f}")
    if "--preview" in sys.argv:
        comps = rig(legs)
        poses = preview_poses(legs)
        preview(groups, comps, sys.argv[sys.argv.index("--preview") + 1], poses)
