"""Particle texture for the SO2 sublimation streams (Textures/IoLava_sublim.dds).
Orbiter's particle renderer takes ONE QUARTER of the texture per particle (stock Contrail*/Tantra_dust are 2x2 atlases
of four puffs): a single centred blob gives square particles. So: four puffs, each fading to alpha 0 at its own edge."""
import numpy as np
from PIL import Image
from noise import fbm

def build(path='../../Textures/IoLava_sublim.dds', n=128):
    out = np.zeros((2 * n, 2 * n, 4), np.uint8)
    y, x = np.mgrid[0:n, 0:n] + 0.5
    for k in range(4):
        r = np.hypot(x - n / 2, y - n / 2) / (n / 2)
        lump = fbm(x / n * 6 + 13 * k, y / n * 6, 1.0, 0.02, 4, seed=500 + k)       # irregular grain cloud
        a = np.clip(1 - r * (1 + 0.35 * lump), 0, 1) ** 1.6 * (0.75 + 0.5 * np.clip(lump + 0.5, 0, 1))
        a *= np.clip((1 - r) * 8, 0, 1)                                              # exactly 0 at the cell edge
        col = np.array([240, 228, 215])                                             # SO2 frost grains, faint warm tint
        i, j = divmod(k, 2)
        cell = out[i * n:(i + 1) * n, j * n:(j + 1) * n]
        cell[..., :3] = col; cell[..., 3] = (np.clip(a, 0, 1) * 255).astype(np.uint8)
    Image.fromarray(out).save(path)
    return out

if __name__ == '__main__':
    a = build()
    print('corners alpha', a[0, 0, 3], a[127, 127, 3], a[128, 128, 3], ' centres', a[64, 64, 3], a[192, 192, 3])
