"""Io surface microtextures for D3D9Client (Config/MicroTex.cfg, BODY Io): three seamless 2048^2 DXT5 maps in the
client's format - normal x in .a, normal y in .g, luminance in .b (and .r), neutral 128 - blended by the planet shader
when the camera is within ~15 km.
  A  42 px/m (2.4 cm/px, 49 m period): grains of SO2 frost and sulfur, vesicles (gas pits) of the basalt crust
  B   6 px/m (17 cm/px, 340 m period): wrinkled crust, small bumps
  C 0.8 px/m (1.25 m/px, 2.6 km period): gentle undulations
Seamless by construction: spectral (FFT) noise is periodic.
    python microtex.py
"""
import os, numpy as np
from PIL import Image

N = 2048
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
rng = np.random.default_rng(2026)

def spectral(beta, kmin=1, kmax=N // 2):
    """periodic noise with power ~ k^-beta between kmin and kmax (cycles per texture), unit std"""
    kx = np.fft.fftfreq(N) * N; ky = np.fft.fftfreq(N) * N
    K = np.hypot(*np.meshgrid(kx, ky))
    amp = np.where((K >= kmin) & (K <= kmax), np.maximum(K, 1) ** (-beta / 2), 0)
    z = (rng.normal(size=(N, N)) + 1j * rng.normal(size=(N, N))) * amp
    f = np.real(np.fft.ifft2(z))
    return (f - f.mean()) / (f.std() + 1e-12)

def pits(n, rmin, rmax):
    """n periodic round pits (vesicles) of radius rmin..rmax px, depth 1"""
    h = np.zeros((N, N))
    y, x = np.mgrid[0:N, 0:N]
    for cx, cy, r in zip(rng.uniform(0, N, n), rng.uniform(0, N, n), rng.uniform(rmin, rmax, n)):
        x0, x1 = int(cx - 2 * r), int(cx + 2 * r) + 1; y0, y1 = int(cy - 2 * r), int(cy + 2 * r) + 1
        xs = np.arange(x0, x1) % N; ys = np.arange(y0, y1) % N
        X, Y = np.meshgrid(np.arange(x0, x1), np.arange(y0, y1))
        d = np.hypot(X - cx, Y - cy) / r
        h[np.ix_(ys, xs)] -= np.clip(1 - d * d, 0, 1) ** 0.7
    return h

def save_dxt5_mips(img, path):
    """DXT5 with the full mip chain down to 4x4 written into the file: D3DX then loads it as is (without mips it built
    them at every start, ~0.42 s per 2048^2 map). Each level is compressed by Pillow; the files' block data are joined
    under one header with DDSD_MIPMAPCOUNT."""
    import io, struct
    levels, im = [], Image.fromarray(img)
    while True:
        bio = io.BytesIO(); im.save(bio, format='DDS', pixel_format='DXT5'); data = bio.getvalue()
        levels.append(data)
        if im.width <= 4: break
        im = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
    hdr = bytearray(levels[0][:128])
    flags = struct.unpack_from('<I', hdr, 8)[0] | 0x20000                 # DDSD_MIPMAPCOUNT
    struct.pack_into('<I', hdr, 8, flags)
    struct.pack_into('<I', hdr, 28, len(levels))                          # dwMipMapCount
    caps = struct.unpack_from('<I', hdr, 108)[0] | 0x8 | 0x400000         # DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    struct.pack_into('<I', hdr, 108, caps)
    with open(path, 'wb') as f:
        f.write(bytes(hdr))
        for d in levels: f.write(d[128:])

def encode(h, lum, strength, path):
    """height (px units, any scale) -> normals; lum: -1..1 brightness variation"""
    gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength
    gy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * strength
    nx, ny = -gx, gy                                   # image rows go south: flip y like the stock maps
    l = np.sqrt(1 + nx * nx + ny * ny); nx /= l; ny /= l
    a = np.clip(128 + 127 * nx, 0, 255); g = np.clip(128 + 127 * ny, 0, 255)
    b = np.clip(128 + 60 * lum, 0, 255)
    img = np.stack([b, g, b, a], -1).astype(np.uint8)    # r = b (as in the stock maps)
    save_dxt5_mips(img, path)
    print(os.path.basename(path), 'std a/g/b', img[..., 3].std().round(1), img[..., 1].std().round(1), img[..., 2].std().round(1))

def build():
    tex = os.path.join(ROOT, 'Textures')
    # A: grain + vesicles
    hA = 0.6 * spectral(1.2, 40, 1024) + 0.8 * pits(9000, 2, 7)
    lA = 0.5 * spectral(1.0, 60, 1024) + 0.25 * np.clip(-pits(0, 1, 2), 0, 1)
    encode(hA, np.clip(lA, -1, 1) * 0.35, 0.6, os.path.join(tex, 'D3D9Io_A.dds'))
    # B: wrinkled crust (ridged), small bumps
    r = spectral(1.8, 8, 600)
    hB = 1.0 - np.abs(r) + 0.5 * spectral(2.2, 4, 200)
    encode(hB * 3.0, np.clip(spectral(1.6, 6, 300), -1, 1) * 0.30, 0.2, os.path.join(tex, 'D3D9Io_B.dds'))
    # C: gentle undulations
    hC = spectral(2.6, 2, 400)
    encode(hC * 6.0, np.clip(spectral(2.0, 2, 200), -1, 1) * 0.20, 0.35, os.path.join(tex, 'D3D9Io_C.dds'))
    # Config/MicroTex.cfg: add (or keep) the Io block
    cfg = os.path.join(ROOT, 'Config', 'MicroTex.cfg')
    s = open(cfg, encoding='utf-8', errors='replace').read()
    if '\nBODY Io\n' not in s:
        s = s.rstrip('\n') + ('\n\n// Io (Tantra_Design/io_lava/microtex.py): frost/sulfur grain and vesicles, wrinkled crust, undulations\n'
                              'BODY Io\nNORMALS 1\nLEVEL 0 D3D9Io_A.dds 42.0\nLEVEL 1 D3D9Io_B.dds 6.0\nLEVEL 2 D3D9Io_C.dds 0.8\n')
        open(cfg, 'w', encoding='utf-8').write(s)
        print('MicroTex.cfg: BODY Io added')

if __name__ == '__main__':
    build()
