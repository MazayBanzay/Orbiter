# «Наплечный» М1 «Гекса» - the game mesh (the user's choices, 2026-10-17/18): the hexagonal body with its deep core, the
# hinge posts (screw hinges: the swing back lifts the boom 9 cm), the 40x60 booms in three telescopic stages, the faceted
# twin-chamber capsules with the annular plug nozzle (rhenium cowl lip, HfB2 plug with the cBN tip ring, the throat ring
# split by two struts). The nozzle's hot surfaces carry a texture whose _emis companion is the glow's pattern (yellow at
# the throat, red to the plug's tip); the module sets the material's emission by the capsule's temperature (as the
# «Панцирь» soles: Skin::SetGlow).
#   Meshes\Tantra\JetPackHexaWorn.msh  - her model frame at rest (x right, y up, z forward), deployed
#   Meshes\Tantra\JetPackHexa.msh      - lying about: folded, origin at the body's centre
#   Config\Tantra\JetPackHexa.geo      - KIN 3: hinges, pivots (the gimbals), the telescope's stroke, the lift, the groups
#   Textures\Tantra\JetHexaNoz.dds, JetHexaNoz_emis.dds
# Run: python build_jetpack_hexa.py   (numpy, Pillow)
import os, math
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
WORN = os.path.join(ROOT, "Meshes", "Tantra", "JetPackHexaWorn.msh")
DROP = os.path.join(ROOT, "Meshes", "Tantra", "JetPackHexa.msh")
GEO = os.path.join(ROOT, "Config", "Tantra", "JetPackHexa.geo")
TEXD = os.path.join(ROOT, "Textures", "Tantra")
MATS = [("White", (0.88, 0.89, 0.90), 0.3), ("Dark", (0.05, 0.055, 0.06), 0.1), ("Metal", (0.55, 0.56, 0.58), 0.6), ("Red", (0.78, 0.09, 0.07), 0.2),
        ("Grey", (0.28, 0.29, 0.31), 0.25), ("GlowR1", (1.0, 1.0, 1.0), 0.4), ("GlowR2", (1.0, 1.0, 1.0), 0.4),
        ("GlowL1", (1.0, 1.0, 1.0), 0.4), ("GlowL2", (1.0, 1.0, 1.0), 0.4)]   # Glow*: textured, emissive - a capsule's two chambers (1 outboard, 2 inboard)
MI = {n: i for i, (n, _, _) in enumerate(MATS)}
TEXNAME = "Tantra\\JetHexaNoz.dds"

def M(xb, yb, zb): return (xb, zb - 0.93, -yb + 0.0014)        # the Blender sketches' frame -> her model frame
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def unit(a):
    l = math.sqrt(sum(x * x for x in a)) or 1.0; return mul(a, 1 / l)

groups, order, role = {}, [], {}
def grp(name, mi, r="core", side=-1):
    if name not in groups: groups[name] = [mi, [], []]; order.append(name); role[name] = (r, side)
    return groups[name]

def tri_poly(g, pts, n=None, uv=None):
    """a flat polygon; its normal from the winding unless given"""
    if n is None: n = unit(cross(sub(pts[1], pts[0]), sub(pts[2], pts[0])))
    b = len(g[1])
    for k, p in enumerate(pts): g[1].append((tuple(p), tuple(n), uv[k] if uv else (0.0, 0.0)))
    for k in range(1, len(pts) - 1): g[2].append((b, b + k, b + k + 1)); g[2].append((b, b + k + 1, b + k))   # both sides (as the Соты mesh)

def quad(g, a, b, c, d, uv=None): tri_poly(g, [a, b, c, d], uv=uv)

# ---- extrusions in the Blender frame (y back from her back), mapped to the model frame ----
def chamfered_prism(g, outline_xz, y0, y1, ch):
    """a silhouette (x, z) extruded from y0 (her back) to y1, its back edge chamfered by ch"""
    n = len(outline_xz); cx = sum(p[0] for p in outline_xz) / n; cz = sum(p[1] for p in outline_xz) / n
    ext = max(math.hypot(x - cx, z - cz) for x, z in outline_xz); s = 1 - ch / ext
    ring = lambda k, y: [M(cx + (x - cx) * k, y, cz + (z - cz) * k) for x, z in outline_xz]
    L = [ring(1.0, y0), ring(1.0, y1 - ch), ring(s, y1)]
    for a, b in zip(L, L[1:]):
        for i in range(n): quad(g, a[i], a[(i + 1) % n], b[(i + 1) % n], b[i])
    tri_poly(g, L[-1]); tri_poly(g, L[0][::-1])

