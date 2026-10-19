# BEND STUDY (suit_plate_bend.py, from suit_plate_2g.py): the arcs off the helmet, no flank plates, the lower tier
# as nesting lames (a lobster tail) on their own bones, the lugs replaced by a sliding link - rendered upright and bent.
# The new suit type's cuirass, form 2г (the user's cuirass: wrap + two tiers + posture, the profile stood up), refined
# (the user, 2026-10-16): the arcs even, smooth and kinematic; the flank between the plate and the hip ring protected;
# the chest seams neat. Renders only; deformations of HIS mesh. Three variants А / Б / В.
#   blender -b astronavigator_suit.blend --python suit_plate_2g.py -> Tantra_Design/suit_mockups/g2_*.png
# The arcs: flat bands (a flat profile - the techno-frame, no tubes), symmetric, over the trapezius from the plate's top to
#   the pack's top, a knuckle at each end (the plate swings on them; the shoulders shrug and the arms rise under them).
#   А one band 40 mm; Б two bands 18 mm with ties (a ladder); В one band widening to a shoulder pad at the top.
# The flank (no protection there): А one curved side plate from the plate's side edge round to the pack, on the hip
#   ring; Б two lames over each other (she bends sideways); В the lower tier itself carried round to the side.
# The seams: the step a rolled lip of the upper tier over the lower one (no groove, no studs); the side seams engraved
#   fine lines with countersunk screws at the ends (А, Б) or none (В: the width taken in at the flank plates).
import bpy, bmesh, math, os, traceback
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "..", "suit_mockups"); os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
try:
    cu = bpy.data.objects["Cuirass"]; mark = bpy.data.objects["MarkChest"]; arcs = bpy.data.objects["ShoulderArcs"]
    PLATE = bpy.data.materials["SuitPlate"]; FRAME = bpy.data.materials["Frame"]
    def mk(name, col, rough):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; return m
    LINE = mk("gb_line", (0.08, 0.08, 0.09), 0.8)
    Z0, Z1, HW = 1.036, 1.436, 0.188
    def sstep(a, b, x): t = min(1.0, max(0.0, (x - a) / (b - a))); return t * t * (3 - 2 * t)
    def front0(u, v): return -0.215 - 0.071 * v + 0.047 * u * u
    # form 2г
    V_STEP, STEP, TUCK, WRAP, NARROW, SCOOP, V_WRAP_TOP, LEAN, POST = 0.66, 0.016, 0.006, 0.09, 0.022, 0.030, 0.72, 0.055, 1.0
    def delta(u, v, wrap_bottom=0.0):
        a = abs(u); sg = 1.0 if u >= 0 else -1.0
        dy = POST * (-0.022 * math.exp(-((v - 0.58) / 0.20) ** 2) + 0.020 * sstep(0.82, 1.0, v) + 0.020 * (1 - sstep(0.0, 0.32, v)))
        up = sstep(V_STEP - 0.02, V_STEP + 0.02, v)
        dy += -STEP * up + TUCK * (1 - up) * (1 - v / V_STEP)
        w = sstep(0.55, 1.0, a) * (1 - sstep(V_WRAP_TOP - 0.12, V_WRAP_TOP, v))
        w2 = wrap_bottom * sstep(0.55, 1.0, a) * (1 - sstep(0.0, 0.45, v))          # В: the lower tier further round
        dx = -sg * NARROW * w; dy += WRAP * w * w + w2 * w2
        sc_ = sstep(0.62, 1.0, a) * sstep(0.62, 1.0, v)
        dx += -sg * SCOOP * sc_; dy += 0.6 * SCOOP * sc_
        dy += LEAN * (v - 0.5)
        return dx, dy
    def spt(u, v, out=0.002, wb=0.0):
        dx, dy = delta(u, v, wb); return Vector((u * HW + dx, front0(u, v) + dy - out, Z0 + (Z1 - Z0) * v))

    def link(o): sc.collection.objects.link(o); return o
    def mesh_obj(name, bm, m, smooth=True):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        me.materials.append(m); return link(bpy.data.objects.new(name, me))
    def catmull(pts, n=12):
        P = [pts[0]] + pts + [pts[-1]]; out = []
        for i in range(1, len(P) - 2):
            p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
            for k in range(n):
                t = k / n
                out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
        out.append(pts[-1]); return out
    def band(name, path, width, thick, m, widths=None):
        """a flat band swept along the path: its width across (x), its thickness along the path's normal in the y-z plane"""
        bm = bmesh.new(); rings = []
        for i, p in enumerate(path):
            t = (path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)]).normalized()
            side = Vector((1, 0, 0)); nrm = t.cross(side).normalized()
            if nrm.z < 0: nrm = -nrm
            w = (widths[i] if widths else width) / 2; h = thick / 2
            ring = [bm.verts.new(p + side * sx * w + nrm * sz * h) for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            rings.append(ring)
        for a, b in zip(rings, rings[1:]):
            for k in range(4): bm.faces.new((a[k], a[(k + 1) % 4], b[(k + 1) % 4], b[k]))
        bm.faces.new(rings[0][::-1]); bm.faces.new(rings[-1])
        o = mesh_obj(name, bm, m)
        bv = o.modifiers.new("b", 'BEVEL'); bv.width = min(thick, width) * 0.35; bv.segments = 3
        return o
    def knuckle(name, c, r=0.011, L=0.05):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=20, radius1=r, radius2=r, depth=L)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector((1, 0, 0))).to_matrix())
        bmesh.ops.translate(bm, vec=c, verts=bm.verts); return mesh_obj(name, bm, FRAME)
    def thin(name, pts, r, m):
        cd = bpy.data.curves.new(name, 'CURVE'); cd.dimensions = '3D'; cd.bevel_depth = r; cd.bevel_resolution = 2; cd.use_fill_caps = True
        sp = cd.splines.new('POLY'); sp.points.add(len(pts) - 1)
        for q, c in zip(sp.points, pts): q.co = (*c, 1)
        cd.materials.append(m); return link(bpy.data.objects.new(name, cd))
    def screw(name, c):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=0.004, radius2=0.004, depth=0.0015)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector((0, -1, 0))).to_matrix())
        bmesh.ops.translate(bm, vec=c, verts=bm.verts); return mesh_obj(name, bm, LINE)
    def patch(name, f, nu, nv, thick, m):
        bm = bmesh.new(); g = [[bm.verts.new(f(i / nu, j / nv)) for i in range(nu + 1)] for j in range(nv + 1)]
        for j in range(nv):
            for i in range(nu): bm.faces.new((g[j][i], g[j][i + 1], g[j + 1][i + 1], g[j + 1][i]))
        o = mesh_obj(name, bm, m); s = o.modifiers.new("t", 'SOLIDIFY'); s.thickness = thick; s.offset = 0.0
        b = o.modifiers.new("b", 'BEVEL'); b.width = 0.003; b.segments = 2; b.limit_method = 'ANGLE'; return o

    # ---- the parts ----
    def arcs_path(sg):
        f = spt(sg * 0.62, 0.985, 0.0)                     # the plate's top, a little in from the corner
        # (the user: «дуги близко к шлему») lower, over the shoulder's slope, out from the helmet's rim (4-5 cm)
        return catmull([f + Vector((0, 0.012, 0.004)), Vector((sg * 0.150, f.y + 0.065, 1.468)), Vector((sg * 0.168, -0.06, 1.482)),
                        Vector((sg * 0.168, 0.035, 1.480)), Vector((sg * 0.150, 0.112, 1.490))], 10)
    STEEL_L = mk("gb_steel_light", (0.62, 0.63, 0.65), 0.35)     # a light metal (the user: «А, но не чёрная»)
    REDS = bpy.data.materials["MarkingRed"]
    def arcs_A(mat=None, stripe=False):
        objs = []
        for sg in (-1, 1):
            p = arcs_path(sg); objs.append(band("arc%+d" % sg, p, 0.040, 0.009, mat or FRAME))
            if stripe: objs.append(band("arcred%+d" % sg, [q + Vector((0, 0, 0.0052)) for q in p[2:-2]], 0.008, 0.0015, REDS))
            objs += [knuckle("kf%+d" % sg, p[0]), knuckle("kb%+d" % sg, p[-1])]
        return objs
    def arcs_B():
        objs = []
        for sg in (-1, 1):
            p = arcs_path(sg)
            for dx in (-0.016, 0.016):
                objs.append(band("arc%+d%+.3f" % (sg, dx), [q + Vector((dx, 0, 0)) for q in p], 0.016, 0.008, FRAME))
            for k in (len(p) // 3, 2 * len(p) // 3):            # the ties across the pair
                objs.append(band("tie%+d_%d" % (sg, k), [p[k] + Vector((-0.02, 0, 0)), p[k] + Vector((0.02, 0, 0))], 0.012, 0.007, FRAME))
            objs += [knuckle("kf%+d" % sg, p[0], L=0.055), knuckle("kb%+d" % sg, p[-1], L=0.055)]
        return objs
    def arcs_C():
        objs = []
        for sg in (-1, 1):
            p = arcs_path(sg); n = len(p)
            ws = [0.034 + 0.030 * math.sin(math.pi * i / (n - 1)) ** 2 for i in range(n)]   # a pad over the shoulder
            objs.append(band("arc%+d" % sg, p, 0.04, 0.010, PLATE, ws))
            objs.append(band("arcspine%+d" % sg, [q + Vector((0, 0, 0.006)) for q in p], 0.010, 0.004, FRAME))
            objs += [knuckle("kf%+d" % sg, p[0]), knuckle("kb%+d" % sg, p[-1])]
        return objs
    def flank_round(sg, z0, z1, a0, a1, out):
        """a surface round her flank: angle a from her front (0) to her back (pi), on an ellipse round the waist"""
        def f(s, t):
            a = a0 + (a1 - a0) * s; z = z0 + (z1 - z0) * t
            rx, ry = 0.205 + out, 0.165 + out
            return Vector((sg * rx * math.sin(a), -0.03 - ry * math.cos(a), z))
        return f
    def flank_A():
        objs = []
        for sg in (-1, 1):
            objs.append(patch("flank%+d" % sg, flank_round(sg, 1.035, 1.215, 0.95, 2.35, 0.0), 16, 8, 0.008, PLATE))
            f = flank_round(sg, 1.035, 1.215, 0.95, 2.35, 0.0)
            objs.append(thin("flankrim%+d" % sg, [f(s / 20, 1.0) for s in range(21)], 0.004, FRAME))
        return objs
    def flank_B():
        objs = []
        for sg in (-1, 1):
            for k, (z0, z1, o) in enumerate(((1.035, 1.13, 0.0), (1.11, 1.215, 0.006))):   # the upper lame over the lower
                f = flank_round(sg, z0, z1, 0.95, 2.30, o)
                objs.append(patch("lame%+d_%d" % (sg, k), f, 16, 5, 0.007, PLATE))
                objs.append(thin("lamerim%+d_%d" % (sg, k), [f(s / 20, 0.0) for s in range(21)], 0.0035, FRAME))
        return objs
    def seams_lip():
        objs = [thin("lip", [spt(-0.96 + 1.92 * k / 40, V_STEP - 0.012, 0.004) for k in range(41)], 0.0045, PLATE)]   # the rolled lip
        return objs
    def seams_engraved():
        objs = []
        for sg in (-1, 1):
            u = sg * 0.48
            objs.append(thin("eng%+d" % sg, [spt(u, 0.07 + (V_STEP - 0.1) * k / 20, 0.0006) for k in range(21)], 0.0011, LINE))
            for v in (0.10, V_STEP - 0.06): objs.append(screw("scr%+d_%.2f" % (sg, v), spt(u, v, 0.0008)))
        return objs

    def plate(wb=0.0):
        me = cu.data.copy(); bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=2, use_grid_fill=True)
        M = cu.matrix_world; Mi = M.inverted()
        for vt in bm.verts:
            w = M @ vt.co
            u = max(-1.0, min(1.0, w.x / HW)); v = max(0.0, min(1.0, (w.z - Z0) / (Z1 - Z0)))
            dx, dy = delta(u, v, wb); vt.co = Mi @ Vector((w.x + dx, w.y + dy, w.z))
        bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = True
        o = cu.copy(); o.data = me; link(o)
        # the marking onto the new face: the front's y at the marking measured on both meshes (it sat inside the plate)
        def front_at(mesh):
            ys = [(M @ q.co).y for q in mesh.vertices if abs((M @ q.co).x) < 0.03 and abs((M @ q.co).z - 1.37) < 0.025]
            return min(ys) if ys else None
        new_f, old_f = front_at(me), front_at(cu.data)
        m2 = mark.copy(); link(m2)
        if new_f is not None and old_f is not None: m2.location.y += new_f - old_f
        return [o, m2]

    # ---- the bending: every rigid part on its own bone; the lower tier as lames nesting upwards under each other ----
    arm = bpy.data.objects["Astronavigator"]; lugs = bpy.data.objects["PlateLugs"]
    def to_bone(o, bone):
        if o.type == 'MESH':
            for md in list(o.modifiers):
                if md.type == 'ARMATURE': o.modifiers.remove(md)
        bpy.context.view_layer.update(); mw = o.matrix_world.copy()
        o.parent = arm; o.parent_type = 'BONE'; o.parent_bone = bone
        bpy.context.view_layer.update(); o.matrix_world = mw
        return o
    def upper_tier():
        objs = plate()                                        # the whole 2г plate, then its lower part cut away
        o = objs[0]; me = o.data; M = o.matrix_world
        bm = bmesh.new(); bm.from_mesh(me)
        cut = [f for f in bm.faces if (M @ f.calc_center_median()).z < Z0 + (Z1 - Z0) * (V_STEP - 0.035)]
        bmesh.ops.delete(bm, geom=cut, context='FACES'); bm.to_mesh(me); bm.free()
        for x in objs: to_bone(x, "Spine1")
        return objs
    LAMES = [(V_STEP - 0.28, V_STEP + 0.02, 0.004, "Spine"), (V_STEP - 0.50, V_STEP - 0.19, 0.011, "LowerBack"), (0.0, V_STEP - 0.40, 0.018, "Hips")]   # deep overlaps
    def lames():
        objs = []
        for k, (v0, v1, inset, bone) in enumerate(LAMES):     # each lower lame inside the one above: it slides up under it
            f = lambda a, b, v0=v0, v1=v1, inset=inset: spt(-0.97 + 1.94 * a, v0 + (v1 - v0) * b, -inset)
            o = patch("lame%d" % k, f, 24, 6, 0.007, PLATE); objs.append(to_bone(o, bone))
            rim = thin("lamerim%d" % k, [f(i / 30, 0.0) + Vector((0, -0.003, 0)) for i in range(31)], 0.0035, PLATE)
            objs.append(to_bone(rim, bone))
        for sg in (-1, 1):                                     # the sliding link to the hip ring (the rigid lugs are gone)
            a = spt(sg * 0.40, 0.02, -0.02); b = Vector((sg * 0.08, -0.175, 1.0))
            objs.append(to_bone(band("link%+d" % sg, [a, (a + b) / 2 + Vector((0, -0.004, 0)), b], 0.022, 0.006, FRAME), "Hips"))
            objs.append(to_bone(knuckle("lk%+d" % sg, b, 0.009, 0.03), "Hips"))
        return objs
    def arcs_on(fn):
        objs = fn()
        for o in objs: to_bone(o, "Spine1")
        return objs
    def lip():
        return [to_bone(o, "Spine1") for o in seams_lip()]
    VARS = [("Aw", "А: лента белая", lambda: upper_tier() + lames() + arcs_on(lambda: arcs_A(PLATE)) + lip()),
            ("Ag", "А: лента светлый металл", lambda: upper_tier() + lames() + arcs_on(lambda: arcs_A(STEEL_L)) + lip()),
            ("Ar", "А: лента белая, красная полоса", lambda: upper_tier() + lames() + arcs_on(lambda: arcs_A(PLATE, True)) + lip())]
    VARS_OLD = [("A", "А: лента", lambda: upper_tier() + lames() + arcs_on(arcs_A) + lip()),
            ("B", "Б: двойные ленты", lambda: upper_tier() + lames() + arcs_on(arcs_B) + lip()),
            ("V", "В: лента-наплечник", lambda: upper_tier() + lames() + arcs_on(arcs_C) + lip())]
    BEND = float(os.environ.get("BEND", "10"))                  # deg on each of LowerBack, Spine, Spine1
    arm.data.pose_position = 'POSE'                         # (the file keeps the rest position on)
    def pose(deg):
        for b in ("LowerBack", "Spine", "Spine1"):
            pb = arm.pose.bones[b]; pb.rotation_mode = 'XYZ'; pb.rotation_euler = (math.radians(deg), 0, 0)
        bpy.context.view_layer.update()

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
    def views(key):
        shoot(os.path.join(OUT, "gb_%s_34.png" % key), (-0.95, -1.55, 1.42), (0, -0.16, 1.22), 50)
        shoot(os.path.join(OUT, "gb_%s_side.png" % key), (1.65, -0.35, 1.22), (0, -0.10, 1.15), 50)
        shoot(os.path.join(OUT, "gb_%s_top.png" % key), (-0.55, -0.70, 1.95), (0, -0.05, 1.42), 45)     # the arcs from above
        pose(BEND)
        shoot(os.path.join(OUT, "gb_%s_bent.png" % key), (1.65, -0.35, 1.22), (0, -0.10, 1.10), 50)
        shoot(os.path.join(OUT, "gb_%s_bent34.png" % key), (-0.95, -1.55, 1.30), (0, -0.20, 1.10), 50)
        pose(0)
    for key, title, build in VARS:
        objs = build(); cu.hide_render = True; mark.hide_render = True; arcs.hide_render = True; lugs.hide_render = True
        views(key)
        for o in objs: bpy.data.objects.remove(o)
        cu.hide_render = False; mark.hide_render = False; arcs.hide_render = False; lugs.hide_render = False
    open(os.path.join(OUT, "gb_done.txt"), "w").write("ok")
except Exception:
    open(os.path.join(OUT, "gb_error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
