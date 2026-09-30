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
    rib = yoke & ((np.floor(z / 0.012) % 2) == 0); col[rib] = PANEL * 0.82
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

def paint_uv(obj, mat_index=0):
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
        c_ = pattern(P, Nn)
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
    body = next(o for o in bpy.data.objects if o.type == 'MESH' and 'female1605' in o.name)
    names = {g.index: g.name for g in body.vertex_groups}
    # bone names for either rig (MPFB game_engine or cmu_mb)
    BN = arm.data.bones
    RIG = {'neck': 'neck_01', 'hand_l': 'hand_l', 'hand_r': 'hand_r', 'lowerarm_l': 'lowerarm_l', 'lowerarm_r': 'lowerarm_r', 'foot_l': 'foot_l'} if 'neck_01' in BN else           {'neck': 'Neck', 'hand_l': 'LeftHand', 'hand_r': 'RightHand', 'lowerarm_l': 'LeftForeArm', 'lowerarm_r': 'RightForeArm', 'foot_l': 'LeftFoot'}
    neck_z = (arm.matrix_world @ BN[RIG['neck']].head_local).z

    cov = body.copy(); cov.data = body.data.copy(); cov.name = "Coverall"; cov.data.name = "Coverall"
    body.users_collection[0].objects.link(cov)
    for m in list(cov.modifiers):
        if m.type != 'ARMATURE': cov.modifiers.remove(m)
    cov.data.materials.clear()

    HEAD = {'head', 'Head', 'Neck1', 'lips', 'ears'}; HANDS = lambda n: n and (n.startswith('hand_') or any(n.startswith(f) for f in ('index', 'middle', 'pinky', 'ring', 'thumb')))
    FEET = lambda n: n and (n.startswith('foot_') or n.startswith('ball_'))
    bm = bmesh.new(); bm.from_mesh(cov.data); bm.verts.ensure_lookup_table()
    deform = bm.verts.layers.deform.active
    me_src = cov.data
    W = lambda b: arm.matrix_world @ arm.data.bones[b].head_local
    wrist = {s: (W(RIG['hand_' + s]), (W(RIG['hand_' + s]) - W(RIG['lowerarm_' + s])).normalized()) for s in ('l', 'r')}
    ankle_z = W(RIG['foot_l']).z
    kill = []
    for v in bm.verts:
        d = dict(v[deform]); dom = names[max(d, key=d.get)] if d else None
        side = 'l' if v.co.x > 0 else 'r'; wp, wd = wrist[side]
        beyond_wrist = (v.co - wp).dot(wd) > 0.015 and abs(v.co.x) > 0.20
        if dom in HEAD or v.co.z > neck_z + 0.035 or beyond_wrist or v.co.z < ankle_z - 0.012: kill.append(v)
        # flatten nipples before offsetting
    bmesh.ops.delete(bm, geom=kill, context='VERTS')
    nip = [v for v in bm.verts if any(names[k] == 'nipples' and w > 0.05 for k, w in v[deform].items())]
    for _ in range(3):
        for v in nip:
            nb = [e.other_vert(v).co for e in v.link_edges]
            if nb: v.co = v.co.lerp(sum(nb, Vector()) / len(nb), 0.6)
    bm.normal_update()
    for v in bm.verts: v.co += v.normal * OFFSET
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

    def extrude(loop, up, out, mat):
        c, _ = centre(loop)
        r = bmesh.ops.extrude_edge_only(bm, edges=loop)
        nv = [g for g in r['geom'] if isinstance(g, bmesh.types.BMVert)]
        for v in nv:
            radial = (v.co - c); radial.z = 0; radial.normalize() if radial.length > 1e-6 else None
            v.co += Vector((0, 0, up)) + radial * out
        for f in [g for g in r['geom'] if isinstance(g, bmesh.types.BMFace)]: f.material_index = mat
        return [g for g in r['geom'] if isinstance(g, bmesh.types.BMEdge) and g.is_boundary]

    # stand collar: 38 mm up with slight flare, red piping at the top edge
    neck_loop = info[0][1]
    top = extrude(neck_loop, 0.036, 0.003, 1)
    extrude(top, 0.004, 0.0, 2)
    # cuffs at the wrists (the two loops closest to the hand height)
    hz = W(RIG['hand_l']).z
    wrists = sorted(info[1:], key=lambda t: abs(t[0] - hz))[:2]
    for _, l in wrists:
        c, vs = centre(l)
        # direction along the forearm, from the elbow region to the wrist
        top_loop = extrude(l, 0.0, 0.004, 3)
        extrude(top_loop, -0.012, 0.0, 3)
    bm.to_mesh(cov.data); bm.free()

    fabric_img = make_image("coverall_diffuse", paint_uv(cov, 0))
    for m in (material("CoverallFabric", image=fabric_img), material("CoverallCollar", color=tuple(FABRIC)),
              material("CoverallRed", color=tuple(RED), rough=0.5), material("CoverallCuff", color=tuple(PANEL * 0.85))):
        cov.data.materials.append(m)
    for p in cov.data.polygons: p.use_smooth = True

    # hide the skin under the garment (keeps the head, neck and hands visible)
    covered = body.vertex_groups.new(name="covered_by_coverall")
    idx = []
    for v in body.data.vertices:
        if not v.groups: continue
        g = max(v.groups, key=lambda g: g.weight); n = names[g.group]
        far_hand = any((Vector(v.co) - wrist[s][0]).dot(wrist[s][1]) > 0.0 and abs(v.co.x) > 0.2 for s in ('l', 'r'))
        if not (n in HEAD or far_hand or n in ('neck_01', 'Neck') or v.co.z < ankle_z - 0.01) and v.co.z < neck_z + 0.02: idx.append(v.index)
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
