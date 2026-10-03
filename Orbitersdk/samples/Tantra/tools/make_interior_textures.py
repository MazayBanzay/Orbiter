"""Procedural textures of the Tantra interior (Textures/Tantra/in_*.dds, 512x512, tileable, 32-bit DDS).
Dark working look: deck plates, wall panels, ceiling panels, hazard trim, brushed metal. The mesh generator maps them with a planar
projection by the face normal (gen_mesh.py, _uv_project): floor/ceiling 2 m per tile, walls 2 m per tile.
Run:  python make_interior_textures.py
"""
import os
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))   # the Orbiter 2016 folder
N = 512
rng = np.random.default_rng(7)


def noise(amp):
    n = rng.normal(0.0, amp, (N, N))
    k = np.ones(3) / 3.0
    n = np.apply_along_axis(lambda r: np.convolve(r, k, "same"), 0, n)                   # slight smoothing: dust, not static
    return n


def base(rgb, amp=3.0):
    t = np.empty((N, N, 3), float)
    t[:] = rgb
    return t + noise(amp)[..., None]


def line_h(t, y, w, d):
    t[max(0, y - w // 2):y - w // 2 + w, :, :] += d


def line_v(t, x, w, d):
    t[:, max(0, x - w // 2):x - w // 2 + w, :] += d


def bolts(t, pts, r=4, d=18):
    yy, xx = np.mgrid[0:N, 0:N]
    for (px, py) in pts:
        for ox in (-N, 0, N):
            for oy in (-N, 0, N):
                m = (xx - px - ox) ** 2 + (yy - py - oy) ** 2 < r * r
                t[m] += d


def bevel(t, x0, y0, x1, y1, w=3, d=14):
    """Light top/left edges and dark bottom/right edges of a panel: a seam with relief."""
    t[y0:y0 + w, x0:x1] += d
    t[y0:y1, x0:x0 + w] += d
    t[y1 - w:y1, x0:x1] -= d
    t[y0:y1, x1 - w:x1] -= d


def floor():
    t = base((52, 54, 58), 2.5)
    P = N // 2                                                                             # plates of 1 m
    for i in range(2):
        for j in range(2):
            x0, y0 = i * P, j * P
            bevel(t, x0 + 4, y0 + 4, x0 + P - 4, y0 + P - 4, 3, 12)
            for k in range(14, P - 14, 12):                                               # non-slip ribs
                t[y0 + k:y0 + k + 3, x0 + 14:x0 + P - 14] += 7
                t[y0 + k + 3:y0 + k + 5, x0 + 14:x0 + P - 14] -= 6
    for v in (0, P):
        line_h(t, v, 5, -26)
        line_v(t, v, 5, -26)
    bolts(t, [(14, 14), (P - 14, 14), (14, P - 14), (P - 14, P - 14), (P + 14, 14), (N - 14, 14), (P + 14, P - 14), (N - 14, P - 14),
              (14, P + 14), (P - 14, P + 14), (14, N - 14), (P - 14, N - 14), (P + 14, P + 14), (N - 14, P + 14), (P + 14, N - 14), (N - 14, N - 14)], 4, 16)
    return t


def wall():
    t = base((92, 98, 104), 2.5)                                                           # 2 m x 2 m: panels 0.5 m x 1.0 m
    W, H = N // 4, N // 2
    for i in range(4):
        for j in range(2):
            bevel(t, i * W + 2, j * H + 2, (i + 1) * W - 2, (j + 1) * H - 2, 3, 13)
    for v in range(0, N, W):
        line_v(t, v, 3, -34)
    for v in range(0, N, H):
        line_h(t, v, 3, -34)
    t[N - 70:N, :, :] *= 0.72                                                              # kick strip along the floor
    t[N - 72:N - 68, :, :] -= 20
    t[N // 2 - 40:N // 2 - 34, :, :] += 10                                                 # a handrail line at about 1 m
    bolts(t, [(x, y) for x in range(14, N, W) for y in (14, H - 14, H + 14, N - 14)], 3, 15)
    return t


def ceiling():
    t = base((70, 74, 80), 2.0)
    P = N // 2                                                                             # panels 1 m x 1 m
    for i in range(2):
        for j in range(2):
            bevel(t, i * P + 3, j * P + 3, (i + 1) * P - 3, (j + 1) * P - 3, 3, 10)
            for k in range(8):                                                             # ventilation slots
                t[j * P + 190 + k * 8:j * P + 194 + k * 8, i * P + 90:i * P + 170] -= 18
    line_h(t, 0, 4, -30); line_h(t, P, 4, -30)
    line_v(t, 0, 4, -30); line_v(t, P, 4, -30)
    return t


def trim():
    t = np.zeros((N, N, 3), float)
    yy, xx = np.mgrid[0:N, 0:N]
    stripe = (((xx + yy) // 64) % 2) == 0
    t[stripe] = (200, 150, 30)
    t[~stripe] = (28, 28, 30)
    return t + noise(2.0)[..., None]


def metal():
    t = base((98, 103, 110), 1.5)
    for y in range(0, N, 2):                                                               # brushed along u
        t[y:y + 1, :, :] += rng.normal(0, 4.5)
    bevel(t, 2, 2, N - 2, N - 2, 4, 10)
    return t


def write_dds(path, rgb):
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
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


# ---- light decals (RGBA, 256x256): alpha-blended over the surfaces, self-lit; they paint the light of the watch lamps
M = 256


def _uv():
    v, u = np.mgrid[0:M, 0:M]
    return (u + 0.5) / M, (v + 0.5) / M


def _rgba(rgb, a):
    t = np.empty((M, M, 4), float)
    t[..., :3] = rgb
    t[..., 3] = np.clip(a, 0, 1) * 255.0
    return t


def _smooth(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def fx_pool(rgb, peak):
    """Pool of light on the floor under a downlight: bright core, soft long falloff."""
    u, v = _uv()
    r = np.hypot(u - 0.5, v - 0.5) * 2.0
    a = peak * (0.65 * (1 - _smooth(r / 0.45)) + 0.35 * (1 - _smooth(r)) ** 2)
    return _rgba(rgb, a)


def fx_wash():
    """Scallop of a downlight on the wall next to it: v = 0 at the ceiling. The cone cuts the wall in a hyperbola: narrow
    near the ceiling, widening and fading downwards."""
    u, v = _uv()
    halfw = 0.06 + 0.42 * np.sqrt(np.clip(v, 0, 1))
    a = 0.42 * np.exp(-((u - 0.5) / halfw) ** 2 * 2.2) * _smooth(v / 0.08) * (1 - v) ** 1.6
    return _rgba((255, 222, 176), a)


def fx_ao():
    """Contact shadow: v = 0 at the corner."""
    u, v = _uv()
    return _rgba((0, 0, 0), 0.62 * (1 - v) ** 2.2)


def fx_cove():
    """The cove strip washes the top of the wall: v = 0 at the ceiling."""
    u, v = _uv()
    return _rgba((150, 185, 240), 0.2 * (1 - v) ** 2.4)


def fx_glow(rgb, peak):
    u, v = _uv()
    r = np.hypot((u - 0.5) * 2.0, (v - 0.5) * 2.0)
    return _rgba(rgb, peak * (1 - _smooth(r)) ** 2)


def write_dds_rgba(path, rgba):
    rgba = np.clip(rgba, 0, 255).astype(np.uint8)
    h, w, _ = rgba.shape
    header = bytearray(128)
    header[0:4] = b"DDS "
    vals = {4: 124, 8: 0x100F, 12: h, 16: w, 20: w * 4, 76: 32, 80: 0x41, 88: 32,
            92: 0x00FF0000, 96: 0x0000FF00, 100: 0x000000FF, 104: 0xFF000000, 108: 0x1000}
    for off, v in vals.items():
        header[off:off + 4] = int(v).to_bytes(4, "little")
    bgra = np.empty((h, w, 4), np.uint8)
    bgra[..., 0], bgra[..., 1], bgra[..., 2], bgra[..., 3] = rgba[..., 2], rgba[..., 1], rgba[..., 0], rgba[..., 3]
    with open(path, "wb") as f:
        f.write(header)
        f.write(bgra.tobytes())


FX = (("in_fx_pool", lambda: fx_pool((255, 224, 178), 0.55)), ("in_fx_amber", lambda: fx_pool((255, 176, 92), 0.5)),
      ("in_fx_wash", fx_wash), ("in_fx_ao", fx_ao), ("in_fx_cove", fx_cove), ("in_fx_glow", lambda: fx_glow((90, 205, 255), 0.42)))


def cab_panel():
    """The lift cabin's panel (512 x 512): title, the screen's recess (the screen is drawn over it in the game), three big buttons
    with big labels. Layout = gen_mesh CAB_PANEL: screen u 0.08..0.92 v 0.10..0.46, buttons u 0.20 / 0.50 / 0.80 at v 0.64."""
    from PIL import Image, ImageDraw, ImageFont
    S = 512
    im = Image.new("RGB", (S, S), (26, 29, 34))
    d = ImageDraw.Draw(im)
    fnt = lambda sz: ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", sz)
    d.rectangle([4, 4, S - 5, S - 5], outline=(70, 76, 84), width=5)
    d.text((S / 2, 27), "КАБИНА ЛИФТА", font=fnt(34), fill=(220, 180, 110), anchor="mm")
    d.rectangle([int(0.08 * S) - 6, int(0.10 * S) - 6, int(0.92 * S) + 6, int(0.46 * S) + 6], fill=(6, 8, 10), outline=(60, 66, 74), width=3)
    for u, lab, col in ((0.20, "ВНИЗ", (110, 230, 140)), (0.50, "ВВЕРХ", (110, 200, 240)), (0.80, "ВЫХОД", (240, 180, 90))):
        cx, cy = int(u * S), int(0.64 * S)
        d.ellipse([cx - 52, cy - 52, cx + 52, cy + 52], outline=col, width=5)
        d.text((cx, int(0.82 * S)), lab, font=fnt(34), fill=col, anchor="mm")
    d.text((S / 2, int(0.94 * S)), "в вакууме - только в скафандре", font=fnt(22), fill=(160, 160, 160), anchor="mm")
    return np.asarray(im, float)


if __name__ == "__main__":
    out = os.path.join(ROOT, "Textures", "Tantra")
    os.makedirs(out, exist_ok=True)
    for name, fn in (("in_floor", floor), ("in_wall", wall), ("in_ceiling", ceiling), ("in_trim", trim), ("in_metal", metal)):
        write_dds(os.path.join(out, name + ".dds"), fn())
    write_dds(os.path.join(out, "in_cabpanel.dds"), cab_panel())
    for name, fn in FX:
        write_dds_rgba(os.path.join(out, name + ".dds"), fn())
    print("textures written to", out)
