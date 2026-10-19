# «Панцирь» rev. R (borazon + photonic mirror, the honest wall: legs 79 / torso 93 / arms 62 / hand 30 mm per side)
# (previously P3 «Гибрид») -> a skinned game asset for OrbiterCrew (the suit pipeline of Tantra_Design/blender/export_skin.py).
#   blender -b Tantra_Design\blender\astronavigator_suit.blend --python build_pantsir_game.py
#   -> Tantra_Design\blender\astronavigator_pantsir.blend (NEW; astronavigator_suit.blend is only read), textures in game\tex\
# Steps: generate P3 with the approved generator (pantsir_lib), fit every segment to the Astronavigator rig and to the
# person's body + the physical wall stack (HEAVY_SUIT.md), unwrap, clean (no posts/bolts/fins/stencils/ghosts), merge
# per bone region x material, decimate coplanar faces, triangulate, weight rigidly to the bones (joint fans blended),
# check the body against the shell in the rest, idle, walk and run poses.  Log: game\build_pantsir_game.log
import bpy, bmesh, os, sys, math, re, json, traceback
import numpy as np
from mathutils import Vector as V, Matrix, Quaternion
from mathutils.bvhtree import BVHTree
HERE = os.path.dirname(os.path.abspath(__file__)); BL = os.path.abspath(os.path.join(HERE, "..", "..", "blender"))
sys.path.insert(0, HERE); sys.path.insert(0, BL)
OUT_BLEND = os.path.join(BL, "astronavigator_pantsir.blend")
LOG = os.path.join(HERE, "build_pantsir_game.log"); log = []
TILE = 0.25


def say(*a):
    log.append(" ".join(str(x) for x in a)); open(LOG, "w", encoding="utf-8").write("\n".join(log) + "\n")


def smooth(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0); return t * t * (3 - 2 * t)


# ------------------------------------------------------------------ fit: the suit (drawn for 1.85 m, soles at 0) -> the rig
ZT = lambda z: 0.93 + 0.84 * (z - 0.88)                     # torso heights: pelvis ring -> hip joint, collar -> above the neck base
TOR = dict(sx=0.96, sy=1.01, dy=-0.03)                     # rev. R: chest 0.48 x 0.41 m outside (body 0.29 x 0.22 + 2 x 0.093)
PEL = dict(sx=1.08, sy=1.39, dy=-0.027)                    # rev. R: pelvis 0.56 x 0.39 (body 0.37 x 0.20 + 2 x 0.093)
WST = dict(sx=1.00, sy=1.15, dy=-0.03)
HEL = dict(sx=1.10, sy=1.07, sz=1.14, cy=-0.0075, dy=-0.04)  # helmet 0.36 x 0.39, crown 0.09 over the head
ARM_OUT = 0.06                                             # arm shells outboard of P3 (clear the torso with the thicker walls)
# cross-section scale (lateral, depth) at the two ends of each segment: outer size = body + 2 x wall (limbs 70 mm,
# knee/elbow 75 mm) over the P3 outer size there
# rev. R: legs body + 2 x 79 mm (knee 84) over the generator's P3 + 9 mm legs; arms body + 2 x 62 mm (elbow 67) over its ~62 mm arms
SC = {"thigh": ((1.23, 1.20), (1.30, 1.27)), "shin": ((1.25, 1.19), (1.22, 1.20)), "knee": (1.22, 1.19), "ankle": 1.25,
      "uarm": ((1.10, 1.13), (1.18, 1.14)), "farm": ((1.17, 1.15), (1.22, 1.21))}
CROTCH = -1.0                                              # (fix pass) the 12 deg suit stance: inner walls not clipped
REG = {}                                                   # per-side fit data


def m_torso(p, k=TOR): return V((p.x * k["sx"], p.y * k["sy"] + k["dy"], ZT(p.z)))


def m_helm(p): return V((p.x * HEL["sx"], (p.y - HEL["cy"]) * HEL["sy"] + HEL["cy"] + HEL["dy"], ZT(1.575) + HEL["sz"] * (p.z - 1.575)))


def seg_map(As, Bs, At, Bt, scA, scB):
    us = Bs - As; Ls = us.length; us = us.normalized(); ut = Bt - At; Lt = ut.length; ut = ut.normalized()
    ex = V((1, 0, 0)) - us * us.x; ex.normalize(); ey = us.cross(ex)
    R = us.rotation_difference(ut).to_matrix(); ext, eyt = R @ ex, R @ ey
    def f(p):
        d = p - As; t = d.dot(us) / Ls; lx, ly = d.dot(ex), d.dot(ey); tc = min(max(t, 0.0), 1.0)
        sx = scA[0] + (scB[0] - scA[0]) * tc; sy = scA[1] + (scB[1] - scA[1]) * tc
        return At + ut * (t * Lt) + ext * (lx * sx) + eyt * (ly * sy)
    return f, R


ARM_OUT_RAD, LEG_OUT_RAD = 0.46, 0.21                     # Motion.cpp suit layer at Pantsir (heavy 0.3)
HAND_WALL = 0.030                                         # rev. R: 30 mm capsule wall (the generator sizes the capsule itself)
HAND = {"w": 0.085, "t": 0.035}                           # measured from the person in setup (defaults)


