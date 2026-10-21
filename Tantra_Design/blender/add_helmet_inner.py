# The helmet's inner shell for the view from inside (OrbiterCrew HelmetView, 2026-10-20): one smooth closed shell round
# the head - dark graphite, matte, nothing on it (the 30th century: the MFDs on the helmet display are the only light
# in it) - with one clean opening: the visor's own edge as seen from the eye. In that view the game shows it alone and
# hides the outer parts (shell, liner, rim, frame, chin bar, neck ring): their seams, steps and the liner's facets no
# longer show, and no look direction can see out through a gap - the shell is closed but for the glass.
# Built in the eye's angles: for each direction round the look axis the opening's edge alpha_b(phi) is the visor's
# outermost vertex seen from the eye; rings from that edge back to the rear pole cast from the eye onto an ellipsoid
# inside the outer shell. Inward normals (smooth, the ellipsoid's), rigid on Spine1.
#   python add_helmet_inner.py        (once; it refuses if InHelm is there - restore the .msh/.skin to rebuild)
import math, os
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MSH = os.path.join(ROOT, "Meshes", "Tantra", "AstronavigatorSuit2.msh")
SKIN = os.path.join(ROOT, "Config", "Tantra", "AstronavigatorSuit2.skin")
EYE = (0.0, 0.700, 0.150)                  # the game's HelmetEye default (keep the two together): 4-5 cm behind the glass
C, RAD = (0.0, 0.705, 0.035), (0.132, 0.160, 0.145)   # the ellipsoid: inside the shell (x +-0.142, y 0.537-0.877, z -0.117-0.193)
NPHI, NRING = 144, 26
BONE = 8

L = [l.rstrip("\r") for l in open(MSH, encoding="latin-1").read().split("\n")]
crlf = open(MSH, "rb").read(200).count(b"\r\n") > 0
if any(l == "LABEL InHelm" for l in L): raise SystemExit("InHelm is already in the mesh")

# the visor's vertices -> the opening's edge in the eye's angles
def group_verts(label):
    for i, l in enumerate(L):
        if l == "LABEL " + label:
            j = i
            while not L[j].startswith("GEOM"): j += 1
            nv = int(L[j].split()[1]); return [tuple(map(float, L[j + 1 + k].split()[:3])) for k in range(nv)]
    raise SystemExit("no group " + label)
edge = [0.0] * NPHI
for (x, y, z) in group_verts("VisorB"):
    d = (x - EYE[0], y - EYE[1], z - EYE[2]); r = math.sqrt(sum(c * c for c in d))
    a = math.acos(max(-1.0, min(1.0, d[2] / r)))          # from the look axis (+z)
    ph = math.atan2(d[1], d[0]) % (2 * math.pi)
    k = int(ph / (2 * math.pi) * NPHI) % NPHI
    edge[k] = max(edge[k], a)
# the opaque frame round the glass (the chin bar under it, the rim and the frame round it) narrows that opening: its inner
# edge seen from the eye - per direction the frame's vertex nearest the look axis (in front of the eye)
frame = [math.pi] * NPHI
for lab in ("HelmChin", "HelmRim", "Helmet_HelmFrame"):
    for (x, y, z) in group_verts(lab):
        d = (x - EYE[0], y - EYE[1], z - EYE[2]); r = math.sqrt(sum(c * c for c in d))
        if d[2] <= 0.0: continue
        a = math.acos(max(-1.0, min(1.0, d[2] / r))); ph = math.atan2(d[1], d[0]) % (2 * math.pi)
        k = int(ph / (2 * math.pi) * NPHI) % NPHI
        frame[k] = min(frame[k], a)
# fill empty bins, then smooth the edge (a clean curve, not the visor's triangle steps)
for k in range(NPHI):
    if edge[k] == 0.0:
        a = b = k
        while edge[a % NPHI] == 0.0: a -= 1
        while edge[b % NPHI] == 0.0: b += 1
        edge[k] = 0.5 * (edge[a % NPHI] + edge[b % NPHI])
edge = [min(edge[k], frame[k]) if frame[k] < math.pi else edge[k] for k in range(NPHI)]
for _ in range(10):
    edge = [0.25 * edge[k - 1] + 0.5 * edge[k] + 0.25 * edge[(k + 1) % NPHI] for k in range(NPHI)]
# a clean outline: the edge's first harmonics only (a smooth oval, not the frame's ports and steps)
NH = 2
cs = [(sum(edge[k] * math.cos(h * 2 * math.pi * (k + 0.5) / NPHI) for k in range(NPHI)) * 2 / NPHI,
       sum(edge[k] * math.sin(h * 2 * math.pi * (k + 0.5) / NPHI) for k in range(NPHI)) * 2 / NPHI) for h in range(NH + 1)]
edge = [cs[0][0] / 2 + sum(cs[h][0] * math.cos(h * 2 * math.pi * (k + 0.5) / NPHI) + cs[h][1] * math.sin(h * 2 * math.pi * (k + 0.5) / NPHI)
        for h in range(1, NH + 1)) for k in range(NPHI)]
