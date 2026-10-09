"""Numeric checks of the Tantra mesh and rig (T9): flush stowage, clearances, sweeps through the openings, ground
contact in every pose, stance geometry. Run: python tests/verify_gear_mesh.py"""
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
    """(signed distance outside the skin, u, s) per point; nan beyond the body (the nose is checked by gm.outside)."""
    s = P[:, 2] - gm.STERN_Z
    d = np.full(len(P), np.nan)
    u = np.full(len(P), np.nan)
    key = np.round(s * 4) / 4
    ok = (s >= 0.0) & (s <= gm.NB)
    for k in np.unique(key[ok]):
        m = ok & (key == k)
        d[m], u[m] = _seg_dist(gm.sec(k), P[m, :2])
    nose = (s > gm.NB) & (s < gm.TIP_S - 0.5)
    for i in np.where(nose)[0]:
        d[i] = gm.outside(P[i])
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
# Parts that stow inside the skin (since 2026-10-09 the stern legs too, in their body bays); the folded wings stow
# outside it, in the shadow of the body (checked separately).
MOVING = [n for n in gm.GROUPS if n.startswith(("fin", "door_", "pod_", "arm_pod", "hip_", "blade_", "ankle_", "foot_", "bay_door", "sleg_door", "sfoot",
                                                 "kang_", "kfoot_", "guy_"))]
MOVING += [n for n in gm.GROUPS if n.startswith("leg") and n != "leg_hinges"]
OUTSIDE = [n for n in gm.GROUPS if n.startswith(("crest_", "wing_outer_", "elevon_"))]   # (2026-10-09) the stern legs stow in body bays
RIBS = lambda pre: [n for n in gm.foot_groups(pre) if "_slat_" in n]   # the petals' plates lie on the ground


def foot_rim(V, pre):
    """Points of a cup foot's ribs: their lower faces carry the canopy, the rim on the ground (the skirt below it)."""
    return np.vstack([V[n] for n in RIBS(pre)])


# ---- A. flight at 0.9 c: every moving part inside the skin, covers flush
V = gm.apply_pose(groups, comps, STOW)
worst = []
for n in MOVING:
    d = outs(samples(G[n], V[n]))
    worst.append((np.nanmax(d), n))
worst.sort(reverse=True)
check(worst[0][0] <= 0.03, "stowed: nothing proud of the skin (worst " + ", ".join(f"{n} {w:+.3f}" for w, n in worst[:4]) + ")")
for n, lim in (("door_pod_0", 0.03), ("door_top_port", 0.03), ("bay_door_starboard", 0.03), ("kang_door", 0.03), ("iris_nose_0", 0.03),
               ("sleg_door_0", 0.03), ("sleg_door_2", 0.03), ("pocket_door_port_up", 0.03), ("pocket_door_starboard_low", 0.03)):
    m = np.nanmax(outs(V[n]))
    check(-lim <= m <= 0.03, f"stowed {n}: outer face flush ({m:+.3f} m)")
for side in gm.SIDES:                                      # blades 0.55 m under the skin, the fan of ribs flush over them
    m = np.nanmax(outs(samples(G[f"blade_{side}_0"], V[f"blade_{side}_0"])))
    check(-0.8 <= m <= -0.4, f"stowed blade {side}: outer face {m:+.2f} m under the skin (doors over it)")
    r = max(np.nanmax(outs(samples(G[n], V[n]))) for n in gm.foot_groups(f"foot_{side}"))
    check(r <= -(gm.POCKET_DOOR_T + 0.05), f"stowed blade foot {side}: folded in the pocket under its doors, outermost {r:+.2f} m")
# sub-light: everything outside the skin lies in the shadow of the widest/tallest body sections
W_MAX, Y_TOP, Y_BOT = gm.wh_at(60.0)[0], (1 - gm.FL) * gm.wh_at(60.0)[1], -gm.FL * gm.wh_at(60.0)[1]
for n in OUTSIDE:
    P = V[n]
    ok = (np.abs(P[:, 0]).max() <= W_MAX + 0.05) and (P[:, 1].max() <= Y_TOP + 0.35) and (P[:, 1].min() >= Y_BOT - 1.7)
    check(ok, f"stowed {n} in the body shadow: |x| {np.abs(P[:, 0]).max():.2f} (<= {W_MAX:.2f}), y {P[:, 1].min():.2f}..{P[:, 1].max():.2f}")
