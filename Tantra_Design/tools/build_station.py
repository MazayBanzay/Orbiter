# The charging and cell-exchange station (TVehicles): Meshes\TVehicles\Station.msh + Orbitersdk\samples\TVehicles\StationGeo.h
# (2026-10-16, the user: «реальная многокиловаттная промышленная станция, а не картонная коробка») a 300 kW unit on its own
# concrete pad: the rectifier cabinet (louvred doors, the status display, the emergency stop, the cooling unit on the roof),
# the open cell-exchange rack on the left (six cells - the vehicles' cartridge, build_battery_cell.py - on rails, each slot
# with its docking connector and charge lamp), the motorised cable drum on the right (15 m, the fairlead roller where the
# cable leaves, the plug's holster on the cabinet), conduits, bollards, the pad's hazard edge and the parking line.
# The vessel's origin is 1.0 m above the ground. Orbiter frame (x right, y up, z forward - the station's face is +z).
# Run: python build_station.py   (after build_battery_cell.py: the rack's cells use its cartridge model and mpu_cell.dds)
import os, math, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_battery_cell as bc                               # the one cartridge model (vehicles' bays, her hands, this rack)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MESH = os.path.join(ROOT, "Meshes", "TVehicles", "Station.msh")
GEO = os.path.join(ROOT, "Orbitersdk", "samples", "TVehicles", "StationGeo.h")
G0 = -1.0                                                     # the ground in the vessel frame
MATS = [("Hull", (0.20, 0.23, 0.26)), ("White", (0.82, 0.83, 0.84)), ("Cell", (0.72, 0.75, 0.77)), ("Cable", (0.05, 0.05, 0.06)),
        ("Lamp", (1.0, 0.75, 0.30)), ("Glass", (0.05, 0.10, 0.12)), ("Metal", (0.45, 0.46, 0.48)), ("Pad", (0.55, 0.54, 0.51)),
        ("Warn", (0.86, 0.64, 0.08)), ("Red", (0.72, 0.10, 0.08)), ("Louver", (0.07, 0.08, 0.09)), ("Green", (0.20, 0.85, 0.35)),
        ("Steel", (0.62, 0.63, 0.64))]
MI = {n: i for i, (n, _) in enumerate(MATS)}
EMIT = {"Lamp": 1.0, "Green": 1.0}
# textures: material -> (file, metres per tile), planar projection on the face normal (as build_mpu.py)
TEX = {"Hull": ("MPU\\mpu_composite.dds", 1.0), "Metal": ("MPU\\mpu_metal.dds", 0.5), "Steel": ("MPU\\mpu_metal.dds", 0.5),
       "Pad": ("MPU\\mpu_deck.dds", 2.0), "White": ("MPU\\mpu_white.dds", 1.0), "Cell": ("MPU\\mpu_cell.dds", 0.0)}
TEXLIST = sorted({t for t, _ in TEX.values()})
groups = []                                                   # [label, mi, verts[(p, n, uv|None)], faces]

def grp(label, mi):
    groups.append([label, mi, [], []]); return groups[-1]

def poly(g, pts, n, both=True):
    b = len(g[2])
    for p in pts: g[2].append((p, n, None))
    for k in range(1, len(pts) - 1):
        g[3].append((b, b + k, b + k + 1))
        if both: g[3].append((b, b + k + 1, b + k))

def box(g, lo, hi):
    x0, y0, z0 = lo; x1, y1, z1 = hi
    poly(g, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1))
    poly(g, [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1))
    poly(g, [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0))
    poly(g, [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0))
    poly(g, [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0))
    poly(g, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0))

def cyl(g, axis, c, r, a0, a1, seg=24, caps=True):            # a cylinder along x / y / z from a0 to a1, centre c (the other two)
    def P(ang, a):
        u, v = r * math.cos(ang), r * math.sin(ang)
        return {"x": (a, c[0] + u, c[1] + v), "y": (c[0] + u, a, c[1] + v), "z": (c[0] + u, c[1] + v, a)}[axis]
    def N(ang):
        u, v = math.cos(ang), math.sin(ang)
        return {"x": (0, u, v), "y": (u, 0, v), "z": (u, v, 0)}[axis]
    e = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}[axis]
    ctr = lambda a: {"x": (a, c[0], c[1]), "y": (c[0], a, c[1]), "z": (c[0], c[1], a)}[axis]
    for k in range(seg):
        t0, t1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
        poly(g, [P(t0, a0), P(t1, a0), P(t1, a1), P(t0, a1)], N((t0 + t1) / 2))
        if caps:
            poly(g, [ctr(a0), P(t1, a0), P(t0, a0)], tuple(-x for x in e)); poly(g, [ctr(a1), P(t0, a1), P(t1, a1)], e)

