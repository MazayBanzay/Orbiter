# «Панцирь» - the helmet: its whole front pressure contour (armour, heat screen, slit window and glass in one shell) opens,
# split from the chin level up, lifted by actuators; what stays is the seal ring round the face on the fixed rear shell.
# Three kinematics of the same contour (size ~0.31 m, sharp faceted forms):
#   K1 the axis at the temples: the contour turns 100° over the crown and lies on it
#   K2 the axis at the back of the head: it lifts like a bonnet and stands up behind
#   K3 the axis over the brow: it swings up and forward and stays as a canopy over the face (shade from heat and glow)
# Each: front and side, closed and open; the head inside for scale (eyes at 1.70 m). Scale 1 m = 820 px.
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
S = 820.0
W, H = 1620, 2000
INK, THIN, NOTE, ACC, GRID = "#1d2733", "#5d6b78", "#2f6f9f", "#c0692a", "#e2e6ea"
SHELL, SHELL2, BEAR, GOLD, GOLD2, DARK, GLASS, SKIN, ACT = "#eef0f2", "#dde2e7", "#c9d1d8", "#c9a24a", "#b48a2e", "#25303b", "#9fb4c4", "#e8c6a8", "#7d8994"
out = []
def X(ox, x): return ox + x * S
def Y(oy, y): return oy - (y - 1.55) * S
def poly(pts, ox, oy, fill="none", stroke=INK, w=2.2, op=1.0, dash=None):
    out.append('<polygon points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' %
               (" ".join("%.1f,%.1f" % (X(ox, x), Y(oy, y)) for x, y in pts), fill, op, stroke, w, ' stroke-dasharray="%s"' % dash if dash else ""))
def line(a, b, ox, oy, stroke=THIN, w=1.3, dash=None):
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f"%s/>' %
               (X(ox, a[0]), Y(oy, a[1]), X(ox, b[0]), Y(oy, b[1]), stroke, w, ' stroke-dasharray="%s"' % dash if dash else ""))
def circ(c, r, ox, oy, fill=BEAR, w=1.6):
    out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" stroke="%s" stroke-width="%.1f"/>' % (X(ox, c[0]), Y(oy, c[1]), r * S, fill, INK, w))
def ell(c, rx, ry, ox, oy, fill, w=1.2):
    out.append('<ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="%s" stroke="%s" stroke-width="%.1f"/>' % (X(ox, c[0]), Y(oy, c[1]), rx * S, ry * S, fill, INK, w))
def arc(a, b, r, ox, oy, sweep=0):
    out.append('<path d="M %.1f %.1f A %.1f %.1f 0 0 %d %.1f %.1f" fill="none" stroke="%s" stroke-width="1.4" stroke-dasharray="5,4"/>' %
               (X(ox, a[0]), Y(oy, a[1]), r * S, r * S, sweep, X(ox, b[0]), Y(oy, b[1]), ACC))
