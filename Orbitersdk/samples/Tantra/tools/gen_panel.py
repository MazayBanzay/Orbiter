"""Generate the Tantra 2D panel texture (Textures/Tantra/panel.dds) and the
matching C++ layout header (orbiter2010/PanelLayout.h).

Three panels (Ctrl+Up / Ctrl+Down), after DanSteph's DG-IV:
  * MAIN (id 0, bottom of the screen): four MFDs with their buttons, engine essentials in the
    centre, message strip;
  * UPPER (id 1): power and propulsion systems - chambers, traps, store, compensator, crew;
  * LOWER (id 2): undercarriage and hull - carriage mimic and loads, gear, landing set, port,
    crests, pods, cup irises, hangar, rovers, airlock.
One 2048x2048 A8R8G8B8 texture holds:
  * the three panel backgrounds (UPPER at the top-left; MAIN and LOWER below the atlas),
  * a glyph atlas for code page 1251 (Cyrillic works regardless of the graphics
    client) in several colours, a large-digit atlas,
  * sprites: the four boron-nitride chamber states (animated), the lever knob.
The module draws dynamic elements by blitting from this texture onto itself
(the DeltaGlider technique), so no GDI is needed.
Instrument names follow Efremov's control post.
"""
import os
import sys

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle

TEX_W, TEX_H = 2048, 2048
PANEL_W, PANEL_H = 1600, 520   # UPPER
PANELS = {  # name -> (texture origin, width, height, orbiter panel id)
    "UPPER": ((0, 0), 1600, 520, 1),
    "MAIN": ((0, 1040), 2040, 400, 0),
    "LOWER": ((0, 1480), 1600, 440, 2),
}

DISPLAY_BG = (10, 15, 13)       # every dynamic area is a display window of this colour
PANEL_BG = (28, 35, 32)
FRAME = (70, 90, 82)
LABEL = (217, 164, 65)

# --- glyph atlas -----------------------------------------------------------------
CELL_W, CELL_H, ATLAS_COLS = 11, 20, 32
ATLAS_ROWS = 7                                   # 224 glyphs: cp1251 32..255
FONT_COLORS = {                                  # name -> (rgb, atlas origin)
    "AMBER": ((255, 190, 90), (0, 540)),
    "GREEN": ((120, 255, 150), (0, 690)),
    "RED": ((255, 95, 75), (380, 540)),
    "WHITE": ((225, 228, 220), (380, 690)),
}
BIG_W, BIG_H, BIG_CHARS, BIG_ORIGIN = 24, 40, "0123456789.g -", (1200, 960)

# --- sprites ---------------------------------------------------------------------
CH_W, CH_H = 60, 210
CH_ORIGIN = (800, 740)          # 13 sprites in a row: off, then 4 frames x (field, beam, feed)
KNOB_W, KNOB_H, KNOB_ORIGIN = 50, 36, (800, 960)