for n in OUTSIDE:
    o_ = outs(samples(G[n], V[n]))
    # a part lying wholly beyond the body's stations (a folded stern foot's skirt aft of the stern) cannot be inside it
    d = np.nanmin(o_) if np.isfinite(o_).any() else float("inf")
    check(d > -0.03, f"stowed {n} does not enter the hull (deepest {d:+.2f} m)")
V_fin = np.vstack([V["fin"], V["fin_upper"]])
check(V_fin[:, 1].max() <= gm.top_y(30) + 0.01 and V_fin[:, 1].min() > -9.0,
      f"fin retracted: y {V_fin[:, 1].min():.1f}..{V_fin[:, 1].max():.2f} (skin {gm.top_y(30):.2f})")
# marching cup home under its iris; nose cups inside the nose
mz = (V["march_unit"][:, 2] - gm.STERN_Z).min()
check(mz > gm.MARCH_IRIS_S + 0.2, f"marching cup stowed behind the well iris: lip s {mz:.2f} (iris {gm.MARCH_IRIS_S})")
Pn = samples(G["cups_nose"], V["cups_nose"])
Pn = Pn[Pn[:, 2] - gm.STERN_Z <= gm.NOSE_CUP_S + 0.05]                    # the cups themselves (the throats sit in the openings)
check(np.nanmax(outs(Pn)) < -0.3, f"nose retro cups inside the nose skin ({np.nanmax(outs(Pn)):+.2f} m)")


# ---- B. stowed parts vs trap cassettes
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


BLADE = lambda side: [f"hip_{side}", f"ankle_{side}"] + [f"blade_{side}_{i}" for i in range(gm.BLADE_N)] + gm.foot_groups(f"foot_{side}")
for side in gm.SIDES:
    pocket = [f"carriage_{side}"]
    sweep(f"blade {side} slide out", BLADE(side), pocket, lambda t, s=side: dict(STOW, **{f"slide_{s}": 1 - t}))
    sweep(f"blade {side} swings down", BLADE(side), pocket, lambda t, s=side: dict(STOW, **{f"slide_{s}": 0, f"pitch_{s}": 1 - t}), n=12)
    sweep(f"blade {side} foot opens", BLADE(side), pocket, lambda t, s=side: dict(STOW, **{f"slide_{s}": 0, f"pitch_{s}": 0, f"foot_fold_{s}": 1 - t}), n=8)
for i, L in enumerate(legs):
    ph, ex = L["phi_stand"] / L["phi_max"], L["e_stand"] / gm.LEG_EXT_MAX
    parts = [f"leg{i}_sec{k}" for k in range(gm.LEG_SEC_N)] + [f"leg{i}_ankle"] + gm.foot_groups(f"sfoot{i}")
    sweep(f"stern leg {i} to stand", parts + [f"sleg_door_{i}"], [f"sleg_{i}"],
          lambda t, i=i, ph=ph, ex=ex: dict(STOW, **{f"leg{i}_swing": ph * t, f"leg{i}_ext": ex * min(1, max(0, (t - gm.LEG_EXT_DELAY) / (1 - gm.LEG_EXT_DELAY))),
                                                    f"leg{i}_foot_stand": t, f"leg{i}_rail": 1 - min(1, 2 * t), f"leg{i}_fold": 1 - max(0, 2 * t - 1)}))
for i in range(4):
    sweep(f"pod {i} arm out", [f"door_pod_{i}", f"pod_{i}", f"arm_pod_{i}"], [f"pod_{i}"], lambda t: dict(STOW, pod_retract=1 - t))
