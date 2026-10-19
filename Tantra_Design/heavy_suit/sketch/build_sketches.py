# «Панцирь» (class 2) - concept sketches from scratch: front and side outlines on a human proportion grid, with notes.
# Engineering base (Orbitersdk/OrbiterCrew/HEAVY_SUIT.md): a hard suit - the 4 g/cm2 shield cannot fold like cloth, so
# the limbs are truncated segments on rotary bearings with oblique cuts (constant volume, human proportions, no folds);
# rear entry through the life-support door (the helmet fixed, close to the head); the heat taken by panels on standoffs
# with a gap; the radiator on the door, looking up and back; the field coils inside the torso (chest ring, collar).
# Output: pantsir_sketches.svg (five variants, each front + side), scale 1 m = 400 px.
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
S = 400.0                       # px per metre
W, H = 5 * 640, 1060
INK, THIN, NOTE, ACC, GRID = "#1d2733", "#52606d", "#2f6f9f", "#c0692a", "#d9dee4"
out = []

def P(x, y, ox, oy): return "%.1f,%.1f" % (ox + x * S, oy - y * S)
def poly(pts, ox, oy, fill="none", stroke=INK, w=2.2, close=True, dash=None, op=1.0):
    d = " ".join(P(x, y, ox, oy) for x, y in pts)
    tag = "polygon" if close else "polyline"
    out.append('<%s points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' %
               (tag, d, fill, op if fill != "none" else 1, stroke, w, ' stroke-dasharray="%s"' % dash if dash else ""))
def line(a, b, ox, oy, stroke=THIN, w=1.4, dash=None):
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f"%s/>' %
               (ox + a[0] * S, oy - a[1] * S, ox + b[0] * S, oy - b[1] * S, stroke, w, ' stroke-dasharray="%s"' % dash if dash else ""))
def ell(c, rx, ry, ox, oy, fill="none", stroke=INK, w=2.2, op=1.0):
    out.append('<ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f"/>' %
               (ox + c[0] * S, oy - c[1] * S, rx * S, ry * S, fill, op, stroke, w))
def text(x, y, s, ox, oy, size=13, color=NOTE, anchor="start", weight="normal"):
    out.append('<text x="%.1f" y="%.1f" font-family="Segoe UI, Arial" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' %
               (ox + x * S, oy - y * S, size, color, anchor, weight, s))
def note(at, to, s, ox, oy, anchor="start"):
    line(at, to, ox, oy, NOTE, 1.0)
    out.append('<circle cx="%.1f" cy="%.1f" r="2.5" fill="%s"/>' % (ox + at[0] * S, oy - at[1] * S, NOTE))
    text(to[0] + (0.01 if anchor == "start" else -0.01), to[1] - 0.008, s, ox, oy, 12, NOTE, anchor)

def mirror(pts): return [(-x, y) for x, y in reversed(pts)]

# ---------------- the common hard suit, front (x across, y up; 0 at the ground) ----------------
# person 1.70 m; the suit adds 3 cm under the boots, the helmet tops at ~1.84 m
def seg(x0, x1, y0, y1, slant=0.0):      # a limb segment: a truncated cone seen from the front, its ends oblique by slant (m)
    return [(x0, y0 - slant), (x1, y0 + slant), (x1 - 0.004, y1 + slant), (x0 + 0.004, y1 - slant)]

