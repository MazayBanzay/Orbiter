# МПУ - the test sample (open platform, a control post with a joystick up front, no cabin): mesh for Orbiter + geometry header.
# blender --background astronavigator_suit.blend --python build_mpu.py
#   -> Meshes\MPU\MPU.msh, Orbitersdk\samples\MPU\MpuGeo.h, renders\mpu_test_side.png / _34.png (with her for scale)
# Blender axes: x across, y along (front -y), z up. Orbiter: x right, y up, z forward  =>  (x, y, z)_orb = (-x, z - CG_H, -y).
# Groups: one per (part, material); the header lists the groups of each arm and each wheel for the animations.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
ORB = os.path.abspath(os.path.join(HERE, "..", ".."))
MESH_OUT = os.path.join(ORB, "Meshes", "MPU", "MPU.msh"); GEO_OUT = os.path.join(ORB, "Orbitersdk", "samples", "MPU", "MpuGeo.h")
LOG = os.path.join(HERE, "build_mpu.log"); log = []
CG_H = 1.0                                        # the vessel's origin (centre of gravity) above the ground at rest
WHEEL_R, WHEEL_W = 0.70, 0.52
AXLES = (-3.30, -1.65, 1.65, 3.30)                # blender y of the axles (front first)
HUB_X = 1.85

MATS = [("Hull", (0.11, 0.12, 0.135), 0.0), ("Deck", (0.20, 0.21, 0.23), 0.0), ("Metal", (0.32, 0.33, 0.35), 0.0),
        ("Glass", (0.05, 0.09, 0.13), 0.0), ("Tread", (0.05, 0.05, 0.055), 0.0), ("Red", (0.72, 0.12, 0.10), 0.0),
        ("Lamp", (1.0, 0.96, 0.85), 1.0), ("Seam", (0.04, 0.04, 0.045), 0.0), ("Blade", (0.20, 0.22, 0.26), 0.0),
        ("White", (0.85, 0.86, 0.87), 0.0)]
MI = {n: i for i, (n, _, _) in enumerate(MATS)}
parts = {}                                        # part name -> list of (verts, faces, material index) triangles collected

class Part:
    def __init__(self): self.v = []; self.f = []      # per material: v list [(co, n)], faces
    pass

def part(name):
    if name not in parts: parts[name] = {}
    return parts[name]

def add_poly(pn, pts, mi):
    """a planar polygon (list of Vectors, CCW seen from outside) to part pn, material mi"""
    d = part(pn).setdefault(mi, ([], []))
    n = (pts[1] - pts[0]).cross(pts[2] - pts[0])
    n = n.normalized() if n.length > 1e-9 else Vector((0, 0, 1))
    base = len(d[0])
    for p in pts: d[0].append((p.copy(), n))
    for k in range(1, len(pts) - 1): d[1].append((base, base + k, base + k + 1))

