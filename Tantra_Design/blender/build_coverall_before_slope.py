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
FABRIC = np.array([0.86, 0.86, 0.84]); PANEL = np.array([0.40, 0.43, 0.47]); DARK = np.array([0.20, 0.21, 0.24])
SEAM = np.array([0.62, 0.62, 0.61]); RED = np.array([0.72, 0.12, 0.10]); ZIP = np.array([0.16, 0.17, 0.19])
OFFSET = 0.008          # m, cloth above skin
TEX = 2048

def smooth01(e0, e1, x): t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def pattern(P, N):
    """P, N: (n,3) surface points and normals in rest pose (Blender: x lateral, -y front, z up). Returns (n,3) colour."""
    x, y, z = P[:, 0], P[:, 1], P[:, 2]; ax = np.abs(x); nx, ny, nz = N[:, 0], N[:, 1], N[:, 2]
    col = np.tile(FABRIC, (len(P), 1))
    front = ny < -0.15; back = ny > 0.15
    torso = (z > 0.80) & (z < 1.48) & (ax < 0.20)
    # side panels: along the flanks of torso and legs, and the outer side of the sleeves
    side = smooth01(0.55, 0.62, np.abs(nx)) * ((z > 0.10) & (z < 1.33))
    col = col * (1 - side[:, None]) + PANEL * side[:, None]
    piping = (np.abs(np.abs(nx) - 0.585) < 0.018) & (z > 0.12) & (z < 1.32)
    col[piping] = RED
    # shoulder yoke with ribbing
    yoke = (z > 1.31) & (nz > 0.20) & (ax > 0.07)
    col[yoke] = PANEL
    rib = yoke & ((np.floor(z / 0.012) % 2) == 0); col[rib] = PANEL * 0.93
    # waist band
    band = (np.abs(z - 1.025) < 0.022) & (ax < 0.22)
    col[band] = DARK
    # princess seams, front and back
    sx = 0.068 + 0.03 * np.clip(1.25 - z, 0, 0.35)
    seam = (np.abs(ax - sx) < 0.0028) & (z > 0.86) & (z < 1.36) & (front | back) & ~band
    col[seam] = SEAM * 0.8
    # centre front zip with teeth
    zip_ = (ax < 0.0045) & front & (z > 0.78) & (z < 1.50)
    col[zip_] = ZIP
    teeth = zip_ & ((np.floor(z / 0.004) % 2) == 0) & (ax < 0.0025); col[teeth] = ZIP * 1.8
    # knee panels with three ribs
    knee = (np.abs(z - 0.50) < 0.065) & (ny < -0.35) & (ax > 0.03)
    col[knee] = PANEL
    kr = knee & (np.abs(((z - 0.50) / 0.02) - np.round((z - 0.50) / 0.02)) < 0.18); col[kr] = PANEL * 0.8
    # fabric noise
    col = col * (1 + 0.025 * (np.random.default_rng(1).random((len(P), 1)) - 0.5))
    return np.clip(col, 0, 1)

def relief(P, N):
    """height of the cloth surface in metres (+ up from the cloth): seams sink, piping and band rise, ribs, zip, weave.
    Same regions as pattern(); returned as (n,3) so it rasterizes like a colour."""
    x, y, z = P[:, 0], P[:, 1], P[:, 2]; ax = np.abs(x); nx, ny, nz = N[:, 0], N[:, 1], N[:, 2]
    h = np.zeros(len(P))
    front = ny < -0.15; back = ny > 0.15
    # side panel edges: a sewn seam (groove) along both edges of the side panel
    for e in (0.55, 0.62):
        h -= 0.0008 * np.exp(-((np.abs(nx) - e) / 0.012) ** 2) * ((z > 0.10) & (z < 1.33))
    h += 0.0007 * np.exp(-((np.abs(np.abs(nx) - 0.585)) / 0.012) ** 2) * ((z > 0.12) & (z < 1.32))   # piping cord
    yoke = (z > 1.31) & (nz > 0.20) & (ax > 0.07)
    h += yoke * 0.00025 * (0.5 + 0.5 * np.cos(2 * np.pi * z / 0.012))                                  # ribbed yoke
    band = np.abs(z - 1.025) < 0.022
    h += (band & (ax < 0.22)) * 0.0012; h -= 0.0006 * np.exp(-((np.abs(z - 1.025) - 0.022) / 0.002) ** 2) * (ax < 0.22)
    sx = 0.068 + 0.03 * np.clip(1.25 - z, 0, 0.35)
    h -= 0.0007 * np.exp(-((ax - sx) / 0.0025) ** 2) * ((z > 0.86) & (z < 1.36) & (front | back))     # princess seams
    zp = (ax < 0.0045) & front & (z > 0.78) & (z < 1.50)
    h += zp * (0.0006 + 0.0006 * (np.floor(z / 0.004) % 2))                                            # zip teeth
    knee = (np.abs(z - 0.50) < 0.065) & (ny < -0.35) & (ax > 0.03)
    h += knee * 0.00025 * (0.5 + 0.5 * np.cos(2 * np.pi * (z - 0.50) / 0.02))
    # (no weave: at ~0.8 mm per texel a woven pattern aliases into stripes and moire)
    return np.stack([h, h, h], 1)


