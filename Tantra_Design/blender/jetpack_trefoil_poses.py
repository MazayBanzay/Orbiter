# The game's trefoil pack v2 (tools/build_jetpack_trefoil.py) on her figure, pose by pose - deployed, the vector's pitch
# +35 deg, half folded, folded - from behind and from the side: the joints' chain seen as the module will move it.
#   blender --background astronavigator_suit.blend --python jetpack_trefoil_poses.py -> renders\tre_<pose>_<view>.png
import bpy, math, os
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); SRC = os.path.join(HERE, "trefoil_poses")
LOG = open(os.path.join(OUT, "tre_log.txt"), "w", encoding="utf-8")
KEEP = ("JetShell", "JetVessel", "JetNiche", "JetMark", "JetPorts", "JetCoupling")
for o in bpy.data.objects:
    if o.name.startswith("Jet") and not o.name.startswith(KEEP): o.hide_render = True
sc = bpy.context.scene
COL = {"White": (0.88, 0.89, 0.90), "Dark": (0.05, 0.055, 0.06), "Metal": (0.60, 0.62, 0.64), "Red": (0.78, 0.09, 0.07),
       "Plug": (0.30, 0.24, 0.20), "Blade": (0.32, 0.34, 0.37)}
try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 760, 760; sc.render.film_transparent = True
    for nm, en, rot in (("TP_key", 3.0, (42, 0, 145)), ("TP_fill", 1.2, (65, 0, -40))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("TP_cam"); cam = bpy.data.objects.new("TP_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    VIEWS = {"rear": (Vector((1.7, 2.9, 2.4)), Vector((0, 0.3, 1.35)), 'PERSP'), "side": (Vector((4.0, 0.3, 1.4)), Vector((0, 0.3, 1.4)), 'ORTHO')}
    for pose in ("deployed", "pitch35", "fold50", "folded"):
        before = set(bpy.data.objects)
        bpy.ops.wm.obj_import(filepath=os.path.join(SRC, pose + ".obj"), forward_axis='Y', up_axis='Z')
        new = [o for o in bpy.data.objects if o not in before]
        for o in new:
            for i, slot in enumerate(o.material_slots):
                m = slot.material
                if m is None: continue
                key = next((k for k in COL if m.name.startswith(k)), None)
                if key and m.use_nodes:
                    bs = next((n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
                    if bs: bs.inputs["Base Color"].default_value = (*COL[key], 1); bs.inputs["Roughness"].default_value = 0.35
            for pl in o.data.polygons: pl.use_smooth = False
        for vn, (loc, tgt, typ) in VIEWS.items():
            cd.type = typ; cd.ortho_scale = 2.4; cd.lens = 45
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "tre_%s_%s.png" % (pose, vn)); bpy.ops.render.render(write_still=True)
            print("rendered", pose, vn, file=LOG); LOG.flush()
        for o in new: bpy.data.objects.remove(o, do_unlink=True)
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
