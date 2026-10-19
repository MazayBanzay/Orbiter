# «Панцирь» - its own suit computer, three concepts (user, 2026-10-18): a wide photonic window (30th century, not a
# narrow slit); the same operating system and tone as the Karkas computer (amber / white / cyan on dark, its fonts and
# alert line), but closer to analogue instruments - built for supercritical conditions: needles, scales, a mnemonic.
# Each panel: the view from inside the V1 helmet, Io at dawn by a lava front. Rendered by headless Edge to PNG.
#   A «Дуги»          - the Karkas arcs become scales with ticks and needles along the window's top edge; mnemonic below
#   B «Приборы»       - a row of round dial gauges low in the window (a Soviet panel drawn by the photonics)
#   C «Столбы»        - vertical scales at the window's sides (heat left, radiation right), red zones and pointers;
#                       a drum counter for the time left and the suit's mnemonic low in the centre
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1600, 1560
out = []
INK, NOTE, GRID = "#1d2733", "#2f6f9f", "#e2e6ea"
AMB, RED, GRN, CYN, WHT, DIM = "#ffb347", "#ff5a4a", "#7fdc8a", "#7fd6ff", "#e8ecef", "#8c98a3"
MONO = "Consolas, monospace"

def r(x, y, w, h, fill, stroke="none", sw=1, rx=0, op=1):
    out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="%g" fill="%s" fill-opacity="%g" stroke="%s" stroke-width="%g"/>' % (x, y, w, h, rx, fill, op, stroke, sw))
