import bpy, os, sys
HERE = os.path.dirname(__file__)
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
from bl_ext.tantra.mpfb.services.humanservice import HumanService
from bl_ext.tantra.mpfb.services.assetservice import AssetService
body = next(o for o in bpy.data.objects if o.name.endswith('.body')); before = set(bpy.data.objects)
HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("elvs_male_coveralls_1/elvs_male_coveralls_1.mhclo", asset_subdir="clothes"), body, asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH'); L = []
L.append("name %s parent %s scale %s" % (g.name, g.parent.name if g.parent else None, tuple(round(x, 3) for x in g.matrix_world.to_scale())))
L.append("modifiers: " + ", ".join("%s(%s)" % (m.type, getattr(m, 'object', None).name if getattr(m, 'object', None) else '') for m in g.modifiers))
L.append("vgroups %d: %s" % (len(g.vertex_groups), [v.name for v in g.vertex_groups][:12]))
L.append("uv layers: %s" % [u.name for u in g.data.uv_layers]); L.append("materials: %s" % [m.name for m in g.data.materials])
nw = sum(1 for v in g.data.vertices if not v.groups); L.append("verts without weights: %d / %d" % (nw, len(g.data.vertices)))
import numpy as np
V = np.array([g.matrix_world @ v.co for v in g.data.vertices]); L.append("bbox z %.3f..%.3f x %.3f..%.3f" % (V[:,2].min(), V[:,2].max(), V[:,0].min(), V[:,0].max()))
# sleeve ends: max |x| region
L.append("sleeve end |x| %.3f; leg bottom z %.3f; collar top z %.3f" % (np.abs(V[:,0]).max(), V[V[:,2] < 0.3][:,2].min() if (V[:,2] < 0.3).any() else -1, V[:,2].max()))
# body: coverall's mask group
L.append("body groups: %s" % [v.name for v in body.vertex_groups if 'cover' in v.name.lower()])
open(os.path.join(HERE, "probe_garment.log"), "w").write("\n".join(L))