sweep("pods swivel", [f"pod_{i}" for i in range(4)], [], lambda t: dict(STOW, pod_retract=0, pod_swivel=t), n=12)
WINGS = ["crest_starboard", "crest_port", "wing_outer_starboard", "wing_outer_port", "elevon_starboard", "elevon_port"]
sweep("wings unfold (outer out, then inner down)", WINGS, [],
      lambda t: dict(STOW, wing_outer=max(0.0, 1 - 2 * t), crest_lateral=min(1.0, 2 - 2 * t)))
sweep("wings 30 deg", WINGS, [], lambda t: dict(STOW, crest_lateral=t * gm.WING_RAISE / gm.WING_FOLD, wing_outer=0), n=10)
sweep("fin extends", ["fin", "fin_upper"], ["fin_slot"], lambda t: dict(STOW, crest_dorsal=1 - t))
sweep("elevons and body flap", ["elevon_starboard", "elevon_port", "body_flap"], [],
      lambda t: {"elevon_starboard": t, "elevon_port": 1 - t, "body_flap": t}, n=10)
sweep("marching cup runs out", ["march_unit"], [], lambda t: dict(STOW, iris_march=1, march_slide=t), n=8)
KANG = ["kang_thigh", "kang_brace", "kang_rod", "kang_ankle"] + [f"kang_shin_{k}" for k in range(gm.KANG_SEC_N)] + gm.foot_groups("kfoot")
REST_K = {k: v for k, v in gm.preview_poses(legs)[1][1].items() if k.startswith("kang")}
# door; the knee unfolds down and aft through the open pocket while the thigh still lies along the belly; then the hip
# swings the straight leg down-forward; the shin runs out; the foot opens on the way (the order core/Carriage uses)
# the knee only to the vertical while the thigh lies along the belly (straight, the shin would run into the hull aft of
# the pocket), the rest of the bend together with the hip swing
def kang_seq(t):
    kn1 = 0.5 * min(1, max(0, (t - 0.15) / 0.25))
    sw = min(1, max(0, (t - 0.4) / 0.45))
    return dict(STOW, kang_door=min(1, 5 * t), kang_knee=kn1 + (REST_K["kang_knee"] - 0.5) * sw, kang_hip=REST_K["kang_hip"] * sw,
                kang_ext=REST_K["kang_ext"] * min(1, max(0, (t - 0.6) / 0.4)), kang_foot=REST_K["kang_foot"] * sw,
                kang_fold=1 - min(1, max(0, (t - 0.7) / 0.3)))
# the door turns about its starboard edge: its 0.2 m back face dips under the skin just past the hinge
KDOOR = dict(gm.OPEN["kang_pocket_starboard"], u=(0.0, gm.OPEN["kang_pocket_starboard"]["u"][1] + 0.015))
sweep("kangaroo leg out (door, knee to vertical, hip + knee, ext, foot)", KANG + ["kang_door"], [KDOOR, "kang_pocket_port"], kang_seq, n=24)

# ---- D. resting level: blade feet and the kangaroo foot on the ground, nothing else near it
P = {n: (st, pitch, lift) for n, st, pitch, lift, _ in gm.preview_poses(legs)}
st, pitch, lift = P["resting level"]
V = gm.apply_pose(groups, comps, st)
gy = -gm.AXIS_H
for side in gm.SIDES:
    y = foot_rim(V, f"foot_{side}")[:, 1]
    check(abs(y.min() - gy) < 0.12 and y.max() < gy + gm.FOOT_RIM_DROP + 0.6, f"{side} blade foot on the ground at rest: rim {y.min():.2f}, hub {y.max():.2f} (ground {gy})")