def suit_layer(arm):
    """Motion.cpp: Skin::Turn(pose, bone, AX_FWD, angle) about the bone's joint, in the order LArm, RArm, LUpLeg, RUpLeg,
    LFoot, RFoot; left limbs on model -x take the negative angle. Model z (forward) = blender -y, and the frame flip
    makes a model angle a about +z equal to a blender rotation by a about +Y."""
    W = arm.matrix_world
    for name, a in (("LeftArm", -ARM_OUT_RAD), ("RightArm", ARM_OUT_RAD), ("LeftUpLeg", -LEG_OUT_RAD), ("RightUpLeg", LEG_OUT_RAD),
                    ("LeftFoot", LEG_OUT_RAD), ("RightFoot", -LEG_OUT_RAD)):
        pb = arm.pose.bones[name]; M = W @ pb.matrix; h = M.translation.copy()
        pb.matrix = W.inverted() @ (Matrix.Translation(h) @ Matrix.Rotation(a, 4, 'Y') @ Matrix.Translation(-h) @ M)
        bpy.context.view_layer.update()


def measure_hand(arm, body):
    """the person's hand: width and thickness of the palm (rest pose), PCA of the body points past the wrist"""
    W = arm.matrix_world; b = arm.data.bones["LeftHand"]; w0 = W @ b.head_local; d = (W @ arm.data.bones["LeftHandFinger1"].tail_local - w0).normalized()
    for m in body.modifiers:
        if m.type == 'MASK': m.show_viewport = False
    dg = bpy.context.evaluated_depsgraph_get(); me = body.evaluated_get(dg).to_mesh()
    P = np.array([body.matrix_world @ v.co for v in me.vertices]); body.evaluated_get(dg).to_mesh_clear()
    t = (P - np.array(w0)) @ np.array(d); r = np.linalg.norm((P - np.array(w0)) - np.outer(t, np.array(d)), axis=1)
    Q = P[(t > 0.02) & (t < 0.09) & (r < 0.07)]
    Q = Q - Q.mean(0); Q = Q - np.outer(Q @ np.array(d), np.array(d)); w_, v_ = np.linalg.eigh(np.cov(Q.T))
    ext = [np.ptp(Q @ v_[:, i]) for i in range(3)]; ext.sort()
    return ext[2], ext[1]                                   # width, thickness


def setup_fit(arm, ns):
    W = arm.matrix_world; B = arm.data.bones
    def head(n): return W @ B[n].head_local
    # idle pose (the arms are designed hanging: rest -> idle relation per arm bone, relative to the torso)
    rest = {b.name: (W @ b.matrix_local).copy() for b in B}
    arm.data.pose_position = 'POSE'
    from pose_util import idle_pose
    idle_pose(arm); bpy.context.view_layer.update(); suit_layer(arm)          # the arms are fitted in the game's suited idle
    idle = {pb.name: (W @ pb.matrix).copy() for pb in arm.pose.bones}
    for pb in arm.pose.bones: pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    T_sp = idle["Spine1"] @ rest["Spine1"].inverted()
    for s, Sd in ((1, "Left"), (-1, "Right")):
        G = {n: rest[Sd + n] @ idle[Sd + n].inverted() for n in ("Arm", "ForeArm", "Hand")}     # designed in the suited idle, on the bones
        # legs (rest space)
        Hs, Ks, As = V((s * 0.135, -0.0067, 0.88)), V((s * 0.135, -0.045, 0.485)), V((s * 0.135, -0.006, 0.156))
        Hr, Kr = head(Sd + "UpLeg"), head(Sd + "Leg")
        At = V((s * 0.177, -0.026, 0.097))                   # the boot's ankle point: footbed 5 mm over the sole of the foot
        fd = head(Sd + "ToeBase") - head(Sd + "Foot"); yaw = math.atan2(fd.x, -fd.y)
        thigh, Rt = seg_map(Hs, Ks, Hr, Kr, *SC["thigh"]); shin, Rs = seg_map(Ks, As, Kr, At, *SC["shin"])
        Rk = Rt.to_quaternion().slerp(Rs.to_quaternion(), 0.5).to_matrix()
        Rz = Matrix.Rotation(yaw, 3, 'Z')
        def knee(p, Ks=Ks, Kr=Kr, Rk=Rk):
            d = p - Ks; return Kr + Rk @ V((d.x * SC["knee"][0], d.y * SC["knee"][1], d.z))
        def boot(p, As=As, At=At, Rz=Rz): return At + Rz @ (p - As)
        def ankle(p, As=As, boot=boot):
            d = p - As; return boot(As + V((d.x * SC["ankle"], d.y * SC["ankle"], d.z)))
        Ar = head(Sd + "Foot")
        def ankled(p, ankle=ankle, dl=V((0, Ar.y - At.y, Ar.z - At.z))): return ankle(p) + dl          # the pitch drive on the ankle pivot
        # arms: centred on the person's arm bones in the suited idle (rigid on Arm / ForeArm / Hand), then to the rest pose
        cxa = ns["ARM_CX"]; sn8, cs8 = math.sin(math.radians(8)), math.cos(math.radians(8))
        Ss = V((s * cxa, -0.005, 1.40)); Es = V((s * (cxa + 0.305 * sn8), -0.0136, 1.40 - 0.305 * cs8)); Ws = V((s * (cxa + 0.598 * sn8), -0.0561, 1.40 - 0.598 * cs8))
        Si, Ei, Wi = idle[Sd + "Arm"].translation, idle[Sd + "ForeArm"].translation, idle[Sd + "Hand"].translation
        ua, _ = seg_map(Ss, Es, Si, Ei, *SC["uarm"]); fa, Rf = seg_map(Es, Ws, Ei, Wi, *SC["farm"])
        def uarm(p, ua=ua, G=G): return G["Arm"] @ ua(p)
        def farm(p, fa=fa, G=G): return G["ForeArm"] @ fa(p)
        # the P3 hand frame (build_pantsir_3d.hand_frame): O at the wrist, D along the hand, B the back, U across
        Dp = V((s * (cxa + 0.70 * sn8), -0.0707, 1.40 - 0.70 * cs8))
        Dh = (Dp - Ws).normalized(); Bh = V((s, 0, 0)); Bh = (Bh - Dh * Bh.dot(Dh)).normalized(); Uh = Dh.cross(Bh)
        hd = (idle[Sd + "Hand"].col[1].xyz).normalized(); Rh = Dh.rotation_difference(hd).to_matrix()
        cU, cB = SC["farm"][1][1], SC["farm"][1][0]                  # rev. R: the cuff grows with the forearm's wrist; capsule as generated
        def hand(p, Ws=Ws, Wi=Wi, Rh=Rh, G=G, Dh=Dh, Bh=Bh, Uh=Uh):
            d = p - Ws; a, b, u = d.dot(Dh), d.dot(Bh), d.dot(Uh); t = smooth(-0.005, 0.05, a)
            sU, sB = cU + (1.0 - cU) * t, cB + (1.0 - cB) * t; a2 = a
            return G["Hand"] @ (Wi + Rh @ (Dh * a2 + Bh * (b * sB) + Uh * (u * sU)))
        REG[s] = dict(thigh=thigh, shin=shin, knee=knee, boot=boot, ankle=ankle, ankled=ankled, uarm=uarm, farm=farm, hand=hand, Kz=Kr.z, yaw=yaw)
    say("fit: hip/knee/ankle (rest) L", tuple(round(c, 3) for c in head("LeftUpLeg")), tuple(round(c, 3) for c in head("LeftLeg")), tuple(round(c, 3) for c in head("LeftFoot")),
        "boot yaw %.1f deg" % math.degrees(REG[1]["yaw"]))