body = grp("Body", MI["Hull"]); white = grp("Panels", MI["White"]); metal = grp("Metal", MI["Metal"])
steel = grp("Steel", MI["Steel"]); warn = grp("Warn", MI["Warn"]); red = grp("Red", MI["Red"]); louv = grp("Louver", MI["Louver"])
pad = grp("Pad", MI["Pad"]); cabw = grp("CableWound", MI["Cable"]); green = grp("ChargeLamps", MI["Green"])

# ---- the pad: 4.6 x 3.6 m of concrete, 6 cm proud of the ground, a hazard edge, the working bay's line before the rack ----
PX0, PX1, PZ0, PZ1, PY = -2.35, 2.25, -1.20, 2.40, G0 + 0.06
box(pad, (PX0, G0 - 0.10, PZ0), (PX1, PY, PZ1))
for x0, x1, z0, z1 in ((PX0, PX1, PZ1 - 0.12, PZ1), (PX0, PX1, PZ0, PZ0 + 0.12), (PX0, PX0 + 0.12, PZ0, PZ1), (PX1 - 0.12, PX1, PZ0, PZ1)):
    for k in range(int(round(((x1 - x0) if x1 - x0 > z1 - z0 else (z1 - z0)) / 0.30))):   # yellow and black dashes
        if k % 2: continue
        if x1 - x0 > z1 - z0: box(warn, (x0 + k * 0.30, PY, z0), (min(x1, x0 + k * 0.30 + 0.30), PY + 0.004, z1))
        else: box(warn, (x0, PY, z0 + k * 0.30), (x1, PY + 0.004, min(z1, z0 + k * 0.30 + 0.30)))
box(white, (-1.85, PY, 0.75), (-1.80, PY + 0.004, 1.95)); box(white, (-0.50, PY, 0.75), (-0.45, PY + 0.004, 1.95))   # the rack's bay
box(white, (-1.85, PY, 1.90), (-0.45, PY + 0.004, 1.95))
for x in (PX1 - 0.35, PX0 + 0.35):                                                   # bollards
    cyl(warn, "y", (x, 1.95), 0.08, PY, PY + 0.95, 16); cyl(steel, "y", (x, 1.95), 0.085, PY + 0.55, PY + 0.65, 16, False)

# ---- the rectifier cabinet: 1.2 x 2.3 x 0.9 m, plinth, louvred doors, display, E-stop, the cooling unit on the roof ----
CX0, CX1, CZ0, CZ1, CB, CT = -0.45, 0.75, -0.50, 0.40, PY + 0.10, PY + 2.30
box(steel, (CX0 - 0.03, PY, CZ0 - 0.03), (CX1 + 0.03, CB, CZ1 + 0.03))                    # plinth
box(body, (CX0, CB, CZ0), (CX1, CT, CZ1))
box(white, (CX0 - 0.04, CT, CZ0 - 0.06), (CX1 + 0.04, CT + 0.06, CZ1 + 0.10))            # the roof cap, overhanging
for x0, x1 in ((CX0 + 0.04, 0.14), (0.16, CX1 - 0.04)):                                 # two doors: seams, hinges, handles
    box(louv, (x0, CB + 0.05, CZ1), (x0 + 0.008, CT - 0.05, CZ1 + 0.006)); box(louv, (x1 - 0.008, CB + 0.05, CZ1), (x1, CT - 0.05, CZ1 + 0.006))
    for k in range(10):                                                                     # louvres, low
        y = CB + 0.12 + k * 0.045
        box(louv, (x0 + 0.06, y, CZ1), (x1 - 0.06, y + 0.022, CZ1 + 0.025))
    box(metal, (x1 - 0.07, CB + 1.00, CZ1), (x1 - 0.04, CB + 1.20, CZ1 + 0.04))
