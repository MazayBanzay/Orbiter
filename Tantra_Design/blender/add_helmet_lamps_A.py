# The helmet lamps, variant A «Порт-фара» (chosen by the user 2026-10-20, mock-up helmet_lamps.py -> renders/hl_sheet.png),
# added straight to the game's mesh and skin of the new suit type (not through a re-export of astronavigator_suit2.blend,
# which would rewrite the whole skin and its clips): on each side port (Helmhp/Helmhr/Helmhc, x -+0.155, y 0.70, z 0.074,
# Orbiter frame) a hex head - the port's own hex, 37 mm across the corners, standing 26 mm forward - a black bezel, a
# polished reflector cup and a 22 mm lens. Rigid on the bone Spine1 (index 8), as the helmet.
#   python add_helmet_lamps_A.py   (system Python; run once - it refuses if the lamp groups are already there)
# The lit look: the game's lamp beacons at the lens (cfg LampPos 0.165 0.700 0.097).
import math, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MSH = os.path.join(ROOT, "Meshes", "Tantra", "AstronavigatorSuit2.msh")
SKIN = os.path.join(ROOT, "Config", "Tantra", "AstronavigatorSuit2.skin")
BONE = 8                                  # Spine1: the helmet's bone
MAT_SHELL, MAT_DARK = 73, 71              # HelmetShell (Helmhp), HV_dark (Helmhc) - 1-based as in the .msh
PORT_X, PORT_Y, PORT_Z = 0.155, 0.700, 0.074

def frustum(cx, cy, z0, z1, r0, r1, seg, rot, cap0=True, cap1=True, smooth=False):
    """a frustum along +z (forward) from z0 (radius r0) to z1 (r1), centred at (cx, cy); flat faces, own vertices per face"""
    V, T = [], []
    ang = [rot + 2 * math.pi * k / seg for k in range(seg)]
    def ring(z, r): return [(cx + r * math.cos(a), cy + r * math.sin(a), z) for a in ang]
    A, B = ring(z0, r0), ring(z1, r1)
    def face(pts, n):
        b = len(V)
        for p in pts: V.append((*p, *n))
        for k in range(1, len(pts) - 1): T.append((b, b + k, b + k + 1))
    for k in range(seg):
        a0, a1, b0, b1 = A[k], A[(k + 1) % seg], B[k], B[(k + 1) % seg]
        am = (ang[k] + ang[(k + 1) % seg]) / 2 + (math.pi if k == seg - 1 and seg > 2 and ang[0] > ang[-1] else 0)
        mid = rot + 2 * math.pi * (k + 0.5) / seg
        slope = (r0 - r1) / max(z1 - z0, 1e-9)
        n = (math.cos(mid), math.sin(mid), slope); l = math.sqrt(sum(c * c for c in n)); n = tuple(c / l for c in n)
        face([a0, b0, b1, a1], n)
    if cap0: face(list(reversed(A)), (0, 0, -1))
    if cap1: face(B, (0, 0, 1))
    # wind each triangle so that cross(e1, e2) . n > 0 (the .msh's convention, checked on Helmhp)
    out = []
    for (a, b, c) in T:
        p0, p1, p2 = V[a][:3], V[b][:3], V[c][:3]
        e1 = [p1[i] - p0[i] for i in range(3)]; e2 = [p2[i] - p0[i] for i in range(3)]
        cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        out.append((a, b, c) if sum(cr[i] * V[a][3 + i] for i in range(3)) > 0 else (a, c, b))
    return V, out

