# МПУ chassis - a realistic prototype (a look only, nothing for the game yet).
# blender --background astronavigator_coverall.blend --python mock_chassis_proto.py
# Backbone: a CNT-composite box beam down the middle (ground clearance 0.55 m), four suspension nodes on cross members (one
# per axle), a perimeter deck frame with the module twist-locks, battery trays between the middle axles (slide out
# sideways), a skid plate under it, front and rear bumper nodes with the coupling (hitch head + power/data plugs), lamps,
# lidar and cameras.
# Wheel module (x8): double wishbones on the node, a steering knuckle with a kingpin and its own steering actuator (every
# wheel steers), an active strut (coil spring + damper + electromechanical actuator: ride height, levelling, kneeling),
# a hub motor with planetary reduction and a parking brake inside the airless wheel, a fender on the knuckle (the dust).
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
src = open(os.path.join(HERE, "mock_mpu.py"), encoding="utf-8").read()
exec(src[src.index("import bpy"):src.index("try:\n    for o in bpy.data.objects")])
LOG = os.path.join(HERE, "mock_chassis_proto.log"); log = []
M_ORANGE, M_CABLE, M_SPRING = 9, 10, 11
AX = (-3.30, -1.65, 1.65, 3.30)
HUBX, HUBZ = 1.85, 0.70

def tube(bm, a, b, r=0.045, mi=None): cyl(bm, a, b, r, M_METAL if mi is None else mi, 10)

def backbone(bm):
    st = [(-4.25, 0.32, 0.62, 1.05, 0.12), (-3.9, 0.45, 0.55, 1.15, 0.14), (3.9, 0.45, 0.55, 1.15, 0.14), (4.25, 0.32, 0.62, 1.05, 0.12)]
    loft(bm, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in st], M_HULL)
    for y in (-3.0, -1.0, 1.0, 3.0): loft(bm, [oct_ring(y - 0.01, 0.455, 0.545, 1.155, 0.14), oct_ring(y + 0.01, 0.455, 0.545, 1.155, 0.14)], M_SEAM)
    box(bm, (-0.50, -4.0, 0.52), (0.50, 4.0, 0.55), M_METAL)                                   # skid plate
    for y0 in AX:                                                                               # suspension nodes
        box(bm, (-1.15, y0 - 0.38, 0.72), (1.15, y0 + 0.38, 1.22), M_HULL)
        for sx in (-1, 1):
            box(bm, (sx * 1.15 - 0.05, y0 - 0.40, 0.70), (sx * 1.15 + 0.05, y0 + 0.40, 1.30), M_METAL)
    # perimeter deck frame (rectangular tubes) and cross tubes; deck grating plates with gaps showing the frame
    for sx in (-1, 1): box(bm, (sx * 1.40 - 0.07, -4.30, 1.25), (sx * 1.40 + 0.07, 4.30, 1.40), M_HULL)
    for y in (-4.30, -2.45, -0.85, 0.85, 2.45, 4.30): box(bm, (-1.40, y - 0.06, 1.27), (1.40, y + 0.06, 1.39), M_HULL)
    for y0, y1 in ((-4.2, -2.55), (-2.35, -0.95), (-0.75, 0.75), (0.95, 2.35), (2.55, 4.2)):
        box(bm, (-1.30, y0, 1.40), (1.30, y1, 1.42), M_METAL)
    for sx in (-1, 1):                                                                          # module twist-locks (ISO-style)
        for y in (-4.2, -2.45, 0.0, 2.45, 4.2):
            box(bm, (sx * 1.40 - 0.09, y - 0.09, 1.40), (sx * 1.40 + 0.09, y + 0.09, 1.50), M_ORANGE)
    # battery trays between the middle axles, sliding out sideways for service
    for sx in (-1, 1):
        box(bm, (sx * 0.47, -0.95, 0.66), (sx * 1.25, 0.95, 1.18), M_HULL)
        for k in range(4): box(bm, (sx * 1.25 - 0.01 * sx, -0.85 + k * 0.45, 0.70), (sx * 1.26, -0.50 + k * 0.45, 1.14), M_SEAM)
        box(bm, (sx * 1.26, -0.10, 0.88), (sx * 1.30, 0.10, 0.96), M_ORANGE)                 # its handle / latch
    # cable trays along the backbone's top
    for sx in (-0.3, 0.3): box(bm, (sx - 0.06, -3.9, 1.155), (sx + 0.06, 3.9, 1.20), M_CABLE)