disp = grp("Display", MI["Glass"]); box(disp, (CX0 + 0.10, CB + 1.45, CZ1), (0.10, CB + 1.80, CZ1 + 0.015))
box(louv, (CX0 + 0.08, CB + 1.43, CZ1), (0.12, CB + 1.82, CZ1 + 0.008))                  # the display's bezel
cyl(red, "z", (0.45, CB + 1.62), 0.045, CZ1, CZ1 + 0.05, 16); cyl(warn, "z", (0.45, CB + 1.62), 0.075, CZ1, CZ1 + 0.012, 16)   # E-stop
lampg = grp("Lamp", MI["Lamp"])
for k, g in enumerate((green, lampg, red)):                                              # status lamps: on, charging, fault
    cyl(g, "z", (0.30 + k * 0.10, CB + 1.85), 0.022, CZ1, CZ1 + 0.03, 12)
box(warn, (0.33, CB + 1.30, CZ1), (0.57, CB + 1.38, CZ1 + 0.004))                        # "300 kW DC" plate
box(white, (CX0 + 0.10, CB + 1.95, CZ1), (0.65, CB + 2.08, CZ1 + 0.004))                 # the station's name plate
# the cooling unit on the roof: a box with two fan grilles
box(body, (CX0 + 0.05, CT + 0.06, CZ0 + 0.05), (CX1 - 0.05, CT + 0.42, CZ1 - 0.05))
for x in (-0.12, 0.42):
    cyl(louv, "y", (x, -0.05), 0.24, CT + 0.42, CT + 0.44, 24); cyl(steel, "y", (x, -0.05), 0.04, CT + 0.44, CT + 0.47, 12)
    for a in range(4): box(steel, (x - 0.24, CT + 0.44, -0.05 - 0.008 + (a - 1.5) * 0.12), (x + 0.24, CT + 0.45, -0.05 + 0.008 + (a - 1.5) * 0.12))
cyl(lampg, "y", (CX1 - 0.12, CZ1 - 0.12), 0.06, CT + 0.06, CT + 0.20, 16)
# conduits: the cabinet into the ground, the cabinet to the drum
cyl(steel, "y", (CX1 - 0.15, CZ0 + 0.15), 0.07, PY, CB, 12)
cyl(steel, "x", (CB + 0.25, CZ0 + 0.20), 0.05, CX1, 0.98, 12)

# ---- the cell-exchange rack on the left: steel frame, two rows of three slots, rails, connectors, charge lamps ----
RX0, RX1, RZ0, RZ1 = -2.05, -0.55, -0.40, 0.42
ROWS = (PY + 0.62, PY + 1.17)                                    # the slots' floors (hand height, the upper at the chest)
COLS = (-1.80, -1.30, -0.80)
for x in (RX0, RX1 - 0.05):                                      # the uprights
    for z in (RZ0, RZ1 - 0.05): box(steel, (x, PY, z), (x + 0.05, PY + 1.72, z + 0.05))
for y in ROWS + (PY + 1.68,):                                     # the shelves (and the top)
    box(steel, (RX0, y - 0.04, RZ0), (RX1, y, RZ1))
box(body, (RX0, PY + 0.05, RZ0 - 0.02), (RX1, PY + 1.72, RZ0))   # the back panel
cell_pos = []
for y in ROWS:
    for xc in COLS:
        for dx in (-0.235, 0.235):                                # the slot's side lips (low: the cartridge stays in sight)
            box(metal, (xc + dx - 0.010, y, RZ0 + 0.05), (xc + dx + 0.010, y + 0.07, RZ1))
        for rx in (xc - 0.10, xc + 0.10):                          # guide rails (the cartridge's underside on them)
            box(metal, (rx - 0.018, y, RZ0 + 0.05), (rx + 0.018, y + 0.027, RZ1))
        box(body, (xc - 0.10, y + 0.12, RZ0), (xc + 0.10, y + 0.24, RZ0 + 0.16))   # the docking connector
        cyl(green, "z", (xc + 0.19, y + 0.36), 0.015, RZ1 - 0.05, RZ1 - 0.03, 10)   # the slot's charge lamp
        box(warn, (xc - 0.22, y - 0.04, RZ1), (xc + 0.22, y - 0.01, RZ1 + 0.005))   # the shelf's edge
        cell_pos.append((xc, y + 0.027 + 0.166, -0.10))            # the cartridge's centre (its underside on the rails)

