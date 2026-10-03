# The user's crew suit design with EXACT colour edges (2026-10-02, "неровные края" five times): the red is not painted
# into a texture from vertex fields any more - the garment mesh is CUT along every boundary line by planes, and faces
# are coloured whole (two materials, no texture). A plane cut is a straight line in the view it is drawn in.
#   collar  : the stand collar's own faces (above its foot)
#   V       : two planes from the collar corners to the point above the bust (front view), front only
#   sides   : one band per side - from the shoulder top (torso only, |x| < XS: the sleeves untouched) down the side,
#             the step at the waist, the long point down the thigh (profile view, planes containing the x axis);
#             bounded towards the spine and the navel by planes x = +-XB (back/front view, straight vertical lines)
#   sleeves : the cuff and the triangle up the outer forearm (planes in the forearm's frame)
# PREVIEW: writes renders/cc_*.png and suit_view/astronavigator_suit.glb. No game file touched.
import bpy, bmesh, os, sys, math
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
OUT = os.path.join(HERE, "..", "suit_view"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "concept_cut.log"); log = []
A = "elvs_racing_fire_suit_female1"
WHITE, RED = (0.80, 0.80, 0.78), (0.30, 0.012, 0.012)
COLLAR_Z = 1.445
OPEN_Z, OPEN_X = 1.43, 0.045     # the collar's V opening: its point (z), its half-width at the top
EP_H = 0.005                     # m: the epaulette strap stands this much off the shoulder
COLLAR_LOOSE, COLLAR_FLARE, COLLAR_TALL, COLLAR_OPEN = 0.010, 0.006, 1.20, 0.008   # m, m, x, m
EP_Y0, EP_Y1, EP_Z, EP_X, EP_XI, EP_POINT = -0.024, 0.020, 1.28, 0.178, 0.098, 0.014
EP_W = 0.042   # strap width   # epaulette: front and back edge (y), lowest point of the shoulder top it covers
BOOT_TOP = 0.13   # m: the boot above this is inside the trousers
VZ, VX = 1.27, 0.075                       # V: its point (z) and the collar corners (x at z 1.45)
# side band in the profile (y forward-negative, z up), clockwise from the shoulder top, front edge first going down:
# (2026-10-02, refined for elegance: one continuous line from the shoulder to below the knee - narrow on the shoulder
# (a seam accent, not a strap), widest at the waist (the waist read by the cut, not by framing), a small notch at the
# hip that keeps the lightning, and a long point at the knee that lengthens the leg. Never on the bust or the seat.)
# (user's pick 2026-10-02: no shoulder part - the line ends in a point under the armpit: a blade with two points)
SIDE = [(-0.008, 1.23),                    # the upper point, under the armpit
        (0.035, 1.10), (0.035, 1.00),      # back edge down the side to the waist
        (0.055, 0.95),                     # the notch (shallow: from behind the band stays narrow)
        (-0.03, 0.50),                     # the lower point, at the knee
        (-0.02, 0.95), (-0.045, 1.08)]     # front edge up: the waist, then to the upper point
XS, XB, BW = 0.205, 0.075, 0.055                      # torso only (no sleeves) / not past this towards the spine, navel, neck
TL, CUFF, SLEEVE_R = 0.20, 0.035, 0.045    # sleeve triangle apex from the wrist, cuff band, sleeve half-width

def plane_from_profile(p0, p1):   # plane containing the x axis through two (y, z) points
    d = Vector((0, p1[0] - p0[0], p1[1] - p0[1])); n = Vector((1, 0, 0)).cross(d).normalized()
    return Vector((0, p0[0], p0[1])), n

def plane_from_front(p0, p1):     # plane containing the y axis through two (x, z) points
    d = Vector((p1[0] - p0[0], 0, p1[1] - p0[1])); n = Vector((0, 1, 0)).cross(d).normalized()
    return Vector((p0[0], 0, p0[1])), n

