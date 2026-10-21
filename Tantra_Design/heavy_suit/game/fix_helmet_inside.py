# «Панцирь»: after export_skin.py and fix_visor_msh.py - the helmet's inside closed for the window view (2026-10-20, «внутри
# всё дырявое»). Idempotent (a second run changes nothing):
#   1  the liner (HelmCapLn*): faces toward her (inward), its width to the helmet's inner wall (|x| 0.088: the shell is
#      built tight round the head; a wider liner left the wall's bare backs between it and her)
#   2  the frame's faces left inside the window's aperture (two of three corners in it): dropped
#   3  a tunnel from the window's border to behind her eyes, its walls facing in (the liner group; its .skin weights too):
#      the bezel's backs round the aperture are hidden behind it
import sys, numpy as np
MP = sys.argv[1] if len(sys.argv) > 1 else r"C:\Games\Orbiter-2024\Meshes\Tantra\AstronavigatorPantsir.msh"
SP = sys.argv[2] if len(sys.argv) > 2 else r"C:\Games\Orbiter-2024\Config\Tantra\AstronavigatorPantsir.skin"
raw = open(MP, encoding="ascii", newline="").read(); nl = "\r\n" if "\r\n" in raw else "\n"; L = raw.split(nl)

def groups(L):
    i, lab, out = 0, "", []
    while i < len(L):
        s = L[i]
        if s.startswith("LABEL"): lab = s.split()[1] if len(s.split()) > 1 else ""
        if s.startswith("GEOM"):
            nv, nf = map(int, s.split()[1:3]); out.append((lab, i, nv, nf)); i += nv + nf
        if s.startswith("MATERIALS"): break
        i += 1
    return out

MOUTH = (0.080, 0.6765, 0.7335, 0.176); BACK = (0.086, 0.636, 0.776, 0.060)
def is_tunnel(p): return (abs(abs(p[0]) - MOUTH[0]) < 1e-4 and abs(p[2] - MOUTH[3]) < 1e-4) or (abs(abs(p[0]) - BACK[0]) < 1e-4 and abs(p[2] - BACK[3]) < 1e-4)
# 1 + 3
lab, gi, nv, nf = next(g for g in groups(L) if g[0].startswith("HelmCapLn"))
V = [L[gi + 1 + k].split() for k in range(nv)]; F = [L[gi + 1 + nv + k].split() for k in range(nf)]
P = np.array([[float(x) for x in v[:3]] for v in V]); N = np.array([[float(x) for x in v[3:6]] for v in V])
tun = np.array([is_tunnel(p) for p in P]); lin = ~tun
ctr = P[lin].mean(0)
if (N[lin] * (P[lin] - ctr)).sum(1).mean() > 0:   # facing out: turn it in
    N[lin] = -N[lin]; F = [[f[0], f[2], f[1]] for f in F]; print("liner turned to face in")
w = np.abs(P[lin, 0]).max()
if abs(w - 0.088) > 1e-4: P[lin, 0] *= 0.088 / w; print("liner width %.3f -> 0.088" % w)
for k in range(nv): V[k][0:6] = ["%.5f" % P[k, 0], "%.5f" % P[k, 1], "%.5f" % P[k, 2], "%.4f" % N[k, 0], "%.4f" % N[k, 1], "%.4f" % N[k, 2]]
newV, newF = [], []
if not tun.any():
    uv = V[0][6:8]; C = np.array([0, 0.705, 0.11])
    def rect(r): x, y0, y1, z = r; return [np.array(p) for p in ((-x, y0, z), (x, y0, z), (x, y1, z), (-x, y1, z))]
    a, b = rect(MOUTH), rect(BACK)
    for k in range(4):
        q = [a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]]; cen = sum(q) / 4
        n = np.cross(q[1] - q[0], q[3] - q[0]); n /= np.linalg.norm(n)
        if np.dot(n, C - cen) < 0: n = -n
        base = nv + len(newV)
        for p in q: newV.append(["%.5f" % p[0], "%.5f" % p[1], "%.5f" % p[2], "%.4f" % n[0], "%.4f" % n[1], "%.4f" % n[2], uv[0], uv[1]])
        for t in ((0, 1, 2), (0, 2, 3)):
            A_, B_, C_ = q[t[0]], q[t[1]], q[t[2]]
            newF.append([str(base + t[0]), str(base + t[1]), str(base + t[2])] if np.dot(np.cross(B_ - A_, C_ - A_), C - cen) > 0 else [str(base + t[0]), str(base + t[2]), str(base + t[1])])
    print("tunnel added")
L = L[:gi] + ["GEOM %d %d" % (nv + len(newV), nf + len(newF))] + [" ".join(v) for v in V + newV] + [" ".join(f) for f in F + newF] + L[gi + 1 + nv + nf:]
# 2
out, i, lab, drop = [], 0, "", 0
X0, X1, Y0, Y1, Z0, Z1 = -0.084, 0.084, 0.6725, 0.7375, 0.120, 0.186
inb = lambda p: X0 < p[0] < X1 and Y0 < p[1] < Y1 and Z0 < p[2] < Z1
while i < len(L):
    s = L[i]
    if s.startswith("LABEL"): lab = s.split()[1] if len(s.split()) > 1 else ""
    if s.startswith("GEOM") and lab.startswith("Helm") and lab not in ("HelmCapGd", "HelmCapSl") and not lab.startswith(("HelmCapLv", "HelmCapLu", "HelmCapLn")):
        nv_, nf_ = map(int, s.split()[1:3]); Vv = L[i + 1:i + 1 + nv_]; Ff = L[i + 1 + nv_:i + 1 + nv_ + nf_]
        Pp = [list(map(float, v.split()[:3])) for v in Vv]; keep = []
        for f in Ff:
            pts = [Pp[int(x)] for x in f.split()]; cen = [sum(p[j] for p in pts) / 3 for j in range(3)]
            if sum(inb(p) for p in pts) >= 2 or inb(cen): drop += 1; continue
            keep.append(f)
        out.append("GEOM %d %d" % (nv_, len(keep))); out += Vv + keep; i += 1 + nv_ + nf_; continue
    out.append(s); i += 1
open(MP, "w", encoding="ascii", newline="").write(nl.join(out)); print("aperture faces dropped: %d" % drop)
if newV:   # the tunnel's weights: the liner's own (Spine1)
    s = open(SP, encoding="latin-1", newline="").read(); snl = "\r\n" if "\r\n" in s else "\n"; S = s.split(snl)
    k = next(j for j, l in enumerate(S) if l.startswith("LABEL") and "HelmCapLn" in l); g = S[k + 1].split(); cnt = int(g[2])
    S[k + 1] = "GROUP %s %d" % (g[1], cnt + len(newV)); S = S[:k + 2 + cnt] + [S[k + 2]] * len(newV) + S[k + 2 + cnt:]
    open(SP, "w", encoding="latin-1", newline="").write(snl.join(S)); print("skin: %d weights added" % len(newV))
