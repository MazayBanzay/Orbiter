# The spaceport's technical point ТП-1 at Brighton Beach (TVehicles\Building): Meshes\TVehicles\TechPoint.msh +
# Orbitersdk\samples\TVehicles\TechPointGeo.h. Spec: Tantra_Design\base\TECHPOINT.md (variant Б «двор», the user 2026-10-21).
#   ПОМ. 101 the hall (reception and rest, the duty post БЩУ), ПОМ. 102 life support + the gas store, ПОМ. 103 the tool room,
#   ШК-1 the airlock to the yard, ШК-2 the airlock to the hangar, ПОМ. 201 the hangar (vehicle service, unpressurised, the
#   floor is the Moon's ground: the test stand of the D3D11 client's lights and regolith), the yard's apron and the flood mast.
# The look: the Э.МПУ's and the suits' - warm white enamel panels, dark composite bands, a red line, chamfers; real volume
# (pilasters, cornices, door portals, vault ribs, roof plant) and per-pixel maps (make_base_textures.py), nothing baked.
# Collisions as the Tantra's (gen_mesh.py -> InteriorLayout.h): this script writes the boxes - the floors and the solids inside,
# the outer solids for people outside, the capsules for the vehicles (TVChassis::Obstacles, TantraSolid kind 1).
# Frame: x east, y up, z north (heading 0), the ground at y = 0, the rooms' floor at FY. Run: python build_techpoint.py
import os, math

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MESH = os.path.join(ROOT, "Meshes", "TVehicles", "TechPoint.msh")
GEO = os.path.join(ROOT, "Orbitersdk", "samples", "TVehicles", "TechPointGeo.h")
FY, CY, LCY = 0.30, 3.80, 3.10            # the rooms' floor, the hall's ceiling, the airlocks' ceiling
PL = -0.80                                # the plinth's foot (sunk: hides the ground's slope)

# ---- materials: name, colour, specular, emissive, texture (file, metres per tile or None) ----
MATS = []
MI = {}
def mat(name, col, spec=0.15, emit=0.0, tex=None, alpha=1.0, power=20):
    MI[name] = len(MATS); MATS.append((name, col, spec, emit, tex, alpha, power)); return MI[name]
mat("Panel", (1, 1, 1), 0.2, tex=("TVehicles\\tp_panel.dds", 3.0))
mat("Lining", (1, 1, 1), 0.3, tex=("TVehicles\\tp_lining.dds", 1.0))   # light ceramic-polymer lining, semi-gloss (albedo ~0.6)
mat("Floor", (1, 1, 1), 0.1, tex=("TVehicles\\tp_floor.dds", 1.2))
mat("Steel", (0.80, 0.82, 0.86), 0.5, tex=("TVehicles\\tp_alloy.dds", 1.0))   # the crane: Ti-HEA alloy, cool grey (the user: not gold)
mat("Sinter", (1, 1, 1), 0.1, tex=("TVehicles\\tp_sinter.dds", 2.0))
mat("Dark", (1, 1, 1), 0.3, tex=("TVehicles\\tp_cnt.dds", 0.5))    # CNT composite: dark twill under a clear ceramic coat
mat("Metal", (1, 1, 1), 0.5, tex=("TVehicles\\tp_alloy.dds", 0.5))   # Ti high-entropy alloy, brushed
mat("Deck", (1, 1, 1), 0.3, tex=("MPU\\mpu_deck.dds", 1.0))
mat("Seat", (1, 1, 1), 0.08, tex=("MPU\\mpu_seat.dds", 0.6))
mat("Red", (0.72, 0.12, 0.10), 0.2)
mat("Warn", (0.86, 0.64, 0.08), 0.2)
mat("Black", (0.05, 0.05, 0.055), 0.2)
mat("Glass", (0.04, 0.06, 0.07), 0.8, alpha=0.12, power=90)   # transparent ceramic (spinel): nearly clear, sharp reflection
mat("O2", (0.36, 0.62, 0.86), 0.4)        # oxygen bottles: light blue (GOST 949)
mat("N2", (0.07, 0.07, 0.08), 0.4)        # nitrogen: black
mat("Green", (0.20, 0.42, 0.30), 0.2)     # the water line
mat("Desk", (0.30, 0.32, 0.34), 0.3)
# the lamps' glowing parts: one material per circuit (the module switches their emission, as the Э.МПУ's LampIn)
for c in ("LampHall", "LampRooms", "LampLock1", "LampLock2", "LampWork", "LampDuty", "LampYard", "LampGate"):
    mat(c, (1.0, 0.96, 0.88), 0.1, emit=1.0)
mat("Beacon", (1.0, 0.45, 0.05), 0.1, emit=1.0)   # the airlocks' amber cycle beacons (blink while a lock cycles)
mat("PanelFace", (0.13, 0.14, 0.15), 0.2)          # the control panels' faces
mat("Screen", (1.0, 1.0, 1.0), 0.2, emit=1.0)     # the screens: their own light (the picture x emission, as D3D9 shows it)
mat("Signs", (1, 1, 1), 0.1, tex=("TVehicles\\tp_signs.dds", None))
mat("ExitSign", (0.20, 1.0, 0.35), 0.1, emit=1.0)    # the green «ВЫХОД» boxes (always lit, as at a plant)
SCREEN_TEX = "TVehicles\\tp_screen.dds"            # the screens' placeholder: the module draws each into its own surface

groups = []                                        # [label, mi, verts[(p, n, uv|None)], faces, texslot override]
def grp(label, mi, tex=None):
    groups.append([label, mi, [], [], tex]); return groups[-1]

def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def unit(a):
    l = math.sqrt(dot(a, a)) or 1.0; return (a[0] / l, a[1] / l, a[2] / l)

def poly(g, pts, n, uvs=None):
    """a planar polygon facing n (the winding is set so that it faces n: Orbiter's front face)"""
    if len(pts) >= 3 and dot(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])), n) < 0:
        pts = pts[::-1]; uvs = uvs[::-1] if uvs else None
    b = len(g[2])
    for k, p in enumerate(pts): g[2].append((p, n, uvs[k] if uvs else None))
    for k in range(1, len(pts) - 1): g[3].append((b, b + k, b + k + 1))

def poly_n(g, pts, ns, uvs=None):
    """a polygon with a normal per vertex (a curved surface shaded smoothly); faces the mean of the normals"""
    m = unit(tuple(sum(n[k] for n in ns) for k in range(3)))
    if len(pts) >= 3 and dot(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])), m) < 0:
        pts, ns = pts[::-1], ns[::-1]; uvs = uvs[::-1] if uvs else None
    b0 = len(g[2])
    for k, p in enumerate(pts): g[2].append((p, ns[k], uvs[k] if uvs else None))
    for k in range(1, len(pts) - 1): g[3].append((b0, b0 + k, b0 + k + 1))

def sheet(g, pts, n):                              # seen from both sides (the vault, the thin plates)
    poly(g, pts, n); poly(g, pts, (-n[0], -n[1], -n[2]))

def box(g, lo, hi, faces="xXyYzZ"):
    x0, y0, z0 = lo; x1, y1, z1 = hi
    if "Z" in faces: poly(g, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1))
    if "z" in faces: poly(g, [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1))
    if "X" in faces: poly(g, [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0))
    if "x" in faces: poly(g, [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0))
    if "Y" in faces: poly(g, [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0))
    if "y" in faces: poly(g, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0))

def cyl(g, axis, c, r, a0, a1, seg=16, caps=True):
    def P(t, a):
        u, v = r * math.cos(t), r * math.sin(t)
        return {"x": (a, c[0] + u, c[1] + v), "y": (c[0] + u, a, c[1] + v), "z": (c[0] + u, c[1] + v, a)}[axis]
    def N(t):
        u, v = math.cos(t), math.sin(t)
        return {"x": (0, u, v), "y": (u, 0, v), "z": (u, v, 0)}[axis]
    e = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}[axis]
    ctr = lambda a: {"x": (a, c[0], c[1]), "y": (c[0], a, c[1]), "z": (c[0], c[1], a)}[axis]
    for k in range(seg):
        t0, t1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
        poly(g, [P(t0, a0), P(t1, a0), P(t1, a1), P(t0, a1)], N((t0 + t1) / 2))
        if caps:
            poly(g, [ctr(a0), P(t1, a0), P(t0, a0)], tuple(-x for x in e)); poly(g, [ctr(a1), P(t0, a1), P(t1, a1)], e)

# ---- the collision data (as the Tantra's InteriorLayout.h): boxes x0 y0 z0 x1 y1 z1 ----
FLOORS, SOL_IN, SOL_OUT, CAPS = [], [], [], []

def wall(g, run, c0, c1, u0, u1, y0, y1, holes=(), where="io"):
    """a wall along x (run 'x': thickness z c0..c1) or along z (run 'z': thickness x c0..c1) from u0 to u1, y0..y1, with
    openings (ou0, ou1, oy0, oy1); its boxes go to the mesh and to the solids inside ('i') / outside ('o')"""
    cuts = sorted({u0, u1} | {h[0] for h in holes} | {h[1] for h in holes})
    for a, b in zip(cuts, cuts[1:]):
        m = 0.5 * (a + b)
        spans = [(y0, y1)]
        for h in holes:
            if h[0] <= m <= h[1]: spans = [(y0, h[2]), (h[3], y1)]
        for s0, s1 in spans:
            if s1 - s0 < 1e-3: continue
            lo, hi = ((a, s0, c0), (b, s1, c1)) if run == "x" else ((c0, s0, a), (c1, s1, b))
            box(g, lo, hi)
            bx = (lo[0], lo[1], lo[2], hi[0], hi[1], hi[2])
            if "i" in where: SOL_IN.append(bx)
            if "o" in where: SOL_OUT.append(bx)

# ======================= the pressurised block: the hall, rooms 102 / 103, ШК-1, ШК-2 =======================
lin = grp("Lining", MI["Lining"]); pan = grp("Facade", MI["Panel"]); flo = grp("Floors", MI["Floor"]); deck = grp("LockDecks", MI["Deck"])
dark = grp("Bands", MI["Dark"]); red = grp("RedLine", MI["Red"]); sin = grp("Plinth", MI["Sinter"]); metal = grp("Metal", MI["Metal"])
warn = grp("Warn", MI["Warn"]); blk = grp("Black", MI["Black"]); glass = grp("Glass", MI["Glass"])
T, K = 0.40, 0.06                         # wall core, the facade's cladding
DW, DH = 1.40, 2.40                       # the doors' opening (Pantsir-wide), from the floor
# the doors: name, run of their wall, the wall's thickness range, the opening (u0, u1), the leaf's slide direction (+1 / -1 along u)
DOORS = [("Lock1Outer", "x", 0.40, 0.80, -10.70, -9.30, +1), ("Lock1Inner", "x", 4.00, 4.40, -10.70, -9.30, +1),
         ("Lock2Inner", "z", -4.40, -4.00, 8.30, 9.70, +1), ("Lock2Outer", "z", -0.40, 0.10, 8.30, 9.70, -1)]
DOOR = {d[0]: d for d in DOORS}
def dhole(name): d = DOOR[name]; return (d[4], d[5], FY, FY + DH)
RD = [(-16.00, -15.60, 9.40, 10.60), (-16.00, -15.60, 5.40, 6.60)]        # the rooms' doorways (open, framed), x range, z range
# the hall's two south viewports (the user, 2026-10-21: no «hut window» with a cross): no mullions, rounded corners, a deep
# embrasure splayed both ways, a dark frame with its seal, a brow over it, the rails of the radiation shutter at its sides.
# Profiles (w, h, corner r): the glass, the outer frame's opening, the inner reveal's opening; centres at x, height WIN_Y
WIN = [-13.80, -6.20]
WIN_Y = FY + 1.45
WIN_GLASS, WIN_OUT, WIN_IN = (1.50, 0.80, 0.26), (1.80, 1.10, 0.36), (2.00, 1.25, 0.38)

# floors (the walking boxes; top = FY): the hall, the rooms, the locks, the inner doorways
for b in ((-15.60, -4.40, 4.40, 11.60), (-21.60, -16.00, 8.10, 11.60), (-21.60, -16.00, 4.40, 7.90), (-11.60, -8.40, 0.80, 4.00), (-4.00, -0.40, 7.00, 10.20)):
    FLOORS.append((b[0], FY - 0.30, b[2], b[1], FY, b[3]))
