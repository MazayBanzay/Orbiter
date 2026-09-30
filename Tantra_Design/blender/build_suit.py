# Tantra / OrbiterCrew: space suit variant 1 "Каркас" - a protective, close-fitting suit on a load-bearing exoskeleton.
# Direction (user, with Interstellar suits as the reference, "pushed further"): dense protective fabric that fits the body
# without showing anatomy; hard pieces only where they carry or protect - chest cuirass, shoulder blocks, knee pads,
# a dark soft neck seal, a helmet with a framed, recessed visor; and a reinforced frame that carries the load:
# hip ring, two back rails from the pack to the ring, beam struts along the legs with hip and knee drives, ankle stirrups.
# Soft parts come from the body proxy (they deform with the rig); hard parts are projected onto the garment and take
# their skin weights from it. White main, red <= 20 % (drives, helmet crest, the "37 ЗВЁЗДНАЯ" marking).
import bpy, bmesh, os, math, traceback
import numpy as np
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

HERE = os.path.dirname(__file__)
TEX_DIR = os.path.join(HERE, "textures"); os.makedirs(TEX_DIR, exist_ok=True)
OUT = os.path.join(HERE, "renders"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "build_suit.log"); log = []
TEX = 2048
FONT = r"C:\Windows\Fonts\ARIALNB.TTF"

# ---- palette ----
WHITE = np.array([0.88, 0.88, 0.86]); LIGHT = np.array([0.76, 0.77, 0.78]); SEAM = np.array([0.66, 0.67, 0.68])
GLOVE = np.array([0.30, 0.31, 0.33]); BOOT = np.array([0.56, 0.57, 0.58]); SOLE = np.array([0.18, 0.18, 0.19])
RED = (0.72, 0.12, 0.10); METAL = (0.50, 0.52, 0.55); FRAME = (0.36, 0.38, 0.41); DARK = (0.14, 0.15, 0.17)
SHELL = (0.93, 0.93, 0.91); PLATE = (0.90, 0.90, 0.88); PAD = (0.80, 0.81, 0.82)

# ---- helmet (Blender frame: x lateral, -y front, z up) ----
HC = Vector((0.0, -0.045, 1.625)); HR = Vector((0.142, 0.162, 0.182)); CUT_Z = 1.465

def smooth01(e0, e1, x): t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

# hard parts get a small tiled surface set (colour + _norm relief + _refl reflectivity) for D3D9Client
SURF = {"SuitPlate": "plate", "Pack": "plate", "HelmetShell": "plate", "Frame": "frame", "Housing": "frame", "HelmMetal": "metal", "Bolt": "metal"}
SURF_SPEC = {  # base colour, reflectivity (0..1), relief kind
    "plate": ((0.90, 0.90, 0.88), 0.10, "composite"),
    "frame": ((0.36, 0.38, 0.41), 0.40, "brushed"),
    "metal": ((0.55, 0.57, 0.60), 0.55, "brushed"),
}
_surf_cache = {}

def save_png(name, arr, size):
    im = bpy.data.images.new(name, size, size, alpha=False)
    rgba = np.concatenate([arr, np.ones((size, size, 1))], 2).astype(np.float32)
    im.pixels.foreach_set(rgba.ravel()); im.filepath_raw = os.path.join(TEX_DIR, name + ".png"); im.file_format = 'PNG'; im.save()
    return im

