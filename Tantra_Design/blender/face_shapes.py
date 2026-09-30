"""Coverall figure: a blink. Run after hand_shapes.py (after every export_skin.py of the coverall).

Adds MORPH "blink" to Config\\Tantra\\Astronavigator.skin for the Skin group (upper eyelids) and the Lashes group.
The upper lid turns down over the eyeball about the horizontal axis through the eye's centre - the way a real lid
slides over the eye - fully at the lid margin, fading out towards the brow; the lower lid rises a little.
Eye centre and radius come from the eyeball mesh (group Eyes). OrbiterCrew fires it every few seconds.
"""
import os
import numpy as np

O = r"C:\Games\Orbiter 2016"
MSH = os.path.join(O, "Meshes", "Tantra", "AstronavigatorSkin.msh")
SKIN = os.path.join(O, "Config", "Tantra", "Astronavigator.skin")
CLOSE_UP = np.radians(float(os.environ.get("TANTRA_BLINK_UP", "42")))    # upper lid travel
CLOSE_LO = np.radians(float(os.environ.get("TANTRA_BLINK_LO", "6")))     # lower lid travel


def read_groups(path):
    lines = open(path).read().split("\n"); out = {}; label = None; i = 0; gi = -1
    while i < len(lines):
        t = lines[i].split()
        if t and t[0] == "LABEL": label = t[1]
        if t and t[0] == "GEOM":
            gi += 1; nv = int(t[1])
            V = np.array([list(map(float, lines[i + 1 + k].split()[:6])) for k in range(nv)])
            out[label] = (gi, V); label = None; i += nv
        i += 1
    return out


def rot_x(P, N, c, ang):
    """rotate about the lateral (x) axis through c by ang (per point); + tips the top forward (towards +z)"""
    ca, sa = np.cos(ang), np.sin(ang)
    y, z = P[:, 1] - c[1], P[:, 2] - c[2]
    P2 = P.copy(); P2[:, 1] = c[1] + ca * y - sa * z; P2[:, 2] = c[2] + sa * y + ca * z
    N2 = N.copy(); N2[:, 1] = ca * N[:, 1] - sa * N[:, 2]; N2[:, 2] = sa * N[:, 1] + ca * N[:, 2]
    return P2, N2


def main():
    skin_txt = open(SKIN).read()
    if "MORPH blink" in skin_txt:
        print("blink already present - run export_skin.py (+ hand_shapes.py) first; nothing done"); return
    G = read_groups(MSH)
    eyes = G["Eyes"][1][:, :3]
    out = []
    for side in (-1, 1):   # Orbiter x: -1 left eye, +1 right eye
        E = eyes[np.sign(eyes[:, 0]) == side]
        c = E.mean(0); R = min(np.percentile(np.linalg.norm(E - c, axis=1), 90), 0.0125)   # the eyes mesh overstates the ball
        for label in ("Skin", "Lashes"):
            gi, V = G[label]
            P, N = V[:, :3], V[:, 3:6]
            d = P - c; r = np.linalg.norm(d, axis=1)
            near = (np.sign(P[:, 0]) == side) & (d[:, 2] > -0.2 * R) & (r < R + 0.011) & (np.abs(d[:, 0]) < 0.022)
            elev = np.arctan2(d[:, 1], d[:, 2])                  # angle above the eye's forward axis
            w = np.clip((R + 0.011 - r) / 0.007, 0, 1); w = w * w * (3 - 2 * w)   # full at the lid, nothing at the brow
            up = near & (elev > np.radians(-5)) & (elev < np.radians(60))
            lo = near & (elev < np.radians(-12)) & (label == "Skin")
            ang = np.zeros(len(P)); ang[up] = CLOSE_UP * w[up]; ang[lo] = -CLOSE_LO * w[lo]
            if label == "Lashes": ang[up] = CLOSE_UP                 # lashes ride the lid margin
            idx = np.where(ang != 0)[0]
            if not len(idx): continue
            P2, N2 = rot_x(P[idx], N[idx], c, ang[idx])
            for k, p2, n2 in zip(idx, P2, N2):
                out.append((gi, k, p2 - P[k], n2 - N[k]))
        print("eye %s: centre %s, radius %.1f mm" % ("L" if side < 0 else "R", np.round(c, 3), 1000 * R))
    by_group = {}
    for gi, k, dp, dn in out: by_group.setdefault(gi, []).append((k, dp, dn))
    lines = skin_txt.rstrip("\n").split("\n")
    for gi, rows in sorted(by_group.items()):
        lines.append("MORPH blink %d %d" % (gi, len(rows)))
        for k, dp, dn in rows: lines.append("%d %.6f %.6f %.6f %.5f %.5f %.5f" % (k, *dp, *dn))
    open(SKIN, "w").write("\n".join(lines) + "\n")
    print("MORPH blink: %s" % {gi: len(r) for gi, r in by_group.items()})


if __name__ == "__main__":
    main()
