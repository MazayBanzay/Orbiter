# Tantra: the astronavigator's flight suit (2026-10-02, replaces the skin-tight coverall of build_coverall.py).
# The garment is a ready-made MakeHuman Community asset fitted to her body by its .mhclo (elvs_male_coveralls_1 by
# Elvaerwyn, CC-BY: credit in the README) - a loose coverall cut with its own collar, flapped chest pockets and folds in
# the geometry. Here it becomes a flight suit: a clean fabric of our colour painted in the body's 3D space (front zip,
# thigh pockets, a pen pocket on the left sleeve), a relief map for those details, and the skin hidden under it.
# Input: astronavigator_coverall.blend (the prepared body: build_coverall.py's body_extras already applied).
# Output: the same .blend (the old Coverall object replaced), textures/coverall_diffuse(_norm).png, renders/fs_*.png.
import bpy, bmesh, os, sys, math, traceback
import numpy as np
from mathutils import Vector

HERE = os.path.dirname(__file__); sys.path.insert(0, HERE)
TEX_DIR = os.path.join(HERE, "textures"); OUT = os.path.join(HERE, "renders")
LOG = os.path.join(HERE, "build_flightsuit.log"); log = []
ASSET = "elvs_male_coveralls_1"
PALETTES = {"light": ((0.86, 0.85, 0.82), (0.40, 0.46, 0.54)),   # light + steel blue-grey (Soviet aviation) "sage": ((0.50, 0.53, 0.44), (0.30, 0.32, 0.27)),
            "navy": ((0.19, 0.22, 0.31), (0.12, 0.14, 0.20))}
PAL = os.environ.get("COV_PALETTE", "light")
FABRIC, TRIM = (np.array(c) for c in PALETTES[PAL]); ZIP = np.array([0.15, 0.16, 0.18])
TEX = 2048; SS = ((0.25, 0.25), (0.75, 0.25), (0.25, 0.75), (0.75, 0.75))
RENDER_ONLY = os.environ.get("FS_RENDER_ONLY") == "1"
HEM_Z = 0.085  # m: the trouser hem, just over the boot's top
POCKET_LIFT = 0.012   # m: a pocket layer stands this much further off the body than the cloth around it
ASSET_NORMAL = 1.0   # weight of the asset's normal map in ours     # palette previews: no save

def smooth01(e0, e1, x): t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def regions(P, N):
    """user 2026-10-02: no pockets; colours 1 + 3 (steel blue-grey + light): light cloth, steel on the shoulder yoke,
    collar, cuffs and waist band, steel zip tape. Far-future tech heritage of the USSR and China: clean, no decoration."""
    x, y, z = P[:, 0], P[:, 1], P[:, 2]; ax = np.abs(x); nx, ny, nz = N[:, 0], N[:, 1], N[:, 2]
    front = ny < -0.2
    R = {}
    R["zip"] = (ax < 0.006) & front & (y < 0) & (z > 0.72) & (z < 1.44)
    R["ziptape"] = (ax < 0.016) & front & (y < 0) & (z > 0.72) & (z < 1.44)
    R["collar"] = z > 1.445
    R["yoke"] = (z > 1.33) & (nz > 0.5) & (ax > 0.07) & (ax < 0.24)
    R["cuff"] = (ax > 0.43) & (z > 0.85)
    R["band"] = (np.abs(z - 1.03) < 0.024) & (np.abs(nz) < 0.7)
    R["seam"] = np.exp(-((np.abs(np.abs(nx) - 0.6)) / 0.015) ** 2) * ((z > 0.12) & (z < 1.30))   # side seams
    return R

def pattern(P, N):
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    R = regions(P, N); col = np.tile(FABRIC, (len(P), 1))
    for k in ("collar", "yoke", "cuff", "band", "ziptape"): col[R[k]] = TRIM
    col = col * (1 - 0.08 * np.clip(R["seam"], 0, 1))[:, None]
    col[R["zip"]] = ZIP; teeth = R["zip"] & ((np.floor(z / 0.004) % 2) == 0) & (np.abs(x) < 0.003); col[teeth] = ZIP * 1.8
    mott = np.sin(x * 61 + 1.3) * np.sin(y * 47 + 0.4) * np.sin(z * 53 + 2.1) + 0.5 * np.sin(x * 23 - z * 31)
    col = col * (1 + 0.012 * mott)[:, None]
    return np.clip(col, 0, 1)

