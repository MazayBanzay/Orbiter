# The impeller jet pack «Вихрь» (Orbitersdk/OrbiterCrew/JETPACK_FAN.md), v2 (2026-10-16, the user: the rings behind her
# pull her back, the size is not explained by the mechanism, no thrust vector): the rings stand BESIDE her, their axes
# through the vertical of the common centre of mass and 0.4 m above it (balanced; held upright by the active control -
# a thrust along her body does not right her by itself, the "pendulum" is a fallacy); the smallest
# duct 110 kW carry her on at sea level (D0.50 m, a duct 0.24 m tall); the mechanism shown - the turboshaft core on
# her back (its casing is the metallic hydrogen tank), a drive shaft in each boom to a bevel gearbox in the fan's hub,
# the rotor on it, the straightening vanes (one of them the shaft's fairing), the control vanes at the exit and the
# hub's rocket boost nozzle (the same hydrogen: thin air, vacuum, the rotors' spool-up). The rings swing on gimbals at
# the booms' ends (the module tilts them); the booms do not fold.
#   Meshes\Tantra\JetPackFanWorn.msh - the crew member's model frame at rest (x right, y up, z forward): the module poses
#     it on her back (bone Spine1) every frame, tilts the rings, spins the rotors
#   Meshes\Tantra\JetPackFan.msh     - the pack lying about, origin at the core's centre
#   Config\Tantra\JetPackFan.geo     - the core, the gimbals, the rotors' height and which group is which
# Run: python build_jetpack_fan.py
import os, math
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WORN = os.path.join(ROOT, "Meshes", "Tantra", "JetPackFanWorn.msh")
DROP = os.path.join(ROOT, "Meshes", "Tantra", "JetPackFan.msh")
GEO = os.path.join(ROOT, "Config", "Tantra", "JetPackFan.geo")
MATS = [("White", (0.86, 0.87, 0.88)), ("Dark", (0.08, 0.09, 0.10)), ("Metal", (0.50, 0.51, 0.53)), ("Warn", (0.92, 0.45, 0.08)),
        ("Seam", (0.04, 0.04, 0.05)), ("Nozzle", (0.30, 0.22, 0.18))]
MI = {n: i for i, (n, _) in enumerate(MATS)}
TEX = {"White": ("MPU\\mpu_white.dds", 0.5), "Metal": ("MPU\\mpu_metal.dds", 0.3)}
TEXLIST = sorted({t for t, _ in TEX.values()})

# ---- the layout (model frame, metres) ----
CORE_C = (0.0, 0.35, -0.245); CORE_H = (0.19, 0.25, 0.115)          # the core (tank + turboshaft) behind the suit's back
GB_Y, GB_Z = 0.44, -0.20                                             # the shafts leave the core's sides here
RX, RY, RZ = 0.62, 0.42, -0.05                                       # the rings' centres: beside her, over the common CM
DUCT0, DUCT1 = -0.12, 0.12                                           # the duct from its exit to its lip (about RY)
RIN, ROUT, RLIP = 0.255, 0.275, 0.300
ROT_DY, HUB_R, BLADES = 0.05, 0.075, 7                               # the rotor's plane over the ring's centre

groups, order = {}, []
def grp(name, mi):
    if name not in groups: groups[name] = [mi, [], []]; order.append(name)
    return groups[name]

def poly(g, pts, n, both=True):                                      # both sides: the winding never hides a face
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

def lathe(g, cx, cz, prof, seg=36):
    """a surface of revolution about the vertical axis at (cx, cz): prof = [(r, y), ...]"""
    for k in range(seg):
        a0, a1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
        for (r0, y0), (r1, y1) in zip(prof, prof[1:]):
            am = (a0 + a1) / 2; dr, dy = r1 - r0, y1 - y0; L = math.hypot(dr, dy) or 1
            n = (dy / L * math.cos(am), -dr / L, dy / L * math.sin(am))
            P = lambda r, y, a: (cx + r * math.cos(a), y, cz + r * math.sin(a))
            poly(g, [P(r0, y0, a0), P(r0, y0, a1), P(r1, y1, a1), P(r1, y1, a0)], n)

def tube(g, a, b, r, seg=14):                                         # a cylinder from point a to point b
    ax = [b[i] - a[i] for i in range(3)]; L = math.sqrt(sum(c * c for c in ax)); ax = [c / L for c in ax]
    up = (0, 1, 0) if abs(ax[1]) < 0.9 else (1, 0, 0)
    u = [ax[1] * up[2] - ax[2] * up[1], ax[2] * up[0] - ax[0] * up[2], ax[0] * up[1] - ax[1] * up[0]]
    lu = math.sqrt(sum(c * c for c in u)); u = [c / lu for c in u]
    w = [ax[1] * u[2] - ax[2] * u[1], ax[2] * u[0] - ax[0] * u[2], ax[0] * u[1] - ax[1] * u[0]]
    for k in range(seg):
        t0, t1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
        d = lambda t: [u[i] * math.cos(t) + w[i] * math.sin(t) for i in range(3)]
        P = lambda p, t: [p[i] + r * d(t)[i] for i in range(3)]
        poly(g, [P(a, t0), P(b, t0), P(b, t1), P(a, t1)], d((t0 + t1) / 2))

