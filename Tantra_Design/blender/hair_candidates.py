# Tantra: try MakeHuman CC0 hair assets on the astronavigator (copper tint), head renders side by side.
# Usage: run.ps1 -Script hair_candidates.py -Rest <asset>,<asset>,...
import bpy, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.join(HERE, "renders", "hair"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "hair_candidates.log"); log = []
HAIR_TINT = (0.62, 0.24, 0.07, 1.0)


def tint(o):
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


try:
    import addon_utils
    addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    sc = bpy.context.scene
    basemesh = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    cam = render_util.setup_stage()
    sc.render.resolution_x, sc.render.resolution_y = 420, 480
    current = [o for o in bpy.data.objects if o.type == 'MESH' and 'bob' in o.name.lower()]
    for name in argv:
        for o in list(bpy.data.objects):
            if o.type == 'MESH' and o.get("cand"): bpy.data.objects.remove(o, do_unlink=True)
        for o in current: o.hide_render = name != "current"
        if name != "current":
            path = AssetService.find_asset_absolute_path("%s/%s.mhclo" % (name, name), asset_subdir="hair")
            before = set(bpy.data.objects)
            HumanService.add_mhclo_asset(path, basemesh, asset_type="Hair", subdiv_levels=0, material_type="MAKESKIN")
            new = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
            for o in new: o["cand"] = 1; tint(o)
            log.append("%s: %s, %d verts" % (name, [o.name for o in new], sum(len(o.data.vertices) for o in new)))
        for vn, loc in (("tq", Vector((0.55, -0.75, 1.62))), ("back", Vector((-0.45, 0.8, 1.62))), ("side", Vector((0.95, 0.0, 1.58)))):
            tgt = Vector((0, 0.01, 1.57)); cam.location = loc; cam.data.lens = 55
            cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "%s_%s.png" % (name, vn)); bpy.ops.render.render(write_still=True)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