# --- panel areas (panel coordinates) ----------------------------------------------
AREAS = {
    "CHAMBERS": (30, 50, 322, 268),
    "LEVER": (24, 274, 328, 318),
    "STOP": (24, 346, 328, 390),
    "TRAPS": (356, 50, 616, 300),
    "TRAPSEL": (356, 346, 616, 390),
    "STORE": (652, 50, 896, 108),
    "IONRODS": (652, 140, 896, 272),
    "PLANT": (652, 290, 896, 390),
    "COMP": (932, 50, 1192, 190),
    "GLIM": (932, 200, 1192, 236),
    "GSTEP": (932, 244, 1192, 280),
    "OVERRIDE": (932, 288, 1192, 324),
    "ALT": (932, 332, 1192, 390),
    "READOUT": (1228, 50, 1576, 210),
    "AIRLOCK": (1228, 222, 1398, 258),
    "EVA": (1406, 222, 1576, 258),
    "CREWSEL": (1228, 266, 1576, 302),
    "CREWINFO": (1228, 310, 1576, 390),
    "SEL_PLAN": (150, 410, 390, 446),
    "SEL_ANA": (398, 410, 638, 446),
    "PODS_AFT": (800, 410, 960, 446),
    "PODS_DOWN": (968, 410, 1128, 446),
    "PODS_IND": (1136, 410, 1588, 446),
    "MSG": (12, 480, 1588, 510),
}
# MAIN: four MFD units [USER1][LEFT] centre [RIGHT][USER2]; screens 320 px square.
MFD_UNITS = [(8, 2), (428, 0), (1200, 1), (1620, 3)]   # (unit x, Orbiter MFD id)
MFD_SY, MFD_S, MFD_BTN_H, MFD_BTN_PITCH, MFD_BTN_Y0 = 36, 320, 32, 50, 14
MAIN_AREAS = {}
for ux, mid in MFD_UNITS:
    MAIN_AREAS[f"M_MFD{mid}_L"] = (ux + 2, MFD_SY, ux + 42, MFD_SY + MFD_S)
    MAIN_AREAS[f"M_MFD{mid}_R"] = (ux + 370, MFD_SY, ux + 410, MFD_SY + MFD_S)
    MAIN_AREAS[f"M_MFD{mid}_B"] = (ux + 46, MFD_SY + MFD_S + 6, ux + 366, MFD_SY + MFD_S + 36)
MAIN_AREAS.update({
    "M_MSG": (428, 6, 1612, 30),
    "M_SEL_PLAN": (856, 36, 1016, 72), "M_SEL_ANA": (1024, 36, 1184, 72),
    "M_START": (856, 80, 1016, 114), "M_STOP": (1024, 80, 1184, 114),
    "M_THROTTLE": (856, 122, 1184, 180),
    "M_G": (856, 188, 1184, 262),
    "M_FLIGHT": (856, 270, 1184, 350),
    "M_GEAR": (856, 358, 1184, 392),
})
LOWER_AREAS = {
    "L_MIMIC": (20, 40, 170, 190), "L_POSE": (180, 40, 332, 190),
    "L_PHASE": (20, 198, 332, 230), "L_PROG": (20, 238, 332, 262),
    "L_GEAR": (364, 44, 688, 80), "L_SET_LEVEL": (364, 88, 522, 124), "L_SET_STAND": (530, 88, 688, 124),
    "L_ERECT": (364, 132, 688, 176), "L_PORT": (364, 184, 688, 220), "L_LOADS": (364, 228, 688, 382),
    "L_CRESTS": (732, 44, 1008, 80), "L_PODS_AFT": (732, 88, 866, 124), "L_PODS_DOWN": (874, 88, 1008, 124),
    "L_PODS_IND": (732, 132, 1008, 166), "L_IRIS": (732, 176, 1008, 262),
    "L_HANGAR": (1052, 44, 1310, 80), "L_ROVERS": (1318, 44, 1576, 80), "L_DECK": (1052, 88, 1576, 150),
    "L_AIRLOCK": (1052, 158, 1310, 194), "L_EVA": (1318, 158, 1576, 194), "L_CREWSEL": (1052, 202, 1576, 238),
    "L_PT_LIFT": (1052, 256, 1310, 290), "L_PT_LOAD": (1318, 256, 1576, 290),
    "L_PT_DROP": (1052, 298, 1310, 332), "L_PT_STOP": (1318, 298, 1576, 332), "L_PT_ST": (1052, 340, 1576, 388),
    "L_MSG": (12, 400, 1588, 430),
}
LOWER_BLOCKS = [(12, 344, "ПОЛОЖЕНИЕ КОРАБЛЯ"), (352, 700, "ЛАФЕТ И ШАССИ"), (720, 1020, "КОРПУС"),
                (1040, 1588, "АНГАР · ШЛЮЗ · ПОРТ АНАМЕЗОНА")]
LOWER_LABELS = [(95, 268 + 12, "0   1   2   3   4   5   6", 8)]
# Mimic sprites of the ship pose: 3 x 3 cells of 150 px beside the atlas.
MIMIC_W, MIMIC_ORIGIN = 150, (1596, 560)
MIMIC_POSES = ["flight", "level", "lifted", "turn30", "turn60", "vertcols", "stand", "port", "legsout"]

