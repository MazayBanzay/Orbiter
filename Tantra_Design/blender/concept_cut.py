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
COLLAR_XW = None   # set below: the V's half-width at the collar foot (no step where V and collar meet)
OPEN_Z, OPEN_X = 1.43, 0.045     # the collar's V opening: its point (z), its half-width at the top
EP_H = 0.005                     # m: the epaulette strap stands this much off the shoulder
COLLAR_LOOSE, COLLAR_FLARE, COLLAR_TALL, COLLAR_OPEN = 0.010, 0.006, 1.20, 0.008   # m, m, x, m
EP_Y0, EP_Y1, EP_Z, EP_X, EP_XI, EP_POINT = -0.024, 0.020, 1.28, 0.178, 0.084, 0.014
EP_W = 0.042   # strap width
EPAULETTES = False   # user 2026-10-02: no epaulettes for now
EP_T = 0.004   # strap thickness   # epaulette: front and back edge (y), lowest point of the shoulder top it covers
BOOT_TOP = 0.13   # m: the boot above this is inside the trousers
VZ, VX = 1.445 - (1.445 - 1.077) / ((1 + 5 ** 0.5) / 2) ** 2, 0.085                       # V: its point (z) and the collar corners (x at z 1.45)
# side band in the profile (y forward-negative, z up), clockwise from the shoulder top, front edge first going down:
# (2026-10-02, refined for elegance: one continuous line from the shoulder to below the knee - narrow on the shoulder
# (a seam accent, not a strap), widest at the waist (the waist read by the cut, not by framing), a small notch at the
# hip that keeps the lightning, and a long point at the knee that lengthens the leg. Never on the bust or the seat.)
# (user's pick 2026-10-02: no shoulder part - the line ends in a point under the armpit: a blade with two points)
# (2026-10-02, the golden section: H = 1.743 m; the navel line Z1 = H/phi = 1.077 is the waist and the panel's widest
# point; the panel's lower point Z2 = H/phi^2 = 0.666 (mid-thigh); its upper point under the armpit ZA; the V's point
# VZ divides collar foot -> navel line in phi^2 : 1 (above the bust); the sleeve wedge TL = forearm / phi)
PHI = (1 + 5 ** 0.5) / 2; H_ = 1.743; Z1, Z2, ZA = H_ / PHI, H_ / PHI ** 2, 1.23
SIDE = [(-0.010, ZA),                      # the upper point, under the armpit
        (0.028, Z1),                       # back edge out to the waist (the widest) - never onto the seat
        (0.016, Z1 - 0.07),                # down the hip, turning forward
        (-0.026, Z2),                      # the lower point, mid-thigh
        (-0.045, Z1 - 0.07), (-0.064, Z1)] # front edge up: hip, waist, and back to the upper point
