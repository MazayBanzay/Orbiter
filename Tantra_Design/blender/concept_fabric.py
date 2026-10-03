# Crew suit (the user's pick: the cut of concept 3 without the quilting, elvs_racing_fire_suit_female1) in five colours and
# fabrics. PREVIEW ONLY. Argument after --: 1..5. Writes renders/cf_<n>.png (full figure) and cf_<n>_cu.png (chest close-up).
# Fabric relief is procedural (world coordinates): twill, ripstop grid, rib, satin sheen, matte.
import bpy, os, sys, math
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
N = int(sys.argv[sys.argv.index("--") + 1]) if "--" in sys.argv else 1
LOG = os.path.join(HERE, "concept_fabric.log"); log = []
#    name                         colour               trim (yoke, collar, cuffs, zip)   rough metal sheen  relief
WHITE, RED = (0.80, 0.80, 0.78), (0.30, 0.012, 0.012)
# user 2026-10-02: white with red, five ways of placing the red
V = {1: ("w_red_yoke",    WHITE, RED, 0.85, 0.0, 0.0, "twill", "yoke"),     # red yoke, collar, cuffs
     2: ("w_red_collar",  WHITE, RED, 0.85, 0.0, 0.0, "twill", "collar"),   # red collar and cuffs only
     3: ("w_red_piping",  WHITE, RED, 0.85, 0.0, 0.0, "twill", "piping"),   # thin red piping: yoke edge, side seams
     4: ("w_red_sides",   WHITE, RED, 0.85, 0.0, 0.0, "twill", "sides"),    # red side panels, torso and legs
     5: ("w_red_sleeves", WHITE, RED, 0.85, 0.0, 0.0, "twill", "sleeves")}[N] # red outer sleeves from the shoulder