# Fill palette: 8x8 swatches in the free row y 1008..1016. Panel fills are stretch-blits from
# here (oapiColourFill does not reach the MAIN/LOWER panels below texture row 1024 under D3D9Client).
PALETTE = [DISPLAY_BG, (0, 0, 0), (255, 255, 255), (22, 20, 18), (30, 34, 32), (30, 36, 34), (50, 45, 38),
           (90, 70, 40), (120, 95, 50), (120, 90, 40), (60, 170, 90), (190, 60, 45), (80, 170, 110), (80, 220, 120),
           (110, 170, 230), (230, 140, 40), (200, 160, 70), (80, 220, 70), (190, 120, 255), (230, 150, 60)]
PALETTE_ORIGIN = (0, 1008)

LEVER_X = [60, 132, 204, 276]   # knob centres: off, field, beam, feed
TRAP_X = [372, 432, 492, 552]   # column left edges, 44 px wide
TRAP_Y0, TRAP_Y1 = 60, 270
ROD_X0, ROD_W, ROD_GAP, ROD_N, ROD_Y0, ROD_Y1 = 658, 16, 8, 10, 146, 246
CH_X = [36, 110, 184, 258]      # chamber tile left edges
CH_Y = 54

BLOCKS = [  # (x0, x1, title)
    (12, 336, "АНАМЕЗОННЫЕ ДВИГАТЕЛИ"),
    (344, 628, "ЛОВУШКИ АНАМЕЗОНА"),
    (640, 908, "ЭНЕРГЕТИКА"),
    (920, 1204, "КОМПЕНСАТОР"),
    (1216, 1588, "УРАВНИТЕЛЬ СКОРОСТЕЙ · ЭКИПАЖ"),
]
STATIC_LABELS = [  # (x, y, text, size)
    (60, 332, "ВЫКЛ", 8), (132, 332, "ПОЛЕ", 8), (204, 332, "ЛУЧ", 8), (276, 332, "ПОДАЧА", 8),
    (774, 36, "НАКОПИТЕЛЬ ПОЛЯ", 8), (774, 128, "ИОННЫЕ ЗАРЯДЫ", 8), (774, 282, "ВЭУ · ГРАНУЛЫ", 8),
    (1062, 36, "ГАСИТ", 8), (1402, 36, "УРАВНИТЕЛЬ", 8), (486, 36, "ОРАНЖЕВЫЕ СТОЛБИКИ", 8),
    (80, 428, "МАРШЕВЫЕ", 9), (719, 428, "ГОНДОЛЫ", 9),
    (176, 36, "ПРОРЕЗЬ ПУЛЬТА: ЧЕТЫРЕ КАМЕРЫ", 8),
]


def rgb01(c):
    return tuple(v / 255 for v in c)


def render(width, height, draw):
    """Render with matplotlib in pixel coordinates (origin top-left)."""
    fig = plt.figure(figsize=(width / 100, height / 100), dpi=100)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, width)
    ax.set_ylim(height, 0)
    ax.axis("off")
    draw(fig, ax)
    fig.canvas.draw()
    img = np.asarray(fig.canvas.buffer_rgba())[:, :, :3].copy()
    plt.close(fig)
    return img


def background():
    def draw(fig, ax):
        fig.patch.set_facecolor(rgb01(PANEL_BG))
        ax.add_patch(Rectangle((0, 0), PANEL_W, PANEL_H, color=rgb01(PANEL_BG)))
        for x0, x1, title in BLOCKS:
            ax.add_patch(FancyBboxPatch((x0, 8), x1 - x0, 388, boxstyle="round,pad=0,rounding_size=10",
                                        fill=False, ec=rgb01(FRAME), lw=2))
            ax.text((x0 + x1) / 2, 20, title, color=rgb01(LABEL), ha="center", va="center",
                    fontsize=9, fontweight="bold", family="DejaVu Sans")
        ax.add_patch(FancyBboxPatch((12, 402), 1576, 52, boxstyle="round,pad=0,rounding_size=10",
                                    fill=False, ec=rgb01(FRAME), lw=2))
        for name, (x0, y0, x1, y1) in AREAS.items():
            ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, color=rgb01(DISPLAY_BG)))
            ax.add_patch(Rectangle((x0 - 1, y0 - 1), x1 - x0 + 2, y1 - y0 + 2, fill=False, ec=rgb01(FRAME), lw=1))
        for x, y, text, size in STATIC_LABELS:
            ax.text(x, y, text, color=rgb01(LABEL), ha="center", va="center", fontsize=size, family="DejaVu Sans")
    return render(PANEL_W, PANEL_H, draw)


