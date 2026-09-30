# Converts the export manifest's textures to DDS for Orbiter (DXT1 opaque, DXT5 with alpha). System Python + Pillow.
import json, os, sys
from PIL import Image
man = json.load(open(sys.argv[1]))
root = os.path.join(man["orbiter"], "Textures")
SIZE = {"body": 2048, "coverall": 2048, "hair": 1024, "bob": 1024, "shoes": 1024, "eyebrow": 512, "eyelash": 512, "low-poly": 256}
for t in man["textures"]:
    dst = os.path.join(root, t["dds"]); os.makedirs(os.path.dirname(dst), exist_ok=True)
    im = Image.open(t["src"]).convert("RGBA")
    lim = next((v for k, v in SIZE.items() if k in t["dds"].lower()), 1024)
    if max(im.size) > lim:
        s = lim / max(im.size); im = im.resize((max(4, int(im.width * s)), max(4, int(im.height * s))), Image.LANCZOS)
    has_alpha = t["alpha"] and im.getchannel("A").getextrema()[0] < 250
    im.save(dst, pixel_format="DXT5" if has_alpha else "DXT1")
    print("%-60s %s %s" % (t["dds"], im.size, "DXT5" if has_alpha else "DXT1"))
