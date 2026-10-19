# «Наплечный» M2 with four capsules: the body of jetpack_m2body.py, M1's own УВТ untouched, and a second pair of the
# same capsules on booms of their own in the seven places of tools/jetpack_m2_quad.py (heat against M1, hover trim).
#   blender --background astronavigator_suit.blend --python jetpack_m2quad.py -> renders\m2q_<0..7>_<rear|side|back>.png
# «Наплечный» M2 - the body designed anew (the user, 2026-10-17: «доработай форму согласно моим предложениям и скринам,
# с нуля, только УВТ не менять»). Kept exactly as they are: the booms, their hinges and the capsule pods (the thrust
# vector). Built new, in the family's materials (JetShell, JetDark, JetRed, JetSeam, JetPlug):
#   the back plate against her back - the pack's spine, everything hangs on it;
#   the tank section - two upright metallic-hydrogen tanks in a bar cage between armoured cheeks, under a lid that
#     carries the hinges (the UMMU turbopack: the tanks in their frame, on show);
#   the lumbar propulsion block - a rounded armoured block on her lower back with two kidney panels round the hips
#     (the Off the Grid packs: the propulsion low, hugging the body, nothing on arms);
#   the main engine out of the block's base, down and 20 deg aft, in a gimbal ring with trunnions (+-10 deg: the
#     cruise thrust and its vector); the attitude ports in four corner clusters.
#   blender --background astronavigator_suit.blend --python jetpack_m2body.py -> renders\m2b_<rear|side|back|low>.png
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import jetpack_m2_quad as Q
LOG = open(os.path.join(OUT, "m2q_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
KEEP = {"JetBoomL", "JetBoomR", "JetHingeL", "JetHingeR", "JetPodL", "JetPodR"}
for o in bpy.data.objects:
    if o.name.startswith("Jet") and o.name not in KEEP: o.hide_render = True; o.hide_viewport = True
SHELL, DARK, RED, SEAM, PLUG = (bpy.data.materials[k] for k in ("JetShell", "JetDark", "JetRed", "JetSeam", "JetPlug"))
FRAME = bpy.data.materials.new("M2_frame"); FRAME.use_nodes = True
_b = next(n for n in FRAME.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
_b.inputs["Base Color"].default_value = (0.52, 0.54, 0.57, 1); _b.inputs["Metallic"].default_value = 0.85; _b.inputs["Roughness"].default_value = 0.3
parts = []

def obj(name, bm, mat, smooth=True):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(mat)
    if smooth:
        for p in me.polygons: p.use_smooth = True
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); parts.append(o); return o

def quad(bm, a, b, c, d):
    vs = []
    for v in (a, b, c, d):
        if v not in vs: vs.append(v)
    if len(vs) >= 3:
        try: bm.faces.new(vs)
        except ValueError: pass

def sbox(name, c, r, mat, e1=0.4, e2=0.4, rot=None, nu=36, nv=18):
    """a superellipsoid: a rounded armoured block (e -> 0 boxy, 1 an ellipsoid)"""
    def sp(w, e): return math.copysign(abs(w) ** e, w)
    bm = bmesh.new(); rings = []
    for i in range(nv + 1):
        th = -math.pi / 2 + math.pi * i / nv
        if i == 0 or i == nv: rings.append([bm.verts.new(Vector((0, 0, r[2] * sp(math.sin(th), e1))))] * nu); continue
        rings.append([bm.verts.new(Vector((r[0] * sp(math.cos(th), e1) * sp(math.cos(2 * math.pi * j / nu), e2),
                                            r[1] * sp(math.cos(th), e1) * sp(math.sin(2 * math.pi * j / nu), e2), r[2] * sp(math.sin(th), e1)))) for j in range(nu)])
    for i in range(nv):
        for j in range(nu): quad(bm, rings[i][j], rings[i][(j + 1) % nu], rings[i + 1][(j + 1) % nu], rings[i + 1][j])
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ (rot or Matrix.Identity(4)), verts=bm.verts)
    return obj(name, bm, mat)

def prism(name, outline, t0, t1, mat, ch=0.006, M=None, smooth=False):
    """a chamfered slab: the outline (local x, y) extruded along local z from t0 to t1; M places it"""
    n = len(outline); cx = sum(p[0] for p in outline) / n; cy = sum(p[1] for p in outline) / n
    bm = bmesh.new()
    ext = max(math.hypot(x - cx, y - cy) for x, y in outline); s_in = 1 - ch / ext
    def ring(s, z): return [bm.verts.new(Vector((cx + (x - cx) * s, cy + (y - cy) * s, z))) for x, y in outline]
    L = [ring(s_in, t0), ring(1, t0 + ch), ring(1, t1 - ch), ring(s_in, t1)]
    bm.faces.new(L[0][::-1]); bm.faces.new(L[3])
    for a, b in zip(L, L[1:]):
        for k in range(n): quad(bm, a[k], a[(k + 1) % n], b[(k + 1) % n], b[k])
    if M is not None: bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
    return obj(name, bm, mat, smooth)

def lathe(name, c, axis, prof, mat, nu=36, smooth=True):
    """revolve (r, t) about the axis through c (t along it)"""
    A = Vector(axis).normalized(); T = A.cross(Vector((1, 0, 0)) if abs(A.x) < 0.9 else Vector((0, 0, 1))).normalized(); S = A.cross(T)
    bm = bmesh.new(); rings = []
    for r, t in prof:
        if r < 1e-6: rings.append([bm.verts.new(Vector(c) + A * t)] * nu)
        else: rings.append([bm.verts.new(Vector(c) + A * t + T * (r * math.cos(2 * math.pi * j / nu)) + S * (r * math.sin(2 * math.pi * j / nu))) for j in range(nu)])
    for a, b in zip(rings, rings[1:]):
        for j in range(nu): quad(bm, a[j], a[(j + 1) % nu], b[(j + 1) % nu], b[j])
    return obj(name, bm, mat, smooth)

def bar(name, a, b, r, mat=None, nu=12):
    a, b = Vector(a), Vector(b); L = (b - a).length
    return lathe(name, a, b - a, [(0, 0), (r, 0), (r, L), (0, L)], mat or FRAME, nu)

def belt(name, c, r, mat, h, e=0.4, rot=None, nu=48):
    """a thin band round a superellipsoid's waist (a panel seam, a red line)"""
    def sp(w, e_): return math.copysign(abs(w) ** e_, w)
    bm = bmesh.new(); rings = []
    for z in (-h / 2, h / 2):
        rings.append([bm.verts.new(Vector((r[0] * 1.004 * sp(math.cos(2 * math.pi * j / nu), e), r[1] * 1.004 * sp(math.sin(2 * math.pi * j / nu), e), z))) for j in range(nu)])
    for j in range(nu): quad(bm, rings[0][j], rings[0][(j + 1) % nu], rings[1][(j + 1) % nu], rings[1][j])
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(c)) @ (rot or Matrix.Identity(4)), verts=bm.verts)
    return obj(name, bm, mat)

