# Tantra: ship coverall for the astronavigator (UACS BodyMesh variant).
# The garment is derived from the body proxy (same skin weights -> deforms with the rig), offset outwards,
# trimmed at neck/wrists/ankles, with a stand collar and cuffs as geometry. Panel lines, zip, side panels
# and red piping are painted into a texture in the body's UV space by evaluating a pattern on the 3D surface.
import bpy, bmesh, os, math, traceback
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(__file__)
TEX_DIR = os.path.join(HERE, "textures"); os.makedirs(TEX_DIR, exist_ok=True)
OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "build_coverall.log"); log = []

# ---- palette (linear-ish sRGB values, 0..1) ----
# user 2026-10-02: the grey top replaced by another neutral - a deep muted navy (sits with the copper hair, reads as
# a crew garment, not as a sketch); the red kept as a thin, clean, deeper piping
import os as _os
PALETTES = {"navy": (0.17, 0.21, 0.30), "taupe": (0.36, 0.32, 0.28), "graphite_green": (0.22, 0.26, 0.24)}
FABRIC = np.array([0.88, 0.87, 0.84]); PANEL = np.array(PALETTES[_os.environ.get("COV_PALETTE", "navy")]); DARK = PANEL * 0.62
SEAM = np.array([0.66, 0.65, 0.63]); RED = np.array([0.58, 0.09, 0.08]); ZIP = np.array([0.15, 0.16, 0.18])
OFFSET = 0.008          # m, cloth above skin
BRIDGE_HALF_W = 0.07    # m, half-width of the strip where the fabric spans the buttock cleft / crotch
BRIDGE_SAG = 0.004      # m, how far the fabric may dip between the buttocks
CLEFT_KEEP = 0.25       # share of the body's cleft depth the fabric still shows (a soft groove, not a fold)
CLEFT_W = 0.011         # m, its half-width (gaussian sigma): gentle slopes, never read as a side panel
# ---- the person's body, beyond build_human.py (TRIAL, applied here only; the same values belong to the suit build too,
# so the person has one body in every garment - for the Architect to take over once accepted). The rig is not refitted:
# both changes stay within ~1.5 cm and move no joint, so every clip stays valid.
BODY_TARGETS = [("arms/l-upperarm-shoulder-muscle-decr", 1.0), ("arms/r-upperarm-shoulder-muscle-decr", 1.0),   # MakeHuman targets: a flatter deltoid
                # user 2026-10-02: hips and legs a little slimmer, finer (build_human's hip widening taken back,
                # thighs and calves narrower; circumference only - no joint moves)
                ("hip/hip-scale-horiz-incr", -0.12), ("buttocks/buttocks-volume-incr", -0.05),
                ("legs/measure-thigh-circ-decr", 0.35), ("legs/measure-calf-circ-decr", 0.15),
                ("breast/breast-point-decr", 0.6)]   # a rounder breast, no pointed tip (user via the Architect, 2026-10-02)
# user 2026-10-02: a smaller bust. build_human has cupsize 0.72; the macro breast targets' share is moved to CUP
# (MakeHuman's own min/average/max weighting), with the other macros (young, muscle 0.60, weight 0.48, firmness 0.78) as built
CUP_BUILT, CUP = 0.72, 0.42
MACRO = {"muscle": 0.60, "weight": 0.48, "firmness": 0.78}
SLOPE_DROP = 0.010      # m, the shoulder cap lowered over the acromion: the trapezius falls ~20-25 deg (MakeHuman has no target for it)
MPFB_TARGETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bx", "mpfb", "data", "targets")
TEX = 2048

def smooth01(e0, e1, x): t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def body_extras(body, arm):
    """The person's shape beyond build_human.py, on the body itself (skin and everything cut from it follow)."""
    import gzip
    me = body.data; k = 0.1 / body.matrix_world.to_scale().x   # MakeHuman targets are in decimetres, y up, z forward
    for t, w in BODY_TARGETS:
        n = 0
        for l in gzip.open(os.path.join(MPFB_TARGETS, t + ".target.gz"), "rt"):
            f = l.split()
            if len(f) < 4 or l.startswith("#"): continue
            i = int(f[0])
            if i < len(me.vertices):
                dx, dy, dz = (float(x) * w * k for x in f[1:4]); me.vertices[i].co += Vector((dx, -dz, dy)); n += 1
        log.append("body target %s x%.2f: %d verts" % (t, w, n))
    def mix3(v, names):   # MakeHuman macro weighting: min/average/max around 0.5
        return {names[0]: max(0.0, 1 - v / 0.5), names[1]: 1 - abs(v - 0.5) / 0.5, names[2]: max(0.0, (v - 0.5) / 0.5)}
    wm = mix3(MACRO["muscle"], ("minmuscle", "averagemuscle", "maxmuscle")); ww = mix3(MACRO["weight"], ("minweight", "averageweight", "maxweight"))
    wf = mix3(MACRO["firmness"], ("minfirmness", "averagefirmness", "maxfirmness"))
    c0 = mix3(CUP_BUILT, ("mincup", "averagecup", "maxcup")); c1 = mix3(CUP, ("mincup", "averagecup", "maxcup"))
    acc = {}; nt = 0
    for m_, a in wm.items():
        for w_, b in ww.items():
            for c_ in c0:
                for f_, d in wf.items():
                    wgt = a * b * (c1[c_] - c0[c_]) * d
                    fn = os.path.join(MPFB_TARGETS, "breast", "female-young-%s-%s-%s-%s.target.gz" % (m_, w_, c_, f_))
                    if abs(wgt) < 1e-6 or not os.path.exists(fn): continue
                    nt += 1
                    for l in gzip.open(fn, "rt"):
                        f = l.split()
                        if len(f) < 4 or l.startswith("#"): continue
                        i = int(f[0]); dx, dy, dz = (float(x) * wgt * k for x in f[1:4])
                        o = acc.get(i, (0, 0, 0)); acc[i] = (o[0] + dx, o[1] - dz, o[2] + dy)
    for i, d in acc.items():
        if i < len(me.vertices): me.vertices[i].co += Vector(d)
    log.append("bust: cup %.2f -> %.2f from %d macro targets, %d verts" % (CUP_BUILT, CUP, nt, len(acc)))
    # shoulder line: the base mesh runs almost level from the neck to the shoulder edge (~11 deg) - a square, padded look.
    # Lower the shoulder cap above the joint, most over the acromion, nothing at the neck or down the arm.
    M = body.matrix_world; Mi = M.inverted()
    jz = (arm.matrix_world @ arm.data.bones['LeftArm' if 'LeftArm' in arm.data.bones else 'upperarm_l'].head_local).z
    n = 0
    for v in me.vertices:
        p = M @ v.co; ax = abs(p.x)
        d = SLOPE_DROP * float(smooth01(0.09, 0.17, ax) * smooth01(0.27, 0.21, ax) * smooth01(jz - 0.06, jz + 0.02, p.z))
        if d > 0: v.co = Mi @ (p - Vector((0, 0, d))); n += 1
    me.update(); log.append("shoulder slope: %d verts, up to %.0f mm" % (n, SLOPE_DROP * 1000))

