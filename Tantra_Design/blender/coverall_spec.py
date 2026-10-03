# Coverall fabric sheen for D3D9Client (Coverall_Fabric_spec.dds: RGB = highlight colour, alpha = Phong power / 4).
# Run after build_coverall.py. System Python + Pillow + numpy. The parts are told apart by their colour in the diffuse:
# the white body - a matte woven cloth; the dark panels - a denser technical fabric with a soft sheen;
# the piping - a satin cord; the zip - metal teeth.
import os, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
HERE = os.path.dirname(os.path.abspath(__file__)); O = os.path.abspath(os.path.join(HERE, "..", ".."))
c = np.asarray(Image.open(os.path.join(HERE, "textures", "coverall_diffuse.png")).convert("RGB"), np.float32) / 255
lum = c.mean(2); sat = c.max(2) - c.min(2)
red = (c[..., 0] > 0.35) & (c[..., 0] > 1.8 * c[..., 1])
white = (lum > 0.62) & ~red
zip_ = (lum < 0.14) & (sat < 0.06)
spec = np.empty_like(c); power = np.empty(lum.shape, np.float32)
spec[:] = 0.10; power[:] = 14                      # dark panels: soft sheen
spec[white] = 0.05; power[white] = 6               # white cloth: matte
spec[red] = 0.16; power[red] = 24                  # satin piping
spec[zip_] = 0.40; power[zip_] = 48                # metal teeth
a = np.clip(power / 4, 0, 255)
im = Image.fromarray(np.concatenate([spec * 255, a[..., None]], 2).astype(np.uint8))
dst = os.path.join(O, "Textures", "Tantra", "Astronavigator", "Coverall_Fabric_spec.dds")
im.save(dst, pixel_format="DXT5"); print(dst, im.size, "white %.0f%% red %.1f%% zip %.1f%%" % (white.mean() * 100, red.mean() * 100, zip_.mean() * 100))
