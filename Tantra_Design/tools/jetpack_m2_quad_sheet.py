# The sheet of the four-capsule layouts: M1 as the reference, then the seven; renders (blender/jetpack_m2quad.py) and the
# numbers of jetpack_m2_quad.json. Run: python jetpack_m2_quad_sheet.py -> blender/renders/m2quad_sheet.png
import json, os
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); R = os.path.join(HERE, "..", "blender", "renders")
d = json.load(open(os.path.join(HERE, "jetpack_m2_quad.json"), encoding="utf-8"))
F = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 17); FB = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 21); FS = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 15)
W, TXT, ROW = 290, 760, 330
def wrap(t, width, f=F):
    out, cur = [], ""
    for w in t.split(" "):
        if f.getlength(cur + " " + w) > width and cur: out.append(cur); cur = w
        else: cur = (cur + " " + w).strip()
    return out + [cur]
keys = ["0", "1", "2", "3", "4", "5", "6", "7"]
im = Image.new("RGB", (3 * W + TXT, len(keys) * ROW + 110), (250, 250, 248)); dr = ImageDraw.Draw(im)
dr.text((12, 10), "«Наплечный» М2 с четырьмя капсулами, без экранов: верхняя пара — ваш УВТ как есть, нижняя — те же капсулы на своих штангах", fill=(20, 20, 20), font=FB)
for i, l in enumerate(wrap("Тепло — лучистый поток на тело в воздухе, кВт/м², по той же модели для всех (водород 10 км/с догорает в воздухе; корпус ранца заслоняет; экранов нет). "
                           "Эталон — сам М1: точка проходит, если не горячее М1 более чем на 15 % или ниже 4 кВт/м². В вакууме у М1 везде < 1 кВт/м²: вся задача — только для атмосфер. "
                           "Тримм — висение на Земле (163 кг): газ пар, карданы (верх ±20°), крен корпуса против остатка тяги (у М1 он ~12°).", 3 * W + TXT - 24, FS)):
    dr.text((12, 40 + i * 19), l, fill=(60, 60, 60), font=FS)
Y0 = 110
for i, k in enumerate(keys):
    x = d[k]; y0 = Y0 + i * ROW
    for c, vn in enumerate(("rear", "side", "back")):
        p = os.path.join(R, f"m2q_{k}_{vn}.png")
        if os.path.exists(p):
            s = Image.open(p).convert("RGBA"); b = Image.new("RGBA", s.size, (238, 238, 235, 255)); b.alpha_composite(s)
            im.paste(b.convert("RGB").resize((W, W)), (c * W, y0 + (ROW - W) // 2))
    tx = 3 * W + 14; y = y0 + 8
    dr.text((tx, y), f"{k} · {x['name']}", fill=(20, 20, 20), font=FB); y += 28
    if k == "0":
        v = "ЭТАЛОН · в вакууме < 1 кВт/м² везде"; col = (20, 110, 40)
    else:
        v = ("ТЕПЛО НЕ ХУЖЕ М1" if x["heat_ok"] else "ГОРЯЧЕЕ М1 ×%.1f" % x["ratio"]) + " · " + ("ТРИММ ОК" if x["trim"] and x["trim"]["ok"] else "ТРИММ: крен %.0f°" % x["trim"]["lean"] if x["trim"] else "НЕ ВИСИТ")
        col = (20, 110, 40) if x["heat_ok"] and x["trim"] and x["trim"]["ok"] else (150, 90, 0) if x["ratio"] < 2 else (170, 30, 30)
    dr.text((tx, y), v, fill=col, font=FB); y += 28
    for l in wrap(x["note"], TXT - 28): dr.text((tx, y), l, fill=(35, 35, 35), font=F); y += 21
    y += 4
    tr = x["trim"]
    if tr: dr.text((tx, y), f"подъём {x['lift_max']:.0f} Н (Т/В Земля {x['lift_max'] / 1599:.2f}) · висение: верх {tr['gim_u']:+d}° газ {100 * tr['t_u']:.0f} %, низ {tr['gim_l']:+d}° газ {100 * tr['t_l']:.0f} %, крен {tr['lean']:.0f}°", fill=(35, 35, 35), font=F)
    else: dr.text((tx, y), f"подъём {x['lift_max']:.0f} Н (Т/В Земля {x['lift_max'] / 1599:.2f}) — на Земле не висит (Луна 3,9)", fill=(35, 35, 35), font=F)
    y += 26
    rows = x["rows"]; verd = x.get("verdicts", [""] * len(rows)); half = (len(rows) + 1) // 2
    for j, (r, vv) in enumerate(zip(rows, verd)):
        name, q, hit = r; c2 = (35, 35, 35) if k == "0" else (20, 110, 40) if vv == "ок" else (150, 90, 0) if vv == "ЭКРАН" else (170, 30, 30)
        cx = tx + (0 if j < half else 370); cy = y + (j if j < half else j - half) * 19
        dr.text((cx, cy), f"{name}: {q:.1f}" + ("" if k == "0" else " " + vv), fill=c2, font=FS)
im.save(os.path.join(R, "m2quad_sheet.png")); print(im.size)
