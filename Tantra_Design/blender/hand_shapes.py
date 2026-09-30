"""Coverall figure: shape the hands, which the 31-bone rig cannot (one bone carries all four fingers).

Run after export_skin.py (+ validate_skin.py --ground); it rewrites the exported files in place:
  Meshes\\Tantra\\AstronavigatorSkin.msh   the hands of the Skin group get a relaxed shape baked in:
                                          fingers together, softly curled (the MakeHuman bind hand is spread flat)
  Config\\Tantra\\Astronavigator.skin      gets a MORPH "fist" (position + normal deltas from the relaxed hand),
                                          which OrbiterCrew blends in while running
Fingers bend at three joints each (knuckle, middle, tip), applied tip first like a real finger chain.
The four fingers are told apart by their position along the knuckle line. System Python + numpy.
"""
import os
import numpy as np

O = r"C:\Games\Orbiter 2016"
MSH = os.path.join(O, "Meshes", "Tantra", "AstronavigatorSkin.msh")
SKIN = os.path.join(O, "Config", "Tantra", "Astronavigator.skin")

RELAXED = dict(mcp=22, pip=38, dip=22, adduct=1.0)   # fingers together, softly curled - never a spread fan
FIST = dict(mcp=92, pip=105, dip=70, adduct=0.95, thumb=-65)   # thumb: negative folds it across the fingers (35 stuck up)
JOINTS = (("dip", 0.74), ("pip", 0.45), ("mcp", 0.0))   # fraction of finger length from the knuckle, tip first


def read_msh(path):
    lines = open(path).read().split("\n")
    groups, i = [], 0
    label = None
    while i < len(lines):
        t = lines[i].split()
        if t and t[0] == "LABEL": label = t[1]
        if t and t[0] == "GEOM":
            nv = int(t[1]); groups.append((label, i + 1, nv)); label = None; i += nv
        i += 1
    return lines, groups


def read_skin(path):
    L = open(path).read().split("\n")
    names, W, i = [], [], 0
    while i < len(L):
        t = L[i].split()
        if t and t[0] == "MORPH": break
        if t and t[0] == "BONES":
            for k in range(int(t[1])): names.append(L[i + 1 + k].split()[0])
            i += int(t[1])
        elif t and t[0] == "GROUP":
            nv = int(t[2]); W.append(np.array([list(map(float, L[i + 1 + k].split())) for k in range(nv)])); i += nv
        i += 1
    return L, names, W


def rotate(P, N, pivot, axis, ang, mask):
    """rotate the masked rows of P (points) and N (normals) about the line (pivot, axis) by ang*mask"""
    a = axis / np.linalg.norm(axis)
    th = ang * mask[:, None]
    c, s = np.cos(th), np.sin(th)
    def rot(V):
        return V * c + np.cross(a, V) * s + a * (V @ a)[:, None] * (1 - c)
    return pivot + rot(P - pivot), rot(N)


def shape(P, N, wf, info, spec):
    """P, N: finger vertices; wf: finger-bone weight (fades the shape towards the palm)"""
    K, f, p, a, L, lat, cl, centres = info
    P2, N2 = P.copy(), N.copy()
    # fingers together: each finger moves towards the middle of the hand, more towards the tips
    along = (P - K) @ f
    s = np.clip(along / L, 0, 1)
    # the bind hand is a fan: each finger diverges from the knuckles at its own angle. Fit that angle (lateral
    # offset per metre along the finger) for each finger and take it out, so the fingers lie side by side
    slopes = np.zeros(4); offs = np.zeros(4)
    for k in range(4):
        m_ = cl == k
        if m_.sum() > 5:
            A_ = np.stack([along[m_], np.ones(m_.sum())], 1)
            slopes[k], offs[k] = np.linalg.lstsq(A_, lat[m_], rcond=None)[0]
    target = np.linspace(-0.0075, 0.0075, 4) * 3 / 3.0      # fingers ~15 mm apart at the tips (they touch)
    tgt_slope = (target - offs) / max(L, 1e-3)
    shift = (tgt_slope[cl] - slopes[cl]) * np.clip(along, 0, None) * spec["adduct"]
    P2 = P2 + a * shift[:, None]
    # curl, tip joint first; the pivot runs through the middle of the finger's thickness
    for j, frac in JOINTS:
        x0 = frac * L
        m = np.clip((along - (x0 - 0.006)) / 0.012, 0, 1); m = m * m * (3 - 2 * m)
        near = np.abs(along - x0) < 0.012
        depth = ((P[near] - K) @ p).mean() if near.any() else 0.0
        pivot = K + f * x0 + p * depth
        P2, N2 = rotate(P2, N2, pivot, a, np.radians(spec[j]), m)
    P2 = P + (P2 - P) * wf[:, None]
    N2 = N + (N2 - N) * wf[:, None]
    N2 /= np.linalg.norm(N2, axis=1, keepdims=True) + 1e-12
    return P2, N2


