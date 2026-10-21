"""check_mesh_holes.py <mesh.msh> - rays from all round a vessel at its surface: the first triangle each meets must face the
eye (a back face there is culled by the client: a hole, you see what lies behind). Prints every back-facing first hit by
group. Written 2026-10-20 after the EMPU's cabin shell turned out wound inside out (the user: «дыры»)."""
import sys, numpy as np
path = sys.argv[1]
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
T = np.array(tris); A, B, C = T[:, 0], T[:, 1], T[:, 2]
E1, E2 = B - A, C - A; FN = np.cross(E1, E2)
lo, hi = T.reshape(-1, 3).min(0), T.reshape(-1, 3).max(0); ctr = (lo + hi) / 2; R = np.linalg.norm(hi - lo) * 0.8
ylo = float(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] != "inside" else lo[1]
rng = np.random.default_rng(1)
bad, n = {}, 0
# "inside x0 x1 y0 y1 z0 z1": eyes inside that box looking every way (a cabin), else from all round outside
INSIDE = [float(v) for v in sys.argv[3:9]] if len(sys.argv) > 8 and sys.argv[2] == "inside" else None
if INSIDE: ylo = lo[1]
for _ in range(1500):
    if INSIDE:
        eye = np.array([rng.uniform(INSIDE[0], INSIDE[1]), rng.uniform(INSIDE[2], INSIDE[3]), rng.uniform(INSIDE[4], INSIDE[5])])
        d = rng.normal(size=3); d /= np.linalg.norm(d)
    else:
        d0 = rng.normal(size=3); d0[1] = abs(d0[1]) * 0.8; d0 /= np.linalg.norm(d0)
        eye = ctr + d0 * R
        tgt = np.array([rng.uniform(lo[0], hi[0]), rng.uniform(max(ylo, lo[1]), hi[1]), rng.uniform(lo[2], hi[2])])
        d = tgt - eye; d /= np.linalg.norm(d)
    p = np.cross(d, E2); det = np.einsum("ij,ij->i", E1, p)
    ok = np.abs(det) > 1e-12
    inv = np.where(ok, 1.0 / np.where(ok, det, 1), 0)
    s = eye - A; u = np.einsum("ij,ij->i", s, p) * inv
    q = np.cross(s, E1); v = (q @ d) * inv; t = np.einsum("ij,ij->i", E2, q) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1e-6)
    if not hit.any(): continue
    hk = np.where(hit)[0]; t0 = t[hk].min(); n += 1
    near = hk[t[hk] < t0 + 0.002]                       # a double-sided part: its two faces at the same place
    k = near[np.argmin(t[near])]
    if all(FN[j] @ d > 0 for j in near):               # facing away from the eye: culled - a hole
        bad.setdefault(labs[k], []).append(np.round((A[k] + B[k] + C[k]) / 3, 2))
print("rays that hit: %d, back faces met first: %d" % (n, sum(len(v) for v in bad.values())))
for lab, pts in sorted(bad.items(), key=lambda x: -len(x[1])): print("  %-18s %3d  e.g. %s" % (lab, len(pts), pts[:3]))
