# The «тележка» as a fast 8x8 chassis with swappable modules (crew cabin / anamezon tanker) - looks only, nothing for the game.
# blender --background astronavigator_suit.blend --python mock_chassis.py      (MOD = cabin | tanker)
# Chassis ~9 x 4.2 m, ~18 t: a faceted spine between the wheels, eight airless wheels D1.5 m on long active arms (each wheel
# its own hub drive and its own active leg, ~0.8 m of travel, all steer). Modules lock onto the spine's top (z 1.35).
# Local axes: x across (near side -x), y along (front -y), z up.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
MOD = os.environ.get("MOD", "cabin"); LOG = os.path.join(HERE, "mock_chassis.log"); log = []
O = Vector((3.0, 7.2, 0))

def mat(name, col, rough=0.5, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
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

def oct_ring(y, hw, z0, z1, ch):
    """an octagonal section across x-z at station y: half width hw, from z0 to z1, chamfers ch"""
    return [(-hw + ch, y, z0), (hw - ch, y, z0), (hw, y, z0 + ch), (hw, y, z1 - ch), (hw - ch, y, z1), (-hw + ch, y, z1), (-hw, y, z1 - ch), (-hw, y, z0 + ch)]

def wheel(bm, c, sx, w=0.56, r=0.75):
    """airless wheel: a dark tread band, an elastic lattice of spokes, a hub with the drive"""
    c = Vector(c); ax = Vector((1, 0, 0))
    a, b = c - ax * w / 2, c + ax * w / 2
    loft(bm, [ring(a, ax, r - 0.10, 48), ring(a, ax, r, 48), ring(b, ax, r, 48), ring(b, ax, r - 0.10, 48)], 4, cap=False, closed=True)   # tread
    for k in range(48):                                                       # tread blocks
        t = 2 * math.pi * k / 48
        if k % 2: continue
        p = c + Vector((0, math.cos(t), math.sin(t))) * (r + 0.012)
        box(bm, (p.x - w * 0.45, p.y - 0.045, p.z - 0.03), (p.x + w * 0.45, p.y + 0.045, p.z + 0.03), 4)
    for k in range(14):                                                       # lattice spokes (curved pairs)
        t = 2 * math.pi * k / 14
        for dt, xo in ((0.18, -0.12), (-0.18, 0.12)):
            p0 = c + Vector((xo, math.cos(t) * 0.36, math.sin(t) * 0.36)); p1 = c + Vector((xo, math.cos(t + dt) * (r - 0.11), math.sin(t + dt) * (r - 0.11)))
            cyl(bm, p0, p1, 0.022, 2, 8)
    cyl(bm, c - ax * 0.20, c + ax * 0.20, 0.36, 2, 32)                 # hub drive
    cyl(bm, c + ax * sx * 0.20, c + ax * sx * 0.24, 0.24, 0, 32)        # hub cap
    cyl(bm, c + ax * sx * 0.24, c + ax * sx * 0.25, 0.10, 5, 16)

def obj(name, bm, mats):
    me = bpy.data.meshes.new(name); bm.normal_update(); bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:]); bm.to_mesh(me); bm.free()
    for m in mats: me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); return o

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    HULL = mat("XHull", (0.12, 0.13, 0.145), 0.5, 0.45); WHT = mat("XWhite", (0.86, 0.87, 0.88), 0.35)
    MET = mat("XMetal", (0.30, 0.31, 0.33), 0.3, 0.8); GLS = mat("XGlass", (0.04, 0.07, 0.11), 0.05, 0.4)
    TRD = mat("XTread", (0.05, 0.05, 0.055), 0.85); RED = mat("XRed", (0.72, 0.12, 0.10), 0.4)
    LMP = mat("XLamp", (1.0, 0.96, 0.85), 0.15)
    mats = [HULL, WHT, MET, GLS, TRD, RED, LMP]      # 0 hull 1 white 2 metal 3 glass 4 tread 5 red 6 lamp
    b = bmesh.new()
    # ---- spine: faceted, its nose and tail swept up ----
    st = [(-4.6, 1.05, 0.85, 1.30, 0.25), (-4.2, 1.30, 0.62, 1.35, 0.30), (-3.0, 1.40, 0.55, 1.35, 0.30), (3.0, 1.40, 0.55, 1.35, 0.30),
          (4.2, 1.30, 0.62, 1.35, 0.30), (4.55, 1.10, 0.85, 1.30, 0.25)]
    loft(b, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in st], 0)
    loft(b, [oct_ring(y, hw + 0.01, 1.30, 1.36, 0.0) for y, hw in ((-4.2, 1.30), (4.2, 1.30))], 2)            # module rail
    for sx in (-1, 1): box(b, (sx * 1.415 - 0.01, -4.0, 1.06), (sx * 1.415 + 0.01, 4.0, 1.10), 5)          # red line
    box(b, (-1.0, -4.66, 1.00), (1.0, -4.60, 1.12), 6)                                                       # lamp band
    # ---- wheels on long active arms (trailing, pivot ahead of the wheel), each arm its own actuator inside ----
    for yw in (-3.45, -1.75, 1.75, 3.45):
        for sx in (-1, 1):
            hub = Vector((sx * 1.85, yw, 0.75))
            piv = Vector((sx * 1.42, yw - 0.85 * (1 if yw < 0 else -1), 1.05))
            for dx in (-0.06, 0.06):
                cyl(b, (piv.x + dx * sx, piv.y, piv.z), (hub.x - sx * 0.30 + dx * sx, hub.y, hub.z + 0.05), 0.075, 2, 12)
            cyl(b, (piv.x - sx * 0.02, piv.y, piv.z), (piv.x + sx * 0.12, piv.y, piv.z), 0.14, 0, 20)
            wheel(b, hub, sx)
    # ---- the module ----
    if MOD == "cabin":
        # pressurised cabin: a faceted capsule, wrap-round glazing at the front, a side hatch (octagonal) with a fold-down step
        cs = [(-3.30, 1.20, 1.45, 2.55, 0.35), (-3.05, 1.62, 1.40, 3.05, 0.45), (-2.40, 1.78, 1.38, 3.45, 0.50), (2.60, 1.78, 1.38, 3.45, 0.50),
              (3.15, 1.65, 1.40, 3.30, 0.45), (3.35, 1.40, 1.45, 3.00, 0.40)]
        loft(b, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in cs], 1)
        g = [(-3.12, 1.55, 2.30, 3.02, 0.42), (-2.45, 1.80, 2.35, 3.25, 0.40), (-0.6, 1.80, 2.35, 3.25, 0.40)]   # glazing band
        for i in range(len(g) - 1):
            y0, h0, a0, c0, k0 = g[i]; y1, h1, a1, c1, k1 = g[i + 1]
            for side in (-1, 1):
                p = [(side * (h0 + 0.012), y0, a0), (side * (h1 + 0.012), y1, a1), (side * (h1 + 0.012), y1, c1), (side * (h0 + 0.012), y0, c0)]
                vs = [b.verts.new(O + Vector(q)) for q in p]; b.faces.new(vs).material_index = 3
        p = [(-1.25, -3.08, 2.25), (1.25, -3.08, 2.25), (1.30, -2.85, 3.05), (-1.30, -2.85, 3.05)]                # windscreen
        vs = [b.verts.new(O + Vector(q)) for q in p]; b.faces.new(vs).material_index = 3
        for sx in (-1, 1): box(b, (sx * 1.795 - 0.012, -2.3, 2.12), (sx * 1.795 + 0.012, 2.5, 2.18), 5)         # red line
        oc = Vector((-1.80, 1.20, 2.30)); r8 = 0.62                                                              # side hatch
        hv = [b.verts.new(O + oc + Vector((0, r8 * math.cos(math.pi / 8 + k * math.pi / 4), r8 * math.sin(math.pi / 8 + k * math.pi / 4) * 1.3))) for k in range(8)]
        hv2 = [b.verts.new(v.co + Vector((-0.04, 0, 0))) for v in hv]
        for k in range(8): b.faces.new((hv[k], hv[(k + 1) % 8], hv2[(k + 1) % 8], hv2[k])).material_index = 2
        b.faces.new(hv2[::-1]).material_index = 0
        for k in range(3): box(b, (-2.30, 0.80, 0.55 + k * 0.38), (-2.00, 1.60, 0.60 + k * 0.38), 2)          # fold-down steps
        for sx in (-0.9, -0.3, 0.3, 0.9): cyl(b, (sx, -2.55, 3.46), (sx, -2.75, 3.43), 0.07, 6, 16)            # roof lamps
        top = 3.45
    else:
        # tanker: two faceted cradles on the rail, a transport trap D2.2 x 6.4 m (cryostat white, dark ends, a red band)
        for yy in (-2.0, 2.0):
            loft(b, [oct_ring(yy - 0.3, 1.45, 1.36, 1.95, 0.35), oct_ring(yy + 0.3, 1.45, 1.36, 1.95, 0.35)], 0)
        c = Vector((0, 0, 2.62)); L, D = 6.4, 2.2
        cyl(b, c + Vector((0, -L / 2 + 0.3, 0)), c + Vector((0, L / 2 - 0.3, 0)), D / 2, 1, 48)
        for e, s in ((-L / 2, 1), (L / 2, -1)): cyl(b, c + Vector((0, e, 0)), c + Vector((0, e + 0.3 * s, 0)), D / 2 - 0.12, 0, 48)
        for k in range(6):
            yy = -L / 2 + 0.7 + k * (L - 1.4) / 5
            cyl(b, c + Vector((0, yy - 0.05, 0)), c + Vector((0, yy + 0.05, 0)), D / 2 + 0.03, 2, 48)
        cyl(b, c + Vector((0, -0.3, 0)), c + Vector((0, 0.3, 0)), D / 2 + 0.012, 5, 48)
        top = 3.72
    o = obj("Chassis_" + MOD, b, mats)
    log.append("%s: %d verts, top %.2f m" % (MOD, len(o.data.vertices), top))
    sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (8, 8, 1)
    sun = bpy.data.lights.new("XSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
    so = bpy.data.objects.new("XSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    cam.data.clip_end = 300
    # side elevation (orthographic), her in front of the nose
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = 13.5
    cam.location = (-60, (O.y - 4.7 - 1.6 + O.y + 4.6) / 2 - 0.5, 1.9)
    cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update()
    sc.render.filepath = os.path.join(OUT, "chassis_%s_side.png" % MOD); bpy.ops.render.render(write_still=True); log.append("rendered side")
    # three-quarter perspective
    cam.data.type = 'PERSP'; cam.data.lens = 35
    loc = Vector((-9.5, -6.5, 4.2)); tgt = Vector((O.x, O.y - 1.0, 1.5))
    cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update()
    sc.render.filepath = os.path.join(OUT, "chassis_%s_34.png" % MOD); bpy.ops.render.render(write_still=True); log.append("rendered 3/4")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