def main_background():
    W, H = PANELS["MAIN"][1], PANELS["MAIN"][2]

    def draw(fig, ax):
        fig.patch.set_facecolor(rgb01(PANEL_BG))
        ax.add_patch(Rectangle((0, 0), W, H, color=rgb01(PANEL_BG)))
        for ux, mid in MFD_UNITS:
            ax.add_patch(FancyBboxPatch((ux, 30), 412, H - 36, boxstyle="round,pad=0,rounding_size=12",
                                        fill=True, fc=(0.09, 0.11, 0.10), ec=rgb01(FRAME), lw=2))
            ax.add_patch(Rectangle((ux + 44, MFD_SY - 2), MFD_S + 4, MFD_S + 4, color=(0, 0, 0)))
            for side in (0, 1):
                for i in range(6):
                    x0 = ux + (2 if side == 0 else 370)
                    y0 = MFD_SY + MFD_BTN_Y0 + i * MFD_BTN_PITCH
                    ax.add_patch(Rectangle((x0, y0), 40, MFD_BTN_H, color=rgb01(DISPLAY_BG)))
                    ax.add_patch(Rectangle((x0, y0), 40, MFD_BTN_H, fill=False, ec=rgb01(FRAME), lw=1))
            by = MFD_SY + MFD_S + 6
            for bx, lbl in ((ux + 56, "PWR"), (ux + 186, "SEL"), (ux + 306, "MNU")):
                ax.add_patch(Rectangle((bx, by), 50, 30, color=rgb01(DISPLAY_BG)))
                ax.add_patch(Rectangle((bx, by), 50, 30, fill=False, ec=rgb01(FRAME), lw=1))
                ax.text(bx + 25, by + 15, lbl, color=rgb01(LABEL), ha="center", va="center", fontsize=8,
                        family="DejaVu Sans")
        ax.add_patch(FancyBboxPatch((848, 30), 344, H - 36, boxstyle="round,pad=0,rounding_size=12",
                                    fill=False, ec=rgb01(FRAME), lw=2))
        for name, (x0, y0, x1, y1) in MAIN_AREAS.items():
            if "_MFD" in name:
                continue
            ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, color=rgb01(DISPLAY_BG)))
            ax.add_patch(Rectangle((x0 - 1, y0 - 1), x1 - x0 + 2, y1 - y0 + 2, fill=False, ec=rgb01(FRAME), lw=1))
        ax.text(1020, 20, "«ТАНТРА» · ЦЕНТРАЛЬНЫЙ ПОСТ", color=rgb01(LABEL), ha="center", va="center", fontsize=9,
                fontweight="bold", family="DejaVu Sans", alpha=0.0)
    return render(W, H, draw)


