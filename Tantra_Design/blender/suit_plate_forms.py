# The user's own cuirass (Cuirass) in five forms - renders only. Each form is a deformation of HIS mesh (its outline,
# its curves and its lean kept), each from what the plate protects against (the user, 2026-10-16: «думай формами -
# защита, красота техническая»; the arcs, the lugs, the legs and the rest untouched).
#   blender -b astronavigator_suit.blend --python suit_plate_forms.py   -> Tantra_Design/suit_mockups/form_*.png
#   1 Киль        a shallow V section with a ridge down the middle: a blow glances off to the sides (sloped armour),
#                 the ridge is the stiffener
#   2 Обхват      the sides wrapped back round the ribs towards the armpits: the flanks covered in a fall on the side
#   3 Два яруса   an upper glacis over a lower plate tucked in: the step between them a shadow line and the hinge
#   4 Осанка      the profile as an S: hollow under the collarbones (room for the chin), full at the breastbone,
#                 drawn in to the belt
#   5 Гранёная    flat facets and sharp creases - one down the middle, one across at the breastbone
import bpy, bmesh, math, os, traceback
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "..", "suit_mockups"); os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
try:
    cu = bpy.data.objects["Cuirass"]; mark = bpy.data.objects["MarkChest"]
    Z0, Z1, HW = 1.036, 1.436, 0.188
    def sstep(a, b, x): t = min(1.0, max(0.0, (x - a) / (b - a))); return t * t * (3 - 2 * t)
    def yc(v): return -0.215 - 0.071 * v                          # the front's centre line now
    # deltas (dx, dy) at a point of the plate by (u across -1..1, v up 0..1); the back face moves with the front
    def keel(u, v):
        a = abs(u); return 0.0, -0.032 * (1 - a) + 0.020 * (1 - a) ** 2     # forward along the middle, the edges stay
    def wrap(u, v):
        a = abs(u); s = sstep(0.55, 1.0, a); return -math.copysign(0.022 * s, u), 0.085 * s * s
    def tiers(u, v):
        up = sstep(0.52, 0.56, v)
        return 0.0, -0.014 * up + 0.022 * (1 - up) * (1 - v / 0.56)          # the glacis out, the lower plate tucked in
    def posture(u, v):
        return 0.0, -0.022 * math.exp(-((v - 0.58) / 0.20) ** 2) + 0.020 * sstep(0.82, 1.0, v) + 0.020 * (1 - sstep(0.0, 0.32, v))
    def facets(u, v):
        a = abs(u)
        pl = 0.0 if a < 0.32 else (a - 0.32) / 0.68                  # a flat middle, flat sides: the u^2 curve made straight
        dy_u = 0.047 * (pl - a * a)
        knee = -0.014 * (1 - abs(v - 0.55) / 0.55)                    # the crease across at the breastbone, out
        return 0.0, dy_u + knee
    FORMS = [("1_kil", "1 Киль", keel, True), ("2_obkhvat", "2 Обхват", wrap, True), ("3_yarusy", "3 Два яруса", tiers, True),
             ("4_osanka", "4 Осанка", posture, True), ("5_granyonaya", "5 Гранёная", facets, False)]

    def make(key, fn, smooth):
        me = cu.data.copy(); bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=2, use_grid_fill=True)   # dense enough for a crease
        M = cu.matrix_world; Mi = M.inverted()
        for vt in bm.verts:
            w = M @ vt.co
            u = max(-1.0, min(1.0, w.x / HW)); v = max(0.0, min(1.0, (w.z - Z0) / (Z1 - Z0)))
            dx, dy = fn(u, v)
            vt.co = Mi @ Vector((w.x + dx, w.y + dy, w.z))
        bm.to_mesh(me); bm.free()
        for p in me.polygons: p.use_smooth = smooth
        o = cu.copy(); o.data = me; o.name = "Cuirass_" + key; sc.collection.objects.link(o)
        m2 = mark.copy(); m2.name = "Mark_" + key; sc.collection.objects.link(m2)
        dx, dy = fn(0.0, (1.37 - Z0) / (Z1 - Z0)); m2.location.y += dy
        return [o, m2]

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
    # a raking key light from the upper left: the forms read by their light and shade
    light("key", (-2.2, -1.6, 3.0), 260, 1.2); light("fill", (2.4, -1.6, 1.6), 45, 2.0); light("rim", (0.5, 2.8, 2.4), 140, 1.5)
    cd = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    def shoot(path, loc, tgt, lens):
        cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = path; bpy.ops.render.render(write_still=True)
    def views(key):
        shoot(os.path.join(OUT, "form_%s_34.png" % key), (-0.95, -1.55, 1.42), (0, -0.16, 1.22), 50)
        shoot(os.path.join(OUT, "form_%s_side.png" % key), (1.55, -0.55, 1.30), (0, -0.12, 1.22), 50)
        shoot(os.path.join(OUT, "form_%s_top.png" % key), (-0.35, -0.75, 1.95), (0, -0.18, 1.25), 45)   # from over the shoulder: the section
    views("0_now")
    for key, title, fn, smooth in FORMS:
        objs = make(key, fn, smooth)
        cu.hide_render = True; mark.hide_render = True
        views(key)
        for o in objs: bpy.data.objects.remove(o)
        cu.hide_render = False; mark.hide_render = False
    open(os.path.join(OUT, "form_done.txt"), "w").write("ok")
except Exception:
    open(os.path.join(OUT, "form_error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
