# Crew suit, white with 15-20 % red, in the design language of Deus Ex: Human Revolution (user 2026-10-02): angular panels
# and seams, red following the seams, a fine geometric micro-pattern in the white cloth, the collar coloured along its own
# seam (no horizontal cut across the shoulders). PREVIEW ONLY. Argument after --: 1..5. Writes renders/cx_<n>.png, _cu.png
# and logs the red share of the cloth area.
# Clean edges: the red regions are thresholds of smooth fields - analytic in x/z, or per-vertex distances interpolated
# across faces (collar: distance to the neck; sides: distance to the outer silhouette) - never per-vertex colours.
import bpy, bmesh, os, sys, math
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
N = int(sys.argv[sys.argv.index("--") + 1]) if "--" in sys.argv else 1
LOG = os.path.join(HERE, "concept_dx.log"); log = []
WHITE, RED = (0.80, 0.80, 0.78), (0.30, 0.012, 0.012)
A = "elvs_racing_fire_suit_female1"
VZ, VK = 1.27, 0.075 / 0.18   # the V: its point (z) and the slope to the collar corners (x 0.075 at z 1.45)
WTOP = 1.17                    # the side wedges end below the armpit (higher, the arm joins and the edge tears)

def masks_np(P, F):
    """the same red masks as the shader, in numpy (for the area share). P (n,3) world, F (n,2) fields (neck, side)"""
    ax, z = np.abs(P[:, 0]), P[:, 2]; neck, side = F[:, 0], F[:, 1]
    collar = neck < 0; cuffs = ax > 0.445
    if N == 1:   # angled shoulder panels from the collar down to the shoulder point
        r = collar | cuffs | ((z > 1.42 - 0.60 * (ax - 0.07)) & (ax < 0.37) & (z > 1.12))
    elif N == 2: # side wedges: narrow at the hip, widening to the armpit
        w = 0.015 + 0.045 * np.clip((z - 0.95) / (WTOP - 0.95), 0, 1)
        vee = (P[:, 1] < 0) & (ax < (z - VZ) * VK) & (z < 1.47)
        r = collar | cuffs | vee | ((side < w) & (z > 0.92) & (z < WTOP) & (ax < 0.22))
    elif N == 3: # V-yoke line, side seams down the legs, collar and cuffs: piping 1 cm
        r = collar | cuffs | (np.abs(z - (1.30 + 0.45 * ax)) < 0.008) & (ax < 0.22) | ((side < 0.02) & (z < 1.22))
    elif N == 4: # raglan: from the collar seam to the armpit, over the shoulder down to the elbow
        r = collar | ((z > 1.44 - 1.8 * (ax - 0.07)) & (ax < 0.34) & (z > 1.0))
    else:        # angled waist band (dips to the front), collar, cuffs, bands above the boots
        r = collar | cuffs | (np.abs(z - (1.00 + 0.10 * np.clip(ax / 0.2, 0, 1))) < 0.035) & (ax < 0.26) | (z < 0.24)
    return r

