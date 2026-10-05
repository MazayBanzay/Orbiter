# МПУ (мобильная платформа универсальная) - looks and stowage, nothing for the game.
# blender --background astronavigator_suit.blend --python mock_mpu.py       (SCENE = cabin | tanker | stow)
# Chassis 8.8 x 4.2 m, ~18 t: a faceted CNT-composite spine; eight airless wheels D1.4 m (elastic CNT/NiTi blades, a polymer
# tread), each with a hub motor on its own active arm (~0.8 m of travel), all steer. Stowed it kneels to 1.45 m.
# Modules lock onto the spine (z 1.35): a crew cabin assembled from flat ceramic-composite sandwich panels (ALON windows),
# an anamezon tanker (transport trap D2.2 x 6.4 m).
# Stowage on the hangar's rover platform (18 x 9 m): 3.0 m under the shuttle, 1.18 m under the two cradle pads
# (s 102.6-104.6 and 113.6-115.6, |x| < 2.5): between the pads 9.0 m - two chassis abreast, one pair stacked on a rack,
# the third with two flat-packed cabins on its rail; the tanker's trap across the aft strip.
# Local axes: x across (near side -x), y along (front -y), z up.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
SCENE = os.environ.get("SCENE", "cabin"); LOG = os.path.join(HERE, "mock_mpu.log"); log = []
O = Vector((0, 0, 0))