def xform(kind, s, p):
    if kind == "torso": return m_torso(p)
    if kind == "pelvis": return m_torso(p, PEL)
    if kind == "waist": return m_torso(p, WST)
    if kind == "helm": return m_helm(p)
    if kind == "collar": q = m_torso(p); q.y += HEL["dy"] - TOR["dy"]; return q
    if kind == "yoke":
        q = m_torso(p); q.x -= s * 0.06 * min(max((abs(p.x) - 0.05) / 0.30, 0.0), 1.0); return q
    if kind == "shinstrut": p = p + V((0, 0, -0.03 * smooth(0.26, 0.20, p.z))); kind = "shin"
    q = REG[s][kind](p)
    if kind in ("thigh", "knee", "shin") and s * q.x < CROTCH: q.x = s * CROTCH
    return q


# ------------------------------------------------------------------ classification of the generated objects
def classify(base, x, cap_names, full):
    s = 1 if x > 0 else -1; S = "L" if s > 0 else "R"
    if full in cap_names: return "HelmCap", "helm", "Spine1", s
    if base.startswith(("helmet_back", "helmet_stripe", "hinge_motor", "hinge_cap", "hinge_con", "valve")): return "Helm", "helm", "Spine1", s
    if base == "collar": return "NeckSeal", "collar", "Spine1", s
    if base.startswith("shoulder_yoke"): return "Tor", "yoke", "Spine1", s
    if re.match(r"^C\d[io](_lip)?$", base) or base.startswith(("hut", "fieldband", "chest_mark", "door", "latch", "evap", "fin", "radfield", "battery", "handle", "spine_")):
        return "Tor", "torso", "Spine1", s
    if base.startswith(("brief", "hip_band", "pelvis_ring", "ring_bracket", "motor_hip", "cap_hip", "hip_lug")): return "Pel", "pelvis", "Hips", s
    if base == "waist": return "Wst", "waist", "waist", s
    if "_fore" in base: return "To" + S, "boot", "Toe", s
    if base.startswith(("thigh", "leg_upper")): return "Th" + S, "thigh", "UpLeg", s
    if base.startswith(("motor_knee", "cap_knee")): return "Kn" + S, "knee", "Leg", s
    if base.startswith(("knee", "bellows")): return "Kn" + S, "knee", "knee", s
    if re.match(r"^ankle[LR]_band", base): return "Sh" + S, "ankle", "Leg", s
    if base.startswith("leg_lower"): return "Sh" + S, "shinstrut", "Leg", s
    if base.startswith("shin"): return "Sh" + S, "shin", "Leg", s
    if base.startswith(("motor_ankleL", "motor_ankleR", "stirrup")) and not base.startswith("motor_ankle_inv"): return "Ft" + S, "ankled", "Foot", s
    if base.startswith(("ankle", "malleolus", "motor_ankle", "inv_bracket", "stirrup")): return "Ft" + S, "ankle", "Foot", s
    if base.startswith(("boot_upper", "carrier", "sole_", "heel_", "instep")): return "Ft" + S, "boot", "Foot", s
    if base.startswith(("shoulder", "upperarm", "deltoid", "motor_shoulder", "cap_shoulder", "arm_member")): return "Ua" + S, "uarm", "Arm", s
    if base.startswith(("elbow", "motor_elbow", "olecranon", "forearm")): return "Fa" + S, "farm", "ForeArm", s
    if base.startswith(("cuff", "coupling", "capsule", "hand_", "palm_plate", "knuckle", "thenar", "grip_lock", "index", "broad", "thumb")):
        return "Hd" + S, "hand", "Hand", s
    return None