for x0, x1, z0, z1 in RD: FLOORS.append((x0 - 0.05, FY - 0.30, z0, x1 + 0.05, FY, z1))
FLOORS.append((-10.70, FY - 0.30, 3.95, -9.30, FY, 4.45))                  # ШК-1 inner doorway
FLOORS.append((-4.45, FY - 0.30, 8.30, -3.95, FY, 9.70))                   # ШК-2 inner doorway
for b in FLOORS[:3]: box(flo, (b[0], b[1], b[2]), (b[3], b[4], b[5]), "Y")
for b in FLOORS[3:5]: box(deck, (b[0], b[1], b[2]), (b[3], b[4], b[5]), "Y")
# the doorways' sills: drawn over the wall's thickness only (the walking boxes above reach 5 cm into the rooms; drawn so
# they lay over the rooms' floors in one plane and the floor flickered - the user, 2026-10-21)
for x0, x1, z0, z1 in RD: box(metal, (x0, FY - 0.30, z0), (x1, FY, z1), "Y")
box(metal, (-10.70, FY - 0.30, 4.00), (-9.30, FY, 4.40), "Y")
box(metal, (-4.40, FY - 0.30, 8.30), (-4.00, FY, 9.70), "Y")

# the walls (core: lining inside; the facade's panels outside as a cladding with the same openings)
H1 = CY + 0.60                            # the block's walls up to the roof slab's top
win_holes = [(x - WIN_IN[0] / 2, x + WIN_IN[0] / 2, WIN_Y - WIN_IN[1] / 2, WIN_Y + WIN_IN[1] / 2) for x in WIN]          # the core
win_clad = [(x - WIN_OUT[0] / 2, x + WIN_OUT[0] / 2, WIN_Y - WIN_OUT[1] / 2, WIN_Y + WIN_OUT[1] / 2) for x in WIN]         # the skin
# hall: south (z 4.0..4.4) with ШК-1's inner door and the windows; north; east (with ШК-2 inner door); west = the rooms' east wall
wall(lin, "x", 4.00, 4.40, -16.00, -4.00, FY - 0.30, H1, [dhole("Lock1Inner")] + win_holes)
wall(lin, "x", 11.60, 12.00, -22.00, -4.00, FY - 0.30, H1)
wall(lin, "z", -4.40, -4.00, 4.00, 12.00, FY - 0.30, H1, [dhole("Lock2Inner")])
wall(lin, "z", -16.00, -15.60, 4.40, 11.60, FY - 0.30, H1, [(z0, z1, FY, FY + 2.20) for _, _, z0, z1 in RD])
# rooms: south, west, the partition
wall(lin, "x", 4.00, 4.40, -22.00, -16.00, FY - 0.30, H1)
wall(lin, "z", -22.00, -21.60, 4.00, 12.00, FY - 0.30, H1)
wall(lin, "x", 7.90, 8.10, -21.60, -16.00, FY - 0.30, CY)
# ШК-1: west, east, south (outer door); ШК-2: north, south, east (= the hangar's west wall, outer door)
wall(lin, "z", -12.00, -11.60, 0.40, 4.00, FY - 0.30, LCY + 0.60)
wall(lin, "z", -8.40, -8.00, 0.40, 4.00, FY - 0.30, LCY + 0.60)
wall(lin, "x", 0.40, 0.80, -12.00, -8.00, FY - 0.30, LCY + 0.60, [dhole("Lock1Outer")])
wall(lin, "x", 10.20, 10.60, -4.00, -0.40, FY - 0.30, LCY + 0.60)
wall(lin, "x", 6.60, 7.00, -4.00, -0.40, FY - 0.30, LCY + 0.60)
# the cladding (outside only: the facade's panel skin 6 cm proud), the plinth, the bands, the red line, the cornice
def clad(run, c, s, u0, u1, y0, y1, holes=()):
    """the facade skin on the outer face c of a wall (s: +1 / -1, which way is out)"""
    wall(pan, run, min(c, c + s * K), max(c, c + s * K), u0, u1, y0, y1 - 0.31, holes, where="")   # (under the cornice)
clad("x", 4.00, -1, -22.00, -12.00, FY, H1, win_clad[:1]); clad("x", 4.00, -1, -8.00, -4.00, FY, H1, win_clad[1:])
clad("x", 12.00, +1, -22.00, -4.00, FY, H1)
clad("z", -22.00, -1, 4.00, 12.00, FY, H1)
clad("z", -4.00, +1, 4.00, 7.00, FY, H1); clad("z", -4.00, +1, 10.20, 12.00, FY, H1)
clad("x", 0.40, -1, -12.00, -8.00, FY, LCY + 0.60, [dhole("Lock1Outer")])
clad("z", -12.00, -1, 0.40, 4.00, FY, LCY + 0.60); clad("z", -8.00, +1, 0.40, 4.00, FY, LCY + 0.60)
clad("x", 10.60, +1, -4.00, -0.40, FY, LCY + 0.60); clad("x", 6.60, -1, -4.00, -0.40, FY, LCY + 0.60)
# the people outside meet the block as whole solids (the doors are passed with F: OC_AIRLOCK / OC_EXIT)
for b in ((-22.15, PL, 3.85, -3.85, H1, 12.15), (-12.15, PL, 0.25, -7.85, LCY + 0.6, 4.0), (-4.0, PL, 6.45, -0.40, LCY + 0.6, 10.75)):
    SOL_OUT.append(b)
# the plinth: sintered regolith blocks round the foot, 0.15 proud, from PL to FY; the step before ШК-1
# (its top 3 cm under the floors: at FY it lay in the floors' plane under the whole block - the floor flickered)
for lo, hi in (((-22.15, PL, 3.85), (-3.85, FY - 0.03, 12.15)), ((-12.15, PL, 0.25), (-7.85, FY - 0.03, 4.0)), ((-4.0, PL, 6.45), (-0.40, FY - 0.03, 10.75))):
    box(sin, lo, hi, "xXzZY")
# the steps before ШК-1 and, in the hangar, before ШК-2: metal treads on stringers up to the door's sill (FY)
for (x0, x1, z0, z1, axis, s) in ((-10.9, -9.1, -0.75, 0.25, 'z', 1), (0.10, 1.10, 8.1, 9.9, 'x', -1)):
    for k, (h, d) in enumerate(((0.15, 1.0), (FY, 0.5))):
        if axis == 'z': box(metal, (x0, h - 0.04, z1 - d), (x1, h, z1), 'xXzZYy')
        else: box(metal, (x0, h - 0.04, z0), (x0 + d, h, z1), 'xXzZYy')
    # the stringers outside the treads and under their tops (sharing the treads' faces they flickered)
    for a in ((x0 - 0.05, x0) if axis == 'z' else (z0 - 0.05, z0), (x1, x1 + 0.05) if axis == 'z' else (z1, z1 + 0.05)):
        if axis == 'z': box(dark, (a[0], -0.2, z0), (a[1], FY - 0.06, z1))
        else: box(dark, (x0, -0.2, a[0]), (x1, FY - 0.06, a[1]))
# the base band (dark composite 0.06 proud, 0.6 m), the red line at 2.9 m, the cornice (0.25 proud, chamfered under)
# along each outer face only (the faces where a lock meets the block or the hangar have none: nothing pokes into a room),
# broken at the doors; at the outer corners each strip runs on by its own projection, so they meet
def bands(run, c, s, u0, u1, top, holes=(), e0=True, e1=True):
    for y0, y1, g, pr in ((FY, FY + 0.60, dark, K + 0.05), (FY + 2.62, FY + 2.70, red, K + 0.02), (top - 0.30, top, dark, K + 0.25)):
        if g is red and y1 > top - 0.4: continue
        a, b = u0 - (pr if e0 and run == "x" else 0.0), u1 + (pr if e1 and run == "x" else 0.0)   # (the x-run strips cover the corners)
        wall(g, run, min(c, c + s * pr), max(c, c + s * pr), a, b, y0, y1, [(h[0], h[1], -9, 99) for h in holes] if y0 < FY + 2.8 else (), where="")
    a, b = u0 - (0.02 if e0 else 0.0), u1 + (0.02 if e1 else 0.0)
    p0, p1 = sorted((c - s * 0.20, c + s * 0.02))
    wall(dark, run, p0, p1, a, b, top + 0.002, top + 0.45, where="")   # the parapet
D1 = (DOOR["Lock1Outer"][4] - 0.20, DOOR["Lock1Outer"][5] + 0.20)
bands("x", 4.00, -1, -22.00, -12.00, H1, e1=False); bands("x", 4.00, -1, -8.00, -4.00, H1, e0=False)
bands("x", 12.00, +1, -22.00, -4.00, H1)
bands("z", -22.00, -1, 4.00, 12.00, H1)
bands("z", -4.00, +1, 4.00, 6.60, H1, e1=False); bands("z", -4.00, +1, 10.60, 12.00, H1, e0=False)
bands("x", 0.40, -1, -12.00, -8.00, LCY + 0.60, [D1])
bands("z", -12.00, -1, 0.40, 4.00, LCY + 0.60, e1=False); bands("z", -8.00, +1, 0.40, 4.00, LCY + 0.60, e1=False)
bands("x", 10.60, +1, -4.00, -0.40, LCY + 0.60, e0=False, e1=False); bands("x", 6.60, -1, -4.00, -0.40, LCY + 0.60, e0=False, e1=False)
# pilasters every 3 m on the long facades (white, 0.30 wide, 0.14 proud), between the base band and the cornice
for x in (-21.0, -18.0, -15.0, -12.6, -7.4, -5.0):
    box(pan, (x - 0.15, FY + 0.60, 4.00 - K - 0.14), (x + 0.15, H1 - 0.30, 4.00 - K), "xXzY")
    box(pan, (x - 0.15, FY + 0.60, 12.00 + K), (x + 0.15, H1 - 0.30, 12.00 + K + 0.14), "xXZY")
# the viewports: the splayed reveals (lofts between the rounded profiles), the corner fillers on the faces, the glass
def rrect(w, h, r, n=5):
    """a rounded rectangle's outline (u, v), counter-clockwise from the right edge"""
    pts = []
    for cu, cv, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n); pts.append((cu + r * math.cos(a), cv + r * math.sin(a)))
    return pts
ZO, ZG, ZI = 4.00 - K, 4.18, 4.40                                    # the outer skin's face, the glass, the inner face
for cx in WIN:
    P = lambda prof, z: [(cx + u, WIN_Y + v, z) for u, v in rrect(*prof)]
    rings = [(P(WIN_OUT, ZO - 0.10), dark), (P(WIN_GLASS, ZG - 0.03), dark), (P(WIN_GLASS, ZG + 0.03), metal), (P(WIN_IN, ZI), lin)]
    for (ra, _), (rb, g) in zip(rings, rings[1:]):                   # the reveal: each band faces the opening's axis
        for k in range(len(ra)):
            a0, a1, b0, b1 = ra[k], ra[(k + 1) % len(ra)], rb[k], rb[(k + 1) % len(rb)]
            m = ((a0[0] + a1[0]) / 2 - cx, (a0[1] + a1[1]) / 2 - WIN_Y)
            poly(g, [a0, a1, b1, b0], unit((-m[0], -m[1], 0.0)))
    # the outer frame: a dark collar from the skin's rectangular hole out 0.10 m to the rounded opening; its front face
    # fills between the hole's corners and the rounded profile (fans from each corner)
    ou, ov = WIN_OUT[0] / 2, WIN_OUT[1] / 2
    ring = rrect(*WIN_OUT); q = len(ring) // 4
    for c, (su, sv) in enumerate(((1, 1), (-1, 1), (-1, -1), (1, -1))):
        corner = (cx + su * ou, WIN_Y + sv * ov, ZO - 0.10)
        arc = ring[c * q:(c + 1) * q]
        for k in range(len(arc) - 1):
            poly(dark, [corner, (cx + arc[k][0], WIN_Y + arc[k][1], ZO - 0.10), (cx + arc[k + 1][0], WIN_Y + arc[k + 1][1], ZO - 0.10)], (0, 0, -1))
    for x0, x1, y0, y1 in ((cx - ou - 0.12, cx + ou + 0.12, WIN_Y + ov, WIN_Y + ov + 0.12), (cx - ou - 0.12, cx + ou + 0.12, WIN_Y - ov - 0.12, WIN_Y - ov),
                           (cx - ou - 0.12, cx - ou, WIN_Y - ov, WIN_Y + ov), (cx + ou, cx + ou + 0.12, WIN_Y - ov, WIN_Y + ov)):
        box(dark, (x0, y0, ZO - 0.10), (x1, y1, ZO), "xXyYz")            # the collar's flat rim round the hole
    # the inner face: lining fillers between the core's hole and the rounded reveal
    iu, iv = WIN_IN[0] / 2, WIN_IN[1] / 2
    ring = rrect(*WIN_IN); q = len(ring) // 4
    for c, (su, sv) in enumerate(((1, 1), (-1, 1), (-1, -1), (1, -1))):
        corner = (cx + su * iu, WIN_Y + sv * iv, ZI)
        arc = ring[c * q:(c + 1) * q]
        for k in range(len(arc) - 1):
            poly(lin, [corner, (cx + arc[k][0], WIN_Y + arc[k][1], ZI), (cx + arc[k + 1][0], WIN_Y + arc[k + 1][1], ZI)], (0, 0, 1))
    # the glass: one pane, three layers in reality - one sheet here, seen from both sides
    sheet(glass, P(WIN_GLASS, ZG), (0, 0, -1))
    # the brow over it (dark, chamfered under) and the shutter's rails up the sides to under the cornice
    bx0, bx1, by = cx - ou - 0.25, cx + ou + 0.25, WIN_Y + ov + 0.14
    box(dark, (bx0, by, ZO - 0.42), (bx1, by + 0.10, ZO))
    poly(dark, [(bx0, by, ZO - 0.42), (bx1, by, ZO - 0.42), (bx1, by - 0.10, ZO - 0.12), (bx0, by - 0.10, ZO - 0.12)], unit((0, -0.95, -0.3)))
    for x in (cx - ou - 0.20, cx + ou + 0.20):
        box(metal, (x - 0.04, WIN_Y - ov - 0.25, ZO - 0.09), (x + 0.04, by + 0.45, ZO))   # (up to the shutter's housing over the brow)
    box(metal, (cx - ou - 0.18, WIN_Y - ov - 0.20, ZO - 0.20), (cx + ou + 0.18, WIN_Y - ov - 0.14, ZO))   # the sill: a metal drip edge