def ring(c, ax, r, seg):
    ax = ax.normalized(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    return [c + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r for j in range(seg)]

def loft(pn, rings, mi, cap=True, closed=False, smooth=True):
    d = part(pn).setdefault(mi, ([], [])); n = len(rings[0]); k = len(rings)
    cen = [sum(r, Vector()) / n for r in rings]
    if smooth:
        base = len(d[0])
        for i, r in enumerate(rings):
            for p in r:
                nn = (p - cen[i]); nn = nn.normalized() if nn.length > 1e-9 else Vector((0, 0, 1)); d[0].append((p.copy(), nn))
        for i in range(k if closed else k - 1):
            a, b = base + i * n, base + ((i + 1) % k) * n
            for j in range(n):
                j1 = (j + 1) % n
                d[1].append((a + j, a + j1, b + j1)); d[1].append((a + j, b + j1, b + j))
    else:
        for i in range(k if closed else k - 1):
            A, B = rings[i], rings[(i + 1) % k]
            for j in range(n):
                j1 = (j + 1) % n; add_poly(pn, [A[j], A[j1], B[j1], B[j]], mi)
    if cap and not closed:
        add_poly(pn, list(reversed(rings[0])), mi); add_poly(pn, list(rings[-1]), mi)

def cyl(pn, a, b, r, mi, seg=24):
    a, b = Vector(a), Vector(b); loft(pn, [ring(a, b - a, r, seg), ring(b, b - a, r, seg)], mi)

def box(pn, lo, hi, mi):
    lo, hi = Vector(lo), Vector(hi)
    P = [Vector((x, y, z)) for z in (lo.z, hi.z) for y in (lo.y, hi.y) for x in (lo.x, hi.x)]
    for q in ((0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)):
        add_poly(pn, [P[i] for i in q], mi)

def oct_ring(y, hw, z0, z1, ch):
    return [Vector(p) for p in ((-hw + ch, y, z0), (hw - ch, y, z0), (hw, y, z0 + ch), (hw, y, z1 - ch), (hw - ch, y, z1), (-hw + ch, y, z1), (-hw, y, z1 - ch), (-hw, y, z0 + ch))]

def wheel(pn, c, sx):
    c = Vector(c); ax = Vector((1, 0, 0)); w, r = WHEEL_W, WHEEL_R
    a, b = c - ax * w / 2, c + ax * w / 2
    loft(pn, [ring(a, ax, r - 0.09, 48), ring(a, ax, r, 48), ring(b, ax, r, 48), ring(b, ax, r - 0.09, 48)], MI["Tread"], cap=False, closed=True, smooth=False)
    for k in range(20):
        t = 2 * math.pi * k / 20
        for s in (-1, 1):
            p0 = c + Vector((0, math.cos(t), math.sin(t))) * (r + 0.01)
            p1 = c + Vector((s * w * 0.46, math.cos(t + 0.10) * (r + 0.01), math.sin(t + 0.10) * (r + 0.01)))
            cyl(pn, p0, p1, 0.018, MI["Tread"], 4)
    for k in range(20):
        t0 = 2 * math.pi * k / 20; pts = []
        for i in range(7):
            u = i / 6; rr = 0.30 + (r - 0.40) * u; tt = t0 + 0.35 * math.sin(math.pi * u); pts.append((rr, tt))
        for i in range(6):
            (r0, t0_), (r1, t1_) = pts[i], pts[i + 1]
            q = [c + Vector((-w * 0.38, math.cos(t0_) * r0, math.sin(t0_) * r0)), c + Vector((-w * 0.38, math.cos(t1_) * r1, math.sin(t1_) * r1)),
                 c + Vector((w * 0.38, math.cos(t1_) * r1, math.sin(t1_) * r1)), c + Vector((w * 0.38, math.cos(t0_) * r0, math.sin(t0_) * r0))]
            add_poly(pn, q, MI["Blade"]); add_poly(pn, q[::-1], MI["Blade"])
    cyl(pn, c - ax * 0.20, c + ax * 0.20, 0.31, MI["Metal"], 40)
    cyl(pn, c + ax * sx * 0.20, c + ax * sx * 0.235, 0.22, MI["Hull"], 40)
    cyl(pn, c + ax * sx * 0.235, c + ax * sx * 0.245, 0.08, MI["Red"], 20)

def to_orb(p): return Vector((-p.x, p.z - CG_H, -p.y))

try:
    # ---------------- body: spine, deck, module rail, lamps, the control post up front ----------------
    st = [(-4.40, 1.05, 0.85, 1.30, 0.25), (-4.00, 1.30, 0.60, 1.35, 0.30), (-2.9, 1.40, 0.53, 1.35, 0.30), (2.9, 1.40, 0.53, 1.35, 0.30),
          (4.00, 1.30, 0.60, 1.35, 0.30), (4.35, 1.10, 0.85, 1.30, 0.25)]
    loft("Body", [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in st], MI["Hull"], smooth=False)
    for yy in (-2.9, -1.0, 1.0, 2.9):
        loft("Body", [oct_ring(yy - 0.01, 1.405, 0.525, 1.355, 0.30), oct_ring(yy + 0.01, 1.405, 0.525, 1.355, 0.30)], MI["Seam"], smooth=False)
    for sx in (-1, 1): box("Body", (sx * 1.415 - 0.01, -3.8, 1.06), (sx * 1.415 + 0.01, 3.8, 1.10), MI["Red"])
    box("Body", (-1.0, -4.46, 1.00), (1.0, -4.40, 1.12), MI["Lamp"])
    for sx in (-1, 1): box("Body", (sx * 0.75 - 0.18, -4.47, 0.92), (sx * 0.75 + 0.18, -4.41, 0.97), MI["Lamp"])
    # the platform deck over the spine (the module seat): plates with tie-down rails
    box("Body", (-2.05, -3.0, 1.35), (2.05, 4.1, 1.42), MI["Deck"])
    for xx in (-1.6, -0.55, 0.55, 1.6): box("Body", (xx - 0.04, -2.9, 1.42), (xx + 0.04, 4.0, 1.47), MI["Metal"])
    for sx in (-1, 1): box("Body", (sx * 2.05 - 0.05, -3.0, 1.42), (sx * 2.05 + 0.05, 4.1, 1.62), MI["Hull"])   # low side coaming
    # control post: a floor plate, a pedestal with a sloped panel, a joystick, a waist hoop for the standing driver
    box("Body", (-1.0, -4.2, 1.35), (1.0, -3.0, 1.42), MI["Deck"])
    box("Body", (-0.45, -4.15, 1.42), (0.45, -3.85, 2.30), MI["Hull"])
    add_poly("Body", [Vector((-0.42, -3.85, 2.30)), Vector((0.42, -3.85, 2.30)), Vector((0.42, -4.12, 2.42)), Vector((-0.42, -4.12, 2.42))], MI["Glass"])
    box("Body", (-0.45, -4.15, 2.30), (0.45, -4.12, 2.44), MI["Hull"])
    cyl("Body", (0.22, -3.78, 2.20), (0.22, -3.62, 2.42), 0.018, MI["Metal"], 10)                          # joystick
    cyl("Body", (0.22, -3.62, 2.42), (0.22, -3.60, 2.50), 0.035, MI["Hull"], 12)
    box("Body", (0.12, -3.85, 2.12), (0.32, -3.62, 2.20), MI["Hull"])
    hoop = [Vector((-0.75 * math.cos(math.pi * k / 12), -3.25 - 0.55 * math.sin(math.pi * k / 12), 2.35)) for k in range(13)]
    for p, q in zip(hoop, hoop[1:]): cyl("Body", p, q, 0.03, MI["Metal"], 8)
    for sx in (-1, 1): cyl("Body", (sx * 0.75, -3.25, 1.42), (sx * 0.75, -3.25, 2.35), 0.03, MI["Metal"], 8)
    for k in range(3):                                                                              # steps on the driver's left (orbiter -x)
        box("Body", (2.08, -2.9, 0.36 + k * 0.36), (2.42, -2.1, 0.42 + k * 0.36), MI["Metal"])
    for yy in (-2.92, -2.08): box("Body", (2.08, yy - 0.03, 0.30), (2.42, yy + 0.03, 1.36), MI["Metal"])
    cyl("Body", (0.9, -3.9, 1.42), (0.9, -3.9, 2.9), 0.02, MI["Metal"], 8); cyl("Body", (0.9, -3.9, 2.9), (0.9, -3.9, 2.97), 0.07, MI["Lamp"], 12)   # beacon mast
    # ---------------- arms and wheels (each its own part) ----------------
    geo = []
    for i, yw in enumerate(AXLES):
        for sx in (-1, 1):
            idx = len(geo)
            hub = Vector((sx * HUB_X, yw, WHEEL_R)); piv = Vector((sx * 1.42, yw - 0.80 * (1 if yw < 0 else -1), 1.02))
            an = "Arm%d" % idx; wn = "Wheel%d" % idx
            for dxx in (-0.06, 0.06):
                cyl(an, (piv.x + dxx * sx, piv.y, piv.z), (hub.x - sx * 0.27 + dxx * sx, hub.y, hub.z + 0.05), 0.07, MI["Metal"], 12)
            cyl(an, (piv.x - sx * 0.02, piv.y, piv.z), (piv.x + sx * 0.12, piv.y, piv.z), 0.13, MI["Hull"], 20)
            wheel(wn, hub, sx)
            geo.append((to_orb(hub), to_orb(piv)))
    # ---------------- export: groups per (part, material) ----------------
    names = ["Body"] + ["Arm%d" % i for i in range(8)] + ["Wheel%d" % i for i in range(8)]
    groups, gidx = [], {}
    for pn in names:
        for mi in sorted(parts[pn]):
            gidx.setdefault(pn, []).append(len(groups)); groups.append((pn, mi, parts[pn][mi]))
    os.makedirs(os.path.dirname(MESH_OUT), exist_ok=True)
    with open(MESH_OUT, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(groups))
        for pn, mi, (vs, fs) in groups:
            f.write("LABEL %s_%s\nMATERIAL %d\nTEXTURE 0\nGEOM %d %d\n" % (pn, MATS[mi][0], mi + 1, len(vs), len(fs)))
            for p, n in vs:
                q = to_orb(p); m = Vector((-n.x, n.z, -n.y))
                f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (q.x, q.y, q.z, m.x, m.y, m.z))
            for a, b, c in fs: f.write("%d %d %d\n" % (a, c, b))
        f.write("MATERIALS %d\n" % len(MATS))
        for n, _, _ in MATS: f.write("MPU_%s\n" % n)
        for n, col, em in MATS:
            spec = 0.6 if n in ("Metal", "Glass", "Blade") else 0.15
            f.write("MATERIAL MPU_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 %d\n%.3f %.3f %.3f 1\n" %
                    (n, *col, *col, spec, spec, spec, 40 if spec > 0.3 else 10, *(c * em for c in col)))
    # ---------------- header for the module ----------------
    os.makedirs(os.path.dirname(GEO_OUT), exist_ok=True)
    with open(GEO_OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Written by Tantra_Design/blender/build_mpu.py - do not edit. Orbiter vessel frame (x right, y up, z forward).\n#pragma once\n\n")
        f.write("namespace mpu {\nconstexpr double kCgH = %.3f, kWheelR = %.3f, kWheelW = %.3f;\nconstexpr int kWheels = 8;\n" % (CG_H, WHEEL_R, WHEEL_W))
        f.write("struct WheelGeo { double hub[3], pivot[3]; int armGrp[4]; int nArm; int wheelGrp[8]; int nWheel; };\n")
        f.write("constexpr WheelGeo kWheel[kWheels] = {\n")
        for i, (h, p) in enumerate(geo):
            ag = gidx["Arm%d" % i]; wg = gidx["Wheel%d" % i]
            f.write("    {{%.4f, %.4f, %.4f}, {%.4f, %.4f, %.4f}, {%s}, %d, {%s}, %d},\n" % (h.x, h.y, h.z, p.x, p.y, p.z,
                    ", ".join(map(str, ag + [0] * (4 - len(ag)))), len(ag), ", ".join(map(str, wg + [0] * (8 - len(wg)))), len(wg)))
        f.write("};\nconstexpr double kDriverEye[3] = {%.3f, %.3f, %.3f};\n}  // namespace mpu\n" % tuple(to_orb(Vector((0.0, -3.30, 1.42 + 1.62)))))
    log.append("groups %d, verts %d" % (len(groups), sum(len(v) for _, _, (v, f) in groups)))
    # ---------------- preview with her for scale ----------------
    if os.environ.get("MPU_PREVIEW", "1") == "1":
        for o in bpy.data.objects:
            if o.name.startswith("Jet"): o.hide_render = True
        bm = bmesh.new(); mats = []
        for n, col, em in MATS:
            m = bpy.data.materials.new("MPUp_" + n); m.use_nodes = True
            bs = next(x for x in m.node_tree.nodes if x.type == 'BSDF_PRINCIPLED')
            bs.inputs["Base Color"].default_value = (*col, 1); bs.inputs["Roughness"].default_value = 0.4 if n in ("Metal", "Glass") else 0.6
            bs.inputs["Metallic"].default_value = 0.7 if n in ("Metal", "Blade") else 0.2; mats.append(m)
        OFF = Vector((3.0, 6.9, 0))
        for pn, mi, (vs, fs) in groups:
            base = [bm.verts.new(p + OFF) for p, n in vs]
            for a, b, c in fs:
                try: bm.faces.new((base[a], base[b], base[c])).material_index = mi
                except ValueError: pass
        me = bpy.data.meshes.new("MPU"); bm.to_mesh(me); bm.free()
        for m in mats: me.materials.append(m)
        ob = bpy.data.objects.new("MPU", me); bpy.context.scene.collection.objects.link(ob)
        sys.path.insert(0, HERE); import render_util
        cam = render_util.setup_stage()
        fl = bpy.data.objects.get("Floor")
        if fl: fl.scale = (8, 8, 1)
        sun = bpy.data.lights.new("MSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
        so = bpy.data.objects.new("MSun", sun); bpy.context.scene.collection.objects.link(so)
        so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
        sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 300
        cam.data.type = 'ORTHO'; cam.data.ortho_scale = 13.0
        cam.location = (-60, 5.9, 1.9); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_test_side.png"); bpy.ops.render.render(write_still=True)
        cam.data.type = 'PERSP'; cam.data.lens = 35
        loc = Vector((-9.5, -6.5, 5.0)); tgt = Vector((OFF.x, OFF.y - 1.0, 1.4))
        cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_test_34.png"); bpy.ops.render.render(write_still=True)
        log.append("rendered")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
