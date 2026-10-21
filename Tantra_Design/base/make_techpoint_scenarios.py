# The D3D11 preview scenarios of the technical point ТП-1 (Brighton Beach, the Moon): Scenarios\D3D11 Preview\*.scn.
# Everything is placed in the building's frame (x east, z north, metres; TechPointGeo.h / build_techpoint.py) and turned
# into longitude / latitude here. The Sun's elevation of each date: Orbiter.log, "TVehicles Building TP-1: the Sun ...".
# Written in cp1251 (the people's and the crates' Russian labels). Run: python make_techpoint_scenarios.py
import math, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "Scenarios", "D3D11 Preview")
LON0, LAT0 = -33.4100, 41.1300                     # the building's origin (where the MPU scenario's vehicles stand)
R = 1737400.0
def pos(x, z):
    lat = LAT0 + math.degrees(z / R); lon = LON0 + math.degrees(x / (R * math.cos(math.radians(LAT0))))
    return "%.7f %.7f" % (lon, lat)

def building(doors="0 0 0 0", gate=0.0, lockp="101.3 101.3", lights="1df"):
    return f"""TP-1:TVehicles\\Building
  STATUS Landed Moon
  POS {pos(0, 0)}
  HEADING 0.00
  ALT 0.000
  TP_DOORS {doors}
  TP_GATE {gate:.3f}
  TP_LOCKP {lockp}
  TP_LIGHTS {lights}
END
"""
def vehicles(heads_near=0, heads_far=0):
    return f"""Station-1:TVehicles\\Station
  STATUS Landed Moon
  POS {pos(12.0, 12.2)}
  HEADING 180.00
  ALT 1.000
  CELLS 1.000 1.000 1.000 0.950 -1.000 -1.000
  PLUGGED MPU-1
END
EMPU-1:EMPU
  STATUS Landed Moon
  POS {pos(6.3, 4.0)}
  HEADING 180.00
  ALT 0.880
  CELLS 0.550 0.600 0.500 -1.000
  PARK 1
  HEADLIGHTS {heads_near}
END
MPU-1:MPU
  STATUS Landed Moon
  POS {pos(18.0, 5.0)}
  HEADING 325.00
  ALT 0.880
  CELLS 0.300 0.250 0.350 -1.000
  PARK 1
  HEADLIGHTS {heads_far}
END
GL-01:DeltaGlider
  STATUS Landed Moon
  POS {pos(2.0, -62.0)}
  HEADING 0.00
  FUEL 1.000
  GEAR 1 1.0
END
Crate-M1:OrbiterCrew\\Item
  STATUS Landed Moon
  POS {pos(-6.0, -3.0)}
  HEADING 20.00
  ALT 0.250
  ITEM 3 18.0 0.50 0.50 0.50 OrbiterCrew\\Crate_M Ящик М: кислород
END
Crate-S1:OrbiterCrew\\Item
  STATUS Landed Moon
  POS {pos(-5.0, -3.6)}
  HEADING 0.00
  ALT 0.375
  ITEM 3 140.0 0.75 0.75 0.75 OrbiterCrew\\Crate_S Ящик S: прожектор
END
Crate-B1:OrbiterCrew\\Item
  STATUS Landed Moon
  POS {pos(-4.4, -2.2)}
  HEADING 5.00
  ALT 0.500
  ITEM 3 260.0 1.00 1.00 1.00 OrbiterCrew\\Crate_B Ящик B: ЗИП
END
"""
def kaira(x, z, hdg, lamp=0):
    # the light suit as on Titan: Suit_T10 «Каркас» with the borazon set (fins), the flight module (Hexa) on her back
    return f"""KairaRean:OrbiterCrew\\AstronavigatorT2
  STATUS Landed Moon
  POS {pos(x, z)}
  HEADING {hdg:.2f}
  ALT 0.930
  AFCMODE 7
  NAME Kaira Rean
  ROLE astronavigator
  SEX female
  SUIT 1
  SUIT_SET Boron
  SUIT_O2 1.0
  SUIT_WATER 2.0
  SUIT_SORBENT 0.0
  SUIT_BATTERY 6.0
  SUIT_N2 1.5
  SUIT_SERVO 1
  JETPACK 1
  JET_FUEL 13
  JET_MODEL 3
  LAMP {lamp}
END
"""
def pantsir(x, z, hdg, lamp=0):
    return f"""Niza_Krit:OrbiterCrew\\AstronavigatorPantsir
  STATUS Landed Moon
  POS {pos(x, z)}
  HEADING {hdg:.2f}
  ALT 0.989
  AFCMODE 7
  NAME Niza Krit
  ROLE astronavigator
  SEX female
  SUIT 1
  SUIT_CLASS Pantsir
  LAMP {lamp}
END
"""
def scenario(fname, desc, mjd, focus, ships, camera="  MODE Cockpit\n  FOV 70.00"):
    os.makedirs(OUT, exist_ok=True)
    txt = f"""BEGIN_DESC
{desc}
END_DESC

BEGIN_ENVIRONMENT
  System Sol
  Date MJD {mjd:.4f}
END_ENVIRONMENT

BEGIN_FOCUS
  Ship {focus}
END_FOCUS

BEGIN_CAMERA
  TARGET {focus}
{camera}
END_CAMERA

BEGIN_HUD
  TYPE Surface
END_HUD

BEGIN_SHIPS
{ships}END_SHIPS
"""
    with open(os.path.join(OUT, fname), "w", encoding="cp1251", newline="\r\n") as f: f.write(txt)
    print("written", fname)

