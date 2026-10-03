"""The lift's control panels as real hardware (2026-10-03, the user: "real buttons, not game ones; a serious monitor").

in_btncaps.dds (512 x 512): the caps of the illuminated push buttons, 40 mm square, the legend ON the cap (aircraft / ISS style).
  Cells of 128 px: row = button, column 0 = lit, 1 = dark. Buttons: 0..2 the lift zone (СКАФ / ДАВЛ / ВЫЗОВ), 3..5 the cabin
  (ВНИЗ / ВВЕРХ / ВЫХОД). Lit: a frosted cap with the legend glowing in its colour; dark: a grey cap, the legend engraved.
in_liftpanel.dds, in_cabpanel.dds (1024 x 512 / 512 x 512): anodised dark grey plates, white engraved legends and frames,
  the placard. The layouts match gen_mesh (the lift zone: buttons u 0.25 / 0.50 / 0.75 at v 0.60; the cabin: CAB_PANEL).
Run:  python make_lift_textures.py
"""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
FB = "C:/Windows/Fonts/arialbd.ttf"
FN = "C:/Windows/Fonts/arial.ttf"
BUTTONS = [("ПРОВ", "СКАФ", (235, 235, 225)), ("ДАВЛ", "ШЛЮЗ", (235, 235, 225)), ("ВЫЗОВ", "", (120, 230, 140)),
           ("ВНИЗ", "", (120, 230, 140)), ("ВВЕРХ", "", (235, 235, 225)), ("ВЫХОД", "", (255, 190, 80))]
CELL = 128


def font(path, size):
    return ImageFont.truetype(path, int(size))


def write_dds(path, im):
    a = np.asarray(im.convert("RGB"))
    h, w, _ = a.shape
    header = bytearray(128)
    header[0:4] = b"DDS "
    for off, v in {4: 124, 8: 0x100F, 12: h, 16: w, 20: w * 4, 76: 32, 80: 0x41, 88: 32, 92: 0x00FF0000, 96: 0x0000FF00,
                   100: 0x000000FF, 104: 0xFF000000, 108: 0x1000}.items():
        header[off:off + 4] = int(v).to_bytes(4, "little")
    bgra = np.empty((h, w, 4), np.uint8)
    bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3] = a[..., 2], a[..., 1], a[..., 0], 255
    with open(path, "wb") as f:
        f.write(header)
        f.write(bgra.tobytes())


def caps():
    im = Image.new("RGB", (512, 512), (40, 42, 46))
    for k, (l1, l2, col) in enumerate(BUTTONS):
        for lit in (1, 0):
            x0, y0 = (0 if lit else 1) * CELL + (2 * CELL if k >= 4 else 0), (k % 4) * CELL
            cell = Image.new("RGB", (CELL, CELL))
            d = ImageDraw.Draw(cell)
            base = (34, 36, 38) if lit else (58, 60, 63)                  # lit: a dark frosted cap; dark: a grey one
            d.rectangle([0, 0, CELL - 1, CELL - 1], fill=base)
            for i in range(10):                                          # the cap's bevel
                c = tuple(int(v + (14 - i * 1.4) * (1 if lit == 0 else 0.6)) for v in base)
                d.rectangle([i, i, CELL - 1 - i, CELL - 1 - i], outline=c)
            tc = col if lit else (24, 25, 27)
            sz = 30 if l2 else 34
            while sz > 12 and max(ImageDraw.Draw(cell).textlength(t_, font=font(FB, sz)) for t_ in (l1, l2)) > CELL * 0.8:
                sz -= 1
            if l2:
                d.text((CELL / 2, CELL / 2 - 17), l1, font=font(FB, sz), fill=tc, anchor="mm")
                d.text((CELL / 2, CELL / 2 + 18), l2, font=font(FB, sz), fill=tc, anchor="mm")
            else:
                d.text((CELL / 2, CELL / 2), l1, font=font(FB, sz), fill=tc, anchor="mm")
            if lit:                                                      # the glow of the backlit legend
                g = cell.filter(ImageFilter.GaussianBlur(4))
                cell = Image.blend(cell, g, 0.35)
                d = ImageDraw.Draw(cell)
                if l2:
                    d.text((CELL / 2, CELL / 2 - 17), l1, font=font(FB, sz), fill=col, anchor="mm")
                    d.text((CELL / 2, CELL / 2 + 18), l2, font=font(FB, sz), fill=col, anchor="mm")
                else:
                    d.text((CELL / 2, CELL / 2), l1, font=font(FB, sz), fill=col, anchor="mm")
            im.paste(cell, (x0, y0))
    return im


def cap_uv(k, lit):
    """The atlas rectangle (u0, v0, u1, v1) of button k, lit or dark."""
    x0 = (0 if lit else 1) * CELL + (2 * CELL if k >= 4 else 0)
    y0 = (k % 4) * CELL
    return x0 / 512, y0 / 512, (x0 + CELL) / 512, (y0 + CELL) / 512


def plate(W, H):
    """Anodised dark grey with a faint brushed grain."""
    rng = np.random.default_rng(3)
    a = np.empty((H, W, 3), float)
    a[:] = (44, 47, 51)
    a += rng.normal(0, 1.6, (H, 1, 1))
    a += rng.normal(0, 1.0, (H, W, 1))
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))