# ---- the core: the tank's casing, the turboshaft's round intake on top, its exhaust, the gearboxes at the sides ----
cx, cy, cz = CORE_C; hx, hy, hz = CORE_H
core = grp("Core", MI["White"]); dark = grp("CoreDark", MI["Dark"]); met = grp("CoreMetal", MI["Metal"]); seam = grp("CoreSeam", MI["Seam"])
box(core, (cx - hx, cy - hy, cz - hz), (cx + hx, cy + hy, cz + hz))
lathe(dark, cx, cz - 0.01, [(0.09, cy + hy), (0.09, cy + hy + 0.04), (0.07, cy + hy + 0.05), (0.0, cy + hy + 0.05)])   # the intake
lathe(met, cx, cz - 0.01, [(0.10, cy + hy + 0.035), (0.10, cy + hy + 0.045)])
tube(dark, (cx + 0.08, cy - hy + 0.02, cz - hz), (cx + 0.08, cy - hy - 0.06, cz - hz - 0.07), 0.035)   # the exhaust (spent H2), down and back
box(met, (cx - hx - 0.01, cy - hy + 0.03, cz + hz - 0.02), (cx + hx + 0.01, cy + hy - 0.03, cz + hz + 0.01))      # the harness plate
for y in (cy - 0.08, cy + 0.10): box(seam, (cx - hx - 0.002, y, cz - hz - 0.002), (cx + hx + 0.002, y + 0.008, cz + hz + 0.002))
box(grp("CoreWarn", MI["Warn"]), (cx - hx - 0.003, cy + hy - 0.07, cz - hz + 0.03), (cx - hx, cy + hy - 0.03, cz + hz - 0.03))
for s in (1, -1):                                                     # the side gearboxes the shafts leave from
    tube(met, (s * hx, GB_Y, GB_Z), (s * (hx + 0.06), GB_Y, GB_Z), 0.06)

# ---- each side: the boom (the shaft's tube), the gimbal yoke, the ring, the hub gearbox, the rotor, the vanes, the boost ----
for side, L in ((1, "R"), (-1, "L")):                                # "R": on +x, her right (index 0 in the module)
    rx = side * RX
    boom = grp("Boom" + L, MI["Metal"])
    a = (side * (hx + 0.06), GB_Y, GB_Z); b = (side * (RX - ROUT - 0.05), RY + 0.02, RZ)
    tube(boom, a, b, 0.035)
    box(boom, (min(b[0], side * (RX - ROUT - 0.01)), RY - 0.05, RZ - 0.05), (max(b[0], side * (RX - ROUT - 0.01)), RY + 0.07, RZ + 0.05))   # the gimbal's yoke
    ring = grp("Ring" + L, MI["White"])
    lathe(ring, rx, RZ, [(ROUT, RY + DUCT0), (ROUT, RY + DUCT1 - 0.03), (RLIP, RY + DUCT1)])                  # the outer skin
    lathe(ring, rx, RZ, [(RLIP - 0.02, RY + DUCT1), (RIN, RY + DUCT1 - 0.04), (RIN, RY + DUCT0)])            # the duct (inside)
    lathe(ring, rx, RZ, [(RIN, RY + DUCT0), (ROUT, RY + DUCT0)])                                             # the exit's rim
    lathe(ring, rx, RZ, [(RLIP - 0.02, RY + DUCT1), (RLIP, RY + DUCT1)])                                     # the lip's rim
    lathe(grp("RingWarn" + L, MI["Warn"]), rx, RZ, [(ROUT + 0.002, RY + DUCT1 - 0.10), (ROUT + 0.002, RY + DUCT1 - 0.07)])
    tube(ring, (side * (RX - ROUT - 0.01), RY, RZ), (side * (RX - ROUT + 0.005), RY, RZ), 0.045)             # the gimbal's trunnion
    st = grp("Stator" + L, MI["Dark"])
    hy0, hy1 = RY + DUCT0 + 0.02, RY + ROT_DY - 0.03                  # the hub: the bevel gearbox under the rotor
    lathe(st, rx, RZ, [(0.0, hy0), (HUB_R, hy0), (HUB_R, hy1)])
    for k in range(4):                                                # the straightening vanes; k == 0: the shaft's fairing (inboard)
        a0 = (math.pi if side > 0 else 0.0) + k * math.pi / 2
        c, s_ = math.cos(a0), math.sin(a0)
        t = (-s_, 0, c)
        w = 0.022 if k == 0 else 0.006
        P = lambda r, y, o: (rx + c * r + t[0] * o, y, RZ + s_ * r + t[2] * o)
        ya, yb = hy0 + 0.01, hy1 - 0.01
        for o in (-w, w):
            poly(st, [P(HUB_R, ya, o), P(RIN, ya, o), P(RIN, yb, o), P(HUB_R, yb, o)], t)
        poly(st, [P(HUB_R, yb, -w), P(RIN, yb, -w), P(RIN, yb, w), P(HUB_R, yb, w)], (0, 1, 0))
    for dz in (-0.12, 0.0, 0.12):                                     # the control vanes at the exit
        wv = math.sqrt(max(0.0, RIN * RIN - dz * dz)) - 0.01
        y = RY + DUCT0 + 0.015
        poly(st, [(rx - wv, y, RZ + dz - 0.025), (rx + wv, y, RZ + dz - 0.025), (rx + wv, y + 0.04, RZ + dz + 0.005), (rx - wv, y + 0.04, RZ + dz + 0.005)], (0, 0.6, -0.8))
    lathe(grp("Boost" + L, MI["Nozzle"]), rx, RZ, [(0.030, hy0), (0.022, hy0 - 0.015), (0.045, hy0 - 0.075)])   # the hub's boost nozzle
    rot = grp("Rotor" + L, MI["Dark"])
    ry = RY + ROT_DY
    lathe(rot, rx, RZ, [(HUB_R, ry - 0.03), (HUB_R, ry + 0.02), (0.035, ry + 0.06), (0.0, ry + 0.065)])         # the spinner
    for k in range(BLADES):
        a0 = 2 * math.pi * k / BLADES
        u = (math.cos(a0), 0.0, math.sin(a0)); t = (-math.sin(a0), 0.0, math.cos(a0))
        for r0, r1, pitch, ch in ((HUB_R - 0.005, 0.16, 34.0, 0.070), (0.16, RIN - 0.005, 20.0, 0.058)):
            p = math.radians(pitch)
            cv = (t[0] * math.cos(p) * ch / 2, math.sin(p) * ch / 2 * side, t[2] * math.cos(p) * ch / 2)
            P = lambda r, sg: (rx + u[0] * r + sg * cv[0], ry + sg * cv[1], RZ + u[2] * r + sg * cv[2])
            n = (-t[0] * math.sin(p), math.cos(p), -t[2] * math.sin(p))
            poly(rot, [P(r0, -1), P(r1, -1), P(r1, 1), P(r0, 1)], n)