y = foot_rim(V, "kfoot")[:, 1]
check(abs(y.min() - gy) < 0.12 and y.max() < gy + gm.FOOT_RIM_DROP + 0.6, f"kangaroo foot level on the ground at rest: rim {y.min():.2f}, hub {y.max():.2f}")
zk = (V["kfoot_hub"][:, 2] - gm.STERN_Z).mean() - (gm.KANG_S1 - 0.6)
check(abs(zk - gm.KANG_FOOT_FWD) < 0.5, f"kangaroo foot {zk:.1f} m ahead of the hip (design {gm.KANG_FOOT_FWD})")
feet = ("foot_", "kfoot_", "ankle_", "blade_", "kang_")
low = min((V[n][:, 1].min(), n) for n in gm.GROUPS if not n.startswith(feet))
check(low[0] > gy + 0.8, f"lowest non-foot part at rest: {low[1]} {low[0]:.2f} (ground {gy})")
VS = gm.apply_pose(groups, comps, dict(st, strut_carriage=1))
for side in gm.SIDES:
    d = foot_rim(V, f"foot_{side}")[:, 1].min() - foot_rim(VS, f"foot_{side}")[:, 1].min()
    check(abs(d - gm.STRUT_EXT_C) < 0.01, f"{side} strut unloaded: foot {d:.2f} m lower")
V0 = gm.apply_pose(groups, comps, dict(st, hangar=1, rover_lift=1))
yl = V0["rover_platform"][:, 1].min()
check(abs(yl - gy) < 0.3, f"hangar platform (crew and rovers) on the ground: {yl:.2f} (ground {gy})")
podlow = min(V[f"pod_{i}"][:, 1].min() for i in range(4))
check(podlow > gy + 0.8, f"pods out, cups down: lowest {podlow:.2f} m ({podlow - gy:.2f} above ground)")
for i in range(4):
    d = np.nanmax(-outs(samples(G[f"pod_{i}"], V[f"pod_{i}"])))
    check(d < 0.03, f"pod {i} deployed clear of the skin (max penetration {d:.2f})")
VH = gm.apply_pose(groups, comps, dict(st, pod_retract=0, pod_swivel=61.8 / 180.0, pod_cant=1.0))   # hover: thrust vertical, pairs apart
for i in range(4):
    d = np.nanmax(-outs(samples(G[f"pod_{i}"], VH[f"pod_{i}"])))
    check(d < 0.03 and VH[f"pod_{i}"][:, 1].min() > gy + 0.8, f"pod {i} at hover (61.8 deg, pairs 15 deg apart) clear of the skin ({d:.2f}) and the ground")
px = max(abs(V[f"pod_{i}"][:, 0]).max() for i in range(4))
check(px > gm.POD_X_OUT, f"pods out to x {px:.1f} (centre {gm.POD_X_OUT})")

# ---- E. turning on the blades: feet flat on the ground, nothing else down there
st, pitch, lift = P["turning 45 deg"]
V = gm.apply_pose(groups, comps, st)
for side in gm.SIDES:
    W = gm.pose_world(foot_rim(V, f"foot_{side}"), pitch, lift)
    check(abs(W[:, 1].min() - gy) < 0.12, f"{side} foot on the ground while turning: {W[:, 1].min():.3f}")
for n in gm.GROUPS:
    if n.startswith(("foot_", "ankle_", "blade_", "hip_", "carriage_", "pin_", "guy_")):   # the guys hold the foot rims
        continue
    W = gm.pose_world(V[n], pitch, lift)
    if W[:, 1].min() < gy + 1.0:
        check(False, f"turning: {n} comes down to {W[:, 1].min():.2f}")

# ---- F. standing on the stern legs
st, pitch, lift = P["standing"]
V = gm.apply_pose(groups, comps, st)
gz = gm.zs(gm.STAND_GROUND_S)
for i in range(4):
    z = foot_rim(V, f"sfoot{i}")[:, 2]
    check(abs(z.min() - gz) < 0.2 and np.ptp(z) < gm.FOOT_RIM_DROP + 0.6,
          f"stern leg {i} foot flat on the ground standing: {z.min():.2f}..{z.max():.2f} (ground {gz})")
    r = np.hypot(V[f"sfoot{i}_hub"][:, 0].mean(), V[f"sfoot{i}_hub"][:, 1].mean() - gm.YC)
    check(abs(r - gm.STAND_R) < 0.5, f"stern leg {i} foot on R {r:.1f} (design {gm.STAND_R})")