ENGR = (205, 208, 210)


def engrave_box(d, x0, y0, x1, y1, w=2):
    d.rectangle([x0, y0, x1, y1], outline=ENGR, width=w)


def zone_panel():
    """The lift zone's panel, 1024 x 512 on 1.8 x 0.9 m: the caps sit at u 0.25 / 0.50 / 0.75, v 0.60."""
    W, H = 1024, 512
    im = plate(W, H)
    d = ImageDraw.Draw(im)
    d.rectangle([6, 6, W - 7, H - 7], outline=(78, 82, 88), width=6)
    d.text((40, 52), "ШЛЮЗ 1  ·  ЛИФТ НАРУЖУ", font=font(FB, 40), fill=ENGR, anchor="lm")
    d.line([(40, 82), (W - 40, 82)], fill=ENGR, width=2)
    d.text((40, 112), "ПАНЕЛЬ ЗОНЫ ЛИФТА   П-1.2", font=font(FN, 24), fill=(150, 154, 158), anchor="lm")
    for u, lab in ((0.25, "ПРОВЕРКА СКАФАНДРА"), (0.50, "ДАВЛЕНИЕ ШЛЮЗА"), (0.75, "ВЫЗОВ КАБИНЫ")):
        cx, cy = int(u * W), int(0.60 * H)
        hb = int((0.020 + 0.007 + 0.010) * W / 1.8)                      # the button's bay round the cap and bezel (gen_mesh ZONE_CAP_H)
        engrave_box(d, cx - hb, cy - hb, cx + hb, cy + hb, 2)
        d.text((cx, cy + hb + 26), lab, font=font(FB, 18), fill=ENGR, anchor="mm")
    d.rectangle([40, H - 70, W - 40, H - 30], fill=(150, 112, 30))
    d.text((W / 2, H - 50), "ВЫХОД НАРУЖУ - ТОЛЬКО В СКАФАНДРЕ, КРОМЕ ПРИГОДНОЙ АТМОСФЕРЫ", font=font(FB, 22), fill=(20, 18, 14), anchor="mm")
    return im


def cab_panel(scr, btn_u, btn_v, width_m=1.4):
    """The cabin's panel, 512 x 512: the screen bay (scr = u0, v0, u1, v1), the buttons' bays, the legends, the placard."""
    S = 512
    im = plate(S, S)
    d = ImageDraw.Draw(im)
    d.rectangle([4, 4, S - 5, S - 5], outline=(78, 82, 88), width=5)
    d.text((22, 22), "КАБИНА ЛИФТА  Л-1", font=font(FB, 22), fill=ENGR, anchor="lm")
    u0, v0, u1, v1 = scr
    d.rectangle([int(u0 * S) - 8, int(v0 * S) - 8, int(u1 * S) + 8, int(v1 * S) + 8], fill=(10, 11, 12), outline=(96, 100, 106), width=4)
    for u, lab in zip(btn_u, ("СПУСК", "ПОДЪЁМ", "ВЫХОД НА ГРУНТ")):
        cx, cy = int(u * S), int(btn_v * S)
        hb = int((0.030 + 0.007 + 0.010) * S / width_m)                   # the button's bay (gen_mesh CAB_CAP_H)
        engrave_box(d, cx - hb, cy - hb, cx + hb, cy + hb, 2)
        d.text((cx, cy + hb + 18), lab, font=font(FB, 17), fill=ENGR, anchor="mm")
    d.rectangle([14, S - 52, S - 14, S - 20], fill=(150, 112, 30))
    d.text((S / 2, S - 36), "СПУСК - ПОСЛЕ ЗАКРЫТИЯ ДВЕРИ ШЛЮЗА", font=font(FB, 16), fill=(20, 18, 14), anchor="mm")
    return im


if __name__ == "__main__":
    import sys
    sys.path.insert(0, os.path.dirname(__file__))
    out = os.path.join(ROOT, "Textures", "Tantra")
    os.makedirs(out, exist_ok=True)
    write_dds(os.path.join(out, "in_btncaps.dds"), caps())
    write_dds(os.path.join(out, "in_liftpanel.dds"), zone_panel())
    from gen_mesh import CAB_PANEL                                       # the cabin panel's layout lives in the generator
    write_dds(os.path.join(out, "in_cabpanel.dds"), cab_panel(CAB_PANEL["scr"], CAB_PANEL["btn_u"], CAB_PANEL["btn_v"], CAB_PANEL["x1"] - CAB_PANEL["x0"]))
    if "--preview" in sys.argv:
        caps().save(os.path.join(os.path.dirname(__file__), "..", "build", "btncaps_preview.png"))
        zone_panel().save(os.path.join(os.path.dirname(__file__), "..", "build", "liftpanel_preview.png"))
        cab_panel(CAB_PANEL["scr"], CAB_PANEL["btn_u"], CAB_PANEL["btn_v"], CAB_PANEL["x1"] - CAB_PANEL["x0"]).save(os.path.join(os.path.dirname(__file__), "..", "build", "cabpanel_preview.png"))
    print("lift textures written")
