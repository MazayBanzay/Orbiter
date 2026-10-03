"""Generate particle textures for the anamezon beam in an atmosphere (Textures/Tantra_*.dds).

D3D9Client draws particles with alpha blending and takes the colour from the texture
(emissive particles are not lit, diffuse ones are lit by the sun), so the colour is
baked in here. Like the stock Contrail1.dds, each texture is a 2x2 atlas: every
particle uses one quadrant, so each quadrant holds a whole puff with transparent edges.
  * Tantra_plasma: the plasma ball - white-hot core, yellow-orange shell, dull red rim;
  * Tantra_wake:   shock-heated air and condensation behind the ball - light grey;
  * Tantra_dust:   ejecta where the beam reaches the ground - sandy brown.
"""
import os
import sys

import numpy as np

from gen_exhaust import write_dds

N = 128  # one atlas cell; the texture is 2N x 2N


def fbm(seed, octaves=5):
    """Fractal value noise in 0..1, tileable enough for a single puff."""
    rng = np.random.default_rng(seed)
    out = np.zeros((N, N))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = 4 * 2 ** o
        grid = rng.random((cells + 1, cells + 1))
        y, x = np.mgrid[0:N, 0:N] * (cells / N)
        x0, y0 = x.astype(int), y.astype(int)
        fx, fy = x - x0, y - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        a = grid[y0, x0] * (1 - fx) + grid[y0, x0 + 1] * fx
        b = grid[y0 + 1, x0] * (1 - fx) + grid[y0 + 1, x0 + 1] * fx
        out += amp * (a * (1 - fy) + b * fy)
        total += amp
        amp *= 0.5
    return out / total


def radius():
    y, x = np.mgrid[0:N, 0:N]
    return np.hypot((x + 0.5) / N - 0.5, (y + 0.5) / N - 0.5) * 2.0  # 0 centre .. 1 edge


def window():
    """Opacity envelope that is exactly zero well inside the square: particles are
    square sprites (and cast square shadows), so no edge may keep any alpha."""
    return np.clip((0.9 - radius()) / 0.45, 0, 1) ** 2


def ramp(t, stops):
    """Piecewise-linear colour ramp; stops = [(t, (r, g, b)), ...]."""
    ts = [s[0] for s in stops]
    rgb = np.zeros(t.shape + (3,))
    for i in range(3):
        rgb[..., i] = np.interp(t, ts, [s[1][i] for s in stops])
    return rgb


def plasma(seed):
    r = radius()
    n = fbm(seed)
    rr = np.clip(r * (0.85 + 0.35 * n), 0, 1.2)  # turbulent boundary
    rgb = ramp(rr, [(0.0, (1.0, 1.0, 0.97)), (0.25, (1.0, 0.95, 0.75)), (0.5, (1.0, 0.7, 0.3)),
                    (0.75, (0.85, 0.32, 0.1)), (1.0, (0.5, 0.12, 0.05))])
    alpha = np.clip(1.0 - rr, 0, 1) ** 0.8 * (0.8 + 0.2 * n) * window()
    return rgb, alpha


def puff(base, seed, contrast, opacity):
    r = radius()
    n = fbm(seed)
    shade = 0.75 + contrast * (n - 0.5)
    rgb = np.clip(np.stack([base[i] * shade for i in range(3)], axis=-1), 0, 1)
    edge = np.clip(1.0 - r * (0.8 + 0.5 * (1 - n)), 0, 1)
    alpha = edge ** 1.5 * np.clip(0.4 + 0.9 * n, 0, 1) * window() * opacity
    return rgb, alpha


def halo(seed):
    """Gamma-fluorescence halo: flux ~ 1/r^2 seen through gives ~ 1/b, a bright core
    with long faint wings; blue-violet N2+ bands, whiter in the core."""
    b = radius()
    b0 = 0.15
    prof = (b0 / (b + b0) - b0 / (1 + b0)) / (1 - b0 / (1 + b0))
    rgb = ramp(np.clip(b * 3, 0, 1), [(0.0, (0.92, 0.88, 1.0)), (0.3, (0.7, 0.55, 1.0)), (1.0, (0.5, 0.35, 0.95))])
    alpha = np.clip(prof, 0, 1) ** 0.7 * window()
    return rgb, alpha


def ionplume(seed):
    r = radius()
    n = fbm(seed)
    rr = np.clip(r * (0.85 + 0.35 * n), 0, 1.2)
    rgb = ramp(rr, [(0.0, (1.0, 0.97, 1.0)), (0.3, (1.0, 0.75, 0.85)), (0.6, (1.0, 0.55, 0.35)),
                    (1.0, (0.6, 0.25, 0.15))])
    alpha = np.clip(1.0 - rr, 0, 1) ** 0.9 * (0.75 + 0.25 * n) * window()
    return rgb, alpha


def column(seed):
    r = radius()
    n = fbm(seed)
    t = np.clip(0.35 + 0.5 * n + 0.3 * r, 0, 1)
    rgb = ramp(t, [(0.0, (1.0, 0.96, 0.88)), (0.5, (1.0, 0.8, 0.5)), (1.0, (0.95, 0.5, 0.2))])
    alpha = np.clip(1.0 - r, 0, 1) ** 0.6 * (0.55 + 0.45 * n) * window() * 0.8
    return rgb, alpha


def atlas(make):
    cells = [make(k) for k in range(4)]
    rgb = np.concatenate([np.concatenate([cells[0][0], cells[1][0]], axis=1),
                          np.concatenate([cells[2][0], cells[3][0]], axis=1)], axis=0)
    alpha = np.concatenate([np.concatenate([cells[0][1], cells[1][1]], axis=1),
                            np.concatenate([cells[2][1], cells[3][1]], axis=1)], axis=0)
    return rgb, alpha


TEXTURES = {
    "Tantra_plasma": atlas(lambda k: plasma(11 + k)),
    "Tantra_wake": atlas(lambda k: puff((0.97, 0.95, 0.92), 23 + k, 0.3, 0.5)),
    "Tantra_halo": atlas(halo),
    # plasma column in air: many overlapping particles - flat turbulent glow, no core
    "Tantra_column": atlas(lambda k: column(61 + k)),
    # planetary jet in air: plasma mixing with shock-heated air
    "Tantra_ionplume": atlas(lambda k: ionplume(51 + k)),
    "Tantra_dust": atlas(lambda k: puff((0.72, 0.58, 0.43), 37 + k, 0.5, 0.8)),
}

if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")
    for name, (rgb, alpha) in TEXTURES.items():
        write_dds(os.path.join(root, "Textures", name + ".dds"), rgb, alpha)
        print("wrote", name + ".dds")
    if "--preview" in sys.argv:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        tiles = [np.clip(rgb * a[..., None] + 0.25 * (1 - a[..., None]), 0, 1) for rgb, a in TEXTURES.values()]
        plt.imsave(sys.argv[sys.argv.index("--preview") + 1], np.concatenate(tiles, axis=1))
