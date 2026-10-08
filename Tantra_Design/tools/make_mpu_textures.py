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


def save(name, rgb, h, strength, spec_rgb, power):
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").save(os.path.join(DST, name + ".dds"))
    nrm = normal_from(h, strength)
    Image.fromarray((np.clip(nrm * 0.5 + 0.5, 0, 1) * 255).astype(np.uint8), "RGB").save(os.path.join(DST, name + "_norm.dds"))
    s = np.clip(np.broadcast_to(spec_rgb, rgb.shape), 0, 1) * 255
    a = np.clip(np.broadcast_to(power, h.shape) / 4.0, 0, 255)
    Image.fromarray(np.concatenate([s, a[..., None]], 2).astype(np.uint8), "RGBA").save(os.path.join(DST, name + "_spec.dds"))
    print("%-16s colour, normal, spec" % name)


# ---- CNT composite of the backbone, frame, bumpers, fenders: a fine twill, panel seams every 0.5 m, bolts on them ----
tw = ((xx // 6 + yy // 6) % 2) * 2.0 - 1.0                           # 2x2 twill, ~1.2 cm tows
tw *= 0.5 + 0.5 * np.cos(2 * np.pi * (xx % 6) / 6) * np.cos(2 * np.pi * (yy % 6) / 6)
sm = seams(256, 3.0)
bp = [(x, y) for x in (10, 246, 266, 502) for y in range(32, N, 64)] + [(x, y) for y in (10, 246, 266, 502) for x in range(32, N, 64)]
bl = bolts(bp, 5.0)
dirt = pnoise(0.15, 1.0)
h = 0.04 * tw - 1.0 * sm + 0.8 * bl + 0.05 * pnoise(0.02)
col = np.empty((N, N, 3))
col[:] = (36, 39, 44)
col += (6 * tw + 5 * dirt)[..., None]
col[sm > 0.3] *= 0.45
col += (bl * 28)[..., None]
col[..., 0] += dirt * 4; col[..., 1] += dirt * 3                         # a hint of regolith in the hollows
save("mpu_composite", col, h, 6.0, (0.20, 0.21, 0.23), 18.0)

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
col = np.empty((N, N, 3)); col[:] = (112, 115, 120)
col += (14 * br + 6 * pnoise(0.2))[..., None]
save("mpu_metal", col, h, 2.0, (0.70, 0.71, 0.74), 60.0)

# ---- rubber of the tread and the mud flaps: fine grain, small cuts, worn lighter ----
gr = pnoise(0.004, 1.0)
cuts = np.zeros((N, N))
for _ in range(60):
    x0, y0, L, a = rng.integers(0, N), rng.integers(0, N), rng.integers(6, 22), rng.uniform(0, np.pi)
    for t in range(L):
        cuts[int(y0 + t * np.sin(a)) % N, int(x0 + t * np.cos(a)) % N] = 1.0
h = 0.4 * gr - 0.8 * cuts
col = np.empty((N, N, 3)); col[:] = (22, 22, 24)
col += (6 * gr + 10 * pnoise(0.25))[..., None]
col[cuts > 0] *= 0.5
save("mpu_rubber", col, h, 3.0, (0.06, 0.06, 0.06), 6.0)

# ---- white sandwich panels of the cabins: matt, panel seams every 0.5 m, rivet lines ----
sm = seams(256, 2.5)
bp = [(x, y) for x in (8, 248, 264, 504) for y in range(24, N, 48)]
bl = bolts(bp, 3.5)
dirt = pnoise(0.12, 1.0)
h = -1.0 * sm + 0.6 * bl + 0.03 * pnoise(0.02)
col = np.empty((N, N, 3)); col[:] = (212, 214, 216)
col += (6 * dirt)[..., None]; col[..., 2] -= np.clip(dirt, 0, 1) * 10
col[sm > 0.3] *= 0.55
col -= (bl * 25)[..., None]
save("mpu_white", col, h, 5.0, (0.25, 0.25, 0.26), 22.0)
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
