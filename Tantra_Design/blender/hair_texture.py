"""Coverall figure: the hair texture recoloured to copper-auburn (keeps the strands' light and shade and the alpha),
then converted to Textures\Tantra\Astronavigator\Hair.dds. Run after build_coverall.py (it writes hair_texture.json)."""
import json, os, subprocess, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
COPPER = np.array([0.66, 0.27, 0.10])     # linear-ish target at mid lightness
man = json.load(open(os.path.join(HERE, "hair_texture.json")))
t = man["textures"][0]
a = np.array(Image.open(t["src"]).convert("RGBA")).astype(float) / 255
lum = a[..., :3] @ np.array([0.30, 0.55, 0.15])
m = a[..., 3] > 0.5
k = lum / max(1e-3, np.median(lum[m]))                 # strand light and shade around 1
rgb = np.clip(COPPER[None, None, :] * k[..., None] ** 1.15, 0, 1)
out = np.concatenate([rgb, a[..., 3:4]], 2)
dst = os.path.join(HERE, "textures", "hair_copper.png"); os.makedirs(os.path.dirname(dst), exist_ok=True)
Image.fromarray((out * 255).astype(np.uint8), "RGBA").save(dst)
t["src"] = dst; json.dump(man, open(os.path.join(HERE, "hair_texture_copper.json"), "w"), indent=1)
subprocess.run([sys.executable, os.path.join(HERE, "convert_textures.py"), os.path.join(HERE, "hair_texture_copper.json")], check=True)
print("hair copper mean", (rgb[m].mean(0) * 255).round())
