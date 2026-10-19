# «Панцирь» - revision sketch of the BODY (2026-10-18): the user judged rev. R a «протозаготовка» next to «Каркас».
# One sheet, «было -> стало» for each problem: tube legs, stumpy proportions, the blank back unit, spidery hands vs huge
# forearms, no surface language (chamfers, split lines, recessed frames, exoskeleton, stencils, worn enamel), abducted
# arms, and the kinematics of a rigid suit (ring motors, their axes, ranges, where plates collide, nested shells).
# The helmet is drawn on its own sheet (another agent) - here only a dashed placeholder.
# Base: Orbitersdk/OrbiterCrew/HEAVY_SUIT.md (wall ~79 mm limbs / ~93 mm torso per side, borazon impact plates, photonic
# mirror under borazon windows, ring motors in the bearings, artificial muscles under the plates, hands «Ладонь»).
# Style: Soviet school + 500 years + techno-frame; no pistons/cylinders, no bolts or rivets, no chest console, no tubes.
# Coordinates in metres: x lateral (+ = to the viewer's right in front view), z up from the ground, y forward (side view).
#   python build_sketch_body_rev.py  ->  pantsir_body_rev.svg + pantsir_body_rev.png (headless Edge, as the other sheets)
import os, math, random, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 2800, 2520
out = []
random.seed(7)

INK, THIN, NOTE, DIM, GRID = "#1d2733", "#5d6b78", "#2f6f9f", "#b0523a", "#d6dbe0"
EN, EN2, EN3 = "#dcd9d2", "#c6c3bc", "#a9a6a0"          # light grey matte enamel (lit / side / recess)
GR, GR2, GRL = "#3c4248", "#565d64", "#7d858d"           # dark graphite (frame, motors, rings)
FRM = "#a9afb5"                                          # light-metal plate frames
RED, RUB = "#c3362b", "#1e1e1e"
BOR, MIR = "url(#bor)", "url(#mir)"
OLD_F, OLD_S = "#e6e9ec", "#8c969e"                      # «было» drawings
SANS = "Segoe UI, Arial"
STEN = "Bahnschrift Condensed, Bahnschrift, Arial Narrow, Arial"


class View:
    def __init__(s, ox, oy, sc): s.ox, s.oy, s.sc = ox, oy, sc
    def p(s, a, b): return "%.1f,%.1f" % (s.ox + a * s.sc, s.oy - b * s.sc)
    def xy(s, a, b): return s.ox + a * s.sc, s.oy - b * s.sc


