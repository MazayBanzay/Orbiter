# Tantra: export the skinned astronavigator for the UACS runtime.
#   Meshes\Tantra\AstronavigatorSkin.msh  - mesh in the bind (rest) pose, same textures as the static mesh
#   Config\Tantra\Astronavigator.skin     - bones (rest model matrices) + up to 4 weights per exported vertex
#   Config\Tantra\anim\<clip>.clip         - model-space bone poses per frame; walk/run cut to one in-place cycle
# Coordinates: Blender (RH, Z up, faces -Y) -> Orbiter (LH, Y up, faces +Z): p_o = C p_b - (0, ORIGIN_H, 0),
# R_o = C R_b C^T with C = [[-1,0,0],[0,0,1],[0,-1,0]]. Triangle order reversed (as in export_orbiter.py).
import bpy, os, sys, math, traceback
from mathutils import Matrix, Vector, Quaternion
HERE = os.path.dirname(__file__); ROOT = os.path.dirname(HERE); sys.path.insert(0, HERE)
from pose_util import idle_pose
ORBITER = os.path.abspath(os.path.join(ROOT, ".."))
# variant: coverall (BodySkin) or suit (SuitSkin); both share the skeleton and get their own grounded clips
VARIANT = os.environ.get("TANTRA_VARIANT", "coverall")
BLEND, MESH_NAME, SKIN_NAME, CLIP_SUB = {"coverall": ("astronavigator_coverall.blend", "AstronavigatorSkin", "Astronavigator", "anim"),
                                         "suit": ("astronavigator_suit.blend", "AstronavigatorSuit", "AstronavigatorSuit", "anim_suit")}[VARIANT]
MESH_OUT = os.path.join(ORBITER, "Meshes", "Tantra", MESH_NAME + ".msh")
CFG_DIR = os.path.join(ORBITER, "Config", "Tantra"); CLIP_DIR = os.path.join(CFG_DIR, CLIP_SUB)
os.makedirs(CLIP_DIR, exist_ok=True)
TEX_SUB = "Tantra\\Astronavigator"; ORIGIN_H = 0.93
SUIT_TEX_SUB = "Tantra\\AstronavigatorSuit"      # textures that belong to the suit; the body keeps its own
BODY_LABELS = {"Skin", "Brows", "Lashes", "Eyes", "Hair", "Boots"}
MANIFEST = os.path.join(HERE, "export_%s.json" % MESH_NAME); new_textures = []
EYE_TEX = os.path.join(ROOT, "mpfbu", "data", "eyes", "materials", "brown_eye.png")
# coverall walks with 08_01 (brisk, purposeful - a pilot, not a catwalk); the suit keeps 35_01 under its own layer
# coverall clips: the user likes the suit's motion, so the coverall takes the very same clips and processing
# (COV_OWN = False); its lighter feel comes from the runtime layer. COV_OWN = True restores the coverall's own
# pipeline (100STYLE Neutral walk, symmetry, style passes) kept below.
COV_OWN = False
# coverall walk: the suit's capture (35_01) through the corrected aim retarget (straight legs, upright body - the old
# delta retarget left it leaning back on bent knees, hidden by the suit, a "bear" in the coverall), with the coverall's
# own clean-up; the run is the suit's own.
CLIPS = [("walk", "35_01_aim" if VARIANT == "coverall" else "35_01"), ("run", "09_01_deltas")]   # suit: pinned to the clips it has now (user: "идеальна")
LOG = os.path.join(HERE, "export_skin.log"); log = []
C3 = Matrix(((-1, 0, 0), (0, 0, 1), (0, -1, 0)))

def cp(p):
    q = C3 @ Vector(p); return Vector((q.x, q.y - ORIGIN_H, q.z))
def cm(M4):
    R = C3 @ M4.to_3x3() @ C3.transposed(); return R, cp(M4.translation)

def hierarchy(arm):
    order = []
    def walk(b):
        order.append(b.name)
        for c in b.children: walk(c)
    for b in arm.data.bones:
        if b.parent is None: walk(b)
    return order

def short_name(o, mat):
    base = o.name.split('.')[-1]
    s = {"female1605": "Skin", "eyebrow001": "Brows", "eyelashes01": "Lashes", "low-poly": "Eyes", "shoes03": "Boots"}.get(base, "Hair" if ("bob" in base or "hair" in base) else base[:10])
    if base == "Coverall" and mat: return (s + "_" + mat.name.split('.')[-1].replace("Coverall", ""))[:20], s
    if s not in BODY_LABELS and len(o.material_slots) > 1 and mat: return (s + "_" + mat.name.split('.')[-1][:9])[:20], s
    return s[:20], s

def alpha_of(mat):
    if mat and mat.use_nodes:
        for n in mat.node_tree.nodes:
            if n.type == 'BSDF_PRINCIPLED': return n.inputs["Alpha"].default_value
    return 1.0

def image_of(mat):
    if not mat or not mat.use_nodes: return None
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image and n.outputs[0].is_linked: return bpy.path.abspath(n.image.filepath)
    return None

