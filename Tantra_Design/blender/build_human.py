# Tantra: builds the astronavigator base human with MPFB (MakeHuman, CC0 assets), rigs it and renders previews.
# Phenotype values are MakeHuman macro sliders (0..1). Canon proportions: to be confirmed and set here.
import bpy, os, math, json, traceback
HERE = os.path.dirname(__file__)
OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "build_human.log")
log = []

PHENOTYPE = {"gender": 0.0, "age": 0.5, "muscle": 0.60, "weight": 0.48, "proportions": 1.0, "height": 0.60,
             "cupsize": 0.72, "firmness": 0.78, "race": {"asian": 0.0, "caucasian": 1.0, "african": 0.0}}
# Figure: athletic but feminine -- defined waist, fuller hips and bust; face: soft oval, fuller lips.
SHAPE = [("hip-scale-horiz-incr", 0.12), ("torso-scale-horiz-decr", 0.30), ("torso-scale-depth-decr", 0.15), ("measure-waist-circ-decr", 0.45), ("buttocks-volume-incr", 0.15),
         ("breast-dist-decr", 0.20), ("breast-volume-vert-up", 0.25), ("torso-vshape-decr", 0.15),
         ("mouth-lowerlip-volume-incr", 0.40), ("mouth-upperlip-volume-incr", 0.30), ("head-oval", 0.45), ("chin-width-decr", 0.35)]
EYES_BROWN = {"IrisMajorColor": [0.30, 0.14, 0.05, 1.0], "IrisMinorColor": [0.10, 0.045, 0.015, 1.0], "IrisSection4Color": [0.04, 0.02, 0.008, 1.0]}
HAIR_TINT = (0.62, 0.24, 0.07, 1.0)   # copper-auburn

ASSETS = {"proxy": "female1605/female1605.proxy", "eyes": "low-poly/low-poly.mhclo", "eyebrows": "eyebrow001/eyebrow001.mhclo",
          "eyelashes": "eyelashes01/eyelashes01.mhclo", "hair": "toigo_curled_under_bob/toigo_curled_under_bob.mhclo",
          "skin": "toigo_light_skin_female_ginger/toigo_light_skin_female_ginger.mhmat", "rig": "cmu_mb"}

def clean_scene():
    bpy.ops.wm.read_homefile(use_empty=True)   # keeps preferences (the tantra extension repo)

def build():
    import addon_utils
    addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    info = HumanService._create_default_human_info_dict()
    info["phenotype"] = PHENOTYPE
    info["proxy"] = ASSETS["proxy"]; info["eyes"] = ASSETS["eyes"]; info["eyebrows"] = ASSETS["eyebrows"]
    info["eyelashes"] = ASSETS["eyelashes"]; info["hair"] = ASSETS["hair"]; info["rig"] = ASSETS["rig"]
    info["skin_mhmat"] = ASSETS["skin"]; info["skin_material_type"] = "MAKESKIN"; info["eyes_material_type"] = "PROCEDURAL_EYES"
    info["alternative_materials"] = {}; info["name"] = "Astronavigator"
    info["targets"] = [{"target": t, "value": v} for t, v in SHAPE]
    info["eyes_material_settings"] = EYES_BROWN
    st = HumanService.get_default_deserialization_settings()
    st["subdiv_levels"] = 0; st["override_skin_model"] = "MAKESKIN"
    basemesh = HumanService.deserialize_from_dict(info, st)
    return basemesh

def tint_hair():
    """Multiply the hair texture by a copper tint (MakeSkin materials keep the texture for export)."""
    for o in bpy.data.objects:
        if o.type != 'MESH' or "hair" not in o.name.lower(): continue
        for slot in o.material_slots:
            m = slot.material
            if not m or not m.use_nodes: continue
            nt = m.node_tree
            for node in list(nt.nodes):
                for inp in node.inputs:
                    if inp.name in ("Base Color", "Color") and inp.is_linked and node.type in ('BSDF_PRINCIPLED', 'BSDF_DIFFUSE'):
                        link = inp.links[0]; src = link.from_socket
                        mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'
                        mix.inputs[0].default_value = 1.0; mix.inputs[7].default_value = HAIR_TINT
                        nt.links.remove(link); nt.links.new(src, mix.inputs[6]); nt.links.new(mix.outputs[2], inp)
                        log.append("hair tint on %s / %s" % (o.name, m.name))