def mat(name, col, rough=0.5, metal=0.0, alpha=1.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if alpha < 1:
        b.inputs["Alpha"].default_value = alpha
        try: m.blend_method = 'BLEND'
        except Exception: pass
    return m

def ring(c, ax, r, seg):
    ax = ax.normalized(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    return [c + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r for j in range(seg)]

def loft(bm, rings, mi=0, cap=True, closed=False):
    Vs = [[bm.verts.new(O + Vector(p)) for p in r] for r in rings]; n = len(rings[0])
    for i in range(len(Vs) if closed else len(Vs) - 1):
        A, B = Vs[i], Vs[(i + 1) % len(Vs)]
        for j in range(n): bm.faces.new((A[j], A[(j + 1) % n], B[(j + 1) % n], B[j])).material_index = mi
    if cap and not closed:
        bm.faces.new(Vs[0][::-1]).material_index = mi; bm.faces.new(Vs[-1]).material_index = mi
    return Vs

def cyl(bm, a, b, r, mi=0, seg=24):
    a, b = Vector(a), Vector(b); loft(bm, [ring(a, b - a, r, seg), ring(b, b - a, r, seg)], mi)

def box(bm, lo, hi, mi=0, bev=0.0):
    r = bmesh.ops.create_cube(bm, size=1.0); vs = r['verts']
    c = (Vector(lo) + Vector(hi)) / 2; s = Vector(hi) - Vector(lo)
    for v in vs: v.co = O + Vector((c.x + v.co.x * s.x, c.y + v.co.y * s.y, c.z + v.co.z * s.z))
    for f in {f for v in vs for f in v.link_faces}: f.material_index = mi
    if bev > 0:
        res = bmesh.ops.bevel(bm, geom=list({e for v in vs for e in v.link_edges}), offset=bev, segments=3, profile=0.5, affect='EDGES')
        for f in res['faces']: f.material_index = mi

def quad(bm, pts, mi):
    vs = [bm.verts.new(O + Vector(p)) for p in pts]; bm.faces.new(vs).material_index = mi

def oct_ring(y, hw, z0, z1, ch):
    return [(-hw + ch, y, z0), (hw - ch, y, z0), (hw, y, z0 + ch), (hw, y, z1 - ch), (hw - ch, y, z1), (-hw + ch, y, z1), (-hw, y, z1 - ch), (-hw, y, z0 + ch)]

M_HULL, M_WHITE, M_METAL, M_GLASS, M_TREAD, M_RED, M_LAMP, M_SEAM, M_BLADE = range(9)

def wheel(bm, c, sx, w=0.52, r=0.70):
    """airless wheel: polymer tread band with chevrons, 20 curved elastic blades, a hub motor"""
    c = Vector(c); ax = Vector((1, 0, 0))
    a, b = c - ax * w / 2, c + ax * w / 2
    loft(bm, [ring(a, ax, r - 0.09, 60), ring(a, ax, r, 60), ring(b, ax, r, 60), ring(b, ax, r - 0.09, 60)], M_TREAD, cap=False, closed=True)
    for k in range(30):                                                   # chevrons
        t = 2 * math.pi * k / 30
        for s in (-1, 1):
            p0 = c + Vector((0, math.cos(t), math.sin(t))) * (r + 0.01)
            p1 = c + Vector((s * w * 0.46, math.cos(t + 0.07), math.sin(t + 0.07))) * 1.0
            p1 = c + Vector((s * w * 0.46, math.cos(t + 0.07) * (r + 0.01), math.sin(t + 0.07) * (r + 0.01)))
            cyl(bm, p0, p1, 0.016, M_TREAD, 6)
    for k in range(20):                                                   # elastic blades: S-curves from hub to band
        t0 = 2 * math.pi * k / 20; pts = []
        for i in range(7):
            u = i / 6; rr = 0.30 + (r - 0.40) * u + 0.0; tt = t0 + 0.35 * math.sin(math.pi * u)
            pts.append((rr, tt))
        A = [bm.verts.new(O + c + Vector((-w * 0.38, math.cos(tt) * rr, math.sin(tt) * rr))) for rr, tt in pts]
        B = [bm.verts.new(O + c + Vector((w * 0.38, math.cos(tt) * rr, math.sin(tt) * rr))) for rr, tt in pts]
        for i in range(6): bm.faces.new((A[i], A[i + 1], B[i + 1], B[i])).material_index = M_BLADE
    cyl(bm, c - ax * 0.20, c + ax * 0.20, 0.31, M_METAL, 40)              # hub motor
    cyl(bm, c + ax * sx * 0.20, c + ax * sx * 0.235, 0.22, M_HULL, 40)
    cyl(bm, c + ax * sx * 0.235, c + ax * sx * 0.245, 0.08, M_RED, 20)

def chassis(bm, oy=0.0, ox=0.0, oz=0.0, stowed=False):
    """the 8x8 chassis at (ox, oy, oz); stowed: arms raised, body down to the wheels' bottom"""
    global O
    O0 = O; O = O0 + Vector((ox, oy, oz))
    dz = -0.48 if stowed else 0.0
    st = [(-4.40, 1.05, 0.85, 1.30, 0.25), (-4.00, 1.30, 0.60, 1.35, 0.30), (-2.9, 1.40, 0.53, 1.35, 0.30), (2.9, 1.40, 0.53, 1.35, 0.30),
          (4.00, 1.30, 0.60, 1.35, 0.30), (4.35, 1.10, 0.85, 1.30, 0.25)]
    loft(bm, [oct_ring(y, hw, z0 + dz, z1 + dz, ch) for y, hw, z0, z1, ch in st], M_HULL)
    for yy in (-2.9, -1.0, 1.0, 2.9):                                     # panel seams of the composite spine
        loft(bm, [oct_ring(yy - 0.01, 1.405, 0.525 + dz, 1.355 + dz, 0.30), oct_ring(yy + 0.01, 1.405, 0.525 + dz, 1.355 + dz, 0.30)], M_SEAM)
    loft(bm, [oct_ring(y, hw + 0.01, 1.30 + dz, 1.36 + dz, 0.0) for y, hw in ((-4.0, 1.30), (4.0, 1.30))], M_METAL)   # module rail
    for sx in (-1, 1): box(bm, (sx * 1.415 - 0.01, -3.8, 1.06 + dz), (sx * 1.415 + 0.01, 3.8, 1.10 + dz), M_RED)
    box(bm, (-1.0, -4.46, 1.00 + dz), (1.0, -4.40, 1.12 + dz), M_LAMP)
    for sx in (-1, 1): box(bm, (sx * 0.75 - 0.18, -4.47, 0.92 + dz), (sx * 0.75 + 0.18, -4.41, 0.97 + dz), M_LAMP)
    for yw in (-3.30, -1.65, 1.65, 3.30):
        for sx in (-1, 1):
            hub = Vector((sx * 1.85, yw, 0.70))
            piv = Vector((sx * 1.42, yw - 0.80 * (1 if yw < 0 else -1), 1.02 + dz))
            for dxx in (-0.06, 0.06):
                cyl(bm, (piv.x + dxx * sx, piv.y, piv.z), (hub.x - sx * 0.27 + dxx * sx, hub.y, hub.z + 0.05), 0.07, M_METAL, 12)
            cyl(bm, (piv.x - sx * 0.02, piv.y, piv.z), (piv.x + sx * 0.12, piv.y, piv.z), 0.13, M_HULL, 20)
            wheel(bm, hub, sx)
    O = O0

def cabin(bm, oy=0.0, ox=0.0, oz=0.0):
    """crew cabin assembled from flat sandwich panels: a faceted shell, seams between the panels, ALON windows"""
    global O
    O0 = O; O = O0 + Vector((ox, oy, oz))
    cs = [(-3.20, 1.25, 1.45, 2.55, 0.35), (-2.95, 1.62, 1.40, 3.05, 0.45), (-2.30, 1.76, 1.38, 3.40, 0.50), (2.60, 1.76, 1.38, 3.40, 0.50),
          (3.10, 1.62, 1.40, 3.25, 0.45), (3.30, 1.40, 1.45, 3.00, 0.40)]
    loft(bm, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in cs], M_WHITE)
    for yy in (-2.30, -0.95, 0.40, 1.75):                                 # seams between the shell panels
        loft(bm, [oct_ring(yy - 0.012, 1.772, 1.375, 3.405, 0.50), oct_ring(yy + 0.012, 1.772, 1.375, 3.405, 0.50)], M_SEAM)
    for sx in (-1, 1):                                                    # side windows (ALON), two panes per side ahead of the hatch
        for y0, y1 in ((-2.15, -1.10), (-0.80, 0.25)):
            quad(bm, [(sx * 1.775, y0, 2.45), (sx * 1.775, y1, 2.45), (sx * 1.775, y1, 3.10), (sx * 1.775, y0, 3.10)][::sx], M_GLASS)
        box(bm, (sx * 1.762 - 0.012, -2.2, 2.08), (sx * 1.762 + 0.012, 3.0, 2.13), M_RED)
    quad(bm, [(-1.15, -3.02, 2.30), (1.15, -3.02, 2.30), (1.20, -2.80, 3.02), (-1.20, -2.80, 3.02)], M_GLASS)      # windscreen
    oc = Vector((-1.78, 1.25, 2.30)); r8 = 0.58                                                                   # side hatch
    hv = [bm.verts.new(O + oc + Vector((0, r8 * math.cos(math.pi / 8 + k * math.pi / 4), r8 * math.sin(math.pi / 8 + k * math.pi / 4) * 1.32))) for k in range(8)]
    hv2 = [bm.verts.new(v.co + Vector((-0.04, 0, 0))) for v in hv]
    for k in range(8): bm.faces.new((hv[k], hv[(k + 1) % 8], hv2[(k + 1) % 8], hv2[k])).material_index = M_METAL
    bm.faces.new(hv2[::-1]).material_index = M_HULL
    for k in range(3): box(bm, (-2.28, 0.85, 0.50 + k * 0.38), (-1.98, 1.65, 0.55 + k * 0.38), M_METAL)
    for sx in (-0.9, -0.3, 0.3, 0.9): cyl(bm, (sx, -2.45, 3.41), (sx, -2.65, 3.38), 0.065, M_LAMP, 16)
    cyl(bm, (0.9, 2.4, 3.40), (0.9, 2.4, 3.75), 0.025, M_METAL, 8); cyl(bm, (0.9, 2.4, 3.75), (0.9, 2.4, 3.80), 0.10, M_HULL, 16)   # sensor mast
    O = O0

def trap(bm, c, L=6.4, D=2.2, axis='y'):
    c = Vector(c); u = Vector((0, 1, 0)) if axis == 'y' else Vector((1, 0, 0))
    cyl(bm, c - u * (L / 2 - 0.3), c + u * (L / 2 - 0.3), D / 2, M_WHITE, 48)
    for s in (-1, 1): cyl(bm, c + u * s * (L / 2 - 0.3), c + u * s * L / 2, D / 2 - 0.12, M_HULL, 48)
    for k in range(6):
        t = -L / 2 + 0.7 + k * (L - 1.4) / 5
        cyl(bm, c + u * (t - 0.05), c + u * (t + 0.05), D / 2 + 0.03, M_METAL, 48)
    cyl(bm, c - u * 0.3, c + u * 0.3, D / 2 + 0.012, M_RED, 48)

def tanker(bm, oy=0.0, ox=0.0, oz=0.0):
    global O
    O0 = O; O = O0 + Vector((ox, oy, oz))
    for yy in (-2.0, 2.0): loft(bm, [oct_ring(yy - 0.3, 1.45, 1.36, 1.95, 0.35), oct_ring(yy + 0.3, 1.45, 1.36, 1.95, 0.35)], M_HULL)
    trap(bm, (0, 0, 2.62))
    O = O0

def obj(name, bm, mats):
    me = bpy.data.meshes.new(name); bm.normal_update(); bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:]); bm.to_mesh(me); bm.free()
    for m in mats: me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); return o

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    MATS = [mat("PHull", (0.11, 0.12, 0.135), 0.6, 0.3), mat("PWhite", (0.85, 0.86, 0.87), 0.45), mat("PMetal", (0.32, 0.33, 0.35), 0.3, 0.8),
            mat("PGlass", (0.05, 0.09, 0.13), 0.05, 0.4), mat("PTread", (0.05, 0.05, 0.055), 0.85), mat("PRed", (0.72, 0.12, 0.10), 0.4),
            mat("PLamp", (1.0, 0.96, 0.85), 0.15), mat("PSeam", (0.04, 0.04, 0.045), 0.7), mat("PBlade", (0.20, 0.22, 0.26), 0.35, 0.6)]
    sys.path.insert(0, HERE); import render_util
    b = bmesh.new()
    if SCENE in ("cabin", "tanker"):
        O = Vector((3.0, 6.9, 0.0))
        chassis(b)
        (cabin if SCENE == "cabin" else tanker)(b)
        obj("MPU_" + SCENE, b, MATS)
        cam = render_util.setup_stage()
        fl = bpy.data.objects.get("Floor")
        if fl: fl.scale = (8, 8, 1)
        sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
        so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
        so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
        sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 300
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = 13.0
        cam.location = (-60, 5.9, 1.9); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_%s_side.png" % SCENE); bpy.ops.render.render(write_still=True)
        cam.data.type = 'PERSP'; cam.data.lens = 35
        loc = Vector((-9.5, -6.5, 4.2)); tgt = Vector((O.x, O.y - 1.0, 1.5))
        cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_%s_34.png" % SCENE); bpy.ops.render.render(write_still=True)
        log.append("rendered " + SCENE)
    else:
        # the hangar's rover platform with what lies on it; she stands on the platform for scale
        for ob in bpy.data.objects:
            if ob.type == 'MESH' and not ob.name.startswith("Jet"): pass
        O = Vector((0, 0, 0)); PY0 = 1.0                                   # platform y from PY0 to PY0 + 18
        box(b, (-4.5, PY0, -0.25), (4.5, PY0 + 18.0, 0.0), M_METAL)                                   # platform deck
        for y0 in (1.5, 12.5):                                                                         # cradle pads (hang from the shuttle)
            box(b, (-2.5, PY0 + y0, 1.18), (2.5, PY0 + y0 + 2.0, 1.58), M_RED)
            box(b, (-0.4, PY0 + y0 + 0.6, 1.58), (0.4, PY0 + y0 + 1.4, 3.0), M_RED)
        m_sh = mat("PShuttle", (0.6, 0.62, 0.66), 0.5, 0.0, alpha=0.18); MATS.append(m_sh)
        box(b, (-4.5, PY0, 3.0), (4.5, PY0 + 18.0, 3.04), len(MATS) - 1)                              # shuttle belly line
        ymid = PY0 + 8.0                                                                               # between the pads: 3.5 .. 12.5
        for sx in (-1, 1):                                                                             # two chassis abreast, stowed
            chassis(b, oy=ymid, ox=sx * 2.15, stowed=True)
        chassis(b, oy=ymid, ox=-2.15, oz=1.49, stowed=True)                                           # the third on the rack above
        for x0 in (-4.3, -0.05):                                                                       # rack: four posts and a deck
            for yy in (ymid - 4.3, ymid + 4.3): box(b, (x0, yy - 0.06, 0.0), (x0 + 0.1, yy + 0.06, 1.49), M_HULL)
        box(b, (-4.3, ymid - 4.4, 1.45), (-0.05, ymid + 4.4, 1.49), M_HULL)
        for k in range(2):                                                                             # two flat-packed cabins on the right chassis
            box(b, (2.15 - 1.75, ymid - 3.2, 0.88 + k * 0.40), (2.15 + 1.75, ymid + 3.2, 1.26 + k * 0.40), M_WHITE, 0.02)
            box(b, (2.15 - 1.76, ymid - 3.2, 1.06 + k * 0.40), (2.15 + 1.76, ymid + 3.2, 1.08 + k * 0.40), M_RED)
        trap(b, (0, PY0 + 16.25, 1.12), axis='x')                                                     # the tanker's trap across the aft strip
        for xx in (-2.0, 2.0): box(b, (xx - 0.3, PY0 + 15.6, 0.0), (xx + 0.3, PY0 + 16.9, 0.3), M_HULL)
        obj("Stowage", b, MATS)
        for ob in bpy.data.objects:
            if ob.type == 'ARMATURE': ob.location = Vector((0, 0, 0))
        cam = render_util.setup_stage()
        sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
        so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
        so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-6, -4, 12))).to_track_quat('-Z', 'Y')
        sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 1000; cam.data.clip_end = 300
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = 21.0
        cam.location = (0, PY0 + 9.0, 60); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = Vector((0, 0, -1)).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_stow_top.png"); bpy.ops.render.render(write_still=True)
        cam.data.ortho_scale = 20.0
        cam.location = (-60, PY0 + 9.0, 1.6); cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_stow_side.png"); bpy.ops.render.render(write_still=True)
        cam.data.type = 'PERSP'; cam.data.lens = 30
        loc = Vector((-13, -9, 9)); tgt = Vector((0, PY0 + 9, 1.0))
        cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_stow_34.png"); bpy.ops.render.render(write_still=True)
        log.append("rendered stowage")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
