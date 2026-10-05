# Mock-up of the «тележка» - the Tantra's universal tracked vehicle (cargo and crew) - looks only, nothing for the game.
# blender --background astronavigator_suit.blend --python mock_cart.py      (the suited astronavigator stands by it for scale)
# ~10 x 5 x 3.5 m, ~20 t (DESIGN_LOCAL): two tracks on sprung road wheels, a low hull between them, a deck over them;
# front: a dozer blade; deck: the crew cabin (pressurised, a side hatch with steps), a cargo bay with a container,
# a folding crane; rear: the power-cable drum. Axes: x across, y along (front -y), z up.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = os.path.join(HERE, "mock_cart.log"); log = []
O = Vector((4.3, 2.2, 0.0))                     # the cart's origin in the scene: beside her

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
    Vs = [[bm.verts.new(O + p) for p in r] for r in rings]; n = len(rings[0])
    for i in range(len(Vs) if closed else len(Vs) - 1):
        A, B = Vs[i], Vs[(i + 1) % len(Vs)]
        for j in range(n): bm.faces.new((A[j], A[(j + 1) % n], B[(j + 1) % n], B[j])).material_index = mi
    if cap and not closed:
        bm.faces.new(Vs[0][::-1]).material_index = mi; bm.faces.new(Vs[-1]).material_index = mi

def cyl(bm, a, b, r, mi=0, seg=24):
    loft(bm, [ring(a, b - a, r, seg), ring(b, b - a, r, seg)], mi)

def box(bm, lo, hi, mi=0, bev=0.0):
    r = bmesh.ops.create_cube(bm, size=1.0); vs = r['verts']
    c = (Vector(lo) + Vector(hi)) / 2; s = Vector(hi) - Vector(lo)
    for v in vs: v.co = O + Vector((c.x + v.co.x * s.x, c.y + v.co.y * s.y, c.z + v.co.z * s.z))
    for f in {f for v in vs for f in v.link_faces}: f.material_index = mi
    if bev > 0:
        res = bmesh.ops.bevel(bm, geom=list({e for v in vs for e in v.link_edges}), offset=bev, segments=3, profile=0.5, affect='EDGES')
        for f in res['faces']: f.material_index = mi

def poly_prism(bm, pts, x0, x1, mi=0):       # a prism: an outline in the y-z plane, extruded across x0..x1
    A = [bm.verts.new(O + Vector((x0, y, z))) for y, z in pts]; B = [bm.verts.new(O + Vector((x1, y, z))) for y, z in pts]
    n = len(pts)
    for j in range(n): bm.faces.new((A[j], A[(j + 1) % n], B[(j + 1) % n], B[j])).material_index = mi
    bm.faces.new(A[::-1]).material_index = mi; bm.faces.new(B).material_index = mi

