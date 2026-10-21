# «Панцирь» rev. S -> the skinned game asset (OrbiterCrew), replacing the rev. R build (build_pantsir_game.py, kept).
#   blender -b Tantra_Design\blender\astronavigator_suit.blend --python build_pantsir_game_S.py
#   -> Tantra_Design\blender\astronavigator_pantsir.blend, then export_skin.py with TANTRA_VARIANT=pantsir
# Rev. S is built ON the person (the mockup's figure = rig x 1.046 + 0.07 y; the sleeves on her arm bones, the legs on
# her measured legs), so the fit here is exact, not a per-segment stretch: every part is taken from the mockup's posed
# frame back to the rig's REST frame through its own bone (rest @ pose^-1), then to the rig scale. The boots drop by
# SOLE_T - 5 mm (footbed under her soles: StandHeight 0.989) and the shins stretch to meet them. Radiator: «криогеника»
# (static); helmet louvres open. Groups = label x surface kind (one Orbiter material per kind, the rev. R material set
# and order: Borazon 6, Sole 12, Rad 15). Log: game\build_pantsir_game_S.log
import bpy, bmesh, os, sys, math, re, traceback
import numpy as np
from mathutils import Vector as V, Matrix
HERE = os.path.dirname(os.path.abspath(__file__)); BL = os.path.abspath(os.path.join(HERE, "..", "..", "blender"))
sys.path.insert(0, HERE); sys.path.insert(0, BL)
OUT_BLEND = os.path.join(BL, "astronavigator_pantsir.blend")
LOG = os.path.join(HERE, "build_pantsir_game_S.log"); log = []
TILE = 0.25
FIG_S, FIG_DY = 1.046, 0.07                      # the mockup's figure_head(): rig x 1.046, +0.07 y


def say(*a):
    log.append(" ".join(str(x) for x in a)); open(LOG, "w", encoding="utf-8").write("\n".join(log) + "\n")


def smooth(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0); return t * t * (3 - 2 * t)


GM = {"Pantsir_mirror": "Mi", "Pantsir_borazon": "Ce", "Pantsir_shieldp": "Mi", "Pantsir_ceram": "Ce", "Pantsir_foil": "Mi",
      "Pantsir_shell": "En", "Pantsir_shell2": "En", "Pantsir_door": "En", "Pantsir_lampglass": "En",
      "Pantsir_frame": "Gr", "Pantsir_rim": "Gr", "Pantsir_bnaero": "Gr", "Pantsir_fin": "Gr", "Pantsir_dark": "Gr", "Pantsir_graphite": "Gr",
      "Pantsir_louvre": "Gr", "Pantsir_wzr": "Gr", "Pantsir_iris": "Gr", "Pantsir_stencil_dark": "Gr", "Pantsir_stencil_white": "Gr",
      "Pantsir_aerogel": "So", "Pantsir_tile": "So", "Pantsir_bear": "Be", "Pantsir_hub": "Be", "Pantsir_titan": "Be", "Pantsir_lmetal": "Be",
      "Pantsir_rubber": "Rb", "Pantsir_red": "Rd", "Pantsir_stencil_red": "Rd", "Pantsir_sealor": "Rd",
      "Pantsir_photonic": "Gd", "Pantsir_gold": "Gd", "Pantsir_gold2": "Gd", "Pantsir_lens": "Sl", "Pantsir_germ": "Sl", "Pantsir_slit": "Sl"}