def base_front(ox, oy, v):
    fill = {"tile": "#f1efe9", "panel": "#eef0f2", "rad": "#e9ece9", "field": "#f3eedd", "seg": "#eceff1"}[v]
    # boots (hard, a flat sole with a heel block)
    for s in (1, -1):
        poly([(s * 0.04, 0.0), (s * 0.165, 0.0), (s * 0.165, 0.10), (s * 0.05, 0.10)], ox, oy, fill, w=2.2)
        line((s * 0.04, 0.035), (s * 0.165, 0.035), ox, oy, THIN)
    # legs: ankle bearing, shin, knee bearing, thigh, hip bearing (oblique rings)
    for s in (1, -1):
        sx = lambda a: s * a
        poly([(sx(0.055), 0.10), (sx(0.155), 0.10), (sx(0.150), 0.135), (sx(0.060), 0.135)], ox, oy, "#cfd6dc", w=1.8)     # ankle bearing
        poly([(sx(0.058), 0.135), (sx(0.152), 0.135), (sx(0.160), 0.44), (sx(0.050), 0.44)], ox, oy, fill)                 # shin
        poly([(sx(0.048), 0.44), (sx(0.162), 0.44), (sx(0.160), 0.50), (sx(0.050), 0.50)], ox, oy, "#cfd6dc", w=1.8)       # knee bearings
        poly([(sx(0.050), 0.50), (sx(0.162), 0.50), (sx(0.178), 0.80), (sx(0.030), 0.80)], ox, oy, fill)                   # thigh
        line((sx(0.040), 0.645), (sx(0.170), 0.665), ox, oy, THIN, 1.2)                                                    # oblique cut
    # the brief (hard pelvis) with the hip bearings
    poly([(-0.205, 0.80), (0.205, 0.80), (0.215, 0.95), (-0.215, 0.95)], ox, oy, fill)
    for s in (1, -1): ell((s * 0.11, 0.835), 0.07, 0.022, ox, oy, "#cfd6dc", w=1.6)
    # waist bearing
    poly([(-0.20, 0.95), (0.20, 0.95), (0.205, 1.00), (-0.205, 1.00)], ox, oy, "#cfd6dc", w=1.8)
    # the hard upper torso (HUT): one shell, chest planes, shoulder bearings set into its corners
    hut = [(-0.215, 1.00), (0.215, 1.00), (0.255, 1.20), (0.285, 1.36), (0.275, 1.47), (0.15, 1.53), (-0.15, 1.53), (-0.275, 1.47), (-0.285, 1.36), (-0.255, 1.20)]
    poly(hut, ox, oy, fill, w=2.6)
    line((-0.20, 1.27), (0.20, 1.27), ox, oy, THIN, 1.2, "6,4")                                                            # the field ring inside
    # arms: shoulder bearing (canted), upper arm, elbow, forearm, wrist bearing, glove - hanging, a little away
    for s in (1, -1):
        ang = s * math.radians(9)
        def R(x, y, cx=s * 0.27, cy=1.40): return (cx + (x - cx) * math.cos(ang) + (y - cy) * math.sin(ang), cy - (x - cx) * math.sin(ang) + (y - cy) * math.cos(ang))
        base = s * 0.27
        parts = [(0.000, -0.02, 0.085, "#cfd6dc"), (-0.02, -0.30, 0.078, fill), (-0.30, -0.35, 0.075, "#cfd6dc"), (-0.35, -0.60, 0.068, fill), (-0.60, -0.635, 0.058, "#cfd6dc")]
        for y0, y1, r, f in parts:
            poly([R(base - r, 1.40 + y0), R(base + r, 1.40 + y0), R(base + r * 0.95, 1.40 + y1), R(base - r * 0.95, 1.40 + y1)], ox, oy, f, w=1.9)
        poly([R(base - 0.055, 0.765), R(base + 0.055, 0.765), R(base + 0.05, 0.64), R(base, 0.62), R(base - 0.05, 0.64)], ox, oy, "#9aa6b1", w=1.9)   # glove
    # the collar ring and the helmet: fixed to the HUT, close to the head (head 0.16 wide -> shell 0.31)
    poly([(-0.155, 1.53), (0.155, 1.53), (0.15, 1.56), (-0.15, 1.56)], ox, oy, "#cfd6dc", w=1.8)
    helm = [(-0.155, 1.56), (0.155, 1.56), (0.160, 1.66), (0.150, 1.75), (0.115, 1.815), (0.05, 1.84), (-0.05, 1.84), (-0.115, 1.815), (-0.150, 1.75), (-0.160, 1.66)]
    poly(helm, ox, oy, fill, w=2.6)

