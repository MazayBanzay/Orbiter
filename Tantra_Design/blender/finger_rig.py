"""Coverall figure: real finger bones (2026-10-05). The 31-bone CMU rig carries all four fingers on one bone
(<side>HandFinger1) and the thumb on one (<side>Thumb), so a hand could only be shaped by morphs. This adds three
phalanges per finger and two more thumb bones per hand, with the MakeHuman 'default' rig's skin weights (CC0,
MPFB data/rigs/standard/weights.default.json) - the same base mesh the body is, so its weights fit vertex for vertex.

Run after hand_shapes.py / grip_shapes.py; it rewrites Config\\Tantra\\Astronavigator.skin in place (the mesh is
not touched):
  BONES 31 -> 59: new bones after the CMU ones (clips keep 31 bones; OrbiterCrew poses the rest from their parents)
    <side>F<k>_<j>  k = 2 index .. 5 little, j = 1 proximal .. 3 distal; parent of j=1 is <side>HandFinger1
    <side>T_2, <side>T_3  the thumb's middle and tip; parent <side>Thumb
  rest frames: y along the phalanx, z towards the palm (the side the relaxed fingers curl to), x = y cross z: a turn
    about +x curls the finger into the palm
  weights: what the vertex had on HandFinger1 (Thumb) is shared out over the phalanges as the default rig shares it;
    the knuckle's blend with the palm stays as it was
The exported vertices are told apart by their texture coordinates: the MakeHuman base mesh's UV map (base.obj vt).
System Python + numpy. --dry: report only; --out <file>: write there instead.
"""
import os, sys, json
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE); O = os.path.dirname(ROOT)
MSH = os.path.join(O, "Meshes", "Tantra", "AstronavigatorSkin.msh")
SKIN = os.path.join(O, "Config", "Tantra", "Astronavigator.skin")
BASE = os.path.join(ROOT, "bx", "mpfb", "data", "3dobjs", "base.obj")
WTS = os.path.join(ROOT, "bx", "mpfb", "data", "rigs", "standard", "weights.default.json")
DRY = "--dry" in sys.argv
OUT = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else SKIN   # e.g. a staging copy while Orbiter runs


def read_msh_group(path, label):
    L = open(path).read().split("\n")
    lab = None
    for i, l in enumerate(L):
        t = l.split()
        if t and t[0] == "LABEL": lab = t[1]
        if t and t[0] == "GEOM" and lab == label:
            nv = int(t[1])
            return np.array([list(map(float, L[i + 1 + k].split())) for k in range(nv)])
    raise SystemExit("no group " + label)


def read_skin(path):
    L = open(path).read().split("\n")
    i = 0; bones = []; groups = {}; bones_at = None
    while i < len(L):
        t = L[i].split()
        if t and t[0] == "BONES":
            bones_at = i
            for k in range(int(t[1])):
                u = L[i + 1 + k].split(); bones.append([u[0], int(u[1])] + list(map(float, u[2:14])))
            i += int(t[1])
        elif t and t[0] == "GROUP":
            gi, nv = int(t[1]), int(t[2]); groups[gi] = (i + 1, nv); i += nv
        i += 1
    return L, bones, bones_at, groups


def uv_to_basevert(path):
    vt, m = [], {}
    for l in open(path):
        if l.startswith("vt "): vt.append(tuple(map(float, l.split()[1:3])))
        elif l.startswith("f "):
            for c in l.split()[1:]:
                a = c.split("/"); v = int(a[0]) - 1; u = vt[int(a[1]) - 1]
                m.setdefault((round(u[0], 4), round(1 - u[1], 4)), set()).add(v)
    return m


