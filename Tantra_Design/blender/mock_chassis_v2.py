# МПУ chassis prototype v2 - the off-road measures (a look only).
# blender --background astronavigator_coverall.blend --python mock_chassis_v2.py
# 1 the coupling folds up against the bumper in transit (approach angle 29 -> 37 deg);
# 2 long-travel double wishbones: their inner pivots on the backbone's sides (arms ~1.1 m), +-0.45 m of wheel travel
#   (clearance up to ~1.0 m; a wheel lifted onto a step);
# 3 wheels D1.6 x 0.65 m (sinkage -18 %, rolling resistance -15 %), track 3.70, overall width 4.35 m;
# 4 levelling: the struts hold the deck level across a slope.
# The deck is raised to 1.62 m (the wheels' tops 1.60 at rest stay under it). Renders: rest, raised, levelled on a 15 deg
# side slope, a wheel on a 0.5 m step, the suspension without the near wheels.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
src = open(os.path.join(HERE, "mock_mpu.py"), encoding="utf-8").read()
exec(src[src.index("import bpy"):src.index("try:\n    for o in bpy.data.objects")])
LOG = os.path.join(HERE, "mock_chassis_v2.log"); log = []
M_ORANGE, M_CABLE, M_SPRING, M_GROUND = 9, 10, 11, 12
AX = (-3.30, -1.65, 1.65, 3.30)
R, W, HUBX0, HUBZ0 = 0.80, 0.65, 1.85, 0.80
P_LOW, P_UP = (0.50, 0.58), (0.55, 1.18)                     # inner pivots (x, z) on the backbone, body frame
L_LOW = math.hypot(1.57 - P_LOW[0], 0.44 - P_LOW[1]); L_UP = math.hypot(1.54 - P_UP[0], 1.10 - P_UP[1])

def tube(bm, a, b, r=0.045, mi=None): cyl(bm, a, b, r, M_METAL if mi is None else mi, 10)

def body(bm, dz, deployed_coupling=False):
    D = lambda z: z + dz
    st = [(-4.25, 0.32, D(0.72), D(1.20), 0.12), (-3.9, 0.45, D(0.65), D(1.30), 0.14), (3.9, 0.45, D(0.65), D(1.30), 0.14), (4.25, 0.32, D(0.72), D(1.20), 0.12)]
    loft(bm, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in st], M_HULL)
    for y in (-3.0, -1.0, 1.0, 3.0): loft(bm, [oct_ring(y - 0.01, 0.455, D(0.645), D(1.305), 0.14), oct_ring(y + 0.01, 0.455, D(0.645), D(1.305), 0.14)], M_SEAM)
    box(bm, (-0.50, -4.0, D(0.62)), (0.50, 4.0, D(0.65)), M_METAL)                              # skid plate
    for y0 in AX:                                                                                 # pivot brackets on the backbone
        for sx in (-1, 1):
            box(bm, (sx * 0.45, y0 - 0.36, D(0.52)), (sx * 0.62, y0 + 0.36, D(1.26)), M_METAL)
    # deck frame on posts from the backbone, deck plates, twist-locks
    for y in (-4.25, -2.45, -0.85, 0.85, 2.45, 4.25):
        box(bm, (-1.40, y - 0.06, D(1.47)), (1.40, y + 0.06, D(1.59)), M_HULL)
        for sx in (-1, 1): tube(bm, (sx * 0.42, y, D(1.25)), (sx * 1.30, y, D(1.47)), 0.05, M_HULL)
    for sx in (-1, 1): box(bm, (sx * 1.40 - 0.07, -4.30, D(1.45)), (sx * 1.40 + 0.07, 4.30, D(1.60)), M_HULL)
    for y0, y1 in ((-4.2, -2.55), (-2.35, -0.95), (-0.75, 0.75), (0.95, 2.35), (2.55, 4.2)):
        box(bm, (-1.30, y0, D(1.60)), (1.30, y1, D(1.62)), M_METAL)
    for sx in (-1, 1):
        for y in (-4.2, -2.45, 0.0, 2.45, 4.2): box(bm, (sx * 1.40 - 0.09, y - 0.09, D(1.60)), (sx * 1.40 + 0.09, y + 0.09, D(1.70)), M_ORANGE)
    for sx in (-1, 1):                                                                            # battery trays (between the middle axles)
        box(bm, (sx * 0.47, -0.95, D(0.75)), (sx * 1.25, 0.95, D(1.30)), M_HULL)
        for k in range(4): box(bm, (sx * 1.25, -0.85 + k * 0.45, D(0.80)), (sx * 1.26, -0.50 + k * 0.45, D(1.26)), M_SEAM)
    for sy in (-1, 1): bumper(bm, sy, dz, deployed_coupling)
    for k in range(3):                                                                            # side steps, stowed in the side
        box(bm, (1.10, -0.4, D(0.95) + k * 0.12), (1.28, 0.4, D(0.98) + k * 0.12), M_METAL)