# the roof slab over the hall and the rooms, the locks' roofs (dark composite on top), the ceilings (lining underneath)
box(dark, (-22.00, CY, 4.00), (-4.00, H1 + 0.01, 12.00), "Yy")          # (1 cm over the walls' tops: no shared plane)
box(dark, (-12.00, LCY, 0.40), (-8.00, LCY + 0.61, 4.00), "Yy")
box(dark, (-4.00, LCY, 6.60), (-0.40, LCY + 0.61, 10.60), "Yy")
# the roof plant: two air-handling units, the radiator panels on their frames, the antenna mast with its beacon lamp
for x0 in (-20.5, -14.5):
    box(pan, (x0, H1, 6.0), (x0 + 3.2, H1 + 1.3, 8.4)); box(blk, (x0 + 0.2, H1 + 0.3, 5.96), (x0 + 3.0, H1 + 1.1, 6.0), "z")
    for k in range(6): box(dark, (x0 + 0.25 + 0.5 * k, H1 + 1.3, 6.4), (x0 + 0.45 + 0.5 * k, H1 + 1.55, 8.0))
for k in range(5):                                                       # the radiators: tilted plates on struts
    z0 = 8.9 + 0.55 * k
    sheet(metal, [(-12.0, H1 + 0.5, z0), (-6.0, H1 + 0.5, z0), (-6.0, H1 + 1.4, z0 + 0.35), (-12.0, H1 + 1.4, z0 + 0.35)], unit((0, -0.36, 0.93)))
    for x in (-11.8, -6.2): box(metal, (x - 0.03, H1, z0 - 0.03), (x + 0.03, H1 + 0.5, z0 + 0.03))
cyl(metal, "y", (-5.0, 11.2), 0.06, H1, H1 + 5.5, 8)
cyl(metal, "z", (-5.0, H1 + 4.8), 0.03, 10.4, 12.0, 6)
beacon_roof = grp("RoofBeacon", MI["Beacon"]); cyl(beacon_roof, "y", (-5.0, 11.2), 0.10, H1 + 5.5, H1 + 5.7, 10)
# the ceilings: lining panels under the slabs (hall, rooms, locks); the lamp strips set in them (each circuit its group)
box(lin, (-21.60, CY - 0.02, 4.40), (-4.40, CY, 11.60), "y")
box(lin, (-11.60, LCY - 0.02, 0.80), (-8.40, LCY, 4.00), "y")
box(lin, (-4.00, LCY - 0.02, 7.00), (-0.40, LCY, 10.20), "y")
lampHall = grp("LampHall", MI["LampHall"]); lampRooms = grp("LampRooms", MI["LampRooms"])
lampL1 = grp("LampLock1", MI["LampLock1"]); lampL2 = grp("LampLock2", MI["LampLock2"])
HALL_LAMPS = [(x, z) for z in (6.4, 9.6) for x in (-13.0, -7.0)]   # 4 long panels (2.0 x 0.36 m), 8000 lm each: ~300 lx
ROOM_LAMPS = [(-18.8, 9.85), (-18.8, 6.15)]                          # one panel (1.4 x 0.3 m) per room
HL, HW_, RL, RW = 0.98, 0.18, 0.68, 0.15                               # the glowing faces' half-sizes
for x, z in HALL_LAMPS:
    box(dark, (x - HL - 0.07, CY - 0.10, z - HW_ - 0.07), (x + HL + 0.07, CY - 0.02, z + HW_ + 0.07), "xXzZy")
    box(lampHall, (x - HL, CY - 0.11, z - HW_), (x + HL, CY - 0.10, z + HW_), "y")
for x, z in ROOM_LAMPS:
    box(dark, (x - RL - 0.07, CY - 0.10, z - RW - 0.07), (x + RL + 0.07, CY - 0.02, z + RW + 0.07), "xXzZy")
    box(lampRooms, (x - RL, CY - 0.11, z - RW), (x + RL, CY - 0.10, z + RW), "y")
# the duty (watch) light: warm low strips on the walls 0.3 m over the floor - the half-light of a night watch, nothing glares
# (circuit ЩО-1 «дежурное» / БЩУ «вахта»): (room, centre, the wall's inward normal, length)
DUTY_STRIPS = [(1, (-12.5, 11.60), (0, 0, -1), 3.0), (1, (-7.0, 11.60), (0, 0, -1), 3.0), (1, (-15.60, 8.0), (1, 0, 0), 2.0),
               (2, (-21.60, 9.85), (1, 0, 0), 1.6), (3, (-21.60, 6.15), (1, 0, 0), 1.6),
               (4, (-8.40, 2.4), (-1, 0, 0), 1.0), (5, (-2.2, 7.00), (0, 0, 1), 1.0)]
lampDutyIn = grp("LampDutyIn", MI["LampDuty"])
for room, (u, v), n, L in DUTY_STRIPS:
    y = FY + 0.30
    if n[2]:
        z = v + n[2] * 0.0
        box(dark, (u - L / 2 - 0.04, y - 0.05, min(z, z + n[2] * 0.05)), (u + L / 2 + 0.04, y + 0.05, max(z, z + n[2] * 0.05)), "xXzZyY")
        zf = z + n[2] * 0.051
        poly(lampDutyIn, [(u - L / 2, y - 0.02, zf), (u + L / 2, y - 0.02, zf), (u + L / 2, y + 0.02, zf), (u - L / 2, y + 0.02, zf)], n)
    else:
        x = u
        box(dark, (min(x, x + n[0] * 0.05), y - 0.05, v - L / 2 - 0.04), (max(x, x + n[0] * 0.05), y + 0.05, v + L / 2 + 0.04), "xXzZyY")
        xf = x + n[0] * 0.051
        poly(lampDutyIn, [(xf, y - 0.02, v - L / 2), (xf, y - 0.02, v + L / 2), (xf, y + 0.02, v + L / 2), (xf, y + 0.02, v - L / 2)], n)
for g, (x, z) in ((lampL1, (-10.0, 2.4)), (lampL2, (-2.2, 8.6))):
    box(dark, (x - 0.45, LCY - 0.10, z - 0.45), (x + 0.45, LCY - 0.02, z + 0.45), "xXzZy")
    box(g, (x - 0.38, LCY - 0.11, z - 0.38), (x + 0.38, LCY - 0.10, z + 0.38), "y")
# the amber cycle beacons over the airlocks' outer doors (outside and inside)
beac1 = grp("BeaconLock1", MI["Beacon"]); beac2 = grp("BeaconLock2", MI["Beacon"])
cyl(beac1, "y", (-8.75, 0.30), 0.07, FY + DH + 0.25, FY + DH + 0.40, 10); cyl(beac1, "y", (-8.75, 0.95), 0.07, FY + DH + 0.15, FY + DH + 0.30, 10)
cyl(beac2, "y", (0.35, 10.05), 0.07, FY + DH + 0.25, FY + DH + 0.40, 10); cyl(beac2, "y", (-0.55, 10.05), 0.07, FY + DH + 0.15, FY + DH + 0.30, 10)

# the pipes along the hall's north wall under the ceiling, colour-coded as at a plant: O2 light blue, N2 black, water green
for k, (g, y) in enumerate(((grp("PipeO2", MI["O2"]), CY - 0.25), (grp("PipeN2", MI["N2"]), CY - 0.45), (grp("PipeW", MI["Green"]), CY - 0.65))):
    cyl(g, "x", (y, 11.40), 0.05, -21.5, -4.5, 10)
for x in range(-21, -4, 2): box(metal, (x - 0.03, CY - 0.75, 11.33), (x + 0.03, CY, 11.60))

# ---- the doors: portals (dark jambs and lintel 0.12 proud each side, the hazard stripes on the outer doors' jambs), the
# leaves (each its group: the module slides it along its wall into the pocket) ----
LEAF_T = 0.10
leaves = {}
for name, run, c0, c1, u0, u1, sgn in DOORS:
    cm = 0.5 * (c0 + c1)
    for side in (c0 - 0.12, c1):                                          # the portal's two faces
        s0, s1 = side, side + 0.12
        # the jambs up to the lintel (not into it: their fronts in its plane flickered), 2 cm into the opening (their inner
        # faces off the cladding's cut); the lintel 2 cm down into it and 1 cm proud of the jambs
        for a, b in ((u0 - 0.20, u0 + 0.02), (u1 - 0.02, u1 + 0.20)):
            box(dark, (a, FY, s0) if run == "x" else (s0, FY, a), (b, FY + DH - 0.02, s1) if run == "x" else (s1, FY + DH - 0.02, b))
        l0, l1 = s0 - 0.01, s1 + 0.01
        box(dark, (u0 - 0.21, FY + DH - 0.02, l0) if run == "x" else (l0, FY + DH - 0.02, u0 - 0.21), (u1 + 0.21, FY + DH + 0.20, l1) if run == "x" else (l1, FY + DH + 0.20, u1 + 0.21))
    if "Outer" in name:                                                    # the hazard stripes on the jambs' fronts
        out = c0 - 0.12 - 0.005 if name == "Lock1Outer" else c1 + 0.12 + 0.005
        for k in range(12):
            y0 = FY + k * 0.20; g = warn if k % 2 == 0 else blk
            for a, b in ((u0 - 0.20, u0), (u1, u1 + 0.20)):
                pts = [(a, y0, out), (b, y0, out), (b, y0 + 0.20, out), (a, y0 + 0.20, out)] if run == "x" else [(out, y0, a), (out, y0, b), (out, y0 + 0.20, b), (out, y0 + 0.20, a)]
                n = (0, 0, -1) if name == "Lock1Outer" else (1, 0, 0)
                poly(g, pts, n)
    # the leaf: a panel with a recessed centre and a porthole, closed in the opening (the module moves it by (u1 - u0) * sgn)
    g = grp("Door" + name, MI["Metal"]); leaves[name] = g
    lo = (u0 + 0.01, FY + 0.005, cm - LEAF_T / 2) if run == "x" else (cm - LEAF_T / 2, FY + 0.005, u0 + 0.01)   # 1 cm clear of the opening's
    hi = (u1 - 0.01, FY + DH - 0.01, cm + LEAF_T / 2) if run == "x" else (cm + LEAF_T / 2, FY + DH - 0.01, u1 - 0.01)   # faces
    box(g, lo, hi)
    um = 0.5 * (u0 + u1)
    for s in (-1, 1):                                                      # the porthole's rims, both faces
        f = cm + s * (LEAF_T / 2 + 0.01)
        if run == "x": cyl(g, "z", (um, FY + 1.65), 0.16, f - 0.01, f + 0.01, 16)
        else: cyl(g, "x", (FY + 1.65, um), 0.16, f - 0.01, f + 0.01, 16)
    # (the leaf's material is metal; its glass is the porthole's black disc - a dark rim in metal reads as a port at 5 m)
# the rooms' open doorways: framed (dark), with a lintel
for x0, x1, z0, z1 in RD:
    for a, b in ((z0 - 0.15, z0 + 0.02), (z1 - 0.02, z1 + 0.15)):          # (2 cm into the opening: off the wall's cut faces)
        box(dark, (x0 - 0.10, FY, a), (x1 + 0.10, FY + 2.18, b))
    box(dark, (x0 - 0.11, FY + 2.18, z0 - 0.16), (x1 + 0.11, FY + 2.35, z1 + 0.16))

