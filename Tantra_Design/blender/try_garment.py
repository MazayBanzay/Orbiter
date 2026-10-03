# Try a ready-made MakeHuman garment on the astronavigator: fit by .mhclo (the garment follows the body's shape), hide the
# skin under it, render. Argument after --: the asset's name (folder in mpfbu/data/clothes). Preview only.
import bpy, os, sys
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
ASSET = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "male_worksuit01"
LOG = os.path.join(HERE, "try_garment.log"); log = []
try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    import addon_utils
    addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    cov = bpy.data.objects["Coverall"]; cov.hide_render = True; cov.hide_viewport = True
    body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    before = set(bpy.data.objects)
    path = AssetService.find_asset_absolute_path("%s/%s.mhclo" % (ASSET, ASSET), asset_subdir="clothes")
    log.append("asset path: %s" % path)
    HumanService.add_mhclo_asset(path, body, asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
    g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH')
    log.append("garment %s: %d verts, %d faces" % (g.name, len(g.data.vertices), len(g.data.polygons)))
    # hide the skin under the garment: the body mask is the coverall's (same cut: neck, wrists, ankles)
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x, sc.render.resolution_y = 600, 900
    for view, loc, tz in (("tq", (2.2, -3.4, 1.15), 0.92), ("side", (3.9, -0.15, 1.05), 0.92), ("back", (-1.6, 3.6, 1.1), 0.92)):
        cam.location = loc; cam.data.lens = 60; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector((0, 0, tz)) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(HERE, "renders", "try_%s_%s.png" % (ASSET, view)); bpy.ops.render.render(write_still=True)
    log.append("rendered")
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
