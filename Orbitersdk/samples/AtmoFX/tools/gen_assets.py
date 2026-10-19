# AtmoFX assets: textures (Textures\AtmoFX\*.dds) and the texture-list mesh (Meshes\AtmoFX\atmofx_tex.msh).
# Geometry itself is built at run time by AtmoFX.dll; the .msh only carries the texture list
# (runtime meshes cannot add textures), in the order of the TEX_* constants in AtmoFX.cpp.
# Run:  python -I gen_assets.py
import os, numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ORB = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
TEX = os.path.join(ORB, "Textures", "AtmoFX")
MSH = os.path.join(ORB, "Meshes", "AtmoFX")
os.makedirs(TEX, exist_ok=True); os.makedirs(MSH, exist_ok=True)

HAZE = np.array([0.80, 0.75, 0.66])          # Jupiter haze at ~0.7 bar (matches the mockups)
HAZE_LV = [0.06, 0.22, 0.45, 0.72]           # per distance band: <40, <120, <300, >300 km

def save(name, rgba):
    """Uncompressed A8R8G8B8 DDS with a full mip chain (tiled decks shimmer without mips)."""
    import struct
    levels = [rgba]
    while min(levels[-1].shape[:2]) > 1:
        p = levels[-1]
        levels.append((p[0::2, 0::2] + p[1::2, 0::2] + p[0::2, 1::2] + p[1::2, 1::2]) / 4)
    h, w = rgba.shape[:2]
    hdr = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0x2100F, h, w, w * 4, 0, len(levels))
    hdr += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += struct.pack("<IIIII", 0x401008, 0, 0, 0, 0)
    with open(os.path.join(TEX, name), "wb") as f:
        f.write(hdr)
        for p in levels:
            b = np.clip(p * 255 + 0.5, 0, 255).astype(np.uint8)
            f.write(b[..., [2, 1, 0, 3]].tobytes())

def fbm(n, beta, seed):
    """Tileable fractal noise via a power-law spectrum (periodic by construction)."""
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)[:, None] ** 2 + np.fft.rfftfreq(n)[None, :] ** 2
    f[0, 0] = 1
    spec = (rng.normal(size=f.shape) + 1j * rng.normal(size=f.shape)) * f ** (-beta / 2)
    spec[0, 0] = 0
    x = np.fft.irfft2(spec, s=(n, n))
    return (x - x.mean()) / (x.std() * 4) + 0.5

def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def deck(name, col, dark, cover, soft, beta, seed, base_alpha=1.0):
    n = fbm(512, beta, seed)
    a = smooth(cover, cover + soft, n) * base_alpha
    sh = smooth(cover, cover + 0.3, n)                       # thicker = brighter tops
    rgb = np.array(dark)[None, None] * (1 - sh[..., None]) + np.array(col)[None, None] * sh[..., None]
    for i, h in enumerate(HAZE_LV):
        r = rgb * (1 - h) + HAZE * h
        al = a * (1 - h) + h * 0.85                         # far deck turns into a haze floor
        save("%s_%d.dds" % (name, i), np.dstack([r, al]))

deck("nh3", (1.0, 0.98, 0.94), (0.70, 0.66, 0.60), 0.45, 0.10, 2.2, 1)
deck("nh4", (0.62, 0.44, 0.30), (0.40, 0.28, 0.20), 0.30, 0.20, 2.0, 2)

# cumulus atlas 4x4: columns = sprite variants, rows = haze level
atlas = np.zeros((1024, 1024, 4))
for k in range(4):
    im = Image.open(os.path.join(HERE, "..", "assets", "cu%d.png" % k)).convert("RGBA").resize((256, 256), Image.LANCZOS)
    s = np.asarray(im).astype(float) / 255
    a = s[..., 3]
    m = a > 0.3
    fill = s[..., :3][m].mean(0) if m.any() else np.array([0.9, 0.9, 0.9])
    rgb = np.where((a < 0.05)[..., None], fill[None, None], s[..., :3])   # no dark fringe under bilinear filtering
    for r, h in enumerate(HAZE_LV):
        atlas[r * 256:(r + 1) * 256, k * 256:(k + 1) * 256, :3] = rgb * (1 - h) + HAZE * h
        atlas[r * 256:(r + 1) * 256, k * 256:(k + 1) * 256, 3] = a * (1 - 0.3 * h)
save("cu_atlas.dds", atlas)

# lightning flash: bright core + wide faint halo
n = 128; y, x = np.mgrid[0:n, 0:n]; r = np.hypot(x - n / 2 + 0.5, y - n / 2 + 0.5) / (n / 2)
a = np.clip(0.95 * np.exp(-(r / 0.16) ** 2) + 0.35 * np.exp(-(r / 0.5) ** 2), 0, 1) * (r < 1)
rgb = np.dstack([0.80 + 0.2 * np.exp(-(r / 0.2) ** 2), 0.88 + 0.12 * np.exp(-(r / 0.2) ** 2), np.ones_like(r)])
save("flash.dds", np.dstack([rgb, a]))

# texture-list mesh: one hidden degenerate group
texs = ["cu_atlas"] + ["nh3_%d" % i for i in range(4)] + ["nh4_%d" % i for i in range(4)] + ["flash"]
with open(os.path.join(MSH, "atmofx_tex.msh"), "w") as f:
    f.write("MSHX1\nGROUPS 1\nLABEL dummy\nMATERIAL 0\nTEXTURE 0\nFLAG 2\nGEOM 3 1\n")
    f.write("0 0 0 0 1 0 0 0\n0 0 0 0 1 0 0 0\n0 0 0 0 1 0 0 0\n0 1 2\n")
    f.write("MATERIALS 0\nTEXTURES %d\n" % len(texs))
    for t in texs: f.write("AtmoFX\\%s.dds\n" % t)
print("ok", len(texs), "textures ->", TEX)
