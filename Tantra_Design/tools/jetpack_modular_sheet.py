# The modular pack's comparison sheets: each variant's row - rear, front, top renders (blender/jetpack_modular.py) and its
# numbers (jetpack_modular.json from jetpack_modular_check.py). Two sheets of five rows + the folded variant 8.
# Run: python jetpack_modular_sheet.py  -> blender/renders/modular_sheet_1.png, modular_sheet_2.png
import json, os
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.join(HERE, "..", "blender", "renders")
import sys
SK = len(sys.argv) > 1 and sys.argv[1] == "sketch"                 # the sketches: sk_<n>_<rear|top|fold>, jetpack_sketch.json
d = json.load(open(os.path.join(HERE, "jetpack_sketch.json" if SK else "jetpack_modular.json"), encoding="utf-8"))
F = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 19); FB = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 23)
S, TXT = 400, 150

def row(k):
    x = d[k]; e = x["Земля +15°C"]; hot = x["Земля +50°C"]; hi = x["Земля 3 км"]; t = x["Титан"]; v = x["Венера 42 км"]
    im = Image.new("RGB", (3 * S, S + TXT), (245, 245, 242)); dr = ImageDraw.Draw(im)
    if SK:
        n = k[1:]
        views = ["rear", "top"] + (["fold"] if os.path.exists(os.path.join(R, f"sk_{n}_fold.png")) else [])
        for i, vn in enumerate(views):
            src = Image.open(os.path.join(R, f"sk_{n}_{vn}.png")).convert("RGB"); w, h = src.size
            c = Image.new("RGB", (w, w), src.getpixel((w // 2, h - 2))); c.paste(src, (0, (w - h) // 2))   # fit, not crop
            im.paste(c.resize((S, S)), (i * S, 0))
        if len(views) == 3: dr.text((2 * S + 10, 40), "сложен для шага", fill=(30, 30, 30), font=FB)
    else:
        views = ["rear", "front", "fold" if k == "8" else "top"]
        for i, vn in enumerate(views): im.paste(Image.open(os.path.join(R, f"mod_{k}_{vn}.png")).resize((S, S)), (i * S, 0))
        if k == "8": dr.text((2 * S + 10, 8), "сложен для шага", fill=(255, 255, 255), font=FB)
    fw = f" (сложен {x['fold_w']:.2f})" if x["fold_w"] != x["width"] else ""
    lines = [(f"№{k.lstrip('S')} {x['name']} — {x['note']}", FB),
             (f"кит {x['mass_kit']:.0f} кг, ранец всего {x['mass_all']:.0f} кг · ширина {x['width']:.2f} м{fw} · Земля: Т/В {e['tw']:.2f}, с капсулами {e['boost_tw']:.2f},"
              f" на форсаже {e['force_tw']:.2f} · висение {e['hours']:.1f} ч", F),
             (f"+50°C {hot['tw']:.2f} · 3 км {hi['tw']:.2f} · Титан {t['tw']:.1f} ({t['hours']:.0f} ч) · Венера 42 км {v['tw']:.2f} · vmax {x['vmax']:.0f} / {x['vmax_boost']:.0f} м/с"
              f" с капсулами · крен 15° за {x['t15']:.2f} с", F),
             (f"отказ сота: {x['mod_fail']:.2f} (форсаж {x['mod_fail_f']:.2f}) · потеря плеча сот: {x['arm_fail']:.2f} на форсаже · отказ ЭБ: {x['pu_fail']:.2f}"
              f" (форсаж {x['pu_fail_f']:.2f}) · форсажа {x['force_s']:.0f} с", F)]
    for i, (l, f) in enumerate(lines): dr.text((12, S + 8 + i * 34), l, fill=(20, 20, 20), font=f)
    return im

keys = list(d)
for n, part in enumerate((keys[:5], keys[5:]), 1):
    rows = [row(k) for k in part]
    sh = Image.new("RGB", (3 * S, sum(r.height for r in rows)), (255, 255, 255)); y = 0
    for r in rows: sh.paste(r, (0, y)); y += r.height
    sh.save(os.path.join(R, f"{'sketch' if SK else 'modular'}_sheet_{n}.png")); print(n, sh.size)