# ---- the cable drum on the right: A-frame, flanges, the wound cable, the drive, the fairlead roller ----
DX0, DX1, DY, DZ, DR = 1.00, 1.55, PY + 0.80, 0.05, 0.42
for x in (DX0 - 0.10, DX1 + 0.04):                                  # the frame's sides: two posts, the hub's beam, the foot
    for z0 in (-0.45, 0.51):
        box(steel, (x, PY, z0), (x + 0.06, DY + 0.08, z0 + 0.08))
    box(steel, (x, DY - 0.08, -0.45), (x + 0.06, DY + 0.08, 0.59))  # the hub's beam
    box(steel, (x, PY, -0.45), (x + 0.06, PY + 0.06, 0.59))         # the foot
reel = grp("Reel", MI["Metal"])
for x in (DX0, DX1 - 0.04):
    cyl(reel, "x", (DY, DZ), DR + 0.10, x, x + 0.04, 32)           # flanges
cyl(reel, "x", (DY, DZ), 0.08, DX0 - 0.12, DX1 + 0.12, 16)          # the axle
cyl(cabw, "x", (DY, DZ), DR - 0.02, DX0 + 0.04, DX1 - 0.04, 32)     # the cable wound on it
for k in range(9):                                                  # its turns
    x = DX0 + 0.06 + k * 0.053
    cyl(cabw, "x", (DY, DZ), DR, x, x + 0.03, 32, False)
box(body, (DX1 + 0.10, DY - 0.16, DZ - 0.16), (DX1 + 0.36, DY + 0.16, DZ + 0.16))   # the drive motor and gearbox
cyl(body, "x", (DY, DZ), 0.12, DX1 + 0.36, DX1 + 0.46, 16)
# the fairlead: two rollers on an arm at the drum's front foot, where the cable leaves
FX, FY, FZ = (DX0 + DX1) / 2, PY + 0.32, 0.62
box(steel, (DX0 - 0.04, PY, FZ - 0.10), (DX0, FY + 0.06, FZ + 0.06)); box(steel, (DX1, PY, FZ - 0.10), (DX1 + 0.04, FY + 0.06, FZ + 0.06))
for dy in (-0.035, 0.035): cyl(metal, "x", (FY + dy, FZ), 0.025, DX0, DX1, 12)
EXIT = (FX, FY, FZ + 0.02)                                          # where the cable leaves the fairlead
# the plug's holster on the cabinet's right face
box(metal, (CX1, CB + 1.05, 0.10), (CX1 + 0.12, CB + 1.30, 0.30)); box(louv, (CX1 + 0.12, CB + 1.08, 0.13), (CX1 + 0.125, CB + 1.27, 0.27))
HOLDER = (CX1 + 0.17, CB + 1.18, 0.20)

# ---- the cells (the vehicles' cartridge), the cable and the plug: groups the module moves ----
cell_grp = []
for k, (x, y, z) in enumerate(cell_pos):
    v, f = bc.build_geo((x, y, z))
    g = grp("Cell%d" % k, MI["Cell"]); cell_grp.append(len(groups) - 1)
    for row in v: g[2].append(((row[0], row[1], row[2]), (row[3], row[4], row[5]), (row[6], row[7])))
    g[3].extend(tuple(t) for t in f)
# the cable: rings along t = 0..1 stored in z (x, y = the ring's offset), stretched and sagged by the module each frame
cab = grp("Cable", MI["Cable"]); cable_grp = len(groups) - 1
SEG, RR, NR = 8, 0.018, 33
rings = [[(RR * math.cos(2 * math.pi * j / SEG), RR * math.sin(2 * math.pi * j / SEG), i / (NR - 1)) for j in range(SEG)] for i in range(NR)]
for i in range(NR - 1):
    for j in range(SEG):
        a, b = rings[i][j], rings[i][(j + 1) % SEG]; c, d = rings[i + 1][(j + 1) % SEG], rings[i + 1][j]
        nn = (math.cos(2 * math.pi * (j + 0.5) / SEG), math.sin(2 * math.pi * (j + 0.5) / SEG), 0)
        poly(cab, [a, d, c, b], nn)
plug = grp("Plug", MI["Hull"]); plug_grp = len(groups) - 1
box(plug, (-0.05, -0.05, -0.10), (0.05, 0.05, 0.06)); box(plug, (-0.035, -0.035, 0.06), (0.035, 0.035, 0.10))
cyl(plug, "z", (0, 0), 0.03, -0.16, -0.10, 12)                    # the strain relief
# The renderer culls a group by the box of its vertices as loaded: the module moves the cable and the plug up to 15 m out,
# so each carries 8 unused corner vertices of a 32 m box (no face uses them; the module leaves them where they are)
CABLE_VTX, PLUG_VTX = len(groups[cable_grp][2]), len(groups[plug_grp][2])
for g in (groups[cable_grp], groups[plug_grp]):
    for sx in (-16, 16):
        for sy in (-16, 16):
            for sz in (-16, 16): g[2].append(((sx, sy, sz), (0, 1, 0), None))

