# Offline check of the skin/clip files: CPU skinning in numpy, exactly as the UACS runtime will do it.
import numpy as np, os, sys
O = r"C:\Games\Orbiter 2016"
VARIANT = os.environ.get("TANTRA_VARIANT", "coverall")
MESH, SKIN, CLIPS = {"coverall": ("AstronavigatorSkin", "Astronavigator", "anim"), "suit": ("AstronavigatorSuit", "AstronavigatorSuit", "anim_suit")}[VARIANT]
def read_msh(p):
    L = open(p).read().split('\n'); i = 0; G = []
    while i < len(L):
        l = L[i].strip()
        if l.startswith('GEOM'):
            nv, nt = map(int, l.split()[1:3]); V = np.array([list(map(float, L[i + 1 + k].split()[:6])) for k in range(nv)]); G.append(V); i += nv + nt
        i += 1
    return G
def read_skin(p):
    L = [l.split() for l in open(p).read().split('\n') if l.strip()]; i = 0; bones = []; groups = []
    while i < len(L):
        t = L[i]
        if t[0] == 'BONES':
            n = int(t[1])
            for k in range(n):
                r = L[i + 1 + k]; R = np.array(list(map(float, r[2:11]))).reshape(3, 3); T = np.array(list(map(float, r[11:14])))
                bones.append((r[0], int(r[1]), R, T))
            i += n
        elif t[0] == 'GROUP':
            nv = int(t[2]); W = np.array([list(map(float, L[i + 1 + k])) for k in range(nv)]); groups.append(W); i += nv
        i += 1
    return bones, groups
def read_clip(p):
    L = [l.split() for l in open(p).read().split('\n') if l.strip()]; hdr = {}; frames = []; i = 0
    while i < len(L):
        t = L[i]
        if t[0] == 'FRAME':
            nb = int(hdr['BONES']); frames.append(np.array([list(map(float, L[i + 1 + k])) for k in range(nb)])); i += nb
        elif len(t) == 2: hdr[t[0]] = t[1]
        i += 1
    return hdr, frames
def qmat(q):
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
def skin(bones, groups, G, frame):
    S = []
    for (n, par, R0, T0), f in zip(bones, frame):
        R = qmat(f[:4]); T = f[4:7]
        # skin matrix = pose * inverse(rest)
        Ri = R @ R0.T; S.append((Ri, T - Ri @ T0))
    out = []
    for V, W in zip(G, groups):
        P = V[:, :3]; acc = np.zeros_like(P)
        for k in range(4):
            b = W[:, k].astype(int); w = W[:, 4 + k][:, None]
            Rs = np.stack([S[j][0] for j in b]); Ts = np.stack([S[j][1] for j in b])
            acc += w * (np.einsum('nij,nj->ni', Rs, P) + Ts)
        out.append(acc)
    return np.vstack(out)
G = read_msh(os.path.join(O, "Meshes", "Tantra", MESH + ".msh")); bones, groups = read_skin(os.path.join(O, "Config", "Tantra", SKIN + ".skin"))
print("groups mesh/skin", len(G), len(groups), "verts match", all(len(a) == len(b) for a, b in zip(G, groups)))
bind = np.vstack([V[:, :3] for V in G]); print("bind bbox", bind.min(0).round(3), bind.max(0).round(3))
static = np.vstack([V[:, :3] for V in read_msh(os.path.join(O, r"Meshes\Tantra\Astronavigator.msh"))])
for name in ("idle", "walk", "run"):
    hdr, frames = read_clip(os.path.join(O, "Config", "Tantra", CLIPS, name + ".clip"))
    P = skin(bones, groups, G, frames[0])
    ys = [skin(bones, groups, G, f)[:, 1].min() for f in frames[::max(1, len(frames) // 6)]]
    print("%-5s frames %2d  frame0 bbox %s .. %s  min-y over cycle %s" % (name, len(frames), P.min(0).round(3), P.max(0).round(3), np.round(ys, 3)))
print("static idle bbox", static.min(0).round(3), static.max(0).round(3))

# ---- grounding: shift each clip so the lowest sole point over the cycle is exactly on the ground (y = -0.93) ----
if "--ground" in sys.argv:
    GROUND = -0.93
    for name in ("idle", "walk", "run"):
        p = os.path.join(O, "Config", "Tantra", CLIPS, name + ".clip"); hdr, frames = read_clip(p)
        lows = [skin(bones, groups, G, f)[:, 1].min() for f in frames]
        # the lowest point in each frame is the stance sole; use the median of per-frame minima for the idle/walk,
        # and the minimum for the run (airborne frames lift the median)
        low = min(lows) if name == "run" else float(np.median(lows))
        dy = GROUND - low
        L = open(p).read().split('\n'); out = []; nb = int(hdr['BONES']); cnt = 0; inframe = 0
        for l in L:
            t = l.split()
            if t and t[0] == 'FRAME': inframe = nb; out.append(l); continue
            if inframe and len(t) == 7:
                v = list(map(float, t)); v[5] += dy; out.append("%.6f %.6f %.6f %.6f %.5f %.5f %.5f" % tuple(v)); inframe -= 1; continue
            out.append(l)
        open(p, 'w', newline='\n').write('\n'.join(out))
        print("grounded %-5s dy %+.3f m" % (name, dy))
