# «Панцирь» - the hand, three variants on the same standard wrist coupling (an AX-5-style quick joint: the end effector
# changes in the field). Each: the back of the hand, the palm, and the side (grip); scale 1 m = 1350 px.
#   A «Пятипалая» - a hard five-finger glove: flat phalanx plates on micro ring-motors at each knuckle (force assist),
#     contact pads on the fingertips and the palm; the back of the hand under shield plates
#   B «Трёхпалая» - thumb, index, and the middle+ring+little in one broad digit (a heavy-work gauntlet): the index and
#     thumb keep the fine work, the broad digit holds a handle with the whole hand; fewer seams - better shield and warmth
#   C «Манипулятор» - the hand inside a rigid sealed pod moves freely; outside a three-jaw manipulator copies it
#     (master-slave with force feedback, as the hard-suit/ADS prehensors); best protection, the tool heads change
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
S = 1350.0
W, H = 1700, 1250
INK, THIN, NOTE, ACC, GRID = "#1d2733", "#5d6b78", "#2f6f9f", "#c0692a", "#e2e6ea"
SHELL, PLATE, MOT, PAD, DARK, RED = "#e6e8ea", "#d3d8dc", "#4a525a", "#2b2f33", "#25303b", "#c8463c"
out = []
def P(ox, oy, x, y): return "%.1f,%.1f" % (ox + x * S, oy - y * S)
def poly(pts, ox, oy, fill="none", stroke=INK, w=2.0, op=1.0):
    out.append('<polygon points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"/>' %
               (" ".join(P(ox, oy, x, y) for x, y in pts), fill, op, stroke, w))
def line(a, b, ox, oy, stroke=THIN, w=1.3):
    out.append('<line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" stroke-width="%.1f"/>' % (*P(ox, oy, *a).split(","), *P(ox, oy, *b).split(","), stroke, w))
def circ(c, r, ox, oy, fill=MOT, w=1.4):
    x, y = P(ox, oy, *c).split(",")
    out.append('<circle cx="%s" cy="%s" r="%.1f" fill="%s" stroke="%s" stroke-width="%.1f"/>' % (x, y, r * S, fill, INK, w))