def lamp(side):
    """side -1: the -x port, +1: the +x port; the parts as (label, material, verts, tris)"""
    cx = side * (PORT_X + 0.010); cy = PORT_Y; z0 = PORT_Z - 0.010
    rot = math.radians(30)                                   # the hex as the port's: corners up and down
    sfx = "L" if side < 0 else "R"
    parts = []
    parts.append(("LampHead" + sfx, MAT_SHELL, *frustum(cx, cy, z0, z0 + 0.026, 0.0185, 0.0175, 6, rot)))
    parts.append(("LampBezel" + sfx, MAT_DARK, *frustum(cx, cy, z0 + 0.026, z0 + 0.0295, 0.0135, 0.0135, 24, 0.0, cap0=False, cap1=False)))
    parts.append(("LampRefl" + sfx, None, *frustum(cx, cy, z0 + 0.0275, z0 + 0.0305, 0.0115, 0.0080, 24, 0.0, cap0=False, cap1=False)))
    parts.append(("LampLens" + sfx, None, *frustum(cx, cy, z0 + 0.0293, z0 + 0.0305, 0.0110, 0.0110, 24, 0.0, cap0=False)))
    return parts

L = open(MSH, encoding="latin-1").read().split("\n")
crlf = L[0].endswith("\r")
L = [l.rstrip("\r") for l in L]
if any(l.startswith("LABEL LampHead") for l in L): raise SystemExit("the lamp groups are already in the mesh")
gi = next(i for i, l in enumerate(L) if l.startswith("GROUPS ")); ng = int(L[gi].split()[1])
mi = next(i for i, l in enumerate(L) if l.startswith("MATERIALS ")); nm = int(L[mi].split()[1])
MAT_REFL, MAT_LENS = nm + 1, nm + 2
groups = lamp(-1) + lamp(1)
blocks = []
for (label, m, V, T) in groups:
    m = m if m is not None else (MAT_REFL if label.startswith("LampRefl") else MAT_LENS)
    blocks += ["LABEL " + label, "MATERIAL %d" % m, "TEXTURE 0", "GEOM %d %d" % (len(V), len(T))]
    blocks += ["%.5f %.5f %.5f %.4f %.4f %.4f 0 0" % v for v in V]
    blocks += ["%d %d %d" % t for t in T]
# the new materials: the polished cup, the sapphire lens over the emitter (white, glossy; the game's beacons light it)
names_end = mi + 1 + nm
mats = ["MATERIAL LampRefl", "0.780 0.790 0.810 1.000", "0.780 0.790 0.810 1.000", "1.0 1.0 1.0 1 120", "0 0 0 1",
        "MATERIAL LampLens", "0.920 0.940 1.000 1.000", "0.920 0.940 1.000 1.000", "1.0 1.0 1.0 1 200", "0.25 0.24 0.22 1"]
ti = next(i for i, l in enumerate(L) if l.startswith("TEXTURES "))
L = L[:ti] + mats + L[ti:]
L = L[:names_end] + ["LampRefl", "LampLens"] + L[names_end:]
L[mi] = "MATERIALS %d" % (nm + 2)
L = L[:mi] + blocks + L[mi:]
L[gi] = "GROUPS %d" % (ng + len(groups))
open(MSH, "w", encoding="latin-1", newline="").write(("\r\n" if crlf else "\n").join(L))

S = open(SKIN, encoding="latin-1").read().split("\n")
scrlf = S[0].endswith("\r"); S = [l.rstrip("\r") for l in S]
sgi = next(i for i, l in enumerate(S) if l.startswith("GROUPS ")); sng = int(S[sgi].split()[1])
assert sng == ng, ("the skin's groups and the mesh's differ", sng, ng)
add = []
for k, (label, m, V, T) in enumerate(groups):
    add += ["LABEL %d %s" % (ng + k, label), "GROUP %d %d" % (ng + k, len(V))] + ["%d 0 0 0 1.0000 0.0000 0.0000 0.0000" % BONE] * len(V)
while S and S[-1] == "": S.pop()
S += add
S[sgi] = "GROUPS %d" % (sng + len(groups))
open(SKIN, "w", encoding="latin-1", newline="").write(("\r\n" if scrlf else "\n").join(S) + ("\r\n" if scrlf else "\n"))
print("added %d groups (%s), materials %d %d" % (len(groups), ", ".join(g[0] for g in groups), MAT_REFL, MAT_LENS))