def hexo(r, cz, rot=90.0): return [(r * math.cos(math.radians(rot + 60 * k)), cz + r * math.sin(math.radians(rot + 60 * k))) for k in range(6)]
def clip_z(poly, z0, z1):
    def clip(p, keep, zc):
        out = []
        for i in range(len(p)):
            a, b = p[i], p[(i + 1) % len(p)]
            ia, ib = keep(a[1]), keep(b[1])
            if ia: out.append(a)
            if ia != ib: t = (zc - a[1]) / (b[1] - a[1]); out.append((a[0] + (b[0] - a[0]) * t, zc))
        return out
    return clip(clip(poly, lambda z: z >= z0, z0), lambda z: z <= z1, z1)

def cyl(g, c, axis, r, h, nu=16, caps=True):
    a = unit(axis); t = unit(cross(a, (0, 1, 0) if abs(a[1]) < 0.9 else (1, 0, 0))); s = cross(a, t)
    e0, e1 = add(c, mul(a, -h / 2)), add(c, mul(a, h / 2))
    R0 = [add(e0, add(mul(t, r * math.cos(2 * math.pi * j / nu)), mul(s, r * math.sin(2 * math.pi * j / nu)))) for j in range(nu)]
    R1 = [add(p, mul(a, h)) for p in R0]
    for j in range(nu):
        k = (j + 1) % nu; nm = unit(sub(add(R0[j], R0[k]), mul(e0, 2)))
        g_b = len(g[1])
        for p in (R0[j], R0[k], R1[k], R1[j]): g[1].append((p, nm, (0.0, 0.0)))
        for t in ((g_b, g_b + 1, g_b + 2), (g_b, g_b + 2, g_b + 3)): g[2].append(t); g[2].append((t[0], t[2], t[1]))
    if caps: tri_poly(g, R1, a); tri_poly(g, R0[::-1], mul(a, -1))

# ---- the body: the Монолит «Ш» (2026-10-18) - one cast shell over the suit's back block, exported from Blender by
# jetpack_onblock.py (POB_EXPORT=26) to jetpack_mono.json; its hinge nodes hold the booms' roots, no screw (the fold clears
# the shell by 2.7 cm without the lift). Without the json: the old hexagonal body and the screw posts.
import json
MONO = os.path.join(HERE, "jetpack_mono.json"); USE_MONO = os.path.exists(MONO)
def MX(xb, yb, zb): return (-xb, zb - 0.93, -yb + 0.0014)   # the true mirror into her model frame (the block's interface: x her right)
if USE_MONO:
    for mn, gd in json.load(open(MONO, encoding="utf-8"))["groups"].items():
        g = grp("Mono" + mn, MI[mn])
        b = len(g[1])
        for x, y, z, nx, ny, nz in gd["v"]: g[1].append((MX(x, y, z), (-nx, nz, -ny), (0.0, 0.0)))
        for a, b_, c in gd["t"]: g[2].append((b + a, b + b_, b + c))   # Blender's winding: the mirror and the writer's swap make it Orbiter's
