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
SIDEBAND, NXMIN = 0.06, 0.55
# the lightning sign in the side coordinates (s along the surface from the side seam, front negative; z up)
BOLT_TRIS = ["ABC", "ACG", "GCF", "CDE", "CEF"]   # a triangulation of the panel A..G
BOLT = [(-0.06, 1.17), (0.03, 1.17), (0.03, 1.02), (0.085, 0.95), (-0.025, 0.55), (-0.01, 0.95), (-0.06, 1.00)]   # the user's shape (the profile version), now on the surface: straight edges
SLEEVE_R = 0.045
S_RELAX = 120   # relaxation of the side coordinate: the slices' seam angles wobble, the cloth has folds
ZP, WM, DEPTH = 0.62, 0.06, 0.9             # side panels: their point on the thigh (z) and their width at the waist
TL, CUFF = 0.20, 0.035          # sleeves: the triangle's apex from the wrist, the cuff band

def masks_np(P, F):
    """the same red masks as the shader, in numpy (for the area share). P (n,3) world, F (n,2) fields (neck, side)"""
    ax, z = np.abs(P[:, 0]), P[:, 2]; neck, side = F[:, 0], F[:, 1]
    collar = neck < 0; cuffs = ax > 0.445
    if N == 1:   # angled shoulder panels from the collar down to the shoulder point
        r = collar | cuffs | ((z > 1.42 - 0.60 * (ax - 0.07)) & (ax < 0.37) & (z > 1.12))
    elif N == 2: # the user's design: collar V, side panels tapering to a point on the thigh, cuffs running up the forearm
        vee = (P[:, 1] < 0) & (ax < (z - VZ) * VK) & (z < 1.47)
        r = collar | vee | (F[:, 2] < 0)
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
        vee = AND(AND(lt(sep.outputs[1], 0.0), lt(ax, M_('MULTIPLY', M_('SUBTRACT', z, VZ), VK))), lt(z, 1.47))
        at2 = nt.nodes.new("ShaderNodeVertexColor"); at2.layer_name = "surf"; f2 = nt.nodes.new("ShaderNodeSeparateColor"); L.new(at2.outputs[0], f2.inputs[0])
        Sx = M_('SUBTRACT', f2.outputs[0], 0.5)
        # the panel = a union of triangles; a triangle = three half-planes (per pixel, so its edges are straight)
        P_ = dict(zip("ABCDEFG", BOLT)); side_r = None
        for tri in BOLT_TRIS:
            a_, b_, c_ = (P_[k] for k in tri)
            sgn = 1.0 if (b_[0] - a_[0]) * (c_[1] - a_[1]) - (b_[1] - a_[1]) * (c_[0] - a_[0]) > 0 else -1.0
            ins = None
            for (p0, p1) in ((a_, b_), (b_, c_), (c_, a_)):
                # sign of the cross product (p1 - p0) x (q - p0), q = (S, z)
                cr = M_('SUBTRACT', M_('MULTIPLY', M_('SUBTRACT', z, p0[1]), (p1[0] - p0[0]) * sgn),
                                     M_('MULTIPLY', M_('SUBTRACT', Sx, p0[0]), (p1[1] - p0[1]) * sgn))
                h = gt(cr, -1e-4); ins = h if ins is None else AND(ins, h)
            side_r = ins if side_r is None else OR(side_r, ins)
        r = OR(OR(OR(collar, vee), lt(M_('SUBTRACT', fs.outputs[2], 0.5), 0.0)), side_r)
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
    armg = [body.vertex_groups[n].index for n in ("LeftArm", "RightArm", "LeftForeArm", "RightForeArm") if n in body.vertex_groups]
    extra = [v.index for v in body.data.vertices if sum(x.weight for x in v.groups if x.group in armg) > 0.5]
    dg.add(extra, 1.0, 'REPLACE')
    mk = body.modifiers.new("HideC", 'MASK'); mk.vertex_group = dg.name; mk.invert_vertex_group = True
    # the centre ridge between the breasts smoothed (as accepted)
    bm = bmesh.new(); bm.from_mesh(g.data)
    zone = [(v, float(max(0.0, 1 - abs((M @ v.co).x) / 0.05))) for v in bm.verts if abs((M @ v.co).x) < 0.05 and 1.08 < (M @ v.co).z < 1.42 and (M @ v.co).y < -0.02]
    for _ in range(60):
        nw = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5 * w)) for v, w in zone]
        for v, co in nw: v.co = co
    # small loose pieces of the asset (a badge on the leg showed as a black spot) go
    seen = set(); small = []
    for v in bm.verts:
        if v in seen: continue
        st = [v]; c = []
        while st:
            u = st.pop()
            if u in seen: continue
            seen.add(u); c.append(u); st.extend(e.other_vert(u) for e in u.link_edges)
        if len(c) < 200: small += c
    bmesh.ops.delete(bm, geom=small, context='VERTS'); log.append("loose pieces removed: %d verts" % len(small))
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
    # shape field (variant 2), negative = red, continuous so the edges come out clean:
    #  side panels: red where the distance to the silhouette is below w(z) - w grows from 0 at a point on the thigh (ZP)
    #  to WM at the waist and stays to WTOP below the armpit (user's sketch, 2026-10-02)
    #  sleeves: a full cuff (CUFF m from the wrist) and a triangle up the outer forearm, its apex TL from the wrist
    zz = P[:, 2]
    # side panel: the lightning sign (user, 2026-10-02: "ровно", "знак молнии - ориентир"), drawn in the cloth's own
    # side coordinates so its edges run straight ON the surface: s = arc length round the body from the side seam
    # (front negative), z = height. Slices of 1 cm: the torso (|x| < 0.22) round its centre, each leg round its own.
    S = np.full(len(P), 9.0)
    # pass 1: the seam angle per slice and side (outermost point), pass 2: smoothed along the height, pass 3: S
    rec = []   # (k, key, idx, theta, radius)
    for k in np.unique(kb):
        ks = np.nonzero(kb == k)[0]
        if zz[ks].mean() > 0.86: parts = [("t", ks[np.abs(P[ks, 0]) < 0.22], None)]
        else: parts = [("l", ks[P[ks, 0] > 0], 1), ("r", ks[P[ks, 0] < 0], -1)]
        for name, gi, legside in parts:
            if len(gi) < 8: continue
            cx_, cy_ = P[gi, 0].mean(), P[gi, 1].mean()
            for sd in ((1, -1) if legside is None else (legside,)):     # legs: the outer half only
                hs = gi[sd * (P[gi, 0] - cx_) > 0]
                if len(hs) < 4: continue
                th = np.arctan2(P[hs, 1] - cy_, sd * (P[hs, 0] - cx_)); rad = np.hypot(P[hs, 0] - cx_, P[hs, 1] - cy_)
                rec.append((k, (name, sd), hs, th, rad, th[np.argmax(sd * P[hs, 0])]))
    from collections import defaultdict
    byk = defaultdict(dict)
    for k, key, hs, th, rad, t0 in rec: byk[key][k] = t0
    for k, key, hs, th, rad, t0 in rec:
        win = [byk[key][j] for j in range(k - 6, k + 7) if j in byk[key]]
        S[hs] = (th - float(np.median(win))) * rad
    # relax S over the mesh (the slices are 1 cm and the cloth has folds): a smooth coordinate, straight iso-lines
    E_ = np.array([e.vertices[:] for e in g.data.edges]); ok = S < 8
    for _ in range(S_RELAX):
        acc = np.zeros(len(P)); cnt = np.zeros(len(P))
        m_ = ok[E_[:, 0]] & ok[E_[:, 1]]
        np.add.at(acc, E_[m_, 0], S[E_[m_, 1]]); np.add.at(cnt, E_[m_, 0], 1)
        np.add.at(acc, E_[m_, 1], S[E_[m_, 0]]); np.add.at(cnt, E_[m_, 1], 1)
        S = np.where(ok & (cnt > 0), 0.5 * S + 0.5 * acc / np.maximum(cnt, 1), S)
    poly = np.array(BOLT)
    def sdist(Q):
        d = np.full(len(Q), np.inf); inside = np.zeros(len(Q), bool)
        for a_, b_ in zip(poly, np.roll(poly, -1, 0)):
            e = b_ - a_; w_ = Q - a_; t_ = np.clip((w_ @ e) / (e @ e), 0, 1)
            d = np.minimum(d, np.linalg.norm(w_ - np.outer(t_, e), axis=1))
            c1 = (a_[1] > Q[:, 1]) != (b_[1] > Q[:, 1])
            xint = a_[0] + (Q[:, 1] - a_[1]) * (b_[0] - a_[0]) / (b_[1] - a_[1] + 1e-12)
            inside ^= c1 & (Q[:, 0] < xint)
        return np.where(inside, -d, d)
    f_side = np.where(S < 8, sdist(np.stack([S, zz], 1)), 1.0)
    f_slv = np.ones(len(P))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); Ma = arm.matrix_world
    for sd, (eb, wb) in ((1, ("LeftForeArm", "LeftHand")), (-1, ("RightForeArm", "RightHand"))):
        E = np.array(Ma @ arm.data.bones[eb].head_local); W = np.array(Ma @ arm.data.bones[wb].head_local)
        if sd * W[0] < 0: E, W = np.array(Ma @ arm.data.bones[eb.replace("Left", "R_").replace("Right", "Left").replace("R_", "Right")].head_local), np.array(Ma @ arm.data.bones[wb.replace("Left", "R_").replace("Right", "Left").replace("R_", "Right")].head_local)
        u = (E - W) / np.linalg.norm(E - W)
        o = np.cross(u, [0, 1, 0]); o = o / np.linalg.norm(o); o = o if o[0] * sd > 0 else -o          # outward, in the arm's plane
        q = P - W; t = q @ u; rv = q - np.outer(t, u); rn = rv / (np.linalg.norm(rv, axis=1, keepdims=True) + 1e-9)
        lat = rn[:, 1]; outer = rn @ o
        on = (sd * P[:, 0] > 0.20) & (t > -0.10) & (t < np.linalg.norm(E - W) + 0.05) & (np.linalg.norm(rv, axis=1) < 0.09)
        ry = rv[:, 1]                                   # across the sleeve, front-back, metres
        f_tri = np.maximum(t - TL * (1 - np.abs(ry) / SLEEVE_R), -outer * 0.2)
        f = np.minimum(t - CUFF, f_tri)
        f_slv = np.where(on, np.minimum(f_slv, f), f_slv)
    shape = np.minimum(f_side, f_slv)       # (for the area share only)
    log.append("shape: side red %d verts, sleeve red %d verts" % (int((f_side < 0).sum()), int((f_slv < 0).sum())))
    fa = g.data.color_attributes.new("fields", 'FLOAT_COLOR', 'POINT')
    for i in range(len(P)): fa.data[i].color = (float(neck[i] + 0.5), float(side[i]), float(np.clip(f_slv[i], -0.4, 0.4) + 0.5), 1.0)
    fb = g.data.color_attributes.new("surf", 'FLOAT_COLOR', 'POINT')
    for i in range(len(P)): fb.data[i].color = (float(np.clip(S[i], -0.5, 5.0) + 0.5), 0.0, 0.0, 1.0)
    g.data.materials.clear(); g.data.materials.append(build_material(g, "fields"))
    for p in g.data.polygons: p.use_smooth = True
    # red share of the cloth area (face centres)
    g.data.calc_loop_triangles(); ar = []; rd = []
    F = np.stack([neck, side, shape], 1)
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