def bumper(bm, sy):
    y = sy * 4.35
    box(bm, (-1.25, y - 0.10, 0.70), (1.25, y + 0.10, 1.12), M_HULL)
    for sx in (-1, 1):
        tube(bm, (sx * 1.25, y, 0.95), (sx * 1.38, sy * 4.10, 1.30), 0.06, M_HULL)
        box(bm, (sx * 0.95 - 0.22, y + sy * 0.10, 0.98), (sx * 0.95 + 0.22, y + sy * 0.12, 1.06), M_LAMP)
        cyl(bm, (sx * 1.15, y + sy * 0.10, 0.82), (sx * 1.15, y + sy * 0.13, 0.82), 0.05, M_GLASS, 12)   # camera
    # the coupling: a yoke, the hitch head on a pin (it swings and pitches), two umbilical plugs (power, data / fluids)
    for dx in (-0.16, 0.16): box(bm, (dx - 0.03, y + sy * 0.10, 0.80), (dx + 0.03, y + sy * 0.40, 1.04), M_METAL)
    cyl(bm, (-0.2, y + sy * 0.32, 0.92), (0.2, y + sy * 0.32, 0.92), 0.04, M_METAL, 10)
    cyl(bm, (0, y + sy * 0.32, 0.92), (0, y + sy * 0.62, 0.92), 0.08, M_ORANGE, 16)
    cyl(bm, (0, y + sy * 0.62, 0.92), (0, y + sy * 0.74, 0.92), 0.16, M_ORANGE, 20)
    for dx in (-0.55, 0.55):
        cyl(bm, (dx, y + sy * 0.10, 0.92), (dx, y + sy * 0.25, 0.92), 0.07, M_METAL, 14)
        cyl(bm, (dx, y + sy * 0.25, 0.92), (dx, y + sy * 0.27, 0.92), 0.05, M_ORANGE, 14)
    if sy < 0:                                                                                  # the lidar on the front node
        cyl(bm, (0, y, 1.12), (0, y, 1.30), 0.05, M_METAL, 10); cyl(bm, (0, y, 1.30), (0, y, 1.42), 0.10, M_HULL, 20)
        cyl(bm, (0, y, 1.33), (0, y, 1.38), 0.105, M_GLASS, 20)

def spring(bm, a, b, r, turns=7):
    a, b = Vector(a), Vector(b); ax = (b - a); L = ax.length; u = ax.normalized()
    e1 = u.orthogonal().normalized(); e2 = u.cross(e1); pts = []
    for k in range(turns * 12 + 1):
        t = k / (turns * 12); ang = 2 * math.pi * turns * t
        pts.append(a + u * (L * t) + (e1 * math.cos(ang) + e2 * math.sin(ang)) * r)
    for p, q in zip(pts, pts[1:]): cyl(bm, p, q, 0.012, M_SPRING, 5)