def relief(P, N):
    z = P[:, 2]; R = regions(P, N); h = np.zeros(len(P))
    h += R["band"] * 0.0012 + R["yoke"] * 0.0005 + R["cuff"] * 0.0008 - 0.0005 * np.clip(R["seam"], 0, 1)
    h += R["ziptape"] * 0.0005 + R["zip"] * (0.0006 + 0.0006 * (np.floor(z / 0.004) % 2))
    return np.stack([h, h, h], 1)

def smooth_normals(me, iters=12):
    E = np.array([e.vertices[:] for e in me.edges]); N = np.array([v.normal for v in me.vertices])
    deg = np.bincount(E.ravel(), minlength=len(N))[:, None].astype(float) + 1e-9
    for _ in range(iters):
        acc = np.zeros_like(N); np.add.at(acc, E[:, 0], N[E[:, 1]]); np.add.at(acc, E[:, 1], N[E[:, 0]])
        N = 0.5 * N + 0.5 * acc / deg; N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-9
    return N

def paint_uv(obj, fn, normals=None):
    me = obj.data; me.calc_loop_triangles(); uv = me.uv_layers.active.data
    M = obj.matrix_world; N3 = M.to_3x3().inverted().transposed()
    acc = np.zeros((TEX, TEX, 3), np.float32); cnt = np.zeros((TEX, TEX), np.float32)
    V = np.array([M @ v.co for v in me.vertices]); VN0 = normals if normals is not None else np.array([v.normal for v in me.vertices])
    VN = np.array([(N3 @ Vector(n)).normalized() for n in VN0])
    for tri in me.loop_triangles:
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
            m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
            if not m.any(): continue
            W = np.stack([w0[m], w1[m], w2[m]], 1)
            P = W @ p; Nn = W @ n; Nn /= np.linalg.norm(Nn, axis=1, keepdims=True) + 1e-9
            c_ = fn(P, Nn)
            iy, ix = np.floor(gy[m]).astype(int), np.floor(gx[m]).astype(int)
            acc[iy, ix] += c_; cnt[iy, ix] += 1
    filled = cnt > 0; img = np.zeros((TEX, TEX, 3)); img[filled] = acc[filled] / cnt[filled][:, None]
    for _ in range(4):
        grow = ~filled & (np.roll(filled, 1, 0) | np.roll(filled, -1, 0) | np.roll(filled, 1, 1) | np.roll(filled, -1, 1))
        src = np.where(np.roll(filled, 1, 0)[..., None], np.roll(img, 1, 0), np.where(np.roll(filled, -1, 0)[..., None], np.roll(img, -1, 0),
              np.where(np.roll(filled, 1, 1)[..., None], np.roll(img, 1, 1), np.roll(img, -1, 1))))
        img[grow] = src[grow]; filled |= grow
    return img

def make_image(name, arr, path):
    im = bpy.data.images.new(name, TEX, TEX, alpha=False)
    im.pixels.foreach_set(np.concatenate([arr, np.ones((TEX, TEX, 1))], 2).astype(np.float32).ravel())
    im.filepath_raw = path; im.file_format = 'PNG'
    if not RENDER_ONLY: im.save()
    return im