GM = {"Pantsir_mirror": "Mi", "Pantsir_borazon": "Ce", "Pantsir_rim": "Gr", "Pantsir_bnaero": "Gr", "Pantsir_foil": "Mi", "Pantsir_shieldp": "Mi", "Pantsir_ceram": "Ce", "Pantsir_shell": "En", "Pantsir_door": "En", "Pantsir_frame": "Gr",
      "Pantsir_fin": "Gr", "Pantsir_dark": "Gr", "Pantsir_stencil_dark": "Gr", "Pantsir_aerogel": "So", "Pantsir_tile": "So", "Pantsir_bear": "Be",
      "Pantsir_hub": "Be", "Pantsir_titan": "Be", "Pantsir_rubber": "Rb", "Pantsir_red": "Rd", "Pantsir_stencil_red": "Rd", "Pantsir_lgreen": "Rd",
      "Pantsir_lamber": "Rd", "Pantsir_gold": "Gd", "Pantsir_gold2": "Gd", "Pantsir_slit": "Sl"}
# Orbiter material per surface kind: (name, texture, flat colour, specular "r g b a power")
GMAT = {"Mi": ("Mirror", "surf_mirror", None, "0.9 0.8 0.9 1 80"), "Ce": ("Borazon", "surf_boraz", None, "0.6 0.4 0.25 1 60"),
        "So": ("Sole", "surf_sole", None, "0.05 0.05 0.05 1 5"), "Ra": ("Rad", "surf_rad", None, "0.3 0.3 0.3 1 20"),
        "En": ("Enamel", "surf_enamel", None, "0.2 0.2 0.2 1 15"), "Gr": ("Graphite", "surf_graphite", None, "0.3 0.3 0.3 1 25"),
        "Be": ("Metal", "surf_metal", None, "0.6 0.6 0.6 1 50"), "Gd": ("GoldVisor", "surf_visor", None, "1.0 0.85 0.5 1 90"),
        "Rd": ("Red", "surf_red", None, "0.15 0.1 0.1 1 10"), "Rb": ("Rubber", "surf_rubber", None, "0.04 0.04 0.04 1 5"),
        "Sl": ("Slit", "surf_slit", None, "0.8 0.8 0.8 1 80")}            # flat kinds textured too: Metalness needs _metl/_rghn
FLAT = ("Rd", "Rb", "Sl")                                                    # uniform maps: one UV, no vertex splits


def game_materials():
    import pantsir_tex
    out = {}                                                   # (the textures on disk are final; pantsir_tex.make_all() not run)
    for code, (name, tex, col, spec) in GMAT.items():
        m = bpy.data.materials.new("PG_" + name); m.use_nodes = True; nt = m.node_tree
        p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
        if tex:
            img = bpy.data.images.load(os.path.join(pantsir_tex.OUT, tex + ".png"), check_existing=True)
            it = nt.nodes.new("ShaderNodeTexImage"); it.image = img; nt.links.new(it.outputs["Color"], p.inputs["Base Color"])
        else:
            p.inputs["Base Color"].default_value = (*col, 1)
        p.inputs["Metallic"].default_value = 1.0 if code in ("Mi", "Gd", "Be") else 0.0
        p.inputs["Roughness"].default_value = 0.3 if code in ("Mi", "Gd") else 0.55
        m["orb_name"] = name; m["orb_spec"] = spec; out[code] = m
    return out


