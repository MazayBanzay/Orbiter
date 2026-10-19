# Adds the "pantsir" variant (one VARIANT entry + clearly marked minimal branches) to Tantra_Design/blender/export_skin.py
import os
p = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "blender", "export_skin.py"))
s = open(p, encoding="utf-8").read()
if '"pantsir":' in s: print("already patched"); raise SystemExit
BS = chr(92)


def R(a, b):
    global s
    assert s.count(a) == 1, a; s = s.replace(a, b)


R('''                                         "suit2": ("astronavigator_suit2.blend", "AstronavigatorSuit2", "AstronavigatorSuit2", "anim_suit2")}[VARIANT]''',
  '''                                         "suit2": ("astronavigator_suit2.blend", "AstronavigatorSuit2", "AstronavigatorSuit2", "anim_suit2"),
                                         # (2026-10-17, pantsir session) the heavy suit «Панцирь» P3 (Tantra_Design/heavy_suit/game)
                                         "pantsir": ("astronavigator_pantsir.blend", "AstronavigatorPantsir", "AstronavigatorPantsir", "anim_pantsir")}[VARIANT]''')
a = 'SUIT_TEX_SUB = "Tantra' + BS + BS + 'AstronavigatorSuit"      # textures that belong to the suit; the body keeps its own\n'
R(a, a + 'if VARIANT == "pantsir": SUIT_TEX_SUB = "Tantra' + BS + BS + 'AstronavigatorPantsir"   # (pantsir) its own texture folder\n'
     'PANTSIR_SPEC = {}                                   # (pantsir) Orbiter material name -> specular, from the blend\'s materials\n')
R('''            mat_i = mat_index(lname, ((1, 1, 1) if tex else base_color(mat)[:3]) + (a,))''',
  '''            mkey = lname
            if VARIANT == "pantsir" and mat is not None and mat.get("orb_name"):   # (pantsir) one Orbiter material per surface kind, shared by its groups
                mkey = mat["orb_name"]; PANTSIR_SPEC[mkey] = mat.get("orb_spec", "0.1 0.1 0.1 1 8")
            mat_i = mat_index(mkey, ((1, 1, 1) if tex else base_color(mat)[:3]) + (a,))''')
R('''            spec = "1.0 0.85 0.5 1 90" if k.startswith("SunShade") else "0.5 0.5 0.5 1 40" if _a < 1 else "0.1 0.1 0.1 1 8"
''', '''            spec = "1.0 0.85 0.5 1 90" if k.startswith("SunShade") else "0.5 0.5 0.5 1 40" if _a < 1 else "0.1 0.1 0.1 1 8"
            spec = PANTSIR_SPEC.get(k, spec)                                   # (pantsir)
''')
open(p, "w", encoding="utf-8", newline="").write(s); print("patched", p)