DROP_MATS = ("Pantsir_boraz_glass",)                       # the clear outer pane: the photonic pane carries the window
GMAT = {"Mi": ("Mirror", "surf_mirror", "0.9 0.8 0.9 1 80"), "Ce": ("Borazon", "surf_boraz", "0.6 0.4 0.25 1 60"),
        "So": ("Sole", "surf_sole", "0.05 0.05 0.05 1 5"), "Ra": ("Rad", "surf_rad", "0.3 0.3 0.3 1 20"),
        "En": ("Enamel", "surf_enamel", "0.2 0.2 0.2 1 15"), "Gr": ("Graphite", "surf_graphite", "0.3 0.3 0.3 1 25"),
        "Be": ("Metal", "surf_metal", "0.6 0.6 0.6 1 50"), "Gd": ("GoldVisor", "surf_visor", "1.0 0.85 0.5 1 90"),
        "Rd": ("Red", "surf_red", "0.15 0.1 0.1 1 10"), "Rb": ("Rubber", "surf_rubber", "0.04 0.04 0.04 1 5"),
        "Sl": ("Slit", "surf_slit", "0.8 0.8 0.8 1 80")}
FLAT = ("Rd", "Rb", "Sl")


def game_materials():
    import pantsir_tex
    out = {}
    for code, (name, tex, spec) in GMAT.items():
        m = bpy.data.materials.new("PG_" + name); m.use_nodes = True; nt = m.node_tree
        p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
        img = bpy.data.images.load(os.path.join(pantsir_tex.OUT, tex + ".png"), check_existing=True)
        it = nt.nodes.new("ShaderNodeTexImage"); it.image = img; nt.links.new(it.outputs["Color"], p.inputs["Base Color"])
        p.inputs["Metallic"].default_value = 1.0 if code in ("Mi", "Gd", "Be") else 0.0
        p.inputs["Roughness"].default_value = 0.3 if code in ("Mi", "Gd") else 0.55

        m["orb_name"] = name; m["orb_spec"] = spec; out[code] = m
    return out


def planar_uv(n, p, code):
    if code in FLAT: return (0.5, 0.5)
    ax = max(range(3), key=lambda i: abs(n[i]))
    a, b = ((p.y, p.z) if ax == 0 else (p.x, p.z) if ax == 1 else (p.x, p.y))
    return a / TILE, b / TILE


def classify(base, x, cap):
    """label, bone rule, side"""
    s = 1 if x > 0 else -1; S = "L" if s > 0 else "R"
    if base.startswith("helm_liner"): return "HelmCapLn", "Spine1", s
    m_ = re.match(r"helmet_louvre(\d)$", base)
    if m_: return "HelmCapLv" + m_.group(1), "Spine1", s                         # a closed slat, its own group
    if base.startswith("helmet_louvre_up"): return "HelmCapLu", "Spine1", s     # the open stack
    if cap: return "HelmCap", "Spine1", s
    if base.startswith(("helmet_", "hinge_")) and "_fore" not in base: return "Helm", "Spine1", s
    if base == "collar": return "NeckSeal", "Spine1", s
    if base.startswith(("waist_P0", "waist_rib", "waist_zero")): return "Wst", "waist", s
    if base.startswith(("pelvis_ring",)): return "Pel", "Hips", s
    if base.startswith(("pelvis",)): return "Pel", "pelvis", s
    if base.startswith("hip_bellows"): return "Pel", "hipfold", s
    if base.startswith(("hut", "chest_", "flank", "torso_member", "shoulder_yoke", "motor_shoulder_P2", "backunit", "pump_hatch", "battery_bay",
                        "cassette", "port_cap", "emergency", "handle_", "grab_slot", "rad_", "wing_", "evap_", "waist_skirt")):
        return "Tor", "Spine1", s
    if "_fore" in base: return "To" + S, "Toe", s
    if base.startswith("ankle_bellows"): return "Sh" + S, "Leg", s
    if base.startswith(("boot_upper", "carrier", "sole_", "heel_", "instep", "ankle", "malleolus", "motor_ankle", "inv_bracket", "stirrup")): return "Ft" + S, "Foot", s
    if base.startswith(("knee", "motor_knee")): return "Kn" + S, "knee", s
    if base.startswith(("thigh", "ring_p9", "motor_hip")): return "Th" + S, "UpLeg", s
    if base.startswith(("shin", "calf_pl", "achilles")): return "Sh" + S, "Leg", s
    if base.startswith("leg_member"): return None                                   # side decided per vertex below
    if base.startswith(("elbow_bellows", "elbow_fold")): return "Ua" + S, "elbowfold", s      # the folds bend with the joint
    if base.startswith(("deltoid", "armhole_P1", "shoulder_collar", "ring_P3", "upperarm", "upper_pl", "elbow", "motor_elbow", "arm_member", "arm_link")):
        return "Ua" + S, "Arm", s
    if base.startswith(("forearm", "fore_", "ring_P5", "olecranon")): return "Fa" + S, "ForeArm", s
    if base.startswith(("cuff", "coupling", "capsule", "hand_", "palm_plate", "knuckle", "thenar", "grip_lock", "index", "broad", "thumb")):
        return "Hd" + S, "Hand", s
    return None


