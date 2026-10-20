# Bakes the spray sheet flipbook (Spray.cpp, spray.hlsl): a liquid sheet thrown off a wheel's rim, flying, thinning,
# tearing into holes and breaking into drops. Own simulation (numpy), no third-party data.
#
# The card's frame moves with the sheet's mean velocity: x along the travel, y across it (the wheel's width). The liquid is
# N parcels thrown over the emission interval with the rim's velocity spread (speed +-sx, direction +-sy); in the card's
# frame each parcel drifts with its velocity difference, the sheet stretching. Its thickness h is the parcels' volume over
# the area (splatted). Perturbations grow on it (Kelvin-Helmholtz against the gas: amplitude ~ t^2). Where h falls below
# the critical thickness the sheet ruptures and the hole opens at the Taylor-Culick speed sqrt(2 sigma / (rho h)); the
# liquid of the torn sheet gathers in rims and leaves as drops (the parcels clustered, each drop flying with its mean
# velocity). Frames 0..63 over the card's life, 8 x 8 in a 1024 x 1024 atlas, each 128 x 128:
#   R, G  the surface normal's x, y (0.5 + 0.5 n) from the thickness gradient
#   B     the liquid's thickness (normalised; 1 = the fresh sheet's)
#   A     the coverage (the sheet, the drops)
# Output: Textures\D3D11Client\spray_sheet.dds (RGBA8, DX10 header)
import os, struct, sys
import numpy as np
from scipy.ndimage import gaussian_filter, binary_dilation, label, center_of_mass

R = 128; G = 8; F = G * G
rng = np.random.default_rng(7)

# the sheet in units of the card (the card spans x 0..1, y -0.5..0.5 at the end of its life). The parcels on a fine
# lattice (a continuous sheet, not noise): the slab thrown over the emission interval, the wheel's width across
NA, NB = 420, 260
A, B = np.meshgrid(np.linspace(0, 1, NA), np.linspace(-1, 1, NB))
A = A.ravel(); B = B.ravel(); N = A.size
x0 = 0.02 + 0.04 * A
y0 = 0.07 * B
# the velocity field (card frame): the front faster than the back (the sheet stretches along the travel), the fan opening
# sideways; streaks across the width - lanes the rim's grooves and the gas's shear leave (low wavenumbers in y)
def lane(n, k):
    ph = rng.uniform(0, 2 * np.pi, k); amp = rng.uniform(0.3, 1.0, k) / np.arange(1, k + 1) ** 0.7
    return lambda v: sum(amp[i] * np.sin((i + 2) * np.pi * v + ph[i]) for i in range(k))
L1, L2 = lane(0, 9), lane(0, 9)
ux = 0.48 + 0.80 * (A - 0.5) + 0.05 * L1(B)
uy = 0.12 * B + 0.025 * L2(B * 1.7)
# the mass per parcel: the sheet thicker along some lanes, thinner along others (corrugation): the thin lanes tear first,
# leaving ligaments along the flow
mass = np.clip(1.0 + 0.75 * L2(B * 2.3) / 1.5 + 0.15 * L1(A * 3.1 + B) / 1.5, 0.08, 2.2)
# the sheet's edges: thin at the sides and the back (the liquid peeled last is the least), the front gathering in a rim that
# shoots fingers (Rayleigh-Taylor on the decelerated rim: a wavelength ~ a few rim widths)
mass *= np.sqrt(np.clip(1 - B * B, 0, 1)) * np.clip(A / 0.35, 0.15, 1.0) * (1 + 1.2 * np.clip((A - 0.85) / 0.15, 0, 1))
F3 = lane(0, 12)
# each parcel's own small deviation (the rim's chevrons do not throw a perfect sheet): ragged edges, no straight cut
ux += rng.normal(0, 0.035, N) * (0.4 + A); uy += rng.normal(0, 0.02, N)
ux += 0.10 * np.clip((A - 0.7) / 0.3, 0, 1) ** 2 * (1 + F3(B * 2.0))
# perturbation field (Kelvin-Helmholtz waves on the sheet): smooth noise displacements, two octaves
def noise_field(n, s):
    a = gaussian_filter(rng.normal(0, 1, (n, n)), s)
    return a / (a.std() + 1e-9)
NX = noise_field(256, 10) + 0.5 * noise_field(256, 4)
NY = noise_field(256, 10) + 0.5 * noise_field(256, 4)
def sample(field, x, y):
    i = np.clip(((y + 0.5) * 255).astype(int), 0, 255); j = np.clip((x * 255).astype(int), 0, 255)
    return field[i, j]
