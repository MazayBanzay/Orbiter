"""Procedural textures of the Tantra skin (Textures/Tantra/hull_*.dds, 1024x1024, tileable), 2026-10-09.
For each of the three coatings the colour map and the D3D9 client's coating maps (it loads them by the suffix):
  <name>.dds       colour, a detail map near white (the hue comes from the material, gen_mesh.py MATERIALS);
  <name>_norm.dds  tangent-space normal map from a height map: panel seams as grooves, plate bevels, facets, grain;
  <name>_spec.dds  RGB specular colour, A specular power (the client reads the alpha x 255 as the power).
One tile = 8 m along the ship x 1/9 of the section round (gen_mesh.py HULL_TEX_T, HULL_TEX_ROUND): panels 2 m.
  hull_ceramic: white boron-nitride/zirconia ceramic tiles 2 x 2 m in staggered rows, fine seams, fasteners at the
    corners, a satin glaze worn duller in patches, faint streaks along the flow;
  hull_borazon: amber cubic boron nitride armour plates 2 x 2 m, each a low four-sided pyramid (the crystal facets catch
    the light), polished, deep seams;
  hull_carbide: black boron-carbide tiles 1 x 1 m, matt, each tile its own burn, lighter gaps.
Run:  python make_hull_textures.py
"""
import os

import numpy as np



def write_dds(path, rgba, normal=False):
    """32-bit BGRA DDS with the full mip chain (box filter; normal maps renormalised per level): no shimmer far away."""
    a = np.clip(rgba, 0, 255).astype(float)
    if a.shape[2] == 3:
        a = np.concatenate([a, np.full(a.shape[:2] + (1,), 255.0)], 2)
    levels = [a]
    while levels[-1].shape[0] > 1:
        m = levels[-1]
        m = 0.25 * (m[0::2, 0::2] + m[1::2, 0::2] + m[0::2, 1::2] + m[1::2, 1::2])
        if normal:
            v = m[..., :3] / 127.5 - 1.0
            v[..., 2] = m[..., 2] / 255.0
            v /= np.maximum(1e-6, np.linalg.norm(v, axis=-1, keepdims=True))
            m = np.concatenate([np.stack([(v[..., 0] + 1) * 127.5, (v[..., 1] + 1) * 127.5, v[..., 2] * 255], -1), m[..., 3:]], -1)
        levels.append(m)
    h, w = a.shape[:2]
    header = bytearray(128)
    header[0:4] = b"DDS "
    vals = {4: 124, 8: 0x2100F, 12: h, 16: w, 20: w * 4, 28: len(levels), 76: 32, 80: 0x41, 88: 32,
            92: 0x00FF0000, 96: 0x0000FF00, 100: 0x000000FF, 104: 0xFF000000, 108: 0x401008}
    for off, v in vals.items():
        header[off:off + 4] = int(v).to_bytes(4, "little")
    with open(path, "wb") as f:
        f.write(header)
        for m in levels:
            m = np.clip(np.round(m), 0, 255).astype(np.uint8)
            f.write(np.ascontiguousarray(m[..., [2, 1, 0, 3]]).tobytes())

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
N = 1024
rng = np.random.default_rng(1957)
YY, XX = np.mgrid[0:N, 0:N].astype(float)


def smooth_noise(cells, amp):
    """Tileable value noise: a coarse random grid, smoothly interpolated (wraps at the edges)."""
    g = rng.normal(0.0, amp, (cells, cells))
    yy, xx = YY * (cells / N), XX * (cells / N)
    x0, y0 = np.floor(xx).astype(int), np.floor(yy).astype(int)
    fx, fy = xx - x0, yy - y0
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
    x0, y0 = x0 % cells, y0 % cells
    a = g[y0, x0] * (1 - fx) + g[y0, x1] * fx
    b = g[y1, x0] * (1 - fx) + g[y1, x1] * fx
    return a * (1 - fy) + b * fy


def fbm(octaves, amp):
    return sum(smooth_noise(4 * 2 ** k, amp / 2 ** k) for k in range(octaves))


def tiles(step_x, step_y, stagger=0.0):
    """Per pixel: local coordinates inside the tile (0..1), the tile index, the distance to the nearest edge (px)."""
    row = np.floor(YY / step_y).astype(int)
    xs = XX + (row % 2) * stagger * step_x
    col = np.floor(xs / step_x).astype(int)
    lx, ly = (xs % step_x) / step_x, (YY % step_y) / step_y
    edge = np.minimum.reduce([lx * step_x, (1 - lx) * step_x, ly * step_y, (1 - ly) * step_y])
    nx, ny = int(round(N / step_x)), int(round(N / step_y))
    return lx, ly, (col % nx) + nx * (row % ny), edge


def groove(edge, width, depth):
    """Height of a seam: a rounded groove `width` px wide."""
    return -depth * np.clip(1.0 - edge / width, 0.0, 1.0) ** 2


