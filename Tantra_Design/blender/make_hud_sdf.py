# OrbiterCrew: signed-distance glyph atlases of the helmet display's font for the D3D11 client's native text
# (D3D11Hud.h: FontLoad). One atlas per weight, any size and colour at run time, the glow from the distance.
# Font: Jura (SIL Open Font License 1.1, Tantra_Design/fonts/OFL.txt), the same as make_hud_font.py (the Sketchpad path).
# Output: Textures\Tantra\HudSDF<weight>.dds (RGBA: white, distance in alpha, 0.5 = the edge) and
#         Config\Tantra\HudSDF<weight>.txt (SDF <em> <spread>, then codepoint x y w h ox oy advance in atlas px).
# Run with the system Python (Pillow, numpy, scipy).
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt

ORB = r"C:\Games\Orbiter-2024"
FONT = os.path.join(ORB, "Tantra_Design", "fonts", "Jura.ttf")
CHARS = "".join(chr(c) for c in range(32, 127)) + "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя" + "°…±·—–«»№×−²₂↑↓←→▲▶◀▸◂✓≈≤≥Δ"
EM = 48          # atlas px per em
SPREAD = 8       # the distance kept on each side of the edge, atlas px
OVER = 4         # rendered at 4x, the distance measured there, then reduced: smooth edges
W = 1024

def build(weight):
    font = ImageFont.truetype(FONT, EM * OVER); font.set_variation_by_axes([weight])
    asc, desc = font.getmetrics()
    pad = SPREAD * OVER + OVER
    cells = []
    for ch in CHARS:
        adv = font.getlength(ch) / OVER
        box = font.getbbox(ch)            # relative to the pen at the top of the ascent
        if box[2] <= box[0] or box[3] <= box[1]:
            cells.append((ch, None, 0, 0, adv)); continue
        w = box[2] - box[0] + 2 * pad; h = box[3] - box[1] + 2 * pad
        w += (-w) % OVER; h += (-h) % OVER
        m = Image.new("L", (w, h), 0); ImageDraw.Draw(m).text((pad - box[0], pad - box[1]), ch, font=font, fill=255)
        a = np.array(m) >= 128
        d = distance_transform_edt(~a) - distance_transform_edt(a)       # + outside, - inside (big px)
        d = d.reshape(h // OVER, OVER, w // OVER, OVER).mean(axis=(1, 3)) / OVER   # atlas px
        v = np.clip(0.5 - d / (2 * SPREAD), 0, 1)                         # 0.5 at the edge, 1 inside
        img = Image.fromarray((v * 255 + 0.5).astype(np.uint8))
        ox = (box[0] - pad) / OVER; oy = (box[1] - pad) / OVER - asc / OVER    # from the pen at the baseline
        cells.append((ch, img, ox, oy, adv))
    order = sorted([c for c in cells if c[1] is not None], key=lambda c: -c[1].height)
    x = y = shelf = 0; placed = {}
    for c in order:
        im = c[1]
        if x + im.width + 1 > W: x = 0; y += shelf + 1; shelf = 0
        placed[id(c)] = (x, y); x += im.width + 1; shelf = max(shelf, im.height)
    H = 1 << math.ceil(math.log2(y + shelf + 1))
    alpha = Image.new("L", (W, H), 0)
    for c in order: alpha.paste(c[1], placed[id(c)])
    atlas = Image.merge("RGBA", (Image.new("L", (W, H), 255),) * 3 + (alpha,))
    name = "HudSDF%d" % weight
    atlas.save(os.path.join(ORB, "Textures", "Tantra", name + ".dds"))
    with open(os.path.join(ORB, "Config", "Tantra", name + ".txt"), "w", encoding="ascii", newline="\n") as f:
        f.write("# OrbiterCrew helmet display, signed-distance glyphs (Jura %d, SIL OFL 1.1): codepoint x y w h ox oy advance, atlas px\n" % weight)
        f.write("SDF %d %d\n" % (EM, SPREAD))
        for c in cells:
            ch, im, ox, oy, adv = c
            px, py = placed.get(id(c), (0, 0))
            f.write("%d %d %d %d %d %.2f %.2f %.2f\n" % (ord(ch), px, py, im.width if im else 0, im.height if im else 0, ox, oy, adv))
    print("%s: %dx%d, %d glyphs" % (name, W, H, len(cells)))

for wt in (600, 700):
    build(wt)
