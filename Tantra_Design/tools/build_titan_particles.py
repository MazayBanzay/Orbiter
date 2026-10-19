# Particle textures for Titan (2026-10-18, the Titan session's plan, the user: «делаем всё»):
#   titan_dust.dds - the damp, sticky organic sand the soles and the flight module's jet stir up: a dark ochre-brown puff,
#                    soft and lumpy (Huygens' landing raised a low, slow cloud that settled at once, no fan of grains)
#   ice_xtal.dds   - ice crystals flash-frozen out of a valve's vent (water or gas, into 94 K or vacuum): a few bright
#                    specks with a faint glint cross, white with a cold blue edge
# Uncompressed A8R8G8B8 with mips (no DXT blocks on soft edges - see build_h2_exhaust.py)
# Run: python build_titan_particles.py -> Textures\Tantra\titan_dust.dds, ice_xtal.dds
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # (run with python -I: the scratch folder shadows the stdlib)
from build_h2_exhaust import write_dds   # (its module code writes the jet textures too - harmless, same output)

OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Textures", "Tantra"))
N = 128
y, x = (np.mgrid[0:N, 0:N] + 0.5) / N * 2 - 1
r = np.hypot(x, y)
rng = np.random.default_rng(11)

def smooth_noise(n, scale):
    g = rng.random((n // scale + 2, n // scale + 2))
    from PIL import Image
    return np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC), float) / 255

# the dust puff: a soft lumpy blob, denser in the middle, its edge broken by noise
lump = 0.6 * smooth_noise(N, 16) + 0.4 * smooth_noise(N, 6)
a = np.clip(1 - (r / (0.75 + 0.25 * lump)) ** 2, 0, 1) ** 1.5 * (0.55 + 0.45 * lump)
col = np.stack([0.30 + 0.06 * lump, 0.22 + 0.04 * lump, 0.13 + 0.02 * lump], -1)     # dark ochre-brown (tholin sand)
write_dds(os.path.join(OUT, "titan_dust.dds"), np.concatenate([col, a[..., None]], -1))

# ice crystals: three specks and a glint cross on the brightest
a = np.zeros((N, N))
for cx, cy, s in ((0.0, 0.0, 0.10), (0.35, -0.25, 0.05), (-0.3, 0.32, 0.045)):
    d = np.hypot(x - cx, y - cy)
    a = np.maximum(a, np.exp(-(d / s) ** 2))
cross = np.exp(-(x / 0.02) ** 2) * np.exp(-(y / 0.45) ** 2) + np.exp(-(y / 0.02) ** 2) * np.exp(-(x / 0.45) ** 2)
a = np.clip(a + 0.5 * cross, 0, 1) * np.clip(1 - r, 0, 1)
col = np.stack([0.90 + 0.10 * a, 0.95 + 0.05 * a, np.ones_like(a)], -1)
write_dds(os.path.join(OUT, "ice_xtal.dds"), np.concatenate([col, a[..., None]], -1))
print("titan_dust.dds, ice_xtal.dds")