def normal_map(h, strength):
    """Tangent-space normal map (RGB 0..255) from a height field (px units), tileable gradients."""
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * strength
    n = np.stack([-dx, dy, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return np.stack([(n[..., 0] * 0.5 + 0.5) * 255, (n[..., 1] * 0.5 + 0.5) * 255, n[..., 2] * 255], -1)


def per_tile(idx, amp, n):
    v = rng.normal(0.0, amp, n)
    return v[idx % n]


def ceramic():
    lx, ly, idx, edge = tiles(256, 256, stagger=0.5)                      # 2 x 2 m tiles, staggered rows
    tone = per_tile(idx, 3.0, 64)
    col = 244.0 + tone + fbm(4, 3.0) - np.clip(-fbm(3, 1.2), 0, None) * 8.0          # worn, duller patches
    col += (smooth_noise(64, 1.0) * np.exp(-((XX % 128) - 64) ** 2 / 3000.0)) * 2.0  # faint streaks along the flow (u)
    col += rng.normal(0.0, 1.3, (N, N))
    seam = np.clip(1.0 - edge / 3.0, 0, 1)
    col -= 62.0 * seam
    # fasteners: four dots near each tile's corners
    for cx, cy in ((0.06, 0.06), (0.94, 0.06), (0.06, 0.94), (0.94, 0.94)):
        d = np.hypot((lx - cx) * 256, (ly - cy) * 256)
        col -= 26.0 * np.clip(1.0 - d / 3.2, 0, 1)
    h = groove(edge, 4.0, 3.0) + fbm(4, 0.25) + rng.normal(0.0, 0.05, (N, N))
    gloss = np.clip(1.0 - np.clip(-fbm(3, 1.2), 0, None) * 0.6, 0.3, 1.0)           # the worn patches less glossy
    spec = np.stack([60 * gloss, 60 * gloss, 58 * gloss, 18 + 10 * gloss], -1)       # satin glaze: power ~25
    return col, normal_map(h, 1.6), spec


def borazon():
    lx, ly, idx, edge = tiles(256, 256)
    col = np.full((N, N), 238.0)
    h = np.zeros((N, N))
    apex = rng.uniform(0.38, 0.62, (64, 2))
    ax, ay = apex[idx % 64, 0], apex[idx % 64, 1]
    # a low pyramid per plate: height falls linearly to the edges from an off-centre apex (four facets)
    hx = np.where(lx < ax, lx / ax, (1 - lx) / (1 - ax))
    hy = np.where(ly < ay, ly / ay, (1 - ly) / (1 - ay))
    h += 6.0 * np.minimum(hx, hy)
    facet = np.where(hx < hy, np.where(lx < ax, 0, 1), np.where(ly < ay, 2, 3))
    shade = per_tile(idx * 4 + facet, 5.0, 256)
    col += shade + per_tile(idx, 3.0, 64) + fbm(3, 2.0)
    col += 10.0 * np.clip(1.0 - np.abs(np.minimum(hx, hy) - 0.08) / 0.03, 0, 1)    # a bright bevel line round each plate
    col += rng.normal(0.0, 1.0, (N, N))
    col -= 85.0 * np.clip(1.0 - edge / 4.0, 0, 1)
    h += groove(edge, 5.0, 6.0) + fbm(3, 0.15)
    spec = np.stack([np.full((N, N), 235.0), np.full((N, N), 205.0), np.full((N, N), 150.0),
                     np.clip(70 + per_tile(idx, 12.0, 64), 40, 110)], -1)          # polished crystal: power 70 +-12
    return col, normal_map(h, 2.2), spec


def carbide():
    lx, ly, idx, edge = tiles(128, 128)
    col = 205.0 + per_tile(idx, 10.0, 64) + fbm(4, 5.0)                                # each tile burnt in differently
    col += rng.normal(0.0, 2.0, (N, N))
    col += 48.0 * np.clip(1.0 - edge / 2.5, 0, 1)                                    # the gaps between the tiles show lighter
    h = groove(edge, 3.0, 3.5) + 1.2 * (1.0 - np.hypot(lx - 0.5, ly - 0.5) * 1.2) + fbm(4, 0.35)   # slightly domed tiles
    spec = np.stack([np.full((N, N), 30.0), np.full((N, N), 30.0), np.full((N, N), 32.0),
                     np.clip(8 + per_tile(idx, 2.0, 64), 4, 14)], -1)               # matt: power ~8
    return col, normal_map(h, 1.4), spec


def rgb(a):
    a = np.clip(a, 0, 255)
    return np.repeat(a[..., None], 3, -1) if a.ndim == 2 else a


if __name__ == "__main__":
    out = os.path.join(ROOT, "Textures", "Tantra")
    os.makedirs(out, exist_ok=True)
    for name, fn in (("hull_ceramic", ceramic), ("hull_borazon", borazon), ("hull_carbide", carbide)):
        col, nrm, spec = fn()
        write_dds(os.path.join(out, name + ".dds"), rgb(col))
        write_dds(os.path.join(out, name + "_norm.dds"), rgb(nrm), normal=True)
        write_dds(os.path.join(out, name + "_spec.dds"), spec)
    print("hull textures (colour, normal, specular) written to", out)
