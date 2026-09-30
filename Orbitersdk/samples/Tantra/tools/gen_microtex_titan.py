"""Surface micro textures for Titan (D3D9Client), Textures/D3D9Titan_{A,B,C}.dds.

Format as the stock D3D9Moon_*.dds (Config/MicroTex.cfg, NORMALS 1): 2048x2048 DXT5 with
a full mip chain; normal x in alpha, normal y in green, luminance in blue (red = blue),
all centred on 128. Tileable: every field is built by spectral synthesis or wrapped
placement, so it repeats seamlessly.

What is drawn (Huygens DISR images, Cassini RADAR/VIMS):
  A  ~1.25 cm/px, 26 m tile:  dark sand of organic grains with rounded water-ice cobbles
     5-15 cm (Huygens landing site), brighter than the sand, partly sunk in it.
  B  ~20 cm/px, 410 m tile:  patchy cobble fields, shallow rills and channels cut by
     methane runoff, mottled albedo.
  C  ~1.25 m/px, 2.6 km tile: low linear dune ridges running east-west (spacing ~1.3 km,
     two per tile) over eroded terrain; kept gentle because the layer is global.
"""
import io
import os
import sys

import numpy as np
from PIL import Image

N = 2048
rng = np.random.default_rng(2005)  # Huygens landed 14 January 2005


def spectral(beta, kmin=1.0, kmax=None, seed=None):
    """Periodic noise with power spectrum ~ k^-beta, normalised to std 1."""
    g = np.random.default_rng(seed)
    kx = np.fft.fftfreq(N) * N
    k = np.hypot(*np.meshgrid(kx, kx))
    amp = np.where(k >= kmin, (k + 1e-9) ** (-beta / 2.0), 0.0)
    if kmax:
        amp *= np.exp(-(k / kmax) ** 2)
    phase = np.exp(2j * np.pi * g.random((N, N)))
    f = np.real(np.fft.ifft2(amp * phase))
    return (f - f.mean()) / f.std()


def cobbles(count, rmin, rmax, seed, sink=0.35):
    """Height and albedo of rounded cobbles placed with wrap-around; radii in px."""
    g = np.random.default_rng(seed)
    h = np.zeros((N, N))
    alb = np.zeros((N, N))
    # power-law size distribution: many small, few large
    r = rmin * (1 - g.random(count) * (1 - (rmin / rmax) ** 1.5)) ** (-1 / 1.5)
    cx, cy = g.random(count) * N, g.random(count) * N
    ecc = 0.6 + 0.4 * g.random(count)
    ang = g.random(count) * np.pi
    for x0, y0, rr, e, a in zip(cx, cy, r, ecc, ang):
        n = int(rr * 1.3) + 2
        ys, xs = np.mgrid[-n:n + 1, -n:n + 1]
        c, s = np.cos(a), np.sin(a)
        u = (xs * c + ys * s) / rr
        v = (-xs * s + ys * c) / (rr * e)
        d2 = u * u + v * v
        dome = np.sqrt(np.clip(1 - d2, 0, None)) * rr * 0.8
        dome = np.clip(dome - sink * rr, 0, None)  # partly sunk in sand
        iy = (ys + int(y0)) % N
        ix = (xs + int(x0)) % N
        h[iy, ix] = np.maximum(h[iy, ix], dome)
        alb[iy, ix] = np.maximum(alb[iy, ix], (dome > 0) * (0.6 + 0.4 * g.random()))
    return h, alb


