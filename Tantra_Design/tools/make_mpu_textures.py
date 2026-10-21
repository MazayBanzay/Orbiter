# МПУ textures (procedural, tileable, original) -> Textures\MPU\*.dds with D3D9Client per-pixel maps:
#   <name>.dds colour, <name>_norm.dds tangent-space normal (+x along u, +y along v), <name>_spec.dds (RGB highlight, A = power/4).
# The mesh (build_mpu.py) maps them by a planar projection on the face normal: 1 m per tile (composite, deck, white panels),
# 0.5 m (metal, rubber).  System Python + numpy + Pillow.
import os
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DST = os.path.join(ROOT, "Textures", "MPU")
os.makedirs(DST, exist_ok=True)
N = 512
rng = np.random.default_rng(11)
yy, xx = np.mgrid[0:N, 0:N]


def pnoise(scale, amp=1.0):
    """periodic smooth noise: white noise low-passed in the Fourier domain (tiles seamlessly)"""
    f = np.fft.fft2(rng.standard_normal((N, N)))
    ky, kx = np.meshgrid(np.fft.fftfreq(N), np.fft.fftfreq(N), indexing="ij")
    f *= np.exp(-(kx ** 2 + ky ** 2) * (scale ** 2) * 40.0)
    n = np.real(np.fft.ifft2(f))
    return n / (np.abs(n).max() + 1e-9) * amp


def aniso(sx, sy, amp=1.0):
    f = np.fft.fft2(rng.standard_normal((N, N)))
    ky, kx = np.meshgrid(np.fft.fftfreq(N), np.fft.fftfreq(N), indexing="ij")
    f *= np.exp(-(kx * sx) ** 2 * 40.0 - (ky * sy) ** 2 * 40.0)
    n = np.real(np.fft.ifft2(f))
    return n / (np.abs(n).max() + 1e-9) * amp


def seams(period, width):
    """grooves along the tile edges and every 'period' px: 1 inside the groove"""
    a = np.minimum(xx % period, period - xx % period)
    b = np.minimum(yy % period, period - yy % period)
    return np.clip(1.0 - np.minimum(a, b) / width, 0, 1)


def bolts(pts, r):
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
    """an uncompressed 32-bit DDS with its full mip chain (box-filtered; normals renormalised each level). PIL wrote no
    mips: far away and at grazing angles the GPU sampled the full-size fine weaves every few texels - they shimmered and
    flickered as the camera moved (the user, 2026-10-19: «мерцание этих текстур»). As build_battery_cell.py writes them"""
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


def save(name, rgb, h, strength, spec_rgb, power):
    dds(os.path.join(DST, name + ".dds"), np.clip(rgb, 0, 255))
    nrm = normal_from(h, strength)
    dds(os.path.join(DST, name + "_norm.dds"), np.clip(nrm * 0.5 + 0.5, 0, 1) * 255, normal=True)
    s = np.clip(np.broadcast_to(spec_rgb, rgb.shape), 0, 1) * 255
    a = np.clip(np.broadcast_to(power, h.shape) / 4.0, 0, 255)
    dds(os.path.join(DST, name + "_spec.dds"), np.concatenate([s, a[..., None]], 2))
    print("%-16s colour, normal, spec (with mips)" % name)


