# Packs a splash bake (tools\bake_splash_blender.py: n_0001..n_0064.png, RGB the view-space normal, A the coverage)
# 8 x 8 into one RGBA8 DDS for the client (Spray.cpp / spray.hlsl):
#   python pack_splash.py <frames folder> <out.dds> [cell px = 256]
# The bake's frame covers the domain seen from the side: x -1.2..4.8 m along the throw, z 0..6 m up (ortho 6 m,
# centred at x 1.8, z 3), the rim's hub at x 0.4, z 0.3; the client places the card by those numbers (Spray.cpp, spray.hlsl).
import sys, os, glob, struct
import numpy as np
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
C = int(sys.argv[3]) if len(sys.argv) > 3 else 256
G = 8
fs = sorted(glob.glob(os.path.join(src, "n_*.png")))[:G * G]
atlas = np.zeros((G * C, G * C, 4), np.uint8)
for k, f in enumerate(fs):
    im = np.asarray(Image.open(f).convert("RGBA").resize((C, C), Image.LANCZOS), np.float32) / 255.0
    a = im[..., 3:4]
    rgb = np.where(a > 0.004, im[..., :3] / np.maximum(a, 1e-3), 0.5)      # un-premultiply the edges (straight normal)
    rgb = np.clip(rgb, 0, 1)
    if a.max() > 0.99 and a.mean() > 0.9: a[:] = 0                             # an empty domain renders as its box
    cell = np.concatenate([rgb, a], -1)
    r, c = (k // G) * C, (k % G) * C
    atlas[r:r + C, c:c + C] = (cell * 255 + 0.5).astype(np.uint8)
W = H = G * C
hdr = struct.pack("<4s7I44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8, H, W, W * 4, 0, 1)
pf = struct.pack("<2I4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)
dx10 = struct.pack("<5I", 28, 3, 0, 1, 0)
os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
with open(out, "wb") as fo:
    fo.write(hdr + pf + caps + dx10 + atlas.tobytes())
print("packed", len(fs), "frames", W, "x", H, "->", out)