def uv(p, n, tile):
    a = max(range(3), key=lambda k: abs(n[k]))
    u, v = (p[2], p[1]) if a == 0 else (p[0], p[2]) if a == 1 else (p[0], p[1])
    return u / tile, -v / tile

def write(path, off):
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(order))
        for name in order:
            mi, vs, fs = groups[name]; tx = TEX.get(MATS[mi][0])
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (name, mi + 1, TEXLIST.index(tx[0]) + 1 if tx else 0, len(vs), len(fs)))
            for p, n in vs:
                q = (p[0] - off[0], p[1] - off[1], p[2] - off[2])
                if tx: f.write("%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*q, *n, *uv(p, n, tx[1])))
                else: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (*q, *n))
            for a, b, c in fs: f.write("%d %d %d\n" % (a, c, b))
        f.write("MATERIALS %d\n" % len(MATS))
        for n, _ in MATS: f.write("JF_%s\n" % n)
        for n, col in MATS:
            sp = 0.5 if n in ("Metal", "Nozzle") else 0.25 if n == "White" else 0.1
            if n in TEX: col = (1.0, 1.0, 1.0)
            f.write("MATERIAL JF_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 20\n0 0 0 1\n" % (n, *col, *col, sp, sp, sp))
        f.write("TEXTURES %d\n" % len(TEXLIST))
        for t in TEXLIST: f.write("%s\n" % t)

os.makedirs(os.path.dirname(WORN), exist_ok=True)
write(WORN, (0, 0, 0))
write(DROP, CORE_C)
def role(nm):
    if nm.startswith("Rotor"): return "rotor"
    if nm.startswith(("Ring", "Stator", "Boost")): return "ring"
    return "core"                                                      # the core and the booms do not move on her
with open(GEO, "w", newline="\n") as f:
    f.write("# OrbiterCrew impeller jet pack Vikhr in the crew member's model frame (written by build_jetpack_fan.py)\n")
    f.write("SHELL %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*CORE_C, 2 * CORE_H[0], 2 * CORE_H[1], 2 * CORE_H[2]))
    for side in (1, -1):                                               # HINGE: the boom's root; PIVOT: the ring's gimbal centre
        f.write("HINGE %.4f %.4f %.4f\nPIVOT %.4f %.4f %.4f\n" % (side * (CORE_H[0] + 0.06), GB_Y, GB_Z, side * RX, RY, RZ))
    f.write("RING %.4f %.4f %.4f\nROTOR_Y %.4f\nFOLD 0\nBOOST_Y %.4f\n" % (RIN, RY + DUCT0, RY + DUCT1, RY + ROT_DY, RY + DUCT0 - 0.06))
    for k, nm in enumerate(order):                                     # GROUP index role side (0: her right, 1: left, -1: none)
        r = role(nm)
        side = -1 if r == "core" else 0 if nm.endswith("R") else 1
        f.write("GROUP %d %s %d\n" % (k, r, side))
print("groups", len(order), "verts", sum(len(groups[n][1]) for n in order))
