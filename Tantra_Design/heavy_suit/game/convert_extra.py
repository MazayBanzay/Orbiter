# Pantsir: the companion maps convert_textures.py does not handle (_rghn smoothness, _metl metalness, _heat glow) -> DDS next to
# the colour textures in Textures\Tantra\AstronavigatorPantsir\.  System Python + Pillow.
import os, glob
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
DST = os.path.abspath(os.path.join(HERE, "..", "..", "..", "Textures", "Tantra", "AstronavigatorPantsir"))
os.makedirs(DST, exist_ok=True)
for src in sorted(sum((glob.glob(os.path.join(HERE, "tex", "*_%s.png" % k)) for k in ("rghn", "metl", "heat", "emis")), [])):
    im = Image.open(src).convert("RGB")
    if max(im.size) > 512: im = im.resize((512, 512), Image.LANCZOS)
    dst = os.path.join(DST, os.path.splitext(os.path.basename(src))[0] + ".dds"); im.save(dst, pixel_format="DXT1")
    print("%-50s %s DXT1" % (os.path.relpath(dst, DST), im.size))