BACK = 0.108
HINGE = {1: M(0.204, 0.186, 1.4685), -1: M(-0.204, 0.186, 1.4685)}
if not USE_MONO:
  body = grp("Body", MI["White"]); core = grp("Core", MI["White"]); red = grp("CoreRed", MI["Red"]); dark = grp("Ports", MI["Dark"])
  chamfered_prism(body, hexo(0.26, 1.27), BACK, BACK + 0.13, 0.02)
  chamfered_prism(core, hexo(0.15, 1.27), BACK + 0.12, BACK + 0.24, 0.02)
  band = clip_z(hexo(0.152, 1.27), 1.33, 1.345)
  chamfered_prism(red, [(x * 1.012, z) for x, z in band], BACK + 0.12, BACK + 0.2415, 0.0)
  for x, z in ((0.18, 1.40), (-0.18, 1.40), (0.18, 1.14), (-0.18, 1.14)):
      cyl(dark, M(x, BACK + 0.131, z), (0, 0, -1), 0.011, 0.006, 12)
  cyl(dark, M(0.14, BACK + 0.10, 1.18), (1, 0, 0), 0.025, 0.03, 4)            # the suit coupling (hose port)
  # the hinge posts: a vertical pin per side from the body's top edge, a bracket into the body; the boom rides up it 9 cm
  post = grp("Posts", MI["Metal"]); postr = grp("PostsRed", MI["Red"]); brk = grp("Brackets", MI["White"])
  for sg in (1, -1):
      h = HINGE[sg]
      cyl(post, add(h, (0, 0.045, 0)), (0, 1, 0), 0.016, 0.19, 16)
      cyl(postr, add(h, (0, 0.142, 0)), (0, 1, 0), 0.020, 0.008, 16)
      cyl(brk, add(h, (0, -0.06, 0)), (0, 1, 0), 0.026, 0.04, 6)
      chamfered_prism(brk, [(sg * 0.12, 1.36), (sg * 0.23, 1.36), (sg * 0.23, 1.40), (sg * 0.12, 1.46)][:: (1 if sg > 0 else -1)], 0.16, 0.215, 0.006)

# ---- a boom: three octagonal stages (40 x 60, 0.86, 0.72), built extended ----
OCT = [(-0.020, -0.022), (-0.012, -0.030), (0.012, -0.030), (0.020, -0.022), (0.020, 0.022), (0.012, 0.030), (-0.012, 0.030), (-0.020, 0.022)]   # (y_b, z_b)
X0, LFULL, LST = 0.215, 0.337, 0.135
STROKE = LFULL - LST
STAGES = [(1.0, 0.0, LST), (0.86, 0.115, 0.256), (0.72, 0.216, LFULL)]
def oct_tube(g, sg, k, a, b, y=0.18, z=1.492):
    R0 = [M(sg * (X0 + a), y + v * k, z + w * k) for v, w in OCT]; R1 = [M(sg * (X0 + b), y + v * k, z + w * k) for v, w in OCT]
    n = len(OCT)
    for i in range(n): quad(g, R0[i], R0[(i + 1) % n], R1[(i + 1) % n], R1[i]) if sg > 0 else quad(g, R0[(i + 1) % n], R0[i], R1[i], R1[(i + 1) % n])
    tri_poly(g, R1 if sg > 0 else R1[::-1]); tri_poly(g, R0[::-1] if sg > 0 else R0)
for sg, S in ((1, "R"), (-1, "L")):
    side = 0 if sg > 0 else 1
    for i, (k, a, b) in enumerate(STAGES):
        g = grp("Stage%d%s" % (i, S), MI["White"] if i != 1 else MI["Grey"], ("boom", "tele1", "tele2")[i], side)
        oct_tube(g, sg, k, a, b)
        rg = grp("StageRing%d%s" % (i, S), MI["Red"] if i == 0 else MI["Dark"], ("boom", "tele1", "tele2")[i], side)
        oct_tube(rg, sg, k * 1.07, b - 0.008, b)

# ---- the capsule: faceted body, the band, the cowl, and the nozzle ----
POD = {1: (0.597, 0.175, 1.471), -1: (-0.597, 0.175, 1.471)}
PR, PH = 0.047, 0.118
PROF = [(0.0, 0.5), (0.45, 0.47), (0.75, 0.38), (0.92, 0.24), (1.0, 0.08), (1.0, -0.12), (0.97, -0.30), (0.90, -0.42), (0.86, -0.47)]
def lathe(g, c, prof, nu=32, nseg=8, uvv=None, a0=0.0, a1=360.0):
    """revolve (r, dz) about the vertical axis through the Blender point c; nseg>0 faceted; uvv: v per profile point"""
    rings = []
    for r, dz in prof:
        ring = []
        for j in range(nu + 1):
            a = math.radians(a0 + (a1 - a0) * j / nu); k = 1.0
            if nseg: s = 2 * math.pi / nseg; k = math.cos(s / 2) / math.cos((a - s / 2) % s - s / 2)
            ring.append(M(c[0] + r * k * math.cos(a), c[1] + r * k * math.sin(a), c[2] + dz))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for j in range(nu):
            uv = None
            if uvv: uv = [(j / nu, uvv[i]), ((j + 1) / nu, uvv[i]), ((j + 1) / nu, uvv[i + 1]), (j / nu, uvv[i + 1])]
            a, b, c_, d = rings[i][j], rings[i][j + 1], rings[i + 1][j + 1], rings[i + 1][j]
            nrm = unit(cross(sub(b, a), sub(d, a)))
            if nrm == (0.0, 0.0, 0.0) or sum(x * x for x in cross(sub(b, a), sub(d, a))) < 1e-14: nrm = unit(cross(sub(c_, b), sub(d, b)))
            tri_poly(g, [a, b, c_, d], uv=uv)
