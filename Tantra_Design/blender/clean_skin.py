# Skin without freckles (user, 2026-10-02: "веснушки убери, или сделай хорошо"). System Python + Pillow + numpy.
# The same artist's bronze variant has the same layout and no freckles: its fine detail (pores, lips, eyes, creases) is
# kept, and its overall tone is replaced by the ginger skin's, blurred wide enough that freckles do not survive in it -
# so her complexion stays the same, only the spots go.
import os
import numpy as np
from PIL import Image, ImageFilter

O = r"C:\Games\Orbiter 2016"
SK = os.path.join(O, "Tantra_Design", "mpfbu", "data", "skins")
TONE = os.path.join(SK, "toigo_light_skin_female_ginger", "young_lightskinned_female_diffuse_ginger.png")
DETAIL = os.path.join(SK, "toigo_light_skin_female_bronze", "young_lightskinned_female_diffuse_Bronze.png")
OUT = os.path.join(O, "Tantra_Design", "blender", "textures", "skin_clean.png")
SIGMA = 14   # px at 2048: freckles are 2..6 px; features (lips, brows, eye corners) live in the detail layer

def blur(a): return np.asarray(Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(SIGMA)), np.float32) / 255

tone = np.asarray(Image.open(TONE).convert("RGB"), np.float32) / 255
det = np.asarray(Image.open(DETAIL).convert("RGB").resize((tone.shape[1], tone.shape[0]), Image.LANCZOS), np.float32) / 255
# freckles darken the ginger's mean a little; take the tone from its brighter half (a local median-ish estimate)
lo = blur(tone); tone_clean = np.where(tone < lo, lo, tone); base = blur(tone_clean)
out = np.clip(det / np.maximum(blur(det), 1e-3) * base, 0, 1)
Image.fromarray((out * 255).astype(np.uint8), "RGB").save(OUT)
print("skin_clean.png", out.shape)