def bumper(bm, sy, dz, deployed):
    D = lambda z: z + dz; y = sy * 4.35
    box(bm, (-1.25, y - 0.10, D(0.95)), (1.25, y + 0.10, D(1.40)), M_HULL)
    for sx in (-1, 1):
        tube(bm, (sx * 1.25, y, D(1.15)), (sx * 1.38, sy * 4.10, D(1.50)), 0.06, M_HULL)
        box(bm, (sx * 0.95 - 0.22, y + sy * 0.10, D(1.22)), (sx * 0.95 + 0.22, y + sy * 0.12, D(1.30)), M_LAMP)
        cyl(bm, (sx * 1.15, y + sy * 0.10, D(1.05)), (sx * 1.15, y + sy * 0.13, D(1.05)), 0.05, M_GLASS, 12)
    for dx in (-0.16, 0.16): box(bm, (dx - 0.03, y + sy * 0.10, D(1.00)), (dx + 0.03, y + sy * 0.26, D(1.12)), M_METAL)
    pin = Vector((0, y + sy * 0.20, D(1.06)))
    cyl(bm, pin - Vector((0.2, 0, 0)), pin + Vector((0.2, 0, 0)), 0.04, M_METAL, 10)
    d = Vector((0, sy, 0)) if deployed else Vector((0, sy * 0.15, 1.0)).normalized()             # folded: up against the bumper
    cyl(bm, pin, pin + d * 0.32, 0.07, M_ORANGE, 14); cyl(bm, pin + d * 0.32, pin + d * 0.44, 0.15, M_ORANGE, 20)
    for dx in (-0.55, 0.55):
        cyl(bm, (dx, y + sy * 0.10, D(1.15)), (dx, y + sy * 0.22, D(1.15)), 0.07, M_METAL, 14)
        cyl(bm, (dx, y + sy * 0.22, D(1.15)), (dx, y + sy * 0.24, D(1.15)), 0.05, M_ORANGE, 14)
    if sy < 0:
        cyl(bm, (0, y, D(1.40)), (0, y, D(1.58)), 0.05, M_METAL, 10); cyl(bm, (0, y, D(1.58)), (0, y, D(1.70)), 0.10, M_HULL, 20)
        cyl(bm, (0, y, D(1.61)), (0, y, D(1.66)), 0.105, M_GLASS, 20)
        cyl(bm, (0.75, y + sy * 0.10, D(1.10)), (0.75, y + sy * 0.32, D(1.10)), 0.10, M_ORANGE, 16)   # the winch drum's fairlead

def spring(bm, a, b, r, turns=7):
    a, b = Vector(a), Vector(b); ax = (b - a); L = ax.length; u = ax.normalized()
    e1 = u.orthogonal().normalized(); e2 = u.cross(e1); pts = []
    for k in range(turns * 12 + 1):
        t = k / (turns * 12); ang = 2 * math.pi * turns * t
        pts.append(a + u * (L * t) + (e1 * math.cos(ang) + e2 * math.sin(ang)) * r)
    for p, q in zip(pts, pts[1:]): cyl(bm, p, q, 0.013, M_SPRING, 5)