def weights_of(rule, s, p):
    """bone weights from the mockup position p (before the transforms)"""
    S = "Left" if s > 0 else "Right"
    if rule == "waist": t = smooth(1.026, 1.060, p.z); return {"Spine1": t, "Hips": 1 - t}
    if rule == "knee": w = smooth(0.578, 0.502, p.z); return {S + "Leg": w, S + "UpLeg": 1 - w}
    if rule == "hipfold": w = smooth(0.975, 0.905, p.z); return {S + "UpLeg": w, "Hips": 1 - w}
    if rule == "pelvis":   # the seat and side plates follow the thigh under them (up to half), so it never comes through them
        lo = smooth(0.875, 0.800, p.z)
        wl = 0.5 * lo * smooth(0.02, 0.09, p.x); wr = 0.5 * lo * smooth(-0.02, -0.09, p.x)
        return {"Hips": 1 - wl - wr, "LeftUpLeg": wl, "RightUpLeg": wr}
    if rule == "elbowfold":   # across the elbow: upper arm before the joint's mid plane, forearm after it, blended over 6 cm
        E = HPOS[S + "ForeArm"]; u = ((E - HPOS[S + "Arm"]).normalized() + (HPOS[S + "Hand"] - E).normalized()).normalized()
        w = smooth(-0.03, 0.03, (p - E).dot(u)); return {S + "ForeArm": w, S + "Arm": 1 - w}
    if rule in ("Spine1", "Hips"): return {rule: 1.0}
    return {S + {"UpLeg": "UpLeg", "Leg": "Leg", "Foot": "Foot", "Toe": "ToeBase", "Arm": "Arm", "ForeArm": "ForeArm", "Hand": "Hand"}[rule]: 1.0}


