# The technical point's textures (procedural, tileable, original) -> Textures\TVehicles\tp_*.dds with the D3D9Client / D3D11
# per-pixel maps: <name>.dds colour, <name>_norm.dds tangent-space normal (+x along u, +y along v), <name>_spec.dds (RGB
# highlight, A = power/4). No baked shading: the client's lights, shadows and AO do that. The mesh (build_techpoint.py)
# maps them by a planar projection on the face normal: tp_panel 3 m a tile (2 x 2 panels), tp_sinter 2 m, tp_floor 1.2 m.
# The rest is the MPU's own set (mpu_lining, mpu_composite, mpu_metal, mpu_deck). System Python + numpy + Pillow.
import os
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DST = os.path.join(ROOT, "Textures", "TVehicles")
os.makedirs(DST, exist_ok=True)
N = 512
rng = np.random.default_rng(29)
yy, xx = np.mgrid[0:N, 0:N]


def pnoise(scale, amp=1.0):
    """periodic smooth noise: white noise low-passed in the Fourier domain (tiles seamlessly)"""
    f = np.fft.fft2(rng.standard_normal((N, N)))
    ky, kx = np.meshgrid(np.fft.fftfreq(N), np.fft.fftfreq(N), indexing="ij")
    f *= np.exp(-(kx ** 2 + ky ** 2) * (scale ** 2) * 40.0)
    n = np.real(np.fft.ifft2(f))
    return n / (np.abs(n).max() + 1e-9) * amp


def grooves(px, py, width):
    """1 in the grooves along every px columns and py rows (periodic)"""
    a = np.minimum(xx % px, px - xx % px)
    b = np.minimum(yy % py, py - yy % py)
    return np.clip(1.0 - np.minimum(a, b) / width, 0, 1)


def dots(pts, r):
    h = np.zeros((N, N))
    for px, py in pts:
        for ox in (-N, 0, N):
            for oy in (-N, 0, N):
                d = np.hypot(xx - px - ox, yy - py - oy)
                h = np.maximum(h, np.clip(1.0 - (d / r) ** 2, 0, 1) ** 0.5)
    return h


