"""Coverall figure: the seated hands' grips for OrbiterCrew (SeatArms), made the way hand_shapes.py makes the fist.

Run after hand_shapes.py (the relaxed hands must be baked in the mesh). It adds to Config\\Tantra\\Astronavigator.skin
  MORPH grip   round a handle ~40 mm across (a yoke's horn): the fingers wrapped, the thumb over them
  MORPH cup    over a ball ~46 mm across (the cursor unit's trackball): the palm on it, the fingers curved down
both relative to the relaxed hand (the mesh as it is now); running it again replaces them. The mesh is not changed.
System Python + numpy.
"""
import os
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import hand_shapes as hs

O = os.path.join("C:" + os.sep, "Games", "Orbiter-2024")
MSH = os.path.join(O, "Meshes", "Tantra", "AstronavigatorSkin.msh")
SKIN = os.path.join(O, "Config", "Tantra", "Astronavigator.skin")

# the curl added to the relaxed hand (deg, hand_shapes' three joints); thumb: negative folds it across the fingers
SHAPES = (("grip", dict(mcp=34, pip=40, dip=22, adduct=0.0, thumb=-75)),
          ("cup", dict(mcp=14, pip=14, dip=10, adduct=0.0, thumb=-12)))


def main():
    lines, groups = hs.read_msh(MSH)
    skin_lines, names, W = hs.read_skin(SKIN)
    gi = next(i for i, g in enumerate(groups) if g[0] == "Skin")
    _, start, nv = groups[gi]
    V = np.array([list(map(float, lines[start + k].split())) for k in range(nv)])
    P, N = V[:, 0:3].copy(), V[:, 3:6].copy()
    w = W[gi]
    def weight(bone):
        b = names.index(bone); return ((w[:, :4] == b) * w[:, 4:]).sum(1)
    shapes = {}
    for name, spec in SHAPES:
        sP, sN = P.copy(), N.copy()
        for side, th in (("Left", "LThumb"), ("Right", "RThumb")):
            wf = weight(side + "HandFinger1")
            idx = np.where(wf > 0.15)[0]
            F = P[idx]
            wrist_d = np.linalg.norm(F - P[np.argmax(weight(side + "Hand"))], axis=1)
            order = np.argsort(wrist_d)
            K = F[order[:max(8, len(F) // 7)]].mean(0); tips = F[order[-max(8, len(F) // 10):]].mean(0)
            f = tips - K; f /= np.linalg.norm(f)
            _, _, vt = np.linalg.svd(F - F.mean(0), full_matrices=False); p = vt[2].copy()
            if p[0] * -np.sign(K[0]) < 0: p = -p
            p -= (p @ f) * f; p /= np.linalg.norm(p)
            a = np.cross(f, p); a /= np.linalg.norm(a)
            L = ((F - K) @ f).max()
            lat = (F - K) @ a
            centres = np.quantile(lat, [0.125, 0.375, 0.625, 0.875])
            for _ in range(20):
                cl = np.argmin(np.abs(lat[:, None] - centres[None, :]), 1)
                centres = np.array([lat[cl == k].mean() if (cl == k).any() else centres[k] for k in range(4)])
            fP, fN = hs.shape(F, N[idx], wf[idx], (K, f, p, a, L, lat, cl, centres), spec)
            sP[idx], sN[idx] = fP, fN
            wt = weight(th); tidx = np.where(wt > 0.3)[0]
            if len(tidx):
                T = P[tidx]; tb = T[np.argsort(np.linalg.norm(T - K, axis=1))[:5]].mean(0)
                tP, tN = hs.rotate(T, N[tidx], tb, f, np.radians(spec["thumb"]) * (1 if side == "Left" else -1), wt[tidx])
                sP[tidx], sN[tidx] = tP, tN
        shapes[name] = (sP, sN)
    # the skin: its own lines without earlier grip / cup shapes, then the new ones
    out, i = [], 0
    L_ = [l for l in skin_lines]
    while L_ and not L_[-1].strip(): L_.pop()
    while i < len(L_):
        t = L_[i].split()
        if t and t[0] == "MORPH" and t[1] in ("grip", "cup"):
            i += int(t[3]) + 1; continue
        out.append(L_[i]); i += 1
    for name, (sP, sN) in shapes.items():
        d = np.where(np.linalg.norm(sP - P, axis=1) > 1e-5)[0]
        out.append("MORPH %s %d %d" % (name, gi, len(d)))
        for k in d:
            dp = sP[k] - P[k]; dn = sN[k] - N[k]
            out.append("%d %.6f %.6f %.6f %.5f %.5f %.5f" % (k, *dp, *dn))
        print("MORPH %s on group %d, %d verts" % (name, gi, len(d)))
    open(SKIN, "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
