"""check_mesh_zfight.py <mesh.msh> [tol_m] - faces that lie in one plane, facing the same way, and overlap: the depth test
cannot tell them apart and they flicker («дрожание»). Prints the pairs of groups and where. Written 2026-10-21 for ТП-1."""
import sys, numpy as np
from collections import defaultdict
path = sys.argv[1]; tol = float(sys.argv[2]) if len(sys.argv) > 2 else 0.002
L = open(path, encoding="latin-1").read().splitlines()
tris, labs = [], []
i = 0
while i < len(L):
    if L[i].startswith("LABEL"):
        lab = L[i].split(None, 1)[1]; g = L[i + 3].split(); nv, nf = int(g[1]), int(g[2])
        P = np.array([list(map(float, L[i + 4 + k].split()[:3])) for k in range(nv)])
        for k in range(nf):
            a, b, c = map(int, L[i + 4 + nv + k].split()); tris.append((P[a], P[b], P[c])); labs.append(lab)
        i += 4 + nv + nf
    elif L[i].startswith("MATERIALS"): break
    else: i += 1
T = np.array(tris); N = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); A = np.linalg.norm(N, axis=1)
ok = A > 1e-8; N[ok] /= A[ok, None]
D = np.einsum("ij,ij->i", N, T[:, 0])
buckets = defaultdict(list)                                      # the plane, quantised: normal to 1e-3, offset to tol
for k in np.where(ok)[0]:
    key = (tuple(np.round(N[k], 3)), int(round(D[k] / tol)))
    buckets[key].append(k)
def overlap(a, b, n):
    ax = int(np.argmax(np.abs(n))); idx = [j for j in range(3) if j != ax]
    pa, pb = T[a][:, idx], T[b][:, idx]
    def sep(p, q):                                               # separating axis test of two triangles in 2D
        for tri in (p, q):
            for e in range(3):
                d = tri[(e + 1) % 3] - tri[e]; ax2 = np.array([-d[1], d[0]])
                s1, s2 = p @ ax2, q @ ax2
                if s1.max() <= s2.min() + 1e-6 or s2.max() <= s1.min() + 1e-6: return True
        return False
    return not sep(pa, pb)
hits = defaultdict(list)
for key, ks in buckets.items():
    if len(ks) < 2 or len(ks) > 4000: continue
    n = np.array(key[0])
    for x in range(len(ks)):
        for y in range(x + 1, len(ks)):
            a, b = ks[x], ks[y]
            if overlap(a, b, n):
                hits[tuple(sorted((labs[a], labs[b])))].append(np.round(T[a].mean(0), 2))
print("coplanar overlapping pairs: %d" % sum(len(v) for v in hits.values()))
for (ga, gb), pts in sorted(hits.items(), key=lambda x: -len(x[1])): print("  %-16s / %-16s %4d  e.g. %s" % (ga, gb, len(pts), pts[:2]))