def main():
    lines, groups = read_msh(MSH)
    skin_lines, names, W = read_skin(SKIN)
    if any(l.startswith("MORPH") for l in skin_lines):
        print("already shaped (MORPH present) - run export_skin.py first; nothing done"); return
    gi = next(i for i, g in enumerate(groups) if g[0] == "Skin")
    _, start, nv = groups[gi]
    V = np.array([list(map(float, lines[start + k].split())) for k in range(nv)])
    P, N = V[:, 0:3].copy(), V[:, 3:6].copy()
    w = W[gi]
    def weight(bone):
        b = names.index(bone); return ((w[:, :4] == b) * w[:, 4:]).sum(1)
    relaxed_P, relaxed_N = P.copy(), N.copy()
    fist_P, fist_N = P.copy(), N.copy()
    for side, th in (("Left", "LThumb"), ("Right", "RThumb")):
        wf = weight(side + "HandFinger1")
        idx = np.where(wf > 0.15)[0]
        F = P[idx]
        wrist_d = np.linalg.norm(F - P[np.argmax(weight(side + "Hand"))], axis=1)
        order = np.argsort(wrist_d)
        K = F[order[:max(8, len(F) // 7)]].mean(0); tips = F[order[-max(8, len(F) // 10):]].mean(0)
        f = tips - K; f /= np.linalg.norm(f)
        # palm normal = the thinnest direction of the finger block (PCA), turned towards the body's centre line:
        # the bind hand is rotated ~45 deg, so assuming 'palm faces +-x' curled the fingers sideways into a fan
        _, _, vt = np.linalg.svd(F - F.mean(0), full_matrices=False); p = vt[2].copy()
        if p[0] * -np.sign(K[0]) < 0: p = -p
        p -= (p @ f) * f; p /= np.linalg.norm(p)
        a = np.cross(f, p); a /= np.linalg.norm(a)
        L = ((F - K) @ f).max()
        lat = (F - K) @ a
        centres = np.quantile(lat, [0.125, 0.375, 0.625, 0.875])
        for _ in range(20):   # 1-D k-means into four fingers
            cl = np.argmin(np.abs(lat[:, None] - centres[None, :]), 1)
            centres = np.array([lat[cl == k].mean() if (cl == k).any() else centres[k] for k in range(4)])
        info = (K, f, p, a, L, lat, cl, centres)
        rP, rN = shape(F, N[idx], wf[idx], info, RELAXED)
        fP, fN = shape(F, N[idx], wf[idx], info, FIST)
        relaxed_P[idx], relaxed_N[idx] = rP, rN
        fist_P[idx], fist_N[idx] = fP, fN
        # thumb: folds across the front of the fingers in the fist
        wt = weight(th); tidx = np.where(wt > 0.3)[0]
        if len(tidx):
            T = P[tidx]; tb = T[np.argsort(np.linalg.norm(T - K, axis=1))[:5]].mean(0)
            tP, tN = rotate(T, N[tidx], tb, f, np.radians(FIST["thumb"]) * (1 if side == "Left" else -1), wt[tidx])
            fist_P[tidx], fist_N[tidx] = tP, tN
        print("%s hand: %d finger verts, finger length %.3f m, finger centres %s" % (side, len(idx), L, np.round(centres, 3)))
    # bake relaxed into the mesh
    for k in range(nv):
        t = lines[start + k].split()
        t[0:6] = ["%.6f" % v for v in (*relaxed_P[k], *relaxed_N[k])]
        lines[start + k] = " ".join(t)
    open(MSH, "w").write("\n".join(lines))
    # fist morph, relative to the relaxed hand
    d = np.where(np.linalg.norm(fist_P - relaxed_P, axis=1) > 1e-5)[0]
    out = [l for l in skin_lines]
    while out and not out[-1].strip(): out.pop()
    cut = next((i for i, l in enumerate(out) if l.startswith("MORPH")), len(out))
    out = out[:cut]
    out.append("MORPH fist %d %d" % (gi, len(d)))
    for k in d:
        dp = fist_P[k] - relaxed_P[k]; dn = fist_N[k] - relaxed_N[k]
        out.append("%d %.6f %.6f %.6f %.5f %.5f %.5f" % (k, *dp, *dn))
    open(SKIN, "w").write("\n".join(out) + "\n")
    print("baked relaxed hands; MORPH fist on group %d, %d verts" % (gi, len(d)))


if __name__ == "__main__":
    main()