def wheel_module(bm, sx, y0, show=True):
    hub = Vector((sx * HUBX, y0, HUBZ))
    kL, kU = Vector((sx * 1.58, y0, 0.44)), Vector((sx * 1.55, y0, 1.02))                     # ball joints on the knuckle
    for dy in (-0.30, 0.30): tube(bm, (sx * 1.15, y0 + dy, 0.80), kL, 0.045)                    # lower wishbone
    tube(bm, (sx * 1.15, y0 - 0.30, 0.80), (sx * 1.15, y0 + 0.30, 0.80), 0.05)
    for dy in (-0.22, 0.22): tube(bm, (sx * 1.15, y0 + dy, 1.20), kU, 0.038)                    # upper wishbone
    for p in (kL, kU): cyl(bm, p - Vector((0, 0.05, 0)), p + Vector((0, 0.05, 0)), 0.055, M_HULL, 12)
    box(bm, (sx * 1.56 - 0.06, y0 - 0.09, 0.44), (sx * 1.56 + 0.06, y0 + 0.09, 1.02), M_HULL)   # the knuckle (upright)
    # steering: an arm on the knuckle, a linear actuator to the node
    arm_end = Vector((sx * 1.50, y0 - 0.28, 0.92))
    tube(bm, (sx * 1.56, y0 - 0.08, 0.92), arm_end, 0.03, M_HULL)
    cyl(bm, (sx * 1.15, y0 - 0.30, 0.95), (sx * 1.32, y0 - 0.29, 0.94), 0.05, M_HULL, 12)
    tube(bm, (sx * 1.32, y0 - 0.29, 0.94), arm_end, 0.022)
    # the active strut: from the lower wishbone up to the deck frame - body, spring, actuator motor on top
    s0 = Vector((sx * 1.42, y0 + 0.17, 0.60)); s1 = Vector((sx * 1.28, y0 + 0.17, 1.30))
    cyl(bm, s0, s0 + (s1 - s0) * 0.55, 0.035, M_METAL, 10)
    cyl(bm, s0 + (s1 - s0) * 0.45, s1, 0.055, M_HULL, 12)
    spring(bm, s0 + (s1 - s0) * 0.12, s0 + (s1 - s0) * 0.80, 0.085)
    box(bm, (s1.x - 0.08, s1.y - 0.08, 1.20), (s1.x + 0.08, s1.y + 0.08, 1.28), M_ORANGE)
    # hub motor + planetary reduction + parking brake, inside the wheel
    cyl(bm, hub + Vector((sx * -0.24, 0, 0)), hub + Vector((sx * 0.10, 0, 0)), 0.30, M_METAL, 32)
    cyl(bm, hub + Vector((sx * -0.27, 0, 0)), hub + Vector((sx * -0.24, 0, 0)), 0.33, M_HULL, 32)
    box(bm, (hub.x - 0.05 - sx * 0.22, y0 - 0.10, HUBZ + 0.24), (hub.x + 0.05 - sx * 0.22, y0 + 0.10, HUBZ + 0.36), M_RED)
    if not show: return
    wheel(bm, hub, sx)
    # fender on the knuckle: an arc over the wheel's top, turning with it
    pts = [Vector((0, math.cos(a_) * 0.98, math.sin(a_) * 0.98)) for a_ in [math.radians(40 + 100 * k / 10) for k in range(11)]]
    for p, q in zip(pts, pts[1:]):
        quad(bm, [(hub.x - 0.30, y0 + p.y, HUBZ + p.z), (hub.x + 0.30, y0 + p.y, HUBZ + p.z), (hub.x + 0.30, y0 + q.y, HUBZ + q.z), (hub.x - 0.30, y0 + q.y, HUBZ + q.z)], M_HULL)
        quad(bm, [(hub.x - 0.30, y0 + q.y, HUBZ + q.z), (hub.x + 0.30, y0 + q.y, HUBZ + q.z), (hub.x + 0.30, y0 + p.y, HUBZ + p.z), (hub.x - 0.30, y0 + p.y, HUBZ + p.z)], M_HULL)
    tube(bm, (sx * 1.56, y0, 1.02), (hub.x, y0, HUBZ + 0.98), 0.025, M_HULL)
    # the motor's cable from the node to the hub, in a loop (travel and steering)
    c = [Vector((sx * 1.15, y0 + 0.30, 1.05)), Vector((sx * 1.35, y0 + 0.40, 0.95)), Vector((sx * 1.55, y0 + 0.30, 0.80)), Vector((sx * 1.62, y0 + 0.12, 0.72))]
    for p, q in zip(c, c[1:]): cyl(bm, p, q, 0.022, M_CABLE, 8)

