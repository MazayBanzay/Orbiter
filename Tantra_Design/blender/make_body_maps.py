# Coverall figure: D3D9Client per-pixel maps for the exposed person (run after convert_textures.py). System Python + Pillow + numpy.
#   Skin_norm / Skin_spec  - a procedural pore grain (not from the colour: freckles are pigment); a low skin sheen
#   Hair_norm / Hair_spec  - the hair pack's own baked strand normals and strand highlights
#   Eyes_spec              - a wet, sharp highlight on the eyeball
# D3D9Client: _norm is tangent space (+x along u, +y along v), stored uncompressed RGB; _spec RGB = highlight colour,
# alpha = specular power / 4.
import os
import numpy as np
from PIL import Image, ImageFilter

O = r"C:\Games\Orbiter 2016"
DST = os.path.join(O, "Textures", "Tantra", "Astronavigator")
MPFB = os.path.join(O, "Tantra_Design", "mpfbu", "data")
SKIN = os.path.join(O, "Tantra_Design", "blender", "textures", "skin_clean.png")   # freckles removed (clean_skin.py, 2026-10-02)
HAIR = os.path.join(MPFB, "hair", "toigo_curled_under_bob")
EYES = os.path.join(MPFB, "eyes", "materials", "brown_eye.png")
NORM_SIZE = 1024   # relief does not need the diffuse's resolution; 1024 uncompressed RGB = 3 MB


def save_norm(nrm, name):
    im = Image.fromarray((np.clip(nrm * 0.5 + 0.5, 0, 1) * 255).astype(np.uint8), "RGB")
    if im.width > NORM_SIZE: im = im.resize((NORM_SIZE, NORM_SIZE), Image.LANCZOS)
    im.save(os.path.join(DST, name + "_norm.dds")); print("%-14s %s RGB" % (name + "_norm", im.size))


def save_spec(rgb, power, name, size):
    """rgb: HxWx3 in 0..1, power: Phong exponent (scalar or HxW)"""
    a = np.clip(np.broadcast_to(power, rgb.shape[:2]) / 4.0, 0, 255)
    im = Image.fromarray(np.concatenate([np.clip(rgb, 0, 1) * 255, a[..., None]], 2).astype(np.uint8), "RGBA")
    if im.width > size: im = im.resize((size, size), Image.LANCZOS)
    im.save(os.path.join(DST, name + "_spec.dds"), pixel_format="DXT5"); print("%-14s %s DXT5" % (name + "_spec", im.size))


# --- skin: relief NOT taken from the colour (freckles and blotches are pigment, not bumps): a procedural grain of
# pores - small sunk dots on a faint, fine wavy noise - the same everywhere, independent of the diffuse. Small amplitude.
rng = np.random.default_rng(7); N = NORM_SIZE
pores = np.zeros((N, N), np.float32)
ij = rng.integers(0, N, (int(N * N * 0.06), 2)); pores[ij[:, 0], ij[:, 1]] = -rng.uniform(0.5, 1.0, len(ij))
pores = np.asarray(Image.fromarray(((pores + 1) * 127.5).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.7)), np.float32) / 127.5 - 1
grain = rng.standard_normal((N, N)).astype(np.float32)
grain = np.asarray(Image.fromarray(((grain * 0.25 + 0.5).clip(0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.0)), np.float32) / 255 - 0.5
H = (pores * 0.9 + grain * 0.6) * 0.25  # texel units; slopes mostly under ~3 deg (stronger reads as grime in shadow)
gy, gx = np.gradient(H)         # rows run top-down in the file = -v
nrm = np.stack([-gx, gy, np.ones_like(H)], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
print("skin relief: max slope %.1f deg, 99%% %.1f deg" % (np.degrees(np.arccos(nrm[..., 2].min())), np.degrees(np.arccos(np.percentile(nrm[..., 2], 1)))))
save_norm(nrm, "Skin")
# skin sheen: skin F0 ~3 %; a little more on the lips (where the diffuse is red)
c = np.asarray(Image.open(SKIN).convert("RGB"), np.float32) / 255
red = np.clip((c[..., 0] - c[..., 1]) * 2.5 - 0.35, 0, 1)
spec = (0.035 + 0.025 * red)[..., None] * np.array([1.0, 0.97, 0.94])
save_spec(spec, 18 + 22 * red, "Skin", 1024)

# --- hair: baked strand normals and highlights from the pack (3600 px) -> 1024 normal, 2048 spec
hn = np.asarray(Image.open(os.path.join(HAIR, "BakedHairNORMAL.png")).convert("RGB"), np.float32) / 255 * 2 - 1
save_norm(hn, "Hair")
hs = np.asarray(Image.open(os.path.join(HAIR, "BakedHair_spec.png")).convert("L"), np.float32) / 255
save_spec(hs[..., None] * 0.30 * np.array([1.0, 0.92, 0.82]), 48, "Hair", 2048)

# --- eyes: a wet highlight everywhere on the eyeball (sclera and cornea alike), sharp
ey = Image.open(EYES)
save_spec(np.full((ey.height, ey.width, 3), 0.45), 240, "Eyes", 512)