def build_material(g, fields_name):
    m = bpy.data.materials.new("dx%d" % N); m.use_nodes = True; nt = m.node_tree; L = nt.links
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'); b.inputs["Roughness"].default_value = 0.8
    geo = nt.nodes.new("ShaderNodeNewGeometry"); sep = nt.nodes.new("ShaderNodeSeparateXYZ"); L.new(geo.outputs["Position"], sep.inputs[0])
    at = nt.nodes.new("ShaderNodeVertexColor"); at.layer_name = fields_name; fs = nt.nodes.new("ShaderNodeSeparateColor"); L.new(at.outputs[0], fs.inputs[0])
    def M_(op, a, b_=None):
        n = nt.nodes.new("ShaderNodeMath"); n.operation = op
        for k, v in enumerate((a, b_)):
            if v is None: continue
            if isinstance(v, (int, float)): n.inputs[k].default_value = v
            else: L.new(v, n.inputs[k])
        return n.outputs[0]
    x, z = sep.outputs[0], sep.outputs[2]; ax = M_('ABSOLUTE', x)
    neck = M_('SUBTRACT', fs.outputs[0], 0.5); side = fs.outputs[1]          # neck field stored +0.5 (colours clamp at 0)
    lt = lambda a, b_: M_('LESS_THAN', a, b_); gt = lambda a, b_: M_('GREATER_THAN', a, b_)
    OR = lambda a, b_: M_('MAXIMUM', a, b_); AND = lambda a, b_: M_('MULTIPLY', a, b_)
    collar = lt(neck, 0.0); cuffs = gt(ax, 0.445)
    lin = lambda k, c: M_('ADD', M_('MULTIPLY', ax, k), c)                   # k*ax + c
    if N == 1:
        r = OR(OR(collar, cuffs), AND(AND(gt(z, lin(-0.60, 1.42 + 0.60 * 0.07)), lt(ax, 0.37)), gt(z, 1.12)))
    elif N == 2:
        w = M_('ADD', M_('MULTIPLY', M_('MINIMUM', M_('MAXIMUM', M_('DIVIDE', M_('SUBTRACT', z, 0.95), WTOP - 0.95), 0.0), 1.0), 0.045), 0.015)
        vee = AND(AND(lt(sep.outputs[1], 0.0), lt(ax, M_('MULTIPLY', M_('SUBTRACT', z, VZ), VK))), lt(z, 1.47))
        r = OR(OR(OR(collar, cuffs), vee), AND(AND(AND(lt(side, w), gt(z, 0.92)), lt(z, WTOP)), lt(ax, 0.22)))
    elif N == 3:
        v = AND(lt(M_('ABSOLUTE', M_('SUBTRACT', z, lin(0.45, 1.30))), 0.008), lt(ax, 0.22))
        r = OR(OR(collar, cuffs), OR(v, AND(lt(side, 0.02), lt(z, 1.22))))
    elif N == 4:
        r = OR(collar, AND(AND(gt(z, lin(-1.8, 1.44 + 1.8 * 0.07)), lt(ax, 0.33)), gt(z, 1.0)))
    else:
        band = AND(lt(M_('ABSOLUTE', M_('SUBTRACT', z, M_('ADD', M_('MULTIPLY', M_('MINIMUM', M_('DIVIDE', ax, 0.2), 1.0), 0.10), 1.00))), 0.035), lt(ax, 0.26))
        r = OR(OR(collar, cuffs), OR(band, lt(z, 0.24)))
    mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; L.new(r, mix.inputs[0])
    mix.inputs[6].default_value = (*WHITE, 1); mix.inputs[7].default_value = (*RED, 1); L.new(mix.outputs[2], b.inputs["Base Color"])
    # fine hexagonal micro-pattern in the cloth (Voronoi cells ~3 mm, as a light bump)
    vo = nt.nodes.new("ShaderNodeTexVoronoi"); vo.feature = 'DISTANCE_TO_EDGE'; vo.inputs["Scale"].default_value = 300.0
    L.new(geo.outputs["Position"], vo.inputs["Vector"])
    bump = nt.nodes.new("ShaderNodeBump"); bump.inputs["Strength"].default_value = 0.25; bump.inputs["Distance"].default_value = 0.0004
    L.new(vo.outputs["Distance"], bump.inputs["Height"]); L.new(bump.outputs[0], b.inputs["Normal"])
    return m

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
    import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    before = set(bpy.data.objects)
    HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("%s/%s.mhclo" % (A, A), asset_subdir="clothes"), body,
                                 asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
    g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH'); M = g.matrix_world
    dg = body.vertex_groups.get("Delete." + A)
    for m_ in list(body.modifiers):
        if m_.type == 'MASK' and m_.name != "Hide helpers": body.modifiers.remove(m_)
    mk = body.modifiers.new("HideC", 'MASK'); mk.vertex_group = dg.name; mk.invert_vertex_group = True
    # the centre ridge between the breasts smoothed (as accepted)
    bm = bmesh.new(); bm.from_mesh(g.data)
    zone = [(v, float(max(0.0, 1 - abs((M @ v.co).x) / 0.05))) for v in bm.verts if abs((M @ v.co).x) < 0.05 and 1.08 < (M @ v.co).z < 1.42 and (M @ v.co).y < -0.02]
    for _ in range(60):
        nw = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5 * w)) for v, w in zone]
        for v, co in nw: v.co = co
    bm.to_mesh(g.data); bm.free()
    # fields: neck - the collar is the band above the neckline seam: the horizontal distance to the neck axis minus the
    # collar radius, only above the collar's foot (positive elsewhere); side - distance to the outer silhouette per height
    P = np.array([M @ v.co for v in g.data.vertices])
    # the neck from the body: its axis and radius at the collar height; the collar is the cloth standing round it
    Mb = body.matrix_world; gb = body.vertex_groups['body'].index
    BN = np.array([Mb @ v.co for v in body.data.vertices if any(x.group == gb for x in v.groups)])
    nk = BN[(BN[:, 2] > 1.44) & (BN[:, 2] < 1.50) & (np.abs(BN[:, 0]) < 0.08)]
    cx, cy = nk[:, 0].mean(), nk[:, 1].mean(); rr = np.percentile(np.hypot(nk[:, 0] - cx, nk[:, 1] - cy), 90)
    hd = np.hypot(P[:, 0] - cx, P[:, 1] - cy)
    # continuous in both terms (a binary cut gave a ragged edge): inside the stand (hd) and above its foot (z)
    neck = np.maximum(hd - (rr + 0.018), (1.445 - P[:, 2]) * 0.6)
    log.append('collar: neck r %.3f, %d verts in' % (rr, int((neck < 0).sum())))
    torso = (np.abs(P[:, 0]) < 0.24) & (P[:, 2] > 0.86) & (P[:, 2] < 1.40); legs = P[:, 2] <= 0.86; sel = torso | legs
    kb = np.floor(P[:, 2] / 0.01).astype(int); out = {}
    for i in np.nonzero(sel)[0]:
        k = (kb[i], 1 if P[i, 0] > 0 else -1); out[k] = max(out.get(k, 0), abs(P[i, 0]))
    side = np.ones(len(P))
    for i in np.nonzero(sel)[0]:
        xm = max(out.get((kb[i] + d, 1 if P[i, 0] > 0 else -1), 0) for d in (-1, 0, 1)); side[i] = xm - abs(P[i, 0])
    fa = g.data.color_attributes.new("fields", 'FLOAT_COLOR', 'POINT')
    for i in range(len(P)): fa.data[i].color = (float(neck[i] + 0.5), float(side[i]), 0.0, 1.0)
    g.data.materials.clear(); g.data.materials.append(build_material(g, "fields"))
    for p in g.data.polygons: p.use_smooth = True
    # red share of the cloth area (face centres)
    g.data.calc_loop_triangles(); ar = []; rd = []
    F = np.stack([neck, side], 1)
    for t in g.data.loop_triangles:
        c = P[list(t.vertices)].mean(0); f = F[list(t.vertices)].mean(0); ar.append(t.area); rd.append(masks_np(c[None], f[None])[0])
    ar = np.array(ar); rd = np.array(rd, bool); log.append("variant %d: red %.1f %% of the cloth" % (N, 100 * ar[rd].sum() / ar.sum()))
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.view_settings.exposure = -0.6
    sc.render.resolution_x, sc.render.resolution_y = 500, 1000
    cam.location = (0, -4.4, 1.0); cam.data.lens = 58; cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0, 0, 0.90)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", "cx_%d.png" % N); bpy.ops.render.render(write_still=True)
    sc.render.resolution_x = sc.render.resolution_y = 500
    cam.location = (0.35, -0.85, 1.38); cam.data.lens = 55
    cam.rotation_quaternion = (Vector((0, -0.03, 1.30)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", "cx_%d_cu.png" % N); bpy.ops.render.render(write_still=True)
    sc.render.resolution_x, sc.render.resolution_y = 500, 1000
    cam.location = (2.6, -3.4, 1.05); cam.data.lens = 58
    cam.rotation_quaternion = (Vector((0, 0, 0.90)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(HERE, "renders", "cx_%d_tq.png" % N); bpy.ops.render.render(write_still=True)
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