def surface_image(kind, size=512):
    if kind in _surf_cache: return _surf_cache[kind]
    col, refl, relief = SURF_SPEC[kind]
    rng = np.random.default_rng({"plate": 11, "frame": 12, "metal": 13}[kind])
    yy, xx = np.mgrid[0:size, 0:size] / size
    def tile_noise(freq, n=6):      # periodic (tiles seamlessly) smooth noise
        acc = np.zeros((size, size))
        for _ in range(n):
            kx, ky = rng.integers(-freq, freq + 1, 2); ph = rng.uniform(0, 6.28)
            acc += np.sin(2 * np.pi * (kx * xx + ky * yy) + ph)
        return acc / n
    if relief == "composite":       # moulded composite: a faint weave and soft casting ripples
        h = 0.5 + 0.10 * tile_noise(3) + 0.05 * np.sin(2 * np.pi * 64 * xx) * np.sin(2 * np.pi * 64 * yy)
        shade = 1 + 0.015 * tile_noise(5)
    else:                           # brushed metal: fine streaks along u
        streak = np.zeros((size, size))
        for _ in range(24):
            ky = rng.integers(20, 180); ph = rng.uniform(0, 6.28); streak += np.sin(2 * np.pi * ky * yy + ph + 0.3 * np.sin(2 * np.pi * 2 * xx))
        h = 0.5 + 0.03 * streak / 24 + 0.06 * tile_noise(2)
        shade = 1 + 0.05 * streak / 24
    base = np.clip(np.array(col)[None, None, :] * shade[..., None], 0, 1)
    img = save_png("surf_" + kind, base, size)
    gy, gx = np.gradient(h, 1.0 / size)
    depth = 0.0015 if relief == "composite" else 0.001      # relief depth (m) over a 0.25 m tile
    n = np.stack([-gx * depth / 0.25, gy * depth / 0.25, np.ones_like(h)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
    save_png("surf_" + kind + "_norm", n * 0.5 + 0.5, size)
    save_png("surf_" + kind + "_refl", np.full((size, size, 3), refl) * (1 + 0.1 * tile_noise(4))[..., None], size)
    _surf_cache[kind] = img
    return img

def box_uv(bm, scale):
    """box projection in metres: each face takes the two axes across its dominant normal"""
    uv = bm.loops.layers.uv.new("UVMap")
    for f in bm.faces:
        n = f.normal; a = max(range(3), key=lambda i: abs(n[i])); u, v = [i for i in range(3) if i != a]
        for l in f.loops: l[uv].uv = (l.vert.co[u] / scale, l.vert.co[v] / scale)

def material(name, color=None, image=None, rough=0.6, alpha=1.0, metal=0.0):
    if image is None and name in SURF: image = surface_image(SURF[name])
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial"); bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs[0]); bsdf.inputs["Roughness"].default_value = rough; bsdf.inputs["Metallic"].default_value = metal
    if image:
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = image; nt.links.new(t.outputs[0], bsdf.inputs["Base Color"])
    else:
        bsdf.inputs["Base Color"].default_value = (*color, 1)
    if alpha < 1:
        bsdf.inputs["Alpha"].default_value = alpha
        if hasattr(m, "surface_render_method"): m.surface_render_method = 'BLENDED'
        else: m.blend_method = 'BLEND'
    return m

def new_object(name, bm, mats, weights, arm, smooth=True):
    """weights: {bone: w} for all vertices, or a function(co) -> {bone: w}"""
    if not bm.loops.layers.uv: bm.normal_update(); box_uv(bm, 0.25)   # hard parts: tiled surface textures, 25 cm per tile
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    for m in mats: me.materials.append(m)
    groups = {}
    for v in me.vertices:
        wm = weights(v.co) if callable(weights) else weights
        for b, w in wm.items():
            if w <= 0: continue
            if b not in groups: groups[b] = o.vertex_groups.new(name=b)
            groups[b].add([v.index], w, 'REPLACE')
    mod = o.modifiers.new("Armature", 'ARMATURE'); mod.object = arm
    for p in me.polygons: p.use_smooth = smooth
    return o

def cylinder(bm, a, b, r, seg=16, mat=0, twist=0.0):
    """closed prism/cylinder between points a and b (seg=6: a hex beam)"""
    d = b - a; L = d.length
    res = bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=seg, radius1=r, radius2=r, depth=L)
    q = Vector((0, 0, 1)).rotation_difference(d.normalized())
    M = Matrix.Translation((a + b) / 2) @ q.to_matrix().to_4x4() @ Matrix.Rotation(twist, 4, 'Z')
    for v in res['verts']: v.co = M @ v.co
    for f in {f for v in res['verts'] for f in v.link_faces}: f.material_index = mat
    return res['verts']

def ring_loft(bm, rings, mat=0, closed=True):
    """rings: list of equal-length point lists; quads between consecutive rings (and last->first if closed)"""
    V = [[bm.verts.new(p) for p in ring] for ring in rings]
    n = len(rings[0]); k = len(rings)
    for i in range(k if closed else k - 1):
        A, B = V[i], V[(i + 1) % k]
        for j in range(n):
            f = bm.faces.new((A[j], A[(j + 1) % n], B[(j + 1) % n], B[j])); f.material_index = mat
    return V

def box_beam(bm, a, c, w, t, outward, mat=0):
    """flat structural link from a to c: width w in the limb plane, thickness t along 'outward'"""
    d = (c - a).normalized(); n = (outward - d * outward.dot(d)).normalized(); s_ = d.cross(n).normalized()
    V = [bm.verts.new(p + s_ * (sw * w / 2) + n * (sn * t / 2)) for p in (a, c) for sw, sn in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    for q in ((0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)):
        bm.faces.new([V[i] for i in q]).material_index = mat

def rounded_box(bm, center, size, bev, mat=0):
    res = bmesh.ops.create_cube(bm, size=1.0); vs = res['verts']
    for v in vs: v.co = center + Vector((v.co.x * size.x, v.co.y * size.y, v.co.z * size.z))
    for f in {f for v in vs for f in v.link_faces}: f.material_index = mat
    bmesh.ops.bevel(bm, geom=list({e for v in vs for e in v.link_edges}), offset=bev, segments=2, profile=0.5, affect='EDGES')

def actuator(bm, center, axis, R, depth, back, mats):
    """rotary joint drive: finned housing, red end ring, dark hub with bolts, and the motor can behind it"""
    housing, redm, hub, metalm = mats
    a0, a1 = center - axis * (depth * 0.5), center + axis * (depth * 0.5)
    cylinder(bm, a0, a1, R, 28, housing)
    for k in (0.2, 0.5, 0.8):                                               # cooling fins
        p = a0 + (a1 - a0) * k; cylinder(bm, p - axis * 0.0015, p + axis * 0.0015, R * 1.08, 28, housing)
    cylinder(bm, a1, a1 + axis * 0.004, R * 0.93, 28, redm)                 # red end ring
    cylinder(bm, a1 + axis * 0.004, a1 + axis * 0.012, R * 0.52, 20, hub)   # output hub
    ref = Vector((0, 0, 1)) if abs(axis.z) < 0.9 else Vector((1, 0, 0))
    u = axis.cross(ref).normalized(); w = axis.cross(u).normalized()
    for k in range(8):                                                      # bolt circle
        ang = 2 * math.pi * k / 8; p = a1 + axis * 0.004 + (u * math.cos(ang) + w * math.sin(ang)) * R * 0.73
        cylinder(bm, p, p + axis * 0.004, 0.0035, 6, metalm)
    m0 = center + back * (R * 0.75); m1 = m0 + back * (R * 1.2)            # motor can, geared into the housing
    cylinder(bm, m0, m1, R * 0.42, 18, metalm)
    cylinder(bm, m1, m1 + back * 0.006, R * 0.34, 18, hub)

# ---------------------------------------------------------------- fabric pattern
def pattern_factory(elbows, wrists, knees):
    def pattern(P, N):
        x, y, z = P[:, 0], P[:, 1], P[:, 2]; ax = np.abs(x)
        col = np.tile(WHITE, (len(P), 1))
        front = N[:, 1] < -0.2
        # quilted protective layer: stitch rows on the limbs, panel seams on the torso
        arms = (ax > 0.21) & (z > 0.95); legs = (z < 0.90) & (z > 0.18)
        q = np.abs(((z / 0.042) % 1) - 0.5) < 0.045
        col[(arms | legs) & q] *= 0.92
        col[(ax < 0.004) & front & (z > 0.95) & (z < 1.40)] = SEAM
        col[(np.abs(np.abs(N[:, 0]) - 0.70) < 0.02) & (z > 0.95) & (z < 1.40)] = SEAM
        col[legs & (np.abs(np.abs(N[:, 0]) - 0.80) < 0.02)] = SEAM          # side seams of the legs, under the struts
        # bellows at elbows and knees
        for c in elbows + knees:
            d = np.linalg.norm(P - c, axis=1)
            m = d < 0.075
            col[m] = LIGHT
            rib = m & ((np.floor((z - c[2]) / 0.014) % 2) == 0); col[rib] = LIGHT * 0.86
        # gloves beyond the wrist, with a dark wrist lock ring
        for wp, wd in wrists:
            s = (P - wp) @ wd
            side = (ax > 0.25) & (z > 0.75)
            col[side & (s > -0.005)] = GLOVE
            col[side & (s > -0.028) & (s <= -0.005)] = np.array(DARK)
        # boots: grey shaft, dark sole
        col[z < 0.18] = BOOT; col[(z < 0.18) & (z > 0.165)] = LIGHT * 0.9
        col[z < 0.030] = SOLE
        col = col * (1 + 0.02 * (np.random.default_rng(3).random((len(P), 1)) - 0.5))
        return np.clip(col, 0, 1)
    return pattern

def height_factory(elbows, wrists, knees, joints):
    """relief of the fabric (0..1, 0.5 = flat): soft cloth undulation, folds bunched at the joints, stitch and seam
    grooves, bellows ribs; turned into a tangent-space normal map below"""
    rng = np.random.default_rng(7)
    dirs = [(rng.normal(size=3), rng.uniform(0, 6.28)) for _ in range(12)]
    def noise(P, freq, k0, k1):
        acc = np.zeros(len(P))
        for d, ph in dirs[k0:k1]:
            d = d / np.linalg.norm(d); acc += np.sin(P @ d * freq + ph)
        return acc / (k1 - k0)
    def height(P, N):
        x, y, z = P[:, 0], P[:, 1], P[:, 2]; ax = np.abs(x)
        h = 0.5 + 0.09 * noise(P, 2 * np.pi / 0.22, 0, 4) + 0.04 * noise(P, 2 * np.pi / 0.07, 4, 8)
        # folds bunched round the joints: ripples across the limb, their phase wanders so they look sewn by nobody
        for c, a, r, lam, amp in joints:
            d2 = np.sum((P - c) ** 2, 1); w = np.exp(-d2 / (r * r))
            sa = (P - c) @ a
            h += 1.5 * amp * w * np.sin(2 * np.pi * sa / lam + 4.0 * noise(P, 2 * np.pi / 0.12, 8, 12))
        # stitch rows and seams are grooves
        arms = (ax > 0.21) & (z > 0.95); legs = (z < 0.90) & (z > 0.18)
        q = np.abs(((z / 0.042) % 1) - 0.5)
        h -= np.where((arms | legs) & (q < 0.06), 0.18 * (1 - q / 0.06), 0)
        front = N[:, 1] < -0.2
        h -= np.where((ax < 0.004) & front & (z > 0.95) & (z < 1.40), 0.25, 0)
        h -= np.where((np.abs(np.abs(N[:, 0]) - 0.70) < 0.02) & (z > 0.95) & (z < 1.40), 0.2, 0)
        h -= np.where(legs & (np.abs(np.abs(N[:, 0]) - 0.80) < 0.02), 0.2, 0)
        # bellows: rounded ribs
        for c in elbows + knees:
            m = np.linalg.norm(P - c, axis=1) < 0.075
            h += np.where(m, 0.22 * np.cos(np.pi * (z - c[2]) / 0.014), 0)
        # wrist lock and boot top: a raised ring
        for wp, wd in wrists:
            s = (P - wp) @ wd; side = (ax > 0.25) & (z > 0.75)
            h += np.where(side & (s > -0.028) & (s <= -0.005), 0.2, 0)
        h += np.where((z < 0.18) & (z > 0.165), 0.2, 0)
        return np.repeat(np.clip(h, 0, 1)[:, None], 3, 1)
    return height

def normal_map(hgt, strength=12.0):
    """height (rows = v in Blender, bottom first) -> tangent-space normal map, +u = red, +v(D3D, down the file) = green"""
    gy, gx = np.gradient(hgt)
    n = np.stack([-strength * gx, strength * gy, np.ones_like(hgt)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5

def paint_uv(obj, pattern, mat_index=0):
    me = obj.data; me.calc_loop_triangles(); uv = me.uv_layers.active.data
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
        m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
        if not m.any(): continue
        W = np.stack([w0[m], w1[m], w2[m]], 1)
        P = W @ p; Nn = W @ n; Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-9
        iy, ix = (gy[m] - 0.5).astype(int), (gx[m] - 0.5).astype(int)
        img[iy, ix] = pattern(P, Nn); filled[iy, ix] = True
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

def text_mesh(body, size):
    cu = bpy.data.curves.new("Marking", 'FONT'); cu.body = body; cu.align_x = 'CENTER'; cu.align_y = 'CENTER'; cu.size = size
    try: cu.font = bpy.data.fonts.load(FONT, check_existing=True)
    except Exception as e: log.append("font: %s" % e)
    to = bpy.data.objects.new("MarkingTmp", cu); bpy.context.scene.collection.objects.link(to); bpy.context.view_layer.update()
    ev = to.evaluated_get(bpy.context.evaluated_depsgraph_get()); tm = ev.to_mesh()
    b = bmesh.new(); b.from_mesh(tm); ev.to_mesh_clear(); bpy.data.objects.remove(to)
    return b

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_base.blend"))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    arm.data.pose_position = 'REST'; bpy.context.view_layer.update()
    body = next(o for o in bpy.data.objects if o.type == 'MESH' and 'female1605' in o.name)
    names = {g.index: g.name for g in body.vertex_groups}
    BONES = set(arm.data.bones.keys())
    BW = lambda b: arm.matrix_world @ arm.data.bones[b].head_local

    # ================= protective garment: close fitting, dense, no anatomy =================
    suit = body.copy(); suit.data = body.data.copy(); suit.name = "SuitBody"; suit.data.name = "SuitBody"
    body.users_collection[0].objects.link(suit)
    for m in list(suit.modifiers):
        if m.type != 'ARMATURE': suit.modifiers.remove(m)
    suit.data.materials.clear()
    bm = bmesh.new(); bm.from_mesh(suit.data); bm.verts.ensure_lookup_table()
    deform = bm.verts.layers.deform.active
    HEADG = {'Head', 'Neck1'}
    def dom(v):
        d = {k: w for k, w in v[deform].items() if names[k] in BONES}; return names[max(d, key=d.get)] if d else None
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if dom(v) in HEADG or v.co.z > 1.485], context='VERTS')
    # the layered suit does not follow the bust or the buttocks: pull them in, then relax the torso surface
    for v in bm.verts:
        if 1.10 < v.co.z < 1.42 and abs(v.co.x) < 0.19 and v.co.y < -0.06:
            yr = -0.075 - 0.025 * max(0.0, 1.30 - v.co.z)
            if v.co.y < yr: v.co.y = yr + (v.co.y - yr) * 0.35
        if 0.80 < v.co.z < 1.02 and v.co.y > 0.04:
            v.co.y = 0.04 + (v.co.y - 0.04) * 0.6
    torso = [v for v in bm.verts if 0.97 < v.co.z < 1.46 and abs(v.co.x) < 0.25]
    for it in range(8):                                   # Taubin: smooths detail without shrinking the torso
        k = 0.5 if it % 2 == 0 else -0.53
        new = {}
        for v in torso:
            nb = [e.other_vert(v).co for e in v.link_edges]
            if nb: new[v] = v.co + (sum(nb, Vector()) / len(nb) - v.co) * k
        for v, c in new.items(): v.co = c
    bm.normal_update()
    for v in bm.verts:
        n = dom(v) or ''
        if 'Hand' in n or 'Thumb' in n or 'Finger' in n: off = 0.009
        elif 'Foot' in n or 'Toe' in n: off = 0.018
        elif any(k in n for k in ('Arm', 'Leg')): off = 0.021
        else: off = 0.024
        v.co += v.normal * off
    bm.normal_update()
    bm.to_mesh(suit.data); bm.free()
    for p in suit.data.polygons: p.use_smooth = True

    wrists = [(np.array(BW(s + 'Hand')), np.array((BW(s + 'Hand') - BW(s + 'ForeArm')).normalized())) for s in ('Left', 'Right')]
    elbows = [np.array(BW(s + 'ForeArm')) for s in ('Left', 'Right')]
    knees = [np.array(BW(s + 'Leg')) + np.array((0, -0.02, 0)) for s in ('Left', 'Right')]
    fabric = make_image("suit_diffuse", paint_uv(suit, pattern_factory(elbows, wrists, knees)))
    # relief -> normal map (D3D9Client picks up <texture>_norm.dds next to the diffuse texture)
    arm_dir = {s: (BW(s + 'Hand') - BW(s + 'Arm')).normalized() for s in ('Left', 'Right')}
    joints = []
    for s in ('Left', 'Right'):
        fa = (BW(s + 'Hand') - BW(s + 'ForeArm')).normalized()
        joints += [(np.array(BW(s + 'ForeArm')), np.array(fa), 0.10, 0.026, 0.22),                     # elbow
                   (np.array(BW(s + 'Arm')), np.array(arm_dir[s]), 0.09, 0.030, 0.16),                 # shoulder / armpit
                   (np.array(BW(s + 'Leg')), np.array((0, 0, 1.0)), 0.11, 0.030, 0.22),               # knee
                   (np.array(BW(s + 'UpLeg')), np.array((0, 0, 1.0)), 0.12, 0.034, 0.14),             # hip / crotch
                   (np.array(BW(s + 'Foot')) + np.array((0, 0, 0.12)), np.array((0, 0, 1.0)), 0.07, 0.022, 0.16)]   # ankle
    joints.append((np.array((0, -0.02, 1.06)), np.array((0, 0, 1.0)), 0.16, 0.036, 0.12))            # waist
    hgt = paint_uv(suit, height_factory(elbows, wrists, knees, joints))[:, :, 0]
    nmap = normal_map(hgt); make_image("suit_diffuse_norm", nmap)
    gl = nmap.copy(); gl[:, :, 1] = 1 - gl[:, :, 1]                     # Blender preview wants OpenGL green
    prev = make_image("suit_preview_norm_gl", gl); prev.colorspace_settings.name = 'Non-Color'
    SUIT_NORMAL_PREVIEW = prev
    fm = material("SuitFabric", image=fabric, rough=0.85); suit.data.materials.append(fm)
    nt = fm.node_tree; bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    ti = nt.nodes.new("ShaderNodeTexImage"); ti.image = SUIT_NORMAL_PREVIEW; nm_ = nt.nodes.new("ShaderNodeNormalMap"); nm_.inputs["Strength"].default_value = 1.0
    nt.links.new(ti.outputs[0], nm_.inputs["Color"]); nt.links.new(nm_.outputs[0], bsdf.inputs["Normal"])
    log.append("garment verts %d tris %d" % (len(suit.data.vertices), sum(len(p.vertices) - 2 for p in suit.data.polygons)))

    # garment surface (rest pose) for fitting, and its skin weights for the hard parts
    sbm = bmesh.new(); sbm.from_mesh(suit.data); sbm.transform(suit.matrix_world); bvh = BVHTree.FromBMesh(sbm)
    gname = {g.index: g.name for g in suit.vertex_groups}
    kd = KDTree(len(suit.data.vertices))
    for i, v in enumerate(suit.data.vertices): kd.insert(v.co, i)
    kd.balance()
    def skin_weights(co, k=4):
        acc = {}
        for _, i, d in kd.find_n(co, k):
            for g in suit.data.vertices[i].groups:
                n = gname[g.group]
                if n in BONES: acc[n] = acc.get(n, 0) + g.weight / (d + 0.01)
        top = sorted(acc.items(), key=lambda t: -t[1])[:4]; s = sum(w for _, w in top) or 1
        return {n: w / s for n, w in top}
    def surface_down(x, y, default):
        hit = bvh.ray_cast(Vector((x, y, 2.0)), Vector((0, 0, -1)))
        return hit[0].z if hit[0] else default
    def surface_along(origin, direction, default=None):
        hit = bvh.ray_cast(origin, direction)
        return hit[0] if hit[0] else default

    # ---- hard plate projected onto the garment: rounded outline, chamfered edge, closed solid ----
    def plate(name, origin, U, V, hu, hv, D, off, th, mat, n=16, rk=0.4, weights=None, tree=None, window=None):
        tree = tree or bvh
        grid, depth = [], []
        for i in range(n + 1):
            row = []
            for j in range(n + 1):
                s, t = -1 + 2 * i / n, -1 + 2 * j / n
                a = s + (s * math.sqrt(1 - t * t / 2) - s) * rk; c = t + (t * math.sqrt(1 - s * s / 2) - t) * rk
                p0 = origin + U * (a * hu) + V * (c * hv); hit = tree.ray_cast(p0, D)
                dd = (hit[0] - p0).dot(D) if hit[0] else None
                if dd is not None and window and not (window[0] <= dd <= window[1]): dd = None
                row.append((a, c, p0, dd))
            grid.append(row)
        # a smooth quadratic cap fitted to the hits: plates are moulded, they do not copy every facet of the garment
        pts = [(r[0], r[1], r[3]) for row in grid for r in row if r[3] is not None]
        A = np.array([[1, a, c, a * a, a * c, c * c] for a, c, _ in pts]); y = np.array([d for _, _, d in pts])
        coef = np.linalg.lstsq(A, y, rcond=None)[0]
        dep = [[float(np.dot(coef, [1, r[0], r[1], r[0] ** 2, r[0] * r[1], r[1] ** 2])) for r in row] for row in grid]
        if len(pts) < 8: log.append("plate %s: only %d hits" % (name, len(pts)))
        b = bmesh.new()
        I = [[b.verts.new(grid[i][j][2] + D * (dep[i][j] - off)) for j in range(n + 1)] for i in range(n + 1)]
        O = [[b.verts.new(origin + U * (grid[i][j][0] * hu * 0.93) + V * (grid[i][j][1] * hv * 0.93) + D * (dep[i][j] - off - th)) for j in range(n + 1)] for i in range(n + 1)]
        for i in range(n):
            for j in range(n):
                b.faces.new((O[i][j], O[i + 1][j], O[i + 1][j + 1], O[i][j + 1])).material_index = 0
                b.faces.new((I[i][j], I[i][j + 1], I[i + 1][j + 1], I[i + 1][j])).material_index = 0
        loop = [(i, 0) for i in range(n)] + [(n, j) for j in range(n)] + [(i, n) for i in range(n, 0, -1)] + [(0, j) for j in range(n, 0, -1)]
        for k in range(len(loop)):
            (i0, j0), (i1, j1) = loop[k], loop[(k + 1) % len(loop)]
            b.faces.new((I[i0][j0], I[i1][j1], O[i1][j1], O[i0][j0])).material_index = 0
        bmesh.ops.recalc_face_normals(b, faces=b.faces[:])
        o = new_object(name, b, [mat], weights or skin_weights, arm)
        # keep the rim sharp, the faces smooth
        return o

    plate_mat = material("SuitPlate", PLATE, rough=0.35); pad_mat = material("SuitPad", PAD, rough=0.5)
    # chest cuirass (Interstellar-like front plate, carries the marking)
    chest = plate("Cuirass", Vector((0, -1.0, 1.235)), Vector((1, 0, 0)), Vector((0, 0, 1)), 0.150, 0.200, Vector((0, 1, 0)), 0.004, 0.024, plate_mat)
    # shoulder blocks: the frame bears on them from the pack
    for s, sx in (('L', 1), ('R', -1)):
        plate("Shoulder" + s, Vector((sx * 0.140, -0.015, 1.70)), Vector((1, 0, 0)), Vector((0, 1, 0)), 0.060, 0.085, Vector((0, 0, -1)), 0.004, 0.034, plate_mat, n=12, window=(0.20, 0.40))
    # knee pads
    for s, sx in (('L', 1), ('R', -1)):
        kb = BW(('Left' if sx > 0 else 'Right') + 'Leg')
        plate("KneePad" + s, Vector((kb.x, -1.0, kb.z + 0.005)), Vector((1, 0, 0)), Vector((0, 0, 1)), 0.050, 0.068, Vector((0, 1, 0)), 0.003, 0.018, pad_mat, n=10)

    # chest marking on the cuirass
    cbm = bmesh.new(); cbm.from_mesh(chest.data); cbvh = BVHTree.FromBMesh(cbm)
    b = text_mesh("37 ЗВЁЗДНАЯ", 0.026)
    for v in b.verts:
        x, z = v.co.x, 1.37 + v.co.y
        hit = cbvh.ray_cast(Vector((x, -1.0, z)), Vector((0, 1, 0)))
        v.co = Vector((x, (hit[0].y if hit[0] else -0.15) - 0.0015, z))
    bmesh.ops.triangulate(b, faces=b.faces[:])
    for f in b.faces:
        f.normal_update()
        if f.normal.y > 0: f.normal_flip()
    new_object("MarkChest", b, [material("MarkingRed", RED, rough=0.5)], skin_weights, arm, smooth=False)

    # ================= helmet: shell with a framed, recessed visor, neck ring, red crest; liner; visor =================
    PHV, T1, T2, RIM = 1.02, -0.60, 0.40, 0.14          # visor half-width (rad), lower/upper elevation (rad), frame width (rad)
    TC = math.asin((CUT_Z - HC.z) / HR.z)
    phis = sorted(set([round(-math.pi + 2 * math.pi * k / 72, 6) for k in range(72)] + [-PHV, PHV, -PHV - RIM, PHV + RIM]))
    thetas = sorted(set([round(TC + (math.pi / 2 - 0.05 - TC) * k / 28, 6) for k in range(29)] + [T1, T2, T1 - RIM, T2 + RIM, TC + 0.10]))
    def hp(phi, th, r=1.0):
        return Vector((HC.x + HR.x * r * math.sin(phi) * math.cos(th), HC.y - HR.y * r * math.cos(phi) * math.cos(th), HC.z + HR.z * r * math.sin(th)))
    def in_visor(pm, tm): return abs(pm) < PHV and T1 < tm < T2
    def in_frame(pm, tm): return abs(pm) < PHV + RIM and T1 - RIM < tm < T2 + RIM and not in_visor(pm, tm)
    def build_helmet(select, radius, lift, mats_of):
        b = bmesh.new(); vid = {}
        def vert(i, j):
            i %= len(phis)
            if (i, j) not in vid:
                phi, th = phis[i % len(phis)], thetas[j]
                vid[(i, j)] = b.verts.new(hp(phi, th, radius + lift(phi, th)))
            return vid[(i, j)]
        top = None
        for i in range(len(phis)):
            for j in range(len(thetas)):
                p0, p1 = phis[i], phis[(i + 1) % len(phis)] + (2 * math.pi if i + 1 == len(phis) else 0)
                pm = (p0 + p1) / 2; pm = (pm + math.pi) % (2 * math.pi) - math.pi
                if j + 1 < len(thetas):
                    tm = (thetas[j] + thetas[j + 1]) / 2
                    if not select(pm, tm): continue
                    f = b.faces.new((vert(i, j), vert(i + 1, j), vert(i + 1, j + 1), vert(i, j + 1))); f.material_index = mats_of(pm, tm)
                elif select(pm, math.pi / 2):
                    if top is None: top = b.verts.new(hp(0, math.pi / 2, radius))
                    f = b.faces.new((vert(i, j), vert(i + 1, j), top)); f.material_index = mats_of(pm, math.pi / 2)
        bmesh.ops.recalc_face_normals(b, faces=b.faces[:])
        return b
    step = 2 * math.pi / 72
    def shell_mat(pm, tm):
        if in_frame(pm, tm): return 2
        if tm < TC + 0.10: return 3
        return 0
    def shell_lift(phi, th):
        w = 0.0
        if abs(phi) <= PHV + RIM + 1e-6 and T1 - RIM - 1e-6 <= th <= T2 + RIM + 1e-6: w = 0.065     # the frame stands proud
        if th <= TC + 0.10 + 1e-6: w = max(w, 0.05)                                                 # neck ring
        return w
    b = build_helmet(lambda pm, tm: not in_visor(pm, tm), 1.0, shell_lift, shell_mat)
    # frame outline verts on the visor edge sit at the shell radius (the visor is recessed below them)
    SPINE1 = {'Spine1': 1.0}
    new_object("Helmet", b, [material("HelmetShell", SHELL, rough=0.3), material("HelmCrest", RED, rough=0.45),
                             material("HelmFrame", PAD, rough=0.4), material("HelmRing", METAL, rough=0.3, metal=0.5)], SPINE1, arm)
    b = build_helmet(lambda pm, tm: not in_visor(pm, tm), 0.955, lambda p, t: 0.0, lambda pm, tm: 0)
    bmesh.ops.reverse_faces(b, faces=b.faces[:])
    new_object("HelmLiner", b, [material("HelmetLiner", DARK, rough=0.9)], SPINE1, arm)
    b = build_helmet(lambda pm, tm: in_visor(pm, tm), 0.99, lambda p, t: 0.0, lambda pm, tm: 0)
    new_object("Visor", b, [material("Visor", (0.62, 0.76, 0.86), rough=0.05, alpha=0.22)], SPINE1, arm)
    # sun shade: an outer tinted visor, parked half raised
    b = build_helmet(lambda pm, tm: abs(pm) < PHV + 0.05 and T2 - 0.16 < tm < T2 + RIM + 0.24, 1.10, lambda p, t: 0.0, lambda pm, tm: 0)
    new_object("SunShade", b, [material("SunShade", (0.55, 0.40, 0.12), rough=0.05, alpha=0.55, metal=0.6)], SPINE1, arm)

    hmetal = material("HelmMetal", METAL, rough=0.3, metal=0.6); hdark = material("HelmDark", DARK, rough=0.6)
    hred = material("HelmRed", RED, rough=0.45); lamp = material("HelmLamp", (0.97, 0.96, 0.88), rough=0.1)
    # crown rib: a raised spine from the visor frame over the top to the back, with a red inlay
    def sweep(path, w, h, lift):
        rings = []
        for k, P in enumerate(path):
            T = (path[min(k + 1, len(path) - 1)] - path[max(k - 1, 0)]).normalized()
            n = (P - HC); n = Vector((n.x / HR.x, n.y / HR.y, n.z / HR.z)).normalized()
            sd = T.cross(n).normalized(); base = P + n * lift
            rings.append([base - sd * w, base + sd * w, base + sd * (w * 0.75) + n * h, base - sd * (w * 0.75) + n * h])
        b = bmesh.new(); ring_loft(b, rings, closed=False)
        bmesh.ops.contextual_create(b, geom=[v for v in b.verts][:4]); bmesh.ops.contextual_create(b, geom=[v for v in b.verts][-4:])
        bmesh.ops.recalc_face_normals(b, faces=b.faces[:]); return b
    path = [hp(0, th, 1.0) for th in np.linspace(T2 + RIM + 0.02, math.pi / 2 - 0.02, 12)] + [hp(math.pi, th, 1.0) for th in np.linspace(math.pi / 2 - 0.02, -0.25, 14)]
    new_object("HelmRib", sweep(path, 0.013, 0.009, 0.0), [material("HelmRib", PAD, rough=0.35)], SPINE1, arm)
    new_object("HelmRibRed", sweep(path, 0.005, 0.002, 0.0095), [hred], SPINE1, arm)
    # side pods: lamp and camera housings over the temples
    b = bmesh.new()
    for sg in (-1, 1):
        c = hp(sg * 1.50, 0.02, 1.0) + Vector((sg * 0.020, 0, 0))
        rounded_box(b, c, Vector((0.030, 0.085, 0.052)), 0.008)
        cylinder(b, c + Vector((0, -0.040, 0.008)), c + Vector((0, -0.050, 0.008)), 0.012, 16, 1)     # lamp lens
        cylinder(b, c + Vector((0, -0.040, -0.014)), c + Vector((0, -0.047, -0.014)), 0.006, 12, 2)   # camera
    new_object("HelmPods", b, [material("HelmPod", PAD, rough=0.35), lamp, hdark], SPINE1, arm)
    # neck ring bolts and the comm unit with its antenna at the back
    b = bmesh.new()
    for k in range(18):
        ph = -math.pi + 2 * math.pi * (k + 0.5) / 18
        if abs(ph) < PHV: continue
        p = hp(ph, TC + 0.05, 1.05); n = (p - HC); n.z = 0; n.normalize()
        cylinder(b, p - n * 0.002, p + n * 0.004, 0.004, 6, 0)
    c = hp(math.pi, -0.30, 1.0) + Vector((0, 0.022, 0))
    rounded_box(b, c, Vector((0.075, 0.032, 0.048)), 0.007, 1)
    cylinder(b, c + Vector((0.025, 0.004, 0.02)), c + Vector((0.032, 0.012, 0.085)), 0.0028, 6, 0)
    new_object("HelmHardware", b, [hmetal, hdark], SPINE1, arm)

    # ================= dark soft neck seal: from the helmet ring down onto the shoulders =================
    N = 48; ring_scale = math.cos(TC)
    rix, riy = HR.x * ring_scale * 1.05, HR.y * ring_scale * 1.05
    rings = []
    for dz, sc in ((0.004, 1.0), (-0.020, 1.14), (-0.045, 1.22)):
        ring = []
        for j in range(N):
            t = 2 * math.pi * j / N; cx, cy = math.sin(t), -math.cos(t)
            x, y = HC.x + rix * sc * cx, HC.y + riy * sc * cy
            zs = surface_down(x, y, CUT_Z + dz) + 0.006
            z = CUT_Z + dz if dz > -0.04 or not (CUT_Z - 0.10 < zs < CUT_Z) else min(CUT_Z + dz, zs)
            ring.append((x, y, z))
        rings.append(ring)
    b = bmesh.new(); ring_loft(b, rings, closed=False); bmesh.ops.recalc_face_normals(b, faces=b.faces[:])
    for f in b.faces:
        f.normal_update()
        c = f.calc_center_median()
        if f.normal.dot(Vector((c.x - HC.x, c.y - HC.y, 0))) < 0: f.normal_flip()
    new_object("NeckSeal", b, [material("NeckSeal", DARK, rough=0.8)], lambda co: {'Spine1': 0.8, 'Neck': 0.2}, arm)

    # ================= life-support pack on the frame =================
    back_y = max((surface_along(Vector((x, 1.0, z)), Vector((0, -1, 0)), Vector((0, 0.10, 0))).y for x in (-0.1, 0, 0.1) for z in (1.15, 1.25, 1.35, 1.42)))
    PW, PH, PD = 0.37, 0.44, 0.13; pz0, pz1 = 1.06, 1.06 + PH; py0 = back_y + 0.018
    b = bmesh.new(); bmesh.ops.create_cube(b, size=1.0)
    for v in b.verts: v.co = Vector((v.co.x * PW, py0 + (v.co.y + 0.5) * PD, pz0 + (v.co.z + 0.5) * PH))
    bmesh.ops.bevel(b, geom=b.edges[:], offset=0.025, segments=3, profile=0.5, affect='EDGES')
    for sx in (-1, 1):
        for zz in (pz0 + 0.035, pz1 - 0.035):
            cylinder(b, Vector((sx * (PW / 2 - 0.005), py0 + PD * 0.55, zz)), Vector((sx * (PW / 2 + 0.035), py0 + PD * 0.55, zz)), 0.022, 12, 1)
    new_object("Pack", b, [material("Pack", SHELL, rough=0.4), material("PackRCS", DARK, rough=0.6)], {'Spine1': 0.85, 'Spine': 0.15}, arm)
    b = text_mesh("37 ЗВЁЗДНАЯ", 0.044)
    ty = py0 + PD + 0.0015; tz = pz0 + PH * 0.80
    for v in b.verts: v.co = Vector((-v.co.x, ty, tz + v.co.y))
    bmesh.ops.triangulate(b, faces=b.faces[:])
    for f in b.faces:
        f.normal_update()
        if f.normal.y < 0: f.normal_flip()
    new_object("MarkPack", b, [material("MarkingRed2", RED, rough=0.5)], {'Spine1': 0.85, 'Spine': 0.15}, arm, smooth=False)

    # ================= load-bearing frame =================
    frame = material("Frame", FRAME, rough=0.35, metal=0.55); red = material("DriveRed", RED, rough=0.4)
    # hip ring: carries the pack and the leg struts
    zb = 1.00; band = [Vector(v.co) for v in suit.data.vertices if abs(v.co.z - zb) < 0.035 and abs(v.co.x) < 0.26]
    def radius_at(t, pts, cx=0.0, cy=-0.02):
        best = 0.10
        for p in pts:
            a = math.atan2(p.x - cx, -(p.y - cy)); da = abs((a - t + math.pi) % (2 * math.pi) - math.pi)
            if da < 0.14: best = max(best, math.hypot(p.x - cx, p.y - cy))
        return best
    rr = [radius_at(2 * math.pi * j / N, band) for j in range(N)]
    for _ in range(3): rr = [(rr[j - 1] + 2 * rr[j] + rr[(j + 1) % N]) / 4 for j in range(N)]
    rings = []
    for dz, dr in ((-0.028, 0.008), (-0.028, 0.032), (0.028, 0.032), (0.028, 0.008)):
        rings.append([(math.sin(2 * math.pi * j / N) * (rr[j] + dr), -0.02 - math.cos(2 * math.pi * j / N) * (rr[j] + dr), zb + dz) for j in range(N)])
    b = bmesh.new(); ring_loft(b, rings); bmesh.ops.recalc_face_normals(b, faces=b.faces[:])
    new_object("HipRing", b, [frame], {'Hips': 1.0}, arm)
    back_ring = -0.02 + rr[N // 2] + 0.026
    # back rails: pack to hip ring, so the pack weight goes to the pelvis, not the shoulders
    b = bmesh.new()
    for sx in (-1, 1):
        cylinder(b, Vector((sx * 0.11, max(back_ring, py0 + 0.02), zb + 0.02)), Vector((sx * 0.11, py0 + 0.03, pz0 + 0.06)), 0.016, 6, 0, math.pi / 6)
    new_object("BackRails", b, [frame], lambda co: {'Spine': 0.5, 'Hips': 0.5} if co.z < 1.03 else {'Spine': 0.6, 'Spine1': 0.4}, arm)
    # shoulder arcs: pack top over the shoulder blocks
    b = bmesh.new()
    for sx in (-1, 1):
        top = surface_down(sx * 0.14, -0.015, 1.42) + 0.040
        pts = [Vector((sx * 0.12, py0 + 0.03, pz1 - 0.04)), Vector((sx * 0.135, 0.05, top + 0.005)), Vector((sx * 0.14, -0.015, top + 0.002))]
        for a_, b_ in zip(pts, pts[1:]): cylinder(b, a_, b_, 0.014, 6, 0, math.pi / 6)
    new_object("ShoulderArcs", b, [frame], lambda co: {'Spine1': 1.0}, arm)

    # legs: hip drive, thigh beam, knee drive, shin beam, ankle stirrup
    def outer(sx, y, z, extra):
        h = surface_along(Vector((sx * 1.0, y, z)), Vector((-sx, 0, 0)))
        return sx * (abs(h.x) + extra) if h else None
    legs = []
    for s, sx in (('Left', 1), ('Right', -1)):
        hb, kb, fb = BW(s + 'UpLeg'), BW(s + 'Leg'), BW(s + 'Foot')
        hip = Vector((outer(sx, hb.y, hb.z, 0.028) or sx * (abs(hb.x) + 0.10), hb.y - 0.005, hb.z))
        knee = Vector((outer(sx, kb.y, kb.z, 0.024) or sx * (abs(kb.x) + 0.08), kb.y + 0.004, kb.z))
        az = fb.z + 0.06
        ankle = Vector((outer(sx, fb.y + 0.01, az, 0.018) or sx * (abs(fb.x) + 0.05), fb.y + 0.01, az))
        legs.append((s, sx, hip, knee, ankle))
    housing = material("Housing", FRAME, rough=0.35, metal=0.6); hubm = material("Hub", DARK, rough=0.5)
    boltm = material("Bolt", METAL, rough=0.3, metal=0.7); strap = material("Strap", DARK, rough=0.7)
    AM = (0, 1, 2, 3)
    def cuff(b, top, bot, t, sx, reach=2.0):
        """band round the limb at fraction t between two joints, from the garment cross-section; open on the inner side"""
        zc = top.z + (bot.z - top.z) * t
        pts = [Vector(v.co) for v in suit.data.vertices if abs(v.co.z - zc) < 0.04 and v.co.x * sx > 0.03 and abs(v.co.x) < 0.30]
        cx = sum(p.x for p in pts) / len(pts); cy = sum(p.y for p in pts) / len(pts)
        def rad(ang):
            d = Vector((sx * math.cos(ang), -math.sin(ang), 0))
            hit = bvh.ray_cast(Vector((cx, cy, zc)), d, 0.25)          # from inside the leg outwards: the first wall is its surface
            return (hit[0] - Vector((cx, cy, zc))).length if hit[0] else 0.07
        angs = np.linspace(-reach, reach, 21); rows = []
        for ang in angs:
            dx, dy = sx * math.cos(ang), -math.sin(ang); r = rad(ang)
            P = lambda rr, dz: Vector((cx + dx * rr, cy + dy * rr, zc + dz))
            rows.append([P(r + 0.002, -0.018), P(r + 0.013, -0.018), P(r + 0.013, 0.018), P(r + 0.002, 0.018)])
        V = [[b.verts.new(p) for p in row] for row in rows]
        for i in range(len(V) - 1):
            for j in range(4): b.faces.new((V[i][j], V[i + 1][j], V[i + 1][(j + 1) % 4], V[i][(j + 1) % 4])).material_index = 0
        b.faces.new(V[0]).material_index = 0; b.faces.new(V[-1][::-1]).material_index = 0
        bmesh.ops.recalc_face_normals(b, faces=b.faces[:])
    for s, sx, hip, knee, ankle in legs:
        L = s[0]; out = Vector((sx, 0, 0)); back = Vector((0, 1, 0))
        # hip: drive on the pelvis side, a plate up to the hip ring
        b = bmesh.new()
        actuator(b, hip, out, 0.050, 0.036, back, AM)
        box_beam(b, hip + Vector((0, 0, 0.03)), Vector((hip.x, hip.y + 0.01, zb + 0.02)), 0.050, 0.012, out, 0)
        new_object("HipDrive" + L, b, [housing, red, hubm, boltm], {'Hips': 1.0}, arm)
        # thigh link: flat bar with a raised rib, a cuff round the thigh
        b = bmesh.new()
        box_beam(b, hip + Vector((0, 0, -0.02)), knee + Vector((0, 0, 0.02)), 0.034, 0.012, out, 0)
        box_beam(b, hip + Vector((0, 0, -0.05)) + out * 0.008, knee + Vector((0, 0, 0.05)) + out * 0.008, 0.012, 0.008, out, 1)
        new_object("ThighLink" + L, b, [frame, housing], {s + 'UpLeg': 1.0}, arm)
        b = bmesh.new(); cuff(b, hip, knee, 0.45, sx); new_object("ThighCuff" + L, b, [strap], {s + 'UpLeg': 1.0}, arm)
        # knee: drive rides the thigh link, the shin link turns on its hub
        b = bmesh.new(); actuator(b, knee, out, 0.042, 0.030, back, AM)
        new_object("KneeDrive" + L, b, [housing, red, hubm, boltm], {s + 'UpLeg': 1.0}, arm)
        b = bmesh.new()
        box_beam(b, knee + Vector((0, 0, -0.02)), ankle + Vector((0, 0, 0.015)), 0.030, 0.011, out, 0)
        box_beam(b, knee + Vector((0, 0, -0.05)) + out * 0.007, ankle + Vector((0, 0, 0.05)) + out * 0.007, 0.010, 0.007, out, 1)
        new_object("ShinLink" + L, b, [frame, housing], {s + 'Leg': 1.0}, arm)
        b = bmesh.new(); cuff(b, knee, ankle, 0.40, sx); new_object("ShinCuff" + L, b, [strap], {s + 'Leg': 1.0}, arm)
        # ankle: passive hinge and a stirrup plate along the outer edge of the sole
        b = bmesh.new()
        cylinder(b, ankle - out * 0.008, ankle + out * 0.010, 0.026, 20, 0)
        cylinder(b, ankle + out * 0.010, ankle + out * 0.014, 0.012, 12, 1)
        sole = Vector((ankle.x - sx * 0.010, BW(s + 'Foot').y + 0.03, 0.026))
        box_beam(b, ankle + Vector((0, 0.004, -0.018)), sole, 0.022, 0.009, out, 0)
        box_beam(b, sole + Vector((0, 0.035, 0)), sole - Vector((0, 0.10, 0)), 0.020, 0.008, out, 0)
        new_object("Stirrup" + L, b, [housing, hubm], {s + 'Foot': 1.0}, arm)
        # power and data: a cable from the pack to each hip drive
        b = bmesh.new()
        pts = [Vector((sx * 0.15, py0 + 0.03, pz0 + 0.02)), Vector((sx * 0.20, max(back_ring, py0) - 0.01, zb + 0.05)), hip + Vector((0, 0.035, 0.035))]
        for a_, c_ in zip(pts, pts[1:]): cylinder(b, a_, c_, 0.0065, 8, 0)
        new_object("Cable" + L, b, [strap], lambda co: {'Spine': 0.7, 'Hips': 0.3} if co.z > zb + 0.03 else {'Hips': 1.0}, arm)
    log.append("legs: " + "; ".join("%s hip %s knee %s ankle %s" % (s, tuple(round(c, 3) for c in h), tuple(round(c, 3) for c in k), tuple(round(c, 3) for c in a)) for s, _, h, k, a in legs))

    # ================= hide what the suit covers: keep only the head (face behind the visor) =================
    keep = body.vertex_groups.new(name="covered_by_suit")
    idx = [v.index for v in body.data.vertices if not (v.groups and names[max(v.groups, key=lambda g: g.weight).group] in HEADG) and v.co.z < 1.50]
    keep.add(idx, 1.0, 'REPLACE')
    mk = body.modifiers.new("HideUnderSuit", 'MASK'); mk.vertex_group = keep.name; mk.invert_vertex_group = True

    # red share (by surface area) for the <= 20 % rule
    area = {}
    for o in [o for o in bpy.data.objects if o.type == 'MESH' and o.name not in ("Floor",) and not o.name.startswith("Astronavigator")]:
        for p in o.data.polygons:
            mn = o.material_slots[p.material_index].material.name if o.material_slots else ""
            area[mn] = area.get(mn, 0) + p.area
    tot = sum(v for k, v in area.items() if k not in ("HelmetLiner",)); redA = sum(v for k, v in area.items() if "Red" in k or "Crest" in k)
    log.append("surface %.3f m2, red %.1f %%" % (tot, 100 * redA / tot))

    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_suit.blend"))
    log.append("saved astronavigator_suit.blend")

    import sys; sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage()
    render_util.shoot(cam, OUT, render_util.standard_shots("suit", 1.84) + [("suit_side", (-4.2, -0.4, 1.1), 0.95, 70), ("suit_helmet", (0.5, -1.2, 1.62), 1.55, 60)], log)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