def wheel_module(bm, sx, y0, hz, dz, show=True):
    """the hub stands at height hz (world); the body is raised by dz; the knuckle goes where the lower arm reaches"""
    Pl = Vector((sx * P_LOW[0], y0, P_LOW[1] + dz)); Pu = Vector((sx * P_UP[0], y0, P_UP[1] + dz))
    zl = hz - 0.36
    xl = P_LOW[0] + math.sqrt(max(L_LOW ** 2 - (zl - Pl.z) ** 2, 0.01))
    kL = Vector((sx * xl, y0, zl)); kU = Vector((sx * (xl - 0.03), y0, hz + 0.30))
    hub = Vector((sx * (xl + 0.28), y0, hz))
    for dy in (-0.32, 0.32): tube(bm, Pl + Vector((0, dy, 0)), kL, 0.048)
    tube(bm, Pl + Vector((0, -0.32, 0)), Pl + Vector((0, 0.32, 0)), 0.05)
    for dy in (-0.24, 0.24): tube(bm, Pu + Vector((0, dy, 0)), kU, 0.04)
    for p in (kL, kU): cyl(bm, p - Vector((0, 0.05, 0)), p + Vector((0, 0.05, 0)), 0.06, M_HULL, 12)
    box(bm, (kL.x - 0.06, y0 - 0.09, kL.z), (kL.x + 0.06, y0 + 0.09, kU.z), M_HULL)
    # the active strut: from the lower arm (2/3 out) up to the deck frame
    s0 = Pl + (kL - Pl) * 0.66 + Vector((0, 0.20, 0.05)); s1 = Vector((sx * 0.95, y0 + 0.20, 1.45 + dz))
    cyl(bm, s0, s0 + (s1 - s0) * 0.55, 0.04, M_METAL, 10); cyl(bm, s0 + (s1 - s0) * 0.45, s1, 0.065, M_HULL, 12)
    spring(bm, s0 + (s1 - s0) * 0.10, s0 + (s1 - s0) * 0.80, 0.10)
    box(bm, (s1.x - 0.09, s1.y - 0.09, s1.z - 0.10), (s1.x + 0.09, s1.y + 0.09, s1.z), M_ORANGE)
    # steering actuator from the bracket to the knuckle's arm
    arm_end = kU + Vector((0, -0.28, -0.12))
    tube(bm, kU + Vector((0, -0.08, -0.12)), arm_end, 0.03, M_HULL)
    cyl(bm, Pu + Vector((0, -0.30, -0.06)), Pu + (arm_end - Pu) * 0.5 + Vector((0, -0.30, -0.06)), 0.05, M_HULL, 12)
    tube(bm, Pu + (arm_end - Pu) * 0.5 + Vector((0, -0.30, -0.06)), arm_end, 0.022)
    cyl(bm, hub + Vector((sx * -0.30, 0, 0)), hub + Vector((sx * 0.12, 0, 0)), 0.34, M_METAL, 32)
    cyl(bm, hub + Vector((sx * -0.33, 0, 0)), hub + Vector((sx * -0.30, 0, 0)), 0.37, M_HULL, 32)
    box(bm, (hub.x - 0.05 - sx * 0.26, y0 - 0.10, hz + 0.27), (hub.x + 0.05 - sx * 0.26, y0 + 0.10, hz + 0.40), M_RED)
    c = [Pu + Vector((0, 0.30, -0.1)), Pu + (kL - Pu) * 0.5 + Vector((0, 0.40, 0)), kL + Vector((0, 0.30, 0.1)), hub + Vector((-sx * 0.25, 0.12, 0))]
    for p, q in zip(c, c[1:]): cyl(bm, p, q, 0.024, M_CABLE, 8)
    if not show: return
    wheel(bm, hub, sx, w=W, r=R)
    pts = [Vector((0, math.cos(a_) * 0.98, math.sin(a_) * 0.98)) for a_ in [math.radians(40 + 100 * k / 10) for k in range(11)]]
    for p, q in zip(pts, pts[1:]):
        for o_ in ((p, q), (q, p)):
            a, b_ = o_
            quad(bm, [(hub.x - 0.36, y0 + a.y, hz + a.z), (hub.x + 0.36, y0 + a.y, hz + a.z), (hub.x + 0.36, y0 + b_.y, hz + b_.z), (hub.x - 0.36, y0 + b_.y, hz + b_.z)], M_HULL)
    tube(bm, kU, Vector((hub.x, y0, hz + 0.98)), 0.025, M_HULL)

def chassis_v2(bm, dz=0.0, hubs=None, show=True, deployed=False):
    body(bm, dz, deployed)
    for i, y0 in enumerate(AX):
        for j, sx in enumerate((-1, 1)):
            hz = hubs[(i, sx)] if hubs and (i, sx) in hubs else HUBZ0
            wheel_module(bm, sx, y0, hz, dz, show or sx > 0)