def lower_background():
    W, H = PANELS["LOWER"][1], PANELS["LOWER"][2]

    def draw(fig, ax):
        fig.patch.set_facecolor(rgb01(PANEL_BG))
        ax.add_patch(Rectangle((0, 0), W, H, color=rgb01(PANEL_BG)))
        for x0, x1, title in LOWER_BLOCKS:
            ax.add_patch(FancyBboxPatch((x0, 8), x1 - x0, 384, boxstyle="round,pad=0,rounding_size=10",
                                        fill=False, ec=rgb01(FRAME), lw=2))
            ax.text((x0 + x1) / 2, 22, title, color=rgb01(LABEL), ha="center", va="center", fontsize=9,
                    fontweight="bold", family="DejaVu Sans")
        for name, (x0, y0, x1, y1) in LOWER_AREAS.items():
            ax.add_patch(Rectangle((x0, y0), x1 - x0, y1 - y0, color=rgb01(DISPLAY_BG)))
            ax.add_patch(Rectangle((x0 - 1, y0 - 1), x1 - x0 + 2, y1 - y0 + 2, fill=False, ec=rgb01(FRAME), lw=1))
        ax.text(176, 276, "0 лёжа · 1 подгот. · 2 подъём · 3 поворот · 4 лапы · 5 опуск. · 6 сбор", color=rgb01(LABEL),
                ha="center", va="center", fontsize=6.5, family="DejaVu Sans")
    return render(W, H, draw)


def mimic_sprites():
    """Side views of the ship pose for the LOWER panel mimic (same profile as the mesh)."""
    import math
    TOP = [(0, 13), (8, 14), (44, 14), (52, 12.5), (58, 9.5), (62, 8.6), (122, 8.6), (130, 7.4), (138, 4.8),
           (143, 2.2), (146, 0)]
    BOT = [(0, -9), (8, -9.5), (44, -9.5), (52, -9), (58, -8.8), (62, -8.6), (122, -8.6), (130, -7.4),
           (138, -4.8), (143, -2.2), (146, 0)]
    prof = TOP + BOT[::-1]

    def one(pose):
        th = {"turn30": 30, "turn60": 60, "vertcols": 90, "stand": 90, "port": 90, "legsout": 90}.get(pose, 0)
        h = {"flight": 45, "level": 20, "lifted": 54}.get(pose, 54 if pose in ("turn30", "turn60", "vertcols", "legsout") else 50)
        if pose == "port":
            h = 45
        c, sn = math.cos(math.radians(th)), math.sin(math.radians(th))

        def T(s, y):  # trunnion (s = 38) at (0, h)
            ds = s - 38
            return ds * c - y * sn, h + ds * sn + y * c

        def draw(fig, ax):
            ax.add_patch(Rectangle((0, 0), MIMIC_W, MIMIC_W, color=rgb01(DISPLAY_BG)))
            k = MIMIC_W / 172.0
            xc = 35.0 * c  # centre the hull in the cell
            X = lambda x: MIMIC_W / 2 + (x - xc) * k
            Y = lambda y: MIMIC_W - 8 - y * k
            ax.plot([4, MIMIC_W - 4], [Y(0), Y(0)], color=(0.45, 0.35, 0.25), lw=1.5)
            pts = [T(s, y) for s, y in prof]
            ax.fill([X(p[0]) for p in pts], [Y(p[1]) for p in pts], color=(0.55, 0.6, 0.65), ec=(0.8, 0.85, 0.9), lw=0.6)
            col = (0.95, 0.72, 0.3)
            if pose in ("level", "lifted", "turn30", "turn60", "vertcols", "legsout"):
                ax.plot([X(0), X(0)], [Y(h), Y(0)], color=(0.5, 0.65, 0.95), lw=2.2)
                ax.plot([X(-13), X(13)], [Y(0.6), Y(0.6)], color=col, lw=2.2)
            if pose == "level":
                a = T(5, -7)
                ax.plot([X(a[0]), X(a[0] + 2)], [Y(a[1]), Y(0.5)], color=(0.8, 0.82, 0.86), lw=2)
            if pose in ("stand", "legsout", "port"):
                for sd in (-1, 1):
                    a = T(5, sd * 9)
                    foot_y = 0.5 if pose == "stand" else (4.5 if pose == "legsout" else None)
                    if foot_y is not None:
                        fx = a[0] - sd * 12
                        ax.plot([X(a[0]), X(fx)], [Y(a[1]), Y(foot_y)], color=(0.8, 0.82, 0.86), lw=2)
                        ax.plot([X(fx - 6), X(fx + 6)], [Y(foot_y), Y(foot_y)], color=col, lw=2)
            if pose == "port":
                ax.add_patch(Rectangle((X(-30), Y(4)), 60 * k, 4 * k, color=(0.25, 0.35, 0.5)))
                for r in (6, 10, 14):
                    ax.add_patch(matplotlib.patches.Ellipse((X(0), Y(6)), 2 * r * k, 0.6 * r * k, fill=False,
                                                            ec=(0.7, 0.5, 1.0), lw=0.8))
        return render(MIMIC_W, MIMIC_W, draw)

    rows = []
    for r in range(3):
        rows.append(np.concatenate([one(MIMIC_POSES[r * 3 + c]) for c in range(3)], axis=1))
    return np.concatenate(rows, axis=0)


