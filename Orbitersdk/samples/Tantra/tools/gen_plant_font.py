# The glyph atlas of the power plant screen (orbiter2016/TantraPlantScreen.cpp): Segoe UI exactly as the mockup draws it
# (Tantra_Design/tantra_plant_screen.html: "<weight> <size>px Segoe UI"), at the very pixel sizes the screen uses (it draws in
# the mockup's own pixels - no scaling). White glyphs, the coverage in alpha: the screen tints them to the mockup's colours.
# CSS weight matching on Windows: 500 -> Segoe UI regular (no medium face), 600 semibold, 700 bold, 800 -> black.
# Writes Textures/Tantra/PlantFont.dds (A8R8G8B8) and Config/Tantra/PlantFont.txt. Run: python tools/gen_plant_font.py
import os
import struct

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))   # the Orbiter folder
FONTS = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts")
FACE = {500: "segoeui.ttf", 600: "seguisb.ttf", 700: "segoeuib.ttf", 800: "seguibl.ttf"}
# (px, css weight): every font the screen asks for
SPECS = [(10, 500), (11, 500), (11, 700), (12, 500), (12, 600), (12, 700), (13, 500), (13, 600), (13, 700), (14, 500),
         (14, 600), (14, 700), (15, 700), (16, 500), (16, 700), (20, 700), (30, 700), (46, 800)]
CHARS = ([chr(c) for c in range(32, 127)] + [chr(c) for c in range(0x410, 0x450)] + ["Ё", "ё"] +
         list("·—–→×°²³¹₀μΔ«»≈±−…№") + ["\u00a0", "\u202f"])
W = 1024
PAD = 2


def main():
    glyphs = []                                       # (spec, char, image or None, ox, oy, adv)
    for si, (px, wt) in enumerate(SPECS):
        font = ImageFont.truetype(os.path.join(FONTS, FACE[wt]), px)
        for ch in CHARS:
            adv = font.getlength(ch)
            if ch in ("\u00a0", "\u202f"):
                adv = font.getlength(" ") if ch == "\u00a0" else font.getlength(" ") * 0.6
            x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
            img = None
            if x1 > x0 and y1 > y0 and not ch.isspace() and ch not in ("\u00a0", "\u202f"):
                img = Image.new("L", (x1 - x0, y1 - y0), 0)
                ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            glyphs.append((si, ch, img, x0, y0, adv))
    # shelf packing, the tallest first
    order = sorted(range(len(glyphs)), key=lambda i: -(glyphs[i][2].size[1] if glyphs[i][2] else 0))
    pos = {}
    x = y = shelf = 0
    for i in order:
        img = glyphs[i][2]
        if img is None:
            continue
        w, h = img.size
        if x + w + PAD > W:
            x, y, shelf = 0, y + shelf + PAD, 0
        pos[i] = (x, y)
        x += w + PAD
        shelf = max(shelf, h)
    H = 1
    while H < y + shelf + PAD:
        H *= 2
    atlas = Image.new("RGBA", (W, H), (255, 255, 255, 0))
    for i, (x, y) in pos.items():
        a = glyphs[i][2]
        white = Image.new("RGBA", a.size, (255, 255, 255, 255))
        white.putalpha(a)
        atlas.paste(white, (x, y))
    tex = os.path.join(ROOT, "Textures", "Tantra", "PlantFont.dds")
    write_dds(tex, atlas)
    cfg = os.path.join(ROOT, "Config", "Tantra", "PlantFont.txt")
    with open(cfg, "w", encoding="ascii") as f:
        f.write("# The plant screen's glyph atlas (tools/gen_plant_font.py): Segoe UI, white, tinted on the screen\n")
        f.write("# S <spec> <px> <css weight> | <spec> <codepoint> <x> <y> <w> <h> <ox> <oy> <advance> (ox, oy from the pen at the baseline)\n")
        for si, (px, wt) in enumerate(SPECS):
            f.write(f"S {si} {px} {wt}\n")
        for i, (si, ch, img, x0, y0, adv) in enumerate(glyphs):
            x, y = pos.get(i, (0, 0))
            w, h = img.size if img else (0, 0)
            f.write(f"{si} {ord(ch)} {x} {y} {w} {h} {x0} {y0} {adv:.3f}\n")
    print(f"{len(glyphs)} glyphs, atlas {W} x {H} -> {tex}, {cfg}")


def write_dds(path, img):
    w, h = img.size
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000            # CAPS HEIGHT WIDTH PITCH PIXELFORMAT
    hdr = b"DDS " + struct.pack("<7I", 124, flags, h, w, w * 4, 0, 0) + b"\0" * 44
    hdr += struct.pack("<8I", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)   # RGB + alpha, A8R8G8B8
    hdr += struct.pack("<5I", 0x1000, 0, 0, 0, 0)       # TEXTURE
    with open(path, "wb") as f:
        f.write(hdr + img.tobytes("raw", "BGRA"))


if __name__ == "__main__":
    main()