def encode(height, lum, slope_gain):
    """Normal (from the height field, wrap-around gradient) and luminance -> RGBA."""
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5
    nx, ny = -dx * slope_gain, -dy * slope_gain
    nz = np.ones_like(nx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny = nx / ln, ny / ln
    to8 = lambda a: np.clip(np.round(128 + 127 * a), 0, 255).astype(np.uint8)
    b = np.clip(np.round(128 + lum), 0, 255).astype(np.uint8)
    rgba = np.dstack([b, to8(ny), b, to8(nx)])
    return rgba


def level_a():
    # 1.25 cm/px (80 px/m in MicroTex.cfg), 25.6 m tile; heights in px
    sand = spectral(3.2, kmin=2, seed=11) * 0.8 + spectral(2.0, kmin=300, seed=12) * 0.05
    h1, a1 = cobbles(9000, 2.0, 6.0, seed=13)             # 5-15 cm ice cobbles, ~15% cover
    h2, a2 = cobbles(30000, 0.8, 1.8, seed=14, sink=0.2)  # gravel
    h = sand + h1 + 0.5 * h2
    lum = -6 + 26 * a1 + 10 * a2 + 3 * spectral(2.5, kmin=4, seed=15)
    return encode(h, lum, 1.2)


def level_b():
    # 20 cm/px, 410 m tile: channels ~1 m deep (5 px) on gently rolling ground
    field = spectral(3.4, kmin=2, seed=21)
    patches = np.clip(field * 1.2, 0, 1)                  # where cobble fields lie
    rills = np.exp(-(spectral(2.8, kmin=3, seed=22) / 0.06) ** 2)   # zero lines of noise -> thin channels
    h = 12.0 * spectral(3.6, kmin=1, seed=24) - 3.0 * rills + 0.3 * spectral(2.6, kmin=64, seed=23) * (0.3 + patches)
    lum = 10 * patches - 4 * rills + 3 * spectral(2.6, kmin=4, seed=25)
    return encode(h, lum, 0.9)


def level_c():
    # 1.25 m/px, 2.56 km tile: two E-W linear dunes, ~60 m (48 px) high, on eroded ground
    y = np.arange(N)[:, None] / N
    warp = 0.04 * spectral(3.6, kmin=1, seed=31)          # sinuous crests, junctions
    phase = (y * 2 + warp) % 1.0
    prof = np.where(phase < 0.7, phase / 0.7, (1 - phase) / 0.3)   # gentle stoss, steep lee
    dunes = prof ** 1.3
    terrain = spectral(3.8, kmin=1, seed=32)
    h = 48.0 * dunes + 20.0 * terrain
    lum = -8 * dunes + 3 * spectral(2.8, kmin=2, seed=33)  # crests sandy-dark, troughs brighter
    return encode(h, lum, 0.6)


def save_dds_dxt5_mips(rgba, path):
    """DXT5 with a full mip chain: each level compressed by Pillow, one header."""
    img = Image.fromarray(rgba, "RGBA")
    blobs, size, levels = [], N, 0
    while True:
        buf = io.BytesIO()
        img.resize((size, size), Image.LANCZOS).save(buf, "DDS", pixel_format="DXT5")
        data = buf.getvalue()
        if levels == 0:
            header = bytearray(data[:128])
        blobs.append(data[128:])
        levels += 1
        if size == 1:
            break
        size //= 2
    flags = int.from_bytes(header[8:12], "little") | 0x20000 | 0x80000   # MIPMAPCOUNT | LINEARSIZE
    header[8:12] = flags.to_bytes(4, "little")
    header[28:32] = levels.to_bytes(4, "little")
    header[108:112] = (0x1000 | 0x8 | 0x400000).to_bytes(4, "little")    # TEXTURE | COMPLEX | MIPMAP
    with open(path, "wb") as f:
        f.write(header)
        for b in blobs:
            f.write(b)


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")
    out = os.path.join(root, "Textures")
    for name, make in (("A", level_a), ("B", level_b), ("C", level_c)):
        rgba = make()
        save_dds_dxt5_mips(rgba, os.path.join(out, f"D3D9Titan_{name}.dds"))
        if "--preview" in sys.argv:
            Image.fromarray(rgba[:512, :512, 2]).save(os.path.join(sys.argv[sys.argv.index("--preview") + 1], f"titan_{name}_lum.png"))
            Image.fromarray(rgba[:512, :512, 3]).save(os.path.join(sys.argv[sys.argv.index("--preview") + 1], f"titan_{name}_nx.png"))
        st = rgba.reshape(-1, 4).astype(float)
        print(f"D3D9Titan_{name}.dds  lum std {st[:, 2].std():.1f}  normal std {st[:, 3].std():.1f}/{st[:, 1].std():.1f}")
