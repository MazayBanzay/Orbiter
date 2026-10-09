# -*- coding: utf-8 -*-
"""Сценарии испытаний «Грани» (cp1251, как у Orbiter) и конфиг корабля -> orbiter/scenarios/*.scn, orbiter/TantraLander.cfg.
Запуск: python make_scenarios.py (build.bat копирует их в Scenarios\\Tantra и Config\\Vessels)."""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "scenarios")
os.makedirs(OUT, exist_ok=True)
KEYS = """Клавиши: 1 висение, 2 переход, 3 полёт, 4 вход, 5 баллистика, 6 полоса, 7 вертикальная посадка, 8 ангар;
G шасси, B форсаж маршевых, K киль, O люк, = / - высота висения (Shift x10), E аварийный режим,
P перекачка в носовой перед входом, N вдув в нос. Тяга маршевых - основная рукоять (Num+/Num-),
в висении ручкой задаётся наклон (автомат держит), рысканием - курс."""
FULL = "ARGON 10291 30851 11867"          # старт: носовой и кормовой полные, крыло 11,87 т (lander_gran_254.json)
ENTRY = "ARGON 0 6300 0"                  # после выхода на орбиту: 6,3 т в кормовом


def ship(lines):
    return "Gran:TantraLander\n" + "".join("  %s\n" % l for l in lines) + "END\n"


def scn(name, desc, env, focus_cam, ships):
    t = "BEGIN_DESC\n%s\n\n%s\nEND_DESC\n\n" % (desc, KEYS)
    t += "BEGIN_ENVIRONMENT\n  System Sol\n  Date MJD %s\nEND_ENVIRONMENT\n\n" % env
    t += "BEGIN_FOCUS\n  Ship Gran\nEND_FOCUS\n\nBEGIN_CAMERA\n  TARGET Gran\n  MODE Extern\n  POS 4.0 %s\nEND_CAMERA\n\n" % focus_cam
    t += "BEGIN_HUD\n  TYPE Surface\nEND_HUD\n\nBEGIN_SHIPS\n" + ships + "END_SHIPS\n"
    t = t.replace("α ~", "угол атаки ~").replace("−", "-")
    open(os.path.join(OUT, name), "w", encoding="cp1251", newline="\r\n").write(t)


scn("Грань - 01 KSC, полоса 33 (полная, взлёт).scn",
    "«Грань» 25,4 м на полосе 33 KSC: полная (212 т), 14 человек, 2 контейнера. Режим «полоса».\n"
    "Взлёт на маршевых: 3 (полёт), основная рукоять на полную; шасси уберётся само в полёте (на земле не уберётся).\n"
    "Висение с экипажем на Земле запрещено (только E - аварийно).",
    "51982.6690545866", "-160.0 15.0",
    ship(["STATUS Landed Earth", "POS -80.6826440 28.5970140", "HEADING 330.01", "RCSMODE 0", "AFCMODE 7",
          "MODE 5", "CREW 14", FULL, "STORE 1.0", "GEARCMD -1"]))
scn("Грань - 02 KSC, без экипажа (висение).scn",
    "«Грань» на полосе 33 KSC без экипажа (висение на Земле разрешено), аргон после входа 6,3 т (~54 с висения при 165 т).\n"
    "1 - висение: ряды чаш выпускаются (~6 с), автомат поднимает на заданную высоту (=/-), ручкой - наклон и смещение;\n"
    "7 - вертикальная посадка: шасси на амортизаторах (обжатие на HUD).",
    "51982.6690545866", "-150.0 12.0",
    ship(["STATUS Landed Earth", "POS -80.6826440 28.5970140", "HEADING 330.01", "RCSMODE 0", "AFCMODE 7",
          "MODE 6", "CREW 0", ENTRY, "STORE 1.0", "HHOLD 30.0"]))
scn("Грань - 03 Луна, Брайтон-Бич (висение и посадка).scn",
    "«Грань» на реголите у Брайтон-Бич: 2 пилота, полная. На Луне висение с экипажем штатно.\n"
    "1 - висение, = / - высота, 7 - вертикальная посадка на амортизаторах; 3 - полёт на маршевых (форсаж B).",
    "52006.7485132526", "-150.0 10.0",
    ship(["STATUS Landed Moon", "POS -33.4600 41.1350", "HEADING 70.00", "RCSMODE 0",
          "MODE 0", "CREW 2", FULL, "STORE 1.0", "HHOLD 40.0"]))
scn("Грань - 04 Низкая орбита (вход).scn",
    "«Грань» на низкой орбите Земли после выхода: 165 т, 6,3 т аргона в кормовом, 14 человек.\n"
    "P - перекачка в носовой (~79 с), торможение маршевыми (развернуть кормой вперёд), затем 4 - вход:\n"
    "концевые 60°, щиток 15°, автомат держит α ~33°; 5 - баллистика днищем (α ~58°, элевоны −25°).",
    "51982.5292925579", "-120.0 20.0",
    ship(["STATUS Orbiting Earth", "RPOS 3626158.96 4307928.18 -3325004.36", "RVEL 6623.108 -3432.497 2656.884",
          "AROT -52.67 -56.93 90.32", "RCSMODE 1", "MODE 2", "CREW 14", ENTRY, "STORE 0.9", "GEARCMD -1"]))
cfg = ("; «Grань» 25.4 m - the lander of Tantra (Orbitersdk/samples/TantraLander)\n"
       "ClassName = TantraLander\nModule = TantraLander\nMeshName = Tantra\\Lander\nSize = 12.7\n")
open(os.path.join(HERE, "TantraLander.cfg"), "w", encoding="ascii", errors="replace", newline="\r\n").write(cfg.replace("«Grань»", "Gran"))
print("ok", sorted(os.listdir(OUT)))
