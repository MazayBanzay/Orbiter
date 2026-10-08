# The charging and cell-exchange station (TVehicles): Meshes\TVehicles\Station.msh + Orbitersdk\samples\TVehicles\StationGeo.h
# A cabinet 1.6 x 2.2 x 0.8 m on its feet: a cable reel (15 m, the plug in its holder), a rack of six energy cells (80 kg,
# 250 kWh each) in two rows at hand height, a status display, a beacon. The vessel's origin is 1.0 m above the ground.
# Orbiter frame (x right, y up, z forward - the cabinet's face is +z). Run: python build_station.py
import os, math
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MESH = os.path.join(ROOT, "Meshes", "TVehicles", "Station.msh")
GEO = os.path.join(ROOT, "Orbitersdk", "samples", "TVehicles", "StationGeo.h")
G0 = -1.0                                                   # the ground in the vessel frame
MATS = [("Hull", (0.12, 0.13, 0.15)), ("White", (0.82, 0.83, 0.84)), ("Cell", (0.85, 0.48, 0.12)), ("Cable", (0.06, 0.06, 0.07)),
        ("Lamp", (1.0, 0.75, 0.30)), ("Glass", (0.05, 0.10, 0.12)), ("Metal", (0.45, 0.46, 0.48))]
MI = {n: i for i, (n, _) in enumerate(MATS)}
groups = []                                                  # (label, mi, verts[(p, n)], faces)

def grp(label, mi):
    groups.append([label, mi, [], []]); return groups[-1]

def poly(g, pts, n):
    b = len(g[2])
    for p in pts: g[2].append((p, n))
    for k in range(1, len(pts) - 1): g[3].append((b, b + k, b + k + 1)); g[3].append((b, b + k + 1, b + k))   # both sides (winding-proof)

def box(g, lo, hi):
    x0, y0, z0 = lo; x1, y1, z1 = hi
    poly(g, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1))
    poly(g, [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1))
    poly(g, [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0))
    poly(g, [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0))
    poly(g, [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0))
    poly(g, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0))

def cyl_x(g, c, r, x0, x1, seg=24):                          # a cylinder along x (the reel)
    cy, cz = c
    for k in range(seg):
        a0, a1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
        p = lambda a, x: (x, cy + r * math.cos(a), cz + r * math.sin(a))
        n = (0, math.cos((a0 + a1) / 2), math.sin((a0 + a1) / 2))
        poly(g, [p(a0, x0), p(a1, x0), p(a1, x1), p(a0, x1)][::-1], n)
        poly(g, [(x0, cy, cz), p(a1, x0), p(a0, x0)], (-1, 0, 0)); poly(g, [(x1, cy, cz), p(a0, x1), p(a1, x1)], (1, 0, 0))

body = grp("Body", MI["Hull"]); white = grp("Panels", MI["White"]); metal = grp("Metal", MI["Metal"])
box(body, (-0.80, G0 + 0.10, -0.40), (0.80, G0 + 2.20, 0.40))                      # the cabinet
for x in (-0.70, 0.70):
    for z in (-0.30, 0.30): box(metal, (x - 0.08, G0, z - 0.08), (x + 0.08, G0 + 0.12, z + 0.08))   # feet
box(white, (-0.80, G0 + 2.20, -0.40), (0.80, G0 + 2.26, 0.40))                     # the roof cap
box(white, (-0.81, G0 + 1.55, -0.41), (0.81, G0 + 1.60, 0.41))                     # a band
disp = grp("Display", MI["Glass"]); box(disp, (-0.70, G0 + 1.68, 0.40), (-0.05, G0 + 2.08, 0.42))
lamp = grp("Lamp", MI["Lamp"]); box(lamp, (0.50, G0 + 2.26, -0.06), (0.62, G0 + 2.40, 0.06))
# the rack: six bays in two rows on the left half of the face; a cell in each (its own group, hidden when taken)
CELL = (0.30, 0.30, 0.32)
bays = [(x, y) for y in (G0 + 0.55, G0 + 1.05) for x in (-0.62, -0.30, 0.02)]
for bx, by in bays:
    box(metal, (bx - 0.165, by - 0.17, 0.40), (bx + 0.165, by - 0.15, 0.62))       # shelf