def uv(p, n, tile):
    a = max(range(3), key=lambda k: abs(n[k]))
    u, v = (p[2], p[1]) if a == 0 else (p[0], p[2]) if a == 1 else (p[0], p[1])
    return u / tile, -v / tile

os.makedirs(os.path.dirname(MESH), exist_ok=True)
with open(MESH, "w", encoding="ascii", newline="\r\n") as f:
    used = [g for g in groups if g[2]]
    f.write("MSHX1\nGROUPS %d\n" % len(used))
    for label, mi, vs, fs in used:
        name = MATS[mi][0]; tx = TEX.get(name)
        f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (label, mi + 1, TEXLIST.index(tx[0]) + 1 if tx else 0, len(vs), len(fs)))
        for p, n, t in vs:
            if tx:
                tu, tv = t if t is not None else uv(p, n, tx[1] or 1.0)
                f.write("%.4f %.4f %.4f %.4f %.4f %.4f %.5f %.5f\n" % (*p, *n, tu, tv))
            else: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (*p, *n))
        for a, b, c in fs: f.write("%d %d %d\n" % (a, b, c))
    f.write("MATERIALS %d\n" % len(MATS))
    for n, _ in MATS: f.write("ST_%s\n" % n)
    for n, col in MATS:
        em = EMIT.get(n, 0.0); sp = 0.5 if n in ("Metal", "Glass", "Steel") else 0.15
        if n in TEX: col = (1.0, 1.0, 1.0) if n != "Hull" else (0.75, 0.78, 0.82)   # the texture carries the colour
        f.write("MATERIAL ST_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 20\n%.3f %.3f %.3f 1\n" % (n, *col, *col, sp, sp, sp, *(c * em for c in col)))
    f.write("TEXTURES %d\n" % len(TEXLIST))
    for t in TEXLIST: f.write("%s\n" % t)
gi = {id(g): k for k, g in enumerate([g for g in groups if g[2]])}
with open(GEO, "w", encoding="utf-8", newline="\n") as f:
    f.write("// Written by Tantra_Design/tools/build_station.py - do not edit. Vessel frame (x right, y up, z forward).\n#pragma once\n")
    f.write("namespace st {\nconstexpr int kCells = %d;\nconstexpr int kCellGrp[kCells] = {%s};\n" % (len(cell_pos), ", ".join(str(gi[id(groups[k])]) for k in cell_grp)))
    f.write("constexpr double kCellPos[kCells][3] = {%s};   // the cartridges' centres (the rack: lower row first)\n" % ", ".join("{%.3f, %.3f, %.3f}" % p for p in cell_pos))
    f.write("constexpr int kCableGrp = %d, kPlugGrp = %d, kCableRings = %d;\n" % (gi[id(groups[cable_grp])], gi[id(groups[plug_grp])], NR))
    f.write("constexpr int kCableVtx = %d, kPlugVtx = %d;   // the vertices the module moves (the rest: the culling box)\n" % (CABLE_VTX, PLUG_VTX))
    f.write("constexpr double kExit[3] = {%.3f, %.3f, %.3f}, kHolder[3] = {%.3f, %.3f, %.3f};\n" % (*EXIT, *HOLDER))
    f.write("constexpr double kReel[3] = {%.3f, %.3f, %.3f};   // the drum's front, where the hand takes the cable\n" % (FX, DY, DZ + DR + 0.12))
    f.write("constexpr double kSolid[4] = {%.2f, %.2f, %.2f, %.2f};   // the units' footprint people walk round: x0 x1 z0 z1\n" % (RX0, DX1 + 0.48, -0.55, 0.70))
    f.write("constexpr double kPad[4] = {%.2f, %.2f, %.2f, %.2f}, kPadTop = %.3f;   // the concrete pad: x0 x1 z0 z1\n" % (PX0, PX1, PZ0, PZ1, PY))
    f.write("constexpr double kGround = %.3f;\n}  // namespace st\n" % G0)
print("groups", len([g for g in groups if g[2]]), "->", MESH)