def obj(name, bm, mats):
    me = bpy.data.meshes.new(name); bm.normal_update(); bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:]); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = False
    for m in mats: me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o); return o

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    HULL = mat("CHull", (0.13, 0.14, 0.15), 0.55, 0.35); TRK = mat("CTrack", (0.06, 0.06, 0.065), 0.7, 0.5)
    WHL = mat("CWheel", (0.22, 0.23, 0.25), 0.4, 0.7); CAB = mat("CCabin", (0.84, 0.85, 0.86), 0.4)
    GLS = mat("CGlass", (0.05, 0.08, 0.12), 0.05, 0.3); RED = mat("CRed", (0.70, 0.12, 0.10), 0.45)
    LMP = mat("CLamp", (1.0, 0.95, 0.8), 0.2); CON = mat("CContainer", (0.55, 0.57, 0.58), 0.6, 0.2)
    CBL = mat("CCable", (0.03, 0.03, 0.03), 0.8)
    mats = [HULL, TRK, WHL, CAB, GLS, RED, LMP, CON, CBL]     # 0 hull 1 track 2 wheel 3 cabin 4 glass 5 red 6 lamp 7 container 8 cable
    b = bmesh.new()
    # ---- running gear: per side a track loop over a rear sprocket, a front idler and six sprung road wheels ----
    YS, YI, ZC, RE = 3.45, -3.45, 0.58, 0.53          # sprocket / idler centres along y, their height, their radius
    for sx in (-1, 1):
        cx = sx * 2.0
        path = []
        for k in range(40): t = k / 40; path.append((YI + (YS - YI) * t, ZC + RE))                       # top run
        for k in range(20): a = math.pi / 2 - math.pi * k / 20; path.append((YS + RE * math.cos(a), ZC + RE * math.sin(a)))
        for k in range(40): t = k / 40; path.append((YS - (YS - YI) * t, ZC - RE))                       # ground run
        for k in range(20): a = -math.pi / 2 - math.pi * k / 20; path.append((YI + RE * math.cos(a), ZC + RE * math.sin(a)))
        rings = []
        for i, (y, z) in enumerate(path):
            yp, zp = path[(i + 1) % len(path)]; ty, tz = yp - y, zp - z; L = math.hypot(ty, tz); ny, nz = tz / L, -ty / L   # outward normal
            rings.append([Vector((cx + dx, y + ny * dn, z + nz * dn)) for dx, dn in ((-0.5, 0.0), (0.5, 0.0), (0.5, -0.07), (-0.5, -0.07))])
        loft(b, rings, 1, cap=False, closed=True)
        for i in range(0, len(path), 2):              # grousers: cleats across the track
            y, z = path[i]; yp, zp = path[(i + 1) % len(path)]; ty, tz = yp - y, zp - z; L = math.hypot(ty, tz); ny, nz = tz / L, -ty / L
            c = Vector((cx, y + ny * 0.03, z + nz * 0.03))
            box(b, c - Vector((0.5, 0.05, 0.03)), c + Vector((0.5, 0.05, 0.03)), 1)
        for yy in (YS, YI):                            # sprocket (rear, driven) and idler (front)
            cyl(b, Vector((cx - 0.42, yy, ZC)), Vector((cx + 0.42, yy, ZC)), RE - 0.08, 2, 28)
            cyl(b, Vector((cx + sx * 0.42, yy, ZC)), Vector((cx + sx * 0.48, yy, ZC)), 0.16, 0, 16)
        for k in range(6):                             # road wheels on trailing arms (the suspension: each arm on its own spring)
            yw = -2.75 + k * 1.1; zw = 0.36
            cyl(b, Vector((cx - 0.40, yw, zw)), Vector((cx + 0.40, yw, zw)), 0.30, 2, 24)
            cyl(b, Vector((sx * 1.45, yw - 0.55, 0.78)), Vector((cx - sx * 0.42, yw, zw)), 0.07, 0, 10)
            cyl(b, Vector((sx * 1.45, yw - 0.55, 0.62)), Vector((sx * 1.45, yw - 0.55, 0.94)), 0.10, 0, 12)   # torsion pivot
        for k in range(3):                             # return rollers under the top run
            cyl(b, Vector((cx - 0.30, -2.0 + k * 2.0, ZC + RE - 0.16)), Vector((cx + 0.30, -2.0 + k * 2.0, ZC + RE - 0.16)), 0.09, 2, 16)
    # ---- hull between the tracks, deck over them, track guards ----
    box(b, (-1.45, -4.55, 0.55), (1.45, 4.45, 1.30), 0, 0.10)
    poly_prism(b, [(-4.75, 1.30), (-4.30, 0.95), (4.35, 0.95), (4.70, 1.30), (4.70, 1.62), (-4.75, 1.62)], -2.60, 2.60, 0)   # deck
    for sx in (-1, 1):
        box(b, (sx * 2.62 - 0.02, -4.5, 1.15), (sx * 2.62 + 0.02, 4.45, 1.62), 0)                             # guard skirt
    # ---- front: dozer blade on two push arms, lamps on the blade ----
    blade = []
    for k in range(9):
        a = -0.55 + 1.1 * k / 8; blade.append((-5.35 - 0.28 * math.cos(a) + 0.25, 0.62 + 0.62 * math.sin(a)))
    blade_in = [(y + 0.10, z) for y, z in blade[::-1]]
    poly_prism(b, blade + blade_in, -2.65, 2.65, 0)
    box(b, (-2.65, -5.55, 0.02), (2.65, -5.38, 0.12), 2)                                                        # cutting edge
    for sx in (-1, 1):
        cyl(b, Vector((sx * 1.5, -5.05, 0.75)), Vector((sx * 1.5, -4.30, 0.95)), 0.10, 0, 12)
        cyl(b, Vector((sx * 0.9, -5.05, 1.05)), Vector((sx * 0.9, -4.55, 1.35)), 0.07, 2, 12)                  # lift ram
    # ---- crew cabin: pressurised, white, a window band, roof lamps, an octagonal side hatch with steps ----
    CY0, CY1, CZ0, CZ1 = -4.40, -0.95, 1.62, 3.50
    box(b, (-1.85, CY0, CZ0), (1.85, CY1, CZ1), 3, 0.32)
    box(b, (-1.62, CY0 - 0.012, 2.62), (1.62, CY0 + 0.4, 3.12), 4, 0.06)                                       # front windows
    for sx in (-1, 1):
        box(b, (sx * 1.862 - 0.02, CY0 + 0.45, 2.72), (sx * 1.862 + 0.02, CY0 + 1.55, 3.10), 4, 0.01)          # side windows
    box(b, (-1.86, CY0 + 0.3, 2.35), (1.86, CY1 - 0.3, 2.40), 5)                                               # red line
    oc = Vector((-1.87, -2.15, 2.40)); r8 = 0.58                                                                # hatch on the near side
    hv = [bmesh.ops.create_vert(b, co=O + oc + Vector((0, r8 * math.cos(math.pi / 8 + k * math.pi / 4), r8 * math.sin(math.pi / 8 + k * math.pi / 4) * 1.25)))['vert'][0] for k in range(8)]
    hv2 = [b.verts.new(v.co + Vector((-0.04, 0, 0))) for v in hv]
    for k in range(8): b.faces.new((hv[k], hv[(k + 1) % 8], hv2[(k + 1) % 8], hv2[k])).material_index = 0
    b.faces.new(hv2[::-1]).material_index = 2
    for k in range(3): box(b, (-2.62, -2.55, 0.45 + k * 0.40), (-2.28, -1.75, 0.50 + k * 0.40), 2)             # steps
    for sx in (-0.9, -0.3, 0.3, 0.9):
        cyl(b, Vector((sx, CY0 + 0.25, CZ1 + 0.02)), Vector((sx, CY0 + 0.05, CZ1 + 0.08)), 0.09, 6, 16)       # roof floodlights
    cyl(b, Vector((1.2, CY1 - 0.5, CZ1)), Vector((1.2, CY1 - 0.5, CZ1 + 0.55)), 0.02, 0, 8)                    # antenna
    # ---- cargo bay: a 20-ft-class container on the deck, tie-down posts ----
    box(b, (-1.75, -0.55, 1.62), (0.75, 3.05, 3.62), 7, 0.03)
    for yy in (-0.6, 3.1):
        for xx in (-1.80, 0.80): box(b, (xx - 0.06, yy - 0.06, 1.62), (xx + 0.06, yy + 0.06, 2.20), 0)
    # ---- folding crane on the rear right corner, boom stowed forward along the deck ----
    T = Vector((1.75, 3.65, 1.62))
    cyl(b, T, T + Vector((0, 0, 0.45)), 0.38, 0, 24)
    cyl(b, T + Vector((0, 0, 0.45)), T + Vector((0, 0, 0.75)), 0.24, 2, 16)
    cyl(b, T + Vector((0, 0, 0.62)), Vector((1.75, -0.40, 2.45)), 0.14, 0, 8)                                  # main boom
    cyl(b, Vector((1.75, -0.40, 2.45)), Vector((1.75, 2.60, 2.25)), 0.10, 0, 8)                                # jib folded back
    cyl(b, Vector((1.75, -0.10, 1.62)), Vector((1.75, -0.10, 2.30)), 0.06, 2, 8)                               # boom rest
    box(b, (1.62, 2.55, 1.95), (1.88, 2.75, 2.20), 5)                                                         # hook block
    # ---- rear: the power-cable drum across the stern (cable to the ship), a winch below ----
    cyl(b, Vector((-1.25, 4.35, 2.12)), Vector((1.05, 4.35, 2.12)), 0.50, 8, 32)
    for xx in (-1.30, 1.10): cyl(b, Vector((xx - 0.05, 4.35, 2.12)), Vector((xx + 0.05, 4.35, 2.12)), 0.62, 0, 32)
    for xx in (-1.30, 1.10): box(b, (xx - 0.06, 4.0, 1.62), (xx + 0.06, 4.7, 2.15), 0)
    cyl(b, Vector((-0.6, 4.55, 0.95)), Vector((0.6, 4.55, 0.95)), 0.22, 2, 20)                                 # winch
    for sx in (-1, 1): cyl(b, Vector((sx * 2.2, 4.70, 1.45)), Vector((sx * 2.2, 4.80, 1.45)), 0.10, 5, 16)    # tail lamps
    o = obj("Cart", b, mats)
    log.append("cart: %d verts" % len(o.data.vertices))
    sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (6, 6, 1)
    sun = bpy.data.lights.new("CartSun", 'SUN'); sun.energy = 3.2; sun.angle = 0.05
    so = bpy.data.objects.new("CartSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-6, -9, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene
    for name, loc, tgt, lens in (("cart_threequarter", (-8.5, -12.5, 5.6), (3.6, 1.2, 1.5), 33),
                                 ("cart_side", (-15.5, 2.0, 2.6), (3.8, 2.0, 1.6), 40),
                                 ("cart_rear", (13.5, 13.0, 6.5), (4.0, 1.8, 1.4), 33)):
        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y'); cam.data.clip_end = 200
        bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); log.append("rendered " + name)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