edge = [e - math.radians(1.0) for e in edge]          # a degree inside the glass's edge: the shell overlaps the frame

def cast(dx, dy, dz):
    """the eye's ray onto the ellipsoid (from inside): the point and the inward normal"""
    ox, oy, oz = ((EYE[0] - C[0]) / RAD[0], (EYE[1] - C[1]) / RAD[1], (EYE[2] - C[2]) / RAD[2])
    ux, uy, uz = dx / RAD[0], dy / RAD[1], dz / RAD[2]
    a = ux * ux + uy * uy + uz * uz; b = 2 * (ox * ux + oy * uy + oz * uz); c = ox * ox + oy * oy + oz * oz - 1
    t = (-b + math.sqrt(b * b - 4 * a * c)) / (2 * a)
    p = (EYE[0] + dx * t, EYE[1] + dy * t, EYE[2] + dz * t)
    n = (-(p[0] - C[0]) / RAD[0] ** 2, -(p[1] - C[1]) / RAD[1] ** 2, -(p[2] - C[2]) / RAD[2] ** 2)
    l = math.sqrt(sum(q * q for q in n)); return p, (n[0] / l, n[1] / l, n[2] / l)

V, T = [], []
rings = []
for r in range(NRING):
    s = r / (NRING - 1)
    ring = []
    for k in range(NPHI):
        ph = (k + 0.5) / NPHI * 2 * math.pi
        a0 = edge[k]; a = a0 + (math.pi * 0.995 - a0) * (s ** 1.3)
        d = (math.sin(a) * math.cos(ph), math.sin(a) * math.sin(ph), math.cos(a))
        p, n = cast(*d); ring.append(len(V)); V.append((*p, *n))
    rings.append(ring)
p, n = cast(0.0, 0.0, -1.0); pole = len(V); V.append((*p, *n))
def tri(a, b, c):
    # facing the eye (inward): cross(e1, e2) . n > 0 as the mesh's convention
    pa, pb, pc = V[a][:3], V[b][:3], V[c][:3]
    e1 = [pb[i] - pa[i] for i in range(3)]; e2 = [pc[i] - pa[i] for i in range(3)]
    cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    T.append((a, b, c) if sum(cr[i] * V[a][3 + i] for i in range(3)) > 0 else (a, c, b))
for r in range(NRING - 1):
    for k in range(NPHI):
        a, b, c, d = rings[r][k], rings[r][(k + 1) % NPHI], rings[r + 1][k], rings[r + 1][(k + 1) % NPHI]
        tri(a, b, d); tri(a, d, c)
for k in range(NPHI): tri(rings[-1][k], rings[-1][(k + 1) % NPHI], pole)

gi = next(i for i, l in enumerate(L) if l.startswith("GROUPS ")); ng = int(L[gi].split()[1])
mi = next(i for i, l in enumerate(L) if l.startswith("MATERIALS ")); nm = int(L[mi].split()[1])
block = ["LABEL InHelm", "MATERIAL %d" % (nm + 1), "TEXTURE 0", "GEOM %d %d" % (len(V), len(T))]
block += ["%.5f %.5f %.5f %.4f %.4f %.4f 0 0" % v for v in V] + ["%d %d %d" % t for t in T]
mat = ["MATERIAL InHelm", "0.035 0.037 0.042 1.000", "0.035 0.037 0.042 1.000", "0.06 0.06 0.07 1 12", "0 0 0 1"]
ti = next(i for i, l in enumerate(L) if l.startswith("TEXTURES "))
names_end = mi + 1 + nm
L = L[:ti] + mat + L[ti:]
L = L[:names_end] + ["InHelm"] + L[names_end:]
L[mi] = "MATERIALS %d" % (nm + 1)
L = L[:mi] + block + L[mi:]
L[gi] = "GROUPS %d" % (ng + 1)
open(MSH, "w", encoding="latin-1", newline="").write(("\r\n" if crlf else "\n").join(L))

S = [l.rstrip("\r") for l in open(SKIN, encoding="latin-1").read().split("\n")]
scrlf = open(SKIN, "rb").read(200).count(b"\r\n") > 0
sgi = next(i for i, l in enumerate(S) if l.startswith("GROUPS ")); assert int(S[sgi].split()[1]) == ng
while S and S[-1] == "": S.pop()
S += ["LABEL %d InHelm" % ng, "GROUP %d %d" % (ng, len(V))] + ["%d 0 0 0 1.0000 0.0000 0.0000 0.0000" % BONE] * len(V)
S[sgi] = "GROUPS %d" % (ng + 1)
open(SKIN, "w", encoding="latin-1", newline="").write(("\r\n" if scrlf else "\n").join(S) + ("\r\n" if scrlf else "\n"))
print("InHelm: %d vertices, %d triangles; the opening's edge %.0f..%.0f deg from the look axis" % (len(V), len(T), math.degrees(min(edge)), math.degrees(max(edge))))
