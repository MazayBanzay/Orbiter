# «Панцирь» - the chosen scheme «Плитка + Радиатор» (tiles + radiator), worked out: front, side, back and the helmet
# closed / open. Heavy: 130 kg suit on servo drives - thick legs (thigh ~0.23 m, shin ~0.19 m), rotary drives at the hips,
# knees and ankles on the outside, a power cylinder behind each shin, an elbow drive. Tiles (ceramic, each on standoffs,
# rows overlapped like shingles) only where the heat and the ground's glow hit: chest, belly, the front of the thighs and
# shins, the forearms; the rest smooth segments. The rear door is the life support and the radiator (fins to the sky),
# the evaporator's slots over the shoulders. Scale 1 m = 360 px. Output: pantsir_tile_rad.svg
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
S = 360.0
W, H = 2760, 1100
INK, THIN, NOTE, ACC, GRID = "#1d2733", "#5d6b78", "#2f6f9f", "#c0692a", "#dde2e7"
SHELL, BEAR, TILE, DRIVE, DOOR, GLOVE, GOLD, DARK = "#eef0f2", "#c9d1d8", "#fbfaf6", "#8c98a4", "#d7dde2", "#8f9aa5", "#c9a24a", "#25303b"
out = []
def X(ox, x): return ox + x * S
def Y(oy, y): return oy - y * S
def poly(pts, ox, oy, fill="none", stroke=INK, w=2.0, op=1.0, close=True, dash=None):
    d = " ".join("%.1f,%.1f" % (X(ox, x), Y(oy, y)) for x, y in pts)
    out.append('<%s points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' %
               ("polygon" if close else "polyline", d, fill, op, stroke, w, ' stroke-dasharray="%s"' % dash if dash else ""))
def line(a, b, ox, oy, stroke=THIN, w=1.2, dash=None):
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f"%s/>' %
               (X(ox, a[0]), Y(oy, a[1]), X(ox, b[0]), Y(oy, b[1]), stroke, w, ' stroke-dasharray="%s"' % dash if dash else ""))
def circ(c, r, ox, oy, fill=DRIVE, stroke=INK, w=1.8):
    out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" stroke="%s" stroke-width="%.1f"/>' % (X(ox, c[0]), Y(oy, c[1]), r * S, fill, stroke, w))
