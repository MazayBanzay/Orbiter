# Э.МПУ crew cabins - three concepts on the МПУ chassis (a look only, nothing for the game).
# blender --background astronavigator_coverall.blend --python mock_empu_concepts.py
# A "Expedition": low wide cabin, wrap-round glazing, a separate airlock pod at the rear, a solar array on the roof, a rear rack.
# B "Cab + bed": a short cabin for four with suitports in its rear wall, an open cargo bed with a folding crane, solar wings.
# C "Long range": a full-length cabin - cockpit, living (bunks, galley, table), an airlock room at the rear; a deployable
#   solar fan on the roof.
# For each: a three-quarter view with her for scale, and the plan from above with the roof cut off at sill height.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector, Quaternion
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
src = open(os.path.join(HERE, "mock_mpu.py"), encoding="utf-8").read()
exec(src[src.index("import bpy"):src.index("try:\n    for o in bpy.data.objects")])
LOG = os.path.join(HERE, "mock_empu_concepts.log"); log = []
M_SOLAR, M_LINING, M_SEAT, M_CARGO, M_FLOOR = 9, 10, 11, 12, 13
CUT = 2.30                                                   # the plan's cut height (blender z)

def shell(bm, secs, cut=False):
    """the cabin's faceted shell; cut: walls only up to CUT, open on top (the plan view)"""
    if cut:
        rs = [oct_ring(y, hw, z0, min(z1, CUT), min(ch, (min(z1, CUT) - z0) / 2.5)) for y, hw, z0, z1, ch in secs]
        for A, B in zip(rs, rs[1:]):                                              # the walls only: no top facets
            Va = [bm.verts.new(O + Vector(p)) for p in A]; Vb = [bm.verts.new(O + Vector(p)) for p in B]
            for j in (0, 1, 2, 6, 7):
                j1 = (j + 1) % 8
                bm.faces.new((Va[j], Va[j1], Vb[j1], Vb[j])).material_index = M_WHITE
        box(bm, (-secs[0][1] + 0.3, secs[0][0] - 0.01, secs[0][2]), (secs[0][1] - 0.3, secs[0][0] + 0.01, CUT), M_WHITE)
        box(bm, (-secs[-1][1] + 0.3, secs[-1][0] - 0.01, secs[-1][2]), (secs[-1][1] - 0.3, secs[-1][0] + 0.01, CUT), M_WHITE)
        y0, y1 = secs[0][0], secs[-1][0]
        box(bm, (-1.65, y0 + 0.1, 1.42), (1.65, y1 - 0.1, 1.44), M_FLOOR)
    else:
        loft(bm, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in secs], M_WHITE)

def window(bm, sx, y0, y1, z0, z1, x):
    """a side window with volume: the pane set in, a frame round it standing out"""
    quad(bm, [(sx * (x - 0.03), y0, z0), (sx * (x - 0.03), y1, z0), (sx * (x - 0.03), y1, z1), (sx * (x - 0.03), y0, z1)][::sx], M_GLASS)
    f = 0.06
    for a, b in (((y0 - f, z0 - f), (y1 + f, z0)), ((y0 - f, z1), (y1 + f, z1 + f)), ((y0 - f, z0), (y0, z1)), ((y1, z0), (y1 + f, z1))):
        box(bm, (sx * x - 0.03, a[0], a[1]), (sx * x + 0.04, b[0], b[1]), M_HULL)

def screen(bm, pts):
    """the windscreen: glass on the sloped front with a frame"""
    quad(bm, pts, M_GLASS)
    for p, q in zip(pts, pts[1:] + pts[:1]): cyl(bm, p, q, 0.04, M_HULL, 8)

def seat(bm, x, y, face=-1):
    """a seat with its back to +y (face -1: forward)"""
    box(bm, (x - 0.27, y - 0.25, 1.42), (x + 0.27, y + 0.25, 1.97), M_SEAT)
    box(bm, (x - 0.27, y + 0.17, 1.97), (x + 0.27, y + 0.27, 2.75), M_SEAT)

def bunk(bm, x0, x1, y0, y1, z):
    box(bm, (x0, y0, z), (x1, y1, z + 0.12), M_LINING); box(bm, (x0, y0, z + 0.12), (x1, y1, z + 0.22), M_SEAT)

