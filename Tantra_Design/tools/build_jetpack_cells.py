# The honeycomb jet pack «Соты» (Orbitersdk/OrbiterCrew/JETPACK_FAN.md; the user chose it from five mock-ups, 2026-10-16):
# two wings at her shoulders, each a panel of 34 hexagonal cells - an electric micro-impeller in each (D0.10 m) - fed by
# the core on her back (a metallic hydrogen power unit: the relaxation heat to a generator, the hydrogen then to a fuel
# cell on the air's oxygen). The wings' centres are over the vertical of the common centre of mass; they tilt on their
# spars for the thrust vector (the cells' own differential thrust holds her attitude); folded (the power unit stopped)
# they stand up behind her shoulders. At each wing tip a linear aerospike - the boost on the same hydrogen (thin air,
# vacuum, an emergency): a wedge whose ramp glows when it fires; the hydrogen jet itself is all but invisible by day.
#   Meshes\Tantra\JetPackFanWorn.msh - her model frame at rest (x right, y up, z forward), deployed: posed on her back
#     (bone Spine1) every frame by the module, the wings folded and tilted
#   Meshes\Tantra\JetPackFan.msh     - the pack lying about: the wings folded, origin at the core's centre
#   Config\Tantra\JetPackFan.geo     - the core, the wings' roots and centres, the fold, which group is which
# Run: python build_jetpack_cells.py      (the earlier impeller «Вихрь»: build_jetpack_fan.py, kept for the record)
import os, math
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WORN = os.path.join(ROOT, "Meshes", "Tantra", "JetPackFanWorn.msh")
DROP = os.path.join(ROOT, "Meshes", "Tantra", "JetPackFan.msh")
GEO = os.path.join(ROOT, "Config", "Tantra", "JetPackFan.geo")
MATS = [("White", (0.86, 0.87, 0.88)), ("Dark", (0.06, 0.065, 0.07)), ("Metal", (0.55, 0.56, 0.58)), ("Warn", (0.92, 0.45, 0.08)),
        ("Seam", (0.04, 0.04, 0.05)), ("Ramp", (0.30, 0.24, 0.20))]   # Ramp: the aerospike's ramp - the module makes it glow
MI = {n: i for i, (n, _) in enumerate(MATS)}
TEX = {"White": ("MPU\\mpu_white.dds", 0.5), "Metal": ("MPU\\mpu_metal.dds", 0.3)}
TEXLIST = sorted({t for t, _ in TEX.values()})

# ---- the layout (model frame, metres) ----
CORE_C = (0.0, 0.33, -0.25); CORE_R, CORE_H = 0.14, 0.46       # the power unit: a hexagonal prism behind her back
WY, WZ = 0.62, -0.05                                            # the wings' plane height; their chord's centre (over the CM)
X0, X1 = 0.18, 1.00                                             # the root (outside the helmet) to the tip
CH, TH = 0.50, 0.07                                             # chord, thickness
CELL_R = 0.055                                                  # a cell's circumradius (D0.10 across the corners)
FOLD = 80.0                                                     # deg: folded up about the root, behind her shoulders

groups, order = {}, []
def grp(name, mi):
    if name not in groups: groups[name] = [mi, [], []]; order.append(name)
    return groups[name]

def poly(g, pts, n, both=True):
    b = len(g[1])
    for p in pts: g[1].append((tuple(p), tuple(n)))
    for k in range(1, len(pts) - 1):
        g[2].append((b, b + k, b + k + 1))
        if both: g[2].append((b, b + k + 1, b + k))

def box(g, lo, hi):
    x0, y0, z0 = lo; x1, y1, z1 = hi
    poly(g, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1))
    poly(g, [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1))
    poly(g, [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0))
    poly(g, [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0))
    poly(g, [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0))
    poly(g, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0))

def hexagon(cx, cz, r, rot=0.0):
    return [(cx + r * math.cos(rot + k * math.pi / 3), cz + r * math.sin(rot + k * math.pi / 3)) for k in range(6)]

def hex_prism(g, cx, cz, r, y0, y1, rot=0.0):                   # a vertical hexagonal prism (axis y)
    h = hexagon(cx, cz, r, rot)
    for k in range(6):
        (ax, az), (bx, bz) = h[k], h[(k + 1) % 6]
        mx, mz = (ax + bx) / 2 - cx, (az + bz) / 2 - cz; L = math.hypot(mx, mz)
        poly(g, [(ax, y0, az), (bx, y0, bz), (bx, y1, bz), (ax, y1, az)], (mx / L, 0, mz / L))
    poly(g, [(x, y1, z) for x, z in h], (0, 1, 0)); poly(g, [(x, y0, z) for x, z in h[::-1]], (0, -1, 0))

