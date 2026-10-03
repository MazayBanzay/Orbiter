# Survey: library garments on the approved body, front view each, one plain material so only the cut is judged.
# Writes renders/sv_<asset>.png. Changes nothing else.
import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
ASSETS = sys.argv[sys.argv.index("--") + 1].split(",") if "--" in sys.argv else []
LOG = os.path.join(HERE, "survey_garments.log"); log = []
try:
    import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    for a in ASSETS:
        bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
        body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
        before = set(bpy.data.objects)
        try:
            HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("%s/%s.mhclo" % (a, a), asset_subdir="clothes"), body,
                                         asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
        except Exception as e:
            log.append("%s: %s" % (a, e)); continue
        g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH')
        dg = body.vertex_groups.get("Delete." + a)
        if dg:
            mk = body.modifiers.new("Hide", 'MASK'); mk.vertex_group = dg.name; mk.invert_vertex_group = True
        m = bpy.data.materials.new("plain"); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'); b.inputs["Base Color"].default_value = (0.7, 0.72, 0.74, 1); b.inputs["Roughness"].default_value = 0.85
        g.data.materials.clear(); g.data.materials.append(m)
        sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x, sc.render.resolution_y = 450, 900
        cam.location = (0, -4.6, 1.0); cam.data.lens = 60; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector((0, 0, 0.92)) - cam.location).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(HERE, "renders", "sv_%s.png" % a); bpy.ops.render.render(write_still=True)
        log.append("%s: %d verts, delete group %s" % (a, len(g.data.vertices), bool(dg)))
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
