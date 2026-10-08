# Э.МПУ on the chassis v2 - three looks built on the approved layout (a look only, nothing for the game).
# blender --background astronavigator_coverall.blend --python mock_empu_v2.py
# Common: the deck at 1.62 m; a framed nose with lower glazing (the ground by the front wheels in view); the pressure hull
# within the frame's width (+-1.47 m) up to above the fenders; side system bays over the fenders (gases, CO2, water on the left;
# the thermal loop, heaters, water on the right) with service hatches; the roof: tilting radiators and a ~10 m2 solar panel,
# a mast; the airlock in the stern, its door and stairs; the couplings on the chassis.
# A "Bays": a faceted hull with the system bays as boxes along its flanks.
# B "Raised cab": the cab's roof higher (the driver sees over the train), the habitat lower; the tanks in open racks.
# C "Exoskeleton": the hull inside an external load frame (ribs and rails) carrying the bays, the radiators, the mast.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
src = open(os.path.join(HERE, "mock_mpu.py"), encoding="utf-8").read()
exec(src[src.index("import bpy"):src.index("try:\n    for o in bpy.data.objects")])
src2 = open(os.path.join(HERE, "mock_chassis_v2.py"), encoding="utf-8").read()
exec(src2[src2.index("M_ORANGE, M_CABLE"):src2.index("try:\n    for o in bpy.data.objects")])
LOG = os.path.join(HERE, "mock_empu_v2.log"); log = []
M_SOLAR, M_RAD = 13, 14
D = 1.62                                                   # the deck

def inset_pane(bm, pts, depth=0.05, rim=0.09):
    P = [Vector(p) for p in pts]; c = sum(P, Vector()) / len(P)
    n = (P[1] - P[0]).cross(P[2] - P[0]).normalized()
    Q = [p + (c - p) * min(rim / max((c - p).length, 1e-6), 0.45) - n * depth for p in P]
    bm.faces.new([bm.verts.new(O + q) for q in Q]).material_index = M_GLASS
    for a in range(len(P)):
        b = (a + 1) % len(P)
        bm.faces.new([bm.verts.new(O + P[a]), bm.verts.new(O + P[b]), bm.verts.new(O + Q[b]), bm.verts.new(O + Q[a])]).material_index = M_HULL

def shell(bm, R, glass, frame_rings=(), mi=None):
    rings = [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in R]
    for s_, (A, B) in enumerate(zip(rings, rings[1:])):
        for j in range(8):
            j1 = (j + 1) % 8; q = [A[j], A[j1], B[j1], B[j]]
            if s_ in glass and j in glass[s_]: inset_pane(bm, q)
            else: quad(bm, q, M_WHITE if mi is None else mi)
    quad(bm, list(reversed(rings[0])), M_WHITE); quad(bm, list(rings[-1]), M_WHITE)
    for k in frame_rings:
        for a, b in zip(rings[k], rings[k][1:] + rings[k][:1]): cyl(bm, a, b, 0.07, M_WHITE, 6)
    for k in range(len(rings) - 1):
        if k in frame_rings and k + 1 in frame_rings:
            for j in range(8): cyl(bm, rings[k][j], rings[k + 1][j], 0.07, M_WHITE, 6)
    return rings

def bay(bm, sx, y0, y1, z0, z1, xin=1.47, xout=2.02, hatches=3):
    """a system bay over the fenders: a chamfered box from the hull out, service hatches with handles, a louvred vent"""
    ch = 0.12
    rs = []
    for y in (y0, y1):
        x0, x1 = (xin, xout) if sx > 0 else (-xout, -xin)
        rs.append([Vector(p) for p in ((x0, y, z0), (x1 - (ch if sx > 0 else 0), y, z0), (x1, y, z0 + (ch if sx > 0 else 0)),
                                       (x1, y, z1 - (ch if sx > 0 else 0)), (x1 - (ch if sx > 0 else 0), y, z1), (x0 + (0 if sx > 0 else ch), y, z1),
                                       (x0, y, z1 - (0 if sx > 0 else ch)), (x0, y, z0 + (0 if sx > 0 else ch)))])
    loft(bm, rs, M_WHITE)
    L = (y1 - y0) / hatches
    for k in range(hatches):
        ya, yb = y0 + k * L + 0.05, y0 + (k + 1) * L - 0.05
        x = sx * (xout + 0.004)
        box(bm, (x - 0.004, ya, z0 + 0.15), (x + 0.004, yb, z1 - 0.15), M_SEAM)
        box(bm, (x - 0.01, ya + 0.04, z0 + 0.19), (x + 0.01, yb - 0.04, z1 - 0.19), M_WHITE)
        box(bm, (x - 0.03, 0.5 * (ya + yb) - 0.08, 0.5 * (z0 + z1) - 0.03), (x + 0.03, 0.5 * (ya + yb) + 0.08, 0.5 * (z0 + z1) + 0.03), M_ORANGE)
    for k in range(6):                                                 # a vent on the bay's top
        yy = y0 + 0.3 + k * 0.08
        box(bm, (sx * (xin + 0.12), yy, z1), (sx * (xout - 0.18), yy + 0.04, z1 + 0.02), M_SEAM)

