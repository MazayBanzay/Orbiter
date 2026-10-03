# OrbiterCrew: glyph atlas for the suit computer's helmet display (Cyrillic on any Windows, any graphics client).
# Glyphs come pre-coloured (one set per size and colour) with a soft holographic glow baked in; the module blits them.
# Font: Jura (SIL Open Font License 1.1, Tantra_Design/fonts/OFL.txt): free to redistribute with the addon.
# Sizes are in display units (the helmet display is laid out on 1280x720 units) and baked at 2 px per unit.
# Output: Textures\Tantra\HudFont.dds (uncompressed RGBA) and Config\Tantra\HudFont.txt (glyph table).
# Run with the system Python (Pillow, numpy).
import os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ORB = r"C:\Games\Orbiter 2016"
FONT = os.path.join(ORB, "Tantra_Design", "fonts", "Jura.ttf")
CHARS = "".join(chr(c) for c in range(32, 127)) + "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя" + "°…±·—–«»№×−²₂↑↓←→▲▶◀▸◂✓≈≤≥Δ"
UNIT = 2                                              # atlas px per display unit
SIZES = [(9.5, 600), (11, 600), (13, 600), (15, 700)]  # 0 tiny, 1 small, 2 mid, 3 big: display units, weight
# 0 cyan, 1 orange, 2 white, 3 red, 4 dim cyan, 5 dim orange, 6 dim grey (the three palettes pick from these)
COLORS = [(70, 226, 255), (255, 148, 40), (242, 248, 255), (255, 70, 70), (94, 142, 165), (163, 123, 82), (125, 141, 156)]
PAD, W = 5, 2048

cells = []   # (size, colour, char, image, ox, oy, advance): ox/oy = top-left of the image from the pen at the baseline
for si, (units, weight) in enumerate(SIZES):
    font = ImageFont.truetype(FONT, round(units * UNIT)); font.set_variation_by_axes([weight])
    asc, desc = font.getmetrics(); h = asc + desc + 2 * PAD
    for ch in CHARS:
        adv = font.getlength(ch)
        w = int(math.ceil(adv)) + 2 * PAD + 4
        mask = Image.new("L", (w, h), 0); ImageDraw.Draw(mask).text((PAD, PAD), ch, font=font, fill=255)
        glow = np.array(mask.filter(ImageFilter.GaussianBlur(2.4))) * 0.55
        a = np.maximum(np.array(mask), glow).astype(np.uint8)
        box = Image.fromarray(a).getbbox()
        for ci, rgb in enumerate(COLORS):
            if not box:   # space
                cells.append((si, ci, ch, None, 0, 0, adv)); continue
            crop = Image.fromarray(a).crop(box)
            img = Image.new("RGBA", crop.size, rgb + (0,)); img.putalpha(crop)
            cells.append((si, ci, ch, img, box[0] - PAD, box[1] - PAD - asc, adv))

# shelf packing, tallest first
order = sorted([c for c in cells if c[3] is not None], key=lambda c: -c[3].height)
x = y = shelf = 0; placed = {}
for c in order:
    img = c[3]
    if x + img.width + 1 > W: x = 0; y += shelf + 1; shelf = 0
    placed[id(c)] = (x, y); x += img.width + 1; shelf = max(shelf, img.height)
H = 1 << math.ceil(math.log2(y + shelf + 1))
atlas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
for c in order: atlas.paste(c[3], placed[id(c)])
os.makedirs(os.path.join(ORB, "Textures", "Tantra"), exist_ok=True)
atlas.save(os.path.join(ORB, "Textures", "Tantra", "HudFont.dds"))
with open(os.path.join(ORB, "Config", "Tantra", "HudFont.txt"), "w", encoding="ascii", newline="\n") as f:
    f.write("# OrbiterCrew helmet display glyphs (Jura, SIL OFL 1.1): size colour codepoint x y w h ox oy advance, in atlas px at %d px per display unit\n" % UNIT)
    f.write("ATLAS %d %d %d\n" % (W, H, UNIT))
    for c in cells:
        si, ci, ch, img, ox, oy, adv = c
        px, py = placed.get(id(c), (0, 0))
        f.write("%d %d %d %d %d %d %d %d %d %.2f\n" % (si, ci, ord(ch), px, py, img.width if img else 0, img.height if img else 0, ox, oy, adv))
print("atlas %dx%d, %d glyphs" % (W, H, len(cells)))