# ---- the core: the power unit's hexagonal casing, its intake grille on top, the vent, the band ----
cx, cy, cz = CORE_C
core = grp("Core", MI["White"]); dark = grp("CoreDark", MI["Dark"]); met = grp("CoreMetal", MI["Metal"])
hex_prism(core, cx, cz, CORE_R, cy - CORE_H / 2, cy + CORE_H / 2, math.pi / 6)
hex_prism(dark, cx, cz, CORE_R * 0.72, cy + CORE_H / 2, cy + CORE_H / 2 + 0.015, math.pi / 6)
for k in range(5): box(dark, (cx - 0.08, cy - 0.10 + k * 0.045, cz - CORE_R * 0.87 - 0.004), (cx + 0.08, cy - 0.085 + k * 0.045, cz - CORE_R * 0.87))   # the vent, at the back
box(met, (cx - 0.15, cy - CORE_H / 2 + 0.03, cz + 0.10), (cx + 0.15, cy + CORE_H / 2 - 0.03, cz + 0.13))          # the harness plate
box(grp("CoreWarn", MI["Warn"]), (cx - 0.07, cy + 0.12, cz - CORE_R * 0.87 - 0.005), (cx + 0.07, cy + 0.15, cz - CORE_R * 0.87))
for s in (1, -1):                                               # the spars from the core's top to the wings' roots
    box(met, (min(s * 0.05, s * X0), WY - 0.025, -0.22), (max(s * 0.05, s * X0), WY + 0.025, WZ + 0.02))

# ---- each wing: the panel, the honeycomb of cells (top and bottom), the root fitting, the aerospike ----
for side, L in ((1, "R"), (-1, "L")):                           # "R": on +x, her right (index 0 in the module)
    wing = grp("Wing" + L, MI["White"]); cells = grp("Cells" + L, MI["Dark"]); rims = grp("Rims" + L, MI["Metal"])
    xa, xb = side * X0, side * X1
    box(wing, (min(xa, xb), WY - TH / 2, WZ - CH / 2), (max(xa, xb), WY + TH / 2, WZ + CH / 2))
    box(grp("WingWarn" + L, MI["Warn"]), (min(xa, xb), WY + TH / 2, WZ + CH / 2 - 0.02), (max(xa, xb), WY + TH / 2 + 0.002, WZ + CH / 2))   # the leading edge band
    dx = CELL_R * math.sqrt(3) + 0.006; dz = 1.5 * CELL_R + 0.006
    n = 0
    for j in range(-3, 4):
        z = WZ + j * dz
        if abs(z - WZ) > CH / 2 - CELL_R - 0.005: continue
        off = 0.5 * dx if j % 2 else 0.0
        for i in range(0, 12):
            x = X0 + 0.062 + off + i * dx
            if x > X1 - 0.058: continue
            px = side * x; n += 1
            for yy, sgn in ((WY + TH / 2, 1), (WY - TH / 2, -1)):
                hx = hexagon(px, z, CELL_R * 0.86, math.pi / 6)
                poly(cells, [(a, yy + sgn * 0.001, b) for a, b in hx], (0, sgn, 0))
                hr = hexagon(px, z, CELL_R, math.pi / 6)     # the rim: a thin ring between the outer and the inner hexagon
                for k in range(6):
                    (a0, b0), (a1, b1) = hr[k], hr[(k + 1) % 6]; (c0, d0), (c1, d1) = hx[k], hx[(k + 1) % 6]
                    poly(rims, [(a0, yy + sgn * 0.003, b0), (a1, yy + sgn * 0.003, b1), (c1, yy + sgn * 0.003, d1), (c0, yy + sgn * 0.003, d0)], (0, sgn, 0))
            hex_prism(rims, px, z, 0.012, WY + TH / 2, WY + TH / 2 + 0.008, math.pi / 6)   # the rotor's hub
    print("cells per wing", n)
    box(grp("Root" + L, MI["Dark"]), (side * X0 - 0.05, WY - 0.05, WZ - 0.06), (side * X0 + 0.03, WY + 0.05, WZ + 0.06))
    # the aerospike at the tip: a faceted pod, its wedge ramp below (the exit a slot along the chord)
    spike = grp("Spike" + L, MI["White"]); ramp = grp("Ramp" + L, MI["Ramp"])
    sx0, sx1 = side * X1, side * (X1 + 0.08)
    box(spike, (min(sx0, sx1), WY - 0.05, WZ - 0.17), (max(sx0, sx1), WY + 0.05, WZ + 0.17))
    xm = side * (X1 + 0.04)
    for zz0, zz1 in ((WZ - 0.15, WZ + 0.15),):
        poly(ramp, [(side * X1, WY - 0.05, zz0), (side * (X1 + 0.08), WY - 0.05, zz0), (xm, WY - 0.16, zz0)], (0, 0, -1))
        poly(ramp, [(side * X1, WY - 0.05, zz1), (side * (X1 + 0.08), WY - 0.05, zz1), (xm, WY - 0.16, zz1)], (0, 0, 1))
        poly(ramp, [(side * X1, WY - 0.05, zz0), (xm, WY - 0.16, zz0), (xm, WY - 0.16, zz1), (side * X1, WY - 0.05, zz1)], (-side * 0.7, -0.7, 0))
        poly(ramp, [(side * (X1 + 0.08), WY - 0.05, zz0), (xm, WY - 0.16, zz0), (xm, WY - 0.16, zz1), (side * (X1 + 0.08), WY - 0.05, zz1)], (side * 0.7, -0.7, 0))

