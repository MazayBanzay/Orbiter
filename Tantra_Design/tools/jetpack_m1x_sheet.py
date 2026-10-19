# The sheet of M1's finished bodies: M1 as it is and the ten closed bodies (blender/jetpack_m1x.py), each with its
# measures - the fuel it holds (scaled from M1's 12 kg by volume: the same packing round the life-support pack), its
# depth off her back, its width, the fuel's centre behind her - and what it gives for ergonomics and reliability.
# Run: python jetpack_m1x_sheet.py -> blender/renders/m1x_sheet.png
import json, os
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.join(HERE, "..", "blender", "renders")
m = json.load(open(os.path.join(R, "m1x_metrics.json"), encoding="utf-8"))
F = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 16); FB = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 20); FS = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 14)
NOTES = {
 "0": ("эталон", "скруглённый короб с нишей и баллоном; капсулы-яйца"),
 "1": ("база", "короб с большими фасками, три продольных ребра, плоская спинка. Самый простой переход от М1: тот же габарит, +28 % объёма за счёт прямых стенок"),
 "2": ("масса низко", "глубина растёт к пояснице: топливо внизу, центр масс ниже и ближе к её центру; у плеч тонко — голова и штанги свободны. Минус: 29 см за спиной у пояса"),
 "3": ("руки свободны", "толстый под штангами (питание капсул коротким путём), тонкий у пояса — локти и наклоны свободны; центр масс выше"),
 "4": ("локти", "глубина убрана на высоте локтей: назад рука проходит мимо; объём в двух долях. Два пояса — два отсека"),
 "5": ("два бака", "две независимые доли с защищённым хребтом-каналом между ними: пробой одной — вторая держит; полная симметрия левого и правого борта"),
 "6": ("прижат к спине", "оболочка, огибающая спину: всего 16 см глубины, но 64 см ширины, обхватывает бока; самый большой объём; поперечные рёбра — жёсткость"),
 "7": ("грани", "вытянутая шестигранная призма: плоская передняя грань к спине, боковые скосы уводят от локтей; сотовый мотив семьи"),
 "8": ("сумка внизу", "тонкая верхняя плита (шарниры, электроника) и глубокий бак на пояснице; малый объём, зато спина выше пояса почти открыта"),
 "9": ("рёбра", "короб с пятью внешними рёбрами: жёсткость, защита спинки, сброс тепла энергоблока; короб тоньше М1, объём меньше"),
 "10": ("сегменты", "три сегмента с тёмными швами, средний глубже: корпус следует изгибу спины при наклоне; каждый сегмент — свой отсек"),
}
keys = [str(i) for i in range(11)]
CW, CH, TXT = 230, 230, 150
cols = 2; cell_w = 3 * CW + 12; cell_h = CH + TXT
rows = (len(keys) + cols - 1) // cols
im = Image.new("RGB", (cols * cell_w + 12, rows * cell_h + 70), (250, 250, 248)); dr = ImageDraw.Draw(im)
dr.text((12, 10), "«Наплечный» М1 — корпус начисто: закрытые тела, штанги и шарниры как есть, капсулы те же по силуэту, но гранёные (восьмигранник, рёбра)", fill=(20, 20, 20), font=FB)
dr.text((12, 38), "Водород — по объёму от М1 (12 кг при той же упаковке вокруг системы жизнеобеспечения). Глубина — от плоскости спины. Центр топлива — насколько позади её центра масс (чем меньше, тем меньше наклон на тягу).", fill=(60, 60, 60), font=FS)
def wrap(t, width, f=F):
    out, cur = [], ""
    for w in t.split(" "):
        if f.getlength(cur + " " + w) > width and cur: out.append(cur); cur = w
        else: cur = (cur + " " + w).strip()
    return out + [cur]
V0 = m["0"]["volume"]
for i, k in enumerate(keys):
    x0 = 6 + (i % cols) * cell_w; y0 = 70 + (i // cols) * cell_h
    for c, vn in enumerate(("rear", "side", "back")):
        s = Image.open(os.path.join(R, f"m1x_{k}_{vn}.png")).convert("RGBA"); b = Image.new("RGBA", s.size, (238, 238, 235, 255)); b.alpha_composite(s)
        im.paste(b.convert("RGB").resize((CW, CH)), (x0 + c * CW, y0))
    x = m[k]; tag, note = NOTES[k]; fuel = 12.0 * x["volume"] / V0
    dr.text((x0 + 6, y0 + CH + 4), f"{k} · {x['name']} — {tag}", fill=(20, 20, 20), font=FB)
    dr.text((x0 + 6, y0 + CH + 28), f"водород ~{fuel:.0f} кг ({x['volume'] * 1000:.0f} л) · глубина {x['depth'] * 100:.0f} см · ширина {x['width'] * 100:.0f} см · центр топлива {(x['cy'] - 0.085) * 100:.0f} см позади ЦМ", fill=(35, 35, 35), font=F)
    for j, l in enumerate(wrap(note, cell_w - 16, FS)): dr.text((x0 + 6, y0 + CH + 52 + j * 18), l, fill=(50, 50, 50), font=FS)
im.save(os.path.join(R, "m1x_sheet.png")); print(im.size)