def main():
    V = read_msh_group(MSH, "Skin")
    P, N, UV = V[:, 0:3], V[:, 3:6], V[:, 6:8]
    L, bones, bones_at, groups = read_skin(SKIN)
    names = [b[0] for b in bones]
    if len(bones) != 31: raise SystemExit("skin has %d bones (not the CMU 31) - already rigged? nothing done" % len(bones))
    start, nv = groups[0]
    assert nv == len(P), (nv, len(P))
    W = np.array([list(map(float, L[start + k].split())) for k in range(nv)])
    Bi, Bw = W[:, :4].astype(int), W[:, 4:].copy()
    uvm = uv_to_basevert(BASE)
    wj = json.load(open(WTS))["weights"]
    # base-mesh vertex of every exported vertex (by UV; the fingers are all that matter here)
    base = np.full(nv, -1)
    amb = miss = 0
    for k in range(nv):
        c = uvm.get((round(UV[k, 0], 4), round(UV[k, 1], 4)))
        if not c: miss += 1; continue
        if len(c) > 1: amb += 1
        base[k] = min(c)
    print("UV match: %d of %d (%d missing, %d ambiguous)" % ((base >= 0).sum(), nv, miss, amb))

    new_bones, new_w = [], {}          # name -> per-vertex weight
    for side, s, cmu_f, cmu_t in (("Left", "L", "LeftHandFinger1", "LThumb"), ("Right", "R", "RightHandFinger1", "RThumb")):
        kf, kt = names.index(cmu_f), names.index(cmu_t)
        def dflt(bn):
            w = np.zeros(nv); d = dict((int(a), float(b)) for a, b in wj.get(bn, []))
            ok = base >= 0
            w[ok] = [d.get(int(b), 0.0) for b in base[ok]]
            return w
        hand = np.where((P[:, 0] < 0) == (s == "L"))[0]
        okh = hand[base[hand] >= 0]; noh = hand[base[hand] < 0]
        near = okh[np.argmin(((P[noh][:, None, :] - P[okh][None, :, :]) ** 2).sum(2), 1)] if len(noh) else noh
        def fill(w):   # vertices without a UV match (split normals at creases) take their nearest matched neighbour's
            w = w.copy(); w[noh] = w[near]; return w
        wf = ((Bi == kf) * Bw).sum(1); wt = ((Bi == kt) * Bw).sum(1)
        ph = {(k, j): fill(dflt("finger%d-%d.%s" % (k, j, s))) for k in range(2, 6) for j in (1, 2, 3)}
        th = {j: fill(dflt("finger1-%d.%s" % (j, s))) for j in (1, 2, 3)}
        mc = {k: fill(dflt("metacarpal%d.%s" % (k - 1, s))) for k in range(2, 6)}
        tot = sum(ph.values()); ttot = sum(th.values())
        fin = np.where(wf > 1e-4)[0]
        print("%s: %d verts on %s, %d of them with phalanx weights; thumb %d verts" % (side, len(fin), cmu_f, ((tot > 1e-4) & (wf > 1e-4)).sum(), (wt > 1e-4).sum()))
        # the hand's frame, from the CMU bones: f wrist->knuckles, palm normal from the relaxed curl (as Skin::FindHands)
        hb = names.index(side + "Hand")
        wrist = np.array(bones[hb][11:14]); knu = np.array(bones[kf][11:14])
        f = knu - wrist; f /= np.linalg.norm(f)
        # joints: the middle of each blend ring between a phalanx and the next one (the knuckle: phalanx 1 and the palm)
        def ring(a, b):
            m = np.minimum(a, b); m = m * (m > 0.05)
            return (P * m[:, None]).sum(0) / m.sum() if m.sum() > 1e-3 else None
        chains = {}
        for k in range(2, 6):
            w1, w2, w3 = ph[(k, 1)], ph[(k, 2)], ph[(k, 3)]
            j1 = ring(w1, mc[k]); j2 = ring(w1, w2); j3 = ring(w2, w3)   # j1, the knuckle: the ring between the metacarpal and phalanx 1
            m3 = np.where(w3 > 0.5)[0]
            ax3 = (j3 - j2) / np.linalg.norm(j3 - j2)
            tip = P[m3[np.argsort(-((P[m3] - j3) @ ax3))[:max(4, len(m3) // 10)]]].mean(0)
            chains[k] = [j1, j2, j3, tip]
        # palm side: the curl of the relaxed middle finger (tip vs the proximal line)
        j1, j2, j3, tip = chains[3]
        prox = (j2 - j1) / np.linalg.norm(j2 - j1)
        z_hand = (tip - j2) - prox * ((tip - j2) @ prox); z_hand /= np.linalg.norm(z_hand)
        for k in range(2, 6):
            pts = chains[k]
            for j in (1, 2, 3):
                h, t_ = pts[j - 1], pts[j]
                y = (t_ - h) / np.linalg.norm(t_ - h)
                z = z_hand - y * (z_hand @ y); z /= np.linalg.norm(z)
                x = np.cross(y, z)
                par = cmu_f if j == 1 else "%sF%d_%d" % (s, k, j - 1)
                new_bones.append(("%sF%d_%d" % (s, k, j), par, np.stack([x, y, z], 1), h, np.linalg.norm(t_ - h)))
        # finger weights: HandFinger1's share goes to the phalanges in the default rig's proportions
        sh = np.where(tot > 1e-6, 1.0, 0.0)
        for k in range(2, 6):
            for j in (1, 2, 3):
                new_w["%sF%d_%d" % (s, k, j)] = wf * sh * ph[(k, j)] / np.maximum(tot, 1e-9)
        new_w[cmu_f] = wf * (1 - sh)
        # thumb: Thumb keeps finger1-1's share, T_2 and T_3 take the rest
        tsh = np.where(ttot > 1e-6, 1.0, 0.0)
        new_w[cmu_t] = wt * (1 - tsh) + wt * tsh * th[1] / np.maximum(ttot, 1e-9)
        for j in (2, 3): new_w["%sT_%d" % (s, j)] = wt * tsh * th[j] / np.maximum(ttot, 1e-9)
        T = {}
        for j in (2, 3):
            T[j] = ring(th[j - 1], th[j])
        mt = np.where(th[3] > 0.5)[0]
        axt = (T[3] - T[2]) / np.linalg.norm(T[3] - T[2])
        ttip = P[mt[np.argsort(-((P[mt] - T[3]) @ axt))[:max(4, len(mt) // 10)]]].mean(0)
        tpts = [T[2], T[3], ttip]
        # thumb flexion: across the thumb, towards the palm's middle (the thumb closes over the fingers)
        palm_c = (chains[3][0] + wrist) / 2
        for j in (2, 3):
            h, t_ = tpts[j - 2], tpts[j - 1]
            y = (t_ - h) / np.linalg.norm(t_ - h)
            z = palm_c - h; z = z - y * (z @ y); z /= np.linalg.norm(z)
            x = np.cross(y, z)
            par = cmu_t if j == 2 else "%sT_2" % s
            new_bones.append(("%sT_%d" % (s, j), par, np.stack([x, y, z], 1), h, np.linalg.norm(t_ - h)))
        for k in range(2, 6):
            print("  finger %d joints %s  lengths %s mm" % (k, " ".join("(%.3f %.3f %.3f)" % tuple(p) for p in chains[k]),
                  " ".join("%.0f" % (1000 * np.linalg.norm(chains[k][i + 1] - chains[k][i])) for i in range(3))))
        print("  thumb joints %s" % " ".join("(%.3f %.3f %.3f)" % tuple(p) for p in tpts))
        print("  palm side z %s" % np.round(z_hand, 3))
    if DRY: return
    # the new weights: per vertex, CMU weights with the finger/thumb bones' share split; the 4 largest kept
    allnames = names + [b[0] for b in new_bones]
    idx = {n: i for i, n in enumerate(allnames)}
    split = {idx[n]: w for n, w in new_w.items()}
    touched = set(idx[n] for n in new_w if n in names)
    out_rows = []
    for v in range(nv):
        d = {}
        for k in range(4):
            b, w = int(Bi[v, k]), float(Bw[v, k])
            if w <= 0 or b in touched: continue
            d[b] = d.get(b, 0.0) + w
        for b, w in split.items():
            if w[v] > 1e-4: d[b] = d.get(b, 0.0) + float(w[v])
        ws = sorted(d.items(), key=lambda t: -t[1])[:4] or [(int(Bi[v, 0]), 1.0)]
        sm = sum(w for _, w in ws); ws = [(b, w / sm) for b, w in ws] + [(0, 0.0)] * (4 - len(ws))
        out_rows.append("%d %d %d %d %.4f %.4f %.4f %.4f" % (*[b for b, _ in ws], *[w for _, w in ws]))
    L[start:start + nv] = out_rows
    blines = []
    for n, par, R, h, ln in new_bones:
        blines.append("%s %d %s %.6f %.6f %.6f" % (n, idx[par], " ".join("%.6f" % R[i][j] for i in range(3) for j in range(3)), *h))
    L[bones_at] = "BONES %d" % len(allnames)
    L[bones_at + 1 + 31:bones_at + 1 + 31] = blines
    open(OUT, "w", newline="\n").write("\n".join(L))
    print("wrote %s: %d bones (+%d)" % (OUT, len(allnames), len(new_bones)))


if __name__ == "__main__":
    main()