# ---- write: the worn mesh as built; the lying one with the wings folded, centred on the core ----
def uv(p, n, tile):
    a = max(range(3), key=lambda k: abs(n[k]))
    u, v = (p[2], p[1]) if a == 0 else (p[0], p[2]) if a == 1 else (p[0], p[1])
    return u / tile, -v / tile

def folded(name, p, n):
    side = 1 if name.endswith("R") else -1 if name.endswith("L") else 0
    if side and not name.startswith("Root"):
        a = side * math.radians(FOLD); c, s = math.cos(a), math.sin(a)
        dx, dy = p[0] - side * X0, p[1] - WY
        p = (side * X0 + c * dx - s * dy, WY + s * dx + c * dy, p[2]); n = (c * n[0] - s * n[1], s * n[0] + c * n[1], n[2])
    return (p[0] - CORE_C[0], p[1] - CORE_C[1], p[2] - CORE_C[2]), n

def write(path, xf):
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(order))
        for name in order:
            mi, vs, fs = groups[name]; tx = TEX.get(MATS[mi][0])
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (name, mi + 1, TEXLIST.index(tx[0]) + 1 if tx else 0, len(vs), len(fs)))
            for p, n in vs:
                q, m = xf(name, p, n)
                if tx: f.write("%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*q, *m, *uv(p, n, tx[1])))
                else: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (*q, *m))
            for a, b, c in fs: f.write("%d %d %d\n" % (a, c, b))
        f.write("MATERIALS %d\n" % len(MATS))
        for n, _ in MATS: f.write("JF_%s\n" % n)
        for n, col in MATS:
            sp = 0.5 if n in ("Metal", "Ramp") else 0.25 if n == "White" else 0.1
            if n in TEX: col = (1.0, 1.0, 1.0)
            f.write("MATERIAL JF_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 20\n0 0 0 1\n" % (n, *col, *col, sp, sp, sp))
        f.write("TEXTURES %d\n" % len(TEXLIST))
        for t in TEXLIST: f.write("%s\n" % t)

os.makedirs(os.path.dirname(WORN), exist_ok=True)
write(WORN, lambda name, p, n: (p, n))
write(DROP, folded)
def role(nm):
    if nm.startswith(("Wing", "Cells", "Rims", "Spike", "Ramp")): return "ring"   # the wing's parts tilt and fold with it
    return "core"
with open(GEO, "w", newline="\n") as f:
    f.write("# OrbiterCrew honeycomb jet pack Soty in the crew member's model frame (written by build_jetpack_cells.py)\n")
    f.write("SHELL %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*CORE_C, 2 * CORE_R, CORE_H, 2 * CORE_R))
    for side in (1, -1):                                          # HINGE: the wing's root (the fold); PIVOT: its centre (the tilt)
        f.write("HINGE %.4f %.4f %.4f\nPIVOT %.4f %.4f %.4f\n" % (side * X0, WY, WZ, side * (X0 + X1) / 2, WY, WZ))
    f.write("ROTOR_Y %.4f\nFOLD %.1f\nFOLD_AXIS Z\nBOOST_Y %.4f\nGLOW_MAT %d\n" % (WY, FOLD, WY - 0.16, MI["Ramp"]))
    for k, nm in enumerate(order):
        r = role(nm)
        side = -1 if r == "core" else 0 if nm.endswith("R") else 1
        f.write("GROUP %d %s %d\n" % (k, r, side))
print("groups", len(order), "verts", sum(len(groups[n][1]) for n in order))