def poly(v, pts, fill, stroke=INK, w=1.4, op=1.0, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    out.append('<polygon points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' %
               (" ".join(v.p(*q) for q in pts), fill, op, stroke, w, d))
def pline(v, pts, stroke=INK, w=1.2, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    out.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' % (" ".join(v.p(*q) for q in pts), stroke, w, d))
def line(v, a, b, stroke=INK, w=1.2, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    x1, y1 = v.xy(*a); x2, y2 = v.xy(*b)
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f"%s/>' % (x1, y1, x2, y2, stroke, w, d))
def circ(v, c, r, fill, stroke=INK, w=1.2, op=1.0):
    x, y = v.xy(*c)
    out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f"/>' % (x, y, r * v.sc, fill, op, stroke, w))
def esc(s): return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
def txt(x, y, s, size=14, color=NOTE, anchor="start", weight="normal", fam=SANS, rot=0, ls=0):
    tr = ' transform="rotate(%g %.1f %.1f)"' % (rot, x, y) if rot else ""
    l = ' letter-spacing="%g"' % ls if ls else ""
    out.append('<text x="%.1f" y="%.1f" font-family="%s" font-size="%g" fill="%s" text-anchor="%s" font-weight="%s"%s%s>%s</text>' %
               (x, y, fam, size, color, anchor, weight, tr, l, esc(s)))
def vtxt(v, a, b, s, size=12, color=THIN, anchor="middle", weight="normal"):
    x, y = v.xy(a, b); txt(x, y, s, size, color, anchor, weight)
def sten(v, a, b, s, size=10, color=GR, anchor="middle", rot=0):
    x, y = v.xy(a, b); txt(x, y, s, size, color, anchor, "600", STEN, rot, 0.6)
def lines_(x, y, rows, size=13, color=THIN, dy=None, weight="normal"):
    dy = dy or size * 1.35
    for k, r in enumerate(rows): txt(x, y + k * dy, r, size, color, "start", weight)
def note(v, at, to, s, anchor="start", size=12, color=NOTE):
    x0, y0 = v.xy(*at); x1, y1 = v.xy(*to)
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="0.9"/>' % (x0, y0, x1, y1, color))
    out.append('<circle cx="%.1f" cy="%.1f" r="2.4" fill="%s"/>' % (x0, y0, color))
    rows = s.split("\n")
    for k, r in enumerate(rows):
        txt(x1 + (4 if anchor == "start" else -4), y1 + 4 + k * (size + 3), r, size, color, anchor)
def badge(v, a, b, n):
    x, y = v.xy(a, b)
    out.append('<circle cx="%.1f" cy="%.1f" r="11" fill="%s" stroke="none"/>' % (x, y, DIM))
    txt(x, y + 5, str(n), 14, "#ffffff", "middle", "700")
def dim_h(v, x0, x1, z, label, off=0.0, size=12, color=DIM):
    line(v, (x0, z), (x1, z), color, 1.0)
    for xx in (x0, x1): line(v, (xx, z - 0.012), (xx, z + 0.012), color, 1.0)
    if label:
        x, y = v.xy((x0 + x1) / 2 + off, z + 0.008); wpx = len(label) * size * 0.56 + 6
        out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#ffffff" fill-opacity="0.88" stroke="none"/>' % (x - wpx / 2, y - size, wpx, size + 4))
        vtxt(v, (x0 + x1) / 2 + off, z + 0.008, label, size, color)
def dim_v(v, x, z0, z1, label, size=12, color=DIM, anchor="start"):
    line(v, (x, z0), (x, z1), color, 1.0)
    for zz in (z0, z1): line(v, (x - 0.01, zz), (x + 0.01, zz), color, 1.0)
    vtxt(v, x + (0.012 if anchor == "start" else -0.012), (z0 + z1) / 2, label, size, color, anchor)
def mir(pts): return [(-a, b) for a, b in pts]
def rev(pts): return list(reversed(pts))


def offset(pts, d):
    """offset a simple polygon; d > 0 shrinks it (inward), d < 0 grows it (mitred corners, capped)"""
    n = len(pts); A = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    sg = 1.0 if A > 0 else -1.0
    res = []
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        def nrm(a, b):
            dx, dy = b[0] - a[0], b[1] - a[1]; L = math.hypot(dx, dy) or 1e-9
            return (-dy / L * sg, dx / L * sg)
        n1, n2 = nrm(p0, p1), nrm(p1, p2)
        bx, by = n1[0] + n2[0], n1[1] + n2[1]; bl = math.hypot(bx, by) or 1e-9
        cosh = max(0.35, (bx / bl) * n1[0] + (by / bl) * n1[1])
        k = d / cosh
        res.append((p1[0] + bx / bl * k, p1[1] + by / bl * k))
    return res


def plate(v, pts, fill, ch=0.004, frame=0.0, shade=0.0, w=1.3, split=None):
    """a flat plate: optional recessed light-metal frame around it, the plate, a light chamfer band, optional shading"""
    if frame:
        poly(v, offset(pts, -frame), FRM, INK, 0.9)
    poly(v, pts, fill, INK, w)
    poly(v, pts, "#ffffff", "none", 0, op=0.30)
    poly(v, offset(pts, ch), fill, THIN, 0.5)
    if shade: poly(v, pts, "#000000", "none", 0, op=shade)
    if split:
        for a, b in split: line(v, a, b, GR, 1.4)


def wear(v, pts, n=10, seed=None):
    """worn matte enamel: chips to bare metal and scuffs at the edges"""
    rnd = random.Random(seed if seed is not None else len(pts) * 31 + int(pts[0][1] * 1000))
    m = len(pts)
    for _ in range(n):
        i = rnd.randrange(m); a, b = pts[i], pts[(i + 1) % m]; t = rnd.random()
        x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
        cx, cy = sum(p[0] for p in pts) / m, sum(p[1] for p in pts) / m
        x += (cx - x) * 0.04; y += (cy - y) * 0.04
        L = 0.004 + rnd.random() * 0.01
        ang = math.atan2(b[1] - a[1], b[0] - a[0])
        line(v, (x, y), (x + math.cos(ang) * L, y + math.sin(ang) * L), "#f6f4ef" if rnd.random() < 0.6 else "#9a978f", 1.1)


def sector(v, c, r0, r1, a0, a1, fill, stroke=INK, w=1.2, op=1.0):
    n = max(6, int(abs(a1 - a0) / 4))
    pts = [(c[0] + r1 * math.cos(math.radians(a0 + (a1 - a0) * k / n)), c[1] + r1 * math.sin(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n + 1)]
    pts += [(c[0] + r0 * math.cos(math.radians(a1 - (a1 - a0) * k / n)), c[1] + r0 * math.sin(math.radians(a1 - (a1 - a0) * k / n))) for k in range(n + 1)]
    poly(v, pts, fill, stroke, w, op)


def ring_motor(v, c, r, lab=None, tick=90, face=True):
    """a ring motor seen face-on: graphite housing, stator ring, ribs on the rim, red zero mark"""
    circ(v, c, r, GR, INK, 1.4)
    circ(v, c, r * 0.78, GR2, INK, 0.9)
    circ(v, c, r * 0.45, GR, GRL, 0.9)
    for k in range(16):
        a = math.radians(k * 22.5)
        line(v, (c[0] + r * 0.82 * math.cos(a), c[1] + r * 0.82 * math.sin(a)), (c[0] + r * 0.96 * math.cos(a), c[1] + r * 0.96 * math.sin(a)), GRL, 0.8)
    a = math.radians(tick)
    line(v, (c[0] + r * 0.5 * math.cos(a), c[1] + r * 0.5 * math.sin(a)), (c[0] + r * 0.98 * math.cos(a), c[1] + r * 0.98 * math.sin(a)), RED, 2.2)
    if lab: sten(v, c[0], c[1] - r * 0.15, lab, max(8, r * v.sc * 0.42), "#f0f0f0")


def ring_edge(v, x0, x1, z0, z1, lab=None, lab_side=1):
    """a ring motor seen edge-on: graphite block with rib lines"""
    poly(v, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], GR, INK, 1.2)
    for k in range(1, 4):
        z = z0 + (z1 - z0) * k / 4; line(v, (x0, z), (x1, z), GRL, 0.7)
    line(v, (x0, (z0 + z1) / 2), (x1, (z0 + z1) / 2), RED, 1.6)


def band(v, x0, x1, z0, z1, fill=GR, ribs=0):
    poly(v, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], fill, INK, 1.1)
    for k in range(ribs):
        x = x0 + (x1 - x0) * (k + 0.5) / ribs; line(v, (x, z0), (x, z1), GRL, 0.6)


def chain(v, x, z, ang, segs, th, curl, fill=EN):
    """a mechanised digit seen from the side: flat chamfered phalanges on ring joints"""
    for k, L in enumerate(segs):
        r = math.radians(ang); ux, uz = math.cos(r), math.sin(r); nx, nz = -uz, ux; t = th * (1 - 0.07 * k)
        q = [(x + nx * t / 2, z + nz * t / 2), (x + ux * (L - 0.006) + nx * t * 0.46, z + uz * (L - 0.006) + nz * t * 0.46),
             (x + ux * L + nx * t * 0.3, z + uz * L + nz * t * 0.3), (x + ux * L - nx * t * 0.3, z + uz * L - nz * t * 0.3),
             (x + ux * (L - 0.006) - nx * t * 0.46, z + uz * (L - 0.006) - nz * t * 0.46), (x - nx * t / 2, z - nz * t / 2)]
        plate(v, q, fill, ch=0.0025, w=1.1)
        circ(v, (x, z), t * 0.20, GR, INK, 0.9)
        x, z = x + ux * L, z + uz * L; ang += curl
    return x, z


# ===================================== GEOMETRY: FRONT / BACK ======================================================
LC = 0.135                                               # leg centre (as rev. R)
def LP(s, pts): return [(s * (LC + u), z) for u, z in pts]

SHIN_O = [(0.078, 0.20), (0.092, 0.28), (0.106, 0.37), (0.108, 0.41), (0.106, 0.445), (0.103, 0.49)]
SHIN_I = [(-0.077, 0.20), (-0.088, 0.29), (-0.098, 0.38), (-0.102, 0.44), (-0.100, 0.49)]
THI_O = [(0.100, 0.585), (0.106, 0.66), (0.116, 0.76), (0.125, 0.85), (0.128, 0.95)]
THI_I = [(-0.098, 0.585), (-0.100, 0.66), (-0.106, 0.74), (-0.116, 0.80), (-0.132, 0.85)]

def leg_front(v, s, back=False, detail=True):
    # boot
    poly(v, LP(s, [(-0.084, 0.0), (0.084, 0.0), (0.086, 0.06), (-0.086, 0.06)]), RUB, INK, 1.2)
    for k in range(5): line(v, LP(s, [(-0.07 + k * 0.035, 0.0)])[0], LP(s, [(-0.07 + k * 0.035, 0.022)])[0], "#555", 0.8)
    if not back:
        plate(v, LP(s, [(-0.076, 0.06), (0.076, 0.06), (0.073, 0.12), (-0.073, 0.12)]), BOR, frame=0.004)
        plate(v, LP(s, [(-0.072, 0.124), (0.072, 0.124), (0.077, 0.172), (-0.077, 0.172)]), EN)
        line(v, LP(s, [(0.0, 0.124)])[0], LP(s, [(0.0, 0.172)])[0], GR, 1.1)
    else:
        plate(v, LP(s, [(-0.07, 0.06), (0.07, 0.06), (0.076, 0.172), (-0.076, 0.172)]), EN)
        pline(v, LP(s, [(-0.04, 0.062), (-0.05, 0.10), (-0.03, 0.135), (0.03, 0.135), (0.05, 0.10), (0.04, 0.062)]), GR, 3.0)   # heel leaf spring
    xa, xb = s * (LC - 0.08), s * (LC + 0.08)
    band(v, min(xa, xb), max(xa, xb), 0.170, 0.205, GR, 6)
    # shin shell (enamel frame) with plates
    sh = LP(s, SHIN_O + rev(SHIN_I))
    poly(v, sh, EN, INK, 1.5)
    if detail: wear(v, sh, 8)
    if not back:
        plate(v, LP(s, [(-0.028, 0.235), (0.028, 0.235), (0.036, 0.44), (0.0, 0.462), (-0.036, 0.44)]), BOR, frame=0.005)
        plate(v, LP(s, [(0.048, 0.23), (0.068, 0.225), (0.084, 0.31), (0.092, 0.40), (0.090, 0.445), (0.050, 0.445)]), MIR, frame=0.004, shade=0.12,
              split=[(LP(s, [(0.049, 0.335)])[0], LP(s, [(0.088, 0.335)])[0])])
        plate(v, LP(s, [(-0.048, 0.23), (-0.066, 0.225), (-0.080, 0.31), (-0.086, 0.40), (-0.086, 0.445), (-0.050, 0.445)]), MIR, frame=0.004, shade=0.2,
              split=[(LP(s, [(-0.049, 0.335)])[0], LP(s, [(-0.083, 0.335)])[0])])
    else:
        poly(v, LP(s, [(-0.022, 0.205), (0.022, 0.205), (0.026, 0.285), (-0.026, 0.285)]), EN3, INK, 1.0)       # Achilles recess
        plate(v, LP(s, [(0.006, 0.30), (0.05, 0.29), (0.088, 0.36), (0.093, 0.445), (0.006, 0.452)]), MIR, frame=0.004, shade=0.1)
        plate(v, LP(s, [(-0.006, 0.30), (-0.05, 0.29), (-0.084, 0.36), (-0.088, 0.445), (-0.006, 0.452)]), MIR, frame=0.004, shade=0.18)
    # knee: bearing band, nested shells, guard (front) / popliteal shells (back)
    poly(v, LP(s, [(-0.088, 0.488), (0.088, 0.488), (0.090, 0.578), (-0.090, 0.578)]), GR, INK, 1.2)
    if not back:
        plate(v, LP(s, [(-0.080, 0.478), (0.080, 0.478), (0.078, 0.512), (-0.078, 0.512)]), EN, ch=0.003)
        plate(v, LP(s, [(-0.075, 0.506), (0.075, 0.506), (0.073, 0.536), (-0.073, 0.536)]), EN2, ch=0.003)
        plate(v, LP(s, [(-0.060, 0.530), (0.060, 0.530), (0.070, 0.562), (0.050, 0.606), (-0.050, 0.606), (-0.070, 0.562)]), BOR, frame=0.005)
    else:
        plate(v, LP(s, [(-0.084, 0.49), (0.084, 0.49), (0.087, 0.532), (-0.087, 0.532)]), EN2, ch=0.003)
        plate(v, LP(s, [(-0.087, 0.527), (0.087, 0.527), (0.089, 0.572), (-0.089, 0.572)]), EN, ch=0.003)
    # thigh shell with plates
    th = LP(s, THI_O + [(-0.132, 0.95)][:0] + rev(THI_I))
    poly(v, th, EN, INK, 1.5)
    if detail: wear(v, th, 8)
    line(v, LP(s, [(-0.099, 0.589)])[0], LP(s, [(0.101, 0.589)])[0], GR, 3.0)                                   # lower lip over the knee
    if not back:
        plate(v, LP(s, [(-0.070, 0.615), (0.078, 0.615), (0.086, 0.725), (-0.074, 0.725)]), MIR, frame=0.004, shade=0.05)
        plate(v, LP(s, [(-0.076, 0.735), (0.088, 0.735), (0.096, 0.83), (-0.084, 0.83)]), MIR, frame=0.004)
    else:
        plate(v, LP(s, [(0.006, 0.62), (0.085, 0.62), (0.098, 0.80), (0.012, 0.84)]), MIR, frame=0.004, shade=0.08)
        plate(v, LP(s, [(-0.006, 0.62), (-0.080, 0.62), (-0.094, 0.78), (-0.012, 0.84)]), MIR, frame=0.004, shade=0.16)
    # exoskeleton members on the outer side, ring motors edge-on (hip Р8, knee Р10, ankle Р11)
    poly(v, LP(s, [(0.128, 0.88), (0.146, 0.88), (0.126, 0.565), (0.112, 0.565)]), GR, INK, 1.0)
    poly(v, LP(s, [(0.112, 0.485), (0.124, 0.485), (0.108, 0.165), (0.098, 0.165)]), GR, INK, 1.0)
    for (u0, u1, z0, z1) in ((0.096, 0.128, 0.485, 0.567), (0.086, 0.111, 0.10, 0.165), (0.128, 0.160, 0.855, 0.945)):
        a, b = LP(s, [(u0, z0), (u1, z1)]); ring_edge(v, min(a[0], b[0]), max(a[0], b[0]), z0, z1)


TORSO = [(0.0, 1.06), (0.232, 1.06), (0.236, 1.12), (0.250, 1.22), (0.268, 1.30), (0.282, 1.38), (0.285, 1.44), (0.265, 1.50),
         (0.200, 1.545), (0.165, 1.56), (0.0, 1.565)]
DZ = 0.015
C1 = [(0.0, 1.445), (0.150, 1.445), (0.215, 1.405), (0.205, 1.370), (0.0, 1.372)]
C2i = [(0.0, 1.395), (0.180, 1.395), (0.195, 1.225), (0.0, 1.215)]
C2o = [(0.180, 1.395), (0.215, 1.405), (0.272, 1.360), (0.262, 1.250), (0.195, 1.225)]
C3i = [(0.0, 1.240), (0.180, 1.240), (0.170, 1.125), (0.0, 1.118)]
C3o = [(0.180, 1.240), (0.250, 1.250), (0.236, 1.140), (0.170, 1.125)]
C4i = [(0.0, 1.140), (0.165, 1.140), (0.150, 1.048), (0.0, 1.040)]
C4o = [(0.165, 1.140), (0.228, 1.150), (0.212, 1.060), (0.150, 1.048)]
def up(pts, dz=DZ): return [(a, b + dz) for a, b in pts]
def sym(pts): return pts + mir(rev(pts))      # right half from the axis -> whole outline

PELVIS = [(0.0, 1.025), (0.245, 1.025), (0.255, 0.975), (0.18, 0.93), (0.09, 0.87), (0.03, 0.838), (0.0, 0.832)]

def arm_front(v, s, back=False):
    X = lambda pts: [(s * a, b) for a, b in pts]
    poly(v, X([(0.282, 1.365), (0.302, 1.365), (0.305, 1.535), (0.285, 1.53)]), GR, INK, 1.1)                  # Р1 armhole ring (edge-on)
    ua = X([(0.300, 1.375), (0.490, 1.375), (0.494, 1.30), (0.488, 1.205), (0.304, 1.205), (0.298, 1.30)])
    poly(v, ua, EN, INK, 1.4)
    plate(v, X([(0.322, 1.292), (0.472, 1.292), (0.468, 1.218), (0.326, 1.218)]), MIR, frame=0.004, shade=0.08 if not back else 0.15)
    band(v, s * 0.296, s * 0.495, 1.296, 1.330, GR, 0)                                                         # Р3 rotation ring
    line(v, (s * 0.40, 1.296), (s * 0.40, 1.330), RED, 1.6)
    plate(v, X([(0.300, 1.535), (0.40, 1.545), (0.468, 1.50), (0.497, 1.42), (0.492, 1.335), (0.300, 1.335)]), MIR, frame=0.005, shade=0.0)   # deltoid cap
    poly(v, X([(0.16, 1.548), (0.36, 1.548), (0.405, 1.536), (0.405, 1.512), (0.16, 1.522)]), GR, INK, 1.1)    # shoulder yoke (flat profile)
    ring_motor(v, (s * 0.39, 1.515), 0.04, None, 90)                                                          # Р2 abduction, face-on
    # elbow
    poly(v, X([(0.296, 1.12), (0.490, 1.12), (0.494, 1.205), (0.300, 1.205)]), GR, INK, 1.1)
    if not back:
        plate(v, X([(0.304, 1.168), (0.484, 1.168), (0.488, 1.206), (0.300, 1.206)]), EN, ch=0.003)
        plate(v, X([(0.302, 1.132), (0.488, 1.132), (0.486, 1.172), (0.304, 1.172)]), EN2, ch=0.003)
    ring_edge(v, min(s * 0.492, s * 0.518), max(s * 0.492, s * 0.518), 1.125, 1.20)                          # Р4 elbow (edge-on)
    fa = X([(0.292, 1.125), (0.497, 1.125), (0.492, 1.04), (0.470, 0.905), (0.466, 0.89), (0.296, 0.89), (0.292, 0.97)])
    poly(v, fa, EN, INK, 1.4); wear(v, fa, 6)
    plate(v, X([(0.312, 1.10), (0.476, 1.10), (0.470, 1.05), (0.314, 1.05)]), MIR, frame=0.004, shade=0.06)
    plate(v, X([(0.314, 1.028), (0.468, 1.028), (0.456, 0.925), (0.318, 0.925)]), MIR, frame=0.004, shade=0.06)
    plate(v, X([(0.479, 1.10), (0.490, 1.10), (0.470, 0.925), (0.461, 0.925)]), BOR)                         # outer face (borazon) edge
    band(v, s * 0.296, s * 0.494, 1.032, 1.048, GR)                                                           # Р5 forearm rotation
    if back:
        plate(v, X([(0.34, 1.105), (0.45, 1.105), (0.462, 1.16), (0.395, 1.212), (0.33, 1.16)]), BOR, frame=0.005)   # olecranon guard
    band(v, s * 0.298, s * 0.464, 0.866, 0.892, GR, 6)                                                        # Р6 wrist coupling
    circ(v, (s * 0.452, 0.879), 0.006, RED, INK, 0.8)
    # hand capsule seen on its thumb edge (palm toward the thigh), effector digits
    cap = X([(0.322, 0.866), (0.440, 0.866), (0.446, 0.80), (0.434, 0.735), (0.340, 0.728), (0.326, 0.79)])
    plate(v, cap, EN, ch=0.004)
    plate(v, X([(0.425, 0.862), (0.442, 0.80), (0.432, 0.74), (0.418, 0.80)]), BOR, ch=0.002)
    chain(v, s * 0.405, 0.732, -90 - s * 6, (0.046, 0.036, 0.028), 0.040, -s * 13, EN2)
    chain(v, s * 0.345, 0.765, -90 + s * 14, (0.044, 0.034), 0.036, -s * 16, EN)


def body_front(v, back=False):
    # the back unit seen past the shoulders
    if not back:
        poly(v, [(-0.21, 1.635), (0.21, 1.635), (0.24, 1.605), (0.24, 1.55), (-0.24, 1.55), (-0.24, 1.605)], EN2, INK, 1.2)
    for s in (1, -1): leg_front(v, s, back)
    # pelvis + pelvis ring + waist bearing (Р0)
    pv = sym(PELVIS)
    poly(v, pv, EN, INK, 1.5); wear(v, pv, 8)
    if not back:
        plate(v, [(-0.13, 0.99), (0.13, 0.99), (0.075, 0.905), (0.0, 0.872), (-0.075, 0.905)], MIR, frame=0.004)
        for s in (1, -1): plate(v, [(s * 0.205, 0.99), (s * 0.243, 0.99), (s * 0.248, 0.972), (s * 0.205, 0.945)], BOR, ch=0.002)
        sten(v, 0.20, 0.955, "№ 2-0417", 8, GR)
    else:
        for s in (1, -1):
            plate(v, [(s * 0.012, 0.99), (s * 0.222, 0.99), (s * 0.236, 0.968), (s * 0.17, 0.912), (s * 0.08, 0.866), (s * 0.012, 0.856)], MIR, frame=0.004, shade=0.1)
    band(v, -0.245, 0.245, 0.995, 1.025, GR)                                                                   # pelvis ring (exoskeleton)
    band(v, -0.232, 0.232, 1.025, 1.06, GR2, 14)                                                               # Р0 waist ring
    line(v, (0.0, 1.025), (0.0, 1.06), RED, 1.8)
    # torso shell
    ts = sym(TORSO)
    poly(v, ts, EN, INK, 1.6); wear(v, ts, 14)
    if not back:
        rows = [(C4i, MIR, C4o, MIR, 0.12), (C3i, MIR, C3o, MIR, 0.06), (C2i, BOR, C2o, BOR, 0.0), (C1, MIR, None, None, 0.0)]
        for inner, fi, outer, fo, sh in rows:
            for pts, f, sd in ((inner, fi, sh), (outer, fo, sh + 0.12)):
                if pts is None: continue
                plate(v, up(pts), f, frame=0.004, shade=sd); plate(v, up(mir(pts)), f, frame=0.004, shade=sd)
        line(v, (0, 1.46), (0, 1.055), GR, 1.6)                                                                # sternum ridge
        sten(v, 0.0, 1.49, "ПАНЦИРЬ-2", 10, RED)
    else:
        backunit_back(v, detail=False)
    band(v, -0.17, 0.17, 1.545, 1.585, GR, 12)                                                                 # collar / neck bearing
    for s in (1, -1): arm_front(v, s, back)
    # helmet placeholder
    hp = [(-0.15, 1.59), (0.15, 1.59), (0.175, 1.66), (0.175, 1.80), (0.13, 1.88), (0.0, 1.905), (-0.13, 1.88), (-0.175, 1.80), (-0.175, 1.66)]
    poly(v, hp, "#ffffff", THIN, 1.2, op=0.6, dash="6,4")
    vtxt(v, 0, 1.76, "шлем —", 11, THIN); vtxt(v, 0, 1.735, "отдельный лист", 11, THIN)


# ===================================== BACK UNIT ===================================================================
BU = [(-0.24, 1.065), (-0.215, 1.04), (0.215, 1.04), (0.24, 1.065), (0.24, 1.605), (0.21, 1.635), (-0.21, 1.635), (-0.24, 1.605)]
PORTS = [(1.272, "О₂"), (1.226, "ВОДА"), (1.180, "ТН"), (1.134, "28 В"), (1.088, "ДАННЫЕ")]

def backunit_back(v, detail=True):
    """the back unit (rear door) from behind; figure's left = viewer's left in a back view"""
    poly(v, BU, GR, INK, 1.6)                                                                                  # frame profile
    inner = offset(BU, 0.016)
    poly(v, inner, EN, INK, 1.0); wear(v, inner, 16 if detail else 8, 3)
    # hinge knuckles (figure's left), integrated in the frame, seamless
    for z0, z1 in ((1.10, 1.20), (1.28, 1.38), (1.46, 1.56)):
        poly(v, [(-0.254, z0), (-0.238, z0), (-0.238, z1), (-0.254, z1)], GR2, INK, 1.0)
        line(v, (-0.246, z0 + 0.004), (-0.246, z1 - 0.004), GRL, 0.7)
    # radiator: rails, main fin panel, side wing panels (tilted up-back to the sky)
    poly(v, [(-0.205, 1.35), (0.205, 1.35), (0.205, 1.585), (-0.205, 1.585)], "#2a2f34", INK, 1.0)
    nf = 22
    for k in range(nf):
        x = -0.162 + 0.324 * k / (nf - 1)
        poly(v, [(x - 0.0042, 1.352), (x + 0.0042, 1.352), (x + 0.0042, 1.583), (x - 0.0042, 1.583)], GRL if k % 2 else "#959ca3", "#22272b", 0.5)
    for sgn in (1, -1):
        for k in range(5):
            x = sgn * (0.176 + k * 0.0065)
            poly(v, [(x - 0.0025, 1.356), (x + 0.0025, 1.356), (x + 0.0025, 1.579), (x - 0.0025, 1.579)], GR2, "#22272b", 0.4)
    band(v, -0.21, 0.21, 1.335, 1.352, GR)
    band(v, -0.21, 0.21, 1.585, 1.602, GR)
    # top carry handle in its pocket
    poly(v, [(-0.09, 1.606), (0.09, 1.606), (0.09, 1.630), (-0.09, 1.630)], "#2a2f34", INK, 0.9)
    poly(v, [(-0.08, 1.611), (0.08, 1.611), (0.08, 1.621), (-0.08, 1.621)], GRL, INK, 0.8)
    # middle band: heat-pump access panel + markings
    plate(v, [(-0.17, 1.222), (0.09, 1.222), (0.09, 1.322), (-0.17, 1.322)], EN, ch=0.004)
    sten(v, -0.04, 1.302, "ОСТОРОЖНО · РАДИАТОР ДО +300 °C", 6.5 if not detail else 11, RED)
    sten(v, -0.04, 1.268 if detail else 1.262, "ПАНЦИРЬ-2", 11 if not detail else 20, RED)
    if detail: sten(v, -0.04, 1.238, "ЗАВ. № 2-0417 · ТЕПЛОВОЙ НАСОС", 10, GR)
    # battery bay: two cassettes with recessed pull handles and a flush latch lever
    plate(v, [(-0.17, 1.062), (0.085, 1.062), (0.085, 1.21), (-0.17, 1.21)], EN2, ch=0.004)
    for x0, x1, lab in ((-0.162, -0.045, "Б1"), (-0.037, 0.077, "Б2")):
        plate(v, [(x0, 1.07), (x1, 1.07), (x1, 1.182), (x0, 1.182)], EN, ch=0.003)
        poly(v, [((x0 + x1) / 2 - 0.035, 1.085), ((x0 + x1) / 2 + 0.035, 1.085), ((x0 + x1) / 2 + 0.035, 1.098), ((x0 + x1) / 2 - 0.035, 1.098)], "#2a2f34", INK, 0.8)
        sten(v, (x0 + x1) / 2, 1.13 if detail else 1.122, lab, 18 if detail else 8, GR)
    poly(v, [(-0.16, 1.188), (0.075, 1.188), (0.075, 1.2), (-0.16, 1.2)], GR2, INK, 0.8)
    if detail: sten(v, -0.042, 1.152, "ЗАМЕНА БЕЗ НАГРУЗКИ", 9, GR)
    # service ports column (caps, no bolts)
    for z, lab in PORTS:
        circ(v, (0.18, z), 0.017, EN3, INK, 1.0); circ(v, (0.18, z), 0.011, GR2, INK, 0.8)
        line(v, (0.18 - 0.008, z), (0.18 + 0.008, z), GRL, 1.0)
        if detail: sten(v, 0.157, z - 0.004, lab, 10, GR, "end")
    # side grab slots, red emergency release (figure's right = viewer's right from behind)
    for sgn in (1, -1):
        poly(v, [(sgn * 0.226, 1.38), (sgn * 0.234, 1.38), (sgn * 0.234, 1.50), (sgn * 0.226, 1.50)], "#2a2f34", INK, 0.7)
    poly(v, [(0.24, 1.112), (0.256, 1.112), (0.256, 1.098), (0.268, 1.098), (0.268, 1.172), (0.256, 1.172), (0.256, 1.158), (0.24, 1.158)], RED, INK, 1.0)


def backunit_side(v):
    """side profile of the back unit (y back = negative), the torso back face at y = -0.178"""
    prof = [(-0.178, 1.05), (-0.37, 1.05), (-0.395, 1.075), (-0.40, 1.44), (-0.345, 1.60), (-0.30, 1.635), (-0.19, 1.635), (-0.178, 1.60)]
    poly(v, prof, GR, INK, 1.5)
    inner = offset(prof, 0.012)
    poly(v, inner, EN, INK, 1.0); wear(v, inner, 10, 5)
    # tilted radiator face (up-back to the sky): fins edge-on along the slope
    a, b = (-0.40, 1.44), (-0.30, 1.635)
    for k in range(13):
        t = (k + 0.5) / 13; x, z = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
        line(v, (x, z), (x - 0.018 * 0.89, z + 0.018 * 0.45), GR2, 2.0)
    for k in range(10):
        z = 1.35 + k * 0.009; line(v, (-0.40, z), (-0.418, z), GR2, 1.6)
    poly(v, [(-0.36, 1.30), (-0.344, 1.30), (-0.344, 1.42), (-0.36, 1.42)], "#2a2f34", INK, 0.8)                 # grab slot
    line(v, (-0.20, 1.205), (-0.39, 1.205), GR, 1.2); line(v, (-0.20, 1.06), (-0.20, 1.205), GR, 1.0)          # battery cassette split
    poly(v, [(-0.392, 1.112), (-0.408, 1.112), (-0.408, 1.098), (-0.418, 1.098), (-0.418, 1.172), (-0.408, 1.172), (-0.408, 1.158), (-0.392, 1.158)], RED, INK, 1.0)
    for k in range(6):                                                                                         # evaporator slots on top
        x = -0.285 + k * 0.016; line(v, (x, 1.633), (x + 0.008, 1.633), "#20252a", 2.4)


# ===================================== SIDE VIEW ===================================================================
SH_F = [(0.07, 0.205), (0.082, 0.30), (0.092, 0.40), (0.098, 0.475)]
SH_B = [(-0.075, 0.205), (-0.085, 0.26), (-0.115, 0.33), (-0.134, 0.395), (-0.130, 0.44), (-0.112, 0.488)]
TH_F = [(0.098, 0.59), (0.112, 0.66), (0.120, 0.75), (0.122, 0.83), (0.120, 0.90)]
TH_B = [(-0.088, 0.59), (-0.105, 0.65), (-0.122, 0.75), (-0.130, 0.83), (-0.135, 0.87)]

def leg_side(v, labels=True):
    # boot
    poly(v, [(-0.10, 0.0), (0.195, 0.0), (0.20, 0.02), (0.19, 0.06), (-0.10, 0.06)], RUB, INK, 1.2)
    for k in range(9): line(v, (-0.09 + k * 0.034, 0.0), (-0.09 + k * 0.034, 0.018), "#555", 0.8)
    bt = [(-0.095, 0.06), (0.185, 0.06), (0.19, 0.085), (0.12, 0.115), (0.078, 0.168), (0.072, 0.205), (-0.075, 0.205), (-0.092, 0.15)]
    poly(v, bt, EN, INK, 1.4); wear(v, bt, 6)
    plate(v, [(0.10, 0.062), (0.183, 0.062), (0.188, 0.084), (0.12, 0.112), (0.10, 0.104)], BOR, frame=0.004)
    plate(v, [(0.075, 0.150), (0.112, 0.118), (0.12, 0.125), (0.082, 0.160)], BOR, ch=0.002)
    plate(v, [(0.074, 0.168), (0.098, 0.14), (0.106, 0.146), (0.078, 0.178)], BOR, ch=0.002)
    pline(v, [(-0.093, 0.062), (-0.112, 0.085), (-0.108, 0.12), (-0.088, 0.135)], GR, 3.0)                      # heel leaf spring
    band(v, -0.078, 0.074, 0.17, 0.205, GR)
    ring_motor(v, (0.0, 0.135), 0.032, None, 90)                                                                # Р11
    ring_edge(v, -0.098, -0.084, 0.15, 0.19)                                                                   # Р12 inversion (edge-on)
    # shin
    sh = SH_F + rev(SH_B)
    poly(v, sh, EN, INK, 1.5); wear(v, sh, 8)
    plate(v, [(-0.05, 0.235), (0.05, 0.235), (0.062, 0.445), (-0.085, 0.445), (-0.08, 0.33), (-0.058, 0.27)], MIR, frame=0.004, shade=0.05,
          split=[((-0.075, 0.335), (0.056, 0.335))])
    plate(v, [(0.056, 0.235), (0.070, 0.235), (0.088, 0.445), (0.069, 0.445)], BOR, ch=0.002)
    plate(v, [(-0.064, 0.24), (-0.072, 0.24), (-0.098, 0.31), (-0.122, 0.395), (-0.118, 0.445), (-0.094, 0.445), (-0.09, 0.33)], MIR, ch=0.002, shade=0.15)
    # knee
    poly(v, [(-0.105, 0.485), (0.095, 0.485), (0.10, 0.585), (-0.105, 0.585)], GR, INK, 1.2)
    for k, (z0, z1, f) in enumerate(((0.474, 0.506, EN), (0.502, 0.534, EN2))):
        plate(v, [(0.078, z0), (0.100 + k * 0.006, z0), (0.108 + k * 0.006, z1), (0.084, z1)], f, ch=0.002)
    plate(v, [(0.080, 0.53), (0.110, 0.522), (0.122, 0.548), (0.114, 0.602), (0.090, 0.612), (0.084, 0.56)], BOR, frame=0.004)
    plate(v, [(-0.113, 0.49), (-0.092, 0.49), (-0.09, 0.535), (-0.112, 0.535)], EN2, ch=0.002)
    plate(v, [(-0.115, 0.53), (-0.093, 0.53), (-0.092, 0.578), (-0.112, 0.58)], EN, ch=0.002)
    # thigh
    th = TH_F + [(0.02, 0.97), (-0.135, 0.87)][:0] + rev(TH_B)
    poly(v, th, EN, INK, 1.5); wear(v, th, 8)
    line(v, (-0.088, 0.592), (0.098, 0.592), GR, 3.0)
    plate(v, [(-0.075, 0.62), (0.085, 0.62), (0.090, 0.725), (-0.100, 0.725), (-0.090, 0.67)], MIR, frame=0.004, shade=0.04)
    plate(v, [(-0.102, 0.735), (0.091, 0.735), (0.095, 0.83), (-0.112, 0.83)], MIR, frame=0.004)
    poly(v, [(-0.128, 0.80), (0.121, 0.835), (0.121, 0.852), (-0.13, 0.817)], GR2, INK, 1.0)                    # Р9 oblique ring pair
    line(v, (-0.129, 0.8085), (0.121, 0.8435), GRL, 0.7)
    # exoskeleton: flat profiles hip - knee - ankle, face-on
    for pts in ([(-0.018, 0.86), (0.018, 0.86), (0.018, 0.555), (-0.018, 0.555)], [(-0.015, 0.495), (0.015, 0.495), (0.012, 0.16), (-0.012, 0.16)]):
        poly(v, pts, GR, INK, 1.0)
        poly(v, offset(pts, 0.006), GR2, "none", 0)
    ring_motor(v, (0.0, 0.525), 0.042, None, 90)                                                                # Р10
    sten(v, 0.0, 0.29, "НЕ НАСТУПАТЬ", 7, RED, rot=-90) if False else None


def body_side(v):
    backunit_side(v)
    # pelvis (high leg cut), torso shell, waist ring
    pv = [(-0.165, 1.025), (0.145, 1.025), (0.15, 0.96), (0.12, 0.89), (0.08, 0.92), (0.03, 0.965), (-0.03, 0.97), (-0.09, 0.93), (-0.14, 0.87), (-0.165, 0.92)]
    ts = [(-0.155, 1.06), (0.14, 1.06), (0.165, 1.15), (0.168, 1.24), (0.163, 1.36), (0.165, 1.46), (0.12, 1.55), (-0.14, 1.55), (-0.18, 1.40), (-0.175, 1.10)]
    poly(v, ts, EN, INK, 1.6); wear(v, ts, 10)
    P = [[(0.130, 1.452), (0.205, 1.372), (0.198, 1.364), (0.140, 1.440)], [(0.205, 1.398), (0.207, 1.215), (0.193, 1.210), (0.192, 1.392)],
         [(0.198, 1.242), (0.180, 1.118), (0.166, 1.115), (0.185, 1.238)], [(0.178, 1.142), (0.150, 1.040), (0.137, 1.040), (0.165, 1.140)]]
    for k, pts in enumerate(P):
        poly(v, up(pts), [MIR, BOR, MIR, MIR][k], INK, 1.3)
        for zz in (0.25, 0.75):                                                                                # standoffs (flat, hidden heads)
            q = up(pts); zc = q[0][1] + (q[1][1] - q[0][1]) * zz; yc = q[0][0] + (q[1][0] - q[0][0]) * zz
            line(v, (0.16, zc), (yc - 0.008, zc), GR2, 2.0)
    for leg_first in (True,):
        leg_side(v)
    poly(v, pv, EN, INK, 1.5); wear(v, pv, 6)
    plate(v, [(-0.162, 1.0), (-0.07, 1.0), (-0.11, 0.935), (-0.155, 0.93)], MIR, frame=0.004, shade=0.1)        # seat plate
    band(v, -0.165, 0.145, 0.995, 1.025, GR)
    band(v, -0.158, 0.143, 1.025, 1.06, GR2, 10)
    ring_motor(v, (0.0, 0.90), 0.062, None, 90)                                                                 # Р8 hip
    band(v, -0.15, 0.13, 1.545, 1.585, GR, 8)
    # torso side frame member (pelvis ring -> shoulder yoke), behind the arm
    poly(v, [(-0.105, 1.06), (-0.075, 1.06), (-0.06, 1.40), (-0.09, 1.40)], GR, INK, 1.0)
    arm_side(v)
    hp = [(-0.17, 1.59), (0.15, 1.59), (0.185, 1.67), (0.18, 1.80), (0.12, 1.885), (0.0, 1.905), (-0.12, 1.88), (-0.18, 1.80), (-0.185, 1.66)]
    poly(v, hp, "#ffffff", THIN, 1.2, op=0.6, dash="6,4")
    vtxt(v, 0, 1.75, "шлем", 11, THIN)


def forearm_hand_side(v):
    fa = [(-0.098, 1.12), (0.115, 1.12), (0.12, 1.03), (0.135, 0.90), (0.132, 0.89), (-0.035, 0.89), (-0.07, 0.98)]
    poly(v, fa, EN, INK, 1.4); wear(v, fa, 6, 11)
    plate(v, [(-0.075, 1.10), (0.10, 1.10), (0.104, 1.05), (-0.08, 1.05)], BOR, frame=0.004)                     # outer face: borazon
    plate(v, [(-0.074, 1.028), (0.106, 1.028), (0.118, 0.925), (-0.04, 0.925), (-0.06, 0.975)], BOR, frame=0.004,
          split=[((0.02, 1.028), (0.035, 0.925))])
    band(v, -0.09, 0.12, 1.032, 1.048, GR)
    poly(v, [(-0.034, 0.866), (0.134, 0.866), (0.136, 0.892), (-0.036, 0.892)], GR, INK, 1.1)                   # Р6
    circ(v, (0.12, 0.879), 0.006, RED, INK, 0.8)
    cap = [(-0.032, 0.866), (0.132, 0.866), (0.142, 0.80), (0.138, 0.735), (0.124, 0.726), (-0.026, 0.726), (-0.040, 0.80)]
    poly(v, cap, EN, INK, 1.4)
    plate(v, [(-0.020, 0.858), (0.050, 0.858), (0.050, 0.746), (-0.024, 0.744), (-0.030, 0.80)], BOR, frame=0.004)
    plate(v, [(0.050, 0.858), (0.122, 0.858), (0.130, 0.80), (0.126, 0.748), (0.050, 0.746)], BOR, frame=0.004, shade=0.1)
    plate(v, [(-0.026, 0.738), (0.128, 0.740), (0.122, 0.727), (-0.024, 0.727)], GR2, ch=0.0015)                 # knuckle ridge
    circ(v, (0.142, 0.79), 0.012, EN2, INK, 1.0)
    chain(v, 0.146, 0.79, -55, (0.044, 0.034), 0.036, -10, EN)                                                  # thumb (forward)
    chain(v, 0.108, 0.726, -84, (0.046, 0.036, 0.028), 0.034, 9, EN2)                                           # index
    chain(v, 0.035, 0.726, -88, (0.046, 0.036, 0.028), 0.062, 8, EN)                                            # broad digit


def arm_side(v):
    ring_motor(v, (0.0, 1.465), 0.074, None, 90)                                                                # Р1
    ua = [(-0.103, 1.40), (0.103, 1.40), (0.106, 1.30), (0.105, 1.205), (-0.10, 1.205), (-0.106, 1.30)]
    poly(v, ua, EN, INK, 1.4); wear(v, ua, 6, 21)
    plate(v, [(-0.08, 1.292), (0.085, 1.292), (0.085, 1.215), (-0.078, 1.215)], MIR, frame=0.004, shade=0.05)
    band(v, -0.107, 0.107, 1.296, 1.33, GR)
    plate(v, [(-0.105, 1.335), (0.105, 1.335), (0.11, 1.42), (0.07, 1.47), (-0.07, 1.47), (-0.11, 1.42)], MIR, frame=0.004)
    ring_motor(v, (0.0, 1.465), 0.074, None, 90)
    poly(v, [(-0.10, 1.12), (0.105, 1.12), (0.106, 1.205), (-0.10, 1.205)], GR, INK, 1.1)
    plate(v, [(0.080, 1.13), (0.115, 1.13), (0.12, 1.20), (0.084, 1.20)], EN, ch=0.002)                         # inner elbow shells (front)
    plate(v, [(-0.118, 1.105), (-0.085, 1.10), (-0.08, 1.16), (-0.105, 1.21), (-0.125, 1.16)], BOR, frame=0.004)   # olecranon
    forearm_hand_side(v)
    ring_motor(v, (0.005, 1.16), 0.048, None, 90)                                                               # Р4


# ===================================== «БЫЛО» (rev. R, schematic from the renders) =================================
def old_front(v):
    f, s_ = OLD_F, OLD_S
    for s in (1, -1):
        poly(v, [(s * 0.05, 0.0), (s * 0.215, 0.0), (s * 0.215, 0.09), (s * 0.05, 0.09)], f, s_, 1.2)
        poly(v, [(s * 0.048, 0.10), (s * 0.222, 0.10), (s * 0.226, 0.48), (s * 0.044, 0.48)], f, s_, 1.2)          # shin: straight prism
        poly(v, [(s * 0.035, 0.48), (s * 0.235, 0.48), (s * 0.235, 0.53), (s * 0.035, 0.53)], "#cfd5da", s_, 1.2)  # knee ring
        poly(v, [(s * 0.04, 0.53), (s * 0.235, 0.53), (s * 0.26, 0.88), (s * 0.0, 0.88)], f, s_, 1.2)              # thigh: straight prism
        for z in (0.25, 0.38, 0.65, 0.78): line(v, (s * 0.05, z), (s * 0.23, z), s_, 0.8)
        # arm abducted 8 deg, forearm flaring into a cuff, small capsule, long digits
        poly(v, [(s * 0.30, 1.52), (s * 0.48, 1.52), (s * 0.50, 1.36), (s * 0.30, 1.36)], "#cfd5da", s_, 1.2)
        poly(v, [(s * 0.30, 1.36), (s * 0.50, 1.36), (s * 0.53, 1.13), (s * 0.34, 1.12)], f, s_, 1.2)
        poly(v, [(s * 0.33, 1.12), (s * 0.54, 1.12), (s * 0.60, 0.87), (s * 0.38, 0.86)], f, s_, 1.2)
        poly(v, [(s * 0.43, 0.86), (s * 0.55, 0.86), (s * 0.55, 0.74), (s * 0.45, 0.74)], f, s_, 1.2)
        for dx in (0.455, 0.50, 0.535):
            line(v, (s * dx, 0.74), (s * (dx + 0.01), 0.56), s_, 3.0)
    poly(v, [(-0.24, 0.88), (0.24, 0.88), (0.24, 1.02), (-0.24, 1.02)], "#cfd5da", s_, 1.2)
    poly(v, [(-0.28, 1.02), (0.28, 1.02), (0.28, 1.47), (0.20, 1.575), (-0.20, 1.575), (-0.28, 1.47)], f, s_, 1.4)   # box torso
    poly(v, [(-0.15, 1.58), (0.15, 1.58), (0.15, 1.80), (0.10, 1.86), (-0.10, 1.86), (-0.15, 1.80)], "#ffffff", s_, 1.0, dash="5,3")


def old_leg_side(v):
    f, s_ = OLD_F, OLD_S
    poly(v, [(-0.10, 0.0), (0.19, 0.0), (0.19, 0.09), (-0.10, 0.09)], f, s_, 1.2)
    poly(v, [(-0.115, 0.10), (0.115, 0.10), (0.105, 0.48), (-0.105, 0.48)], f, s_, 1.2)
    poly(v, [(-0.115, 0.48), (0.115, 0.48), (0.115, 0.53), (-0.115, 0.53)], "#cfd5da", s_, 1.2)
    poly(v, [(-0.10, 0.53), (0.10, 0.53), (0.13, 0.88), (-0.13, 0.88)], f, s_, 1.2)
    for z in (0.20, 0.30, 0.40, 0.62, 0.72, 0.82): line(v, (-0.11 + 0.0, z), (0.11, z), s_, 0.8)
    ring_motor(v, (0.0, 0.505), 0.04, None)


def old_section(v, c, w, lab):
    h = w / 2; k = h * 0.41
    pts = [(c[0] - k, c[1] + h), (c[0] + k, c[1] + h), (c[0] + h, c[1] + k), (c[0] + h, c[1] - k), (c[0] + k, c[1] - h), (c[0] - k, c[1] - h), (c[0] - h, c[1] - k), (c[0] - h, c[1] + k)]
    poly(v, pts, OLD_F, OLD_S, 1.4)
    vtxt(v, c[0], c[1] - h - 0.03, lab, 11, THIN)


# ===================================== LAYOUT ======================================================================
out.append('<defs>'
           '<linearGradient id="bor" x1="0" y1="0" x2="0.7" y2="1"><stop offset="0" stop-color="#f6d08e"/><stop offset="0.45" stop-color="#e0a04c"/><stop offset="1" stop-color="#b9772b"/></linearGradient>'
           '<linearGradient id="mir" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#e8c56a"/><stop offset="0.40" stop-color="#bf8d48"/><stop offset="0.72" stop-color="#93607a"/><stop offset="1" stop-color="#5f4289"/></linearGradient>'
           '<pattern id="hat" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="6" stroke="#c3362b" stroke-width="2"/></pattern>'
           '</defs>')
out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
txt(30, 46, "«Панцирь» · ревизия корпуса (эскиз) · было → стало: пропорции, ноги, блок на спине, кисть, язык поверхности, кинематика", 26, INK, weight="600")
txt(30, 74, "шлем — отдельный лист (здесь только габарит) · размеры в метрах · стенка по HEAVY_SUIT.md: конечности ~79 мм, торс ~93 мм на сторону · "
            "суставы — кольцевые моторы в подшипниках, усилие — искусственные мышцы под плитами; без поршней, без болтов, без пульта на груди", 15, THIN)

def panel(x, y, w, h, title, n=None):
    out.append('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="%s" stroke-width="1.2"/>' % (x, y, w, h, GRID))
    txt(x + 16 + (26 if n else 0), y + 30, title, 19, INK, weight="600")
    if n:
        out.append('<circle cx="%d" cy="%d" r="12" fill="%s"/>' % (x + 28, y + 24, DIM)); txt(x + 28, y + 30, str(n), 15, "#ffffff", "middle", "700")

# ---------------- A: the revised suit, three orthographic views ----------------
panel(5, 90, 1535, 1065, "Стало: спереди · сбоку · сзади (ортогонально, один масштаб 1 м = 500 px)")
SC = 500; GZ = 1120
vF, vS, vB = View(285, GZ, SC), View(790, GZ, SC), View(1255, GZ, SC)
for vv in (vF, vS, vB):
    line(vv, (-0.55, 0), (0.55, 0), GRID, 1.2)
body_front(vF)
body_side(vS)
body_front(vB, back=True)
for vv, lab in ((vF, "спереди"), (vS, "сбоку (справа)"), (vB, "сзади")):
    vtxt(vv, 0, -0.045, lab, 14, THIN)
# dimensions, front
dim_h(vF, LC - 0.130, LC + 0.127, 0.905, "0,26", 0.0, 11)
dim_h(vF, LC - 0.099, LC + 0.101, 0.60, "0,20", 0.0, 11)
dim_h(vF, LC - 0.102, LC + 0.106, 0.455, "0,21", 0.0, 11)
dim_h(vF, LC - 0.078, LC + 0.078, 0.215, "0,155", 0.0, 11)
dim_h(vF, -0.518, 0.518, 0.03 + 0.0, "", 0, 11) if False else None
# dimensions, side
dim_h(vS, -0.135, 0.120, 0.92 - 0.06, "", 0, 11) if False else None
note(vS, (-0.30, 1.62), (-0.30, 1.72), "блок на спине,\nглубина 0,22", "start", 11)
note(vS, (0.0, 1.465), (0.25, 1.62), "Р1 (ось X)", "start", 11)
note(vS, (0.005, 1.16), (0.26, 1.25), "Р4 локоть", "start", 11)
note(vS, (0.0, 0.90), (0.26, 0.97), "Р8 бедро, Р9 косая пара", "start", 11)
note(vS, (0.0, 0.525), (0.26, 0.56), "Р10 колено + щиток", "start", 11)
note(vS, (0.0, 0.135), (0.26, 0.20), "Р11 / Р12 голеностоп", "start", 11)
note(vS, (0.06, 0.66), (0.26, 0.70), "руки висят свободно,\nлокоть согнут ~8°", "start", 11)
# problem badges
badge(vF, -0.36, 0.40, 1); badge(vF, -0.17, 0.95, 2); badge(vB, 0.0, 1.66, 3); badge(vF, -0.40, 0.58, 4)
badge(vF, -0.09, 1.30, 5); badge(vF, 0.40, 0.58, 6); badge(vS, 0.07, 0.47, 7)
# front notes
note(vF, (-0.39, 1.515), (-0.53, 1.62), "Р2 отведение (ось Y)", "start", 11)

# ---------------- B: legs ----------------
panel(1545, 90, 1250, 1065, "Ноги: не трубы — плоские боковые плиты, разная глубина спереди и сзади, икра, щиток колена, сужения", 1)
vO = View(1680, 760, 560); old_leg_side(vO); vtxt(vO, 0.0, -0.06, "было (рев. R): прямые", 13, OLD_S); vtxt(vO, 0.0, -0.09, "восьмигранники + кольцо", 13, OLD_S)
vtxt(vO, 0.0, 0.95, "0,26 → 0,20 / 0,21 → 0,155", 12, OLD_S); vtxt(vO, 0.0, 0.92, "линейно, перёд = зад", 12, OLD_S)
vN = View(1960, 760, 560); leg_side(vN); vtxt(vN, 0.0, -0.06, "стало, сбоку", 13, INK)
for z, lab in ((0.72, "A"), (0.53, "B"), (0.40, "C"), (0.22, "D")):
    line(vN, (-0.16, z), (0.15, z), NOTE, 0.8, "5,3"); vtxt(vN, 0.17, z - 0.005, lab, 13, NOTE)
vNF = View(2215 - LC * 560, 760, 560); leg_front(vNF, 1); vtxt(vNF, LC, -0.06, "стало, спереди", 13, INK)
note(vNF, (LC + 0.0, 0.58), (LC + 0.20, 0.64), "щиток колена\nборазон", "start", 11)
note(vNF, (LC + 0.11, 0.525), (LC + 0.20, 0.50), "сужение:\nподшипник 0,18", "start", 11)
note(vNF, (LC + 0.108, 0.41), (LC + 0.20, 0.38), "икра —\nв верхней трети", "start", 11)
note(vNF, (LC + 0.0, 0.30), (LC + 0.20, 0.27), "голень спереди:\nборазон в раме", "start", 11)
note(vNF, (LC + 0.08, 0.70), (LC + 0.20, 0.76), "окна-зеркала,\nшов-разрез", "start", 11)
note(vN, (-0.075, 0.205), (-0.20, 0.13), "выемка\nахилла", "end", 11)
# sections
txt(2425, 138, "Сечения (вид сверху, перёд — вверх)", 14, INK, weight="600")
txt(2425, 162, "было                      стало", 13, THIN)
vSo = View(2470, 0, 520); vSn = View(2660, 0, 520)
SECS = [
    ("A бедро z 0,72", 0.227, [(0.0, 0.118), (0.07, 0.105), (0.11, 0.06), (0.112, -0.04), (0.085, -0.10), (0.03, -0.12), (0.0, -0.105), (-0.03, -0.12), (-0.08, -0.10), (-0.105, -0.04), (-0.105, 0.06), (-0.065, 0.105)],
     "0,217 × 0,238: складка спереди,\nплоские бока, V-выемка сзади"),
    ("B колено z 0,53", 0.215, [(-0.06, 0.10), (0.06, 0.10), (0.09, 0.06), (0.10, 0.0), (0.095, -0.06), (0.06, -0.095), (-0.06, -0.095), (-0.095, -0.06), (-0.10, 0.0), (-0.09, 0.06)],
     "подшипник 0,18 + щиток,\nвложенные скорлупы"),
    ("C икра z 0,40", 0.19, [(-0.025, 0.092), (0.025, 0.092), (0.075, 0.06), (0.106, 0.01), (0.104, -0.06), (0.075, -0.115), (0.03, -0.132), (0.0, -0.118), (-0.03, -0.13), (-0.07, -0.112), (-0.10, -0.06), (-0.10, 0.01), (-0.07, 0.06)],
     "0,206 × 0,224: плоская\nберцовая плита, 2 доли икры"),
    ("D лодыжка z 0,22", 0.163, [(-0.03, 0.072), (0.03, 0.072), (0.065, 0.05), (0.078, 0.0), (0.072, -0.05), (0.04, -0.075), (0.016, -0.075), (0.012, -0.06), (-0.012, -0.06), (-0.016, -0.075), (-0.04, -0.075), (-0.072, -0.05), (-0.077, 0.0), (-0.062, 0.05)],
     "0,155 × 0,147:\nвыемка ахилла"),
]
for k, (lab, wold, pts, cap) in enumerate(SECS):
    zc = -(0.42 + k * 0.38) ; zc = None
    cy = 252 + k * 182
    vo, vn = View(2475, cy, 520), View(2660, cy, 520)
    old_section(vo, (0, 0), wold, "")
    poly(vn, pts, EN, INK, 1.5)
    if k == 1:
        plate(vn, [(-0.06, 0.10), (0.06, 0.10), (0.045, 0.124), (-0.045, 0.124)], BOR, ch=0.003)
        poly(vn, offset(pts, 0.012), GR, "none", 0, op=0.9)
        poly(vn, [(0.10, 0.025), (0.128, 0.025), (0.128, -0.025), (0.10, -0.025)], GR, INK, 1.0)
    else:
        poly(vn, offset(pts, 0.0035), "#ffffff", THIN, 0.5, op=0.2)
        for i in range(len(pts)):
            a = pts[i]; line(vn, (a[0] * 0.94, a[1] * 0.94), a, THIN, 0.5)
    wl = 0.079 if k != 1 else 0.084
    hx_ = max(abs(q[0]) for q in pts); hy_ = max(abs(q[1]) for q in pts)
    sx_, sy_ = max(0.0, (hx_ - wl) / hx_), max(0.0, (hy_ - wl) / hy_)
    poly(vn, [(q[0] * sx_, q[1] * sy_) for q in pts], "none", NOTE, 1.0, dash="3,3")
    vtxt(vn, 0.0, hy_ + 0.018, "внутри: %.2f × %.2f" % (2 * max(0, hx_ - wl), 2 * max(0, hy_ - wl)), 10, NOTE)
    txt(2425, cy - 70, lab, 13, INK, weight="600")
    rows = cap.split("\n")
    for i, r in enumerate(rows): txt(2580, cy + 78 + i * 15, r, 11, THIN, "middle")
txt(2380, 1110, "пунктир — внутренняя граница стенки 79 мм (у колена 84)", 11, NOTE)

# knee kinematics (nested shells)
txt(1565, 852, "Колено: вложенные скорлупы (спереди 3, сзади 2), Р10 — ось X", 15, INK, weight="600")
TH_LOC = [(0.098, 0.065), (0.112, 0.135), (0.120, 0.225), (0.122, 0.30), (-0.130, 0.30), (-0.126, 0.24), (-0.112, 0.185), (-0.07, 0.15), (-0.05, 0.075)]   # popliteal recess
SH_LOC = [(0.098, -0.05), (0.092, -0.125), (0.082, -0.225), (0.07, -0.32), (-0.075, -0.32), (-0.085, -0.265), (-0.118, -0.215), (-0.134, -0.165), (-0.118, -0.125), (-0.068, -0.095), (-0.05, -0.035)]   # calf top cut back
def rot(p, a):
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a)); return (p[0] * c - p[1] * s, p[0] * s + p[1] * c)
def inside(p, pts):
    x, y = p; c = False; n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]; x2, y2 = pts[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-12) + x1: c = not c
    return c
