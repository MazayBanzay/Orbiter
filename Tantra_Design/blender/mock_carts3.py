# Side views of the three «тележка» variants (anamezon tankers that also carry the crew), the suited astronavigator
# beside each for scale - looks only, nothing for the game.
# blender --background astronavigator_suit.blend --python mock_carts3.py      (CV = A | B | C)
#   A  tracked tanker, 8.5 x 4.3 x 1.1 m stowed, two built-in bottles (~22 t anamezon), dozer blade, folding crew arch
#   B  wheeled modules 6 x 3 x 1.0 m, coupled end to end under a shared trap D2.4 x 6 m (~75 t)
#   C  six-legged carrier, body 5 x 2.5 m, a slung trap D1.6 x 5 m (~28 t)
# Local axes: x across (near side -x, towards the camera), y along (front -y), z up.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
CV = os.environ.get("CV", "A"); LOG = os.path.join(HERE, "mock_carts3.log"); log = []
O = Vector((0, 0, 0))

def mat(name, col, rough=0.5, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    return m

def ring(c, ax, r, seg):
    ax = ax.normalized(); e1 = ax.orthogonal().normalized(); e2 = ax.cross(e1)
    return [c + (e1 * math.cos(2 * math.pi * j / seg) + e2 * math.sin(2 * math.pi * j / seg)) * r for j in range(seg)]

def loft(bm, rings, mi=0, cap=True, closed=False):
    Vs = [[bm.verts.new(O + p) for p in r] for r in rings]; n = len(rings[0])
    for i in range(len(Vs) if closed else len(Vs) - 1):
        A, B = Vs[i], Vs[(i + 1) % len(Vs)]
        for j in range(n): bm.faces.new((A[j], A[(j + 1) % n], B[(j + 1) % n], B[j])).material_index = mi
    if cap and not closed:
        bm.faces.new(Vs[0][::-1]).material_index = mi; bm.faces.new(Vs[-1]).material_index = mi

def cyl(bm, a, b, r, mi=0, seg=24):
    a, b = Vector(a), Vector(b); loft(bm, [ring(a, b - a, r, seg), ring(b, b - a, r, seg)], mi)

def tube(bm, pts, r, mi=0, seg=12):
    pts = [Vector(p) for p in pts]
    for a, b in zip(pts, pts[1:]): cyl(bm, a, b, r, mi, seg)
    for p in pts[1:-1]: loft(bm, [ring(p - Vector((0, 0, r)), Vector((0, 0, 1)), r, seg), ring(p + Vector((0, 0, r)), Vector((0, 0, 1)), r, seg)], mi)

def box(bm, lo, hi, mi=0, bev=0.0):
    r = bmesh.ops.create_cube(bm, size=1.0); vs = r['verts']
    c = (Vector(lo) + Vector(hi)) / 2; s = Vector(hi) - Vector(lo)
    for v in vs: v.co = O + Vector((c.x + v.co.x * s.x, c.y + v.co.y * s.y, c.z + v.co.z * s.z))
    for f in {f for v in vs for f in v.link_faces}: f.material_index = mi
    if bev > 0:
        res = bmesh.ops.bevel(bm, geom=list({e for v in vs for e in v.link_edges}), offset=bev, segments=3, profile=0.5, affect='EDGES')
        for f in res['faces']: f.material_index = mi

def track(bm, cx, w, ys, yi, zc, re, wheels, rw, zw):
    path = []
    for k in range(40): t = k / 40; path.append((yi + (ys - yi) * t, zc + re))
    for k in range(16): a = math.pi / 2 - math.pi * k / 16; path.append((ys + re * math.cos(a), zc + re * math.sin(a)))
    for k in range(40): t = k / 40; path.append((ys - (ys - yi) * t, zc - re))
    for k in range(16): a = -math.pi / 2 - math.pi * k / 16; path.append((yi + re * math.cos(a), zc + re * math.sin(a)))
    rings = []
    for i, (y, z) in enumerate(path):
        yp, zp = path[(i + 1) % len(path)]; ty, tz = yp - y, zp - z; L = math.hypot(ty, tz); ny, nz = tz / L, -ty / L
        rings.append([Vector((cx + dx, y + ny * dn, z + nz * dn)) for dx, dn in ((-w / 2, 0.0), (w / 2, 0.0), (w / 2, -0.05), (-w / 2, -0.05))])
    loft(bm, rings, 1, cap=False, closed=True)
    for i in range(0, len(path), 2):
        y, z = path[i]; yp, zp = path[(i + 1) % len(path)]; ty, tz = yp - y, zp - z; L = math.hypot(ty, tz); ny, nz = tz / L, -ty / L
        c = Vector((cx, y + ny * 0.02, z + nz * 0.02)); box(bm, c - Vector((w / 2, 0.035, 0.02)), c + Vector((w / 2, 0.035, 0.02)), 1)
    for yy in (ys, yi): cyl(bm, (cx - w / 2 + 0.04, yy, zc), (cx + w / 2 - 0.04, yy, zc), re - 0.06, 2, 28)
    for yw in wheels: cyl(bm, (cx - w / 2 + 0.06, yw, zw), (cx + w / 2 - 0.06, yw, zw), rw, 2, 20)

def trap(bm, c, L, D, mi_shell=3, mi_cap=0, mi_red=5):
    """a transport trap: cryostat shell, dark end caps, stiffening rings, a red band"""
    c = Vector(c); a = c + Vector((0, -L / 2, 0)); b = c + Vector((0, L / 2, 0))
    cyl(bm, a + Vector((0, 0.25, 0)), b - Vector((0, 0.25, 0)), D / 2, mi_shell, 40)
    for e, s in ((a, 1), (b, -1)): cyl(bm, e, e + Vector((0, 0.25 * s, 0)), D / 2 - 0.08, mi_cap, 40)
    for k in range(5):
        yy = -L / 2 + 0.6 + k * (L - 1.2) / 4
        cyl(bm, c + Vector((0, yy - 0.05, 0)), c + Vector((0, yy + 0.05, 0)), D / 2 + 0.03, mi_cap, 40)
    cyl(bm, c + Vector((0, -0.25, 0)), c + Vector((0, 0.25, 0)), D / 2 + 0.01, mi_red, 40)

def obj(name, bm, mats):
    me = bpy.data.meshes.new(name); bm.normal_update(); bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:]); bm.to_mesh(me); bm.free()
    for m in mats: me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); return o

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    HULL = mat("VHull", (0.14, 0.15, 0.16), 0.55, 0.35); TRK = mat("VTrack", (0.06, 0.06, 0.065), 0.7, 0.5)
    WHL = mat("VWheel", (0.22, 0.23, 0.25), 0.4, 0.7); WHT = mat("VWhite", (0.84, 0.85, 0.86), 0.4)
    TYR = mat("VTyre", (0.05, 0.05, 0.055), 0.85); RED = mat("VRed", (0.70, 0.12, 0.10), 0.45)
    LMP = mat("VLamp", (1.0, 0.95, 0.8), 0.2); MUS = mat("VMuscle", (0.17, 0.18, 0.20), 0.45, 0.25)
    mats = [HULL, TRK, WHL, WHT, TYR, RED, LMP, MUS]       # 0 hull 1 track 2 wheel/metal 3 white 4 tyre 5 red 6 lamp 7 muscle
    b = bmesh.new()
    if CV == "A":
        L, W, H = 8.5, 4.3, 1.1
        O = Vector((W / 2 + 0.9, 1.3 + 4.75, 0))
        for sx in (-1, 1):
            track(b, sx * 1.75, 0.8, 3.65, -3.65, 0.42, 0.40, [-2.6, -1.3, 0.0, 1.3, 2.6], 0.22, 0.26)
        box(b, (-1.30, -4.0, 0.22), (1.30, 4.1, 0.92), 0, 0.06)                              # hull: battery, drives, the two bottles
        box(b, (-2.15, -4.2, 0.88), (2.15, 4.25, 1.02), 0, 0.03)                             # deck
        for sx in (-1, 1): box(b, (sx * 0.62 - 0.55, -3.6, 1.0), (sx * 0.62 + 0.55, 3.6, 1.10), 3, 0.05)   # bottle covers (white)
        box(b, (-2.16, -4.2, 0.93), (2.16, 4.25, 0.96), 5)                                    # red line
        box(b, (-2.2, -4.75, 0.02), (2.2, -4.62, 0.78), 0, 0.02)                             # blade, lowered for road work
        for sx in (-1, 1): cyl(b, (sx * 1.2, -4.62, 0.45), (sx * 1.2, -4.0, 0.62), 0.07, 2, 12)
        for sx in (-1, 1): cyl(b, (sx * 1.6, -4.21, 0.75), (sx * 1.6, -4.25, 0.75), 0.11, 6, 16)                # lamps
        for yy in (0.4, 3.4):                                                                 # crew arch, raised (folds flat to the deck)
            pts = [(-2.0, yy, 1.02)] + [(-2.0 * math.cos(math.pi * k / 10), yy, 1.02 + 1.35 * math.sin(math.pi * k / 10)) for k in range(1, 10)] + [(2.0, yy, 1.02)]
            tube(b, pts, 0.045, 0)
        tube(b, [(0, 0.4, 2.37), (0, 3.4, 2.37)], 0.04, 0)
        for sx in (-1, 1): tube(b, [(sx * 1.9, 0.4, 1.75), (sx * 1.9, 3.4, 1.75)], 0.03, 0)              # hand rails
        front = -4.75
    elif CV == "B":
        O = Vector((1.5 + 0.9, 1.3 + 6.6, 0))
        for k, y0 in enumerate((-6.05, 0.05)):
            yc = y0 + 3.0
            box(b, (-1.5, yc - 3.0, 0.55), (1.5, yc + 3.0, 1.0), 0, 0.05)                    # module body
            box(b, (-0.85, yc - 2.9, 0.30), (0.85, yc + 2.9, 0.56), 0, 0.03)                 # keel (its own bottle inside)
            box(b, (-1.51, yc - 3.0, 0.72), (1.51, yc + 3.0, 0.76), 5)
            for ya in (-2.1, 0.0, 2.1):
                for sx in (-1, 1):
                    c = Vector((sx * 1.18, yc + ya, 0.42))
                    cyl(b, c - Vector((0.21, 0, 0)), c + Vector((0.21, 0, 0)), 0.42, 4, 32)                     # tyre
                    cyl(b, c - Vector((0.22, 0, 0)), c + Vector((0.22, 0, 0)), 0.22, 2, 20)                     # hub
                    cyl(b, (sx * 0.85, yc + ya, 0.62), c + Vector((-sx * 0.2, 0, 0.0)), 0.06, 0, 10)            # swing arm
                    cyl(b, (sx * 1.18, yc + ya, 0.62), (sx * 1.18, yc + ya, 0.95), 0.07, 2, 10)                 # steering king pin
            for e in (-3.0, 3.0): box(b, (-0.3, yc + e - 0.08, 0.6), (0.3, yc + e + 0.08, 0.9), 2, 0.02)      # couplers
        for yy in (-2.0, 2.0): box(b, (-1.0, yy - 0.3, 1.0), (1.0, yy + 0.3, 1.45), 0, 0.04)                 # saddles
        trap(b, (0, 0, 2.35), 6.0, 2.4)
        box(b, (-1.5, -6.6, 0.02), (1.5, -6.47, 0.65), 0, 0.02)                              # blade on the lead module
        cyl(b, (0, -6.47, 0.35), (0, -6.05, 0.6), 0.07, 2, 12)
        for yy in (4.6, 5.9):                                                                 # crew arch on the rear deck
            pts = [(-1.4, yy, 1.0)] + [(-1.4 * math.cos(math.pi * k / 10), yy, 1.0 + 1.2 * math.sin(math.pi * k / 10)) for k in range(1, 10)] + [(1.4, yy, 1.0)]
            tube(b, pts, 0.04, 0)
        front = -6.6
    else:
        O = Vector((3.0 + 0.9, 1.3 + 2.9, 0))
        box(b, (-1.25, -2.5, 2.15), (1.25, 2.5, 2.85), 3, 0.25)                               # body (white)
        box(b, (-1.26, -2.3, 2.42), (1.26, 2.3, 2.47), 5)
        box(b, (-0.6, -2.85, 2.35), (0.6, -2.45, 2.75), 0, 0.08)                              # sensor head
        for sx in (-0.35, 0.35): cyl(b, (sx, -2.86, 2.62), (sx, -2.9, 2.62), 0.08, 6, 16)
        for sx in (-1, 1):
            for yh, ys in ((-1.9, -0.5), (0.0, 0.0), (1.9, 0.5)):
                hip = Vector((sx * 1.25, yh, 2.45)); knee = Vector((sx * 2.25, yh + ys * 0.6, 3.05)); foot = Vector((sx * 2.85, yh + ys, 0.10))
                cyl(b, hip - Vector((0, 0.18, 0)), hip + Vector((0, 0.18, 0)), 0.16, 0, 20)                      # hip drive
                tube(b, [hip, knee], 0.10, 0); tube(b, [knee, foot + Vector((0, 0, 0.12))], 0.075, 0)
                cyl(b, knee - Vector((0, 0.13, 0)), knee + Vector((0, 0.13, 0)), 0.12, 2, 16)                   # knee
                m0 = hip + (knee - hip) * 0.15 + Vector((0, 0, 0.16)); m1 = knee + Vector((0, 0, 0.10))
                cyl(b, m0, m0 + (m1 - m0) * 0.75, 0.085, 7, 16); cyl(b, m0 + (m1 - m0) * 0.75, m1, 0.03, 2, 8)   # muscle + tendon
                cyl(b, foot, foot + Vector((0, 0, 0.12)), 0.42, 0, 24)                                          # foot pad
        trap(b, (0, 0, 1.05), 5.0, 1.6)
        for yy in (-1.6, 1.6):
            for sx in (-1, 1): tube(b, [(sx * 0.55, yy, 1.8), (sx * 0.6, yy, 2.15)], 0.05, 2)                  # slings
        front = -2.9
    o = obj("Variant" + CV, b, mats)
    log.append("variant %s: %d verts" % (CV, len(o.data.vertices)))
    # her: in front of the vehicle's nose, facing along the vehicle (profile to the camera)
    sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor"); fl.scale = (8, 8, 1) if fl else None
    sun = bpy.data.lights.new("VSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
    so = bpy.data.objects.new("VSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    cam.data.type = 'ORTHO'; cam.data.clip_end = 300
    back = {"A": 4.25, "B": 6.05, "C": 2.6}[CV]
    yc = ((front - 1.6) + back) / 2 + O.y
    span = {"A": 13.5, "B": 16.5, "C": 11.0}[CV]
    cam.data.ortho_scale = span
    cam.location = (-60, yc - 0.6, 1.9 if CV != "A" else 1.5)
    cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update()
    sc.render.filepath = os.path.join(OUT, "cart%s_side.png" % CV); bpy.ops.render.render(write_still=True); log.append("rendered cart%s_side" % CV)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