# ---- the cut, after the user's sketch (2026-10-02, "фигура и костюм на эскизе"): one light fabric; a stand collar with a
# V at the zip; shoulder caps with a step at the collar side; princess seams curving from the armhole over the side of the
# bust to the waist; a wide waist band; the leg seams running from the band down the front of each leg; oval knee pads;
# cuffs. No side panels and no red piping (the sketch has none).
def regions(P, N):
    x, y, z = P[:, 0], P[:, 1], P[:, 2]; ax = np.abs(x); nx, ny, nz = N[:, 0], N[:, 1], N[:, 2]
    front = ny < -0.15; back = ny > 0.15
    R = {}
    # shoulder cap: the top of the shoulder from the collar out over the deltoid; its front edge steps back near the collar
    step = np.where(ax < 0.125, -0.030, -0.065)
    R["cap"] = (z > 1.28) & (nz > 0.22) & (ax > 0.075) & (ax < 0.235) & (y > step) & (y < 0.075)
    R["band"] = (np.abs(z - 1.03) < 0.026) & (ax < 0.24) & (np.abs(nz) < 0.6)
    # princess seams: from the armhole (x 0.145 at z 1.34) over the outer side of the bust to the waist (x 0.085)
    t = np.clip((z - 1.06) / 0.28, 0, 1); sx = 0.085 + 0.060 * t ** 1.6
    R["princess"] = np.exp(-((ax - sx) / 0.0022) ** 2) * ((z > 1.056) & (z < 1.34) & (front | back))
    # leg seams: from the band at x 0.15 curving to the front of the leg (x 0.088 at the hip crease), then straight down
    u = np.clip((z - 0.80) / 0.204, 0, 1); lx = 0.088 + 0.062 * u ** 2
    R["legseam"] = np.exp(-((ax - lx) / 0.0022) ** 2) * ((z > 0.10) & (z < 1.004) & (ny < -0.25) & ~(np.abs(z - 0.50) < 0.075))
    # knee pad: an oval on the front of the knee
    R["knee"] = (((z - 0.50) / 0.068) ** 2 + (nx / 0.50) ** 2 < 1) & (ny < -0.55)
    R["kneerim"] = np.exp(-((np.sqrt(((z - 0.50) / 0.068) ** 2 + (nx / 0.50) ** 2) - 1) / 0.05) ** 2) * (ny < -0.45)
    R["zip"] = (ax < 0.0045) & front & (y < 0) & (z > 0.78) & (z < 1.50)
    return R

def pattern(P, N):
    """P, N: (n,3) surface points and normals in rest pose (Blender: x lateral, -y front, z up). Returns (n,3) colour."""
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    R = regions(P, N); col = np.tile(FABRIC, (len(P), 1))
    for k in ("cap", "band", "knee"): col[R[k]] = PANEL
    sew = np.clip(R["princess"] + R["legseam"], 0, 1)[:, None]; col = col * (1 - 0.22 * sew)   # seams: a shade, the relief does the rest
    col[R["zip"]] = ZIP; teeth = R["zip"] & ((np.floor(z / 0.004) % 2) == 0) & (np.abs(x) < 0.0025); col[teeth] = ZIP * 1.8
    mott = np.sin(x * 61 + 1.3) * np.sin(y * 47 + 0.4) * np.sin(z * 53 + 2.1) + 0.5 * np.sin(x * 23 - z * 31)
    col = col * (1 + 0.012 * mott)[:, None]
    return np.clip(col, 0, 1)

def relief(P, N):
    """height of the cloth surface in metres (+ up from the cloth); same regions as pattern(), as (n,3)."""
    z = P[:, 2]; R = regions(P, N); h = np.zeros(len(P))
    h += R["cap"] * 0.0006; h += R["band"] * 0.0012; h += R["knee"] * 0.0016 - R["kneerim"] * 0.0005
    h -= 0.0008 * (R["princess"] + R["legseam"])
    h += R["zip"] * (0.0006 + 0.0006 * (np.floor(z / 0.004) % 2))
    return np.stack([h, h, h], 1)