def normal_from(h, strength):
    gy, gx = np.gradient(np.pad(h, 1, mode="wrap"))
    gx, gy = gx[1:-1, 1:-1], gy[1:-1, 1:-1]
    n = np.stack([-gx * strength, gy * strength, np.ones_like(h)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def dds(path, rgba, normal=False):
    """an uncompressed 32-bit DDS with its full mip chain (box-filtered; normals renormalised each level), as
    make_mpu_textures.py writes them (no mips shimmered far away)"""
    arr = np.asarray(rgba, dtype=np.float32)
    if arr.shape[2] == 3: arr = np.concatenate([arr, np.full((*arr.shape[:2], 1), 255.0)], axis=2)
    levels = [np.clip(arr, 0, 255)]
    while levels[-1].shape[0] > 1 or levels[-1].shape[1] > 1:
        src = Image.fromarray(np.clip(levels[-1], 0, 255).astype(np.uint8), "RGBA")
        dst = np.asarray(src.resize((max(1, src.width // 2), max(1, src.height // 2)), Image.Resampling.BOX), dtype=np.float32)
        if normal:
            v = dst[..., :3] / 127.5 - 1.0
            v /= np.maximum(1e-6, np.linalg.norm(v, axis=2, keepdims=True))
            dst = dst.copy(); dst[..., :3] = (v * 0.5 + 0.5) * 255.0
        levels.append(dst)
    h, w = levels[0].shape[:2]
    header = bytearray(128); header[0:4] = b"DDS "
    for off, val in {4: 124, 8: 0x000A100F, 12: h, 16: w, 20: w * 4, 28: len(levels), 76: 32, 80: 0x41, 88: 32,
                     92: 0x00FF0000, 96: 0x0000FF00, 100: 0x000000FF, 104: 0xFF000000, 108: 0x401008}.items():
        header[off:off + 4] = int(val).to_bytes(4, "little")
    with open(path, "wb") as f:
        f.write(header)
        for level in levels:
            f.write(np.ascontiguousarray(np.clip(np.round(level), 0, 255).astype(np.uint8)[..., [2, 1, 0, 3]]).tobytes())


def save(name, rgb, h, strength, spec_rgb, power, smooth=None, metal=0.0):
    """+ the PBR maps the D3D11 client reads (mesh.hlsl): <name>_rghn.dds smoothness in G, <name>_metl.dds metalness in R"""
    if smooth is not None:
        sm = np.clip(np.broadcast_to(smooth, h.shape), 0, 1) * 255
        dds(os.path.join(DST, name + "_rghn.dds"), np.stack([sm, sm, sm], -1))
        mt = np.clip(np.broadcast_to(metal, h.shape), 0, 1) * 255
        dds(os.path.join(DST, name + "_metl.dds"), np.stack([mt, mt, mt], -1))
    dds(os.path.join(DST, name + ".dds"), np.clip(rgb, 0, 255))
    nrm = normal_from(h, strength)
    dds(os.path.join(DST, name + "_norm.dds"), np.clip(nrm * 0.5 + 0.5, 0, 1) * 255, normal=True)
    s = np.clip(np.broadcast_to(spec_rgb, rgb.shape), 0, 1) * 255
    a = np.clip(np.broadcast_to(power, h.shape) / 4.0, 0, 255)
    dds(os.path.join(DST, name + "_spec.dds"), np.concatenate([s, a[..., None]], 2))
    print("%-12s colour, normal, spec (with mips)" % name)


# ---- tp_panel: the facade's enamelled sandwich panels, 1.5 x 1.5 m (2 x 2 a tile of 3 m): wide joints with a sealant bead,
# a row of countersunk fasteners along each joint, a faint orange-peel of the enamel, regolith dust settled in the joints ----
P = N // 2
jt = grooves(P, P, 3.2)                                                   # the joint (~2 cm)
bead = np.clip(1.0 - np.abs(np.minimum(np.minimum(xx % P, P - xx % P), np.minimum(yy % P, P - yy % P)) - 0.0) / 1.2, 0, 1)
fp = [(x, y) for x in (9, P - 9, P + 9, N - 9) for y in range(20, N, 40)] + [(x, y) for y in (9, P - 9, P + 9, N - 9) for x in range(20, N, 40)]
fs = dots(fp, 3.0)
peel = pnoise(0.008, 1.0)
dust = np.clip(pnoise(0.18, 1.0), 0, 1)
h = -1.0 * jt + 0.35 * bead - 0.45 * fs + 0.03 * peel
col = np.empty((N, N, 3)); col[:] = (196, 192, 183)                      # the warm off-white of the Э.МПУ and the suits
col += (4 * peel + 5 * dust)[..., None]; col[..., 2] -= dust * 8
col[jt > 0.35] = col[jt > 0.35] * 0.42 + np.array([28, 26, 23])          # the dark joint with dust in it
col -= (fs * 30)[..., None]
save("tp_panel", col, h, 5.0, (0.32, 0.32, 0.31), 30.0, smooth=0.62 - 0.40 * (jt > 0.35) - 0.15 * dust, metal=0.0)   # enamel: glossy, the joints and the dust matt

# ---- tp_sinter: the plinth, the apron and the yard's kerbs: sintered regolith blocks 1.0 x 0.5 m laid in courses
# (2 x 4 a tile of 2 m), the glassy fused skin with pores, chipped arrises ----
BX, BY = N // 2, N // 4
off = ((yy // BY) % 2) * (BX // 2)                                        # the courses are offset by half a block
gx = np.minimum((xx + off) % BX, BX - (xx + off) % BX); gy = np.minimum(yy % BY, BY - yy % BY)
jt = np.clip(1.0 - np.minimum(gx, gy) / 2.2, 0, 1)
chip = np.clip(pnoise(0.03, 1.0) * 2.0 - 1.1, 0, 1) * np.clip(1.0 - np.minimum(gx, gy) / 9.0, 0, 1)   # broken edges
pores = np.clip(-pnoise(0.004, 1.0) * 3.0 - 1.6, 0, 1)
blk = pnoise(0.25, 1.0)                                                   # each block's own shade, smooth
h = -1.0 * jt - 0.6 * chip - 0.35 * pores + 0.08 * pnoise(0.02)
col = np.empty((N, N, 3)); col[:] = (108, 104, 98)                       # the fused regolith: grey with a warm cast
col += (10 * blk + 6 * pnoise(0.05))[..., None]
col[jt > 0.3] *= 0.55; col *= (1.0 - 0.3 * chip - 0.35 * pores)[..., None]
spec = np.stack([0.10 + 0.10 * (1 - pores)] * 3, -1)                    # the glassy skin glints a little
save("tp_sinter", col, h, 4.0, spec, 18.0, smooth=0.30 * (1 - pores) * (1 - 0.6 * chip), metal=0.0)   # the fused skin a little glassy

# ---- tp_floor: the rooms' floor: rubber-coated tiles 0.6 m (2 x 2 a tile of 1.2 m), fine raised studs, joints ----
P = N // 2
jt = grooves(P, P, 2.0)
sx, sy = (xx % 32) - 16, (yy % 32) - 16
stud = np.clip(1.0 - (np.hypot(sx, sy) / 5.0) ** 2, 0, 1) ** 0.5
scuff = np.clip(pnoise(0.05, 1.0), 0, 1) ** 2
h = -1.0 * jt + 0.5 * stud + 0.04 * pnoise(0.02)
col = np.empty((N, N, 3)); col[:] = (122, 128, 132)                      # a light grey elastomer (albedo ~0.2: the floor bounces the lamps' light)
col += (6 * stud - 8 * scuff + 4 * pnoise(0.2))[..., None]
col[jt > 0.3] *= 0.5
save("tp_floor", col, h, 3.5, (0.22, 0.22, 0.23), 24.0, smooth=0.55 + 0.15 * stud - 0.25 * scuff, metal=0.0)   # sealed rubber coating: the lamps glint in it   # rubber coating, the studs worn shiny
# ---- tp_lining: the rooms' wall and ceiling lining: powder-coated steel panels 0.5 m (2 x 2 a tile of 1 m) with folded
# edges, quarter-turn fasteners in the corners, a faint orange-peel; semi-gloss (the lamps' highlights run along it), the
# joints matt, the fasteners bare metal ----
P = N // 2
jt = grooves(P, P, 2.4)
fold = np.clip(1.0 - np.minimum(np.minimum(xx % P, P - xx % P), np.minimum(yy % P, P - yy % P)) / 7.0, 0, 1) - jt
fp = [(x, y) for x in (16, P - 16, P + 16, N - 16) for y in (16, P - 16, P + 16, N - 16)]
fs = dots(fp, 6.5)
slot = np.zeros((N, N))
for px, py in fp: slot[(np.abs(yy - py) < 1.2) & (np.abs(xx - px) < 4.5)] = 1.0
peel = pnoise(0.006, 1.0); scuff = np.clip(pnoise(0.08, 1.0), 0, 1) ** 3
h = -1.0 * jt + 0.35 * fold + 0.6 * fs - 0.5 * slot + 0.04 * peel
col = np.empty((N, N, 3)); col[:] = (98, 102, 102)
col += (3 * peel + 6 * scuff)[..., None]
col[jt > 0.3] *= 0.45
col += (fs * 25)[..., None]; col[slot > 0] *= 0.5
col = col * 0.0 + (206, 204, 198)                                         # 30th c.: a light ceramic-polymer lining (albedo ~0.6:
col += (3 * peel - 10 * scuff)[..., None]                                  # the rooms bounce the lamps' light, not swallow it)
col[jt > 0.3] *= 0.45
col -= (fs * 40)[..., None]; col[slot > 0] *= 0.5
save("tp_lining", col, h, 5.0, (0.25, 0.25, 0.26), 30.0, smooth=0.60 - 0.45 * (jt > 0.3) - 0.2 * scuff + 0.2 * fs, metal=np.clip(fs * 1.2, 0, 1))

# ---- tp_cnt: carbon-nanotube composite (the doors, the trim bands, the frames): a fine 2x2 twill of CNT yarn (8 mm) under
# a clear ceramic coat. Dielectric (metal 0), dark (albedo ~0.05), glossy: the lamps run along it as sharp streaks ----
T = 16
u, v = xx // T, yy // T
over = ((u + v) // 2) % 2                                                  # twill: the warp over two, under two, stepped
fx, fy = (xx % T) / T, (yy % T) / T
bulge = np.where(over == 1, np.sin(np.pi * fx), np.sin(np.pi * fy))       # each yarn crossing bulges along its own run
h = 0.5 * bulge + 0.03 * pnoise(0.01)
col = np.empty((N, N, 3)); col[:] = (30, 31, 34)
col += (np.where(over == 1, 6, -2) + 3 * pnoise(0.1))[..., None]
save("tp_cnt", col, h, 2.5, (0.30, 0.30, 0.32), 60.0, smooth=0.82 - 0.06 * (1 - bulge), metal=0.0)

# ---- tp_alloy: a titanium high-entropy alloy (the crane, the rails, the handrails, the fittings): brushed along u, bare
# metal (metal 1, F0 ~0.5 grey with a faint warm cast), the brushing scatters the highlight across the lines ----
brush = np.real(np.fft.ifft2(np.fft.fft2(rng.standard_normal((N, N))) *
                np.exp(-(np.meshgrid(np.fft.fftfreq(N), np.fft.fftfreq(N), indexing="ij")[1] ** 2) * 4000.0)))   # long along u
brush /= np.abs(brush).max() + 1e-9
mar = np.clip(pnoise(0.06, 1.0), 0, 1) ** 2                                # handling marks
h = 0.25 * brush + 0.02 * pnoise(0.01)
col = np.empty((N, N, 3)); col[:] = (176, 172, 166)
col += (10 * brush - 14 * mar)[..., None]
save("tp_alloy", col, h, 1.5, (0.6, 0.6, 0.6), 80.0, smooth=0.70 + 0.08 * brush - 0.25 * mar, metal=1.0)
print("written to", DST)

# ---- tp_signs: the plates' atlas (build_techpoint.py writes techpoint_signs.txt: text, plate w, h in m): one row each, red
# letters (Jura, OFL) on white enamel with a red border, as the signs at a plant. tp_screen: the screens' placeholder ----
from PIL import ImageDraw, ImageFont
rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(os.path.dirname(__file__), "techpoint_signs.txt"), encoding="utf-8") if l.strip()]
W, RH = 1024, 1024 // len(rows)
img = Image.new("RGB", (W, RH * len(rows)), (214, 211, 203)); d = ImageDraw.Draw(img)
FONT = os.path.join(ROOT, "Tantra_Design", "fonts", "Jura.ttf")
for k, (txt, w, h) in enumerate(rows):
    y0 = k * RH
    d.rectangle([4, y0 + 4, W - 5, y0 + RH - 5], outline=(168, 30, 24), width=5)
    aspect = float(w) / float(h)                              # the plate's own aspect: the row is stretched to it on the mesh
    sx = (W / RH) / aspect                                    # draw the letters wider by this, so they come out true
    size = int(RH * 0.62)
    f = ImageFont.truetype(FONT, size)
    tw = d.textlength(txt, font=f)
    layer = Image.new("RGBA", (int(tw) + 8, RH), (0, 0, 0, 0)); ld = ImageDraw.Draw(layer)
    ld.text((4, RH * 0.14), txt, font=f, fill=(168, 30, 24, 255))
    tgt_w = min(int((tw + 8) * sx), W - 40)
    layer = layer.resize((tgt_w, RH))
    img.paste(layer, ((W - tgt_w) // 2, y0), layer)
arr = np.asarray(img, dtype=np.float64)
dds(os.path.join(DST, "tp_signs.dds"), arr)
dds(os.path.join(DST, "tp_screen.dds"), np.full((8, 8, 3), 12.0))
print("tp_signs     %d plates; tp_screen" % len(rows))
