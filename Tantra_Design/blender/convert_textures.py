# Converts the export manifest's textures to DDS for Orbiter (DXT1 opaque, DXT5 with alpha). System Python + Pillow.
import json, os, sys
from PIL import Image
man = json.load(open(sys.argv[1]))
root = os.path.join(man["orbiter"], "Textures")
SIZE = {r"\skin.dds": 2048, r"\hair.dds": 2048, r"\eyes.dds": 1024, "suit_diffuse": 2048, "surf_": 512, "body": 2048, "coverall": 2048, "hair": 1024, "bob": 1024, "shoes": 1024, "eyebrow": 512, "eyelash": 512, "low-poly": 256}
for t in man["textures"]:
    dst = os.path.join(root, t["dds"]); os.makedirs(os.path.dirname(dst), exist_ok=True)
    im = Image.open(t["src"]).convert("RGBA")
    lim = next((v for k, v in SIZE.items() if k in t["dds"].lower()), 1024)
    if max(im.size) > lim:
        s = lim / max(im.size); im = im.resize((max(4, int(im.width * s)), max(4, int(im.height * s))), Image.LANCZOS)
    has_alpha = t["alpha"] and im.getchannel("A").getextrema()[0] < 250
    im.save(dst, pixel_format="DXT5" if has_alpha else "DXT1")
    print("%-60s %s %s" % (t["dds"], im.size, "DXT5" if has_alpha else "DXT1"))
    nsrc = os.path.splitext(t["src"])[0] + "_norm.png"
    if os.path.exists(nsrc):
        ndst = os.path.splitext(dst)[0] + "_norm.dds"
        nm = Image.open(nsrc).convert("RGB")
        if max(nm.size) > lim: k = lim / max(nm.size); nm = nm.resize((max(4, int(nm.width * k)), max(4, int(nm.height * k))), Image.LANCZOS)
        nm.save(ndst)   # uncompressed R8G8B8: the best quality D3D9Client reads for _norm
        print("%-60s %s RGB" % (t["dds"].replace(".dds", "_norm.dds"), nm.size))
    rsrc = os.path.splitext(t["src"])[0] + "_refl.png"
    if os.path.exists(rsrc):
        rm = Image.open(rsrc).convert("RGB"); rm.save(os.path.splitext(dst)[0] + "_refl.dds", pixel_format="DXT1")
        print("%-60s %s DXT1" % (t["dds"].replace(".dds", "_refl.dds"), rm.size))
