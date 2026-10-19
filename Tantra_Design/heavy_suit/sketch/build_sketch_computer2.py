# «Панцирь» computer, layout v2 (user, 2026-10-18): the top arcs as analogue scales (concept A); below - navigation and
# the two MFDs of the Karkas computer (smaller); a control menu of the Pantsir itself: its data, the radiator, the drives.
# Same operating system and tone as the Karkas computer. The background is only a dark outside (no scene drawn).
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1920, 1080
out = []
AMB, RED, GRN, CYN, WHT, DIM, BG = "#ffb347", "#ff5a4a", "#7fdc8a", "#7fd6ff", "#e8ecef", "#8c98a3", "#06080b"
MONO = "Consolas, monospace"
def r(x, y, w, h, fill, stroke="none", sw=1, op=1):
    out.append('<rect x="%g" y="%g" width="%g" height="%g" fill="%s" fill-opacity="%g" stroke="%s" stroke-width="%g"/>' % (x, y, w, h, fill, op, stroke, sw))
def t(x, y, s, size=14, color=WHT, anchor="start", weight="normal"):
    out.append('<text x="%g" y="%g" font-family="%s" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, MONO, size, color, anchor, weight, s))
def line(x1, y1, x2, y2, c, w=1.5):
    out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="%g"/>' % (x1, y1, x2, y2, c, w))
def poly(pts, fill, stroke="none", sw=1):
    out.append('<polygon points="%s" fill="%s" stroke="%s" stroke-width="%g"/>' % (" ".join("%g,%g" % p for p in pts), fill, stroke, sw))
def arc(cx, cy, rad, a0, a1, c, w):
    x0, y0 = cx + rad * math.cos(a0), cy + rad * math.sin(a0); x1, y1 = cx + rad * math.cos(a1), cy + rad * math.sin(a1)
    out.append('<path d="M %g %g A %g %g 0 0 1 %g %g" fill="none" stroke="%s" stroke-width="%g"/>' % (x0, y0, rad, rad, x1, y1, c, w))
def btn(x, y, w, s, on=False):
    r(x, y, w, 30, "none", AMB if on else DIM, 2 if on else 1); t(x + w / 2, y + 21, s, 14, AMB if on else WHT, "middle")

# the outside (a mid-dark grey so the shading reads), and the helmet's edge as a soft shading top and bottom - no frame
out.append('<defs><linearGradient id="top" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#000" stop-opacity="0.92"/>'
           '<stop offset="1" stop-color="#000" stop-opacity="0"/></linearGradient>'
           '<linearGradient id="bot" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#000" stop-opacity="0.92"/>'
           '<stop offset="1" stop-color="#000" stop-opacity="0"/></linearGradient></defs>')
out.append('<rect width="%d" height="%d" fill="#1c2128"/>' % (W, H))
out.append('<rect x="0" y="0" width="%d" height="70" fill="#000" fill-opacity="0.92"/>' % W)
out.append('<rect x="0" y="70" width="%d" height="90" fill="url(#top)"/>' % W)
out.append('<rect x="0" y="735" width="%d" height="%d" fill="#000" fill-opacity="0.92"/>' % (W, H - 735))
out.append('<rect x="0" y="645" width="%d" height="90" fill="url(#bot)"/>' % W)
# a static vignette: the corners a little darker too (one fixed texture in the game - nothing animated, no cost)
out.append('<defs><radialGradient id="vig" cx="0.5" cy="0.5" r="0.75"><stop offset="0.6" stop-color="#000" stop-opacity="0"/>'
           '<stop offset="1" stop-color="#000" stop-opacity="0.55"/></radialGradient></defs>')
out.append('<rect width="%d" height="%d" fill="url(#vig)"/>' % (W, H))
# ---- the top: heading tape (as Karkas) and the arc scales (concept A)
for k in range(-5, 6):
    x = 960 + k * 58; line(x, 118, x, 128 if k % 2 else 134, DIM, 1)
    if k % 2 == 0: t(x, 108, "%03d" % ((358 + k * 5) % 360), 13, WHT, "middle")
r(926, 140, 68, 30, "none", WHT, 1.5); t(960, 162, "358°", 18, WHT, "middle")
cx, cy, R = 960, 1180, 1150
def scale_arc(a0d, a1d, f, red0, lab, val, c):
    a0, a1 = math.radians(a0d), math.radians(a1d)
    arc(cx, cy, R, a0, a1, DIM, 2); arc(cx, cy, R - 4, a0 + (a1 - a0) * red0, a1, RED, 5)
    for j in range(13):
        a = a0 + (a1 - a0) * j / 12; l = 14 if j % 4 == 0 else 7
        line(cx + (R - l) * math.cos(a), cy + (R - l) * math.sin(a), cx + R * math.cos(a), cy + R * math.sin(a), DIM, 1)
    a = a0 + (a1 - a0) * f
    poly([(cx + (R + 4) * math.cos(a), cy + (R + 4) * math.sin(a)), (cx + (R + 24) * math.cos(a - 0.006), cy + (R + 24) * math.sin(a - 0.006)),
          (cx + (R + 24) * math.cos(a + 0.006), cy + (R + 24) * math.sin(a + 0.006))], c)
    am = (a0 + a1) / 2; t(cx + (R - 34) * math.cos(am), cy + (R - 34) * math.sin(am), lab + "  " + val, 15, c, "middle")
scale_arc(236, 250, 0.25, 0.6, "РАДИАЦИЯ мЗв/ч", "14 · поле 50 %", GRN)
scale_arc(252, 266, 0.42, 0.85, "ТЕЛО", "37,6 °C · пульс 92", WHT)
scale_arc(274, 288, 0.78, 0.62, "СРЕДА", "+612 °C", RED)
scale_arc(290, 304, 0.6, 0.7, "КОРПУС", "+540 °C", AMB)
t(960, 232, "22:41:06  |  СРЕДА ВНЕ ДОПУСКА +612 °C  ·  подтвердите щелчком", 14, RED, "middle")
# top-right buttons (the Karkas row, Pantsir's own set)
for k, (s, on) in enumerate([("ПОЛЕ 50", True), ("ШЛЕМ", False), ("СВЕТ", True), ("ПНВ", False)]): btn(1350 + k * 92, 60, 86, s, on)
for k, (s, on) in enumerate([("СЕРВО", True), ("ЭКОНОМ", False), ("ИСПАР.", True)]): btn(1442 + k * 92, 100, 86, s, on)
t(1710, 48, "Ио · рассвет · вакуум · 1 нбар", 13, DIM, "end")
# top-left: the ZAPAS drum (the Pantsir's one number)
for k, d in enumerate("037"): r(190 + k * 34, 60, 30, 44, "#141920", "#4a535d", 1); t(205 + k * 34, 92, d, 28, AMB, "middle", "bold")
t(300, 80, "ЗАПАС мин", 14, AMB); t(300, 100, "по жаре · вода 3,1 кг", 13, AMB)

# ---- the Pantsir's control menu (left): its pages; the RADIATOR page open
mx, my, mw, mh = 70, 760, 420, 280
r(mx, my, mw, mh, "#0b0f14", "#2c333b", 1, 0.85)
t(mx + 14, my + 26, "ПАНЦИРЬ", 18, WHT, weight="bold"); t(mx + mw - 14, my + 26, "управление", 13, DIM, "end")
tabs = ["ДАННЫЕ", "РАДИАТОР", "ПРИВОДЫ", "ПОЛЕ", "ПОВРЕЖД."]
for k, s_ in enumerate(tabs):
    on = s_ == "РАДИАТОР"; r(mx + 10 + k * 80, my + 38, 77, 24, "none", AMB if on else DIM, 2 if on else 1); t(mx + 48 + k * 80, my + 55, s_, 11, AMB if on else WHT, "middle")
rows = rows_all = [("Радиатор", "+774 °C · светится", AMB), ("Нагрузка насоса", "64 % · COP 0,12", AMB), ("Компрессор", "перегрузка x2,1", RED),
        ("Охлаждение", "983 Вт (испаритель)", WHT), ("Испаритель", "3,1 кг · 1 кВт · 186 мин", CYN), ("Предел по жаре", "+502 °C · без поля +522", AMB),
        ("Аппаратура", "+40 °C", GRN), ("Контур", "исправен", GRN)]
for k, (a, b, c) in enumerate(rows):
    t(mx + 14, my + 90 + 23 * k, a, 13, WHT); t(mx + mw - 14, my + 90 + 23 * k, b, 13, c, "end")

# ---- the two MFDs of the Karkas computer, smaller; navigation between them
def mfd(x, y, w, h, title, tabs_, sel):
    r(x, y, w, h, "#0b0f14", "#2c333b", 1, 0.85)
    t(x + 12, y + 22, title, 15, WHT); t(x + w - 12, y + 22, "▿", 14, DIM, "end")
    for k, s in enumerate(tabs_):
        on = k == sel; t(x + 14 + k * (w - 28) / len(tabs_), y + 44, s, 11, AMB if on else DIM)
    line(x + 10, y + 52, x + w - 10, y + 52, "#2c333b", 1)
mfd(520, 760, 420, 280, "КАРТА", ["МЕСТН", "ОРБИТА", "ПИТАНИЕ", "ОПЦИИ"], 0)
for k in range(1, 5): out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="#3a6070" stroke-width="1"/>' % (730, 920, 26 * k))
poly([(730, 905), (722, 930), (738, 930)], CYN); t(538, 1025, "100 м", 11, DIM)
mfd(970, 760, 420, 280, "ОРГАНИЗМ", ["ЦЕЛИ", "ПОЛЁТ", "ПОСАДКА", "ОРГАНИЗМ"], 3)
for k, (a, b) in enumerate([("ПУЛЬС", "92"), ("ТЕМПЕРАТУРА ТЕЛА", "37,6 °C"), ("ДОЗА ЗА ВЫХОД", "0,21 Зв"), ("СОСТОЯНИЕ", "ПЕРЕГРЕВ 1")]):
    t(986, 840 + 44 * k, a, 13, DIM); t(1374, 840 + 44 * k, b, 14, WHT, "end")
# navigation strip (bottom centre, as Karkas): speed, gait, target, ground
for k, (a, b, c) in enumerate([("СКОРОСТЬ", "0,7 м/с", WHT), ("КУРС", "358°", WHT), ("ЦЕЛЬ", "кромка 4 м", AMB), ("ГРУНТ", "+780 °C", RED)]):
    t(560 + 220 * k, 720, a, 13, DIM); t(560 + 220 * k, 742, b, 17, c)
# right: consumption (as Karkas РАСХОД), short
r(1420, 760, 420, 280, "#0b0f14", "#2c333b", 1, 0.85)
t(1434, 786, "РАСХОД", 17, WHT); t(1826, 786, "хватит на 9 ч · батарея", 13, AMB, "end")
for k, (a, b) in enumerate([("Питание", "2 310 Вт · поле 1 000"), ("Батарея", "62 % · 9 ч"), ("Кислород", "81 % · 30 ч"), ("Вода испарителя", "3,1 кг · 186 мин")]):
    t(1434, 830 + 40 * k, a, 14, WHT); t(1826, 830 + 40 * k, b, 14, CYN if k == 3 else WHT, "end")

svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_computer2.svg"), "w", encoding="utf-8").write(svg)
print("ok")