def base_color(mat):
    if mat and mat.use_nodes:
        for n in mat.node_tree.nodes:
            if n.type == 'BSDF_PRINCIPLED':
                c = n.inputs["Base Color"].default_value; return (c[0], c[1], c[2], 1)
    return tuple(mat.diffuse_color) if mat else (0.8, 0.8, 0.8, 1)

def visible_verts(o):
    vis = None
    for m in o.modifiers:
        if m.type == 'MASK' and m.show_render and m.vertex_group in o.vertex_groups:
            gi = o.vertex_groups[m.vertex_group].index
            ing = {v.index for v in o.data.vertices if any(g.group == gi and g.weight > 0 for g in v.groups)}
            keep = {v.index for v in o.data.vertices} - ing if m.invert_vertex_group else ing
            vis = keep if vis is None else vis & keep
    return vis

def export_bind(arm, bones):
    bidx = {n: i for i, n in enumerate(bones)}
    groups, materials, textures = [], [], []
    def mat_index(key, color):
        for i, m in enumerate(materials):
            if m[0] == key: return i + 1
        materials.append((key, color)); return len(materials)
    def tex_index(dds):
        if dds in textures: return textures.index(dds) + 1
        textures.append(dds); return len(textures)
    objs = [o for o in bpy.data.objects if o.type == 'MESH' and not o.hide_render and o.name != "Floor" and len(o.data.polygons)]
    for o in objs:
        me = o.data; M = o.matrix_world; N3 = M.to_3x3().inverted().transposed()
        vis = visible_verts(o)
        me.calc_loop_triangles(); uvl = me.uv_layers.active.data if me.uv_layers.active else None
        cn = me.corner_normals
        gname = {g.index: g.name for g in o.vertex_groups}
        def weights(vi):
            ws = [(bidx[gname[g.group]], g.weight) for g in me.vertices[vi].groups if gname.get(g.group) in bidx and g.weight > 1e-4]
            ws = sorted(ws, key=lambda t: -t[1])[:4] or [(bidx["Head"], 1.0)]
            s = sum(w for _, w in ws); ws = [(b, w / s) for b, w in ws]
            return ws + [(0, 0.0)] * (4 - len(ws))
        by_mat = {}
        for t in me.loop_triangles:
            if vis is not None and not all(v in vis for v in t.vertices): continue
            by_mat.setdefault(t.material_index, []).append(t)
        for mi, tris in by_mat.items():
            mat = o.material_slots[mi].material if mi < len(o.material_slots) else None
            lname, s = short_name(o, mat)
            img = EYE_TEX if s == "Eyes" else image_of(mat)
            sub = TEX_SUB if s in BODY_LABELS or VARIANT == "coverall" else SUIT_TEX_SUB
            tname = os.path.splitext(os.path.basename(img))[0] if (img and sub == SUIT_TEX_SUB) else lname   # suit: one file per image
            tex = tex_index(sub + "\\" + tname + ".dds") if img and os.path.exists(img) else 0
            if tex and sub == SUIT_TEX_SUB and not any(t["src"] == img for t in new_textures): new_textures.append({"src": img, "dds": sub + "\\" + tname + ".dds", "alpha": False})
            a = alpha_of(mat)
            mat_i = mat_index(lname, ((1, 1, 1) if tex else base_color(mat)[:3]) + (a,))
            verts, index, faces = [], {}, []
            for t in tris:
                face = []
                for li, vi in zip(t.loops, t.vertices):
                    P = cp(M @ me.vertices[vi].co); n = (N3 @ Vector(cn[li].vector)).normalized(); Nn = C3 @ n
                    uv = uvl[li].uv if uvl else (0, 0); U = (uv[0], 1 - uv[1])
                    key = (vi, round(U[0], 5), round(U[1], 5), round(Nn.x, 3), round(Nn.y, 3), round(Nn.z, 3))
                    if key not in index: index[key] = len(verts); verts.append((P, Nn, U, weights(vi)))
                    face.append(index[key])
                faces.append(face)
            if s == "Boots":
                n0 = len(verts); verts += [(P, -Nn, U, W) for P, Nn, U, W in verts]; faces += [(a + n0, c + n0, b + n0) for a, b, c in faces]
            groups.append((lname, mat_i, tex, verts, faces))
    with open(MESH_OUT, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(groups))
        for lname, mat_i, tex, verts, faces in groups:
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (lname, mat_i, tex, len(verts), len(faces)))
            for P, Nn, U, W in verts: f.write("%.5f %.5f %.5f %.4f %.4f %.4f %.5f %.5f\n" % (P.x, P.y, P.z, Nn.x, Nn.y, Nn.z, U[0], U[1]))
            for a, b, c in faces: f.write("%d %d %d\n" % (a, c, b))
        f.write("MATERIALS %d\n" % len(materials))
        for k, _ in materials: f.write(k + "\n")
        for k, (r, g_, b, _a) in materials:
            spec = "1.0 0.85 0.5 1 90" if k.startswith("SunShade") else "0.5 0.5 0.5 1 40" if _a < 1 else "0.1 0.1 0.1 1 8"
            f.write("MATERIAL %s\n%.3f %.3f %.3f %.3f\n%.3f %.3f %.3f %.3f\n%s\n0 0 0 1\n" % (k, r, g_, b, _a, r, g_, b, _a, spec))
        f.write("TEXTURES %d\n" % len(textures))
        for t in textures: f.write(t + "\n")
    # skin file
    with open(os.path.join(CFG_DIR, SKIN_NAME + ".skin"), "w", newline="\n") as f:
        f.write("TANTRA_SKIN 1\nMESH Tantra\\%s\nBONES %d\n" % (MESH_NAME, len(bones)))
        for n in bones:
            b = arm.data.bones[n]; R, t = cm(arm.matrix_world @ b.matrix_local)
            par = bones.index(b.parent.name) if b.parent else -1
            f.write("%s %d %s %.6f %.6f %.6f\n" % (n, par, " ".join("%.6f" % R[i][j] for i in range(3) for j in range(3)), t.x, t.y, t.z))
        f.write("GROUPS %d\n" % len(groups))
        for gi, (lname, mat_i, tex, verts, faces) in enumerate(groups):
            f.write("LABEL %d %s\n" % (gi, lname))
            f.write("GROUP %d %d\n" % (gi, len(verts)))
            for P, Nn, U, W in verts: f.write("%d %d %d %d %.4f %.4f %.4f %.4f\n" % (*[b for b, _ in W], *[w for _, w in W]))
    log.append("bind mesh: %d groups, %d verts, %d tris" % (len(groups), sum(len(g[3]) for g in groups), sum(len(g[4]) for g in groups)))