def smooth_normals(me, iters=18):
    """The pattern follows the garment's cut, not every bump of the body: panels are placed by normals diffused over
    ~5 cm, so the bust, the shoulder blades and the buttocks no longer throw islands of panel colour and break the piping."""
    E = np.array([e.vertices[:] for e in me.edges]); N = np.array([v.normal for v in me.vertices])
    deg = np.bincount(E.ravel(), minlength=len(N))[:, None].astype(float) + 1e-9
    for _ in range(iters):
        acc = np.zeros_like(N); np.add.at(acc, E[:, 0], N[E[:, 1]]); np.add.at(acc, E[:, 1], N[E[:, 0]])
        N = 0.5 * N + 0.5 * acc / deg; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    return N

SS = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75))   # 2x2 samples per texel: clean, antialiased edges

def paint_uv(obj, mat_index=0, fn=None, normals=None):
    """Rasterize the pattern into the object's UV space."""
    me = obj.data; me.calc_loop_triangles()
    uv = me.uv_layers.active.data
    acc = np.zeros((TEX, TEX, 3), np.float32); cnt = np.zeros((TEX, TEX), np.float32)
    V = np.array([v.co for v in me.vertices]); VN = normals if normals is not None else np.array([v.normal for v in me.vertices])
    for tri in me.loop_triangles:
        if tri.material_index != mat_index: continue
        uvs = np.array([uv[l].uv for l in tri.loops]) * (TEX - 1)
        p = V[list(tri.vertices)]; n = VN[list(tri.vertices)]
        x0, y0 = np.floor(uvs.min(0)).astype(int) - 1; x1, y1 = np.ceil(uvs.max(0)).astype(int) + 1
        x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, TEX - 1), min(y1, TEX - 1)
        if x1 < x0 or y1 < y0: continue
        a, b, c = uvs
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-12: continue
        for ox, oy in SS:
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + ox, np.arange(y0, y1 + 1) + oy)
            w0 = ((b[1] - c[1]) * (gx - c[0]) + (c[0] - b[0]) * (gy - c[1])) / den
            w1 = ((c[1] - a[1]) * (gx - c[0]) + (a[0] - c[0]) * (gy - c[1])) / den
            w2 = 1 - w0 - w1
            m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)      # slight overdraw closes seams between islands
            if not m.any(): continue
            W = np.stack([w0[m], w1[m], w2[m]], 1)
            P = W @ p; Nn = W @ n; Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-9
            c_ = (fn or pattern)(P, Nn)
            iy, ix = np.floor(gy[m]).astype(int), np.floor(gx[m]).astype(int)
            acc[iy, ix] += c_; cnt[iy, ix] += 1
    filled = cnt > 0; img = np.zeros((TEX, TEX, 3)); img[filled] = acc[filled] / cnt[filled][:, None]
    # dilate islands a few pixels to avoid dark seams in mip levels
    for _ in range(4):
        grow = ~filled & (np.roll(filled, 1, 0) | np.roll(filled, -1, 0) | np.roll(filled, 1, 1) | np.roll(filled, -1, 1))
        src = np.where(np.roll(filled, 1, 0)[..., None], np.roll(img, 1, 0), np.where(np.roll(filled, -1, 0)[..., None], np.roll(img, -1, 0),
              np.where(np.roll(filled, 1, 1)[..., None], np.roll(img, 1, 1), np.roll(img, -1, 1))))
        img[grow] = src[grow]; filled |= grow
    return img

def make_image(name, arr):
    im = bpy.data.images.new(name, TEX, TEX, alpha=False)
    rgba = np.concatenate([arr, np.ones((TEX, TEX, 1))], 2).astype(np.float32)
    im.pixels.foreach_set(rgba.ravel()); im.filepath_raw = os.path.join(TEX_DIR, name + ".png"); im.file_format = 'PNG'; im.save()
    return im

