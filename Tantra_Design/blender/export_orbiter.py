# Tantra: export the astronavigator (posed, evaluated) to an Orbiter 2016 mesh (.msh) + a texture manifest.
# Frame: from anim/anim_<clip>.blend, the frame where the feet are closest together (neutral passing pose).
# Axes: Blender (right-handed, Z up, character faces -Y) -> Orbiter (left-handed, Y up, +Z forward, +X right):
#   x_o = -x_b, y_o = z_b - ORIGIN_H, z_o = -y_b. The mirror flips handedness, so Blender CCW fronts become
#   Orbiter CW fronts without reordering indices. v_o = 1 - v_b.
# Usage: run.ps1 -Script export_orbiter.py -Rest <clip> <name>
import bpy, os, sys, json, traceback
from mathutils import Vector, Matrix, Quaternion
KNEE = float(os.environ.get('TANTRA_KNEE', '0.16'))
HERE = os.path.dirname(__file__); ROOT = os.path.dirname(HERE)
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
CLIP = argv[0] if argv else "35_01"; NAME = argv[1] if len(argv) > 1 else "Astronavigator"
ORBITER = os.path.abspath(os.path.join(ROOT, ".."))
MESH_OUT = os.path.join(ORBITER, "Meshes", "Tantra", NAME + ".msh")
TEX_SUB = "Tantra\\" + NAME                                  # relative to Orbiter\Textures
MANIFEST = os.path.join(HERE, "export_%s.json" % NAME)
LOG = os.path.join(HERE, "export_orbiter.log"); log = []
ORIGIN_H = 0.93                                             # vessel origin above the soles (UACS "height")
EYE_TEX = os.path.join(ROOT, "mpfbu", "data", "eyes", "materials", "brown_eye.png")

def image_of(mat):
    if not mat or not mat.use_nodes: return None
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image and n.outputs[0].is_linked:
            for l in n.outputs[0].links:
                if l.to_socket.name in ("Base Color", "Color") or l.to_node.type in ('MIX', 'MIX_RGB', 'GROUP'):
                    return bpy.path.abspath(n.image.filepath) if n.image.filepath else n.image
    for n in mat.node_tree.nodes:
        if n.type == 'TEX_IMAGE' and n.image: return bpy.path.abspath(n.image.filepath) if n.image.filepath else n.image
    return None

def base_color(mat):
    if not mat: return (0.8, 0.8, 0.8, 1)
    if mat.use_nodes:
        for n in mat.node_tree.nodes:
            if n.type == 'BSDF_PRINCIPLED':
                c = n.inputs["Base Color"].default_value; return (c[0], c[1], c[2], 1)
    return tuple(mat.diffuse_color)

sys.path.insert(0, HERE); from pose_util import aim, idle_pose

