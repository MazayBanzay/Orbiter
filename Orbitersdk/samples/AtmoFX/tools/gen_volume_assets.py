# AtmoFX volume assets for the D3D9Client fork:
#   Textures\AtmoFX\noise3d.dds    128^3 tileable cloud noise: r perlin-worley, g/b/a worley at 2x/4x/8x
#   Textures\AtmoFX\jup_weather.dds weather map from a jup_flow.py image: rgb albedo, a coverage
# Usage: python -I gen_volume_assets.py noise
#        python -I gen_volume_assets.py weather <flow.png> [flow_h.npy]
import os, sys, struct, numpy as np
from scipy.spatial import cKDTree

HERE = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "Textures", "AtmoFX"))
os.makedirs(TEX, exist_ok=True)

def save2d(name, rgba):
    levels = [rgba]
    while min(levels[-1].shape[:2]) > 1:
        p = levels[-1]; levels.append((p[0::2, 0::2] + p[1::2, 0::2] + p[0::2, 1::2] + p[1::2, 1::2]) / 4)
    h, w = rgba.shape[:2]
    hdr = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0x2100F, h, w, w * 4, 0, len(levels))
    hdr += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += struct.pack("<IIIII", 0x401008, 0, 0, 0, 0)
    with open(os.path.join(TEX, name), "wb") as f:
        f.write(hdr)
        for p in levels: f.write(np.clip(p * 255 + 0.5, 0, 255).astype(np.uint8)[..., [2, 1, 0, 3]].tobytes())

def save3d(name, vol):   # vol: (D, H, W, 4) in 0..1, no mips (the client builds them)
    d, h, w = vol.shape[:3]
    hdr = struct.pack("<4sIIIIIII44x", b"DDS ", 124, 0x80100F, h, w, w * 4, d, 1)
    hdr += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += struct.pack("<IIIII", 0x1008, 0x200000, 0, 0, 0)
    with open(os.path.join(TEX, name), "wb") as f:
        f.write(hdr); f.write(np.clip(vol * 255 + 0.5, 0, 255).astype(np.uint8)[..., [2, 1, 0, 3]].tobytes())

def worley(n, cells, rng):
    pts = rng.random((cells ** 3, 3))
    tree = cKDTree(pts, boxsize=1.0)
    g = (np.arange(n) + 0.5) / n
    Z, Y, X = np.meshgrid(g, g, g, indexing="ij")
    q = np.stack([X.ravel(), Y.ravel(), Z.ravel()], 1)
    d, _ = tree.query(q, k=1, workers=-1)
    d = d.reshape(n, n, n) * cells
    return np.clip(d / 0.9, 0, 1)

def perlin3(n, beta, rng):
    f = (np.fft.fftfreq(n)[:, None, None] ** 2 + np.fft.fftfreq(n)[None, :, None] ** 2 + np.fft.rfftfreq(n)[None, None, :] ** 2)
    f[0, 0, 0] = 1
    s = (rng.normal(size=f.shape) + 1j * rng.normal(size=f.shape)) * f ** (-beta / 2); s[0, 0, 0] = 0
    x = np.fft.irfftn(s, s=(n, n, n), axes=(0, 1, 2)); x = (x - x.mean()) / x.std()
    return np.clip(0.5 + x * 0.22, 0, 1)

def noise():
    n = 128; rng = np.random.default_rng(11)
    w1, w2, w3, w4 = (1 - worley(n, c, rng) for c in (4, 8, 16, 32))
    wf = 0.625 * w1 + 0.25 * w2 + 0.125 * w3
    p = perlin3(n, 3.0, rng)
    pw = np.clip((p - (1 - wf)) / (1 - (1 - wf) + 1e-3), 0, 1)       # perlin remapped by worley: billowy
    pw = 0.6 * pw + 0.4 * wf
    lo, hi = np.percentile(pw, 3), np.percentile(pw, 99.7)
    pw = np.clip((pw - lo) / (hi - lo), 0, 1)                       # use the full range
    g = 0.625 * w2 + 0.25 * w3 + 0.125 * w4
    b = 0.625 * w3 + 0.25 * w4 + 0.125 * w4
    vol = np.stack([pw, g, b, w4], -1)
    save3d("noise3d.dds", vol)
    print("noise3d.dds", vol.shape, [round(float(vol[..., i].mean()), 3) for i in range(4)])