def material(name, color=None, image=None, rough=0.75):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial"); bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs[0]); bsdf.inputs["Roughness"].default_value = rough
    if image:
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = image; nt.links.new(t.outputs[0], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = (*color, 1)
    return m

def dominant_group(v, names):
    best, bw = None, 0
    for g in v.groups:
        if g.weight > bw: best, bw = names[g.group], g.weight
    return best

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_base.blend"))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    # the full MakeHuman base mesh (19k verts with helpers) instead of the 1605-vertex proxy: a smooth face and
    # hands, and the base mesh's own skin weights, which deform far better at the shoulders. The figure's shape
    # (phenotype and the SHAPE targets) lives in its shape keys: bake it into the mesh first.
    proxy = next(o for o in bpy.data.objects if o.type == 'MESH' and 'female1605' in o.name)
    proxy.hide_render = True; proxy.hide_viewport = True
    body = next(o for o in bpy.data.objects if o.type == 'MESH' and o.name.endswith('.body'))
    for o in bpy.context.view_layer.objects: o.select_set(False)
    bpy.context.view_layer.objects.active = body; body.select_set(True)
    if body.data.shape_keys: bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
    for m in list(body.modifiers):
        if m.type == 'MASK' and m.name == "Hide base mesh": body.modifiers.remove(m)   # keep "Hide helpers"
    body_extras(body, next(o for o in bpy.data.objects if o.type == 'ARMATURE'))
    names = {g.index: g.name for g in body.vertex_groups}
    BONES = set(arm.data.bones.keys()) if (arm := next(o for o in bpy.data.objects if o.type == 'ARMATURE')) else set()
    def dom_bone(groups):   # strongest bone weight (the base mesh also has non-bone groups: body, helpers, joints)
        best, bw = None, 0
        for g in groups:
            n = names.get(g.group if hasattr(g, 'group') else g[0]); w = g.weight if hasattr(g, 'weight') else g[1]
            if n in BONES and w > bw: best, bw = n, w
        return best
    # bone names for either rig (MPFB game_engine or cmu_mb)
    BN = arm.data.bones
    RIG = {'neck': 'neck_01', 'hand_l': 'hand_l', 'hand_r': 'hand_r', 'lowerarm_l': 'lowerarm_l', 'lowerarm_r': 'lowerarm_r', 'foot_l': 'foot_l'} if 'neck_01' in BN else           {'neck': 'Neck', 'hand_l': 'LeftHand', 'hand_r': 'RightHand', 'lowerarm_l': 'LeftForeArm', 'lowerarm_r': 'RightForeArm', 'foot_l': 'LeftFoot'}
    neck_z = (arm.matrix_world @ BN[RIG['neck']].head_local).z

    cov = body.copy(); cov.data = body.data.copy(); cov.name = "Coverall"; cov.data.name = "Coverall"
    body.users_collection[0].objects.link(cov)
    for m in list(cov.modifiers):
        if m.type != 'ARMATURE': cov.modifiers.remove(m)
    cov.data.materials.clear()
    # drop the helper geometry (teeth, tongue, joint cubes, tights...): only the 'body' group is the skin surface
    bmh = bmesh.new(); bmh.from_mesh(cov.data); dl = bmh.verts.layers.deform.active
    gbody = cov.vertex_groups['body'].index if 'body' in cov.vertex_groups else -1
    if gbody >= 0:
        bmesh.ops.delete(bmh, geom=[v for v in bmh.verts if v[dl].get(gbody, 0) <= 0], context='VERTS')
        bmh.to_mesh(cov.data)
    bmh.free()
    # one level of Catmull-Clark: the body proxy is low-poly (1605 verts) and the garment showed it as square
    # shoulders; weights and UVs are interpolated by the modifier. The skin underneath is masked, so the slight
    # shrink of the smooth surface cannot let it poke through.
    arm_mods = [(m.name, m.object) for m in cov.modifiers if m.type == 'ARMATURE']
    for m in list(cov.modifiers): cov.modifiers.remove(m)
    if cov.data.shape_keys: cov.shape_key_clear()
    for o in bpy.context.view_layer.objects: o.select_set(False)
    bpy.context.view_layer.objects.active = cov; cov.select_set(True)
    if len(cov.data.vertices) < 4000:   # the base mesh is dense enough already; only the old proxy needed this
        sub = cov.modifiers.new("Smooth", 'SUBSURF'); sub.levels = 1; sub.render_levels = 1
        bpy.ops.object.modifier_apply(modifier=sub.name)
    for n_, ob in arm_mods: cov.modifiers.new(n_, 'ARMATURE').object = ob
    log.append("coverall subdivided: %d verts" % len(cov.data.vertices))

    HEAD = {'head', 'Head', 'Neck1', 'lips', 'ears'}; HANDS = lambda n: n and (n.startswith('hand_') or any(n.startswith(f) for f in ('index', 'middle', 'pinky', 'ring', 'thumb')))
    FEET = lambda n: n and (n.startswith('foot_') or n.startswith('ball_'))
    bm = bmesh.new(); bm.from_mesh(cov.data); bm.verts.ensure_lookup_table()
    deform = bm.verts.layers.deform.active
    me_src = cov.data
    W = lambda b: arm.matrix_world @ arm.data.bones[b].head_local
    wrist = {s: (W(RIG['hand_' + s]), (W(RIG['hand_' + s]) - W(RIG['lowerarm_' + s])).normalized()) for s in ('l', 'r')}
    ankle_z = W(RIG['foot_l']).z
    # neckline: an elliptic cylinder around the neck axis (neck base -> head), not a horizontal plane.
    # A plane cuts the trapezius far out towards the shoulders and leaves a wide ragged ring ("wings").
    a0 = W('Neck' if 'Neck' in BN else 'neck_01'); a1 = W('Head' if 'Head' in BN else 'head')
    # the hole is a VERTICAL cylinder: the neck leans forward, and a cylinder along it would reach down into the upper back
    nneck = (a1 - a0).normalized()
    nu = Vector((0, 0, 1)); ex = Vector((1, 0, 0)); ey = nu.cross(ex)
    def ncoord(p): d = Vector(p) - a0; return d.dot(nu), d.dot(ex), d.dot(ey)
    nk = []
    for v in body.data.vertices:
        if not v.groups: continue
        if dom_bone(v.groups) in ('Neck', 'Neck1', 'neck_01'):
            c = ncoord(v.co)
            if 0.0 < c[0] < 0.05: nk.append(c)
    nk = np.array(nk)
    NCY = float(np.median(nk[:, 2]))
    NA = float(np.percentile(np.abs(nk[:, 1]), 90)) + 0.011; NB = float(np.percentile(np.abs(nk[:, 2] - NCY), 90)) + 0.011
    log.append("neck hole: half-width %.3f, half-depth %.3f, centre offset %.3f (from %d neck verts)" % (NA, NB, NCY, len(nk)))
    def neck_r(p):
        s_, x, y = ncoord(p); return s_, math.hypot(x / NA, (y - NCY) / NB), math.atan2((y - NCY) / NB, x / NA)
    def neck_point(th, s_, grow, h=0.0):
        # on the hole's ellipse at height s_, then h up along the leaning neck (the collar stands along the neck)
        return a0 + nu * s_ + (nu * 0.6 + nneck * 0.4).normalized() * h + ex * ((NA + grow) * math.cos(th)) + ey * (NCY + (NB + grow) * math.sin(th))
    kill = []
    for v in bm.verts:
        dom = dom_bone(list(v[deform].items()))
        side = 'l' if v.co.x > 0 else 'r'; wp, wd = wrist[side]
        beyond_wrist = (v.co - wp).dot(wd) > 0.015 and abs(v.co.x) > 0.20 and v.co.z > 0.75   # hands never reach below 0.9 m in the rest pose
        ns, nr, _ = neck_r(v.co)
        in_neck = (ns > -0.12 and nr < 1.0) or ns > 0.05   # deep enough that the cut is the cylinder all round (the nape too)
        if dom in HEAD or in_neck or beyond_wrist or v.co.z < ankle_z - 0.012: kill.append(v)
        # flatten nipples before offsetting
    bmesh.ops.delete(bm, geom=kill, context='VERTS')
    # user 2026-10-02: no nipples through the cloth. A coverall over a bra-like inner layer: the whole tip of each breast
    # is relaxed into a smooth dome (within NIP_R of the nipple, fading out), not just the nipple vertices
    NIP_R = 0.045
    for sd in (-1, 1):   # the tip: the most forward point of each breast (the base mesh has no nipple group)
        nv_ = [v for v in bm.verts if 0.03 < sd * v.co.x < 0.16 and 1.10 < v.co.z < 1.40 and v.co.y < 0]
        if not nv_: continue
        c = min(nv_, key=lambda v: v.co.y).co.copy()
        zone = [(v, float(smooth01(NIP_R, NIP_R * 0.35, (v.co - c).length))) for v in bm.verts if (v.co - c).length < NIP_R and v.co.y < c.y + 0.03]
        for _ in range(14):
            new = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.55 * w)) for v, w in zone if v.link_edges]
            for v, co in new: v.co = co
        log.append("bust %s: %d verts relaxed" % ("l" if sd > 0 else "r", len(zone)))
    # user 2026-10-02 ("ткань же плотная", the sketch): a dense cloth does not follow the cleavage or tuck under the bust -
    # it spans from breast to breast and falls from the bust to the ribs. The front of the chest is replaced by its convex
    # envelope: per horizontal slice (the bridge between the breasts), then per vertical slice (no fold under the bust,
    # a straight fall from the collarbone to the apex). Vertices only ever move outwards, faded in at the zone's rim.
    apex = [min((v for v in bm.verts if 0.03 < sd * v.co.x < 0.16 and 1.10 < v.co.z < 1.40 and v.co.y < 0), key=lambda v: v.co.y).co.copy() for sd in (-1, 1)]
    az = (apex[0].z + apex[1].z) / 2; ax_ = (abs(apex[0].x) + abs(apex[1].x)) / 2
    Z0, Z1, XW = az - 0.13, az + 0.16, ax_ + 0.07
    front = [v for v in bm.verts if v.co.y < -0.02 and abs(v.co.x) < XW and Z0 < v.co.z < Z1]
    def hull_top(pts):   # upper convex hull of (u, h) points, as arrays for np.interp
        pts = sorted(set(pts)); h = []
        for p in pts:
            while len(h) >= 2 and (h[-1][0] - h[-2][0]) * (p[1] - h[-2][1]) - (h[-1][1] - h[-2][1]) * (p[0] - h[-2][0]) >= 0: h.pop()
            h.append(p)
        return np.array([q[0] for q in h]), np.array([q[1] for q in h])
    def envelope(key_bin, key_u, BIN):
        bins = {}
        for v in front: bins.setdefault(int(math.floor(key_bin(v) / BIN)), []).append(v)
        tgt = {}
        for k_, vs in bins.items():
            vs = vs + bins.get(k_ - 1, []) + bins.get(k_ + 1, [])
            u, hgt = hull_top([(round(key_u(v), 5), round(-v.co.y, 5)) for v in vs])
            if len(u) < 2: continue
            for v in bins[k_]: tgt[v] = -float(np.interp(key_u(v), u, hgt))
        return tgt
    moved = 0
    # the bridge between the breasts (horizontal) only around the bust; the fall under the bust (vertical) only below the
    # apex - above it the cloth follows the upper chest (a straight ramp from the collarbone read as a shield, user 2026-10-02)
    zones = ((az - 0.10, az + 0.07), (Z0, az + 0.015))
    for (key_bin, key_u, BIN), (za, zb) in zip(((lambda v: v.co.z, lambda v: v.co.x, 0.006), (lambda v: v.co.x, lambda v: v.co.z, 0.006)), zones):
        for v, ty in envelope(key_bin, key_u, BIN).items():
            fade = float(smooth01(XW, XW - 0.04, abs(v.co.x)) * smooth01(za, za + 0.04, v.co.z) * smooth01(zb, zb - 0.04, v.co.z))
            if ty < v.co.y: v.co.y += (ty - v.co.y) * fade; moved += 1
    for _ in range(4):   # settle the bin steps
        new = {v: sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges) for v in front if v.link_edges}
        for v, c in new.items(): v.co.y = min(v.co.y, c.y * 0.5 + v.co.y * 0.5)
    log.append("bust envelope: apex z %.3f, %d of %d front verts moved out" % (az, moved, len(front)))
    bm.normal_update()
    for v in bm.verts:
        # the trousers go over the boot shafts (shoes03 reaches 0.24 m and is wider than the shin + offset)
        v.co += v.normal * (OFFSET + 0.013 * float(smooth01(0.30, 0.22, v.co.z)))
    bm.normal_update()
    # cloth spans hollows, it does not follow the skin into them: the cleft between the buttocks and the crotch.
    # Smooth those regions, but only ever move a vertex outwards (away from the body), so the fabric bridges the gap.
    hz = W('Hips' if 'Hips' in BN else 'pelvis').z
    def bridge(sel, outward, iters):
        for _ in range(iters):
            new = {}
            for v in sel:
                nb = [e.other_vert(v).co for e in v.link_edges]
                if not nb: continue
                c = sum(nb, Vector()) / len(nb); d = (c - v.co).dot(outward)
                if d > 0: new[v] = v.co + outward * d
            for v, c in new.items(): v.co = c
        return len(sel)
    back = [v for v in bm.verts if abs(v.co.x) < BRIDGE_HALF_W * (1 if v.co.y > 0 else 0.8)
            and (v.co.y > 0 and hz - 0.17 < v.co.z < hz + 0.10 or v.co.y < 0 and hz - 0.17 < v.co.z < hz - 0.04)]
    # behind: the cleft between the buttocks becomes a flat span from buttock crest to buttock crest (a sag of a few mm).
    # The cleft's vertices are relaxed across the span with the strip's rim held still (a Laplacian with a fixed rim
    # cannot fold - pushing the walls straight back stacks them into a fold that reads as a dark ridge), then put on the
    # span. Between the thighs (no surface across the middle) nothing is changed.
    bk = [v for v in bm.verts if v.co.y > 0 and abs(v.co.x) < 0.12 and hz - 0.20 < v.co.z < hz + 0.12]
    bands = {}
    for v in bk: bands.setdefault(int(round(v.co.z / 0.008)), []).append(v)
    span = {}   # band -> (crest x left, crest x right, span height)
    for k_ in bands:
        band = bands.get(k_ - 1, []) + bands[k_] + bands.get(k_ + 1, [])
        if not any(abs(u.co.x) < 0.012 for u in band): continue
        cr = [max((u.co for u in band if 0.035 < sd * u.co.x < 0.12), key=lambda c: c.y, default=None) for sd in (-1, 1)]
        if None in cr: continue
        span[k_] = (cr[0].x, cr[1].x, min(cr[0].y, cr[1].y), min(u.co.y for u in band if abs(u.co.x) < 0.012))
    def span_at(z):
        k0 = z / 0.008; ks = sorted(span, key=lambda k: abs(k - k0))[:2]
        if not ks: return None
        if len(ks) == 1 or ks[0] == ks[1]: return span[ks[0]]
        t = min(max((k0 - ks[0]) / (ks[1] - ks[0]), 0), 1)
        return tuple(a_ + (b_ - a_) * t for a_, b_ in zip(span[ks[0]], span[ks[1]]))
    region = set()
    for v in bk:
        sp = span.get(int(round(v.co.z / 0.008)))
        if sp and sp[0] + 0.006 < v.co.x < sp[1] - 0.006 and v.co.y < sp[2] - 0.0005: region.add(v)
    # grow by one ring so the relaxed patch includes the shallow lips of the cleft
    region |= {e.other_vert(v) for v in list(region) for e in v.link_edges if e.other_vert(v) in bk and int(round(e.other_vert(v).co.z / 0.008)) in span}
    inner = [v for v in region if all(e.other_vert(v) in region for e in v.link_edges)]
    for _ in range(300):
        new = {}
        for v in inner:
            nb = [e.other_vert(v).co for e in v.link_edges]
            new[v] = (sum(c.x for c in nb) / len(nb), sum(c.z for c in nb) / len(nb))
        for v, (x_, z_) in new.items(): v.co.x, v.co.z = x_, z_
    for v in region:
        sp = span_at(v.co.z)
        if not sp: continue
        h = (sp[1] - sp[0]) / 2; m = (sp[1] + sp[0]) / 2; t = min(abs(v.co.x - m) / h, 1.0)
        groove = CLEFT_KEEP * max(sp[2] - sp[3], 0) * math.exp(-0.5 * ((v.co.x - m) / CLEFT_W) ** 2)   # a soft, shallow cleft
        v.co.y = max(v.co.y, sp[2] - BRIDGE_SAG * (1 - t * t) - groove)
    nb_ = len(region)
    # where the span ends (the gluteal fold) a few faces may still turn sideways or face front: smooth just those
    bm.normal_update()
    for _ in range(3):
        bad = {v for f in bm.faces if f.normal.y < 0.2 and all(v.co.y > 0 and abs(v.co.x) < 0.05 and hz - 0.10 < v.co.z < hz + 0.10 for v in f.verts)
               for v in f.verts}
        if not bad: break
        for _ in range(8):
            new = {v: sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges) for v in bad}
            for v, c in new.items(): v.co = c
        bm.normal_update()
    log.append("seat: %d faces still turned" % sum(1 for f in bm.faces if f.normal.y < 0.2 and all(v.co.y > 0 and abs(v.co.x) < 0.05 and hz - 0.10 < v.co.z < hz + 0.10 for v in f.verts)))
    nf_ = bridge([v for v in back if v.co.y < 0], Vector((0, -1, 0)), 25)
    bm.normal_update(); log.append("bridged hollows: %d verts behind, %d in front" % (nb_, nf_))

    # boundary loops: neck (highest), wrists, ankles
    bnd = [e for e in bm.edges if e.is_boundary]
    loops, seen = [], set()
    for e in bnd:
        if e in seen: continue
        loop, stack = [], [e]
        while stack:
            f = stack.pop()
            if f in seen: continue
            seen.add(f); loop.append(f)
            for v in f.verts:
                for g in v.link_edges:
                    if g.is_boundary and g not in seen: stack.append(g)
        loops.append(loop)
    def centre(loop):
        vs = {v for e in loop for v in e.verts}; return sum((v.co for v in vs), Vector()) / len(vs), vs
    info = sorted([(centre(l)[0].z, l) for l in loops], key=lambda t: -t[0])
    log.append("boundary loops: %s" % [round(z, 3) for z, _ in info])

    gidx = {g.name: g.index for g in cov.vertex_groups}
    def set_weights(verts, wmap):
        for v in verts:
            d = v[deform]; d.clear()
            for n, w in wmap.items():
                if n in gidx: d[gidx[n]] = w
    def extrude(loop, up, out, mat, weights=None):
        c, _ = centre(loop)
        r = bmesh.ops.extrude_edge_only(bm, edges=loop)
        nv = [g for g in r['geom'] if isinstance(g, bmesh.types.BMVert)]
        for v in nv:
            radial = (v.co - c); radial.z = 0; radial.normalize() if radial.length > 1e-6 else None
            v.co += Vector((0, 0, up)) + radial * out
        for f in [g for g in r['geom'] if isinstance(g, bmesh.types.BMFace)]: f.material_index = mat
        if weights: set_weights(nv, weights)
        return [g for g in r['geom'] if isinstance(g, bmesh.types.BMEdge) and g.is_boundary]

    # stand collar, built as its own smooth band with thickness: outer wall, red top edge, inner wall.
    # The neckline ring keeps the garment's weights (no tearing from the shoulders); the upper part follows the neck base.
    neck_loop = info[0][1]
    NECKW = {'Neck': 0.35, 'Spine1': 0.65} if 'Neck' in gidx else {'neck_01': 0.35, 'spine_03': 0.65}
    base = sorted({v for e in neck_loop for v in e.verts}, key=lambda v: neck_r(v.co)[2])
    th = [neck_r(v.co)[2] for v in base]; S = [neck_r(v.co)[0] for v in base]; n_ = len(base)
    for _ in range(10):   # a smooth neckline height around the neck
        S = [(S[i - 1] + 2 * S[i] + S[(i + 1) % n_]) / 4 for i in range(n_)]
    for v, t, s_ in zip(base, th, S): v.co = neck_point(t, s_, 0.0)
    def ring(h, grow, wmix):
        out = []
        for v, t, s_ in zip(base, th, S):
            dt_ = abs(math.atan2(math.sin(t + math.pi / 2), math.cos(t + math.pi / 2)))   # angle from the front centre (-pi/2: front)
            vee = 1 - 0.85 * max(0.0, 1 - dt_ / 0.45)                                     # the V at the zip (sketch)
            nv = bm.verts.new(neck_point(t, s_, grow, h * vee if h > 0 else h))
            bw = dict(v[deform]); d = nv[deform]
            for gi, w in bw.items(): d[gi] = w * (1 - wmix)
            for nm, w in NECKW.items():
                if nm in gidx: d[gidx[nm]] = d.get(gidx[nm], 0) + w * wmix
            out.append(nv)
        return out
    H = 0.036
    # the collar stands a few mm off the neck (the neck skin cannot show through it); the inner wall reaches 2 cm down
    # inside, so no gap between it and the neck opens at the nape
    mid, top_o = ring(0.018, 0.004, 0.5), ring(H, 0.006, 1.0)
    top_i, bot_i = ring(H, -0.004, 1.0), ring(-0.020, -0.008, 0.0)
    def band(lo, hi, mat, flip=False):
        for i in range(n_):
            j = (i + 1) % n_
            q = (lo[i], lo[j], hi[j], hi[i]) if not flip else (hi[i], hi[j], lo[j], lo[i])
            bm.faces.new(q).material_index = mat
    # the collar's foot: a 15 mm band lying on the garment around the neckline - it closes the gaps where the cut
    # neckline and the collar meet (user 2026-10-02: "дыра воротник")
    foot = ring(-0.015, 0.010, 0.0)
    band(foot, base, 1)
    band(base, mid, 1); band(mid, top_o, 1)                  # outer wall
    band(top_o, top_i, 2)                                    # red top edge (faces up)
    band(bot_i, top_i, 1, flip=True)                         # inner wall (faces the neck)
    # cuffs at the wrists (the two loops closest to the hand height)
    hz = W(RIG['hand_l']).z
    wrists = sorted(info[1:], key=lambda t: abs(t[0] - hz))[:2]
    for _, l in wrists:
        c, vs = centre(l)
        # a clean cuff line: flatten the ragged cut onto the plane across the forearm
        side = 'l' if c.x > 0 else 'r'; wp, wd = wrist[side]; p0 = wp + wd * 0.015
        for v in vs: v.co -= wd * (v.co - p0).dot(wd)
        # direction along the forearm, from the elbow region to the wrist
        # cuff: a 3 mm step out from the forearm axis, then a 15 mm band back towards the elbow
        axis_c = sum((v.co for v in vs), Vector()) / len(vs)
        r1 = bmesh.ops.extrude_edge_only(bm, edges=l)
        v1 = [g for g in r1['geom'] if isinstance(g, bmesh.types.BMVert)]
        for f in [g for g in r1['geom'] if isinstance(g, bmesh.types.BMFace)]: f.material_index = 3
        for v in v1:
            rad = v.co - axis_c; rad -= wd * rad.dot(wd); v.co += rad.normalized() * 0.003
        e1 = [g for g in r1['geom'] if isinstance(g, bmesh.types.BMEdge) and g.is_boundary]
        r2 = bmesh.ops.extrude_edge_only(bm, edges=e1)
        for f in [g for g in r2['geom'] if isinstance(g, bmesh.types.BMFace)]: f.material_index = 3
        for v in [g for g in r2['geom'] if isinstance(g, bmesh.types.BMVert)]: v.co -= wd * 0.015
    # clean trouser hems: flatten the ankle cuts onto a level line
    for zc, l in info[1:]:
        if zc < 0.4:
            _, vs = centre(l)
            for v in vs: v.co.z = ankle_z - 0.012
    bm.to_mesh(cov.data); bm.free()

    SN = smooth_normals(cov.data)
    fabric_img = make_image("coverall_diffuse", paint_uv(cov, 0, normals=SN))
    # normal map from the relief, in the same UV space (tangent space: +x along u, +y along v - D3D9Client *_norm)
    H = paint_uv(cov, 0, relief, normals=SN)[..., 0]
    # soften: a small blur so seams read as rounded grooves, not steps; slopes limited to ~35 deg
    k = np.exp(-0.5 * (np.arange(-4, 5) / 1.5) ** 2); k /= k.sum()     # separable gaussian, sigma 1.5 px
    H = np.apply_along_axis(lambda r: np.convolve(r, k, 'same'), 0, H)
    H = np.apply_along_axis(lambda r: np.convolve(r, k, 'same'), 1, H)
    texel = 1.6 / TEX                      # ~metres per texel on the body (the UV layout spans ~1.6 m of cloth)
    gy, gx = np.gradient(H, texel)
    gx = np.clip(gx, -0.7, 0.7); gy = np.clip(gy, -0.7, 0.7)
    nrm = np.stack([-gx, -gy, np.ones_like(H)], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    im_n = bpy.data.images.new("coverall_diffuse_norm", TEX, TEX, alpha=False)
    im_n.pixels.foreach_set(np.concatenate([nrm * 0.5 + 0.5, np.ones((TEX, TEX, 1))], 2).astype(np.float32).ravel())
    im_n.filepath_raw = os.path.join(TEX_DIR, "coverall_diffuse_norm.png"); im_n.file_format = 'PNG'; im_n.save()
    log.append("normal map: relief %.1f..%.1f mm" % (H.min() * 1000, H.max() * 1000))
    for m in (material("CoverallFabric", image=fabric_img), material("CoverallCollar", color=tuple(FABRIC)),
              material("CoverallRed", color=tuple(PANEL), rough=0.6), material("CoverallCuff", color=tuple(PANEL * 0.85))):
        cov.data.materials.append(m)
    for p in cov.data.polygons: p.use_smooth = True

    # hide the skin under the garment (keeps the head, neck and hands visible)
    covered = body.vertex_groups.new(name="covered_by_coverall")
    idx = []
    for v in body.data.vertices:
        if not v.groups: continue
        n = dom_bone(v.groups)
        far_hand = v.co.z > 0.75 and any((Vector(v.co) - wrist[s][0]).dot(wrist[s][1]) > 0.0 and abs(v.co.x) > 0.2 for s in ('l', 'r'))
        ns, nr, _ = neck_r(v.co)
        inside_collar = ns > -0.12 and nr < 0.88       # only the neck well inside the collar stays visible (nearer the wall it showed through it)
        neck_vis = n in ('neck_01', 'Neck', 'Neck1') and (nr < 0.88 or ns > 0.10)
        if not ((n in HEAD and n != 'Neck1') or far_hand or neck_vis or inside_collar or v.co.z < ankle_z - 0.01) and v.co.z < neck_z + 0.02: idx.append(v.index)
    covered.add(idx, 1.0, 'REPLACE')
    mk = body.modifiers.new("HideUnderCoverall", 'MASK'); mk.vertex_group = covered.name; mk.invert_vertex_group = True

    # boots (CC0 MakeHuman asset)
    import addon_utils
    addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    basemesh = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    boots = AssetService.find_asset_absolute_path("shoes03/shoes03.mhclo", asset_subdir="clothes")
    if boots: HumanService.add_mhclo_asset(boots, basemesh, asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN"); log.append("boots added")
    # hair: short, voluminous, curly copper (user's pick: cortu_strawberry_cloud_hair, CC0) instead of the sleek bob
    HAIR = None   # the curly cortu_strawberry_cloud_hair was rejected (2026-10-01); the base figure keeps toigo_curled_under_bob
    if HAIR:
        for o in list(bpy.data.objects):
            if o.type == 'MESH' and ('bob' in o.name.lower() or ('hair' in o.name.lower() and HAIR not in o.name)):
                bpy.data.objects.remove(o, do_unlink=True)
        hp = AssetService.find_asset_absolute_path("%s/%s.mhclo" % (HAIR, HAIR), asset_subdir="hair")
        HumanService.add_mhclo_asset(hp, basemesh, asset_type="Hair", subdiv_levels=0, material_type="MAKESKIN")
        hair = next(o for o in bpy.data.objects if o.type == 'MESH' and HAIR in o.name)
        img = None
        for slot in hair.material_slots:
            if slot.material and slot.material.use_nodes:
                for nd in slot.material.node_tree.nodes:
                    if nd.type == 'TEX_IMAGE' and nd.image and "normal" not in nd.image.name.lower() and img is None: img = bpy.path.abspath(nd.image.filepath)
        import json
        json.dump({"textures": [{"src": img, "dds": r"Tantra\Astronavigator\Hair.dds", "alpha": True}], "orbiter": os.path.abspath(os.path.join(HERE, "..", ".."))},
                  open(os.path.join(HERE, "hair_texture.json"), "w"), indent=1)
        log.append("hair %s: %d verts, texture %s" % (HAIR, len(hair.data.vertices), img))

    tris = sum(len(p.vertices) - 2 for p in cov.data.polygons); log.append("coverall tris %d" % tris)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))

    # renders
    import sys; sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage()
    H = max((body.matrix_world @ v.co).z for v in body.data.vertices)
    render_util.shoot(cam, OUT, render_util.standard_shots("cov", H), log)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
