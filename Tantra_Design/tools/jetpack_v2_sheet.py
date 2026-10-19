# The three chosen, v2 (Лезвие, Исида, Трилистник-гондола): rear, top, side, folded + their numbers (jetpack_sketch.json).
# Run: python jetpack_v2_sheet.py  -> blender/renders/sketch_v2_sheet.png
import json, os
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.join(HERE, "..", "blender", "renders")
d = json.load(open(os.path.join(HERE, "jetpack_sketch.json"), encoding="utf-8"))
F = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 19); FB = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 23)
FS = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 17)
W, H, TXT, BG = 420, 360, 150, (238, 238, 235)
VIEWS = (("rear", "в полёте"), ("top", "сверху"), ("side", "сбоку"), ("fold", "сложен для шага"))
rows = []
for k in ("S8b", "S1b", "S9b"):
    x = d[k]; e = x["Земля +15°C"]; n = k[1:]
    im = Image.new("RGB", (4 * W, H + TXT), (250, 250, 248)); dr = ImageDraw.Draw(im)
    for i, (vn, cap) in enumerate(VIEWS):
        src = Image.open(os.path.join(R, f"sk_{n}_{vn}.png")).convert("RGBA")
        bg = Image.new("RGBA", src.size, (*BG, 255)); bg.alpha_composite(src)
        im.paste(bg.convert("RGB").resize((W, H)), (i * W, 0)); dr.text((i * W + 10, 8), cap, fill=(60, 60, 60), font=FS)
    lines = [(f"{x['name']} — {x['note']}", FB),
             (f"кит {x['mass_kit']:.0f} кг, ранец всего {x['mass_all']:.0f} кг · ширина {x['width']:.2f} м, сложен ~0.70 · Земля: Т/В {e['tw']:.2f},"
              f" с капсулами {e['boost_tw']:.2f}, форсаж {e['force_tw']:.2f} · висение {e['hours']:.1f} ч", F),
             (f"+50°C {x['Земля +50°C']['tw']:.2f} · 3 км {x['Земля 3 км']['tw']:.2f} · Титан {x['Титан']['tw']:.1f} ({x['Титан']['hours']:.0f} ч) · Венера 42 км"
              f" {x['Венера 42 км']['tw']:.2f} · vmax {x['vmax']:.0f}/{x['vmax_boost']:.0f} м/с · крен 15° за {x['t15']:.2f} с", F),
             (f"отказ сота {x['mod_fail']:.2f} (форсаж {x['mod_fail_f']:.2f}) · потеря плеча сот {x['arm_fail']:.2f} на форсаже · отказ ЭБ {x['pu_fail']:.2f}"
              f" (форсаж {x['pu_fail_f']:.2f}, {x['force_s']:.0f} с)", F)]
    for i, (l, f) in enumerate(lines): dr.text((12, H + 8 + i * 34), l, fill=(20, 20, 20), font=f)
    rows.append(im)
sh = Image.new("RGB", (4 * W, sum(r.height for r in rows)), (255, 255, 255)); y = 0
for r in rows: sh.paste(r, (0, y)); y += r.height
sh.save(os.path.join(R, "sketch_v2_sheet.png")); print(sh.size)