# ------------------------------------------------------------------ grid UVs for the shield bands (along the band, round it)
def band_uv_wrap(ns):
    _b, _o = ns["_band_mesh"], ns["_open_band_mesh"]
    def tag(o, rows, n, closed):
        me = o.data; rk = me.attributes.new("rk", 'INT', 'POINT'); rj = me.attributes.new("rj", 'INT', 'POINT')
        rk.data.foreach_set("value", [i // n for i in range(len(me.vertices))]); rj.data.foreach_set("value", [i % n for i in range(len(me.vertices))])
        o["grid_n"] = n; o["grid_closed"] = int(closed)
    def band(ringpts, name, part="shield", thick=0.003, posts=(), m="shieldp"):
        o = _b(ringpts, name, part, thick, posts, m); tag(o, len(ringpts), len(ringpts[0]), True); return o
    def open_band(grid, name, part="shield", thick=0.003, m="shieldp"):
        o = _o(grid, name, part, thick, m); tag(o, len(grid), len(grid[0]), False); return o
    ns.update(_band_mesh=band, _open_band_mesh=open_band)


def grid_uv(o):
    me = o.data; n = o["grid_n"]; closed = o["grid_closed"]
    rk = np.zeros(len(me.vertices), int); rj = np.zeros(len(me.vertices), int)
    me.attributes["rk"].data.foreach_get("value", rk); me.attributes["rj"].data.foreach_get("value", rj)
    P = np.array([v.co for v in me.vertices]); nr = rk.max() + 1
    idx = np.full((nr, n), -1); idx[rk, rj] = np.arange(len(P)); G = P[idx]
    du = np.linalg.norm(np.diff(G, axis=1), axis=2); U = np.concatenate([np.zeros((nr, 1)), np.cumsum(du, 1)], 1)
    Ltot = U[:, -1] + (np.linalg.norm(G[:, 0] - G[:, -1], axis=1) if closed else 0)
    dv = np.linalg.norm(np.diff(G, axis=0), axis=2).mean(1); Vv = np.concatenate([[0], np.cumsum(dv)])
    uvl = me.uv_layers.new(name="UVMap"); me.uv_layers.active = uvl
    for pg in me.polygons:
        js = [rj[me.loops[li].vertex_index] for li in pg.loop_indices]; wrap = closed and (n - 1 in js) and (0 in js)
        for li in pg.loop_indices:
            vi = me.loops[li].vertex_index; k, j = rk[vi], rj[vi]
            u = Ltot[k] if (wrap and j == 0) else U[k, j]
            uvl.data[li].uv = (u / TILE, -Vv[k] / TILE)


# ------------------------------------------------------------------ merge per region x material, clean, weight
def planar_uv(n, p, code):
    """cube projection (continuous over faces facing the same way: few vertex splits); untextured kinds get no UV"""
    if GMAT[code][1] is None or code in FLAT: return (0.5, 0.5)
    ax = max(range(3), key=lambda i: abs(n[i]))
    return ((p.y, p.z) if ax == 0 else (p.x, p.z) if ax == 1 else (p.x, p.y))[0] / TILE, ((p.y, p.z) if ax == 0 else (p.x, p.z) if ax == 1 else (p.x, p.y))[1] / TILE


def weights_of(rule, s, p):
    S = "Left" if s > 0 else "Right"
    if rule == "waist":
        t = smooth(ZT(0.97) + 0.005, ZT(1.02) - 0.005, p.z); return {"Spine1": t, "Hips": 1 - t}
    if rule == "knee":
        w = smooth(REG[s]["Kz"] + 0.035, REG[s]["Kz"] - 0.035, p.z); return {S + "Leg": w, S + "UpLeg": 1 - w}
    if rule in ("Spine1", "Hips"): return {rule: 1.0}
    return {S + {"UpLeg": "UpLeg", "Leg": "Leg", "Foot": "Foot", "Toe": "ToeBase", "Arm": "Arm", "ForeArm": "ForeArm", "Hand": "Hand"}[rule]: 1.0}


def main():
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'REST'
    keep_head = ("female1605", "eyebrow001", "eyelashes01", "low-poly", "toigo_curled_under_bob")
    HEADOBJ[:] = [o for o in bpy.data.objects if o.type == 'MESH' and o.name.split(".")[-1] in keep_head]
    old = [o for o in bpy.data.objects if o.type != 'ARMATURE' and o not in HEADOBJ]
    body = bpy.data.objects.get("Astronavigator.body")
    if body: w_, t_ = measure_hand(arm, body); HAND.update(w=w_, t=t_); say("person's hand: width %.3f, thickness %.3f m -> capsule %.3f x %.3f" % (w_, t_, w_ + 2 * HAND_WALL, t_ + 2 * HAND_WALL))
    import pantsir_lib
    ns = pantsir_lib.load(); band_uv_wrap(ns)
    ns["build"](); ns["set_open"](False); ns["set_variant"](4)
    for o in ns["SHIELD_SET"]:                                    # rev. R: the plate edges and lips in the plate's own material
        if len(o.data.materials) > 1: o.data.materials[1] = o.data.materials[0]
    setup_fit(arm, ns)
    cap_names = set(o.name for o in ns["OPENABLE"])
    drop = set(ns["PARTS"].get("standoffs", []))
    src = []; nfont = 0; ndrop = 0; uncl = []
    for o in list(ns["COLL"][0].objects):
        base = re.sub(r"\.\d+$", "", o.name)[len("Pantsir_"):]
        if o in drop or o.hide_render or o.type != 'MESH' or re.search(r"(_seam|_mark|hinge_fin|ghost|finger_ring)", base) or base == "grip":
            ndrop += 1; continue
        for md in list(o.modifiers):
            if md.type == 'BEVEL': o.modifiers.remove(md)                  # game: no 1-3 mm bevels (geometry chamfers stay)
        me = o.data; me.transform(o.matrix_world); o.matrix_world = Matrix.Identity(4)
        c = sum((v.co for v in me.vertices), V((0, 0, 0))) / max(1, len(me.vertices))
        cl = classify(base, c.x, cap_names, o.name)
        if cl is None: uncl.append(base); cl = ("Tor", "torso", "Spine1", 1)
        src.append((o, base, cl))
    say("source objects %d, dropped %d (posts, seams, stencils, fins, ghosts, hidden), unclassified %s" % (len(src), ndrop, sorted(set(uncl))))
    # fit the base meshes (before solidify / bevel: wall thicknesses stay physical), band UVs, then apply the modifiers
    dg = bpy.context.evaluated_depsgraph_get()
    for o, base, (lab, kind, rule, s) in src:
        me = o.data
        for v in me.vertices: v.co = xform(kind, s, v.co.copy())
        me.update()
        if "grid_n" in o: grid_uv(o)
    seat_drives(src, arm)
    bpy.context.view_layer.update(); dg = bpy.context.evaluated_depsgraph_get()
    gm = game_materials()
    groups = {}
    for o, base, (lab, kind, rule, s) in src:
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); has_uv = "grid_n" in o and len(me.uv_layers) > 0
        uvd = me.uv_layers.active.data if has_uv else None
        rad = re.match(r"^(fin\d\d|fin_rail|radfield)$", base) is not None
        inner = None
        if o.get("grid_closed"):                                              # closed band: its axis = least-variance direction
            Pm = np.array([v.co for v in me.vertices]); cm_ = Pm.mean(0); w_, vv_ = np.linalg.eigh(np.cov((Pm - cm_).T)); axn = V(vv_[:, 0]); cmv = V(cm_)
            def inner(pg, cmv=cmv, axn=axn):
                r = pg.center - cmv; r -= axn * r.dot(axn); return r.length > 1e-6 and pg.normal.dot(r.normalized()) < -0.5
        for pg in me.polygons:
            if inner and inner(pg): continue
            mn = me.materials[pg.material_index].name if pg.material_index < len(me.materials) and me.materials[pg.material_index] else ""
            code = GM.get(re.sub(r"\.\d+$", "", mn), "Gr")
            if rad and code != "Rd": code = "Ra"
            key = (lab, code); g = groups.setdefault(key, {"bm": bmesh.new(), "maps": {}})
            bm = g["bm"]; uvl = bm.loops.layers.uv.verify(); dfl = bm.verts.layers.deform.verify()
            vm = g["maps"].setdefault(id(me), {})
            vs = []
            for vi in pg.vertices:
                if vi not in vm:
                    p = me.vertices[vi].co.copy(); bv = bm.verts.new(p); vm[vi] = bv
                    for bn, w in weights_of(rule, s, p).items():
                        if w > 1e-4: bv[dfl][bone_index(arm, bn)] = w
                vs.append(vm[vi])
            if len(set(vs)) < 3: continue
            try: f = bm.faces.new(vs)
            except ValueError: continue
            nrm = pg.normal
            for li, loop in zip(pg.loop_indices, f.loops):
                loop[uvl].uv = tuple(uvd[li].uv) if (uvd and GMAT[code][1] and code not in FLAT) else planar_uv(nrm, loop.vert.co, code)
        bpy.data.meshes.remove(me)
    # build the game objects
    coll = bpy.data.collections.new("PantsirGame"); bpy.context.scene.collection.children.link(coll)
    made = []; tv = tt = 0
    for (lab, code), g in sorted(groups.items()):
        bm = g["bm"]
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0002)
        bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(1.0), use_dissolve_boundaries=False, verts=bm.verts, edges=bm.edges, delimit={'UV'})
        bmesh.ops.triangulate(bm, faces=bm.faces, quad_method='BEAUTY', ngon_method='BEAUTY')
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        name = lab + code; me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        me.materials.append(gm[code]); me.shade_smooth(); me.set_sharp_from_angle(angle=math.radians(30))
        o = bpy.data.objects.new(name, me); coll.objects.link(o)
        for b in arm.data.bones: o.vertex_groups.new(name=b.name)
        mod = o.modifiers.new("Armature", 'ARMATURE'); mod.object = arm
        made.append(o); tv += len(me.vertices); tt += len(me.polygons)
    say("game objects %d, %d verts, %d tris" % (len(made), tv, tt))
    for o in made: say("  %-12s v=%-6d t=%-6d mat=%s" % (o.name, len(o.data.vertices), len(o.data.polygons), o.data.materials[0]["orb_name"]))
    # hinge of the cap (Orbiter model frame) and the open rotation
    hb = m_helm(ns["HINGE"]); C = Matrix(((-1, 0, 0), (0, 0, 1), (0, -1, 0)))
    ho = C @ hb - V((0, 0.93, 0)); Rb = Matrix.Rotation(math.radians(-ns["OPEN_DEG"]), 3, 'X'); Ro = C @ Rb @ C.transposed()
    ax = C @ V((1, 0, 0)); q = Ro.to_quaternion(); axis, ang = q.to_axis_angle()
    say("HelmCapHinge (Orbiter frame) %.4f %.4f %.4f; open rotation axis %s angle %.1f deg (blender: -%.0f deg about +X)" % (ho.x, ho.y, ho.z, tuple(round(c, 4) for c in axis), math.degrees(ang), ns["OPEN_DEG"]))
    test = C @ m_helm(V((0, -0.17, 1.72))) - V((0, 0.93, 0)); moved = Ro @ (test - ho) + ho
    say("  check: a cap front point %s -> %s when open (y up, z forward)" % (tuple(round(c, 3) for c in test), tuple(round(c, 3) for c in moved)))
    # remove the generator objects, the old suit and the person from THIS new file only
    for o, _, _ in src: bpy.data.objects.remove(o, do_unlink=True)
    for o in list(ns["COLL"][0].objects): bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.collections.remove(ns["COLL"][0])
    REPORT["old"] = old
    return arm, made