# ---- CNT composite of the backbone, frame, bumpers, fenders: a fine twill, panel seams every 0.5 m, bolts on them ----
tw = ((xx // 6 + yy // 6) % 2) * 2.0 - 1.0                           # 2x2 twill, ~1.2 cm tows
tw *= 0.5 + 0.5 * np.cos(2 * np.pi * (xx % 6) / 6) * np.cos(2 * np.pi * (yy % 6) / 6)
sm = seams(256, 3.0)
bp = [(x, y) for x in (10, 246, 266, 502) for y in range(32, N, 64)] + [(x, y) for y in (10, 246, 266, 502) for x in range(32, N, 64)]
bl = bolts(bp, 5.0)
dirt = pnoise(0.15, 1.0)
h = 0.04 * tw - 1.0 * sm + 0.8 * bl + 0.05 * pnoise(0.02)
col = np.empty((N, N, 3))
col[:] = (44, 47, 52)                                                    # natural graphite (the user, 2026-10-19: a shade lighter,
col += (7 * tw + 5 * dirt)[..., None]                                     # so its form reads in Titan's dim light)
col[sm > 0.3] *= 0.45
col += (bl * 28)[..., None]
col[..., 0] += dirt * 4; col[..., 1] += dirt * 3                         # a hint of regolith in the hollows
save("mpu_composite", col, h, 6.0, (0.32, 0.33, 0.35), 26.0)          # satin, its weave catching the light

# ---- deck plates: non-slip tread plate (raised lozenges, alternating), seams every 0.5 m ----
P = 32
cx = (xx % P) - P / 2; cy = (yy % P) - P / 2
alt = ((xx // P + yy // P) % 2) == 0
u = np.where(alt, (cx + cy) / np.sqrt(2), (cx - cy) / np.sqrt(2)); v = np.where(alt, (cx - cy) / np.sqrt(2), (cx + cy) / np.sqrt(2))
loz = np.clip(1.0 - (np.abs(u) / 11.0) ** 2 - (np.abs(v) / 2.6) ** 2, 0, 1) ** 0.6
sm = seams(256, 3.0)
h = 0.9 * loz - 1.2 * sm + 0.05 * pnoise(0.03)
wear = pnoise(0.2, 1.0)
col = np.empty((N, N, 3)); col[:] = (66, 69, 73)
col += (loz * 22 + wear * 8)[..., None]
col[sm > 0.3] *= 0.4
save("mpu_deck", col, h, 5.0, (0.35, 0.36, 0.38), 30.0)

# ---- machined metal of the wishbones, brackets, wheel blades: brushed along u, faint machining rings ----
br = aniso(0.02, 1.2, 1.0)
h = 0.25 * br + 0.05 * pnoise(0.05)
col = np.empty((N, N, 3)); col[:] = (118, 113, 104)                   # titanium, a faint warm anodised tint
col += (14 * br + 6 * pnoise(0.2))[..., None]
save("mpu_metal", col, h, 2.0, (0.66, 0.63, 0.58), 50.0)

# ---- the tyre (kept in mpu_rubber.dds, the mesh's name for it): the airless wheel's woven spring mesh of Mo-47.5Re wire
# (TVHeat.h - the heat model's material): silver-grey wire over and under in a plain weave, dark where you look through
# the gaps into the wheel, the running surface polished bright by the ground, the wire's own scuffs. Metallic sheen
P = 16                                                                       # a weave cell: ~1.6 cm at 0.5 m a tile... 512 px
cu, cv = (xx % P) / P, (yy % P) / P
over = ((xx // P + yy // P) % 2) == 0
wu = np.exp(-((cv - 0.5) / 0.22) ** 2)                                       # a wire along u in each cell's middle band
wv = np.exp(-((cu - 0.5) / 0.22) ** 2)                                       # a wire along v
hu = wu * (0.6 + 0.4 * np.where(over, 1.0, -0.2) * np.sin(np.pi * cu))     # over / under: the crossing wire rises
hv_ = wv * (0.6 + 0.4 * np.where(over, -0.2, 1.0) * np.sin(np.pi * cv))
wire = np.maximum(hu, hv_)
gap = np.clip(1.0 - (wu + wv) * 1.4, 0, 1)                                    # the holes between the wires
polish = np.clip(pnoise(0.25, 1.0) * 0.5 + 0.6, 0, 1)                         # the running surface, polished unevenly
scuff = np.clip(aniso(0.8, 0.02, 1.0), 0, 1) ** 4
h = 0.9 * wire - 0.8 * gap + 0.05 * pnoise(0.02)
col = np.empty((N, N, 3)); col[:] = (150, 150, 156)
col *= (0.75 + 0.35 * wire * polish)[..., None]
col[..., :] *= (1.0 - 0.85 * gap)[..., None]                                 # into the wheel: dark
col += (25 * scuff)[..., None]
spec = np.stack([0.75 * (1 - gap)] * 3, -1)
save("mpu_rubber", col, h, 4.0, spec, 45.0)

# ---- white sandwich panels of the cabins: matt, panel seams every 0.5 m, rivet lines ----
sm = seams(256, 2.5)
bp = [(x, y) for x in (8, 248, 264, 504) for y in range(24, N, 48)]
bl = bolts(bp, 3.5)
dirt = pnoise(0.12, 1.0)
h = -1.0 * sm + 0.6 * bl + 0.03 * pnoise(0.02)
col = np.empty((N, N, 3)); col[:] = (194, 190, 180)                      # warm off-white enamel (Lunokhod, Soyuz): ~0.75, not snow
col += (6 * dirt)[..., None]; col[..., 2] -= np.clip(dirt, 0, 1) * 10
col[sm > 0.3] *= 0.55
col -= (bl * 25)[..., None]
save("mpu_white", col, h, 5.0, (0.30, 0.30, 0.29), 28.0)
# ---- the cabin's lining (Э.МПУ): powder-coated panels 0.5 m, quarter-turn fasteners in the corners, scuffs ----
sm = seams(256, 2.0)
fp = [(x, y) for x in (16, 240, 272, 496) for y in (16, 240, 272, 496)]
fs = bolts(fp, 7.0)
slot = np.zeros((N, N))
for px, py in fp:                                                        # the fastener's slot
    slot[(np.abs(yy - py) < 1.2) & (np.abs(xx - px) < 5)] = 1.0
peel = pnoise(0.006, 1.0); scuff = np.clip(aniso(0.6, 0.03, 1.0), 0, 1) ** 3
h = -1.0 * sm + 0.7 * fs - 0.6 * slot + 0.08 * peel
col = np.empty((N, N, 3)); col[:] = (84, 88, 88)
col += (4 * peel + 18 * scuff)[..., None]
col[sm > 0.3] *= 0.45
col += (fs * 30)[..., None]; col[slot > 0] *= 0.5
save("mpu_lining", col, h, 5.0, (0.18, 0.18, 0.19), 14.0)

# ---- the seats and the bunk mattresses: rubber-coated ballistic weave, quilted seams ----
wv = (np.sin(2 * np.pi * xx / 4) * np.sin(2 * np.pi * yy / 4))
q = seams(128, 2.0)
h = 0.15 * wv - 0.9 * q + 0.1 * pnoise(0.05)
col = np.empty((N, N, 3)); col[:] = (40, 44, 42)
col += (5 * wv + 6 * pnoise(0.2))[..., None]
col[q > 0.3] *= 0.6
save("mpu_seat", col, h, 3.0, (0.08, 0.08, 0.08), 8.0)
print("written to", DST)