lowest = min((V[n][:, 2].min(), n) for n in gm.GROUPS if not n.startswith(("leg", "sfoot")))
check(lowest[0] > gz + 15.0, f"stern clearance standing: {lowest[1]} {lowest[0] - gz:.1f} m (marching cup lip wants 16)")
VM = gm.apply_pose(groups, comps, dict(st, iris_march=1, march_slide=1))
lip = VM["march_unit"][:, 2].min()
check(abs(lip - gz - 16.0) < 0.5, f"marching cup run out: lip {lip - gz:.1f} m above the ground (design 16)")
F = np.array([L["rad"][:2] * gm.STAND_R for L in legs])
poly = F[[0, 3, 2, 1]]
dmin = min(abs((q - p)[0] * (-p)[1] - (q - p)[1] * (-p)[0]) / np.linalg.norm(q - p) for p, q in zip(poly, np.roll(poly, -1, 0)))
for name, s_cg in (("loaded", 58.1), ("landing, no anamezon", 64.7), ("empty", 64.1)):
    ang = math.degrees(math.atan2(dmin, s_cg - gm.STAND_GROUND_S))
    check(ang > 10.0, f"standing, {name} CG: tip-over angle {ang:.1f} deg (feet polygon inradius {dmin:.1f} m)")
for i, L in enumerate(legs):
    d = L["rad"][:2] * gm.STAND_R - L["H"][:2]
    mu = np.linalg.norm(d) / (L["H"][2] - gm.zs(gm.STAND_GROUND_S) - gm.LEG_FOOT_H)
    check(mu < 0.55, f"stern leg {i}: splay needs friction {mu:.2f} (rock, regolith 0.6..0.8)")
# stern feet clear of the blade feet when the blades still stand (load transfer) - blade feet at x +-19 under the hull
for i, L in enumerate(legs):
    hub = V[f"sfoot{i}_hub"][:, :2].mean(0)
    for sx in (-1, 1):
        d = np.hypot(hub[0] - sx * gm.HIP_X_OUT, hub[1] - 0.0) - gm.FOOT_R - (gm.LEG_RIB_L * math.cos(math.radians(6.0)) + gm.HUB_R)
        if sx * hub[0] > 0:
            check(d > 0.5, f"stern foot {i} clear of the {'starboard' if sx > 0 else 'port'} blade foot by {d:.1f} m")
# (2026-10-09) the blade guys: anchored on the foot rim hoops over the lift, the turn and standing; reeled inside the blade
for e in (0.0, 0.114, 0.3):
    VG = gm.apply_pose(groups, comps, {f"blade_ext_{s}": e for s in gm.SIDES})
    worst = 0.0
    for side, sgn in (("port", -1), ("starboard", 1)):
        lat, drop = gm.guy_vec(e)
        hy = -gm.LEG_LMAX / 2 + (gm.BLADE_N - 1) / 2 * gm.BLADE_EXT * e
        for gname, gsx in (("in", -sgn), ("out", sgn)):
            exp = np.array([sgn * gm.HIP_X_OUT + gsx * lat, hy - drop])
            P = VG[f"guy_{side}_{gname}_{gm.GUY_N - 1}"][:, :2]
            worst = max(worst, np.linalg.norm(P - exp, axis=1).min())
    check(worst < 0.4, f"guys at blade extension {e}: the cable end on the rim hoop (worst {worst:.2f} m)")
VG = gm.apply_pose(groups, comps, {f"blade_ext_{s}": 0.9 for s in gm.SIDES})
inside = all(np.abs(VG[f"guy_{s}_{g}_{j}"][:, 0] - (1 if s == "starboard" else -1) * gm.HIP_X_OUT).max() < gm.BLADE_T[-1] / 2
             for s in gm.SIDES for g in ("in", "out") for j in range(gm.GUY_N))
check(inside, "guys reeled inside the blade lying (extension 0.9)")
print("ALL OK" if not fails else f"{fails} FAILED")