scenario("7 - Moon, technical point TP-1.scn",
         'D3D11 preview 7. The spaceport\'s technical point TP-1 at Brighton Beach, the Moon, day (the Sun ~40 deg up). A demo of people, vehicles, airlocks and lights.\nWHO: Kaira Rean (light suit Suit_T10 with the borazon fins, the flight module on her back) stands in the yard before airlock SHK-1. Niza Krit (heavy suit Pantsir) is in the hangar by the EMPU. F2 / ship list: switch between them.\nWHAT IS HERE: the hall (reception and rest, the duty post BSHCHU, a table and chairs; endless air), room 102 life support and the gas store, room 103 the tool room, airlock SHK-1 to the yard, airlock SHK-2 to the hangar, the hangar (vehicle service: the EMPU, the MPU on the charging station\'s cable), the gate VA-1, crates, the Delta-glider on landing pad PP-1 60 m south (its edge lights).\nWHAT TO DO: 1) Kaira: look at SHK-1\'s door, F - "enter airlock SHK-1": the door opens, she steps in. In the lock F at the inner door - "pass: airlock <-> hall" (the lock fills in 6 s and opens). In the hall sit at the duty post or the table (F at a chair). 2) Through SHK-2 into the hangar: F at its outer door - "go out into the hangar". 3) Niza: the EMPU\'s battery bay is empty - F at the station\'s rack: take a cell, carry it to the EMPU, open its bay cover (F), put the cell in. The MPU is charging on the station\'s cable (F at the drum: take / return the cable). 4) The gate: F at it or at board SHCHU VA-1 - open / close / stop. Lighting board SHCHO-1 by SHK-2\'s door in the hangar: work, duty, emergency lighting, the gate and yard floods. 5) The Delta-glider: walk south to the pad, look at its nose, F - the nose airlock: open, then enter. Inside V shows the ship from outside.\nCONTROLS (the suit): W / S walk / brake, A / D turn, Q / E side step, Shift servo run, Space jump. The mouse turns the view. F: the action at what she looks at (a corner-bracketed node); the mouse wheel picks the line when a node has several. Alt (or the middle button) frees the cursor: click the airlock panels\' buttons and screens inside (they have F menus too), Orbiter\'s own menu and dialogs. L helmet lamps, Shift+L the beam, H the helmet display, K suit on / off (only in air), Shift+V sun shade. The flight module (Kaira): G unfolds it, numpad 0 / . thrust up / down, W / S tilt, J height hold, C landing autopilot, B take off / put on the module.\n',
         52008.0, "KairaRean",
         building(lockp="0.0 0.0") + vehicles() + kaira(-10.0, -2.4, 0.0) + pantsir(9.5, 6.0, 270.0))