def rect(w, h, cut=0.0):
    if cut <= 0: return [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
    return [(-w / 2 + cut, -h / 2), (w / 2 - cut, -h / 2), (w / 2, -h / 2 + cut), (w / 2, h / 2 - cut), (w / 2 - cut, h / 2), (-w / 2 + cut, h / 2), (-w / 2, h / 2 - cut), (-w / 2, -h / 2 + cut)]

RX = lambda deg: Matrix.Rotation(math.radians(deg), 4, 'X')
RY = lambda deg: Matrix.Rotation(math.radians(deg), 4, 'Y')
RZ = lambda deg: Matrix.Rotation(math.radians(deg), 4, 'Z')
BACK = 0.108                                                     # her back plane (where M1's shell face was)

def body():
    prism("plate", rect(0.40, 0.52, 0.04), 0, 0.035, SHELL, M=Matrix.Translation(Vector((0, BACK, 1.26))) @ RX(-90))
    TY, TR = 0.235, 0.075
    for sg in (1, -1):
        x = sg * 0.097
        lathe("tank%d" % sg, (x, TY, 1.235), (0, 0, 1), [(0, 0), (0.045, 0), (TR, 0.03), (TR, 0.225), (0.045, 0.255), (0, 0.258)], SHELL)
        lathe("tankband%d" % sg, (x, TY, 1.405), (0, 0, 1), [(TR + 0.002, 0), (TR + 0.002, 0.014)], RED)
        lathe("tankseam%d" % sg, (x, TY, 1.262), (0, 0, 1), [(TR + 0.001, 0), (TR + 0.001, 0.006)], SEAM)
        lathe("tankfit%d" % sg, (x, TY, 1.19), (0, 0, 1), [(0, 0), (0.03, 0), (0.03, 0.055), (0, 0.055)], DARK)
        prism("cheek%d" % sg, rect(0.28, 0.20, 0.03), 0, 0.02, SHELL, M=Matrix.Translation(Vector((sg * 0.185, 0.24, 1.36))) @ RY(90 * sg))
        bar("post%d" % sg, (sg * 0.172, 0.335, 1.24), (sg * 0.172, 0.335, 1.49), 0.007)
    for z in (1.31, 1.43): bar("band%d" % int(z * 100), (-0.172, 0.335, z), (0.172, 0.335, z), 0.007)
    bar("spinebar", (0, 0.335, 1.24), (0, 0.335, 1.49), 0.007)
    Ml = Matrix.Translation(Vector((0, 0.24, 1.495)))
    prism("lid", rect(0.41, 0.20, 0.03), 0, 0.035, SHELL, M=Ml); prism("lidband", rect(0.414, 0.204, 0.03), 0.012, 0.022, RED, ch=0.002, M=Ml)
    LZ = 1.085
    sbox("lumbar", (0, 0.228, LZ), (0.165, 0.118, 0.125), SHELL, 0.35, 0.4)
    for sg in (1, -1):
        sbox("kidney%d" % sg, (sg * 0.238, 0.215, LZ - 0.02), (0.072, 0.092, 0.10), SHELL, 0.28, 0.32, rot=RZ(-22 * sg))
        belt("kseam%d" % sg, (sg * 0.238, 0.215, LZ + 0.015), (0.072, 0.092), SEAM, 0.006, 0.28, RZ(-22 * sg))
    belt("lseam", (0, 0.228, LZ + 0.04), (0.165, 0.118), SEAM, 0.006, 0.4); belt("lred", (0, 0.228, LZ - 0.03), (0.165, 0.118), RED, 0.012, 0.4)
    def rcs(name, c, dirs):
        prism(name, rect(0.05, 0.04), 0, 0.03, SHELL, M=Matrix.Translation(Vector(c)))
        for k, d in enumerate(dirs):
            d = Vector(d); face = Vector(c) + Vector((0.025 * d.x, 0.02 * d.y, 0.015 + 0.015 * d.z))
            lathe("%s_p%d" % (name, k), face - d * 0.003, d, [(0, 0), (0.009, 0), (0.009, 0.005), (0, 0.005)], DARK, 12)
    for sg in (1, -1):
        rcs("rcsT%d" % sg, (sg * 0.175, 0.315, 1.53), [(sg, 0, 0), (0, 1, 0), (0, 0, 1)])
        rcs("rcsB%d" % sg, (sg * 0.13, 0.318, 0.952), [(sg, 0, 0), (0, 1, 0), (0, 0, -1)])


POD_C = {1: Vector((0.597, 0.175, 1.471)), -1: Vector((-0.597, 0.175, 1.471))}
HINGE_C = Vector((0.204, 0.186, 1.4685))
J = {n: bpy.data.objects[n] for n in ("JetPodL", "JetPodR", "JetHingeR")}
REST = {n: o.matrix_world.copy() for n, o in J.items()}

def pod_copy(name, pos, out, aft):
    """one more of M1's capsules, untouched, its centre at pos, canted out and aft (the mounting, not the pod)"""
    sg = 1 if pos[0] >= 0 else -1; src = "JetPodR" if sg > 0 else "JetPodL"
    o = J[src].copy(); o.data = J[src].data; sc.collection.objects.link(o); parts.append(o)
    o.matrix_world = Matrix.Translation(Vector(pos)) @ RY(-sg * out) @ RX(aft) @ Matrix.Translation(-POD_C[sg]) @ REST[src]
    o.name = name; return o

def hinge_copy(name, at, sg):
    o = J["JetHingeR"].copy(); o.data = J["JetHingeR"].data; sc.collection.objects.link(o); parts.append(o)
    o.matrix_world = Matrix.Translation(Vector(at)) @ Matrix.Scale(sg, 4, Vector((1, 0, 0))) @ Matrix.Translation(-HINGE_C) @ REST["JetHingeR"]
    o.name = name; return o

def boom(name, a, b):
    """a boom in M1's manner: a flattened white rod with a dark seam line"""
    a, b = Vector(a), Vector(b); d = b - a; L = d.length; c = (a + b) / 2
    rot = Vector((1, 0, 0)).rotation_difference(d.normalized()).to_matrix().to_4x4()
    sbox(name, c, (L / 2 + 0.02, 0.028, 0.036), SHELL, 0.25, 0.6, rot=rot)
    sbox(name + "s", c, (L / 2 - 0.01, 0.0295, 0.004), SEAM, 0.25, 0.6, rot=rot)

def lower_pair(L):
    for pos in L["lower"]:
        sg = 1 if pos[0] >= 0 else -1
        pod_copy("pod2%d" % sg, pos, L["out"], L["aft"])
        top = Vector(pos) + Vector((0, 0, 0.10))
        if L["boom"] == "extend":                                  # the shoulder boom carried on past the first pod
            root = Vector((sg * 0.64, 0.18, 1.492)); boom("boom2%d" % sg, root, (pos[0] - sg * 0.05, 0.18, 1.492))
        elif L["boom"] == "lid":                                   # from the lid's rear corner, back and out
            root = Vector((sg * 0.19, 0.33, 1.49)); hinge_copy("hinge2%d" % sg, root, sg); boom("boom2%d" % sg, root + Vector((sg * 0.03, 0, 0)), top)
        elif L["boom"] == "tail":                                  # a short tail from the lumbar block
            root = Vector((sg * 0.06, 0.345, 1.02)); boom("boom2%d" % sg, root, top)
        else:                                                      # from the kidney panel, out (and down)
            root = Vector((sg * 0.30, 0.22, 1.06)); hinge_copy("hinge2%d" % sg, root, sg); boom("boom2%d" % sg, root + Vector((sg * 0.03, 0, 0)), top)

LAYOUTS = Q.LAYOUTS
try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 720, 720; sc.render.film_transparent = True
    for nm, en, rot in (("MQ_key", 3.0, (42, 0, 145)), ("MQ_fill", 1.3, (65, 0, -40)), ("MQ_low", 0.9, (120, 0, 160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("MQ_cam"); cam = bpy.data.objects.new("MQ_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    VIEWS = {"rear": ((1.6, 2.4, 2.0), (0, 0.3, 1.2), 'PERSP', 38, 0), "side": ((3.5, 0.32, 1.15), (0, 0.32, 1.15), 'ORTHO', 0, 1.5),
             "back": ((0, 3.5, 1.2), (0, 0.3, 1.2), 'ORTHO', 0, 2.0)}
    for L in LAYOUTS:
        body(); lower_pair(L)
        for vn, (loc, tgt, typ, lens, osc) in VIEWS.items():
            cd.type = typ; cd.lens = lens or 50; cd.ortho_scale = osc or 1
            cam.location = Vector(loc); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "m2q_%s_%s.png" % (L["key"], vn)); bpy.ops.render.render(write_still=True)
            print("rendered", L["key"], vn, file=LOG); LOG.flush()
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