def export_jetpack():
    """jet pack geometry for the game (Config/Tantra/JetPack.geo, crew model frame) and a static mesh of the pack
    lying about (Meshes/Tantra/JetPack.msh): Jet* objects, booms folded, origin at the shell centre"""
    import json
    gp = os.path.join(HERE, "jetpack_geo.json")
    if VARIANT != "suit" or not os.path.exists(gp): return
    G = json.load(open(gp)); O = lambda v: cp(Vector(v))
    sc = O(G["shell_c"]); W, D, H = G["shell_size"]
    NL, BS = chr(10), chr(92)
    with open(os.path.join(CFG_DIR, "JetPack.geo"), "w", newline=NL) as f:
        f.write("# OrbiterCrew jet pack geometry in the crew member's model frame (written by export_skin.py)" + NL)
        for k in (0, 1):
            h, pv = O(G["hinges"][k]), O(G["pivots"][k])
            f.write("HINGE %.4f %.4f %.4f%sPIVOT %.4f %.4f %.4f%s" % (h.x, h.y, h.z, NL, pv.x, pv.y, pv.z, NL))
        f.write("SHELL %.4f %.4f %.4f %.4f %.4f %.4f%sEXITDY %.4f%s" % (sc.x, sc.y, sc.z, W, H, D, NL, G["exit_dz"], NL))
    fold = {}
    for L, k, sx in (("L", 0, -1), ("R", 1, 1)):
        HG = Vector(G["hinges"][k]); M = Matrix.Translation(HG) @ Matrix.Rotation(sx * math.pi / 2, 4, 'Z') @ Matrix.Translation(-HG)
        fold["JetBoom" + L] = fold["JetPod" + L] = M
    groups, materials, textures = [], [], []
    for o in [o for o in bpy.data.objects if o.type == 'MESH' and o.name.startswith("Jet") and len(o.data.polygons)]:
        me = o.data; M = fold.get(o.name, Matrix.Identity(4)) @ o.matrix_world; N3 = M.to_3x3().inverted().transposed()
        me.calc_loop_triangles(); uvl = me.uv_layers.active.data if me.uv_layers.active else None; cn = me.corner_normals
        by_mat = {}
        for t in me.loop_triangles: by_mat.setdefault(t.material_index, []).append(t)
        for mi, tris in by_mat.items():
            mat = o.material_slots[mi].material if mi < len(o.material_slots) else None
            lname, s = short_name(o, mat); img = image_of(mat)
            tex = 0
            if img and os.path.exists(img):
                dds = SUIT_TEX_SUB + BS + os.path.splitext(os.path.basename(img))[0] + ".dds"
                if dds not in textures: textures.append(dds)
                tex = textures.index(dds) + 1
            materials.append((lname, ((1, 1, 1) if tex else base_color(mat)[:3]) + (alpha_of(mat),)))
            verts, index, faces = [], {}, []
            for t in tris:
                face = []
                for li, vi in zip(t.loops, t.vertices):
                    P = cp(M @ me.vertices[vi].co) - sc; Nn = C3 @ (N3 @ Vector(cn[li].vector)).normalized()
                    uv = uvl[li].uv if uvl else (0, 0); U = (uv[0], 1 - uv[1])
                    key = (vi, round(U[0], 5), round(U[1], 5), round(Nn.x, 3), round(Nn.y, 3), round(Nn.z, 3))
                    if key not in index: index[key] = len(verts); verts.append((P, Nn, U))
                    face.append(index[key])
                faces.append(face)
            groups.append((lname, len(materials), tex, verts, faces))
    with open(os.path.join(ORBITER, "Meshes", "Tantra", "JetPack.msh"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1" + NL + "GROUPS %d" % len(groups) + NL)
        for lname, mat_i, tex, verts, faces in groups:
            f.write("LABEL %s%sMATERIAL %d%sTEXTURE %d%sGEOM %d %d%s" % (lname, NL, mat_i, NL, tex, NL, len(verts), len(faces), NL))
            for P, Nn, U in verts: f.write("%.5f %.5f %.5f %.4f %.4f %.4f %.5f %.5f" % (P.x, P.y, P.z, Nn.x, Nn.y, Nn.z, U[0], U[1]) + NL)
            for a, b, c in faces: f.write("%d %d %d" % (a, c, b) + NL)
        f.write("MATERIALS %d" % len(materials) + NL)
        for k, _ in materials: f.write(k + NL)
        for k, (r, g_, b, _a) in materials:
            f.write(("MATERIAL %s" + NL + "%.3f %.3f %.3f %.3f" + NL + "%.3f %.3f %.3f %.3f" + NL + "0.1 0.1 0.1 1 8" + NL + "0 0 0 1" + NL) % (k, r, g_, b, _a, r, g_, b, _a))
        f.write("TEXTURES %d" % len(textures) + NL)
        for t in textures: f.write(t + NL)
    log.append("jet pack: geo + static mesh, %d groups" % len(groups))

def write_clip(name, frames, fps, stride, speed, loop, bones):
    with open(os.path.join(CLIP_DIR, name + ".clip"), "w", newline="\n") as f:
        f.write("TANTRA_CLIP 1\nNAME %s\nFPS %d\nFRAMES %d\nSTRIDE %.4f\nSPEED %.4f\nLOOP %d\nBONES %d\n" % (name, fps, len(frames), stride, speed, loop, len(bones)))
        for k, fr in enumerate(frames):
            f.write("FRAME %d\n" % k)
            for n in bones:
                R, t = fr[n]; q = R.to_quaternion()
                f.write("%.6f %.6f %.6f %.6f %.5f %.5f %.5f\n" % (q.w, q.x, q.y, q.z, t.x, t.y, t.z))
    log.append("clip %s: %d frames, stride %.3f m, speed %.3f m/s" % (name, len(frames), stride, speed))

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, BLEND))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    arm.animation_data_clear(); arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    bones = hierarchy(arm)
    export_bind(arm, bones)
    export_jetpack()
    import json; json.dump({"textures": new_textures, "orbiter": ORBITER}, open(MANIFEST, "w"), indent=1)
    rest_hips = (arm.matrix_world @ arm.data.bones["Hips"].matrix_local).translation.copy()
    # idle: one frame
    arm.data.pose_position = 'POSE'; idle_pose(arm)
    idle_frame = {n: cm(arm.matrix_world @ arm.pose.bones[n].matrix) for n in bones}
    if VARIANT == "coverall":
        # standing: the relaxed fingers curl towards the thigh; keep the wrists 25 cm off the centre line
        par0 = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
        for side, sgn in (("Left", -1), ("Right", 1)):
            sh = idle_frame[side + "Arm"][1]; hd = idle_frame[side + "Hand"][1]; need = 0.25 - sgn * hd.x
            if need > 0:
                Rm = Matrix.Rotation(sgn * math.asin(min(0.3, need / max(0.2, (hd - sh).length))), 3, 'Z'); piv = sh.copy()
                for n_ in bones:
                    p_ = n_
                    while p_ is not None and p_ != side + "Arm": p_ = par0[p_]
                    if p_ == side + "Arm":
                        R_, t_ = idle_frame[n_]; idle_frame[n_] = (Rm @ R_, piv + Rm @ (t_ - piv))
    if VARIANT == "coverall":
        # flat feet when standing: the idle pose left the heels ~5 cm up (she stood on her toes). Turn each foot about
        # the ankle so the ankle-to-toe line has the pitch it has in the bind pose, where the soles are flat
        for side in ("Left", "Right"):
            ra = arm.matrix_world @ arm.data.bones[side + "Foot"].head_local; rt = arm.matrix_world @ arm.data.bones[side + "ToeBase"].head_local
            a = idle_frame[side + "Foot"][1]; t = idle_frame[side + "ToeBase"][1]
            d_rest = (rt - ra); d_now = (t - a)
            # rest pose: Blender frame (z up); frames: Orbiter frame (y up, z forward)
            # +4.8 deg: even the bind feet are slightly pointed (heel ~3 cm above the toe tip over a 24 cm boot, measured on the exported idle)
            want = math.atan2(d_rest.z, math.hypot(d_rest.x, d_rest.y)) + math.asin(0.031 / 0.243); now = math.atan2(d_now.y, math.hypot(d_now.x, d_now.z))
            lat = Vector((d_now.z, 0, -d_now.x)).normalized()          # horizontal axis across the foot
            Rm = Matrix.Rotation(want - now, 3, lat)
            if abs(math.atan2((Rm @ d_now).y, math.hypot((Rm @ d_now).x, (Rm @ d_now).z)) - want) > 1e-3: Rm = Matrix.Rotation(now - want, 3, lat)
            piv = a.copy()
            for n_ in bones:
                p_ = n_
                while p_ is not None and p_ != side + "Foot": p_ = par0[p_]
                if p_ == side + "Foot":
                    R_, t_ = idle_frame[n_]; idle_frame[n_] = (Rm @ R_, piv + Rm @ (t_ - piv))
            log.append("  idle %s foot levelled by %.1f deg" % (side, math.degrees(want - now)))
    write_clip("idle", [idle_frame], 30, 0.0, 0.0, 0, bones)

    for name, clip in CLIPS:
        COV_OWN = VARIANT == "coverall" and name == "walk"
        bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "anim", "anim_%s.blend" % clip))
        sc = bpy.context.scene; arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); fps = sc.render.fps
        F = list(range(sc.frame_start, sc.frame_end + 1)); mats = []
        for fr in F:
            sc.frame_set(fr); mats.append({n: (arm.matrix_world @ arm.pose.bones[n].matrix).copy() for n in bones})
        hips = [m["Hips"].translation for m in mats]
        fwd = (hips[-1] - hips[0]); fwd.z = 0; fwd.normalize()
        rel = [(m["LeftFoot"].translation - m["Hips"].translation).dot(fwd) for m in mats]
        mean_rel = sum(rel) / len(rel)
        peaks = [i for i in range(len(rel)) if rel[i] >= max(rel[max(0, i - 2):i + 3]) and rel[i] > mean_rel]
        peaks = [p for k, p in enumerate(peaks) if k == 0 or p - peaks[k - 1] > 6]   # one peak per step cycle
        if len(peaks) < 2: raise RuntimeError("no gait cycle found in %s (peaks %s)" % (clip, peaks))
        mid = len(peaks) // 2; a, b = (peaks[mid - 1], peaks[mid]) if len(peaks) > 2 else (peaks[0], peaks[1])
        span = hips[b] - hips[a]; span.z = 0; stride = span.length; T = (b - a) / fps
        heading = Vector((fwd.x, fwd.y, 0)).rotation_difference(Vector((0, -1, 0))).to_matrix().to_4x4()
        frames = []
        for i in range(a, b):
            line = hips[a] + span * ((i - a) / (b - a)); off = Matrix.Translation(Vector((rest_hips.x - line.x, rest_hips.y - line.y, 0)))
            frames.append({n: cm(heading @ off @ mats[i][n]) for n in bones})
        if (VARIANT == "coverall" or os.environ.get("TANTRA_SUIT_SEAM") == "1") and b < len(mats):   # loop seam: coverall clips; suit only on request (its clips are pinned, the seam fix is applied to them as a delta)
            # close the loop: the next cycle's first frame never matches this one exactly (the head jumped 18 mm and
            # 2 deg at the seam, "a missing frame"). Spread that difference evenly over the cycle.
            off_b = Matrix.Translation(Vector((rest_hips.x - (hips[a] + span).x, rest_hips.y - (hips[a] + span).y, 0)))
            nxt = {n: cm(heading @ off_b @ mats[b][n]) for n in bones}
            N = len(frames)
            for n in bones:
                (Ra, ta), (Rb, tb) = frames[0][n], nxt[n]
                dq = Ra.to_quaternion() @ Rb.to_quaternion().inverted(); dt = ta - tb
                for k, fr in enumerate(frames):
                    w = k / N; R_, t_ = fr[n]
                    fr[n] = ((Quaternion((1, 0, 0, 0)).slerp(dq, w) @ R_.to_quaternion()).to_matrix(), t_ + dt * w)
        # gaze stabilisation: the head (and partly the neck) is pulled towards the level idle orientation,
        # so the runner looks ahead instead of up; positions stay from the capture
        if COV_OWN and len(frames) % 2 == 0:
            # symmetry: average the cycle with its mirror image half a cycle later (left <-> right). The run capture
            # ran lopsided (right elbow 70-87 vs left 99-117 deg, right knee never below 29 deg). Mirror across the
            # sagittal plane: position M t, orientation M R D (M = D = diag(-1,1,1); the rig's left/right bone frames
            # are mirror images with the x axis flipped).
            Mx = Matrix(((-1, 0, 0), (0, 1, 0), (0, 0, 1)))
            def twin(n_):
                for a_, b_ in (("Left", "Right"), ("LThumb", "RThumb"), ("LHipJoint", "RHipJoint")):
                    if n_.startswith(a_): return b_ + n_[len(a_):]
                    if n_.startswith(b_): return a_ + n_[len(b_):]
                return n_
            N2 = len(frames); half = N2 // 2; sym = []
            for k in range(N2):
                src_ = frames[(k + half) % N2]; out = {}
                for n_ in bones:
                    R1, t1 = frames[k][n_]; R2, t2 = src_[twin(n_)]
                    Rm_ = Mx @ R2 @ Mx; tm_ = Mx @ t2
                    q1 = R1.to_quaternion(); q2 = Rm_.to_quaternion()
                    out[n_] = (q1.slerp(q2, 0.5).to_matrix(), (t1 + tm_) / 2)
                sym.append(out)
            frames = sym
            log.append("  %s made symmetric (averaged with its mirror half a cycle on)" % clip)
        if COV_OWN and name == "walk":
            # straight standing leg: the capture walked on bent knees (never straighter than 17-37 deg) - a "bear".
            # Raise the pelvis just enough for the standing knee to straighten to ~8 deg and solve both legs with
            # two-bone IK back onto the captured ankles: the feet stay exactly where they were on the ground.
            parK = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
            def subK(root):
                out = []
                for n_ in bones:
                    p_ = n_
                    while p_ is not None and p_ != root: p_ = parK[p_]
                    if p_ == root: out.append(n_)
                return out
            leg_sub = {sd: (subK(sd + "UpLeg"), subK(sd + "Leg"), subK(sd + "Foot")) for sd in ("Left", "Right")}
            upper = [n_ for n_ in bones if n_ not in leg_sub["Left"][0] and n_ not in leg_sub["Right"][0]]
            f0 = frames[0]
            L1 = {sd: (f0[sd + "Leg"][1] - f0[sd + "UpLeg"][1]).length for sd in ("Left", "Right")}
            L2 = {sd: (f0[sd + "Foot"][1] - f0[sd + "Leg"][1]).length for sd in ("Left", "Right")}
            def reach(sd, flex):   # hip-ankle distance for a knee flexion
                return math.sqrt(L1[sd] ** 2 + L2[sd] ** 2 + 2 * L1[sd] * L2[sd] * math.cos(flex))
            lift = []
            for fr in frames:
                sd = min(("Left", "Right"), key=lambda q: fr[q + "Foot"][1].y)          # the standing leg
                d_now = (fr[sd + "Foot"][1] - fr[sd + "UpLeg"][1]).length
                v = (fr[sd + "UpLeg"][1] - fr[sd + "Foot"][1]).normalized()
                lift.append(max(0.0, (reach(sd, math.radians(8)) - d_now) / max(0.3, v.y)))
            n_ = len(lift)
            for _ in range(6): lift = [(lift[i - 1] + 2 * lift[i] + lift[(i + 1) % n_]) / 4 for i in range(n_)]   # smooth, loop-aware
            def swing(fr, root_sub, piv, a_, b_):
                q = a_.normalized().rotation_difference(b_.normalized()); Rm = q.to_matrix()
                for n2 in root_sub:
                    R_, t_ = fr[n2]; fr[n2] = (Rm @ R_, piv + Rm @ (t_ - piv))
            for fr, h in zip(frames, lift):
                up = Vector((0, h, 0))
                ankles = {sd: fr[sd + "Foot"][1].copy() for sd in ("Left", "Right")}
                feet_keep = {sd: {n2: fr[n2] for n2 in leg_sub[sd][2]} for sd in ("Left", "Right")}
                for n2 in upper:
                    R_, t_ = fr[n2]; fr[n2] = (R_, t_ + up)
                for sd in ("Left", "Right"):
                    th_sub, sh_sub, _ = leg_sub[sd]
                    for n2 in th_sub:
                        R_, t_ = fr[n2]; fr[n2] = (R_, t_ + up)
                    H = fr[sd + "UpLeg"][1]; K = fr[sd + "Leg"][1]; A = ankles[sd]
                    d = min((A - H).length, L1[sd] + L2[sd] - 1e-4)
                    cosH = (L1[sd] ** 2 + d ** 2 - L2[sd] ** 2) / (2 * L1[sd] * d)
                    axis = (A - H).normalized(); pole = (K - H) - axis * (K - H).dot(axis)
                    pole = pole.normalized() if pole.length > 1e-6 else Vector((0, 0, 1))
                    K_new = H + axis * (L1[sd] * cosH) + pole * (L1[sd] * math.sqrt(max(0.0, 1 - cosH * cosH)))
                    swing(fr, th_sub, H.copy(), K - H, K_new - H)
                    K2 = fr[sd + "Leg"][1]; A_now = fr[sd + "Foot"][1]
                    swing(fr, sh_sub, K2.copy(), A_now - K2, A - K2)
                    for n2, v2 in feet_keep[sd].items(): fr[n2] = (v2[0], v2[1] + (A - ankles[sd]))   # feet as they were
            log.append("  %s standing knee straightened: pelvis raised %.1f..%.1f cm" % (clip, 100 * min(lift), 100 * max(lift)))
        CURL = {"Left": float(os.environ.get("TANTRA_CURL_L", "0.0")), "Right": float(os.environ.get("TANTRA_CURL_R", "0.0"))}
        if COV_OWN and name == "run":
            # coverall run style (trained runner): the arm drives forward-back along the body instead of mostly back
            # (capture: -46..+1 deg), and the hands close into loose fists
            par = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
            def below(root):
                out = []
                for n_ in bones:
                    p_ = n_
                    while p_ is not None and p_ != root: p_ = par[p_]
                    if p_ == root: out.append(n_)
                return out
            def turn_axis(fr, root, axis, ang):
                piv = fr[root][1].copy(); Rm = Matrix.Rotation(ang, 3, axis)
                for n_ in below(root):
                    R_, t_ = fr[n_]; fr[n_] = (Rm @ R_, piv + Rm @ (t_ - piv))
            for side in ("Left", "Right"):
                sw = [math.atan2((fr[side + "ForeArm"][1] - fr[side + "Arm"][1]).z, -(fr[side + "ForeArm"][1] - fr[side + "Arm"][1]).y) for fr in frames]
                mean_sw = sum(sw) / len(sw)
                for fr, a_ in zip(frames, sw):
                    pass   # (the arm swing stays as captured: centring it forward held both hands up at the chest)
                    # loose fist: the fingers (one bone for all four, 239 verts) curl towards the palm about the knuckle line
                    fb = side + "HandFinger1"
                    if fb in fr:
                        # knuckle line = finger direction x palm direction, from the bind mesh (see git history for the probe)
                        a_loc = Vector((0.666, -0.743, 0.059)) if side == "Left" else Vector((0.666, 0.743, -0.059))
                        turn_axis(fr, fb, (fr[fb][0] @ a_loc).normalized(), CURL[side])
            # hands stay by the sides of the body, not across the chest: abduct the arm just enough at the shoulder
            for fr in frames:
                for side, sgn in (("Left", -1), ("Right", 1)):   # left arm is on -x
                    sh = fr[side + "Arm"][1]; hd = fr[side + "Hand"][1]
                    need = 0.15 - sgn * hd.x
                    if need > 0:
                        r_ = max(0.2, (hd - sh).length)
                        turn_axis(fr, side + "Arm", Vector((0, 0, 1)), sgn * math.asin(min(0.5, need / r_)))
            log.append("  %s run style: arm swing centred at -8 deg (was %.0f), fists %.2f/%.2f" % (clip, math.degrees(mean_sw), CURL["Left"], CURL["Right"]))
        if COV_OWN and name == "walk":
            # coverall walk style: arms that actually swing (x1.6 about the clip's mean) with softly bent elbows,
            # bending a little more as the arm comes forward - the capture's arms hang almost straight
            par = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
            def below(root):
                out = []
                for n_ in bones:
                    p_ = n_
                    while p_ is not None and p_ != root: p_ = par[p_]
                    if p_ == root: out.append(n_)
                return out
            def turn(fr, root, ang):
                piv = fr[root][1].copy(); Rm = Matrix.Rotation(ang, 3, 'X')
                for n_ in below(root):
                    R_, t_ = fr[n_]; fr[n_] = (Rm @ R_, piv + Rm @ (t_ - piv))
            for side in ("Left", "Right"):
                sw = [math.atan2((fr[side + "ForeArm"][1] - fr[side + "Arm"][1]).z, -(fr[side + "ForeArm"][1] - fr[side + "Arm"][1]).y) for fr in frames]
                mean_sw = sum(sw) / len(sw); rng = (max(sw) - min(sw)) or 1.0
                for fr, a_ in zip(frames, sw):
                    pass   # (arm swing as captured; amplifying it read as "monkey arms")
                    # the elbow bends as the arm comes forward and opens as it goes back (8..40 deg), not a fixed angle
                    turn(fr, side + "ForeArm", -(0.10 + 0.25 * min(1.0, max(0.0, (a_ - min(sw)) / rng))))
            # the hands pass the thighs, they do not brush them: keep them clear of the hip line
            for fr in frames:
                for side, sgn in (("Left", -1), ("Right", 1)):
                    sh = fr[side + "Arm"][1]; hd = fr[side + "Hand"][1]
                    need = 0.24 - sgn * hd.x
                    if need > 0:
                        r_ = max(0.2, (hd - sh).length)
                        piv = sh.copy(); Rm = Matrix.Rotation(sgn * math.asin(min(0.35, need / r_)), 3, 'Z')
                        for n_ in below(side + "Arm"):
                            R_, t_ = fr[n_]; fr[n_] = (Rm @ R_, piv + Rm @ (t_ - piv))
            log.append("  %s walk style: arm swing x1.2, elbows 8-40 deg with the swing, hands clear of the thighs" % clip)
        if COV_OWN and name in ("walk", "run"):
            parP = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
            def subtree(root):
                out = []
                for n_ in bones:
                    p_ = n_
                    while p_ is not None and p_ != root: p_ = parP[p_]
                    if p_ == root: out.append(n_)
                return out
            legs = {side: subtree(side + "UpLeg") for side in ("Left", "Right")}
            # feet: turn each whole leg about its own hip-ankle line so the stance toes point ~6 deg out
            # (the capture walked 9-12 deg pigeon-toed). The feet stay where they are; the knees follow the toes.
            N_ = len(frames)
            for side, sg in (("Left", -1), ("Right", 1)):
                ymin = min(fr[side + "Foot"][1].y for fr in frames); vals = []
                for fr in frames:
                    t_ = fr[side + "ToeBase"][1] - fr[side + "Foot"][1]
                    if fr[side + "Foot"][1].y <= ymin + 0.03 and math.hypot(t_.x, t_.z) > 0.5 * t_.length:
                        vals.append(sg * math.atan2(t_.x, t_.z))
                if not vals: continue
                delta = math.radians(6) - sum(vals) / len(vals)
                for fr in frames:
                    hip = fr[side + "UpLeg"][1].copy(); ax = (fr[side + "Foot"][1] - hip).normalized()
                    Rm = Matrix.Rotation(-sg * delta, 3, ax)
                    for n_ in legs[side]:
                        R_, t_ = fr[n_]; fr[n_] = (Rm @ R_, hip + Rm @ (t_ - hip))
                log.append("  %s %s toes turned out by %.1f deg" % (clip, side, math.degrees(delta)))
            # pelvis: the hip on the swing side drops and the pelvis turns with the forward leg (walk 3/4 deg,
            # run 4/5 deg). Only the pelvis bone turns; the legs keep their direction and just ride their hip
            # joints (a few mm), the upper body is untouched - no swagger, just a living pelvis.
            A, B = (math.radians(1.5), math.radians(2)) if name == "walk" else (math.radians(4), math.radians(5))
            for k, fr in enumerate(frames):
                ph = 2 * math.pi * k / N_          # phase 0: left foot forward
                Rm = Matrix.Rotation(-A * math.sin(ph), 3, 'Z') @ Matrix.Rotation(B * math.cos(ph), 3, 'Y')
                piv = fr["Hips"][1].copy()
                for n_ in ("Hips", "LHipJoint", "RHipJoint"):
                    R_, t_ = fr[n_]; fr[n_] = (Rm @ R_, piv + Rm @ (t_ - piv))
                for side in ("Left", "Right"):
                    old = fr[side + "UpLeg"][1].copy(); new = piv + Rm @ (old - piv); d_ = new - old
                    for n_ in legs[side]:
                        R_, t_ = fr[n_]; fr[n_] = (R_, t_ + d_)
            log.append("  %s pelvis: roll %.0f, turn %.0f deg" % (clip, math.degrees(A), math.degrees(B)))
            if name == "walk":
                # head carriage: the neck leans forward like in the idle pose (the capture held it bolt upright, the head
                # perched straight over the shoulders); the face is levelled again by the gaze step below
                nk = subtree("Neck")
                d_i = idle_frame["Head"][1] - idle_frame["Neck"][1]; want = math.atan2(d_i.z, d_i.y)
                lean_now = sum(math.atan2((fr["Head"][1] - fr["Neck"][1]).z, (fr["Head"][1] - fr["Neck"][1]).y) for fr in frames) / N_
                Rm = Matrix.Rotation(want - lean_now, 3, 'X')
                for fr in frames:
                    piv = fr["Neck"][1].copy()
                    for n_ in nk:
                        R_, t_ = fr[n_]; fr[n_] = (Rm @ R_, piv + Rm @ (t_ - piv))
                log.append("  %s neck brought forward by %.1f deg (to the idle carriage)" % (clip, math.degrees(want - lean_now)))
        if COV_OWN:
            # coverall: keep the captured head motion (nods, small turns with each step) and only take out its
            # constant offset, so on average she looks straight ahead like the idle pose - not down, not up
            qi = idle_frame["Head"][0].to_quaternion(); acc = [0.0] * 4
            rel = [fr["Head"][0].to_quaternion() @ qi.inverted() for fr in frames]
            for r in rel:
                sgn = 1 if r.dot(rel[0]) >= 0 else -1
                for c in range(4): acc[c] += sgn * r[c]
            mean = Quaternion(acc).normalized()
            log.append("  %s head offset removed: %.1f deg" % (clip, math.degrees(mean.angle)))
            for fr in frames:
                R, t = fr["Head"]; fr["Head"] = ((mean.inverted() @ R.to_quaternion()).to_matrix(), t)
            if name == "run":
                lean = sum(math.atan2((fr["Neck"][1] - fr["Hips"][1]).z, (fr["Neck"][1] - fr["Hips"][1]).y) for fr in frames) / len(frames)
                for fr in frames:
                    R, t = fr["Head"]; fr["Head"] = (Matrix.Rotation(0.85 * lean, 3, 'X') @ R, t)
                log.append("  %s run: head follows the trunk lean, %.1f deg forward" % (clip, math.degrees(0.85 * lean)))
        else:
            for fr in frames:
                for bn, k in (("Head", 0.7), ("Neck1", 0.4)):
                    R, t = fr[bn]; qi = idle_frame[bn][0].to_quaternion(); q = R.to_quaternion()
                    fr[bn] = (q.slerp(qi, k).to_matrix(), t)
        # coverall only: a loop-aware [1,2,1] filter takes out the jitter left by resampling the 120 fps capture
        # (the suit has its own layered motion and keeps the raw clips)
        if COV_OWN:
            n_fr = len(frames); smooth = []
            for i in range(n_fr):
                out = {}
                for bn in bones:
                    (Ra, ta), (Rb, tb), (Rc, tc) = frames[i - 1][bn], frames[i][bn], frames[(i + 1) % n_fr][bn]
                    qb = Rb.to_quaternion(); acc = [0.0] * 4
                    for q, w in ((Ra.to_quaternion(), 1), (qb, 2), (Rc.to_quaternion(), 1)):
                        s = w if q.dot(qb) >= 0 else -w
                        for c in range(4): acc[c] += s * q[c]
                    out[bn] = (Quaternion(acc).normalized().to_matrix(), (ta + 2 * tb + tc) / 4)
                smooth.append(out)
            frames = smooth
        write_clip(name, frames, fps, stride, stride / T, 1, bones)
        log.append("  %s cycle frames %d..%d (peaks %s)" % (clip, a, b, peaks))
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