def txt(x, y, s, size=14, color=NOTE, anchor="start", weight="normal"):
    out.append('<text x="%.1f" y="%.1f" font-family="Segoe UI, Arial" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, size, color, anchor, weight, s))

def digit(ox, oy, base, ang, segs, wid, plates=True, pad=True):
    """a finger as chamfered flat segments from base along angle (deg from up), knuckle motors between"""
    a = math.radians(ang); ux, uy = math.sin(a), math.cos(a); nx, ny = uy, -ux
    x, y = base
    for k, L in enumerate(segs):
        w0, w1 = wid * (1 - 0.06 * k), wid * (1 - 0.06 * (k + 1))
        c = 0.18 * w0
        pts = [(x - nx * w0 / 2, y - ny * w0 / 2), (x + nx * w0 / 2, y + ny * w0 / 2),
               (x + nx * w1 / 2 + ux * (L - c), y + ny * w1 / 2 + uy * (L - c)), (x + nx * (w1 / 2 - c) + ux * L, y + ny * (w1 / 2 - c) + uy * L),
               (x - nx * (w1 / 2 - c) + ux * L, y - ny * (w1 / 2 - c) + uy * L), (x - nx * w1 / 2 + ux * (L - c), y - ny * w1 / 2 + uy * (L - c))]
        poly(pts, ox, oy, PLATE if plates else SHELL, w=1.8)
        if k == len(segs) - 1 and pad:
            poly([(x - nx * w1 * 0.35 + ux * (L * 0.45), y - ny * w1 * 0.35 + uy * (L * 0.45)), (x + nx * w1 * 0.35 + ux * (L * 0.45), y + ny * w1 * 0.35 + uy * (L * 0.45)),
                  (x + nx * w1 * 0.3 + ux * (L * 0.9), y + ny * w1 * 0.3 + uy * (L * 0.9)), (x - nx * w1 * 0.3 + ux * (L * 0.9), y - ny * w1 * 0.3 + uy * (L * 0.9))], ox, oy, PAD, w=1.0)
        x, y = x + ux * L, y + uy * L
        if k < len(segs) - 1:
            line((x - nx * w1 * 0.5, y - ny * w1 * 0.5), (x + nx * w1 * 0.5, y + ny * w1 * 0.5), ox, oy, MOT, 4.0)   # the knuckle ring motor

def wrist(ox, oy):
    poly([(-0.048, -0.02), (0.048, -0.02), (0.050, 0.012), (-0.050, 0.012)], ox, oy, MOT, w=1.8)                  # the coupling ring
    for k in range(7): line((-0.044 + k * 0.0147, -0.018), (-0.044 + k * 0.0147, 0.010), ox, oy, "#7d8994", 1.0)
    circ((0.05, -0.004), 0.006, ox, oy, RED, 1.0)                                                                  # the release
    poly([(-0.055, -0.02), (0.055, -0.02), (0.05, -0.06), (-0.05, -0.06)], ox, oy, SHELL, w=1.8)                  # the cuff (forearm end)

def hand_A(ox, oy, view):
    wrist(ox, oy)
    palm = [(-0.045, 0.012), (0.045, 0.012), (0.050, 0.075), (0.040, 0.098), (-0.042, 0.098), (-0.050, 0.07)]
    poly(palm, ox, oy, SHELL if view == "palm" else PLATE, w=2.2)
    if view == "palm":
        for x0, y0 in ((-0.03, 0.03), (0.0, 0.03), (0.03, 0.03), (-0.02, 0.065), (0.015, 0.065)):
            poly([(x0 - 0.012, y0 - 0.010), (x0 + 0.012, y0 - 0.010), (x0 + 0.012, y0 + 0.012), (x0 - 0.012, y0 + 0.012)], ox, oy, PAD, w=0.8)
    else:
        for k in range(3): line((-0.04, 0.035 + k * 0.02), (0.04, 0.035 + k * 0.02), ox, oy, INK, 1.0)       # shield plates on the back
        txt(ox - 12, oy - 0.055 * S, "2-0417", 9, THIN)
    for bx, ang, segs, w in ((-0.033, -4, (0.040, 0.026, 0.022), 0.018), (-0.011, -1, (0.044, 0.028, 0.024), 0.019),
                             (0.011, 2, (0.042, 0.026, 0.022), 0.018), (0.032, 6, (0.034, 0.022, 0.019), 0.016)):
        digit(ox, oy, (bx, 0.098), ang, segs, w)
    digit(ox, oy, (-0.048 if view == "back" else 0.048, 0.035), -55 if view == "back" else 55, (0.034, 0.028, 0.022), 0.021)   # thumb
    return ["Пять пальцев, каждая фаланга - плоская плита", "на кольцевом микромоторе в суставе (усилие", "помогает мотор, чувствительность - подушки на",
            "кончиках и ладони). Самая тонкая работа,", "но больше всего швов и слабее экран кисти."]

def hand_B(ox, oy, view):
    wrist(ox, oy)
    palm = [(-0.046, 0.012), (0.046, 0.012), (0.052, 0.08), (0.040, 0.102), (-0.044, 0.102), (-0.052, 0.07)]
    poly(palm, ox, oy, SHELL if view == "palm" else PLATE, w=2.2)
    if view == "palm":
        for x0, y0 in ((-0.025, 0.035), (0.02, 0.035), (0.0, 0.072)):
            poly([(x0 - 0.016, y0 - 0.012), (x0 + 0.016, y0 - 0.012), (x0 + 0.016, y0 + 0.014), (x0 - 0.016, y0 + 0.014)], ox, oy, PAD, w=0.8)
    else:
        for k in range(3): line((-0.042, 0.035 + k * 0.022), (0.042, 0.035 + k * 0.022), ox, oy, INK, 1.0)
        poly([(0.032, 0.02), (0.046, 0.02), (0.046, 0.05), (0.032, 0.05)], ox, oy, RED, w=0.8)                  # grip lock indicator
    s = -1 if view == "back" else 1
    digit(ox, oy, (s * -0.028, 0.102), s * -3, (0.046, 0.030, 0.025), 0.022)                                        # index
    digit(ox, oy, (s * 0.016, 0.102), s * 3, (0.044, 0.030, 0.026), 0.048)                                          # the broad digit (3 fingers)
    line((s * 0.000, 0.110), (s * 0.000, 0.19), ox, oy, THIN, 0.8); line((s * 0.032, 0.110), (s * 0.032, 0.19), ox, oy, THIN, 0.8)
    digit(ox, oy, (s * -0.050, 0.035), s * -55, (0.036, 0.030, 0.024), 0.023)                                       # thumb
    return ["Большой, указательный и широкий палец под", "средний, безымянный и мизинец. Указательный", "с большим - тонкая работа, широкий держит",
            "поручень всей кистью (замок хвата снимает", "нагрузку с мышц). Меньше швов - экран и тепло лучше."]

def hand_C(ox, oy, view):
    wrist(ox, oy)
    pod = [(-0.05, 0.012), (0.05, 0.012), (0.058, 0.07), (0.05, 0.11), (-0.05, 0.11), (-0.058, 0.07)]
    poly(pod, ox, oy, PLATE, w=2.4)                                                                                 # the sealed hand pod
    if view == "back":
        for k in range(4): line((-0.048, 0.03 + k * 0.02), (0.048, 0.03 + k * 0.02), ox, oy, INK, 1.0)
        txt(ox - 22, oy - 0.06 * S, "МАНИП.", 9, THIN)
    else:
        poly([(-0.035, 0.03), (0.035, 0.03), (0.035, 0.09), (-0.035, 0.09)], ox, oy, "#9fb4c4", w=1.0, op=0.5)      # the hand inside (dashed)
    poly([(-0.03, 0.11), (0.03, 0.11), (0.028, 0.125), (-0.028, 0.125)], ox, oy, MOT, w=1.6)                     # tool coupling
    for ang in (-25, 0, 25):
        a = math.radians(ang)
        digit(ox, oy, (math.sin(a) * 0.02, 0.125), ang * 0.6, (0.040, 0.034), 0.020)
    return ["Рука внутри жёсткой герметичной капсулы", "двигается свободно, снаружи трёхчелюстной", "манипулятор повторяет её с обратной связью",
            "по усилию. Лучший экран и сила, хуже тонкость.", "Головки меняются: хват, бур, пробоотборник."]

VARS = [("A  «Пятипалая»", hand_A), ("B  «Трёхпалая»", hand_B), ("C  «Манипулятор»", hand_C)]
out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
txt(30, 44, "«Панцирь» · кисть: три варианта на одной стандартной стыковке запястья (меняется в поле)", 24, INK, weight="600")
txt(30, 70, "плоские гранёные фаланги, моторы в суставах пальцев, подушки контакта; без круглых ладоней · сзади / ладонь", 14, THIN)
for i, (title, fn) in enumerate(VARS):
    y0 = 95 + i * 380
    out.append('<rect x="5" y="%d" width="%d" height="370" fill="none" stroke="%s"/>' % (y0, W - 10, GRID))
    txt(25, y0 + 32, title, 22, INK, weight="600")
    notes = None
    for j, (view, lab) in enumerate((("back", "тыльная сторона"), ("palm", "ладонь"))):
        ox, oy = 260 + j * 380, y0 + 280
        notes = fn(ox, oy, view)
        txt(ox, oy + 28, lab, 12.5, THIN, "middle")
    for k, s in enumerate(notes): txt(1000, y0 + 90 + k * 26, s, 17, NOTE)
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_hands.svg"), "w", encoding="utf-8").write(svg)
print("ok")
