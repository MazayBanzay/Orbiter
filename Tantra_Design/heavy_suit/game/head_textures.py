# Pantsir: the person's head gets its own texture copies for the Metalness shader (Textures\Tantra\Astronavigator is only
# read): head_<part>.dds (+ the original _norm/_spec companions) and constant _metl (0) / _rghn (smoothness) maps.
# Also adds the exporter branch that points the Pantsir head groups at these copies.  System Python + Pillow.
import os, shutil
from PIL import Image
R = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
SRC = os.path.join(R, "Textures", "Tantra", "Astronavigator"); DST = os.path.join(R, "Textures", "Tantra", "AstronavigatorPantsir")
SMOOTH = {"Skin": 0.35, "Brows": 0.2, "Lashes": 0.2, "Hair": 0.3, "Eyes": 0.85}
for part, sm in SMOOTH.items():
    base = "head_" + part.lower()
    for suf in ("", "_norm", "_spec"):
        s = os.path.join(SRC, part + suf + ".dds")
        if os.path.exists(s): shutil.copyfile(s, os.path.join(DST, base + suf + ".dds")); print("copied", part + suf, "->", base + suf)
    for suf, v in (("_metl", 0.0), ("_rghn", sm)):
        Image.new("RGB", (64, 64), (int(round(v * 255)),) * 3).save(os.path.join(DST, base + suf + ".dds"), pixel_format="DXT1"); print("made", base + suf, v)

p = os.path.join(R, "Tantra_Design", "blender", "export_skin.py"); b = open(p, "rb").read(); crlf = b"\r\n" in b
s = b.decode("utf-8").replace("\r\n", "\n")
if "the head's own copies" not in s:
    a = '            tex = tex_index(sub + "\\\\" + tname + ".dds") if img and os.path.exists(img) else 0\n'
    assert s.count(a) == 1, "exporter line not found"
    s = s.replace(a, '            if VARIANT == "pantsir" and s in BODY_LABELS and img and os.path.exists(img):   # (pantsir) the head\'s own copies (Metalness maps), copied, not converted\n'
                     '                tex = tex_index(SUIT_TEX_SUB + "\\\\head_" + s.lower() + ".dds")\n'
                     '            else:\n    ' + a)
    open(p, "wb").write((s.replace("\n", "\r\n") if crlf else s).encode("utf-8")); print("exporter: head branch added")
