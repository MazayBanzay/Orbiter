# Mock-up of the cargo exoskeleton «Атлант» (Orbitersdk/OrbiterCrew/EXO_CARGO.md) round the astronavigator in her suit -
# looks only, nothing for the game. 1 t at 1 g, MR hydraulics with locks, carbon nanotube frame, an MPU energy cell.
# blender --background astronavigator_suit.blend --python mock_exo.py      (EXO_VARIANT = A | B | C | D | E)
#   A «Рохля»:   short fork guides on the pelvis frame, the forks lie right over the long toes (the pallet truck's way)
#   B «Мачта»:   a forklift mast before her, the carriage and the forks on it, lighter arms
#   C «Горилла»: no forks - big arms with three-fingered claws hold the load at the chest, short toes, the cell low
#                behind as the counterweight
#   D «Журавль» (the user's direction, 2026-10-16): no toes - massive reverse-knee legs with wide feet, the operator
#                in a roll cage on the hips, two cells behind, long arms; the machine is bigger (3.4 m)
# All: foot plates under her boots, legs outside hers (ankle, knee, hip), the pelvis hoop, two spine rails and a yoke
# behind her, the overhead guard, the hydraulic cylinders along the joints, a 1 t crate (1.0 x 0.7 x 0.7 m).
# Blender: x across (+x her left), y along (front -y), z up. -> renders\exo_<V>_*.png
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
V = os.environ.get("EXO_VARIANT", "A").upper(); LOG = os.path.join(HERE, "mock_exo.log"); log = []
LIFT = 0.10                                                   # she stands on the foot plates: raised by their thickness