try:
    if CLIP == "idle":
        bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    else:
        bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "anim", "anim_%s.blend" % CLIP))
    sc = bpy.context.scene
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    if CLIP == "idle":
        arm.animation_data_clear(); idle_pose(arm); sc.frame_start = sc.frame_end = 1
    best, bf = 1e9, sc.frame_start
    for f in range(sc.frame_start, sc.frame_end + 1):
        sc.frame_set(f)
        a = (arm.matrix_world @ arm.pose.bones['LeftFoot'].head); b = (arm.matrix_world @ arm.pose.bones['RightFoot'].head)
        d = (a - b).xy.length
        if d < best: best, bf = d, f
    sc.frame_set(bf); bpy.context.view_layer.update()
    hips = (arm.matrix_world @ arm.pose.bones['Hips'].head)
    log.append("pose frame %d (feet %.3f m apart), hips %s" % (bf, best, tuple(round(c, 3) for c in hips)))
    dg = bpy.context.evaluated_depsgraph_get()

    groups, materials, textures = [], [], []
    def mat_index(key, color, alpha):
        for i, m in enumerate(materials):
            if m["key"] == key: return i + 1
        materials.append({"key": key, "color": color, "alpha": alpha}); return len(materials)
    def tex_index(src, name, alpha):
        for i, t in enumerate(textures):
            if t["src"] == src: return i + 1
        textures.append({"src": src, "dds": TEX_SUB + "\\" + name + ".dds", "alpha": alpha}); return len(textures)

    # centre the posed figure over the origin (horizontal) and put the soles at y = -ORIGIN_H
    cx, cy = hips.x, hips.y
    zmin = 1e9
    objs = [o for o in bpy.data.objects if o.type == 'MESH' and o.visible_get() and not o.hide_render and o.name not in ("Floor",)]
    evals = []
    for o in objs:
        oe = o.evaluated_get(dg); me = oe.to_mesh()
        if len(me.polygons) == 0: oe.to_mesh_clear(); continue
        M = o.matrix_world
        zmin = min(zmin, min((M @ v.co).z for v in me.vertices)); evals.append((o, oe, me, M))
    log.append("objects: %s" % [o.name for o, *_ in evals])

    for o, oe, me, M in evals:
        me.calc_loop_triangles()
        uvl = me.uv_layers.active.data if me.uv_layers.active else None
        N3 = M.to_3x3().inverted().transposed()
        cn = me.corner_normals if hasattr(me, "corner_normals") else None
        by_mat = {}
        for t in me.loop_triangles: by_mat.setdefault(t.material_index, []).append(t)
        for mi, tris in by_mat.items():
            mat = o.material_slots[mi].material if mi < len(o.material_slots) else None
            base = o.name.split('.')[-1]
            short = {"female1605": "Skin", "eyebrow001": "Brows", "eyelashes01": "Lashes", "low-poly": "Eyes", "shoes03": "Boots"}.get(base, "Hair" if "bob" in base or "hair" in base else base[:10])
            lname = (short + ("_" + mat.name.split('.')[-1].replace("Coverall", "") if base == "Coverall" and mat else ""))[:20]   # D3D9Client: keep group labels short
            is_eye = "low-poly" in o.name or (mat and "eye" in mat.name.lower() and "brow" not in mat.name.lower() and "lash" not in mat.name.lower())
            img = EYE_TEX if is_eye else image_of(mat)
            alpha = any(k in o.name.lower() for k in ("hair", "bob", "eyebrow", "eyelash"))
            tex = 0
            if isinstance(img, str) and os.path.exists(img):
                tex = tex_index(img, lname, alpha); color = (1, 1, 1, 1)
            else:
                color = base_color(mat)
            mat_i = mat_index(lname, color, alpha)
            verts, index, faces = [], {}, []
            for t in tris:
                face = []
                for li, vi in zip(t.loops, t.vertices):
                    p = M @ me.vertices[vi].co
                    n = (N3 @ (Vector(cn[li].vector) if cn else me.vertices[vi].normal)).normalized()
                    uv = uvl[li].uv if uvl else (0, 0)
                    P = (-(p.x - cx), p.z - zmin - ORIGIN_H, -(p.y - cy)); Nn = (-n.x, n.z, -n.y); U = (uv[0], 1 - uv[1])
                    key = (vi, round(U[0], 5), round(U[1], 5), round(Nn[0], 3), round(Nn[1], 3), round(Nn[2], 3))
                    if key not in index: index[key] = len(verts); verts.append((P, Nn, U))
                    face.append(index[key])
                faces.append(face)
            if short == "Boots":     # MakeHuman shoe asset is single-sided: add the back faces
                n0 = len(verts); verts += [(P, (-Nn[0], -Nn[1], -Nn[2]), U) for P, Nn, U in verts]
                faces += [(a + n0, c + n0, b + n0) for a, b, c in faces]
            groups.append({"label": lname, "mat": mat_i, "tex": tex, "verts": verts, "faces": faces})
        oe.to_mesh_clear()

    os.makedirs(os.path.dirname(MESH_OUT), exist_ok=True)
    with open(MESH_OUT, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(groups))
        for g in groups:
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE %d\nGEOM %d %d\n" % (g["label"], g["mat"], g["tex"], len(g["verts"]), len(g["faces"])))
            for P, Nn, U in g["verts"]:
                f.write("%.5f %.5f %.5f %.4f %.4f %.4f %.5f %.5f\n" % (*P, *Nn, *U))
            for a, b, c in g["faces"]: f.write("%d %d %d\n" % (a, c, b))   # Orbiter front faces: reversed order (checked against UACS Z2Suit.msh)
        f.write("MATERIALS %d\n" % len(materials))
        for m in materials: f.write(m["key"] + "\n")
        for m in materials:
            r, g_, b, _ = m["color"]
            f.write("MATERIAL %s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n0.1 0.1 0.1 1 8\n0 0 0 1\n" % (m["key"], r, g_, b, r, g_, b))
        f.write("TEXTURES %d\n" % len(textures))
        for t in textures: f.write(t["dds"] + "\n")
    json.dump({"textures": textures, "orbiter": ORBITER}, open(MANIFEST, "w"), indent=1)
    tris = sum(len(g["faces"]) for g in groups)
    log.append("wrote %s: %d groups, %d tris, %d materials, %d textures" % (MESH_OUT, len(groups), tris, len(materials), len(textures)))
    for g in groups: log.append("  %-40s mat %d tex %d  v %d t %d" % (g["label"], g["mat"], g["tex"], len(g["verts"]), len(g["faces"])))
    if CLIP == "idle":
        sys.path.insert(0, HERE); import render_util
        cam = render_util.setup_stage(); H = 1.74
        for m in bpy.data.materials: m.use_backface_culling = True     # match Orbiter (single-sided)
        legshots = [("leg_back", (0.45, 1.3, 0.45), 0.35, 50), ("leg_side", (1.4, -0.3, 0.45), 0.35, 50)]
        render_util.shoot(cam, os.path.join(HERE, "renders"), [(n + "_culled", l, t, f) for n, l, t, f in legshots], log)
        for m in bpy.data.materials: m.use_backface_culling = False
        render_util.shoot(cam, os.path.join(HERE, "renders"), [(n + "_double", l, t, f) for n, l, t, f in legshots], log)
        for m in bpy.data.materials: m.use_backface_culling = True
        render_util.shoot(cam, os.path.join(HERE, "renders"), [("idle_front", (0, -5.2, 0.6 * H), 0.54 * H, 70), ("idle_threequarter", (3.3, -3.9, 0.75 * H), 0.54 * H, 70), ("idle_right", (-2.4, -1.6, 1.1), 0.85, 60), ("idle_back", (1.6, 3.6, 1.0), 0.6, 60)], log)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