def fabric(name, col, trim, rough, metal, sheen, relief, scheme):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; L = nt.links
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if "Sheen Weight" in b.inputs: b.inputs["Sheen Weight"].default_value = sheen
    geo = nt.nodes.new("ShaderNodeNewGeometry"); sep = nt.nodes.new("ShaderNodeSeparateXYZ"); L.new(geo.outputs["Position"], sep.inputs[0])
    def mth(op, a, b_=None, v=None):
        n = nt.nodes.new("ShaderNodeMath"); n.operation = op
        (L.new(a, n.inputs[0]) if not isinstance(a, float) else n.inputs[0].__setattr__("default_value", a))
        if b_ is not None: (L.new(b_, n.inputs[1]) if not isinstance(b_, float) else n.inputs[1].__setattr__("default_value", b_))
        return n.outputs[0]
    x, y, z = sep.outputs[0], sep.outputs[1], sep.outputs[2]
    ax = mth('ABSOLUTE', x)
    nrm = nt.nodes.new("ShaderNodeSeparateXYZ"); L.new(geo.outputs["Normal"], nrm.inputs[0]); anx = mth('ABSOLUTE', nrm.outputs[0])
    collar = mth('GREATER_THAN', z, 1.445); cuffs = mth('GREATER_THAN', ax, 0.445)
    if scheme == "yoke":     t = mth('MAXIMUM', mth('GREATER_THAN', z, 1.37), cuffs)
    elif scheme == "collar": t = mth('MAXIMUM', collar, cuffs)
    elif scheme == "piping":
        yl = mth('LESS_THAN', mth('ABSOLUTE', mth('SUBTRACT', z, 1.37)), 0.004)                  # 8 mm line at the yoke edge
        att = nt.nodes.new("ShaderNodeVertexColor"); att.layer_name = "seam"; sx = nt.nodes.new("ShaderNodeSeparateColor"); L.new(att.outputs[0], sx.inputs[0])
        side = mth('GREATER_THAN', sx.outputs[0], 0.5)
        t = mth('MAXIMUM', mth('MAXIMUM', yl, side), mth('MAXIMUM', collar, cuffs))
    elif scheme == "sides":
        att = nt.nodes.new("ShaderNodeVertexColor"); att.layer_name = "seam"; sx = nt.nodes.new("ShaderNodeSeparateColor"); L.new(att.outputs[0], sx.inputs[0])
        side = mth('GREATER_THAN', sx.outputs[1], 0.5)
        t = mth('MAXIMUM', side, collar)
    else:   # sleeves: the outer half of each sleeve, from the shoulder to the cuff
        slv = mth('MULTIPLY', mth('GREATER_THAN', ax, 0.17), mth('MULTIPLY', mth('GREATER_THAN', z, 0.85), mth('GREATER_THAN', nrm.outputs[2], -0.15)))
        t = mth('MAXIMUM', slv, collar)
    mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; L.new(t, mix.inputs[0])
    mix.inputs[6].default_value = (*col, 1); mix.inputs[7].default_value = (*trim, 1); L.new(mix.outputs[2], b.inputs["Base Color"])
    if relief != "none":
        def bands(u, period):
            w = nt.nodes.new("ShaderNodeTexWave"); w.wave_type = 'BANDS'; w.bands_direction = 'X'; w.inputs["Scale"].default_value = 1.0
            w.inputs["Distortion"].default_value = 0; cmb = nt.nodes.new("ShaderNodeCombineXYZ"); L.new(mth('DIVIDE', u, period), cmb.inputs[0]); L.new(cmb.outputs[0], w.inputs["Vector"])
            return w.outputs["Fac"]
        if relief == "twill":   h = bands(mth('ADD', x, z), 0.004)                                   # 2.5 mm diagonal
        elif relief == "rib":   h = bands(z, 0.007)                                                    # 4 mm horizontal rib
        else:                   h = mth('MAXIMUM', mth('GREATER_THAN', bands(x, 0.012), 0.85),         # 6 mm ripstop grid
                                         mth('GREATER_THAN', bands(z, 0.012), 0.85))
        bump = nt.nodes.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.5; bump.inputs["Distance"].default_value = 0.0006
        L.new(h, bump.inputs["Height"]); L.new(bump.outputs[0], b.inputs["Normal"])
    return m

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
    import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    body = next(o for o in bpy.data.objects if o.name.endswith('.body')); A = "elvs_racing_fire_suit_female1"
    before = set(bpy.data.objects)
    HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("%s/%s.mhclo" % (A, A), asset_subdir="clothes"), body,
                                 asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
    g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH')
    dg = body.vertex_groups.get("Delete." + A)
    for m_ in list(body.modifiers):
        if m_.type == 'MASK' and m_.name != "Hide helpers": body.modifiers.remove(m_)
    mk = body.modifiers.new("HideC", 'MASK'); mk.vertex_group = dg.name; mk.invert_vertex_group = True
    import bmesh
    bm = bmesh.new(); bm.from_mesh(g.data); M = g.matrix_world; Mi = M.inverted()
    zone = [(v, float(max(0.0, 1 - abs((M @ v.co).x) / 0.05))) for v in bm.verts
            if abs((M @ v.co).x) < 0.05 and 1.08 < (M @ v.co).z < 1.42 and (M @ v.co).y < -0.02]
    bm.normal_update()
    for it in range(60):   # outward fill (the cloth spans the cleavage) and relax: no ridge, no fold
        if it % 10 == 0: bm.normal_update()
        nw = []
        for v, w in zone:
            c = sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges)
            nw.append((v, v.co.lerp(c, 0.5 * w)))
        for v, co in nw: v.co = co
    bm.to_mesh(g.data); bm.free()
    # side seams: per 1 cm height band, the outermost cloth on each side (torso: |x| < 0.24; legs below the crotch:
    # each leg's outer side). R = an 8 mm piping strip, G = a 45 mm side panel (front and back halves of the side)
    import numpy as np
    P = np.array([M @ v.co for v in g.data.vertices]); seam = g.data.color_attributes.new("seam", 'FLOAT_COLOR', 'POINT')
    torso = (np.abs(P[:, 0]) < 0.24) & (P[:, 2] > 0.86) & (P[:, 2] < 1.32); legs = P[:, 2] <= 0.86
    sel = torso | legs; kb = np.floor(P[:, 2] / 0.01).astype(int)
    out = {}
    for i in np.nonzero(sel)[0]:
        key = (kb[i], 1 if P[i, 0] > 0 else -1); out[key] = max(out.get(key, 0), abs(P[i, 0]))
    for i, v in enumerate(g.data.vertices):
        r = g_ = 0.0
        if sel[i]:
            xm = max(out.get((kb[i] + d, 1 if P[i, 0] > 0 else -1), 0) for d in (-1, 0, 1))
            dd = xm - abs(P[i, 0])
            r = 1.0 if dd < 0.006 else 0.0; g_ = 1.0 if dd < 0.03 else 0.0
        seam.data[i].color = (r, g_, 0, 1)
    g.data.materials.clear(); g.data.materials.append(fabric(*V))
    for p in g.data.polygons: p.use_smooth = True
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.view_settings.exposure = -0.6
    sc.render.resolution_x, sc.render.resolution_y = 500, 1000
    cam.location = (0, -4.4, 1.0); cam.data.lens = 58; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0, 0, 0.90)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", "cf_%d.png" % N); bpy.ops.render.render(write_still=True)
    sc.render.resolution_x = sc.render.resolution_y = 500
    cam.location = (0.25, -0.75, 1.30); cam.data.lens = 60
    cam.rotation_quaternion = (Vector((0, -0.05, 1.22)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", "cf_%d_cu.png" % N); bpy.ops.render.render(write_still=True)
    log.append("rendered %d" % N)
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
