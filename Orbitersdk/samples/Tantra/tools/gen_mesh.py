"""Generate the Tantra mesh (Orbiter .msh, MSHX1) and the animation layout header.

Layout "Spear C-146", final design (docs/DESIGN.md, Tantra_Design/mockup_spear_c146.html):
armoured shoulder with the deflector coil, folding crests, stern well with iris-shuttered
anamezon cups and a ring of 12 planetary cups behind a baffle, small auxiliary pods on the
shoulder, carriage («лафет») columns formed from band drums, four stern legs on long feet.

Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane,
z = s + STERN_Z (mesh origin at s = 38). Hull axis 20 m above ground while resting level.

Every movable part is built in ONE reference pose and moved by Orbiter animations.
The reference pose, pivots, axes and angles are written to orbiter2010/MeshLayout.h so
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
AXIS_H = 20.0
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
    (0.0, 12.0, 13.0, -9.0), (8.0, 13.5, 14.0, -9.5), (47.0, 14.0, 14.0, -9.5), (53.0, 11.6, 12.0, -9.0),
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

CAR_S0, CAR_S1 = 34.0, 58.0                   # carriage track along the flank
CAR_SREF = CAR_S0                              # carriage built at the track start (all anims start at 0)
LID_S = 46.0                                   # lid hinge line taken at mid-track
CAR_X_OUT, CAR_X_IN = 17.5, 8.5                # trunnion plane deployed / stowed
MAST_W, MAST_LMAX = 4.2, 72.0                  # square mast, full length (trunnion -> foot top)
MAST_N, MAST_SEG, MAST_OVL = 12, 6.0, 0.6      # telescope: 12 sections of 6 m, each slides into the one above
CFOOT_L, CFOOT_W, CFOOT_T, CFOOT_PAD = 26.0, 2.6, 1.2, 3.2
LEG_S, LEG_CASING, LEG_R, LEG_STAGE_R = 5.0, 13.0, 2.2, 1.7
LEG_FOOT_L, LEG_FOOT_W, LEG_FOOT_T, LEG_PAD = 16.0, 2.0, 1.2, 2.3
LEG_HINGES = [(9.6, 8.8), (-9.6, 8.8), (-9.6, -6.8), (9.6, -6.8)]   # 45, 135, 225, 315 deg corners
STAND_R, STAND_GROUND_S, FOOT_HALF_T = 21.0, -12.0, 0.6
REST_GROUND_Y = -AXIS_H
POD_S, POD_X_OUT, POD_X_IN = 60.0, 15.0, 10.6


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


def leg_layout():
    """Per stern leg: hinge, swing axis/angles, extensions and foot rotations (rest/stand)."""
    out = []
    for i, (hx, hy) in enumerate(LEG_HINGES):
        H = np.array([hx, hy, zs(LEG_S)])
        rad = unit([hx, hy, 0.0])
        F_stand = np.array([STAND_R * rad[0], STAND_R * rad[1], zs(STAND_GROUND_S + FOOT_HALF_T)])
        d_stand = F_stand - H
        L_stand = np.linalg.norm(d_stand)
        dz = np.array([0, 0, 1.0])
        axis = unit(np.cross(dz, d_stand))
        phi_stand = math.acos(np.dot(dz, unit(d_stand)))
        perp = unit(d_stand - np.dot(d_stand, dz) * dz)          # in-plane, perpendicular to the stowed leg
        lower = hy < 0
        phi_rest = e_rest = 0.0
        if lower:  # horizontal rest: swing straight across (90 deg) until the foot meets the ground
            phi_rest = math.pi / 2
            L_rest = (REST_GROUND_Y + FOOT_HALF_T - H[1]) / perp[1]
            e_rest = L_rest - LEG_CASING
        # foot built "collected": beam along +z, pad normal = radial, centred at the casing tip
        B0 = np.column_stack([dz, rad, np.cross(dz, rad)])

        def foot_child(phi, beam_t, n_t):
            Bt = np.column_stack([beam_t, n_t, np.cross(beam_t, n_t)])
            return axis_angle(rot(axis, phi).T @ (Bt @ B0.T))

        fs_ax, fs_ang = foot_child(phi_stand, rad, dz)                       # radial beam, ground = ship axis
        fr_ax, fr_ang = foot_child(phi_rest, np.array([np.sign(hx), 0, 0]), np.array([0, 1.0, 0])) if lower \
            else (np.array([1.0, 0, 0]), 0.0)
        out.append(dict(H=H, axis=axis, phi_stand=phi_stand, e_stand=L_stand - LEG_CASING, phi_rest=phi_rest,
                        e_rest=e_rest, lower=lower, rad=rad, fs_ax=fs_ax, fs_ang=fs_ang, fr_ax=fr_ax, fr_ang=fr_ang))
    return out


# ---------------------------------------------------------------------------
# Groups (stable order = module contract, written to MeshLayout.h)

GROUPS = (["hull", "shoulder", "nose", "crest_root", "crest_dorsal", "crest_port", "crest_starboard",
           "well", "baffle", "cups_anamezon"]
          + [f"iris_ana_{i}" for i in range(4)]
          + ["cups_planetary"] + [f"iris_plan_{i}" for i in range(12)]
          + ["pod_port", "pod_starboard", "pylon_port", "pylon_starboard",
             "door_top_port", "door_top_starboard", "door_bottom_port", "door_bottom_starboard",
             "hangar_inner", "shuttle", "rover_platform", "hatches", "airlock", "track_lids_static"]
          + [f"{part}_{side}" for side in ("port", "starboard") for part in ("lid", "carriage", "cfoot")]
          + [f"mast_{side}_{i}" for side in ("port", "starboard") for i in range(12)]
          + ["leg_hinges"]
          + [f"leg{i}_{part}" for i in range(4) for part in ("casing", "stage", "foot")]
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
    loft(g, HULL_KEYS, 0.0, BAY_S0, 12)
    loft(g, HULL_KEYS, BAY_S0, BAY_S1, 12, arc=(BAY_ARC[1][1], BAY_ARC[0][0] + 360.0))   # all but the bays and keel
    loft(g, HULL_KEYS, BAY_S0, BAY_S1, 12, arc=(BAY_ARC[0][1], BAY_ARC[1][0]))          # keel strip between them
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
    for name, sgn in (("crest_port", -1), ("crest_starboard", 1)):
        g = grp(name, "crest_radiator")
        prism(g, [(zs(2), sgn * 12.5), (zs(30), sgn * 12.5), (zs(22), sgn * 26.0), (zs(8), sgn * 26.0)],
              (0, 0, 1), (1, 0, 0), (0, 1, 0), -1.0, 0.8)

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
    # Flank slots of the carriage tracks: dark recess following the hull curvature just inside the
    # skin (flat strips cut through the curved shoulder and showed as black shards).
    g = grp("track_lids_static", "dark")
    loft(g, HULL_KEYS, CAR_S0, CAR_S1, 12, arc=(-14.0, 14.0), grow=-0.6)
    loft(g, HULL_KEYS, CAR_S0, CAR_S1, 12, arc=(166.0, 194.0), grow=-0.6)

    # Carriage per side (reference: deployed at s = CAR_SREF, mast full length, lid open).
    for side, sgn in (("port", -1), ("starboard", 1)):
        g = grp(f"lid_{side}", "hull_lacquer")    # long armoured lid over the track, hinged at its top edge
        a0, a1 = (-17.0, 17.0) if sgn > 0 else (163.0, 197.0)
        loft(g, HULL_KEYS, CAR_S0, CAR_S1, 12, arc=(a0, a1), grow=0.45)
        T = np.array([sgn * CAR_X_OUT, 0.0, zs(CAR_SREF)])
        g = grp(f"carriage_{side}", "mechanism")
        obox(g, T - np.array([sgn * 2.0, 0, 0]), (0, 0, 1), (0, 1, 0), (1, 0, 0), 3.5, 2.6, 1.3)       # carriage
        tube(g, T - np.array([sgn * 3.6, 0, 0]), T + np.array([sgn * 1.6, 0, 0]), 3.4, n=20)           # band drum
        tube(g, np.array([sgn * 9.0, 0, zs(CAR_SREF)]), T - np.array([sgn * 3.3, 0, 0]), 1.4, n=12)   # trunnion pin
        for i in range(MAST_N):   # nested square sections, thinner downwards, extended in the reference
            g = grp(f"mast_{side}_{i}", "band")
            w = MAST_W - 0.16 * i
            top = T - np.array([0, i * MAST_SEG - (0.0 if i == 0 else MAST_OVL), 0])
            tube(g, top, T - np.array([0, (i + 1) * MAST_SEG, 0]), w / 2 * math.sqrt(2), n=4)
        g = grp(f"cfoot_{side}", "mechanism")
        fc = T - np.array([0, MAST_LMAX + CFOOT_T / 2, 0])
        box(g, fc - np.array([CFOOT_W / 2, CFOOT_T / 2, CFOOT_L / 2]), fc + np.array([CFOOT_W / 2, CFOOT_T / 2, CFOOT_L / 2]))
        for e in (-1, 1):
            disc_n(g, fc + np.array([0, -CFOOT_T / 2, e * CFOOT_L / 2]), (0, -1, 0), CFOOT_PAD, both=True)
            tube(g, fc + np.array([0, -CFOOT_T / 2, e * CFOOT_L / 2]), fc + np.array([0, CFOOT_T / 2, e * CFOOT_L / 2]),
                 CFOOT_PAD, n=16)

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

    # Stern legs (reference: stowed along the corners, collected feet).
    legs = leg_layout()
    g = grp("leg_hinges", "nose_iridium")
    for L in legs:
        tube(g, L["H"] - 2.6 * L["axis"], L["H"] + 2.6 * L["axis"], 2.0, n=12)
    for i, L in enumerate(legs):
        H = L["H"]
        dz = np.array([0, 0, 1.0])
        tube(grp(f"leg{i}_casing", "hull_lacquer"), H, H + LEG_CASING * dz, LEG_R, n=12)
        tube(grp(f"leg{i}_stage", "mechanism"), H + 1.0 * dz, H + (LEG_CASING - 0.2) * dz, LEG_STAGE_R, n=12)
        g = grp(f"leg{i}_foot", "mechanism")
        fc = H + LEG_CASING * dz
        rad = L["rad"]
        side = np.cross(dz, rad)
        obox(g, fc, dz, rad, side, LEG_FOOT_L / 2, LEG_FOOT_T / 2, LEG_FOOT_W / 2)
        for e in (-1, 1):
            p = fc + e * (LEG_FOOT_L / 2) * dz
            tube(g, p - rad * LEG_FOOT_T / 2, p + rad * LEG_FOOT_T / 2, LEG_PAD, n=16)
    return [G[name] for name in GROUPS], legs


# ---------------------------------------------------------------------------
# Animation rig (the same one the C++ code builds; used here for previews)


def lid_hinge(sgn):
    w, top, _ = section(HULL_KEYS, LID_S)
    return np.array([sgn * (w + 0.45) * math.cos(math.radians(17)), (top + 0.45) * math.sin(math.radians(17)), 0.0])


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
    add("crest_lateral", "rot", ["crest_starboard"], (np.array([12.5, -1.0, 0]), np.array([0, 0, 1.0]), math.pi / 2))
    add("crest_lateral", "rot", ["crest_port"], (np.array([-12.5, -1.0, 0]), np.array([0, 0, 1.0]), -math.pi / 2))
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
        add(f"lid_{side}", "rot", [f"lid_{side}"], (lid_hinge(sgn), np.array([0, 0, 1.0]), sgn * math.radians(100)))
        tr = add(f"track_{side}", "tr", [], np.array([0, 0, CAR_S1 - CAR_S0]))                 # pivot only
        sl = add(f"slide_{side}", "tr", [f"carriage_{side}"], np.array([-sgn * (CAR_X_OUT - CAR_X_IN), 0, 0]), parent=tr)
        T = np.array([sgn * CAR_X_OUT, 0.0, zs(CAR_SREF)])
        pi = add(f"pitch_{side}", "rot", [], (T, np.array([1.0, 0, 0]), math.pi / 2), parent=sl)   # pivot only
        # Telescope: state 1 = collapsed to one section. Section i slides up by i sections, the foot with the
        # last one. Only translations under the pitch pivot (Orbiter 2010 turns scale vectors with a rotated parent).
        for i in range(MAST_N):
            add(f"mast_len_{side}", "tr", [f"mast_{side}_{i}"], np.array([0, i * MAST_SEG, 0]), parent=pi)
        add(f"mast_len_{side}", "tr", [f"cfoot_{side}"], np.array([0, (MAST_N - 1) * MAST_SEG, 0]), parent=pi)
    for i, L in enumerate(legs):
        sw = add(f"leg{i}_swing", "rot", [f"leg{i}_casing"], (L["H"], L["axis"], L["phi_stand"]))
        ex = add(f"leg{i}_ext", "tr", [f"leg{i}_stage"], np.array([0, 0, 10.0]), parent=sw)
        fc = L["H"] + np.array([0, 0, LEG_CASING])
        fr = add(f"leg{i}_foot_rest", "rot", [], (fc, L["fr_ax"], L["fr_ang"]), parent=ex)       # pivot only
        add(f"leg{i}_foot_stand", "rot", [f"leg{i}_foot"], (fc, L["fs_ax"], L["fs_ang"]), parent=fr)
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
    L.append(f"constexpr double kCarS0 = {CAR_S0}, kCarS1 = {CAR_S1}, kCarSRef = {CAR_SREF};  // track, reference station")
    L.append(f"constexpr double kCarXOut = {CAR_X_OUT}, kCarXIn = {CAR_X_IN};")
    L.append(f"constexpr double kMastLMax = {MAST_LMAX}, kMastSeg = {MAST_SEG};  // trunnion -> foot top; one section")
    L.append(f"constexpr double kCFootL = {CFOOT_L}, kCFootT = {CFOOT_T};")
    L.append(f"constexpr double kLidAngle = {math.radians(100):.6f}, kLidHingeX = {lid_hinge(1)[0]:.5f}, kLidHingeY = {lid_hinge(1)[1]:.5f};")
    L.append(f"constexpr double kPodS = {POD_S}, kPodXOut = {POD_X_OUT}, kPodXIn = {POD_X_IN}, kPodSwivelMax = {math.radians(100):.6f};")
    L.append(f"constexpr double kLegS = {LEG_S}, kLegCasing = {LEG_CASING}, kLegExtMax = 10.0;")
    L.append(f"constexpr double kStandR = {STAND_R}, kStandGroundS = {STAND_GROUND_S}, kFootHalfT = {FOOT_HALF_T};")
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
    L.append("struct RigComp { int anim, kind, group; V ref, vec; double angle; int parent; };  // group -1 = pivot only")
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
        L.append(f"    {{ANIM_{c['anim'].upper()}, {kind}, {grp}, {v3(ref)}, {v3(vec)}, {ang:.6f}, {par}}},")
    L.append("};\n\n}  // namespace tantra::mesh\n")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L))


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
    write_layout(legs, rig(legs), os.path.join(here, "..", "orbiter2010", "MeshLayout.h"))
    nv = sum(len(g.v) for g in groups)
    nt = sum(len(g.t) for g in groups)
    print(f"Tantra.msh: {len(groups)} groups, {nv} vertices, {nt} triangles; MeshLayout.h written")
    for i, g in enumerate(legs):
        print(f"leg{i}: phi_stand {math.degrees(g['phi_stand']):.1f} ext {g['e_stand']:.2f} | "
              f"phi_rest {math.degrees(g['phi_rest']):.1f} ext {g['e_rest']:.2f} | foot stand {math.degrees(g['fs_ang']):.1f} rest {math.degrees(g['fr_ang']):.1f}")
    if "--preview" in sys.argv:
        comps = rig(legs)
        mid = (38.0 - CAR_S0) / (CAR_S1 - CAR_S0)  # trunnion on the CG = origin
        flight = {"slide_port": 1, "slide_starboard": 1, "mast_len_port": 1, "mast_len_starboard": 1,
                  "track_port": mid, "track_starboard": mid}
        rest_len = 1 - (AXIS_H - 1.2) / MAST_LMAX
        rest = {"lid_port": 1, "lid_starboard": 1, "track_port": mid, "track_starboard": mid,
                "mast_len_port": (MAST_LMAX - (AXIS_H - 1.2)) / (MAST_LMAX - MAST_SEG), "mast_len_starboard": (MAST_LMAX - (AXIS_H - 1.2)) / (MAST_LMAX - MAST_SEG)}
        for i, g in enumerate(legs):
            if g["lower"]:
                rest[f"leg{i}_swing"] = g["phi_rest"] / g["phi_stand"]
                rest[f"leg{i}_ext"] = g["e_rest"] / 10.0
                rest[f"leg{i}_foot_rest"] = 1
        stand = dict(flight)
        stand.update({"crest_lateral": 1, "crest_dorsal": 1, "pod_retract": 1})
        for i, g in enumerate(legs):
            stand[f"leg{i}_swing"] = 1
            stand[f"leg{i}_ext"] = g["e_stand"] / 10.0
            stand[f"leg{i}_foot_stand"] = 1
        turn_h = 38.0 + 16.0   # trunnion (origin, s = 38) height while turning
        turn = {"lid_port": 1, "lid_starboard": 1, "track_port": mid, "track_starboard": mid, "crest_lateral": 1,
                "pod_retract": 1, "pitch_port": 0.5, "pitch_starboard": 0.5}
        L = turn_h - 1.2
        for s in ("port", "starboard"):
            turn[f"mast_len_{s}"] = (MAST_LMAX - L) / (MAST_LMAX - MAST_SEG)
        hang = {"hangar": 1, "rover_lift": 1}
        hang.update(rest)
        poses = [("flight (stowed)", flight, 0, 0, (15, -60)), ("resting level", rest, 0, 0, (10, -120)),
                 ("turning 45 deg", turn, 45, turn_h - AXIS_H, (8, -80)), ("standing", stand, 90, 12 + 38 - AXIS_H - 0, (8, -60)),
                 ("hangar open", hang, 0, 0, (-15, -60))]
        preview(groups, comps, sys.argv[sys.argv.index("--preview") + 1], poses)
