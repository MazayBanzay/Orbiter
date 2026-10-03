# Five clothing concepts on the approved body, built from MakeHuman Community garments (CC0 / CC-BY), front view each.
# PREVIEW ONLY: no game file is touched. Argument after --: the variant number (1..5). Writes renders/cv_<n>.png.
#  1 classic flight coverall      elvs_male_coveralls_1
#  2 ship flight suit (knit)      elvs_racing_fire_suit_female1
#  3 diagonal wrap closure        elvs_male_coveralls_1 + a wrap flap marked by a steel edge (colour only - a mock-up)
#  4 coverall + short jacket      elvs_racing_fire_suit_female1 + elvs_emt_uniform_jacket_female
#  5 belted wrap tunic + trousers mindfront_f_ninja without its hood and mask
import bpy, os, sys, math
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
N = int(sys.argv[sys.argv.index("--") + 1]) if "--" in sys.argv else 1
LIGHT, STEEL = (0.80, 0.80, 0.77), (0.16, 0.20, 0.27)
SPEC = {1: [("elvs_male_coveralls_1", LIGHT)], 2: [("elvs_racing_fire_suit_female1", LIGHT)], 3: [("elvs_male_coveralls_1", LIGHT)],
        4: [("elvs_racing_fire_suit_female1", LIGHT), ("elvs_emt_uniform_jacket_female", STEEL)], 5: [("mindfront_f_ninja", LIGHT)]}
LOG = os.path.join(HERE, "concept_variants.log"); log = []

def plain(name, rgb, attr=None):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'); b.inputs["Roughness"].default_value = 0.88
    if attr:
        a = nt.nodes.new("ShaderNodeVertexColor"); a.layer_name = attr; nt.links.new(a.outputs[0], b.inputs["Base Color"])
    else: b.inputs["Base Color"].default_value = (*rgb, 1)
    return m

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
    import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    hide = body.vertex_groups.new(name="hidden_concept"); hidx = set()
    for asset, rgb in SPEC[N]:
        before = set(bpy.data.objects)
        HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("%s/%s.mhclo" % (asset, asset), asset_subdir="clothes"), body,
                                     asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
        g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH')
        dg = body.vertex_groups.get("Delete." + asset)
        if dg: hidx |= {v.index for v in body.data.vertices if any(x.group == dg.index for x in v.groups)}
        M = g.matrix_world
        if N == 4 and asset == "elvs_racing_fire_suit_female1":   # under the jacket: only the legs of the suit stay
            import bmesh
            bm = bmesh.new(); bm.from_mesh(g.data)
            bmesh.ops.delete(bm, geom=[v for v in bm.verts if (M @ v.co).z > 0.90], context='VERTS'); bm.to_mesh(g.data); bm.free()
        if asset == "mindfront_f_ninja":   # no hood, no mask: everything above the collar line goes
            import bmesh
            bm = bmesh.new(); bm.from_mesh(g.data)
            bmesh.ops.delete(bm, geom=[v for v in bm.verts if (M @ v.co).z > 1.47], context='VERTS'); bm.to_mesh(g.data); bm.free()
        g.data.materials.clear()
        if N == 3 and asset == "elvs_male_coveralls_1":
            # the wrap: the flap runs from the collar, left of the centre, diagonally to the wearer's right side at the waist;
            # its edge is a 2 cm steel band (a mock-up of the closure line)
            col = g.data.color_attributes.new("wrap", 'FLOAT_COLOR', 'POINT')
            for v in g.data.vertices:
                p = M @ v.co; n = (M.to_3x3() @ v.normal)
                t = np.clip((1.46 - p.z) / 0.43, 0, 1); lx = 0.03 - 0.17 * t            # closure line x(z)
                edge = abs(p.x - lx) < 0.018 and 1.02 < p.z < 1.47 and p.y < -0.03
                belt = abs(p.z - 1.02) < 0.025
                c = STEEL if (edge or belt or p.z > 1.45) else LIGHT
                col.data[v.index].color = (*c, 1)
            g.data.materials.append(plain("v3", None, "wrap"))
        else:
            g.data.materials.append(plain("c_" + asset, rgb))
        log.append("%s: %d verts" % (asset, len(g.data.vertices)))
    if N == 5:   # the ninja's delete group hides the neck (its hood covered it)
        Mb = body.matrix_world; hidx = {i for i in hidx if (Mb @ body.data.vertices[i].co).z < 1.43}
    hide.add(list(hidx), 1.0, 'REPLACE')
    for m in list(body.modifiers):
        if m.type == 'MASK' and m.name != "Hide helpers": body.modifiers.remove(m)
    mk = body.modifiers.new("HideConcept", 'MASK'); mk.vertex_group = hide.name; mk.invert_vertex_group = True
    bpy.context.view_layer.update()
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x, sc.render.resolution_y = 500, 1000
    cam.location = (0, -4.4, 1.0); cam.data.lens = 58; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0, 0, 0.90)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", "cv_%d.png" % N); bpy.ops.render.render(write_still=True)
    log.append("rendered %d" % N)
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
