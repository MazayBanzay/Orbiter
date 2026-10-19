# Jupiter cloud-deck texture by advecting "dye" through a flow: alternating zonal jets, elongated vortices,
# drifting curl-noise turbulence; convective outbreaks inject bright, tall cloud that the wind shears into plumes.
# Semi-Lagrangian steps on a periodic grid, so the result tiles. Relief shading from the advected cloud height.
# Usage: python -I jup_flow.py <out.png> [size] [seed] [steps]
import sys, numpy as np
from scipy.ndimage import map_coordinates, gaussian_filter
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else "jup_flow.png"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 768
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else 3
STEPS = int(sys.argv[4]) if len(sys.argv) > 4 else 90
rng = np.random.default_rng(SEED)

def fbm(n, beta, lo=1):
    f = np.fft.fftfreq(n)[:, None] ** 2 + np.fft.rfftfreq(n)[None, :] ** 2
    f[0, 0] = 1
    s = (rng.normal(size=f.shape) + 1j * rng.normal(size=f.shape)) * f ** (-beta / 2)
    s[np.sqrt(f) * n < lo] = 0
    x = np.fft.irfft2(s, s=(n, n)); return x / x.std()

y, x = np.mgrid[0:N, 0:N].astype(float)
Y = y / N

# ---- flow (pixels per step)
U = 1.6
jets = np.zeros(N)
for k, a, ph in ((2, 1.0, 0.2), (3, 0.7, 1.1), (5, 0.35, 2.0), (7, 0.2, 0.4)):
    jets += a * np.sin(2 * np.pi * k * np.arange(N) / N + ph)
jets = U * np.tanh(1.6 * jets)                          # sharper jets, shear zones between
psi_v = np.zeros((N, N))
for _ in range(12):
    cx, cy = rng.uniform(0, N, 2); a = rng.uniform(0.015, 0.09) * N; b = a * rng.uniform(0.4, 0.65)
    g = rng.choice([-1, 1]) * rng.uniform(1.2, 2.2) * a
    dx = (x - cx + N / 2) % N - N / 2; dy = (y - cy + N / 2) % N - N / 2
    psi_v += g * np.exp(-(dx / a) ** 2 - (dy / b) ** 2)
turbA, turbB = fbm(N, 3.6, 3), fbm(N, 3.6, 3)

def vel(t):
    ps = psi_v + (turbA * np.cos(t) + turbB * np.sin(t)) * N * 0.012
    gy, gx = np.gradient(ps)
    return jets[:, None] - gy, gx                         # u (east), v (north)

# ---- initial dye: belts/zones with ragged edges and mottling
m = fbm(N, 2.4)
band = 0.5 + 0.5 * np.tanh(3.0 * np.sin(2 * np.pi * 3 * Y + 0.6 + 0.35 * m))
zone = np.array([0.92, 0.88, 0.79]); belt = np.array([0.64, 0.45, 0.30]); dark = np.array([0.40, 0.28, 0.20])
col = belt * (1 - band[..., None]) + zone * band[..., None]
mot = fbm(N, 1.7, 4)
col = col * (1 + 0.10 * mot[..., None])
col = col * (1 - np.clip(m - 1.2, 0, 1)[..., None] * 0.5) + dark * np.clip(m - 1.2, 0, 1)[..., None] * 0.5
H = 0.3 * band + 0.1 * mot                                # cloud-top height (zones a bit higher)

# convective sites, mostly in belts
sites = []
while len(sites) < 22:
    cx, cy = rng.uniform(0, N, 2)
    if band[int(cy), int(cx)] < 0.5 or rng.random() < 0.25: sites.append((cx, cy, rng.uniform(1.5, 4.0), rng.uniform(0.3, 1.0)))

# characteristic back-tracing: follow each final pixel backwards through the time-varying flow and
# sample the initial dye once (no repeated interpolation blur); outbreak sources met on the way are
# composited front-to-back, so plumes are continuous and sharp.
def src_field(s):
    g = np.zeros((N, N))
    for (cx, cy, r, k) in sites:
        pulse = 0.5 + 0.5 * np.sin(s * 0.21 + cx)
        dx = (x - cx + N / 2) % N - N / 2; dy = (y - cy + N / 2) % N - N / 2
        g += np.exp(-(dx * dx + dy * dy) / (r * r)) * 0.22 * k * pulse
    return np.clip(g, 0, 1)
def smp(f, px, py, order=1):
    return map_coordinates(f, [py, px], order=order, mode="grid-wrap")
px, py = x.copy(), y.copy()
T = np.ones((N, N)); acc = np.zeros((N, N)); Hacc = np.zeros((N, N))
for s in range(STEPS - 1, -1, -1):
    g = smp(src_field(s), px, py)
    age = (STEPS - 1 - s) / STEPS
    acc += T * g; Hacc += T * g * (1.3 - age)       # older plume cloud has spread and sunk
    T *= (1 - g)
    u, v = vel(s * 0.03)
    uu, vv = smp(u, px, py), smp(v, px, py)
    px = (px - uu) % N; py = (py - vv) % N
base = np.dstack([smp(col[..., c], px, py, 3) for c in range(3)])
white = np.array([1.0, 0.99, 0.96])
col = base * T[..., None] + white * acc[..., None]
H = smp(H, px, py, 3) * T + Hacc

# ---- relief and fine convective texture
gran = gaussian_filter(fbm(N, 1.2, 60), 0.6, mode="wrap")
h = gaussian_filter(H, 0.7, mode="wrap") * 5 + 0.03 * gran * (0.3 + band)
gy, gx = np.gradient(h)
shade = np.clip(1 + 1.2 * (-gx * 0.75 - gy * 0.55), 0.45, 1.35)
img = np.clip(col * shade[..., None], 0, 1)
Image.fromarray((img * 255).astype(np.uint8)).save(OUT)
np.save(OUT.rsplit(".", 1)[0] + "_h.npy", h.astype(np.float32))
print("ok", OUT, N, STEPS)