def glyph_atlas(color, cell_w=CELL_W, cell_h=CELL_H, chars=None, cols=ATLAS_COLS, rows=ATLAS_ROWS, fontsize=11):
    if chars is None:
        chars = []
        for code in range(32, 256):
            try:
                chars.append(bytes([code]).decode("cp1251"))
            except UnicodeDecodeError:
                chars.append(" ")

    def draw(fig, ax):
        fig.patch.set_facecolor(rgb01(DISPLAY_BG))
        ax.add_patch(Rectangle((0, 0), cols * cell_w, rows * cell_h, color=rgb01(DISPLAY_BG)))
        for i, ch in enumerate(chars):
            cx = (i % cols) * cell_w + cell_w / 2
            cy = (i // cols) * cell_h + cell_h / 2
            ax.text(cx, cy, ch, color=rgb01(color), ha="center", va="center", fontsize=fontsize,
                    family="DejaVu Sans Mono")
    return render(cols * cell_w, rows * cell_h, draw)


def chamber_sprites():
    """13 sprites: off, then frames 0..3 of field / beam / feed."""
    def one(kind, frame):
        def draw(fig, ax):
            ax.add_patch(Rectangle((0, 0), CH_W, CH_H, color=rgb01(DISPLAY_BG)))
            y = np.linspace(8, CH_H - 8, 200)
            if kind >= 1:  # green double helix of the chamber field
                for ph in (0.0, np.pi):
                    x = CH_W / 2 + 17 * np.sin(y / 14.0 - frame * np.pi / 2 + ph)
                    ax.plot(x, y, color=(0.3, 1.0, 0.55), lw=2.2, alpha=0.9)
            if kind >= 2:  # grey guide beam of K-particles
                ax.add_patch(Rectangle((CH_W / 2 - 4, 6), 8, CH_H - 12, color=(0.62, 0.64, 0.68), alpha=0.85))
            if kind >= 3:  # violet anamezon flash
                for w, a in ((22, 0.25), (12, 0.5), (4, 1.0)):
                    ax.add_patch(Rectangle((CH_W / 2 - w / 2, 6), w, CH_H - 12, color=(0.78, 0.55, 1.0), alpha=a))
            # boron-nitride cylinder walls
            ax.add_patch(FancyBboxPatch((6, 4), CH_W - 12, CH_H - 8, boxstyle="round,pad=0,rounding_size=12",
                                        fill=False, ec=(0.85, 0.87, 0.9), lw=1.6, alpha=0.8))
        return render(CH_W, CH_H, draw)

    tiles = [one(0, 0)]
    for kind in (1, 2, 3):
        for frame in range(4):
            tiles.append(one(kind, frame))
    return np.concatenate(tiles, axis=1)


def knob():
    def draw(fig, ax):
        ax.add_patch(Rectangle((0, 0), KNOB_W, KNOB_H, color=rgb01(DISPLAY_BG)))
        ax.add_patch(FancyBboxPatch((3, 3), KNOB_W - 6, KNOB_H - 6, boxstyle="round,pad=0,rounding_size=7",
                                    color=(0.72, 0.52, 0.2)))
        ax.add_patch(Rectangle((KNOB_W / 2 - 2, 6), 4, KNOB_H - 12, color=(0.15, 0.1, 0.05)))
    return render(KNOB_W, KNOB_H, draw)


def write_dds(path, rgb):
    h, w, _ = rgb.shape
    header = bytearray(128)
    header[0:4] = b"DDS "
    vals = {4: 124, 8: 0x100F, 12: h, 16: w, 20: w * 4, 76: 32, 80: 0x41, 88: 32,
            92: 0x00FF0000, 96: 0x0000FF00, 100: 0x000000FF, 104: 0xFF000000, 108: 0x1000}
    for off, v in vals.items():
        header[off:off + 4] = int(v).to_bytes(4, "little")
    bgra = np.empty((h, w, 4), np.uint8)
    bgra[:, :, 0], bgra[:, :, 1], bgra[:, :, 2], bgra[:, :, 3] = rgb[:, :, 2], rgb[:, :, 1], rgb[:, :, 0], 255
    with open(path, "wb") as f:
        f.write(header)
        f.write(bgra.tobytes())


def place(tex, img, origin):
    x, y = origin
    h, w, _ = img.shape
    tex[y:y + h, x:x + w] = img


def write_header(path):
    L = ["// GENERATED by tools/gen_panel.py - do not edit, change the generator instead.",
         "#pragma once", "", "namespace tantra::panel {", ""]
    L += [f"constexpr int kTexW = {TEX_W}, kTexH = {TEX_H}, kPanelW = {PANEL_W}, kPanelH = {PANEL_H};",
          f"constexpr int kDisplayBg[3] = {{{DISPLAY_BG[0]}, {DISPLAY_BG[1]}, {DISPLAY_BG[2]}}};",
          f"constexpr int kCellW = {CELL_W}, kCellH = {CELL_H}, kAtlasCols = {ATLAS_COLS};",
          "enum Font { FONT_" + ", FONT_".join(FONT_COLORS) + " };",
          "constexpr int kFontOrigin[][2] = {" + ", ".join(f"{{{o[0]}, {o[1]}}}" for _, o in FONT_COLORS.values()) + "};",
          f"constexpr int kBigW = {BIG_W}, kBigH = {BIG_H}, kBigX = {BIG_ORIGIN[0]}, kBigY = {BIG_ORIGIN[1]};",
          f'constexpr char kBigChars[] = "{BIG_CHARS}";',
          f"constexpr int kChW = {CH_W}, kChH = {CH_H}, kChX = {CH_ORIGIN[0]}, kChY = {CH_ORIGIN[1]};",
          "constexpr int kChTileX[4] = {" + ", ".join(map(str, CH_X)) + f"}}, kChTileY = {CH_Y};",
          f"constexpr int kKnobW = {KNOB_W}, kKnobH = {KNOB_H}, kKnobX = {KNOB_ORIGIN[0]}, kKnobY = {KNOB_ORIGIN[1]};",
          "constexpr int kLeverX[4] = {" + ", ".join(map(str, LEVER_X)) + "};",
          "constexpr int kTrapX[4] = {" + ", ".join(map(str, TRAP_X)) + f"}}, kTrapY0 = {TRAP_Y0}, kTrapY1 = {TRAP_Y1}, kTrapW = 44;",
          f"constexpr int kRodX0 = {ROD_X0}, kRodW = {ROD_W}, kRodGap = {ROD_GAP}, kRodN = {ROD_N}, kRodY0 = {ROD_Y0}, kRodY1 = {ROD_Y1};",
          f"constexpr int kPaletteN = {len(PALETTE)}, kPaletteX = {PALETTE_ORIGIN[0]}, kPaletteY = {PALETTE_ORIGIN[1]};",
          "constexpr int kPalette[kPaletteN][3] = {" + ", ".join(f"{{{c[0]}, {c[1]}, {c[2]}}}" for c in PALETTE) + "};",
          "enum PanelId { PANEL_MAIN = 0, PANEL_UPPER = 1, PANEL_LOWER = 2 };",
          "struct PanelGeom { int texX, texY, w, h; };",
          "constexpr PanelGeom kPanel[3] = {" + ", ".join(
              f"{{{PANELS[n][0][0]}, {PANELS[n][0][1]}, {PANELS[n][1]}, {PANELS[n][2]}}}" for n in ("MAIN", "UPPER", "LOWER")) + "};",
          f"constexpr int kMfdSY = {MFD_SY}, kMfdS = {MFD_S}, kMfdBtnH = {MFD_BTN_H}, kMfdBtnPitch = {MFD_BTN_PITCH}, kMfdBtnY0 = {MFD_BTN_Y0};",
          "constexpr int kMfdUnit[4][2] = {" + ", ".join(f"{{{ux}, {mid}}}" for ux, mid in MFD_UNITS) + "};  // x, MFD id",
          f"constexpr int kMimicW = {MIMIC_W}, kMimicX = {MIMIC_ORIGIN[0]}, kMimicY = {MIMIC_ORIGIN[1]};",
          "enum MimicPose { " + ", ".join(f"MIMIC_{m.upper()}" for m in MIMIC_POSES) + " };",
          "", "enum Area {"]
    allareas = [(n, a, "UPPER") for n, a in AREAS.items()] + [(n, a, "MAIN") for n, a in MAIN_AREAS.items()] + \
               [(n, a, "LOWER") for n, a in LOWER_AREAS.items()]
    L += [f"    A_{n}," if not n.startswith(("M_", "L_")) else f"    {n}," for n, _, _ in allareas] + ["    A_COUNT", "};"]
    L += ["// Area rectangles in panel coordinates (registration, mouse) and in texture coordinates (drawing).",
          "constexpr int kAreaPanel[A_COUNT] = {" + ", ".join({"MAIN": "0", "UPPER": "1", "LOWER": "2"}[p] for _, _, p in allareas) + "};",
          "constexpr int kAreaLocal[A_COUNT][4] = {"]
    L += [f"    {{{a[0]}, {a[1]}, {a[2]}, {a[3]}}},  // {n}" for n, a, _ in allareas]
    L += ["};", "constexpr int kArea[A_COUNT][4] = {"]
    for n, a, pnl in allareas:
        ox, oy = PANELS[pnl][0]
        L.append(f"    {{{a[0] + ox}, {a[1] + oy}, {a[2] + ox}, {a[3] + oy}}},  // {n}")
    L += ["};", "", "}  // namespace tantra::panel", ""]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    args = [a for i, a in enumerate(sys.argv[1:], 1) if not a.startswith("--") and sys.argv[i - 1] != "--preview"]
    root = args[0] if args else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..")
    tex = np.zeros((TEX_H, TEX_W, 3), np.uint8)
    tex[:, :] = DISPLAY_BG
    place(tex, background(), (0, 0))
    place(tex, main_background(), PANELS["MAIN"][0])
    place(tex, lower_background(), PANELS["LOWER"][0])
    place(tex, mimic_sprites(), MIMIC_ORIGIN)
    for i, c in enumerate(PALETTE):
        tex[PALETTE_ORIGIN[1]:PALETTE_ORIGIN[1] + 8, PALETTE_ORIGIN[0] + 8 * i:PALETTE_ORIGIN[0] + 8 * i + 8] = c
    for color, origin in FONT_COLORS.values():
        place(tex, glyph_atlas(color), origin)
    place(tex, glyph_atlas(FONT_COLORS["AMBER"][0], BIG_W, BIG_H, list(BIG_CHARS), len(BIG_CHARS), 1, 22),
          BIG_ORIGIN)
    place(tex, chamber_sprites(), CH_ORIGIN)
    place(tex, knob(), KNOB_ORIGIN)
    out = os.path.join(root, "Textures", "Tantra")
    os.makedirs(out, exist_ok=True)
    write_dds(os.path.join(out, "panel.dds"), tex)
    write_header(os.path.join(os.path.dirname(__file__), "..", "orbiter2010", "PanelLayout.h"))
    if "--preview" in sys.argv:
        import matplotlib.image as mpimg
        mpimg.imsave(sys.argv[sys.argv.index("--preview") + 1], tex)
    print("panel.dds + PanelLayout.h written")