def tilt_panel(bm, sx, y0, y1, hinge_x, hinge_z, w, ang, mi):
    """a panel hinged along y at (hinge_x, hinge_z), width w outwards, raised by ang (rad)"""
    p0 = Vector((hinge_x, 0, hinge_z)); d = Vector((sx * math.cos(ang), 0, math.sin(ang))) * w
    for th in (0.0, 0.03):
        q = [p0 + Vector((0, y0, th)), p0 + d + Vector((0, y0, th)), p0 + d + Vector((0, y1, th)), p0 + Vector((0, y1, th))]
        quad(bm, q if (th > 0) == (sx > 0) else q[::-1], mi)
    for yy in (y0 + 0.3, 0.5 * (y0 + y1), y1 - 0.3): cyl(bm, p0 + Vector((0, yy, 0)), p0 + d * 0.5 + Vector((0, yy, -0.25)), 0.03, M_METAL, 8)

def roof(bm, ztop, y0, y1, hw):
    tilt_panel(bm, 1, y0, y1, hw - 0.05, ztop + 0.10, 1.25, math.radians(28), M_RAD)      # radiators, tilted to the sky
    tilt_panel(bm, -1, y0, y1, -hw + 0.05, ztop + 0.10, 1.25, math.radians(28), M_RAD)
    box(bm, (-0.95, y0 + 0.2, ztop + 0.12), (0.95, y1 - 0.2, ztop + 0.16), M_SOLAR)          # the solar panel ~9.5 m2
    for x in (-0.8, 0.8):
        for y in (y0 + 0.4, y1 - 0.4): box(bm, (x - 0.04, y - 0.04, ztop), (x + 0.04, y + 0.04, ztop + 0.12), M_METAL)

def mast(bm, x, y, z):
    cyl(bm, (x, y, z), (x, y, z + 0.55), 0.035, M_METAL, 8); cyl(bm, (x, y, z + 0.55), (x, y - 0.12, z + 0.80), 0.24, M_WHITE, 20)
    cyl(bm, (x + 0.35, y, z), (x + 0.35, y, z + 0.30), 0.03, M_METAL, 8); cyl(bm, (x + 0.35, y, z + 0.30), (x + 0.35, y, z + 0.40), 0.08, M_HULL, 16)

def stern_lock(bm, y0, y1, hw, z1, door=True):
    shell(bm, [(y0, hw, D, z1, 0.35), (y1 - 0.25, hw, D, z1, 0.35), (y1, hw - 0.15, D + 0.05, z1 - 0.15, 0.30)], {})
    if door:
        y = y1 + 0.005
        box(bm, (-0.46, y, D + 0.06), (0.46, y + 0.03, D + 1.95), M_METAL)
        for a, b in (((-0.54, D + 0.02), (0.54, D + 0.06)), ((-0.54, D + 1.95), (0.54, D + 2.02)), ((-0.54, D + 0.02), (-0.46, D + 2.02)), ((0.46, D + 0.02), (0.54, D + 2.02))):
            box(bm, (a[0], y, a[1]), (b[0], y + 0.05, b[1]), M_HULL)
        cyl(bm, (0, y + 0.03, D + 1.45), (0, y + 0.05, D + 1.45), 0.12, M_GLASS, 16); box(bm, (0.30, y + 0.03, D + 0.9), (0.36, y + 0.09, D + 1.12), M_ORANGE)
        for k in range(5):                                             # the stairs, out (parked)
            z = D - 0.05 - k * 0.30; yy = y1 + 0.08 + k * 0.27
            box(bm, (-0.45, yy, z - 0.04), (0.45, yy + 0.28, z), M_METAL)
        for sx in (-0.48, 0.48): cyl(bm, (sx, y1 + 0.05, D), (sx, y1 + 1.45, 0.10), 0.03, M_METAL, 8)

def nose(bm, raised=False):
    zt = 3.85 if raised else 3.45
    R = [(-4.30, 1.00, D + 0.10, zt - 0.85, 0.30), (-3.85, 1.42, D + 0.02, zt - 0.25, 0.45), (-3.05, 1.47, D, zt, 0.50), (-2.55, 1.47, D, zt, 0.50)]
    glass = {0: (1, 2, 3, 4, 5, 6, 7), 1: (1, 2, 3, 4, 5, 6, 7), 2: (2, 6)}   # down to the lower chamfers: the wheels in view
    shell(bm, R, glass, frame_rings=(0, 1, 2))
    box(bm, (-0.9, -3.30, zt), (0.9, -3.18, zt + 0.10), M_HULL)
    for x in (-0.75, -0.25, 0.25, 0.75): box(bm, (x - 0.12, -3.31, zt + 0.02), (x + 0.12, -3.30, zt + 0.08), M_LAMP)
    return zt