def material(name, image, rough=0.9):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial"); bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs[0]); bsdf.inputs["Roughness"].default_value = rough
    if "Specular IOR Level" in bsdf.inputs: bsdf.inputs["Specular IOR Level"].default_value = 0.25
    t = nt.nodes.new("ShaderNodeTexImage"); t.image = image; nt.links.new(t.outputs[0], bsdf.inputs["Base Color"])
    return m

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    body = next(o for o in bpy.data.objects if o.name.endswith('.body'))
    for o in [o for o in bpy.data.objects if o.name == "Coverall" or o.name.startswith("V_")]: bpy.data.objects.remove(o, do_unlink=True)
    for m in list(body.modifiers):
        if m.type == 'MASK' and m.name == "HideUnderCoverall": body.modifiers.remove(m)
    import addon_utils; addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    from bl_ext.tantra.mpfb.services.assetservice import AssetService
    before = set(bpy.data.objects)
    HumanService.add_mhclo_asset(AssetService.find_asset_absolute_path("%s/%s.mhclo" % (ASSET, ASSET), asset_subdir="clothes"),
                                 body, asset_type="Clothes", subdiv_levels=0, material_type="MAKESKIN")
    cov = next(o for o in set(bpy.data.objects) - before if o.type == 'MESH')
    cov.name = "Coverall"; cov.data.name = "Coverall"
    log.append("garment %s: %d verts, %d tris" % (ASSET, len(cov.data.vertices), sum(len(p.vertices) - 2 for p in cov.data.polygons)))
    # the skin under the garment: the asset's own delete group (what its author hid under it), as a mask on the body
    # ...together with the boots' (shoes03): one mask group for everything under garment or boots
    dels = [g for g in body.vertex_groups if g.name in ("Delete." + ASSET, "Delete.shoes03")]
    for m in list(body.modifiers):
        if m.type == 'MASK' and m.name != "Hide helpers": body.modifiers.remove(m)
    hid = body.vertex_groups.get("hidden_under_clothes") or body.vertex_groups.new(name="hidden_under_clothes")
    idx = [v.index for v in body.data.vertices if any(g.group in [d.index for d in dels] for g in v.groups)]
    hid.add(idx, 1.0, 'REPLACE')
    mk = body.modifiers.new("HideUnderCoverall", 'MASK'); mk.vertex_group = hid.name; mk.invert_vertex_group = True
    log.append("skin hidden under %s: %d verts" % ([d.name for d in dels], len(idx)))
    # the asset's legs reach the floor as shoe covers: cut them at the boot shaft, the trouser hem lies over the boot
    bm = bmesh.new(); bm.from_mesh(cov.data)
    low = [v for v in bm.verts if (cov.matrix_world @ v.co).z < HEM_Z]
    bmesh.ops.delete(bm, geom=low, context='VERTS')
    # a clean, level hem: the ragged cut's boundary verts set onto the hem height, then the hem ring relaxed in x/y
    Mi = cov.matrix_world.inverted()
    rim = [v for v in bm.verts if v.is_boundary and (cov.matrix_world @ v.co).z < HEM_Z + 0.03]
    for v in rim:
        p = cov.matrix_world @ v.co; p.z = HEM_Z; v.co = Mi @ p
    for _ in range(6):
        nw = {}
        for v in rim:
            nb = [e.other_vert(v) for e in v.link_edges if e.other_vert(v) in rim]
            if len(nb) >= 2: nw[v] = (v.co + 0.5 * (sum((u.co for u in nb), Vector()) / len(nb) - v.co))
        for v, c in nw.items(): v.co.x, v.co.y = c.x, c.y
    bm.to_mesh(cov.data); bm.free()
    # user 2026-10-02: no pockets. The chest pockets are an extra layer of faces lying on the chest surface: in the chest
    # zone, faces that stand further off the body than the garment around them are that layer - removed, holes filled.
    from mathutils.bvhtree import BVHTree
    bmb = bmesh.new(); bmb.from_mesh(body.data); bmb.transform(body.matrix_world)   # the full skin (the evaluated body has it masked off under the garment)
    tree = BVHTree.FromBMesh(bmb); bmb.free()
    bm = bmesh.new(); bm.from_mesh(cov.data); bm.verts.ensure_lookup_table(); M = cov.matrix_world
    dist = {v: (tree.find_nearest(M @ v.co)[3] or 0.0) for v in bm.verts}
    def zone(v): p = M @ v.co; return 0.03 < abs(p.x) < 0.20 and 1.12 < p.z < 1.36 and p.y < -0.02
    ring = [dist[v] for v in bm.verts if zone(v)]
    base = float(np.percentile(ring, 30)) if ring else 0.01
    kill = [f for f in bm.faces if all(zone(v) and dist[v] > base + POCKET_LIFT for v in f.verts)]
    bmesh.ops.delete(bm, geom=kill, context='FACES')
    loose = [v for v in bm.verts if not v.link_faces]; bmesh.ops.delete(bm, geom=loose, context='VERTS')
    bmesh.ops.holes_fill(bm, edges=[e for e in bm.edges if e.is_boundary and 1.10 < (M @ e.verts[0].co).z < 1.40 and abs((M @ e.verts[0].co).x) < 0.22 and (M @ e.verts[0].co).y < 0], sides=0)
    log.append("pockets: base gap %.1f mm, %d faces removed" % (base * 1000, len(kill)))
    # small decorations (badge, sleeve patch): islands under 60 verts away from the collar
    seen = set(); comps = []
    for v in bm.verts:
        if v in seen: continue
        st = [v]; c = []
        while st:
            u = st.pop()
            if u in seen: continue
            seen.add(u); c.append(u); st.extend(e.other_vert(u) for e in u.link_edges)
        comps.append(c)
    deco = [u for c in comps if len(c) < 60 and max((M @ u.co).z for u in c) < 1.44 for u in c]
    bmesh.ops.delete(bm, geom=deco, context='VERTS'); log.append("decorations removed: %d verts" % len(deco))
    bm.to_mesh(cov.data); bm.free()
    # the asset's folds stay (user 2026-10-02: smoothing them out would be junk); its own normal map carries the cloth
    log.append("hem cut at %.2f m: %d verts removed" % (HEM_Z, len(low)))
    # our fabric
    cov.data.materials.clear()
    SN = smooth_normals(cov.data)
    diff = paint_uv(cov, pattern, SN)
    fabric_img = make_image("coverall_diffuse", diff, os.path.join(TEX_DIR, "coverall_diffuse.png"))
    H = paint_uv(cov, relief, SN)[..., 0]
    k = np.exp(-0.5 * (np.arange(-4, 5) / 1.5) ** 2); k /= k.sum()
    H = np.apply_along_axis(lambda r: np.convolve(r, k, 'same'), 0, H); H = np.apply_along_axis(lambda r: np.convolve(r, k, 'same'), 1, H)
    gy, gx = np.gradient(H, 1.6 / TEX); gx = np.clip(gx, -0.7, 0.7); gy = np.clip(gy, -0.7, 0.7)
    nrm = np.stack([-gx, -gy, np.ones_like(H)], -1)
    # the asset's own normal map (its folds, seams and weave, OpenGL convention like ours): slopes added to ours
    aimg = bpy.data.images.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mpfbu", "data", "clothes", ASSET, "normals.png"))
    aimg.scale(TEX, TEX); an = np.array(aimg.pixels[:], np.float32).reshape(TEX, TEX, 4)[..., :3] * 2 - 1   # Blender rows: bottom-up, like ours
    sl = an[..., :2] / np.maximum(an[..., 2:3], 0.2)     # slopes of the asset's map
    nrm[..., :2] += ASSET_NORMAL * sl
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    make_image("coverall_diffuse_norm", nrm * 0.5 + 0.5, os.path.join(TEX_DIR, "coverall_diffuse_norm.png"))
    cov.data.materials.append(material("CoverallFabric", fabric_img))
    for p in cov.data.polygons: p.use_smooth = True
    if not RENDER_ONLY: bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    import render_util
    cam = render_util.setup_stage()
    Hh = max((body.matrix_world @ v.co).z for v in body.data.vertices)
    render_util.shoot(cam, OUT, render_util.standard_shots("fs_" + PAL, Hh), log)
    sc = bpy.context.scene; sc.render.resolution_x = sc.render.resolution_y = 700
    for name, (loc, tgt) in {"fs_%s_cu_chest" % PAL: ((0.45, -0.9, 1.30), (0.0, -0.05, 1.22)), "fs_%s_cu_wrist" % PAL: ((0.75, -0.45, 0.95), (0.40, 0.0, 0.88)),
                             "fs_%s_cu_neck" % PAL: ((0.45, -0.25, 1.55), (0.0, 0.0, 1.43))}.items():
        cam.location = loc; cam.data.lens = 60; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); log.append("rendered " + name)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