def rect(x0, y0, x1, y1, ox, oy, fill, w=1.2, op=1.0): poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], ox, oy, fill, w=w, op=op)
def txt(x, y, s, size=13, color=NOTE, anchor="start", weight="normal"):
    out.append('<text x="%.1f" y="%.1f" font-family="Segoe UI, Arial" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, size, color, anchor, weight, s))
def note(at, to, s, ox, oy):
    ax, ay, tx, ty = X(ox, at[0]), Y(oy, at[1]), X(ox, to[0]), Y(oy, to[1])
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="1"/>' % (ax, ay, tx, ty, NOTE))
    out.append('<circle cx="%.1f" cy="%.1f" r="2.6" fill="%s"/>' % (ax, ay, NOTE))
    txt(tx + (4 if tx >= ax else -4), ty - 4, s, 12, NOTE, "start" if tx >= ax else "end")
def tiles(x0, x1, y0, y1, cols, rows, ox, oy, offset=True):
    """rows of tiles, every other row shifted half a tile (no straight gap through the layer)"""
    tw, th = (x1 - x0) / cols, (y1 - y0) / rows
    for r in range(rows):
        sh = (tw / 2 if (offset and r % 2) else 0)
        for c in range(-1, cols + 1):
            a, b = x0 + c * tw + sh + 0.004, x0 + (c + 1) * tw + sh - 0.004
            a, b = max(a, x0), min(b, x1)
            if b - a < 0.012: continue
            rect(a, y0 + r * th + 0.004, b, y0 + (r + 1) * th - 0.002, ox, oy, TILE, 1.0)

# ---------------- front ----------------
def legs_front(ox, oy, back=False):
    for s in (1, -1):
        c = s * 0.125
        poly([(c - 0.085, 0.0), (c + 0.085, 0.0), (c + 0.085, 0.035), (c - 0.085, 0.035)], ox, oy, DARK, w=1.6)          # sole plate
        poly([(c - 0.08, 0.035), (c + 0.08, 0.035), (c + 0.075, 0.12), (c - 0.075, 0.12)], ox, oy, SHELL)                 # boot
        poly([(c - 0.078, 0.12), (c + 0.078, 0.12), (c + 0.080, 0.155), (c - 0.080, 0.155)], ox, oy, BEAR, w=1.6)         # ankle bearing
        poly([(c - 0.090, 0.155), (c + 0.090, 0.155), (c + 0.098, 0.45), (c - 0.098, 0.45)], ox, oy, SHELL)               # shin
        poly([(c - 0.100, 0.45), (c + 0.100, 0.45), (c + 0.102, 0.52), (c - 0.102, 0.52)], ox, oy, BEAR, w=1.6)           # knee bearings
        poly([(c - 0.104, 0.52), (c + 0.104, 0.52), (c + 0.118, 0.83), (c - 0.112, 0.83)], ox, oy, SHELL)                 # thigh
        line((c - 0.106, 0.665), (c + 0.112, 0.69), ox, oy, THIN)                                                        # oblique cut
        if not back:
            tiles(c - 0.080, c + 0.080, 0.18, 0.43, 2, 4, ox, oy)                                                         # shin tiles
            tiles(c - 0.088, c + 0.090, 0.545, 0.81, 2, 4, ox, oy)                                                        # thigh tiles
        # the drives on the outside: ankle, knee, hip
        circ((s * 0.215, 0.138), 0.026, ox, oy); circ((s * 0.232, 0.485), 0.050, ox, oy); circ((s * 0.232, 0.485), 0.018, ox, oy, BEAR)
        circ((s * 0.262, 0.875), 0.062, ox, oy); circ((s * 0.262, 0.875), 0.022, ox, oy, BEAR)
        line((s * 0.232, 0.435), (s * 0.215, 0.164), ox, oy, INK, 3.0)                                                   # the knee-ankle link
        line((s * 0.240, 0.535), (s * 0.255, 0.813), ox, oy, INK, 3.0)                                                   # the hip-knee link
    poly([(-0.245, 0.83), (0.245, 0.83), (0.255, 0.97), (-0.255, 0.97)], ox, oy, SHELL)                                   # the brief
    poly([(-0.235, 0.97), (0.235, 0.97), (0.24, 1.02), (-0.24, 1.02)], ox, oy, BEAR, w=1.6)                               # waist bearing

def torso_front(ox, oy, back=False):
    hut = [(-0.24, 1.02), (0.24, 1.02), (0.275, 1.20), (0.305, 1.36), (0.295, 1.47), (0.165, 1.545), (-0.165, 1.545), (-0.295, 1.47), (-0.305, 1.36), (-0.275, 1.20)]
    poly(hut, ox, oy, SHELL, w=2.6)
    if not back:
        tiles(-0.215, 0.215, 1.05, 1.36, 5, 4, ox, oy)                                                                    # chest and belly tiles
        rect(-0.09, 1.385, 0.09, 1.45, ox, oy, DARK, 1.4)                                                                 # the status panel
        for i, col in enumerate(("#6fd38a", "#f0c35a", "#e86a5a")): circ((-0.06 + i * 0.03, 1.418), 0.009, ox, oy, col, INK, 0.8)
        circ((0.06, 1.418), 0.016, ox, oy, "#f7f3e0", INK, 1.0)                                                          # lamp
        line((-0.27, 1.37), (0.27, 1.37), ox, oy, ACC, 1.2, "6,4")                                                        # the field ring inside
    # arms: shoulder bearing (canted), upper arm, elbow drive, forearm (tiles on its front), wrist bearing, glove
    for s in (1, -1):
        a = s * math.radians(8)
        def R(x, y): cx, cy = s * 0.30, 1.40; return (cx + (x - cx) * math.cos(a) + (y - cy) * math.sin(a), cy - (x - cx) * math.sin(a) + (y - cy) * math.cos(a))
        b = s * 0.30
        def seg(y0, y1, r0, r1, f, w=1.9): poly([R(b - r0, y0), R(b + r0, y0), R(b + r1, y1), R(b - r1, y1)], ox, oy, f, w=w)
        seg(1.43, 1.39, 0.092, 0.092, BEAR, 1.6)
        seg(1.39, 1.12, 0.088, 0.082, SHELL)
        seg(1.12, 1.07, 0.082, 0.080, BEAR, 1.6)
        seg(1.07, 0.82, 0.080, 0.072, SHELL)
        if not back:
            for k in range(3):
                y0, y1 = 1.05 - k * 0.075, 0.985 - k * 0.075
                poly([R(b - 0.055, y0), R(b + 0.055, y0), R(b + 0.052, y1), R(b - 0.052, y1)], ox, oy, TILE, w=1.0)
        seg(0.82, 0.79, 0.070, 0.068, BEAR, 1.6)
        poly([R(b - 0.064, 0.79), R(b + 0.064, 0.79), R(b + 0.058, 0.66), R(b, 0.64), R(b - 0.058, 0.66)], ox, oy, GLOVE, w=1.9)
        circ(R(b + s * 0.095, 1.095), 0.034, ox, oy)                                                                      # elbow drive

def helmet_front(ox, oy, state="closed", back=False):
    poly([(-0.17, 1.545), (0.17, 1.545), (0.165, 1.575), (-0.165, 1.575)], ox, oy, BEAR, w=1.6)                           # collar (field coil)
    helm = [(-0.16, 1.575), (0.16, 1.575), (0.163, 1.67), (0.152, 1.76), (0.115, 1.82), (0.05, 1.845), (-0.05, 1.845), (-0.115, 1.82), (-0.152, 1.76), (-0.163, 1.67)]
    poly(helm, ox, oy, SHELL, w=2.6)
    if back: return
    for s in (1, -1): circ((s * 0.152, 1.70), 0.017, ox, oy, BEAR, INK, 1.3)                                            # visor pivots
    rect(0.150, 1.60, 0.175, 1.635, ox, oy, ACC, 1.0)                                                                    # equalising valve
    if state == "closed":
        poly([(-0.115, 1.655), (0.115, 1.655), (0.12, 1.74), (-0.12, 1.74)], ox, oy, GOLD, w=1.8)                        # the shield
        rect(-0.085, 1.692, 0.085, 1.704, ox, oy, DARK, 0.8)                                                             # its slit
        for k in range(3): rect(-0.06, 1.615 + k * 0.012, 0.06, 1.621 + k * 0.012, ox, oy, DARK, 0.6)                  # jaw grille
    else:
        poly([(-0.10, 1.65), (0.10, 1.65), (0.105, 1.74), (-0.105, 1.74)], ox, oy, "#e9d9c7", w=1.4)                     # face opening (seal)
        poly([(-0.103, 1.65), (0.103, 1.65), (0.108, 1.742), (-0.108, 1.742)], ox, oy, "none", ACC, 2.2)
        out.append('<ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="#e8c6a8" stroke="%s" stroke-width="1"/>' % (X(ox, 0), Y(oy, 1.695), 0.072 * S, 0.042 * S, INK))
        poly([(-0.115, 1.755), (0.115, 1.755), (0.11, 1.80), (-0.11, 1.80)], ox, oy, GOLD, w=1.6, op=0.9)               # shield parked in the crown
        poly([(-0.10, 1.745), (0.10, 1.745), (0.095, 1.765), (-0.095, 1.765)], ox, oy, "#9fb4c4", w=1.2, op=0.9)         # visor raised

# ---------------- side (+x forward) ----------------
def side(ox, oy):
    poly([(-0.10, 0.0), (0.24, 0.0), (0.24, 0.035), (-0.10, 0.035)], ox, oy, DARK, w=1.6)
    poly([(-0.09, 0.035), (0.23, 0.035), (0.20, 0.08), (0.10, 0.12), (-0.09, 0.12)], ox, oy, SHELL)
    poly([(-0.08, 0.12), (0.10, 0.12), (0.10, 0.155), (-0.08, 0.155)], ox, oy, BEAR, w=1.6)
    poly([(-0.085, 0.155), (0.105, 0.155), (0.115, 0.45), (-0.085, 0.45)], ox, oy, SHELL)
    for k in range(4): rect(0.105, 0.18 + k * 0.064, 0.122, 0.236 + k * 0.064, ox, oy, TILE, 1.0)                        # shin tiles (edge)
    poly([(-0.09, 0.45), (0.12, 0.45), (0.12, 0.52), (-0.09, 0.52)], ox, oy, BEAR, w=1.6)
    poly([(-0.095, 0.52), (0.125, 0.52), (0.135, 0.83), (-0.11, 0.83)], ox, oy, SHELL)
    for k in range(4): rect(0.127, 0.545 + k * 0.066, 0.146, 0.602 + k * 0.066, ox, oy, TILE, 1.0)
    poly([(-0.145, 0.83), (0.14, 0.83), (0.15, 0.97), (-0.15, 0.97)], ox, oy, SHELL)
    poly([(-0.15, 0.97), (0.15, 0.97), (0.15, 1.02), (-0.15, 1.02)], ox, oy, BEAR, w=1.6)
    # the drives seen from the side: discs at the joints, the power cylinder behind the shin
    circ((0.0, 0.138), 0.03, ox, oy); circ((0.015, 0.485), 0.058, ox, oy); circ((0.015, 0.485), 0.02, ox, oy, BEAR); circ((0.0, 0.875), 0.068, ox, oy); circ((0.0, 0.875), 0.024, ox, oy, BEAR)
    poly([(-0.115, 0.20), (-0.085, 0.20), (-0.083, 0.43), (-0.113, 0.43)], ox, oy, DRIVE, w=1.6)                          # the shin's power cylinder
    line((-0.10, 0.43), (-0.06, 0.50), ox, oy, INK, 2.4); line((-0.10, 0.20), (-0.05, 0.15), ox, oy, INK, 2.4)
    hut = [(-0.16, 1.02), (0.16, 1.02), (0.19, 1.22), (0.185, 1.40), (0.12, 1.545), (-0.14, 1.545), (-0.18, 1.40), (-0.175, 1.15)]
    poly(hut, ox, oy, SHELL, w=2.6)
    for k in range(4): rect(0.17 + k * 0.002, 1.06 + k * 0.075, 0.195 + k * 0.002, 1.125 + k * 0.075, ox, oy, TILE, 1.0)
    # the rear door: life support + radiator, its hinge on her right, the evaporator slots on top
    door = [(-0.175, 1.04), (-0.40, 1.07), (-0.42, 1.60), (-0.33, 1.65), (-0.14, 1.545)]
    poly(door, ox, oy, DOOR, w=2.4)
    for k in range(10):
        y = 1.10 + k * 0.05
        line((-0.402 - k * 0.0016, y), (-0.445 - k * 0.0016, y + 0.02), ox, oy, INK, 1.6)                                 # radiator fins (edge on)
    line((-0.175, 1.04), (-0.14, 1.545), ox, oy, ACC, 1.6, "5,3")                                                       # the seal
    for k in range(3): line((-0.33 + k * 0.045, 1.635 - k * 0.015), (-0.31 + k * 0.045, 1.66 - k * 0.015), ox, oy, INK, 2.0)   # evaporator slots
    # arm
    poly([(-0.07, 1.42), (0.08, 1.42), (0.07, 1.12), (-0.06, 1.12)], ox, oy, SHELL, w=1.9)
    poly([(-0.06, 1.12), (0.08, 1.12), (0.12, 0.82), (-0.01, 0.81)], ox, oy, SHELL, w=1.9)
    for k in range(3): rect(0.075 + k * 0.013, 1.03 - k * 0.075, 0.093 + k * 0.013, 0.97 - k * 0.075, ox, oy, TILE, 1.0)
    poly([(-0.01, 0.81), (0.12, 0.82), (0.13, 0.68), (0.02, 0.67)], ox, oy, GLOVE, w=1.9)
    circ((0.0, 1.40), 0.085, ox, oy, BEAR, INK, 1.6); circ((0.02, 1.11), 0.04, ox, oy)
    poly([(-0.14, 1.545), (0.12, 1.545), (0.12, 1.575), (-0.14, 1.575)], ox, oy, BEAR, w=1.6)
    helm = [(-0.14, 1.575), (0.12, 1.575), (0.165, 1.63), (0.175, 1.71), (0.15, 1.79), (0.07, 1.84), (-0.04, 1.85), (-0.12, 1.815), (-0.165, 1.735), (-0.165, 1.63)]
    poly(helm, ox, oy, SHELL, w=2.6)
    poly([(0.13, 1.655), (0.178, 1.66), (0.18, 1.74), (0.14, 1.745)], ox, oy, GOLD, w=1.8)
    line((0.05, 1.70), (0.13, 1.83), ox, oy, ACC, 1.3, "4,3")                                                           # shield rail
    circ((0.02, 1.70), 0.017, ox, oy, BEAR, INK, 1.2)

# ---------------- back ----------------
def back(ox, oy):
    legs_front(ox, oy, back=True)
    torso_front(ox, oy, back=True)
    helmet_front(ox, oy, back=True)
    door = [(-0.235, 1.05), (0.235, 1.05), (0.25, 1.58), (0.18, 1.66), (-0.18, 1.66), (-0.25, 1.58)]
    poly(door, ox, oy, DOOR, w=2.6)
    rect(-0.20, 1.10, 0.20, 1.55, ox, oy, "#bfc8d0", 1.6)                                                               # the radiator field
    for k in range(17): line((-0.19 + k * 0.0238, 1.11), (-0.19 + k * 0.0238, 1.54), ox, oy, INK, 1.4)                 # fins (vertical, to the sky)
    for k in range(5): rect(-0.12 + k * 0.05, 1.605, -0.09 + k * 0.05, 1.635, ox, oy, DARK, 0.8)                       # evaporator slots
    for x in (-0.245, 0.245):
        for y in (1.12, 1.35, 1.55): rect(x - 0.012, y - 0.02, x + 0.012, y + 0.02, ox, oy, ACC, 1.0)                  # latches
    rect(-0.06, 1.075, 0.06, 1.095, ox, oy, DARK, 1.0)                                                                  # the door's grip
    rect(0.10, 1.062, 0.20, 1.09, ox, oy, "#3b4752", 1.0)                                                               # battery cassette slot

# ---------------- the sheet ----------------
out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
oy = 930
for k in range(0, 20):
    out.append('<line x1="20" y1="%.1f" x2="%d" y2="%.1f" stroke="%s" stroke-width="0.6"/>' % (oy - k * 0.1 * S, W - 20, oy - k * 0.1 * S, GRID))
txt(30, 44, "«Панцирь» · схема «Плитка + Радиатор»", 26, INK, weight="600")
txt(30, 70, "жёсткие сегменты на подшипниках · вход сзади через дверь-радиатор · плитка на стойках там, где жара · сервоприводы снаружи · 130 кг", 14, THIN)
oF, oS, oB = 560, 1080, 1480
legs_front(oF, oy); torso_front(oF, oy); helmet_front(oF, oy)
side(oS, oy)
back(oB, oy)
for x, s in ((oF, "спереди"), (oS, "сбоку"), (oB, "сзади")): txt(x, oy + 26, s, 13, THIN, "middle")
txt(W - 70, oy - 1.85 * S + 4, "1,85 м", 11, THIN); txt(W - 70, oy - 1.0 * S + 4, "1,0 м", 11, THIN)
# notes (front)
note((0.0, 1.70), (-0.30, 1.95), "щиток со щелью: уезжает вверх по направляющей", oF, oy)
note((0.15, 1.70), (0.30, 1.86), "шарниры гермовизора, клапан", oF, oy)
note((-0.10, 1.20), (-0.55, 1.62), "плитка на стойках, ряды внахлёст", oF, oy)
note((-0.27, 1.37), (-0.62, 1.50), "кольцо поля в груди", oF, oy)
note((-0.262, 0.875), (-0.62, 0.97), "привод бедра", oF, oy)
note((-0.232, 0.485), (-0.62, 0.58), "привод колена + тяга", oF, oy)
note((-0.125, 0.02), (-0.62, 0.08), "опорная плита ботинка", oF, oy)
note((-0.10, 0.32), (-0.50, 0.30), "силовой цилиндр голени", oS, oy)
note((-0.43, 1.35), (-0.62, 1.28), "дверь-ранец = радиатор", oS, oy)
note((-0.31, 1.65), (-0.10, 2.0), "щели испарителя (пар вверх)", oS, oy)
note((0.0, 1.30), (0.40, 1.20), "рёбра радиатора: в небо", oB, oy)
note((0.245, 1.35), (0.45, 1.45), "замки двери", oB, oy)
note((0.15, 1.075), (0.45, 0.98), "кассета батареи", oB, oy)
# the helmet close: closed and open, x2
og = 1600; oyh = 930 - 0.0 * S
for ox, st, label in ((2000, "closed", "шлем закрыт"), (2420, "open", "открыт: щиток в макушке, визор поднят")):
    out.append('<g transform="translate(%.1f,%.1f) scale(1.8) translate(%.1f,%.1f)">' % (ox, 470, -ox, -(oy - 1.70 * S)))
    helmet_front(ox, oy, st)
    poly([(-0.25, 1.49), (-0.165, 1.545), (0.165, 1.545), (0.25, 1.49), (0.25, 1.45), (-0.25, 1.45)], ox, oy, SHELL, w=2.0)
    out.append('</g>')
    txt(ox, 700, label, 13, THIN, "middle")
txt(1830, 760, "Окно 0,20 × 0,09 м: мало стекла - мало дозы и тепла. Сначала клапан выравнивает давление,", 12.5, NOTE)
txt(1830, 778, "щиток уезжает по направляющим в карман макушки, гермовизор откидывается на шарнирах у висков.", 12.5, NOTE)
txt(1830, 796, "Корпус шлема неподвижен - он часть жёсткого торса (вход сзади, через дверь).", 12.5, NOTE)
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_tile_rad.svg"), "w", encoding="utf-8").write(svg)
print("ok")
