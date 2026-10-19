# «Панцирь» - the mechanised hand (user, 2026-10-16: the man's hand stays inside the protected volume, outside only
# machinery). Three variants of capsule + three-digit effector; each: side view with the hand inside as a ghost, and
# top view (back of the hand). Scale 1 m = 1500 px. Coordinates: x forward along the forearm, y up; 0 = coupling face.
#   1 «Кулак»   - axial: the fist on a grip inside a short capsule, the effector continues the forearm axis
#   2 «Ладонь»  - anthropomorphic: the flat hand in a finger cradle inside a palm-shaped capsule, effector fingers
#                 start where the man's knuckles are and go on like fingers
#   3 «Захват»  - the capsule is a closed cuff over the fist, the effector rides on its own two-axis powered wrist
#                 (pitch + yaw ring motors), longer digits, the thumb on a turret (opposes either digit)
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
S = 1500.0
W, H = 1950, 1420
INK, THIN, NOTE, GRID = "#1d2733", "#5d6b78", "#2f6f9f", "#e2e6ea"
SHELL, PLATE, MOT, PAD, CORE, RED, TILE = "#e6e8ea", "#d3d8dc", "#4a525a", "#2b2f33", "#7d8790", "#c8463c", "#f1efe8"
GHOST, GRIP = "#e59a55", "#4f86c6"
out = []
def P(ox, oy, x, y): return "%.1f,%.1f" % (ox + x * S, oy - y * S)
def poly(pts, ox, oy, fill="none", stroke=INK, w=2.0, op=1.0, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    out.append('<polygon points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' %
               (" ".join(P(ox, oy, x, y) for x, y in pts), fill, op, stroke, w, d))
def line(a, b, ox, oy, stroke=THIN, w=1.3, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    out.append('<line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" stroke-width="%.1f"%s/>' % (*P(ox, oy, *a).split(","), *P(ox, oy, *b).split(","), stroke, w, d))
def circ(c, r, ox, oy, fill=MOT, w=1.4, stroke=INK):
    x, y = P(ox, oy, *c).split(",")
    out.append('<circle cx="%s" cy="%s" r="%.1f" fill="%s" stroke="%s" stroke-width="%.1f"/>' % (x, y, r * S, fill, stroke, w))
def txt(x, y, s, size=14, color=NOTE, anchor="start", weight="normal"):
    out.append('<text x="%.1f" y="%.1f" font-family="Segoe UI, Arial" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, size, color, anchor, weight, s))

def chain(ox, oy, base, ang0, lens, th, curl, pad=-1, plate=PLATE):
    """a machine digit seen from the side (or top): chamfered segments, each turned 'curl' deg more (negative = down),
    a ring motor in every joint, the contact pad on side 'pad' (-1 = lower face, +1 = upper, 0 = none)"""
    x, y = base
    a = ang0
    for k, L in enumerate(lens):
        t0, t1 = th * (1 - 0.08 * k), th * (1 - 0.08 * (k + 1))
        r = math.radians(a); ux, uy = math.cos(r), math.sin(r); nx, ny = -uy, ux
        c = 0.22 * t1
        pts = [(x + nx * t0 / 2, y + ny * t0 / 2), (x + ux * (L - c) + nx * t1 / 2, y + uy * (L - c) + ny * t1 / 2),
               (x + ux * L + nx * (t1 / 2 - c), y + uy * L + ny * (t1 / 2 - c)), (x + ux * L - nx * (t1 / 2 - c), y + uy * L - ny * (t1 / 2 - c)),
               (x + ux * (L - c) - nx * t1 / 2, y + uy * (L - c) - ny * t1 / 2), (x - nx * t0 / 2, y - ny * t0 / 2)]
        poly(pts, ox, oy, plate, w=1.8)
        # the back plate: a raised shield over the core, overlapping onto the next segment
        line((x + ux * L * 0.15 + nx * t0 * 0.32, y + uy * L * 0.15 + ny * t0 * 0.32), (x + ux * L * 0.95 + nx * t1 * 0.32, y + uy * L * 0.95 + ny * t1 * 0.32), ox, oy, THIN, 1.0)
        if pad:
            s = pad
            q = [(x + ux * L * 0.18 + s * nx * t0 * 0.5, y + uy * L * 0.18 + s * ny * t0 * 0.5),
                 (x + ux * L * 0.82 + s * nx * t1 * 0.5, y + uy * L * 0.82 + s * ny * t1 * 0.5),
                 (x + ux * L * 0.78 + s * nx * t1 * 0.30, y + uy * L * 0.78 + s * ny * t1 * 0.30),
                 (x + ux * L * 0.22 + s * nx * t0 * 0.30, y + uy * L * 0.22 + s * ny * t0 * 0.30)]
            poly(q, ox, oy, PAD, w=0.8)
        rm = min(th, 0.028) * 0.36
        circ((x, y), rm, ox, oy, MOT, 1.2)
        circ((x, y), rm * 0.38, ox, oy, CORE, 0.8)
        x, y = x + ux * L, y + uy * L
        a += curl
    return x, y

def forearm(ox, oy, h=0.105, top=False):
    # the forearm end with its tiles, then the coupling ring (x -0.02..0) with the red release
    poly([(-0.10, -h / 2 - 0.008), (-0.02, -h / 2), (-0.02, h / 2), (-0.10, h / 2 + 0.008)], ox, oy, SHELL, w=2.0)
    for k in range(3):
        poly([(-0.095 + k * 0.026, h / 2 + 0.006), (-0.072 + k * 0.026, h / 2 + 0.004), (-0.070 + k * 0.026, h / 2 + 0.016), (-0.097 + k * 0.026, h / 2 + 0.018)], ox, oy, TILE, w=1.0)
    poly([(-0.02, -h / 2 - 0.006), (0.0, -h / 2 - 0.006), (0.0, h / 2 + 0.006), (-0.02, h / 2 + 0.006)], ox, oy, MOT, w=1.8)
    for k in range(6): line((-0.02 + 0.0033 * k + 0.0017, -h / 2 - 0.004), (-0.02 + 0.0033 * k + 0.0017, h / 2 + 0.004), ox, oy, "#7d8994", 0.9)
    if not top: circ((-0.01, h / 2 + 0.012), 0.0055, ox, oy, RED, 1.0)

def ghost_fist(ox, oy, x0, y0, sc=1.0):
    # the man's fist (side) around a vertical grip, thumb on top: an outline only
    f = [(x0 - 0.045 * sc, y0 - 0.030 * sc), (x0 + 0.020 * sc, y0 - 0.036 * sc), (x0 + 0.042 * sc, y0 - 0.018 * sc), (x0 + 0.045 * sc, y0 + 0.020 * sc),
         (x0 + 0.030 * sc, y0 + 0.036 * sc), (x0 - 0.010 * sc, y0 + 0.040 * sc), (x0 - 0.045 * sc, y0 + 0.030 * sc)]
    poly([(x0 + 0.004 * sc, y0 - 0.052 * sc), (x0 + 0.020 * sc, y0 - 0.052 * sc), (x0 + 0.020 * sc, y0 + 0.050 * sc), (x0 + 0.004 * sc, y0 + 0.050 * sc)], ox, oy, GRIP, w=1.0, op=0.55)
    poly(f, ox, oy, GHOST, stroke=GHOST, w=1.6, op=0.18, dash="5,3")
    for k in range(3): line((x0 + 0.020 * sc, y0 - 0.022 * sc + k * 0.018 * sc), (x0 + 0.042 * sc, y0 - 0.022 * sc + k * 0.018 * sc), ox, oy, GHOST, 1.0, "3,3")

def ghost_flat(ox, oy, x0, y0):
    # the flat hand (side): palm and fingers straight in a finger cradle, thumb below
    poly([(x0 - 0.04, y0 - 0.014), (x0 + 0.05, y0 - 0.012), (x0 + 0.13, y0 - 0.008), (x0 + 0.135, y0 + 0.006), (x0 + 0.05, y0 + 0.014), (x0 - 0.04, y0 + 0.018)],
         ox, oy, GHOST, stroke=GHOST, w=1.6, op=0.18, dash="5,3")
    poly([(x0 + 0.0, y0 - 0.012), (x0 + 0.05, y0 - 0.040), (x0 + 0.06, y0 - 0.030), (x0 + 0.02, y0 - 0.006)], ox, oy, GHOST, stroke=GHOST, w=1.4, op=0.18, dash="5,3")
    for xr in (0.06, 0.095, 0.125):   # finger rings of the cradle (master sensors)
        poly([(x0 + xr - 0.004, y0 - 0.016), (x0 + xr + 0.004, y0 - 0.016), (x0 + xr + 0.004, y0 + 0.016), (x0 + xr - 0.004, y0 + 0.016)], ox, oy, GRIP, w=0.8, op=0.55)

def capsule(ox, oy, pts, tiles):
    poly(pts, ox, oy, SHELL, w=2.4, op=0.82)
    for t in tiles: poly(t, ox, oy, TILE, w=1.0)

def tiles_row(x0, x1, y, n, h=0.012, up=True):
    w = (x1 - x0) / n; r = []
    for k in range(n):
        a = x0 + k * w; s = 1 if up else -1
        r.append([(a, y), (a + w * 1.30, y + s * 0.003), (a + w * 1.28, y + s * (h * 0.6 + 0.003)), (a - 0.002, y + s * h * 0.6)])   # shingled, overlapping
    return r

# --- variant 1 «Кулак» -------------------------------------------------------------------------------------------
def v1(ox, oy, view):
    forearm(ox, oy, top=view == "top")
    if view == "side":
        capsule(ox, oy, [(0, -0.058), (0.10, -0.052), (0.135, -0.034), (0.140, 0.030), (0.115, 0.054), (0, 0.060)],
                tiles_row(0.006, 0.11, 0.058, 4))
        ghost_fist(ox, oy, 0.055, 0.0)
        poly([(0.138, -0.03), (0.152, -0.026), (0.152, 0.026), (0.138, 0.03)], ox, oy, MOT, w=1.6)        # effector flange
        chain(ox, oy, (0.152, 0.016), 4, (0.050, 0.036, 0.030), 0.030, -22)                               # index + broad (one behind)
        chain(ox, oy, (0.150, -0.020), -28, (0.044, 0.034), 0.028, 40, pad=1)                             # the thumb under
    else:
        capsule(ox, oy, [(0, -0.062), (0.11, -0.058), (0.140, -0.040), (0.140, 0.040), (0.11, 0.058), (0, 0.062)],
                tiles_row(0.006, 0.12, -0.045, 4) + tiles_row(0.006, 0.12, 0.002, 4))
        poly([(0.138, -0.04), (0.152, -0.036), (0.152, 0.036), (0.138, 0.04)], ox, oy, MOT, w=1.6)
        chain(ox, oy, (0.152, 0.022), 2, (0.050, 0.036, 0.030), 0.022, 0, pad=0)                         # index
        chain(ox, oy, (0.152, -0.012), -2, (0.048, 0.036, 0.030), 0.042, 0, pad=0)                       # broad digit
        chain(ox, oy, (0.140, 0.040), 55, (0.036, 0.030), 0.024, -30, pad=0)                             # the thumb
    return ["Рука сжата на рукояти внутри короткой капсулы,", "эффектор продолжает ось предплечья.",
            "+ капсула малая, броня и теплоэкран проще всего", "+ рукоять - привычный задатчик: сжал, повернул",
            "- кисть короткая и тупая, на силуэте как «молоток»", "- тонкая работа слабее: пальцы человека не задают", "  каждый палец эффектора отдельно"]

# --- variant 2 «Ладонь» ------------------------------------------------------------------------------------------
def v2(ox, oy, view):
    forearm(ox, oy, top=view == "top")
    if view == "side":
        capsule(ox, oy, [(0, -0.056), (0.08, -0.048), (0.15, -0.026), (0.16, -0.010), (0.16, 0.022), (0.13, 0.038), (0.06, 0.050), (0, 0.058)],
                tiles_row(0.006, 0.13, 0.046, 5, h=0.010))
        ghost_flat(ox, oy, 0.02, 0.006)
        poly([(0.158, -0.012), (0.170, -0.010), (0.170, 0.022), (0.158, 0.024)], ox, oy, MOT, w=1.5)
        chain(ox, oy, (0.170, 0.008), -6, (0.046, 0.032, 0.026), 0.026, -18)                              # fingers go on from the knuckles
        chain(ox, oy, (0.100, -0.044), -35, (0.040, 0.032), 0.026, 32, pad=1)                             # thumb from the thenar
    else:
        capsule(ox, oy, [(0, -0.060), (0.10, -0.062), (0.16, -0.046), (0.168, 0.0), (0.16, 0.044), (0.10, 0.052), (0, 0.060)],
                tiles_row(0.006, 0.15, -0.050, 5, h=0.016) + tiles_row(0.006, 0.15, -0.012, 5, h=0.016) + tiles_row(0.006, 0.13, 0.026, 4, h=0.016))
        poly([(0.164, -0.044), (0.174, -0.042), (0.174, 0.040), (0.164, 0.042)], ox, oy, MOT, w=1.5)
        chain(ox, oy, (0.174, 0.024), 3, (0.046, 0.032, 0.026), 0.022, 0, pad=0)
        chain(ox, oy, (0.174, -0.016), -2, (0.044, 0.032, 0.026), 0.044, 0, pad=0)
        chain(ox, oy, (0.110, 0.052), 62, (0.036, 0.030), 0.024, -32, pad=0)
    return ["Ладонь прямая в «люльке» с кольцами на пальцах;", "пальцы эффектора начинаются там, где костяшки.",
            "+ силуэт человеческой кисти, длина руки верная", "+ каждый палец задаётся своим пальцем человека",
            "- капсула длинная: больше брони, тяжелее кисть", "- ладонь прямая - рука устаёт без хвата"]

# --- variant 3 «Захват» ------------------------------------------------------------------------------------------
def v3(ox, oy, view):
    forearm(ox, oy, top=view == "top")
    if view == "side":
        capsule(ox, oy, [(0, -0.056), (0.09, -0.050), (0.115, -0.030), (0.115, 0.030), (0.09, 0.050), (0, 0.058)],
                tiles_row(0.006, 0.09, 0.054, 3))
        ghost_fist(ox, oy, 0.05, 0.0, 0.95)
        circ((0.132, 0.0), 0.024, ox, oy, MOT, 1.6); circ((0.132, 0.0), 0.009, ox, oy, CORE, 1.0)          # wrist pitch motor
        poly([(0.150, -0.026), (0.175, -0.022), (0.175, 0.022), (0.150, 0.026)], ox, oy, PLATE, w=1.6)    # yaw block
        line((0.162, -0.024), (0.162, 0.024), ox, oy, MOT, 3.0)
        chain(ox, oy, (0.176, 0.012), 0, (0.058, 0.042, 0.032), 0.028, -20)
        chain(ox, oy, (0.170, -0.018), -38, (0.048, 0.038), 0.028, 45, pad=1)
    else:
        capsule(ox, oy, [(0, -0.060), (0.095, -0.056), (0.115, -0.036), (0.115, 0.036), (0.095, 0.056), (0, 0.060)],
                tiles_row(0.006, 0.095, -0.040, 3) + tiles_row(0.006, 0.095, 0.004, 3))
        poly([(0.115, -0.018), (0.132, -0.022), (0.132, 0.022), (0.115, 0.018)], ox, oy, MOT, w=1.4)
        circ((0.150, 0.0), 0.024, ox, oy, MOT, 1.6); circ((0.150, 0.0), 0.009, ox, oy, CORE, 1.0)          # wrist yaw motor
        poly([(0.160, -0.040), (0.178, -0.036), (0.178, 0.036), (0.160, 0.040)], ox, oy, PLATE, w=1.6)
        chain(ox, oy, (0.178, 0.020), 2, (0.058, 0.042, 0.032), 0.022, 0, pad=0)
        chain(ox, oy, (0.178, -0.014), -2, (0.056, 0.042, 0.032), 0.044, 0, pad=0)
        circ((0.170, 0.040), 0.012, ox, oy, MOT, 1.2)                                                     # thumb turret
        chain(ox, oy, (0.170, 0.040), 60, (0.044, 0.036), 0.024, -35, pad=0)
    return ["Капсула - короткая манжета над кулаком,", "эффектор на своём запястье (два кольцевых мотора).",
            "+ запястье эффектора гнётся само: рука в капсуле", "  неподвижна, броня без складок", "+ самые длинные и сильные пальцы, большой на поворотной",
            "  башенке - противостоит любому", "- меньше всего похож на кисть человека"]

VARS = [("1  «Кулак»", v1), ("2  «Ладонь»", v2), ("3  «Захват»", v3)]
out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
txt(30, 44, "«Панцирь» · механизированная кисть: рука человека в капсуле, снаружи только механика · 3 варианта", 24, INK, weight="600")
txt(30, 70, "кольцевые моторы в суставах, цельные сегменты, накладки только на гранях хвата · пунктир - рука человека, синее - задатчик · 1 м = 1500 px", 14, THIN)
for i, (title, fn) in enumerate(VARS):
    y0 = 90 + i * 440
    out.append('<rect x="5" y="%d" width="%d" height="430" fill="none" stroke="%s"/>' % (y0, W - 10, GRID))
    txt(25, y0 + 32, title, 22, INK, weight="600")
    notes = None
    for j, (view, lab) in enumerate((("side", "сбоку (рука внутри)"), ("top", "сверху"))):
        ox, oy = 250 + j * 620, y0 + 230
        notes = fn(ox, oy, view)
        txt(ox + 0.09 * S, y0 + 410, lab, 13, THIN, "middle")
    for k, s in enumerate(notes): txt(1400, y0 + 80 + k * 30, s, 16, NOTE)
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_mech_hands.svg"), "w", encoding="utf-8").write(svg)
print("ok")