HB = (VZ - Z1) / PHI ** 3; BT, BB = Z1 + HB / PHI ** 2, Z1 - HB / PHI; BDIP, BVW = HB / PHI, 0.12
BKX = 0.145; BK = BKX / (ZA - BT)   # the back V: its lines reach x = BKX at the panels' top
XS, XB, BW = 0.205, 0.075, 0.055                      # torso only (no sleeves) / not past this towards the spine, navel, neck
DMAX, DMIN, SMOOTH_IT = 0.014, 0.004, 8   # m: the cloth at most this far off the body / at least; smoothing passes
TL, CUFF, SLEEVE_R = 0.25 / ((1 + 5 ** 0.5) / 2), 0.25 / ((1 + 5 ** 0.5) / 2) ** 4, 0.045    # sleeve triangle apex from the wrist, cuff band, sleeve half-width
SW = SLEEVE_R / PHI ** 2   # half-width of the arm stripe
ARM_EDGE = 0.012   # m: the arm's red reaches this far below the axis plane at the back (a straight line to the cuff)
YOKE_X, YOKE_Z = 0.25, 1.30   # the yoke line's outer end, on the upper arm (back view)
BAND_DZ = 0.05   # the shoulder band reaches this far below the arm's root (it meets the arm stripe on the shoulder's slope)
COLLAR_XW = VX * (COLLAR_Z - VZ) / (1.45 - VZ)

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
    # less baggy (user 2026-10-03: "мешковатость - пиздец"): the cloth drawn in to at most DMAX off the body; then the
    # asset's crumples relaxed (a few smoothing passes; the hems, cuffs and collar left alone)
    from mathutils.bvhtree import BVHTree as _BVH0
    bmb = bmesh.new(); bmb.from_mesh(body.data); bmb.transform(body.matrix_world)
    gbi = bmb.verts.layers.deform.active; gbody = body.vertex_groups['body'].index
    bmesh.ops.delete(bmb, geom=[v for v in bmb.verts if v[gbi].get(gbody, 0) <= 0], context='VERTS')
    btree = _BVH0.FromBMesh(bmb); bmb.free()
    pulled = 0
    for v in bm.verts:
        if not (0.12 < v.co.z < 1.42) or v.is_boundary: continue
        hit = btree.find_nearest(v.co)
        if hit[0] is None: continue
        d = (v.co - hit[0]).length
        if d > DMAX: v.co = hit[0] + (v.co - hit[0]) * (DMAX / d); pulled += 1
    movable = [v for v in bm.verts if 0.12 < v.co.z < 1.40 and not v.is_boundary and not any(e.is_boundary for e in v.link_edges)]
    for _ in range(SMOOTH_IT):
        nw = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5)) for v in movable]
        for v, co in nw: v.co = co
    for v in movable:   # never into the body
        hit = btree.find_nearest(v.co)
        if hit[0] is not None and (v.co - hit[0]).length < DMIN and (v.co - hit[0]).dot(hit[1]) >= 0: v.co = hit[0] + hit[1] * DMIN
    bm.normal_update(); log.append("de-bagged: %d verts pulled in to %.0f mm, %d smoothing passes" % (pulled, DMAX * 1000, SMOOTH_IT))
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
    # waist band joining the side panels round the body (user 2026-10-02), its proportions golden: height HB = (VZ - Z1)/phi^3,
    # split by the navel line Z1 in phi : 1 (the larger part below); in front its lower edge dips into a V (the concept's V)
    # of depth HB/phi
    for zz in (BT, BB):
        cut(Vector((0, 0, zz)), Vector((0, 0, 1)), lambda c, zz=zz: abs(c.z - zz) < 0.04 and abs(c.x) < 0.28)
    for sx in (1, -1):
        co, no = plane_from_front((sx * BVW, BB), (0.0, BB - BDIP)); cut(co, no, lambda c: c.y < 0 and BB - BDIP - 0.03 < c.z < BB + 0.03 and abs(c.x) < BVW + 0.03)
    for sx in (1, -1):   # the back V: straight lines from under the arms to the band's middle
        co, no = plane_from_front((0.0, BT), (sx * BKX, ZA)); cut(co, no, lambda c: c.y > 0 and BT - 0.02 < c.z < ZA + 0.04 and abs(c.x) < 0.26)
    cut(Vector((0, 0, ZA)), Vector((0, 0, 1)), lambda c: c.y > 0 and abs(c.z - ZA) < 0.04 and abs(c.x) < 0.26)
    # collar foot: a level cut round the neck (the stand only - within reach of the neck, so not across the shoulders)
    cut(Vector((0, 0, COLLAR_Z)), Vector((0, 0, 1)), lambda c: abs(c.z - COLLAR_Z) < 0.03 and abs(c.x) < COLLAR_XW + 0.04)
    # at the back the collar's red ends where the shoulder bands' back edges meet it, so the band runs into the collar
    # without a step (user, five times): the cloth's height at (COLLAR_XW, ncy + SW)
    from mathutils.bvhtree import BVHTree as _BVH2
    _t2 = _BVH2.FromBMesh(bm); _zs = []
    for _x in np.linspace(COLLAR_XW + 0.02, COLLAR_XW + 0.06, 5):     # the shoulder just outside the collar, extrapolated in
        _h = _t2.ray_cast(Vector((_x, ncy + SW, 1.8)), Vector((0, 0, -1)), 1.0)
        if _h[0] is not None and _h[0].z < COLLAR_Z + 0.04: _zs.append((_x, _h[0].z))
    global COLLAR_ZB
    COLLAR_ZB = COLLAR_Z   # (the back yoke below joins collar and shoulders)
    log.append("back collar edge at z %.3f (front %.3f)" % (COLLAR_ZB, COLLAR_Z))
    cut(Vector((0, 0, COLLAR_ZB)), Vector((0, 0, 1)), lambda c: c.y > ncy and abs(c.z - COLLAR_ZB) < 0.04 and abs(c.x) < COLLAR_XW + 0.04)
    for sx in (1, -1): cut(Vector((sx * COLLAR_XW, 0, 0)), Vector((1, 0, 0)), lambda c: c.z > min(COLLAR_Z, COLLAR_ZB) - 0.02 and abs(abs(c.x) - COLLAR_XW) < 0.04)
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
    # the arm stripe (user 2026-10-03): from the cuff wedge up the top of the forearm and the upper arm, over the shoulder to
    # the collar; width 2*SW = 2*SLEEVE_R/phi^2. Edges: planes parallel to each segment's axis.
    segs = []; band_xy = []; yoke = []
    for sd, (sb, eb) in ((1, ("LeftArm", "LeftForeArm")), (-1, ("RightArm", "RightForeArm"))):
        S_ = Ma @ arm.data.bones[sb].head_local; E2 = Ma @ arm.data.bones[eb].head_local
        if sd * S_.x < 0:
            sb, eb = ("RightArm", "RightForeArm") if sb.startswith("Left") else ("LeftArm", "LeftForeArm")
            S_ = Ma @ arm.data.bones[sb].head_local; E2 = Ma @ arm.data.bones[eb].head_local
        u2 = (E2 - S_).normalized(); o2 = u2.cross(Vector((0, 1, 0))).normalized()
        if o2.z < 0: o2 = -o2                                            # the top of the upper arm
        y2 = o2.cross(u2).normalized(); segs.append((sd, S_, u2, o2, y2, (E2 - S_).length))
        onup = lambda c, S_=S_, u2=u2, L2=(E2 - S_).length: -0.04 < (c - S_).dot(u2) < L2 + 0.06 and (c - S_ - u2 * (c - S_).dot(u2)).length < 0.10
        for s_ in (1, -1): cut(S_ + y2 * s_ * SW, y2, onup)
    for sd, S_, u2, o2, y2, L2 in segs:   # the back edge of the arm's red: a plane through the arm's axis
        cut(S_ - o2 * ARM_EDGE, o2, lambda c, S_=S_, u2=u2, L2=L2: -0.04 < (c - S_).dot(u2) < L2 + 0.06 and (c - S_ - u2 * (c - S_).dot(u2)).length < 0.10)
    for sd, W, u, o, yv, L_ in frames:
        cut(W - o * ARM_EDGE, o, lambda c, W=W, u=u, L_=L_: -0.02 < (c - W).dot(u) < L_ + 0.06 and (c - W - u * (c - W).dot(u)).length < 0.10)
        onfa = lambda c, W=W, u=u, L_=L_: TL - 0.03 < (c - W).dot(u) < L_ + 0.06 and (c - W - u * (c - W).dot(u)).length < 0.10
        for s_ in (1, -1): cut(W + yv * s_ * SW, yv, onfa)
    for sd, S_, u2, o2, y2, L2 in segs:      # over the shoulder: from the arm's root to the collar, a band across the ridge
        P0 = Vector((sd * COLLAR_XW, ncy, 0)); P1 = Vector((S_.x, S_.y, 0)); dxy = (P1 - P0).normalized(); nxy = Vector((-dxy.y, dxy.x, 0))
        band_xy.append((sd, P0, P1, dxy, nxy, S_))
        for s_ in (1, -1): cut(P0 + nxy * s_ * SW, nxy, lambda c, sd=sd, S_=S_: sd * c.x > 0 and c.z > S_.z - BAND_DZ - 0.01 and abs(c.x) < abs(S_.x) + 0.06)
        # the back yoke (user 2026-10-03): above a straight line (back view) from the collar's lower corner to the arm's root,
        # behind the shoulder band's front edge - collar and shoulders one red piece
        YK0, YK1 = (sd * COLLAR_XW, COLLAR_Z), (S_.x, S_.z - ARM_EDGE)   # (user 2026-10-03: one straight line from the collar's corner to where the arm's back edge starts)
        yoke.append((sd, YK0, YK1, P0, nxy if nxy.y > 0 else -nxy, S_))
        co, no = plane_from_front(YK0, YK1); cut(co, no, lambda c, sd=sd, S_=S_: sd * c.x > 0 and c.y > ncy - 0.05 and S_.z - 0.08 < c.z < COLLAR_Z + 0.04 and COLLAR_XW - 0.02 < abs(c.x) < abs(S_.x) + 0.02)
        cut(Vector((0, 0, S_.z - BAND_DZ)), Vector((0, 0, 1)), lambda c, sd=sd, S_=S_: sd * c.x > 0 and abs(c.z - (S_.z - BAND_DZ)) < 0.03 and abs(c.x) < abs(S_.x) + 0.06)
    # ---- colour the faces ----
    def is_red(c, nrm):
        # collar: the stand round the neck above its foot
        # collar: only its front, between the V's corners (the V runs up through it); white at the neck's sides and back
        if c.z > (COLLAR_ZB if c.y > ncy else COLLAR_Z) and abs(c.x) < COLLAR_XW: return True   # the collar: everything above its foot plane within x = +-COLLAR_XW (plane cuts: straight)   # the whole collar (user: red at the back too)
        # epaulettes
        # V
        if c.y < ncy and abs(c.x) < (c.z - VZ) * VX / (1.45 - VZ) and (c.z < 1.43 or math.hypot(c.x - ncx, c.y - ncy) < nr + 0.06): return True
        for sd, S_, u2, o2, y2, L2 in segs:   # the stripe along the top of the upper arm
            q = c - S_; t2 = q.dot(u2); rv2 = q - u2 * t2
            yb2 = y2 if y2.y > 0 else -y2
            if sd * c.x > 0 and -0.07 < t2 < L2 + 0.01 and rv2.length < 0.12 and rv2.dot(o2) > -ARM_EDGE and rv2.dot(yb2) > -SW: return True   # top and back of the upper arm
        for (sd, YK0, YK1, P0, nb, S_), (sd2, S2, u2, o2, y2, L2) in zip(yoke, segs):   # the back yoke
            if sd * c.x <= 0 or not (COLLAR_XW - 0.001 < abs(c.x) < abs(S_.x) + 0.04): continue
            zl = YK0[1] + (YK1[1] - YK0[1]) * (abs(c.x) - abs(YK0[0])) / (abs(YK1[0]) - abs(YK0[0]))
            if c.z > zl and (c.y > ncy - 0.005 or (Vector((c.x, c.y, 0)) - P0).dot(nb) > -SW): return True
        for sd, P0, P1, dxy, nxy, S_ in band_xy:   # over the shoulder: a straight band (top view) from the collar's corner to the arm's root
            q = Vector((c.x, c.y, 0)) - P0; t3 = q.dot(dxy)
            if sd * c.x > 0 and c.z > S_.z - BAND_DZ and abs(q.dot(nxy)) < SW and -0.03 < t3 < (P1 - P0).length + 0.04: return True
        # sleeves
        for sd, W, u, o, yv, L_ in frames:
            q = c - W; t = q.dot(u); rv = q - u * t
            if sd * c.x > 0.20 and -0.12 < t < L_ + 0.01 and rv.length < 0.10:
                if t < CUFF: return True
                if rv.dot(o) > 0 and t < TL * (1 - abs(rv.dot(yv)) / SLEEVE_R): return True
                yb = yv if yv.y > 0 else -yv
                if rv.dot(o) > -ARM_EDGE and rv.dot(yb) > -SW: return True   # top and back of the forearm, to the cuff
                return False
        # waist band
        if abs(c.x) < 0.26 and c.z < BT:
            low = BB - (BDIP * max(0.0, 1 - abs(c.x) / BVW) if c.y < 0 else 0.0)
            if c.z > low: return True
        # side band
        if c.z > BT and inner_ok(c) and abs(c.x) < XS and inside_poly((c.y, c.z), poly): return True   # above the band only (user: no blade on the thigh)
        # the back: the panels' inner edges run straight from under the arms down to the band's middle - a V on the back
        if c.y > 0 and BT <= c.z < ZA + 0.02 and abs(c.x) < XS and abs(c.x) > (c.z - BT) * BK: return True   # (torso only: not the sleeves)
        return False
    def inner_ok(c):   # outside the inner line (between it and the outline), segment by segment as cut
        k = min(max(int((c.z - 0.50) / 0.04), 0), len(knots) - 2); z0, z1 = knots[k], knots[k + 1]
        t = (c.z - z0) / (z1 - z0); return abs(c.x) > inner(z0) * (1 - t) + inner(z1) * t
    red_n = 0
    for f in bm.faces:
        r = is_red(f.calc_center_median(), f.normal); f.material_index = 1 if r else 0; red_n += r
    # the asset's belt loops on the waist (user 2026-10-03: "убери"): an outer layer - faces with garment cloth right behind
    from mathutils.bvhtree import BVHTree as _BVH
    rem = 0
    for it in range(5):
        bm.normal_update(); tr_ = _BVH.FromBMesh(bm); kill2 = []
        for f in bm.faces:
            c = f.calc_center_median()
            if not (abs(c.x) < 0.26 and 0.92 < c.z < 1.18): continue
            for sgn in (-1, 1):   # the loop's outer and inner faces both have cloth close behind or in front
                hit = tr_.ray_cast(c + f.normal * sgn * 0.0008, f.normal * sgn, 0.030 if sgn < 0 else 0.004)
                if hit[0] is not None and hit[2] != f.index: kill2.append(f); break
        if not kill2: break
        bmesh.ops.delete(bm, geom=kill2, context='FACES'); rem += len(kill2)
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    # the holes the loops leave in the cloth: filled and relaxed into the surface around
    bnd = [e for e in bm.edges if e.is_boundary and all(abs(v.co.x) < 0.27 and 0.90 < v.co.z < 1.20 for v in e.verts)]
    nf = bmesh.ops.holes_fill(bm, edges=bnd, sides=60)["faces"]
    patch = {v for f in nf for v in f.verts}
    patch |= {e.other_vert(v) for v in list(patch) for e in v.link_edges}
    for _ in range(15):
        nw = {v: sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges) for v in patch if v.link_edges}
        for v, c_ in nw.items(): v.co = v.co.lerp(c_, 0.5)
    bmesh.ops.triangulate(bm, faces=[f for f in nf if f.is_valid and len(f.verts) > 4])
    bmesh.ops.recalc_face_normals(bm, faces=[f for f in bm.faces if all(abs(v.co.x) < 0.28 and 0.9 < v.co.z < 1.2 for v in f.verts)])   # the filled patches faced inwards (dark specks)
    log.append("belt loops removed: %d faces, %d holes filled" % (rem, len(nf)))
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
    NU, NV = 24, 10; made = 0
    for sx in ((1, -1) if EPAULETTES else ()):
        # the ridge of the shoulder: for each x the highest point of the cloth
        xs_r = np.linspace(EP_XI, EP_X, 12); ridge = []
        for xx in xs_r:
            best = None
            for yy in np.linspace(-0.06, 0.06, 61):
                h_ = tree.ray_cast(Vector((sx * xx, yy, 1.8)), Vector((0, 0, -1)), 1.0)
                if h_[0] is not None and (best is None or h_[0].z > best[1]): best = (yy, h_[0].z)
            ridge.append(best)
        yc_ = float(np.median([r[0] for r in ridge])); zr = np.array([r[1] for r in ridge])
        # the strap is a rigid piece: its axis a STRAIGHT line along the shoulder, raised to clear the ridge; across its
        # width a fixed arc (a slice of a cylinder) - every edge of it a straight line or one clean arc
        a_, b_ = np.polyfit(xs_r, zr, 1); b_ += max(0.0, float((zr - (a_ * xs_r + b_)).max())) + 0.001
        drops = []
        for xx in xs_r[2:-2]:
            for dy in (-EP_W / 2, EP_W / 2):
                h_ = tree.ray_cast(Vector((sx * xx, yc_ + dy, 1.8)), Vector((0, 0, -1)), 1.0)
                if h_[0] is not None: drops.append((a_ * xx + b_) - h_[0].z)
        drop = float(np.clip(np.median(drops) if drops else 0.004, 0.002, 0.012)) * 0.8
        R = (EP_W / 2) ** 2 / (2 * drop) + drop / 2
        # no cloth may come through the strap anywhere (the trapezius rises towards the neck at the strap's edges):
        # the axis line is lifted to clear the cloth under the whole underside, not only under the ridge
        need_x, need_z = [], []
        for i in range(NU + 1):
            for j in range(NV + 1):
                t, u = i / NU, j / NV
                xin = EP_XI + EP_POINT * abs(2 * u - 1); x = xin + (EP_X - xin) * t; dy = EP_W * (u - 0.5)
                arc = R - math.sqrt(max(R * R - dy * dy, 0.0))
                h_ = tree.ray_cast(Vector((sx * x, yc_ + dy, 1.8)), Vector((0, 0, -1)), 1.0)
                if h_[0] is not None: need_x.append(x); need_z.append(h_[0].z + arc + 0.0015)
        need_x, need_z = np.array(need_x), np.array(need_z)
        a_, b_ = np.polyfit(need_x, need_z, 1); b_ += max(0.0, float((need_z - (a_ * need_x + b_)).max()))
        top, bot = {}, {}
        for i in range(NU + 1):
            for j in range(NV + 1):
                t, u = i / NU, j / NV
                xin = EP_XI + EP_POINT * abs(2 * u - 1)
                x = xin + (EP_X - xin) * t; dy = EP_W * (u - 0.5)
                zc = a_ * x + b_
                arc = R - math.sqrt(max(R * R - dy * dy, 0.0))         # how far the arc falls at dy
                ang = math.asin(max(-1.0, min(1.0, dy / R)))
                nrm_ = Vector((0, math.sin(ang), math.cos(ang)))        # the arc's normal
                pb = Vector((sx * x, yc_ + dy, zc - arc))
                bot[i, j] = bm.verts.new(pb); top[i, j] = bm.verts.new(pb + nrm_ * EP_T)
                _, idx, _ = kd.find(pb)
                for v_ in (top[i, j], bot[i, j]):
                    for gi, w in orig[idx][dl].items(): v_[dl][gi] = w
        faces = []
        for i in range(NU):
            for j in range(NV):
                q = (top[i, j], top[i + 1, j], top[i + 1, j + 1], top[i, j + 1]); faces.append(bm.faces.new(q if sx > 0 else q[::-1]))
                q = (bot[i, j + 1], bot[i + 1, j + 1], bot[i + 1, j], bot[i, j]); faces.append(bm.faces.new(q if sx > 0 else q[::-1]))
        ring = [(i, 0) for i in range(NU)] + [(NU, j) for j in range(NV)] + [(i, NV) for i in range(NU, 0, -1)] + [(0, j) for j in range(NV, 0, -1)]
        for a2, b2 in zip(ring, ring[1:] + ring[:1]):
            q = (bot[a2], bot[b2], top[b2], top[a2]); faces.append(bm.faces.new(q if sx > 0 else q[::-1]))
        for f in faces: f.material_index = 1; f.smooth = True
        made += len(faces)
        log.append("epaulette %d: axis slope %.2f, arc R %.0f mm" % (sx, a_, R * 1000))
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
    # the toes and the instep of the body came through the boot (more so when the foot bends: in the game it looked
    # like slippers, the user): inside the boot the foot is never seen - remove the body below the boot's top
    for bo in (o for o in bpy.data.objects if o.type == 'MESH' and o.name.split('.')[-1] in ("body", "female1605")):
        bb = bmesh.new(); bb.from_mesh(bo.data); Mb = bo.matrix_world
        gone = [v for v in bb.verts if (Mb @ v.co).z < BOOT_TOP - 0.025]
        bmesh.ops.delete(bb, geom=gone, context='VERTS'); bb.to_mesh(bo.data); bb.free()
        log.append("body %s: %d foot vertices removed (inside the boot)" % (bo.name, len(gone)))
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