def locker(bm, x0, x1, y0, y1, z1=3.0):
    box(bm, (x0, y0, 1.42), (x1, y1, z1), M_LINING)
    for k in range(3): box(bm, (min(x0, x1) - 0.005, y0 + 0.02, 1.6 + k * 0.45), (max(x0, x1) + 0.005, y1 - 0.02, 1.62 + k * 0.45), M_SEAM)

def suit_stand(bm, x, y):
    box(bm, (x - 0.06, y - 0.06, 1.42), (x + 0.06, y + 0.06, 3.0), M_METAL)
    cyl(bm, (x, y, 2.25), (x, y, 2.95), 0.24, M_WHITE, 16); cyl(bm, (x, y, 2.95), (x, y, 3.15), 0.16, M_WHITE, 16)
    cyl(bm, (x - 0.1, y, 1.5), (x - 0.1, y, 2.25), 0.09, M_WHITE, 10); cyl(bm, (x + 0.1, y, 1.5), (x + 0.1, y, 2.25), 0.09, M_WHITE, 10)

def console(bm, y0, y1, hw):
    box(bm, (-hw, y0, 1.42), (hw, y1, 2.15), M_HULL)
    quad(bm, [(-hw + 0.1, y1, 2.15), (hw - 0.1, y1, 2.15), (hw - 0.1, y1 - 0.25, 2.35), (-hw + 0.1, y1 - 0.25, 2.35)][::-1], M_GLASS)

def solar(bm, x0, x1, y0, y1, z, tilt=0.0):
    box(bm, (x0, y0, z), (x1, y1, z + 0.04), M_SOLAR)
    n = int((y1 - y0) / 0.6)
    for k in range(1, n): box(bm, (x0, y0 + k * (y1 - y0) / n - 0.01, z + 0.04), (x1, y0 + k * (y1 - y0) / n + 0.01, z + 0.045), M_METAL)

# ---------------- the three cabins ----------------
def var_A(bm, cut):
    S = [(-3.95, 1.20, 1.45, 2.30, 0.30), (-3.65, 1.62, 1.42, 2.95, 0.45), (-3.15, 1.78, 1.40, 3.20, 0.55),
         (1.45, 1.78, 1.40, 3.20, 0.55), (1.70, 1.60, 1.42, 3.00, 0.50)]
    shell(bm, S, cut)
    if not cut:
        screen(bm, [(-1.0, -3.97, 2.05), (1.0, -3.97, 2.05), (1.15, -3.67, 2.85), (-1.15, -3.67, 2.85)][::-1])
        for sx in (-1, 1):
            window(bm, sx, -3.05, -2.05, 2.05, 2.62, 1.78); window(bm, sx, -1.6, -0.7, 2.05, 2.62, 1.78)
        solar(bm, -1.65, 1.65, -2.9, 1.3, 3.36)
        for x in (-1.3, 1.3):
            for y in (-2.6, 1.0): box(bm, (x - 0.04, y - 0.04, 3.15), (x + 0.04, y + 0.04, 3.36), M_METAL)
        cyl(bm, (0.9, 1.25, 3.40), (0.9, 1.25, 3.70), 0.03, M_METAL, 8); cyl(bm, (0.9, 1.25, 3.70), (0.9, 1.15, 3.95), 0.28, M_WHITE, 20)
        for sx in (-1, 1): box(bm, (sx * 1.79 - 0.01, -3.0, 1.95), (sx * 1.79 + 0.01, 1.4, 1.99), M_RED)
        for sx in (-0.6, 0.6): box(bm, (sx - 0.25, -3.98, 1.70), (sx + 0.25, -3.94, 1.80), M_LAMP)
    # the airlock pod at the rear: a short pressure cylinder across the tunnel, its outer hatch at the back
    cyl(bm, (0, 1.65, 2.35), (0, 3.25, 2.35), 0.92, M_WHITE, 40)
    if not cut:
        cyl(bm, (0, 3.25, 2.35), (0, 3.32, 2.35), 0.60, M_METAL, 8); cyl(bm, (0, 3.32, 2.35), (0, 3.36, 2.35), 0.10, M_RED, 12)
        for k in range(3): cyl(bm, (0, 1.9 + k * 0.6, 2.35), (0, 1.95 + k * 0.6, 2.35), 0.94, M_HULL, 40)
    else:
        box(bm, (-0.8, 1.7, 1.43), (0.8, 3.2, 1.45), M_FLOOR); suit_stand(bm, -0.5, 2.6); suit_stand(bm, 0.5, 2.6)
    # the rear rack with two containers
    box(bm, (-1.4, 3.4, 1.42), (1.4, 4.3, 1.46), M_METAL)
    for x in (-0.7, 0.7): box(bm, (x - 0.55, 3.5, 1.46), (x + 0.55, 4.2, 2.15), M_CARGO, 0.02)
    if cut:
        console(bm, -3.65, -3.35, 1.3); seat(bm, -0.65, -2.85); seat(bm, 0.65, -2.85)
        bunk(bm, -1.68, -1.08, -1.9, 0.2, 1.85); bunk(bm, 1.08, 1.68, -1.9, 0.2, 1.85)     # benches by day, bunks by night
        locker(bm, -1.68, -1.08, 0.3, 1.40); box(bm, (1.08, 0.3, 1.42), (1.68, 1.4, 2.35), M_METAL)   # lockers / galley
        box(bm, (-0.45, 1.40, 1.42), (0.45, 1.75, 2.9), M_HULL)                           # the inner hatch frame