def steps(bm):
    for k in range(3): box(bm, (1.55, -0.4, 0.36 + k * 0.36), (1.90, 0.4, 0.42 + k * 0.36), M_METAL)
    for yy in (-0.42, 0.42): box(bm, (1.55, yy - 0.03, 0.30), (1.90, yy + 0.03, 1.10), M_METAL)

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    MATS = [mat("PHull", (0.11, 0.12, 0.135), 0.6, 0.3), mat("PWhite", (0.85, 0.86, 0.87), 0.45), mat("PMetal", (0.40, 0.41, 0.43), 0.3, 0.8),
            mat("PGlass", (0.05, 0.09, 0.13), 0.05, 0.4), mat("PTread", (0.05, 0.05, 0.055), 0.85), mat("PRed", (0.72, 0.12, 0.10), 0.4),
            mat("PLamp", (1.0, 0.96, 0.85), 0.15), mat("PSeam", (0.04, 0.04, 0.045), 0.7), mat("PBlade", (0.20, 0.22, 0.26), 0.35, 0.6),
            mat("POrange", (0.90, 0.50, 0.10), 0.5), mat("PCable", (0.08, 0.08, 0.09), 0.9), mat("PSpring", (0.75, 0.70, 0.20), 0.3, 0.9)]
    sys.path.insert(0, HERE); import render_util
    O = Vector((3.4, 6.9, 0.0))
    b = bmesh.new()
    backbone(b)
    for sy in (-1, 1): bumper(b, sy)
    for y0 in AX:
        for sx in (-1, 1): wheel_module(b, sx, y0)
    steps(b)
    obj("Chassis", b, MATS)
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (10, 10, 1)
    sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.2; sun.angle = 0.05
    so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 400
    shots = [("34", 'PERSP', 32, Vector((-8.5, -7.5, 4.5)), Vector((0, -0.5, 0.9))),
             ("front", 'ORTHO', 6.5, Vector((0, -40, 0.9)), Vector((0, 0, 0.9))),
             ("side", 'ORTHO', 11.5, Vector((-40, 0, 1.0)), Vector((0, 0, 1.0))),
             ("wheel", 'PERSP', 40, Vector((-4.2, -5.0, 1.6)), Vector((-1.5, -3.3, 0.8))),
             ("under", 'PERSP', 28, Vector((-5.5, -6.0, 0.25)), Vector((0, -1.0, 0.75)))]
    for name, kind, lens, loc, tgt in shots:
        cam.data.type = kind; cam.rotation_mode = 'QUATERNION'
        if kind == 'ORTHO': cam.data.ortho_scale = lens
        else: cam.data.lens = lens
        cam.location = O + loc; t = O + tgt
        cam.rotation_quaternion = (t - cam.location).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "chassis_proto_%s.png" % name); bpy.ops.render.render(write_still=True)
    # the suspension seen: the near (-x) wheels and fenders left off
    bpy.data.objects.remove(bpy.data.objects["Chassis"], do_unlink=True)
    b = bmesh.new(); backbone(b)
    for sy in (-1, 1): bumper(b, sy)
    for y0 in AX:
        for sx in (-1, 1): wheel_module(b, sx, y0, show=(sx > 0))
    obj("Chassis2", b, MATS)
    for name, loc, tgt, lens in (("susp", Vector((-4.6, -5.6, 1.7)), Vector((-1.4, -3.0, 0.85)), 34), ("susp_side", Vector((-7.5, -1.0, 1.2)), Vector((-1.3, -1.0, 0.85)), 30)):
        cam.data.type = 'PERSP'; cam.data.lens = lens; cam.location = O + loc; t = O + tgt
        cam.rotation_quaternion = (t - cam.location).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "chassis_proto_%s.png" % name); bpy.ops.render.render(write_still=True)
    log.append("rendered")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