def var_A(bm):
    zt = nose(bm)
    shell(bm, [(-2.55, 1.47, D, 3.45, 0.50), (2.45, 1.47, D, 3.45, 0.50)], {})
    for yy in (-1.3, 0.0, 1.3):                                         # hull hoops
        r = oct_ring(yy, 1.49, D - 0.02, 3.47, 0.50)
        for a, b in zip(r, r[1:] + r[:1]): cyl(bm, a, b, 0.035, M_HULL, 6)
    for sx in (-1, 1): bay(bm, sx, -2.45, 2.35, 2.20, 2.95)
    stern_lock(bm, 2.45, 4.25, 1.05, 3.30)
    roof(bm, 3.45, -2.4, 2.3, 1.20); mast(bm, 0.6, -2.0, 3.47)

def var_B(bm):
    zt = nose(bm, raised=True)
    shell(bm, [(-2.55, 1.47, D, 3.85, 0.50), (-2.10, 1.47, D, 3.85, 0.50), (-1.70, 1.47, D, 3.30, 0.50), (2.45, 1.47, D, 3.30, 0.50)], {1: (2, 6)})
    for sx in (-1, 1):                                                  # open racks of tanks over the fenders
        for y in (-1.5, 1.4): box(bm, (sx * 1.48, y - 0.04, 2.18), (sx * 2.02, y + 0.04, 2.92), M_METAL)
        box(bm, (sx * 1.48, -1.55, 2.18), (sx * 2.02, 1.45, 2.22), M_METAL)
        for k, z in enumerate((2.40, 2.72)):
            cyl(bm, (sx * 1.75, -1.35, z), (sx * 1.75, 1.25, z), 0.17, M_WHITE if k == 0 else M_ORANGE, 20)
        bay(bm, sx, -2.45, -1.65, 2.20, 2.95, hatches=1); bay(bm, sx, 1.60, 2.35, 2.20, 2.95, hatches=1)
    stern_lock(bm, 2.45, 4.25, 1.05, 3.20)
    roof(bm, 3.30, -1.5, 2.3, 1.20); mast(bm, 0.6, -2.3, 3.87)

def var_C(bm):
    var_A(bm)
    for yy in (-2.40, -1.20, 0.0, 1.20, 2.40):                          # the external load frame: ribs over the hull
        pts = [Vector((sx * 2.06, yy, 2.18)) for sx in (-1,)] + [Vector((-1.60, yy, 3.30)), Vector((-1.05, yy, 3.62)), Vector((1.05, yy, 3.62)), Vector((1.60, yy, 3.30)), Vector((2.06, yy, 2.18))]
        for a, b in zip(pts, pts[1:]): cyl(bm, a, b, 0.07, M_HULL, 8)
        for sx in (-1, 1): cyl(bm, (sx * 2.06, yy, 2.18), (sx * 1.47, yy, D + 0.02), 0.06, M_HULL, 8)
    for x, z in ((-1.05, 3.62), (1.05, 3.62), (-2.06, 2.18), (2.06, 2.18)):   # rails along it
        cyl(bm, (x, -2.6, z), (x, 2.6, z), 0.055, M_HULL, 8)

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    MATS = [mat("PHull", (0.11, 0.12, 0.135), 0.6, 0.3), mat("PWhite", (0.84, 0.85, 0.86), 0.45), mat("PMetal", (0.40, 0.41, 0.43), 0.3, 0.8),
            mat("PGlass", (0.06, 0.10, 0.14), 0.05, 0.5), mat("PTread", (0.05, 0.05, 0.055), 0.85), mat("PRed", (0.72, 0.12, 0.10), 0.4),
            mat("PLamp", (1.0, 0.96, 0.85), 0.15), mat("PSeam", (0.04, 0.04, 0.045), 0.7), mat("PBlade", (0.20, 0.22, 0.26), 0.35, 0.6),
            mat("POrange", (0.90, 0.50, 0.10), 0.5), mat("PCable", (0.08, 0.08, 0.09), 0.9), mat("PSpring", (0.75, 0.70, 0.20), 0.3, 0.9),
            mat("PGround", (0.4, 0.4, 0.4), 0.9), mat("PSolar", (0.05, 0.08, 0.22), 0.15, 0.6), mat("PRad", (0.92, 0.93, 0.95), 0.25, 0.2)]
    sys.path.insert(0, HERE); import render_util
    O = Vector((3.4, 6.9, 0.0))
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (10, 10, 1)
    sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.2; sun.angle = 0.05
    so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 400
    for name, fn in (("A", var_A), ("B", var_B), ("C", var_C)):
        b = bmesh.new(); chassis_v2(b); fn(b); o = obj("EMPU_" + name, b, MATS)
        for tag, loc, tgt in (("34", Vector((-9.0, -8.0, 4.6)), Vector((0, -0.6, 1.9))), ("rear", Vector((8.5, 10.0, 4.4)), Vector((0, 0.8, 1.8)))):
            cam.data.type = 'PERSP'; cam.data.lens = 32; cam.rotation_mode = 'QUATERNION'
            cam.location = O + loc; t = O + tgt
            cam.rotation_quaternion = (t - cam.location).to_track_quat('-Z', 'Y')
            bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "empu_v2_%s_%s.png" % (name, tag)); bpy.ops.render.render(write_still=True)
        bpy.data.objects.remove(o, do_unlink=True)
        log.append("rendered " + name)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