def var_B(bm, cut):
    S = [(-4.25, 1.22, 1.45, 2.40, 0.30), (-3.95, 1.62, 1.42, 3.05, 0.45), (-3.45, 1.76, 1.40, 3.30, 0.55),
         (-0.75, 1.76, 1.40, 3.30, 0.55), (-0.45, 1.66, 1.42, 3.20, 0.50)]
    shell(bm, S, cut)
    if not cut:
        screen(bm, [(-1.0, -4.27, 2.15), (1.0, -4.27, 2.15), (1.15, -3.97, 2.95), (-1.15, -3.97, 2.95)][::-1])
        for sx in (-1, 1): window(bm, sx, -3.35, -2.25, 2.15, 2.75, 1.76)
        # solar wings folded flat on the roof, hinged at its sides (they open out to ~3x the roof)
        for sx in (-1, 1): solar(bm, sx * 0.05, sx * 1.55, -3.3, -0.85, 3.36 + (0.06 if sx > 0 else 0.0))
        for sx in (-1, 1): box(bm, (sx * 1.79 - 0.01, -3.3, 2.0), (sx * 1.79 + 0.01, -0.8, 2.04), M_RED)
        # suitports in the rear wall: two suits docked outside, backs into the cabin
        for x in (-0.6, 0.6):
            cyl(bm, (x, -0.40, 2.45), (x, -0.20, 2.45), 0.32, M_METAL, 20)
            cyl(bm, (x, -0.20, 2.20), (x, 0.15, 2.20), 0.25, M_WHITE, 16); cyl(bm, (x, -0.20, 2.80), (x, 0.05, 2.80), 0.17, M_WHITE, 16)
            for dx in (-0.1, 0.1): cyl(bm, (x + dx, 0.0, 2.0), (x + dx, 0.0, 1.5), 0.08, M_WHITE, 10)
    # the cargo bed: floor, stake sides, two containers, a folding crane
    box(bm, (-1.75, -0.3, 1.42), (1.75, 4.3, 1.47), M_METAL)
    for sx in (-1, 1):
        for y in (0.2, 1.3, 2.4, 3.5, 4.25): box(bm, (sx * 1.72 - 0.03, y - 0.03, 1.47), (sx * 1.72 + 0.03, y + 0.03, 2.05), M_HULL)
        box(bm, (sx * 1.72 - 0.03, 0.2, 1.95), (sx * 1.72 + 0.03, 4.25, 2.02), M_HULL)
    box(bm, (-1.4, 1.2, 1.47), (-0.1, 2.6, 2.4), M_CARGO, 0.02); box(bm, (0.1, 2.8, 1.47), (1.4, 4.1, 2.1), M_CARGO, 0.02)
    cyl(bm, (1.3, 0.3, 1.47), (1.3, 0.3, 2.2), 0.12, M_RED, 16); cyl(bm, (1.3, 0.3, 2.2), (1.3, 3.5, 2.35), 0.08, M_RED, 12)
    if cut:
        console(bm, -3.95, -3.65, 1.35); seat(bm, -0.65, -3.1); seat(bm, 0.65, -3.1); seat(bm, -0.65, -1.95); seat(bm, 0.65, -1.95)
        locker(bm, -1.70, -1.20, -1.40, -0.55); locker(bm, 1.20, 1.70, -1.40, -0.55)
        for x in (-0.6, 0.6): box(bm, (x - 0.33, -0.70, 1.9), (x + 0.33, -0.5, 2.9), M_METAL)        # the suitports from inside