DRIVES = [("hip", ("motor_hip", "cap_hip", "hip_lug"), "UpLeg", ("Th", "Pel")), ("knee", ("motor_knee", "cap_knee"), "Leg", ("Th", "Kn", "Sh")),
          ("ankle", ("motor_ankleL", "motor_ankleR"), "Foot", ("Sh", "Ft")), ("inv", ("motor_ankle_inv", "inv_bracket"), "Foot", ("Ft", "Sh")),
          ("shoulder", ("motor_shoulder", "cap_shoulder"), "Arm", ("Ua",)), ("elbow", ("motor_elbow",), "ForeArm", ("Fa", "Ua"))]
STRUTS = [("leg_upper", "hip", "knee"), ("leg_lower", "knee", "ankle"), ("stirrup", "ankle", None), ("arm_member", "shoulder", "elbow"), ("forearm_member", "elbow", "elbow")]


def seat_drives(src, arm):
    """after the fit: every ring drive back onto its fitted limb - the disc centred on the joint's pivot axis, its inner
    face on the shell's outer surface there (no gap, no penetration); the frame struts follow their two drives"""
    W = arm.matrix_world; delta = {}
    def verts(objs): return [v.co for o in objs for v in o.data.vertices]
    for s_ in (1, -1):
        S, Sd = ("L" if s_ > 0 else "R"), ("Left" if s_ > 0 else "Right")
        for key, prefixes, bone, shells in DRIVES:
            grp = [(o, b) for o, b, cl in src if cl[3] == s_ and b.startswith(prefixes)]
            body = [o for o, b in grp if b.startswith("motor_") and not b.endswith(("_face", "_hub", "_con", "_concap"))]
            if key == "ankle": body = [o for o in body if "_inv" not in o.name]; grp = [(o, b) for o, b in grp if "_inv" not in b]
            if not body: continue
            J = W @ arm.data.bones[Sd + bone].head_local
            P = np.array([tuple(v) for v in verts(body)]); c = V(P.mean(0)); w_, v_ = np.linalg.eigh(np.cov((P - P.mean(0)).T)); n = V(v_[:, 0]).normalized()
            if n.dot(c - J) < 0: n = -n
            sh = [o for o, b, cl in src if cl[3] == s_ and cl[0][:2] in shells and not b.startswith(("motor_", "cap_", "hip_lug", "leg_", "stirrup", "inv_bracket", "arm_member", "forearm_member"))]
            if key == "hip": sh += [o for o, b, cl in src if cl[0] == "Pel" and b.startswith(("brief", "hip_band"))]
            vs, fs = [], []
            for o in sh:
                b0 = len(vs); vs += [v.co.copy() for v in o.data.vertices]; fs += [tuple(b0 + i for i in pg.vertices) for pg in o.data.polygons]
            hit = BVHTree.FromPolygons(vs, fs).ray_cast(J + n * 0.6, -n, 1.2)[0] if fs else None
            if hit is None: say("  seat %s %s: no shell hit" % (key, S)); continue
            surf = (hit - J).dot(n) + 0.004 + 0.001                    # the band's outer face (solidify half thickness) + 1 mm
            m = min((v - c).dot(n) for v in verts(body))               # the drive's inner face relative to its centre
            cn = J + n * (surf - m); d = cn - c
            for o, b in grp:
                o.data.transform(Matrix.Translation(d)); o.data.update()
            delta[(key, s_)] = d
            say("  seat %-8s %s: moved %.3f m (axis %s)" % (key, S, d.length, tuple(round(x, 2) for x in n)))
        for name, top, bot in STRUTS:
            for o, b, cl in src:
                if cl[3] != s_ or not b.startswith(name): continue
                dt = delta.get((top, s_), V((0, 0, 0))); db = delta.get((bot, s_), V((0, 0, 0))) if bot else V((0, 0, 0))
                P = np.array([tuple(v.co) for v in o.data.vertices]); w_, v_ = np.linalg.eigh(np.cov((P - P.mean(0)).T)); ax = V(v_[:, 2])
                pr = [v.co.dot(ax) for v in o.data.vertices]; lo, hi = min(pr), max(pr)
                Jt = W @ arm.data.bones[Sd + {"hip": "UpLeg", "knee": "Leg", "ankle": "Foot", "shoulder": "Arm", "elbow": "ForeArm"}[top]].head_local
                if abs(Jt.dot(ax) - lo) > abs(Jt.dot(ax) - hi): lo, hi = hi, lo   # t = 0 at the top drive
                for v, q in zip(o.data.vertices, pr):
                    t = (q - lo) / (hi - lo) if hi != lo else 0.0; v.co = v.co + dt.lerp(db, min(max(t, 0.0), 1.0))
                o.data.update()