def mat(name, col, rough=0.45, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    return m

M = {}
def mats():
    M["frame"] = mat("ExoFrame", (0.07, 0.075, 0.085), 0.35, 0.2)      # CNT composite, near black, satin
    M["warn"] = mat("ExoWarn", (0.88, 0.62, 0.06), 0.45)
    M["chrome"] = mat("ExoChrome", (0.80, 0.82, 0.85), 0.12, 1.0)
    M["steel"] = mat("ExoSteel", (0.42, 0.43, 0.45), 0.35, 0.9)
    M["dark"] = mat("ExoDark", (0.025, 0.025, 0.03), 0.6)
    M["cell"] = mat("ExoCell", (0.60, 0.63, 0.66), 0.4, 0.5)
    M["crate"] = mat("Crate", (0.55, 0.36, 0.18), 0.7)
    M["red"] = mat("ExoRed", (0.70, 0.08, 0.06), 0.4)
    M["lamp"] = mat("ExoLamp", (0.2, 0.9, 0.4), 0.3)

parts = {}                                                    # material key -> bmesh
def bm_of(k):
    if k not in parts: parts[k] = bmesh.new()
    return parts[k]

def cyl(k, a, b, r, seg=16, cap=True):
    a, b = Vector(a), Vector(b); bm = bm_of(k); ax = (b - a)
    if ax.length < 1e-6: return
    ax.normalize(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    ra = [bm.verts.new(a + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r) for j in range(seg)]
    rb = [bm.verts.new(b + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r) for j in range(seg)]
    for j in range(seg): bm.faces.new((ra[j], ra[(j + 1) % seg], rb[(j + 1) % seg], rb[j]))
    if cap: bm.faces.new(ra[::-1]); bm.faces.new(rb)

def box(k, lo, hi):
    bm = bm_of(k); (x0, y0, z0), (x1, y1, z1) = lo, hi
    v = [bm.verts.new(p) for p in ((x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
    for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)): bm.faces.new([v[i] for i in f])

def sphere(k, c, r):
    bmesh.ops.create_uvsphere(bm_of(k), u_segments=16, v_segments=10, radius=r, matrix=__import__("mathutils").Matrix.Translation(Vector(c)))

def ram(a, b, r=0.022):
    """a hydraulic cylinder from a to b: the barrel (frame colour) over 55 %, the chrome rod the rest, the eyes at both ends"""
    a, b = Vector(a), Vector(b); m = a + (b - a) * 0.55
    cyl("frame", a, m, r * 1.45); cyl("chrome", m, b, r * 0.7); sphere("steel", a, r * 1.6); sphere("steel", b, r * 1.6)

def joint(c, ax, r=0.055, w=0.05):
    c, ax = Vector(c), Vector(ax).normalized(); cyl("frame", c - ax * w, c + ax * w, r, 20); cyl("warn", c - ax * (w + 0.004), c - ax * (w - 0.006), r * 0.75, 20)

def build():
    S = 1                                                     # both sides: s = +1 her left (+x), -1 her right
    toe = 0.85 if V != "C" else 0.50
    for s in (1, -1):
        fx = s * 0.175                                         # under her boot
        box("frame", (fx - 0.15, -0.26, 0.0), (fx + 0.15, 0.17, LIFT))                       # the foot plate
        box("dark", (fx - 0.15, -0.26, -0.003), (fx + 0.15, 0.17, 0.004))                    # the sole
        tx = s * (0.30 if V != "C" else 0.36)
        box("frame", (tx - 0.05, -0.26 - toe, 0.0), (tx + 0.05, -0.20, 0.075))               # the toe (outrigger)
        box("warn", (tx - 0.051, -0.26 - toe, 0.075), (tx + 0.051, -0.26 - toe + 0.12, 0.078))
        cyl("steel", (tx - 0.04, -0.20 - toe, 0.035), (tx + 0.04, -0.20 - toe, 0.035), 0.035)   # the toe's roller
        if V == "C": box("frame", (fx - 0.17, 0.17, 0.0), (fx + 0.17, 0.42, 0.075))           # heel extension
        A, K, H = Vector((s * 0.33, -0.02, 0.20)), Vector((s * 0.35, -0.10, 0.64)), Vector((s * 0.35, 0.02, 1.08))
        cyl("frame", (fx + s * 0.13, -0.02, LIFT), A, 0.03)                                   # the stirrup to the ankle
        joint(A, (1, 0, 0)); joint(K, (1, 0, 0), 0.06); joint(H, (1, 0, 0), 0.075, 0.06)
        cyl("frame", A, K, 0.038); cyl("frame", K, H, 0.045)                                  # the shin, the thigh (CNT tubes)
        ram(A + Vector((0, 0.07, 0.06)), K + Vector((0, 0.09, -0.08)))                        # the knee's ram behind the shin
        ram(H + Vector((0, -0.10, -0.02)), K + Vector((0, -0.08, 0.10)), 0.026)               # the hip's ram before the thigh
        cyl("dark", K + Vector((-s * 0.06, -0.06, 0.03)), K + Vector((-s * 0.06, -0.06, 0.12)), 0.03)   # the knee pad (on her)
        # the cuffs round her thigh and shin
        for c, r in ((K + Vector((-s * 0.13, 0.02, 0.18)), 0.085), (A + Vector((-s * 0.14, 0.0, 0.17)), 0.07)):
            cyl("frame", c - Vector((0, 0, 0.025)), c + Vector((0, 0, 0.025)), r, 20, cap=False)
    # the pelvis hoop, the spine rails, the yoke, the overhead guard
    hoop = [Vector((0.36 * math.cos(a), 0.05 + 0.27 * math.sin(a), 1.08)) for a in [2 * math.pi * k / 24 for k in range(25)]]
    for p, q in zip(hoop, hoop[1:]): cyl("frame", p, q, 0.032, 10)
    for x in (0.12, -0.12):
        cyl("frame", (x, 0.31, 1.06), (x, 0.31, 1.78), 0.035)
    yoke = [Vector((0.42 * math.cos(a), 0.04 + 0.27 * math.sin(a), 1.76)) for a in [math.pi * (0.0 + k / 12) for k in range(13)]]
    for p, q in zip(yoke, yoke[1:]): cyl("frame", p, q, 0.03, 10)
    for x in (0.42, -0.42): cyl("frame", (x, 0.04, 1.76), (x, 0.0, 1.62), 0.03)
    arch = [Vector((0.40 * math.cos(a), 0.15, 1.78 + 0.55 * math.sin(a))) for a in [math.pi * k / 16 for k in range(17)]]
    for p, q in zip(arch, arch[1:]): cyl("warn" if abs(p.x) < 0.12 else "frame", p, q, 0.028, 10)
    cyl("frame", (0, 0.15, 2.33), (0, -0.25, 2.30), 0.022); cyl("frame", (0, 0.15, 2.33), (0, 0.31, 1.78), 0.022)   # the guard's spar
    sphere("lamp", (0.0, -0.26, 2.30), 0.03)
    # the back module: the MPU energy cell (its face to the back), the pump and the pressure accumulator, the computer
    cz0 = 0.80 if V != "C" else 0.55; cy = 0.40 if V != "C" else 0.48
    box("cell", (-0.19, cy, cz0), (0.19, cy + 0.27, cz0 + 0.37)); box("warn", (-0.19, cy + 0.27, cz0 + 0.30), (0.19, cy + 0.272, cz0 + 0.34))
    box("frame", (-0.22, cy - 0.04, cz0 - 0.03), (0.22, cy + 0.30, cz0))                      # its cradle
    cyl("steel", (0.25, 0.36, cz0 + 0.05), (0.25, 0.36, cz0 + 0.42), 0.05)                     # the accumulator
    box("frame", (-0.32, 0.30, cz0 + 0.02), (-0.20, 0.46, cz0 + 0.26))                        # the pump
    box("frame", (-0.15, 0.36, 1.45), (0.15, 0.46, 1.70)); box("lamp", (-0.10, 0.461, 1.62), (-0.06, 0.463, 1.65))   # the computer
    # the arms beside hers: shoulder, elbow, wrist, a three-fingered claw
    big = V == "C"
    for s in (1, -1):
        Sh = Vector((s * 0.46, 0.03, 1.62)); El = Vector((s * 0.50, -0.16, 1.28)); Wr = Vector((s * (0.44 if big else 0.46), -0.50, 1.12))
        joint(Sh, (1, 0, 0), 0.075 if big else 0.06); joint(El, (1, 0, 0), 0.06 if big else 0.05)
        cyl("frame", Sh, El, 0.05 if big else 0.04); cyl("frame", El, Wr, 0.045 if big else 0.035)
        ram(Sh + Vector((0, 0.10, 0.06)), El + Vector((0, 0.09, 0.02)), 0.024 if big else 0.018)
        cyl("frame", Sh, Sh + Vector((-s * 0.04, 0.0, 0.14)), 0.03)                          # to the yoke
        palm = Wr + Vector((0, -0.10, -0.02)); cyl("steel", Wr, palm, 0.05 if big else 0.04)
        for k, (dx, dz) in enumerate(((-0.04, 0.04), (0.04, 0.04), (0.0, -0.05))):            # the claw's fingers
            f0 = palm + Vector((s * dx, 0, dz)); f1 = f0 + Vector((s * dx * 0.5, -0.14 if big else 0.10, dz * 0.4))
            f2 = f1 + Vector((0, -0.05, -dz * 0.9)); cyl("steel", f0, f1, 0.018 if big else 0.014); cyl("steel", f1, f2, 0.016 if big else 0.012)
    # the forks
    if V == "A":                                               # short guides on the hoop's front, the forks over the toes
        for x in (0.22, -0.22):
            cyl("frame", (x, -0.30, 0.16), (x, -0.30, 1.20), 0.028); cyl("chrome", (x, -0.30, 0.16), (x, -0.30, 1.20), 0.012)
        box("steel", (-0.30, -0.34, 0.14), (0.30, -0.30, 0.34))                                # the carriage
        for x in (0.22, -0.22): box("steel", (x - 0.05, -1.08, 0.12), (x + 0.05, -0.30, 0.16))
        ram((0.0, -0.27, 0.40), (0.0, -0.27, 1.06), 0.02)
        crate = ((-0.50, -1.08, 0.16), (0.50, -0.38, 0.86))
    elif V == "B":                                             # the mast before her, the carriage on it
        for x in (0.26, -0.26):
            box("frame", (x - 0.03, -0.62, 0.02), (x + 0.03, -0.56, 2.05)); box("chrome", (x - 0.012, -0.625, 0.02), (x + 0.012, -0.62, 2.05))
        for z in (0.02, 1.0, 2.0): box("frame", (-0.29, -0.62, z), (0.29, -0.56, z + 0.05))
        for x in (0.26, -0.26): cyl("frame", (x, -0.56, 1.0), (x * 1.3, -0.05, 1.08), 0.025)   # the mast's stays to the hoop
        box("steel", (-0.32, -0.68, 0.30), (0.32, -0.63, 0.62))                                # the carriage
        for x in (0.20, -0.20): box("steel", (x - 0.05, -1.45, 0.30), (x + 0.05, -0.63, 0.35))
        ram((0.0, -0.58, 0.10), (0.0, -0.58, 1.70), 0.025)
        crate = ((-0.50, -1.42, 0.35), (0.50, -0.72, 1.05))
    else:                                                      # held at the chest between the claws
        crate = ((-0.36, -0.95, 0.88), (0.36, -0.45, 1.30))
    box("crate", *crate)
    lo, hi = Vector(crate[0]), Vector(crate[1])                 # its battens
    for t in (0.2, 0.8):
        z = lo.z + (hi.z - lo.z) * t; box("dark", (lo.x - 0.005, lo.y - 0.005, z - 0.02), (hi.x + 0.005, hi.y + 0.005, z + 0.02))
    box("warn", (lo.x + 0.1, lo.y - 0.006, hi.z - 0.12), (lo.x + 0.4, lo.y - 0.004, hi.z - 0.05))   # «1000 кг» plate

def build_d():
    """D «Журавль» (the user, 2026-10-16: no toes; reverse-knee legs, massive, a big contact patch; bigger): she stands on
    the cockpit floor at the machine's hips inside a roll cage; the legs below are the machine's own (pedals and her
    weight shift steer them); the knees point back, so the feet can be set forward under the load; wide feet with three
    toes and a heel; two MPU cells behind as the counterweight; long arms with big claws hold the load before her chest"""
    for s in (1, -1):
        H, K, A = Vector((s * 0.64, 0.05, 1.10)), Vector((s * 0.70, 0.50, 0.72)), Vector((s * 0.64, -0.36, 0.32))   # the ankle forward, under the load
        cyl("frame", H - Vector((s * 0.14, 0, 0)), H + Vector((s * 0.12, 0, 0)), 0.21, 28)            # the hip's housing
        cyl("warn", H + Vector((s * 0.12, 0, 0)), H + Vector((s * 0.135, 0, 0)), 0.15, 24)
        joint(K, (1, 0, 0), 0.14, 0.11); joint(A, (1, 0, 0), 0.12, 0.10)
        for dx in (-0.075, 0.075):                                                                    # the thigh, the shin: twin box beams
            cyl("frame", H + Vector((dx, 0, 0)), K + Vector((dx, 0, 0)), 0.085, 8); cyl("frame", K + Vector((dx, 0, 0)), A + Vector((dx, 0, 0)), 0.075, 8)
        for t in (0.35, 0.7):                                                                         # the webs between them
            c = H + (K - H) * t; box("frame", (c.x - 0.08, c.y - 0.06, c.z - 0.06), (c.x + 0.08, c.y + 0.06, c.z + 0.06))
            c = K + (A - K) * t; box("frame", (c.x - 0.075, c.y - 0.05, c.z - 0.05), (c.x + 0.075, c.y + 0.05, c.z + 0.05))
        box("warn", (A.x + s * 0.11 - 0.005, A.y + 0.15, A.z + 0.10), (A.x + s * 0.11 + 0.005, A.y + 0.30, A.z + 0.16))
        ram(H + Vector((0, -0.20, -0.10)), K + Vector((0, -0.17, 0.14)), 0.055)                         # the hip's ram
        ram(K + Vector((0, 0.19, -0.04)), A + Vector((0, 0.22, 0.20)), 0.055)                           # the knee's ram (behind)
        ram(A + Vector((0, -0.16, 0.26)), A + Vector((0, -0.34, -0.20)), 0.04)                         # the ankle's ram
        fx = A.x
        box("frame", (fx - 0.26, -0.80, 0.04), (fx + 0.26, 0.06, 0.20))                              # the foot's body, 0.86 m long
        box("dark", (fx - 0.28, -0.82, 0.0), (fx + 0.28, 0.08, 0.04))                                # its sole (0.50 m2, 0.62 with the toes)
        box("warn", (fx - 0.281, -0.82, 0.16), (fx + 0.281, -0.70, 0.20))
        for k, (dx, ang) in enumerate(((-0.17, -0.35), (0.0, 0.0), (0.17, 0.35))):                   # three splayed toes
            a = Vector((fx + dx, -0.80, 0.07)); b = a + Vector((math.sin(ang) * 0.26, -math.cos(ang) * 0.26, -0.02))
            cyl("frame", a, b, 0.06, 8); box("dark", (b.x - 0.08, b.y - 0.08, 0.0), (b.x + 0.08, b.y + 0.08, 0.035))
            box("warn", (b.x - 0.05, b.y - 0.072, 0.03), (b.x + 0.05, b.y - 0.06, 0.06))
        cyl("frame", (fx, 0.06, 0.09), (fx, 0.36, 0.05), 0.06, 8); box("dark", (fx - 0.12, 0.30, 0.0), (fx + 0.12, 0.46, 0.035))   # the heel
    # the hips: the cockpit floor, the hydraulic pack under it, the cells behind
    box("frame", (-0.50, -0.32, 1.12), (0.50, 0.40, 1.25)); box("warn", (-0.50, -0.33, 1.21), (0.50, -0.32, 1.25))
    box("frame", (-0.44, -0.20, 0.82), (0.44, 0.34, 1.12)); cyl("steel", (-0.30, -0.05, 0.86), (0.30, -0.05, 0.86), 0.07)   # the pump, the accumulator
    for x in (-0.21, 0.21):
        box("cell", (x - 0.19, 0.42, 1.25), (x + 0.19, 0.69, 1.62)); box("warn", (x - 0.19, 0.69, 1.55), (x + 0.19, 0.692, 1.59))
    box("frame", (-0.44, 0.40, 1.22), (0.44, 0.72, 1.25))
    # the roll cage round her: two posts behind, two before, the hoop over her head, the spars
    for x in (0.40, -0.40):
        cyl("frame", (x, 0.40, 1.25), (x, 0.40, 3.10), 0.045); cyl("frame", (x, -0.32, 1.25), (x, -0.30, 2.30), 0.035)
        cyl("frame", (x, -0.30, 2.30), (x, 0.40, 3.10), 0.035)
    top = [Vector((0.40 * math.cos(a), 0.40, 3.10 + 0.25 * math.sin(a))) for a in [math.pi * k / 12 for k in range(13)]]
    for p, q in zip(top, top[1:]): cyl("warn" if abs(p.x) < 0.12 else "frame", p, q, 0.04, 12)
    cyl("frame", (-0.40, 0.40, 2.40), (0.40, 0.40, 2.40), 0.035); cyl("frame", (-0.40, 0.40, 1.80), (0.40, 0.40, 1.80), 0.035)
    sphere("lamp", (0.0, 0.40, 3.37), 0.04)
    box("frame", (-0.18, 0.36, 2.55), (0.18, 0.44, 2.85)); box("lamp", (-0.12, 0.445, 2.78), (-0.08, 0.447, 2.81))   # the computer
    # the arms: shoulders on the cage, long, three-fingered claws
    for s in (1, -1):
        Sh = Vector((s * 0.58, 0.20, 2.45)); El = Vector((s * 0.70, -0.22, 2.02)); Wr = Vector((s * 0.58, -0.58, 1.62))
        cyl("frame", (s * 0.40, 0.40, 2.40), Sh, 0.05); joint(Sh, (1, 0, 0), 0.10, 0.07); joint(El, (1, 0, 0), 0.08, 0.06)
        for dz in (-0.03, 0.03): cyl("frame", Sh + Vector((0, 0, dz)), El + Vector((0, 0, dz)), 0.045)
        cyl("frame", El, Wr, 0.055)
        ram(Sh + Vector((0, 0.14, 0.10)), El + Vector((0, 0.12, 0.05)), 0.032); ram(El + Vector((0, 0.10, -0.08)), Wr + Vector((0, 0.15, 0.10)), 0.026)
        palm = Wr + Vector((0, -0.12, -0.02)); cyl("steel", Wr, palm, 0.06)
        for dx, dz in ((-0.05, 0.05), (0.05, 0.05), (0.0, -0.06)):
            f0 = palm + Vector((s * dx, 0, dz)); f1 = f0 + Vector((s * dx * 0.6, -0.16, dz * 0.4)); f2 = f1 + Vector((-s * 0.06, -0.06, -dz))
            cyl("steel", f0, f1, 0.022); cyl("steel", f1, f2, 0.019)
    crate = ((-0.50, -1.06, 1.30), (0.50, -0.36, 1.85))
    box("crate", *crate)
    lo, hi = Vector(crate[0]), Vector(crate[1])
    for t in (0.2, 0.8):
        z = lo.z + (hi.z - lo.z) * t; box("dark", (lo.x - 0.005, lo.y - 0.005, z - 0.02), (hi.x + 0.005, hi.y + 0.005, z + 0.02))
    box("warn", (lo.x + 0.1, lo.y - 0.006, hi.z - 0.12), (lo.x + 0.4, lo.y - 0.004, hi.z - 0.05))

def octa(c, e1, e2, w, d, ch):
    """an octagon w x d (e1 across, e2 deep) with its corners cut by ch of the smaller side"""
    k = ch * min(w, d); hw, hd = w / 2, d / 2
    pts = [(-hw + k, -hd), (hw - k, -hd), (hw, -hd + k), (hw, hd - k), (hw - k, hd), (-hw + k, hd), (-hw, hd - k), (-hw, -hd + k)]
    return [c + e1 * x + e2 * y for x, y in pts]

def obox(k, a, b, w, d, up=(1, 0, 0), w1=None, d1=None, ch=0.28, mid=None):
    """a chamfered, tapering beam from a to b: w x d at a, w1 x d1 at b (and an optional bulge `mid` = (t, scale)) -
    the shells of the legs, the arms and the manipulator; no box corners"""
    a, b = Vector(a), Vector(b); ax = (b - a); ax.normalize()
    e1 = Vector(up).normalized(); e1 = (e1 - ax * e1.dot(ax)).normalized(); e2 = ax.cross(e1)
    w1 = w if w1 is None else w1; d1 = d if d1 is None else d1
    secs = [(0.0, w, d)]
    if mid: t, sc = mid; secs.append((t, (w + (w1 - w) * t) * sc, (d + (d1 - d) * t) * sc))
    secs.append((1.0, w1, d1))
    bm = bm_of(k); rings = [[bm.verts.new(p) for p in octa(a + (b - a) * t, e1, e2, ww, dd, ch)] for t, ww, dd in secs]
    for A, B in zip(rings, rings[1:]):
        for j in range(8): bm.faces.new((A[j], A[(j + 1) % 8], B[(j + 1) % 8], B[j]))
    bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])

def cbox(k, lo, hi, ch=0.25, up=(1, 0, 0)):
    """a chamfered box: an octagonal prism along z"""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    obox(k, ((x0 + x1) / 2, (y0 + y1) / 2, z0), ((x0 + x1) / 2, (y0 + y1) / 2, z1), x1 - x0, y1 - y0, up, ch=ch)

def foot(s, A):
    """the articulated foot under the ankle A (the user, 2026-10-16: not a board - its joints and its heel): the ankle's
    fork round the axle, the mid-foot with tread pads, the heel behind with a raking spur and its own hinge, two toes of
    two phalanges each on pins, a small ram lifting the toes, rubber pads under every part"""
    x = A.x
    cyl("steel", A + Vector((-0.20, 0, 0)), A + Vector((0.20, 0, 0)), 0.05, 20)                     # the ankle's axle
    for dx in (-0.17, 0.17):                                                                         # its fork: two cheeks down to the mid-foot
        obox("frame", A + Vector((dx, 0, 0.02)), Vector((x + dx, A.y - 0.04, 0.12)), 0.05, 0.20, (0, 1, 0), 0.05, 0.30, ch=0.35)
    M0, M1 = Vector((x, 0.08, 0.10)), Vector((x, -0.36, 0.09))                                       # the mid-foot, rear to front
    obox("frame", M0, M1, 0.40, 0.13, (1, 0, 0), 0.44, 0.11, ch=0.4)
    obox("warn", M1 + Vector((0, 0.035, 0.055)), M1 + Vector((0, 0.0, 0.055)), 0.40, 0.012, ch=0.3)
    for yy in (0.02, -0.12, -0.26):                                                                  # its tread pads
        for dx in (-0.11, 0.11): cbox("dark", (x + dx - 0.085, yy - 0.055, 0.0), (x + dx + 0.085, yy + 0.055, 0.04), 0.3)
    # the heel: its own hinge behind the mid-foot, a tapering block, a raking spur down behind, a pad
    Hh = Vector((x, 0.10, 0.10)); cyl("steel", Hh + Vector((-0.17, 0, 0)), Hh + Vector((0.17, 0, 0)), 0.035, 16)
    # (the user, 2026-10-16: the heel grows - the contact patch) 0.42 wide, 0.36 long, two pads, the spur behind
    obox("frame", Hh, Vector((x, 0.42, 0.12)), 0.42, 0.15, (1, 0, 0), 0.36, 0.16, ch=0.4, mid=(0.5, 1.04))
    obox("frame", Vector((x, 0.40, 0.11)), Vector((x, 0.54, 0.02)), 0.24, 0.10, (1, 0, 0), 0.14, 0.05, ch=0.4)   # the spur
    for dx in (-0.10, 0.10): cbox("dark", (x + dx - 0.095, 0.14, 0.0), (x + dx + 0.095, 0.42, 0.04), 0.35)
    # the toes: two, each two phalanges on pins, slightly splayed, the tips turned down with a claw lip
    cyl("steel", M1 + Vector((-0.21, -0.01, 0)), M1 + Vector((0.21, -0.01, 0)), 0.035, 16)            # the toes' pin
    for dx, sp in ((-0.11, -0.10), (0.11, 0.10)):
        P0 = Vector((x + dx, -0.37, 0.085)); P1 = P0 + Vector((sp * 0.06, -0.17, -0.025)); P2 = P1 + Vector((sp * 0.04, -0.13, -0.03))
        obox("frame", P0, P1, 0.17, 0.11, (1, 0, 0), 0.15, 0.10, ch=0.4)
        cyl("steel", P1 + Vector((-0.08, 0, 0)), P1 + Vector((0.08, 0, 0)), 0.03, 14)
        obox("frame", P1, P2, 0.14, 0.09, (1, 0, 0), 0.11, 0.06, ch=0.4)
        obox("dark", P2, P2 + Vector((0, -0.05, -0.04)), 0.11, 0.05, (1, 0, 0), 0.08, 0.03, ch=0.3)   # the claw lip
        for c, ww, ll in ((P0 + (P1 - P0) * 0.5, 0.14, 0.12), (P1 + (P2 - P1) * 0.5, 0.11, 0.09)):
            cbox("dark", (c.x - ww / 2, c.y - ll / 2, max(0.0, c.z - 0.085)), (c.x + ww / 2, c.y + ll / 2, max(0.03, c.z - 0.045)), 0.35)
        ram(Vector((x + dx, -0.22, 0.17)), P1 + Vector((0, 0.05, 0.05)), 0.016)                     # the toe's lifter

def build_e():
    """E «Атлант» after the user's sample (2026-10-16; take the scheme, not the look): her legs inside the machine's leg
    shells (thigh and shin follow hers, the knees on her knees), her feet on stirrups at the machine's raised heel; below
    them one more segment, the reverse-knee 'metatarsus', down to articulated feet (heel, mid-foot, two toes on pins).
    Upper body open: a spine frame with two MPU cells, a roll bar; power forearms over hers with tool plates (a claw on
    the right, a clamp pad on the left - modular, they change), and a manipulator on the back-left shoulder (a small
    crane arm with a gripper) instead of any gun. Shells chamfered and tapering - no box corners"""
    for s in (1, -1):
        Hp, Kn, Hl, A = Vector((s * 0.19, 0.04, 1.48)), Vector((s * 0.19, -0.06, 0.98)), Vector((s * 0.19, 0.12, 0.50)), Vector((s * 0.30, -0.04, 0.27))
        cyl("frame", Hp + Vector((s * 0.12, 0, 0)), Hp + Vector((s * 0.27, 0, 0)), 0.13, 28)                 # the hip's drum, outside
        cyl("warn", Hp + Vector((s * 0.27, 0, 0)), Hp + Vector((s * 0.285, 0, 0)), 0.09, 28)
        obox("frame", Hp + Vector((0, 0, 0.06)), Kn, 0.27, 0.30, (1, 0, 0), 0.22, 0.24, mid=(0.35, 1.06))   # the thigh's shell round hers
        joint(Kn + Vector((s * 0.13, 0, 0)), (1, 0, 0), 0.10, 0.06)
        obox("frame", Kn, Hl + Vector((0, 0, 0.04)), 0.23, 0.27, (1, 0, 0), 0.19, 0.22, mid=(0.3, 1.05))    # the shin's shell round hers
        obox("dark", Kn + Vector((0, -0.13, 0.08)), Kn + Vector((0, -0.14, -0.06)), 0.21, 0.04, (1, 0, 0), ch=0.4)   # the knee guard
        joint(Hl + Vector((s * 0.10, 0, 0)), (1, 0, 0), 0.10, 0.07)                                        # the raised heel: the reverse knee
        cbox("steel", (Hl.x - 0.11, Hl.y - 0.24, Hl.z - 0.045), (Hl.x + 0.11, Hl.y + 0.04, Hl.z), 0.3)    # her stirrup
        obox("frame", Hl + Vector((s * 0.05, 0, 0)), A + Vector((0, 0.03, 0.04)), 0.17, 0.20, (1, 0, 0), 0.20, 0.15, mid=(0.5, 0.92))   # the metatarsus
        ram(Kn + Vector((0, 0.17, -0.06)), Hl + Vector((0, 0.13, 0.18)), 0.045)                              # the knee's ram behind the shin
        ram(Hl + Vector((0, 0.15, -0.02)), A + Vector((0, 0.22, 0.06)), 0.04)                                # the heel's ram
        ram(Hp + Vector((0, -0.17, -0.04)), Kn + Vector((0, -0.11, 0.18)), 0.045)                            # the hip's ram before the thigh
        foot(s, A)
    # the pelvis and the spine: the hips' block behind her, the cells, the pump, the computer, the roll bar
    cbox("frame", (-0.38, 0.10, 1.32), (0.38, 0.36, 1.62), 0.3); box("warn", (-0.33, 0.361, 1.56), (0.33, 0.363, 1.60))
    for x in (0.12, -0.12): cyl("frame", (x, 0.30, 1.55), (x, 0.30, 2.18), 0.04)
    # (the user, 2026-10-16) the counterweight: the two cells on a carriage that slides back along two rails as the load
    # goes out (the load-moment computer drives it), ballast plates under it for the long reaches (2 x 150 kg, left at
    # the station when not needed)
    for x in (0.20, -0.20):
        obox("steel", (x, 0.30, 1.36), (x, 1.22, 1.36), 0.05, 0.06, (0, 0, 1), ch=0.3)
        obox("frame", (x, 0.34, 1.62), (x, 1.10, 1.40), 0.04, 0.05, (1, 0, 0), ch=0.3)                    # the rails' stays
    Y0 = 0.86                                                                                              # the carriage, slid out
    cbox("frame", (-0.30, Y0 - 0.05, 1.38), (0.30, Y0 + 0.32, 1.44), 0.2)
    for x in (-0.20, 0.20):
        cbox("cell", (x - 0.19, Y0, 1.44), (x + 0.19, Y0 + 0.27, 1.81), 0.15); box("warn", (x - 0.17, Y0 + 0.27, 1.74), (x + 0.17, Y0 + 0.272, 1.78))
    for k in range(2): cbox("dark", (-0.32, Y0 - 0.03, 1.36 - 0.11 * (k + 1)), (0.32, Y0 + 0.30, 1.36 - 0.11 * k - 0.01), 0.15)   # ballast plates
    cbox("frame", (-0.32, 0.12, 1.12), (0.32, 0.34, 1.32), 0.3)                                            # the pump
    cbox("frame", (-0.16, 0.30, 1.62), (0.16, 0.44, 2.12), 0.3)                                            # the spine's spar
    cyl("steel", (0.26, 0.24, 1.15), (0.26, 0.24, 1.30), 0.05)
    yoke = [Vector((0.40 * math.cos(a), 0.06 + 0.25 * math.sin(a), 2.18)) for a in [math.pi * k / 12 for k in range(13)]]
    for p, q in zip(yoke, yoke[1:]): cyl("frame", p, q, 0.034, 10)
    bar = [Vector((0.34 * math.cos(a), 0.22, 2.18 + 0.36 * math.sin(a))) for a in [math.pi * k / 14 for k in range(15)]]
    for p, q in zip(bar, bar[1:]): cyl("warn" if abs(p.x) < 0.1 else "frame", p, q, 0.03, 10)
    sphere("lamp", (0.0, 0.22, 2.56), 0.03)
    # the power forearms over hers, the tool plates
    for s in (1, -1):
        Sh, El, Wr = Vector((s * 0.42, 0.06, 2.10)), Vector((s * 0.56, -0.12, 1.70)), Vector((s * 0.66, -0.62, 1.30))
        Ex = Wr + (Wr - El).normalized() * 0.32                                                             # the telescopic stage out
        joint(Sh, (1, 0, 0), 0.10, 0.07); joint(El, (1, 0, 0), 0.08, 0.07)
        obox("frame", Sh + Vector((s * 0.04, 0, 0)), El + Vector((s * 0.04, 0, 0)), 0.12, 0.15, (1, 0, 0), 0.10, 0.12)   # the upper arm's shell
        obox("frame", El + Vector((s * 0.03, 0, 0)), Wr, 0.17, 0.19, (1, 0, 0), 0.21, 0.22, mid=(0.6, 1.06), ch=0.32)    # the power forearm
        for t in (0.3, 0.5, 0.7):
            c = El + (Wr - El) * t; obox("dark", c + Vector((s * 0.105, 0, 0)), c + Vector((s * 0.105, -0.07, -0.03)), 0.012, 0.15, ch=0.3)   # cooling fins
        ram(Sh + Vector((0, 0.13, 0.04)), El + Vector((0, 0.12, 0.10)), 0.032)
        obox("steel", Wr, Ex, 0.13, 0.14, (1, 0, 0), ch=0.3); box("warn", (Ex.x - 0.066, Ex.y + 0.04, Ex.z - 0.07), (Ex.x + 0.066, Ex.y + 0.06, Ex.z + 0.07))
        Wr = Ex                                                                                            # the tools at the stage's end
        obox("steel", Wr, Wr + Vector((0, -0.06, -0.025)), 0.24, 0.24, ch=0.3)                                  # the tool plate (quick change)
        if False:                                                                                                  # right: the three-fingered claw
            palm = Wr + Vector((0, -0.12, 0)); cyl("steel", Wr + Vector((0, -0.06, 0)), palm, 0.07)
            for dx, dz in ((-0.06, 0.06), (0.06, 0.06), (0.0, -0.07)):
                f0 = palm + Vector((dx, 0, dz)); f1 = f0 + Vector((dx * 0.5, -0.18, dz * 0.5)); f2 = f1 + Vector((-dx * 0.6, -0.07, -dz))
                obox("steel", f0, f1, 0.05, 0.05, w1=0.04, d1=0.04, ch=0.35); obox("steel", f1, f2, 0.04, 0.04, w1=0.025, d1=0.03, ch=0.35)
        else:                                                                                                      # both: clamp pads (vacuum / magnetic) facing in
            obox("steel", Wr + Vector((-s * 0.04, -0.06, 0)), Wr + Vector((-s * 0.04, -0.20, 0)), 0.10, 0.10, ch=0.35)
            obox("dark", Wr + Vector((-s * 0.09, -0.26, 0)), Wr + Vector((-s * 0.09, -0.26, 0)) + Vector((-s * 0.03, 0, 0)), 0.46, 0.40, (0, 1, 0), ch=0.25)
    # the manipulator on the back-left shoulder: a turret, a boom, a forearm, a gripper
    # (the user: the upper auxiliary arm for real) a crane: the turret on the spine, a two-stage telescopic boom, the
    # winch at its foot, the rope down to the load's lifting eye - it takes the load from above while the arms steady it
    T = Vector((0.0, 0.40, 2.22)); B1 = Vector((0.0, 0.18, 2.92)); B2 = Vector((0.0, -0.40, 2.80)); B3 = Vector((0.0, -1.00, 2.62))
    cyl("frame", T - Vector((0, 0, 0.07)), T + Vector((0, 0, 0.07)), 0.13, 24); cyl("warn", T + Vector((0, 0, 0.07)), T + Vector((0, 0, 0.08)), 0.10, 24)
    obox("frame", T, B1, 0.14, 0.15, (1, 0, 0), 0.12, 0.13, ch=0.3); joint(B1, (1, 0, 0), 0.09, 0.08)
    obox("frame", B1, B2, 0.13, 0.13, (1, 0, 0), 0.12, 0.12, ch=0.3); obox("steel", B2, B3, 0.09, 0.09, (1, 0, 0), 0.08, 0.08, ch=0.3)
    ram(T + Vector((0, -0.10, 0.05)), B1 + Vector((0, -0.16, -0.10)), 0.035)
    cyl("dark", Vector((-0.10, 0.30, 2.40)), Vector((0.10, 0.30, 2.40)), 0.08, 20)                     # the winch drum
    cyl("steel", B3, B3 + Vector((0, -0.08, 0)), 0.05, 16)                                              # the sheave
    cyl("dark", B3 + Vector((0, -0.04, -0.05)), Vector((0.0, -1.04, 1.70)), 0.008, 6)                   # the rope
    cyl("steel", Vector((0.0, -1.04, 1.70)), Vector((0.0, -1.04, 1.60)), 0.03, 10)                       # the hook block
    crate = ((-0.62, -1.48, 0.86), (0.62, -0.60, 1.56))   # 1.24 x 0.88 x 0.70 m, between the pads, the crane on its eye
    cyl("steel", (-0.06, -1.04, 1.58), (0.06, -1.04, 1.58), 0.03, 12)                                    # the lifting eye
    box("crate", *crate)
    lo, hi = Vector(crate[0]), Vector(crate[1])
    for t in (0.2, 0.8):
        z = lo.z + (hi.z - lo.z) * t; box("dark", (lo.x - 0.005, lo.y - 0.005, z - 0.02), (hi.x + 0.005, hi.y + 0.005, z + 0.02))
    box("warn", (lo.x + 0.1, lo.y - 0.006, hi.z - 0.12), (lo.x + 0.4, lo.y - 0.004, hi.z - 0.05))

def main():
    global LIFT
    sys.path.insert(0, HERE); import render_util
    if V == "D": LIFT = 1.25
    if V == "E": LIFT = 0.45                                     # her feet on the stirrups at the raised heel
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True           # no jet pack in the frame
    for o in bpy.data.objects:
        if o.parent is None and o.type in ("MESH", "ARMATURE", "EMPTY") and not o.hide_render: o.location.z += LIFT
    mats(); {"D": build_d, "E": build_e}.get(V, build)()
    for k, bm in parts.items():
        me = bpy.data.meshes.new("Exo_" + k); bm.to_mesh(me); bm.free(); me.materials.append(M[k])
        for p in me.polygons: p.use_smooth = k in ("chrome", "steel", "lamp")
        o = bpy.data.objects.new("Exo_" + k, me); bpy.context.scene.collection.objects.link(o)
    cam = render_util.setup_stage()
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1100, 1200
    k = 1.6 if V == "D" else 1.3 if V == "E" else 1.0                                 # the bigger machine: the cameras farther and higher
    shots = [("exo_%s_threequarter" % V, (3.6 * k, -4.4 * k, 1.9 * k), 1.05 * k, 55), ("exo_%s_side" % V, (6.2 * k, -0.4, 1.3 * k), 1.05 * k, 55),
             ("exo_%s_back" % V, (-3.0 * k, 4.2 * k, 2.0 * k), 1.10 * k, 55)]
    if V == "E":                                                # a low perspective and the foot close up
        shots += [("exo_E_hero", (2.3, -3.1, 0.55), 1.30, 28), ("exo_E_foot", (1.45, -1.25, 0.55), 0.12, 50)]
    render_util.shoot(cam, OUT, shots, log)

try:
    main()
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