def inside_poly(q, poly):
    c = False
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        if (ay > q[1]) != (by > q[1]) and q[0] < ax + (q[1] - ay) * (bx - ax) / (by - ay): c = not c
    return c

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
    import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); Ma = arm.matrix_world
    before = set(bpy.data.objects)
    HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("%s/%s.mhclo" % (A, A), asset_subdir="clothes"), body,
                                 asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
    g = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH'); M = g.matrix_world; Mi = M.inverted()
    # skin under the suit (its own delete group + the arms under the sleeves)
    dg = body.vertex_groups.get("Delete." + A)
    armg = [body.vertex_groups[n].index for n in ("LeftArm", "RightArm", "LeftForeArm", "RightForeArm") if n in body.vertex_groups]
    dg.add([v.index for v in body.data.vertices if sum(x.weight for x in v.groups if x.group in armg) > 0.5], 1.0, 'REPLACE')
    for m_ in list(body.modifiers):
        if m_.type == 'MASK' and m_.name != "Hide helpers": body.modifiers.remove(m_)
    mk = body.modifiers.new("HideC", 'MASK'); mk.vertex_group = dg.name; mk.invert_vertex_group = True
    bm = bmesh.new(); bm.from_mesh(g.data); bm.transform(M)          # work in world space; back to local at the end
    # clean-up as accepted: no ridge between the breasts, no loose badges
    zone = [(v, float(max(0.0, 1 - abs(v.co.x) / 0.05))) for v in bm.verts if abs(v.co.x) < 0.05 and 1.08 < v.co.z < 1.42 and v.co.y < -0.02]
    for _ in range(60):
        nw = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5 * w)) for v, w in zone]
        for v, co in nw: v.co = co
    seen = set(); small = []
    for v in bm.verts:
        if v in seen: continue
        st = [v]; c = []
        while st:
            u = st.pop()
            if u in seen: continue
            seen.add(u); c.append(u); st.extend(e.other_vert(u) for e in u.link_edges)
        if len(c) < 200: small += c
    bmesh.ops.delete(bm, geom=small, context='VERTS')
    # neck (for the collar)
    Mb = body.matrix_world; gb = body.vertex_groups['body'].index
    BN = np.array([Mb @ v.co for v in body.data.vertices if any(x.group == gb for x in v.groups)])
    nk = BN[(BN[:, 2] > 1.44) & (BN[:, 2] < 1.50) & (np.abs(BN[:, 0]) < 0.08)]
    ncx, ncy = nk[:, 0].mean(), nk[:, 1].mean(); nr = np.percentile(np.hypot(nk[:, 0] - ncx, nk[:, 1] - ncy), 90)
    # collar a little looser and bigger (user 2026-10-02: "как дышать то"): the stand pushed out from the neck axis
    # (more towards its top) and made taller; the front slit opened a little; blended in below the collar's foot
    for v in bm.verts:
        p = v.co; w = float(np.clip((p.z - 1.40) / 0.05, 0, 1))
        if w <= 0 or math.hypot(p.x - ncx, p.y - ncy) > nr + 0.06: continue
        w = w * w * (3 - 2 * w); up = max(p.z - 1.44, 0.0)
        dx, dy = p.x - ncx, p.y - ncy; r = math.hypot(dx, dy) + 1e-9
        grow = COLLAR_LOOSE * w + COLLAR_FLARE * up / 0.06
        p.x += dx / r * grow; p.y += dy / r * grow
        p.z += up * (COLLAR_TALL - 1.0)
        if dy < 0 and abs(dx) < 0.03:                                  # the front slit: open it a little
            p.x += np.sign(dx) * COLLAR_OPEN * w * (1 - abs(dx) / 0.03)
    bm.normal_update()
    # ---- the cuts ----
    def cut(co, no, sel):
        geom = [f for f in bm.faces if sel(f.calc_center_median())]
        geom = list({e for f in geom for e in f.edges}) + geom + list({v for f in geom for v in f.verts})
        if geom: bmesh.ops.bisect_plane(bm, geom=geom, plane_co=co, plane_no=no, dist=1e-5)
    near = lambda lo, hi: (lambda c: lo <= c.z <= hi)
    # V: front view lines from the collar corners (+-VX, 1.45) to the point (0, VZ), front only
    for sx in (1, -1):
        co, no = plane_from_front((sx * VX, 1.45), (0.0, VZ)); cut(co, no, lambda c: c.y < 0 and VZ - 0.02 < c.z < 1.60 and abs(c.x) < 0.16)
    # side band: every edge of the profile polygon, on the torso and thighs; and the planes x = +-XS, +-XB
    poly = SIDE
    for p0, p1 in zip(poly, poly[1:] + poly[:1]):
        co, no = plane_from_profile(p0, p1)
        zl, zh = min(p0[1], p1[1]) - 0.03, max(p0[1], p1[1]) + 0.03
        cut(co, no, lambda c, zl=zl, zh=zh: zl < c.z < zh and abs(c.x) < XS + 0.03)
    V_ = np.array([v.co[:] for v in bm.verts])
    zs = np.arange(0.50, 1.50, 0.01); xs = []
    for zc in zs:
        sl = V_[(np.abs(V_[:, 2] - zc) < 0.006) & (np.abs(V_[:, 0]) < (XS if zc > 0.86 else 0.40))]
        xs.append(np.abs(sl[:, 0]).max() if len(sl) else np.nan)
    xs = np.array(xs); ok_ = ~np.isnan(xs); xs = np.interp(zs, zs[ok_], xs[ok_])
    k_ = np.ones(9) / 9; xs = np.convolve(np.r_[[xs[0]] * 4, xs, [xs[-1]] * 4], k_, 'valid')   # smoothed outline
    inner = lambda z: float(np.interp(z, zs, xs)) - BW
    knots = np.arange(0.50, 1.50 + 1e-6, 0.04)
    for sx in (1, -1):
        cut(Vector((sx * XS, 0, 0)), Vector((1, 0, 0)), lambda c: c.z > 1.0 and abs(abs(c.x) - XS) < 0.04)
        for z0, z1 in zip(knots[:-1], knots[1:]):
            co, no = plane_from_front((sx * inner(z0), z0), (sx * inner(z1), z1))
            cut(co, no, lambda c, z0=z0, z1=z1: z0 - 0.01 < c.z < z1 + 0.01 and abs(abs(c.x) - inner(c.z)) < 0.04)
    # the collar's opening: a V (user's sketch) - two planes from the top corners to the point at the collar's foot
    for sx in (1, -1):
        co, no = plane_from_front((sx * OPEN_X, 1.56), (0.0, OPEN_Z))
        cut(co, no, lambda c: c.y < ncy and c.z > OPEN_Z - 0.02 and abs(c.x) < 0.07 and math.hypot(c.x - ncx, c.y - ncy) < nr + 0.08)
    # collar foot: a level cut round the neck (the stand only - within reach of the neck, so not across the shoulders)
    cut(Vector((0, 0, COLLAR_Z)), Vector((0, 0, 1)), lambda c: abs(c.z - COLLAR_Z) < 0.03 and math.hypot(c.x - ncx, c.y - ncy) < nr + 0.05 + COLLAR_LOOSE + COLLAR_FLARE)
    # sleeves: in the forearm's frame - the cuff plane and the two triangle edges
    frames = []
    for sd, (eb, wb) in ((1, ("LeftForeArm", "LeftHand")), (-1, ("RightForeArm", "RightHand"))):
        E = Ma @ arm.data.bones[eb].head_local; W = Ma @ arm.data.bones[wb].head_local
        if sd * W.x < 0:
            eb, wb = ("RightForeArm", "RightHand") if eb.startswith("Left") else ("LeftForeArm", "LeftHand")
            E = Ma @ arm.data.bones[eb].head_local; W = Ma @ arm.data.bones[wb].head_local
        u = (E - W).normalized(); o = u.cross(Vector((0, 1, 0))).normalized()
        if o.x * sd < 0: o = -o
        yv = o.cross(u).normalized()                                   # across the sleeve, front-back
        frames.append((sd, W, u, o, yv, (E - W).length))
        onarm = lambda c, W=W, u=u: (c - W).dot(u) > -0.12 and (c - W).dot(u) < 0.40 and (c - W - u * (c - W).dot(u)).length < 0.10
        cut(W + u * CUFF, u, onarm)
        for s_ in (1, -1):
            n_ = (u + yv * (s_ * TL / SLEEVE_R)).normalized(); cut(W + u * TL, n_, onarm)
    # ---- colour the faces ----
    def is_red(c, nrm):
        # collar: the stand round the neck above its foot
        # collar: only its front, between the V's corners (the V runs up through it); white at the neck's sides and back
        # (the collar's front is the V running up through it: see below)
        # epaulettes
        # V
        if c.y < ncy and abs(c.x) < (c.z - VZ) * VX / (1.45 - VZ) and (c.z < 1.43 or math.hypot(c.x - ncx, c.y - ncy) < nr + 0.06): return True
        # sleeves
        for sd, W, u, o, yv, L_ in frames:
            q = c - W; t = q.dot(u); rv = q - u * t
            if sd * c.x > 0.20 and -0.12 < t < L_ + 0.05 and rv.length < 0.10:
                if t < CUFF: return True
                if rv.dot(o) > 0 and t < TL * (1 - abs(rv.dot(yv)) / SLEEVE_R): return True
                return False
        # side band
        if inner_ok(c) and abs(c.x) < XS and inside_poly((c.y, c.z), poly): return True
        return False
    def inner_ok(c):   # outside the inner line (between it and the outline), segment by segment as cut
        k = min(max(int((c.z - 0.50) / 0.04), 0), len(knots) - 2); z0, z1 = knots[k], knots[k + 1]
        t = (c.z - z0) / (z1 - z0); return abs(c.x) > inner(z0) * (1 - t) + inner(z1) * t
    red_n = 0
    for f in bm.faces:
        r = is_red(f.calc_center_median(), f.normal); f.material_index = 1 if r else 0; red_n += r
    # the collar's V opening: its faces go
    kill = []
    for f in bm.faces:
        c = f.calc_center_median()
        if c.y < ncy and c.z > OPEN_Z and abs(c.x) < (c.z - OPEN_Z) * OPEN_X / (1.56 - OPEN_Z) and math.hypot(c.x - ncx, c.y - ncy) < nr + 0.08:
            kill.append(f)
    bmesh.ops.delete(bm, geom=kill, context='FACES'); bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    log.append("collar opening: %d faces" % len(kill))
    # epaulettes as straps of cloth (user: "объёмная фактура, часть костюма"): a separate strap on each shoulder - its outline
    # from above a trapezoid (the inner end slanted along the collar), laid on the shoulder by casting down onto the suit,
    # standing EP_H off it, with side walls down into the cloth; the skin weights of the suit's nearest vertex
    from mathutils.bvhtree import BVHTree
    from mathutils.kdtree import KDTree
    bm.verts.ensure_lookup_table(); tree = BVHTree.FromBMesh(bm)
    kd = KDTree(len(bm.verts))
    for v in bm.verts: kd.insert(v.co, v.index)
    kd.balance(); dl = bm.verts.layers.deform.active; orig = list(bm.verts)
    NU, NV = 28, 10; made = 0
    for sx in (1, -1):
        ys_r = []
        for xx in np.linspace(EP_XI, EP_X, 6):
            best = None
            for yy in np.linspace(-0.06, 0.06, 49):
                h_ = tree.ray_cast(Vector((sx * xx, yy, 1.8)), Vector((0, 0, -1)), 1.0)
                if h_[0] is not None and (best is None or h_[0].z > best[1]): best = (yy, h_[0].z)
            ys_r.append(best[0])
        yc_ = float(np.median(ys_r)); log.append("epaulette %d: ridge at y %.3f" % (sx, yc_))
        grid = []
        for i in range(NU + 1):
            row = []
            for j in range(NV + 1):
                t, u = i / NU, j / NV
                y = yc_ + EP_W * (u - 0.5)
                xin = EP_XI + EP_POINT * abs(2 * u - 1)            # the inner end: a blunt point, as on a shoulder strap
                x = sx * (xin + (EP_X - xin) * t)
                hit = tree.ray_cast(Vector((x, y, 1.8)), Vector((0, 0, -1)), 1.0)
                if hit[0] is None: row.append(None); continue
                row.append((hit[0], hit[1].normalized() if hit[1].z > 0 else Vector((0, 0, 1))))
            grid.append(row)
        if any(p_ is None for r in grid for p_ in r): log.append("epaulette %d: missed the shoulder" % sx); continue
        # a stiff strap: a flat plate lying on the shoulder (a plane fitted to the shoulder, raised to clear it), not cloth
        # following every curve; its walls go down to the suit, so no gap shows
        # stiff along its length (each lengthwise line straight, raised to clear the shoulder), bending across its width
        # with the shoulder's ridge - as a real strap lies
        Z_ = np.array([[grid[i][j][0].z for j in range(NV + 1)] for i in range(NU + 1)])
        for _ in range(4):   # smooth the shoulder under the strap (a strap bridges small dips), never below the cloth
            Zs = Z_.copy(); Zs[1:-1, :] = (Z_[:-2, :] + 2 * Z_[1:-1, :] + Z_[2:, :]) / 4
            Z_ = np.maximum(Zs, np.array([[grid[i][j][0].z for j in range(NV + 1)] for i in range(NU + 1)]))
        # clear the cloth between the samples too: the max of the cloth over a 3x3 neighbourhood
        Zp = np.pad(Z_, 1, mode='edge'); Z_ = np.max([Zp[1 + di:NU + 2 + di, 1 + dj:NV + 2 + dj] for di in (-1, 0, 1) for dj in (-1, 0, 1)], axis=0)
        zt = {(i, j): float(Z_[i, j]) for i in range(NU + 1) for j in range(NV + 1)}
        top, bot = {}, {}
        for i in range(NU + 1):
            for j in range(NV + 1):
                p_, n_ = grid[i][j]
                top[i, j] = bm.verts.new(Vector((p_.x, p_.y, zt[i, j] + EP_H))); bot[i, j] = bm.verts.new(p_ - Vector((0, 0, 0.002)))
                _, idx, _ = kd.find(p_)
                for v_ in (top[i, j], bot[i, j]):
                    for gi, w in orig[idx][dl].items(): v_[dl][gi] = w
        faces = []
        for i in range(NU):
            for j in range(NV):
                faces.append(bm.faces.new((top[i, j], top[i + 1, j], top[i + 1, j + 1], top[i, j + 1])))
        ring = [(i, 0) for i in range(NU)] + [(NU, j) for j in range(NV)] + [(i, NV) for i in range(NU, 0, -1)] + [(0, j) for j in range(NV, 0, -1)]
        for a_, b_ in zip(ring, ring[1:] + ring[:1]):
            faces.append(bm.faces.new((bot[a_], bot[b_], top[b_], top[a_])))
        for f in faces:
            f.material_index = 1; f.smooth = False
            if f.normal.z < -0.5 and sx: pass
        made += len(faces)
    bm.normal_update()
    # outward-facing walls (the ring order differs per side)
    log.append("epaulette straps: %d faces, %.0f mm high" % (made, EP_H * 1000))
    area = np.array([f.calc_area() for f in bm.faces]); redm = np.array([f.material_index == 1 for f in bm.faces])
    log.append("faces %d, red %d, red share %.1f %% of the cloth" % (len(bm.faces), red_n, 100 * area[redm].sum() / area.sum()))
    def mat(name, rgb):
        m = bpy.data.materials.new(name); m.use_nodes = True; b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*rgb, 1); b.inputs["Roughness"].default_value = 0.8; return m
    g.data.materials.clear(); g.data.materials.append(mat("SuitWhite", WHITE)); g.data.materials.append(mat("SuitRed", RED))
    for sl in g.material_slots: sl.link = 'DATA'          # the asset links its slots to the object: they hid the mesh's
    bm.transform(Mi); bm.to_mesh(g.data); bm.free()   # (the slots first: to_mesh clamps the face material indices to the slots there are)
    for p in g.data.polygons: p.use_smooth = True
    # the boot shaft poked through the trouser leg (a dark spot): under the trousers it is never seen - cut it off
    shoes = next((o for o in bpy.data.objects if o.type == 'MESH' and "shoes" in o.name), None)
    if shoes:
        bs = bmesh.new(); bs.from_mesh(shoes.data); Ms = shoes.matrix_world
        bmesh.ops.delete(bs, geom=[v for v in bs.verts if (Ms @ v.co).z > BOOT_TOP], context='VERTS'); bs.to_mesh(shoes.data); bs.free()
    g.name = "Coverall"; g.data.name = "Coverall"     # export_skin.py labels the garment's groups Coverall_<material>
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"), copy=True)
    log.append("game blend saved: astronavigator_crewsuit.blend")
    log.append("mats %s, slots %s, idx1 %d, mods %s, objs %s" % ([m.name for m in g.data.materials], [(sl.link, sl.material.name if sl.material else None) for sl in g.material_slots],
               sum(1 for p in g.data.polygons if p.material_index == 1), [m.type for m in g.modifiers], [o.name for o in bpy.data.objects if o.type == 'MESH']))
    # previews
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.view_settings.exposure = -0.6; sc.render.resolution_x = sc.render.resolution_y = 800
    for nm, loc, tgt in (("cc_front", (0, -2.6, 1.0), (0, 0, 0.95)), ("cc_side", (2.6, 0.0, 1.0), (0, 0, 0.95)),
                         ("cc_back", (0, 2.6, 1.0), (0, 0, 0.95)), ("cc_back34", (1.5, 1.6, 1.0), (0, 0, 0.95)),
                         ("cc_front34", (1.6, -1.6, 1.0), (0, 0, 0.95)), ("cc_top", (0.0, -0.35, 2.3), (0, 0, 1.35)), ("cc_topback", (0.0, 0.55, 1.85), (0, 0, 1.38)), ("cc_collar", (0.0, -0.55, 1.47), (0, -0.03, 1.40)), ("cc_collar34", (0.38, -0.40, 1.50), (0, -0.02, 1.42))):
        cam.location = loc; cam.data.lens = 40 if nm != "cc_cu" else 40; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(HERE, "renders", nm + ".png"); bpy.ops.render.render(write_still=True)
    # glb: plain materials for the rest, static meshes
    TEX = os.path.join(HERE, "textures"); MP = os.path.join(HERE, "..", "mpfbu", "data")
    def simple_mat(name, img_path, alpha=False, rough=0.6):
        m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
        for n in list(nt.nodes): nt.nodes.remove(n)
        out = nt.nodes.new("ShaderNodeOutputMaterial"); b = nt.nodes.new("ShaderNodeBsdfPrincipled"); nt.links.new(b.outputs[0], out.inputs[0])
        b.inputs["Roughness"].default_value = rough
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = bpy.data.images.load(img_path, check_existing=True); nt.links.new(t.outputs[0], b.inputs["Base Color"])
        if alpha: nt.links.new(t.outputs[1], b.inputs["Alpha"])
        return m
    for o in bpy.data.objects:
        if o.type != 'MESH' or o == g or o.hide_render: continue
        n = o.name
        if n.endswith(".body"): mm = simple_mat("Skin", os.path.join(TEX, "skin_clean.png"), rough=0.55)
        elif "bob" in n: mm = simple_mat("Hair", os.path.join(TEX, "hair_copper.png"), alpha=True, rough=0.5)
        elif "eyebrow" in n: mm = simple_mat("Brows", os.path.join(MP, "eyebrows", "eyebrow001", "eyebrow001.png"), alpha=True)
        elif "eyelash" in n: mm = simple_mat("Lashes", os.path.join(MP, "eyelashes", "eyelashes01", "eyelashes01.png"), alpha=True)
        elif "low-poly" in n: mm = simple_mat("Eyes", os.path.join(MP, "eyes", "materials", "brown_eye.png"), rough=0.2)
        elif "shoes" in n: mm = simple_mat("Boots", os.path.join(MP, "clothes", "shoes03", "shoes03_diffuse.png"), rough=0.5)
        else: continue
        o.data.materials.clear(); o.data.materials.append(mm)
    dg_ = bpy.context.evaluated_depsgraph_get(); keepo = []
    for o in list(bpy.data.objects):
        if o.type != 'MESH' or o.hide_render or not o.data.materials: continue
        me = bpy.data.meshes.new_from_object(o.evaluated_get(dg_)); no = bpy.data.objects.new("X_" + o.name.split(".")[-1], me)
        no.matrix_world = o.matrix_world; sc.collection.objects.link(no); keepo.append(no)
    for o in bpy.context.view_layer.objects: o.select_set(o in keepo)
    path = os.path.join(OUT, "astronavigator_suit.glb")
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_format='GLB', export_image_format='JPEG', export_jpeg_quality=88, export_apply=True, export_yup=True)
    log.append("exported %.1f MB" % (os.path.getsize(path) / 1e6))
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
