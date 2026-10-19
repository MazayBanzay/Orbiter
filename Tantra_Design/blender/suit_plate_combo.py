# The user's cuirass: «Обхват» + «Два яруса» + «Осанка» joined, five variations; the fitting seams shown; the armpits
# kept clear (the user, 2026-10-16). Renders only - deformations of HIS mesh (its outline, its curves, its lean).
#   blender -b astronavigator_suit.blend --python suit_plate_combo.py -> Tantra_Design/suit_mockups/combo_*.png, fit_*.png
# The joined form, per point of the plate (u across -1..1, v up 0..1):
#   posture - the S profile: hollow under the collarbones, full at the breastbone, drawn in to the belt
#   tiers   - the upper glacis out over the lower plate tucked in; the step at v_step
#   wrap    - the sides back round the ribs, only BELOW the armpits: it fades out up to v_wrap_top, and the upper corners
#             are scooped in and back - the arm swings past the plate's top corner, not into it
# The fitting seams (shown as dark grooves with studs): vertical ones at u = +-u_seam let the width grow (an insert in
# the joint), the step's overlap lets the length grow (the glacis slides over the lower plate).
import bpy, bmesh, math, os, traceback
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "..", "suit_mockups"); os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
try:
    cu = bpy.data.objects["Cuirass"]; mark = bpy.data.objects["MarkChest"]
    FRAME = bpy.data.materials["Frame"]
    def mk(name, col, rough):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; return m
    GROOVE = mk("cm_groove", (0.05, 0.05, 0.055), 0.8)
    INSERT = mk("cm_insert", (0.22, 0.23, 0.25), 0.6)            # the width insert in an opened seam: a darker band
    Z0, Z1, HW = 1.036, 1.436, 0.188
    def sstep(a, b, x): t = min(1.0, max(0.0, (x - a) / (b - a))); return t * t * (3 - 2 * t)
    def front0(u, v): return -0.215 - 0.071 * v + 0.047 * u * u   # the plate's front now (probed)

    class P:   # one variation's numbers
        def __init__(s, name, title, posture=1.0, step=0.014, tuck=0.020, v_step=0.55, wrap=0.075, v_wrap_top=0.72,
                     scoop=0.030, u_seam=0.48, seams="side", spine=0.0, narrow=0.022, lean=0.0):
            s.__dict__.update(locals()); del s.__dict__["s"]
    VARS = [
        P("1_osnova", "1 Основа", seams="side"),
        P("2_vysokiy_yarus", "2 Ярус выше", v_step=0.66, step=0.016, tuck=0.026, wrap=0.09, seams="side+step"),
        P("3_glubokiy_obkhvat", "3 Глубокий обхват", wrap=0.12, v_wrap_top=0.62, scoop=0.040, u_seam=0.60, seams="wings"),
        P("4_myagkaya", "4 Мягкая осанка", posture=1.6, step=0.008, tuck=0.012, wrap=0.06, seams="centre", spine=0.006),
        P("5_bronevaya", "5 Броневая", posture=0.8, step=0.024, tuck=0.024, v_step=0.52, wrap=0.08, seams="side+step"),
    ]
    # (the user: «вариант 2, но уменьшить угол - слишком уже; у меня оптимален») 2 with a smaller wrap angle and no
    # narrowing: the plate keeps his width, the sides only turn back a little
    VARS2 = [
        # (the user: «угол, а не обхват» - the plate's angle in profile made smaller, the wrap of 2 as it was)
        P("2v", "2в угол меньше", v_step=0.66, step=0.016, tuck=0.012, wrap=0.09, seams="side+step", lean=0.030),
        P("2g", "2г угол почти прямой", v_step=0.66, step=0.016, tuck=0.006, wrap=0.09, seams="side+step", lean=0.055),
    ]

    def delta(p, u, v, open_=0.0):
        a = abs(u); sg = 1.0 if u >= 0 else -1.0
        dy = p.posture * (-0.022 * math.exp(-((v - 0.58) / 0.20) ** 2) + 0.020 * sstep(0.82, 1.0, v) + 0.020 * (1 - sstep(0.0, 0.32, v)))
        up = sstep(p.v_step - 0.02, p.v_step + 0.02, v)
        dy += -p.step * up + p.tuck * (1 - up) * (1 - v / p.v_step)
        w = sstep(0.55, 1.0, a) * (1 - sstep(p.v_wrap_top - 0.12, p.v_wrap_top, v))       # no wrap up by the armpit
        dx = -sg * p.narrow * w; dy += p.wrap * w * w
        sc_ = sstep(0.62, 1.0, a) * sstep(0.62, 1.0, v)                                      # the top corners scooped
        dx += -sg * p.scoop * sc_; dy += 0.6 * p.scoop * sc_
        dy += p.lean * (v - 0.5)                                                             # the profile stood up: top back, bottom out
        dy += -p.spine * max(0.0, 1 - a / 0.15)                                              # a low spine (4)
        if open_ > 0 and a > p.u_seam: dx += sg * open_                                      # the seam opened: wider
        return dx, dy

    def surf_pt(p, u, v, out=0.002, open_=0.0):
        dx, dy = delta(p, u, v, open_)
        x = u * HW + dx
        return Vector((x, front0(u, v) + dy - out, Z0 + (Z1 - Z0) * v))

    def tube(name, pts, r, m):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 3; cd.use_fill_caps = True
        sp = cd.splines.new('POLY'); sp.points.add(len(pts) - 1)
        for q, c in zip(sp.points, pts): q.co = (*c, 1)
        cd.materials.append(m); o = bpy.data.objects.new(name, cd); sc.collection.objects.link(o); return o

    def stud(name, c, m=None):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=0.0045, radius2=0.0045, depth=0.004)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector((0, -1, 0))).to_matrix())
        bmesh.ops.translate(bm, vec=c, verts=bm.verts)
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m or FRAME)
        o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); return o

    def seams(p, open_=0.0):
        objs = []
        def vline(u, v0, v1, key):
            pts = [surf_pt(p, u, v0 + (v1 - v0) * k / 24, open_=open_) for k in range(25)]
            objs.append(tube("seam" + key, pts, 0.0028, GROOVE))
            for k in range(1, 5):                                          # studs along it, both sides of the groove
                v = v0 + (v1 - v0) * k / 5
                for du in (-0.035, 0.035): objs.append(stud("st%s_%d_%+.2f" % (key, k, du), surf_pt(p, u + du, v, 0.001, open_)))
        def hline(v, key):
            pts = [surf_pt(p, -0.97 + 1.94 * k / 40, v, open_=open_) for k in range(41)]
            objs.append(tube("seam" + key, pts, 0.0030, GROOVE))
        top = p.v_step - 0.03 if "step" in p.seams else 0.95
        if p.seams.startswith("side"):
            for sg in (-1, 1): vline(sg * p.u_seam, 0.06, top, "s%+d" % sg)
        if p.seams == "wings":
            for sg in (-1, 1): vline(sg * p.u_seam, 0.04, 0.92, "w%+d" % sg)
        if p.seams == "centre":
            vline(0.0, 0.08, 0.93, "c")
        if "step" in p.seams:
            hline(p.v_step - 0.015, "step")                                 # the overlap: the glacis slides over the lower plate
            for k in range(-3, 4): objs.append(stud("ss%d" % k, surf_pt(p, k * 0.25, p.v_step + 0.02, 0.001, open_)))
        if open_ > 0:                                                       # the insert in the opened side seams
            for sg in (-1, 1):
                pts = [surf_pt(p, sg * p.u_seam, 0.06 + 0.88 * k / 24, 0.0, 0.0) for k in range(25)]
                for q in pts: q.x += sg * open_ * 0.5                         # in the middle of the opened joint
                objs.append(tube("ins%+d" % sg, pts, max(0.003, open_ * 0.55), INSERT))
        return objs

    def make(p, open_=0.0):
        me = cu.data.copy(); bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=2, use_grid_fill=True)
        M = cu.matrix_world; Mi = M.inverted()
        for vt in bm.verts:
            w = M @ vt.co
            u = max(-1.0, min(1.0, w.x / HW)); v = max(0.0, min(1.0, (w.z - Z0) / (Z1 - Z0)))
            dx, dy = delta(p, u, v, open_)
            vt.co = Mi @ Vector((w.x + dx, w.y + dy, w.z))
        bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = True
        o = cu.copy(); o.data = me; o.name = "Cuirass_" + p.name; sc.collection.objects.link(o)
        m2 = mark.copy(); sc.collection.objects.link(m2); m2.location.y += delta(p, 0.0, (1.37 - Z0) / (Z1 - Z0))[1]
        return [o, m2] + seams(p, open_)

    for o in list(bpy.data.objects):
        if o.type in ('LIGHT', 'CAMERA'): bpy.data.objects.remove(o)
    for eng in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE', 'CYCLES'):
        try: sc.render.engine = eng; break
        except Exception: pass
    sc.render.resolution_x, sc.render.resolution_y = 900, 1100
    sc.world = sc.world or bpy.data.worlds.new("w"); sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None) or sc.world.node_tree.nodes.new("ShaderNodeBackground")
    bg.inputs[0].default_value = (0.20, 0.21, 0.23, 1); bg.inputs[1].default_value = 0.5
    def light(name, loc, energy, size):
        ld = bpy.data.lights.new(name, 'AREA'); ld.energy = energy; ld.size = size
        o = bpy.data.objects.new(name, ld); o.location = loc; sc.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (Vector((0, 0, 1.2)) - Vector(loc)).to_track_quat('-Z', 'Y')
    light("key", (-2.2, -1.6, 3.0), 260, 1.2); light("fill", (2.4, -1.6, 1.6), 45, 2.0); light("rim", (0.5, 2.8, 2.4), 140, 1.5)
    cd = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    def shoot(path, loc, tgt, lens):
        cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = path; bpy.ops.render.render(write_still=True)
    def views(prefix, key):
        shoot(os.path.join(OUT, "%s_%s_34.png" % (prefix, key)), (-0.95, -1.55, 1.42), (0, -0.16, 1.22), 50)
        shoot(os.path.join(OUT, "%s_%s_side.png" % (prefix, key)), (1.55, -0.55, 1.30), (0, -0.12, 1.22), 50)
        shoot(os.path.join(OUT, "%s_%s_arm.png" % (prefix, key)), (-1.10, -0.20, 1.36), (-0.12, -0.12, 1.30), 45)   # the armpit
    def run(prefix, key, p, open_=0.0):
        objs = make(p, open_)
        cu.hide_render = True; mark.hide_render = True
        views(prefix, key)
        for o in objs: bpy.data.objects.remove(o)
        cu.hide_render = False; mark.hide_render = False
    if os.environ.get("ONLY2") == "1":
        for p in VARS2: run("combo", p.name, p)
    else:
        views("combo", "0_now")
        for p in VARS: run("combo", p.name, p)
        for k, op in enumerate((0.0, 0.015, 0.030)): run("fit", "%d" % k, VARS[0], op)
    open(os.path.join(OUT, "combo_done.txt"), "w").write("ok")
except Exception:
    open(os.path.join(OUT, "combo_error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