def dense(pts, n=12):
    r = []
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        r += [(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n) for k in range(n)]
    return r
KNEE_LIMIT = 0
for th_ in range(0, 160):
    if any(inside(rot(p, -th_), TH_LOC) for p in dense(SH_LOC) if math.hypot(*p) > 0.09): KNEE_LIMIT = th_; break
def knee_state(v, theta, lab):
    poly(v, TH_LOC, EN, INK, 1.4)
    plate(v, [(-0.10, 0.10), (0.09, 0.10), (0.095, 0.27), (-0.11, 0.27)], MIR, frame=0.004, shade=0.05)
    shp = [rot(p, -theta) for p in SH_LOC]
    poly(v, shp, EN, INK, 1.4)
    pl = [rot(p, -theta) for p in [(-0.05, -0.29), (0.06, -0.29), (0.08, -0.08), (-0.10, -0.08), (-0.10, -0.17)]]
    plate(v, pl, MIR, frame=0.004, shade=0.08)
    # back shells: D on the thigh, E on the shin (slide inside each other)
    sector(v, (0, 0), 0.084, 0.094, 150, 200, EN2)
    sector(v, (0, 0), 0.094, 0.104, 165 - theta * 0.5, 215 - theta * 0.5, EN)
    # front shells: A on the thigh, B floating (half the angle), C = guard on the shin
    a0 = -30; a1 = 40
    c0, c1 = -27 - theta, 43 - theta
    b_c = ((a0 + a1) / 2 + (c0 + c1) / 2) / 2
    sector(v, (0, 0), 0.100, 0.109, a0, a1, EN)
    sector(v, (0, 0), 0.109, 0.118, b_c - 35, b_c + 35, EN2)
    sector(v, (0, 0), 0.118, 0.130, c0, c1, BOR)
    ring_motor(v, (0, 0), 0.042, None, 90 - theta)
    if theta >= KNEE_LIMIT - 1:
        hits = [rot(p, -theta) for p in dense(SH_LOC) if math.hypot(*p) > 0.09 and inside(rot(p, -theta), TH_LOC)]
        if hits:
            cx = sum(h[0] for h in hits) / len(hits); cy = sum(h[1] for h in hits) / len(hits)
            circ(v, (cx, cy), 0.03, "url(#hat)", RED, 1.6)
            note(v, (cx, cy + 0.02), (cx + 0.10, 0.315), "упор: икра ↔ плита бедра сзади", "start", 11, RED)
    vtxt(v, 0.0, -0.36, lab, 13, INK)