def t(x, y, s, size=13, color=WHT, anchor="start", weight="normal", fam=MONO):
    out.append('<text x="%g" y="%g" font-family="%s" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, fam, size, color, anchor, weight, s))
def note(x, y, s): t(x, y, s, 15, NOTE, fam="Segoe UI, Arial")
def line(x1, y1, x2, y2, c, w=1.5):
    out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="%g"/>' % (x1, y1, x2, y2, c, w))
def poly(pts, fill, stroke="none", sw=1, op=1):
    out.append('<polygon points="%s" fill="%s" fill-opacity="%g" stroke="%s" stroke-width="%g"/>' % (" ".join("%g,%g" % p for p in pts), fill, op, stroke, sw))
def arc(cx, cy, rad, a0, a1, c, w):
    x0, y0 = cx + rad * math.cos(a0), cy + rad * math.sin(a0); x1, y1 = cx + rad * math.cos(a1), cy + rad * math.sin(a1)
    out.append('<path d="M %g %g A %g %g 0 %d 1 %g %g" fill="none" stroke="%s" stroke-width="%g"/>' % (x0, y0, rad, rad, 1 if a1 - a0 > math.pi else 0, x1, y1, c, w))

def view(x, y, w, h):
    """the window: Io at dawn (dark sky, a low sun glow), the plain, a glowing lava front, cracks; the helmet edge"""
    r(x, y, w, h, "#0c0e12")
    poly([(x + 40, y + 20), (x + w - 40, y + 20), (x + w - 10, y + h * 0.5), (x + w - 40, y + h - 20), (x + 40, y + h - 20), (x + 10, y + h * 0.5)], "#07080c")
    gx, gy, gw, gh = x + 40, y + 20, w - 80, h - 40
    poly([(gx, gy + gh * 0.52), (gx + gw, gy + gh * 0.48), (gx + gw, gy + gh), (gx, gy + gh)], "#1b1613")
    for k in range(10):
        cx = gx + gw * (0.06 + 0.095 * k); cy = gy + gh * (0.6 + 0.04 * (k % 3))
        out.append('<ellipse cx="%g" cy="%g" rx="%g" ry="%g" fill="%s" fill-opacity="0.85"/>' % (cx, cy, gw * 0.045, gh * 0.05, ["#ff7a1a", "#ffb030", "#ff5010"][k % 3]))
    for k in range(16):
        x0 = gx + gw * (0.04 + 0.06 * k); line(x0, gy + gh * 0.76, x0 + 18, gy + gh * 0.9, "#ff6a1a", 1.2)
    poly([(x + 40, y + 20), (x + w - 40, y + 20), (x + w - 10, y + h * 0.5), (x + w - 40, y + h - 20), (x + 40, y + h - 20), (x + 10, y + h * 0.5)], "none", "#3a3f46", 3)
    return gx, gy, gw, gh

def alerts(x, y):   # the common OS alert line (as on the Karkas computer)
    t(x, y, "22:41:06  |  СРЕДА ВНЕ ДОПУСКА +612 °C  ·  подтвердите щелчком", 12, RED, "middle")

def silhouette(cx, cy, s, cols):
    P = {"helm": [(-0.12, -1.0), (0.12, -1.0), (0.14, -0.78), (-0.14, -0.78)], "torso": [(-0.26, -0.74), (0.26, -0.74), (0.22, -0.2), (-0.22, -0.2)],
         "armL": [(-0.42, -0.72), (-0.29, -0.72), (-0.31, -0.22), (-0.44, -0.24)], "armR": [(0.29, -0.72), (0.42, -0.72), (0.44, -0.24), (0.31, -0.22)],
         "thL": [(-0.22, -0.16), (-0.03, -0.16), (-0.05, 0.28), (-0.2, 0.28)], "thR": [(0.03, -0.16), (0.22, -0.16), (0.2, 0.28), (0.05, 0.28)],
         "shL": [(-0.2, 0.32), (-0.05, 0.32), (-0.07, 0.78), (-0.18, 0.78)], "shR": [(0.05, 0.32), (0.2, 0.32), (0.18, 0.78), (0.07, 0.78)],
         "btL": [(-0.24, 0.82), (-0.04, 0.82), (-0.04, 0.95), (-0.26, 0.95)], "btR": [(0.04, 0.82), (0.24, 0.82), (0.26, 0.95), (0.04, 0.95)]}
    for k, pts in P.items():
        poly([(cx + px * s, cy + py * s) for px, py in pts], "none", cols.get(k, GRN), 1.6)
COLS = {"btL": RED, "btR": AMB, "shL": AMB}

def dial(cx, cy, rad, frac, label, value, red0=0.8, c=WHT):
    """a round gauge, 240 deg sweep: ticks, a red zone, a needle"""
    a0, a1 = math.radians(150), math.radians(390)
    arc(cx, cy, rad, a0, a1, DIM, 1.5)
    arc(cx, cy, rad - 2, a0 + (a1 - a0) * red0, a1, RED, 4)
    for k in range(11):
        a = a0 + (a1 - a0) * k / 10; l = 7 if k % 5 == 0 else 4
        line(cx + (rad - l) * math.cos(a), cy + (rad - l) * math.sin(a), cx + rad * math.cos(a), cy + rad * math.sin(a), DIM, 1.2)
    a = a0 + (a1 - a0) * frac
    line(cx, cy, cx + (rad - 6) * math.cos(a), cy + (rad - 6) * math.sin(a), AMB if frac < red0 else RED, 2.5)
    out.append('<circle cx="%g" cy="%g" r="3" fill="%s"/>' % (cx, cy, WHT))
    t(cx, cy + rad * 0.55, value, 13, c, "middle", "bold"); t(cx, cy + rad + 14, label, 11, CYN, "middle")

def vscale(x, y, h, frac, red0, label, value, c):
    line(x, y, x, y + h, DIM, 1.5)
    line(x, y, x, y + h * (1 - red0), RED, 4)
    for k in range(11):
        yy = y + h * k / 10; line(x - (8 if k % 5 == 0 else 4), yy, x, yy, DIM, 1.2)
    py = y + h * (1 - frac)
    poly([(x + 2, py), (x + 14, py - 6), (x + 14, py + 6)], c)
    t(x + 18, py + 5, value, 13, c, weight="bold"); t(x, y + h + 16, label, 11, CYN, "middle")

def drum(x, y, digits, sub):
    for k, d in enumerate(digits):
        r(x + k * 26, y, 24, 34, "#151a20", "#4a535d", 1, 3); t(x + k * 26 + 12, y + 25, d, 22, AMB, "middle", "bold")
    t(x, y + 50, sub, 11, AMB)

def frame(i, title, sub):
    y0 = 80 + i * 490
    r(20, y0 - 8, W - 40, 476, "none", GRID, 1)
    t(36, y0 + 18, title, 22, INK, weight="600", fam="Segoe UI, Arial"); t(36, y0 + 40, sub, 14, NOTE, fam="Segoe UI, Arial")
    return y0 + 52

out.append('<rect width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
t(30, 44, "«Панцирь» · свой компьютер, та же система, что у «Каркаса», но ближе к аналоговым приборам · 3 концепции", 23, INK, weight="600", fam="Segoe UI, Arial")
VX, VW, VH, NX = 40, 940, 400, 1010

# ---- A «Дуги»
y = frame(0, "A  «Дуги»", "дуги компьютера «Каркаса» становятся шкалами со стрелками вдоль верхнего края окна; внизу - мнемосхема и ЗАПАС")
gx, gy, gw, gh = view(VX, y, VW, VH)
cx, cy = gx + gw / 2, gy + 520
for k, (lab, f, val, red0) in enumerate([("РАД мЗв/ч", 0.25, "14", 0.6), ("СРЕДА °C", 0.78, "+612", 0.62), ("КОРПУС °C", 0.6, "+540", 0.7)]):
    a0 = math.radians(232 + 26 * k); a1 = a0 + math.radians(22)
    arc(cx, cy, 470, a0, a1, DIM, 2); arc(cx, cy, 466, a0 + (a1 - a0) * red0, a1, RED, 4)
    for j in range(9):
        a = a0 + (a1 - a0) * j / 8; line(cx + 460 * math.cos(a), cy + 460 * math.sin(a), cx + 470 * math.cos(a), cy + 470 * math.sin(a), DIM, 1)
    a = a0 + (a1 - a0) * f
    poly([(cx + 474 * math.cos(a), cy + 474 * math.sin(a)), (cx + 490 * math.cos(a - 0.01), cy + 490 * math.sin(a - 0.01)), (cx + 490 * math.cos(a + 0.01), cy + 490 * math.sin(a + 0.01))], AMB if f < red0 else RED)
    am = (a0 + a1) / 2; t(cx + 440 * math.cos(am), cy + 440 * math.sin(am) + 4, lab + " " + val, 12, WHT, "middle")
alerts(cx, gy + 175)
silhouette(gx + 90, gy + gh - 70, 55, COLS)
drum(gx + gw - 200, gy + gh - 90, ["0", "3", "7"], "ЗАПАС мин · по жаре")
t(gx + 160, gy + gh - 70, "ИЗОЛ 18 %   УПЛ 4 %", 13, AMB); t(gx + 160, gy + gh - 50, "ПОДОШВА Л +930", 13, RED)
for k, s in enumerate(["+ ближе всего к «Каркасу»: та же раскладка, дуги стали шкалами", "+ центр окна свободен",
                       "- шкалы на дугах мелкие, стрелку видно хуже, чем на круге", "главное вверху: радиация, среда, корпус;", "внизу: схема отказов и барабан ЗАПАСА"]):
    note(NX, y + 50 + 32 * k, s)

# ---- B «Приборы»
y = frame(1, "B  «Приборы»", "ряд круглых стрелочных приборов низко в окне - советская приборная доска, нарисованная фотоникой")
gx, gy, gw, gh = view(VX, y, VW, VH)
alerts(gx + gw / 2, gy + 40)
by = gy + gh - 75
for k, (lab, f, val, red0) in enumerate([("ЗАПАС мин", 0.62, "37", 2.0), ("РАД мЗв/ч", 0.22, "14", 0.6), ("СРЕДА °C", 0.78, "+612", 0.62),
                                         ("КОРПУС °C", 0.6, "+540", 0.7), ("ВОДА кг", 0.52, "3,1", 2.0), ("БАТ %", 0.62, "62", 2.0)]):
    dial(gx + 110 + k * 138, by, 48, f, lab, val, red0 if red0 <= 1 else 1.0)
silhouette(gx + gw - 45, gy + 70, 38, COLS)
for k, s in enumerate(["+ самое «аналоговое»: стрелка и красный сектор читаются боковым зрением", "+ советская школа: круглые приборы в ряд, как в кабине",
                       "- занимает нижнюю треть окна", "- точные цифры - мелко под стрелкой", "главное: ЗАПАС, радиация, среда, корпус, вода, батарея;", "отказы - схема в углу"]):
    note(NX, y + 50 + 32 * k, s)

# ---- C «Столбы»
y = frame(2, "C  «Столбы»", "вертикальные шкалы по краям окна (слева жара, справа радиация) с красными зонами; внизу по центру барабан ЗАПАСА и мнемосхема")
gx, gy, gw, gh = view(VX, y, VW, VH)
alerts(gx + gw / 2, gy + 40)
vscale(gx + 40, gy + 70, 230, 0.78, 0.62, "СРЕДА", "+612", RED)
vscale(gx + 120, gy + 70, 230, 0.6, 0.7, "КОРП", "+540", AMB)
vscale(gx + gw - 140, gy + 70, 230, 0.25, 0.6, "РАД", "14", GRN)
vscale(gx + gw - 60, gy + 70, 230, 0.21, 0.5, "ДОЗА", "0,21", GRN)
drum(gx + gw / 2 - 40, gy + gh - 95, ["0", "3", "7"], "ЗАПАС мин · жара")
silhouette(gx + gw / 2 - 120, gy + gh - 60, 40, COLS)
t(gx + gw / 2 + 60, gy + gh - 70, "ВОДА 3,1 · БАТ 62 %", 12, CYN); t(gx + gw / 2 + 60, gy + gh - 50, "ИЗОЛ 18 % · ПОДОШВА +930", 12, AMB)
for k, s in enumerate(["+ центр окна полностью свободен", "+ жара и радиация разведены по сторонам: сразу видно, что давит",
                       "+ красная зона и треугольный указатель - читаются мгновенно", "- шкалы у краёв уходят из центра взгляда", "главное: две шкалы жары, две радиации,", "барабан ЗАПАСА, схема отказов"]):
    note(NX, y + 50 + 32 * k, s)

svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_computer.svg"), "w", encoding="utf-8").write(svg)
print("ok")