def ground_slab(name, tilt_deg=0.0, step=None):
    b = bmesh.new()
    t = math.tan(math.radians(tilt_deg))
    P = [(-12, -12), (12, -12), (12, 12), (-12, 12)]
    vs = [b.verts.new(O + Vector((x, y, -t * x - 0.001))) for x, y in P]; b.faces.new(vs)
    if step: box(b, step[0], step[1], 0)
    return obj(name, b, [mat("PG_" + name, (0.42, 0.41, 0.39), 0.95)])

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    MATS = [mat("PHull", (0.11, 0.12, 0.135), 0.6, 0.3), mat("PWhite", (0.85, 0.86, 0.87), 0.45), mat("PMetal", (0.40, 0.41, 0.43), 0.3, 0.8),
            mat("PGlass", (0.05, 0.09, 0.13), 0.05, 0.4), mat("PTread", (0.05, 0.05, 0.055), 0.85), mat("PRed", (0.72, 0.12, 0.10), 0.4),
            mat("PLamp", (1.0, 0.96, 0.85), 0.15), mat("PSeam", (0.04, 0.04, 0.045), 0.7), mat("PBlade", (0.20, 0.22, 0.26), 0.35, 0.6),
            mat("POrange", (0.90, 0.50, 0.10), 0.5), mat("PCable", (0.08, 0.08, 0.09), 0.9), mat("PSpring", (0.75, 0.70, 0.20), 0.3, 0.9)]
    sys.path.insert(0, HERE); import render_util
    O = Vector((3.4, 6.9, 0.0))
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (10, 10, 1)
    sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.2; sun.angle = 0.05
    so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 400
    def shoot(name, kind, lens, loc, tgt):
        cam.data.type = kind; cam.rotation_mode = 'QUATERNION'
        if kind == 'ORTHO': cam.data.ortho_scale = lens
        else: cam.data.lens = lens
        cam.location = O + loc; t = O + tgt
        cam.rotation_quaternion = (t - cam.location).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "chassis_v2_%s.png" % name); bpy.ops.render.render(write_still=True)
    def make(name, **kw):
        b = bmesh.new(); chassis_v2(b, **kw); return obj(name, b, MATS)
    # rest
    o = make("Rest"); shoot("rest", 'PERSP', 32, Vector((-8.5, -7.5, 4.5)), Vector((0, -0.5, 1.0)))
    shoot("rest_side", 'ORTHO', 11.5, Vector((-40, 0, 1.1)), Vector((0, 0, 1.1))); bpy.data.objects.remove(o, do_unlink=True)
    # raised 0.40 m
    o = make("Raised", dz=0.40); shoot("raised", 'ORTHO', 11.5, Vector((-40, 0, 1.3)), Vector((0, 0, 1.3)))
    shoot("raised_front", 'ORTHO', 6.0, Vector((0, -40, 1.2)), Vector((0, 0, 1.2))); bpy.data.objects.remove(o, do_unlink=True)
    # levelled across a 15 deg side slope (the +x side lower)
    t15 = math.tan(math.radians(15))
    hubs = {(i, sx): HUBZ0 - t15 * sx * HUBX0 for i in range(4) for sx in (-1, 1)}
    if fl: fl.hide_render = True
    g = ground_slab("Slope", 15.0)
    o = make("Level", dz=0.05, hubs=hubs); shoot("slope_front", 'ORTHO', 6.5, Vector((0, -40, 1.0)), Vector((0, 0, 1.0)))
    shoot("slope_34", 'PERSP', 32, Vector((-8.0, -8.0, 4.0)), Vector((0, -0.5, 1.0)))
    bpy.data.objects.remove(o, do_unlink=True); bpy.data.objects.remove(g, do_unlink=True)
    # the front-left wheel up on a 0.5 m step, the body level
    g = ground_slab("Step", 0.0, ((1.2, -4.6, 0.0), (3.0, -2.6, 0.50)))
    o = make("Step", dz=0.15, hubs={(0, 1): HUBZ0 + 0.50}); shoot("step", 'PERSP', 34, Vector((7.5, -7.0, 2.6)), Vector((0.5, -2.0, 0.9)))
    bpy.data.objects.remove(o, do_unlink=True); bpy.data.objects.remove(g, do_unlink=True)
    if fl: fl.hide_render = False
    # the suspension without the near wheels, and the coupling down (in use)
    o = make("Susp", show=False, deployed=True); shoot("susp", 'PERSP', 34, Vector((-4.8, -6.2, 1.8)), Vector((-1.2, -3.2, 0.95)))
    log.append("rendered")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