# ======================= the hangar ПОМ. 201 (unpressurised, the floor is the ground) =======================
HX0, HX1, HZ0, HZ1 = -0.40, 24.00, -4.00, 14.00            # outside
HT, HW, HR = 0.50, 6.50, 2.60                              # the walls' thickness, their height, the vault's rise
GX0, GX1, GH = 7.00, 17.00, 6.00                           # the gate's opening (south wall)
hpan = grp("HangarWalls", MI["Panel"]); hrib = grp("HangarRibs", MI["Dark"])
# walls (panels both faces: the inside is the stand's backdrop), with ШК-2's outer door in the west wall
wall(hpan, "z", HX0, HX0 + HT, HZ0, HZ1, PL, HW, [(8.30, 9.70, FY, FY + DH)], where="o")
wall(hpan, "z", HX1 - HT, HX1, HZ0, HZ1, PL, HW, where="o")
wall(hpan, "x", HZ1 - HT, HZ1, HX0, HX1, PL, HW, where="o")
wall(hpan, "x", HZ0, HZ0 + HT, HX0, HX1, PL, HW, [(GX0, GX1, PL, GH)], where="o")
# the vault: a segmental arch across x (span HX0..HX1), 16 segments, its axis along z; the gables close it
SPAN = HX1 - HX0; RV = (SPAN ** 2 / 4 + HR ** 2) / (2 * HR); XC = 0.5 * (HX0 + HX1); YC = HW + HR - RV
A0 = math.asin((SPAN / 2) / RV)
def vault(t): a = -A0 + 2 * A0 * t; return (XC + RV * math.sin(a), YC + RV * math.cos(a), (math.sin(a), math.cos(a)))
vroof = grp("Vault", MI["Panel"]); NV = 32
for k in range(NV):
    # smooth: each vertex its own radial normal (one normal per strip lit the vault in facets - the user, 2026-10-21)
    x0, y0, n0 = vault(k / NV); x1, y1, n1 = vault((k + 1) / NV)
    N0, N1 = (n0[0], n0[1], 0.0), (n1[0], n1[1], 0.0)
    q = [(x0, y0, HZ0), (x1, y1, HZ0), (x1, y1, HZ1), (x0, y0, HZ1)]
    # the panels run along the arc: u = the arc length, v = along the hangar, 3 m a tile (a planar projection switched
    # its axis over the arc and laid the texture and its normals askew in patches - the user, 2026-10-21)
    s0, s1 = 2 * A0 * RV * k / NV / 3.0, 2 * A0 * RV * (k + 1) / NV / 3.0
    uv = [(s0, -HZ0 / 3.0), (s1, -HZ0 / 3.0), (s1, -HZ1 / 3.0), (s0, -HZ1 / 3.0)]
    poly_n(vroof, q, [N0, N1, N1, N0], uv)                                   # outside
    poly_n(vroof, q, [tuple(-c for c in N0), tuple(-c for c in N1), tuple(-c for c in N1), tuple(-c for c in N0)], uv)   # inside
    for z, s in ((HZ0, -1), (HZ1, +1)):                                      # the gables: fans to the walls' top centre
        sheet(hpan, [(x0, y0, z), (x1, y1, z), (XC, HW, z)], (0, 0, s))
# the ribs: portal frames every 3 m, outside on the walls and the vault (dark composite, 0.35 wide, 0.25 proud) and inside
# under the vault (the trusses' lower chords, 0.25 deep)
for z in [HZ0 + 0.25 + 3.0 * k for k in range(7)]:
    for x0, x1 in ((HX0 - 0.25, HX0), (HX1, HX1 + 0.25)):
        if x0 < 0 and 6.4 < z < 10.8: continue                               # (ШК-2 stands against the west wall there)
        box(hrib, (x0, PL, z - 0.17), (x1, HW, z + 0.17))
    for k in range(NV):
        x0, y0, n0 = vault(k / NV); x1, y1, n1 = vault((k + 1) / NV)
        for d, s in ((0.25, 1), (-0.25, -1)):                                 # outside (over the vault) and inside (under it)
            if d < 0 and (k == 0 or k == NV - 1): continue                    # (inside: from column to column, not over the walls)
            hw = 0.17 if d > 0 else 0.16
            p = [(x0, y0, z - hw), (x1, y1, z - hw), (x1 + n1[0] * d, y1 + n1[1] * d, z - hw), (x0 + n0[0] * d, y0 + n0[1] * d, z - hw)]
            q = [(a, b, z + hw) for a, b, _ in p]
            nn = unit((n0[0] + n1[0], n0[1] + n1[1], 0))
            poly(hrib, [p[2], p[3], q[3], q[2]], (nn[0] * s, nn[1] * s, 0))
            poly(hrib, p, (0, 0, -1)); poly(hrib, q, (0, 0, 1))
    for x0, x1 in ((HX0 + HT, HX0 + HT + 0.25), (HX1 - HT - 0.25, HX1 - HT)):   # the inner columns under the trusses
        if z < 6.4 and x0 < 1 or abs(z - 8.7) < 2.0 and x0 < 1: continue      # (ШК-2's door and the lock: no column there)
        ytop = vault((x0 - HX0) / SPAN if x0 < 10 else (x1 - HX0) / SPAN)[1] - 0.01  # up to the vault's underside at its wall side (the lower edge)
        box(hrib, (x0, 0.0, z - 0.17), (x1, ytop, z + 0.17))
        SOL_OUT.append((x0, 0.0, z - 0.17, x1, HW, z + 0.17))
# the gate's frame: the portal (dark, 0.6 wide) with the hazard stripes, the lintel; the overhead track along the facade
gfr = grp("GateFrame", MI["Dark"])
for a, b in ((GX0 - 0.6, GX0 + 0.02), (GX1 - 0.02, GX1 + 0.6)):
    box(gfr, (a, 0.0, HZ0 - 0.30), (b, GH - 0.01, HZ0 + HT + 0.02))
    for k in range(int(GH / 0.4)):
        g = warn if k % 2 == 0 else blk
        poly(g, [(a, k * 0.4, HZ0 - 0.305), (b, k * 0.4, HZ0 - 0.305), (b, (k + 1) * 0.4, HZ0 - 0.305), (a, (k + 1) * 0.4, HZ0 - 0.305)], (0, 0, -1))
box(gfr, (GX0 - 0.61, GH - 0.01, HZ0 - 0.31), (GX1 + 0.61, GH + 0.6, HZ0 + HT + 0.03))
box(metal, (GX0 - 5.6, GH + 0.10, HZ0 - 0.75), (GX1 + 5.6, GH + 0.30, HZ0 - 0.55))        # the track beam
for x in (GX0 - 5.4, GX0 - 0.3, GX1 + 0.3, GX1 + 5.4): box(metal, (x - 0.08, GH + 0.10, HZ0 - 0.75), (x + 0.08, GH + 0.30, HZ0 - 0.30))
box(sin, (GX0 - 5.6, -0.10, HZ0 - 0.90), (GX1 + 5.6, 0.06, HZ0 - 0.40), "xXzZY")             # the bottom guide's sill
# the gate's two leaves (each its group), 5.0 x 5.9 m, outside the wall; they slide apart along the track to x GX0-5 / GX1+5
GL_T = 0.20; GZ0, GZ1 = HZ0 - 0.55, HZ0 - 0.35
gate = []
for k, (a, b) in enumerate(((GX0, 0.5 * (GX0 + GX1)), (0.5 * (GX0 + GX1), GX1))):
    g = grp("GateLeaf%d" % (k + 1), MI["Panel"]); gate.append(g)
    box(g, (a, 0.05, GZ0), (b, GH - 0.05, GZ1))
    for y in (0.05, 1.5, 3.0, 4.5):                                                       # the leaf's horizontal stiffeners
        box(g, (a + 0.05, y, GZ0 - 0.08), (b - 0.05, y + 0.18, GZ0))
        box(g, (a + 0.05, y, GZ1), (b - 0.05, y + 0.18, GZ1 + 0.08))
    if k == 0: box(g, (b - 0.055, 0.06, GZ0 - 0.02), (b - 0.005, GH - 0.06, GZ1 + 0.02))   # the meeting edge's rubber seal (on leaf 1 only)
    for hx in (a + 0.5, b - 0.5):                                         # the hangers: up to the rollers in the track
        box(g, (hx - 0.06, GH - 0.05, GZ0 + 0.04), (hx + 0.06, GH + 0.14, GZ1 - 0.04))
        box(g, (hx - 0.14, GH + 0.14, -4.73), (hx + 0.14, GH + 0.22, -4.57))
gwarn = grp("GateLeafStripes", MI["Warn"])                                                # (moved with the leaves: their own groups)
gate_str = []
for k, (a, b) in enumerate(((GX0, 0.5 * (GX0 + GX1)), (0.5 * (GX0 + GX1), GX1))):
    g = grp("GateLeaf%dStripes" % (k + 1), MI["Warn"]); gate_str.append(g)
    for y in (0.05, 4.5):
        poly(g, [(a + 0.05, y, GZ0 - 0.085), (b - 0.05, y, GZ0 - 0.085), (b - 0.05, y + 0.18, GZ0 - 0.085), (a + 0.05, y + 0.18, GZ0 - 0.085)], (0, 0, -1))
# the hangar's lights: six high-bay spot lamps under the trusses (the work circuit), the duty lamps on the walls, the flood
# over the gate (outside) - housings dark, the glowing lens its circuit's material
lampWork = grp("LampWork", MI["LampWork"]); lampDuty = grp("LampDuty", MI["LampDuty"]); lampGate = grp("LampGate", MI["LampGate"])
WORK = [(x, z) for z in (1.0, 9.0) for x in (5.0, 12.0, 19.0)]
def ydown(x): # the vault's underside height at x (the lamps hang 0.9 m under it)
    t = (x - HX0) / SPAN; return vault(t)[1]
for x, z in WORK:
    y = ydown(x) - 0.9
    cyl(metal, "y", (x, z), 0.02, y + 0.35, ydown(x), 6)
    cyl(hrib, "y", (x, z), 0.30, y + 0.05, y + 0.35, 16)
    cyl(lampWork, "y", (x, z), 0.26, y, y + 0.05, 16)
DUTY = [(HX0 + HT + 0.12, 2.0), (HX1 - HT - 0.12, 2.0), (HX0 + HT + 0.12, 12.0), (HX1 - HT - 0.12, 12.0)]
for x, z in DUTY:
    s = 1 if x < 10 else -1
    box(hrib, (x - 0.10 * s if s > 0 else x, 4.2, z - 0.25), (x if s > 0 else x + 0.10, 4.5, z + 0.25))
    box(lampDuty, (x - 0.12 if s < 0 else x, 4.25, z - 0.2), (x + 0.12 if s > 0 else x, 4.45, z + 0.2), "xX")
GATE_FLOOD = (12.0, GH + 1.0, HZ0 - 0.6)
box(hrib, (11.4, GH + 0.65, HZ0 - 0.75), (12.6, GH + 1.15, HZ0 - 0.25))
sheet(lampGate, [(11.5, GH + 0.66, HZ0 - 0.70), (12.5, GH + 0.66, HZ0 - 0.70), (12.5, GH + 0.66, HZ0 - 0.30), (11.5, GH + 0.66, HZ0 - 0.30)], (0, -1, 0))
# the cable tray along the hangar's north wall, the earthing bar, the floor markings of the two bays (thin plates on the
# ground: they ride the plinth's level, 3 cm proud - the ground may be lower in places: the paint then hangs, it is small)
box(metal, (HX0 + HT, 4.8, HZ1 - HT - 0.40), (HX1 - HT, 4.9, HZ1 - HT))
for x in range(1, 24, 2): box(metal, (x - 0.02, 4.9, HZ1 - HT - 0.05), (x + 0.02, 5.4, HZ1 - HT))

