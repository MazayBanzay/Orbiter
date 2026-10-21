# Render only the game asset's groups named in env GROUPS (comma list), helmet front close-up, rest pose -> group_<TAG>.png
import bpy, os, sys, math
from mathutils import Vector as V
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..", "blender")))
import render_util
keep = os.environ.get("GROUPS", "HelmCapGr").split(","); TAG = os.environ.get("TAG", "g")
for o in bpy.data.objects:
    if o.type == 'MESH': o.hide_render = o.name.split(".")[0] not in keep
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'REST'
cam = render_util.setup_stage(); sc = bpy.context.scene; sc.render.engine = 'BLENDER_EEVEE_NEXT'
w = sc.world.node_tree; bg = next(n for n in w.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.5, 0.5, 0.55, 1)
hd = arm.matrix_world @ arm.data.bones["Head"].head_local
cam.location = hd + V((0.0, -0.75, 0.06)); cam.data.lens = 70; cam.rotation_mode = 'QUATERNION'
cam.rotation_quaternion = (hd + V((0, 0, 0.05)) - cam.location).to_track_quat('-Z', 'Y')
sc.render.resolution_x = sc.render.resolution_y = 700
sc.render.filepath = os.path.join(HERE, "group_" + TAG + ".png"); bpy.ops.render.render(write_still=True)
bpy.ops.wm.quit_blender()