def var_C(bm, cut):
    S = [(-4.30, 1.22, 1.45, 2.40, 0.30), (-4.00, 1.62, 1.42, 3.10, 0.45), (-3.50, 1.78, 1.40, 3.35, 0.55),
         (3.70, 1.78, 1.40, 3.35, 0.55), (4.15, 1.58, 1.42, 3.10, 0.45), (4.38, 1.25, 1.45, 2.65, 0.30)]
    shell(bm, S, cut)
    if not cut:
        screen(bm, [(-1.0, -4.32, 2.15), (1.0, -4.32, 2.15), (1.15, -4.02, 3.0), (-1.15, -4.02, 3.0)][::-1])
        for sx in (-1, 1):
            window(bm, sx, -3.40, -2.40, 2.15, 2.80, 1.78)
            for y in (-1.2, 0.6): window(bm, sx, y, y + 0.55, 2.35, 2.75, 1.78)
        # the solar fan deployed: the roof panel and two wings out to the sides, a little tilted
        solar(bm, -1.65, 1.65, -3.2, 3.4, 3.42)
        for sx in (-1, 1):
            b = bmesh.ops.create_cube(bm, size=1.0)
            for v in b['verts']:
                v.co = O + Vector((sx * (1.65 + 1.25 + v.co.x * 2.5), -3.2 + 3.3 + v.co.y * 6.6, 3.42 + 0.02 + v.co.z * 0.04 + sx * 0 + abs(v.co.x + 0.5 * sx) * 0.25))
            for f_ in {f_ for v in b['verts'] for f_ in v.link_faces}: f_.material_index = M_SOLAR
        for x in (-1.4, 1.4):
            for y in (-3.0, 0.1, 3.2): box(bm, (x - 0.04, y - 0.04, 3.30), (x + 0.04, y + 0.04, 3.42), M_METAL)
        cyl(bm, (0, 4.39, 2.25), (0, 4.43, 2.25), 0.55, M_METAL, 8)                    # the rear airlock door
        for sx in (-1, 1): box(bm, (sx * 1.79 - 0.01, -3.4, 2.05), (sx * 1.79 + 0.01, 3.6, 2.09), M_RED)
    if cut:
        console(bm, -4.00, -3.70, 1.35); seat(bm, -0.65, -3.15); seat(bm, 0.65, -3.15)
        bunk(bm, 1.05, 1.70, -2.3, -0.2, 1.55); bunk(bm, 1.05, 1.70, -2.3, -0.2, 2.35)     # two-tier bunks, right
        bunk(bm, 1.05, 1.70, 0.0, 2.1, 1.55); bunk(bm, 1.05, 1.70, 0.0, 2.1, 2.35)
        box(bm, (-1.70, -2.3, 1.42), (-1.10, -0.7, 1.95), M_SEAT); box(bm, (-1.70, 0.5, 1.42), (-1.10, 1.6, 1.95), M_SEAT)   # benches
        box(bm, (-1.65, -0.55, 2.05), (-0.75, 0.35, 2.12), M_LINING)                  # the table between them
        locker(bm, -1.70, -1.10, 1.75, 2.55)                                           # galley
        box(bm, (-1.75, 2.62, 1.42), (1.75, 2.72, 3.0), M_HULL); box(bm, (-0.45, 2.60, 1.42), (0.45, 2.74, 2.9), M_SEAM)   # airlock bulkhead, inner door
        suit_stand(bm, -1.2, 3.5); suit_stand(bm, 1.2, 3.5)

