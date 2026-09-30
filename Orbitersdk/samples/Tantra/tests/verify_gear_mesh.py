"""Numeric checks of the gear geometry in the rig poses (same transforms as Orbiter): python tests/verify_gear_mesh.py"""
import math
import sys

import numpy as np

sys.path.insert(0, __import__("os").path.join(__import__("os").path.dirname(__import__("os").path.abspath(__file__)), "..", "tools"))
import gen_mesh as gm  # noqa: E402

groups, legs = gm.build()
comps = gm.rig(legs)
P = {n: (st, pitch, lift) for n, st, pitch, lift, _ in gm.preview_poses(legs)}
G = {g.name: g for g in groups}
fails = 0


def check(ok, msg):
    global fails
    print(("ok   " if ok else "FAIL ") + msg)
    fails += 0 if ok else 1


def skin_margin(p):
    """Positive = inside the hull outline (m), at the vertex station."""
    s = p[2] - gm.STERN_Z
    w, top, bot = gm.section(gm.HULL_KEYS, s)
    b = top if p[1] >= 0 else abs(bot)
    # ellipse-like outline used by loft: x = w cos t, y = b sin t
    r = math.hypot(p[0] / w, p[1] / b)
    return (1 - r) * min(w, b)


def pose(name):
    st, pitch, lift = P[name]
    return gm.apply_pose(groups, comps, st)


# 1. Flight: everything of the gear under the skin, carriage legs clear of the trap columns.
V = pose("flight (stowed)")
for side in ("port", "starboard"):
    parts = [f"hip_{side}", f"thigh_{side}", f"ankle_{side}", f"pad_{side}"] + [f"shin_{side}_{i}" for i in range(gm.SHIN_N)]
    pts = np.vstack([V[n] for n in parts])
    m = min(skin_margin(p) for p in pts)
    xin = min(abs(p[0]) for p in np.vstack([V[n] for n in parts[1:]]))
    s_rng = (pts[:, 2].min() - gm.STERN_Z, pts[:, 2].max() - gm.STERN_Z)
    ymax = np.abs(pts[:, 1]).max()
    check(m > -0.02, f"{side} leg stowed: min margin to skin {m:.2f} m (pad corners may sit proud <= 0.45)")
    check(xin >= 10.5, f"{side} leg stowed: inner face |x| {xin:.2f} m (trap columns end at 10.45)")
    check(gm.POCKET_S0 - 0.1 <= s_rng[0] and s_rng[1] <= gm.POCKET_S1 + 0.1, f"{side} leg stowed: s {s_rng[0]:.1f}..{s_rng[1]:.1f} in pocket {gm.POCKET_S0}..{gm.POCKET_S1}")
    check(ymax <= 3.5 * 1.0 + 0.3 + (gm.HIP_R - 3.5 if gm.HIP_R > 3.5 else 0), f"{side} leg stowed: |y| max {ymax:.2f} m (pocket +-3.6)")
for i in range(4):
    pts = np.vstack([V[f"leg{i}_{p}"] for p in ("thigh", "shin0", "shin1", "shin2", "ankle", "pad")])
    m = min(skin_margin(p) for p in pts)
    check(m > -0.2, f"stern leg {i} stowed: min margin to skin {m:.2f} m")
    s_rng = (pts[:, 2].min() - gm.STERN_Z, pts[:, 2].max() - gm.STERN_Z)
    check(gm.CORNER_S0 - 0.2 <= s_rng[0] and s_rng[1] <= gm.CORNER_S1 + 0.2, f"stern leg {i} stowed: s {s_rng[0]:.1f}..{s_rng[1]:.1f}")

# 2. Resting level: pads on the ground, nothing else below it.
V = pose("resting level")
gy = -gm.AXIS_H
for side in ("port", "starboard"):
    y = V[f"pad_{side}"][:, 1].min()
    check(abs(y - gy) < 0.02, f"{side} pad on the ground at rest: bottom {y:.3f} (ground {gy})")
for i, L in enumerate(legs):
    if L["lower"]:
        y = V[f"leg{i}_pad"][:, 1].min()
        check(abs(y - gy) < 0.05, f"stern leg {i} pad on the ground at rest: bottom {y:.3f}")
low = min(V[g.name][:, 1].min() for g in groups if not g.name.startswith(("pad_", "leg", "airlock_lift", "rover")))
check(low > gy + 0.3, f"lowest non-foot part at rest {low:.2f} (ground {gy})")

# 3. Turning 45 deg (ship pitched about the trunnion): pads flat on the ground.
st, pitch, lift = P["turning 45 deg"]
V = gm.apply_pose(groups, comps, st)
R = gm.rot([1, 0, 0], -math.radians(pitch))
for side in ("port", "starboard"):
    W = (R @ V[f"pad_{side}"].T).T + np.array([0, lift, 0])
    check(W[:, 1].max() - W[:, 1].min() < gm.PAD_OFF + gm.PAD_T + 0.05, f"{side} pad level while turning: height span {W[:, 1].max() - W[:, 1].min():.2f}")
    check(abs(W[:, 1].min() - gy) < 0.05, f"{side} pad on the ground while turning: {W[:, 1].min():.3f}")

# 4. Standing: stern pads on the ground plane (ship frame z = stand ground).
st, pitch, lift = P["standing"]
V = gm.apply_pose(groups, comps, st)
gz = gm.zs(gm.STAND_GROUND_S)
for i in range(4):
    z = V[f"leg{i}_pad"][:, 2]
    check(abs(z.min() - gz) < 0.05 and z.max() - z.min() < gm.PAD_OFF + gm.PAD_T + 0.05, f"stern leg {i} pad on the ground standing: {z.min():.2f}..{z.max():.2f} (ground {gz})")
lowest = min(V[g.name][:, 2].min() for g in groups if not g.name.startswith("leg"))
check(lowest > gz + 3.0, f"stern clearance standing: {lowest - gz:.1f} m")
print("ALL OK" if not fails else f"{fails} FAILED")