def base_side(ox, oy, v):
    fill = {"tile": "#f1efe9", "panel": "#eef0f2", "rad": "#e9ece9", "field": "#f3eedd", "seg": "#eceff1"}[v]
    # +x forward
    poly([(-0.08, 0.0), (0.20, 0.0), (0.20, 0.06), (0.10, 0.10), (-0.08, 0.10)], ox, oy, fill)                              # boot
    poly([(-0.06, 0.10), (0.09, 0.10), (0.10, 0.44), (-0.05, 0.44)], ox, oy, fill)                                            # shin
    poly([(-0.06, 0.44), (0.11, 0.44), (0.11, 0.50), (-0.06, 0.50)], ox, oy, "#cfd6dc", w=1.8)
    poly([(-0.06, 0.50), (0.11, 0.50), (0.12, 0.80), (-0.09, 0.80)], ox, oy, fill)                                            # thigh
    poly([(-0.12, 0.80), (0.12, 0.80), (0.13, 0.95), (-0.13, 0.95)], ox, oy, fill)                                            # brief
    poly([(-0.13, 0.95), (0.13, 0.95), (0.13, 1.00), (-0.13, 1.00)], ox, oy, "#cfd6dc", w=1.8)
    hut = [(-0.14, 1.00), (0.14, 1.00), (0.17, 1.22), (0.165, 1.40), (0.10, 1.53), (-0.13, 1.53), (-0.17, 1.40), (-0.16, 1.15)]
    poly(hut, ox, oy, fill, w=2.6)
    # the rear door with the life support (rear entry): its hinge on the left side, latches at the edges
    door = [(-0.16, 1.03), (-0.37, 1.06), (-0.39, 1.50), (-0.30, 1.56), (-0.13, 1.53)]
    poly(door, ox, oy, "#dfe4e8", w=2.4)
    line((-0.16, 1.03), (-0.13, 1.53), ox, oy, ACC, 1.6, "5,3")                                                              # the door seal
    # arm (side): shoulder bearing, upper arm, forearm forward a little
    poly([(-0.06, 1.42), (0.07, 1.42), (0.06, 1.10), (-0.05, 1.10)], ox, oy, fill, w=1.9)
    poly([(-0.05, 1.10), (0.07, 1.10), (0.11, 0.80), (0.00, 0.79)], ox, oy, fill, w=1.9)
    poly([(0.00, 0.79), (0.11, 0.80), (0.12, 0.69), (0.03, 0.68)], ox, oy, "#9aa6b1", w=1.9)
    ell((0.005, 1.40), 0.075, 0.075, ox, oy, "#cfd6dc", w=1.6, op=0.9)
    poly([(-0.12, 1.53), (0.11, 1.53), (0.11, 1.56), (-0.12, 1.56)], ox, oy, "#cfd6dc", w=1.8)
    helm = [(-0.13, 1.56), (0.12, 1.56), (0.165, 1.62), (0.175, 1.70), (0.15, 1.78), (0.07, 1.835), (-0.04, 1.845), (-0.12, 1.81), (-0.165, 1.73), (-0.16, 1.62)]
    poly(helm, ox, oy, fill, w=2.6)

def face_front(ox, oy, kind):
    # the window: small, deep; the outer shield (slides up into the crown) and the pressure visor behind
    poly([(-0.10, 1.665), (0.10, 1.665), (0.105, 1.725), (-0.105, 1.725)], ox, oy, "#2a3644", w=1.8)                       # visor
    if kind == "slit":
        poly([(-0.112, 1.655), (0.112, 1.655), (0.118, 1.735), (-0.118, 1.735)], ox, oy, "#c9a24a", w=1.8, op=0.95)       # shield
        poly([(-0.085, 1.688), (0.085, 1.688), (0.085, 1.700), (-0.085, 1.700)], ox, oy, "#1b232c", w=1.0)                 # its slit
    else:
        poly([(-0.10, 1.665), (0.10, 1.665), (0.105, 1.725), (-0.105, 1.725)], ox, oy, "#b48a2e", w=1.8, op=0.75)          # gold film
    for s in (1, -1): ell((s * 0.15, 1.69), 0.016, 0.016, ox, oy, "#cfd6dc", w=1.4)                                      # visor pivots
    out.append('<circle cx="%.1f" cy="%.1f" r="5" fill="#f7f3e0" stroke="%s" stroke-width="1.2"/>' % (ox, oy - 1.22 * S, INK))   # chest lamp

def face_side(ox, oy, kind):
    poly([(0.13, 1.66), (0.172, 1.665), (0.176, 1.73), (0.14, 1.735)], ox, oy, "#2a3644" if kind != "slit" else "#c9a24a", w=1.8)
    line((0.03, 1.69), (0.15, 1.83), ox, oy, ACC, 1.2, "4,3")                                                                # the shield's rail into the crown