def txt(x, y, s, size=14, color=NOTE, anchor="start", weight="normal"):
    out.append('<text x="%.1f" y="%.1f" font-family="Segoe UI, Arial" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, size, color, anchor, weight, s))
def mir(pts): return pts + [(-x, y) for x, y in reversed(pts)]
def rot(pts, c, deg):
    a = math.radians(deg); ca, sa = math.cos(a), math.sin(a)
    return [(c[0] + (x - c[0]) * ca - (y - c[1]) * sa, c[1] + (x - c[0]) * sa + (y - c[1]) * ca) for x, y in pts]
def actuator(a, b, ox, oy):          # a linear actuator: cylinder and rod
    mx, my = a[0] + (b[0] - a[0]) * 0.55, a[1] + (b[1] - a[1]) * 0.55
    line(a, (mx, my), ox, oy, ACT, 7); line((mx, my), b, ox, oy, INK, 3)
    circ(a, 0.007, ox, oy, BEAR, 1.2); circ(b, 0.007, ox, oy, BEAR, 1.2)

# ---- the common parts ----
CHIN = 1.615                              # the split: chin level
REAR_F = mir([(0.0, 1.86), (0.08, 1.855), (0.14, 1.81), (0.162, 1.73), (0.163, 1.65), (0.155, 1.585)])   # the shell, front outline
CONTOUR_F = mir([(0.0, 1.80), (0.10, 1.795), (0.135, 1.75), (0.146, 1.68), (0.135, CHIN), (0.0, CHIN - 0.004)])   # the opening contour
SIDE = [(-0.15, 1.575), (0.115, 1.575), (0.165, 1.61), (0.183, 1.70), (0.175, 1.78), (0.125, 1.835), (0.02, 1.86), (-0.09, 1.85), (-0.165, 1.79), (-0.18, 1.69)]
# the front contour in side view: from the chin level up to the brow / crown line (a thick shell)
HATCH_S = [(0.085, CHIN), (0.15, CHIN), (0.178, 1.66), (0.188, 1.71), (0.18, 1.78), (0.135, 1.825), (0.06, 1.835), (0.04, 1.80), (0.10, 1.79), (0.12, 1.70), (0.10, 1.64)]

def face_front(ox, oy):
    ell((0, 1.69), 0.062, 0.088, ox, oy, SKIN)
    for s in (1, -1): ell((s * 0.024, 1.705), 0.010, 0.005, ox, oy, "#4b5966", 0.8)
    line((-0.012, 1.655), (0.012, 1.655), ox, oy, "#8a5a4a", 1.4)
def face_side(ox, oy):
    poly([(0.045, 1.60), (0.07, 1.625), (0.078, 1.645), (0.072, 1.655), (0.085, 1.672), (0.098, 1.69), (0.082, 1.70), (0.08, 1.73), (0.07, 1.76), (0.03, 1.785),
          (-0.04, 1.785), (-0.085, 1.75), (-0.095, 1.70), (-0.08, 1.645), (-0.04, 1.62), (-0.02, 1.57), (0.02, 1.57)], ox, oy, SKIN, w=1.2)
    ell((0.062, 1.705), 0.008, 0.005, ox, oy, "#4b5966", 0.8)
def collar_front(ox, oy):
    poly([(-0.22, 1.49), (-0.17, 1.545), (0.17, 1.545), (0.22, 1.49), (0.22, 1.45), (-0.22, 1.45)], ox, oy, SHELL, w=2.0)
    poly([(-0.17, 1.545), (0.17, 1.545), (0.165, 1.575), (-0.165, 1.575)], ox, oy, BEAR, w=1.8)
def collar_side(ox, oy):
    poly([(-0.17, 1.45), (0.19, 1.45), (0.17, 1.50), (0.12, 1.545), (-0.14, 1.545), (-0.18, 1.49)], ox, oy, SHELL, w=2.0)
    poly([(-0.14, 1.545), (0.12, 1.545), (0.12, 1.575), (-0.14, 1.575)], ox, oy, BEAR, w=1.8)
def hatch_face(ox, oy):
    """the closed contour seen from the front: armour + heat screen (gold), the slit window, the jaw grille"""
    poly(CONTOUR_F, ox, oy, GOLD, w=2.4)
    poly(mir([(0.0, 1.758), (0.105, 1.752), (0.118, 1.72), (0.0, 1.724)]), ox, oy, GOLD2, w=1.6)      # the brow block
    poly(mir([(0.0, 1.712), (0.092, 1.71), (0.092, 1.698), (0.0, 1.70)]), ox, oy, DARK, w=1.0)       # the slit window
    for k in range(3): line((-0.05, 1.635 + k * 0.012), (0.05, 1.635 + k * 0.012), ox, oy, DARK, 2.0)
def opening_front(ox, oy):
    poly(CONTOUR_F, ox, oy, "#2f3a46", w=2.0)
    poly(CONTOUR_F, ox, oy, "none", ACC, 3.4)                                                          # the seal ring
    face_front(ox, oy)
def base_front(ox, oy):
    collar_front(ox, oy)
    poly(REAR_F, ox, oy, SHELL, w=2.6)
    line((0, 1.86), (0, 1.81), ox, oy, THIN, 1.6)
def base_side(ox, oy, opened):
    collar_side(ox, oy)
    poly(SIDE, ox, oy, SHELL, w=2.6)
    line((0.125, 1.835), (-0.165, 1.79), ox, oy, THIN)
    if opened: face_side(ox, oy); line((0.085, CHIN), (0.06, 1.835), ox, oy, ACC, 3.0)               # the seal ring edge-on
    else: poly(HATCH_S, ox, oy, GOLD, w=2.4); line((0.185, 1.705), (0.19, 1.705), ox, oy, DARK, 3)

# ---------------- the upper part of the helmet opens: the cap (face + forehead + the front of the crown) in one shell,
# split from the chin up the cheeks, behind the ears to the back of the crown; the back of the head stays ----------------
SPLIT_A, SPLIT_B = (0.105, CHIN), (-0.125, 1.83)              # the split line in the side view: chin (front) -> behind the crown
CAP_S = [(0.105, CHIN), (0.15, CHIN), (0.178, 1.66), (0.188, 1.71), (0.18, 1.78), (0.13, 1.835), (0.02, 1.862), (-0.09, 1.852), (-0.125, 1.83)]
OPEN_F = mir([(0.0, 1.835), (0.09, 1.83), (0.13, 1.79), (0.146, 1.71), (0.135, CHIN), (0.0, CHIN - 0.004)])   # face + forehead
def cap_front_closed(ox, oy):
    poly(mir([(0.0, 1.758), (0.105, 1.752), (0.118, 1.72), (0.0, 1.724)]), ox, oy, GOLD2, w=1.6)            # brow block (heat screen)
    poly(mir([(0.0, 1.712), (0.092, 1.71), (0.092, 1.698), (0.0, 1.70)]), ox, oy, DARK, w=1.0)             # the slit window
    for k in range(3): line((-0.05, 1.635 + k * 0.012), (0.05, 1.635 + k * 0.012), ox, oy, DARK, 2.0)
    for s_ in (1, -1): line((s_ * 0.135, CHIN), (s_ * 0.160, 1.70), ox, oy, INK, 1.8)                       # the split up the cheeks
def open_front(ox, oy):
    poly(OPEN_F, ox, oy, "#2f3a46", w=2.0); poly(OPEN_F, ox, oy, "none", ACC, 3.4); face_front(ox, oy)
    out.append('<ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="#3a2e28" stroke="none"/>' % (X(ox, 0), Y(oy, 1.775), 0.062 * S, 0.035 * S))  # hair
def side_common(ox, oy, opened):
    collar_side(ox, oy)
    back = [(-0.15, 1.575), (0.105, 1.575), (0.105, CHIN), (-0.125, 1.83), (-0.165, 1.79), (-0.18, 1.69)]    # the fixed back part
    if opened:
        poly(back, ox, oy, SHELL, w=2.6); face_side(ox, oy)
        line(SPLIT_A, SPLIT_B, ox, oy, ACC, 3.0)                                                              # the seal on the split
    else:
        poly(SIDE, ox, oy, SHELL, w=2.6); line(SPLIT_A, SPLIT_B, ox, oy, INK, 1.8)
        poly([(0.15, 1.66), (0.188, 1.71), (0.18, 1.76), (0.14, 1.765)], ox, oy, GOLD, w=2.0)               # brow block, slit
        line((0.185, 1.705), (0.19, 1.705), ox, oy, DARK, 3)

def k1(ox, oy, view, opened):   # the hinge behind the crown: the cap turns 80° up and back
    if view == "front":
        base_front(ox, oy)
        if not opened: cap_front_closed(ox, oy)
        else:
            poly(REAR_F, ox, oy, SHELL, w=2.6)
            open_front(ox, oy)
            poly(mir([(0.0, 1.98), (0.10, 1.975), (0.15, 1.92), (0.155, 1.86), (0.0, 1.865)]), ox, oy, SHELL2, w=2.4)   # the cap up behind
            poly(mir([(0.0, 1.95), (0.08, 1.948), (0.09, 1.93), (0.0, 1.932)]), ox, oy, GOLD2, w=1.4)
        for s_ in (1, -1): actuator((s_ * 0.166, 1.60), (s_ * 0.166, 1.70 if not opened else 1.83), ox, oy)
    else:
        side_common(ox, oy, opened)
        circ(SPLIT_B, 0.017, ox, oy)
        if not opened: pass
        else:
            poly(rot(CAP_S, SPLIT_B, 65), ox, oy, SHELL2, w=2.4)
            poly(rot([(0.15, 1.66), (0.188, 1.71), (0.18, 1.76), (0.14, 1.765)], SPLIT_B, 65), ox, oy, GOLD, w=1.8)
            arc((0.19, 1.71), (-0.10, 2.0), 0.30, ox, oy, 0)
        actuator((-0.15, 1.65), (-0.04, 1.80) if not opened else (-0.10, 1.90), ox, oy)
    return ["Петля за макушкой: вся верхняя часть (лицо, лоб, передняя часть", "макушки) поворачивается на 65° вверх-назад. Затылок неподвижен.",
            "Разъём - от подбородка по скулам за уши к затылку, кольцо", "уплотнения по всему контуру. Лицо и лоб открыты полностью."]

def k2(ox, oy, view, opened):   # lift and tilt: the cap rises 6 cm on two telescopic posts, then tilts 45° back
    if view == "front":
        base_front(ox, oy)
        if not opened: cap_front_closed(ox, oy)
        else:
            poly(REAR_F, ox, oy, SHELL, w=2.6)
            open_front(ox, oy)
            poly(mir([(0.0, 1.97), (0.12, 1.965), (0.16, 1.90), (0.16, 1.865), (0.0, 1.87)]), ox, oy, SHELL2, w=2.4)
            for s_ in (1, -1): line((s_ * 0.15, 1.83), (s_ * 0.15, 1.875), ox, oy, INK, 4)                  # the posts
        for s_ in (1, -1): actuator((s_ * 0.166, 1.60), (s_ * 0.166, 1.70 if not opened else 1.86), ox, oy)
    else:
        side_common(ox, oy, opened)
        if opened:
            lifted = [(x, y + 0.06) for x, y in CAP_S]
            poly(rot(lifted, (-0.125, 1.89), 45), ox, oy, SHELL2, w=2.4)
            line((-0.11, 1.83), (-0.11, 1.89), ox, oy, INK, 4); line((0.08, 1.80), (0.08, 1.86), ox, oy, INK, 4, "3,2")
        actuator((-0.15, 1.65), (-0.06, 1.80) if not opened else (-0.10, 1.88), ox, oy)
    return ["Подъём и наклон: верхняя часть сначала поднимается на 6 см на двух", "телескопических стойках (уплотнение отходит ровно, без трения),",
            "потом наклоняется на 45° назад. Мягче всего к уплотнению,", "открытый шлем выше на 12 см."]

def k3(ox, oy, view, opened):   # the axis at the ears: the cap turns 95° back over the fixed back shell (concentric)
    if view == "front":
        base_front(ox, oy)
        for s_ in (1, -1): circ((s_ * 0.158, 1.70), 0.018, ox, oy)
        if not opened: cap_front_closed(ox, oy)
        else:
            poly(REAR_F, ox, oy, SHELL, w=2.6)
            open_front(ox, oy)
            poly(mir([(0.0, 1.885), (0.10, 1.88), (0.15, 1.84), (0.165, 1.79), (0.0, 1.85)]), ox, oy, SHELL2, w=2.4, op=0.95)
        for s_ in (1, -1): actuator((s_ * 0.166, 1.60), (s_ * 0.166, 1.69 if not opened else 1.76), ox, oy)
    else:
        side_common(ox, oy, opened)
        circ((0.0, 1.70), 0.018, ox, oy)
        if opened:
            poly(rot(CAP_S, (0.0, 1.70), 95), ox, oy, SHELL2, w=2.4, op=0.95)
            arc((0.19, 1.71), (-0.03, 1.89), 0.19, ox, oy, 0)
        actuator((-0.14, 1.62), (-0.02, 1.66) if not opened else (-0.06, 1.74), ox, oy)
    return ["Ось у ушей: верхняя часть поворачивается на 95° назад и ложится", "поверх неподвижного затылка (затылок меньше, скорлупы - соосные).",
            "Самый компактный открытый шлем: выше всего на 4 см.", "Сложнее уплотнение - оно идёт по сфере вращения."]

VARS = [("V1  петля за макушкой", k1), ("V2  подъём и наклон", k2), ("V3  ось у ушей, на затылок", k3)]
out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
txt(30, 44, "«Панцирь» · шлем: верхняя часть (лицо, лоб, макушка) открывается приводами, затылок неподвижен", 24, INK, weight="600")
txt(30, 70, "разъём от подбородка по скулам за уши к затылку · кольцо уплотнения по разъёму · сначала клапан выравнивает давление · 0,31 м", 14, THIN)
for i, (title, fn) in enumerate(VARS):
    y0 = 95 + i * 630
    out.append('<rect x="5" y="%d" width="%d" height="620" fill="none" stroke="%s"/>' % (y0, W - 10, GRID))
    txt(25, y0 + 32, title, 22, INK, weight="600")
    oy = y0 + 440
    notes = None
    for j, (view, opened, lab) in enumerate((("front", False, "спереди · закрыт"), ("side", False, "сбоку · закрыт"), ("front", True, "спереди · открыт"), ("side", True, "сбоку · открыт"))):
        ox = 215 + j * 390
        notes = fn(ox, oy, view, opened)
        txt(ox, oy + 125, lab, 12.5, THIN, "middle")
    for k, s in enumerate(notes): txt(25 + (k // 2) * 760, oy + 150 + (k % 2) * 22, s, 15, NOTE)
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_helmets.svg"), "w", encoding="utf-8").write(svg)
print("ok")
