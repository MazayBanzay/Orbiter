# Max edge stretch of each mesh group over the walk/run cycles (catches spikes like a vertex dragged between the legs).
import os, sys, numpy as np
os.environ.setdefault("TANTRA_VARIANT", "suit")
src = open(os.path.join(os.path.dirname(__file__), "validate_skin.py"), encoding="utf-8").read()
exec(src[:src.index("G = read_msh(")])
O = r"C:\Games\Orbiter 2016"
L = open(os.path.join(O, "Meshes", "Tantra", MESH + ".msh")).read().split('\n'); i = 0; groups_tri = []; labels = []
while i < len(L):
    l = L[i].strip()
    if l.startswith('LABEL'): labels.append(l.split()[1])
    if l.startswith('GEOM'):
        nv, nt = map(int, l.split()[1:3]); T = np.array([list(map(int, L[i + 1 + nv + k].split()[:3])) for k in range(nt)]); groups_tri.append(T); i += nv + nt
    i += 1
G = read_msh(os.path.join(O, "Meshes", "Tantra", MESH + ".msh")); bones, groups = read_skin(os.path.join(O, "Config", "Tantra", SKIN + ".skin"))
off = np.cumsum([0] + [len(g) for g in G])
bind = np.vstack([V[:, :3] for V in G])
worst = {}
for name in ("walk", "run"):
    hdr, frames = read_clip(os.path.join(O, "Config", "Tantra", CLIPS, name + ".clip"))
    for f in frames:
        P = skin(bones, groups, G, f)
        for gi, T in enumerate(groups_tri):
            E = np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]]) + off[gi]
            l0 = np.linalg.norm(bind[E[:, 0]] - bind[E[:, 1]], axis=1); l1 = np.linalg.norm(P[E[:, 0]] - P[E[:, 1]], axis=1)
            ok = l0 > 1e-4; r = (l1[ok] / l0[ok]).max() if ok.any() else 1; d = (l1 - l0).max()
            w = worst.get(labels[gi], (0, 0)); worst[labels[gi]] = (max(w[0], r), max(w[1], d))
for k, (r, d) in sorted(worst.items(), key=lambda t: -t[1][1])[:10]: print("%-22s max stretch x%.2f  +%.3f m" % (k, r, d))