def weather(png, hnpy=None):
    from PIL import Image
    from scipy.ndimage import gaussian_filter
    c = np.asarray(Image.open(png).convert("RGB")).astype(float) / 255
    lum = c.mean(-1)
    cov = (lum - np.percentile(lum, 8)) / (np.percentile(lum, 97) - np.percentile(lum, 8))
    if hnpy and os.path.exists(hnpy):
        h = np.load(hnpy); h = (h - np.percentile(h, 5)) / (np.percentile(h, 99) - np.percentile(h, 5))
        cov = 0.55 * cov + 0.45 * h
    cov = np.clip(gaussian_filter(cov, 0.8, mode="wrap") * 0.85 + 0.12, 0, 1)
    alb = np.clip(gaussian_filter(c, (0.6, 0.6, 0), mode="wrap") ** 0.9 * 1.05, 0, 1)
    save2d("jup_weather.dds", np.dstack([alb, cov]))
    print("jup_weather.dds", c.shape, "coverage mean", round(float(cov.mean()), 3))

def globalmap(png):
    """Stock Jupiter cloud map (extracted from Textures/Jupiter/Archive/Cloud.tree): rgb colour, a coverage."""
    from PIL import Image
    from scipy.ndimage import gaussian_filter
    c = np.asarray(Image.open(png).convert("RGB")).astype(np.float32) / 255
    lum = c.mean(-1)
    lo, hi = np.percentile(lum, 3), np.percentile(lum, 99)
    cov = np.clip((lum - lo) / (hi - lo), 0, 1) ** 0.8 * 0.8 + 0.15          # zones (bright) thick, belts thinner
    cov = gaussian_filter(cov, 1.0, mode="wrap")
    save2d("jup_global.dds", np.dstack([c, cov]))
    print("jup_global.dds", c.shape)

def flowdir(png):
    """Streak direction of the real map (structure tensor): rg = (cos 2a, sin 2a) * 0.5 + 0.5 of the streak angle a
    in (east, south) km axes, b = coherence (how clearly streaked). 2048 x 1024."""
    from PIL import Image
    from scipy.ndimage import gaussian_filter, sobel
    c = np.asarray(Image.open(png).convert("L").resize((2048, 1024), Image.LANCZOS)).astype(np.float32) / 255
    h, w = c.shape
    lat = (0.5 - (np.arange(h) + 0.5) / h) * np.pi
    cl = np.maximum(np.cos(lat), 0.05)[:, None]
    L = gaussian_filter(c, 1.0, mode="wrap")
    gx = sobel(L, axis=1, mode="wrap") / cl          # per km: an east pixel is cos(lat) times shorter
    gy = sobel(L, axis=0, mode="wrap")
    jxx = gaussian_filter(gx * gx, 4.0, mode="wrap"); jyy = gaussian_filter(gy * gy, 4.0, mode="wrap"); jxy = gaussian_filter(gx * gy, 4.0, mode="wrap")
    ag = 0.5 * np.arctan2(2 * jxy, jxx - jyy)          # gradient direction
    a = ag + np.pi / 2                                  # streaks run across the gradient
    tr = jxx + jyy + 1e-9; det = np.sqrt((jxx - jyy) ** 2 + 4 * jxy ** 2)
    coh = np.clip(det / tr, 0, 1)
    # where nothing is streaked, fall back to zonal (east-west) streaks
    a = np.where(coh < 0.15, 0.0, a)
    rgb = np.dstack([np.cos(2 * a) * 0.5 + 0.5, np.sin(2 * a) * 0.5 + 0.5, coh])
    save2d("jup_flowdir.dds", np.dstack([rgb, np.ones((h, w))]))
    print("jup_flowdir.dds", rgb.shape, "coherence mean", round(float(coh.mean()), 3))

if __name__ == "__main__":
    if sys.argv[1] == "noise": noise()
    elif sys.argv[1] == "global": globalmap(sys.argv[2])
    elif sys.argv[1] == "flowdir": flowdir(sys.argv[2])
    else: weather(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