def tube(bm, c, r, z0, z1, mi, seg=48, a0=0.0, a1=2 * math.pi, cap=True):
    """a vertical cylinder (or its arc a0..a1) about c, from z0 to z1"""
    n = max(4, int(seg * (a1 - a0) / (2 * math.pi)))
    pts = [Vector((c[0] + r * math.cos(a0 + (a1 - a0) * k / n), c[1] + r * math.sin(a0 + (a1 - a0) * k / n), 0)) for k in range(n + 1)]
    for p, q in zip(pts, pts[1:]):
        quad(bm, [(p.x, p.y, z0), (q.x, q.y, z0), (q.x, q.y, z1), (p.x, p.y, z1)], mi)
    if cap and a1 - a0 > 6.2:
        vs = [bm.verts.new(O + Vector((p.x, p.y, z1))) for p in pts[:-1]]; bm.faces.new(vs).material_index = mi

def var_D(bm, cut):
    """A + C: C's long cabin (cockpit, bunks, table, galley) and, at the rear, a ROUND airlock standing upright - a flat
    floor at the deck, a door sliding round its wall, folding stairs to the ground; A's roof array, C's wings folded"""
    S = [(-4.30, 1.22, 1.45, 2.40, 0.30), (-4.00, 1.62, 1.42, 3.10, 0.45), (-3.50, 1.78, 1.40, 3.35, 0.55),
         (1.70, 1.78, 1.40, 3.35, 0.55), (2.05, 1.60, 1.42, 3.15, 0.45)]
    shell(bm, S, cut)
    AC = (0.0, 3.25)                                   # the airlock's axis (blender x, y); radius 1.05; the deck its floor
    top = CUT if cut else 3.45
    tube(bm, AC, 1.05, 1.42, top, M_WHITE, cap=not cut)
    tube(bm, (0.0, 2.10), 0.70, 1.42, min(top, 3.0), M_HULL, 24, math.pi * 0.15, math.pi * 0.85, cap=False)   # the short tunnel into the cabin
    if not cut:
        screen(bm, [(-1.0, -4.32, 2.15), (1.0, -4.32, 2.15), (1.15, -4.02, 3.0), (-1.15, -4.02, 3.0)][::-1])
        for sx in (-1, 1):
            window(bm, sx, -3.40, -2.40, 2.15, 2.80, 1.78)
            for y in (-1.3, 0.4): window(bm, sx, y, y + 0.55, 2.35, 2.75, 1.78)
            box(bm, (sx * 1.79 - 0.01, -3.4, 2.05), (sx * 1.79 + 0.01, 1.6, 2.09), M_RED)
        solar(bm, -1.65, 1.65, -3.3, 1.5, 3.42)                                      # A's array on stands
        for x in (-1.4, 1.4):
            for y in (-3.0, 1.2): box(bm, (x - 0.04, y - 0.04, 3.30), (x + 0.04, y + 0.04, 3.42), M_METAL)
        for sx in (-1, 1): solar(bm, sx * 0.1, sx * 1.6, -3.2, 1.4, 3.47 + (0.05 if sx > 0 else 0.0))   # C's wings, folded on it
        cyl(bm, (0.9, 1.6, 3.48), (0.9, 1.6, 3.75), 0.03, M_METAL, 8); cyl(bm, (0.9, 1.6, 3.75), (0.9, 1.5, 3.98), 0.26, M_WHITE, 20)
        for k in range(3): tube(bm, AC, 1.065, 1.80 + k * 0.6, 1.84 + k * 0.6, M_HULL, cap=False)   # the airlock's hoops
        tube(bm, AC, 1.06, 3.45, 3.50, M_HULL); tube(bm, AC, 0.6, 3.50, 3.58, M_WHITE)       # its roof ring and the dome cap
        # the outer door: a curved leaf on the wall facing aft, slid half open round the wall (the opening shows dark)
        tube(bm, AC, 1.052, 1.47, 3.30, M_SEAM, 48, math.pi * 0.40, math.pi * 0.60, cap=False)
        tube(bm, AC, 1.085, 1.45, 3.32, M_METAL, 48, math.pi * 0.52, math.pi * 0.74, cap=False)
        box(bm, (-0.05, 4.32, 2.30), (0.05, 4.36, 2.55), M_RED)
        # folding stairs from the door down to the ground (stowed under the deck while driving, as the side steps)
        for k in range(4):
            z = 1.10 - k * 0.27; yy = 4.35 + k * 0.28
            box(bm, (-0.45, yy, z), (0.45, yy + 0.30, z + 0.05), M_METAL)
        for sx in (-0.48, 0.48): cyl(bm, (sx, 4.30, 1.40), (sx, 5.40, 0.25), 0.03, M_METAL, 8)
        for sx in (-1, 1): box(bm, (sx * 1.15, 2.6, 1.42), (sx * 1.50, 4.0, 2.10), M_CARGO, 0.02)   # tool lockers by the airlock
    else:
        console(bm, -4.00, -3.70, 1.35); seat(bm, -0.65, -3.15); seat(bm, 0.65, -3.15)
        bunk(bm, 1.05, 1.70, -2.3, -0.2, 1.55); bunk(bm, 1.05, 1.70, -2.3, -0.2, 2.35)
        bunk(bm, 1.05, 1.70, -0.05, 1.65, 1.55); bunk(bm, 1.05, 1.70, -0.05, 1.65, 2.35)
        box(bm, (-1.70, -2.3, 1.42), (-1.10, -1.2, 1.95), M_SEAT); box(bm, (-1.70, -0.1, 1.42), (-1.10, 0.9, 1.95), M_SEAT)
        box(bm, (-1.65, -1.1, 2.05), (-0.75, -0.2, 2.12), M_LINING)
        locker(bm, -1.70, -1.10, 1.0, 1.95)
        box(bm, (-0.45, 1.98, 1.42), (0.45, 2.06, 2.9), M_SEAM)                        # the inner door
        box(bm, (-1.0, 2.25, 1.42), (1.0, 4.25, 1.44), M_FLOOR)
        suit_stand(bm, -0.55, 3.6); suit_stand(bm, 0.55, 3.6)
        tube(bm, AC, 1.052, 1.47, CUT, M_SEAM, 48, math.pi * 0.40, math.pi * 0.60, cap=False)