# ======================= the yard: the apron before the gate, the path to ШК-1, the flood mast =======================
ap = grp("Apron", MI["Sinter"])
#box(ap, (GX0 - 1.0, -0.20, HZ0 - 14.0), (GX1 + 1.0, 0.05, HZ0 - 0.90), "xXzZY")
#box(ap, (-11.20, -0.20, -6.0), (-8.80, 0.05, -0.80), "xXzZY")
YARD_MAST = (-3.0, -6.0)
cyl(metal, "y", YARD_MAST, 0.12, PL, 9.0, 10)
box(dark, (YARD_MAST[0] - 0.6, 8.6, YARD_MAST[1] - 0.25), (YARD_MAST[0] + 0.6, 9.1, YARD_MAST[1] + 0.25))
lampYard = grp("LampYard", MI["LampYard"])
sheet(lampYard, [(YARD_MAST[0] - 0.5, 8.59, YARD_MAST[1] - 0.2), (YARD_MAST[0] + 0.5, 8.59, YARD_MAST[1] - 0.2), (YARD_MAST[0] + 0.5, 8.59, YARD_MAST[1] + 0.2), (YARD_MAST[0] - 0.5, 8.59, YARD_MAST[1] + 0.2)], (0, -1, 0))
SOL_OUT.append((YARD_MAST[0] - 0.15, PL, YARD_MAST[1] - 0.15, YARD_MAST[0] + 0.15, 9.0, YARD_MAST[1] + 0.15))
# a cable trench cover from the block to the hangar (sinter slabs)
#box(ap, (-3.6, -0.10, 4.6), (-0.6, 0.04, 6.4), "xXzZY")
# the landing pad ПП-1 (the Delta-glider stands on it: OrbiterCrew's DeltaGlider.cfg gives its airlock): 32 x 32 m of sintered
# regolith 60 m south of the yard (the jets clear of the buildings), the yellow touchdown ring and the centre cross, the
# edge lights (the pad circuit), the road to the yard
PAD_C, PAD_H = (2.0, -62.0), 16.0
#box(ap, (PAD_C[0] - PAD_H, -0.40, PAD_C[1] - PAD_H), (PAD_C[0] + PAD_H, 0.06, PAD_C[1] + PAD_H), "xXzZY")
#box(ap, (PAD_C[0] - 3.0, -0.25, PAD_C[1] + PAD_H), (PAD_C[0] + 3.0, 0.04, HZ0 - 14.0), "xXzZY")    # the road
#pw = grp("PadMarks", MI["Warn"])
#for k in range(48):                                                  # the ring: r 10.5..11.0
#    a0, a1 = 2 * math.pi * k / 48, 2 * math.pi * (k + 1) / 48
#    pts = [(PAD_C[0] + r * math.sin(a), 0.065, PAD_C[1] + r * math.cos(a)) for r, a in ((10.5, a0), (11.0, a0), (11.0, a1), (10.5, a1))]
#    poly(pw, pts, (0, 1, 0))
#for lo, hi in (((PAD_C[0] - 4.0, PAD_C[1] - 0.25), (PAD_C[0] + 4.0, PAD_C[1] + 0.25)), ((PAD_C[0] - 0.25, PAD_C[1] - 4.0), (PAD_C[0] + 0.25, PAD_C[1] + 4.0))):
#    poly(pw, [(lo[0], 0.066, lo[1]), (hi[0], 0.066, lo[1]), (hi[0], 0.066, hi[1]), (lo[0], 0.066, hi[1])], (0, 1, 0))
lampPad = grp("LampPad", MI["LampYard"])                                 # (on the yard's circuit)
PAD_LIGHTS = []
for k in range(16):
    a = 2 * math.pi * k / 16
    x, z = PAD_C[0] + (PAD_H - 0.5) * math.sin(a) * 1.0, PAD_C[1] + (PAD_H - 0.5) * math.cos(a)
    x = max(PAD_C[0] - PAD_H + 0.5, min(PAD_C[0] + PAD_H - 0.5, x)); z = max(PAD_C[1] - PAD_H + 0.5, min(PAD_C[1] + PAD_H - 0.5, z))
    cyl(metal, "y", (x, z), 0.10, 0.06, 0.22, 8); cyl(lampPad, "y", (x, z), 0.07, 0.22, 0.30, 8)
    PAD_LIGHTS.append((x, z))
SOL_OUT.append((PAD_C[0] - 1e-3, -1.0, PAD_C[1] - 1e-3, PAD_C[0] + 1e-3, -0.9, PAD_C[1] + 1e-3))   # (no solid: a marker only)
SOL_OUT.pop()

# ======================= the furniture (inside: solids with their tops) =======================
desk = grp("Desk", MI["Desk"]); seat = grp("Seats", MI["Seat"]); pface = grp("PanelFaces", MI["PanelFace"])
o2g = grp("BottlesO2", MI["O2"]); n2g = grp("BottlesN2", MI["N2"])
def solid(g, lo, hi, faces="xXzZYy"):
    box(g, lo, hi, faces); SOL_IN.append((lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]))
# the duty post БЩУ ТП: the desk along the north wall, its sloped console riser with the big screen, the operator's chair
solid(desk, (-12.6, FY, 10.70), (-7.8, FY + 0.76, 11.55))
box(desk, (-12.5, FY + 0.76, 11.05), (-7.9, FY + 1.30, 11.55))
SEATS = []                                                           # x, z, hips, facing (x, z), label ru, en
def chair(x, z, fx, fz, ru, en):
    # pedestal, pan, back (the back on the side opposite the facing)
    cyl(metal, "y", (x, z), 0.04, FY, FY + 0.42, 8)
    box(seat, (x - 0.26, FY + 0.42, z - 0.26), (x + 0.26, FY + 0.50, z + 0.26))
    bx, bz = x - fx * 0.24, z - fz * 0.24
    box(seat, (bx - (0.26 if fz else 0.04), FY + 0.50, bz - (0.04 if fz else 0.26)), (bx + (0.26 if fz else 0.04), FY + 1.10, bz + (0.04 if fz else 0.26)))
    SEATS.append((x, z, 0.52, fx, fz, ru, en))
chair(-10.2, 10.05, 0, 1, "кресло дежурного (БЩУ)", "duty officer's chair")
# the rest zone: the table and two chairs, a bench along the west part of the south wall, the water and food cabinet
solid(desk, (-7.6, FY, 6.1), (-5.6, FY + 0.74, 7.3))
cyl(metal, "y", (-6.6, 6.7), 0.06, FY, FY + 0.70, 8)
chair(-6.6, 5.55, 0, 1, "кресло у стола", "chair at the table")
chair(-6.6, 7.85, 0, -1, "кресло у стола", "chair at the table")
solid(seat, (-15.4, FY, 4.45), (-13.0, FY + 0.45, 5.0))
solid(pan, (-15.5, FY, 10.6), (-14.4, FY + 1.9, 11.55))            # the galley cabinet (water, food)
# ПОМ. 102: the life-support units (regeneration, thermal control) and the gas store: O2 / N2 bottles in a rack, the
# filling ramp for the suits on the wall
solid(pan, (-21.55, FY, 8.20), (-20.45, FY + 2.20, 10.60)); box(blk, (-20.46, FY + 1.2, 8.4), (-20.44, FY + 2.0, 10.4), "X")
solid(pan, (-19.20, FY, 10.70), (-16.60, FY + 1.80, 11.55))
for k in range(6):
    x = -21.25 + 0.36 * k
    cyl(o2g if k < 4 else n2g, "y", (x, 8.45), 0.15, FY, FY + 1.55, 12)
    cyl(metal, "y", (x, 8.45), 0.04, FY + 1.55, FY + 1.70, 6)
box(metal, (-21.5, FY + 1.0, 8.12), (-19.2, FY + 1.06, 8.70))
SOL_IN.append((-21.45, FY, 8.20, -19.0, FY + 1.7, 8.75))
box(pface, (-17.8, FY + 0.9, 8.12), (-16.4, FY + 1.8, 8.20))       # the filling ramp
for k in range(3): cyl(o2g, "z", (-17.5 + 0.45 * k, FY + 1.2), 0.04, 8.20, 8.35, 8)
# ПОМ. 103: the workbench, the tool wall, two suit stands, the spares shelves
solid(desk, (-21.55, FY, 4.50), (-20.75, FY + 0.92, 7.70))
box(pface, (-21.59, FY + 1.2, 4.7), (-21.55, FY + 2.3, 7.5), "X")
for k in range(5): box(metal, (-21.55, FY + 1.35 + 0.22 * k, 4.8), (-21.45, FY + 1.37 + 0.22 * k, 7.4))
for z in (5.2, 6.8):                                                 # the suit stands: a frame and a back plate
    cyl(metal, "y", (-17.2, z), 0.04, FY, FY + 1.9, 8)
    box(metal, (-17.45, FY + 1.5, z - 0.30), (-16.95, FY + 1.56, z + 0.30))
    box(pan, (-17.30, FY, z - 0.35), (-17.10, FY + 0.06, z + 0.35))
    SOL_IN.append((-17.5, FY, z - 0.35, -16.9, FY + 1.9, z + 0.35))
solid(pan, (-19.8, FY, 7.35), (-17.9, FY + 2.0, 7.85))

# ======================= the control panels: the faces, the button caps (each its group and material), the screens =======
# A panel: (name, centre, out normal (axis), screen w x h m (0 none), screen px, the buttons [(label ru, label en)]).
# The face is 0.6 m wide; the screen over the buttons; the buttons in rows of three, 0.12 m apart, caps 0.05 m.
PANELS = [
    ("МЩУ ШК-1", (-11.58, FY + 1.30, 2.40), (1, 0, 0), (0.48, 0.24), (512, 256),
     [("1 - проверка скафандра", "1 - suit check"), ("2 - откачка", "2 - depressurise"), ("3 - наддув", "3 - pressurise"),
      ("4 - наружная дверь", "4 - outer door"), ("5 - внутренняя дверь", "5 - inner door"), ("6 - аварийный стоп", "6 - emergency stop")]),
    ("МЩУ ШК-2", (-2.20, FY + 1.30, 10.18), (0, 0, -1), (0.48, 0.24), (512, 256),
     [("1 - проверка скафандра", "1 - suit check"), ("2 - откачка", "2 - depressurise"), ("3 - наддув", "3 - pressurise"),
      ("4 - наружная дверь", "4 - outer door"), ("5 - внутренняя дверь", "5 - inner door"), ("6 - аварийный стоп", "6 - emergency stop")]),
    ("ВЫЗОВ ШК-1 (двор)", (-8.30, 1.35, 0.40 - K - 0.02), (0, 0, -1), (0.24, 0.12), (256, 128), [("вызов шлюза", "call the airlock")]),
    ("ВЫЗОВ ШК-1 (холл)", (-8.70, FY + 1.30, 4.42), (0, 0, 1), (0.24, 0.12), (256, 128), [("вызов шлюза", "call the airlock")]),
    ("ВЫЗОВ ШК-2 (холл)", (-4.42, FY + 1.30, 10.30), (-1, 0, 0), (0.24, 0.12), (256, 128), [("вызов шлюза", "call the airlock")]),
    ("ВЫЗОВ ШК-2 (ангар)", (HX0 + HT + 0.02, 1.35, 10.95), (1, 0, 0), (0.24, 0.12), (256, 128), [("вызов шлюза", "call the airlock")]),
    ("ЩУ ВА-1 (ангар)", (HX0 + HT + 0.02, 1.40, 11.90), (1, 0, 0), (0.48, 0.24), (512, 256),
     [("1 - открыть ворота", "1 - open the gate"), ("2 - закрыть ворота", "2 - close the gate"), ("3 - стоп", "3 - stop")]),
    ("ЩО-1 (ангар)", (HX0 + HT + 0.02, 1.40, 12.90), (1, 0, 0), (0.48, 0.24), (512, 256),
     [("1 - рабочее освещение", "1 - work lighting"), ("2 - дежурное освещение", "2 - duty lighting"), ("3 - аварийное освещение", "3 - emergency lighting"),
      ("4 - прожектор ворот", "4 - gate flood"), ("5 - прожектор двора", "5 - yard flood")]),
    ("ЩУ ВА-1 (двор)", (GX1 + 1.10, 1.40, HZ0 - 0.32), (0, 0, -1), (0.48, 0.24), (512, 256),
     [("1 - открыть ворота", "1 - open the gate"), ("2 - закрыть ворота", "2 - close the gate"), ("3 - стоп", "3 - stop")]),
    ("БЩУ ТП", (-10.20, FY + 1.03, 11.04), (0, 0, -1), (1.10, 0.48), (1024, 448),
     [("1 - освещение холла", "1 - hall lighting"), ("2 - освещение помещений", "2 - rooms' lighting"), ("3 - вахта: полумрак", "3 - watch: half-light"),
      ("4 - вызов ШК-1", "4 - call airlock 1"), ("5 - вызов ШК-2", "5 - call airlock 2"), ("6 - сброс сигнализации", "6 - reset the alarm")]),
]
caps, screens = [], []                     # [(panel, k, group, material index, pos, normal, label ru, en)], [(panel, group, slot, w, h)]
def frame(n):
    """the panel's right and up vectors for an outward normal n (y up)"""
    up = (0, 1, 0); r = cross(n, up)            # facing it, 'right' is to the viewer's right (cross(up, n) mirrored the signs)
    return r, up