ZE = -PH; R_LIP_O, R_LIP_I, R_PLUG, L_PLUG, R_TIP = 0.0405, 0.0300, 0.0225, 0.046, 0.0075
for sg, S in ((1, "R"), (-1, "L")):
    side = 0 if sg > 0 else 1; c = POD[sg]
    lathe(grp("Pod" + S, MI["White"], "ring", side), c, [(r * PR, z * PH * 2) for r, z in PROF], 32, 8)
    lathe(grp("PodBand" + S, MI["Red"], "ring", side), c, [(PR * 1.03, 0.012), (PR * 1.03, -0.012)], 32, 8)
    lathe(grp("PodCowl" + S, MI["Metal"], "ring", side), c, [(0.86 * PR, -0.47 * PH * 2), (R_LIP_O, ZE + 0.004), (R_LIP_O - 0.001, ZE)], 32, 0)
    pts = []
    for i in range(10):
        t = i / 9; z = ZE + 0.012 - (L_PLUG + 0.012) * t; r = R_TIP + (R_PLUG + 0.002 - R_TIP) * (1 - t) ** 1.7; pts.append((r, z))
    for ch, (b0, b1) in ((1, (-90.0, 90.0)), (2, (90.0, 270.0))):   # the struts lie along y: the halves are the +x and -x sides
        if sg < 0: b0, b1 = b0 + 180.0, b1 + 180.0                     # (chamber 1 outboard on either side)
        glow = grp("Glow%s%d" % (S, ch), MI["Glow%s%d" % (S, ch)], "ring", side)
        lathe(glow, c, [(R_LIP_O - 0.001, ZE), (R_LIP_I + 0.0015, ZE - 0.0005), (R_LIP_I, ZE + 0.003), (R_LIP_I + 0.002, ZE + 0.012)], 16, 0, uvv=[0.55, 0.30, 0.08, 0.0], a0=b0, a1=b1)
        lathe(glow, c, pts, 16, 0, uvv=[0.0] + [0.05 + 0.95 * (i / 9) for i in range(1, 10)], a0=b0, a1=b1)
    tipz = pts[-1][1]
    lathe(grp("PodBase" + S, MI["Dark"], "ring", side), c, [(R_TIP + 0.0006, tipz), (0.0, tipz)], 12, 0)
    lathe(grp("PodThroat" + S, MI["Dark"], "ring", side), c, [(R_LIP_I, ZE + 0.010), (R_PLUG + 0.002, ZE + 0.010)], 32, 0)
    for k in (0, 1):   # the struts splitting the throat ring into the two chambers
        a = math.pi / 2 + k * math.pi; rm = (R_LIP_I + R_PLUG) / 2
        cyl(grp("PodStrut" + S, MI["Metal"], "ring", side), M(c[0] + rm * math.cos(a), c[1] + rm * math.sin(a), c[2] + ZE + 0.004), (math.cos(a), math.sin(a), 0), 0.0012, R_LIP_I - R_PLUG, 4)
    # the collar where the boom meets the capsule (the gimbal) - moves with the capsule's telescope, not its tilt: on the tele2 stage
    lathe(grp("Collar" + S, MI["Metal"], "tele2", side), (sg * 0.552 + sg * 0.02, 0.18, 1.492), [(0.0, -0.025), (0.03, -0.025), (0.03, 0.025), (0.0, 0.025)], 8, 8)