def stats():
    tot = 0
    for o in bpy.data.objects:
        if o.type == 'MESH':
            dg = bpy.context.evaluated_depsgraph_get(); me = o.evaluated_get(dg).to_mesh()
            n = sum(len(p.vertices) - 2 for p in me.polygons); o.evaluated_get(dg).to_mesh_clear()
            vis = not o.hide_render
            log.append("  mesh %-28s tris %6d %s" % (o.name, n, "" if vis else "(hidden)"))
            if vis: tot += n
        elif o.type == 'ARMATURE':
            log.append("  armature %s bones %d" % (o.name, len(o.data.bones)))
    log.append("visible tris: %d" % tot)

def render_previews(tag):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 900, 1200
    sc.render.film_transparent = False
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    nt = world.node_tree; bg = nt.nodes.get("Background") or nt.nodes.new("ShaderNodeBackground")
    wo = nt.nodes.get("World Output") or nt.nodes.new("ShaderNodeOutputWorld"); nt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.05, 0.055, 0.065, 1); bg.inputs[1].default_value = 0.6
    def light(name, loc, energy, size, color=(1, 1, 1)):
        l = bpy.data.lights.new(name, 'AREA'); l.energy = energy; l.size = size; l.color = color
        o = bpy.data.objects.new(name, l); o.location = loc; sc.collection.objects.link(o)
        d = o.constraints.new('TRACK_TO'); d.target = target; d.track_axis = 'TRACK_NEGATIVE_Z'; d.up_axis = 'UP_Y'
    target = bpy.data.objects.new("Target", None); target.location = (0, 0, 1.0); sc.collection.objects.link(target)
    light("Key", (2.0, -2.5, 2.4), 320, 2.0); light("Fill", (-2.5, -1.5, 1.6), 110, 3.0, (0.85, 0.9, 1.0)); light("Rim", (0.5, 3.0, 2.5), 260, 1.5, (0.8, 0.88, 1.0))
    floor = bpy.data.meshes.new("Floor"); floor.from_pydata([(-5, -5, 0), (5, -5, 0), (5, 5, 0), (-5, 5, 0)], [], [(0, 1, 2, 3)])
    fo = bpy.data.objects.new("Floor", floor); sc.collection.objects.link(fo)
    fm = bpy.data.materials.new("FloorM"); fm.use_nodes = False; fm.diffuse_color = (0.12, 0.13, 0.15, 1); fm.roughness = 0.9; floor.materials.append(fm)
    cam_d = bpy.data.cameras.new("Cam"); cam_d.lens = 70; cam = bpy.data.objects.new("Cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam
    from mathutils import Vector
    H = HEIGHT; eye = 0.935 * H
    for name, loc, tz in [("front", (0, -5.2, 0.6 * H), 0.54 * H), ("threequarter", (3.3, -3.9, 0.75 * H), 0.54 * H), ("face", (0.20, -0.85, eye), eye - 0.035)]:
        cam.location = loc; target.location = (0, 0, tz)
        cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector((0, 0, tz)) - Vector(loc)).to_track_quat('-Z', 'Y')
        cam_d.lens = 70 if name != "face" else 85
        bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(OUT, "%s_%s.png" % (tag, name)); bpy.ops.render.render(write_still=True)
        log.append("rendered " + sc.render.filepath)

try:
    clean_scene()
    bm = build()
    dg = bpy.context.evaluated_depsgraph_get()
    zs = []
    for o in bpy.data.objects:
        if o.type == 'MESH' and not o.hide_render and 'female1605' in o.name:
            me = o.evaluated_get(dg).to_mesh(); zs += [(o.matrix_world @ v.co).z for v in me.vertices]; o.evaluated_get(dg).to_mesh_clear()
    global HEIGHT; HEIGHT = max(zs) - min(zs)
    log.append("visible body height %.3f m (min z %.3f)" % (HEIGHT, min(zs)))
    stats()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_base.blend"))
    render_previews("base")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
