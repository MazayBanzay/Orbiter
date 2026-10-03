# Five technical concepts for the crew's clothing, grounded in the novel (2026-10-02). PREVIEW ONLY, no game file touched.
# Built from MakeHuman Community garments (cut and recoloured) on the approved body; patterns are procedural shaders
# (world coordinates), so no UVs are needed for a concept. Argument after --: variant 1..5. Writes renders/ct_<n>.png.
#  1 Council of Starflight ship garment: silvery silky cloth, stand collar, wide astronaut belt   (canon: silvery clothes
#    of the Council, silky sheen; "широкий пояс астролетчика")
#  2 Working clothes of the Great World: loose blue artificial-linen shirt, open collar, two chest pockets; wide short
#    trousers above the knee   (canon, described exactly)
#  3 Under-suit coverall: quilted panels over the torso (hide the figure, carry heating), ribbed cuffs, belt with the
#    suit connectors   (canon: heated biological suits put on over clothing in the airlock)
#  4 Spaceport technician: white loose coverall, offset zip, wide belt   (canon: the departure commission in white coveralls)
#  5 Short coverall with several fasteners: knee-length, short sleeves, shoulder and side fasteners   (canon: light short
#    coveralls of the steppe workers; "short garments like elegant coveralls with several fasteners")
import bpy, bmesh, os, sys, math
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
N = int(sys.argv[sys.argv.index("--") + 1]) if "--" in sys.argv else 1
LOG = os.path.join(HERE, "concept_tech.log"); log = []

def principled(name, rgb, rough=0.8, metal=0.0, sheen=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*rgb, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if sheen and "Sheen Weight" in b.inputs: b.inputs["Sheen Weight"].default_value = sheen
    return m, nt, b

def pos_node(nt):
    g = nt.nodes.new("ShaderNodeNewGeometry"); return g.outputs["Position"]

def quilted(name, rgb):
    """diagonal quilting (rhombs ~4 cm) on the torso band z 1.0..1.42: bump + a slight shade in the stitch lines"""
    m, nt, b = principled(name, rgb, 0.85)
    p = pos_node(nt)
    sep = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(p, sep.inputs[0])
    def line(sign):
        mth = nt.nodes.new("ShaderNodeMath"); mth.operation = 'MULTIPLY_ADD'; mth.inputs[1].default_value = sign
        nt.links.new(sep.outputs[0], mth.inputs[0]); nt.links.new(sep.outputs[2], mth.inputs[2])
        w = nt.nodes.new("ShaderNodeTexWave"); w.wave_type = 'BANDS'; w.inputs["Scale"].default_value = 1.0
        w.bands_direction = 'X'; w.inputs["Distortion"].default_value = 0
        cmb = nt.nodes.new("ShaderNodeCombineXYZ"); sc_ = nt.nodes.new("ShaderNodeMath"); sc_.operation = 'MULTIPLY'; sc_.inputs[1].default_value = 25.0
        nt.links.new(mth.outputs[0], sc_.inputs[0]); nt.links.new(sc_.outputs[0], cmb.inputs[0]); nt.links.new(cmb.outputs[0], w.inputs["Vector"])
        return w.outputs["Fac"]
    a, c = line(1.0), line(-1.0)
    mn = nt.nodes.new("ShaderNodeMath"); mn.operation = 'MINIMUM'; nt.links.new(a, mn.inputs[0]); nt.links.new(c, mn.inputs[1])
    # only on the torso band
    zlo = nt.nodes.new("ShaderNodeMath"); zlo.operation = 'GREATER_THAN'; zlo.inputs[1].default_value = 1.06; nt.links.new(sep.outputs[2], zlo.inputs[0])
    zhi = nt.nodes.new("ShaderNodeMath"); zhi.operation = 'LESS_THAN'; zhi.inputs[1].default_value = 1.40; nt.links.new(sep.outputs[2], zhi.inputs[0])
    band = nt.nodes.new("ShaderNodeMath"); band.operation = 'MULTIPLY'; nt.links.new(zlo.outputs[0], band.inputs[0]); nt.links.new(zhi.outputs[0], band.inputs[1])
    inv = nt.nodes.new("ShaderNodeMath"); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0; nt.links.new(mn.outputs[0], inv.inputs[1])
    q = nt.nodes.new("ShaderNodeMath"); q.operation = 'MULTIPLY'; nt.links.new(inv.outputs[0], q.inputs[0]); nt.links.new(band.outputs[0], q.inputs[1])
    bump = nt.nodes.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.6; bump.inputs["Distance"].default_value = 0.004
    nt.links.new(q.outputs[0], bump.inputs["Height"]); nt.links.new(bump.outputs[0], b.inputs["Normal"])
    return m

def add_asset(body, asset):
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    before = set(bpy.data.objects)
    HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("%s/%s.mhclo" % (asset, asset), asset_subdir="clothes"), body,
                                 asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
    g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH')
    dg = body.vertex_groups.get("Delete." + asset)
    ids = {v.index for v in body.data.vertices if dg and any(x.group == dg.index for x in v.groups)}
    # bake the armature into the mesh (static concept) so cuts and belts work in world space
    dgr = bpy.context.evaluated_depsgraph_get(); me = bpy.data.meshes.new_from_object(g.evaluated_get(dgr))
    o = bpy.data.objects.new(asset, me); o.matrix_world = g.matrix_world; bpy.context.scene.collection.objects.link(o)
    bpy.data.objects.remove(g, do_unlink=True)
    return o, ids

def cut(o, keep):
    bm = bmesh.new(); bm.from_mesh(o.data); M = o.matrix_world
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not keep(M @ v.co)], context='VERTS'); bm.to_mesh(o.data); bm.free()

