# The user's crew suit design (2026-10-02: white, red stand collar running into a V down the chest, red side wedges,
# red cuffs; Deus Ex: HR micro-pattern) on the approved body, exported as a GLB for a 3D look in the browser.
# PREVIEW ONLY: no game file touched. The red regions are baked from the concept's shader into a fresh UV (2048).
# Output: Tantra_Design/suit_view/astronavigator_suit.glb
import bpy, bmesh, os, sys, math
import numpy as np
from mathutils import Vector, Quaternion
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE)
OUT = os.path.join(HERE, "..", "suit_view"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "export_suit_glb.log"); log = []
sys.argv = [sys.argv[0], "--", "2"]           # the concept builder's variant 2 (the user's V)
src = open(os.path.join(HERE, "concept_dx.py"), encoding="utf-8").read()
src = src[:src.index("    sc = bpy.context.scene; cam = render_util.setup_stage()")]   # build only, no renders
src = src.replace("\ntry:\n", "\nif True:\n", 1)                                 # its try: has lost its except
TEX = os.path.join(HERE, "textures"); MP = os.path.join(HERE, "..", "mpfbu", "data")

def simple_mat(name, img_path, alpha=False, rough=0.6):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial"); b = nt.nodes.new("ShaderNodeBsdfPrincipled"); nt.links.new(b.outputs[0], out.inputs[0])
    b.inputs["Roughness"].default_value = rough
    t = nt.nodes.new("ShaderNodeTexImage"); t.image = bpy.data.images.load(img_path, check_existing=True)
    nt.links.new(t.outputs[0], b.inputs["Base Color"])
    if alpha:
        nt.links.new(t.outputs[1], b.inputs["Alpha"]); m.blend_method = 'HASHED' if hasattr(m, "blend_method") else None
    return m

try:
    exec(compile(src, "concept_dx", "exec"))
    if "Traceback" in "\n".join(log): raise RuntimeError("\n".join(log))
    suit = g
    # bake the suit's colour into a fresh UV
    for o in bpy.context.view_layer.objects: o.select_set(False)
    bpy.context.view_layer.objects.active = suit; suit.select_set(True)
    uv = suit.data.uv_layers.new(name="Bake"); suit.data.uv_layers.active = uv
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.004, scale_to_bounds=True); bpy.ops.object.mode_set(mode='OBJECT')
    m = suit.data.materials[0]; nt = m.node_tree
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'); colsock = b.inputs["Base Color"].links[0].from_socket
    out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL'); em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(colsock, em.inputs[0]); keep = out.inputs[0].links[0].from_socket; nt.links.new(em.outputs[0], out.inputs[0])
    img = bpy.data.images.new("suit_color", 2048, 2048, alpha=False)
    tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = img; nt.nodes.active = tn
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 4; sc.cycles.device = 'CPU'; sc.render.bake.margin = 32
    bpy.ops.object.bake(type='EMIT')
    img.filepath_raw = os.path.join(OUT, "suit_color.png"); img.file_format = 'PNG'; img.save()
    # the exported suit material: the baked colour, matte; the old UV layers go
    for l in [l for l in suit.data.uv_layers if l.name != "Bake"]: suit.data.uv_layers.remove(l)
    suit.data.materials.clear(); suit.data.materials.append(simple_mat("Suit", img.filepath_raw, rough=0.8))
    # the rest: simple materials with their own images (glTF reads plain Principled + image)
    for o in bpy.data.objects:
        if o.type != 'MESH' or o == suit or o.hide_render: continue
        n = o.name
        if n.endswith(".body"): mm = simple_mat("Skin", os.path.join(TEX, "skin_clean.png"), rough=0.55)
        elif "bob" in n:
            hp = os.path.join(TEX, "hair_copper.png"); mm = simple_mat("Hair", hp if os.path.exists(hp) else os.path.join(MP, "hair", "toigo_curled_under_bob", "GingerHair.png"), alpha=True, rough=0.5)
        elif "eyebrow" in n: mm = simple_mat("Brows", os.path.join(MP, "eyebrows", "eyebrow001", "eyebrow001.png"), alpha=True)
        elif "eyelash" in n: mm = simple_mat("Lashes", os.path.join(MP, "eyelashes", "eyelashes01", "eyelashes01.png"), alpha=True)
        elif "low-poly" in n: mm = simple_mat("Eyes", os.path.join(MP, "eyes", "materials", "brown_eye.png"), rough=0.2)
        elif "shoes" in n: mm = simple_mat("Boots", os.path.join(MP, "clothes", "shoes03", "shoes03_diffuse.png"), rough=0.5)
        else: continue
        o.data.materials.clear(); o.data.materials.append(mm)
    # a natural stance: arms down
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    for bn, ang in ():   # (the arms-down pose crossed the hands in front: the A-pose as rendered)
        pb = arm.pose.bones[bn]; pb.rotation_mode = 'QUATERNION'; Mb = arm.matrix_world @ pb.bone.matrix_local
        pb.rotation_quaternion = (Mb.to_3x3().inverted() @ Quaternion((0, 1, 0), math.radians(ang)).to_matrix() @ Mb.to_3x3()).to_quaternion()
    bpy.context.view_layer.update()
    # static meshes (pose and masks applied), then export
    dg_ = bpy.context.evaluated_depsgraph_get(); keepo = []
    for o in list(bpy.data.objects):
        if o.type != 'MESH' or o.hide_render or not o.data.materials: continue
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg_)); no = bpy.data.objects.new("X_" + o.name.split(".")[-1], me)
        no.matrix_world = o.matrix_world; sc.collection.objects.link(no); keepo.append(no)
    for o in bpy.context.view_layer.objects: o.select_set(o in keepo)
    path = os.path.join(OUT, "astronavigator_suit.glb")
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_format='GLB', export_image_format='JPEG', export_jpeg_quality=88,
                              export_apply=True, export_yup=True)
    log.append("exported %s (%.1f MB), objects %s" % (path, os.path.getsize(path) / 1e6, [o.name for o in keepo]))
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
