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
import hashlib
import json
import math
import os
import sys

import numpy as np

STERN_Z = -60.0
INSERT_S, INSERT_L = 88.0, 9.0                 # T9: hull insert (argon charges); everything ahead moves forward
AXIS_H = 24.0                                  # lying on the gear (2026-10-03, «самое безопасное»): folded blade 16.25 + foot 5.2 + 2.5 m of travel both ways
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
        self.uv, self.tex = [], 0                      # optional texture coordinates and the 1-based texture slot (0 = none)

    def vert(self, p, n, uv=None):
        if uv is not None:
            self.uv.append((float(uv[0]), float(uv[1])))
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


def lathe_axis(g, centre, axis, profile, seg=32, inward=False):
    """Surface of revolution about `axis` through `centre`; profile (h along the axis, r)."""
    c = np.asarray(centre, float)
    n = unit(axis)
    ref = np.array([0, 1, 0]) if abs(n[1]) < 0.9 else np.array([1, 0, 0])
    u = unit(np.cross(n, ref))
    v = np.cross(n, u)
    rings = []
    for i, (h, r) in enumerate(profile):
        h0, r0 = profile[max(i - 1, 0)]
        h1, r1 = profile[min(i + 1, len(profile) - 1)]
        th, tr = h1 - h0, r1 - r0
        nr, nh = th, -tr
        if inward:
            nr, nh = -nr, -nh
        ring = []
        for k in range(seg):
            a = 2 * math.pi * k / seg
            e = math.cos(a) * u + math.sin(a) * v
            ring.append(g.vert(c + n * h + e * r, e * nr + n * nh))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(seg):
            k1 = (k + 1) % seg
            a, b, cc, d = rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]
            nrm = g.n[a] + g.n[b] + g.n[cc] + g.n[d]
            g.quad(a, b, cc, d, nrm)


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
    ("canopy", (0.36, 0.37, 0.40), (0.55, 0.55, 0.60, 35), (0, 0, 0)),             # foot canopy: woven CNT, graphite sheen
    ("br_vault", (0.15, 0.16, 0.18), (0.06, 0.06, 0.07, 8), (0.026, 0.030, 0.038)),    # command bridge capsule (watch twilight: cold graphite, the shapes readable)
    ("br_floor", (0.55, 0.58, 0.62), (0.35, 0.36, 0.40, 40), (0.16, 0.18, 0.21)),        # the grating (br_grate.dds): self-lit enough to read in the twilight
    ("br_panel", (0.17, 0.18, 0.20), (0.20, 0.21, 0.24, 20), (0.028, 0.032, 0.040)),
    ("br_panel_face", (0.24, 0.25, 0.28), (0.30, 0.32, 0.36, 30), (0.042, 0.047, 0.056)),   # the instrument wall of the console
    ("br_tub", (0.30, 0.31, 0.34), (0.25, 0.26, 0.30, 25), (0.050, 0.055, 0.066)),          # the console body: cold graphite in the watch twilight
    ("br_holo", (0.05, 0.20, 0.26), (0, 0, 0, 1), (0.06, 0.26, 0.34)),                   # holo panels and role lists: a dim glow
    ("br_screen", (0.02, 0.03, 0.06), (0.30, 0.30, 0.40, 40), (0.03, 0.05, 0.10)),
    ("br_glow", (0.40, 0.90, 1.00), (0, 0, 0, 1), (0.30, 0.70, 0.90)),
    ("br_display", (1.00, 1.00, 1.00), (0, 0, 0, 1), (1.00, 1.00, 1.00)),   # self-lit display: the texture shows at full brightness
    ("br_green", (0.30, 1.00, 0.50), (0, 0, 0, 1), (0.20, 0.90, 0.40)),
    ("br_red", (0.90, 0.20, 0.15), (0.30, 0.10, 0.10, 20), (0.50, 0.08, 0.05)),
    ("br_amber", (1.00, 0.80, 0.30), (0, 0, 0, 1), (0.70, 0.50, 0.10)),
    ("br_seat", (0.20, 0.21, 0.23), (0.30, 0.30, 0.35, 30), (0.024, 0.026, 0.030)),
    ("br_cushion", (0.21, 0.24, 0.29), (0.10, 0.10, 0.12, 10), (0.028, 0.032, 0.040)),
    ("br_bezel", (0.62, 0.64, 0.68), (0.40, 0.41, 0.45, 40), (0.075, 0.080, 0.090)),        # the main screen's frame: light grey (dimmed by the twilight)
    ("br_rib", (0.22, 0.23, 0.26), (0.30, 0.31, 0.35, 30), (0.034, 0.038, 0.046)),          # the vault's ribs
    ("br_lamp", (0.80, 0.88, 1.00), (0, 0, 0, 1), (0.62, 0.70, 0.80)),                     # the vault's lamp panels: cold, soft
    ("br_cove", (0.30, 0.55, 0.80), (0, 0, 0, 1), (0.16, 0.30, 0.46)),                     # the thin light lines along the vault
    ("br_vault_lit", (0.06, 0.06, 0.07), (0.04, 0.04, 0.05, 8), (1.0, 1.0, 1.0)),          # the shell with the baked screen light (br_vault_lit.dds)
    ("br_floor_lit", (0.08, 0.08, 0.09), (0.10, 0.10, 0.12, 20), (1.0, 1.0, 1.0)),         # the grating floor with the baked screen light
    ("br_belt", (0.88, 0.56, 0.16), (0.10, 0.10, 0.10, 10), (0, 0, 0)),
    # interior of the crew zone: no sun reaches the inside, so the surfaces are partly self-lit (emissive) and the lamps fully
    ("in_floor", (0.50, 0.50, 0.50), (0.10, 0.10, 0.10, 10), (0.50, 0.50, 0.52)),                 # textured (deck plates): the emissive keeps it visible, the texture is dark
    ("in_wall", (0.45, 0.45, 0.45), (0.05, 0.05, 0.05, 5), (0.60, 0.60, 0.62)),
    ("in_ceiling", (0.45, 0.45, 0.45), (0, 0, 0, 1), (0.55, 0.55, 0.57)),
    ("in_light", (1.00, 0.97, 0.88), (0, 0, 0, 1), (1.00, 0.96, 0.84)),
    ("in_furn", (0.30, 0.31, 0.34), (0.10, 0.10, 0.10, 10), (0.15, 0.15, 0.17)),
    ("in_seat", (0.12, 0.15, 0.22), (0.15, 0.15, 0.20, 15), (0.06, 0.08, 0.13)),
    ("in_metal", (0.50, 0.50, 0.50), (0.50, 0.50, 0.50, 30), (0.55, 0.55, 0.58)),
    ("in_wet", (0.36, 0.50, 0.55), (0.20, 0.20, 0.25, 20), (0.13, 0.19, 0.21)),
    ("in_screen", (0.04, 0.08, 0.16), (0.30, 0.30, 0.40, 40), (0.10, 0.28, 0.50)),                            # cinema screen
    ("in_trim", (0.60, 0.60, 0.60), (0.10, 0.10, 0.10, 10), (0.50, 0.50, 0.50)),                                # hazard stripes (textured)
    ("in_green", (0.10, 0.60, 0.25), (0, 0, 0, 1), (0.15, 1.00, 0.40)),                                          # status lamps and guide lights
    ("in_amber", (0.70, 0.45, 0.05), (0, 0, 0, 1), (1.00, 0.62, 0.08)),
    ("in_red", (0.70, 0.10, 0.08), (0, 0, 0, 1), (1.00, 0.15, 0.10)),
    ("in_guide", (0.20, 0.50, 0.70), (0, 0, 0, 1), (0.25, 0.75, 1.00)),
    ("in_btn_dim", (0.16, 0.18, 0.20), (0.3, 0.3, 0.3, 20), (0.04, 0.05, 0.06)),                                   # a push button cap, not lit
    # the watch lighting of the way from the bridge to the lift: a dim base (the emissive is the night level), the local lights
    # of the module add the rest; the floor is glossy (the local lights give it highlights)
    ("in_wall_w", (0.50, 0.50, 0.51), (0.08, 0.08, 0.09, 12), (0.065, 0.065, 0.075)),
    ("in_floor_w", (0.50, 0.50, 0.50), (0.45, 0.45, 0.48, 40), (0.04, 0.04, 0.045)),
    ("in_ceil_w", (0.40, 0.40, 0.41), (0, 0, 0, 1), (0.035, 0.035, 0.04)),
    ("in_metal_w", (0.45, 0.45, 0.47), (0.40, 0.40, 0.42, 30), (0.07, 0.07, 0.08)),                               # furniture, frames of the way
    ("in_trim_w", (0.50, 0.50, 0.50), (0.10, 0.10, 0.10, 10), (0.14, 0.14, 0.14)),
    ("in_guide_w", (0.25, 0.42, 0.42), (0, 0, 0, 1), (0.16, 0.36, 0.38)),
    ("in_fx", (1.0, 1.0, 1.0), (0, 0, 0, 1), (1.0, 1.0, 1.0)),                                                    # light decals: the texture is the light
    ("in_downlight", (1.0, 0.86, 0.62), (0, 0, 0, 1), (1.0, 0.84, 0.58)),
    ("in_cove", (0.30, 0.40, 0.62), (0, 0, 0, 1), (0.26, 0.36, 0.58)),
    ("lift_glass", (0.45, 0.60, 0.70, 0.22), (0.80, 0.80, 0.85, 60), (0.02, 0.03, 0.04)),                       # the lift cabin's windows (transparent)
    ("in_wip", (0.95, 0.42, 0.06), (0.30, 0.25, 0.20, 20), (0.40, 0.17, 0.02)),                                   # orange: a lock door of a part still in work
    ("in_btn_cap", (0.55, 0.56, 0.58), (0.35, 0.35, 0.35, 30), (0.30, 0.31, 0.32)),                              # a push button's cap, dark (the legend engraved)
    ("in_cablight", (0.95, 0.93, 0.86), (0, 0, 0, 1), (0.80, 0.77, 0.68)),                                        # the cabin's ceiling light (neutral, not glaring)
    ("in_glass", (1.0, 1.0, 1.0), (0.9, 0.9, 0.9, 60), (0.12, 0.13, 0.15)),                                       # a porthole's glass (alpha from in_glass.dds)
    # the lower deck at night (the user: darkness, a faint red duty light): dark surfaces, a red glow of the lamps
    ("in_wall_r", (0.40, 0.38, 0.38), (0.05, 0.05, 0.05, 5), (0.050, 0.012, 0.011)),
    ("in_floor_r", (0.40, 0.38, 0.38), (0.20, 0.20, 0.20, 30), (0.032, 0.007, 0.007)),
    ("in_ceil_r", (0.35, 0.33, 0.33), (0, 0, 0, 1), (0.028, 0.006, 0.006)),
    ("in_metal_r", (0.40, 0.38, 0.38), (0.30, 0.30, 0.30, 30), (0.045, 0.011, 0.010)),
    ("in_dutyred", (0.60, 0.08, 0.05), (0, 0, 0, 1), (0.55, 0.05, 0.03)),
    ("in_door", (0.55, 0.55, 0.56), (0.25, 0.25, 0.27, 25), (0.075, 0.075, 0.08)),                                # a door leaf's face (in_door.dds)
    # the rooms behind the shut doors: dark (the user: "behind it let it be dark"), barely lit even by the corridor's lights
    ("in_wall_d", (0.08, 0.08, 0.085), (0.02, 0.02, 0.02, 5), (0.018, 0.018, 0.02)),
    ("in_floor_d", (0.08, 0.08, 0.08), (0.05, 0.05, 0.05, 10), (0.012, 0.012, 0.013)),
    ("in_ceil_d", (0.07, 0.07, 0.075), (0, 0, 0, 1), (0.010, 0.010, 0.011)),
    ("in_metal_d", (0.08, 0.08, 0.085), (0.05, 0.05, 0.05, 10), (0.016, 0.016, 0.018)),
    ("br_malachite", (0.16, 0.38, 0.31), (0.40, 0.48, 0.45, 35), (0.020, 0.048, 0.040)),                        # the bridge's desks: malachite-green enamelled metal
    ("br_cur_ball", (0.62, 0.86, 0.95, 0.72), (0.90, 0.90, 0.90, 60), (0.10, 0.26, 0.32)),                      # the cursor unit's ball, dark (glass)
    ("br_cur_lit", (0.78, 0.95, 1.00), (0, 0, 0, 1), (0.78, 0.95, 1.00)),                                     # the ball lit: the «солнечный зайчик» is on
    ("br_spot", (0.90, 0.97, 1.00, 0.24), (0, 0, 0, 1), (0.90, 0.97, 1.00)),                                  # the light spot on the glasses (stacked discs)
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

# Blade legs («лопасти», T9), one per side: trunnion shoe on rails in a flank pocket, telescopic trunnion pin (four
# stages nesting into the hip block), hip block with the pitch bearing (magnetic, 10 T), the blade - a rigid
# telescopic box of three 30 m stages 6.0x2.1 -> 5.6x1.7 -> 5.2x1.3 (CNT composite, walls 0.15; the long side along
# the hull carries the pitch sway, K = 2 cantilever from the foot), extension 28.5 m per stage (87.5 m hip -> ankle),
# superconducting linear motors in the stage collars. Foot: the umbrella R 10 common to all six legs - 16 ribs on one
# knuckle at the outboard side of the hub (so the folded fan lies flush), CNT fabric cone 0.9 m high, rim on the
# ground. Stowed: pitched aft along the hull (hip at STOW_S, blade to s 49), rolled to the flank lean, fan of ribs
# folded flat forward-up over the blade. Pocket: deep part under the blade, shallow part under the fan.
CAR_S0, CAR_S1 = 53.4, 71.6                    # hip (trunnion) track: parked at S0 while lying (tripod) .. CG range
CAR_SREF = CAR_S0                              # legs built with the hip at the track start
STOW_S = 79.0                                  # hip station with the leg folded aft into the pocket
HIP_X_OUT = 19.0
BLADE_N, BLADE_L, BLADE_EXT = 6, 15.75, 14.25
BLADE_W = (6.7, 6.4, 6.1, 5.8, 5.5, 5.2)         # along the hull (hanging: along z), outer -> inner
BLADE_T = (2.8, 2.5, 2.2, 1.9, 1.6, 1.3)         # across (along x); walls 0.15, nested (stepped column 1346 MN)
ANKLE_R = 0.5
HUB_R, HUB_T = 1.2, 0.5                        # umbrella hub (knuckle on its outboard rim)
RIB_N, RIB_L, RIB_W = 16, 8.6, 0.5             # ribs 0.5 x 0.5 box, 60 mm walls (12 MN m with two struts)
FOOT_R = HUB_R + RIB_L * math.cos(math.radians(6.0))      # umbrella radius ~ 9.8
CONE_H = 0.9                                   # hub above the rim (rim on the ground)
FAN_PHI = (math.radians(-10.0), math.radians(66.0))       # folded fan: forward (0) .. up the flank (90)
LEG_LMIN = BLADE_L + ANKLE_R                   # hip -> ankle, stages in
LEG_LMAX = BLADE_L + (BLADE_N - 1) * BLADE_EXT + ANKLE_R
# «Чаша опоры» (2026-10-02): launch mass on a 2.5 g planet, x1.5, loose sand at touchdown with a 0.9 m skirt
# (Terzaghi, local shear) -> R 12.2 blades, 11.25 stern, 6.35 kangaroo; 12 ribs on a hub R 0.5, two struts per rib
# (0.5 L and the tip) from a mast on the hub, CNT canopy, rim skirt. Ribs and struts: Tantra_Design/DESIGN_LOCAL.md.
FOOT_RIBS, FOOT_STRIPS = 12, 3
FOOT_HUB_R, FOOT_HUB_T, FOOT_MAST_R = 0.5, 0.6, 0.6
# Petal feet (2026-10-04, the user's requirement «вариант Б»: real separate petals). Every foot is 12 rigid petals,
# each on its own hinge at the hub ring and its own MR strut from a collar on the solid post under the ankle to the
# petal's keel over its centre of pressure. Each petal rocks on its own and lies on its own ground; the struts are the
# suspension (stroke and static sag as the old ankle struts). Stowing: the petal's three slats fan onto its keel, the
# petals fold down past the ankle and the collar slides down the post keeping the struts at their length.
# rp post radius, rh petal hinge radius (the hinge ring under the post), hp hinge height over the ground, col collar height, sd / rd strut barrel / rod diameter, kd keel depth,
# pt plate thickness.
PETAL = {"blade": dict(rp=0.8, rh=0.8, hp=1.05, col=4.5, sd=1.3, rd=0.6, kd=0.5, pt=0.25),
         "stern": dict(rp=0.7, rh=0.7, hp=0.55, col=4.5, sd=1.0, rd=0.5, kd=0.45, pt=0.25),
         "kang": dict(rp=0.5, rh=0.5, hp=0.4, col=3.0, sd=0.7, rd=0.36, kd=0.35, pt=0.2)}
PETAL_GAP = 0.012                               # rad between petals (0.15 m at the blade rim)
CELL_REF = {}                                   # (foot prefix, petal) -> (rig comp of its working rotation, u, ey)
FOOT_RIM_DROP = 2.0                             # hub centre over the rim: a conical bowl (spudcan), struts steeper
FOOT_LIP = 0.7                                  # rim hoop standing on the canopy edge
FOOT_SKIRT = 0.9
FOOT_RIB_PHASE = math.pi / 12
FOOT_FOLD_A = math.radians(-90.0)
FOOT_BACK_OFS = 1.25                            # kangaroo: swing-back pin off the shin axis (bundle beside the shin)
# The foot post (hub -> ankle ball, `fork`) carries the strut collars BELOW the leg's end: the struts never enter the leg.
FOOT_KINDS = {   # R, rib depth, post, struts (fraction of the rib, collar height over the hub, tube D)
    "blade": dict(R=12.2, depth=0.84, fork=3.2, struts=((0.5, 2.0, 0.54), (1.0, 2.9, 0.69))),
    "stern": dict(R=11.25, depth=0.68, fork=3.2, struts=((0.5, 2.0, 0.44), (1.0, 2.9, 0.58))),
    "kang": dict(R=6.35, depth=0.30, fork=1.5, struts=((0.5, 0.6, 0.24), (1.0, 1.2, 0.29)), retract=0.55, back=True),
}
FOOT_H = FOOT_KINDS["blade"]["fork"] + FOOT_RIM_DROP      # ankle above the ground (blades, stern legs)
KANG_FOOT_H = FOOT_KINDS["kang"]["fork"] + FOOT_RIM_DROP  # kangaroo ankle above the ground
ROLL = math.atan2(N_F[1], N_F[0])
PIN_N, PIN_X0, PIN_STEP, PIN_LEN = 4, 9.75, 1.8, 1.9   # pin stages (deployed: x0 + k*step .. + len); roll pivot at x0
ROLL_P = np.array([PIN_X0, 0.0])
BLADE_DEPTH = 0.55 + BLADE_T[0] / 2            # stowed blade axis under the flank skin (outer face 0.55 deep: ribs over it)
TRAVEL = HIP_X_OUT - PIN_X0 + float(N_F @ (ROLL_P - _C)) + BLADE_DEPTH
HIP_X_IN = HIP_X_OUT - TRAVEL
SLIDE_ROLL = 0.3
HIP_BLOCK = (0.9, 1.3, 2.7, 1.3)               # hip block: inboard, outboard, half height, half length
POCKET_T = float((ROLL_P - _C) @ E_F)          # flank parameter of the stowed blade axis (~3.9)
POCKET_T0, POCKET_T1 = POCKET_T - BLADE_W[0] / 2 - 0.3, POCKET_T + BLADE_W[0] / 2 + 0.3   # deep pocket (blade)
FAN_T1 = min(POCKET_T + RIB_L + HUB_R + 0.3, float((_T - _C) @ E_F) - 0.3)                # shallow pocket (fan) up to here
LAF_S0 = STOW_S - LEG_LMIN - FOOT_KINDS['blade']['fork'] - (FOOT_KINDS['blade']['R'] - FOOT_HUB_R) - 0.9   # the folded foot lies past the ankle
LAF_S1 = STOW_S + HIP_BLOCK[3] + 0.2
LAF_DEPTH = BLADE_DEPTH + BLADE_T[0] / 2 + 0.35
FAN_DEPTH = RIB_W + 0.4
PIN_SLOT = (LAF_S0, LAF_S1)                    # T9: the pocket spans the whole track - no separate pin slot

# Stern legs (T9): on the outer sides of the four nacelles, 30 deg off the horizontal (in the shadow of the body at
# sub-light: inside |x| <= 13.5, under the top line). Hinge at s 30, leg folded aft along the nacelle on heat shields
# (10 ceramic screens). Four telescopic box sections of 15.5 m (outer 3.0 x 2.5, CNT walls 80 mm; 321 MN each at a
# full-mass 2.5 g landing), 1 m overlaps: 16 -> 59.5 m hinge -> ankle. Umbrella foot R 8.9 (bundle folded along the
# leg) rides a rail: stowed beside the hinge end of the leg, run down to the ankle on deployment; it turns to the
# ground in the last quarter of the swing. Standing feet on R 36 m, ground at s -22.5 (marching cup lip 16 m up).
LEG_ANG = math.radians(30.0)                         # lower legs (and every standing foot)
LEG_ANG_UP = math.radians(21.0)                      # upper legs stowed lower: under the top line at sub-light
LEG_S_H = 30.0                                       # hinge on a pylon ahead of the nacelles (splay friction <= 0.55)
LEG_SEC_N, LEG_SEC_L, LEG_SEC_OVL = 4, 15.5, 1.0
LEG_SEC_W, LEG_SEC_T = 3.0, 2.5                      # outer section: across the leg (along the swing axis) x radial
LEG_SEC_K = 0.86                                     # size ratio per section
LEG_STEP = LEG_SEC_L - LEG_SEC_OVL
LEG_LMIN_S = LEG_SEC_L + ANKLE_R
LEG_EXT_MAX = (LEG_SEC_N - 1) * LEG_STEP
LEG_STANDOFF = 1.7                                   # heat-shield stack between the nacelle and the leg
LEG_FOOT_H = FOOT_H                                  # hub centre above the ground
LEG_RIB_L = 7.6                                      # umbrella R 8.9
LEG_RAIL = 0.0                                       # T9 cup feet: folded past the ankle, no rail
STAND_R, STAND_GROUND_S = 38.5, -22.5
LEG_EXT_DELAY = 0.6
# Leg systems (core/Legs.h): the ankle is a gas-hydraulic strut with an MR valve (stroke 1.5 m blades / 1.0 m stern,
# 30 % taken by the weight: the mesh is built at that static sag, unloaded the rod shows STRUT_EXT more); the umbrella
# rim is the ground face; catcher rings on the magnetic-bearing drums.
STRUT_EXT_C, STRUT_EXT_S = 0.45, 0.3
# the whole stroke (core/Legs): the animation runs from bottomed out (0) through the static sag (mesh pose) to unloaded (1)
STROKE_C, STROKE_S = 1.5, 1.0
STRUT_D_C, STRUT_D_S = (STROKE_C - STRUT_EXT_C) / STROKE_C, (STROKE_S - STRUT_EXT_S) / STROKE_S

# Kangaroo leg (T9, the third support while lying and lifting): hip in a belly pocket on the insert just aft of the
# hangar doors; thigh 11 m (2.2 x 2.2, CNT 80 mm), fork knee with a lateral offset so the folded shin lies beside the
# thigh, telescopic shin of six 11.5 m sections (1.4 x 1.4, 50 mm; 11.5 -> 64 m), knee brace (the knee actuator and
# MR damper, 59 MN), ball-joint ankle with the umbrella foot R 6 (bundle fold). Lying: foot 12 m ahead of the hip,
# knee bent aft with the knee <= 5 m off the hip-foot line; carries 8.5 % of the weight with the trunnions parked.
KANG_S0, KANG_S1 = 88.2 + 0.1, 90.6 + INSERT_L + 0.7      # belly pocket s 88.3 .. 100.3 (between the port doors and the hangar doors)
KANG_HALF_X, KANG_DEPTH = 4.3, 2.8
KANG_HIP_S = 99.4                                             # hip axis station (the pocket runs 0.9 m further forward)
KANG_THIGH_L, KANG_THIGH_W = 10.0, 2.2
KANG_SEC_N, KANG_SEC_L, KANG_SEC_OVL, KANG_SEC_W = 9, 8.3, 1.0, 2.2   # stepped column 175 MN (103 MN at 2.5 g x1.5)
KANG_SEC_STEP = 0.14                                          # each section 0.14 smaller (walls 0.06)
KANG_STEP = KANG_SEC_L - KANG_SEC_OVL
KANG_SHIN_MIN, KANG_SHIN_MAX = KANG_SEC_L, KANG_SEC_L + (KANG_SEC_N - 1) * KANG_STEP
KANG_KNEE_DX = (KANG_THIGH_W + KANG_SEC_W) / 2 + 0.1          # shin axis beside the thigh axis
KANG_THIGH_X, KANG_SHIN_X = -0.9, -0.9 + (KANG_THIGH_W + KANG_SEC_W) / 2 + 0.1
KANG_FOOT_X = KANG_SHIN_X + KANG_SEC_W / 2 + 1.5              # folded foot bundle beside the shin
KANG_RIB_L = 4.8                                              # umbrella R 6
KANG_HIP_MAX = math.radians(100.0)                            # thigh from along the belly (aft) to 10 deg ahead of vertical
KANG_KNEE_E = 5.0
KANG_FOOT_FWD = 12.0
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

# Planetary pods (T9): 4 pods x 3 cups R 0.8 (350 MN each) in flank bays at CG height (y 2.0): aft pair s 43.6, fore
# pair s 84. A telescopic arm along the flank normal carries the pod out to x +-17.5; the bay cover rides on the arm's
# end. The pod turns about the arm axis: 0 = cups aft (thrust forward), 90 = cups down (hover), 180 = cups forward
# (retro). The jets splay 15 deg out / 25 deg down (code: thruster directions), clear of wings and hull by >= 2 radii.
POD_L, POD_W, POD_T = 6.3, 5.2, 1.9                 # along z, along the flank, deep
POD_Y = 2.0                                        # pod centre height (CG line)
POD_X_OUT = 17.5                                   # pod centre deployed
POD_S = (43.6, 84.0)
POD_CUP_R = 0.8
POD_DOOR_L = 6.5
POD_SWIVEL_MAX = math.radians(180.0)

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
NOSE_DOSE_S0, NOSE_DOSE_S1, NOSE_DOSE_R = 156.5, 162.6, 1.3      # nose dosing trap behind each retro cup
# anamezon feed ducts (starboard; port mirrored, detour round the airlock): header just ahead of the traps, then over
# the hangar along the flank, behind the decks, under the central post floor to the dosing trap
FEED_R, FEED_X, FEED_HEAD_S = 0.10, 9.25, 98.0       # anamezon is a magnetically held stream, not fluid: a thin core in an insulating
FEED_SLEEVE_R, FEED_COIL_R, FEED_COIL_STEP = 0.26, 0.40, 2.5     # vacuum jacket and confinement solenoid rings
BUF_S0, BUF_S1, BUF_R = 88.6, 94.6, 1.35                         # buffer bottle in front of each trap: the cassette plugs into it
SCREEN_S0, SCREEN_S1 = 144.0, 152.0                              # 8 m screen (iridium plates, water/charge core) between the crew and the nose motors
MIRROR_S = (154.8, 155.6)                                        # magnetic mirror rings (particle reflection) round each nose axis
MIRROR_R = 2.5
FEED_PATH = [(9.25, 5.0, 98.0), (9.25, 5.0, 119.0), (8.4, 6.1, 121.0), (5.6, 6.1, 123.0), (5.6, 6.1, 134.8), (5.6, 4.5, 135.8),
             (5.6, 4.5, 144.3), (5.6, -1.0, 144.3), (3.0, -1.0, 150.0), (3.0, -1.0, 156.5)]       # over the upper deck, round the bridge drum
FEED_PATH_PORT = FEED_PATH

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
# Main airlock lift (port flank, s 129): the lock cell x -8.85..-6.85, y 1.0..3.1. Door panel swings out; a four-stage telescopic
# arm carries the platform OUT 4.4 m (the hull bulges to x 11 below the door, the platform must clear it), a mast comes down to
# the ground, the platform rides down the mast. Built stowed in the cell.
LOCK_OUT = 4.4                                   # arm travel out of the cell
LOCK_DROP = 1.0 + AXIS_H                         # descent of the cabin floor (y 1.0) to the ground lying on the gear
LOCK_MAST_STUB = 0.5                           # crew-lift mast as stowed (stretched to LOCK_MAST_FULL at state 1)
# closed lift cabin (2026-10-03, user: «закрытая кабина на несколько человек», bigger): 1.8 x 3.0 x 2.6 m outside,
# 1.6 x 2.8 x 2.4 inside (6-8 people in suits); the cell and the hull door grow to y 3.9 and s +-1.7 for it
LOCK_TOP = 3.9                                   # cell and hull door top (was 3.1)
CAB_Y0, CAB_Y1, CAB_HZ = 1.0, 3.6, 1.5          # cabin floor, roof, half length along the hull
LOCK_ARM_Y = 3.75                               # telescopic arm on the cabin roof
LOCK_X0, LOCK_X1 = -8.75, -6.95                  # cabin in the cell (stowed)
LOCK_MAST_X, LOCK_MAST_TOP = LOCK_X0 - 0.25, 3.6   # guide mast outboard of the cabin, pivot under the arm head
LOCK_MAST_FULL = LOCK_MAST_TOP + AXIS_H          # pivot -> ground lying on the gear
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


def _foot_solve_slider(Q, d, n, prof, C, Ls, L):
    """Slider on the rib top: s in [0, L] with |Q + d s + n h(s) - C| = Ls (None if the strut cannot reach)."""
    f = lambda s: float(np.linalg.norm(Q + d * s + n * prof(s) - C)) - Ls
    lo, hi = 0.0, L
    if f(lo) * f(hi) > 0:
        return None
    for _ in range(60):
        mid = (lo + hi) / 2
        if f(lo) * f(mid) <= 0:
            hi = mid
        else:
            lo = mid
    return (lo + hi) / 2


class FootFrame:
    """«Чаша опоры» in its frame: A = ankle (fork pin), ey up the leg, ex / ez across; kind -> FOOT_KINDS.
    Open (state 0): hub `fork` below the ankle (the post), 12 ribs round its rim, canopy on their lower faces, the rim on the
    ground (A FOOT_H above it), the skirt below. Folded: ribs down past the ankle (bundle); the kangaroo foot then
    lowers its mast and swings back 180 deg beside the shin (FOOT_BACK_OFS to +ez)."""

    def __init__(self, A, ex, ey, kind):
        self.A, self.ex, self.ey = np.asarray(A, float), unit(ex), unit(ey)
        self.ez = np.cross(self.ex, self.ey)
        self.kind = kind
        K = FOOT_KINDS[kind]
        self.R, self.depth, self.struts, self.retract, self.back = K["R"], K["depth"], K["struts"], K.get("retract"), K.get("back", False)
        self.fork = K['fork']
        self.H = self.A - self.ey * self.fork                               # hub centre
        self.L = (self.R - FOOT_HUB_R) / math.cos(math.asin(FOOT_RIM_DROP / (self.R - FOOT_HUB_R)))
        self.kap = math.asin(FOOT_RIM_DROP / self.L)                         # canopy cone

    def w(self, v):
        return self.ex * v[0] + self.ey * v[1] + self.ez * v[2]

    def az(self, a):
        """Radial and tangential unit vectors at azimuth a (from ex towards ez)."""
        return self.ex * math.cos(a) + self.ez * math.sin(a), -self.ex * math.sin(a) + self.ez * math.cos(a)

    def rib(self, i, alpha=None):
        """Hinge Q, direction d, normal n, tangential axis t of rib i at the rib angle alpha (open: -kap)."""
        a = 2 * math.pi * i / FOOT_RIBS + FOOT_RIB_PHASE
        u, t = self.az(a)
        al = -self.kap if alpha is None else alpha
        d = u * math.cos(al) + self.ey * math.sin(al)
        n = -u * math.sin(al) + self.ey * math.cos(al)
        return self.H + u * FOOT_HUB_R, d, n, t, a

    def prof(self, s):
        """Rib depth over its lower face at s from the hinge."""
        L, D = self.L, self.depth
        xs, hs = (0.0, 0.5 * L, 0.92 * L, L), (0.35, D, D, 0.6 * D)
        return float(np.interp(s, xs, hs))

    def collar(self, i, k, lower=0.0):
        """Strut k pivot on the mast for rib i (the mast lowered by `lower`)."""
        u, _ = self.az(2 * math.pi * i / FOOT_RIBS + FOOT_RIB_PHASE)
        return self.H + self.ey * (self.struts[k][1] - lower) + u * (FOOT_MAST_R + 0.3)

    def strut_len(self, i, k):
        Q, d, n, t, a = self.rib(i)
        s = self.struts[k][0] * self.L * 0.999
        return float(np.linalg.norm(Q + d * s + n * self.prof(s) - self.collar(i, k)))

    def strut_dir(self, i, k, alpha, lower=0.0):
        Q, d, n, t, a = self.rib(i, alpha)
        C = self.collar(i, k, lower)
        s = _foot_solve_slider(Q, d, n, self.prof, C, self.strut_len(i, k), self.L)
        assert s is not None, (self.kind, i, k, alpha, lower)
        return unit(Q + d * s + n * self.prof(s) - C), s


def foot_groups(pre):
    """Group names of one foot: the post with the hub ring, the collar, per petal its keel, three slats, strut, rod."""
    out = [f"{pre}_hub", f"{pre}_mast"]
    for i in range(FOOT_RIBS):
        out += [f"{pre}_petal_{i}"] + [f"{pre}_slat_{i}_{m}" for m in range(FOOT_STRIPS)] + [f"{pre}_strut_{i}", f"{pre}_rod_{i}"]
        out += [f"{pre}_skirt_{i}_{m}" for m in range(FOOT_STRIPS)]
    return out


def petal_groups(pre, i):
    return [f"{pre}_petal_{i}"] + [f"{pre}_slat_{i}_{m}" for m in range(FOOT_STRIPS)] + [f"{pre}_strut_{i}", f"{pre}_rod_{i}"] + \
        [f"{pre}_skirt_{i}_{m}" for m in range(FOOT_STRIPS)]


def foot_ribs(pre):
    return [f"{pre}_petal_{i}" for i in range(FOOT_RIBS)]


def _sheet(g, pts_a, pts_b, nrm):
    """Two-sided strip between two polylines (the canopy is thin: both faces drawn)."""
    ia = [g.vert(p, nrm) for p in pts_a]
    ib = [g.vert(p, nrm) for p in pts_b]
    ja = [g.vert(p, -nrm) for p in pts_a]
    jb = [g.vert(p, -nrm) for p in pts_b]
    for k in range(len(pts_a) - 1):
        g.quad(ia[k], ia[k + 1], ib[k + 1], ib[k], nrm)
        g.quad(ja[k], ja[k + 1], jb[k + 1], jb[k], -nrm)


class PetalGeo:
    """Where the parts of a petal foot are (frame F, open pose): heights over the ground along F.ey, radii from its axis."""

    def __init__(self, F):
        self.F = F
        self.k = PETAL[F.kind]
        self.FH = KANG_FOOT_H if F.kind == "kang" else FOOT_H
        self.G = F.A - F.ey * self.FH                       # the ground under the axis
        k = self.k
        self.rph = k["rh"]                                 # petal hinge radius
        self.rc = 2.0 / 3.0 * (F.R ** 3 - self.rph ** 3) / (F.R ** 2 - self.rph ** 2)   # centre of pressure of a petal
        self.rcol = k["rp"] + 0.45                         # strut pins on the collar
        self.stroke, self.D = (STROKE_C, STRUT_D_C) if F.kind == "blade" else (STROKE_S, STRUT_D_S)

    def p(self, r, a, h):
        return self.G + self.F.az(a)[0] * r + self.F.ey * h

    def hb(self, r):
        """Plate underside: on the ground at the rim, 0.2 under the hinge at the hub."""
        return (self.k["hp"] - 0.2) * (self.F.R - r) / (self.F.R - self.rph)

    def centre(self, i):
        return 2 * math.pi * i / FOOT_RIBS + FOOT_RIB_PHASE + math.pi / FOOT_RIBS

    def hinge(self, i):
        return self.p(self.rph, self.centre(i), self.k["hp"])

    def pin(self, i):
        return self.p(self.rc, self.centre(i), self.hb(self.rc) + self.k["pt"] + 0.5 * self.k["kd"])   # through the keel

    def collar(self, i):
        return self.p(self.rcol, self.centre(i), self.k["col"])

    def tang(self, i):
        return self.F.az(self.centre(i))[1]

    def outward(self, i):
        """Along the petal from its hinge to the rim (open)."""
        a = self.centre(i)
        return unit(self.p(self.F.R, a, self.hb(self.F.R)) - self.p(self.rph, a, self.hb(self.rph)))


def cup_foot(grp, pre, F, mat_rib="mechanism", mat_fab="canopy"):
    """The petal foot, open, in frame F (groups: foot_groups(pre))."""
    P = PetalGeo(F)
    k, ey, R = P.k, F.ey, F.R
    g = grp(f"{pre}_hub", mat_rib)                                       # solid post, hub ring with the hinges, ball cup
    lathe_axis(g, P.G, ey, [(k["hp"] - 0.45, 0.0), (k["hp"] - 0.45, P.rph + 0.25), (k["hp"] + 0.35, P.rph + 0.25),
                            (k["hp"] + 0.5, k["rp"]), (P.FH - 0.6, k["rp"]), (P.FH - 0.3, k["rp"] + 0.25), (P.FH + 0.45, 0.0)], seg=24)
    g = grp(f"{pre}_mast", mat_rib)                                      # the collar carrying the strut pins
    tube(g, P.G + ey * (k["col"] - 0.35), P.G + ey * (k["col"] + 0.35), P.rcol + 0.3, n=24)
    for i in range(FOOT_RIBS):
        C, t = P.collar(i), P.tang(i)
        tube(g, C - t * 0.4, C + t * 0.4, 0.22, n=8)
    for i in range(FOOT_RIBS):
        a0 = 2 * math.pi * i / FOOT_RIBS + FOOT_RIB_PHASE
        ac, t, o = P.centre(i), P.tang(i), P.outward(i)
        n = unit(np.cross(o, t)) if float(np.cross(o, t) @ ey) > 0 else unit(np.cross(t, o))
        # the keel: a box along the petal on its plate, the hinge lug at the hub
        g = grp(f"{pre}_petal_{i}", mat_rib)
        r0, r1 = P.rph + 0.2, R - 0.4
        mid = (P.p(r0, ac, P.hb(r0)) + P.p(r1, ac, P.hb(r1))) / 2 + n * (k["pt"] + k["kd"] / 2)
        obox(g, mid, o, t, n, (r1 - r0) / 2 / float(o @ F.az(ac)[0]), 0.35 if F.kind != "kang" else 0.25, k["kd"] / 2)
        tube(g, P.hinge(i) - t * 0.45, P.hinge(i) + t * 0.45, 0.3, n=10)
        # the three slats: rigid plates, the rim face, the skirt below and the lip above the rim
        step = 2 * math.pi / FOOT_RIBS / FOOT_STRIPS
        for m in range(FOOT_STRIPS):
            b0 = a0 + m * step + (PETAL_GAP if m == 0 else 0.002)
            b1 = a0 + (m + 1) * step - (PETAL_GAP if m == FOOT_STRIPS - 1 else 0.002)
            gs = grp(f"{pre}_slat_{i}_{m}", mat_rib)
            angs = np.linspace(b0, b1, 4)
            rin = P.rph + 0.35
            bot_i = [P.p(rin, x, P.hb(rin)) for x in angs]
            bot_o = [P.p(R, x, P.hb(R)) for x in angs]
            top_i = [q + ey * k["pt"] for q in bot_i]
            top_o = [q + ey * k["pt"] for q in bot_o]
            u_mid = F.az((b0 + b1) / 2)[0]
            _sheet(gs, top_i, top_o, n)
            _sheet(gs, bot_i, bot_o, -n)
            _sheet(gs, bot_o, top_o, u_mid)
            _sheet(gs, bot_i, top_i, -u_mid)
            _sheet(grp(f"{pre}_skirt_{i}_{m}", mat_rib), [q - ey * FOOT_SKIRT for q in bot_o], bot_o, u_mid)   # into loose soil
            lip = [q + ey * FOOT_LIP for q in top_o]
            _sheet(gs, top_o, lip, u_mid)
            _sheet(gs, lip, [P.p(R - 0.35, x, P.hb(R - 0.35) + k["pt"] + FOOT_LIP) for x in angs], ey)
            for b in (b0, b1):                                            # the petal's side faces
                if (m == 0 and b == b0) or (m == FOOT_STRIPS - 1 and b == b1):
                    side = [P.p(rr, b, P.hb(rr)) for rr in (rin, R)]
                    _sheet(gs, side, [q + ey * k["pt"] for q in side], F.az(b)[1])
        # the strut: barrel from the collar, rod into it from the keel's pin
        C, Pn = P.collar(i), P.pin(i)
        d = unit(Pn - C)
        Ls = float(np.linalg.norm(Pn - C))
        Lb = 0.55 * Ls
        tube(grp(f"{pre}_strut_{i}", "band"), C, C + d * Lb, k["sd"] / 2, n=12)
        tube(grp(f"{pre}_rod_{i}", mat_rib), C + d * (Lb - 0.6), Pn, k["rd"] / 2, n=10)


def _chain_rot(add, anim, group, pivot, dirs, ranges, parent, axis_hint):
    """A part turned about a fixed pivot through the directions dirs[0] -> dirs[1] -> ... over the state ranges: one
    rotation component per step, chained (each child of the previous), axes given in the reference (open) pose."""
    Rcum = np.eye(3)
    comp = parent
    n = len(dirs) - 1
    for p in range(n):
        a, b = dirs[p], dirs[p + 1]
        ang = math.acos(max(-1.0, min(1.0, float(a @ b))))
        ax = np.cross(a, b)
        ax = unit(ax) if np.linalg.norm(ax) > 1e-9 else axis_hint
        ax_ref = Rcum.T @ ax
        comp = add(anim, "rot", [group] if p == n - 1 and group else [], (pivot, ax_ref, ang), parent=comp, s0=ranges[p][0], s1=ranges[p][1])
        Rcum = rot(ax, ang) @ Rcum
    return comp


def cup_rig(add, anim, pre, F, parent):
    """Fold state 0 open .. 1 stowed: the slats fan onto their keels (0-0.3), the petals fold down past the ankle while
    the collar slides down the post with the struts at their length (0.3-0.8); the kangaroo foot then swings back beside
    the shin (0.88-1). Working: each petal turns on its hinge by its own strut's stroke (anim <pre>_petal_<i>, 0 bottomed
    .. 1 unloaded, the mesh at the static sag) - the strut turns on its collar pin, the rod runs in or out."""
    P = PetalGeo(F)
    ey = F.ey
    if F.back:
        piv = F.A + F.ez * FOOT_BACK_OFS
        ax = F.ex
        test = rot(ax, math.pi) @ (F.A - F.ey * 3.0 - piv) + piv
        if float((test - F.A) @ F.ez) < 0:
            ax = -ax
        root = add(anim, "rot", [], (piv, ax, math.pi), parent=parent, s0=0.88, s1=1.0)
    else:
        root = parent
    add(anim, "tr", [f"{pre}_hub"], np.zeros(3), parent=root)
    s0f, s1f, K = 0.3, 0.8, 8
    ranges = [(s0f + (s1f - s0f) * q / K, s0f + (s1f - s0f) * (q + 1) / K) for q in range(K)]
    # the fold of each petal (one angle for all): from its open slope to straight down past the ankle
    phi = math.pi / 2 - math.atan2(P.k["hp"] - 0.2, F.R - P.rph)
    fold_ax, pins = {}, {}
    for i in range(FOOT_RIBS):
        o, t = P.outward(i), P.tang(i)
        fold_ax[i] = t if float((rot(t, 0.1) @ o) @ ey) < float(o @ ey) else -t
        pins[i] = [rot(fold_ax[i], phi * q / K) @ (P.pin(i) - P.hinge(i)) + P.hinge(i) for q in range(K + 1)]
    # the collar height that keeps every strut at its length at each step (all petals alike: one collar)
    Ls = float(np.linalg.norm(P.pin(0) - P.collar(0)))
    hs = []
    for q in range(K + 1):
        v = pins[0][q] - (P.G + F.az(P.centre(0))[0] * P.rcol)
        vy = float(v @ ey)
        vp = float(np.linalg.norm(v - ey * vy))
        hs.append(min(P.k["col"], vy + math.sqrt(max(0.0, Ls * Ls - vp * vp))))
    mast = root
    for q in range(K):
        mast = add(anim, "tr", [f"{pre}_mast"] if q == K - 1 else [], -ey * (hs[q] - hs[q + 1]), parent=mast, s0=ranges[q][0], s1=ranges[q][1])
    for i in range(FOOT_RIBS):
        t, H = P.tang(i), P.hinge(i)
        # petal: fold, then its working turn, then the slats fanning onto the keel
        pf = add(anim, "rot", [], (H, fold_ax[i], phi), parent=root, s0=s0f, s1=s1f)
        work_ax = fold_ax[i]                                            # the same sense lowers the rim
        aw = P.stroke / (P.rc - P.rph)
        pw = add(f"{pre}_petal_{i}", "rot", [f"{pre}_petal_{i}"], (H, work_ax, aw), parent=pf, d=P.D)
        CELL_REF[(pre, i)] = (pw, F.az(P.centre(i))[0], ey)
        o = P.outward(i)
        n = unit(np.cross(o, t)) if float(np.cross(o, t) @ ey) > 0 else unit(np.cross(t, o))
        step = 2 * math.pi / FOOT_RIBS / FOOT_STRIPS
        for m in range(FOOT_STRIPS):
            off = (m - (FOOT_STRIPS - 1) / 2) * step                     # slat centre from the keel
            if abs(off) < 1e-9:
                sl = add(anim, "tr", [f"{pre}_slat_{i}_{m}"], np.zeros(3), parent=pw)
            else:
                uc = F.az(P.centre(i) + off)[0]
                ua = F.az(P.centre(i))[0]
                ax2 = n if float((rot(n, 0.05) @ uc) @ ua) > float(uc @ ua) else -n
                sl = add(anim, "rot", [f"{pre}_slat_{i}_{m}"], (H, ax2, abs(off)), parent=pw, s0=0.0, s1=0.3)
            add(anim, "tr", [f"{pre}_skirt_{i}_{m}"], np.zeros(3), parent=sl)                   # the skirt rides its slat
        # strut: turns on its collar pin to follow the folding petal (the collar sliding), then its working turn
        C0 = P.collar(i)
        dirs = []
        for q in range(K + 1):
            Cq = C0 - ey * (P.k["col"] - hs[q])
            dirs.append(unit(pins[i][q] - Cq))
        chain = _chain_rot(add, anim, None, C0, dirs, ranges, mast, t)
        # working: the pin at bottomed (0) and unloaded (1) -> the strut's turn and the rod's run
        def pin_at(st):
            return rot(work_ax, aw * (st - P.D)) @ (P.pin(i) - H) + H
        d0, d1 = unit(pin_at(0.0) - C0), unit(pin_at(1.0) - C0)
        axb = np.cross(d0, d1)
        axb = unit(axb) if np.linalg.norm(axb) > 1e-12 else t
        ang_b = math.acos(max(-1.0, min(1.0, float(d0 @ d1))))
        sb = add(f"{pre}_petal_{i}", "rot", [f"{pre}_strut_{i}"], (C0, axb, ang_b), parent=chain, d=P.D)
        dL = float(np.linalg.norm(pin_at(1.0) - C0) - np.linalg.norm(pin_at(0.0) - C0))
        add(f"{pre}_petal_{i}", "tr", [f"{pre}_rod_{i}"], unit(P.pin(i) - C0) * dL, parent=sb, d=P.D)


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
    """Flank pods: bay (u-range on the fairing flank, s-range), stowed centre, arm axis (outward normal), travel."""
    out = []
    for s_c in POD_S:
        K, S = key_wh(*wh_at(s_c)), sec(s_c)                      # the pod's own section
        eF = unit(K["T"] - K["C"])
        nF = np.array([eF[1], -eF[0]])
        fp = lambda t: K["C"] + t * eF
        t_c = float((POD_Y - K["C"][1]) / eF[1])                   # flank parameter of the pod centre line
        for sgn in (-1, 1):
            u0, u1 = S.u_of(fp(t_c - POD_W / 2 - 0.15)), S.u_of(fp(t_c + POD_W / 2 + 0.15))
            if sgn < 0:
                u0, u1 = mir(u1), mir(u0)
            m = np.array([sgn, 1.0])
            n3 = np.array([*(nF * m), 0.0])
            e3 = np.array([*(eF * m), 0.0])
            c2 = fp(t_c) - nF * (POD_T / 2 + 0.12)                 # pod flush under the skin
            centre = np.array([*(c2 * m), zs(s_c)])
            travel = (POD_X_OUT - abs(centre[0])) / nF[0]
            cups = [centre + np.array([0, 0, -POD_L / 2]) + e3 * dy for dy in (-1.7, 0.0, 1.7)]
            out.append(dict(s=s_c, sgn=sgn, u=(u0, u1), n=n3, e=e3, centre=centre, travel=travel, cups=cups,
                            s0=s_c - POD_DOOR_L / 2, s1=s_c + POD_DOOR_L / 2))
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
        H2 = P2 + (LEG_STANDOFF + LEG_SEC_T / 2) * n2                         # leg axis
        n3 = np.array([*n2, 0.0])
        e3 = np.array([-n2[1], n2[0], 0.0]) * sx * sy                          # across the leg (tangential)
        H = np.array([*H2, zs(LEG_S_H)])
        # standing foot: on the leg's radial line from the stern axis, at R, behind the stern
        # the foot on the radial line through the hinge: the leg swings in the plane of the stern axis, its thrust on
        # the foot is radial (was 30 deg for every foot - the planes were skewed 2.8 / 7.4 deg off the hinges)
        F2 = np.array([0.0, YC]) + STAND_R * unit(H2 - np.array([0.0, YC]))
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
        # foot: its axis (up the leg, +z stowed) must stand vertical (+z, ship on its stern) after the swing
        Rs = rot(axis, phi_stand)
        want = Rs.T @ np.array([0, 0, 1.0])
        fs_ax = np.cross(np.array([0, 0, 1.0]), want)
        fs_ang = math.acos(max(-1.0, min(1.0, float(want[2]))))
        fs_ax = unit(fs_ax) if np.linalg.norm(fs_ax) > 1e-9 else axis
        fr_ax, fr_ang = np.array([1.0, 0, 0]), 0.0
        assert 0.0 <= e_stand <= LEG_EXT_MAX, (name, e_stand, LEG_EXT_MAX)
        out.append(dict(name=name, H=H, axis=axis, phi_stand=phi_stand, e_stand=e_stand, phi_rest=phi_rest, e_rest=e_rest,
                        phi_max=max(phi_stand, phi_rest), lower=lower, rad=fd, n=n3, e=e3,
                        fs_ax=fs_ax, fs_ang=fs_ang, fr_ax=fr_ax, fr_ang=fr_ang, nac=ni,
                        s0=LEG_S_H - LEG_LMIN_S - 0.5, s1=LEG_S_H + 1.0))
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
    lafet = _open("carriage_starboard", _SA.u_of(flank_pt(POCKET_T0)), _SA.u_of(flank_pt(POCKET_T1)), LAF_S0, LAF_S1, depth=LAF_DEPTH)
    yb = -FL * wh_at(S_FAIR)[1]
    bay = _open("bay_starboard", _SA.u_of((BAY_X0, yb)), _SA.u_of((BAY_X1, yb + 0.15)), BAY_S0, BAY_S1)
    for o, n in ((lafet, "carriage_port"), (bay, "bay_port")):
        O += [o, _mirror(o, n)]
    for i, p in enumerate(PODS):
        O.append(_open(f"pod_{i}", p["u"][0], p["u"][1], p["s0"], p["s1"], depth=POD_T + 0.6))
    S30 = sec(30.0)
    us = S30.u_of((SLOT_HALF, top_y(30.0)))
    O.append(_open("fin_slot", us, mir(us), 3.0, 48.5, depth=3.0))
    SF = sec(HANGAR_REF_S)
    W, H = wh_at(HANGAR_REF_S)
    K = key_wh(W, H)
    uh = SF.u_of((0.62 * W, (1 - FL) * H - 0.12 * H))
    O.append(_open("hangar_top", uh, mir(uh), HANGAR_S[0] + 0.5, HANGAR_S[1] - 0.5, depth=3.0))
    ub = SF.u_of(K["C"])                                   # bottom doors run up to the chine: the XR2 span is 23.9 m
    O.append(_open("hangar_bottom_starboard", 0.0, ub, HANGAR_S[0] + 0.9, HANGAR_S[1] - 0.5, depth=5.0))   # aft edge clear of the kangaroo pocket
    O.append(_open("hangar_bottom_port", mir(ub), 1.0, HANGAR_S[0] + 0.9, HANGAR_S[1] - 0.5, depth=5.0))
    SA = sec(AIRLOCK_S)
    KA = key_wh(*wh_at(AIRLOCK_S))
    fe = unit(KA["T"] - KA["C"])
    fp = lambda y: KA["C"] + fe * (y - KA["C"][1]) / fe[1]
    O.append(_open("airlock_door", mir(SA.u_of(fp(LOCK_TOP))), mir(SA.u_of(fp(1.0))), AIRLOCK_S - CAB_HZ - 0.2, AIRLOCK_S + CAB_HZ + 0.2))
    SK = sec((KANG_S0 + KANG_S1) / 2)
    ybk = -FL * wh_at((KANG_S0 + KANG_S1) / 2)[1]
    uk = SK.u_of((KANG_HALF_X, ybk))
    O.append(_open("kang_pocket_starboard", 0.0, uk, KANG_S0, KANG_S1, depth=KANG_DEPTH))
    O.append(_open("kang_pocket_port", mir(uk), 1.0, KANG_S0, KANG_S1, depth=KANG_DEPTH))
    u0, u1 = nose_u_range(NOSE_CUP_S + 3.0, NOSE_CUP_X, NOSE_CUP_Y, NOSE_CUP_R + 0.4)
    nose_cup = _open("nose_cup_starboard", u0, u1, NOSE_CUP_S, NOSE_CUP_S1, depth=3.0)
    O += [nose_cup, _mirror(nose_cup, "nose_cup_port")]
    return O


OPENINGS = openings()
OPEN = {o["name"]: o for o in OPENINGS}


def overlays():
    """Flush dark patches (seals, hatches): name, u0, u1, s0, s1."""
    return []


# ---------------------------------------------------------------------------
# Surface builder: strips between u-grid lines, zipped between station lists, holes cut exactly


def _stations():
    st = list(np.arange(0.0, CLOVER_S1, 0.5)) + list(np.arange(CLOVER_S1, NB, 1.0)) + [NB]
    st += list(np.arange(NB, TIP_S - 2.0, 0.75)) + [TIP_S - 2.0 * (1 - k / 12) for k in range(12)] + [TIP_S - 0.03]
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
# Interiors (T9): compartments with real volume inside the hull, for the future virtual cockpit and walking between
# decks. name, s0, s1, y0, y1, half width, camera point (x, y, s). Floors/bulkheads go to TantraVC.msh (interior
# skeleton, no exterior); the exterior mesh stays closed.


# Command bridge = a capsule (drum) turning about the cross axis x inside the hull: it counter-rotates with the erection and
# stays level. Ship positions: lying 0 deg, "stella" 70-75 deg, standing 90 deg. Entered through an axial hatch on the
# starboard end disc from an access corridor; the room is a box inside the drum.
BR_S, BR_Y = 139.0, 2.0                 # axis station and height
BR_R, BR_HX, BR_HATCH_R = 4.6, 4.8, 0.8  # drum radius, half length along x, axial hatch radius
BR_ROOM_HX, BR_ROOM_HS, BR_FLOOR, BR_CEIL = 4.2, 3.6, -1.0, 1.7   # room half sizes, floor/ceiling relative to the axis


def compartments():
    """Two decks in the crew zone (lower y -4.5..1.0: lab/library and anabiosis; upper y 1.0..5.5: living deck and bridge),
    the hangar lock into the hangar, the space airlock on the port side, and a 1.1 m technical passage along the
    roof slot between the trap columns. The stern (s 0-20, engines) is unmanned."""
    C = [
        ("tech_passage", "технический проход", 20.0, 119.0, 5.5, 7.5, 0.55, 0.0, (0.0, 6.5, 60.0)),
        ("tech_link", "переход в проход", 119.0, 125.0, 5.5, 7.5, 2.6, -0.2, (0.0, 6.5, 122.0)),
        ("hangar", "ангар", HANGAR_S[0] + 0.5, 120.3, -5.6, 3.3, 12.0, 0.0, (0.0, 1.0, (HANGAR_S[0] + HANGAR_S[1]) / 2)),
        ("hangar_lock", "тамбур ангара", 120.3, 121.6, -4.5, -2.4, 0.9, -0.45, (-0.45, -3.0, 120.9)),
        ("lab_deck", "нижняя палуба: каюты, спортзал, санузлы", 121.6, 134.0, -4.5, 1.0, 8.4, 0.0, (0.0, -2.0, 127.0)),
        ("crew_deck", "верхняя палуба: камбуз, кают-компания, отдых, лаборатория, медотсек", 121.6, 134.0, 1.0, 5.5, 8.4, 0.0, (0.0, 3.0, 127.0)),
        ("airlock", "шлюз-кабина лифта", AIRLOCK_S - CAB_HZ - 0.1, AIRLOCK_S + CAB_HZ + 0.1, 1.0, LOCK_TOP, 1.0, -8.1, (-8.1, 2.0, AIRLOCK_S)),
        ("keel_bay", "киль: анабиоз и технические отсеки", 134.0, 143.8, -4.9, -2.9, 6.2, 0.0, (0.0, -3.6, 139.0)),
        ("command_bridge", "командная рубка (капсула)", BR_S - 4.4, BR_S + 4.4, BR_Y - 1.0, BR_Y + BR_R, 4.7, 0.0,
         (0.0, BR_Y - 1.0 + 1.65, BR_S - 0.5)),
    ]
    out = []
    for key, ru, s0, s1, y0, y1, hx, xc, cam in C:
        out.append(dict(key=key, ru=ru, s0=s0, s1=s1, y0=y0, y1=y1, hx=hx, xc=xc, cam=np.array([cam[0], cam[1], zs(cam[2])])))
    return out


COMPARTMENTS = compartments()
PASS_STEPS = [(20.0, 105.0, 5.5), (105.0, 112.0, 6.0), (112.0, 119.0, 5.5)]     # floor of the passage by station (the hangar beam at s 106-111)
PASS_H = 2.0
# Openings in the interior walls: ("z", s, x, sill, w, h) in a bulkhead, ("y", y, x, s, size) hatch in a floor/ceiling slab,
# ("x", x, s, sill, w, h) in a side wall. Applied to every wall lying in the plane.
DOORS_INT = [
    ("z", 120.3, -0.45, -4.5, 1.4, 2.1),               # hangar -> lock
    ("z", 121.6, -0.45, -4.5, 1.4, 2.1),               # lock -> lower lobby strip
    ("z", 134.0, 0.0, 1.0, 1.66, 2.33),                # upper corridor -> the bridge door (the arch sleeve passes through; 3 cm wider: no shared faces)
    ("z", 20.0, 0.0, 5.5, 0.8, 1.9),                   # passage -> stern service door (the stern is unmanned)
    ("y", 1.0, 1.5, 123.6, 2.4),                       # spiral stair well (east of the lobby), lower <-> upper deck
    ("y", 1.0, -2.05, 123.6, 1.5, 2.4),                # lift shaft, lower <-> upper deck
    ("z", 134.0, 3.4, -4.5, 1.2, 2.1),                 # lower deck -> keel bay (starboard corridor)
    ("z", 134.0, -3.4, -4.5, 1.2, 2.1),                # lower deck -> keel bay (port corridor)
    ("y", 5.5, -2.05, 123.6, 1.5, 2.4),                # lift shaft, upper deck <-> link room (technical level)
    ("z", 119.0, 0.0, 5.5, 1.0, 2.0),                  # passage -> link room
    ("x", -7.0, AIRLOCK_S, 1.0, 1.2, 2.1),             # living deck -> airlock
    ("x", -9.1, AIRLOCK_S, 1.0, 1.2, 2.1),             # airlock -> outside
]


def _holes(plane, pos, tol):
    return [o for o in DOORS_INT if o[0] == plane and abs(o[1] - pos) <= tol]


def _subtract(rects, hole):
    """Cut a rectangular hole (u0, v0, u1, v1) out of a list of rectangles."""
    h0, g0, h1, g1 = hole
    out = []
    for u0, v0, u1, v1 in rects:
        if h1 <= u0 or h0 >= u1 or g1 <= v0 or g0 >= v1:
            out.append((u0, v0, u1, v1))
            continue
        if h0 > u0:
            out.append((u0, v0, h0, v1))
        if h1 < u1:
            out.append((h1, v0, u1, v1))
        a0, a1 = max(u0, h0), min(u1, h1)
        if g0 > v0:
            out.append((a0, v0, a1, g0))
        if g1 < v1:
            out.append((a0, g1, a1, v1))
    return out


def _fit_hx(c):
    """Largest half width (<= c['hx']) for which every corner of the box lies inside the skin."""
    hx = c["hx"]
    while hx > 0.3:
        ok = True
        for x in (c["xc"] - hx, c["xc"] + hx):
            for y in (c["y0"], c["y1"]):
                for sv in (c["s0"], c["s1"]):
                    o = outside(np.array([x, y, zs(sv)]))
                    if o is not None and o > -0.15:
                        ok = False
        if ok:
            return hx
        hx -= 0.1
    return hx


for _c in COMPARTMENTS:
    if _c["key"] not in ("tech_passage", "airlock", "hangar_lock", "hangar", "bridge_access", "command_bridge", "keel_bay", "tech_link"):
        _c["hx"] = round(_fit_hx(_c), 1)


_SEAT_G = [None]                                            # group for the seats of the rooms (blue upholstery), set by build_rooms
_FG = {}                                                    # (deck, class) -> group: furniture classes with their own material
_KIND_CLASS = {"cabin": "furn", "dining": "furn", "lounge": "furn", "store": "furn", "galley": "metal", "gym": "metal", "lab": "metal",
               "medical": "metal", "eva": "metal", "lockroom": "metal", "lss": "metal", "power": "metal", "workshop": "metal", "stores": "furn",
               "washroom": "wet", "wc": "wet", "shower": "wet"}


def _fb3v(g, f, c, h):
    """Visual-only oriented box of a frame (no collision)."""
    _OBOX0(g, f.p(c), f.R[:, 0], f.R[:, 1], f.R[:, 2], h[0], h[1], h[2])


def accel_seat(g, x, floor_y, z):
    """Crew seat with an inertia absorber, the bridge seat simplified: column and absorbers on a base, floating pan with a cushion,
    reclined back with wings and a headrest, arm rests. Faces the nose (+z). Visual parts only; one collision box."""
    g = _SEAT_G[0] or g
    O = np.array([x, floor_y + 0.075, z])
    F0 = _Fr(O)
    _fb3v(g, F0, (0, 0.02, 0), (0.24, 0.02, 0.24))                                                   # base plate
    _fb3v(g, F0, (0, 0.24, 0), (0.07, 0.22, 0.07))                                                   # central column
    for sx in (-1, 1):
        _fb3v(g, F0, (sx * 0.2, 0.2, -0.1), (0.025, 0.17, 0.025))                                    # absorbers
    H = _Fr(O + (0, 0.5, 0))
    _fb3v(g, H, (0, 0.04, 0.05), (0.26, 0.035, 0.26)); _fb3v(g, H, (0, 0.095, 0.05), (0.22, 0.025, 0.22))   # pan and cushion
    B = H.child((0, 0.08, -0.2), rx=-0.35)
    _fb3v(g, B, (0, 0.42, 0), (0.27, 0.42, 0.035)); _fb3v(g, B, (0, 0.42, 0.055), (0.21, 0.36, 0.025))    # back and cushion
    for sx in (-1, 1):
        W = B.child((sx * 0.30, 0.38, 0.06), ry=-sx * 0.25); _fb3v(g, W, (0, 0, 0), (0.03, 0.3, 0.08))     # wings
        _fb3v(g, H, (sx * 0.31, 0.2, 0.05), (0.025, 0.02, 0.2))                                              # arm rests
        _fb3v(g, H, (sx * 0.31, 0.11, 0.05), (0.02, 0.07, 0.03))
    _fb3v(g, B, (0, 0.93, 0.045), (0.14, 0.1, 0.04))                                                        # headrest
    _solid(x - 0.34, x + 0.34, z + 60.0 - 0.46, z + 60.0 + 0.36, floor_y + 0.075, 1.4)


# ---------------------------------------------------------------------------
# Crew rooms for 14 people, inside the existing hull. The decks use the full inner width of the hull (W from the fitted deck
# compartments, ~8.3 m each side). Upper deck = day zone (galley, wardroom, lounge and cinema, lab/library, EVA prep, medical);
# lower deck = night zone (14 two-level cabins on the hull sides, gym, hygiene); keel bay under the bridge = stores and technical
# rooms. x across (port -), s along the ship (nose +). Acceleration seats always face the nose, so they stand where facing the nose
# means facing something: a desk, the lounge screen.
U_Y0, U_Y1 = 1.0, 5.5
L_Y0, L_Y1 = -4.5, 1.0
K_Y0, K_Y1 = -4.9, -2.9
D_S0, D_S1 = 121.6, 134.0                                  # the end walls of the deck compartments
K_S0, K_S1, K_HX = 134.0, 143.8, 6.2
LOB = 125.6                                               # lobby depth: stair, lift and passes round them


def _hull_half(deck, s):
    """Largest |x| that stays 0.25 m inside the skin over the whole height of the deck, at station s (the hull tapers to the
    nose and narrows towards the roof, so the decks are trapezoids, not rectangles)."""
    y0, y1 = (L_Y0, L_Y1) if deck == "L" else (U_Y0, U_Y1)
    best = 99.0
    for y in (y0 + 0.1, 0.5 * (y0 + y1), y1 - 0.1):
        lo, hi = 0.0, 12.5
        for _ in range(14):
            mid = 0.5 * (lo + hi)
            o = outside(np.array([mid, y, zs(s)]))
            if o is not None and o < -0.25:
                lo = mid
            else:
                hi = mid
        best = min(best, lo)
    return best


_HPROF_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_hull_profile.json")


def _x_at(s, y):
    lo, hi = 0.0, 12.5
    for _ in range(14):
        mid = 0.5 * (lo + hi)
        o = outside(np.array([mid, y, zs(s)]))
        if o is not None and o < -0.25:
            lo = mid
        else:
            hi = mid
    return lo


def _load_hprofile():
    probe = [round(float(outside(np.array([8.0, 1.0, zs(128.0)])) or 0.0), 3), round(float(outside(np.array([9.0, 3.0, zs(131.0)])) or 0.0), 3),
             round(float(outside(np.array([9.5, -2.0, zs(124.0)])) or 0.0), 3)]
    if os.path.exists(_HPROF_FILE):
        d = json.load(open(_HPROF_FILE, encoding="utf-8"))
        if d.get("probe") == probe:
            return d
    d = {"probe": probe, "s": [round(float(v), 3) for v in np.arange(D_S0, D_S1 + 0.01, 0.4)]}
    for deck, (ya, yb) in (("L", (L_Y0 + 0.075, L_Y1 - 0.075)), ("U", (U_Y0 + 0.075, U_Y1 - 0.075))):
        ys = [round(float(v), 3) for v in np.linspace(ya, yb, int((yb - ya) / 0.5) + 2)]
        d[deck] = {"y": ys, "x": [[round(_x_at(sv, y), 3) for y in ys] for sv in d["s"]]}
    json.dump(d, open(_HPROF_FILE, "w", encoding="utf-8"))
    return d


_HPROF = _load_hprofile()


def _hull_x(deck, s, y):
    """Half width of the hull (0.25 m inside the skin) at station s and height y."""
    P = _HPROF[deck]
    ys, X = P["y"], np.array(P["x"])
    col = [float(np.interp(s, _HPROF["s"], X[:, k])) for k in range(len(ys))]
    return float(np.interp(y, ys, col))


def _wall_poly(deck, s0, s1):
    """Hull-side wall of a room spanning s0..s1 as (|x|, y) from the floor to the ceiling every 0.5 m: the narrowest width over s0..s1."""
    y0, y1 = (L_Y0, L_Y1) if deck == "L" else (U_Y0, U_Y1)
    ylo, yhi = y0 + 0.075, y1 - 0.075
    n = max(2, int(round((yhi - ylo) / 0.5)) + 1)
    out = []
    for yv in np.linspace(ylo, yhi, n):
        v = min(_hull_x(deck, sv, yv) for sv in np.linspace(s0, s1, max(2, int((s1 - s0) / 0.4) + 1)))
        out.append((math.floor(v * 20.0) / 20.0, float(yv)))
    return out


def _hw(deck, s0, s1):
    """Half width a room spanning s0..s1 can use (rounded down to 5 cm)."""
    v = min(_hull_half(deck, s) for s in np.linspace(s0, s1, max(2, int((s1 - s0) / 0.4) + 1)))
    return math.floor(v * 20.0) / 20.0


for _c in COMPARTMENTS:                                   # the airlock cell sits in the hull gap outside the EVA room
    if _c["key"] == "airlock":
        _c["xc"], _c["hx"] = -7.85, 1.0
        _c["cam"] = np.array([-7.85, 2.0, zs(AIRLOCK_S)])
DOORS_INT[:] = [d for d in DOORS_INT if not (d[0] == "x" and d[1] in (-7.0, -9.1))]
DOORS_INT += [("x", -6.85, AIRLOCK_S, 1.0, 1.4, 2.1),         # EVA room -> main airlock
              ("x", -8.85, AIRLOCK_S, 1.0, 2 * CAB_HZ + 0.1, CAB_Y1 - CAB_Y0 + 0.1)]   # main airlock -> out: the whole cabin passes through

ROOMS = []
ROOM_DOORS = {"U": [], "L": [], "K": []}                  # ("x" | "s", boundary coordinate, centre along it, width)


def _room(key, ru, deck, x0, x1, s0, s1, kind, open_=False, poly=None):
    ROOMS.append(dict(key=key, ru=ru, deck=deck, x0=x0, x1=x1, s0=s0, s1=s1, kind=kind, open=open_, poly=poly))


def _door(deck, axis, coord, centre, width):
    ROOM_DOORS[deck].append((axis, coord, centre, width))


# ---- lower deck: night zone (ring layout: cabins on the sides, side corridors, gym and hygiene in the middle)
_room("lobby_l", "вестибюль, лифт и трап", "L", -2.8, 2.8, D_S0, LOB, "lobby", True)
_room("corr_l_p", "коридор левого борта", "L", -4.0, -2.8, D_S0, D_S1, "corr", True)
_room("corr_l_s", "коридор правого борта", "L", 2.8, 4.0, D_S0, D_S1, "corr", True)
_n = 0
for _side in (-1, 1):                                     # cabins 1-7 port, 8-14 starboard: each runs out to the hull
    for _k in range(7):
        _s0 = D_S0 + _k * (D_S1 - D_S0) / 7
        _s1 = D_S0 + (_k + 1) * (D_S1 - D_S0) / 7
        _p = _wall_poly("L", _s0, _s1)
        _w = _p[0][0]                                      # the floor-level extent; the wall leans along the hull above it
        _n += 1
        _room(f"cabin_{_n}", f"каюта {_n}", "L", -_w if _side < 0 else 4.0, -4.0 if _side < 0 else _w, _s0, _s1, "cabin", poly=_p)
        _door("L", "x", _side * 4.0, _s0 + 1.25, 0.7)
_room("gym", "спортзал", "L", -2.8, 2.8, LOB, 129.2, "gym")
_door("L", "s", LOB, 0.0, 1.6)
_door("L", "x", -2.8, 127.6, 1.2)
_door("L", "x", 2.8, 127.6, 1.2)
_room("washroom", "умывальная", "L", -2.8, 2.8, 129.2, 131.2, "washroom")
_door("L", "x", -2.8, 130.2, 1.2)
_door("L", "x", 2.8, 130.2, 1.2)
for _k in range(6):                                       # 4 toilets and 2 wave showers
    _x0 = -2.8 + _k * (5.6 / 6)
    _room(f"wc_{_k + 1}" if _k < 4 else f"shower_{_k - 3}", f"санузел {_k + 1}" if _k < 4 else f"волновой душ {_k - 3}",
          "L", _x0, _x0 + 5.6 / 6, 131.2, D_S1, "wc" if _k < 4 else "shower")
    _door("L", "s", 131.2, _x0 + 5.6 / 12, 0.7)

# ---- upper deck: day zone. Port: medical bay (next to the lift: stretchers) -> suit-up room -> lock room -> the main airlock lift
#      (the principal exit, straight off the corridor); lab and library forward, quiet. Starboard: galley, dining, lounge/cinema.
_room("lobby_u", "вестибюль, лифт и лестница", "U", -2.8, 2.8, D_S0, LOB, "lobby", True)
_room("corr_u", "центральный коридор", "U", -1.0, 1.0, LOB, D_S1, "corr", True)
_pm = _wall_poly("U", D_S0, LOB)
_room("medical", "медотсек", "U", -_pm[0][0], -2.8, D_S0, LOB, "medical", poly=_pm)
_door("U", "s", LOB, -5.2, 1.2)                           # medical -> suit-up room
_pe = _wall_poly("U", LOB, 127.4)
_room("eva", "экипировка", "U", -_pe[0][0], -1.0, LOB, 127.4, "eva", poly=_pe)
_door("U", "x", -1.0, 128.0, 4.4)  #                        # corridor -> suit-up room (the only way to the lift)
_room("lockroom", "зона лифта: давление и безопасность", "U", -6.85, -1.0, 127.4, 130.6, "lockroom")   # as long as the lift cell (127.4..130.6)
#_door("U", "x", -1.0, 129.0, 1.2)                         # corridor -> lift zone (now one wide opening with the suit-up room) (an ordinary door); door B to the lift cell is in DOORS_INT
PANEL_X = (-6.25, -5.80, -5.35)                           # the three buttons of the lift panel on the forward wall of the lift zone
PANEL_BTN = [0.0, 0.0]                                        # filled by the lockroom: the buttons' centre y and the front z of the caps
LIFT_BTN = []                                                 # the caps' groups (lit, dim per button): at the very end of TantraVC
_pl = _wall_poly("U", 130.6, D_S1)
_room("lab", "лаборатория и библиотека", "U", -_pl[0][0], -1.0, 130.6, D_S1, "lab", poly=_pl)
_door("U", "x", -1.0, 132.1, 1.4)
_pg = _wall_poly("U", D_S0, LOB)
_room("galley", "камбуз", "U", 2.8, _pg[0][0], D_S0, LOB, "galley", poly=_pg)
_door("U", "s", LOB, 5.0, 1.2)
_pd = _wall_poly("U", LOB, 129.0)
_room("dining", "кают-компания", "U", 1.0, _pd[0][0], LOB, 129.0, "dining", poly=_pd)
_door("U", "x", 1.0, 127.3, 1.4)
_pn = _wall_poly("U", 129.0, D_S1)
_room("lounge", "зона отдыха", "U", 1.0, _pn[0][0], 129.0, D_S1, "lounge", poly=_pn)
_door("U", "x", 1.0, 130.0, 1.2)

# ---- keel bay under the bridge capsule (2.0 m high): two side corridors, life support and power in the middle,
#      workshop on the port side, general stores on the starboard side (the crew sleeps chemically in the cabins: no anabiosis)
_room("corr_k_p", "коридор киля левый", "K", -4.0, -2.8, K_S0, K_S1, "corr", True)
_room("corr_k_s", "коридор киля правый", "K", 2.8, 4.0, K_S0, K_S1, "corr", True)
_room("lss", "жизнеобеспечение: воздух и вода", "K", -2.8, 2.8, K_S0, 139.0, "lss")
_door("K", "x", -2.8, 136.5, 1.0)
_door("K", "x", 2.8, 136.5, 1.0)
_room("power", "энергия и тепло", "K", -2.8, 2.8, 139.0, K_S1, "power")
_door("K", "x", -2.8, 141.4, 1.0)
_door("K", "x", 2.8, 141.4, 1.0)
_room("stores", "мастерская и склад", "K", -K_HX, -4.0, K_S0, K_S1, "workshop")
_door("K", "x", -4.0, 138.9, 1.0)
_room("stores_k", "склад и запасы", "K", 4.0, K_HX, K_S0, K_S1, "stores")
_door("K", "x", 4.0, 134.9, 1.0)

for _r in ROOMS:
    _r["area"] = (_r["x1"] - _r["x0"]) * (_r["s1"] - _r["s0"])
    _r["xc0"], _r["xc1"] = _r["x0"], _r["x1"]                # extents at the ceiling: the hull-side wall leans
    if _r.get("poly"):
        if _r["x1"] <= 0.0:
            _r["xc0"] = -_r["poly"][-1][0]
        else:
            _r["xc1"] = _r["poly"][-1][0]
    _r["y0"], _r["y1"] = {"U": (U_Y0, U_Y1), "L": (L_Y0, L_Y1), "K": (K_Y0, K_Y1)}[_r["deck"]]


_OBOX0 = obox                                            # the plain oriented-box builder (build_interior later wraps it to record collisions)


def _deco(g, x0, x1, s0, s1, yb, h):
    """A visual-only box: it does not enter the collision list."""
    c = ((x0 + x1) / 2, yb + h / 2, zs((s0 + s1) / 2))
    _OBOX0(g, c, (1, 0, 0), (0, 1, 0), (0, 0, 1), (x1 - x0) / 2, h / 2, (s1 - s0) / 2)


def _solid(x0, x1, s0, s1, yb, h):
    """The collision box of a piece of furniture (invisible)."""
    _coll("furn", (min(x0, x1), yb, zs(min(s0, s1))), (max(x0, x1), yb + h, zs(max(s0, s1))))


def _legs(g, x0, x1, s0, s1, yb, h, t=0.06):
    for x in (x0 + 0.04, x1 - 0.04 - t):
        for sv in (s0 + 0.04, s1 - 0.04 - t):
            _deco(g, x, x + t, sv, sv + t, yb, h)


def _table(g, x0, x1, s0, s1, yb, h=0.75):
    _deco(g, x0, x1, s0, s1, yb + h - 0.05, 0.05)
    _legs(g, x0, x1, s0, s1, yb, h - 0.05)
    _solid(x0, x1, s0, s1, yb + 0.1, h - 0.1)


def _bench(g, x0, x1, s0, s1, yb, h=0.45):
    _deco(g, x0, x1, s0, s1, yb + h - 0.06, 0.06)
    _legs(g, x0, x1, s0, s1, yb, h - 0.06)
    _solid(x0, x1, s0, s1, yb + 0.1, h - 0.1)


def _counter(g, x0, x1, s0, s1, yb, h=0.9):
    _deco(g, x0, x1, s0, s1, yb, h - 0.04)
    _deco(g, x0 - 0.02, x1 + 0.02, s0 - 0.02, s1 + 0.02, yb + h - 0.04, 0.04)
    _solid(x0, x1, s0, s1, yb, h)


def _bed(g, x0, x1, s0, s1, yb):
    _deco(g, x0, x1, s0, s1, yb + 0.15, 0.08)                 # frame
    _deco(g, x0 + 0.03, x1 - 0.03, s0 + 0.03, s1 - 0.03, yb + 0.23, 0.15)   # mattress
    _legs(g, x0, x1, s0, s1, yb, 0.15)
    _solid(x0, x1, s0, s1, yb + 0.1, 0.5)


def _sofa(g, x0, x1, s0, s1, yb):
    """Along the hull wall on the +x side: the back against the wall."""
    _deco(g, x0, x1 - 0.22, s0, s1, yb + 0.12, 0.28)          # seat cushion
    _deco(g, x1 - 0.22, x1, s0, s1, yb + 0.12, 0.8)           # back
    _deco(g, x0, x1, s0, s0 + 0.12, yb + 0.12, 0.5)           # arm rests
    _deco(g, x0, x1, s1 - 0.12, s1, yb + 0.12, 0.5)
    _solid(x0, x1, s0, s1, yb + 0.1, 0.9)


def _fb(g, x0, x1, s0, s1, yb, h):
    box(g, (min(x0, x1), yb, zs(min(s0, s1))), (max(x0, x1), yb + h, zs(max(s0, s1))))


def _uncovered(a0, a1, covers):
    """The parts of [a0, a1] that no interval of `covers` reaches."""
    parts = [(a0, a1)]
    for c0, c1 in covers:
        nxt = []
        for p0, p1 in parts:
            if c1 <= p0 + 1e-6 or c0 >= p1 - 1e-6:
                nxt.append((p0, p1)); continue
            if c0 > p0 + 1e-6: nxt.append((p0, c0))
            if c1 < p1 - 1e-6: nxt.append((c1, p1))
        parts = nxt
    return [(p0, p1) for p0, p1 in parts if p1 - p0 > 0.05]


def _room_x_at(r, yv):
    """(x_lo, x_hi) of a room at height yv: the hull side follows the sloped wall."""
    lo, hi = r["x0"], r["x1"]
    p = r.get("poly")
    if p:
        w = float(np.interp(yv, [q[1] for q in p], [q[0] for q in p]))
        if r["x1"] <= 0.0:
            lo = -w
        else:
            hi = w
    return lo, hi


_HAZ_DOORS = {("U", "x", -1.0, 128.0)}      # doors on the way to the lift (hazard-striped sill)
_LEAF_DOORS = {}   # doors with a sliding leaf, half open, on the corridor side: slides north (+1) / south (-1)


def _door_frame(deck, axis, plane, centre, width, height, sill, thk, g_wall, hazard=False):
    """An octagonal doorway: the rectangular opening of a wall gets chamfered corners (filler plates in the wall) and a flat
    frame round the opening (visual only, no collision). axis 'x': wall plane x = plane, the door at station `centre`;
    axis 'z' (or 's'): wall plane z = plane, the door at x = `centre`."""
    ct = min(0.42, width * 0.3)
    cb = min(0.24, width * 0.2)
    u0, u1 = (zs(centre) if axis == "x" else centre) - width / 2, (zs(centre) if axis == "x" else centre) + width / 2
    y0, yt = sill, sill + height
    if axis == "x":
        ax_u, ax_n = (0, 0, 1), (1, 0, 0)
    else:
        ax_u, ax_n = (1, 0, 0), (0, 0, 1)
    for (cu, cy, su, sy, c) in ((u0, yt, 1, -1, ct), (u1, yt, -1, -1, ct), (u0, y0, 1, 1, cb), (u1, y0, -1, 1, cb)):   # corner fillers
        tri_ = [(cu, cy), (cu + su * c, cy), (cu, cy + sy * c)]
        prism(g_wall, tri_, ax_u, (0, 1, 0), ax_n, plane, thk)
    pts = [(u0 + cb, y0), (u1 - cb, y0), (u1, y0 + cb), (u1, yt - ct), (u1 - ct, yt), (u0 + ct, yt), (u0, yt - ct), (u0, y0 + cb)]
    gm = _FG[(deck, "metal")]
    gs_ = _FG[(deck, "trim" if hazard else "metal")]
    gc = (sum(q[0] for q in pts) / 8, sum(q[1] for q in pts) / 8)
    for k in range(8):
        pa, pb = pts[k], pts[(k + 1) % 8]
        du, dy = pb[0] - pa[0], pb[1] - pa[1]
        L = math.hypot(du, dy)
        if k == 0:                                                                       # sill: a flat plate in the floor line
            mid = ((pa[0] + pb[0]) / 2, y0 + 0.012)
            hy, off = 0.012, 0.0
        else:
            mid = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
            nu, ny = -dy / L, du / L
            if (mid[0] - gc[0]) * nu + (mid[1] - gc[1]) * ny < 0:
                nu, ny = -nu, -ny
            mid = (mid[0] + nu * 0.05, mid[1] + ny * 0.05)
            hy = 0.045
        e3 = np.array(ax_u, float) * du + np.array((0, 1, 0), float) * dy
        e3 = e3 / (np.linalg.norm(e3) or 1.0)
        n3 = np.cross(np.array(ax_n, float), e3)
        c3 = np.array(ax_u, float) * mid[0] + np.array((0, 1, 0), float) * mid[1] + np.array(ax_n, float) * plane
        _OBOX0(gs_ if k == 0 else gm, c3, e3, n3, ax_n, L / 2 + 0.045, hy, thk / 2 + 0.06)


def _stand(g, deck, cx_, sw, yb):
    """A suit donning stand in an open bay on the aft wall: back plate with rear entry, fins, header, a spine with the torso
    cradle, a helmet shelf, boot cradle and light bars. The suit itself is not modelled here (the suit sessions own it)."""
    gm_, gl_ = _FG[(deck, "metal")], _FG[(deck, "light")]
    d_ = 0.65
    _deco(gm_, cx_ - 0.4, cx_ + 0.4, sw, sw + 0.05, yb + 0.1, 2.25)                                   # back plate
    for sx in (-1, 1):
        _deco(gm_, cx_ + sx * 0.4 - 0.025, cx_ + sx * 0.4 + 0.025, sw, sw + d_, yb, 2.45)               # fins
        _deco(_FG[(deck, "guide")], cx_ + sx * 0.37 - 0.012, cx_ + sx * 0.37 + 0.012, sw + d_ - 0.06, sw + d_ - 0.02, yb + 0.15, 2.2)   # light bars
    _deco(gm_, cx_ - 0.425, cx_ + 0.425, sw, sw + d_, yb + 2.4, 0.06)                                 # header
    _deco(gm_, cx_ - 0.04, cx_ + 0.04, sw + 0.05, sw + 0.13, yb + 0.15, 1.7)                          # spine
    _deco(gm_, cx_ - 0.22, cx_ + 0.22, sw + 0.1, sw + 0.2, yb + 1.1, 0.5)                             # torso cradle
    _deco(gm_, cx_ - 0.2, cx_ + 0.2, sw + 0.05, sw + 0.38, yb + 1.92, 0.04)                           # helmet shelf
    _deco(gm_, cx_ - 0.3, cx_ + 0.3, sw + 0.12, sw + 0.5, yb + 0.03, 0.1)                             # boot cradle
    _deco(_FG[(deck, "green")], cx_ - 0.12, cx_ + 0.12, sw + d_ - 0.03, sw + d_, yb + 2.34, 0.04)     # status lamp
    _solid(cx_ - 0.43, cx_ + 0.43, sw, sw + d_, yb, 2.45)


def _door_leaf(deck, axis, plane, centre, width, height, sill, thk, side, slide):
    """A sliding door leaf, half open: a plate in front of the wall next to the opening (it slides into the wall pocket),
    a light bar on its edge. Visual only. side: which face of the wall (+1/-1); slide: towards +u (+1) or -u (-1)."""
    gm = _FG[(deck, "metal")]
    u0 = (zs(centre) if axis == "x" else centre) - width / 2
    u1 = u0 + width
    a, b = (u1 - 0.45 * width, u1 + 0.55 * width) if slide > 0 else (u0 - 0.55 * width, u0 + 0.45 * width)
    off = side * (thk / 2 + 0.07)
    y0, y1 = sill, sill + height + 0.04
    if axis == "x":
        box(gm, (plane + off - 0.025, y0, a), (plane + off + 0.025, y1, b))
        edge = b if slide < 0 else a
        box(_FG[(deck, "guide")], (plane + off - 0.03, y0 + 0.1, edge - 0.015), (plane + off + 0.03, y1 - 0.1, edge + 0.015))
    else:
        box(gm, (a, y0, plane + off - 0.025), (b, y1, plane + off + 0.025))
        edge = b if slide < 0 else a
        box(_FG[(deck, "guide")], (edge - 0.015, y0 + 0.1, plane + off - 0.03), (edge + 0.015, y1 - 0.1, plane + off + 0.03))


_SHUT_DOORS = {("U", "x", 1.0, 127.3), ("U", "x", 1.0, 130.0), ("U", "s", 125.6, -5.2), ("U", "x", -1.0, 132.1)}   # + the lab (user, 2026-10-03)   # closed for now (user, 2026-10-03): dining, rest area, medical


def _door_shut(deck, axis, plane, centre, width, height, sill, cls="metal"):
    """A shut door: two leaves meeting in the middle, filling the octagonal doorway, a small window in one leaf at eye height,
    a lock bar across the seam and a red lamp on both sides. A wall for the walker. axis 'x': wall x = plane, door at station
    `centre`; axis 'z': wall z = plane, door at x = `centre`."""
    gm = _FG[(deck, cls)]
    ct, cb = min(0.42, width * 0.3), min(0.24, width * 0.2)
    uc = zs(centre) if axis == "x" else centre
    u0, u1, y0, yt, g = uc - width / 2, uc + width / 2, sill, sill + height, 0.006
    wy0, wy1 = sill + 1.3, sill + 1.62                                   # the window band (eye height)
    wl0, wl1 = uc - 0.38, uc - 0.12                                      # the window, in the low-u leaf
    left = [[(u0 + cb, y0), (uc - g, y0), (uc - g, wy0), (u0, wy0), (u0, y0 + cb)],
            [(u0, wy0), (wl0, wy0), (wl0, wy1), (u0, wy1)],
            [(wl1, wy0), (uc - g, wy0), (uc - g, wy1), (wl1, wy1)],
            [(u0, wy1), (uc - g, wy1), (uc - g, yt), (u0 + ct, yt), (u0, yt - ct)]]
    right = [[(2 * uc - u, y) for u, y in left[0]], [(uc + g, wy0), (u1, wy0), (u1, wy1), (uc + g, wy1)], [(2 * uc - u, y) for u, y in left[3]]]
    A = np.array((0, 0, 1) if axis == "x" else (1, 0, 0), float)
    N = np.array((1, 0, 0) if axis == "x" else (0, 0, 1), float)
    for poly in left + right:
        prism(gm, poly, A, (0, 1, 0), N, plane, 0.05)
    gfc = _FG[(deck, "door")]                                            # the faces: brushed panel, frame line, handle recess at the seam,
    for leaf, ua, ub in ((left, u0, uc - g), (right, uc + g, u1)):       # kick plate (the texture; mirrored on the right leaf: the handle at the seam)
        for sd in (-1, 1):
            nrm = N * sd
            for poly in leaf:
                pts = [A * u + np.array((0.0, y, 0.0)) + N * (plane + sd * 0.028) for u, y in poly]
                uvs = [((u - ua) / (ub - ua) if ua < uc else (ub - u) / (ub - ua), (yt - y) / (yt - y0)) for u, y in poly]
                idx = [gfc.vert(p_, nrm, uv) for p_, uv in zip(pts, uvs)]
                for k_ in range(1, len(idx) - 1):
                    gfc.tri(idx[0], idx[k_], idx[k_ + 1], nrm)
    gfc.tex = 45
    for sd in (-1, 1):
        c_ = A * uc + np.array((0.0, y0 + 1.02, 0.0)) + N * (plane + sd * 0.04)
        _OBOX0(gm, c_, A, (0, 1, 0), N, 0.12, 0.07, 0.015)               # lock bar across the seam
        for (ua, ub, ya, yb_) in ((wl0 - 0.03, wl1 + 0.03, wy0 - 0.03, wy0), (wl0 - 0.03, wl1 + 0.03, wy1, wy1 + 0.03),
                                  (wl0 - 0.03, wl0, wy0, wy1), (wl1, wl1 + 0.03, wy0, wy1)):   # window rim
            c_ = A * (ua + ub) / 2 + np.array((0.0, (ya + yb_) / 2, 0.0)) + N * (plane + sd * 0.03)
            _OBOX0(gm, c_, A, (0, 1, 0), N, (ub - ua) / 2, (yb_ - ya) / 2, 0.01)
        _lamp(deck, "red", axis, plane if axis == "x" else plane - STERN_Z, centre, yt + 0.12, sd, 0.06, 0.3)
    lo = A * u0 + np.array((0.0, y0, 0.0)) + N * (plane - 0.05)
    hi = A * u1 + np.array((0.0, yt, 0.0)) + N * (plane + 0.05)
    _coll("door", np.minimum(lo, hi), np.maximum(lo, hi))


def _lamp(deck, cls, axis, plane, centre, y, side, thk=0.06, w=0.5):
    """A status lamp strip above a door, `side` = +1/-1: which face of the wall (plane, thickness thk) it sits on."""
    g_ = _FG[(deck, cls)]
    o_ = side * (thk / 2 + 0.02)
    if axis == "x":
        c = zs(centre)
        box(g_, (plane + o_ - 0.02, y, c - w / 2), (plane + o_ + 0.02, y + 0.06, c + w / 2))
    else:
        box(g_, (centre - w / 2, y, zs(plane) + o_ - 0.02), (centre + w / 2, y + 0.06, zs(plane) + o_ + 0.02))


def _room_walls(g, r):
    """Partitions: one wall per shared line (a room builds the wall on its x1/s1 side; the x0/s0 side only where no closed
    neighbour already has a wall there). Doors are cut by the line they stand on. On the hull side the wall leans along the
    hull (segments every 0.5 m of height) and the partitions end on it."""
    deck = r["deck"]
    xb = (-99.0, 99.0, D_S0, D_S1) if deck in ("U", "L") else (-K_HX, K_HX, K_S0, K_S1)
    ylo, yhi = r["y0"] + 0.075, r["y1"] - 0.075
    doors = ROOM_DOORS[deck]
    others = [o for o in ROOMS if o is not r and not o["open"] and o["deck"] == deck]
    poly = r.get("poly")
    hull_side = (-1 if r["x1"] <= 0.0 else 1) if poly else 0
    py, px = ([q[1] for q in poly], [q[0] for q in poly]) if poly else ([], [])

    def wall_x(xn, a0, a1, inset, owner_side_low):
        if abs(xn - xb[0]) < 0.05 or abs(xn - xb[1]) < 0.05:
            return
        covers = [(max(a0, o["s0"]), min(a1, o["s1"])) for o in others if owner_side_low and abs(o["x1"] - xn) < 0.02 and min(a1, o["s1"]) - max(a0, o["s0"]) > 0.05]
        for p0, p1 in _uncovered(a0, a1, covers):
            rects = [(zs(p0), ylo, zs(p1), yhi)]
            for d in doors:
                if d[0] == "x" and abs(d[1] - xn) < 0.2:
                    rects = _subtract(rects, (zs(d[2] - d[3] / 2), ylo, zs(d[2] + d[3] / 2), ylo + 2.1))
            for u0, v0, u1, v1 in rects:
                box(g, (xn + inset - 0.03, v0, u0), (xn + inset + 0.03, v1, u1))
            for d in doors:
                if d[0] == "x" and abs(d[1] - xn) < 0.2 and p0 - 1e-6 <= d[2] <= p1 + 1e-6:
                    key_ = (deck, "x", round(d[1], 2), round(d[2], 2))
                    _door_frame(deck, "x", xn + inset, d[2], d[3], 2.1, ylo, 0.06, g, key_ in _HAZ_DOORS)
                    if key_ in _LEAF_DOORS:
                        _door_leaf(deck, "x", xn + inset, d[2], d[3], 2.1, ylo, 0.06, 1, _LEAF_DOORS[key_])
                    if key_ in _SHUT_DOORS:
                        _door_shut(deck, "x", xn + inset, d[2], d[3], 2.1, ylo)

    def wall_s(sn, a0, a1, inset, owner_side_low):
        if abs(sn - xb[2]) < 0.05 or abs(sn - xb[3]) < 0.05:
            return
        covers = [(max(a0, o["x0"]), min(a1, o["x1"])) for o in others if owner_side_low and abs(o["s1"] - sn) < 0.02 and min(a1, o["x1"]) - max(a0, o["x0"]) > 0.05]
        if deck == "U" and abs(sn - 127.4) < 0.02:
            covers = covers + [(-6.85, -1.0)]                    # the suit-up room opens into the lift zone: no wall between them
        for p0, p1 in _uncovered(a0, a1, covers):
            rects = [(p0, ylo, p1, yhi)]
            for d in doors:
                if d[0] == "s" and abs(d[1] - sn) < 0.2:
                    rects = _subtract(rects, (d[2] - d[3] / 2, ylo, d[2] + d[3] / 2, ylo + 2.1))
            for d in doors:
                if d[0] == "s" and abs(d[1] - sn) < 0.2 and p0 - 1e-6 <= d[2] <= p1 + 1e-6:
                    _door_frame(deck, "z", zs(sn + inset), d[2], d[3], 2.1, ylo, 0.06, g, (deck, "s", round(d[1], 2), round(d[2], 2)) in _HAZ_DOORS)
                    if (deck, "s", round(d[1], 2), round(d[2], 2)) in _SHUT_DOORS:
                        _door_shut(deck, "z", zs(sn + inset), d[2], d[3], 2.1, ylo)
            for u0, v0, u1, v1 in rects:
                if hull_side:                                # end on the leaning wall: bands of 0.5 m of height
                    ya = v0
                    while ya < v1 - 1e-6:
                        yb_ = min(v1, ya + 0.5)
                        wv = float(np.interp(0.5 * (ya + yb_), py, px))
                        lo_, hi_ = u0, u1
                        if hull_side < 0:
                            lo_ = max(lo_, -wv)
                        else:
                            hi_ = min(hi_, wv)
                        if hi_ - lo_ > 0.02:
                            box(g, (lo_, ya, zs(sn + inset) - 0.03), (hi_, yb_, zs(sn + inset) + 0.03))
                        ya = yb_
                else:
                    box(g, (u0, v0, zs(sn + inset) - 0.03), (u1, v1, zs(sn + inset) + 0.03))

    if r["key"] == "lockroom":                            # above the lift cell (it is 2.9 m high) the zone has its own wall up to the ceiling
        box(g, (r["x0"] + 0.02, LOCK_TOP, zs(r["s0"])), (r["x0"] + 0.08, yhi, zs(r["s1"])))
    if r["key"] != "lockroom" and hull_side != -1:        # the airlock cell has its own inner wall there
        wall_x(r["x0"], r["s0"], r["s1"], 0.05, True)
    if hull_side != 1:
        wall_x(r["x1"], r["s0"], r["s1"], -0.05, False)
    wall_s(r["s0"], r["x0"], r["x1"], 0.05, True)
    wall_s(r["s1"], r["x0"], r["x1"], -0.05, False)
    if hull_side:                                            # the leaning hull wall, one thin oriented slab per 0.5 m of height
        pts = [(hull_side * q[0], q[1]) for q in poly]
        zc, hz = zs(0.5 * (r["s0"] + r["s1"])), 0.5 * (r["s1"] - r["s0"])
        for (xa, ya), (xb_, yb_) in zip(pts[:-1], pts[1:]):
            dx, dy = xb_ - xa, yb_ - ya
            L = math.hypot(dx, dy)
            if L > 1e-6:
                _OBOX0(g, ((xa + xb_) / 2, (ya + yb_) / 2, zc), (dx / L, dy / L, 0.0), (-dy / L, dx / L, 0.0), (0.0, 0.0, 1.0), L / 2, 0.03, hz)
                _coll(g.name, (min(xa, xb_) - 0.03, ya, zc - hz), (max(xa, xb_) + 0.03, yb_, zc + hz))      # one box per 0.5 m of height (the wall is almost vertical)


def _furnish(g, r):
    k, yb = r["kind"], r["y0"] + 0.075
    if _FG:
        g = _FG[(r["deck"], _KIND_CLASS.get(k, "furn"))]
        _SEAT_G[0] = _FG[(r["deck"], "seat")]
    x0, x1, s0, s1 = r["x0"], r["x1"], r["s0"], r["s1"]
    poly = r.get("poly")

    def wallx(yv):                                           # |x| of the hull-side wall at the absolute height yv
        return float(np.interp(yv, [q[1] for q in poly], [q[0] for q in poly])) if poly else abs(x0 if x1 <= 0 else x1)

    def ins(h):                                              # how far the hull-side wall at height h stands inside its floor-level position
        return max(0.0, abs(x0 if x1 <= 0 else x1) - wallx(yb + h)) if poly else 0.0
    if k == "cabin":
        # u = distance from the corridor wall (door at u 0), v = along the ship from the aft partition. Below: a study with the
        # desk on the forward partition and the chair at the desk facing the nose; the wardrobe on the aft partition. Above
        # (2.45 m up): the sleeping loft on an inertia-absorbing base, the bed along the cabin, the chemical-sleep unit on the
        # hull wall over the head, a ladder on the forward partition.
        port = x1 < 0
        xin, o = (x1, -1.0) if port else (x0, 1.0)
        w, D = s1 - s0, abs(x1 - x0)
        Dl = max(D, wallx(yb + 2.8) - 4.0)                           # the hull wall at the height of the loft (the lower deck bulges outwards)
        Dh = max(D, wallx(yb + 3.7) - 4.0)                           # ... and at the height of the chemical-sleep unit

        def at(u0, u1, v0, v1, h, y0=0.0):
            _fb(g, xin + o * u0, xin + o * u1, s0 + v0, s0 + v1, yb + y0, h)
        du0 = max(2.3, D - 2.2)                                      # the desk is 2 m long at most, never over the wardrobe
        at(1.0, Dl - 0.05, 0.06, w - 0.06, 0.35, 2.45)               # loft platform (absorbing base)
        at(Dl - 2.55, Dl - 0.55, 0.12, 0.97, 0.3, 2.8)               # bed on the loft, head to the hull
        at(Dh - 0.5, Dh - 0.05, 0.12, 0.9, 0.5, 3.4)                 # chemical-sleep unit on the hull wall over the bed head
        at(1.3, 1.34, 0.1, 0.16, 2.5)                                # ladder on the aft partition, clear of the door
        at(1.7, 1.74, 0.1, 0.16, 2.5)
        for i in range(8):
            at(1.3, 1.74, 0.1, 0.16, 0.03, 0.3 + i * 0.3)            # rungs
        at(1.0, 2.2, w - 0.5, w - 0.08, 2.0)                         # wardrobe on the forward partition
        if _FG:
            gl = _FG[(r["deck"], "light")]
            _fb(gl, xin + o * 1.9, xin + o * 2.5, s0 + w / 2 - 0.25, s0 + w / 2 + 0.25, yb + 2.41, 0.04)                 # lamp under the loft
            _fb(gl, xin + o * (Dl - 1.6), xin + o * (Dl - 1.1), s0 + w / 2 - 0.25, s0 + w / 2 + 0.25, r["y1"] - 0.18, 0.03)    # lamp over the bed
        at(du0, D - 0.1, w - 0.45, w - 0.08, 0.75)                   # desk on the forward partition
        accel_seat(g, xin + o * (du0 + D - 0.1) / 2.0, r["y0"], zs(s0 + w - 0.83))   # chair at the desk = acceleration seat, faces the nose
        if D >= 4.8:
            at(D - 0.4, D - 0.08, 0.2, 0.7, 1.8)                     # bookshelf on the hull wall, aft side
    elif k == "medical":                                             # aft-port next to the lift: beds on the hull wall, exam table in the middle
        _bed(g, x0 + 0.1 + ins(0.55), x0 + 2.1 + ins(0.55), 122.6, 123.5, yb)                # (two aisles of 1.3 m either side of the table)
        _bed(g, x0 + 0.1 + ins(0.55), x0 + 2.1 + ins(0.55), 124.0, 124.9, yb)
        _table(g, -5.8, -4.2, 123.2, 124.2, yb, 0.8)                 # exam table
        _fb(g, -3.8, -2.95, 121.75, 122.65, yb, 1.6)                 # diagnostic gantry
        _counter(g, -6.8, -4.2, 121.68, 122.1, yb)                   # sink counter on the aft wall
        _fb(g, x0 + 0.1 + ins(1.8), x0 + 2.0 + ins(1.8), 121.7, 122.2, yb, 1.8)            # cabinets on the aft wall by the hull
    elif k == "washroom":
        _fb(g, -1.8, 1.8, 129.3, 129.8, yb, 0.9)                     # basins on the aft wall, doors at both ends stay free
        _fb(g, -1.8, 1.8, 129.25, 129.3, yb + 1.0, 0.9)              # mirror panel
    elif k == "wc":
        xc = (x0 + x1) / 2
        _fb(g, xc - 0.25, xc + 0.25, 133.0, 133.7, yb, 0.4)          # pan
        _fb(g, xc - 0.2, xc + 0.2, 133.8, 133.95, yb + 0.5, 0.5)     # cistern
    elif k == "shower":                                              # wave shower: tray and an emitter panel
        xc = (x0 + x1) / 2
        _fb(g, x0 + 0.1, x1 - 0.1, 132.6, 133.7, yb, 0.02)
        _fb(g, xc - 0.4, xc + 0.4, 133.85, 133.95, yb + 0.1, 2.2)
    elif k == "galley":
        _counter(g, 3.3, x1 - 0.1 - ins(0.9), 121.68, 122.35, yb)               # counter along the aft wall
        _counter(g, x1 - 0.75 - ins(0.9), x1 - 0.1 - ins(0.9), 122.4, 124.9, yb)           # counter along the hull wall
        _counter(g, 4.6, 6.5, 123.2, 124.1, yb)                      # island
        _fb(g, 2.95, 3.75, 122.45, 123.4, yb, 2.0)                   # food synthesiser
        _fb(g, 2.95, 3.75, 123.5, 124.3, yb, 1.9)                    # cold store
    elif k == "dining":                                              # 14 seats facing their tables (the seats face the nose); an aisle between the rows
        t_a = [(1.6, 3.8), (5.95, min(8.9, x1 - 0.75 - ins(0.9)))]      # row 1 leaves a gap for the galley door (x 5.0)
        for xa_, xb2 in t_a:
            _table(g, xa_, xb2, 126.4, 126.95, yb)
        for xc_ in (1.95, 2.7, 3.45, 6.3, 7.05, 7.8, 8.55):
            accel_seat(g, xc_, r["y0"], zs(126.1))
        _table(g, 1.6, 7.0, 128.4, 128.95, yb)
        for k_ in range(7):
            accel_seat(g, 2.05 + k_ * 0.75, r["y0"], zs(128.1))
        _counter(g, x1 - 0.5 - ins(0.9), x1 - 0.1 - ins(0.9), 126.0, 128.6, yb)           # sideboard on the hull wall
    elif k == "lounge":                                              # rest area: 6 seats facing the wall display, an aisle to the bridge door
        for srow in (131.0, 132.4):
            for x in (1.9, 3.0, 4.1):
                accel_seat(g, x, r["y0"], zs(srow))
        _deco(_FG[(r["deck"], "screen")], 1.4, 4.7, 133.93, 134.0, yb + 1.0, 1.6)   # display on the forward wall
    elif k == "gym":                                                 # lower deck, 5.5 m high: VR platform, magnetic resistance trainer, track, rower
        _fb(g, -1.0, 1.0, 126.6, 128.4, yb, 0.15)                    # VR platform
        _fb(g, -2.7, -1.7, 126.0, 127.0, yb, 1.8)                    # magnetic resistance trainer
        _fb(g, 1.6, 2.7, 126.0, 127.0, yb, 0.2)                      # track
        _fb(g, -0.8, 0.8, 128.6, 129.1, yb, 0.4)                     # rower
    elif k == "lab":                                                 # forward-port, quiet: library, lab bench, reading table
        _fb(g, x0 + ins(2.2), x0 + 0.45 + ins(2.2), 130.7, 133.5, yb, 2.2)                 # bookshelves on the hull wall
        _counter(g, x0 + 0.9 + ins(0.9), -2.6, 133.45, 133.93, yb)              # lab bench on the forward wall
        _table(g, -5.6, -3.4, 131.6, 132.5, yb, 0.75)                # reading table
    elif k == "eva":                                                 # suit-up: donning stands in open bays along the aft wall, a scan gate
        sw = s0 + 0.08
        cs = [-4.05, -3.15, -2.25]
        c_ = -6.35
        while c_ - 0.45 >= -wallx(yb + 2.5) + 0.02:                 # west of the medical door, as many bays as fit under the leaning hull wall
            cs.append(c_)
            c_ -= 0.9
        _STANDS[:] = cs
        for cx_ in cs:
            _stand(g, r["deck"], cx_, sw, yb)
    elif k == "lockroom":                                            # the lift zone: control panel (checks, pressure, lift), refuge module, door B to the lift cell
        gm_, gt_ = _FG[(r["deck"], "metal")], _FG[(r["deck"], "trim")]
        sp = s1 - 0.08                                               # inner face of the forward wall
        _deco(gm_, -6.78, -4.82, sp - 0.046, sp, yb + 0.92, 1.06)    # panel housing (its face 6 mm behind the panel face)
        gp = _FG[(r["deck"], "panel")]                              # the panel face: texture with the labels (slot 13), self-lit
        zf = zs(sp - 0.052)
        q = [gp.vert((-6.7, yb + 1.9, zf), (0, 0, -1), (0.0, 0.0)), gp.vert((-4.9, yb + 1.9, zf), (0, 0, -1), (1.0, 0.0)),
             gp.vert((-4.9, yb + 1.0, zf), (0, 0, -1), (1.0, 1.0)), gp.vert((-6.7, yb + 1.0, zf), (0, 0, -1), (0.0, 1.0))]
        gp.quad(*q, (0, 0, -1))
        gp.tex = 13
        for k_, bx in enumerate(PANEL_X):                            # 1 suit check, 2 the lock's pressure, 3 call the cabin: real push buttons
            gl_, gd_ = Group(f"lift_btn{k_}_lit", MAT["br_display"]), Group(f"lift_btn{k_}_dim", MAT["in_btn_cap"])
            zcap = _push_button(gm_, gl_, gd_, k_, bx, yb + 1.36, zf, ZONE_CAP_H)
            LIFT_BTN.extend([gl_, gd_])
        PANEL_BTN[:] = [yb + 1.36, zcap]                             # the caps' centre height and front (for the mouse pick)
        _fb(gm_, -4.3, -3.1, s1 - 0.62, s1 - 0.08, yb, 2.2)          # refuge module on the forward wall
        _fb(_FG[(r["deck"], "red")], -4.2, -3.2, s1 - 0.66, s1 - 0.62, yb + 1.5, 0.12)
        # the status screen right of the lift panel: the step, the suit checks, the pressure, the lift, refusals (drawn in the game)
        _fb(gm_, -2.82, -1.88, s1 - 0.11, s1 - 0.08, yb + 1.18, 0.64)
        gs_ = Group("lift_status", MAT["br_display"]); gs_.tex = 21; zs_ = zs(s1 - 0.115)
        q = [gs_.vert((-2.78, yb + 1.78, zs_), (0, 0, -1), (0.0, 0.0)), gs_.vert((-1.92, yb + 1.78, zs_), (0, 0, -1), (1.0, 0.0)),
             gs_.vert((-1.92, yb + 1.22, zs_), (0, 0, -1), (1.0, 1.0)), gs_.vert((-2.78, yb + 1.22, zs_), (0, 0, -1), (0.0, 1.0))]
        gs_.quad(*q, (0, 0, -1))
        LIFT_BTN.append(gs_)
        _OBOX0(gt_, (x0 + 0.4, yb + 0.012, zs(AIRLOCK_S)), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.3, 0.012, 0.7)      # hazard strip at door B
        _lamp(r["deck"], "amber", "x", x0, AIRLOCK_S, yb + 2.3, 1, 0.16)                      # door B: amber, on this side
    elif k == "store":
        _fb(g, x0, x0 + 0.6, 122.0, 125.0, yb, 2.2)
        _fb(g, -6.0, -3.5, 121.7, 122.2, yb, 2.2)
    elif k == "lss":
        _fb(g, -1.6, -0.6, 134.3, 135.7, yb, 1.5)
        _fb(g, 0.6, 1.6, 134.3, 135.7, yb, 1.5)
        _fb(g, -2.4, 2.4, 137.6, 138.8, yb, 1.7)
    elif k == "power":
        _fb(g, -2.4, 2.4, 140.0, 140.8, yb, 1.7)
        _fb(g, -2.4, 2.4, 142.4, 143.2, yb, 1.7)
    elif k == "workshop":
        _fb(g, -6.1, -4.8, 140.0, 143.0, yb, 0.9)
        _fb(g, -6.1, -5.5, 134.2, 136.2, yb, 1.8)
    elif k == "stores":                                              # shelving along the hull wall and a central rack
        _fb(g, 5.9, 6.2, 134.4, 143.4, yb, 1.9)
        for i in range(4):
            _fb(g, 4.3, 5.2, 135.8 + i * 2.0, 136.8 + i * 2.0, yb, 1.5)


def _lift_and_stair(g):
    """Lift shaft west of the lobby (x -2.8..-1.3, s 122.4..124.8) with doors to the east at the three stops and a cab at the
    lower stop; the spiral stair (1.5 turns, 36 treads) around a column east of the strip, a guard rail round its well on the
    upper deck (the west side, where the stair arrives, stays open)."""
    x0, x1, s0, s1 = -2.8, -1.3, 122.4, 124.8
    ylo, yhi = L_Y0 + 0.075, 7.5
    sc = (s0 + s1) / 2
    for xw in (x0 + 0.03, x1 - 0.03):                                 # west wall solid; east wall with three doors
        rects = [(zs(s0), ylo, zs(s1), yhi)]
        if xw > -2.0:
            for sill in (L_Y0, U_Y0, 5.5):
                rects = _subtract(rects, (zs(sc - 0.6), sill + 0.075, zs(sc + 0.6), sill + 2.1))
        for u0, v0, u1, v1 in rects:
            box(g, (xw - 0.03, v0, u0), (xw + 0.03, v1, u1))
    for zz in (zs(s0) + 0.03, zs(s1) - 0.03):                         # aft and forward walls
        box(g, (x0, ylo, zz - 0.03), (x1, yhi, zz + 0.03))
    cx, cs = 1.5, 123.6
    tube(g, (cx, L_Y0 + 0.075, zs(cs)), (cx, U_Y0 + 0.075, zs(cs)), 0.2, n=16)                        # stair column
    zc_ = zs(cs)

    def sector(r0, r1, a0, a1, n=4):
        pts = [(cx + r1 * math.cos(math.radians(a0 + (a1 - a0) * i / n)), zc_ + r1 * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
        pts += [(cx + r0 * math.cos(math.radians(a1)), zc_ + r0 * math.sin(math.radians(a1))), (cx + r0 * math.cos(math.radians(a0)), zc_ + r0 * math.sin(math.radians(a0)))]
        return pts
    for k in range(36):
        a = 15.0 * k
        yt = L_Y0 + 0.075 + 0.153 * (k + 1) + 0.03                      # the tread's top (= its floor cells)
        prism(g, sector(0.18, 1.15, a - 7.5, a + 7.5), (1, 0, 0), (0, 0, 1), (0, 1, 0), yt - 0.09, 0.18)   # solid tread block
        prism(g, sector(1.12, 1.2, a - 7.5, a + 7.5), (1, 0, 0), (0, 0, 1), (0, 1, 0), yt - 0.2, 0.42)     # outer stringer band
        for da in (-7.5, 0.0):                                          # hand rail on posts along the outer edge
            r_, aa = 1.13, math.radians(a + da)
            px_, pz_ = cx + r_ * math.cos(aa), zc_ + r_ * math.sin(aa)
            _OBOX0(g, (px_, yt + 0.45, pz_), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.02, 0.45, 0.02) if da == 0.0 else None
        a0r, a1r = math.radians(a - 7.5), math.radians(a + 7.5)
        p0 = np.array([cx + 1.13 * math.cos(a0r), yt + 0.9 - 0.0765, zc_ + 1.13 * math.sin(a0r)])
        p1 = np.array([cx + 1.13 * math.cos(a1r), yt + 0.9 + 0.0765, zc_ + 1.13 * math.sin(a1r)])
        d_ = p1 - p0; L_ = float(np.linalg.norm(d_))
        u_ = d_ / L_; v_ = np.cross(u_, (0, 1, 0)); v_ /= np.linalg.norm(v_); w_ = np.cross(v_, u_)
        _OBOX0(g, (p0 + p1) / 2, u_, w_, v_, L_ / 2 + 0.01, 0.025, 0.025)
    # walkable floor of the stair: 0.1 m cells, each at the height of the tread over it (the bounding boxes of the turned treads
    # overlapped, and the floor always took the highest one: one could neither go down nor climb evenly). Where the 1.5 turns
    # overlap a cell has two treads, 3.7 m apart.
    for xi in np.arange(cx - 1.2, cx + 1.2, 0.1):
        for zi in np.arange(-1.2, 1.2, 0.1):
            px, pz = xi + 0.05 - cx, zi + 0.05
            r_ = math.hypot(px, pz)
            if r_ < 0.2 or r_ > 1.15:
                continue
            a_ = math.degrees(math.atan2(pz, px)) % 360.0
            for k in range(36):
                d_ = (a_ - 15.0 * k + 180.0) % 360.0 - 180.0
                if abs(d_) <= 7.5:
                    yt = L_Y0 + 0.075 + 0.153 * (k + 1) + 0.03
                    _coll("stair", (xi, yt - 0.06, zs(cs) + zi), (xi + 0.1, yt, zs(cs) + zi + 0.1))
    yr = U_Y0 + 0.075                                                 # guard rail round the stair well on the upper deck
    # the landing where the stair arrives (west edge, s 123.55..124.1) and rails on the rest of the west edge
    _OBOX0(g, (0.385, yr - 0.03, zs(123.825)), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.085, 0.03, 0.275)
    _coll("stair", (0.25, yr - 0.06, zs(123.55)), (0.5, yr, zs(124.1)))
    for xa, xb, sa, sb in ((2.66, 2.74, 122.4, 124.8), (0.3, 2.7, 122.36, 122.44), (0.3, 2.7, 124.76, 124.84),
                           (0.26, 0.34, 122.4, 123.55), (0.26, 0.34, 124.1, 124.8)):
        box(g, (xa, yr + 0.95, zs(sa)), (xb, yr + 1.0, zs(sb)))        # top rail
        for t in np.linspace(0.0, 1.0, 4):
            px, ps = xa + (xb - xa) * t, sa + (sb - sa) * t
            box(g, (px - 0.02, yr, zs(ps) - 0.02), (px + 0.02, yr + 0.95, zs(ps) + 0.02))


def _deck_extents(deck, ceil=False):
    """(s0, s1, x_lo, x_hi) slices of a deck: the floor runs under the union of the rooms of that deck, the ceiling under their
    extents at the ceiling (the hull-side walls lean)."""
    rooms = [r for r in ROOMS if r["deck"] == deck]
    out = []
    for sa in np.arange(D_S0, D_S1 - 1e-6, 0.4):
        sm = sa + 0.2
        xs_ = [((r["xc0"], r["xc1"]) if ceil else (r["x0"], r["x1"])) for r in rooms if r["s0"] <= sm < r["s1"]]
        lo, hi = round(min(a for a, b in xs_), 2), round(max(b for a, b in xs_), 2)
        if out and abs(out[-1][2] - lo) < 1e-6 and abs(out[-1][3] - hi) < 1e-6:
            out[-1] = (out[-1][0], min(sa + 0.4, D_S1), lo, hi)
        else:
            out.append((sa, min(sa + 0.4, D_S1), lo, hi))
    return out


def _in_hole(deck, x, sv, pad=0.5):
    """True if (x, s) lies over a stair or lift opening of this deck's ceiling."""
    y = L_Y1 if deck == "L" else U_Y1
    for d in DOORS_INT:
        if d[0] != "y" or abs(d[1] - y) > 0.05:
            continue
        _, _, hx_, sc, sx = d[:5]
        ss = d[5] if len(d) > 5 else sx
        if abs(x - hx_) < sx / 2 + pad and abs(sv - sc) < ss / 2 + pad:
            return True
    return False


def _lights():
    """Ceiling lamp panels (self-lit): one per ~3 m in every room, corridor and lobby."""
    for r in ROOMS:
        if r["kind"] == "cabin" or r["key"] in _PATH_ROOMS or r["key"] in _DARK_ROOMS:
            continue                                         # the cabins have their own lamps, the way to the lift its watch lamps
        g = _FG[(r["deck"], "light")]
        dx, ds = r["xc1"] - r["xc0"], r["s1"] - r["s0"]
        nx, ns = max(1, int(dx / 3.0 + 0.5)), max(1, int(ds / 3.0 + 0.5))
        w = max(0.3, min(0.9, 0.4 * min(dx, ds)))
        for i in range(nx):
            for j in range(ns):
                x = r["xc0"] + (i + 0.5) * dx / nx
                sv = r["s0"] + (j + 0.5) * ds / ns
                if _in_hole(r["deck"], x, sv):
                    continue
                _fb(g, x - w / 2, x + w / 2, sv - w / 2, sv + w / 2, r["y1"] - 0.18, 0.03)


def _guides():
    """Guide lights: a thin cyan line along the foot of the walls of the upper corridor (cut at the doors)."""
    gg = _FG[("U", "guide")]
    yb = U_Y0 + 0.075
    for sx in (-1, 1):
        gaps = [(d[2] - d[3] / 2 - 0.05, d[2] + d[3] / 2 + 0.05) for d in ROOM_DOORS["U"] if d[0] == "x" and abs(d[1] - sx * 1.0) < 0.01]
        for a_, b_ in _uncovered(LOB + 0.1, D_S1 - 0.1, gaps):
            box(gg, (sx * 0.99 - 0.015, yb + 0.1, zs(a_)), (sx * 0.99 + 0.015, yb + 0.14, zs(b_)))


# ---- watch lighting of the way from the bridge to the lift (corridor, suit-up room, lift zone). D3D9Client draws up to four
# local lights per mesh and no shadows of them, so: four point lights of the module (Tantra.cpp, kWatchLights) give the real
# shading and the highlights; the pools of light, the scallops of the downlights on the walls, the cove wash and the contact
# shadows in the corners are painted (alpha decals, self-lit). The surfaces of the way have their own dim materials.
_PATH_ROOMS = ("corr_u", "eva", "lockroom", "lobby_u")
_DARK_ROOMS = ("lab", "dining", "lounge", "medical")             # behind the shut doors: dark, no lamps
DARK_BOXES = ((-12.0, -1.0, 130.6, D_S1), (1.0, 12.0, LOB, D_S1), (-12.0, -2.8, D_S0, LOB))
PATH_BOXES = ((-1.0, 1.0, LOB, D_S1), (-6.85, -1.0, 127.4, 130.6), (-10.8, -1.0, 125.65, 127.4), (-2.8, 2.8, D_S0, LOB))   # x0, x1, s0, s1 (the last: the upper lobby)
_STANDS = []
W_YF, W_YC = U_Y0 + 0.075, U_Y1 - 0.15                     # floor top and ceiling bottom of the upper deck
# 2026-10-03 (the user: variant 1, the book's ship, with the labels of variant 3; a tight way): the corridor gets a lining - a lowered
# ceiling 2.4 m over the floor with 45 degree chamfers to the walls (an octagon in section), frames, instrument niches, signs; the
# suit-up room and the lift zone a suspended ceiling 2.6 m over the floor.
W_YCC = W_YF + 2.4                                           # the corridor's ceiling (lower face)
W_CH = 0.3                                                   # its chamfers: from the walls at the door tops (W_YF + 2.1) to the ceiling
W_YCZ = W_YF + 2.6                                           # the suit-up room's and the lift zone's ceiling
W_CORR = 1.02                                              # the corridor's wall faces at x = +-1.02
W_DOWN_CORR = [(0.0, sv) for sv in (126.6, 128.6, 130.6, 132.6)]
W_DOWN_ZONE = [(-5.3, 128.6), (-2.6, 129.7)]
# the point lights: mesh frame (x, y, z) and colour (written to InteriorLayout.h for the module)
W_LIGHTS = [((0.0, 3.25, zs(127.6)), (0.62, 0.55, 0.45)), ((0.0, 3.25, zs(131.6)), (0.62, 0.55, 0.45)),
            ((-4.0, 3.45, zs(129.1)), (0.70, 0.50, 0.26)), ((-4.6, 3.45, zs(126.7)), (0.60, 0.52, 0.42))]


def _fxq(g, pts, n, uvs):
    idx = [g.vert(p_, n, uv) for p_, uv in zip(pts, uvs)]
    g.quad(*idx, n)


def _fx_floor(g, x, sv, R, clip, y=None, down=False):
    """A square decal on the floor (or, down=True, on the ceiling facing down) centred at (x, s), clipped to clip = (x0, x1, s0, s1)."""
    x0, x1, s0, s1 = max(x - R, clip[0]), min(x + R, clip[1]), max(sv - R, clip[2]), min(sv + R, clip[3])
    if x1 - x0 < 0.02 or s1 - s0 < 0.02:
        return
    y = (W_YC - 0.006 if down else W_YF + 0.006) if y is None else y
    U = lambda xv: (xv - (x - R)) / (2 * R)
    V = lambda s_: (s_ - (sv - R)) / (2 * R)
    pts = [(x0, y, zs(s0)), (x1, y, zs(s0)), (x1, y, zs(s1)), (x0, y, zs(s1))]
    uvs = [(U(x0), V(s0)), (U(x1), V(s0)), (U(x1), V(s1)), (U(x0), V(s1))]
    _fxq(g, pts, (0, -1, 0) if down else (0, 1, 0), uvs)


def _wall_gaps(xw):
    """Door openings (s0, s1, top) in the corridor wall at x = xw (+-1.0)."""
    return [(d[2] - d[3] / 2, d[2] + d[3] / 2, W_YF + 2.1 + 0.05) for d in ROOM_DOORS["U"] if d[0] == "x" and abs(d[1] - xw) < 0.01]


def _fx_wall_x(g, xf, nx, sa, sb, ya, yb_, v_top, gaps=(), u_rng=None):
    """A decal on the wall plane x = xf facing nx (+-1), over s sa..sb and heights ya..yb_; v = 0 at the height v_top and 1 at
    the far end of ya..yb_ from it (the decal fades away from v_top); u across sa..sb (or u_rng). Door openings are left out."""
    us0, us1 = u_rng or (sa, sb)
    far = ya if abs(v_top - yb_) < abs(v_top - ya) else yb_
    rects = [(sa, ya, sb, yb_)]
    for g0, g1, gt in gaps:
        rects = _subtract(rects, (g0, ya - 1.0, g1, gt))
    for a0, b0, a1, b1 in rects:
        if a1 - a0 < 0.02 or b1 - b0 < 0.02:
            continue
        U = lambda s_: (s_ - us0) / (us1 - us0)
        V = lambda yv: (yv - v_top) / (far - v_top)
        pts = [(xf, b0, zs(a0)), (xf, b0, zs(a1)), (xf, b1, zs(a1)), (xf, b1, zs(a0))]
        uvs = [(U(a0), V(b0)), (U(a1), V(b0)), (U(a1), V(b1)), (U(a0), V(b1))]
        _fxq(g, pts, (nx, 0, 0), uvs)


def _fx_wall_s(g, sf, ns, xa, xb, ya, yb_, v_top, gaps=()):
    """The same on a wall plane s = sf facing ns (+-1 along s); u across xa..xb."""
    far = ya if abs(v_top - yb_) < abs(v_top - ya) else yb_
    rects = [(xa, ya, xb, yb_)]
    for g0, g1, gt in gaps:
        rects = _subtract(rects, (g0, ya - 1.0, g1, gt))
    for a0, b0, a1, b1 in rects:
        if a1 - a0 < 0.02 or b1 - b0 < 0.02:
            continue
        U = lambda xv: (xv - xa) / (xb - xa)
        V = lambda yv: (yv - v_top) / (far - v_top)
        z = zs(sf)
        pts = [(a0, b0, z), (a1, b0, z), (a1, b1, z), (a0, b1, z)]
        uvs = [(U(a0), V(b0)), (U(a1), V(b0)), (U(a1), V(b1)), (U(a0), V(b1))]
        _fxq(g, pts, (0, 0, ns), uvs)


def _fx_floor_band(g, x0, x1, s0, s1, edge):
    """A contact shadow on the floor along a wall; edge: which side is the wall ('x0', 'x1', 's0', 's1')."""
    y = W_YF + 0.003                                         # the lowest decal layer (the pools lie 3..15 mm higher, each lamp its own)
    pts = [(x0, y, zs(s0)), (x1, y, zs(s0)), (x1, y, zs(s1)), (x0, y, zs(s1))]
    vv = {"x0": [0, 1, 1, 0], "x1": [1, 0, 0, 1], "s0": [0, 0, 1, 1], "s1": [1, 1, 0, 0]}[edge]
    _fxq(g, pts, (0, 1, 0), [(0.5, v_) for v_ in vv])


def build_watch():
    """The groups of the watch lighting (appended at the very end of the interior: the decals blend over what is drawn before)."""
    gl = Group("lamps_path", MAT["in_downlight"])
    fx = {k: Group(f"fx_{k}", MAT["in_fx"]) for k in ("pool", "amber", "wash", "ao", "glow")}
    for k, slot in zip(("pool", "amber", "wash", "ao", "cove", "glow"), range(15, 21)):
        if k in fx:
            fx[k].tex = slot
    C = (-W_CORR, W_CORR, LOB, D_S1 - 0.01)
    Z = (-6.77, -1.08, 125.68, 130.52)
    yw = W_YF + 2.1                                              # the corridor walls end at the chamfers
    # corridor: warm panels in the lowered ceiling, a pool under each, the scallop on the walls up to the chamfers
    for k_, (x, sv) in enumerate(W_DOWN_CORR):
        box(gl, (x - 0.28, W_YCC - 0.02, zs(sv - 0.3)), (x + 0.28, W_YCC + 0.005, zs(sv + 0.3)))
        _fx_floor(fx["pool"], x, sv, 1.35, C, y=W_YF + 0.006 + 0.003 * (k_ % 2))    # neighbouring pools overlap: different heights
        _fx_floor(fx["pool"], x, sv, 0.55, (-0.7, 0.7, LOB, D_S1), y=W_YCC - 0.026, down=True)   # the halo round the panel
        for sx in (-1, 1):
            xf = sx * (W_CORR - 0.006)
            _fx_wall_x(fx["wash"], xf, -sx, sv - 0.95, sv + 0.95, W_YF + 0.02, yw - 0.01, yw - 0.01, _wall_gaps(sx * 1.0))
    for sx in (-1, 1):
        gaps = _wall_gaps(sx * 1.0)
        _fx_wall_x(fx["ao"], sx * (W_CORR - 0.004), -sx, LOB, D_S1, W_YF, W_YF + 0.32, W_YF, gaps)       # contact shadow at the foot of the wall
        for a_, b_ in _uncovered(LOB, D_S1, [(g0, g1) for g0, g1, _ in gaps]):
            x0_, x1_ = (sx * W_CORR - 0.3, sx * W_CORR) if sx > 0 else (sx * W_CORR, sx * W_CORR + 0.3)
            _fx_floor_band(fx["ao"], x0_, x1_, a_, b_, "x1" if sx > 0 else "x0")
    for sv in (122.4, 124.8):                                    # the upper lobby: two dim lamps in its (high) ceiling, small pools between the wells
        box(gl, (-0.3 - 0.15, W_YC - 0.03, zs(sv - 0.15)), (-0.3 + 0.15, W_YC, zs(sv + 0.15)))
        _fx_floor(fx["pool"], -0.3, sv, 0.5, (-0.8, 0.2, D_S0, LOB), y=W_YF + 0.018)
    # the suit-up room and the lift zone: amber watch lamps, light over each stand
    for x, sv in W_DOWN_ZONE:
        box(gl, (x - 0.22, W_YCZ - 0.02, zs(sv - 0.22)), (x + 0.22, W_YCZ + 0.005, zs(sv + 0.22)))
        _fx_floor(fx["amber"], x, sv, 1.7, Z, y=W_YF + 0.012 + 0.003 * W_DOWN_ZONE.index((x, sv)))
        _fx_floor(fx["amber"], x, sv, 0.5, Z, y=W_YCZ - 0.026, down=True)
    eva = next(r for r in ROOMS if r["key"] == "eva")
    for cx_ in _STANDS:
        xl = max(cx_, eva["xc0"] + 0.3)
        box(gl, (xl - 0.1, W_YCZ - 0.02, zs(126.75 - 0.1)), (xl + 0.1, W_YCZ + 0.005, zs(126.75 + 0.1)))
        _fx_floor(fx["pool"], cx_, 126.55, 0.75, (eva["x0"] + 0.1, -1.08, 125.68, 127.4), y=W_YF + 0.021 + 0.003 * (_STANDS.index(cx_) % 2))
        _fx_wall_s(fx["wash"], 125.68 + 0.08 + 0.05 + 0.004, 1, cx_ - 0.45, cx_ + 0.45, W_YF + 0.02, W_YF + 2.6, W_YF + 2.6)   # back of the bay lit from above
    # contact shadows of the lift zone: the forward wall and the wall of the lift cell (door B left out)
    _fx_wall_s(fx["ao"], 130.52 - 0.004, -1, -6.77, -1.08, W_YF, W_YF + 0.32, W_YF)
    _fx_floor_band(fx["ao"], -6.77, -1.08, 130.22, 130.52, "s1")
    _fx_wall_x(fx["ao"], -6.77 + 0.004, 1, 127.4, 130.52, W_YF, W_YF + 0.32, W_YF, [(128.3, 129.7, W_YF + 2.6)])
    for a_, b_ in ((127.4, 128.3), (129.7, 130.52)):
        _fx_floor_band(fx["ao"], -6.77, -6.47, a_, b_, "x0")
    # the glow of the lift panel on its wall
    _fx_wall_s(fx["glow"], 130.52 - 0.008, -1, -7.0, -4.6, W_YF + 0.75, W_YF + 2.15, W_YF + 1.45)
    return [gl] + list(fx.values())


# ---- the lining of the way (variant 1 + the labels of variant 3)
SIGN_PX = 640                                                # atlas pixels per metre of a sign
SIGN_ATLAS = []                                              # (text, colour, x0, y0, x1, y1) in the atlas (1024 x 1024)
_SIGN_SHELF = [0, 0, 0]                                      # x, y, row height


def _sign_cell(text, w, h, col):
    W_, H_ = int(w * SIGN_PX), int(h * SIGN_PX)
    if _SIGN_SHELF[0] + W_ > 1024:
        _SIGN_SHELF[0], _SIGN_SHELF[1], _SIGN_SHELF[2] = 0, _SIGN_SHELF[1] + _SIGN_SHELF[2] + 4, 0
    x0, y0 = _SIGN_SHELF[0], _SIGN_SHELF[1]
    _SIGN_SHELF[0] += W_ + 4
    _SIGN_SHELF[2] = max(_SIGN_SHELF[2], H_)
    SIGN_ATLAS.append((text, col, x0, y0, x0 + W_, y0 + H_))
    return x0 / 1024.0, y0 / 1024.0, (x0 + W_) / 1024.0, (y0 + H_) / 1024.0


def _sign(g, gb, text, c, right, up, w, h, col=(214, 220, 226)):
    """A sign: a dark backing plate 8 mm off the wall (metal) and the label 4 mm in front of it. c = the centre ON the wall,
    right / up: unit vectors in the wall plane as the reader sees them; the normal points to the reader."""
    c, r, u = np.array(c, float), np.array(right, float), np.array(up, float)
    n = -np.cross(r, u)                                      # Orbiter is left-handed: towards the reader = -(right x up)
    _OBOX0(gb, c + n * 0.004, r, u, n, w / 2 + 0.01, h / 2 + 0.01, 0.004)
    u0, v0, u1, v1 = _sign_cell(text, w, h, col)
    cc = c + n * 0.012
    pts = [cc - r * w / 2 + u * h / 2, cc + r * w / 2 + u * h / 2, cc + r * w / 2 - u * h / 2, cc - r * w / 2 - u * h / 2]
    uvs = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
    idx = [g.vert(p_, n, uv) for p_, uv in zip(pts, uvs)]
    g.quad(*idx, n)


def build_lining():
    gw = Group("walls_lining", MAT["in_wall_w"])
    gc = Group("lining_ceil", MAT["in_ceil_w"])
    gm = Group("metal_lining", MAT["in_metal_w"])
    gn = Group("niche_path", MAT["in_guide_w"])
    gs = Group("signs_path", MAT["br_display"])
    yw = W_YF + 2.1
    s0, s1 = LOB, D_S1 - 0.015
    zc, hz = zs((s0 + s1) / 2), (s1 - s0) / 2
    xc = W_CORR - W_CH                                          # the flat ceiling between the chamfers
    # ---- corridor: lowered ceiling, chamfers, the header over the way into the lobby
    box(gc, (-xc, W_YCC, zs(s0)), (xc, W_YCC + 0.04, zs(s1)))
    L = math.hypot(W_CH, W_YCC - yw)
    for sx in (-1, 1):
        ca = np.array([sx * (W_CORR - W_CH / 2), (yw + W_YCC) / 2, zc])
        e = np.array([-sx * W_CH, W_YCC - yw, 0.0]) / L           # along the chamfer, up and in
        n_ = np.array([-sx * (W_YCC - yw), -W_CH, 0.0]) / L        # its face: down and in
        _OBOX0(gw, ca - n_ * 0.015, e, n_, (0, 0, 1), L / 2, 0.015, hz)
    box(gw, (-W_CORR, yw, zs(s0) - 0.02), (W_CORR, U_Y1 - 0.15, zs(s0) + 0.02))   # the header over the way from the lobby
    # ---- frames (flat rings 6 cm proud): posts where the wall is whole, the chamfers and the ceiling
    def frame(sv, sides, num):
        z = zs(sv)
        for sx in sides:
            if sx > 0:                                      # 5 cm proud: the guide light at the foot of the wall runs inside the post
                box(gm, (W_CORR - 0.05, W_YF, z - 0.05), (W_CORR, yw, z + 0.05))
            else:
                box(gm, (-W_CORR, W_YF, z - 0.05), (-W_CORR + 0.05, yw, z + 0.05))
            _sign(gs, gm, num, (sx * (W_CORR - 0.05), W_YF + 1.95, z), (0, 0, -sx), (0, 1, 0), 0.08, 0.05, (232, 200, 120))
        for sx in (-1, 1):
            ca = np.array([sx * (W_CORR - W_CH / 2), (yw + W_YCC) / 2, z])
            e = np.array([-sx * W_CH, W_YCC - yw, 0.0]) / L
            n_ = np.array([-sx * (W_YCC - yw), -W_CH, 0.0]) / L
            _OBOX0(gm, ca + n_ * 0.015, e, n_, (0, 0, 1), L / 2 + 0.02, 0.015, 0.05)
        box(gm, (-xc - 0.02, W_YCC - 0.03, z - 0.05), (xc + 0.02, W_YCC, z + 0.05))
    frame(125.68, (-1, 1), "ШП 126")
    frame(128.25, (1,), "ШП 128")
    frame(131.1, (-1, 1), "ШП 131")
    frame(133.3, (-1, 1), "ШП 133")
    # ---- instrument niches: soft teal panels low on the walls (the book's ship: dim glowing dials)
    for sx, sv in ((1, 131.75), (1, 133.65), (-1, 130.45), (-1, 133.65)):
        x = sx * W_CORR
        box(gm, (min(x, x - sx * 0.02), W_YF + 0.9, zs(sv - 0.26)), (max(x, x - sx * 0.02), W_YF + 1.3, zs(sv + 0.26)))
        xf = x - sx * 0.024
        q = [gn.vert((xf, W_YF + 1.25, zs(sv - 0.21)), (-sx, 0, 0)), gn.vert((xf, W_YF + 1.25, zs(sv + 0.21)), (-sx, 0, 0)),
             gn.vert((xf, W_YF + 0.95, zs(sv + 0.21)), (-sx, 0, 0)), gn.vert((xf, W_YF + 0.95, zs(sv - 0.21)), (-sx, 0, 0))]
        gn.quad(*q, (-sx, 0, 0))
    # ---- signs (variant 3: everything is labelled)
    ye = W_YF + 1.62
    _sign(gs, gm, "КАЮТ-КОМПАНИЯ", (W_CORR, ye, zs(126.28)), (0, 0, -1), (0, 1, 0), 0.62, 0.11)
    _sign(gs, gm, "ЗОНА ОТДЫХА", (W_CORR, ye, zs(130.86)), (0, 0, -1), (0, 1, 0), 0.44, 0.1)
    _sign(gs, gm, "ПОСТ УПРАВЛЕНИЯ  ▲", (W_CORR, ye, zs(132.45)), (0, 0, -1), (0, 1, 0), 1.0, 0.12, (240, 200, 120))
    _sign(gs, gm, "ЛАБОРАТОРИЯ · БИБЛИОТЕКА", (-W_CORR, ye, zs(130.72)), (0, 0, 1), (0, 1, 0), 0.68, 0.09)
    ce = np.array([-(W_CORR - W_CH / 2), (yw + W_YCC) / 2, zs(128.0)])         # on the chamfer over the way to the lift
    e = np.array([W_CH, W_YCC - yw, 0.0]) / L
    _sign(gs, gm, "ЛИФТ ШЛЮЗА · ЭКИПИРОВКА", ce, (0, 0, 1), e, 1.5, 0.17, (240, 200, 120))
    _sign(gs, gm, "ПОСТ УПРАВЛЕНИЯ", (0.0, W_YF + 2.34, zs(133.99) - 0.002), (1, 0, 0), (0, 1, 0), 0.76, 0.09, (240, 200, 120))
    _sign(gs, gm, "ЛИФТ · ШЛЮЗ 1", (-6.77, W_YF + 2.49, zs(AIRLOCK_S)), (0, 0, 1), (0, 1, 0), 0.7, 0.13, (240, 200, 120))
    for k_, cx_ in enumerate(sorted(_STANDS, reverse=True)):
        _sign(gs, gm, f"С-{k_ + 1}", (cx_, W_YF + 2.43, zs(125.68 + 0.65)), (-1, 0, 0), (0, 1, 0), 0.12, 0.05)
    # ---- the suit-up room and the lift zone: suspended ceiling with a frame grid
    eva = next(r for r in ROOMS if r["key"] == "eva")
    xe0 = _room_x_at(eva, W_YCZ)[0] + 0.02
    box(gc, (-6.77, W_YCZ, zs(127.4)), (-1.08, W_YCZ + 0.04, zs(130.52)))
    box(gc, (xe0, W_YCZ, zs(125.68)), (-1.08, W_YCZ + 0.04, zs(127.4)))
    for xv in (-5.9, -4.2, -2.5):
        box(gm, (xv - 0.04, W_YCZ - 0.03, zs(125.68)), (xv + 0.04, W_YCZ, zs(130.52)))
    for sv in (126.6, 128.0, 129.4):
        box(gm, (xe0 if sv < 127.4 else -6.77, W_YCZ - 0.03, zs(sv) - 0.04), (-1.08, W_YCZ, zs(sv) + 0.04))
    return [gw, gc, gm, gn, gs]


def _split_dark(groups):
    """The rooms behind the shut doors: their faces (the same test as _split_path) into dark groups."""
    tgt = {"walls_upper": ("walls_dark", "in_wall_d"), "crew_deck": ("walls_dark", "in_wall_d"),
           "crew_deck_floor": ("dark_floor", "in_floor_d"), "crew_deck_ceil": ("dark_ceil", "in_ceil_d"),
           "metal_upper": ("metal_dark", "in_metal_d"), "furn_upper": ("metal_dark", "in_metal_d"), "seat_upper": ("metal_dark", "in_metal_d"),
           "screen_upper": ("metal_dark", "in_metal_d"), "wet_upper": ("metal_dark", "in_metal_d")}
    return _split_faces(groups, DARK_BOXES, tgt)


def _split_path(groups):
    """Moves the faces of the upper deck that look into the way to the lift (the face centre stepped 5 cm along its normal lies in
    PATH_BOXES) out of the deck's walls, floor and ceiling into their own dim groups."""
    tgt = {"walls_upper": ("walls_path", "in_wall_w"), "crew_deck": ("walls_path", "in_wall_w"), "airlock": ("walls_path", "in_wall_w"),
           "crew_deck_floor": ("path_floor", "in_floor_w"), "crew_deck_ceil": ("path_ceil", "in_ceil_w"),
           "metal_upper": ("metal_path", "in_metal_w"), "furn_upper": ("metal_path", "in_metal_w"), "trim_upper": ("trim_path", "in_trim_w"),
           "guide_upper": ("guide_path", "in_guide_w")}
    return _split_faces(groups, PATH_BOXES, tgt)


def _split_faces(groups, boxes, tgt):
    """Moves the faces whose centre stepped 5 cm along its normal lies in `boxes` (x0, x1, s0, s1; the upper deck's height) from
    the groups named in `tgt` to the groups tgt[name] = (new name, material)."""
    new = {}
    z0 = zs(0.0)
    for g in groups:
        if g.name not in tgt:
            continue
        nm, mat = tgt[g.name]
        dst = new.setdefault(nm, Group(nm, MAT[mat]))
        V, N = g.v, g.n
        keep = []
        remap = {}
        for t in g.t:
            c = (V[t[0]] + V[t[1]] + V[t[2]]) / 3.0
            nrm = np.cross(V[t[1]] - V[t[0]], V[t[2]] - V[t[0]])
            ln = np.linalg.norm(nrm)
            q = c + (nrm / ln) * 0.05 if ln > 1e-12 else c
            sv = q[2] - z0
            inside = U_Y0 < q[1] < U_Y1 and any(b[0] <= q[0] <= b[1] and b[2] <= sv <= b[3] for b in boxes)
            if not inside:
                keep.append(t)
                continue
            tt = []
            for k in t:
                if k not in remap:
                    remap[k] = dst.vert(V[k], N[k])
                tt.append(remap[k])
            dst.t.append(tuple(tt))
        if remap:                                            # drop the moved vertices that no kept face uses
            used = sorted({k for t in keep for k in t})
            ix = {k: j for j, k in enumerate(used)}
            g.v = [g.v[k] for k in used]
            g.n = [g.n[k] for k in used]
            if g.uv:
                g.uv = [g.uv[k] for k in used]
            g.t = [tuple(ix[k] for k in t) for t in keep]
    return [x_ for x_ in new.values() if x_.v]


# ---- the lift (2026-10-03, the user: walk into the cabin, go down with it by its button, step out at the bottom by the command on its
# screen). Door B: two sliding leaves between the lift zone and the cabin in the cell (closed while the cabin is away or its air is
# being equalised), built open, the module slides them. The cabin's panel (DOWN / UP / OUT) and its screen on the forward inner wall of
# the cabin: interior groups that the module moves with the cabin (the cabin itself is the outside mesh, airlock_platform).
DOORB_HW = 0.7                                                    # half width of door B
CAB_PANEL = dict(x0=-8.55, x1=-7.15, y0=CAB_Y0 + 0.85, y1=CAB_Y0 + 2.15, scr=(0.08, 0.10, 0.92, 0.46), btn_u=(0.20, 0.50, 0.80), btn_v=0.64)   # big: read and pressed from a step away
CAB_BTN = []                                                      # (x, y, z) of the caps' fronts, stowed
CAB_SLOTS = {}


ILIFT = dict(x0=-2.72, x1=-1.38, s0=122.48, s1=124.72, stops=(L_Y0 + 0.075, U_Y0 + 0.075, 5.575), door_x=-1.3, door_s=123.6, door_hw=0.6)


def build_ilift_groups():
    """The inner lift's cab (VC groups, moved by the module): floor plate, lining of the three closed sides, roof, rail, light."""
    I = ILIFT
    y = I["stops"][0]
    x0, x1, z0, z1 = I["x0"], I["x1"], zs(I["s0"]), zs(I["s1"])
    gc = Group("ilift_cab", MAT["in_metal_w"])
    _OBOX0(gc, ((x0 + x1) / 2, y + 0.005, (z0 + z1) / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), (x1 - x0) / 2, 0.012, (z1 - z0) / 2)        # floor
    _OBOX0(gc, ((x0 + x1) / 2, y + 2.35, (z0 + z1) / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), (x1 - x0) / 2, 0.03, (z1 - z0) / 2)         # roof
    _OBOX0(gc, (x0 + 0.02, y + 1.18, (z0 + z1) / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.02, 1.17, (z1 - z0) / 2)                     # west wall
    for zz in (z0 + 0.02, z1 - 0.02):
        _OBOX0(gc, ((x0 + x1) / 2, y + 1.18, zz), (1, 0, 0), (0, 1, 0), (0, 0, 1), (x1 - x0) / 2, 1.17, 0.02)                         # end walls
        _OBOX0(gc, ((x0 + x1) / 2, y + 0.95, zz + (0.05 if zz < z1 - 1 else -0.05)), (1, 0, 0), (0, 1, 0), (0, 0, 1), (x1 - x0) / 2 - 0.15, 0.025, 0.025)   # rails
    _OBOX0(gc, (x0 + 0.07, y + 0.95, (z0 + z1) / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.025, 0.025, (z1 - z0) / 2 - 0.15)
    gl = Group("ilift_light", MAT["in_light"])
    _OBOX0(gl, ((x0 + x1) / 2, y + 2.31, (z0 + z1) / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.35, 0.01, 0.8)
    out = [gc, gl]
    for k in range(3):                                                # send buttons inside (moving with the cab): lower, living, technical
        gb = Group(f"ilift_send{k}", MAT[("in_green", "in_guide", "in_amber")[k]])
        _OBOX0(gb, (x0 + 0.06, y + 1.25, zs(I["door_s"]) + (k - 1) * 0.3), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.02, 0.07, 0.07)
        out.append(gb)
    xd = I["door_x"] + 0.06                                           # the doors: on the outer face of the east shaft wall, built shut
    for k, ys in enumerate(I["stops"]):
        hw = I["door_hw"]
        for nm, sgn in (("ilift_door", -1), ("ilift_doorN", 1)):    # two leaves meeting in the middle, each slides aside over the wall
            gd = Group(f"{nm}{k}", MAT["in_metal_w"])
            _OBOX0(gd, (xd, ys + 1.06, zs(I["door_s"]) + sgn * hw / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.025, 1.06, hw / 2 + 0.01)
            _OBOX0(gd, (xd + 0.03, ys + 1.06, zs(I["door_s"]) + sgn * 0.02), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.006, 0.9, 0.01)   # a lit edge strip
            out.append(gd)
        gk = Group(f"ilift_call{k}", MAT["in_guide"])                   # the call button on the forward end of the shaft (facing the lobby)
        _OBOX0(gk, (-1.55, ys + 1.2, zs(I["s1"]) + 0.1), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.08, 0.08, 0.02)
        out.append(gk)
    return out


CAP_SLOT = 43                                                     # in_btncaps.dds
ZONE_CAP_H, CAB_CAP_H = 0.020, 0.030                              # half sizes of the caps: 40 mm in the lift zone, 60 mm in the cabin


def _cap_uv(k, lit):
    x0 = (0 if lit else 1) * 128 + (256 if k >= 4 else 0)
    y0 = (k % 4) * 128
    return x0 / 512, y0 / 512, (x0 + 128) / 512, (y0 + 128) / 512


def _push_button(gm, gl, gd, k, bx, by, zf, h):
    """A square illuminated push button on a panel facing -z: the bezel (metal, 7 mm wide, 6 mm proud), the cap (lit and dark groups,
    12 mm proud, the legend on its face). zf = the panel's face; returns the z of the cap's front."""
    w = 0.007
    for (cx_, cy_, hx_, hy_) in ((bx, by + h + w / 2, h + w, w / 2), (bx, by - h - w / 2, h + w, w / 2), (bx - h - w / 2, by, w / 2, h), (bx + h + w / 2, by, w / 2, h)):
        _OBOX0(gm, (cx_, cy_, zf - .003), (1, 0, 0), (0, 1, 0), (0, 0, 1), hx_, hy_, .003)
    d = 0.012
    zfr = zf - d
    for g_, lit in ((gl, 1), (gd, 0)):
        u0, v0, u1, v1 = _cap_uv(k, lit)
        um, vm = (u0 + u1) / 2, (v0 + v1) / 2
        q = [g_.vert((bx - h, by + h, zfr), (0, 0, -1), (u0, v0)), g_.vert((bx + h, by + h, zfr), (0, 0, -1), (u1, v0)),
             g_.vert((bx + h, by - h, zfr), (0, 0, -1), (u1, v1)), g_.vert((bx - h, by - h, zfr), (0, 0, -1), (u0, v1))]
        g_.quad(*q, (0, 0, -1))
        for (ax, ay) in ((1, 0), (-1, 0), (0, 1), (0, -1)):              # the sides of the cap: the cap's edge colour
            if ax:
                pts = [(bx + ax * h, by - h, zfr), (bx + ax * h, by + h, zfr), (bx + ax * h, by + h, zf), (bx + ax * h, by - h, zf)]
            else:
                pts = [(bx - h, by + ay * h, zfr), (bx + h, by + ay * h, zfr), (bx + h, by + ay * h, zf), (bx - h, by + ay * h, zf)]
            idx = [g_.vert(p_, (ax, ay, 0), (u0 + 0.01, vm)) for p_ in pts]
            g_.quad(*idx, (ax, ay, 0))
        g_.tex = CAP_SLOT
    return zfr


def build_lift_groups():
    zl = zs(AIRLOCK_S)
    out = []
    # door B leaves: on the lift zone side of the cell wall (x -6.85), parked beside the opening, each slides DOORB_HW to the middle
    yb = U_Y0 + 0.075
    xl = -6.85 + 0.08 + 0.045
    for nm, z0, z1, ze in (("lift_doorB_a", zl - 2 * DOORB_HW - 0.02, zl - DOORB_HW + 0.02, zl - DOORB_HW + 0.02),
                           ("lift_doorB_b", zl + DOORB_HW - 0.02, zl + 2 * DOORB_HW + 0.02, zl + DOORB_HW - 0.02)):
        g = Group(nm, MAT["in_metal_w"])
        _OBOX0(g, (xl, yb + 1.07, (z0 + z1) / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.025, 1.07, (z1 - z0) / 2)
        out.append(g)
        ge = Group(nm + "_edge", MAT["in_amber"])                  # the meeting edge: an amber light bar (the door says it is a door)
        _OBOX0(ge, (xl + 0.03, yb + 1.07, ze), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.006, 0.9, 0.012)
        out.append(ge)
    # the cabin's panel on its forward inner wall (stowed pose), facing aft into the cabin
    P = CAB_PANEL
    zf = zl + CAB_HZ - 0.08 - 0.01
    gp = Group("cab_panel", MAT["br_display"])
    q = [gp.vert((P["x0"], P["y1"], zf), (0, 0, -1), (0.0, 0.0)), gp.vert((P["x1"], P["y1"], zf), (0, 0, -1), (1.0, 0.0)),
         gp.vert((P["x1"], P["y0"], zf), (0, 0, -1), (1.0, 1.0)), gp.vert((P["x0"], P["y0"], zf), (0, 0, -1), (0.0, 1.0))]
    gp.quad(*q, (0, 0, -1))
    X = lambda u: P["x0"] + (P["x1"] - P["x0"]) * u
    Y = lambda v: P["y1"] - (P["y1"] - P["y0"]) * v
    gs = Group("cab_screen", MAT["br_display"])
    u0, v0, u1, v1 = P["scr"]
    q = [gs.vert((X(u0), Y(v0), zf - 0.004), (0, 0, -1), (0.0, 0.0)), gs.vert((X(u1), Y(v0), zf - 0.004), (0, 0, -1), (1.0, 0.0)),
         gs.vert((X(u1), Y(v1), zf - 0.004), (0, 0, -1), (1.0, 1.0)), gs.vert((X(u0), Y(v1), zf - 0.004), (0, 0, -1), (0.0, 1.0))]
    gs.quad(*q, (0, 0, -1))
    gm = Group("cab_metal", MAT["in_metal_w"])
    _OBOX0(gm, ((P["x0"] + P["x1"]) / 2, (P["y0"] + P["y1"]) / 2, zf + 0.004), (1, 0, 0), (0, 1, 0), (0, 0, 1),
           (P["x1"] - P["x0"]) / 2 + 0.03, (P["y1"] - P["y0"]) / 2 + 0.03, 0.006)                   # the panel's frame plate on the wall
    caps = []
    CAB_BTN.clear()
    for k, u in enumerate(P["btn_u"]):                                                               # DOWN, UP, OUT: real push buttons
        bx, by = X(u), Y(P["btn_v"])
        gl_, gd_ = Group(f"cab_btn{k}_lit", MAT["br_display"]), Group(f"cab_btn{k}_dim", MAT["in_btn_cap"])
        zcap = _push_button(gm, gl_, gd_, 3 + k, bx, by, zf, CAB_CAP_H)
        caps += [gl_, gd_]
        CAB_BTN.append((bx, by, zcap))
    gda, gdb = Group("cab_door_a", MAT["in_metal_w"]), Group("cab_door_b", MAT["in_metal_w"])
    xd = LOCK_X1 - 0.155                                              # inside the hull-side wall (its inner face x1 - 0.08), 1 cm off the slid leaf of the outside mesh
    _OBOX0(gdb, (xd, CAB_Y0 + 0.12 + 1.04, zl - 0.995), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.012, 1.04, 0.335)
    # leaf a with the porthole (the user: one, in the door): an octagonal hole 22 cm, a metal ring, the glass
    za, zb, ya, yb2 = zl + 0.66, zl + 1.33, CAB_Y0 + 0.12, CAB_Y0 + 0.12 + 2.08
    pc, py_, pr = zl + 0.995, CAB_Y0 + 0.12 + 1.55, 0.11
    for a0, b0, a1, b1 in _subtract([(za, ya, zb, yb2)], (pc - pr, py_ - pr, pc + pr, py_ + pr)):
        _OBOX0(gda, (xd, (b0 + b1) / 2, (a0 + a1) / 2), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.012, (b1 - b0) / 2, (a1 - a0) / 2)
    ch = pr * 0.5858
    oc = [(pc - pr + ch, py_ - pr), (pc + pr - ch, py_ - pr), (pc + pr, py_ - pr + ch), (pc + pr, py_ + pr - ch),
          (pc + pr - ch, py_ + pr), (pc - pr + ch, py_ + pr), (pc - pr, py_ + pr - ch), (pc - pr, py_ - pr + ch)]
    sq = [(pc - pr, py_ - pr), (pc + pr, py_ - pr), (pc + pr, py_ + pr), (pc - pr, py_ + pr)]
    for k_ in range(4):                                                # the corners of the square hole filled: the hole is an octagon
        cz_, cy_ = sq[k_]
        a_, b_ = oc[(2 * k_ - 1) % 8], oc[2 * k_]
        prism(gda, [(cz_, cy_), a_, b_], (0, 0, 1), (0, 1, 0), (1, 0, 0), xd, 0.024)
    for k_ in range(8):                                                # the ring: proud on both faces
        (z0_, y0_), (z1_, y1_) = oc[k_], oc[(k_ + 1) % 8]
        L_ = math.hypot(z1_ - z0_, y1_ - y0_); ez, ey = (z1_ - z0_) / L_, (y1_ - y0_) / L_
        nz_, ny_ = -ey, ez
        cx_ = ((z0_ + z1_) / 2 - nz_ * 0.012 * (1 if (( (z0_ + z1_) / 2 - pc) * nz_ + ((y0_ + y1_) / 2 - py_) * ny_) < 0 else -1),
               (y0_ + y1_) / 2 - ny_ * 0.012 * (1 if (((z0_ + z1_) / 2 - pc) * nz_ + ((y0_ + y1_) / 2 - py_) * ny_) < 0 else -1))
        _OBOX0(gda, (xd, cx_[1], cx_[0]), (0, ey, ez), (0, ny_, nz_), (1, 0, 0), L_ / 2 + 0.006, 0.012, 0.022)
    ggl = Group("cab_door_glass", MAT["in_glass"])
    for sx_ in (-1, 1):                                                # the glass: both faces, the texture gives the tint and a glint
        ctr = ggl.vert((xd + sx_ * 0.004, py_, pc), (sx_, 0, 0), (0.5, 0.5))
        ring = [ggl.vert((xd + sx_ * 0.004, y_, z_), (sx_, 0, 0), (0.5 + (z_ - pc) / (2 * pr), 0.5 - (y_ - py_) / (2 * pr))) for z_, y_ in oc]
        for k_ in range(8):
            ggl.tri(ctr, ring[k_], ring[(k_ + 1) % 8], (sx_, 0, 0))
    ggl.tex = 44
    gde = Group("cab_door_edge", MAT["in_amber"])                     # the meeting edges glow amber (on leaf a: it rides with a)
    _OBOX0(gde, (xd + 0.014, CAB_Y0 + 1.16, zl + 0.665), (1, 0, 0), (0, 1, 0), (0, 0, 1), 0.003, 0.85, 0.006)
    out += [gp, gs, gm] + caps + [gda, gdb, gde, ggl]
    return out


def build_rooms():
    names = {"U": "upper", "L": "lower", "K": "keel"}
    walls = {d: Group(f"walls_{n}", MAT["in_wall"]) for d, n in names.items()}
    _FG.clear()
    for d, n in names.items():
        for cls, mat in (("furn", "in_furn"), ("seat", "in_seat"), ("metal", "in_metal"), ("wet", "in_wet"), ("light", "in_light"), ("screen", "in_screen"),
                         ("trim", "in_trim"), ("panel", "br_display"), ("green", "in_green"), ("amber", "in_amber"), ("red", "in_red"), ("guide", "in_guide"), ("ceil", "in_ceiling"), ("wip", "in_wip"), ("door", "in_door")):
            _FG[(d, cls)] = Group(f"{cls}_{n}", MAT[mat])
    for r in ROOMS:
        if r["open"]:
            continue
        _room_walls(walls[r["deck"]], r)
        _furnish(None, r)
    _lift_and_stair(_FG[("L", "metal")])
    _lights()
    _guides()
    for d_ in DOORS_INT:                                                       # compartment doors: chamfered corners and a frame
        if d_[0] not in ("x", "z"):
            continue
        sill_ = d_[3]
        dk_ = "U" if sill_ > 0.5 else ("L" if sill_ > -4.7 else "K")
        if d_[0] == "z" and not (d_[1] in (134.0, 121.6, 120.3)) and dk_ != "U":
            continue
        if d_[0] == "x" and d_[1] == -8.85:
            continue                                                           # the cabin's way out: the hull door is its frame
        if d_[0] == "z" and d_[1] == 134.0 and dk_ == "U":                    # the bridge door has its own sleeve (bridge_door_frame): no frame of ours;
            zp = zs(134.0) + 0.07 - 0.08 - 0.004                              # a plate 4 mm in front of the wall closes the gap between the sleeve and the hole
            ol = _door_outline()
            top = d_[3] + d_[5]
            hw = d_[4] / 2
            gw_ = walls[dk_]
            sc = 0.815 / BR_DOOR_HW
            arch = [(x_ * sc, y_) for x_, y_ in ol if y_ > BR_DOOR_YC - 1e-6]
            arch.sort()
            for (xa, ya), (xb, yb) in zip(arch[:-1], arch[1:]):
                if xb - xa < 1e-4:
                    continue
                q = [gw_.vert((xa, ya + 0.01, zp), (0, 0, -1)), gw_.vert((xb, yb + 0.01, zp), (0, 0, -1)), gw_.vert((xb, top, zp), (0, 0, -1)), gw_.vert((xa, top, zp), (0, 0, -1))]
                gw_.quad(*q, (0, 0, -1))
            for sx in (-1, 1):                                                 # the side strips beside the sleeve
                xa, xb = sorted((sx * 0.815, sx * hw))
                q = [gw_.vert((xa, d_[3] + 0.075, zp), (0, 0, -1)), gw_.vert((xb, d_[3] + 0.075, zp), (0, 0, -1)), gw_.vert((xb, top, zp), (0, 0, -1)), gw_.vert((xa, top, zp), (0, 0, -1))]
                gw_.quad(*q, (0, 0, -1))
            continue
        _door_frame(dk_, d_[0], (d_[1] if d_[0] == "x" else zs(d_[1]) + (0.07 if (d_[1] == 134.0 and dk_ == "U") else 0.0)), d_[2], d_[4], d_[5], sill_, 0.16, walls[dk_], d_[0] == "x" and d_[1] == -6.85)

    _SEAT_G[0] = None
    return [x_ for x_ in list(walls.values()) + list(_FG.values()) if x_.v]


# ---------------------------------------------------------------------------
# Command bridge capsule (TantraVC.msh): the inside of the drum R 4.6 about the cross axis x at (y BR_Y, s BR_S). Flat floor,
# dark vault, one big concave screen in three zones, the curved console with seats, the navigation table with the holo-projector
# emitters, anti-g seats. No door for now: the capsule is sealed. Everything is built in the lying pose (the capsule turned by 0).
BR_FLOOR_Y = BR_Y - 1.0
BR_FZ = math.sqrt(BR_R ** 2 - 1.0 ** 2)                      # half length of the flat floor along s
BR_CONS_R, BR_CONS_N, BR_CONS_CZ = 3.3, 11, -0.2             # curved console: radius, segments, centre offset (m, from the axis station)
BR_SCR_R, BR_SCR_Y0, BR_SCR_Y1 = 4.25, BR_FLOOR_Y + 1.15, BR_FLOOR_Y + 2.7       # bottom raised: the commander looks over the crest
BR_SCR_HX = 4.7                                               # the front part goes from side wall to side wall (no air beside it)
BR_SCR_RP = 7.0                                               # radius of the concave curve of the front part in plan
BR_SCR_X = (-BR_SCR_HX, -1.8, 1.8, BR_SCR_HX)                 # front zone borders (x): the centre zone is the biggest
BR_SCR_BACK = -0.83                                           # the side zones run on the flat end walls back to this z offset (~100 deg)
BR_SCR_RF = 1.5                                               # the front corners rounded: a fillet of this radius from the arc to the end wall (no crease)
BR_AST = (-3.3, -1.1, BR_FLOOR_Y + 0.7, BR_FLOOR_Y + 2.3)     # astronomer's screen on the port end wall: z0, z1 (offsets), y0, y1


def _scr_plan(x):
    """Plan curve of the screen: z offset from the axis station and the normal (towards the room) at x."""
    c = BR_SCR_R - BR_SCR_RP
    d = c + math.sqrt(BR_SCR_RP ** 2 - x ** 2)
    return d, (-x / BR_SCR_RP, 0.0, -(d - c) / BR_SCR_RP)
def _scr_fillet():
    """The corner fillet (starboard; mirrored): its centre (x, z offset), the tangent point on the arc (x), on the end wall (z)."""
    c, rf = BR_SCR_R - BR_SCR_RP, BR_SCR_RF
    zf = c + math.sqrt((BR_SCR_RP - rf) ** 2 - (BR_SCR_HX - rf) ** 2)
    xt = (BR_SCR_HX - rf) * BR_SCR_RP / (BR_SCR_RP - rf)
    return (BR_SCR_HX - rf, zf), xt, zf


def _scr_side_plan(sg, n_wall=10, n_fil=10):
    """A side zone's plan (sg -1 port, +1 starboard): along the end wall from BR_SCR_BACK, then round the fillet to the arc."""
    (fx, fz), xt, zf = _scr_fillet(); c = BR_SCR_R - BR_SCR_RP
    pl = [(sg * BR_SCR_HX, BR_SCR_BACK + (zf - BR_SCR_BACK) * j / n_wall, (-sg * 1.0, 0.0, 0.0)) for j in range(n_wall + 1)]
    a1 = math.atan2((c + math.sqrt(BR_SCR_RP ** 2 - xt ** 2)) - fz, xt - fx)          # from the wall (angle 0 about the centre) to the arc
    for j in range(1, n_fil + 1):
        a = a1 * j / n_fil
        px, pz = fx + BR_SCR_RF * math.cos(a), fz + BR_SCR_RF * math.sin(a)
        pl.append((sg * px, pz, (-sg * math.cos(a), 0.0, -math.sin(a))))
    return pl
BR_SCR_TOP = BR_FLOOR_Y + 4.0                                 # the screen goes up the vault to this height (no air behind it)
BR_SCR_RV = BR_R - 0.13                                       # radius of the screen part lying on the vault (inside the ribs)
BR_SCR_EYE = (0.0, BR_FLOOR_Y + 1.3, 0.0)                     # design eye (x, y, z offset): the outside view is projected from it
BR_SCR_CAMS = []                                              # per zone: (yaw deg, pitch deg, vfov deg, width/height): filled by the generator


def _screen_cols(plan, zc, rows=8):
    """The points of a screen zone: a vertical wall along `plan` (columns (x, z offset, normal)) up to the vault, then along
    the vault (r BR_SCR_RV about the drum axis) up to BR_SCR_TOP."""
    cols = []
    for x, d, pn in plan:
        y_int = BR_Y + math.sqrt(max(BR_SCR_RV ** 2 - d ** 2, 0.0))          # where the vertical part meets the vault
        y_v = min(y_int, BR_SCR_TOP)
        pts = []
        for k in range(rows + 1):                                            # vertical part
            y = BR_SCR_Y0 + (y_v - BR_SCR_Y0) * k / rows
            pts.append(((x, y, zc + d), pn))
        f0 = math.asin(min((y_v - BR_Y) / BR_SCR_RV, 1.0)); f1 = max(f0, math.asin(min((BR_SCR_TOP - BR_Y) / BR_SCR_RV, 1.0)))
        for k in range(1, rows + 1):                                         # part on the vault (degenerate where the wall reaches the top)
            if y_int >= BR_SCR_TOP:
                pts.append(pts[rows]); continue
            f = f0 + (f1 - f0) * k / rows
            pts.append(((x, BR_Y + BR_SCR_RV * math.sin(f), zc + BR_SCR_RV * math.cos(f)), (0.0, -math.sin(f), -math.cos(f))))
        cols.append(pts)
    return cols


def _screen_cam(P):
    """The camera (frame, tangent half extents) that sees the points P (relative to the design eye)."""
    az = np.arctan2(P[:, 0], P[:, 2]); yaw = (az.min() + az.max()) / 2
    el = np.arctan2(P[:, 1], np.hypot(P[:, 0], P[:, 2])); pitch = (el.min() + el.max()) / 2
    f = np.array([math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch)])
    u = np.array([-math.sin(yaw) * math.sin(pitch), math.cos(pitch), -math.cos(yaw) * math.sin(pitch)])
    r = np.array([math.cos(yaw), 0.0, -math.sin(yaw)])
    df, du, dr = P @ f, P @ u, P @ r
    assert df.min() > 0.1, "screen zone behind the design eye"
    tv, th = np.abs(du / df).max() * 1.01, np.abs(dr / df).max() * 1.01
    return yaw, pitch, f, u, r, tv, th


def _screen_zone(g, plan, zc, slot, rows=8, cam=None):
    """One zone of the big screen (_screen_cols). UV = perspective projection of the outside view from the design eye, so the
    screen works as a slot into the world. Its camera (yaw, pitch, fov) is computed here, or `cam` (_screen_cam) is shared
    with other zones - one camera, one picture: the client renders one custom camera per turn, so fewer cameras = fresher."""
    E = np.array([BR_SCR_EYE[0], BR_SCR_EYE[1], zc + BR_SCR_EYE[2]])
    cols = _screen_cols(plan, zc, rows)
    if cam is None:
        cam = _screen_cam(np.array([p for c in cols for p, _ in c]) - E)
        yaw, pitch, f, u, r, tv, th = cam
        BR_SCR_CAMS.append((math.degrees(yaw), math.degrees(pitch), 2 * math.degrees(math.atan(tv)), th / tv, slot))
    yaw, pitch, f, u, r, tv, th = cam
    idx = []
    for c in cols:
        col = []
        for p, nrm in c:
            q = np.array(p) - E
            col.append(g.vert(p, nrm, (0.5 + 0.5 * (q @ r) / (q @ f) / th, 0.5 - 0.5 * (q @ u) / (q @ f) / tv)))
        idx.append(col)
    for i in range(len(plan) - 1):
        for k in range(2 * rows):
            if np.linalg.norm(np.array(cols[i][k + 1][0]) - np.array(cols[i][k][0])) < 1e-4: continue
            g.quad(idx[i][k], idx[i + 1][k], idx[i + 1][k + 1], idx[i][k + 1], np.array(cols[i][k][1]))
# The bridge console «Полумесяц φ» (the user's choice, 2026-10-03; Tantra_Design/bridge_fork_mockup.html, DESIGN_LOCAL.md):
# one body for every place, swept along a centripetal Catmull-Rom path through the commander's bay (his deepest niche) and
# the crew's arcs on the golden ellipse (half axes 3.20 and 1.98 m). The section: front wall (a knee recess at the seats),
# shelf, raked face, top, back wall. The commander: one concave screen over his face (+-42 deg), the attitude keys on the
# shelf in front of it; at his sides the wings (the same section round his eye, 46..112 deg) with a button shelf, a screen
# on the leaning riser and a curved monitor on the top edge that folds back (развёрнут / свёрнут / сложен).
BR_CMD = (0.0, 0.75)                                          # the commander's seat axis (x, z offset): his eye ~3.5 m from the screen
BR_BAY = [(0, .52, .74, .18, .16), (-14, .52, .74, .18, .16), (-28, .52, .74, .17, .16), (-40, .53, .74, .16, .16), (-52, .57, .95, .18, .12)]
#          angle from his front (- port), inner radius, top H, shelf depth D, top width (the port half; mirrored)
BR_ELL = (3.2, 3.2 / ((1 + math.sqrt(5)) / 2), -0.2)          # the crew's ellipse: half axes (ratio phi), centre z
BR_WING = (.50, .30, .95, .14, .12)                           # wings: inner radius, shelf D, top H, top width, riser lean
BR_MON = (.94, .95, .52, 23.0)                                # curved monitors: radius, bottom y, height, half span (deg) round 80 deg
BR_MON_FOLD = (1.32, 1.62)                                    # свёрнут, сложен (rad, leaning back about the bottom edge)
# The panels (the user, 2026-10-04, Tantra_Design/tantra_bridge_panels_mockup.html, tantra_bridge_3d_preview.html): at his sides
# a curved panel 1.16 x 0.96 m in place of the monitor, the riser and the button shelf (three MFDs over a screen); in front
# one screen 1.50 x 0.65 m over the key shelf, the bays under it lowered to the shelf.
BR_PANEL = (1.00, .72, .96, 1.16, 72.0, 12.0)                 # side panels: radius from his axis, bottom y, height, width, middle angle, lean back (deg)
BR_FRONT = (1.25, .74, .65, 1.50, 18.0)                       # the front screen: radius, bottom y, height, width, lean back (deg)
TOUCH_FACETS = []                                             # flat pieces of the curved touch screens: (c, ex, up, n, w, h, screen, u0, u1, v0, v1)
TOUCH_SLOT_EXT = 46                                           # 46 left riser, 47 attitude keys, 48 / 49 the wing shelves (37..40: see TOUCH_SLOT0)
SIDE_FOLD = []                                                # per monitor: pivot (bottom edge middle), axis (fold direction: positive angle)


def _cres_crew_seat():
    """The starboard crew seat at its console: x, z offset, facing (toward the ellipse's arc)."""
    a, b, cz = BR_ELL; th = math.radians(52)
    x, z = a * math.sin(th), cz + b * math.cos(th)
    n = np.array([x / a ** 2, (z - cz) / b ** 2]); n /= np.linalg.norm(n)
    return x - n[0] * .78, z - n[1] * .78, n[0], n[1]


def _cr_path(ctrl, n):
    """Centripetal Catmull-Rom through ctrl (x, z), resampled evenly by length (as three.js getSpacedPoints): points, tangents."""
    C = [np.array(c, float) for c in ctrl]
    C = [2 * C[0] - C[1]] + C + [2 * C[-1] - C[-2]]
    dense = []
    for i in range(1, len(C) - 2):
        p0, p1, p2, p3 = C[i - 1], C[i], C[i + 1], C[i + 2]
        t0 = 0.0; t1 = t0 + max(np.linalg.norm(p1 - p0), 1e-6) ** .5; t2 = t1 + max(np.linalg.norm(p2 - p1), 1e-6) ** .5; t3 = t2 + max(np.linalg.norm(p3 - p2), 1e-6) ** .5
        for k in range(60):
            t = t1 + (t2 - t1) * k / 60
            a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1; a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2
            a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3
            b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2; b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3
            dense.append((t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2)
    dense.append(C[-2])
    D = np.array(dense); L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(D, axis=0), axis=1))])
    P = np.array([np.interp(np.linspace(0, L[-1], n + 1), L, D[:, k]) for k in range(2)]).T
    T = np.gradient(P, axis=0); T /= np.linalg.norm(T, axis=1)[:, None]
    return P, T


def _strip(g, A, B, ref, uvA=None, uvB=None):
    """Quads between two rows of points A[k], B[k] (k along), facing ref (a vector or a list per k); optional uv rows."""
    va = [g.vert(p, ref if np.ndim(ref) == 1 else ref[k], None if uvA is None else uvA[k]) for k, p in enumerate(A)]
    vb = [g.vert(p, ref if np.ndim(ref) == 1 else ref[k], None if uvB is None else uvB[k]) for k, p in enumerate(B)]
    for k in range(len(A) - 1):
        r = ref if np.ndim(ref) == 1 else ref[k]
        if np.linalg.norm(np.cross(A[k + 1] - A[k], B[k] - A[k])) < 1e-9 and np.linalg.norm(np.cross(B[k + 1] - B[k], B[k] - A[k])) < 1e-9: continue
        g.quad(va[k], va[k + 1], vb[k + 1], vb[k], np.asarray(r, float))


def _facets(screen, A, B, uA, vA, vB, step, toward):
    """Touch facets over a curved band: bottom row A, top row B (texture v down: vB at the top), u per column; n toward the viewer."""
    for k in range(0, len(A) - 1, step):
        k2 = min(k + step, len(A) - 1)
        ex = A[k2] - A[k]; w = np.linalg.norm(ex); ex = ex / w
        up = ((B[k] - A[k]) + (B[k2] - A[k2])) / 2; up = up - ex * (up @ ex); h = np.linalg.norm(up); up = up / h
        n = np.cross(ex, up); n = n / np.linalg.norm(n)
        if n @ toward < 0: n = -n
        c = (A[k] + A[k2] + B[k] + B[k2]) / 4
        TOUCH_FACETS.append((c, ex, up, n, w, h, screen, uA[k], uA[k2], (vB[k] + vB[k2]) / 2, (vA[k] + vA[k2]) / 2))


def _crescent_console(G):
    """The console of the bridge (see BR_CMD). G: the groups (tub, cons, face, leds, metal, mfd_unit, screen slots)."""
    zc = zs(BR_S); F = BR_FLOOR_Y; cx, cz = BR_CMD
    tub, cons, face, leds = G["tub"], G["cons"], G["face"], G["leds"]
    up_ = np.array([0.0, 1.0, 0.0])
    P3 = lambda a, r: np.array([cx + math.sin(math.radians(a)) * r, cz + math.cos(math.radians(a)) * r])
    W3 = lambda q, y: np.array([q[0], F + y, zc + q[1]])
    # the plan: the bay (port half), a shallow junction, the crew's arc; mirrored to starboard
    bayL = [(*P3(a, r), H, D, Tw) for a, r, H, D, Tw in BR_BAY]
    A_, B_, CZ_ = BR_ELL; arc = []
    for t in (30, 40, 50, 60, 70, 80):
        th = math.radians(t); arc.append((-A_ * math.sin(th), CZ_ + B_ * math.cos(th), .95 if t < 40 else 1.16 if t < 72 else 1.08, .30))
    e, f = bayL[-1], arc[0]
    j = ((e[0] + f[0]) / 2 - .08, (e[1] + f[1]) / 2 + .06, .95, .08, .06)
    left = bayL + [j] + [(q[0], q[1], q[2], q[3], .14 if k == 0 else .42) for k, q in enumerate(arc)]
    right = [(-c[0],) + tuple(c[1:]) for c in left]
    ctrl = right[1:][::-1] + left
    N = 260
    P, T = _cr_path([(c[0], c[1]) for c in ctrl], N)
    attr = []
    for p in P:                                                                  # H, D, Tw: blended from the nearest control points
        w = np.array([1 / (math.hypot(p[0] - c[0], p[1] - c[1]) + .02) ** 3 for c in ctrl])
        attr.append(tuple(float(w @ np.array([c[k] for c in ctrl]) / w.sum()) for k in (2, 3, 4)))
    n0 = np.array([-T[N // 2][1], T[N // 2][0]]); sgn = 1.0 if n0 @ (np.array(BR_CMD) - P[N // 2]) > 0 else -1.0
    Nn = np.stack([-T[:, 1] * sgn, T[:, 0] * sgn], axis=1)                     # toward the people
    seats = [(BR_SEAT_AT[i][0], BR_SEAT_AT[i][1], BR_SEAT_DIR[i]) for i in range(3)]

    def knee(i):                                                                 # the knee recess in front of a seat (0.40 deep)
        best = 0.0
        for sx, sz, (fx, fz) in seats:
            d = P[i] - np.array([sx, sz]); along = d @ np.array([fx, fz]); lat = abs(d[0] * fz - d[1] * fx)
            if along > 0: best = max(best, .40 * min(1.0, max(0.0, (.38 - lat) / .08)))
        return best

    def pt(i, d, y):
        return W3(P[i] - Nn[i] * d, y)
    prof = lambda i: [(knee(i), 0), (knee(i), .66), (0, .66), (0, .70), (attr[i][1], .72), (attr[i][1] + .18, attr[i][0]),
                      (attr[i][1] + .18 + attr[i][2], attr[i][0]), (attr[i][1] + .18 + attr[i][2], 0)]
    inw = [np.array([Nn[i][0], 0.0, Nn[i][1]]) for i in range(N + 1)]
    refs = [inw, [-up_] * (N + 1), inw, [up_] * (N + 1), [v + up_ * .6 for v in inw], [up_] * (N + 1), [-v for v in inw]]
    grp = [tub, tub, tub, cons, face, cons, tub]
    for k in range(7):                                                           # knee wall, under the shelf, lip, shelf, face, top, back
        _strip(grp[k], [pt(i, *prof(i)[k]) for i in range(N + 1)], [pt(i, *prof(i)[k + 1]) for i in range(N + 1)], refs[k])
    for i, sg in ((0, -1.0), (N, 1.0)):                                          # end caps
        ring = [pt(i, *q) for q in prof(i)]; c = sum(ring) / len(ring); nrm = np.array([T[i][0], 0.0, T[i][1]]) * sg
        ci = tub.vert(c, nrm); vs = [tub.vert(v, nrm) for v in ring]
        for k in range(len(vs)): tub.tri(ci, vs[k], vs[(k + 1) % len(vs)], nrm)
    for i in range(0, N, 3):                                                     # light lines: the shelf edge, the top front edge
        j2 = min(i + 3, N)
        tube(leds, pt(i, -.004, .705), pt(j2, -.004, .705), .005, n=5)
        tube(leds, pt(i, attr[i][1] + .18, attr[i][0] + .004), pt(j2, attr[j2][1] + .18, attr[j2][0] + .004), .005, n=5)
    for i in range(0, N, 4):                                                     # collision along the body (the knee recess open below)
        j2 = min(i + 4, N); back = max(attr[i][1], attr[j2][1]) + .18 + max(attr[i][2], attr[j2][2]); dk = min(knee(i), knee(j2))
        lo_ = np.array([pt(ii, d, 0) for ii in (i, j2) for d in (dk, back)]); hi_ = np.array([pt(ii, d, 0) for ii in (i, j2) for d in (0, back)])
        _coll("bridge", (lo_[:, 0].min(), F, lo_[:, 2].min()), (lo_[:, 0].max(), F + .66, lo_[:, 2].max()))
        _coll("bridge", (hi_[:, 0].min(), F + .66, hi_[:, 2].min()), (hi_[:, 0].max(), F + max(attr[i][0], attr[j2][0]), hi_[:, 2].max()))
    plen = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))])
    at = lambda a: int(np.argmin(np.linalg.norm(P - P3(a, .52), axis=1)))
    toward_cmd = lambda q: W3(np.array(BR_CMD), 1.2) - q
    del TOUCH_PLACES[:]; del TOUCH_FACETS[:]; del SIDE_DISP[:]; del SIDE_FOLD[:]
    places = {}

    def place(k, A, B, uA, vA, vB, step, slot, g):                               # a touch screen: its facets and its middle rectangle
        n0_ = len(TOUCH_FACETS); _facets(k, A, B, uA, vA, vB, step, toward_cmd(A[len(A) // 2]))
        mid = TOUCH_FACETS[n0_ + (len(TOUCH_FACETS) - n0_) // 2]
        wsum = sum(np.linalg.norm((A[i + 1] + B[i + 1]) / 2 - (A[i] + B[i]) / 2) for i in range(len(A) - 1))
        hmax = max(np.linalg.norm(B[i] - A[i]) / max(abs(vA[i] - vB[i]), 1e-3) for i in range(len(A)))
        places[k] = (mid[0], mid[1], mid[2], mid[3], wsum, hmax, slot, g)

    def band(name, slot, A, B, uA, vA, vB, toward):                              # a self-lit textured band (the texture drawn in the game)
        g = Group(name, MAT["br_display"]); g.tex = slot
        _strip(g, A, B, [toward] * len(A), [(u, v) for u, v in zip(uA, vA)], [(u, v) for u, v in zip(uA, vB)])
        G["out"].append(g); return g
    # THE COMMANDER: one big screen in front (BR_FRONT), a curved band round his axis leaning back, over the lowered bays
    radv = lambda a: np.array([math.sin(math.radians(a)), 0.0, math.cos(math.radians(a))])
    Rf, yf, hf, wf, lf = BR_FRONT; hsf = math.degrees(wf / Rf) / 2; nf = 40; tf = hf * math.tan(math.radians(lf))
    af = [-hsf + 2 * hsf * i / nf for i in range(nf + 1)]
    A = [W3(P3(a, Rf), yf) for a in af]; B = [W3(P3(a, Rf + tf), yf + hf) for a in af]
    uA = [i / nf for i in range(nf + 1)]; vA = [1.0] * (nf + 1); vB = [0.0] * (nf + 1)
    g = band("bridge_centre", TOUCH_SLOT0 + 2, A, B, uA, vA, vB, -radv(0) + up_ * .3)
    place(2, A, B, uA, vA, vB, 2, TOUCH_SLOT0 + 2, g)
    fh = Group("bridge_frontdisp", MAT["mechanism"]); G["out"].append(fh)      # its housing: the back shell, the rims
    Ab = [W3(P3(a, Rf + .03), yf - .015) for a in af]; Bb = [W3(P3(a, Rf + tf + .03), yf + hf + .015) for a in af]
    _strip(fh, Ab, Bb, [radv(a) for a in af])
    _strip(fh, [W3(P3(a, Rf - .004), yf - .015) for a in af], Ab, [-up_] * (nf + 1)); _strip(fh, [W3(P3(a, Rf + tf - .004), yf + hf + .015) for a in af], Bb, [up_] * (nf + 1))
    for a, sg in ((af[0], -1), (af[-1], 1)):
        tg = np.array([math.cos(math.radians(a)), 0.0, -math.sin(math.radians(a))]) * sg
        vs = [fh.vert(v, tg) for v in (W3(P3(a, Rf - .004), yf - .015), W3(P3(a, Rf + .03), yf - .015), W3(P3(a, Rf + tf + .03), yf + hf + .015), W3(P3(a, Rf + tf - .004), yf + hf + .015))]
        fh.quad(*vs, tg)
    # the attitude keys on the shelf in front of it (+-30 deg): the texture's top is the far edge
    iL, iR = at(-30), at(30); st = 1 if iR > iL else -1; idx = list(range(iL, iR + st, st)); n = len(idx) - 1
    A = [pt(i, .02, .723) for i in idx]; B = [pt(i, attr[i][1] - .02, .723) for i in idx]
    uA = [k / n for k in range(n + 1)]
    g = band("bridge_keys", TOUCH_SLOT_EXT + 1, A, B, uA, [1.0] * (n + 1), [0.0] * (n + 1), up_)
    place(5, A, B, uA, [1.0] * (n + 1), [0.0] * (n + 1), 3, TOUCH_SLOT_EXT + 1, g)
    # THE WINGS at his sides: the same section round his eye; the button shelf, the screen on the leaning riser, the curved monitor
    r0, D, Hw, Tw, RB = BR_WING
    for side in (-1, 1):
        A0, A1, N2 = side * 46, side * 112, 40
        pt2 = lambda a, d, y: W3(P3(a, r0 + d), y)
        rad = lambda a: np.array([math.sin(math.radians(a)), 0.0, math.cos(math.radians(a))])
        prof2 = [(0, 0), (0, .70), (D, .72), (D, 0)]                            # the shelf only: the panel stands behind it
        angs = [A0 + (A1 - A0) * i / N2 for i in range(N2 + 1)]
        refs2 = [[-rad(a) for a in angs], [up_] * (N2 + 1), [rad(a) for a in angs]]
        for k, g2 in enumerate((tub, cons, tub)):
            _strip(g2, [pt2(a, *prof2[k]) for a in angs], [pt2(a, *prof2[k + 1]) for a in angs], refs2[k])
        for a, sg in ((A0, -1.0), (A1, 1.0)):                                    # end caps
            ring = [pt2(a, *q) for q in prof2]; c = sum(ring) / len(ring)
            tg = np.array([math.cos(math.radians(a)), 0.0, -math.sin(math.radians(a))]) * sg * side
            ci = tub.vert(c, tg); vs = [tub.vert(v, tg) for v in ring]
            for k in range(len(vs)): tub.tri(ci, vs[k], vs[(k + 1) % len(vs)], tg)
        for i in range(0, N2, 2):
            tube(leds, pt2(angs[i], -.004, .705), pt2(angs[min(i + 2, N2)], -.004, .705), .005, n=5)
        for i in range(0, N2, 5):                                                # collision
            q = np.array([pt2(angs[ii], d, 0) for ii in (i, min(i + 5, N2)) for d in (0, D)])
            _coll("bridge", (q[:, 0].min(), F, q[:, 2].min()), (q[:, 0].max(), F + .72, q[:, 2].max()))
        kk = 6 if side < 0 else 7; kr = 4 if side < 0 else 3                   # the shelf keys and the riser are gone (the panels):
        for k_, sl_ in ((kk, TOUCH_SLOT_EXT + kk - 4), (kr, TOUCH_SLOT_EXT if side < 0 else TOUCH_SLOT0 + 3)):   # places out of reach, numbering kept
            places[k_] = (np.array([side * 9.0, F - 9.0, zc]), np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), np.array([0, 0, -1.0]), .40, .20, sl_, None)
        # the curved monitor on the top edge: an arc round his eye, the centre at eye height; it folds back about its bottom edge
        R0, yb, h, wp, mpa, lean = BR_PANEL; hs = math.degrees(wp / R0) / 2; mid = side * mpa; k = 0 if side < 0 else 1; tl = h * math.tan(math.radians(lean))
        ma = [mid - hs + 2 * hs * i / 24 for i in range(25)]                    # left to right as seen (port: back -> front)
        A = [W3(P3(a, R0), yb) for a in ma]; B = [W3(P3(a, R0 + tl), yb + h) for a in ma]
        uA = [i / 24 for i in range(25)]
        scr = band(f"bridge_sidescr{k}", TOUCH_SLOT0 + k, A, B, uA, [1.0] * 25, [0.0] * 25, -rad(mid))
        place(k, A, B, uA, [1.0] * 25, [0.0] * 25, 3, TOUCH_SLOT0 + k, scr)
        hous = Group(f"bridge_sidedisp{k}", MAT["mechanism"]); G["out"].append(hous)
        Ab = [W3(P3(a, R0 + .025), yb - .012) for a in ma]; Bb = [W3(P3(a, R0 + tl + .025), yb + h + .012) for a in ma]
        _strip(hous, Ab, Bb, [rad(a) for a in ma])                               # the back shell
        _strip(hous, [W3(P3(a, R0 - .004), yb - .012) for a in ma], Ab, [-up_] * 25); _strip(hous, [W3(P3(a, R0 + tl - .004), yb + h + .012) for a in ma], Bb, [up_] * 25)
        for a, sg in ((ma[0], -1), (ma[-1], 1)):
            q0, q1 = W3(P3(a, R0 - .004), yb - .012), W3(P3(a, R0 + tl + .025), yb + h + .012)
            tg = np.array([math.cos(math.radians(a)), 0.0, -math.sin(math.radians(a))]) * sg
            vs = [hous.vert(v, tg) for v in (q0, W3(P3(a, R0 + .025), yb - .012), q1, W3(P3(a, R0 + tl - .004), yb + h + .012))]
            hous.quad(*vs, tg)
        piv = W3(P3(mid, R0 + .01), yb)
        ax = np.array([math.cos(math.radians(mid)), 0.0, -math.sin(math.radians(mid))])   # the tangent at the middle
        top = W3(P3(mid, R0), yb + h) - piv
        Rk = lambda v, a_: v * math.cos(a_) + np.cross(ax, v) * math.sin(a_) + ax * (ax @ v) * (1 - math.cos(a_))   # Rodrigues
        if (Rk(top, .3) - top) @ rad(mid) < 0: ax = -ax                          # a positive angle leans the top away from him
        SIDE_DISP.append((scr, hous)); SIDE_FOLD.append((piv, ax))
    # THE CREW: the seats at their bays, three MFD units on the face in front of each (real Orbiter MFDs, touch)
    for si, modes in ((2, (2, 1, 6)), (1, (9, 1, 3))):                        # port: the engineer (SURFACE, ORBIT, DOCKING); starboard: the navigator (TRANSFER, ORBIT, MAP)
        sx, sz, (fx, fz) = seats[si]
        i0 = int(np.argmin(np.linalg.norm(P - np.array([sx + fx * .78, sz + fz * .78]), axis=1)))
        for o, mode in zip((-1, 0, 1), modes):
            i = int(np.argmin(np.abs(plen - (plen[i0] + o * .45))))                 # 0.45 m apart along the face
            a, b = pt(i, attr[i][1], .72), pt(i, attr[i][1] + .18, attr[i][0])
            upv = (b - a) / np.linalg.norm(b - a); nrm = np.cross(np.array([T[i][0], 0.0, T[i][1]]), upv); nrm /= np.linalg.norm(nrm)
            if nrm @ inw[i] < 0: nrm = -nrm
            ex = np.cross(nrm, upv)                                              # right as seen (the mesh frame is left-handed)
            G["mfd_unit"]((a + b) / 2 + nrm * .006, ex, upv, nrm, mode, .40)
    TOUCH_PLACES[:] = [places[k] for k in range(8)]


# ---- THE BRIDGE CONSOLE, variant 7 of the bridge mockup (Tantra_Design/tantra_bridge_3d_preview.html, bridge_variants/v7.js).
# THE COMMANDER: one curved desk round his axis (BR_CMD), its top sunk in three sloped instrument bands, the glasses standing in
# ONE groove in it: the front glass fixed on a rigid base, the side glasses rising out of the groove (their bays are in the
# desk). Every glass keeps the old screen's width : height at its mid height (the side panels 1600 x 1228 px, the front 1914),
# so the MFD band, its buttons, the tabs and the touches stay pixel for pixel. THE CREW: a desk arc round each seat's place
# at the console, its groove holding a dark glass with the three real MFDs of that place (the same units and modes as before).
BR_DESK = (.92, 1.42, .72)                 # his edge, the far edge (radius from his axis), the top over the floor
BR_GROOVE = (1.25, .035, .06)              # the glasses' groove: radius, half width, depth
BR_GLASS_LEAN = 14.0                       # all the glasses lean back (deg)
BR_GLASS_F = (.6632, 1.6267 / .6835)       # the front glass: height, width : height at mid height (the old front screen's)
BR_GLASS_S = (.96, 1.2782 / .9814)         # a side glass: height, width : height (the old side panels')
BR_GLASS_GAP = 4.0                         # deg between the front glass and a side glass
BR_GLASS_RC = .035                         # the glasses' top corners: round, clear of the MFDs
BR_BANDS = (.99, 1.19, .07)                # the sunken instrument bands of the desk's top: from r, to r, depth at his side
BR_CREW_DESK = (.62, .98, .86, 42.0)       # a crew desk round its seat's place: his edge, far edge, the groove, half span (deg)
SIDE_RISE = []                             # per side glass: the translation that takes it down into its bay (state 1)


def _glass_span(h, aspect):
    """The arc (deg) in the groove of a glass h high whose width : height at mid height is `aspect`."""
    tl = h * math.tan(math.radians(BR_GLASS_LEAN)); slant = h / math.cos(math.radians(BR_GLASS_LEAN))
    return math.degrees(aspect * slant / (BR_GROOVE[0] + tl / 2))


def _ear_clip(poly):
    """Triangles (index triples) of a simple 2D polygon, any winding."""
    pts = [np.asarray(p, float) for p in poly]; n = len(pts)
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    idx = list(range(n)) if area > 0 else list(range(n))[::-1]
    cr = lambda a, b, c: (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    tris = []
    while len(idx) > 3:
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = pts[i0], pts[i1], pts[i2]
            if cr(a, b, c) <= 1e-12: continue
            if any(cr(a, b, pts[j]) > 1e-12 and cr(b, c, pts[j]) > 1e-12 and cr(c, a, pts[j]) > 1e-12 for j in idx if j not in (i0, i1, i2)): continue
            tris.append((i0, i1, i2)); idx.pop(k); break
        else:                                                                     # no ear: drop a straight (collinear) vertex
            k = min(range(len(idx)), key=lambda k: abs(cr(pts[idx[k - 1]], pts[idx[k]], pts[idx[(k + 1) % len(idx)]])))
            idx.pop(k)
    tris.append(tuple(idx))
    return tris


def _sweep_arc(g_of, segs, a0, a1, n, W):
    """A body swept round an axis from angle a0 to a1 (deg) by its section: polylines of (r, h), going CLOCKWISE round the solid
    (his side up, the top outward, the far side down, the bottom back); W(a, r, h) -> the point; g_of(k): polyline k's group.
    Both ends capped (the section ear-clipped)."""
    angs = [a0 + (a1 - a0) * i / n for i in range(n + 1)]
    rad = lambda a: np.array([math.sin(math.radians(a)), 0.0, math.cos(math.radians(a))])
    for k, seg in enumerate(segs):
        g = g_of(k)
        for j in range(len(seg) - 1):
            (r0, h0), (r1, h1) = seg[j], seg[j + 1]; L = math.hypot(r1 - r0, h1 - h0)
            if L < 1e-9: continue
            nr, nh = -(h1 - h0) / L, (r1 - r0) / L                                # outward
            nrm = [rad(a) * nr + np.array([0.0, nh, 0.0]) for a in angs]
            va = [g.vert(W(a, r0, h0), nrm[i]) for i, a in enumerate(angs)]; vb = [g.vert(W(a, r1, h1), nrm[i]) for i, a in enumerate(angs)]
            for i in range(n): g.quad(va[i], va[i + 1], vb[i + 1], vb[i], nrm[i] + nrm[i + 1])
    ring = [p for seg in segs for p in seg[:-1]]
    tris = _ear_clip(ring)
    tg = lambda a: np.array([math.cos(math.radians(a)), 0.0, -math.sin(math.radians(a))])   # the direction of a growing angle
    g = g_of(0)
    for a, sg in ((a0, -1.0), (a1, 1.0)):
        nn = tg(a) * sg; vs = [g.vert(W(a, r, h), nn) for r, h in ring]
        for t in tris: g.tri(vs[t[0]], vs[t[1]], vs[t[2]], nn)


def _desk_section(ri, ro, top, groove, band=None, toe=True):
    """A desk's section (see _sweep_arc): the toe recess, his rounded edge, the top (a sunken sloped band in it), the groove,
    the far rounded edge. Returns the polylines and the index of the band's polyline (-1: none)."""
    gr, ghw, gd = groove; gi, go = gr - ghw, gr + ghw
    arc = lambda cr_, ch, R, f0, f1: [(cr_ + R * math.cos(math.radians(f0 + (f1 - f0) * i / 6)), ch + R * math.sin(math.radians(f0 + (f1 - f0) * i / 6))) for i in range(7)]
    S = ([[(ri + .10, 0), (ri + .10, .09)], [(ri + .10, .09), (ri, .24)]] if toe else [[(ri, 0), (ri, .24)]]) + [[(ri, .24), (ri, top - .03)], arc(ri + .03, top - .03, .03, 180, 90)]
    bi = -1
    if band:
        b0, b1, dp = band
        S += [[(ri + .03, top), (b0, top)], [(b0, top), (b0, top - dp)]]; bi = len(S); S += [[(b0, top - dp), (b1, top)], [(b1, top), (gi, top)]]
    else:
        S += [[(ri + .03, top), (gi, top)]]
    S += [[(gi, top), (gi, top - gd)], [(gi, top - gd), (go, top - gd)], [(go, top - gd), (go, top)], [(go, top), (ro - .03, top)], arc(ro - .03, top - .03, .03, 90, 0),
          [(ro, top - .03), (ro, 0)], [(ro, 0), (ri + .10 if toe else ri, 0)]]
    return S, bi


def _glass_rows(h, rc, W):
    """The rows (sy over the glass's bottom) of a glass with round top corners, and each row's span (sx from, to) over W."""
    rows = [(h - rc) * j / 24 for j in range(25)] + [h - rc + rc * math.sin(j / 12 * math.pi / 2) for j in range(1, 13)]
    def span(sy):
        dy = sy - (h - rc)
        if dy <= 0: return 0.0, W
        dx = rc - math.sqrt(max(0.0, rc * rc - dy * dy)); return dx, W - dx
    return rows, span


def _bridge_console_v7(G):
    """The console of the bridge, variant 7 (see BR_DESK). G: the groups (tub, cons, face, mfd_unit, out) as _crescent_console."""
    zc = zs(BR_S); F = BR_FLOOR_Y; cx, cz = BR_CMD
    tub, face = G["tub"], G["face"]
    up_ = np.array([0.0, 1.0, 0.0])
    rad = lambda a: np.array([math.sin(math.radians(a)), 0.0, math.cos(math.radians(a))])
    W = lambda a, r, h: np.array([cx + math.sin(math.radians(a)) * r, F + h, zc + cz + math.cos(math.radians(a)) * r])
    eye = np.array([cx, F + 1.2, zc + cz])
    del TOUCH_PLACES[:]; del TOUCH_FACETS[:]; del SIDE_DISP[:]; del SIDE_FOLD[:]; del SIDE_RISE[:]
    places = {}

    def place(k, A, B, uA, vA, vB, step, slot, g):                               # a touch screen: its facets and its middle rectangle
        n0_ = len(TOUCH_FACETS); _facets(k, A, B, uA, vA, vB, step, eye - A[len(A) // 2])
        mid = TOUCH_FACETS[n0_ + (len(TOUCH_FACETS) - n0_) // 2]
        wsum = sum(np.linalg.norm((A[i + 1] + B[i + 1]) / 2 - (A[i] + B[i]) / 2) for i in range(len(A) - 1))
        hmax = max(np.linalg.norm(B[i] - A[i]) / max(abs(vA[i] - vB[i]), 1e-3) for i in range(len(A)))
        places[k] = (mid[0], mid[1], mid[2], mid[3], wsum, hmax, slot, g)

    def dummy(k, slot):                                                          # a screen number kept, its place out of reach
        side = -1 if k in (4, 6) else 1
        places[k] = (np.array([side * 9.0, F - 9.0, zc]), np.array([1.0, 0, 0]), np.array([0, 1.0, 0]), np.array([0, 0, -1.0]), .40, .20, slot, None)

    gr, ghw, gd = BR_GROOVE; ri, ro, top = BR_DESK; lean = math.radians(BR_GLASS_LEAN); tl_ = math.tan(lean)
    hf, asf = BR_GLASS_F; hsf = _glass_span(hf, asf) / 2
    hsS, ass = BR_GLASS_S; hss = _glass_span(hsS, ass) / 2
    mids = (-(hsf + BR_GLASS_GAP + hss), hsf + BR_GLASS_GAP + hss)
    a_end = hsf + BR_GLASS_GAP + 2 * hss + BR_GLASS_GAP
    # THE DESK: three bodies (the left, the front, the right), each with its sunken band; its collision along the arc
    sec, bi = _desk_section(ri, ro, top, BR_GROOVE, BR_BANDS)
    for a0, a1 in ((-a_end, -hsf), (-hsf, hsf), (hsf, a_end)):
        _sweep_arc(lambda k: face if k == bi else tub, sec, a0, a1, max(8, int((a1 - a0) / 1.5)), W)
    for i in range(int(2 * a_end / 6)):
        a0 = -a_end + 2 * a_end * i / int(2 * a_end / 6); a1 = -a_end + 2 * a_end * (i + 1) / int(2 * a_end / 6)
        q = np.array([W(a, r, 0) for a in (a0, a1) for r in (ri, ro)])
        _coll("bridge", (q[:, 0].min(), F, q[:, 2].min()), (q[:, 0].max(), F + top, q[:, 2].max()))

    def glass(name, slot, mid, hs, h, rc, g_rim, dR_back=.018):                 # a glass standing in the groove (its texture drawn in the game)
        g = Group(name, MAT["br_display"]); g.tex = slot; G["out"].append(g)
        Wd = 2 * math.radians(hs) * gr
        rows, span = _glass_rows(h, rc, Wd)
        P_ = lambda sx, sy, dR=0.0: W(mid - hs + 2 * hs * sx / Wd, gr + sy * tl_ + dR, top + sy)
        nf = lambda sx: -rad(mid - hs + 2 * hs * sx / Wd) * math.cos(lean) + up_ * math.sin(lean)   # toward him
        NU = 48; grid = []
        for sy in rows:
            x0, x1 = span(sy)
            grid.append([g.vert(P_(x0 + (x1 - x0) * i / NU, sy), nf(x0 + (x1 - x0) * i / NU), ((x0 + (x1 - x0) * i / NU) / Wd, 1 - sy / h)) for i in range(NU + 1)])
        for j in range(len(rows) - 1):
            for i in range(NU): g.quad(grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i], nf(Wd / 2))
        # its rim (the glass's thickness) and its back face, in the frame's group
        out = [(Wd * i / 20, 0.0) for i in range(21)] + [(Wd - span(sy)[0], sy) for sy in rows[1:]][::1]
        out += [(span(sy)[0], sy) for sy in rows[::-1]]
        for i in range(len(out) - 1):
            (sa, ya), (sb, yb) = out[i], out[i + 1]
            pa, pb = P_(sa, ya), P_(sb, yb); qa, qb = P_(sa, ya, dR_back), P_(sb, yb, dR_back)
            e = pb - pa; nrm_ = np.cross(e, nf(sa)); c_ = (pa + pb) / 2; cc = P_(Wd / 2, h / 2)
            if nrm_ @ (c_ - cc) < 0: nrm_ = -nrm_
            vs = [g_rim.vert(p, nrm_) for p in (pa, pb, qb, qa)]; g_rim.quad(*vs, nrm_)
        back = [[g_rim.vert(P_(span(sy)[0] + (span(sy)[1] - span(sy)[0]) * i / 16, sy, dR_back), -nf(Wd / 2)) for i in range(17)] for sy in rows]
        for j in range(len(rows) - 1):
            for i in range(16): g_rim.quad(back[j][i], back[j][i + 1], back[j + 1][i + 1], back[j + 1][i], -nf(Wd / 2))
        nc = 40 if h < .8 else 24                                                # the touch: the full rows at the bottom and the top
        A = [P_(Wd * i / nc, 0.0) for i in range(nc + 1)]; B = [P_(Wd * i / nc, h) for i in range(nc + 1)]
        return g, A, B, [i / nc for i in range(nc + 1)], P_
    # THE FRONT GLASS, fixed on its rigid base: a frame tube round it, the bracket in the groove, two struts behind it
    fh = Group("bridge_frontdisp", MAT["mechanism"])
    g, A, B, uA, Pf = glass("bridge_centre", TOUCH_SLOT0 + 2, 0.0, hsf, hf, BR_GLASS_RC, fh); G["out"].append(fh)   # (the old group order kept)
    keys = Group("bridge_keys", MAT["br_display"]); keys.tex = TOUCH_SLOT_EXT + 1; G["out"].append(keys)   # the old keys shelf: gone (empty, the order kept)
    n_ = len(A) - 1; place(2, A, B, uA, [1.0] * (n_ + 1), [0.0] * (n_ + 1), 2, TOUCH_SLOT0 + 2, g)
    Wf = 2 * math.radians(hsf) * gr; rowsf, spanf = _glass_rows(hf, BR_GLASS_RC, Wf)
    ring_ = [Pf(-.012, 0, .03), Pf(-.012, hf - BR_GLASS_RC, .03)] + [Pf(spanf(sy)[0] - .012, sy + .012, .03) for sy in rowsf[25:]] + \
            [Pf(spanf(sy)[1] + .012, sy + .012, .03) for sy in rowsf[25:][::-1]] + [Pf(Wf + .012, hf - BR_GLASS_RC, .03), Pf(Wf + .012, 0, .03)]
    for i in range(len(ring_) - 1): tube(fh, ring_[i], ring_[i + 1], .014, n=8)
    for sx in (Wf * .2, Wf * .8):                                                # the struts: from the desk behind it to its back
        tube(fh, Pf(sx, -.0, .09) * np.array([1, 0, 1]) + np.array([0, F + top, 0]), Pf(sx, hf * .55, .035), .02, n=10)
    # THE SIDE GLASSES: they rise out of the groove (state 0 up); their rims ride with them
    for k, mid in ((0, mids[0]), (1, mids[1])):
        hous = Group(f"bridge_sidedisp{k}", MAT["br_bezel"])
        scr, A, B, uA, Ps = glass(f"bridge_sidescr{k}", TOUCH_SLOT0 + k, mid, hss, hsS, BR_GLASS_RC, hous); G["out"].append(hous)
        n_ = len(A) - 1; place(k, A, B, uA, [1.0] * (n_ + 1), [0.0] * (n_ + 1), 3, TOUCH_SLOT0 + k, scr)
        Wm = math.radians(hss) * gr; upv = Ps(Wm, hsS) - Ps(Wm, 0); slant = np.linalg.norm(upv)   # along its slant at its middle: the ends stay in the groove (+-2.7 cm)
        SIDE_DISP.append((scr, hous)); SIDE_RISE.append(-upv / slant * (slant + .03))
        SIDE_FOLD.append((Ps(Wm, 0), np.array([1.0, 0.0, 0.0])))                    # (no fold any more: kept for the header's shape)
    # the screens that are gone (the old keys shelf, the risers, the wings' shelves): their numbers kept, out of reach
    dummy(5, TOUCH_SLOT_EXT + 1); dummy(7, TOUCH_SLOT_EXT + 3)
    # THE SIDE CONSOLES (variant 7): fixed beside the seat's lane (x 0.46...0.80 from his axis, from 0.45 m behind it to the desk),
    # their tops 0.79 m over the floor (his forearms lie on them). The right one: the keys of the MFDs and of the computing machine
    # (screen 3: 4 x 10 glass keys) and the machine's own phosphor screen at its front (screen 6, tilted 34 deg to him); the left
    # one: the keys of the screens, the throttle quadrant's slots and the pods' key (screen 4). The keys are drawn by the game.
    CTOP = .79
    Pp = lambda q, h: np.array([cx + q[0], F + h, zc + cz + q[1]])
    for sx in (-1.0, 1.0):
        pts = [(sx * .46, -.45), (sx * .80, -.45)] + [(sx * x, math.sqrt((ri - .005) ** 2 - x * x)) for x in np.linspace(.80, .46, 13)]
        c2 = np.mean(np.array(pts), axis=0)
        for i in range(len(pts)):                                                # the walls
            a_, b_ = pts[i], pts[(i + 1) % len(pts)]
            nrm = np.array([b_[1] - a_[1], 0.0, -(b_[0] - a_[0])])
            if nrm @ np.array([(a_[0] + b_[0]) / 2 - c2[0], 0.0, (a_[1] + b_[1]) / 2 - c2[1]]) < 0: nrm = -nrm
            vs = [tub.vert(Pp(q, h), nrm) for q, h in ((a_, 0), (b_, 0), (b_, CTOP), (a_, CTOP))]; tub.quad(*vs, nrm)
        vs = [tub.vert(Pp(q, CTOP), up_) for q in pts]                           # the top
        for t3 in _ear_clip(pts): tub.tri(vs[t3[0]], vs[t3[1]], vs[t3[2]], up_)
        q = np.array([Pp(p_, 0) for p_ in pts])
        _coll("bridge", (q[:, 0].min(), F, q[:, 2].min()), (q[:, 0].max(), F + CTOP, q[:, 2].max()))

    def field(name, k, slot, x0, x1, z0, z1):                                    # a key field on a console's top: u to his right, v 0 at the far edge
        g = Group(name, MAT["br_display"]); g.tex = slot; G["out"].append(g)
        xs = np.linspace(x0, x1, 9); uA = [i / 8 for i in range(9)]
        A = [Pp((x, z0), CTOP + .002) for x in xs]; B = [Pp((x, z1), CTOP + .002) for x in xs]
        _strip(g, A, B, up_, [(u, 1.0) for u in uA], [(u, 0.0) for u in uA])
        place(k, A, B, uA, [1.0] * 9, [0.0] * 9, 8, slot, g)
    field("bridge_keys_r", 3, TOUCH_SLOT0 + 3, .462, .714, -.299, .211)          # 4 columns (6.4 cm) x 10 rows (5.2 cm), as the mockup's
    field("bridge_keys_l", 4, TOUCH_SLOT_EXT, -.75, -.46, -.12, .42)             # 3 x 5 keys, the pods' key, the two levers' slots
    # the computing machine's screen: a dark slab tilted 34 deg to him on a support (the mockup: centre x 0.585, 0.36 ahead, 6 cm up)
    tlt = math.radians(34.4); hw, hd = .095, .086
    nm_ = np.array([0.0, math.cos(tlt), -math.sin(tlt)]); fw_ = np.array([0.0, math.sin(tlt), math.cos(tlt)]); ex_ = np.array([1.0, 0.0, 0.0])
    c_ = Pp((.585, .36), CTOP + .06)
    obox(G["metal"], c_, ex_, nm_, fw_, .105, .008, .10)
    box(tub, Pp((.48, .37), CTOP), Pp((.69, .46), CTOP + .095))
    mg = Group("bridge_machine", MAT["br_display"]); mg.tex = TOUCH_SLOT_EXT + 2; G["out"].append(mg)
    s0 = c_ + nm_ * .0095
    A = [s0 + ex_ * x - fw_ * hd for x in np.linspace(-hw, hw, 5)]; B = [s0 + ex_ * x + fw_ * hd for x in np.linspace(-hw, hw, 5)]
    _strip(mg, A, B, nm_, [(i / 4, 1.0) for i in range(5)], [(i / 4, 0.0) for i in range(5)])
    place(6, A, B, [i / 4 for i in range(5)], [1.0] * 5, [0.0] * 5, 4, TOUCH_SLOT_EXT + 2, mg)
    # THE CREW: a desk arc round each seat's place at the console, a dark glass in its groove with the three MFDs of that place
    cri, cro, cgr, chs = BR_CREW_DESK
    csec, _ = _desk_section(cri, cro, top, (cgr, .03, .05), None)
    for si, modes in ((2, (2, 1, 6)), (1, (9, 1, 3))):                          # port: the engineer; starboard: the navigator (as before)
        sx_, sz_ = BR_SEAT_AT[si]; fx, fz = BR_SEAT_DIR[si]; phi = math.degrees(math.atan2(fx, fz))
        Wc = lambda a, r, h, sx_=sx_, sz_=sz_: np.array([sx_ + math.sin(math.radians(a)) * r, F + h, zc + sz_ + math.cos(math.radians(a)) * r])
        _sweep_arc(lambda k: tub, csec, phi - chs, phi + chs, 40, Wc)
        for i in range(8):
            a0 = phi - chs + 2 * chs * i / 8; a1 = a0 + 2 * chs / 8
            q = np.array([Wc(a, r, 0) for a in (a0, a1) for r in (cri, cro)])
            _coll("bridge", (q[:, 0].min(), F, q[:, 2].min()), (q[:, 0].max(), F + top, q[:, 2].max()))
        hb = .46; dk = G["dark"]                                                 # the dark glass behind the MFDs, in the groove
        for i in range(24):
            a0 = phi - 41 + 82 * i / 24; a1 = a0 + 82 / 24
            p = [Wc(a0, cgr, top), Wc(a1, cgr, top), Wc(a1, cgr + hb * tl_, top + hb), Wc(a0, cgr + hb * tl_, top + hb)]
            nn = -rad((a0 + a1) / 2) * math.cos(lean) + up_ * math.sin(lean)
            vs = [dk.vert(q_, nn) for q_ in p]; dk.quad(*vs, nn)
        for o, mode in zip((-28.0, 0.0, 28.0), modes):                          # the MFD units: 0.40 m, 28 deg apart (0.42 m on the groove)
            a = phi + o; s_ = .40; c = Wc(a, cgr + (.03 + s_ / 2) * tl_, top + .03 + s_ / 2) - rad(a) * .03   # in front of the curved glass's edges
            upv = np.array([math.sin(math.radians(a)) * math.sin(lean), math.cos(lean), math.cos(math.radians(a)) * math.sin(lean)])
            nrm = -rad(a) * math.cos(lean) + up_ * math.sin(lean)
            ex = np.cross(nrm, upv); ex /= np.linalg.norm(ex)
            if ex @ np.array([math.cos(math.radians(a)), 0, -math.sin(math.radians(a))]) < 0: ex = -ex   # right as seen
            G["mfd_unit"](c, ex, upv, nrm, mode, s_)
    TOUCH_PLACES[:] = [places[k] for k in range(8)]


BR_NAV_X, BR_NAV_DZ = -2.4, -1.3                             # navigation table (x, z offset from the axis station)
# An empty seat stands BACK from its console; one sits down in front of it, then it runs BR_SEAT_TRAVEL along its facing to
# the console; to stand up it runs back first (the user's decision, 2026-10-03). The crew's seats face their bays (crescent).
BR_SEAT_TRAVEL = (1.0, 0.6, 0.6, 0.6)                         # the commander's (variant 7): empty 0.40 m further back - room to stand up and step out
BR_SEAT_ADJ = (-0.10, 0.35)                                   # the commander's own travel at the console (m along the facing; variant 7: nearer the screens)
BR_SEAT_HGT = (-0.20, 0.05, -0.10)                            # his seat's height: min, max, at start (m; 0 = the pan's top 0.635 over the floor)
_CS = _cres_crew_seat()
BR_SEAT_AT = ((BR_CMD[0], BR_CMD[1] - .2), (_CS[0], _CS[1]), (-_CS[0], _CS[1]), (BR_NAV_X, BR_NAV_DZ - 1.5))   # at the console (the commander 0.2 m back from his bay's axis: the user)
BR_SEAT_DIR = ((0.0, 1.0), (_CS[2], _CS[3]), (-_CS[2], _CS[3]), (0.0, 1.0))                              # facing (x, z)
BR_SEATS = [(x - fx * t, z - fz * t, ac, ct) for (x, z), (fx, fz), t, ac, ct in
            zip(BR_SEAT_AT, BR_SEAT_DIR, BR_SEAT_TRAVEL, (0xA8362C, 0x3A5A8A, 0x3A5A8A, 0x3A5A8A), (False, False, False, False))]
BR_SEAT_BOX = (0.38, -0.45, 0.0, 1.5)                         # collision of a seat (moves with it): half x, z from, z to (offsets), height

COLL = []                                                    # (group name, lo, hi): every box of the interior, for collision in the game


def _coll(tag, lo, hi):
    COLL.append((tag, tuple(float(v) for v in lo), tuple(float(v) for v in hi)))


def _Rx(a):
    c, s_ = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s_], [0, s_, c]], float)


def _Ry(a):
    c, s_ = math.cos(a), math.sin(a)
    return np.array([[c, 0, s_], [0, 1, 0], [-s_, 0, c]], float)


class _Fr:
    """Frame: origin and rotation; local vectors map to the mesh frame (three.js conventions for the rotations)."""
    def __init__(self, o, R=None):
        self.o, self.R = np.asarray(o, float), (np.eye(3) if R is None else R)

    def p(self, v):
        return self.o + self.R @ np.asarray(v, float)

    def child(self, off, rx=0.0, ry=0.0):
        return _Fr(self.p(off), self.R @ _Ry(ry) @ _Rx(rx))


def _fb3(g, f, c, h):
    obox(g, f.p(c), f.R[:, 0], f.R[:, 1], f.R[:, 2], h[0], h[1], h[2])


def _arc_strip(g, r, y0, y1, a0, a1, cz, n=16):
    """Surface of a vertical cylinder piece seen from the inside (normals toward the axis)."""
    ring = []
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        nrm = (-math.sin(a), 0.0, -math.cos(a))
        u = i / n
        ring.append((g.vert((r * math.sin(a), y0, cz + r * math.cos(a)), nrm, (u, 1.0)), g.vert((r * math.sin(a), y1, cz + r * math.cos(a)), nrm, (u, 0.0)), nrm))
    for i in range(n):
        g.quad(ring[i][0], ring[i + 1][0], ring[i + 1][1], ring[i][1], ring[i][2])


def _bridge_seat(G, x, z, accent, ctl, yaw=0.0):
    """Anti-g seat: floating frame, inertia absorbers, contoured shell with wings, headrest, leg rest, harness, hand controllers."""
    zc = zs(BR_S)
    O = np.array([x, BR_FLOOR_Y, zc + z]); R = _Ry(yaw)                                                # turned to its facing
    seat, cush, met, belt, acc = G["seat"], G["cush"], G["met"], G["belt"], G["acc"]
    tube(met, O, O + R @ np.array((0, .5, 0)), .08, n=14, r1=.07)                                       # central hydraulic column
    for sx in (-1, 1):
        tube(seat, O + R @ np.array((sx * .27, 0, -.12)), O + R @ np.array((sx * .24, .28, -.12)), .034, n=10)                    # absorber cylinder
        tube(met, O + R @ np.array((sx * .24, .2, -.12)), O + R @ np.array((sx * .22, .5, -.12)), .018, n=8)                      # rod
    H = _Fr(O + np.array((0, .5, 0)), R)
    for k in range(20):                                                                                # floating ring
        a0, a1 = 2 * math.pi * k / 20, 2 * math.pi * (k + 1) / 20
        tube(met, H.p((.22 * math.cos(a0), -.01, .02 + .22 * math.sin(a0))), H.p((.22 * math.cos(a1), -.01, .02 + .22 * math.sin(a1))), .022, n=6, caps=False)   # under the pan
    _fb3(seat, H, (0, .05, .1), (.29, .04, .27)); _fb3(cush, H, (0, .11, .1), (.24, .025, .23))
    for sx in (-1, 1):
        _fb3(seat, H, (sx * .31, .12, .1), (.03, .05, .21))
    B = H.child((0, .1, -.14), rx=-.42)                                                                # reclined back
    _fb3(seat, B, (0, .48, 0), (.29, .475, .035)); _fb3(cush, B, (0, .46, .06), (.23, .41, .025))
    for sx in (-1, 1):
        W = B.child((sx * .32, .42, .1), ry=-sx * .25); _fb3(seat, W, (0, 0, 0), (.035, .33, .11))
        W2 = B.child((sx * .19, 1.0, .11), ry=-sx * .35); _fb3(seat, W2, (0, 0, 0), (.03, .12, .08))
    _fb3(cush, B, (0, 1.0, .06), (.15, .11, .045))
    _fb3(acc, B, (0, .26, .09), (.17, .07, .02)); _fb3(acc, B, (0, .2, .105), (.035, .03, .01))
    for sx in (-1, 1):
        _fb3(belt, B, (sx * .12, .55, .092), (.022, .31, .006)); _fb3(belt, H, (sx * .14, .15, .26), (.11, .015, .006))
    # (no leg rest: the feet stand on the floor under the tub - the user's decision)
    for sx in (-1, 1):
        _fb3(met, H, (sx * .36, .12, 0), (.015, .11, .02)); _fb3(seat, H, (sx * .36, .24, .04), (.04, .025, .18))   # armrest ends at +0.22
        if ctl:
            tube(met, H.p((sx * .36, .26, .22)), H.p((sx * .36, .42, .19)), .018, n=8)                  # side-stick
            S = H.child((sx * .36, .45, .2), rx=-.18); _fb3(seat, S, (0, 0, 0), (.025, .05, .03)); _fb3(acc, S, (0, .06, -.02), (.015, .009, .01))
        else:
            _fb3(seat, H, (sx * .36, .275, .2), (.035, .01, .06))
    # (no static collision: the seat moves; the module puts BR_SEAT_BOX at the seat's current place)


BR_CUR = (-0.36, 0.25, 0.14, 0.17)                            # the cursor unit on the left armrest's front end: x, z from the seat, width, depth
BR_BALL = (-0.37, 0.781, 0.27, 0.023)                         # its ball: x, y over the floor, z from the seat, radius
BR_STRIPS = ((0.350, 0.005), (0.393, 0.005), 0.17, 0.03)       # the sensor strips ХОД, ВЫСОТА on the right armrest's top: (x, z) each, length, width
BR_SEAT_TOP = 0.765                                           # the armrests' top over the floor


def _sphere(g, c, r, nu=20, nv=12):
    """A UV sphere (its own normals)."""
    c = np.asarray(c, float)
    rows = [[g.vert(c + r * np.array([math.sin(math.pi * j / nv) * math.cos(2 * math.pi * i / nu), math.cos(math.pi * j / nv),
                                      math.sin(math.pi * j / nv) * math.sin(2 * math.pi * i / nu)]),
                    np.array([math.sin(math.pi * j / nv) * math.cos(2 * math.pi * i / nu), math.cos(math.pi * j / nv), math.sin(math.pi * j / nv) * math.sin(2 * math.pi * i / nu)]))
             for i in range(nu + 1)] for j in range(nv + 1)]
    for j in range(nv):
        for i in range(nu):
            a, b, d, e = rows[j][i], rows[j][i + 1], rows[j + 1][i + 1], rows[j + 1][i]
            mid = (g.v[a] + g.v[b] + g.v[d] + g.v[e]) / 4 - c
            if j > 0: g.tri(a, b, d, mid)
            if j < nv - 1: g.tri(a, d, e, mid)


def _bridge_seat_cmd(G, x, z, rail):
    """The commander's seat of variant 7 (Tantra_Design/bridge_variants/v7.js): the pan's top 0.635 m (malachite shell, dark
    cushions), the backrest leaning 15 deg with the headrest, the armrests' tops 0.765 m; on the left one's front end the cursor
    unit (its ball: dark glass, lit while the light spot is on), on the right one's top the two sensor strips (ХОД, ВЫСОТА -
    drawn with the cursor unit's keys by the game, group bridge_seat_adj). It rides on a sled in a floor rail; its pan goes up
    and down on a telescopic pedestal (G["base"]: the sled and the outer tube, they do not go up and down)."""
    zc = zs(BR_S); F = BR_FLOOR_Y
    O = np.array([x, F, zc + z]); H0 = _Fr(O)
    seat, cush, met, base = G["seat"], G["cush"], G["met"], G["base"]
    box(base, O + (-.15, 0, -.17), O + (.15, .04, .23))                                                  # the sled
    tube(base, O + (0, .04, .03), O + (0, .36, .03), .085, n=18)                                        # the pedestal: its outer tube
    tube(met, O + (0, .30, .03), O + (0, .57, .03), .065, n=18)                                         # its inner tube (goes with the pan)
    _fb3(seat, H0, (0, .57, .10), (.26, .03, .24)); _fb3(cush, H0, (0, .6175, .11), (.23, .0175, .22))  # the pan, its cushion (top 0.635)
    B = H0.child((0, .60, -.12), rx=-.26)                                                                # the backrest, 15 deg back
    _fb3(seat, B, (0, .40, -.045), (.25, .40, .035)); _fb3(cush, B, (0, .40, .01), (.22, .37, .02)); _fb3(cush, B, (0, .89, -.01), (.15, .075, .04))
    for sx in (-1, 1):
        _fb3(met, H0, (sx * .37, .64, -.02), (.02, .075, .02))                                          # the armrest's post
        _fb3(seat, H0, (sx * .37, .74, .05), (.045, .025, .20))                                         # the armrest: top 0.765, -0.15...+0.25
    cx, cz, cw, cd = BR_CUR
    _fb3(seat, H0, (cx, .74, cz), (.08, .025, .09))                                                      # the left one's front end: the cursor unit's body
    _fb3(cush, H0, (cx, .772, cz), (cw / 2, .007, cd / 2))                                               # its dark housing (the key plate on it: bridge_seat_adj)
    bx, by, bz, br = BR_BALL
    _sphere(G["ball"], O + (bx, by, bz), br); _sphere(G["lit"], O + (bx, by, bz), br + .0005)
    for k in range(24):                                                                                  # the ball's bezel
        a0, a1 = 2 * math.pi * k / 24, 2 * math.pi * (k + 1) / 24
        tube(met, O + (bx + .026 * math.cos(a0), by - .001, bz + .026 * math.sin(a0)), O + (bx + .026 * math.cos(a1), by - .001, bz + .026 * math.sin(a1)), .0035, n=6, caps=False)
    (ax_, az_), (hx_, hz_), sl, sw = BR_STRIPS
    for px, pz in ((ax_, az_), (hx_, hz_)):                                                              # the strips' frames (5 mm proud)
        for dx, dz, hw, hd in ((0, sl / 2 + .004, sw / 2 + .006, .003), (0, -sl / 2 - .004, sw / 2 + .006, .003), (sw / 2 + .0035, 0, .003, sl / 2 + .006), (-sw / 2 - .0035, 0, .003, sl / 2 + .006)):
            _fb3(met, H0, (px + dx, BR_SEAT_TOP + .0035, pz + dz), (hw, .0035, hd))
    trav = BR_SEAT_TRAVEL[0] + BR_SEAT_ADJ[1]
    box(rail, O + (-.06, 0, -.30), O + (.06, .012, trav + .30))                                          # the floor rail along the whole travel


# The exit of the bridge: an arch at the back of the drum on the centre line, into the upper corridor of the living deck
# (corr_u ends at s 134, half a metre behind the drum). Flat sill, straight sides, a superellipse top, a glowing edge; a short
# vestibule sleeve. The capsule turns about x: the door lines up with the corridor while the ship lies.
BR_DOOR_HW = 0.8                                              # half width (1.6 m)
BR_DOOR_YC, BR_DOOR_BT = BR_FLOOR_Y + 1.55, 0.75              # where the top arch starts, its height (top = floor + 2.3)
BR_DOOR_ZW = zs(134.0)                                        # the forward wall of the living deck (the corridor end)
BR_SEG = 64                                                   # segments of the drum (the lathe of the shell)
BR_PATCH_K = (28, 37)                                         # drum segments of the back patch with the door (y 0.24 .. 4.17)
BR_PATCH_HX = 1.25                                            # half width of the patch along x


def _door_outline(n_arc=24):
    """Closed outline of the doorway in (x, y), starting at the sill."""
    a, yf = BR_DOOR_HW, BR_FLOOR_Y
    pts = [(-a + 2 * a * k / 8, yf) for k in range(8)]                            # the sill
    pts += [(a, yf + (BR_DOOR_YC - yf) * k / 5) for k in range(5)]                # one side
    for k in range(n_arc + 1):                                                    # superellipse top (n = 4)
        t = math.pi * k / n_arc; c, s_ = math.cos(t), math.sin(t)
        pts.append((a * math.copysign(abs(c) ** 0.5, c), BR_DOOR_YC + BR_DOOR_BT * abs(s_) ** 0.5))
    pts += [(-a, BR_DOOR_YC - (BR_DOOR_YC - yf) * k / 5) for k in range(1, 5)]   # the other side
    return pts


def _drum_ang(k):
    a = 2 * math.pi * k / BR_SEG                                                  # the lathe's angle: (y, z) = (BR_Y - R sin a, zc + R cos a)
    return a


def _drum_yz(k, r, zc):
    a = _drum_ang(k)
    return BR_Y - r * math.sin(a), zc + r * math.cos(a)


def _drum_back_z(y, zc, r=BR_R):
    """z of the back of the polygonal drum (the patch segments) at height y: on the chords, like the lathe."""
    k0, k1 = BR_PATCH_K
    for k in range(k0, k1):
        ya, za = _drum_yz(k, r, zc); yb, zb = _drum_yz(k + 1, r, zc)
        lo, hi = min(ya, yb), max(ya, yb)
        if lo - 1e-9 <= y <= hi + 1e-9:
            t = (y - ya) / (yb - ya); return za + (zb - za) * t
    raise ValueError(y)


def _plate_hole_uv(g, outline, C, outer, to3d, nrm, rings=3, n_ang=160):
    """A plate with the doorway cut out, in plate coordinates (u, v): rays from C evenly in angle and through every outline
    vertex, each from the outline to the outer boundary (outer(cu, cv, du, dv) -> distance); to3d(u, v) -> mesh point."""
    P = [np.array(q, float) for q in outline]; C = np.array(C, float)
    angs = sorted(set([round(2 * math.pi * k / n_ang, 9) for k in range(n_ang)] +
                      [round(math.atan2(q[1] - C[1], q[0] - C[0]) % (2 * math.pi), 9) for q in P]))

    def hit(u):
        best = 1e9
        for k in range(len(P)):
            a_, b_ = P[k], P[(k + 1) % len(P)]; e = b_ - a_
            den = u[0] * (-e[1]) - u[1] * (-e[0])
            if abs(den) < 1e-12: continue
            w = a_ - C
            t = (w[0] * (-e[1]) - w[1] * (-e[0])) / den; v_ = (u[0] * w[1] - u[1] * w[0]) / den
            if t > 1e-9 and -1e-9 <= v_ <= 1 + 1e-9: best = min(best, t)
        return best
    rows = []
    for t_ in angs:
        u = np.array([math.cos(t_), math.sin(t_)]); L = hit(u)
        Lo = max(outer(C[0], C[1], u[0], u[1]), L)
        rows.append([g.vert(to3d(*(C + u * (L + (Lo - L) * k / rings))), nrm) for k in range(rings + 1)])
    for i in range(len(rows)):
        j = (i + 1) % len(rows)
        for k in range(rings):
            g.quad(rows[i][k], rows[j][k], rows[j][k + 1], rows[i][k + 1], np.array(nrm))


def _rect_outer(u0, u1, v0, v1):
    def f(cu, cv, du, dv):
        tu = ((u1 - cu) / du) if du > 1e-9 else (((u0 - cu) / du) if du < -1e-9 else 1e9)
        tv = ((v1 - cv) / dv) if dv > 1e-9 else (((v0 - cv) / dv) if dv < -1e-9 else 1e9)
        return min(tu, tv)
    return f


def _drum_cyl(g, x0, x1, r, zc, skip=(), nrm_in=True):
    """Cylinder strip of the drum between x0 and x1 at radius r (the lathe's segments), without the segments in `skip`."""
    for k in range(BR_SEG):
        if k in skip: continue
        ya, za = _drum_yz(k, r, zc); yb, zb = _drum_yz(k + 1, r, zc)
        a = _drum_ang(k + 0.5); n_ = np.array([0.0, math.sin(a), -math.cos(a)]) * (1 if nrm_in else -1)
        vs = [g.vert(p, n_) for p in ((x0, ya, za), (x0, yb, zb), (x1, yb, zb), (x1, ya, za))]
        g.quad(*vs, n_)


def _drum_ring_side(g, x, r0, r1, zc, nx, skip=()):
    """Flat side of a rib (annulus between r0 and r1 at x), facing nx."""
    n_ = np.array([nx, 0.0, 0.0])
    for k in range(BR_SEG):
        if k in skip: continue
        ya, za = _drum_yz(k, r0, zc); yb, zb = _drum_yz(k + 1, r0, zc); yc, zc_ = _drum_yz(k + 1, r1, zc); yd, zd = _drum_yz(k, r1, zc)
        vs = [g.vert(p, n_) for p in ((x, ya, za), (x, yb, zb), (x, yc, zc_), (x, yd, zd))]
        g.quad(*vs, n_)


# The bridge console, variant B "the tub" (the user's choice, 2026-10-03; Tantra_Design/console_mockup.html, DESIGN_LOCAL.md):
# one continuous tub round the seats. Its inner edge in plan (x, z offset) runs from the port pod round the front to the
# starboard pod, the commander's bay deepest; the section: wall to the seat 0.72, shelf 0.30 (0.44 at the commander), raked
# instrument face, top 0.4, back wall. Top: 1.02 at the front, 1.15 toward the sides, 1.32 only at the tails.
TUB_CTRL = [(-2.35, .95), (-2.4, 1.75), (-2.15, 2.32), (-1.45, 2.44), (-.72, 2.6), (0, 2.7), (.72, 2.6), (1.45, 2.44), (2.15, 2.32), (2.4, 1.75), (2.35, .95)]
TUB_NP = 180
# The 12 MFD places of the bridge (the Orbiter limit): real Orbiter MFDs (ExternMFD stuck to the ship) are drawn on them by the
# module; each: a screen (own texture slot 22 + k) with the button labels at its edges, a bezel, 6 + 6 side buttons and PWR / SEL / MNU.
# In plate units (scaled by s): screen u -.15..+.15, v -.11..+.19; labels u +-.15..+-.20; bottom row v -.19..-.11; buttons at
# u +-.225 (rows v = .19 - (k + .5) * .05) and v -.215 (u -.1, 0, .1). Mode numbers: Orbiter's MFD_* ids.
MFD_PLACES = []
MFD_SLOT0 = 22
PANEL_SLOT = 34                                               # Textures/Tantra/panel.dds (the 2D panels' texture, redrawn in the game)
DISP_SLOT0, HOLO_SLOT = 37, 41                                # (old: the fins' screens, the holo panel - gone)
DISP_PLACES = []
HOLO_PLACE = []
TOUCH_SLOT0 = 37                                              # the commander's touch screens: 37 left display, 38 right, 39 flight terminal, 40 engines
TOUCH_PLACES = []                                             # (centre, right, up, normal, w, h, slot, group) in that order
SIDE_BTN = []                                                 # per elbow display: its up / down button (top centre)
SIDE_DISP = []                                                # per elbow display: (screen group, housing group) - they move together
PANEL_PIECES = []                                             # (panel id, texture rect, centre, right, up, normal, width, height)


def _tub_curve():
    """Centripetal Catmull-Rom through TUB_CTRL, resampled evenly by length: points P (x, z), tangents T, inward normals N."""
    C = [np.array(c, float) for c in TUB_CTRL]
    C = [2 * C[0] - C[1]] + C + [2 * C[-1] - C[-2]]
    dense = []
    for i in range(1, len(C) - 2):
        p0, p1, p2, p3 = C[i - 1], C[i], C[i + 1], C[i + 2]
        t0 = 0.0; t1 = t0 + np.linalg.norm(p1 - p0) ** .5; t2 = t1 + np.linalg.norm(p2 - p1) ** .5; t3 = t2 + np.linalg.norm(p3 - p2) ** .5
        for k in range(40):
            t = t1 + (t2 - t1) * k / 40
            a1 = (t1 - t) / (t1 - t0) * p0 + (t - t0) / (t1 - t0) * p1; a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2
            a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3
            b1 = (t2 - t) / (t2 - t0) * a1 + (t - t0) / (t2 - t0) * a2; b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3
            dense.append((t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2)
    dense.append(C[-2])
    D = np.array(dense); L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(D, axis=0), axis=1))])
    P = np.array([np.interp(np.linspace(0, L[-1], TUB_NP + 1), L, D[:, k]) for k in range(2)]).T
    T = np.gradient(P, axis=0); T /= np.linalg.norm(T, axis=1)[:, None]
    N = np.stack([-T[:, 1], T[:, 0]], axis=1)
    for i in range(len(N)):                                              # inward: toward the seats
        if np.dot(N[i], np.array([0.0, 1.5]) - P[i]) < 0: N[i] = -N[i]
    return P, T, N


def _tub_h(i):
    return 1.15                                                       # one height: the top is a deck flush with the screen bottom


def _tub_s(P, N, i):
    """Distance along the outward normal from the inner edge at i to the screen (the front arc or the end walls), less 1 cm."""
    p, u = np.array(P[i], float), -np.array(N[i], float)
    c = BR_SCR_R - BR_SCR_RP; best = 1e9
    for sgn in (-1, 1):                                               # the end walls (the side zones)
        if abs(u[0]) > 1e-9:
            t = (sgn * (BR_SCR_HX - .01) - p[0]) / u[0]
            if t > 0: best = min(best, t)
    q = p - np.array([0.0, c]); b = q @ u; cc = q @ q - BR_SCR_RP ** 2   # the front arc: |(x, z - c)| = RP
    disc = b * b - cc
    if disc >= 0:
        t = -b + math.sqrt(disc)
        if t > 0: best = min(best, t)
    return best - .01


def _tub_d(P, i):
    return .3 + .14 * math.exp(-(P[i][0] / .6) ** 2)


def _tub_at_x(P, x):
    return min((i for i in range(len(P)) if P[i][1] > 2.2), key=lambda i: abs(P[i][0] - x))


BR_ADJ_SLOT = 51                                              # the commander's seat slider (drawn by TantraInterior)
BR_ADJ = (0.31, 0.6715, 0.10, 0.28, 0.045)                    # its place on the seat's right side bolster, by the knee (x, y over the floor, z from the seat), length, width
BR_ADJ_C = []                                                 # the strips' and the key plate's centres at the seat's rest place (filled by _bridge_volume)
BR_SEAT_PANEL = (400, 340, 2000.0)                            # its texture: width, height (px), px per metre (ХОД 0..60, ВЫСОТА 60..120, the plate 120..400)
BR_SPOT_R = (0.005, 0.009, 0.016, 0.026, 0.038)               # the light spot's discs (m)
BR_GRATE_SLOT = 50                                            # the bridge's grating texture (the last of the VC list)
BR_RIB_X = (-3.6, -2.4, -1.2, 0.0, 1.2, 2.4, 3.6)             # the vault's ribs (arches across the drum)


def _bridge_volume(out):
    """The bridge's volume (the user, 2026-10-03): a grating technical floor over a dim lit sub-floor, ribs across the vault,
    lamp panels between them at the top and two thin light lines along it. Light only from these (the watch twilight); the
    people are lit by their own lights (Tantra::PersonLights). Kept out of build_bridge (the console fork works there)."""
    zc = zs(BR_S); F = BR_FLOOR_Y
    for g in out:                                                                          # the floor: the grating, 1 m per tile
        if g.name != "bridge_floor": continue
        g.v, g.n, g.t, g.uv = [], [], [], []
        g.tex = BR_GRATE_SLOT
        x0, x1, z0, z1 = -BR_HX, BR_HX, zc - BR_FZ, zc + BR_FZ
        q = [g.vert((x, F, z), (0, 1, 0), (x, -z)) for x, z in ((x0, z0), (x1, z0), (x1, z1), (x0, z1))]
        g.quad(*q, np.array([0, 1.0, 0]))
    rib, lamp, cove = Group("bridge_ribs", MAT["br_rib"]), Group("bridge_lamps", MAT["br_lamp"]), Group("bridge_cove", MAT["br_cove"])
    r = BR_R - .1                                                                          # the vault's inner surface
    f0, f1 = math.asin((BR_SCR_TOP - BR_Y) / r), math.radians(150)                         # from the screen's top over to the back
    def at(x, f, rr): return np.array([x, BR_Y + rr * math.sin(f), zc + rr * math.cos(f)])
    n = 28
    for x in BR_RIB_X:                                                                      # the ribs: 12 cm deep, 10 cm wide
        for k in range(n):
            fa, fb = f0 + (f1 - f0) * k / n, f0 + (f1 - f0) * (k + 1) / n
            fm = (fa + fb) / 2
            c = at(x, fm, r - .06); tang = at(x, fb, r - .06) - at(x, fa, r - .06)
            radial = np.array([0, math.sin(fm), math.cos(fm)])
            obox(rib, c, (1, 0, 0), radial, tang, .05, .06, np.linalg.norm(tang) / 2 + .002)
    for a_, b_ in zip(BR_RIB_X[:-1], BR_RIB_X[1:]):                                         # lamp panels between the ribs, at the top
        xm, hw = (a_ + b_) / 2, (b_ - a_) / 2 - .2
        for fd in (82, 98):
            f = math.radians(fd); c = at(xm, f, r - .015); radial = np.array([0, math.sin(f), math.cos(f)])
            tang = np.array([0, math.cos(f), -math.sin(f)])
            pts = [lamp.vert(c + sx * hw * np.array([1.0, 0, 0]) + sy * .11 * tang, -radial) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            lamp.quad(*pts, -radial)
    for fd in (62, 124):                                                                    # two thin light lines along the vault
        f = math.radians(fd); radial = np.array([0, math.sin(f), math.cos(f)]); tang = np.array([0, math.cos(f), -math.sin(f)])
        c = at(0.0, f, r - .02)
        pts = [cove.vert(c + sx * (BR_HX - .15) * np.array([1.0, 0, 0]) + sy * .015 * tang, -radial) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        cove.quad(*pts, -radial)
    # the main screen's frame (the user: «объём, чтобы было понятно, что это экран»): a thin light-grey body round the whole
    # U - the bottom edge, the top edge where it meets the vault, the back edges on the end walls - standing 4 cm into the
    # room; status lights on its bottom edge (one blinks: TantraInterior::PanelLights)
    frame = Group("bridge_scr_frame", MAT["br_bezel"])
    led, blink = Group("bridge_scr_led", MAT["br_green"]), Group("bridge_scr_blink", MAT["br_amber"])
    E = np.array([BR_SCR_EYE[0], BR_SCR_EYE[1], zc + BR_SCR_EYE[2]])
    rv = BR_SCR_RV; zt = math.sqrt(rv ** 2 - (BR_SCR_TOP - BR_Y) ** 2)
    def chain(pts, w=.035):
        for a_, b_ in zip(pts[:-1], pts[1:]):
            a_, b_ = np.array(a_, float), np.array(b_, float)
            c = (a_ + b_) / 2; t = b_ - a_; L_ = np.linalg.norm(t)
            if L_ < 1e-4: continue
            t /= L_
            n_ = E - c; n_ -= t * (n_ @ t); n_ /= np.linalg.norm(n_)               # toward the room (the design eye)
            obox(frame, c + n_ * .02, t, n_, np.cross(t, n_), L_ / 2 + .02, .02, w)
    # the outline in plan: port end wall -> its rounded corner -> the front arc -> the starboard corner -> its end wall
    # (the screen's own plan: _scr_side_plan, _scr_plan; the corners are fillets now)
    xt_ = _scr_fillet()[1]
    U = [(x, d) for x, d, _ in _scr_side_plan(-1)] + [(x, _scr_plan(x)[0]) for x in np.linspace(-xt_, xt_, 41)[1:-1]] +         [(x, d) for x, d, _ in _scr_side_plan(1)][::-1]
    chain([(x, BR_SCR_Y0, zc + d) for x, d in U])                                           # the bottom edge
    chain([(x, BR_SCR_TOP, zc + min(d, zt)) for x, d in U])                                 # the top edge (on the vault)
    for sx in (-1, 1):                                                                      # the back edges on the end walls
        chain([(sx * BR_SCR_HX, BR_SCR_Y0, zc + BR_SCR_BACK), (sx * BR_SCR_HX, BR_SCR_TOP, zc + BR_SCR_BACK)])
    for k, g_ in enumerate((led, led, blink)):                                              # status lights, bottom right
        x = 3.2 + .09 * k; d, pn = _scr_plan(x)
        c = np.array([x, BR_SCR_Y0, zc + d]) + np.array(pn) * .042
        obox(g_, c, (1, 0, 0), pn, (0, 1, 0), .022, .004, .009)
    out += [rib, lamp, cove, frame, led, blink]
    # the commander's seat slider (the user): a touch scale on the right armrest top, moves with the seat (in its groups);
    # built at the seat's rest place like the seat itself; u along the facing (back -> front)
    # variant 7: one texture (BR_SEAT_PANEL, 2000 px/m) for the two sensor strips on the right armrest (ХОД, ВЫСОТА) and the
    # cursor unit's key plate on the left one; v 0 at the front (along the facing)
    sx_, sz_ = BR_SEATS[0][0], BR_SEATS[0][1]
    adj = Group("bridge_seat_adj", MAT["br_display"]); adj.tex = BR_ADJ_SLOT
    PW, PH, PS = BR_SEAT_PANEL
    def rect(cx, cz, w, d, y, px0):                                                        # a quad over the seat (w across, d along) -> its region
        c = np.array([sx_ + cx, F + y, zc + sz_ + cz])
        q = [adj.vert(c + np.array([ex * w / 2, 0, ez * d / 2]), (0, 1, 0), ((px0 + (ex + 1) / 2 * w * PS) / PW, (1 - ez) / 2 * d * PS / PH))
             for ex, ez in ((-1, -1), (-1, 1), (1, 1), (1, -1))]
        adj.quad(*q, np.array([0, 1.0, 0]))
        return c
    (ax_, az_), (hx_, hz_), sl, sw = BR_STRIPS
    c0 = rect(ax_, az_, sw, sl, BR_SEAT_TOP + .0008, 0)
    c1 = rect(hx_, hz_, sw, sl, BR_SEAT_TOP + .0008, sw * PS)
    cx, cz, cw, cd = BR_CUR
    c2 = rect(cx, cz, cw, cd, .7795, 2 * sw * PS)
    BR_ADJ_C[:] = [list(c0), list(c1), list(c2)]
    out.append(adj)
    # the light spot («солнечный зайчик») on the glasses: stacked discs of a faint light; the game puts their vertices where the
    # cursor points (TantraInterior), front and back faces (5 discs x 13 vertices, twice)
    spot = Group("bridge_spot", MAT["br_spot"])
    for side in (1.0, -1.0):
        for r in BR_SPOT_R:
            c = spot.vert((0, F - 1.0, zc), (0, 0, -side))
            rim = [spot.vert((r * math.cos(2 * math.pi * k / 12), F - 1.0 + r * math.sin(2 * math.pi * k / 12), zc), (0, 0, -side)) for k in range(12)]
            for k in range(12):
                a_, b_ = rim[k], rim[(k + 1) % 12]
                spot.t.append((c, a_, b_) if side > 0 else (c, b_, a_))
    out.append(spot)


# The bridge's light (the user, 2026-10-03: «основной свет — от экрана, тени от него»; the walls and the floor were one flat,
# too light tone). Baked: the screen's surface (the arc, its rounded corners, the end-wall zones) is an area light; its
# irradiance on the vault, the end walls and the floor, with the console's shadow (its collision boxes), goes into light maps:
# br_vault_lit.dds (the shell: the drum unwrapped by angle + the two end discs) and br_floor_lit.dds (the grating x the light).
# The groups are self-lit by their texture (emissive), so the picture holds whatever the sun does.
BR_LIT_SLOT = (52, 53)                                        # TantraVC texture slots: the shell's map, the floor's map
BR_LIT_COL = np.array([0.78, 0.88, 1.00])                     # the screen's light: the sky, cold
BR_LIT_MIN, BR_LIT_REF = 0.035, 0.30                          # darkest (the back, behind the console); the floor under his seat
BR_OCC = []                                                   # the console's boxes (shadows): filled in build_bridge
BR_LIT_MAPS = {}                                              # the baked maps (written with the meshes)


def _lit_emitters():
    """Samples of the screen's surface: position, normal (to the room), area."""
    zc = zs(BR_S); xt = _scr_fillet()[1]
    plan = _scr_side_plan(-1, 4, 5) + [(x, *_scr_plan(x)) for x in np.linspace(-xt, xt, 18)[1:-1]] + _scr_side_plan(1, 4, 5)[::-1]
    P, N, A = [], [], []
    for i in range(len(plan) - 1):
        (xa, da, na), (xb, db, nb) = plan[i], plan[i + 1]
        w = math.hypot(xb - xa, db - da)
        if w < 1e-3: continue
        x, d = (xa + xb) / 2, (da + db) / 2; n = (np.array(na) + np.array(nb)) / 2; n /= np.linalg.norm(n)
        ytop = min(BR_Y + math.sqrt(max(BR_SCR_RV ** 2 - d ** 2, 0.0)), BR_SCR_TOP)
        for k in range(4):
            y0 = BR_SCR_Y0 + (ytop - BR_SCR_Y0) * k / 4; y1 = BR_SCR_Y0 + (ytop - BR_SCR_Y0) * (k + 1) / 4
            P.append((x, (y0 + y1) / 2, zc + d)); N.append(n); A.append(w * (y1 - y0))
    return np.array(P), np.array(N), np.array(A)


def _lit_heights(occ):
    """The console's shadow casters as a height field (3 cm cells): the top of the boxes over each cell (floor 0)."""
    zc = zs(BR_S); cell = .03
    nx, nz = int(2 * BR_HX / cell) + 1, int(2 * BR_R / cell) + 1
    H = np.full((nz, nx), -1e9)
    for lo, hi in occ:
        i0, i1 = int((lo[0] + BR_HX) / cell), int((hi[0] + BR_HX) / cell) + 1
        j0, j1 = int((lo[2] - zc + BR_R) / cell), int((hi[2] - zc + BR_R) / cell) + 1
        i0, j0 = max(i0, 0), max(j0, 0)
        if i1 <= i0 or j1 <= j0: continue
        H[j0:j1, i0:i1] = np.maximum(H[j0:j1, i0:i1], hi[1])
    return H, cell


def _lit_irradiance(Q, Nq, em, hf):
    """Irradiance at the points Q (normals Nq) from the emitter samples; hf: the height field of the shadow casters (or None)."""
    P, N, A = em
    zc = zs(BR_S)
    out = np.zeros(len(Q))
    for s in range(0, len(Q), 1024):
        q, nq = Q[s:s + 1024], Nq[s:s + 1024]
        D = P[None, :, :] - q[:, None, :]                                   # (n, m, 3) receiver -> emitter
        d2 = np.maximum((D ** 2).sum(-1), 0.09); d = np.sqrt(d2); u = D / d[..., None]
        cr = np.maximum((u * nq[:, None, :]).sum(-1), 0.0); ce = np.maximum(-(u * N[None, :, :]).sum(-1), 0.0)
        f = cr * ce * A[None, :] / (math.pi * d2)
        if hf is not None:                                                   # march along the ray over the height field
            H, cell = hf; vis = np.ones(f.shape, bool)
            for t in np.linspace(0.03, 0.95, 24):
                pt = q[:, None, :] + D * t
                i = np.clip(((pt[..., 0] + BR_HX) / cell).astype(int), 0, H.shape[1] - 1)
                j = np.clip(((pt[..., 2] - zc + BR_R) / cell).astype(int), 0, H.shape[0] - 1)
                vis &= pt[..., 1] >= H[j, i]
            f = f * vis
        out[s:s + 1024] = f.sum(-1)
    return out


def _lit_tone(E, ref):
    v = BR_LIT_MIN + (BR_LIT_REF - BR_LIT_MIN) * np.power(np.maximum(E, 0.0) / ref, 0.7)
    return np.clip(v, 0.0, 0.85)


def _lit_shell_uv(p, n):
    """The shell's atlas: v 0..0.6 the drum (u along x, v by the lathe's angle), v 0.6..1 the end discs (port left, starboard right)."""
    zc = zs(BR_S)
    if abs(abs(p[0]) - BR_HX) < 2e-3 and abs(n[0]) > 0.9:
        r = BR_R
        u = (0.25 if p[0] < 0 else 0.75) + 0.24 * (p[2] - zc) / r * (1 if p[0] < 0 else -1)
        v = 0.8 - 0.19 * (p[1] - BR_Y) / r
        return u, v
    a = math.atan2(BR_Y - p[1], p[2] - zc) % (2 * math.pi)
    return 0.01 + 0.98 * (p[0] + BR_HX) / (2 * BR_HX), 0.005 + 0.59 * a / (2 * math.pi)


def _bridge_light(out):
    zc = zs(BR_S); F = BR_FLOOR_Y
    em = _lit_emitters()
    key = hashlib.md5(repr((np.round(em[0], 3).tolist(), [tuple(np.round(b[0], 3)) + tuple(np.round(b[1], 3)) for b in BR_OCC],
                            BR_LIT_MIN, BR_LIT_REF, 3)).encode()).hexdigest()
    cache = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "bridge_light_%s.npz" % key[:12])
    if os.path.exists(cache):
        z_ = np.load(cache); shell_map, floor_lm = z_["shell"], z_["floor"]
    else:
        # the floor: 160 x 150 texels
        nx, nz = 160, 150
        xs = np.linspace(-BR_HX, BR_HX, nx); zz = np.linspace(zc - BR_FZ, zc + BR_FZ, nz)
        X, Z = np.meshgrid(xs, zz)
        Q = np.stack([X.ravel(), np.full(X.size, F + .01), Z.ravel()], 1); Nq = np.tile([0.0, 1.0, 0.0], (len(Q), 1))
        hf = _lit_heights(BR_OCC)
        Ef = _lit_irradiance(Q, Nq, em, hf).reshape(nz, nx)
        ref = _lit_irradiance(np.array([[0.0, F + .01, zc + BR_CMD[1]]]), np.array([[0.0, 1.0, 0.0]]), em, None)[0]
        floor_lm = _lit_tone(Ef, ref)
        # the shell atlas, 256 x 256: each texel back to its point and normal
        S = 256; shell_map = np.full((S, S), BR_LIT_MIN)
        U, V = np.meshgrid((np.arange(S) + .5) / S, (np.arange(S) + .5) / S)
        pts, nrm, idx = [], [], []
        for j in range(S):
            for i in range(S):
                u, v = U[j, i], V[j, i]
                if v < 0.6:
                    a = (v - 0.005) / 0.59 * 2 * math.pi; x = (u - 0.01) / 0.98 * 2 * BR_HX - BR_HX
                    if abs(x) > BR_HX: continue
                    p = (x, BR_Y - BR_R * math.sin(a), zc + BR_R * math.cos(a)); n = (0.0, math.sin(a), -math.cos(a))
                else:
                    port = u < 0.5; zo = (u - (0.25 if port else 0.75)) / 0.24 * BR_R * (1 if port else -1); yo = (0.8 - v) / 0.19 * BR_R
                    if zo * zo + yo * yo > BR_R * BR_R: continue
                    p = ((-BR_HX if port else BR_HX), BR_Y + yo, zc + zo); n = ((1.0 if port else -1.0), 0.0, 0.0)
                pts.append(p); nrm.append(n); idx.append((j, i))
        Es = _lit_irradiance(np.array(pts), np.array(nrm), em, hf)
        for (j, i), e in zip(idx, _lit_tone(Es, ref)): shell_map[j, i] = e
        os.makedirs(os.path.dirname(cache), exist_ok=True); np.savez(cache, shell=shell_map, floor=floor_lm)
    # the maps as colours: the graphite of the shell and the grating of the floor, lit by the screen's cold light
    from scipy.ndimage import zoom, gaussian_filter
    sh = gaussian_filter(shell_map, 1.0)
    shell_rgb = np.clip(zoom(sh, 1024 / sh.shape[0], order=1)[..., None] * BR_LIT_COL * 255 * 1.0, 0, 255)
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import make_interior_textures as mit
        tile = np.clip(mit.grate(), 0, 255) / 255.0
    except Exception:
        tile = np.full((512, 512, 3), 0.25)
    tw = 192                                                                      # px per metre on the floor map
    W_, H_ = int(2 * BR_HX * tw) // 4 * 4, int(2 * BR_FZ * tw) // 4 * 4
    W_, H_ = min(W_, 2048), min(H_, 2048)
    ty_ = np.arange(H_)[:, None] * (2 * BR_FZ / H_); tx_ = np.arange(W_)[None, :] * (2 * BR_HX / W_)
    ti = ((ty_ % 1.0) * tile.shape[0]).astype(int) % tile.shape[0]; tj = ((tx_ % 1.0) * tile.shape[1]).astype(int) % tile.shape[1]
    g = tile[ti, tj]                                                              # (H, W, 3) the grating, 1 m per tile
    lm = zoom(gaussian_filter(floor_lm, 1.0), (H_ / floor_lm.shape[0], W_ / floor_lm.shape[1]), order=1)[:H_, :W_]
    floor_rgb = np.clip(g * (lm[..., None] / 0.30) * BR_LIT_COL * 255 * 1.4, 0, 255)
    BR_LIT_MAPS.clear(); BR_LIT_MAPS["br_vault_lit"] = shell_rgb; BR_LIT_MAPS["br_floor_lit"] = floor_rgb
    for g_ in out:
        if g_.name == "command_bridge":
            g_.material = MAT["br_vault_lit"]; g_.tex = BR_LIT_SLOT[0]
            g_.uv = [_lit_shell_uv(p, n) for p, n in zip(g_.v, g_.n)]
        elif g_.name == "bridge_floor":
            g_.material = MAT["br_floor_lit"]; g_.tex = BR_LIT_SLOT[1]
            g_.uv = [((p[0] + BR_HX) / (2 * BR_HX), (p[2] - (zc - BR_FZ)) / (2 * BR_FZ)) for p in g_.v]   # (row 0 of the map: the back)


def write_lit_maps(dirpath):
    """br_vault_lit.dds, br_floor_lit.dds (BGRA) from _bridge_light."""
    os.makedirs(dirpath, exist_ok=True)
    for name, rgb in BR_LIT_MAPS.items():
        a = np.clip(rgb, 0, 255).astype(np.uint8); h_, w_, _ = a.shape
        header = bytearray(128); header[0:4] = b"DDS "
        for off, v in {4: 124, 8: 0x100F, 12: h_, 16: w_, 20: w_ * 4, 76: 32, 80: 0x41, 88: 32, 92: 0x00FF0000, 96: 0x0000FF00,
                       100: 0x000000FF, 104: 0xFF000000, 108: 0x1000}.items():
            header[off:off + 4] = int(v).to_bytes(4, "little")
        bgra = np.empty((h_, w_, 4), np.uint8); bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3] = a[..., 2], a[..., 1], a[..., 0], 255
        with open(os.path.join(dirpath, name + ".dds"), "wb") as f_:
            f_.write(header); f_.write(bgra.tobytes())


def build_bridge():
    """Groups of the command bridge capsule (all rotate together with the capsule in the game)."""
    zc = zs(BR_S)
    gm = lambda name, mat: Group(name, MAT[mat])
    shell = gm("command_bridge", "br_vault"); floor = gm("bridge_floor", "br_floor")
    scr = [gm("bridge_screen_" + n, "br_display") for n in ("l", "c", "r", "ls", "rs")]
    astro = gm("bridge_astro_screen", "br_display"); astro.tex = 7                     # astronomer's screen: the telescope camera
    cons = gm("bridge_console", "br_panel"); leds = gm("bridge_leds", "br_glow"); green = gm("bridge_green", "br_green"); red = gm("bridge_red", "br_red")
    amber = gm("bridge_amber", "br_amber"); nav = gm("bridge_nav", "br_panel"); metal = gm("bridge_metal", "mechanism")
    mfd = gm("bridge_mfd", "br_display"); mfd.tex = 4                                      # Orbiter MFD on the commander's console (texture replaced in the game)
    btn_on, btn_off = gm("bridge_button_on", "br_green"), gm("bridge_button_off", "br_red")
    bx0, by0, bz0 = 0.33, BR_FLOOR_Y + 0.765, zc + BR_SEATS[0][1] + 0.12                        # armrest top of the commander's seat, front end
    box(btn_on, (bx0, by0, bz0), (bx0 + 0.06, by0 + 0.025, bz0 + 0.08)); box(btn_off, (bx0, by0, bz0), (bx0 + 0.06, by0 + 0.025, bz0 + 0.08))
    GS = [{"seat": gm(f"bridge_seat{i}", "br_seat"), "cush": gm(f"bridge_seat{i}_cushion", "br_cushion"), "met": gm(f"bridge_seat{i}_metal", "mechanism"),
           "belt": gm(f"bridge_seat{i}_belt", "br_belt"), "acc": gm(f"bridge_seat{i}_accent", "br_red")} for i in range(len(BR_SEATS))]   # each seat moves
    # drum shell inside, ribs, flat floor (solid end discs: no door)
    lathe_axis(shell, (0.0, BR_Y, zc), (1, 0, 0), [(-BR_HX, 0.0), (-BR_HX, BR_R)], seg=BR_SEG, inward=True)     # end discs
    lathe_axis(shell, (0.0, BR_Y, zc), (1, 0, 0), [(BR_HX, BR_R), (BR_HX, 0.0)], seg=BR_SEG, inward=True)
    patch = set(range(*BR_PATCH_K)); px = BR_PATCH_HX
    _drum_cyl(shell, -BR_HX, -px, BR_R, zc); _drum_cyl(shell, px, BR_HX, BR_R, zc)                         # the vault
    _drum_cyl(shell, -px, px, BR_R, zc, skip=patch)
    door = gm("bridge_door_frame", "mechanism"); door_glow = gm("bridge_door_glow", "br_glow")
    ol = _door_outline(); C = (0.0, BR_FLOOR_Y + 0.9)
    yp0, yp1 = _drum_yz(BR_PATCH_K[0], BR_R, zc)[0], _drum_yz(BR_PATCH_K[1], BR_R, zc)[0]
    _plate_hole_uv(shell, ol, C, _rect_outer(-px, px, min(yp0, yp1), max(yp0, yp1)),
                   lambda u, v: (u, v, _drum_back_z(v, zc)), (0.0, 0.0, 1.0))                                  # the back patch with the doorway
    zw = BR_DOOR_ZW - 0.08                                                                                     # the corridor side of its forward wall
    for i in range(len(ol)):                                                                                   # the vestibule sleeve
        j = (i + 1) % len(ol); (xa, ya), (xb, yb_) = ol[i], ol[j]
        mx, my = (xa + xb) / 2 - C[0], (ya + yb_) / 2 - C[1]
        n_ = np.array([-mx, -my, 0.0]) / max(math.hypot(mx, my), 1e-9)
        vs = [door.vert(p, n_) for p in ((xa, ya, _drum_back_z(ya, zc)), (xb, yb_, _drum_back_z(yb_, zc)), (xb, yb_, zw), (xa, ya, zw))]
        door.quad(*vs, n_)
    top = BR_DOOR_YC + BR_DOOR_BT
    _plate_hole_uv(door, ol, C, _rect_outer(-BR_DOOR_HW - .01, BR_DOOR_HW + .01, BR_FLOOR_Y, top + .01),
                   lambda u, v: (u, v, zw - 0.005), (0.0, 0.0, -1.0), rings=1)                                  # the arch in the corridor wall's hole
    for i in range(len(ol)):                                                                                   # glowing edge round the doorway
        j = (i + 1) % len(ol)
        if ol[i][1] < BR_FLOOR_Y + 0.01 and ol[j][1] < BR_FLOOR_Y + 0.01: continue
        def outp(q):
            dx, dy = q[0] - C[0], q[1] - C[1]; L = math.hypot(dx, dy); return (q[0] + dx / L * .06, q[1] + dy / L * .06)
        quad = (ol[i], ol[j], outp(ol[j]), outp(ol[i]))
        vs = [door_glow.vert((u, v, _drum_back_z(v, zc) + .012), (0.0, 0.0, 1.0)) for u, v in quad]
        door_glow.quad(*vs, np.array([0.0, 0.0, 1.0]))
    for k in range(-3, 4):                                                                                     # ribs (the one on the centre line is cut at the door)
        x0, x1 = k * 1.2 - .07, k * 1.2 + .07
        sk = patch if abs(k * 1.2) < px else ()
        _drum_cyl(shell, x0, x1, BR_R - .1, zc, skip=sk)
        _drum_ring_side(shell, x0, BR_R - .1, BR_R, zc, -1.0, skip=sk); _drum_ring_side(shell, x1, BR_R - .1, BR_R, zc, 1.0, skip=sk)
    vz0, vz1 = BR_DOOR_ZW - 0.2, _drum_back_z(BR_FLOOR_Y, zc) + 0.25                                        # walking through the vestibule
    _coll("vestibule", (-BR_DOOR_HW - .5, BR_FLOOR_Y, vz0), (-BR_DOOR_HW, top + .4, vz1))
    _coll("vestibule", (BR_DOOR_HW, BR_FLOOR_Y, vz0), (BR_DOOR_HW + .5, top + .4, vz1))
    _coll("vestibule", (-BR_DOOR_HW, top, vz0), (BR_DOOR_HW, top + .4, vz1))
    _coll("vestibule", (-BR_DOOR_HW, BR_FLOOR_Y - .06, vz0), (BR_DOOR_HW, BR_FLOOR_Y, vz1))
    box(floor, (-BR_HX, BR_FLOOR_Y - .06, zc - BR_FZ), (BR_HX, BR_FLOOR_Y, zc + BR_FZ))
    # the screen, U-shaped: the concave front part in three zones, the side zones on the flat end walls (about +-100 deg)
    del BR_SCR_CAMS[:]
    xt = _scr_fillet()[1]                                                              # the front arc ends where the corner fillets begin
    plans = [[], [(x, *_scr_plan(x)) for x in np.linspace(-xt, xt, 49)], []]             # the front: ONE surface (no seams between zones)
    plans += [_scr_side_plan(-1), _scr_side_plan(1)]
    # the three front zones: one wide camera, one picture on texture slot 2 (no seams, no lag between them); the end walls
    # their own (their cameras exist only while someone looks there)
    E_ = np.array([BR_SCR_EYE[0], BR_SCR_EYE[1], zc + BR_SCR_EYE[2]])
    front = _screen_cam(np.array([p for pl in plans[:3] if pl for c in _screen_cols(pl, zc) for p, _ in c]) - E_)
    BR_SCR_CAMS.append((math.degrees(front[0]), math.degrees(front[1]), 2 * math.degrees(math.atan(front[5])), front[6] / front[5], 2))
    for k, (g, plan, slot) in enumerate(zip(scr, plans, (2, 2, 2, 5, 6))):                # texture slots (4 is the console MFD)
        g.tex = slot
        if plan: _screen_zone(g, plan, zc, slot, cam=front if k < 3 else None)
    ax = -BR_SCR_HX + 0.02; z0, z1, y0, y1 = BR_AST
    vs = [astro.vert((ax, yy, zc + zz), (1.0, 0.0, 0.0), (uu, vv)) for zz, yy, uu, vv in ((z1, y1, 0, 0), (z0, y1, 1, 0), (z0, y0, 1, 1), (z1, y0, 0, 1))]
    astro.quad(vs[0], vs[1], vs[2], vs[3], np.array([1.0, 0.0, 0.0]))                  # seen from the room: left = bow side
    for k in range(14):                                                                  # collision of the screen wall (thin boxes along the arc)
        cx = -BR_SCR_HX + 2 * BR_SCR_HX * (k + .5) / 14; cz_ = zc + _scr_plan(cx)[0] + .5
        _coll("bridge", (cx - .55, BR_FLOOR_Y, cz_ - .55), (cx + .55, BR_FLOOR_Y + 3.0, cz_ + .55))
    # the console: «Полумесяц φ» (_crescent_console)
    F = BR_FLOOR_Y
    tub = gm("bridge_tub", "br_malachite"); tface = gm("bridge_console_face", "br_panel_face")   # the desks: the malachite metal (variant 7)
    mfd_off = gm("bridge_mfd_dark", "br_screen")                                           # (empty: kept for the group order)
    holo = gm("bridge_holo", "br_holo")
    panels = gm("bridge_panels", "br_display"); panels.tex = PANEL_SLOT                     # (no 2D panel pieces on this console)
    del PANEL_PIECES[:]; del MFD_PLACES[:]; del DISP_PLACES[:]; del SIDE_BTN[:]

    def screen(group, slot, c, ex, up, nrm, w, h):                                          # a touch screen: a quad with its own texture
        g = gm(group, "br_display"); g.tex = slot
        c = np.asarray(c, float) + np.asarray(nrm, float) * .003
        pts = [c - ex * w / 2 + up * h / 2, c + ex * w / 2 + up * h / 2, c + ex * w / 2 - up * h / 2, c - ex * w / 2 - up * h / 2]
        vs = [g.vert(q, nrm, uv) for q, uv in zip(pts, ((0, 0), (1, 0), (1, 1), (0, 1)))]; g.quad(*vs, np.asarray(nrm, float))
        return g, c

    def mfd_unit(c, ex, up, nrm, mode, size=.34):                                          # a touch MFD: the labels on the screen's edges
        k = len(MFD_PLACES)
        g, c = screen(f"bridge_mfd{k}", MFD_SLOT0 + k, c, ex, up, nrm, size, size)
        MFD_PLACES.append((c, ex, up, nrm, size, mode, g))

    cres = []
    n_occ = len(COLL)                                                                       # the console's boxes cast the screen light's shadows
    _bridge_console_v7({"tub": tub, "cons": cons, "face": tface, "leds": leds, "metal": metal, "mfd_unit": mfd_unit, "out": cres, "dark": mfd_off})   # variant 7 (was _crescent_console)
    # the navigator: his own small desk with his MFD unit, in front of his seat (the seat runs to it)
    nsx, nsz = BR_SEAT_AT[3][0], zc + BR_SEAT_AT[3][1]
    box(cons, (nsx - .45, F, nsz + .47), (nsx + .45, F + .7, nsz + .77)); box(nav, (nsx - .47, F + .7, nsz + .44), (nsx + .47, F + .75, nsz + .8))
    _coll("bridge", (nsx - .47, F, nsz + .44), (nsx + .47, F + .75, nsz + .8))
    nn = np.array([0, .45, -.9]); nn /= np.linalg.norm(nn)
    mfd_unit(np.array([nsx, F + .93, nsz + .66]), np.array([1.0, 0, 0]), np.cross(nn, np.array([1.0, 0, 0])) * -1, nn, 9, .3)   # TRANSFER
    # navigation table with the holo-projector emitter ring and the control zone in front of the navigator
    nx, nz = BR_NAV_X, zc + BR_NAV_DZ
    tube(nav, (nx, BR_FLOOR_Y, nz), (nx, BR_FLOOR_Y + .9, nz), .8, n=32, r1=.62)
    tube(nav, (nx, BR_FLOOR_Y + .9, nz), (nx, BR_FLOOR_Y + .95, nz), .82, n=40)
    tube(leds, (nx, BR_FLOOR_Y + .95, nz), (nx, BR_FLOOR_Y + .965, nz), .5, n=36)
    for k in range(12):
        a = 2 * math.pi * k / 12; box(leds, (nx + math.cos(a) * .4 - .03, BR_FLOOR_Y + .95, nz + math.sin(a) * .4 - .03), (nx + math.cos(a) * .4 + .03, BR_FLOOR_Y + .99, nz + math.sin(a) * .4 + .03))
    NP = _Fr((nx, BR_FLOOR_Y + 1.0, nz - .5), _Rx(0.0)).child((0, 0, 0), rx=-.5)
    _fb3(cons, NP, (0, 0, 0), (.475, .008, .21))
    for k in range(9):
        a = math.pi + (k + .5) / 9 * math.pi; box(leds, (nx + math.cos(a) * .7 - .03, BR_FLOOR_Y + .95, nz + math.sin(a) * .7 - .03), (nx + math.cos(a) * .7 + .03, BR_FLOOR_Y + .99, nz + math.sin(a) * .7 + .03))
    _coll("bridge", (nx - .62, BR_FLOOR_Y, nz - .62), (nx + .62, BR_FLOOR_Y + 1.0, nz + .62))           # the round table: no corners
    BR_OCC[:] = [(np.array(lo), np.array(hi)) for _t, lo, hi in COLL[n_occ:]]
    GS[0]["seat"].material = MAT["br_malachite"]                                            # the commander's: variant 7
    GS[0].update({"base": gm("bridge_seat0_base", "mechanism"), "ball": gm("bridge_cur_ball", "br_cur_ball"), "lit": gm("bridge_cur_lit", "br_cur_lit")})
    for i, (G, (sx, sz, ac, ct), (fx, fz)) in enumerate(zip(GS, BR_SEATS, BR_SEAT_DIR)):
        if i == 0: _bridge_seat_cmd(G, sx, sz, metal)
        else: _bridge_seat(G, sx, sz, ac, ct, math.atan2(fx, fz))
    # horn of the receiver, edge up-lights and the long glass strip with a line of light on the port end
    tube(metal, (-2.75, BR_FLOOR_Y + 1.15, zc + 1.3), (-2.9, BR_FLOOR_Y + 1.25, zc + 1.15), .05, n=14, r1=.17)   # on the port deck of the tub
    box(leds, (-BR_HX + .09, BR_FLOOR_Y + .1, zc - 4.3), (BR_HX - .09, BR_FLOOR_Y + .14, zc - 4.22)); box(leds, (-BR_HX + .09, BR_FLOOR_Y + .1, zc + 4.22), (BR_HX - .09, BR_FLOOR_Y + .14, zc + 4.3))
    # (below the side zone of the screen and the astronomer's screen: they cover the wall from floor + 0.7 up)
    box(nav, (-BR_HX + .02, BR_FLOOR_Y + .38, zc - 1.9), (-BR_HX + .1, BR_FLOOR_Y + .64, zc + 1.1)); box(leds, (-BR_HX + .1, BR_FLOOR_Y + .48, zc - 1.8), (-BR_HX + .13, BR_FLOOR_Y + .53, zc + 1.0))
    _coll("bridge", (-BR_HX - .2, BR_FLOOR_Y, zc - 5), (-BR_HX + .1, BR_FLOOR_Y + 3, zc + 5))
    _coll("bridge", (BR_HX - .1, BR_FLOOR_Y, zc - 5), (BR_HX + .2, BR_FLOOR_Y + 3, zc + 5))
    return [shell, floor] + scr + [astro] + [cons, leds, green, red, amber, nav, metal, btn_on, btn_off, mfd, door, door_glow, tface, mfd_off, holo, tub, panels] + [m[6] for m in MFD_PLACES] + cres + [G[k] for G in GS for k in ("seat", "cush", "met", "belt", "acc")] + [GS[0][k] for k in ("base", "ball", "lit")]


def build_interior():
    """Interior skeleton (TantraVC.msh): floors, ceilings, bulkheads and side walls with the door openings, the passage,
    the bridge consoles and gimbal couches, the anabiosis pods."""
    global obox, tube
    _orig_obox, _orig_tube = obox, tube
    COLL.clear()

    PIECE = 0.25                                             # a slanted part is split into pieces of this size (its own boxes)

    def _piece(name, lo, hi):                                # a piece of a slanted part is a solid, never a floor
        lo, hi = np.array(lo, float), np.array(hi, float)
        if hi[1] - lo[1] < 0.2:
            m = (lo[1] + hi[1]) / 2; lo[1], hi[1] = m - 0.105, m + 0.105
        COLL.append((name, tuple(float(v) for v in lo), tuple(float(v) for v in hi)))

    def _robox(g, centre, ax, ay, az, hx, hy, hz):          # every box of the interior (also oriented ones, e.g. stair treads) is a collision box
        if not g.name.startswith(("bridge", "command_bridge")) or g.name.startswith("bridge_access"):   # the corridor is hull
            c = np.asarray(centre, float); A = [unit(ax), unit(ay), unit(az)]; Hh = [hx, hy, hz]
            ext = sum(np.abs(A[i]) * Hh[i] for i in range(3))
            aligned = all(np.abs(a).max() > 0.995 for a in A)
            flat = ext[1] < 0.1 and ext[0] > 0.125 and ext[2] > 0.125          # a tread or a floor plate: one box (it is walked on)
            if aligned or flat:
                COLL.append((g.name, tuple(float(v) for v in c - ext), tuple(float(v) for v in c + ext)))
            else:                                            # slanted: the bounding box would fill the empty space around it
                n = [max(1, int(math.ceil(2 * Hh[i] / PIECE))) for i in range(3)]
                for i in range(n[0]):
                    for j in range(n[1]):
                        for k in range(n[2]):
                            off = sum(A[q] * Hh[q] * (-1 + (2 * t + 1) / n[q]) for q, t in ((0, i), (1, j), (2, k)))
                            e = sum(np.abs(A[q]) * Hh[q] / n[q] for q in range(3))
                            _piece(g.name, c + off - e, c + off + e)
        return _orig_obox(g, centre, ax, ay, az, hx, hy, hz)

    def _rtube(g, p0, p1, r, n=16, caps=True, r1=None):      # columns and pipes
        if not g.name.startswith(("bridge", "command_bridge")) or g.name.startswith("bridge_access"):   # the corridor is hull
            a_, b_ = np.asarray(p0, float), np.asarray(p1, float); rr = max(r, r if r1 is None else r1)
            d = b_ - a_; ln = float(np.linalg.norm(d))
            if ln < 1e-9 or np.abs(d / ln).max() > 0.995 or ln < 2 * PIECE:
                COLL.append((g.name, tuple(float(v) for v in np.minimum(a_, b_) - rr), tuple(float(v) for v in np.maximum(a_, b_) + rr)))
            else:                                            # slanted pipe or rail: pieces along it
                m = int(math.ceil(ln / PIECE))
                for i in range(m):
                    q0, q1 = a_ + d * i / m, a_ + d * (i + 1) / m
                    _piece(g.name, np.minimum(q0, q1) - rr, np.maximum(q0, q1) + rr)
        return _orig_tube(g, p0, p1, r, n, caps, r1)
    obox, tube = _robox, _rtube
    out = []
    extra = []
    T = 0.08

    def slab_z(g, z, x0, x1, y0, y1, s_plane):
        rects = [(x0, y0, x1, y1)]
        for _, sp_, hx_, sill, w, h in _holes("z", s_plane, 0.05):
            rects = _subtract(rects, (hx_ - w / 2, sill, hx_ + w / 2, sill + h))
        for r in rects:
            box(g, (r[0], r[1], z - T), (r[2], r[3], z + T))

    def slab_y(g, y, x0, x1, z0, z1, thick=0.15, dy=0.0):
        rects = [(x0, z0, x1, z1)]
        for d in _holes("y", y, 0.05):
            _, yp, hx_, sc, sx = d[:5]
            ss = d[5] if len(d) > 5 else sx
            rects = _subtract(rects, (hx_ - sx / 2, zs(sc) - ss / 2, hx_ + sx / 2, zs(sc) + ss / 2))
        for r in rects:
            box(g, (r[0], y + dy - thick / 2, r[1]), (r[2], y + dy + thick / 2, r[3]))

    def slab_x(g, x, z0, z1, y0, y1):
        rects = [(z0, y0, z1, y1)]
        for _, xp, sc, sill, w, h in _holes("x", x, 0.4):
            rects = _subtract(rects, (zs(sc) - w / 2, sill, zs(sc) + w / 2, sill + h))
        for r in rects:
            box(g, (x - T, r[1], r[0]), (x + T, r[3], r[2]))

    for c in COMPARTMENTS:
        if c["key"] == "hangar":
            continue                          # the hangar is the exterior mesh: hangar_inner, its doors and the platform
        if c["key"] == "command_bridge":
            out += build_bridge()
            _bridge_volume(out)
            _bridge_light(out)
            continue
        g = Group(c["key"], MAT["in_wall"])
        gf = Group(c["key"] + "_floor", MAT["in_floor"])
        gc = Group(c["key"] + "_ceil", MAT["in_ceiling"])
        xc, hx, z0, z1 = c["xc"], c["hx"], zs(c["s0"]), zs(c["s1"])
        if c["key"] == "tech_passage":
            for sa, sb, fl in PASS_STEPS:
                za, zb = zs(sa), zs(sb)
                slab_y(g, fl, xc - hx, xc + hx, za, zb)
                slab_y(g, fl + PASS_H, xc - hx, xc + hx, za, zb, 0.12)
                for sgn in (-1, 1):
                    box(g, (xc + sgn * hx - 0.04, fl, za), (xc + sgn * hx + 0.04, fl + PASS_H, zb))
            for (sa, sb, fa), (_, _, fb) in zip(PASS_STEPS[:-1], PASS_STEPS[1:]):
                lo, hi = min(fa, fb), max(fa, fb)
                box(g, (xc - hx, lo, zs(sb) - 0.04), (xc + hx, hi, zs(sb) + 0.04))
            slab_z(g, z0, xc - hx, xc + hx, 5.5, 7.5, c["s0"])
            slab_z(g, z1, xc - hx, xc + hx, 5.5, 7.5, c["s1"])
        elif c["key"] in ("crew_deck", "lab_deck"):
            dk = "U" if c["key"] == "crew_deck" else "L"
            def _cuts(sa, sb, xlo, xhi):                    # the upper deck: pieces cut at the edges of the way to the lift (own materials)
                if dk != "U":
                    return [(sa, sb, xlo, xhi)]
                ss = [sa] + [v for v in (LOB, 127.4, 129.0, 130.6) if sa < v < sb] + [sb]
                xs = [xlo] + [v for v in (-6.85, -2.8, -1.0, 1.0, 2.8) if xlo < v < xhi] + [xhi]
                return [(a, b, c_, d) for a, b in zip(ss[:-1], ss[1:]) for c_, d in zip(xs[:-1], xs[1:])]
            for sa, sb, xlo, xhi in _deck_extents(dk):
                for a, b, c_, d in _cuts(sa, sb, xlo, xhi):
                    slab_y(gf, c["y0"], c_, d, zs(a), zs(b))
            for sa, sb, xlo, xhi in _deck_extents(dk, True):
                for a, b, c_, d in _cuts(sa, sb, xlo, xhi):
                    slab_y(gc, c["y1"], c_, d, zs(a), zs(b), dy=-0.075)      # ceiling hangs below the plane: no face shares the floor above
            for zz, sp, send in ((z0, c["s0"], D_S0), (z1, c["s1"], D_S1)):  # end walls as wide as the end rooms at each height
                ya = c["y0"]
                while ya < c["y1"] - 1e-6:
                    yb_ = min(c["y1"], ya + 0.5)
                    ym = 0.5 * (ya + yb_)
                    ends = [_room_x_at(r_, ym) for r_ in ROOMS if r_["deck"] == dk and (abs(r_["s0"] - send) < 0.02 or abs(r_["s1"] - send) < 0.02)]
                    # the forward end wall of the living deck is set 7 cm back: the bridge door frame stands 5 mm in front of it (no z-fighting)
                    xe0, xe1 = min(e[0] for e in ends), max(e[1] for e in ends)
                    if dk == "U" and send == D_S1:                 # the corridor's part of the forward wall on its own (the way to the lift)
                        for a, b in ((xe0, -1.0), (-1.0, 1.0), (1.0, xe1)):
                            slab_z(g, zz + 0.07, a, b, ya, yb_, sp)
                    else:
                        slab_z(g, zz + (0.07 if send == D_S1 else 0.0), xe0, xe1, ya, yb_, sp)
                    ya = yb_
        else:
            slab_y(gf, c["y0"], xc - hx, xc + hx, z0, z1)
            slab_y(gc, c["y1"], xc - hx, xc + hx, z0, z1, dy=-0.075)
            slab_z(g, z0, xc - hx, xc + hx, c["y0"], c["y1"], c["s0"])
            slab_z(g, z1, xc - hx, xc + hx, c["y0"], c["y1"], c["s1"])
            if c["key"] in ("airlock", "keel_bay", "tech_link"):
                for sgn in (-1, 1):
                    slab_x(g, xc + sgn * hx, z0, z1, c["y0"], c["y1"])
        out.append(g)                                         # (the group order is a contract with the module: new groups go to the end)
        extra += [x_ for x_ in (gf, gc) if x_.v]
    out += build_rooms()
    out += extra
    obox, tube = _orig_obox, _orig_tube
    out += _split_path(out)                                   # dim surfaces of the way to the lift (new groups: at the end)
    out += _split_dark(out)                                   # the rooms behind the shut doors: dark
    NIGHT = {MAT["in_wall"]: "in_wall_r", MAT["in_floor"]: "in_floor_r", MAT["in_ceiling"]: "in_ceil_r", MAT["in_light"]: "in_dutyred",
             MAT["in_metal"]: "in_metal_r", MAT["in_furn"]: "in_metal_r", MAT["in_seat"]: "in_metal_r", MAT["in_wet"]: "in_metal_r",
             MAT["structure"]: "in_wall_r"}
    for g_ in out:                                            # the lower deck (the night zone) at night: darkness and a red duty light
        if g_.name.endswith("_lower") or g_.name.startswith("lab_deck"):
            g_.material = MAT[NIGHT.get(g_.material, "in_metal_r")] if g_.material in NIGHT or not g_.name.startswith(("screen", "green", "amber", "red", "guide")) else g_.material
    out += [x_ for x_ in build_lining() if x_.v]              # the lining of the way: ceilings, chamfers, frames, niches, signs
    out += [x_ for x_ in build_watch() if x_.v]               # watch lamps and the light decals, drawn last
    out += LIFT_BTN                                           # the lift panel's button caps (the module switches them)
    out += build_lift_groups()                                # door B leaves, the cabin's panel, screen and caps (moved by the module)
    out += build_ilift_groups()                               # the inner lift's cab (moved by the module)
    return out


# ---------------------------------------------------------------------------
# Groups (stable order = module contract, written to MeshLayout.h)

SIDES = ("port", "starboard")
GROUPS = (["hull", "shoulder", "nose", "spine", "fin", "fin_upper", "crest_port", "crest_starboard", "wing_outer_port",
           "wing_outer_starboard", "elevon_port", "elevon_starboard",
           "body_flap", "well", "baffle", "cups_anamezon"] + [f"iris_ana_{i}" for i in range(4)]
          + ["well_centre", "march_unit", "iris_march", "cups_nose", "iris_nose_0", "iris_nose_1"]
          + [f"door_pod_{i}" for i in range(4)] + [f"arm_pod_{i}" for i in range(4)] + [f"pod_{i}" for i in range(4)]
          + ["door_top_port", "door_top_starboard", "door_bottom_port", "door_bottom_starboard",
             "hangar_inner", "shuttle", "rover_platform", "airlock", "pocket_liner"]
          + [f"{part}_{side}" for side in SIDES for part in ("carriage", "hip", "ankle")]
          + [f"pin_{side}_{k}" for side in SIDES for k in range(PIN_N)]
          + [f"blade_{side}_{i}" for side in SIDES for i in range(BLADE_N)]
          + [n for side in SIDES for n in foot_groups(f"foot_{side}")]
          + ["leg_hinges"]
          + [f"leg{i}_{part}" for i in range(4) for part in [f"sec{k}" for k in range(LEG_SEC_N)] + ["ankle"]]
          + [n for i in range(4) for n in foot_groups(f"sfoot{i}")]
          + ["kang_door", "kang_thigh", "kang_brace", "kang_rod"] + [f"kang_shin_{k}" for k in range(KANG_SEC_N)] + ["kang_ankle"]
          + foot_groups("kfoot")
          + ["bay_liner", "bay_door_port", "bay_door_starboard"] + [f"trap_{i}" for i in range(4)]
          + [f"lift{c}_heads" for c in range(2)] + [f"lift{c}_m{i}" for c in range(2) for i in range(LIFT_N)]
          + ["nose_ana", "ana_feed", "ana_buffer", "nose_screen", "nose_screen_core", "nose_mirror"]
          + ["airlock_arm1", "airlock_arm2", "airlock_arm3", "airlock_arm4", "airlock_platform", "airlock_glass", "airlock_mast", "airlock_foot"]
          + ["airlock_cab_frame", "airlock_cab_lining", "airlock_cab_light", "airlock_cab_beacon", "airlock_cab_floor", "airlock_cab_trim", "airlock_cab_lamp"]
)


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
    # nose anamezon unit behind each retro cup: dosing trap (small magnetic bottle, 3 coils), neck, injector to the focus
    g = grp("nose_ana", "engine_metal")
    for sgn in (1, -1):
        cx = sgn * NOSE_CUP_X
        apex = NOSE_CUP_S - NOSE_CUP_D
        focus = apex + NOSE_CUP_R ** 2 / (4.0 * NOSE_CUP_D)
        tube(g, (cx, NOSE_CUP_Y, zs(NOSE_DOSE_S0)), (cx, NOSE_CUP_Y, zs(NOSE_DOSE_S1)), NOSE_DOSE_R, n=20)
        for sc in np.linspace(NOSE_DOSE_S0 + 1.0, NOSE_DOSE_S1 - 1.0, 3):
            tube(g, (cx, NOSE_CUP_Y, zs(sc - 0.2)), (cx, NOSE_CUP_Y, zs(sc + 0.2)), NOSE_DOSE_R + 0.25, n=20)
        tube(g, (cx, NOSE_CUP_Y, zs(NOSE_DOSE_S1)), (cx, NOSE_CUP_Y, zs(apex - 0.05)), 0.45, n=12)
        tube(g, (cx, NOSE_CUP_Y, zs(apex - 0.05)), (cx, NOSE_CUP_Y, zs(focus)), 0.12, n=8)
        tube(g, (cx, NOSE_CUP_Y, zs(focus - 0.15)), (cx, NOSE_CUP_Y, zs(focus + 0.05)), 0.22, n=8)
    # anamezon feed. A cassette is set into a buffer bottle in front of its bay (a magnetic bottle with its own coils and
    # a docking collar); only the buffer feeds the line. The line is thin: a core in a vacuum jacket with confinement rings.
    gb = grp("ana_buffer", "engine_metal")
    g = grp("ana_feed", "engine_metal")

    def feed_line(pts):
        for (x0, y0, s0), (x1, y1, s1) in zip(pts[:-1], pts[1:]):
            p0, p1 = np.array([x0, y0, zs(s0)]), np.array([x1, y1, zs(s1)])
            tube(g, p0, p1, FEED_SLEEVE_R, n=10)
            L = float(np.linalg.norm(p1 - p0)); d = (p1 - p0) / L
            for k in range(int(L / FEED_COIL_STEP) + 1):
                c = p0 + d * min(L, 0.6 + k * FEED_COIL_STEP)
                tube(g, c - d * 0.07, c + d * 0.07, FEED_COIL_R, n=10)
        for x, y, sv in pts[1:-1]:
            tube(g, (x, y, zs(sv - 0.3)), (x, y, zs(sv + 0.3)), FEED_COIL_R, n=10)

    for sgn in (1, -1):
        traps = [(x, y) for x, y in TRAP_XY if x * sgn > 0]
        for x, y in traps:
            tube(gb, (x, y, zs(CASS_S1 + 0.5)), (x, y, zs(BUF_S0 + 0.5)), BUF_R + 0.2, n=16)                  # docking collar
            tube(gb, (x, y, zs(BUF_S0)), (x, y, zs(BUF_S1)), BUF_R, n=20)
            for sc in (BUF_S0 + 1.2, (BUF_S0 + BUF_S1) / 2, BUF_S1 - 1.2):
                tube(gb, (x, y, zs(sc - 0.25)), (x, y, zs(sc + 0.25)), BUF_R + 0.25, n=20)                       # coils
            tube(gb, (x, y, zs(BUF_S1)), (x, y, zs(BUF_S1 + 0.5)), 0.5, n=12)                                    # outlet
            feed_line([(x, y, BUF_S1 + 0.5), (x, y, FEED_HEAD_S), (sgn * FEED_X, y, FEED_HEAD_S)])
        ys = [y for _, y in traps]
        feed_line([(sgn * FEED_X, min(ys), FEED_HEAD_S), (sgn * FEED_X, max(ys), FEED_HEAD_S)])
        path = [(sgn * x, y, sv) for x, y, sv in (FEED_PATH_PORT if sgn < 0 else FEED_PATH)]
        feed_line(path)

    # nose: the 8 m screen across the hull (front/rear iridium plates, core of water and ion charges) and the magnetic
    # mirror rings (reflect backscattered particles) round each motor axis
    gp = grp("nose_screen", "nose_iridium")
    gc = grp("nose_screen_core", "structure")
    poly = []
    for k in range(48):
        a_ = 2 * math.pi * k / 48
        r_lo, r_hi = 0.0, 14.0
        for _ in range(24):
            r_mid = (r_lo + r_hi) / 2
            if outside(np.array([r_mid * math.cos(a_), 0.5 + r_mid * math.sin(a_), zs(SCREEN_S1)])) < -0.3: r_lo = r_mid
            else: r_hi = r_mid
        poly.append((r_lo * math.cos(a_), 0.5 + r_lo * math.sin(a_)))
    core_poly = [(x * 0.97, 0.5 + (y - 0.5) * 0.97) for x, y in poly]
    prism(gp, poly, (1, 0, 0), (0, 1, 0), (0, 0, 1), zs(SCREEN_S1 - 0.4), 0.8)
    prism(gp, poly, (1, 0, 0), (0, 1, 0), (0, 0, 1), zs(SCREEN_S0 + 0.4), 0.8)
    prism(gc, core_poly, (1, 0, 0), (0, 1, 0), (0, 0, 1), zs((SCREEN_S0 + SCREEN_S1) / 2), SCREEN_S1 - SCREEN_S0 - 1.6)
    gm_ = grp("nose_mirror", "engine_metal")
    for sgn in (1, -1):
        for sm in MIRROR_S:
            lathe(gm_, [(sm - 0.2, MIRROR_R - 0.3), (sm - 0.2, MIRROR_R + 0.3), (sm + 0.2, MIRROR_R + 0.3), (sm + 0.2, MIRROR_R - 0.3), (sm - 0.2, MIRROR_R - 0.3)],
                  center=(sgn * NOSE_CUP_X, NOSE_CUP_Y), seg=32)

    # ---- planetary pods (reference: stowed in the flank bays, covers flush, cups aft)
    for i, p in enumerate(PODS):
        patch(grp(f"door_pod_{i}", "hull_lacquer"), OPEN[f"pod_{i}"], back=0.25)
        c, e, nn = p["centre"], p["e"], p["n"]
        g = grp(f"pod_{i}", "engine_metal")
        obox(g, c, e, nn, (0, 0, 1), POD_W / 2, POD_T / 2, POD_L / 2)
        for q in p["cups"]:
            cup(g, q[0], q[1], q[2] - STERN_Z - 0.02, 0.5, POD_CUP_R, seg=16)
        tube(g, c - nn * (POD_T / 2 + 0.3), c, 1.0, n=16)                                   # swivel trunnion (inboard)
        g = grp(f"arm_pod_{i}", "mechanism")                                                 # telescopic arm, middle stage
        tube(g, c - nn * (POD_T / 2 + 0.4), c - nn * 0.2, 1.25, n=16)
        box(g, c - nn * (POD_T / 2 + 0.5) - np.array([2.2, 0, 1.5]), c - nn * (POD_T / 2 + 0.4) + np.array([2.2, 0, 1.5]))  # bay-floor shoe

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
    # ceiling frame: as wide as the hull at its own height allows (at the chine width it stuck 0.9 m out of the flanks)
    _us = np.linspace(0.0, 1.0, 1201)
    _wf = min(max(abs(q[0]) for q in (hull_xy(sf, u) for u in _us) if y_ch + 5.5 <= q[1] <= y_ch + 6.1)
              for sf in np.linspace(hs0 + 0.5, hs1 - 0.5, 9)) - 0.3
    box(hi, (-_wf, y_ch + 5.6, zs(hs0 + 0.5)), (_wf, y_ch + 6.0, zs(hs1 - 0.5)))
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
    zl = zs(AIRLOCK_S)
    for i, (hy, hz, st) in enumerate(((0.40, 0.50, "airlock_arm1"), (0.34, 0.42, "airlock_arm2"), (0.28, 0.34, "airlock_arm3"),
                                      (0.22, 0.26, "airlock_arm4"))):             # nested telescopic arm sections, 1.8 m each
        box(grp(st, "engine_metal"), (LOCK_X0 - 0.05, LOCK_ARM_Y - hy / 4, zl - hz / 2), (LOCK_X1, LOCK_ARM_Y + hy / 4, zl + hz / 2))
    # closed cabin (group keeps its name: airlock_platform): body, glazing band, the door on the hull side, mast guides
    gpl = grp("airlock_platform", "hull_lacquer")
    x0, x1, z0, z1 = LOCK_X0, LOCK_X1, zl - CAB_HZ, zl + CAB_HZ
    box(gpl, (x0, CAB_Y0, z0), (x1, CAB_Y0 + 0.12, z1))                                              # floor
    box(gpl, (x0, CAB_Y1 - 0.12, z0), (x1, CAB_Y1, z1))                                              # roof
    for zz in (z0, z1 - 0.08):                                                                       # end walls
        box(gpl, (x0, CAB_Y0, zz), (x1, CAB_Y1, zz + 0.08))
    box(gpl, (x0, CAB_Y0, z0), (x0 + 0.08, CAB_Y1, z1))                                              # outboard wall
    for za, zb in ((z0, zl - 0.65), (zl + 0.65, z1)):                                                # hull-side wall round the door
        box(gpl, (x1 - 0.08, CAB_Y0, za), (x1, CAB_Y1, zb))
    box(gpl, (x1 - 0.08, CAB_Y0 + 2.2, zl - 0.65), (x1, CAB_Y1, zl + 0.65))
    box(gpl, (x1 - 0.13, CAB_Y0 + 0.12, zl + 0.66), (x1 - 0.09, CAB_Y0 + 2.2, zl + 1.4))            # door leaf slid open (2026-10-03: the lift zone is the lock, the cabin is open to it)
    for yy in (1.5, 3.2):                                                                            # guide shoes on the mast
        box(gpl, (LOCK_MAST_X - 0.32, yy - 0.15, zl - 0.32), (x0, yy + 0.15, zl + 0.32))
    # volume (user 2026-10-03: no windows; textures, light and volume): outside a frame - corner posts, roof and base rims, ribs,
    # the door frame; inside a lining, hand rails, a floor plate and a ceiling light; amber beacons on the roof edges
    gw = grp("airlock_glass", "engine_metal")                                                        # (name kept: the rig moves it) base plinth
    box(gw, (x0 - 0.05, CAB_Y0 - 0.06, z0 - 0.05), (x1 + 0.02, CAB_Y0 + 0.1, z1 + 0.05))
    gfr = grp("airlock_cab_frame", "mechanism")
    for xc_, zc_ in ((x0, z0), (x0, z1), (x1, z0), (x1, z1)):                                        # corner posts
        box(gfr, (xc_ - 0.07, CAB_Y0, zc_ - 0.07), (xc_ + (0.02 if xc_ == x1 else 0.07), CAB_Y1 + 0.04, zc_ + 0.07))
    box(gfr, (x0 - 0.06, CAB_Y1 - 0.14, z0 - 0.06), (x1 + 0.02, CAB_Y1 + 0.06, z0 + 0.06))         # roof rim
    box(gfr, (x0 - 0.06, CAB_Y1 - 0.14, z1 - 0.06), (x1 + 0.02, CAB_Y1 + 0.06, z1 + 0.06))
    box(gfr, (x0 - 0.06, CAB_Y1 - 0.14, z0), (x0 + 0.02, CAB_Y1 + 0.06, z1))
    for zr in np.linspace(z0 + 0.6, z1 - 0.6, 4):                                                    # ribs on the outboard wall
        box(gfr, (x0 - 0.04, CAB_Y0 + 0.1, zr - 0.04), (x0, CAB_Y1 - 0.14, zr + 0.04))
    for xr in np.linspace(x0 + 0.45, x1 - 0.45, 2):                                                  # ribs on the end walls
        for zz in (z0 - 0.04, z1):
            box(gfr, (xr - 0.04, CAB_Y0 + 0.1, zz), (xr + 0.04, CAB_Y1 - 0.14, zz + 0.04))
    for za, zb in ((zl - 0.75, zl - 0.65), (zl + 0.65, zl + 0.75)):                                  # the door frame (hull side)
        box(gfr, (x1 - 0.1, CAB_Y0 + 0.1, za), (x1 + 0.02, CAB_Y0 + 2.3, zb))
    box(gfr, (x1 - 0.1, CAB_Y0 + 2.2, zl - 0.75), (x1 + 0.02, CAB_Y0 + 2.3, zl + 0.75))
    gli = grp("airlock_cab_lining", "in_wall_w")
    gcf = grp("airlock_cab_floor", "in_floor_w")
    box(gcf, (x0 + 0.08, CAB_Y0 + 0.12, z0 + 0.08), (x1 - 0.08, CAB_Y0 + 0.14, z1 - 0.08))         # floor plate (deck plates)
    gct = grp("airlock_cab_trim", "in_trim_w")
    box(gct, (x1 - 0.32, CAB_Y0 + 0.14, zl - 0.62), (x1 - 0.08, CAB_Y0 + 0.146, zl + 0.62))          # the hazard sill at the door
    box(gli, (x0 + 0.08, CAB_Y0 + 0.14, z0 + 0.08), (x0 + 0.12, CAB_Y0 + 1.0, z1 - 0.08))          # lower lining
    for zz in (z0 + 0.08, z1 - 0.12):
        box(gli, (x0 + 0.08, CAB_Y0 + 0.14, zz), (x1 - 0.08, CAB_Y0 + 1.0, zz + 0.04))
    box(gli, (x0 + 0.16, CAB_Y0 + 1.0, z0 + 0.25), (x0 + 0.2, CAB_Y0 + 1.06, z1 - 0.25))            # hand rails at hip height
    for zz in (z0 + 0.16, z1 - 0.2):
        box(gli, (x0 + 0.25, CAB_Y0 + 1.0, zz), (x1 - 0.25, CAB_Y0 + 1.06, zz + 0.04))
    box(gli, (x0 + 0.08, CAB_Y1 - 0.2, z0 + 0.08), (x1 - 0.08, CAB_Y1 - 0.12, z1 - 0.08))         # ceiling
    gla = grp("airlock_cab_light", "in_cablight")
    box(gla, (x0 + 0.6, CAB_Y1 - 0.23, z0 + 0.4), (x1 - 0.6, CAB_Y1 - 0.2, z1 - 0.4))              # the ceiling light
    gcl = grp("airlock_cab_lamp", "in_cablight")                                                    # two floodlights on the outboard roof edge (the user:
    for zz in (z0 + 0.2, z1 - 0.2):                                                                  # light on the lift, 2 sources): down and out
        box(gfr, (x0 - 0.2, CAB_Y1 - 0.24, zz - 0.11), (x0 - 0.01, CAB_Y1 - 0.04, zz + 0.11))         # the housing
        box(gcl, (x0 - 0.18, CAB_Y1 - 0.255, zz - 0.09), (x0 - 0.03, CAB_Y1 - 0.24, zz + 0.09))      # the lens (self-lit)
    gbe = grp("airlock_cab_beacon", "in_amber")
    for zz in (z0 - 0.07, z1 + 0.05):                                                                # amber beacons on the roof rims
        box(gbe, (x0 + 0.3, CAB_Y1 + 0.06, zz), (x1 - 0.3, CAB_Y1 + 0.1, zz + 0.02))
    yb_ = 2.9 - (LOCK_DROP + 2.0)                                                                    # the mast and foot are built extended
    # the mast is built as a 0.5 m stub under the arm head and stretched down (a root scaling: Orbiter 2016 crashes on a
    # scaling child), carried out with the arm by its own root translation (y-scaling about y 2.9 and an x shift commute)
    tube(grp("airlock_mast", "engine_metal"), (LOCK_MAST_X, LOCK_MAST_TOP, zl), (LOCK_MAST_X, LOCK_MAST_TOP - LOCK_MAST_STUB, zl), 0.18, n=16)
    gf = grp("airlock_foot", "engine_metal")
    yf_ = LOCK_MAST_TOP - LOCK_MAST_STUB                                                            # stowed under the mast stub
    box(gf, (LOCK_MAST_X - 0.45, yf_ - 0.15, zl - 0.45), (LOCK_MAST_X + 0.25, yf_, zl + 0.45))       # foot pad (on the ground at state 1)

    # ---- blade legs (reference: hip out at the track start, blade hanging with the stages out, umbrella open)
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
        g = grp(f"hip_{side}", "mechanism")                     # hip block with the pitch bearing (drum R 1.5 x 3.0)
        bi, bo, bh, bl = HIP_BLOCK
        x0, x1 = xs(HIP_X_OUT - bi, HIP_X_OUT + bo)
        box(g, (x0, -bh, T[2] - bl), (x1, bh, T[2] + bl))
        tube(g, T - np.array([sgn * 1.0, 0, 0]), T + np.array([sgn * 1.45, 0, 0]), 1.3, n=20)
        for xr in (-1.0, 1.2):                                  # catcher (sliding) bearing rings of the magnetic bearing
            tube(g, T + np.array([sgn * xr, 0, 0]), T + np.array([sgn * (xr + 0.25), 0, 0]), 1.42, n=20)
        for i in range(BLADE_N):                                # stages: boxes hanging from the hip, nested at the top
            g = grp(f"blade_{side}_{i}", "band")
            yc = -(i * BLADE_EXT + BLADE_L / 2)
            obox(g, T + np.array([0, yc, 0]), (1, 0, 0), (0, 1, 0), (0, 0, 1), BLADE_T[i] / 2, BLADE_L / 2, BLADE_W[i] / 2)
            if i:                                               # collar of the linear motors at the top of each stage
                obox(g, T + np.array([0, yc + BLADE_L / 2 - 0.6, 0]), (1, 0, 0), (0, 1, 0), (0, 0, 1), BLADE_T[i] / 2 + 0.12, 0.6, BLADE_W[i] / 2 + 0.12)
        A = T + np.array([0, -LEG_LMAX, 0])
        g = grp(f"ankle_{side}", "mechanism")                   # ankle: ball on the MR strut rod, locked when standing
        tube(g, A, A + np.array([0, ANKLE_R + STRUT_EXT_C + 0.8, 0]), 0.75, n=16)
        lathe_axis(g, A, (0, 1, 0), [(-0.75, 0.0), (-0.75, 0.9), (0.4, 1.0), (0.95, 0.0)], seg=20)
        cup_foot(grp, f"foot_{side}", FootFrame(A, (sgn, 0, 0), (0, 1, 0), "blade"))

    # ---- anamezon port: two belly bays under the trap columns, armoured doors, liner walls
    g = grp("bay_liner", "dark")
    yb = -FL * wh_at(S_FAIR)[1]
    for sgn in (-1, 1):
        for xw in (BAY_X0, BAY_X1 - 0.15):
            # metre by metre on the skin of its own section: aft of s 40 the belly rises towards the stern clover
            # (one box from the s 50 belly stuck up to 1.7 m out under the stern)
            _ss = list(np.arange(BAY_S0, BAY_S1, 1.0)) + [BAY_S1]
            for sa, sb in zip(_ss[:-1], _ss[1:]):
                _sk = [q[1] for sm in (sa, sb) for q in (hull_xy(sm, u) for u in np.linspace(0.0, 1.0, 801))
                       if abs(abs(q[0]) - xw) <= 0.3 and q[1] < 0.0]
                _ya = max(yb, max(_sk) + 0.15) if _sk else yb
                if _ya < yb + 6.0 - 0.3:                                      # where the skin is higher, no wall is needed
                    box(g, (sgn * xw - 0.15, _ya, zs(sa)), (sgn * xw + 0.15, yb + 6.0, zs(sb)))
        for s_end in (BAY_S0, BAY_S1):
            # end walls stand on the skin of their own section (at s 20.4 the belly is already higher: it stuck 1.6 m out)
            _sk = [q for q in (hull_xy(s_end, u) for u in np.linspace(0.0, 1.0, 1201)) if BAY_X0 <= abs(q[0]) <= BAY_X1 and q[1] < 0.0]
            _y0 = max(yb + 0.1, max(q[1] for q in _sk) + 0.05 if _sk else yb + 0.1)
            _xs = [abs(q[0]) for q in (hull_xy(s_end, u) for u in np.linspace(0.0, 1.0, 1201)) if _y0 <= q[1] <= yb + 6.0]
            _xw = min(BAY_X1, min(_xs) - 0.2) if _xs else BAY_X1                      # and no wider than the skin there
            box(g, (min(sgn * BAY_X0, sgn * _xw), _y0, zs(s_end) - 0.15), (max(sgn * BAY_X0, sgn * _xw), yb + 6.0, zs(s_end) + 0.15))
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
                # top under the skin of its own section (the ends of the columns sit lower than at s 50)
                _top = [q[1] for q in (hull_xy(sh, u) for u in np.linspace(0.0, 1.0, 1201)) if abs(abs(q[0]) - abs(xc)) <= 0.8 and q[1] > 0.0]
                yc_ = min(LIFT_CEIL, (min(_top) - 0.3 - w / 2) if _top else LIFT_CEIL)
                tube(g, (xc, yc_, zs(sh)), (xc, yc_ - LIFT_SEG, zs(sh)), w / 2 * math.sqrt(2), n=4)

    # ---- stern legs (reference: stowed aft on the nacelle sides over their heat shields; umbrella open at the ankle)
    g = grp("leg_hinges", "nose_iridium")
    for L in SLEGS:
        tube(g, L["H"] - 1.4 * L["axis"], L["H"] + 1.4 * L["axis"], 1.2, n=16)
        for a0 in (-1.4, 1.15):                                        # catcher rings of the magnetic bearing
            tube(g, L["H"] + a0 * L["axis"], L["H"] + (a0 + 0.25) * L["axis"], 1.32, n=16)
        n3 = L["n"]                                                    # pylon from the body to the hinge
        r_h = float(np.linalg.norm(L["H"][:2] - np.array([0.0, YC])))
        obox(g, np.array([0.0, YC, 0.0]) + n3 * (r_h - 1.5) + np.array([0, 0, L["H"][2] - 0.5]), L["e"], n3, (0, 0, 1),
             1.6, 2.2, 2.5)
        cx, cy = NAC[L["nac"]]                                        # heat-shield stack under the stowed leg
        base = np.array([cx, cy, 0.0]) + n3 * (R_N + LEG_STANDOFF / 2)
        obox(g, base + np.array([0, 0, zs((L["s0"] + LEG_S_H) / 2)]), L["e"], n3, (0, 0, 1), LEG_SEC_W / 2 + 0.3, LEG_STANDOFF / 2,
             (LEG_S_H - L["s0"]) / 2)
    mz = np.array([0, 0, -1.0])
    for i, L in enumerate(SLEGS):
        H, ax = L["H"], L["axis"]
        th = unit(np.cross(ax, mz))
        for k in range(LEG_SEC_N):                                     # sections nested at the hinge end, stowed along -z
            f = LEG_SEC_K ** k
            obox(grp(f"leg{i}_sec{k}", "band"), H + mz * LEG_SEC_L / 2, ax, th, mz, LEG_SEC_W / 2 * f, LEG_SEC_T / 2 * f, LEG_SEC_L / 2)
            if k:
                obox(grp(f"leg{i}_sec{k}", "band"), H + mz * 0.6, ax, th, mz, LEG_SEC_W / 2 * f + 0.1, LEG_SEC_T / 2 * f + 0.1, 0.6)
        fc = H + mz * LEG_LMIN_S
        g = grp(f"leg{i}_ankle", "mechanism")
        tube(g, fc, fc - mz * (ANKLE_R + STRUT_EXT_S + 0.9), 0.7, n=16)        # solid neck into the shin end
        lathe_axis(g, fc, -mz, [(-0.7, 0.0), (-0.7, 0.85), (0.35, 0.95), (0.9, 0.0)], seg=20)
        cup_foot(grp, f"sfoot{i}", FootFrame(fc, ax, -mz, "stern"))
    # ---- kangaroo leg (reference: stowed in the belly pocket)
    yb = -FL * wh_at((KANG_S0 + KANG_S1) / 2)[1]
    K = np.array([KANG_THIGH_X, yb + KANG_DEPTH - KANG_THIGH_W / 2 - 0.1, zs(KANG_HIP_S)])      # hip axis (along x)
    g = grp("kang_door", "hull_lacquer")
    for n in ("kang_pocket_starboard", "kang_pocket_port"):
        patch(g, OPEN[n], back=0.2)
    g = grp("kang_thigh", "band")
    obox(g, K + np.array([0, 0, -KANG_THIGH_L / 2 - 0.75]), (1, 0, 0), (0, 1, 0), (0, 0, 1), KANG_THIGH_W / 2, KANG_THIGH_W / 2, KANG_THIGH_L / 2 - 0.75)
    obox(g, K + np.array([0, 0, -0.8]), (1, 0, 0), (0, 1, 0), (0, 0, 1), KANG_THIGH_W / 2, 0.6, 0.8)    # neck inside the hip drum radius (swings inside the pocket end)
    tube(g, K - np.array([1.6, 0, 0]), K + np.array([1.6, 0, 0]), 0.9, n=16)                      # hip drum
    Kn = K + np.array([0, 0, -KANG_THIGH_L])                                                      # knee axis
    tube(g, Kn + np.array([-0.3, 0, 0]), Kn + np.array([KANG_KNEE_DX + 0.8, 0, 0]), 0.8, n=16)     # knee fork pin
    g = grp("kang_brace", "mechanism")                                                             # brace cylinder along the thigh's upper side
    tube(g, K + np.array([0, KANG_THIGH_W / 2 + 0.45, -1.5]), K + np.array([0, KANG_THIGH_W / 2 + 0.45, -KANG_THIGH_L + 1.0]), 0.42, n=12)
    Ks = Kn + np.array([KANG_KNEE_DX, 0, 0])                                                      # shin axis at the knee
    g = grp("kang_rod", "mechanism")                                                               # brace rod from the knee down the shin
    tube(g, Ks + np.array([0, KANG_SEC_W / 2 + 0.3, 0.3]), Ks + np.array([0, KANG_SEC_W / 2 + 0.3, 5.0]), 0.3, n=12)
    for k in range(KANG_SEC_N):                                                                    # sections nested at the knee, folded along +z
        f = (KANG_SEC_W - KANG_SEC_STEP * k) / KANG_SEC_W
        obox(grp(f"kang_shin_{k}", "band"), Ks + np.array([0, 0, KANG_SEC_L / 2]), (1, 0, 0), (0, 1, 0), (0, 0, 1),
             KANG_SEC_W / 2 * f, KANG_SEC_W / 2 * f, KANG_SEC_L / 2)
        if k:
            obox(grp(f"kang_shin_{k}", "band"), Ks + np.array([0, 0, 0.5]), (1, 0, 0), (0, 1, 0), (0, 0, 1),
                 KANG_SEC_W / 2 * f + 0.08, KANG_SEC_W / 2 * f + 0.08, 0.5)
    Ak = Ks + np.array([0, 0, KANG_SHIN_MIN + ANKLE_R])                                           # ankle (ball), stowed
    g = grp("kang_ankle", "mechanism")
    tube(g, Ak, Ak - np.array([0, 0, ANKLE_R + 0.6]), 0.55, n=16)
    lathe_axis(g, Ak, (0, 0, -1), [(-0.55, 0.0), (-0.55, 0.7), (0.3, 0.8), (0.75, 0.0)], seg=20)
    cup_foot(grp, "kfoot", FootFrame(Ak, (0, -1, 0), (0, 0, -1), "kang"))

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
    # pods: 1 = stowed (mesh pose); the arm runs the pod and the bay cover out along the flank normal, the middle
    # stage half way; the pod turns about the arm axis (0 cups aft .. 180 cups forward)
    for i, p in enumerate(PODS):
        arm = add("pod_retract", "tr", [f"door_pod_{i}"], -p["n"] * p["travel"], d=1.0)
        add("pod_retract", "tr", [f"arm_pod_{i}"], -p["n"] * p["travel"] * 0.5, d=1.0)
        add("pod_swivel", "rot", [f"pod_{i}"], (p["centre"], p["n"] * p["sgn"], POD_SWIVEL_MAX), parent=arm)
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
    # main airlock lift: door -> arm out -> mast to the ground -> platform down the mast
    KA_ = key_wh(*wh_at(AIRLOCK_S))
    fe_ = unit(KA_["T"] - KA_["C"])
    top_ = KA_["C"] + fe_ * (LOCK_TOP - KA_["C"][1]) / fe_[1]
    add("airlock_door", "rot", ["airlock"], (np.array([-top_[0], top_[1], 0.0]), np.array([0, 0, 1.0]), math.radians(-80.0)))
    for k_, g_ in enumerate(("airlock_arm2", "airlock_arm3")):
        add("airlock_out", "tr", [g_], np.array([-LOCK_OUT * (k_ + 1) / 3.0, 0, 0]))
    ao_ = add("airlock_out", "tr", ["airlock_arm4"], np.array([-LOCK_OUT, 0, 0]))
    add("airlock_out", "tr", ["airlock_mast"], np.array([-LOCK_OUT, 0, 0]))                                        # root: out with the arm
    add("airlock_mast", "sc", ["airlock_mast"], (np.array([LOCK_MAST_X, LOCK_MAST_TOP, zs(AIRLOCK_S)]),
                                                  np.array([1.0, LOCK_MAST_FULL / LOCK_MAST_STUB, 1.0])))             # root: stub -> ground
    add("airlock_mast", "tr", ["airlock_foot"], np.array([0, -(LOCK_MAST_FULL - LOCK_MAST_STUB), 0]), parent=ao_)   # stays at the mast's end
    add("airlock_down", "tr", ["airlock_platform"], np.array([0, -LOCK_DROP, 0]), parent=ao_)
    for g_ in ("airlock_glass", "airlock_cab_frame", "airlock_cab_lining", "airlock_cab_light", "airlock_cab_beacon", "airlock_cab_floor", "airlock_cab_trim", "airlock_cab_lamp"):
        add("airlock_down", "tr", [g_], np.array([0, -LOCK_DROP, 0]), parent=ao_)
    add("rover_lift", "tr", ["rover_platform"], np.array([0, (-AXIS_H + 0.25) - (-FL * wh_at(HANGAR_REF_S)[1] + 0.25), 0]))
    for side, sgn in (("port", -1), ("starboard", 1)):
        T = np.array([sgn * HIP_X_OUT, 0.0, zs(CAR_SREF)])
        run = np.array([0, 0, STOW_S - CAR_S0])                                               # the shoe runs on to the stow station
        tr = add(f"track_{side}", "tr", [], run)                                              # pivot only
        add(f"track_{side}", "tr", [f"carriage_{side}"], run)
        P = np.array([sgn * PIN_X0, 0.0, T[2]])
        ro = add(f"slide_{side}", "rot", [f"pin_{side}_0"], (P, np.array([0, 0, 1.0]), sgn * ROLL), parent=tr, s0=0.0, s1=SLIDE_ROLL)
        for k in range(1, PIN_N):                                                              # pin stages nest
            add(f"slide_{side}", "tr", [f"pin_{side}_{k}"], np.array([-sgn * k * PIN_STEP, 0, 0]), parent=ro, s0=SLIDE_ROLL)
        sl = add(f"slide_{side}", "tr", [f"hip_{side}"], np.array([-sgn * TRAVEL, 0, 0]), parent=ro, s0=SLIDE_ROLL)
        pi = add(f"pitch_{side}", "rot", [f"blade_{side}_0"], (T, np.array([1.0, 0, 0]), math.pi / 2), parent=sl)  # 1 = aft
        for i in range(1, BLADE_N):                                                             # stages run out in step
            add(f"blade_ext_{side}", "tr", [f"blade_{side}_{i}"], np.array([0, i * BLADE_EXT, 0]), parent=pi)       # 1 = in
        ank = add(f"blade_ext_{side}", "tr", [], np.array([0, (BLADE_N - 1) * BLADE_EXT, 0]), parent=pi)       # pivot only
        st = add("strut_carriage", "tr", [f"ankle_{side}"], np.array([0, -STROKE_C, 0]), parent=ank, d=STRUT_D_C)  # 0 bottomed .. 1 unloaded
        A = T + np.array([0, -LEG_LMAX, 0])
        cup_rig(add, f"foot_fold_{side}", f"foot_{side}", FootFrame(A, (sgn, 0, 0), (0, 1, 0), "blade"), st)   # 1 = folded past the ankle
    mz = np.array([0, 0, -1.0])
    for i, L in enumerate(legs):
        sw = add(f"leg{i}_swing", "rot", [f"leg{i}_sec0"], (L["H"], L["axis"], L["phi_max"]))
        for k in range(1, LEG_SEC_N):
            add(f"leg{i}_ext", "tr", [f"leg{i}_sec{k}"], mz * LEG_STEP * k, parent=sw)
        ex = add(f"leg{i}_ext", "tr", [], mz * LEG_EXT_MAX, parent=sw)                              # pivot only
        st = add(f"leg{i}_strut", "tr", [f"leg{i}_ankle"], mz * STROKE_S, parent=ex, d=STRUT_D_S)  # 0 bottomed .. 1 unloaded
        fc = L["H"] + mz * LEG_LMIN_S
        # the foot turns to the ground once the ankle is clear of the hull (last quarter of the swing)
        fs = add(f"leg{i}_foot_stand", "rot", [], (fc, L["fs_ax"], L["fs_ang"]), parent=st, s0=0.75)   # pivot only
        rail = add(f"leg{i}_rail", "tr", [], -mz * LEG_RAIL, parent=fs)                            # (no rail with the cup feet)
        cup_rig(add, f"leg{i}_fold", f"sfoot{i}", FootFrame(fc, L["axis"], -mz, "stern"), rail)    # 1 = bundle past the ankle
    # kangaroo: door, hip (thigh down-forward), knee (shin unfolds from beside the thigh), sections, ankle, foot
    yb = -FL * wh_at((KANG_S0 + KANG_S1) / 2)[1]
    K = np.array([KANG_THIGH_X, yb + KANG_DEPTH - KANG_THIGH_W / 2 - 0.1, zs(KANG_HIP_S)])
    Kn = K + np.array([0, 0, -KANG_THIGH_L])
    Ks = Kn + np.array([KANG_KNEE_DX, 0, 0])
    Ak = Ks + np.array([0, 0, KANG_SHIN_MIN + ANKLE_R])
    dp = hull_pt((KANG_S0 + KANG_S1) / 2, OPEN["kang_pocket_starboard"]["u"][1])
    add("kang_door", "rot", ["kang_door"], (np.array([dp[0], dp[1], 0.0]), np.array([0, 0, 1.0]), -math.radians(110.0)))
    hip = add("kang_hip", "rot", ["kang_thigh"], (K, np.array([1.0, 0, 0]), -KANG_HIP_MAX))
    add("kang_hip", "rot", ["kang_brace"], (K, np.array([1.0, 0, 0]), -KANG_HIP_MAX))
    kn = add("kang_knee", "rot", ["kang_shin_0"], (Ks, np.array([1.0, 0, 0]), math.pi), parent=hip)         # 1 = straight
    add("kang_knee", "rot", ["kang_rod"], (Ks, np.array([1.0, 0, 0]), math.pi), parent=hip)
    for k in range(1, KANG_SEC_N):
        add("kang_ext", "tr", [f"kang_shin_{k}"], np.array([0, 0, KANG_STEP * k]), parent=kn)
    ex = add("kang_ext", "tr", [], np.array([0, 0, (KANG_SEC_N - 1) * KANG_STEP]), parent=kn)              # pivot only
    st = add("kang_strut", "tr", ["kang_ankle"], np.array([0, 0, STROKE_S]), parent=ex, d=STRUT_D_S)       # 0 bottomed .. 1 unloaded
    ft = add("kang_foot", "rot", [], (Ak, np.array([1.0, 0, 0]), math.pi), parent=st)                       # foot kept level: state = shin lean / pi (code)
    cup_rig(add, "kang_fold", "kfoot", FootFrame(Ak, (0, -1, 0), (0, 0, -1), "kang"), ft)                    # 1 = swung back beside the shin
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


def write_signs(path):
    """The sign atlas (1024 x 1024): dark plates, light letters, a thin frame (variant 3)."""
    from PIL import Image, ImageDraw, ImageFont
    im = Image.new("RGB", (1024, 1024), (24, 27, 31))
    d = ImageDraw.Draw(im)
    for text, col, x0, y0, x1, y1 in SIGN_ATLAS:
        d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=(30, 34, 39), outline=(84, 92, 100), width=max(1, (y1 - y0) // 24))
        sz = int((y1 - y0) * 0.62)
        f = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", sz)
        while d.textlength(text, font=f) > (x1 - x0) * 0.92 and sz > 6:
            sz -= 1
            f = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", sz)
        d.text(((x0 + x1) / 2, (y0 + y1) / 2), text, font=f, fill=col, anchor="mm")
    a = np.asarray(im)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    h_, w_, _ = a.shape
    header = bytearray(128)
    header[0:4] = b"DDS "
    for off, v in {4: 124, 8: 0x100F, 12: h_, 16: w_, 20: w_ * 4, 76: 32, 80: 0x41, 88: 32, 92: 0x00FF0000, 96: 0x0000FF00,
                   100: 0x000000FF, 104: 0xFF000000, 108: 0x1000}.items():
        header[off:off + 4] = int(v).to_bytes(4, "little")
    bgra = np.empty((h_, w_, 4), np.uint8)
    bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3] = a[..., 2], a[..., 1], a[..., 0], 255
    with open(path, "wb") as f_:
        f_.write(header)
        f_.write(bgra.tobytes())
    im.save(os.path.splitext(path)[0] + "_preview.png")


def _tex_slot(name):
    """Texture slot (1-based, appended after the 7 screens) of an interior group: 8 floor, 9 wall, 10 ceiling, 11 hazard trim, 12 metal."""
    if name.startswith(("bridge", "command_bridge")) and not name.startswith("bridge_access"):
        return 0
    if name.endswith("_floor"):
        return 8
    if name.endswith("_ceil") or name.startswith("ceil_"):
        return 10
    if name.startswith("trim_"):
        return 11
    if name.startswith("metal_"):
        return 12
    if name.startswith(("walls_", "crew_deck", "lab_deck", "airlock", "tech_", "hangar_lock", "keel_bay", "bridge_access")):
        return 9
    return 0


def _uv_project(groups, m_per_tile=2.0):
    for g in groups:
        k = _tex_slot(g.name)
        if not k or not g.v:
            continue
        g.tex = k
        g.uv = []
        for p, n in zip(g.v, g.n):
            ax = int(np.argmax(np.abs(n)))
            if ax == 1:
                u, v = p[0], p[2]
            elif ax == 0:
                u, v = p[2], -p[1]
            else:
                u, v = p[0], -p[1]
            g.uv.append((u / m_per_tile, v / m_per_tile))


def write_msh(groups, path, textures=()):
    with open(path, "w", newline="\r\n") as f:
        f.write("MSHX1\n")
        f.write(f"GROUPS {len(groups)}\n")
        for g in groups:
            if not g.v:                                       # an empty group crashes D3D9Client on loading: one degenerate triangle keeps
                for _ in range(3):                            # the group (its index is a contract with the module) and draws nothing
                    g.vert((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0) if getattr(g, "tex", 0) else None)
                g.t.append((0, 1, 2))
            f.write(f"LABEL {g.name}\nMATERIAL {g.material}\nTEXTURE {getattr(g, 'tex', 0)}\n")
            f.write(f"GEOM {len(g.v)} {len(g.t)} ;{g.name}\n")
            has_uv = len(getattr(g, "uv", ())) == len(g.v) and len(g.v) > 0
            for k, (p, n) in enumerate(zip(g.v, g.n)):
                uvs = f" {g.uv[k][0]:.4f} {g.uv[k][1]:.4f}" if has_uv else ""
                f.write(f"{p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}{uvs}\n")
            for a, b, c in g.t:
                f.write(f"{a} {b} {c}\n")
        f.write(f"MATERIALS {len(MATERIALS)}\n")
        for m in MATERIALS:
            f.write(m[0] + "\n")
        for name, dif, spec, emi in MATERIALS:
            f.write(f"MATERIAL {name}\n")
            al = dif[3] if len(dif) > 3 else 1.0                       # alpha: glass
            f.write("{:.3f} {:.3f} {:.3f} {:.2f}\n".format(dif[0], dif[1], dif[2], al))
            f.write("{:.3f} {:.3f} {:.3f} {:.2f}\n".format(dif[0], dif[1], dif[2], al))
            f.write("{:.3f} {:.3f} {:.3f} 1 {:.0f}\n".format(*spec))
            f.write("{:.3f} {:.3f} {:.3f} 1\n".format(*emi))
        f.write(f"TEXTURES {len(textures)}\n")
        for tname in textures:
            f.write(tname + "\n")


def v3(a):
    return "{%.5f, %.5f, %.5f}" % tuple(a)


# ---------------------------------------------------------------------------
# Debris: every part that can break off becomes a vessel of its own (Config/Vessels/Tantra/Debris_*.cfg,
# Meshes/Tantra/Debris/*.msh), built from the same groups, centred on its own centroid. The hull breaks into
# four chunks by station. name, groups (or station range of the hull for a chunk), mass [t]
_HULL_GROUPS = ("hull", "shoulder", "nose", "spine", "pocket_liner", "airlock", "cups_nose", "iris_nose_0", "iris_nose_1")
HULL_ZONES = [("hull_stern", (-10.0, 21.0), 900.0), ("hull_traps", (21.0, 88.0), 1200.0), ("hull_hangar", (88.0, 121.0), 500.0),
              ("hull_living", (121.0, 145.0), 400.0), ("hull_nose", (145.0, 185.0), 400.0)]
FEET_DEBRIS = [(f"foot_{side}", 420.0) for side in SIDES] + [(f"sfoot{i}", 400.0) for i in range(4)] + [("kfoot", 150.0)]
LOWER_DEBRIS = ([(f"leg_{side}", [f"blade_{side}_{BLADE_N - 1}", f"ankle_{side}"], f"foot_{side}", 520.0) for side in SIDES]
                + [(f"sternleg_{i}", [f"leg{i}_sec{LEG_SEC_N - 1}", f"leg{i}_ankle"], f"sfoot{i}", 450.0) for i in range(4)]
                + [("kangleg", [f"kang_shin_{KANG_SEC_N - 1}", "kang_ankle"], "kfoot", 170.0)])
DEBRIS_DEFS = (
    [("crest_port", ["crest_port", "wing_outer_port", "elevon_port"], 22.0),
     ("crest_starboard", ["crest_starboard", "wing_outer_starboard", "elevon_starboard"], 22.0),
     ("fin", ["fin", "fin_upper"], 24.0)]
    + [(f"pod_{i}", [f"pod_{i}", f"door_pod_{i}", f"arm_pod_{i}"], 120.0) for i in range(4)]
    + [(f"leg_{side}", [f"blade_{side}_{i}" for i in range(1, BLADE_N)] + [f"ankle_{side}"] + foot_groups(f"foot_{side}"), 400.0) for side in SIDES]
    + [(f"sternleg_{i}", [f"leg{i}_sec{k}" for k in range(1, LEG_SEC_N)] + [f"leg{i}_ankle"] + foot_groups(f"sfoot{i}"), 110.0) for i in range(4)]
    + [("kangleg", [f"kang_shin_{k}" for k in range(1, KANG_SEC_N)] + ["kang_ankle"] + foot_groups("kfoot"), 150.0)]
    + [(n, [n], 6.0) for n in ("door_top_port", "door_top_starboard", "door_bottom_port", "door_bottom_starboard", "kang_door")]
    + [(n, [n], 10.0) for n in ("bay_door_port", "bay_door_starboard")]
    + [(f"{n}{v}", rng_, m_) + ((v[1:],) if v else ()) for n, rng_, m_ in HULL_ZONES for v in ("", "_flat", "_short")]
    # a leg breaks at its joints (core/Foot): the foot at the ankle, the last stage with the foot at the lowest joint
    + [(f"{pre}_whole", foot_groups(pre), mf) for pre, mf in FEET_DEBRIS]
    + [(f"{leg}_lower", segs + foot_groups(pre), ml) for leg, segs, pre, ml in LOWER_DEBRIS]
    # one petal torn off (keel, slats, rod, skirts) of each kind
    + [(f"petal_{kind}", [n for n in petal_groups(pre, 0) if "_strut_" not in n], mp) for kind, pre, mp in
       (("blade", "foot_port", 35.0), ("stern", "sfoot0", 30.0), ("kang", "kfoot", 12.0))])
DEBRIS = []  # filled by write_debris: (name, class, centroid (mesh frame), mass)


def write_debris(groups, root):
    G = {g.name: g for g in groups}
    mdir = os.path.join(root, "Meshes", "Tantra", "Debris")
    cdir = os.path.join(root, "Config", "Vessels", "Tantra")
    os.makedirs(mdir, exist_ok=True)
    os.makedirs(cdir, exist_ok=True)
    DEBRIS.clear()
    chunk_extra = {"hull_stern": ["well", "baffle", "cups_anamezon", "well_centre", "march_unit", "iris_march", "body_flap"],
                   "hull_traps": ["bay_liner"] + [f"trap_{i}" for i in range(4)],
                   "hull_hangar": ["hangar_inner", "shuttle", "rover_platform"],
                   "hull_nose": ["nose_ana", "ana_feed", "ana_buffer", "nose_screen", "nose_screen_core", "nose_mirror"]}
    for d_ in DEBRIS_DEFS:
        name, spec, mass = d_[:3]
        crush = d_[3] if len(d_) > 3 else None     # "flat": a belly blow pressed it to 55 %, "short": an axial one to 50 %
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
            parts += [G[n] for n in chunk_extra.get(name.replace("_flat", "").replace("_short", ""), [])]
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
        if crush:
            lo0 = (P.min(0) - c)
            for q in out:
                for k, v in enumerate(q.v):
                    v = np.array(v, float)
                    if crush == "flat":
                        v[1] = lo0[1] + (v[1] - lo0[1]) * 0.55
                    else:
                        v[2] *= 0.5
                    q.v[k] = v
            P = np.vstack([np.array(q.v) for q in out]) + c
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
        DEBRIS.append((name, f"Tantra\\Debris_{name}", c, mass * 1e3, -lo[1]))


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
    L.append(f"constexpr double kLegLMin = {LEG_LMIN:.3f}, kLegLMax = {LEG_LMAX:.3f}, kBladeL = {BLADE_L}, kBladeExt = {BLADE_EXT};  // hip -> ankle, stages in / out")
    L.append(f"constexpr double kFootH = {FOOT_H:.3f}, kFootR = {FOOT_KINDS['blade']['R']:.2f}, kHubR = {HUB_R};  // ankle above ground; cup foot")
    L.append(f"constexpr double kKangFootH = {KANG_FOOT_H:.3f};  // kangaroo ankle above ground")
    L.append(f"constexpr double kLegLMinS = {LEG_LMIN_S:.3f}, kLegExtMax = {LEG_EXT_MAX:.3f}, kLegFootH = {LEG_FOOT_H:.3f}, kLegFootR = {FOOT_KINDS['stern']['R']:.2f};  // stern legs (stowed along -z)")
    L.append(f"constexpr double kStrutExtC = {STRUT_EXT_C}, kStrutExtS = {STRUT_EXT_S};  // unloaded strut rod (= the static sag)")
    L.append(f"constexpr double kStrokeC = {STROKE_C}, kStrokeS = {STROKE_S};  // whole strut stroke: animation 0 bottomed, 1 unloaded")
    # gas cells: ring radius = the centre of pressure of a bay; anims per foot (port, starboard, stern 0..3, kangaroo);
    # each cell's direction from the foot's centre in the deployed pose (blades and kangaroo lying, stern legs standing)
    rc = lambda k: 2.0 / 3.0 * (FOOT_KINDS[k]['R'] ** 3 - PETAL[k]['rh'] ** 3) / (FOOT_KINDS[k]['R'] ** 2 - PETAL[k]['rh'] ** 2)
    L.append(f"constexpr int kCellN = {FOOT_RIBS};   // petals per foot")
    # the legs' groups for the damage: the stages below the hip (top to bottom), the ankle, the foot (contiguous), the petal
    # blocks inside it (keel, slats, strut, rod, skirts)
    prs = [f"foot_{s}" for s in ('port', 'starboard')] + [f"sfoot{i}" for i in range(4)] + ["kfoot"]
    segs = [[f"blade_{s}_{i}" for i in range(1, BLADE_N)] for s in ('port', 'starboard')] + \
           [[f"leg{i}_sec{k}" for k in range(1, LEG_SEC_N)] for i in range(4)] + [[f"kang_shin_{k}" for k in range(1, KANG_SEC_N)]]
    ankles = ["ankle_port", "ankle_starboard"] + [f"leg{i}_ankle" for i in range(4)] + ["kang_ankle"]
    firsts = []
    for pre in prs:
        fg = foot_groups(pre)
        f0 = GROUPS.index(fg[0])
        assert [GROUPS.index(n) for n in fg] == list(range(f0, f0 + len(fg))), pre
        assert foot_groups(pre)[2:2 + 9] == petal_groups(pre, 0), pre
        firsts.append(f0)
    L.append("constexpr int kLegSegN[7] = {" + ", ".join(str(len(sg)) for sg in segs) + "};")
    L.append("constexpr int kLegSeg[7][10] = {" + ", ".join("{" + ", ".join(str(GROUPS.index(n)) for n in sg + [sg[-1]] * (10 - len(sg))) + "}" for sg in segs) + "};   // stages below the hip, top to bottom")
    L.append("constexpr int kAnkleGrp[7] = {" + ", ".join(str(GROUPS.index(n)) for n in ankles) + "};")
    L.append("constexpr int kFootFirst[7] = {" + ", ".join(str(f) for f in firsts) + f"}}, kFootGrpN = {len(foot_groups(prs[0]))};")
    L.append("constexpr int kPetalFirst = 2, kPetalStride = 9, kPetalStrut = 4;   // in a foot: petal i = first + 2 + 9 i .. +9 (its strut at +4 stays on the collar)")
    L.append("constexpr double kCellRc[3] = {" + ", ".join(f"{rc(k):.3f}" for k in ('blade', 'stern', 'kang')) + "};  // petal centre of pressure: blade, stern, kangaroo")
    pres = [f"foot_{s}" for s in ('port', 'starboard')] + [f"sfoot{i}" for i in range(4)] + ["kfoot"]
    poses = preview_poses(legs)
    Mrest, Mstand = comp_matrices(comps, poses[1][1]), comp_matrices(comps, poses[3][1])
    an = []
    for c in comps:
        if c["anim"] not in an:
            an.append(c["anim"])
    rows_a, rows_d = [], []
    for l, pre in enumerate(pres):
        M = Mstand if 2 <= l <= 5 else Mrest
        up_want = np.array([0, 0, 1.0]) if 2 <= l <= 5 else np.array([0, 1.0, 0])
        ds = []
        for i in range(FOOT_RIBS):
            ci, u, ey = CELL_REF[(pre, i)]
            R = M[ci][:3, :3]
            assert float((R @ ey) @ up_want) > 0.99, (pre, i, R @ ey)
            ds.append(R @ u)
        rows_a.append("{" + ", ".join(str(an.index(f"{pre}_petal_{i}")) for i in range(FOOT_RIBS)) + "}")
        rows_d.append("{" + ", ".join(v3(d) for d in ds) + "}")
    L.append("constexpr int kCellAnim[7][12] = {" + ", ".join(rows_a) + "};")
    L.append("constexpr V kCellDir[7][12] = {\n    " + ",\n    ".join(rows_d) + "};")
    yb = -FL * wh_at((KANG_S0 + KANG_S1) / 2)[1]
    L.append(f"// Kangaroo leg: hip axis (mesh frame), thigh, shin range, knee offset off the hip-foot line, foot ahead of the hip while lying.")
    L.append(f"constexpr V kKangHip = {v3([KANG_THIGH_X, yb + KANG_DEPTH - KANG_THIGH_W / 2 - 0.1, zs(KANG_HIP_S)])};")
    L.append(f"constexpr double kKangThigh = {KANG_THIGH_L}, kKangShinMin = {KANG_SHIN_MIN}, kKangShinMax = {KANG_SHIN_MAX}, kKangHipMax = {KANG_HIP_MAX:.6f}, kKangKneeE = {KANG_KNEE_E}, kKangFootFwd = {KANG_FOOT_FWD}, kKangFootR = {FOOT_KINDS['kang']['R']:.2f};")
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
    # Hull contact points (touchdown vertices for the belly, the nose and the stern): rings of the real hull section
    # every ~8 m from the stern to the nose tip, 10 points round each (u from the bottom centre), and the tip itself.
    # x, y in the mesh frame, s = station (the module turns it into its frame).
    hp = []
    n_ring = int(math.ceil(TIP_S / 8.0))
    for i in range(n_ring + 1):
        st = min(TIP_S - 0.5, i * TIP_S / n_ring)
        for k in range(10):
            x, y = hull_xy(st, k / 10.0)
            hp.append((x, y, st))
    hp.append((0.0, _YTIP, TIP_S))
    L.append(f"constexpr int kHullPtN = {len(hp)};  // hull contact points (x, y, station)")
    L.append("constexpr double kHullPts[kHullPtN][3] = {" + ", ".join("{%.3f, %.3f, %.2f}" % q for q in hp) + "};")
    L.append(f"constexpr double kHangarS0 = {HANGAR_S[0]}, kHangarS1 = {HANGAR_S[1]}, kAirlockS = {AIRLOCK_S}, kShipLength = {L_SHIP};")
    L.append(f"constexpr double kLockOut = {LOCK_OUT}, kLockDrop = {LOCK_DROP}, kLockX0 = {LOCK_X0}, kLockX1 = {LOCK_X1}, "
             f"kLockDeckY = {CAB_Y0}, kLockMastTop = {LOCK_MAST_TOP}, kLockMastStub = {LOCK_MAST_STUB}, kLockMastFull = {LOCK_MAST_FULL:.3f};  // crew lift: arm travel, descent, cabin x, floor, mast pivot, stowed and full mast")
    L.append(f"constexpr double kElevonUpDeg = {ELEV_UP}, kElevonDownDeg = {ELEV_DOWN}, kBodyFlapDownDeg = {FLAP_DOWN};")
    L.append("constexpr double kTrapXY[4][2] = {" + ", ".join("{%.2f, %.2f}" % t for t in TRAP_XY) + "};  // lower stbd, lower port, upper stbd, upper port")
    L.append(f"constexpr double kTrapZ = {zs((CASS_S0 + CASS_S1) / 2):.3f}, kCassW = {CASS_W}, kCassLen = {CASS_S1 - CASS_S0:.1f}, kTrapMouthY = {TRAP_MOUTH_Y};")
    L.append(f"constexpr double kLiftY0 = {LIFT_Y0}, kLiftTravel = {LIFT_TRAVEL};  // head (= cassette centre) at rest, travel down")
    L.append("// Planetary pods (reference = stowed in the flank bays): the arm runs the pod out along `axis` (outward flank")
    L.append("// normal) by `travel`; the pod turns about `axis` through `pivot` (0 = cups aft .. kPodSwivelMax = 180 deg).")
    L.append(f"constexpr int kPodCount = {len(PODS)};")
    L.append(f"constexpr double kPodSwivelMax = {POD_SWIVEL_MAX:.6f}, kPodCupR = {POD_CUP_R};")
    L.append("struct PodRig { double s; V pivot, axis; double travel; V cup[3]; };")
    L.append("constexpr PodRig kPods[kPodCount] = {")
    for p in PODS:
        L.append(f"    {{{p['s']}, {v3(p['centre'])}, {v3(p['n'])}, {p['travel']:.4f}, {{{', '.join(v3(c) for c in p['cups'])}}}}},")
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
    L.append("// Compartments (TantraVC.msh skeleton): stations, floor/ceiling, half width, camera point (mesh frame).")
    L.append("struct Compartment { const char* key; double s0, s1, y0, y1, xHalf, xCentre; V cam; };")
    L.append(f"constexpr int kCompartmentCount = {len(COMPARTMENTS)};")
    L.append("constexpr Compartment kCompartments[kCompartmentCount] = {")
    for c in COMPARTMENTS:
        L.append(f'    {{"{c["key"]}", {c["s0"]:.1f}, {c["s1"]:.1f}, {c["y0"]:.2f}, {c["y1"]:.2f}, {c["hx"]:.1f}, {c["xc"]:.1f}, {v3(c["cam"])}}},')
    L.append("};\n")
    L.append("// Command bridge capsule: drum turning about the cross axis x; counter-rotates with the hull pitch (0 lying, ~72.5 stella, 90 standing).")
    L.append(f"constexpr double kBridgeS = {BR_S}, kBridgeY = {BR_Y}, kBridgeR = {BR_R}, kBridgeHx = {BR_HX}, kBridgeZ = {zs(BR_S):.3f};\n")
    L.append("// Debris vessels (tools/gen_mesh.py write_debris): class, spawn point = centroid in the mesh frame.")
    L.append("struct DebrisDef { const char* name; const char* cls; V centre; double mass; double cogH; };   // cogH: COG over the ground lying")
    L.append(f"constexpr int kDebrisCount = {len(DEBRIS)};")
    L.append("constexpr DebrisDef kDebris[kDebrisCount] = {")
    for name, cls, c, mass, cog in DEBRIS:
        L.append(f'    {{"{name}", "{cls.replace(chr(92), chr(92) * 2)}", {v3(c)}, {mass:.0f}, {cog:.2f}}},')
    L.append("};\n\n}  // namespace tantra::mesh\n")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(L))


def kang_states(hip_h, fwd=KANG_FOOT_FWD):
    """Rig states of the kangaroo leg with the hip hip_h above the ground and the foot hub fwd metres ahead, knee aft,
    knee offset KANG_KNEE_E off the hip-foot line (planar IK in the pitch plane; the foot stays level)."""
    d = math.hypot(hip_h - KANG_FOOT_H, fwd)
    e = min(KANG_KNEE_E, KANG_THIGH_L * 0.9)
    along = math.sqrt(KANG_THIGH_L ** 2 - e * e)
    a_hf = math.atan2(fwd, hip_h - KANG_FOOT_H)                    # hip->foot from straight down, forward positive
    a_th = a_hf - math.atan2(e, along)                        # thigh leans aft of that line
    shin_v = np.array([fwd, -(hip_h - KANG_FOOT_H)]) - KANG_THIGH_L * np.array([math.sin(a_th), -math.cos(a_th)])  # (fwd, up)
    shin_l = float(np.linalg.norm(shin_v))
    a_sh = math.atan2(shin_v[0], -shin_v[1])
    knee = math.pi - (a_sh - a_th)                            # 180 = straight
    return {"kang_door": 1.0, "kang_hip": (math.pi / 2 + a_th) / KANG_HIP_MAX, "kang_knee": knee / math.pi,
            "kang_ext": (shin_l - ANKLE_R - KANG_SHIN_MIN) / (KANG_SHIN_MAX - KANG_SHIN_MIN),
            "kang_foot": a_sh / math.pi, "kang_fold": 0.0}


def stowed_states():
    """Flight at 0.9 c: everything folded, doors shut, fin down."""
    st = {"crest_lateral": 1, "wing_outer": 1, "crest_dorsal": 1, "pod_retract": 1, "kang_fold": 1.0}
    for i in range(4):
        st.update({f"leg{i}_rail": 1.0, f"leg{i}_fold": 1.0})
    for s in SIDES:
        st.update({f"track_{s}": 1.0, f"slide_{s}": 1.0, f"pitch_{s}": 1.0,
                   f"blade_ext_{s}": 1.0, f"foot_fold_{s}": 1.0})
    return st


def preview_poses(legs):
    """Named poses: flight (stowed), resting level (pods hovering), turning 45 deg, standing, hangar open."""
    mid = (58.1 - CAR_S0) / (STOW_S - CAR_S0)                  # loaded CG (track state runs S0 .. STOW_S)
    shin = lambda L: (LEG_LMAX - L) / (LEG_LMAX - LEG_LMIN)
    stowed = stowed_states()
    yb = -FL * wh_at((KANG_S0 + KANG_S1) / 2)[1]
    hip_h = AXIS_H + yb + KANG_DEPTH - KANG_THIGH_W / 2 - 0.1
    rest = {"pod_retract": 0, "pod_swivel": 0.5, **kang_states(hip_h)}
    for i in range(4):
        rest.update({f"leg{i}_rail": 1.0, f"leg{i}_fold": 1.0})
    for s in SIDES:
        rest.update({f"track_{s}": 0.0, f"blade_ext_{s}": shin(AXIS_H - FOOT_H)})
    turn_h = 58.1 + 22.5
    turn = {"crest_lateral": 1, "wing_outer": 1, "crest_dorsal": 1, "pod_retract": 1, "kang_fold": 1.0}
    for i in range(4):
        turn.update({f"leg{i}_rail": 1.0, f"leg{i}_fold": 1.0})
    for s in SIDES:
        turn.update({f"track_{s}": mid, f"pitch_{s}": 0.5, f"blade_ext_{s}": shin(turn_h - FOOT_H)})
    stand = dict(stowed)
    for i, g in enumerate(legs):
        stand.update({f"leg{i}_swing": g["phi_stand"] / g["phi_max"], f"leg{i}_ext": g["e_stand"] / LEG_EXT_MAX, f"leg{i}_foot_stand": 1,
                      f"leg{i}_rail": 0.0, f"leg{i}_fold": 0.0})
    hang = dict(rest)
    hang.update({"hangar": 1, "rover_lift": 1, "bay_doors": 1, "elevon_port": 1, "elevon_starboard": 0, "body_flap": 1})
    # ship pose: pitch about the hip (trunnion) at s 53, lift of that point above its level height
    return [("flight (stowed)", stowed, 0, 0, (15, -60)), ("resting level", rest, 0, 0, (10, -120)),
            ("turning 45 deg", turn, 45, turn_h - AXIS_H, (8, -80)), ("standing", stand, 90, 22.5 + 58.1 - AXIS_H, (8, -60)),
            ("hangar open", hang, 0, 0, (-15, -60))]


def pose_world(P, pitch, lift, pivot_s=58.1):
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


def write_interior_layout(path, vc_groups=()):
    """InteriorLayout.h: collision boxes of the interior (walls, furniture) and floors, the bridge ones separately (the capsule turns),
    room rectangles for the HUD. Used by the walk mode of the module."""
    def split(sel):
        solid, flr = [], []
        for tag, lo, hi in COLL:
            if not sel(tag): continue
            h = hi[1] - lo[1]
            (flr if tag == "stair" or (h < 0.2 and hi[0] - lo[0] > 0.25 and hi[2] - lo[2] > 0.25) else solid).append((lo, hi))
        return solid, flr
    w_s, w_f = split(lambda t: t != "bridge")
    b_s, b_f = split(lambda t: t == "bridge")
    b_f.append(((-BR_HX, BR_FLOOR_Y - 0.06, zs(BR_S) - BR_FZ), (BR_HX, BR_FLOOR_Y, zs(BR_S) + BR_FZ)))
    b_f.append(((-BR_DOOR_HW, BR_FLOOR_Y - 0.06, BR_DOOR_ZW - 0.3), (BR_DOOR_HW, BR_FLOOR_Y, zs(BR_S) - BR_FZ + 0.05)))   # the sill to the vestibule
    L = ["// Generated by tools/gen_mesh.py: collision data of the interior (mesh frame, metres).",
         "#pragma once", "namespace tantra::interior {", "struct Box { float x0, y0, z0, x1, y1, z1; };"]

    def arr(name, lst):
        L.append(f"constexpr int {name}Count = {len(lst)};")
        L.append(f"constexpr Box {name}[{max(1, len(lst))}] = {{")
        for lo, hi in lst:
            L.append("    {%.3ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff}," % (lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))
        L.append("};")
    arr("kSolids", w_s); arr("kFloors", w_f); arr("kBridgeSolids", b_s); arr("kBridgeFloors", b_f)
    L.append(f"constexpr double kBridgeAxisY = {BR_Y}, kBridgeAxisZ = {zs(BR_S):.3f}, kBridgeR = {BR_R}, kBridgeHx = {BR_HX}, kBridgeFloorY = {BR_FLOOR_Y};")
    # spawn points for the walk mode: a free spot near the middle of some key places (found among the collision boxes)
    solids_np = np.array([[*lo, *hi] for lo, hi in w_s]) if w_s else np.zeros((0, 6))
    floors_np = np.array([[*lo, *hi] for lo, hi in w_f]) if w_f else np.zeros((0, 6))

    def free_near(x0, x1, z0, z1, feet):
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
        cand = sorted(((x, z) for x in np.arange(x0 + .3, x1 - .3, .2) for z in np.arange(z0 + .3, z1 - .3, .2)), key=lambda q: (q[0] - cx) ** 2 + (q[1] - cz) ** 2)
        for x, z in cand:
            sel = solids_np[(solids_np[:, 4] > feet + .35) & (solids_np[:, 1] < feet + 1.85)] if len(solids_np) else solids_np
            if len(sel):
                dx = x - np.clip(x, sel[:, 0], sel[:, 3]); dz = z - np.clip(z, sel[:, 2], sel[:, 5])
                if np.any(dx * dx + dz * dz < .35 ** 2): continue
            fl = floors_np[(floors_np[:, 0] <= x) & (x <= floors_np[:, 3]) & (floors_np[:, 2] <= z) & (z <= floors_np[:, 5]) & (floors_np[:, 4] <= feet + .45) & (floors_np[:, 4] >= feet - .2)] if len(floors_np) else floors_np
            if len(fl): return x, float(fl[:, 4].max()), z
        return None
    spawns = [("bridge", 0.0, BR_FLOOR_Y, zs(BR_S) - 0.5, 1)]
    rect = {r["key"]: (r["x0"], r["x1"], zs(r["s0"]), zs(r["s1"]), r["y0"] + .075) for r in ROOMS}
    cmp_ = {c["key"]: c for c in COMPARTMENTS}
    pc = cmp_["tech_passage"]
    rect["tech_passage"] = (pc["xc"] - pc["hx"], pc["xc"] + pc["hx"], zs(55.0), zs(65.0), 5.575)
    for nm, key in (("upper_lobby", "lobby_u"), ("lower_lobby", "lobby_l"), ("keel_corridor", "corr_k_s"), ("roof_passage", "tech_passage"), ("upper_corridor", "corr_u")):
        if key in rect:
            x0, x1, z0, z1, ft = rect[key]
            pt = free_near(x0, x1, z0, z1, ft)
            if pt: spawns.append((nm, pt[0], pt[1], pt[2], 0))
    L.append("struct Spawn { const char* name; float x, feet, z; int bridge; };")
    L.append(f"constexpr int kSpawnCount = {len(spawns)};")
    L.append("constexpr Spawn kSpawns[] = {")
    for nm, x, ft, z, br in spawns:
        L.append('    {"%s", %.3ff, %.3ff, %.3ff, %d},' % (nm, x, ft, z, br))
    L.append("};")
    gi = {g.name: i for i, g in enumerate(vc_groups)}
    L.append("// TantraVC.msh: the centre zone of the screen (the Orbiter MFD is drawn on it), the lamps of the screen button and its position")
    L.append("// The big screen: per zone (left, centre, right) the camera yaw (to starboard), pitch, vertical fov (deg), width/height")
    L.append("// the bridge door: at the back of the capsule on the centre line (x 0); half width, top; one may go back to z kDoorZ1")
    L.append("constexpr double kDoorHalfW = %.3f, kDoorTop = %.3f, kDoorZ1 = %.3f;" % (BR_DOOR_HW, BR_DOOR_YC + BR_DOOR_BT, BR_DOOR_ZW - 0.3))
    L.append("// where a person arrives inside from the lift (the airlock cell), facing inboard")
    L.append("constexpr double kArrivalX = %.3f, kArrivalY = %.3f, kArrivalZ = %.3f;" % ((LOCK_X0 + LOCK_X1) / 2, CAB_Y0 + 0.12, zs(AIRLOCK_S)))
    L.append("// the lift cabin (stowed): x range, floor top, roof, half length along z, centre z, hull-side door half width; the lift panel: x of the 3 buttons")
    L.append("// (1 suit checks, 2 pressure equalisation, 3 lift), floor y and z of the wall they sit on")
    L.append("constexpr double kCabX0 = %.3f, kCabX1 = %.3f, kCabFloor = %.3f, kCabRoof = %.3f, kCabHz = %.3f, kCabZ = %.3f, kCabDoorHw = 0.65;" % (LOCK_X0, LOCK_X1, CAB_Y0 + 0.12, CAB_Y1, CAB_HZ, zs(AIRLOCK_S)))
    L.append("constexpr double kPanelBtnX[3] = {%.3f, %.3f, %.3f}, kPanelBtnY = %.3f, kPanelBtnZ = %.3f;" % (PANEL_X + (U_Y0 + 0.075, zs(130.6 - 0.14))))
    L.append("constexpr double kPanelBtnCY = %.3f, kPanelBtnCZ = %.3f;   // the button caps: centre height, front (mouse pick)" % tuple(PANEL_BTN))
    gi2 = {g.name: i for i, g in enumerate(vc_groups)}
    L.append("// the panel pieces on the console: 2D panel id, texture rect (px of panel.dds), centre, right, up, normal, size (m)")
    L.append("struct PanelPiece { int panel; int tx0, ty0, tx1, ty1; double c[3], ex[3], up[3], n[3], w, h; };")
    L.append("constexpr unsigned kPanelSlot = %du; constexpr int kPanelPieceCount = %d;" % (PANEL_SLOT, len(PANEL_PIECES)))
    L.append("constexpr PanelPiece kPanelPieces[%d] = {" % max(1, len(PANEL_PIECES)) + ", ".join(
        "{%d, %d, %d, %d, %d, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, %.4f, %.4f}" %
        ((pp[0],) + tuple(pp[1]) + tuple(pp[2]) + tuple(pp[3]) + tuple(pp[4]) + tuple(pp[5]) + (pp[6], pp[7])) for pp in PANEL_PIECES) + "};")
    L.append("// the commander's touch screens: 0 / 1 left / right curved monitor, 2 the concave centre screen, 3 right riser, 4 left riser,")
    L.append("// 5 the attitude keys, 6 / 7 left / right wing shelf: middle piece (c, ex, up, n), the whole size (w along, h), texture slot")
    L.append("struct TouchPlace { double c[3], ex[3], up[3], n[3], w, h; unsigned slot; };")
    L.append("constexpr int kTouchCount = %d;" % len(TOUCH_PLACES))
    L.append("constexpr TouchPlace kTouch[%d] = {" % max(1, len(TOUCH_PLACES)) + ", ".join(
        "{{%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, %.4f, %.4f, %du}" %
        (tuple(d[0]) + tuple(d[1]) + tuple(d[2]) + tuple(d[3]) + (d[4], d[5], d[6])) for d in TOUCH_PLACES) + "};")
    L.append("// the curved screens in flat pieces (touch): centre, right, up, normal, size, screen, its u and v range (v down)")
    L.append("struct TouchFacet { double c[3], ex[3], up[3], n[3], w, h; int screen; double u0, u1, v0, v1; };")
    L.append("constexpr int kTouchFacetCount = %d;" % len(TOUCH_FACETS))
    L.append("constexpr TouchFacet kTouchFacets[%d] = {" % max(1, len(TOUCH_FACETS)) + ", ".join(
        "{{%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, %.4f, %.4f, %d, %.4f, %.4f, %.4f, %.4f}" %
        (tuple(f[0]) + tuple(f[1]) + tuple(f[2]) + tuple(f[3]) + tuple(f[4:])) for f in TOUCH_FACETS) + "};")
    L.append("// the commander's curved monitors fold back about their bottom edge: groups (screen, housing), pivot, axis (a positive angle")
    L.append("// leans the top away from him), the angle of the full fold (сложен) and of the half state (свёрнут) as a fraction of it")
    L.append("constexpr int kSideDispGrp[2][2] = {%s};" % ", ".join("{%d, %d}" % (gi.get(d[0].name, -1), gi.get(d[1].name, -1)) for d in SIDE_DISP))
    L.append("constexpr double kSideFoldPivot[2][3] = {%s}, kSideFoldAxis[2][3] = {%s};" % (
        ", ".join("{%.4f, %.4f, %.4f}" % tuple(f[0]) for f in SIDE_FOLD), ", ".join("{%.4f, %.4f, %.4f}" % tuple(f[1]) for f in SIDE_FOLD)))
    L.append("constexpr double kSideFoldAngle = %.4f, kSideFoldHalf = %.4f;" % (BR_MON_FOLD[1], BR_MON_FOLD[0] / BR_MON_FOLD[1]))
    L.append("// the side glasses (variant 7): the translation that takes each down along its slant into its bay in the desk")
    L.append("constexpr double kSideRise[2][3] = {%s};" % ", ".join("{%.4f, %.4f, %.4f}" % tuple(r) for r in SIDE_RISE))
    L.append("// the bridge MFDs (touch, square): centre, right, up, normal (interior frame), size (m), texture slot, mode")
    L.append("struct MfdPlace { double c[3], ex[3], up[3], n[3], s; unsigned slot; int mode; };")
    L.append("constexpr int kMfdCount = %d;" % len(MFD_PLACES))
    L.append("constexpr MfdPlace kMfd[%d] = {" % max(1, len(MFD_PLACES)) + ", ".join(
        "{{%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, %.3f, %du, %d}" %
        (tuple(m[0]) + tuple(m[1]) + tuple(m[2]) + tuple(m[3]) + (m[4], MFD_SLOT0 + k, m[5])) for k, m in enumerate(MFD_PLACES)) + "};")
    L.append("constexpr unsigned kLiftStatusSlot = 21u;   // the status screen's texture slot in TantraVC (drawn by the module)")
    L.append("constexpr int kLiftBtnLit[3] = {%s}, kLiftBtnDim[3] = {%s};   // TantraVC groups of the caps" %
             (", ".join(str(gi2.get(f"lift_btn{k}_lit", -1)) for k in range(3)), ", ".join(str(gi2.get(f"lift_btn{k}_dim", -1)) for k in range(3))))
    L.append("// the lift: door B (two leaves, built open, each slides kDoorBTravel to the middle: a -> +z, b -> -z), its half width;")
    L.append("// the cabin's panel: the groups that ride with the cabin, the caps (DOWN, UP, OUT) lit / dim and their fronts (stowed), the screen's slot")
    L.append("constexpr int kDoorBGroups[4] = {%s}; constexpr double kDoorBTravel = %.3f, kDoorBHw = %.3f;" %
             (", ".join(str(gi2.get(n_, -1)) for n_ in ("lift_doorB_a", "lift_doorB_a_edge", "lift_doorB_b", "lift_doorB_b_edge")), DOORB_HW, DOORB_HW))
    L.append("// the inner lift (lobby shaft): cab groups, cab floor x/z range, the three stop floors, the door in the east shaft wall")
    L.append("constexpr int kILiftGroups[2] = {%d, %d};" % (gi2.get("ilift_cab", -1), gi2.get("ilift_light", -1)))
    L.append("constexpr double kILiftX0 = %.3f, kILiftX1 = %.3f, kILiftZ0 = %.3f, kILiftZ1 = %.3f, kILiftDoorX = %.3f, kILiftDoorZ = %.3f, kILiftDoorHw = %.3f;"
             % (ILIFT["x0"], ILIFT["x1"], zs(ILIFT["s0"]), zs(ILIFT["s1"]), ILIFT["door_x"], zs(ILIFT["door_s"]), ILIFT["door_hw"]))
    L.append("constexpr double kILiftStop[3] = {%.3f, %.3f, %.3f};" % ILIFT["stops"])
    L.append("constexpr int kILiftDoor[3] = {%s}; constexpr int kILiftDoorN[3] = {%s}; constexpr int kILiftSend[3] = {%s}; constexpr int kILiftCall[3] = {%s};" % tuple(
        ", ".join(str(gi2.get(f"ilift_{n_}{k}", -1)) for k in range(3)) for n_ in ("door", "doorN", "send", "call")))
    L.append("constexpr double kILiftCallPos[3][3] = {%s};   // the call buttons' fronts (forward end of the shaft, facing +z)" % ", ".join(
        "{%.3f, %.3f, %.3f}" % (-1.55, ys + 1.2, zs(ILIFT["s1"]) + 0.12) for ys in ILIFT["stops"]))
    L.append("constexpr double kILiftSendPos[3][3] = {%s};   // the send buttons' fronts, the cab at the lower stop" % ", ".join(
        "{%.3f, %.3f, %.3f}" % (ILIFT["x0"] + 0.08, ILIFT["stops"][0] + 1.25, zs(ILIFT["door_s"]) + (k - 1) * 0.3) for k in range(3)))
    cabg = [n_ for n_ in ("cab_panel", "cab_screen", "cab_metal") + tuple(f"cab_btn{k}_{w}" for k in range(3) for w in ("lit", "dim"))
            + ("cab_door_a", "cab_door_b", "cab_door_edge", "cab_door_glass")]
    L.append("constexpr int kCabRideCount = %d; constexpr int kCabRide[%d] = {%s};" % (len(cabg), len(cabg), ", ".join(str(gi2.get(n_, -1)) for n_ in cabg)))
    L.append("constexpr int kCabBtnLit[3] = {%s}, kCabBtnDim[3] = {%s};" %
             (", ".join(str(gi2.get(f"cab_btn{k}_lit", -1)) for k in range(3)), ", ".join(str(gi2.get(f"cab_btn{k}_dim", -1)) for k in range(3))))
    L.append("constexpr double kCabBtn[3][3] = {%s};" % ", ".join("{%.3f, %.3f, %.3f}" % b_ for b_ in CAB_BTN))
    L.append("constexpr unsigned kCabScreenSlot = 36u;   // the cabin's screen (drawn by the module)")
    L.append("constexpr int kCabDoorA[3] = {%d, %d, %d}, kCabDoorB = %d; constexpr double kCabDoorTravel = 0.66;   // the cabin's doors: a (+edge, +porthole glass) slides -z, b +z" %
             (gi2.get("cab_door_a", -1), gi2.get("cab_door_edge", -1), gi2.get("cab_door_glass", -1), gi2.get("cab_door_b", -1)))
    L.append("constexpr double kCabLight[3] = {%.3f, %.3f, %.3f};   // the cabin's ceiling light (stowed): a point light that rides with it" %
             ((LOCK_X0 + LOCK_X1) / 2, CAB_Y1 - 0.45, zs(AIRLOCK_S)))
    L.append("constexpr double kCabSpot[2][3] = {{%.3f, %.3f, %.3f}, {%.3f, %.3f, %.3f}}, kCabSpotDir[3] = {-0.45, -0.893, 0.0};   // the floodlights (stowed)" %
             (LOCK_X0 - 0.1, CAB_Y1 - 0.27, zs(AIRLOCK_S) - CAB_HZ + 0.2, LOCK_X0 - 0.1, CAB_Y1 - 0.27, zs(AIRLOCK_S) + CAB_HZ - 0.2))
    L.append("constexpr double kZoneCapR = %.3f, kCabCapR = %.3f;   // the push buttons' half sizes (mouse pick)" % (ZONE_CAP_H, CAB_CAP_H))
    L.append("// watch lighting of the way from the bridge to the lift: point lights (mesh frame) and their colours")
    L.append("struct WatchLight { double x, y, z, r, g, b; };")
    L.append("constexpr int kWatchLightCount = %d;" % len(W_LIGHTS))
    L.append("constexpr WatchLight kWatchLights[] = {" + ", ".join("{%.3f, %.3f, %.3f, %.2f, %.2f, %.2f}" % (tuple(p_) + tuple(c_)) for p_, c_ in W_LIGHTS) + "};")
    L.append("// the bridge capsule's watch lights: warm, on the axis of the capsule (they stay right whatever its turn); used instead of the")
    L.append("// way's lights while the viewer is in the capsule (D3D9Client takes 4 local lights per mesh)")
    L.append("constexpr WatchLight kBridgeWatchLights[] = {{%.3f, %.3f, %.3f, 0.78, 0.52, 0.28}, {%.3f, %.3f, %.3f, 0.78, 0.52, 0.28}};" % (-2.4, BR_Y, zs(BR_S), 2.4, BR_Y, zs(BR_S)))
    L.append("struct ScreenZone { double yaw, pitch, vfov, aspect; unsigned slot; };")
    L.append("constexpr int kScreenZoneCount = %d;" % len(BR_SCR_CAMS))
    L.append("constexpr ScreenZone kScreenZones[%d] = {" % len(BR_SCR_CAMS) + ", ".join("{%.3f, %.3f, %.3f, %.4f, %du}" % c for c in BR_SCR_CAMS) + "};")
    z0, z1, y0, y1 = BR_AST
    L.append("// astronomer's screen (texture slot 7) on the port end wall: width/height")
    L.append("constexpr unsigned kAstroSlot = 7u; constexpr double kAstroAspect = %.4f;" % ((z1 - z0) / (y1 - y0)))
    L.append("// the main screen's frame: its blinking status light (group, -1 none)")
    L.append("constexpr int kGrpScrBlink = %d;" % gi.get("bridge_scr_blink", -1))
    L.append("// the moving seats: travel to the console (along its facing), collision box (half x, z0, z1 along the facing, height), groups")
    L.append("constexpr double kSeatTravel[4] = {%.3f, %.3f, %.3f, %.3f}; constexpr double kSeatBox[4] = {%.3f, %.3f, %.3f, %.3f};" % (tuple(BR_SEAT_TRAVEL) + BR_SEAT_BOX))
    rows = []
    for i in range(len(BR_SEATS)):
        ids = [gi.get(f"bridge_seat{i}{suf}", -1) for suf in ("", "_cushion", "_metal", "_belt", "_accent")]
        if i == 0: ids += [gi.get("bridge_button_on", -1), gi.get("bridge_button_off", -1), gi.get("bridge_seat_adj", -1),
                           gi.get("bridge_seat0_base", -1), gi.get("bridge_cur_ball", -1), gi.get("bridge_cur_lit", -1)]
        ids += [-1] * (12 - len(ids))
        rows.append("{" + ", ".join(str(v) for v in ids) + "}")
    L.append("constexpr int kSeatGroups[4][12] = {" + ", ".join(rows) + "};   // -1: none")
    L.append("// the commander's seat (variant 7): its base (the sled, the pedestal's outer tube) does not go up and down; the cursor unit's ball dark / lit")
    L.append("constexpr int kSeat0Base = %d, kCurBallGrp = %d, kCurLitGrp = %d, kSpotGrp = %d;" % (gi.get("bridge_seat0_base", -1), gi.get("bridge_cur_ball", -1), gi.get("bridge_cur_lit", -1), gi.get("bridge_spot", -1)))
    L.append("constexpr int kSpotDiscs = %d;   // the light spot: discs of 13 vertices (centre, 12 round), front then back faces" % len(BR_SPOT_R))
    L.append("constexpr double kSpotR[%d] = {%s};" % (len(BR_SPOT_R), ", ".join("%.4f" % r for r in BR_SPOT_R)))
    L.append("// the sensor strips ХОД, ВЫСОТА on his right armrest and the cursor unit's key plate on the left one: centres at the seat's rest place")
    L.append("// (height 0), sizes across / along the facing; one texture (kSeatAdjSlot) for all three, kSeatPanelPx px per metre, v 0 at the front")
    L.append("constexpr double kSeatAdjC[3] = {%.4f, %.4f, %.4f}, kSeatHgtC[3] = {%.4f, %.4f, %.4f}, kCurPlateC[3] = {%.4f, %.4f, %.4f};" % tuple(BR_ADJ_C[0] + BR_ADJ_C[1] + BR_ADJ_C[2]))
    L.append("constexpr double kSeatStripLen = %.3f, kSeatStripWid = %.3f, kCurPlateW = %.3f, kCurPlateD = %.3f; constexpr unsigned kSeatAdjSlot = %du;" % (BR_STRIPS[2], BR_STRIPS[3], BR_CUR[2], BR_CUR[3], BR_ADJ_SLOT))
    L.append("constexpr int kSeatPanelW = %d, kSeatPanelH = %d; constexpr double kSeatPanelPx = %.1f;" % BR_SEAT_PANEL)
    L.append("constexpr double kCurBall[4] = {%.4f, %.4f, %.4f, %.4f};   // the ball: x, y, z (interior frame, at the rest place), radius" % (BR_SEATS[0][0] + BR_BALL[0], BR_FLOOR_Y + BR_BALL[1], zs(BR_S) + BR_SEATS[0][1] + BR_BALL[2], BR_BALL[3]))
    L.append("constexpr double kSeatAdjMin = %.2f, kSeatAdjMax = %.2f;   // the seat's own travel at the console (m along the facing)" % BR_SEAT_ADJ)
    L.append("constexpr double kSeatHgtMin = %.2f, kSeatHgtMax = %.2f, kSeatHgtStart = %.2f;   // the commander's seat's height (m)" % BR_SEAT_HGT)
    for nm, key in (("kGrpScreenC", "bridge_screen_c"), ("kGrpMfd", "bridge_mfd"), ("kGrpBtnOn", "bridge_button_on"), ("kGrpBtnOff", "bridge_button_off")):
        L.append(f"constexpr unsigned {nm} = {gi.get(key, 0)}u;")
    L.append("constexpr double kBtnX = %.3f, kBtnY = %.3f, kBtnZ = %.3f;" % (0.36, BR_FLOOR_Y + 0.78, zs(BR_S) + BR_SEATS[0][1] + 0.16))
    L.append("struct Seat { const char* name; float x, z, fx, fz; };   // the empty seat's place, its facing")
    L.append("constexpr Seat kSeats[] = {")
    for nm, (sx, sz, _ac, _ct), (fx, fz) in zip(("commander", "console_starboard", "console_port", "navigator"), BR_SEATS, BR_SEAT_DIR):
        L.append('    {"%s", %.3ff, %.3ff, %.4ff, %.4ff},' % (nm, sx, zs(BR_S) + sz, fx, fz))
    L.append("};")
    L.append("struct RoomRect { const char* name; float x0, x1, z0, z1, y0, y1; };")
    rooms = [(r["key"], r["x0"], r["x1"], zs(r["s0"]), zs(r["s1"]), r["y0"], r["y1"]) for r in ROOMS]
    rooms += [(c["key"], c["xc"] - c["hx"], c["xc"] + c["hx"], zs(c["s0"]), zs(c["s1"]), c["y0"], c["y1"]) for c in COMPARTMENTS]
    L.append(f"constexpr int kRoomCount = {len(rooms)};")
    L.append("constexpr RoomRect kRooms[] = {")
    for nm, x0, x1, z0, z1, y0, y1 in rooms:
        L.append('    {"%s", %.2ff, %.2ff, %.2ff, %.2ff, %.2ff, %.2ff},' % (nm, x0, x1, z0, z1, y0, y1))
    L.append("};")
    L.append("}  // namespace tantra::interior")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L) + "\n")
    print("InteriorLayout.h:", len(w_s), "solids,", len(w_f), "floors,", len(b_s), "bridge solids")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "..", "..", "..")
    install = "--install" in sys.argv            # otherwise everything goes to build/mesh_t9 (the installed DLL matches the old mesh)
    groups, legs = build()
    work = os.path.join(here, "..", "build", "mesh_t9")
    out = os.path.join(root, "Meshes", "Tantra") if install else os.path.join(work, "Meshes", "Tantra")
    os.makedirs(out, exist_ok=True)
    for g_ in groups:                                          # the lift cabin's inside: the interior's textures
        k_ = {"airlock_cab_lining": 1, "airlock_cab_floor": 2, "airlock_cab_trim": 3}.get(g_.name, 0)
        if k_ and g_.v:
            g_.tex = k_
            g_.uv = []
            for p_, n_ in zip(g_.v, g_.n):
                ax = int(np.argmax(np.abs(n_)))
                u_, v_ = (p_[0], p_[2]) if ax == 1 else (p_[2], -p_[1]) if ax == 0 else (p_[0], -p_[1])
                g_.uv.append((u_ / 2.0, v_ / 2.0))
    write_msh(groups, os.path.join(out, "Tantra.msh"), textures=("Tantra\\in_wall.dds", "Tantra\\in_floor.dds", "Tantra\\in_trim.dds"))
    trap = Group("trap", MAT["trap_shell"])
    trap_geom(trap, 0.0, 0.0, -12.4 - STERN_Z, 12.4 - STERN_Z)
    write_msh([trap], os.path.join(out, "TantraTrap.msh"))
    vc_groups = build_interior()
    _uv_project(vc_groups)
    for g_ in vc_groups:                                       # the lift cabin's panel and screen: slots 35, 36; the signs: 42 (37..41 are the bridge's)
        if g_.name == "cab_panel":
            g_.tex = 35
        elif g_.name == "cab_screen":
            g_.tex = 36
        elif g_.name == "signs_path":
            g_.tex = 42
    write_signs(os.path.join(root, "Textures", "Tantra", "in_signs.dds") if install else os.path.join(work, "in_signs.dds"))
    write_lit_maps(os.path.join(root, "Textures", "Tantra") if install else work)
    write_msh(vc_groups, os.path.join(out, "TantraVC.msh"), textures=("Tantra\\screen_l.dds", "Tantra\\screen_c.dds", "Tantra\\screen_r.dds", "Tantra\\screen_c.dds",
                                                                       "Tantra\\screen_l.dds", "Tantra\\screen_r.dds", "Tantra\\screen_c.dds",
                                                                       "Tantra\\in_floor.dds", "Tantra\\in_wall.dds", "Tantra\\in_ceiling.dds", "Tantra\\in_trim.dds",
                                                                       "Tantra\\in_metal.dds", "Tantra\\in_liftpanel.dds",
                                                                       "Tantra\\console_face.dds", "Tantra\\in_fx_pool.dds", "Tantra\\in_fx_amber.dds", "Tantra\\in_fx_wash.dds", "Tantra\\in_fx_ao.dds", "Tantra\\in_fx_cove.dds", "Tantra\\in_fx_glow.dds",
                                                                       "Tantra\\lift_status.dds") + ("Tantra\\lift_status.dds",) * 12 + ("Tantra\\panel.dds",) + ("Tantra\\in_cabpanel.dds", "Tantra\\lift_status.dds") + ("Tantra\\lift_status.dds",) * 5 + ("Tantra\\in_signs.dds", "Tantra\\in_btncaps.dds", "Tantra\\in_glass.dds", "Tantra\\in_door.dds") + ("Tantra\\lift_status.dds",) * 4 + ("Tantra\\br_grate.dds", "Tantra\\lift_status.dds", "Tantra\\br_vault_lit.dds", "Tantra\\br_floor_lit.dds"))   # 50: the bridge grating, 51: the seat slider (drawn in the game); 37..40: the commander's own touch screens, 41: the holo panel (drawn in the game); 22..33: the bridge MFDs (drawn in the game); 34: the 2D panels' texture (redrawn in the game); 21: the lift zone's status screen (drawn in the game); 14 console face; 15..20 watch light decals: the tub's instrument face
    write_interior_layout(os.path.join(here, "..", "orbiter2016", "InteriorLayout.h") if install else os.path.join(work, "InteriorLayout.h"), vc_groups)
    raw_dir = os.path.join(here, "..", "build", "mesh")
    os.makedirs(raw_dir, exist_ok=True)
    write_json(groups, os.path.join(raw_dir, "tantra_raw.json"))
    write_debris(groups, root if install else work)
    write_layout(legs, rig(legs), os.path.join(here, "..", "orbiter2016", "MeshLayout.h") if install else os.path.join(work, "MeshLayout.h"))
    nv = sum(len(g.v) for g in groups)
    nt = sum(len(g.t) for g in groups)
    print(f"Tantra.msh: {len(groups)} groups, {nv} vertices, {nt} triangles; MeshLayout.h written")