vk = [View(1680, 995, 400), View(1950, 995, 400), View(2230, 995, 400)]
knee_state(vk[0], 0, "0°")
knee_state(vk[1], KNEE_LIMIT // 2, "%d°: B делит угол пополам" % (KNEE_LIMIT // 2))
knee_state(vk[2], KNEE_LIMIT, "%d°: предел (упор)" % KNEE_LIMIT)
lines_(2420, 918, ["A — на бедре, C — щиток на голени,", "B плавает (½ угла): спереди", "нет щели до 140°; сзади D/E", "уходят друг в друга.",
                   "Мышцы под плитами бедра", "тянут голень через узел Р10;", "на колено можно встать —", "щиток = опора."], 12, THIN)

# ---------------- C: the back unit ----------------
panel(5, 1165, 930, 700, "Блок на спине скафандра: рама, радиатор теплового насоса, отсек батарей, разъёмы, ручки", 3)
vbo = View(70, 2027, 170)
poly(vbo, BU, OLD_F, OLD_S, 1.3)
for k in range(14): x = -0.17 + 0.34 * k / 13; line(vbo, (x, 1.35), (x, 1.58), OLD_S, 1.0)
poly(vbo, [(-0.08, 1.635), (0.08, 1.635), (0.08, 1.66), (-0.08, 1.66)], OLD_F, OLD_S, 1.0)
for z in (1.15, 1.3, 1.45): poly(vbo, [(0.24, z), (0.255, z), (0.255, z + 0.03), (0.24, z + 0.03)], "#e7a39d", OLD_S, 0.8)
x_, y_ = vbo.xy(0.27, 1.35); txt(x_, y_, "было: короб", 12, OLD_S); txt(x_, y_ + 15, "+ решётка", 12, OLD_S)
vbn = View(440, 2420, 730)
backunit_back(vbn, detail=True)
vtxt(vbn, 0, 1.005, "стало, сзади (петли — слева, замок и аварийная ручка — справа)", 12, INK)
note(vbn, (-0.10, 1.47), (-0.575, 1.50), "радиатор: 22 ребра + 2 крыла,\nнаклон вверх-назад (в небо),\nгорячее среды на 20–100 K", "start", 11)
note(vbn, (-0.246, 1.33), (-0.575, 1.36), "петли двери —\nв профиле рамы", "start", 11)
note(vbn, (-0.22, 1.10), (-0.575, 1.16), "рама: плоский\nпрофиль, фаска 5 мм", "start", 11)
note(vbn, (-0.09, 1.09), (-0.575, 1.07), "отсек батарей Б1/Б2,\nкассеты вниз", "start", 11)
note(vbn, (-0.05, 1.618), (-0.575, 1.60), "ручка в нише", "start", 11)
note(vbn, (0.262, 1.135), (0.30, 1.10), "аварийное\nоткрытие", "start", 11)
note(vbn, (0.232, 1.45), (0.30, 1.47), "щель-ручка", "start", 11)
note(vbn, (0.07, 1.30), (0.30, 1.33), "люк теплового\nнасоса", "start", 11)
vbs = View(1012, 2250, 600)
backunit_side(vbs)
line(vbs, (-0.178, 1.0), (-0.178, 1.68), THIN, 0.8, "4,3")
vtxt(vbs, -0.29, 1.005, "сбоку", 12, INK)
note(vbs, (-0.35, 1.53), (-0.36, 1.72), "испаритель:\nщели сверху", "end", 11)
txt(250, 1855, "без болтов: крышки на скрытых защёлках, рама цельная; батареи меняются снизу без снятия блока", 12, THIN)

# ---------------- D: hand and forearm ----------------
panel(940, 1165, 690, 700, "Кисть «Ладонь» и предплечье: баланс", 4)
vho = View(1060, 1720 + 0.62 * 0, 850)
vho = View(1065, 2207, 850)
f, s_ = OLD_F, OLD_S
poly(vho, [(-0.105, 1.12), (0.115, 1.12), (0.13, 0.90), (0.135, 0.86), (-0.095, 0.86), (-0.10, 0.90)], f, s_, 1.3)
poly(vho, [(-0.03, 0.86), (0.12, 0.86), (0.13, 0.72), (-0.03, 0.72)], f, s_, 1.3)
for xx in (-0.01, 0.045, 0.10):
    line(vho, (xx, 0.72), (xx + 0.01, 0.53), s_, 7.0); line(vho, (xx, 0.72), (xx + 0.01, 0.53), OLD_F, 4.0)
vtxt(vho, 0.03, 1.15, "было", 14, OLD_S, weight="600")
lines_(965, 2440 - 0.45 * 850 + 30, [], 12)
vhn = View(1330, 2207, 850)
forearm_hand_side(vhn)
vtxt(vhn, 0.04, 1.15, "стало (вид снаружи)", 14, INK, weight="600")
dim_h(vhn, -0.098, 0.115, 1.135 - 0.01, "0,213", 0, 11)
dim_h(vhn, -0.035, 0.135, 0.905, "0,17", 0, 11)
dim_v(vhn, 0.175, 0.726, 0.866, "капсула 0,14", 11)
dim_v(vhn, 0.175, 0.62, 0.726, "пальцы 0,11", 11)
lines_(1450, 1235, ["было → стало", "пальцы: 0,19 → 0,11 м,", "  толщина 34 → 34/62/36 мм", "предплечье у кисти:", "  раструб ≈0,23 → 0,17", "капсула 0,18 × 0,12 —", "  та же (рука + 2 × 30 мм)",
                    "тыл кисти: 2 плиты", "  боразона + скат", "  к костяшкам", "большой палец —", "  вперёд, под указательным"], 12, THIN)

# ---------------- E: proportions ----------------
panel(1635, 1165, 1160, 700, "Пропорции и поза: в пределах честной стенки", 2)
vpo = View(1820, 1830, 300); old_front(vpo); vtxt(vpo, 0, -0.06, "было (рев. R)", 13, OLD_S, weight="600")
vpn = View(2160, 1830, 300)
for s in (1, -1):
    poly(vpn, LP(s, SHIN_O + rev(SHIN_I)), EN, INK, 1.2); poly(vpn, LP(s, THI_O + rev(THI_I)), EN, INK, 1.2)
    poly(vpn, LP(s, [(-0.084, 0.0), (0.084, 0.0), (0.086, 0.205), (-0.086, 0.205)]), EN2, INK, 1.0)
    poly(vpn, LP(s, [(-0.09, 0.488), (0.09, 0.488), (0.09, 0.585), (-0.09, 0.585)]), GR, INK, 1.0)
poly(vpn, sym(PELVIS), EN2, INK, 1.2); poly(vpn, sym(TORSO), EN, INK, 1.3)
band(vpn, -0.232, 0.232, 1.0, 1.06, GR)
for s in (1, -1):
    X = lambda pts: [(s * a, b) for a, b in pts]
    poly(vpn, X([(0.30, 1.54), (0.47, 1.50), (0.497, 1.42), (0.494, 1.30), (0.488, 1.205), (0.304, 1.205)]), EN, INK, 1.2)
    poly(vpn, X([(0.292, 1.205), (0.497, 1.205), (0.497, 1.125), (0.47, 0.905), (0.296, 0.89), (0.292, 0.97)]), EN, INK, 1.2)
    poly(vpn, X([(0.322, 0.89), (0.446, 0.89), (0.434, 0.735), (0.34, 0.728)]), EN2, INK, 1.0)
    poly(vpn, X([(0.36, 0.73), (0.42, 0.73), (0.40, 0.64), (0.36, 0.65)]), EN2, INK, 1.0)
poly(vpn, [(-0.15, 1.59), (0.15, 1.59), (0.175, 1.66), (0.175, 1.80), (0.13, 1.88), (0.0, 1.905), (-0.13, 1.88), (-0.175, 1.80), (-0.175, 1.66)], "#ffffff", THIN, 1.0, dash="5,3")
vtxt(vpn, 0, -0.06, "стало", 13, INK, weight="600")
for vv, zl, lab in ((vpo, 0.88, "ноги от 0,88"), (vpn, 0.975, "ноги «от» 0,975")):
    line(vv, (-0.56, zl), (0.56, zl), DIM, 0.9, "6,3"); vtxt(vv, 0.56, zl + 0.01, lab, 11, DIM, "end")
line(vpo, (-0.62, 1.02), (0.62, 1.02), GRID, 0.8)
lines_(2390, 1225, [
    "• талия: 0,476 → 0,464 (больше нельзя: тело 0,28 +",
    "  2 × 93 мм), но торс теперь клин: грудь 0,57 над",
    "  талией, бока сходятся от C2 к поясу (было — короб,",
    "  стенки вертикальны от 1,02 до 1,45)",
    "• таз — отдельная деталь с высоким вырезом бедра:",
    "  нога читается от 0,975, а не от паха 0,84",
    "  (визуально +0,10 к длине ноги)",
    "• ноги разной толщины по высоте: 0,26 / 0,20 /",
    "  0,21 / 0,155 с тремя сужениями (было — колонны)",
    "• плечи: центр руки 0,36 → 0,39, руки висят",
    "  (было отведены на 8°), ширина по рукам 0,99",
    "• кисть ниже середины бедра: кончики на 0,64",
    "• пояс Р0 и кольцо таза — тёмные: «ремень»",
    "  делит фигуру, торс не сливается с тазом",
    "• шлему (другой лист): габарит ≥ 0,35 × 0,32,",
    "  иначе голова снова «маленькая» на 0,57 груди",
], 12.5, THIN, 17)
# check note on the wall
out.append('<rect x="2380" y="1515" width="400" height="140" fill="#fff4ef" stroke="%s" stroke-width="1"/>' % DIM)
lines_(2392, 1537, ["Проверка стенки (для решения):", "цифры 0,26→0,20 / 0,21→0,155 (раздел «Форма",
                    "конечностей», рев. P) со стенкой 79 мм дают", "внутри у колена 0,04 м, у лодыжки ~0 — меньше",
                    "ноги человека. Честно ≈ бедро 0,32→0,27, икра 0,27,", "лодыжка 0,22. Форма листа масштабируется."], 12, DIM, 16)

# ---------------- F: surface language ----------------
panel(5, 1875, 1395, 640, "Язык поверхности: фаски, швы-разрезы, рамы пластин, каркас, маркировка, потёртая эмаль", 5)
# (a) wall + plate edges, 4 px/mm
va = View(40, 2440, 3200)
L_ = [(0.0, 0.010, "#ffffff", "зазор 10"), (0.010, 0.018, "#d6e4ec", "костюм охлаждения 8"), (0.018, 0.024, "#6d7378", "гермооболочка, композит 6"),
      (0.024, 0.028, "#a9c4d6", "каналы теплоносителя 4"), (0.028, 0.041, "#e8d6a8", "боразон + водородная подложка 13"),
      (0.041, 0.071, "#f2efe6", "аэрогель BN 20 + экраны 10"), (0.071, 0.079, None, "рама + пластина 8")]
for z0, z1, c, lab in L_:
    if c: poly(va, [(0, z0), (0.11, z0), (0.11, z1), (0, z1)], c, INK, 0.8)
    vtxt(va, 0.113, (z0 + z1) / 2 - 0.0012, lab, 11, THIN, "start")
for k in range(9): circ(va, (0.006 + k * 0.012, 0.014), 0.0022, "#ffffff", THIN, 0.6)
for k in range(10): line(va, (0.005 + k * 0.011, 0.041), (0.012 + k * 0.011, 0.071), "#d8d2c0", 0.8)
# the outer zone: a light-metal frame between two plates, both chamfered; split line 2 mm
poly(va, [(0.0, 0.071), (0.11, 0.071), (0.11, 0.073), (0.0, 0.073)], "#d9d4c6", INK, 0.6)
plate(va, [(0.0, 0.073), (0.046, 0.073), (0.046, 0.0765), (0.0425, 0.0795), (0.0, 0.0795)], BOR, ch=0.0006)
plate(va, [(0.056, 0.073), (0.11, 0.073), (0.11, 0.0795), (0.0595, 0.0795), (0.056, 0.076)], MIR, ch=0.0006)
poly(va, [(0.044, 0.071), (0.058, 0.071), (0.058, 0.0775), (0.055, 0.080), (0.047, 0.080), (0.044, 0.0775)], FRM, INK, 1.0)
line(va, (0.051, 0.0742), (0.051, 0.080), INK, 1.4)
note(va, (0.0425, 0.0795), (0.002, 0.098), "фаска пластины 3–4 мм", "start", 11)
note(va, (0.051, 0.080), (0.040, 0.106), "шов-разрез 2 мм в раме (рама утоплена на 3 мм)", "start", 11)
note(va, (0.0595, 0.0795), (0.075, 0.098), "фаска 4 мм", "start", 11)
vtxt(va, 0.022, 0.0845, "боразон", 10, INK); vtxt(va, 0.09, 0.0845, "окно-зеркало", 10, INK)
txt(40, 2470, "стенка конечности 79 мм в разрезе (3,2 px = 1 мм), снаружи вверх", 12, THIN)
# (b) exoskeleton profile + ring drive node + muscle pack
vx = View(690, 1990, 3000)
poly(vx, [(0.0, 0.0), (0.040, 0.0), (0.040, 0.009), (0.037, 0.012), (0.003, 0.012), (0.0, 0.009)], GR, INK, 1.2)
poly(vx, [(0.006, 0.0), (0.034, 0.0), (0.034, 0.006), (0.006, 0.006)], "#2a2f34", INK, 0.6)
vtxt(vx, 0.02, -0.012, "профиль каркаса 40 × 12, фаски 3", 11, THIN)
vn_ = View(720, 2300, 900)
poly(vn_, [(-0.016, 0.0), (0.016, 0.0), (0.016, 0.16), (-0.016, 0.16)], GR, INK, 1.0)
poly(vn_, [(-0.013, -0.21), (0.013, -0.21), (0.013, -0.05), (-0.013, -0.05)], GR, INK, 1.0)
ring_motor(vn_, (0.0, -0.025), 0.045, None, 90)
for k in range(9):
    z = 0.02 + k * 0.014
    poly(vn_, [(0.028, z), (0.07, z), (0.07, z + 0.011), (0.028, z + 0.011)], "#cf8f86", "#8a4d46", 0.7, op=0.9)
line(vn_, (0.049, 0.15), (0.049, -0.02), "#8a4d46", 1.0, "3,2"); line(vn_, (0.049, -0.02), (0.01, -0.03), "#8a4d46", 1.0, "3,2")
lines_(790, 2110, ["узел каркаса: кольцевой мотор", "в подшипнике (ось = ось сустава),", "звенья — плоские профили;",
                   "пакет искусственных мышц —", "под плитой, тяга на узел", "(не поршень, не цилиндр)"], 11.5, THIN, 15)
# (c) markings layout
txt(985, 1945, "Маркировка (трафарет, кириллица)", 14, INK, weight="600")
MK = [("ПАНЦИРЬ-2", RED, "грудь над C1, блок на спине"), ("ЗАВ. № 2-0417", GR, "правый бок таза, люк насоса"),
      ("Р0 … Р12", "#f0f0f0", "у моторов, белая + красная риска"),
      ("ОСТОРОЖНО · РАДИАТОР ДО +300 °C", RED, "под рёбрами радиатора"), ("НЕ НАСТУПАТЬ", RED, "каркас бедра, верх блока"),
      ("АВАРИЙНОЕ ОТКРЫТИЕ — ТЯНУТЬ", RED, "у красной ручки блока"), ("Б1 · Б2 · ЗАМЕНА БЕЗ НАГРУЗКИ", GR, "крышки кассет батарей"),
      ("О₂ · ВОДА · ТН · 28 В · ДАННЫЕ", GR, "разъёмы (ТН — теплоноситель)"), ("ВЗЯТЬ ЗДЕСЬ", GR, "у щелей-ручек")]
for k, (s_, col, where) in enumerate(MK):
    y = 1975 + k * 30
    bg = GR if col == "#f0f0f0" else EN
    out.append('<rect x="985" y="%d" width="200" height="22" fill="%s" stroke="%s" stroke-width="0.8"/>' % (y - 16, bg, INK))
    txt(1085, y, s_, 12, col, "middle", "600", STEN, 0, 0.5)
    txt(1192, y, where, 11, THIN)
# (d) worn enamel swatch
vw = View(990, 2495, 1000)
sw = [(0.0, 0.0), (0.16, 0.0), (0.165, 0.005), (0.165, 0.06), (0.16, 0.065), (0.0, 0.065)]
plate(vw, sw, EN, ch=0.004)
wear(vw, sw, 40, 9)
for k in range(30):
    x, y = random.random() * 0.15 + 0.007, random.random() * 0.05 + 0.007
    circ(vw, (x, y), 0.0008, "#b6b2aa", "none", 0)
sten(vw, 0.08, 0.025, "ПАНЦИРЬ-2", 16, RED)
txt(1165, 2445, "потёртая матовая эмаль: сколы по фаскам", 11.5, THIN); txt(1165, 2461, "до металла, пыль в швах, трафарет", 11.5, THIN)
txt(1165, 2477, "стёрт на кромках — без блеска", 11.5, THIN)

# ---------------- G: material key ----------------
panel(1405, 1875, 435, 640, "Материалы")
KEY = [(EN, "эмаль светло-серая, матовая", "корпус блока, рамы-оболочки, торс между окнами"),
       (GR, "графит тёмный", "каркас, кольцевые моторы, подшипники, пояс"),
       (BOR, "боразон янтарный, полированный", "удар: колени, голень спереди, носок, подъём,", "предплечье снаружи, тыл кисти, C2, локоть сзади"),
       (MIR, "фотонное зеркало под боразоновым окном", "янтарное стекло, перелив золото → пурпур;", "бёдра, икры, бока голени, плечи, C1/C3/C4, таз"),
       (FRM, "лёгкий металл рам", "утопленные рамы вокруг каждой пластины"),
       (RED, "красный сигнальный", "риски нуля моторов, аварийная ручка, надписи"),
       (RUB, "чёрная резина", "подошвы (шипы-стойки), накладки ладони")]
y = 1930
for item in KEY:
    col, name = item[0], item[1]
    out.append('<rect x="1420" y="%d" width="60" height="46" fill="%s" stroke="%s" stroke-width="1"/>' % (y, col, INK))
    if col == MIR:
        out.append('<rect x="1420" y="%d" width="60" height="46" fill="#ffffff" fill-opacity="0.18" stroke="none"/>' % y)
    txt(1492, y + 16, name, 13, INK, weight="600")
    for i, r in enumerate(item[2:]): txt(1492, y + 33 + i * 15, r, 11, THIN)
    y += 78

# ---------------- H: kinematics ----------------
panel(1845, 1875, 950, 640, "Кинематика жёсткого скафандра: кольца, оси, диапазоны, упоры", 7)
KT = [("Р0", "пояс", "Z", "вращение ±30°", "гребни колец"),
      ("Р1", "плечо, кольцо проймы", "X", "вперёд 0…+150°, назад −40°", "дельта в шлем / блок"),
      ("Р2", "плечо, узел коромысла", "Y", "отведение 0…+110°", "дельта в воротник"),
      ("Р3", "плечо, кольцо руки", "ось руки", "вращение ±70°", "—"),
      ("Р4", "локоть", "X", "сгиб 0…115°", "предплечье в плечевую плиту"),
      ("Р5", "предплечье", "ось", "пронация ±80°", "—"),
      ("Р6/Р7", "запястье: сопряжение / сгиб", "ось / Y", "±90° / ±45°", "капсула в раструб"),
      ("Р8", "бедро", "X", "сгиб 0…+100°, разгиб −20°", "плита бедра в C4 / таз"),
      ("Р9", "бедро, пара косых колец 12°", "—", "отведение 0…35° / вращ. ±35°", "кольца в упор"),
      ("Р10", "колено", "X", "0…%d°" % KNEE_LIMIT, "икра в плиту бедра"),
      ("Р11/Р12", "голеностоп", "X / Y", "+20…−35° / инверсия ±15°", "голень в подъём")]
hx = [1860, 1930, 2125, 2195, 2420]
for i, hh in enumerate(["кольцо", "где", "ось", "диапазон", "упор (столкновение плит)"]): txt(hx[i], 1935, hh, 12, INK, weight="600")
for k, row in enumerate(KT):
    y = 1957 + k * 21
    if k % 2 == 0: out.append('<rect x="1852" y="%d" width="935" height="21" fill="#f0f2f4"/>' % (y - 15))
    for i, c in enumerate(row): txt(hx[i], y, c, 12, INK if i == 0 else THIN, weight="600" if i == 0 else "normal")
txt(1860, 2205, "X — поперёк тела, Y — спереди назад, Z — вертикаль. Усилие — мышцы под плитами; моторы держат угол и жёсткость.", 11.5, THIN)
# shoulder gimbal schematic
vg = View(1960, 2470, 700)
poly(vg, [(-0.10, 0.0), (0.04, 0.0), (0.06, 0.30), (-0.10, 0.30)], EN, INK, 1.2)
poly(vg, [(0.07, 0.29), (0.15, 0.29), (0.15, 0.03), (0.07, 0.03)], EN, INK, 1.2)
poly(vg, [(0.045, 0.06), (0.06, 0.06), (0.062, 0.29), (0.047, 0.29)], GR, INK, 1.0)
ring_motor(vg, (0.11, 0.30), 0.028)
band(vg, 0.07, 0.15, 0.17, 0.19, GR)
line(vg, (-0.03, 0.17), (0.24, 0.17), DIM, 1.2, "6,3"); vtxt(vg, 0.255, 0.165, "Р1: X", 11, DIM, "start")
line(vg, (0.11, 0.30), (0.11, 0.30), DIM, 1)
circ(vg, (0.11, 0.30), 0.006, DIM, DIM, 1); vtxt(vg, 0.15, 0.33, "Р2: Y (на нас)", 11, DIM, "start")
line(vg, (0.11, 0.36), (0.11, -0.02), DIM, 1.2, "6,3"); vtxt(vg, 0.12, -0.01, "Р3: ось руки", 11, DIM, "start")
vtxt(vg, 0.02, -0.06, "плечо: три кольца, спереди", 12, INK)
# hip oblique ring pair schematic
vh = View(2270, 2470, 700)
poly(vh, [(-0.07, 0.30), (0.07, 0.30), (0.07, 0.20), (-0.07, 0.185)], EN, INK, 1.2)
poly(vh, [(-0.07, 0.185), (0.07, 0.20), (0.07, 0.188), (-0.07, 0.173)], GR2, INK, 1.0)
poly(vh, [(-0.07, 0.173), (0.07, 0.188), (0.07, 0.176), (-0.07, 0.161)], GR, INK, 1.0)
poly(vh, [(-0.07, 0.161), (0.07, 0.176), (0.065, 0.0), (-0.065, 0.0)], EN, INK, 1.2)
pline(vh, [(-0.11, 0.185), (-0.13, 0.18), (-0.11, 0.17)], DIM, 1.2); pline(vh, [(0.11, 0.18), (0.13, 0.175), (0.11, 0.165)], DIM, 1.2)
vtxt(vh, 0.0, -0.06, "бедро Р9: пара косых колец 12°", 12, INK)
lines_(2370, 2290, ["встречно → отведение до 35°", "согласно → вращение бедра ±35°", "без складок и без поршней —", "как подшипники Mk III / AX-5,",
                    "но каждое кольцо — мотор"], 11.5, THIN, 15)
lines_(2600, 2290, ["Глубокий присед нельзя:", "бедро 100° + колено %d°." % KNEE_LIMIT, "Встать на колено — да", "(щиток колена = опора).",
                    "Руки к шлему — да (Р1 150°)."], 11.5, THIN, 15)

svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
svg_path = os.path.join(HERE, "pantsir_body_rev.svg"); png_path = os.path.join(HERE, "pantsir_body_rev.png")
open(svg_path, "w", encoding="utf-8").write(svg)
print("svg ok, knee limit", KNEE_LIMIT)
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
if os.path.exists(EDGE):
    subprocess.run([EDGE, "--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    "--window-size=%d,%d" % (W, H), "--screenshot=" + png_path, "file:///" + svg_path.replace("\\", "/")], check=False, timeout=120)
    print("png", os.path.exists(png_path))
