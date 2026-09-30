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
MESH_OUT = os.path.join(ORBITER, "Meshes", "Tantra", "AstronavigatorSkin.msh")
CFG_DIR = os.path.join(ORBITER, "Config", "Tantra"); CLIP_DIR = os.path.join(CFG_DIR, "anim")
os.makedirs(CLIP_DIR, exist_ok=True)
TEX_SUB = "Tantra\\Astronavigator"; ORIGIN_H = 0.93
EYE_TEX = os.path.join(ROOT, "mpfbu", "data", "eyes", "materials", "brown_eye.png")
CLIPS = [("walk", "35_01"), ("run", "09_01")]
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
    return (s + ("_" + mat.name.split('.')[-1].replace("Coverall", "") if base == "Coverall" and mat else ""))[:20], s

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
            tex = tex_index(TEX_SUB + "\\" + lname + ".dds") if img and os.path.exists(img) else 0
            mat_i = mat_index(lname, (1, 1, 1, 1) if tex else base_color(mat))
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
            f.write("MATERIAL %s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n0.1 0.1 0.1 1 8\n0 0 0 1\n" % (k, r, g_, b, r, g_, b))
        f.write("TEXTURES %d\n" % len(textures))
        for t in textures: f.write(t + "\n")
    # skin file
    with open(os.path.join(CFG_DIR, "Astronavigator.skin"), "w", newline="\n") as f:
        f.write("TANTRA_SKIN 1\nMESH Tantra\\AstronavigatorSkin\nBONES %d\n" % len(bones))
        for n in bones:
            b = arm.data.bones[n]; R, t = cm(arm.matrix_world @ b.matrix_local)
            par = bones.index(b.parent.name) if b.parent else -1
            f.write("%s %d %s %.6f %.6f %.6f\n" % (n, par, " ".join("%.6f" % R[i][j] for i in range(3) for j in range(3)), t.x, t.y, t.z))
        f.write("GROUPS %d\n" % len(groups))
        for gi, (lname, mat_i, tex, verts, faces) in enumerate(groups):
            f.write("GROUP %d %d\n" % (gi, len(verts)))
            for P, Nn, U, W in verts: f.write("%d %d %d %d %.4f %.4f %.4f %.4f\n" % (*[b for b, _ in W], *[w for _, w in W]))
    log.append("bind mesh: %d groups, %d verts, %d tris" % (len(groups), sum(len(g[3]) for g in groups), sum(len(g[4]) for g in groups)))

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
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    arm.animation_data_clear(); arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    bones = hierarchy(arm)
    export_bind(arm, bones)
    rest_hips = (arm.matrix_world @ arm.data.bones["Hips"].matrix_local).translation.copy()
    # idle: one frame
    arm.data.pose_position = 'POSE'; idle_pose(arm)
    idle_frame = {n: cm(arm.matrix_world @ arm.pose.bones[n].matrix) for n in bones}
    write_clip("idle", [idle_frame], 30, 0.0, 0.0, 0, bones)

    for name, clip in CLIPS:
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
        # gaze stabilisation: the head (and partly the neck) is pulled towards the level idle orientation,
        # so the runner looks ahead instead of up; positions stay from the capture
        for fr in frames:
            for bn, k in (("Head", 0.7), ("Neck1", 0.4)):
                R, t = fr[bn]; qi = idle_frame[bn][0].to_quaternion(); q = R.to_quaternion()
                fr[bn] = (q.slerp(qi, k).to_matrix(), t)
        write_clip(name, frames, fps, stride, stride / T, 1, bones)
        log.append("  %s cycle frames %d..%d (peaks %s)" % (clip, a, b, peaks))
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