def bone_index(arm, name):
    return BONES[name]


BONES = {}; REPORT = {}; HEADOBJ = []


def body_check(arm, made, body):
    """count body vertices outside the shell (ray from the nearest bone axis to the vertex hits the suit first)"""
    for m in body.modifiers:
        if m.type == 'MASK': m.show_viewport = False
    body.hide_viewport = False
    names = {g.index: g.name for g in body.vertex_groups}
    keep = [v.index for v in body.data.vertices if not v.groups or names[max(v.groups, key=lambda g: g.weight).group] not in ("HelperGeometry", "JointCubes")][::3]
    segs = [("LeftUpLeg", "LeftLeg"), ("LeftLeg", "LeftFoot"), ("RightUpLeg", "RightLeg"), ("RightLeg", "RightFoot"), ("LeftFoot", "LeftToeBase"), ("RightFoot", "RightToeBase"),
            ("LeftArm", "LeftForeArm"), ("LeftForeArm", "LeftHand"), ("RightArm", "RightForeArm"), ("RightForeArm", "RightHand"),
            ("Hips", "LowerBack"), ("LowerBack", "Spine"), ("Spine", "Spine1"), ("Spine1", "Neck"), ("Neck", "Head"), ("Head", None)]
    def run(tag):
        bpy.context.view_layer.update(); dg = bpy.context.evaluated_depsgraph_get()
        vs, fs = [], []
        for o in made:
            me = o.evaluated_get(dg).to_mesh(); b = len(vs); vs += [o.matrix_world @ v.co for v in me.vertices]; fs += [tuple(b + i for i in p.vertices) for p in me.polygons]; o.evaluated_get(dg).to_mesh_clear()
        bvh = BVHTree.FromPolygons(vs, fs)
        me = body.evaluated_get(dg).to_mesh(); P = [body.matrix_world @ me.vertices[i].co for i in keep]; body.evaluated_get(dg).to_mesh_clear()
        W = arm.matrix_world; S = []
        for a, b in segs:
            pa = W @ arm.pose.bones[a].head; pb = W @ (arm.pose.bones[b].head if b else arm.pose.bones[a].tail); S.append((a, pa, pb))
        out = {}
        for p in P:
            best = None
            for n, a, b in S:
                d = b - a; t = max(0.0, min(1.0, (p - a).dot(d) / max(d.length_squared, 1e-9))); c = a + d * t; r = (p - c).length
                if best is None or r < best[0]: best = (r, n, c)
            r, n, c = best; key = re.sub(r"^(Left|Right)", "", n)
            tot = out.setdefault(key, [0, 0]); tot[1] += 1
            if r > 1e-4:
                d = (p - c).normalized(); h = bvh.ray_cast(p + d * 0.001, d, 0.5)
                if h[0] is None: tot[0] += 1                                  # nothing outside it: the body shows
        say("  body uncovered, %s: " % tag + ", ".join("%s %d/%d" % (k, v[0], v[1]) for k, v in sorted(out.items())))
        hp = arm.matrix_world @ arm.pose.bones["Head"].head; nh = 0; nt_ = 0
        for o in HEADOBJ:
            me = o.evaluated_get(dg).to_mesh()
            for v in list(me.vertices)[::2]:
                p = o.matrix_world @ v.co
                if p.z < hp.z - 0.06: continue                                  # the neck and below are inside the collar / torso
                d = (p - hp).normalized(); nt_ += 1
                if bvh.ray_cast(p + d * 0.001, d, 0.5)[0] is None: nh += 1
            o.evaluated_get(dg).to_mesh_clear()
        say("    head/hair points not covered by the closed helmet: %d/%d" % (nh, nt_))
    arm.data.pose_position = 'REST'; run("rest")
    from pose_util import idle_pose
    arm.data.pose_position = 'POSE'; idle_pose(arm); suit_layer(arm); run("idle+layer")
    for pb in arm.pose.bones: pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    for clip, frames in (("35_01", (15, 22, 50, 57)), ("09_01_deltas", (7, 10, 31))):
        with bpy.data.libraries.load(os.path.join(BL, "anim", "anim_%s.blend" % clip)) as (a, b): b.actions = list(a.actions)
        act = b.actions[0]; arm.animation_data_create(); arm.animation_data.action = act
        for f in frames: bpy.context.scene.frame_set(f); suit_layer(arm); run("%s f%d +layer" % (clip, f))
        arm.animation_data.action = None
    for pb in arm.pose.bones: pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0); pb.rotation_euler = (0, 0, 0)
    arm.data.pose_position = 'REST'; bpy.context.scene.frame_set(1); bpy.context.view_layer.update()


try:
    arm0 = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    BONES.update({b.name: i for i, b in enumerate(arm0.data.bones)})
    arm, made = main()
    # the deform layer indices are vertex-group indices: groups were created in bone order, so they match BONES
    body = bpy.data.objects.get("Astronavigator.body")
    if body: body_check(arm, made, body)
    for o in REPORT["old"]:
        try: bpy.data.objects.remove(o, do_unlink=True)
        except Exception: pass
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    arm.data.pose_position = 'REST'
    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, check_existing=False)
    say("saved", OUT_BLEND)
except Exception:
    say(traceback.format_exc())
bpy.ops.wm.quit_blender()