def paint_uv(obj, mat_index=0, fn=None):
    """Rasterize the pattern into the object's UV space."""
    me = obj.data; me.calc_loop_triangles()
    uv = me.uv_layers.active.data
    img = np.zeros((TEX, TEX, 3)); filled = np.zeros((TEX, TEX), bool)
    V = np.array([v.co for v in me.vertices]); VN = np.array([v.normal for v in me.vertices])
    for tri in me.loop_triangles:
        if tri.material_index != mat_index: continue
        uvs = np.array([uv[l].uv for l in tri.loops]) * (TEX - 1)
        p = V[list(tri.vertices)]; n = VN[list(tri.vertices)]
        x0, y0 = np.floor(uvs.min(0)).astype(int) - 1; x1, y1 = np.ceil(uvs.max(0)).astype(int) + 1
        x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, TEX - 1), min(y1, TEX - 1)
        if x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        a, b, c = uvs
        den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-12: continue
        w0 = ((b[1] - c[1]) * (gx - c[0]) + (c[0] - b[0]) * (gy - c[1])) / den
        w1 = ((c[1] - a[1]) * (gx - c[0]) + (a[0] - c[0]) * (gy - c[1])) / den
        w2 = 1 - w0 - w1
        m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)      # slight overdraw closes seams between islands
        if not m.any(): continue
        W = np.stack([w0[m], w1[m], w2[m]], 1)
        P = W @ p; Nn = W @ n; Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-9
        c_ = (fn or pattern)(P, Nn)
        iy, ix = (gy[m] - 0.5).astype(int), (gx[m] - 0.5).astype(int)
        img[iy, ix] = c_; filled[iy, ix] = True
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
        return a0 + nu * s_ + nneck * h + ex * ((NA + grow) * math.cos(th)) + ey * (NCY + (NB + grow) * math.sin(th))
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
    nip = [v for v in bm.verts if any(names[k] == 'nipples' and w > 0.05 for k, w in v[deform].items())]
    for _ in range(3):
        for v in nip:
            nb = [e.other_vert(v).co for e in v.link_edges]
            if nb: v.co = v.co.lerp(sum(nb, Vector()) / len(nb), 0.6)
    bm.normal_update()
    for v in bm.verts:
        # the trousers go over the boot shafts (shoes03 reaches 0.24 m and is wider than the shin + offset)
        v.co += v.normal * (OFFSET + 0.013 * float(smooth01(0.30, 0.22, v.co.z)))
    bm.normal_update()

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
            nv = bm.verts.new(neck_point(t, s_, grow, h))
            bw = dict(v[deform]); d = nv[deform]
            for gi, w in bw.items(): d[gi] = w * (1 - wmix)
            for nm, w in NECKW.items():
                if nm in gidx: d[gidx[nm]] = d.get(gidx[nm], 0) + w * wmix
            out.append(nv)
        return out
    H = 0.036
    mid, top_o = ring(0.018, 0.001, 0.5), ring(H, 0.002, 1.0)
    top_i, bot_i = ring(H, -0.002, 1.0), ring(-0.004, -0.004, 0.0)
    def band(lo, hi, mat, flip=False):
        for i in range(n_):
            j = (i + 1) % n_
            q = (lo[i], lo[j], hi[j], hi[i]) if not flip else (hi[i], hi[j], lo[j], lo[i])
            bm.faces.new(q).material_index = mat
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

    fabric_img = make_image("coverall_diffuse", paint_uv(cov, 0))
    # normal map from the relief, in the same UV space (tangent space: +x along u, +y along v - D3D9Client *_norm)
    H = paint_uv(cov, 0, relief)[..., 0]
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
              material("CoverallRed", color=tuple(RED), rough=0.5), material("CoverallCuff", color=tuple(PANEL * 0.85))):
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
        inside_collar = ns > -0.12 and nr < 1.08       # the neck inside the collar stays visible (no hole at the nape)
        if not (n in HEAD or far_hand or n in ('neck_01', 'Neck') or inside_collar or v.co.z < ankle_z - 0.01) and v.co.z < neck_z + 0.02: idx.append(v.index)
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
