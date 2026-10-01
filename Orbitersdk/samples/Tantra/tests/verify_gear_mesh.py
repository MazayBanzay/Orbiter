"""Numeric checks of the Tantra mesh and rig (C-148): flush stowage, clearances, sweeps through the
openings, ground contact in every pose. Run: python tests/verify_gear_mesh.py"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import gen_mesh as gm  # noqa: E402

groups, legs = gm.build()
comps = gm.rig(legs)
G = {g.name: g for g in groups}
fails = 0


def check(ok, msg):
    global fails
    print(("ok   " if ok else "FAIL ") + msg)
    fails += 0 if ok else 1


def samples(g, V, step=0.6):
    """Dense points on every triangle (barycentric grid, ~step m apart)."""
    t = np.array(g.t)
    A, B, C = V[t[:, 0]], V[t[:, 1]], V[t[:, 2]]
    L = np.maximum(np.linalg.norm(B - A, axis=1), np.maximum(np.linalg.norm(C - B, axis=1), np.linalg.norm(A - C, axis=1)))
    out = [V]
    for n in np.unique(np.clip(np.ceil(L / step), 1, 40).astype(int)):
        m = np.clip(np.ceil(L / step), 1, 40).astype(int) == n
        w = np.array([(i / n, j / n) for i in range(n + 1) for j in range(n + 1 - i)])
        a, b, c = A[m], B[m], C[m]
        out.append((a[:, None, :] + (b - a)[:, None, :] * w[None, :, :1] + (c - a)[:, None, :] * w[None, :, 1:]).reshape(-1, 3))
    return np.vstack(out)


def _seg_dist(S, P):
    """Signed distance (+ outside) and u of 2D points P to section S (vectorised)."""
    e = S.q - S.p
    ee = (e * e).sum(1)
    d_out = np.empty(len(P))
    u_out = np.empty(len(P))
    for k in range(0, len(P), 4000):
        Q = P[k:k + 4000]
        t = np.clip(((Q[:, None, :] - S.p[None]) * e[None]).sum(2) / ee[None], 0, 1)
        c = S.p[None] + e[None] * t[..., None]
        d2 = ((c - Q[:, None, :]) ** 2).sum(2)
        j = d2.argmin(1)
        d = np.sqrt(d2[np.arange(len(Q)), j])
        y = Q[:, 1:2]
        cross = (S.p[None, :, 1] > y) != (S.q[None, :, 1] > y)
        with np.errstate(divide="ignore", invalid="ignore"):
            xs = S.p[None, :, 0] + (y - S.p[None, :, 1]) * e[None, :, 0] / e[None, :, 1]
        inside = ((cross & (xs > Q[:, 0:1])).sum(1) % 2) == 1
        d_out[k:k + 4000] = np.where(inside, -d, d)
        u_out[k:k + 4000] = (S.cum[j] + t[np.arange(len(Q)), j] * (S.cum[j + 1] - S.cum[j])) / S.L
    return d_out, u_out


def skin(P):
    """(signed distance outside the skin, u, s) per point; nan beyond the hull."""
    s = P[:, 2] - gm.STERN_Z
    d = np.full(len(P), np.nan)
    u = np.full(len(P), np.nan)
    key = np.round(s * 4) / 4                    # T8: the section changes everywhere (stern clover, fairings, nose)
    ok = (s >= 0.0) & (s <= gm.NB)
    for k in np.unique(key[ok]):
        m = ok & (key == k)
        d[m], u[m] = _seg_dist(gm.sec(k), P[m, :2])
    return d, u, s


def outs(P):
    return skin(np.asarray(P))[0]


def in_opening(u, s, names, margin=0.12):
    ok = np.zeros(len(u), bool)
    for n in names:
        o = gm.OPEN[n] if isinstance(n, str) else n
        a = np.array([gm.ev(o["a"], x) for x in u]) if callable(o["a"]) else o["a"]
        b = np.array([gm.ev(o["b"], x) for x in u]) if callable(o["b"]) else o["b"]
        ok |= (u >= o["u"][0] - 0.004) & (u <= o["u"][1] + 0.004) & (s >= a - margin) & (s <= b + margin)
    return ok


STOW = dict(gm.stowed_states())
# Parts that stow inside the skin; the stern legs (on the nacelles) and the folded wings stow outside it, in the
# shadow of the body (checked separately).
MOVING = [n for n in gm.GROUPS if n.startswith(("fin", "door_", "pod_", "hip_", "thigh_", "ankle_", "pad_", "sole_", "anchor_", "shin_",
                                                 "bay_door"))]


def foot(V, pad):
    """A pad with its jamming sole (the sole is the ground face)."""
    return np.vstack([V[pad], V[pad.replace("pad", "sole")]])
OUTSIDE = [n for n in gm.GROUPS if n.startswith(("crest_", "wing_outer_", "elevon_")) or (n.startswith("leg") and n != "leg_hinges")]

# ---- A. flight at 0.9 c: every moving part inside the skin, covers flush
V = gm.apply_pose(groups, comps, STOW)
worst = []
for n in MOVING:
    d = outs(samples(G[n], V[n]))
    worst.append((np.nanmax(d), n))
worst.sort(reverse=True)
check(worst[0][0] <= 0.03, "stowed: nothing proud of the skin (worst " + ", ".join(f"{n} {w:+.3f}" for w, n in worst[:4]) + ")")
for n, lim in (("sole_starboard", 0.21), ("sole_port", 0.21), ("hip_starboard", 0.06),
               ("door_pod_0", 0.03), ("door_top_port", 0.03), ("bay_door_starboard", 0.03)):
    m = np.nanmax(outs(V[n]))
    check(-lim <= m <= 0.03, f"stowed {n}: outer face flush ({m:+.3f} m)")
# sub-light: everything outside the skin lies in the shadow of the widest/tallest body sections
W_MAX, Y_TOP, Y_BOT = gm.wh_at(60.0)[0], (1 - gm.FL) * gm.wh_at(60.0)[1], -gm.FL * gm.wh_at(60.0)[1]
for n in OUTSIDE:
    P = V[n]
    ok = (np.abs(P[:, 0]).max() <= W_MAX + 0.05) and (P[:, 1].max() <= Y_TOP + 0.35) and (P[:, 1].min() >= Y_BOT - 1.7)
    check(ok, f"stowed {n} in the body shadow: |x| {np.abs(P[:, 0]).max():.2f} (<= {W_MAX:.2f}), y {P[:, 1].min():.2f}..{P[:, 1].max():.2f}")
for n in OUTSIDE:
    d = np.nanmin(outs(samples(G[n], V[n])))
    check(d > -0.03, f"stowed {n} does not enter the hull (deepest {d:+.2f} m)")
V_fin = np.vstack([V["fin"], V["fin_upper"]])
check(V_fin[:, 1].max() <= gm.top_y(30) + 0.01 and V_fin[:, 1].min() > -9.0,
      f"fin retracted: y {V_fin[:, 1].min():.1f}..{V_fin[:, 1].max():.2f} (skin {gm.top_y(30):.2f})")


# ---- B. stowed parts vs trap cassettes and lift masts
def in_trap(P):
    hit = np.zeros(len(P), bool)
    s = P[:, 2] - gm.STERN_Z
    for cx, cy in gm.TRAP_XY:
        x, y = np.abs(P[:, 0] - cx), np.abs(P[:, 1] - cy)
        h = gm.CASS_W / 2
        ins = (x < h - 0.02) & (y < h - 0.02) & (x + y < 2 * h - gm.CASS_CH - 0.02)
        hit |= (s > gm.CASS_S0 - 0.1) & (s < gm.CASS_S1 + 0.1) & ins
    return hit


nt = 0
for n in MOVING:
    k = in_trap(samples(G[n], V[n])).sum()
    if k:
        check(False, f"stowed {n}: {k} points inside a trap cassette")
        nt += 1
check(nt == 0, "stowed parts clear of the trap cassettes")
# (T8: no stern-leg pockets in the skin - the legs lie on the nacelles)


# ---- C. sweeps: parts pass through their own openings only
def sweep(name, groups_, allowed, seq, n=24):
    """The parts may cross the skin only inside their own openings; deeper inside they must miss the traps."""
    bad, badt = 0, 0
    for t in np.linspace(0, 1, n + 1):
        W = gm.apply_pose(groups, comps, seq(t))
        for gname in groups_:
            P = samples(G[gname], W[gname])
            d, u, s = skin(P)
            near = (d < -0.03) & (d > -0.6)
            bad += int((near & ~in_opening(u, s, allowed)).sum())
            badt += int(in_trap(P).sum())
    check(bad == 0 and badt == 0, f"sweep {name}: {bad} points cross the skin outside the opening, {badt} inside a trap cassette")


for side in gm.SIDES:
    parts = [f"hip_{side}", f"thigh_{side}", f"ankle_{side}", f"pad_{side}", f"sole_{side}", f"anchor_{side}"] + \
        [f"shin_{side}_{i}" for i in range(gm.SHIN_N)]
    slot = dict(gm.OPEN[f"carriage_{side}"], a=gm.PIN_SLOT[0] - 0.5, b=gm.PIN_SLOT[1], depth=4.0)
    sweep(f"carriage leg {side} slide out", parts, [f"carriage_{side}", slot],
          lambda t, s=side: dict(STOW, **{f"slide_{s}": 1 - t}))
for i, L in enumerate(legs):
    ph = L["phi_stand"] / L["phi_max"]
    ex = L["e_stand"] / gm.LEG_EXT_MAX
    parts = [f"leg{i}_{p}" for p in ["thigh"] + [f"shin{k}" for k in range(gm.LEG_SHIN_N)] + ["ankle", "pad", "sole", "anchor"]]
    sweep(f"stern leg {i} to stand", parts, [],
          lambda t, i=i, ph=ph, ex=ex: dict(STOW, **{f"leg{i}_swing": ph * t, f"leg{i}_ext": ex * min(1, max(0, (t - gm.LEG_EXT_DELAY) / (1 - gm.LEG_EXT_DELAY))),
                                                    f"leg{i}_foot_stand": t}))
    if L["lower"]:
        pr, er = L["phi_rest"] / L["phi_max"], L["e_rest"] / gm.LEG_EXT_MAX
        sweep(f"stern leg {i} to rest", parts, [],
              lambda t, i=i, pr=pr, er=er: dict(STOW, **{f"leg{i}_swing": pr * t, f"leg{i}_ext": er * min(1, max(0, (t - gm.LEG_EXT_DELAY) / (1 - gm.LEG_EXT_DELAY))), f"leg{i}_foot_rest": t}))
for i in range(4):
    sweep(f"pod {i} swing out", [f"door_pod_{i}", f"pod_{i}"], [f"pod_{i}"], lambda t: dict(STOW, pod_retract=1 - t))
sweep("pods swivel", [f"pod_{i}" for i in range(4)], [], lambda t: dict(STOW, pod_retract=0, pod_swivel=t), n=10)
WINGS = ["crest_starboard", "crest_port", "wing_outer_starboard", "wing_outer_port", "elevon_starboard", "elevon_port"]
sweep("wings unfold (outer out, then inner down)", WINGS, [],
      lambda t: dict(STOW, wing_outer=max(0.0, 1 - 2 * t), crest_lateral=min(1.0, 2 - 2 * t)))
sweep("wings 30 deg", WINGS, [], lambda t: dict(STOW, crest_lateral=t * gm.WING_RAISE / gm.WING_FOLD, wing_outer=0), n=10)
sweep("fin extends", ["fin", "fin_upper"], ["fin_slot"], lambda t: dict(STOW, crest_dorsal=1 - t))
sweep("elevons and body flap", ["elevon_starboard", "elevon_port", "body_flap"], [],
      lambda t: {"elevon_starboard": t, "elevon_port": 1 - t, "body_flap": t}, n=10)

# ---- D. resting level on six feet, pods out with the cups down
P = {n: (st, pitch, lift) for n, st, pitch, lift, _ in gm.preview_poses(legs)}
st, pitch, lift = P["resting level"]
V = gm.apply_pose(groups, comps, st)
gy = -gm.AXIS_H
for side in gm.SIDES:
    y = foot(V, f"pad_{side}")[:, 1].min()
    check(abs(y - gy) < 0.02, f"{side} carriage pad on the ground at rest: {y:.3f} (ground {gy})")
for i, L in enumerate(legs):
    if L["lower"]:
        y = foot(V, f"leg{i}_pad")[:, 1]
        check(abs(y.min() - gy) < 0.05 and np.ptp(y) < gm.LEG_PAD_OFF + gm.PAD_T + 0.05,
              f"stern leg {i} pad flat on the ground at rest: {y.min():.3f}..{y.max():.2f}")
feet = ("pad_", "sole_", "anchor_", "ankle_", "shin_") + tuple(f"leg{i}_" for i, L in enumerate(legs) if L["lower"])
low = min((V[n][:, 1].min(), n) for n in gm.GROUPS if not n.startswith(feet))
check(low[0] > gy + 0.8, f"lowest non-foot part at rest: {low[1]} {low[0]:.2f} (ground {gy})")
# leg systems: anchors run into the ground, the unloaded strut drops the pad by its rod
VA = gm.apply_pose(groups, comps, dict(st, anchor_carriage=1, **{f"leg{i}_anchor": 1 for i, L in enumerate(legs) if L["lower"]}))
for n in [f"anchor_{side}" for side in gm.SIDES] + [f"leg{i}_anchor" for i, L in enumerate(legs) if L["lower"]]:
    y = VA[n][:, 1].min()
    check(abs(y - (gy - gm.ANCHOR_OUT + 0.02)) < 0.05, f"{n} out: tips {gy - y:.2f} m into the ground")
VS = gm.apply_pose(groups, comps, dict(st, strut_carriage=1))
for side in gm.SIDES:
    d = foot(V, f"pad_{side}")[:, 1].min() - foot(VS, f"pad_{side}")[:, 1].min()
    check(abs(d - gm.STRUT_EXT_C) < 0.01, f"{side} strut unloaded: pad {d:.2f} m lower")
V0 = gm.apply_pose(groups, comps, dict(st, hangar=1, rover_lift=1))
yl = V0["rover_platform"][:, 1].min()
check(abs(yl - gy) < 0.3, f"hangar platform (crew and rovers) on the ground: {yl:.2f} (ground {gy})")
podlow = min(V[f"pod_{i}"][:, 1].min() for i in range(4))
check(podlow > gy + 0.8, f"pods out, cups down: lowest {podlow:.2f} m ({podlow - gy:.2f} above ground)")
for i in range(4):
    d = np.nanmax(-outs(samples(G[f"pod_{i}"], V[f"pod_{i}"])))
    check(d < 0.03, f"pod {i} deployed clear of the skin (max penetration {d:.2f})")

# ---- E. turning on the carriage legs: pads flat on the ground
st, pitch, lift = P["turning 45 deg"]
V = gm.apply_pose(groups, comps, st)
for side in gm.SIDES:
    W = gm.pose_world(foot(V, f"pad_{side}"), pitch, lift)
    check(np.ptp(W[:, 1]) < gm.PAD_OFF + gm.PAD_T + 0.3, f"{side} pad level while turning: height span {np.ptp(W[:, 1]):.2f}")
    check(abs(W[:, 1].min() - gy) < 0.05, f"{side} pad on the ground while turning: {W[:, 1].min():.3f}")
for n in gm.GROUPS:
    if n.startswith(("pad_", "sole_", "anchor_", "ankle_", "shin_", "hip_", "thigh_", "carriage_", "pin_")):
        continue
    W = gm.pose_world(V[n], pitch, lift)
    if W[:, 1].min() < gy + 1.0:
        check(False, f"turning: {n} comes down to {W[:, 1].min():.2f}")

# ---- F. standing on the stern legs
st, pitch, lift = P["standing"]
V = gm.apply_pose(groups, comps, st)
gz = gm.zs(gm.STAND_GROUND_S)
for i in range(4):
    z = foot(V, f"leg{i}_pad")[:, 2]
    check(abs(z.min() - gz) < 0.05 and np.ptp(z) < gm.LEG_PAD_OFF + gm.PAD_T + 0.05,
          f"stern leg {i} pad flat on the ground standing: {z.min():.2f}..{z.max():.2f} (ground {gz})")
lowest = min((V[n][:, 2].min(), n) for n in gm.GROUPS if not n.startswith("leg"))
check(lowest[0] > gz + 3.0, f"stern clearance standing: {lowest[1]} {lowest[0] - gz:.1f} m")
F = np.array([L["rad"][:2] * gm.STAND_R for L in legs])
poly = F[[0, 3, 2, 1]]
dmin = min(abs((q - p)[0] * (-p)[1] - (q - p)[1] * (-p)[0]) / np.linalg.norm(q - p) for p, q in zip(poly, np.roll(poly, -1, 0)))
for name, s_cg in (("landing", 71.1), ("loaded", 53.8)):
    ang = math.degrees(math.atan2(dmin, s_cg - gm.STAND_GROUND_S))
    check(ang > 10.0, f"standing, {name} CG: tip-over angle {ang:.1f} deg (feet polygon inradius {dmin:.1f} m)")
for i, L in enumerate(legs):
    d = L["rad"][:2] * gm.STAND_R - L["H"][:2]
    mu = np.linalg.norm(d) / (L["H"][2] - gm.zs(gm.STAND_GROUND_S) - gm.LEG_FOOT_H)
    check(mu < 0.55, f"stern leg {i}: splay needs friction {mu:.2f} (rock, regolith 0.6..0.8)")
print("ALL OK" if not fails else f"{fails} FAILED")