def beam(bm, p, q, r=0.075, mi=None):
    cyl(bm, p, q, r, M_WHITE if mi is None else mi, 6)

def inset_pane(bm, pts, depth=0.05, rim=0.10):
    """a pane set into a frame face: the face's corners pulled in by the rim and pushed in by the depth"""
    P = [Vector(p) for p in pts]
    c = sum(P, Vector()) / len(P)
    n = (P[1] - P[0]).cross(P[2] - P[0]).normalized()
    Q = []
    for p in P:
        d = c - p; L = d.length
        Q.append(p + d * min(rim / max(L, 1e-6), 0.45) - n * depth)
    vs = [bm.verts.new(O + q) for q in Q]
    bm.faces.new(vs).material_index = M_GLASS
    for a, b in zip(range(len(P)), list(range(1, len(P))) + [0]):                 # the reveal round the pane
        f = [bm.verts.new(O + P[a]), bm.verts.new(O + P[b]), bm.verts.new(O + Q[b]), bm.verts.new(O + Q[a])]
        bm.faces.new(f).material_index = M_HULL

def var_E(bm, cut):
    """the cab is a load-bearing frame: thick members along every edge of the faceted nose, small panes set between them
    (a pressure cabin: each pane carries its own share, ALON in three layers); behind it the habitat, then the airlock
    built into the same hull - a bulkhead, the outer door in the stern face, folding stairs under it"""
    R = [(-4.35, 1.00, 1.80, 2.55, 0.28), (-3.95, 1.58, 1.52, 3.05, 0.48), (-3.20, 1.80, 1.42, 3.36, 0.60),
         (-2.35, 1.82, 1.42, 3.40, 0.60), (2.55, 1.82, 1.42, 3.40, 0.60), (4.05, 1.78, 1.42, 3.36, 0.58),
         (4.38, 1.55, 1.45, 3.20, 0.50)]
    rings = [oct_ring(y, hw, z0, (min(z1, CUT) if cut else z1), (min(ch, (min(z1, CUT) - z0) / 2.5) if cut else ch)) for y, hw, z0, z1, ch in R]
    glass = {0: (1, 2, 3, 4, 5, 6, 7), 1: (2, 3, 4, 5, 6), 2: (2, 6)}              # segment -> faces that are panes
    for s_, (A, B) in enumerate(zip(rings, rings[1:])):
        for j in range(8):
            j1 = (j + 1) % 8
            if cut and j in (3, 4, 5): continue
            quadp = [A[j], A[j1], B[j1], B[j]]
            if not cut and s_ in glass and j in glass[s_]:
                inset_pane(bm, quadp)
            else:
                quad(bm, quadp, M_WHITE)
    if not cut:
        quad(bm, list(reversed(rings[0])), M_WHITE); quad(bm, list(rings[-1]), M_WHITE)
        # the frame: every ring of the cab, and the longitudinal members between them; lighter hoops along the habitat
        for k in range(4):
            Rg = rings[k]
            for a, b in zip(Rg, Rg[1:] + Rg[:1]): beam(bm, a, b, 0.085)
        for k in range(3):
            for j in range(8): beam(bm, rings[k][j], rings[k + 1][j], 0.085)
        for yy in (-0.6, 1.2):
            hr = oct_ring(yy, 1.84, 1.40, 3.42, 0.60)
            for a, b in zip(hr, hr[1:] + hr[:1]): beam(bm, a, b, 0.05, M_HULL)
        for k in (4, 5, 6):
            for a, b in zip(rings[k], rings[k][1:] + rings[k][:1]): beam(bm, a, b, 0.06)
        # the airlock in the stern: the outer door (opens inwards - the pressure seats it), its frame, the handle, a port
        y = 4.385
        box(bm, (-0.48, y, 1.48), (0.48, y + 0.03, 3.25), M_METAL)
        for a, b in (((-0.56, 1.44), (0.56, 1.50)), ((-0.56, 3.25), (0.56, 3.32)), ((-0.56, 1.44), (-0.48, 3.32)), ((0.48, 1.44), (0.56, 3.32))):
            box(bm, (a[0], y, a[1]), (b[0], y + 0.06, b[1]), M_HULL)
        cyl(bm, (0, y + 0.03, 2.75), (0, y + 0.05, 2.75), 0.13, M_GLASS, 16); box(bm, (0.30, y + 0.03, 2.20), (0.36, y + 0.10, 2.45), M_RED)
        for k in range(4):                                                         # the stairs, out (parked)
            z = 1.10 - k * 0.27; yy = 4.45 + k * 0.28
            box(bm, (-0.45, yy, z), (0.45, yy + 0.30, z + 0.05), M_METAL)
        for sx in (-0.48, 0.48): cyl(bm, (sx, 4.40, 1.42), (sx, 5.50, 0.25), 0.03, M_METAL, 8)
        # the light bar and the mast over the cab; equipment on the habitat's flanks; the roof array with folded wings
        box(bm, (-1.0, -3.40, 3.40), (1.0, -3.28, 3.50), M_HULL)
        for x in (-0.8, -0.4, 0.0, 0.4, 0.8): box(bm, (x - 0.14, -3.41, 3.42), (x + 0.14, -3.40, 3.48), M_LAMP)
        for sx in (-1.15, 1.15): box(bm, (sx - 0.12, -2.8, 3.40), (sx + 0.12, -2.6, 3.58), M_LAMP)
        cyl(bm, (0.9, -2.0, 3.40), (0.9, -2.0, 3.75), 0.03, M_METAL, 8); cyl(bm, (0.9, -2.0, 3.75), (0.9, -2.12, 4.0), 0.25, M_WHITE, 20)
        solar(bm, -1.55, 1.55, -1.6, 3.9, 3.47)
        for sx in (-1, 1): solar(bm, sx * 0.1, sx * 1.55, -1.5, 3.8, 3.52 + (0.05 if sx > 0 else 0.0))
        for x in (-1.3, 1.3):
            for yy in (-1.4, 3.7): box(bm, (x - 0.04, yy - 0.04, 3.40), (x + 0.04, yy + 0.04, 3.47), M_METAL)
        for sx in (-1, 1):
            for y0 in (-1.9, -0.2, 1.5):                                           # service panels with seams on the habitat
                box(bm, (sx * 1.83 - 0.01, y0, 1.95), (sx * 1.83 + 0.01, y0 + 1.2, 1.97), M_SEAM)
                box(bm, (sx * 1.83 - 0.01, y0, 2.85), (sx * 1.83 + 0.01, y0 + 1.2, 2.87), M_SEAM)
            box(bm, (sx * 1.83 - 0.01, -2.3, 2.30), (sx * 1.83 + 0.01, 2.5, 2.34), M_RED)
        for sx in (-0.6, 0.6): box(bm, (sx - 0.22, -4.36, 1.86), (sx + 0.22, -4.34, 1.96), M_LAMP)
    else:
        box(bm, (-1.70, -4.0, 1.42), (1.70, 4.3, 1.44), M_FLOOR)
        console(bm, -4.05, -3.70, 1.25); seat(bm, -0.65, -3.15); seat(bm, 0.65, -3.15)
        bunk(bm, 1.05, 1.75, -2.2, -0.1, 1.55); bunk(bm, 1.05, 1.75, -2.2, -0.1, 2.35)
        bunk(bm, 1.05, 1.75, 0.05, 2.15, 1.55); bunk(bm, 1.05, 1.75, 0.05, 2.15, 2.35)
        box(bm, (-1.75, -2.2, 1.42), (-1.15, -1.1, 1.95), M_SEAT); box(bm, (-1.75, 0.0, 1.42), (-1.15, 1.0, 1.95), M_SEAT)
        box(bm, (-1.70, -1.0, 2.05), (-0.80, -0.1, 2.12), M_LINING)
        locker(bm, -1.75, -1.15, 1.1, 2.45)
        box(bm, (-1.80, 2.55, 1.42), (1.80, 2.65, CUT), M_HULL); box(bm, (-0.45, 2.53, 1.42), (0.45, 2.67, CUT), M_SEAM)   # bulkhead, inner door
        suit_stand(bm, -1.25, 3.6); suit_stand(bm, 1.25, 3.6)
        box(bm, (-0.48, 4.30, 1.44), (0.48, 4.36, CUT), M_SEAM)

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    MATS = [mat("PHull", (0.11, 0.12, 0.135), 0.6, 0.3), mat("PWhite", (0.85, 0.86, 0.87), 0.45), mat("PMetal", (0.32, 0.33, 0.35), 0.3, 0.8),
            mat("PGlass", (0.08, 0.13, 0.18), 0.05, 0.5), mat("PTread", (0.05, 0.05, 0.055), 0.85), mat("PRed", (0.72, 0.12, 0.10), 0.4),
            mat("PLamp", (1.0, 0.96, 0.85), 0.15), mat("PSeam", (0.04, 0.04, 0.045), 0.7), mat("PBlade", (0.20, 0.22, 0.26), 0.35, 0.6),
            mat("PSolar", (0.05, 0.08, 0.22), 0.15, 0.6), mat("PLining", (0.78, 0.74, 0.66), 0.7), mat("PSeat", (0.18, 0.30, 0.55), 0.8),
            mat("PCargo", (0.80, 0.45, 0.12), 0.6), mat("PFloor", (0.10, 0.10, 0.11), 0.9)]
    sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (10, 10, 1)
    sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
    so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 400
    O = Vector((3.4, 6.9, 0.0))
    for name, fn in (("E", var_E),):
        b = bmesh.new(); chassis(b); fn(b, False); ext = obj("EMPU_" + name, b, MATS)
        cam.data.type = 'PERSP'; cam.data.lens = 33; cam.rotation_mode = 'QUATERNION'
        loc = Vector((-9.0, -6.0, 5.0)); tgt = Vector((O.x, O.y - 0.5, 1.6))
        cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "empu_%s_34.png" % name); bpy.ops.render.render(write_still=True)
        loc = Vector((O.x + 9.0, O.y + 10.0, 4.5)); tgt = Vector((O.x, O.y + 0.5, 1.6))
        cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "empu_%s_rear.png" % name); bpy.ops.render.render(write_still=True)
        bpy.data.objects.remove(ext, do_unlink=True)
        b = bmesh.new(); chassis(b); fn(b, True); cut = obj("EMPU_%s_cut" % name, b, MATS)
        cam.data.type = 'PERSP'; cam.data.lens = 30
        loc = Vector((O.x - 5.5, O.y + 1.0, 12.5)); tgt = Vector((O.x, O.y - 0.3, 1.6))
        cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "empu_%s_plan.png" % name); bpy.ops.render.render(write_still=True)
        bpy.data.objects.remove(cut, do_unlink=True)
        log.append("rendered " + name)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