def belt(ref, z, h, mat, off=0.006):
    """a wide belt: a band around the garment at height z, following its cross-section, standing off by 'off'"""
    V = np.array([ref.matrix_world @ v.co for v in ref.data.vertices]); s = V[np.abs(V[:, 2] - z) < 0.02]
    s = s[np.abs(s[:, 0]) < 0.25]
    c = s[:, :2].mean(0); ang = np.arctan2(s[:, 1] - c[1], s[:, 0] - c[0]); r = np.hypot(s[:, 0] - c[0], s[:, 1] - c[1])
    K = 72; rr = []
    for k in range(K):
        a = -math.pi + 2 * math.pi * (k + 0.5) / K; d = np.abs(np.angle(np.exp(1j * (ang - a))))
        rr.append(r[d < math.pi / K * 1.5].max() if (d < math.pi / K * 1.5).any() else np.nan)
    rr = np.array(rr); idx = np.arange(K); ok = ~np.isnan(rr); rr = np.interp(idx, idx[ok], rr[ok], period=K)
    rr = np.convolve(np.r_[rr[-3:], rr, rr[:3]], np.ones(5) / 5, 'same')[3:-3]
    me = bpy.data.meshes.new("belt"); verts = []; faces = []
    for j, zz in enumerate((z - h / 2, z + h / 2)):
        for k in range(K):
            a = -math.pi + 2 * math.pi * (k + 0.5) / K; R = rr[k] + off
            verts.append((c[0] + R * math.cos(a), c[1] + R * math.sin(a), zz))
    for k in range(K): faces.append((k, (k + 1) % K, K + (k + 1) % K, K + k))
    me.from_pydata(verts, [], faces); o = bpy.data.objects.new("belt", me); bpy.context.scene.collection.objects.link(o)
    so = o.modifiers.new("t", 'SOLIDIFY'); so.thickness = 0.0025; o.data.materials.append(mat)
    for p in me.polygons: p.use_smooth = True
    return o

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
    import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    hid = set()
    STEEL = (0.20, 0.24, 0.31)
    # five crew suits of the starship, all full length; a slim belt at most (user: no shorts, no huge belts)
    def paint(o, fn, name):   # per-vertex colour from a function of the world position and normal
        col = o.data.color_attributes.new("c", 'FLOAT_COLOR', 'POINT'); M = o.matrix_world; R3 = M.to_3x3()
        for v in o.data.vertices:
            col.data[v.index].color = (*fn(M @ v.co, (R3 @ v.normal).normalized()), 1)
        m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
        b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'); b.inputs["Roughness"].default_value = 0.6
        a = nt.nodes.new("ShaderNodeVertexColor"); a.layer_name = "c"; nt.links.new(a.outputs[0], b.inputs["Base Color"])
        o.data.materials.clear(); o.data.materials.append(m); return b
    LIGHT, SILVER, STEEL, DARK = (0.72, 0.72, 0.70), (0.55, 0.57, 0.60), (0.12, 0.17, 0.26), (0.04, 0.05, 0.07)
    zipf = lambda p, n: abs(p.x) < 0.008 and p.y < -0.03 and 0.80 < p.z < 1.47
    if N == 1:   # crew suit of the Council of Starflight: silvery, silky, stand collar, central closure
        g, h = add_asset(body, "elvs_racing_fire_suit_female1"); hid |= h
        b = paint(g, lambda p, n: DARK if zipf(p, n) else SILVER, "c1"); b.inputs["Metallic"].default_value = 0.35; b.inputs["Roughness"].default_value = 0.35
    elif N == 2:   # two-tone flight suit: steel yoke over the shoulders and down the outer sleeves, steel side panels
        g, h = add_asset(body, "elvs_racing_fire_suit_female1"); hid |= h
        def f2(p, n):
            if zipf(p, n): return DARK
            if p.z > 1.33 or (abs(p.x) > 0.20 and p.z > 0.9 and n.z > -0.2 and abs(n.x) > 0.3): return STEEL
            if abs(n.x) > 0.6 and 0.15 < p.z < 1.30: return STEEL
            return LIGHT
        paint(g, f2, "c2")
    elif N == 3:   # quilted crew suit: quilted torso (hides the figure, warms), plain sleeves and legs
        g, h = add_asset(body, "elvs_racing_fire_suit_female1"); hid |= h
        g.data.materials.clear(); g.data.materials.append(quilted("quilt", (0.20, 0.24, 0.30)))
    elif N == 4:   # diagonal closure: the flap from the collar across the chest to the right hip, steel edge; slim belt
        g, h = add_asset(body, "elvs_male_coveralls_1"); hid |= h
        def f4(p, n):
            t = min(max((1.46 - p.z) / 0.40, 0), 1); lx = 0.04 - 0.19 * t
            if 1.03 < p.z < 1.47 and p.y < -0.03 and abs(p.x - lx) < 0.012: return DARK
            if p.z > 1.44: return DARK
            return (0.22, 0.27, 0.22)
        paint(g, f4, "c4")
        belt(g, 1.02, 0.03, principled("beltS", DARK, 0.5)[0], off=0.001)
    elif N == 5:   # loose crew coverall: hangs from the shoulders, cuffed sleeves and legs, no pockets on the chest, slim belt
        g, h = add_asset(body, "elvs_male_coveralls_1"); hid |= h
        paint(g, lambda p, n: DARK if (zipf(p, n) or p.z > 1.44) else (0.10, 0.13, 0.20), "c5")
        belt(g, 1.02, 0.03, principled("beltL", DARK, 0.5)[0], off=0.001)
    msk = body.vertex_groups.new(name="hid"); msk.add(list(hid), 1.0, 'REPLACE')
    for m_ in list(body.modifiers):
        if m_.type == 'MASK' and m_.name != "Hide helpers": body.modifiers.remove(m_)
    mk = body.modifiers.new("HideC", 'MASK'); mk.vertex_group = msk.name; mk.invert_vertex_group = True
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x, sc.render.resolution_y = 500, 1000
    sc.view_settings.exposure = -0.8   # the stage light washed the colours out
    cam.location = (0, -4.4, 1.0); cam.data.lens = 58; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0, 0, 0.90)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", "ct_%d.png" % N); bpy.ops.render.render(write_still=True)
    log.append("rendered %d" % N)
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