for pi, (name, c, n, (sw, sh), (pw, ph), btns) in enumerate(PANELS):
    r, up = frame(n)
    rows = (len(btns) + 2) // 3
    W = max(0.30, sw + 0.10) if len(btns) > 1 else 0.30
    Hh = (sh + 0.06 if sw else 0.0) + rows * 0.12 + 0.08
    top = sh + 0.04 if sw else 0.0
    def P(du, dv, dn=0.0): return (c[0] + r[0] * du + up[0] * dv + n[0] * dn, c[1] + r[1] * du + up[1] * dv + n[1] * dn, c[2] + r[2] * du + up[2] * dv + n[2] * dn)
    y0 = -rows * 0.12 - 0.04; y1 = top + 0.04
    # the face: a dark box 3 cm proud of the wall with a chamfered frame
    corners = [P(-W / 2, y0), P(W / 2, y0), P(W / 2, y1), P(-W / 2, y1)]
    poly(pface, [P(-W / 2, y0, 0.03), P(W / 2, y0, 0.03), P(W / 2, y1, 0.03), P(-W / 2, y1, 0.03)], n)
    for a, b in ((0, 1), (1, 2), (2, 3), (3, 0)):
        pa, pb = corners[a], corners[b]
        qa = tuple(pa[i] + n[i] * 0.03 for i in range(3)); qb = tuple(pb[i] + n[i] * 0.03 for i in range(3))
        mid = tuple(0.5 * (pa[i] + pb[i]) - c[i] for i in range(3)); out = unit(tuple(mid[i] - dot(mid, n) * n[i] for i in range(3)))
        poly(dark, [pa, pb, qb, qa], out)
    if sw:
        g = grp("Screen%d" % pi, MI["Screen"], SCREEN_TEX)
        uv = [(0, 1), (1, 1), (1, 0), (0, 0)]
        poly(g, [P(-sw / 2, 0.02, 0.032), P(sw / 2, 0.02, 0.032), P(sw / 2, 0.02 + sh, 0.032), P(-sw / 2, 0.02 + sh, 0.032)], n, uv)
        screens.append((pi, g, pw, ph))
    for k, (ru, en) in enumerate(btns):
        row, col = k // 3, k % 3
        ncol = min(3, len(btns) - row * 3)
        du = (col - (ncol - 1) / 2) * 0.12
        dv = -0.08 - row * 0.12
        mname = "Cap%d_%d" % (pi, k)
        mat(mname, (0.30, 0.32, 0.30) if "стоп" not in ru else (0.45, 0.10, 0.08), 0.3, emit=0.0)
        g = grp(mname, MI[mname])
        cc = P(du, dv)
        ax = "x" if abs(n[0]) > 0.5 else "z"
        a0, a1 = (cc[0] + n[0] * 0.032, cc[0] + n[0] * 0.075) if ax == "x" else (cc[2] + n[2] * 0.032, cc[2] + n[2] * 0.075)
        cyl(g, ax, (cc[1], cc[2]) if ax == "x" else (cc[0], cc[1]), 0.025, min(a0, a1), max(a0, a1), 12)
        b0 = cc[0] if ax == "x" else cc[2]; nn = n[0] if ax == "x" else n[2]
        cyl(metal, ax, (cc[1], cc[2]) if ax == "x" else (cc[0], cc[1]), 0.034, min(b0 + nn * 0.031, b0 + nn * 0.05), max(b0 + nn * 0.031, b0 + nn * 0.05), 12)
        tip = P(du, dv, 0.075)
        caps.append((pi, k, g, MI[mname], tip, n, ru, en))

# ======================= the signs (one atlas, tp_signs.dds - make_base_textures.py writes it) =======================
# (text, plate w x h m, centre, normal): red letters on white enamel plates, as at a plant
SIGNS = [("ТП-1  ТЕХНИЧЕСКИЙ ПУНКТ", 4.0, 0.5, (-13.0, H1 - 0.62, 4.00 - K - 0.16), (0, 0, -1)),   # in front of the pilasters
         ("ШК-1", 1.0, 0.36, (-10.0, FY + DH + 0.55, 0.40 - K - 0.13), (0, 0, -1)),
         ("ПОМ. 201  АНГАР", 3.2, 0.5, (12.0, GH + 1.9, HZ0 - 0.31), (0, 0, -1)),
         ("ВА-1", 1.0, 0.36, (GX1 + 1.10, 2.10, HZ0 - 0.31), (0, 0, -1)),
         ("ШК-2", 1.0, 0.36, (HX0 + HT + 0.01, FY + DH + 0.55, 9.0), (1, 0, 0)),
         ("ПОМ. 101  ХОЛЛ", 1.6, 0.30, (-10.2, CY - 0.35, 11.59), (0, 0, -1)),
         ("ПОМ. 102  СЖО", 1.2, 0.26, (-15.59, FY + 2.55, 10.0), (1, 0, 0)),
         ("ПОМ. 103  ИНСТРУМЕНТАЛЬНАЯ", 1.6, 0.26, (-15.59, FY + 2.55, 6.0), (1, 0, 0)),
         ("ШК-1", 0.6, 0.22, (-10.0, FY + DH + 0.32, 4.41), (0, 0, 1)),
         ("ШК-2", 0.6, 0.22, (-4.41, FY + DH + 0.32, 9.0), (-1, 0, 0))]
sg = grp("Signs", MI["Signs"])
SIGN_ROWS = len(SIGNS)
for k, (txt, w, h, c, n) in enumerate(SIGNS):
    r, up = frame(n)
    def P(du, dv): return (c[0] + r[0] * du + up[0] * dv, c[1] + r[1] * du + up[1] * dv, c[2] + r[2] * du + up[2] * dv)
    v0, v1 = k / SIGN_ROWS, (k + 1) / SIGN_ROWS                   # the atlas: one row per sign, the text fitted in it
    poly(sg, [P(-w / 2, -h / 2), P(w / 2, -h / 2), P(w / 2, h / 2), P(-w / 2, h / 2)], n, [(0, v1), (1, v1), (1, v0), (0, v0)])

# ======================= the lights (LightEmitters of the module): circuit, kind, position, direction, range, cone =======
# the D3D11 client's interiors (D3D11Lights.h): the rooms as volumes (the eye adapts within EV100 evMin..evMax, no sky
# light inside, the room's lamps' bounce) and the panels as area lamps (lit side = cross(ex, ey) = down). Room ids: 1 the
# hall, 2 room 102, 3 room 103, 4 ШК-1, 5 ШК-2, 6 the hangar. Fluxes physical: an office panel 60x60 ~3000-4000 lm
VOLUMES = [((-15.6, FY, 4.4, -4.4, CY, 11.6), 5.0, 8.0, 0.5, 1), ((-21.6, FY, 8.1, -16.0, CY, 11.6), 5.0, 8.0, 0.5, 2),
           ((-21.6, FY, 4.4, -16.0, CY, 7.9), 5.0, 8.0, 0.5, 3), ((-11.6, FY, 0.8, -8.4, LCY, 4.0), 5.0, 8.0, 0.4, 4),
           ((-4.0, FY, 7.0, -0.4, LCY, 10.2), 5.0, 8.0, 0.4, 5), ((HX0 + HT, 0.0, HZ0 + HT, HX1 - HT, HW + HR, HZ1 - HT), 4.0, 9.0, 2.0, 6)]
AREA = []      # (room / circuit, centre, half-axis x, half-axis z, lumen, rgb)
for x, z in HALL_LAMPS: AREA.append((1, (x, CY - 0.112, z), (HL, 0, 0), (0, 0, HW_), 8000.0, (1.0, 0.95, 0.88)))
for x, z in ROOM_LAMPS: AREA.append((2 if z > 8 else 3, (x, CY - 0.112, z), (RL, 0, 0), (0, 0, RW), 4500.0, (1.0, 0.95, 0.88)))
AREA.append((4, (-10.0, LCY - 0.112, 2.4), (0.38, 0, 0), (0, 0, 0.38), 3500.0, (1.0, 0.97, 0.92)))
AREA.append((5, (-2.2, LCY - 0.112, 8.6), (0.38, 0, 0), (0, 0, 0.38), 3500.0, (1.0, 0.97, 0.92)))
# the duty strips: circuit 10 + room (the module switches them with the duty light); the lit face = cross(ex, ey) = n
for room, (u, v), n, L in DUTY_STRIPS:
    y = FY + 0.30; lm = 60.0 * L                                        # ~60 lm a metre: a dim warm glow
    if n[2]: AREA.append((10 + room, (u, y, v + n[2] * 0.052), (0, 0.02, 0), (L / 2 * -n[2], 0, 0), lm, (1.0, 0.70, 0.42)))
    else: AREA.append((10 + room, (u + n[0] * 0.052, y, v), (0, 0.02, 0), (0, 0, L / 2 * n[0]), lm, (1.0, 0.70, 0.42)))
LIGHTS = []   # (circuit, spot 1 / point 0, pos, dir, range m, umbra deg, penumbra deg, colour (r, g, b), intensity)
for x, z in HALL_LAMPS: LIGHTS.append(("LampHall", 0, (x, CY - 0.25, z), (0, -1, 0), 9.0, 0, 0, (1.0, 0.95, 0.86), 1.0))
for x, z in ROOM_LAMPS: LIGHTS.append(("LampRooms", 0, (x, CY - 0.25, z), (0, -1, 0), 7.0, 0, 0, (1.0, 0.95, 0.86), 0.8))
LIGHTS.append(("LampLock1", 1, (-10.0, LCY - 0.15, 2.4), (0, -1, 0), 6.0, 100, 140, (1.0, 0.97, 0.92), 0.8))
LIGHTS.append(("LampLock2", 1, (-2.2, LCY - 0.15, 8.6), (0, -1, 0), 6.0, 100, 140, (1.0, 0.97, 0.92), 0.8))
for x, z in WORK: LIGHTS.append(("LampWork", 1, (x, ydown(x) - 0.95, z), (0, -1, 0), 22.0, 90, 130, (1.0, 0.96, 0.90), 1.0))
for x, z in DUTY: LIGHTS.append(("LampDuty", 0, (x + (0.3 if x < 10 else -0.3), 4.35, z), (0, -1, 0), 10.0, 0, 0, (1.0, 0.78, 0.50), 0.35))
LIGHTS.append(("LampGate", 1, (GATE_FLOOD[0], GATE_FLOOD[1] - 0.4, GATE_FLOOD[2] - 0.1), unit((0, -0.55, -1)), 45.0, 60, 100, (1.0, 0.96, 0.90), 1.0))
LIGHTS.append(("LampYard", 1, (YARD_MAST[0], 8.5, YARD_MAST[1]), unit((0.3, -1.0, -0.4)), 50.0, 70, 110, (1.0, 0.95, 0.85), 1.0))
for k in range(0, 16, 4):                                            # the pad: four of its edge lights shine (the rest glow)
    x, z = PAD_LIGHTS[k]; LIGHTS.append(("LampYard", 0, (x, 0.45, z), (0, -1, 0), 8.0, 0, 0, (0.55, 1.0, 0.65), 0.3))
for (bx, bz) in ((-8.75, 0.30), (0.35, 10.05)):                     # the airlocks' cycle beacons (amber, blink)
    LIGHTS.append(("Beacon", 0, (bx, FY + DH + 0.33, bz + (-0.15 if bz < 1 else 0)), (0, -1, 0), 5.0, 0, 0, (1.0, 0.50, 0.08), 0.5))

# ======================= the vehicles' capsules along the outer walls (TantraSolid kind 1: a..b at 1.0 m, radius) =======
def caps_line(x0, z0, x1, z1, r=0.40, maxlen=6.0):
    L = math.hypot(x1 - x0, z1 - z0); n = max(1, int(math.ceil(L / maxlen)))
    for k in range(n):
        CAPS.append(((x0 + (x1 - x0) * k / n, 1.0, z0 + (z1 - z0) * k / n), (x0 + (x1 - x0) * (k + 1) / n, 1.0, z0 + (z1 - z0) * (k + 1) / n), r))
hi_ = HT / 2
caps_line(HX0 + hi_, HZ0, HX0 + hi_, HZ1)            # the hangar: west, east, north, the south wall's two parts
caps_line(HX1 - hi_, HZ0, HX1 - hi_, HZ1)
caps_line(HX0, HZ1 - hi_, HX1, HZ1 - hi_)
caps_line(HX0, HZ0 + hi_, GX0 - 0.6, HZ0 + hi_); caps_line(GX1 + 0.6, HZ0 + hi_, HX1, HZ0 + hi_)
N_FIXED_CAPS = len(CAPS)
caps_line(-22.2, 3.8, -12.2, 3.8, 0.45); caps_line(-7.8, 3.8, -4.0, 3.8, 0.45)          # the block's south face, ШК-1
caps_line(-12.2, 0.2, -7.8, 0.2, 0.45); caps_line(-12.2, 0.2, -12.2, 3.8, 0.45); caps_line(-7.8, 0.2, -7.8, 3.8, 0.45)
caps_line(-22.2, 3.8, -22.2, 12.2, 0.45); caps_line(-22.2, 12.2, -4.0, 12.2, 0.45)
caps_line(-4.0, 6.4, -0.4, 6.4, 0.45); caps_line(-4.0, 10.8, -0.4, 10.8, 0.45)
caps_line(YARD_MAST[0], YARD_MAST[1], YARD_MAST[0], YARD_MAST[1] + 0.01, 0.30)
assert len(CAPS) + 2 <= 32, len(CAPS)                 # (+ the gate's two leaves, added by the module where they are now)