dX = sample(NX, x0 * 3 % 1, y0); dY = sample(NY, x0 * 3 % 1, y0)

h0 = None
rupt = np.zeros((R, R), bool)
atlas = np.zeros((G * R, G * R, 4), np.uint8)
# the drops: seeds among the parcels, each the liquid of its neighbourhood once the sheet there has torn; sizes log-normal
# (ligament breakup: a spread about ~2x the ligament's width), three size classes drawn as discs
K = 6000
seed = rng.choice(N, K, replace=False, p=mass / mass.sum())
dsz = np.clip(rng.lognormal(0.0, 0.45, K), 0.5, 3.0)        # radius in pixels
born = np.full(K, -1.0)

def splat(x, y, w, sig):
    H, _, _ = np.histogram2d((y + 0.5) * R, x * R, bins=R, range=[[0, R], [0, R]], weights=w)
    return gaussian_filter(H, sig) if sig > 0 else H

for f in range(F):
    t = f / (F - 1)                                   # 0..1 of the card's life
    amp = 0.035 * t * t                                # perturbations growing
    x = x0 + 0.95 * ux * t + amp * dX                  # the card frame: the sheet travels from its back to its front
    y = y0 + uy * t + amp * dY
    h = splat(x, y, mass, 1.2)
    if h0 is None: h0 = h.max()
    h = h / h0                                         # 1 = the fresh sheet
    # rupture: the sheet thins as it stretches (h ~ 1 / area); below the critical thickness (~5 % of the fresh sheet: tens
    # of microns from a millimetre) it tears, and the holes open at the Taylor-Culick speed sqrt(2 sigma / (rho h)) -
    # faster the thinner it is: about a pixel a frame here, two where it is thinnest
    crit = 0.13
    rupt |= (h < crit) & (h > 0.004)
    if rupt.any():
        thin = np.median(h[(h > 0.004)]) if np.any(h > 0.004) else 0.05
        rupt = binary_dilation(rupt, iterations=2 if thin < 0.08 else 1) & (h > 0.002)
    sheet = np.clip((h - 0.02) / 0.03, 0, 1) * (~rupt)
    thick = np.clip(h, 0, 1) * (~rupt)
    # the drops of the torn sheet
    sx, sy = x[seed], y[seed]
    ci = np.clip(((sy + 0.5) * R).astype(int), 0, R - 1); cj = np.clip((sx * R).astype(int), 0, R - 1)
    born[(born < 0) & rupt[ci, cj]] = t
    live = born >= 0
    cov = sheet.copy()
    for lo, hi, sig in ((0.0, 0.9, 0.0), (0.9, 1.6, 0.6), (1.6, 9.0, 1.1)):
        m = live & (dsz >= lo) & (dsz < hi)
        if not m.any(): continue
        d = splat(sx[m], sy[m], np.ones(m.sum()), sig)
        peak = 1.0 if sig == 0 else 1.0 / (2 * np.pi * sig * sig)
        c = np.clip(d / peak * 1.2, 0, 1)
        cov = np.maximum(cov, c)
        thick = np.maximum(thick, c * 0.6)
    # the fade at the end of the life (the drops fallen out of the card)
    cov *= np.clip((1.0 - t) / 0.15, 0, 1)
    # the normal from the thickness (a sheet bulging over its waves)
    th = gaussian_filter(thick, 1.0)
    gy, gx = np.gradient(th)
    k = 6.0
    nx, ny = -gx * k, -gy * k
    nz = np.ones_like(nx)
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    img = np.stack([0.5 + 0.5 * nx / l, 0.5 + 0.5 * ny / l, np.clip(thick, 0, 1), np.clip(cov, 0, 1)], -1)
    r0, c0 = (f // G) * R, (f % G) * R
    atlas[r0:r0 + R, c0:c0 + R] = (img * 255 + 0.5).astype(np.uint8)
    sys.stdout.write("."); sys.stdout.flush()
print()

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "..", "Textures", "D3D11Client", "spray_sheet.dds")
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
W = H = G * R
hdr = struct.pack("<4s7I44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8, H, W, W * 4, 0, 1)
pf = struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)
dx10 = struct.pack("<5I", 28, 3, 0, 1, 0)       # R8G8B8A8_UNORM, 2D
with open(out, "wb") as fo:
    fo.write(hdr + pf + caps + dx10 + atlas.tobytes())
from PIL import Image
if len(sys.argv) > 2: Image.fromarray(atlas[:, :, 3]).save(sys.argv[2])
print("wrote", os.path.abspath(out))