# ---- the textures: the nozzle's base (dark ceramic grey) and its glow pattern (by v: throat 0 -> plug tip 1) ----
def curve(T):
    f = min(1.0, max(0.0, (T - 760.0) / 740.0)); return (f ** 0.5, f ** 1.5, f ** 8)
H_, W_ = 128, 32
base = np.full((H_, W_, 3), 0.17); base += np.random.default_rng(3).normal(0, 0.015, (H_, W_, 1)); base = np.clip(base, 0, 1)
emis = np.zeros((H_, W_, 3))
for row in range(H_):
    v = row / (H_ - 1); T = 1700.0 - 600.0 * v ** 0.8
    emis[row, :, :] = curve(T)
os.makedirs(TEXD, exist_ok=True)
Image.fromarray((base * 255).astype(np.uint8)).save(os.path.join(TEXD, "JetHexaNoz.dds"), pixel_format="DXT1")
Image.fromarray((emis * 255).astype(np.uint8)).save(os.path.join(TEXD, "JetHexaNoz_emis.dds"), pixel_format="DXT1")

# ---- write ----
CENTRE = M(0.0, 0.25, 1.30) if USE_MONO else M(0.0, BACK + 0.12, 1.27)
LIFT = 0.0 if USE_MONO else 0.09
def fold_xf(nm, p):
    """lying about: the telescope in, the swing back with the lift (as the module poses it at deploy 0)"""
    r, side = role[nm]
    if side < 0: return p
    sg = 1 if side == 0 else -1
    if r in ("tele1", "tele2", "ring"): p = add(p, (-sg * STROKE * (0.5 if r == "tele1" else 1.0), 0, 0))
    h = HINGE[sg]; a = math.radians(sg * 90); c_, s_ = math.cos(a), math.sin(a)
    d = sub(p, h); d = (c_ * d[0] + s_ * d[2], d[1], -s_ * d[0] + c_ * d[2])
    return add(add(h, d), (0, LIFT, 0))

def write(path, xf, centre=(0, 0, 0)):
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(order))
        for nm in order:
            mi, vs, fs = groups[nm]; tex = MATS[mi][0].startswith("Glow")
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (nm, mi + 1, 1 if tex else 0, len(vs), len(fs)))
            for p, n, uv in vs:
                q = sub(xf(nm, p), centre); m = n
                if tex: f.write("%.5f %.5f %.5f %.4f %.4f %.4f %.4f %.4f\n" % (*q, *m, *uv))
                else: f.write("%.5f %.5f %.5f %.4f %.4f %.4f\n" % (*q, *m))
            for a, b, c in fs: f.write("%d %d %d\n" % (a, c, b))
        f.write("MATERIALS %d\n" % len(MATS))
        for n, _, _ in MATS: f.write("JH_%s\n" % n)
        for n, col, sp in MATS:
            f.write("MATERIAL JH_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 20\n0 0 0 1\n" % (n, *col, *col, sp, sp, sp))
        f.write("TEXTURES 1\n%s\n" % TEXNAME)

write(WORN, lambda nm, p: p)
write(DROP, fold_xf, CENTRE)
pv = {sg: M(sg * 0.597, 0.18, 1.492) for sg in (1, -1)}
with open(GEO, "w", newline="\n") as f:
    f.write("# OrbiterCrew jet pack M1 «Гекса» in the crew member's model frame (written by build_jetpack_hexa.py)\n")
    f.write("KIN 3\nSHELL %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*CENTRE, 0.52, 0.52, 0.24))
    for sg in (1, -1):   # HINGE: the screw hinge's pin; PIVOT: the capsule's gimbal (at the collar)
        f.write("HINGE %.4f %.4f %.4f\nPIVOT %.4f %.4f %.4f\n" % (*HINGE[sg], *pv[sg]))
    f.write("STROKE %.4f\nLIFT %.2f\nSWING 90\nGLOW_POD %d %d %d %d\n" % (STROKE, LIFT, MI["GlowR1"], MI["GlowR2"], MI["GlowL1"], MI["GlowL2"]))
    for k, nm in enumerate(order):
        r, side = role[nm]; f.write("GROUP %d %s %d\n" % (k, r, side))
print("groups", len(order), "verts", sum(len(groups[n][1]) for n in order))