# ---------------- the five variants ----------------
VARS = [
    ("1  «Сегмент»", "seg", "slit",
     ["Все конечности - жёсткие сегменты на", "подшипниках с косым срезом: защита", "4 г/см² не гнётся, сустав не меняет объём.",
      "Поверх - керамические щитки на стойках", "(зазор 15 мм) - тепловой экран."]),
    ("2  «Плитка»", "tile", "slit",
     ["Щиток за щитком, как теплозащита:", "плоские плитки рядами на сегментах,", "каждая на 4 стойках, швы не совпадают.",
      "Ремонт в поле - замена плитки.", "Самый горячий допуск: +480 °C."]),
    ("3  «Экран»", "panel", "film",
     ["Тепловой экран отдельно от брони:", "большие гнутые панели на стойках", "(грудь, спина, бёдра, голени),",
      "между ними видна броня. Панели", "снимаются - в пустоте без жары легче."]),
    ("4  «Радиатор»", "rad", "slit",
     ["Дверь-ранец выше плеч: радиатор", "смотрит в небо, не на горячий грунт.", "Испаритель - щели над плечами.",
      "Вдоль голеней и предплечий - тепловые", "трубы под рёбрами жёсткости."]),
    ("5  «Поле»", "field", "film",
     ["Поле главное: кольцевые катушки", "в груди, в воротнике и на поясе", "(видны как пояса корпуса).",
      "Броня тоньше (2,5 г/см²), конечности", "полужёсткие - легче на 25 кг."]),
]

def variant_extras(k, ox, oy, side):
    if k == 0:   # stand-off ceramic guards over the segments (front)
        if not side:
            for s in (1, -1):
                poly([(s * 0.062, 0.18), (s * 0.150, 0.18), (s * 0.154, 0.42), (s * 0.058, 0.42)], ox, oy, "#ffffff", w=1.4)
                poly([(s * 0.055, 0.55), (s * 0.165, 0.56), (s * 0.172, 0.77), (s * 0.045, 0.77)], ox, oy, "#ffffff", w=1.4)
            poly([(-0.20, 1.06), (0.20, 1.06), (0.235, 1.24), (-0.235, 1.24)], ox, oy, "#ffffff", w=1.4)
        else:
            poly([(0.095, 0.18), (0.112, 0.18), (0.118, 0.42), (0.10, 0.42)], ox, oy, "#ffffff", w=1.4)
    if k == 1:   # tiles in rows
        if not side:
            for s in (1, -1):
                for row in range(5):
                    y = 0.15 + row * 0.055
                    for c in range(2):
                        x0 = s * (0.060 + c * 0.047); x1 = s * (0.060 + (c + 1) * 0.047 - 0.006)
                        poly([(x0, y), (x1, y), (x1, y + 0.048), (x0, y + 0.048)], ox, oy, "#ffffff", w=1.0)
                for row in range(5):
                    y = 0.53 + row * 0.054
                    for c in range(2):
                        x0 = s * (0.052 + c * 0.058); x1 = s * (0.052 + (c + 1) * 0.058 - 0.006)
                        poly([(x0, y), (x1, y), (x1, y + 0.047), (x0, y + 0.047)], ox, oy, "#ffffff", w=1.0)
            for row in range(3):
                y = 1.04 + row * 0.07
                for c in range(-3, 3):
                    poly([(c * 0.068 + 0.003, y), (c * 0.068 + 0.065, y), (c * 0.068 + 0.065, y + 0.064), (c * 0.068 + 0.003, y + 0.064)], ox, oy, "#ffffff", w=1.0)
    if k == 2:   # large shield panels on standoffs
        if not side:
            for s in (1, -1):
                poly([(s * 0.05, 0.17), (s * 0.165, 0.17), (s * 0.172, 0.43), (s * 0.045, 0.43)], ox, oy, "#e7c66f", w=1.8, op=0.95)
                poly([(s * 0.040, 0.53), (s * 0.18, 0.53), (s * 0.195, 0.79), (s * 0.025, 0.79)], ox, oy, "#e7c66f", w=1.8, op=0.95)
            poly([(-0.235, 1.02), (0.235, 1.02), (0.27, 1.22), (0.25, 1.38), (-0.25, 1.38), (-0.27, 1.22)], ox, oy, "#e7c66f", w=1.8, op=0.95)
            for x, y in ((-0.2, 1.05), (0.2, 1.05), (-0.23, 1.34), (0.23, 1.34)):
                out.append('<circle cx="%.1f" cy="%.1f" r="3" fill="%s"/>' % (ox + x * S, oy - y * S, INK))
        else:
            poly([(0.13, 1.03), (0.20, 1.06), (0.20, 1.37), (0.165, 1.40)], ox, oy, "#e7c66f", w=1.8, op=0.95)
            poly([(0.10, 0.17), (0.125, 0.17), (0.13, 0.43), (0.105, 0.43)], ox, oy, "#e7c66f", w=1.8, op=0.95)
    if k == 3:   # the tall radiator door
        if side:
            poly([(-0.37, 1.06), (-0.40, 1.06), (-0.43, 1.66), (-0.33, 1.70), (-0.30, 1.56)], ox, oy, "#c8cfd6", w=2.2)
            for i in range(9):
                y = 1.12 + i * 0.06
                line((-0.40 - 0.004 * i, y), (-0.44 - 0.004 * i, y + 0.03), ox, oy, INK, 1.4)
        else:
            for s in (1, -1):
                poly([(s * 0.18, 1.47), (s * 0.30, 1.50), (s * 0.28, 1.66), (s * 0.17, 1.62)], ox, oy, "#c8cfd6", w=2.0)
                for i in range(4): line((s * (0.19 + i * 0.026), 1.50), (s * (0.19 + i * 0.026), 1.64), ox, oy, THIN, 1.0)
            for s in (1, -1):
                for i in range(3): line((s * 0.075, 0.17 + i * 0.09), (s * 0.075, 0.24 + i * 0.09), ox, oy, INK, 1.6)
    if k == 4:   # field rings as belts of the shell
        for y, hw in ((1.27, 0.27), (1.545, 0.16), (0.975, 0.21)):
            if not side:
                poly([(-hw - 0.01, y - 0.018), (hw + 0.01, y - 0.018), (hw + 0.01, y + 0.018), (-hw - 0.01, y + 0.018)], ox, oy, "#c98a3a", w=1.6, op=0.85)
            else:
                poly([(-0.18, y - 0.018), (0.18, y - 0.018), (0.18, y + 0.018), (-0.18, y + 0.018)], ox, oy, "#c98a3a", w=1.6, op=0.85)