# ======================= the fittings: what makes a hangar a hangar and a hall a working room (2026-10-21) =======================
def ring_band(g, c, y0, r0, y1, r1, seg=20):
    """a conical band about the vertical through c (x, z) from radius r0 at y0 to r1 at y1, seen from both sides"""
    for k in range(seg):
        a0, a1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
        P = lambda a, y, r: (c[0] + r * math.cos(a), y, c[1] + r * math.sin(a))
        q = [P(a0, y0, r0), P(a1, y0, r0), P(a1, y1, r1), P(a0, y1, r1)]
        m = 0.5 * (a0 + a1); n = unit((math.cos(m) * (y1 - y0), r0 - r1, math.sin(m) * (y1 - y0)))
        sheet(g, q, n)
# the high-bays' reflectors: a spun aluminium cone round each lens, its lip under the lens
for x, z in WORK:
    y = ydown(x) - 0.9
    ring_band(metal, (x, z), y + 0.30, 0.31, y - 0.16, 0.46)
# the overhead travelling crane (5 t): the runway beams along the long walls on brackets at the columns, the bridge girder
# across the span parked at the north end, the hoist trolley, its rope and hook block (over the heads: no collision)
CRY = 5.70                                                            # the rails' top
for x0, x1 in ((HX0 + HT + 0.25, HX0 + HT + 0.55), (HX1 - HT - 0.55, HX1 - HT - 0.25)):
    box(hrib, (x0, CRY - 0.45, HZ0 + HT + 0.2), (x1, CRY, HZ1 - HT - 0.2))                    # the runway beam
    box(metal, (x0 + 0.10, CRY, HZ0 + HT + 0.2), (x1 - 0.10, CRY + 0.06, HZ1 - HT - 0.2))       # the rail
    for z in [HZ0 + 0.25 + 3.0 * k for k in range(7)]:
        c0, c1 = (HX0 + HT + 0.252, x1) if x0 < 10 else (x0, HX1 - HT - 0.252)   # from the column's face out under the beam
        box(metal, (c0, CRY - 0.75, z - 0.10), (c1, CRY - 0.45, z + 0.10))   # the bracket
BZ = HZ1 - HT - 1.6
steel = grp("CraneSteel", MI["Steel"])
box(steel, (HX0 + HT + 0.35, CRY + 0.06, BZ - 0.35), (HX1 - HT - 0.35, CRY + 0.76, BZ + 0.35))     # the bridge girder (steel grey)
for x in (HX0 + HT + 0.40, HX1 - HT - 1.40):
    box(dark, (x, CRY + 0.04, BZ - 0.55), (x + 1.0, CRY + 0.46, BZ + 0.55))                         # the end carriages (2 cm under the girder's face)
box(dark, (11.3, CRY + 0.76, BZ - 0.45), (12.7, CRY + 1.16, BZ + 0.45))                            # the hoist trolley
box(dark, (11.6, CRY - 0.25, BZ - 0.30), (12.4, CRY + 0.06, BZ + 0.30))                            # the hoist drum under it
cyl(metal, "y", (12.0, BZ), 0.012, 3.20, CRY - 0.25, 6)                                            # the rope
box(steel, (11.85, 2.90, BZ - 0.12), (12.15, 3.20, BZ + 0.12)); cyl(metal, "y", (12.0, BZ), 0.05, 2.62, 2.90, 8)   # the hook block, the hook
# the service lines along the north wall (O2 light blue, N2 black, water green, as in the hall) on their brackets
for g, y in ((grp("HPipeO2", MI["O2"]), 3.00), (grp("HPipeN2", MI["N2"]), 3.25), (grp("HPipeW", MI["Green"]), 3.50)):
    cyl(g, "x", (y, HZ1 - HT - 0.15), 0.06, HX0 + HT + 0.3, HX1 - HT - 0.3, 10)
for x in range(2, 24, 3): box(metal, (x - 0.04, 2.85, HZ1 - HT - 0.30), (x + 0.04, 3.62, HZ1 - HT))
# the fire cabinet by ШК-2 (red, the hose reel's window), the workbench and the tool cabinet by the station
red_g = grp("FireCabinet", MI["Red"])
box(red_g, (HX0 + HT, 0.80, 5.10), (HX0 + HT + 0.30, 1.90, 5.90)); SOL_OUT.append((HX0 + HT, 0.0, 5.10, HX0 + HT + 0.30, 1.90, 5.90))
box(glass, (HX0 + HT + 0.30, 1.05, 5.25), (HX0 + HT + 0.31, 1.75, 5.75), "X")
box(desk, (2.40, 0.0, 12.55), (4.60, 0.92, 13.45)); SOL_OUT.append((2.40, 0.0, 12.55, 4.60, 0.92, 13.45))
box(pface, (2.45, 1.10, 13.44), (4.55, 2.10, 13.48), "z")                                          # the tool board over it
for k in range(4): box(metal, (2.6 + 0.5 * k, 1.30, 13.38), (2.66 + 0.5 * k, 1.90, 13.44))
box(pan, (5.00, 0.0, 12.85), (6.10, 1.85, 13.45)); SOL_OUT.append((5.00, 0.0, 12.85, 6.10, 1.85, 13.45))
for y in (0.30, 0.75, 1.20): box(dark, (5.05, y, 12.84), (6.05, y + 0.02, 12.85), "z")             # its drawers' lines
# the hall: the ventilation duct down the middle under the ceiling with its grilles, the green «ВЫХОД» over the locks'
# inner doors, a fire extinguisher by ШК-1, the lamp panels' diffuser frames
box(metal, (-15.50, CY - 0.42, 7.75), (-4.50, CY - 0.06, 8.25), "xXzZy")
for x in range(-15, -4, 2): box(dark, (x - 0.30, CY - 0.43, 7.80), (x + 0.30, CY - 0.42, 8.20), "y")
exitg = grp("ExitSigns", MI["ExitSign"])
box(exitg, (-10.35, FY + DH + 0.62, 4.40), (-9.65, FY + DH + 0.80, 4.48), "xXZyY")                 # over ШК-1's inner door
box(exitg, (-4.48, FY + DH + 0.62, 8.65), (-4.40, FY + DH + 0.80, 9.35), "xzZyY")                  # over ШК-2's inner door
cyl(red_g, "y", (-11.20, 4.62), 0.08, FY + 0.05, FY + 0.60, 12); cyl(blk, "y", (-11.20, 4.62), 0.03, FY + 0.60, FY + 0.70, 8)
for x, z in HALL_LAMPS:                                                # the diffuser's lip round each panel's glowing face
    a, b = HL, HW_
    for lo, hi in (((x - a - 0.04, CY - 0.13, z - b - 0.04), (x + a + 0.04, CY - 0.10, z - b)), ((x - a - 0.04, CY - 0.13, z + b), (x + a + 0.04, CY - 0.10, z + b + 0.04)),
                   ((x - a - 0.04, CY - 0.13, z - b), (x - a, CY - 0.10, z + b)), ((x + a, CY - 0.13, z - b), (x + a + 0.04, CY - 0.10, z + b))):
        box(metal, lo, hi, "xXzZy")

# ======================= write the mesh =======================
TEXLIST = sorted({m[4][0] for m in MATS if m[4]})
TEXLIST.append(SCREEN_TEX)                             # the screens' slots: one entry each (the module puts its surface there)
scr_slot0 = len(TEXLIST)                               # 1-based slot of the first screen = scr_slot0 + 0 ...
TEXLIST += [SCREEN_TEX] * (len(screens) - 1)
def uvp(p, n, tile):
    a = max(range(3), key=lambda k: abs(n[k]))
    u, v = (p[2], p[1]) if a == 0 else (p[0], p[2]) if a == 1 else (p[0], p[1])
    return u / tile, -v / tile
used = [g for g in groups if g[2]]
os.makedirs(os.path.dirname(MESH), exist_ok=True)
scr_of = {id(g): scr_slot0 + i for i, (_, g, _, _) in enumerate(screens)}
nv = 0
with open(MESH, "w", encoding="ascii", newline="\r\n") as f:
    f.write("MSHX1\nGROUPS %d\n" % len(used))
    for g in used:
        label, mi, vs, fs, tov = g
        tx = MATS[mi][4]
        slot = scr_of.get(id(g), (TEXLIST.index(tx[0]) + 1) if tx else 0)
        f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (label, mi + 1, slot, len(vs), len(fs)))
        for p, n, t in vs:
            if slot:
                tu, tv = t if t is not None else uvp(p, n, tx[1] if tx and tx[1] else 1.0)
                f.write("%.4f %.4f %.4f %.4f %.4f %.4f %.5f %.5f\n" % (*p, *n, tu, tv))
            else: f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (*p, *n))
        for a, b, c in fs: f.write("%d %d %d\n" % (a, b, c))
        nv += len(vs)
    f.write("MATERIALS %d\n" % len(MATS))
    for m in MATS: f.write("TP_%s\n" % m[0])
    for name, col, sp, em, tex, al, pw in MATS:
        if name == "PanelFace" and False: pass
        f.write("MATERIAL TP_%s\n%.3f %.3f %.3f %.2f\n%.3f %.3f %.3f %.2f\n%.2f %.2f %.2f 1 %d\n%.3f %.3f %.3f 1\n" % (
            name, *col, al, *col, al, sp, sp, sp, pw, *(c * em for c in col)))
    f.write("TEXTURES %d\n" % len(TEXLIST))
    for t in TEXLIST: f.write("%s\n" % t)
gi = {id(g): k for k, g in enumerate(used)}