def main():
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'REST'
    BONES = {b.name: i for i, b in enumerate(arm.data.bones)}
    keep_head = ("female1605", "eyebrow001", "eyelashes01", "low-poly", "toigo_curled_under_bob")
    head = [o for o in bpy.data.objects if o.type == 'MESH' and o.name.split(".")[-1] in keep_head]
    old = [o for o in bpy.data.objects if o.type != 'ARMATURE' and o not in head]
    import pantsir_lib
    ns = pantsir_lib.load(); ns["PORT_N"] = 16
    ns["build"]()
    # the mockup frame: the figure posed on the suit (pose_fit), the bone matrices pose vs rest
    # the game's idle first (pelvis, spine, collarbones as the game shows them), then the arms and legs onto the suit's
    # sleeves: the torso, the shoulders and the hips then meet in the game's stance, not in the rig's rest (2026-10-19)
    from pose_util import idle_pose
    # the suit stands square (no contrapposto: a 130 kg hard suit stands on both legs): the rig's own upright rest for the
    # pelvis, spine and collarbones, the arms and legs onto the sleeves (2026-10-20: the clip's weight-on-the-right idle,
    # baked in here, made it lean right on the walk)
    ns["figure_head"]()
    for pb in arm.pose.bones: pb.rotation_mode = 'QUATERNION'; pb.rotation_quaternion = (1, 0, 0, 0); pb.location = (0, 0, 0)
    bpy.context.view_layer.update(); ns["pose_fit"]()
    Wm = arm.matrix_world.copy(); Tb = {}; Hp = {}; global HPOS; HPOS = Hp
    for pb in arm.pose.bones:
        Tb[pb.name] = (Wm @ pb.bone.matrix_local) @ (Wm @ pb.matrix).inverted(); Hp[pb.name] = (Wm @ pb.matrix).translation.copy()
    # the pose the suit is built in, kept on the armature: export_skin.py retargets the Pantsir's clips onto it (their
    # arms and legs swing about the sleeves' own directions), so the plates meet in the game's stance
    import json
    arm["pz_fit"] = json.dumps({pb.name: [v for row in (Wm @ pb.matrix) for v in row] for pb in arm.pose.bones})
    for side in ("Left", "Right"):                                               # the boots stay level: the foot's translation only
        d_ = Tb[side + "Foot"] @ Hp[side + "Foot"] - Hp[side + "Foot"]
        Tb[side + "Foot"] = Tb[side + "ToeBase"] = Matrix.Translation(d_)
    arm.data.pose_position = 'REST'; arm.scale = (1, 1, 1); arm.location = (0, 0, 0); bpy.context.view_layer.update()
    for o in head: o.hide_render = False
    ns["set_open"](False); ns["set_louvres"](True); ns["set_rad"]("cryo"); ns["set_variant"](4)
    for o in ns["LOUV_CLOSED"]: o.hide_render = False      # (2026-10-20) the closed slats too: animated in the game
    # the helmet's liner (2026-10-20, «внутри всё дырявое»): a closed padded shell round her head inside the hard helmet,
    # its faces inward (from outside it is culled), the window's opening and the neck ring cut out. Rig-frame size, made in
    # the mockup's frame (rig x 1.046 + 0.07 y); the cap's hinge carries it (it is part of the cap)
    import bmesh as _bm
    bm_ = _bm.new(); _bm.ops.create_uvsphere(bm_, u_segments=40, v_segments=24, radius=1.0)
    C_, R_ = V((0.0, -0.052, 1.655)), V((0.088, 0.118, 0.115))   # x: to the shell's inner wall (built tight round the head)   # top 1.770 under the raised crown (inner ~1.773), front at the glass
    for v_ in bm_.verts: v_.co = V((C_.x + v_.co.x * R_.x, C_.y + v_.co.y * R_.y, C_.z + v_.co.z * R_.z))
    kill = [f for f in bm_.faces if (lambda c: (abs(c.x) < 0.080 and 1.604 < c.z < 1.668 and c.y < -0.120) or c.z < 1.565)(f.calc_center_median())]
    _bm.ops.delete(bm_, geom=kill, context='FACES')   # (outward winding here: the export's flip turns it to face her)
    for v_ in bm_.verts: v_.co = V((v_.co.x, v_.co.y, v_.co.z)) * FIG_S + V((0, FIG_DY, 0))
    lme = bpy.data.meshes.new("Pantsir_helm_liner"); bm_.to_mesh(lme); bm_.free(); lme.materials.append(ns["M"]["rubber"])
    lo_ = bpy.data.objects.new("Pantsir_helm_liner", lme); ns["COLL"][0].objects.link(lo_)
    M_ = ns["M"]
    for o in ns["COLL"][0].objects:                                              # the hand: borazon on the back and pinky edge only
        b = o.name.split(".")[0][len("Pantsir_"):]
        if o.type == 'MESH' and b.startswith(("palm_plate", "thenar", "hand_Ac", "hand_K_thumb")):
            for i in range(len(o.data.materials)): o.data.materials[i] = M_["shell"] if i == 0 else M_["frame"]
    cap_names = set(o.name for o in ns["OPENABLE"])
    SOLE_T = ns["SOLE_T"]; DZ = -0.059 * FIG_S                    # the suit's soles 59 mm under hers: StandHeight 0.989
    def dz_leg(z): return DZ * (1.0 - smooth(0.22, 0.50, z))
    # the helmet's crown raised 5 cm over the brow (2026-10-19): it sat right on her scalp - a hard helmet leaves ~4-5 cm
    # over the head (it moves inside, the comms cap) under its 30 mm wall; the window and the brow bar stay
    def dz_head(z): return 0.05 * smooth(1.755, 1.785, z)
    def to_rig(p): return V((p.x, (p.y - FIG_DY), p.z)) / FIG_S
    src = []; ndrop = 0; uncl = []; fonts = []
    for o in list(ns["COLL"][0].objects):
        base = re.sub(r"\.\d+$", "", o.name)[len("Pantsir_"):]
        if o.type == 'FONT' and not o.hide_render: fonts.append(o)          # (uvbake) the stencils: painted into the albedo, see below
        if o.type != 'MESH' or o.hide_render or re.search(r"(_mark|ghost|finger_ring|_post|post_head)", base) or base.startswith(("rad_fin", "port_cap_slot")):
            ndrop += 1; continue
        for md in list(o.modifiers):
            if md.type == 'BEVEL': o.modifiers.remove(md)
        c = o.matrix_world @ (sum((V(b_) for b_ in o.bound_box), V((0, 0, 0))) / 8)
        cl = classify(base, c.x, o.name in cap_names)
        src.append((o, base, cl))
        if cl is None and not base.startswith("leg_member"): uncl.append(base)
    say("source objects %d, dropped %d, unclassified %s" % (len(src), ndrop, sorted(set(uncl))))
    gm = game_materials(); groups = {}; KD_P = []; KD_I = []
    dg = bpy.context.evaluated_depsgraph_get()
    for o, base, cl in src:
        oc = o.matrix_world @ (sum((V(b_) for b_ in o.bound_box), V((0, 0, 0))) / 8)
        whole = base.startswith(("hip_bellows", "elbow_bellows", "knee_bellows", "ankle_bellows"))
        if cl is None:
            if base.startswith("leg_member"): cl = None
            else: cl = ("Tor", "Spine1", 1)
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); me.transform(o.matrix_world)
        for pg in me.polygons:
            mn = me.materials[pg.material_index].name if pg.material_index < len(me.materials) and me.materials[pg.material_index] else ""
            mn = re.sub(r"\.\d+$", "", mn)
            if mn in DROP_MATS: continue
            code = GM.get(mn, "Gr")
            if code == "Gd" and pg.normal.y > -0.3: continue   # the visor pane: its outer face only (tinted glass: no back or edges seen through it)
            if re.match(r"^(rad_tray|rad_rail|wing_fin)", base) and code != "Rd": code = "Ra"
            c_ = pg.center
            if cl is None:                                                       # leg exo member: thigh or shin by height
                s = 1 if c_.x > 0 else -1; S = "L" if s > 0 else "R"
                lab, rule = ("Th" + S, "UpLeg") if c_.z > 0.54 else ("Sh" + S, "Leg")
            else: lab, rule, s = cl
            key = (lab, code); g = groups.setdefault(key, {"bm": bmesh.new(), "maps": {}})
            bm = g["bm"]; uvl = bm.loops.layers.uv.verify(); dfl = bm.verts.layers.deform.verify()
            vm = g["maps"].setdefault(id(me), {}); vs = []
            for vi in pg.vertices:
                if vi not in vm:
                    p = me.vertices[vi].co.copy()
                    if lab.startswith("Helm") and lab != "HelmCapLn": p.z += dz_head(p.z)   # (the liner is sized to the raised helmet)
                    ws = weights_of(rule, s, oc if whole else p)
                    q = V((0, 0, 0))
                    for bn, w in ws.items(): q += (Tb[bn] @ p) * w if bn in Tb else p * w
                    if lab[:2] in ("Sh", "Ft", "To", "Kn") : q.z += dz_leg(p.z)
                    bv = bm.verts.new(to_rig(q)); vm[vi] = bv; KD_P.append(p); KD_I.append((rule, s, lab))
                    for bn, w in ws.items():
                        if w > 1e-4: bv[dfl][BONES[bn]] = w
                vs.append(vm[vi])
            if len(set(vs)) < 3: continue
            try: f = bm.faces.new(vs)
            except ValueError: continue
            f.normal_update()
            for loop in f.loops: loop[uvl].uv = planar_uv(f.normal, loop.vert.co, code)
        bpy.data.meshes.remove(me)
    coll = bpy.data.collections.new("PantsirGame"); bpy.context.scene.collection.children.link(coll)
    # (uvbake, 2026-10-19) the stencils (FONT objects, not exported) taken to the rig frame by the transform of the nearest
    # suit vertex, kept as hidden meshes in "PantsirStencils": uvbake_pantsir.py bakes them into the albedo as paint
    from mathutils import kdtree
    kd = kdtree.KDTree(len(KD_P))
    for i_, p_ in enumerate(KD_P): kd.insert(p_, i_)
    kd.balance(); scoll = bpy.data.collections.new("PantsirStencils"); bpy.context.scene.collection.children.link(scoll); nst = 0
    for o in fonts:
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); me.transform(o.matrix_world)
        if not len(me.vertices): bpy.data.meshes.remove(me); continue
        cen = sum((v.co for v in me.vertices), V((0, 0, 0))) / len(me.vertices)
        rule, s, lab = KD_I[kd.find(cen)[1]]
        for v in me.vertices:
            p = v.co.copy()
            if lab.startswith("Helm"): p.z += dz_head(p.z)
            ws = weights_of(rule, s, p); q = V((0, 0, 0))
            for bn, w in ws.items(): q += (Tb[bn] @ p) * w if bn in Tb else p * w
            if lab[:2] in ("Sh", "Ft", "To", "Kn"): q.z += dz_leg(p.z)
            v.co = to_rig(q)
        so_ = bpy.data.objects.new("Stn_" + re.sub(r"\.\d+$", "", o.name)[len("Pantsir_"):], me); scoll.objects.link(so_)
        so_.hide_render = True; so_["stn_on"] = lab; nst += 1
    say("stencils kept for the bake: %d (collection PantsirStencils, hidden from render/export)" % nst)
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
    say("game objects %d, %d verts, %d tris (suit only; the head groups come on top)" % (len(made), tv, tt))
    for o in made: say("  %-12s v=%-6d t=%-6d mat=%s" % (o.name, len(o.data.vertices), len(o.data.polygons), o.data.materials[0]["orb_name"]))
    # the cap hinge in the Orbiter model frame
    hh = ns["HINGE"].copy(); hh.z += dz_head(hh.z); hb = to_rig(hh); C = Matrix(((-1, 0, 0), (0, 0, 1), (0, -1, 0))); ho = C @ hb - V((0, 0.93, 0))
    say("HelmCapHinge (Orbiter frame) of rev. S: %.4f %.4f %.4f  (cfg now: 0 0.8745 -0.0943); axis (-1,0,0), 65 deg" % (ho.x, ho.y, ho.z))
    zs = [v.co.z for o in made for v in o.data.vertices]
    say("suit z range (rig) %.4f .. %.4f -> StandHeight %.4f (origin 0.93)" % (min(zs), max(zs), 0.93 - min(zs)))
    for o, _, _ in src:
        try: bpy.data.objects.remove(o, do_unlink=True)
        except Exception: pass
    for o in list(ns["COLL"][0].objects): bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.collections.remove(ns["COLL"][0])
    for o in old:
        try: bpy.data.objects.remove(o, do_unlink=True)
        except Exception: pass
    for o in list(bpy.data.objects):
        if o.type in ('LIGHT', 'CAMERA', 'CURVE', 'FONT') and o.name.startswith(("Pantsir_", "RimP", "SunKey")):
            bpy.data.objects.remove(o, do_unlink=True)
    for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    arm.data.pose_position = 'REST'
    return arm, made


try:
    arm, made = main()
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)
    bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND, check_existing=False)
    say("saved", OUT_BLEND)
except Exception:
    say(traceback.format_exc())
bpy.ops.wm.quit_blender()