out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
for i, (title, mat, face, notes) in enumerate(VARS):
    x0 = i * 640
    out.append('<rect x="%d" y="0" width="640" height="%d" fill="none" stroke="%s"/>' % (x0, H, GRID))
    oyF = 900
    # proportion grid: 0.1 m lines and the person's height
    for k in range(0, 20):
        y = oyF - k * 0.1 * S
        out.append('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="%s" stroke-width="0.6"/>' % (x0 + 10, y, x0 + 630, y, GRID))
    text(0, 0, "", x0, oyF)
    out.append('<text x="%d" y="40" font-family="Segoe UI, Arial" font-size="24" fill="%s" font-weight="600">%s</text>' % (x0 + 20, INK, title))
    oxF, oxS = x0 + 175, x0 + 480
    base_front(oxF, oyF, mat); variant_extras(i, oxF, oyF, False); face_front(oxF, oyF, face)
    base_side(oxS, oyF, mat); variant_extras(i, oxS, oyF, True); face_side(oxS, oyF, face)
    out.append('<text x="%d" y="%d" font-family="Segoe UI, Arial" font-size="12" fill="%s">спереди</text>' % (oxF - 22, oyF + 22, THIN))
    out.append('<text x="%d" y="%d" font-family="Segoe UI, Arial" font-size="12" fill="%s">сбоку</text>' % (oxS - 18, oyF + 22, THIN))
    for j, s in enumerate(notes):
        out.append('<text x="%d" y="%d" font-family="Segoe UI, Arial" font-size="13.5" fill="%s">%s</text>' % (x0 + 20, oyF + 60 + j * 18, NOTE, s))
    # height marks
    out.append('<text x="%d" y="%.1f" font-family="Segoe UI, Arial" font-size="11" fill="%s">1,84 м</text>' % (x0 + 590, oyF - 1.84 * S + 4, THIN))
    out.append('<text x="%d" y="%.1f" font-family="Segoe UI, Arial" font-size="11" fill="%s">1,0</text>' % (x0 + 602, oyF - 1.0 * S + 4, THIN))

svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_sketches.svg"), "w", encoding="utf-8").write(svg)
print("written", os.path.join(HERE, "pantsir_sketches.svg"))
