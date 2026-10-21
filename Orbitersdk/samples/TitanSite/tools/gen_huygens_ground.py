"""Ground material maps for the D3D11 client, from the Huygens DISR surface images
(Orbitersdk/samples/TitanSite/huygens_disr/DISR_GROUND_PARAMETERS.md):
rounded, flattened water-ice cobbles 4-20 cm (more small ones), height/width 0.35-0.5, sunk 30-50 %, a shallow scour
ring around each, on fine dark granular soil. Two materials: a cobble field (~25 % cover) and sand (~5 %).

Output: Textures/Titan_ground_cobbles.dds, Textures/Titan_ground_sand.dds - DX10 DDS, R8G8B8A8_UNORM, 2048^2,
5 mm per pixel (a 10.24 m repeat), full mip chain:
  R height  [-0.02, +0.10] m (0 = the mean soil level)
  G, B      normal x, y (tangent: x along the map's u, y along v; *0.5+0.5)
  A         ambient occlusion (cavities, the rings at the cobbles' feet)
Usage: gen_huygens_ground.py <orbiter root>
"""
import os
import struct
import sys

import numpy as np

N = 2048
PX = 0.005                        # m per pixel
H_LO, H_HI = -0.02, 0.10


def spectral(beta, seed, kmin=1.0):
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(N) * N
    kx, ky = np.meshgrid(f, f)
    k = np.sqrt(kx * kx + ky * ky)
    k[0, 0] = 1
    amp = k ** (-beta / 2) * (k >= kmin)
    ph = np.exp(2j * np.pi * rng.random((N, N)))
    h = np.real(np.fft.ifft2(amp * ph))
    return (h - h.mean()) / h.std()


def cobbles(cover, dmin, dmax, seed):
    """Height of flattened cobbles (power-law sizes) and the scour rings around them, with wrap-around."""
    rng = np.random.default_rng(seed)
    h = np.full((N, N), -1.0)
    ring = np.zeros((N, N))
    area, target = 0.0, cover * (N * PX) ** 2
    while area < target:
        # n(D) ~ D^-2 between dmin and dmax (inverse CDF of a Pareto slice)
        u = rng.random()
        a1, b1 = 1 / dmin, 1 / dmax
        D = 1 / (a1 + u * (b1 - a1))
        a = D / 2 / PX                                    # semi-axes in px
        b = a * rng.uniform(0.6, 1.0)
        c = D / 2 * rng.uniform(0.35, 0.5)                # half height [m]
        sink = 2 * c * rng.uniform(0.3, 0.5)          # 30-50 % of the height below the soil
        ang = rng.uniform(0, np.pi)
        x0, y0 = rng.uniform(0, N), rng.uniform(0, N)
        n = int(a * 1.5) + 2
        ys, xs = np.mgrid[-n:n + 1, -n:n + 1].astype(np.float64)
        cs, sn = np.cos(ang), np.sin(ang)
        u2 = ((xs * cs + ys * sn) / a) ** 2 + ((-xs * sn + ys * cs) / b) ** 2
        top = 2 * c * np.sqrt(np.clip(1 - u2, 0, None)) - sink           # rounded, flattened, partly buried
        top = np.where(u2 < 1, top, -1.0)
        rr = np.sqrt(u2)
        rg = np.exp(-((rr - 1.15) / 0.12) ** 2) * (rr > 1)                # the ring just outside
        iy = (ys.astype(int) + int(y0)) % N
        ix = (xs.astype(int) + int(x0)) % N
        h[iy, ix] = np.maximum(h[iy, ix], top)
        ring[iy, ix] = np.maximum(ring[iy, ix], rg * min(0.008, c * 0.3))
        area += np.pi * (a * PX) * (b * PX)
    return h, ring


def make(cover, dmin, dmax, seed):
    soil = 0.0025 * spectral(2.2, seed + 1, kmin=8) + 0.0012 * spectral(1.4, seed + 2, kmin=200)   # granular soil
    soil += 0.004 * spectral(3.0, seed + 3, kmin=1)                                              # gentle undulation
    hc, ring = cobbles(cover, dmin, dmax, seed)
    h = np.maximum(soil - ring, hc)
    h -= np.mean(soil - ring)                         # the soil level is 0 (the physics surface)
    # normals at the metric scale (wrap-around central differences)
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) / (2 * PX)
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) / (2 * PX)
    nz = 1 / np.sqrt(1 + dx * dx + dy * dy)
    nx, ny = -dx * nz, -dy * nz
    # occlusion: how far below its surroundings a point lies (a blurred height minus the height), plus the rings
    blur = h.copy()
    for s in (2, 4, 8):
        blur = 0.25 * (np.roll(blur, s, 0) + np.roll(blur, -s, 0) + np.roll(blur, s, 1) + np.roll(blur, -s, 1))
    ao = np.clip(1 - np.clip(blur - h, 0, None) * 40, 0.35, 1)
    print('cover %.2f: height %.3f..%.3f m' % (cover, h.min(), h.max()))
    return np.dstack([np.clip((h - H_LO) / (H_HI - H_LO), 0, 1), nx * 0.5 + 0.5, ny * 0.5 + 0.5, ao])


def save(path, img):
    h, w = img.shape[:2]
    mips = []
    while True:
        mips.append(np.round(np.clip(img, 0, 1) * 255).astype(np.uint8).tobytes())
        if img.shape[0] == 1:
            break
        img = 0.25 * (img[0::2, 0::2] + img[1::2, 0::2] + img[0::2, 1::2] + img[1::2, 1::2])
    hdr = struct.pack('<4s7I44x8I5I', b'DDS ', 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000, h, w, 0, 0, len(mips),
                      32, 0x4, struct.unpack('<I', b'DX10')[0], 0, 0, 0, 0, 0, 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + struct.pack('<5I', 28, 3, 0, 1, 0) + b''.join(mips))    # R8G8B8A8_UNORM: 4x cheaper to filter than 16 bit


def main(root):
    tex = os.path.join(root, 'Textures')
    save(os.path.join(tex, 'Titan_ground_cobbles.dds'), make(0.25, 0.05, 0.20, 101))
    save(os.path.join(tex, 'Titan_ground_sand.dds'), make(0.05, 0.03, 0.10, 202))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else r'C:\Games\Orbiter-2024')
