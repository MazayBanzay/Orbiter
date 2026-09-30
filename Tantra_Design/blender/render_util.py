# Shared preview lighting and camera shots for the astronavigator renders.
import bpy, os
FLOOR = tuple(float(x) for x in os.environ.get("TANTRA_FLOOR", "0.12,0.13,0.15,1").split(","))
from mathutils import Vector

def setup_stage():
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 900, 1200
    for o in [o for o in bpy.data.objects if o.name.startswith(("Key", "Fill", "Rim", "Floor", "Cam", "Target"))]:
        bpy.data.objects.remove(o, do_unlink=True)
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    nt = world.node_tree; bg = nt.nodes.get("Background") or nt.nodes.new("ShaderNodeBackground")
    wo = nt.nodes.get("World Output") or nt.nodes.new("ShaderNodeOutputWorld"); nt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.05, 0.055, 0.065, 1); bg.inputs[1].default_value = 0.6
    target = Vector((0, 0, 1.0))
    def light(name, loc, energy, size, color=(1, 1, 1)):
        l = bpy.data.lights.new(name, 'AREA'); l.energy = energy; l.size = size; l.color = color
        o = bpy.data.objects.new(name, l); o.location = loc; sc.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (target - Vector(loc)).to_track_quat('-Z', 'Y')
    light("Key", (2.0, -2.5, 2.4), 320, 2.0); light("Fill", (-2.5, -1.5, 1.6), 110, 3.0, (0.85, 0.9, 1.0)); light("Rim", (0.5, 3.0, 2.5), 260, 1.5, (0.8, 0.88, 1.0))
    floor = bpy.data.meshes.new("Floor"); floor.from_pydata([(-5, -5, 0), (5, -5, 0), (5, 5, 0), (-5, 5, 0)], [], [(0, 1, 2, 3)])
    fo = bpy.data.objects.new("Floor", floor); sc.collection.objects.link(fo)
    fm = bpy.data.materials.new("FloorM"); fm.use_nodes = False; fm.diffuse_color = FLOOR; floor.materials.append(fm)
    cam_d = bpy.data.cameras.new("Cam"); cam = bpy.data.objects.new("Cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam
    return cam

def shoot(cam, out_dir, shots, log=None):
    """shots: list of (name, camera location, target z, lens)"""
    sc = bpy.context.scene
    for name, loc, tz, lens in shots:
        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector((0, 0, tz)) - Vector(loc)).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(out_dir, name + ".png"); bpy.ops.render.render(write_still=True)
        if log is not None: log.append("rendered " + name)

def standard_shots(prefix, H):
    return [(prefix + "_front", (0, -5.2, 0.6 * H), 0.54 * H, 70), (prefix + "_threequarter", (3.3, -3.9, 0.75 * H), 0.54 * H, 70),
            (prefix + "_back", (-2.2, 4.6, 0.7 * H), 0.54 * H, 70), (prefix + "_upper", (0.9, -1.9, 0.84 * H), 0.78 * H, 70)]