# ======================= write the header =======================
def v3(p): return "{%.3f, %.3f, %.3f}" % tuple(p)
def bx(b): return "{%.3ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff}" % tuple(b)
def cstr(s): return '"' + s.replace('"', '\\"') + '"'
with open(GEO, "w", encoding="utf-8", newline="\n") as f:
    w = f.write
    w("// Written by Tantra_Design/tools/build_techpoint.py - do not edit. The technical point ТП-1 (TVehicles\\Building).\n")
    w("// Frame: x east, y up, z north, the ground at y = 0. Boxes: x0 y0 z0 x1 y1 z1 (as the Tantra's InteriorLayout.h).\n#pragma once\n")
    w("namespace tp {\nstruct Box { float x0, y0, z0, x1, y1, z1; };\n")
    w("constexpr double kFloorY = %.3f, kCeilY = %.3f, kLockCeilY = %.3f, kPlinth = %.3f;\n" % (FY, CY, LCY, PL))
    for nm, arr in (("Floors", FLOORS), ("SolidsIn", SOL_IN), ("SolidsOut", SOL_OUT)):
        w("constexpr int k%sCount = %d;\nconstexpr Box k%s[%d] = {\n%s};\n" % (nm, len(arr), nm, len(arr), ",\n".join("    " + bx(b) for b in arr)))
    w("// the vehicles' capsules (TantraSolid kind 1): a, b, radius; the first kFixedCaps are the hangar's own walls\n")
    w("struct Cap { double a[3], b[3], r; };\nconstexpr int kCapsCount = %d, kFixedCaps = %d;\nconstexpr Cap kCaps[%d] = {\n%s};\n" % (
        len(CAPS), N_FIXED_CAPS, len(CAPS), ",\n".join("    {%s, %s, %.2f}" % (v3(a), v3(b), r) for a, b, r in CAPS)))
    # the doors
    w("// the doors: the leaf's group, its wall's run (0 along x, 1 along z), the wall's thickness range, the opening u0..u1, the\n// slide sign (the leaf opens by (u1 - u0) * sign along u), the leaf's thickness\n")
    w("struct Door { const char* name; int grp; int run; double c0, c1, u0, u1; int sign; };\n")
    w("enum { kLock1Outer = 0, kLock1Inner, kLock2Inner, kLock2Outer, kDoorN };\n")
    w("constexpr Door kDoors[kDoorN] = {\n%s};\n" % ",\n".join("    {%s, %d, %d, %.3f, %.3f, %.3f, %.3f, %d}" % (cstr(d[0]), gi[id(leaves[d[0]])], 0 if d[1] == "x" else 1, d[2], d[3], d[4], d[5], d[6]) for d in DOORS))
    w("constexpr double kDoorH = %.3f, kLeafT = %.3f;\n" % (DH, LEAF_T))
    w("// the gate: two leaves (their own groups and their stripes'), closed over x %.2f..%.2f, z %.2f..%.2f; they slide apart by %.2f m\n" % (GX0, GX1, GZ0, GZ1, 0.5 * (GX1 - GX0)))
    w("constexpr int kGateGrp[2] = {%d, %d}, kGateStripeGrp[2] = {%d, %d};\n" % (gi[id(gate[0])], gi[id(gate[1])], gi[id(gate_str[0])], gi[id(gate_str[1])]))
    w("constexpr double kGateX0 = %.3f, kGateX1 = %.3f, kGateZ0 = %.3f, kGateZ1 = %.3f, kGateH = %.3f;\n" % (GX0, GX1, GZ0, GZ1, GH))
    w("constexpr double kHangar[4] = {%.3f, %.3f, %.3f, %.3f}, kHangarWall = %.3f, kHangarWallH = %.3f;   // outside: x0 x1 z0 z1\n" % (HX0, HX1, HZ0, HZ1, HT, HW))
    # rooms (for the air and the lights): the hall + rooms, ШК-1, ШК-2
    w("// the pressurised zones (x0 x1 z0 z1 at the floor): 0 the hall and rooms 102 / 103, 1 ШК-1, 2 ШК-2\n")
    w("constexpr double kZone[3][4] = {{-21.600, -4.400, 4.400, 11.600}, {-11.600, -8.400, 0.800, 4.000}, {-4.000, -0.400, 7.000, 10.200}};\n")
    # the airlocks' entries and exits (F): outside point, inside point, facing
    w("// the ways through the outer doors (F): where she stands outside (the ground), inside (the lock's floor), facing in\n")
    w("struct Way { double out[3], in[3], dirIn[3]; };\n")
    w("constexpr Way kWay[2] = {{{-10.000, 0.000, -0.900}, {-10.000, %.3f, 1.500}, {0, 0, 1}}, {{1.300, 0.000, 9.000}, {-1.300, %.3f, 9.000}, {-1, 0, 0}}};\n" % (FY, FY))
    # seats
    w("struct Seat { double x, z, hips, fx, fz; const char* ru; const char* en; };\nconstexpr int kSeatN = %d;\nconstexpr Seat kSeats[kSeatN] = {\n%s};\n" % (
        len(SEATS), ",\n".join("    {%.3f, %.3f, %.3f, %d, %d, %s, %s}" % (x, z, FY + h, fx, fz, cstr(ru), cstr(en)) for x, z, h, fx, fz, ru, en in SEATS)))
    # panels, buttons, screens
    w("struct Panel { const char* name; double c[3], n[3]; int screenGrp, screenSlot, w, h, nBtn, btn0; };\n")
    w("struct Button { int panel, k, grp, mat; double pos[3], n[3]; const char* ru; const char* en; };\n")
    btn0 = {};
    for i, cp in enumerate(caps): btn0.setdefault(cp[0], i)
    srec = {s[0]: s for s in screens}
    w("constexpr int kPanelN = %d, kButtonN = %d;\n" % (len(PANELS), len(caps)))
    w("constexpr Panel kPanels[kPanelN] = {\n%s};\n" % ",\n".join("    {%s, %s, %s, %d, %d, %d, %d, %d, %d}" % (
        cstr(p[0]), v3(p[1]), v3(p[2]), gi[id(srec[i][1])] if i in srec else -1, scr_of[id(srec[i][1])] if i in srec else 0,
        srec[i][2] if i in srec else 0, srec[i][3] if i in srec else 0, len(p[5]), btn0.get(i, 0)) for i, p in enumerate(PANELS)))
    w("constexpr Button kButtons[kButtonN] = {\n%s};\n" % ",\n".join("    {%d, %d, %d, %d, %s, %s, %s, %s}" % (
        pi, k, gi[id(g)], m, v3(p), v3(n), cstr(ru), cstr(en)) for pi, k, g, m, p, n, ru, en in caps))
    # the circuits' materials and the lights
    circ = ["LampHall", "LampRooms", "LampLock1", "LampLock2", "LampWork", "LampDuty", "LampYard", "LampGate", "Beacon"]
    w("enum { kCHall = 0, kCRooms, kCLock1, kCLock2, kCWork, kCDuty, kCYard, kCGate, kCBeacon, kCircuitN };\n")
    w("constexpr int kCircuitMat[kCircuitN] = {%s};\n" % ", ".join(str(MI[c]) for c in circ))
    w("constexpr int kBeaconGrp[2] = {%d, %d}, kRoofBeaconGrp = %d;\n" % (gi[id(beac1)], gi[id(beac2)], gi[id(beacon_roof)]))
    w("struct Light { int circuit, spot; double pos[3], dir[3], range, umbra, penumbra, col[3], k; };\nconstexpr int kLightN = %d;\n" % len(LIGHTS))
    w("constexpr Light kLights[kLightN] = {\n%s};\n" % ",\n".join("    {%d, %d, %s, %s, %.1f, %.1f, %.1f, %s, %.2f}" % (
        circ.index(c), s, v3(p), v3(d), rg, u, pn, v3(col), k) for c, s, p, d, rg, u, pn, col, k in LIGHTS))
    w("// the D3D11 interiors (D3D11Lights.h): the volumes (box, evMin, evMax, blend, room) and the area lamps (room, c, ex, ey, lm, rgb)\n")
    w("struct Vol { float box[6]; float evMin, evMax, blend; int room; };\nconstexpr int kVolN = %d;\nconstexpr Vol kVols[kVolN] = {\n%s};\n" % (
        len(VOLUMES), ",\n".join("    {{%.3ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff}, %.1ff, %.1ff, %.2ff, %d}" % (*b, e0, e1, bl, r) for b, e0, e1, bl, r in VOLUMES)))
    w("struct Area { int room; float c[3], ex[3], ey[3]; float lumen; float rgb[3]; };\nconstexpr int kAreaN = %d;\nconstexpr Area kAreas[kAreaN] = {\n%s};\n" % (
        len(AREA), ",\n".join("    {%d, {%.3ff, %.3ff, %.3ff}, {%.3ff, %.3ff, %.3ff}, {%.3ff, %.3ff, %.3ff}, %.1ff, {%.2ff, %.2ff, %.2ff}}" % (r, *c, *ex, *ey, lm, *rgb) for r, c, ex, ey, lm, rgb in AREA)))
    # the viewports as openings for the Sun (D3D11Lights Opening): the box round the hole, through the wall's thickness
    wins = [(cx - WIN_OUT[0] / 2, WIN_Y - WIN_OUT[1] / 2, 4.00 - K - 0.45, cx + WIN_OUT[0] / 2, WIN_Y + WIN_OUT[1] / 2, 4.45) for cx in WIN]
    w("constexpr int kWinN = %d;\nconstexpr Box kWinOpen[kWinN] = {\n%s};   // (room 1, the hall)\n" % (len(wins), ",\n".join("    " + bx(b) for b in wins)))
    w("constexpr int kMatCount = %d;\n}  // namespace tp\n" % len(MATS))
print("groups %d, vertices %d, materials %d, textures %d -> %s" % (len(used), nv, len(MATS), len(TEXLIST), MESH))
print("floors %d, solids in %d, out %d, caps %d, panels %d, buttons %d, screens %d, lights %d" % (len(FLOORS), len(SOL_IN), len(SOL_OUT), len(CAPS), len(PANELS), len(caps), len(screens), len(LIGHTS)))
# the signs' atlas texts, for make_base_textures.py (it draws them with Jura)
with open(os.path.join(os.path.dirname(__file__), "techpoint_signs.txt"), "w", encoding="utf-8") as f:
    for txt, w_, h_, _, _ in SIGNS: f.write("%s\t%.2f\t%.2f\n" % (txt, w_, h_))

# ======================= the radiosity's inputs (Tantra_Design/blender/gi_techpoint.py -> Meshes\TVehicles\TechPoint.gi) =======================
# The D3D11 client's real-time radiosity (Enlighten's scheme: precomputed light transport between surface patches, the light
# itself every frame): what the precompute needs besides the mesh - each material's diffuse albedo (the colour x the
# texture's mean, linear: the client samples the textures as sRGB; metals bounce their colour, rougher), which groups do
# not stop or reflect light (glass, the lamps' glowing faces, the screens) or move (the door leaves, the gate's: the
# portals, open or shut each frame), the lamps, the rooms' probe grids, the Moon's ground round the point
import json
def tex_mean_lin(rel):
    try:
        from PIL import Image
        import numpy as np
        a = np.asarray(Image.open(os.path.join(ROOT, "Textures", rel)).convert("RGB"), dtype=np.float64) / 255.0
        a = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
        return [float(x) for x in a.reshape(-1, 3).mean(0)]
    except Exception as e:
        print("  (albedo: %s unreadable - %s; 0.5)" % (rel, e)); return [0.5, 0.5, 0.5]
def metal_mean(rel):
    p = os.path.join(ROOT, "Textures", rel.replace(".dds", "_metl.dds"))
    if not os.path.exists(p): return 0.0
    try:
        from PIL import Image
        import numpy as np
        return float(np.asarray(Image.open(p).convert("RGB"), dtype=np.float64)[..., 0].mean() / 255.0)
    except Exception: return 0.0
alb = []
for name, col, sp, em, tex, al, pw in MATS:
    a = list(col)
    if tex: t = tex_mean_lin(tex[0]); a = [a[k] * t[k] for k in range(3)]; m = metal_mean(tex[0])
    else: m = 0.0
    a = [x * (1 - m) + x * m * 0.5 for x in a]                       # a rough metal sends back about half its colour, not a mirror's
    alb.append({"name": name, "albedo": [round(min(x, 0.95), 4) for x in a], "emit": em > 0, "glass": al < 0.5})
AREA_SKIP = ("LampHall", "LampRooms", "LampLock1", "LampLock2")     # (under the D3D11 client these rooms are lit by the area lamps)
gi_lamps = [{"type": "rect", "c": c, "ex": ex, "ey": ey, "lumen": lm, "circuit": r} for r, c, ex, ey, lm, rgb in AREA]
for circ, spot, pos, d, rng, umb, pen, col, inten in LIGHTS:
    if circ in AREA_SKIP: continue
    gi_lamps.append({"type": "spot" if spot else "point", "c": pos, "dir": d, "range": rng, "cosOut": math.cos(math.radians(pen) / 2) if spot else -1.0})
portals = []
for name, run, c0, c1, u0, u1, sgn in DOORS:                          # a door: its leaf's mid-plane, the opening's rectangle
    portals.append({"name": name, "axis": 2 if run == "x" else 0, "c": 0.5 * (c0 + c1), "u": [u0, u1] if run == "x" else [u0, u1], "v": [FY, FY + DH], "half": LEAF_T / 2, "albedo": alb[MI["Metal"]]["albedo"]})
portals.append({"name": "Gate", "axis": 2, "c": 0.5 * (GZ0 + GZ1), "u": [GX0, GX1], "v": [0.0, GH], "half": GL_T / 2, "albedo": alb[MI["Panel"]]["albedo"]})
grids = []
for b, e0, e1, bl, r in VOLUMES:
    cell = 1.0 if r == 6 else 0.5                                     # (the hangar: 1 m - its light varies slowly; 0.75 m cost 2x the data)
    lo, hi = [min(b[k], b[k + 3]) for k in range(3)], [max(b[k], b[k + 3]) for k in range(3)]
    dims = [max(1, int(round((hi[k] - lo[k]) / cell))) for k in range(3)]
    grids.append({"room": r, "lo": lo, "hi": hi, "dims": dims})
groups_gi = [{"label": g[0], "mat": g[1], "door": g[0].startswith("Door") or g[0].startswith("GateLeaf")} for g in used]
json.dump({"materials": alb, "groups": groups_gi, "lamps": gi_lamps, "portals": portals, "grids": grids,
           "windows": [list(w_) for w_ in wins],
           "hangar_floor": [HX0 + HT, HZ0 + HT, HX1 - HT, HZ1 - HT], "regolith": 0.12,
           "yard": [-36.0, -30.0, 40.0, 34.0],
           "footprints": [[-22.15, 3.85, -3.85, 12.15], [-12.15, 0.25, -7.85, 4.0], [-4.0, 6.45, -0.40, 10.75], [HX0, HZ0, HX1, HZ1]]},
          open(os.path.join(ROOT, "Tantra_Design", "base", "techpoint_gi.json"), "w"), indent=1)
print("radiosity inputs: %d lamps, %d portals, %d grids -> Tantra_Design/base/techpoint_gi.json" % (len(gi_lamps), len(portals), len(grids)))