cell_grp = []
for k, (bx, by) in enumerate(bays):
    g = grp("Cell%d" % k, MI["Cell"]); cell_grp.append(len(groups) - 1)
    box(g, (bx - CELL[0] / 2, by - 0.15, 0.42), (bx + CELL[0] / 2, by - 0.15 + CELL[1], 0.42 + CELL[2]))
    box(g, (bx - 0.06, by - 0.15 + CELL[1], 0.55), (bx + 0.06, by - 0.15 + CELL[1] + 0.04, 0.61))   # handle
# the reel on the right half, the cable's exit guide, the plug's holder
reel = grp("Reel", MI["Metal"]); cyl_x(reel, (G0 + 1.05, 0.55), 0.32, 0.20, 0.70)
cabw = grp("CableWound", MI["Cable"]); cyl_x(cabw, (G0 + 1.05, 0.55), 0.26, 0.24, 0.66)
box(metal, (0.36, G0 + 0.55, 0.40), (0.54, G0 + 0.70, 0.90))                       # guide arm
EXIT = (0.45, G0 + 0.62, 0.90)                                                       # where the cable leaves the guide
box(metal, (0.62, G0 + 1.20, 0.40), (0.76, G0 + 1.40, 0.50))                        # the plug's holder
HOLDER = (0.69, G0 + 1.30, 0.55)
# the cable: 16 rings along t = 0..1 stored in z (x, y = the ring's offset), stretched and sagged by the module each frame
cab = grp("Cable", MI["Cable"]); cable_grp = len(groups) - 1
SEG, RR, NR = 8, 0.016, 17
rings = [[(RR * math.cos(2 * math.pi * j / SEG), RR * math.sin(2 * math.pi * j / SEG), i / (NR - 1)) for j in range(SEG)] for i in range(NR)]
for i in range(NR - 1):
    for j in range(SEG):
        a, b = rings[i][j], rings[i][(j + 1) % SEG]; c, d = rings[i + 1][(j + 1) % SEG], rings[i + 1][j]
        nn = (math.cos(2 * math.pi * (j + 0.5) / SEG), math.sin(2 * math.pi * (j + 0.5) / SEG), 0)
        poly(cab, [a, d, c, b], nn)
# the plug at the cable's end (its own group: moved to the end point)
plug = grp("Plug", MI["Cell"]); plug_grp = len(groups) - 1
box(plug, (-0.05, -0.05, -0.08), (0.05, 0.05, 0.08))

os.makedirs(os.path.dirname(MESH), exist_ok=True)
with open(MESH, "w", encoding="ascii", newline="\r\n") as f:
    f.write("MSHX1\nGROUPS %d\n" % len(groups))
    for label, mi, vs, fs in groups:
        f.write("LABEL %s\nMATERIAL %d\nTEXTURE 0\nGEOM %d %d\n" % (label, mi + 1, len(vs), len(fs)))
        for p, n in vs: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (*p, *n))
        for a, b, c in fs: f.write("%d %d %d\n" % (a, b, c))
    f.write("MATERIALS %d\n" % len(MATS))
    for n, _ in MATS: f.write("ST_%s\n" % n)
    for n, col in MATS:
        em = 1.0 if n == "Lamp" else 0.0; sp = 0.5 if n in ("Metal", "Glass") else 0.15
        f.write("MATERIAL ST_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 20\n%.3f %.3f %.3f 1\n" % (n, *col, *col, sp, sp, sp, *(c * em for c in col)))
with open(GEO, "w", encoding="utf-8", newline="\n") as f:
    f.write("// Written by Tantra_Design/tools/build_station.py - do not edit. Vessel frame (x right, y up, z forward).\n#pragma once\n")
    f.write("namespace st {\nconstexpr int kCells = %d;\nconstexpr int kCellGrp[kCells] = {%s};\n" % (len(bays), ", ".join(map(str, cell_grp))))
    f.write("constexpr double kCellPos[kCells][3] = {%s};\n" % ", ".join("{%.3f, %.3f, %.3f}" % (bx, by, 0.58) for bx, by in bays))
    f.write("constexpr int kCableGrp = %d, kPlugGrp = %d, kCableRings = %d;\n" % (cable_grp, plug_grp, NR))
    f.write("constexpr double kExit[3] = {%.3f, %.3f, %.3f}, kHolder[3] = {%.3f, %.3f, %.3f};\n" % (*EXIT, *HOLDER))
    f.write("constexpr double kGround = %.3f;\n}  // namespace st\n" % G0)
print("groups", len(groups), "->", MESH)
