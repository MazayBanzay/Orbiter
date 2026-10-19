# Suit mock-ups (renders only, nothing for the game): the «Каркас» successor, the Soviet line +500 years (the user,
# 2026-10-16: the figure kept in its proportions - Yefremov's canon; a working coverall, no latex, no "Star Wars";
# «Interstellar» practicality with a Soviet way - Orlan's rear entry, the chest console, the helmet one piece with the torso).
#   blender -b astronavigator_coverall.blend --python suit_mockup.py   -> Tantra_Design/suit_mockups/*.png
# Variants:  A «Орлан-500»: hard upper torso, rear entry through the life-support pack, the console on the chest,
#              the helmet fixed to the torso - an elongated shell, a large visor and a top window
#            Б «Сокол-500»: soft pressure suit, a close helmet turning with the head on a neck bearing, the controls on
#              the left forearm, the consumables in two chest-side packs on the harness
#            В «Грань»: hard upper torso as А, a faceted helmet (flat facets, a faceted visor), the console narrow
import bpy, bmesh, math, os
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.path.join(HERE, "..", "suit_mockups"); os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
import traceback
try:

    # ---------------- materials ----------------
    def mat(name, col, rough=0.6, metal=0.0, alpha=1.0, emit=0.0):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
        m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
        if alpha < 1:
            b.inputs["Alpha"].default_value = alpha
            try: m.surface_render_method = 'BLENDED'
            except Exception: pass
            try: m.blend_method = 'BLEND'
            except Exception: pass
        if emit > 0:
            b.inputs["Emission Color"].default_value = (*col, 1); b.inputs["Emission Strength"].default_value = emit
        return m
    CANVAS = mat("mk_canvas", (0.80, 0.78, 0.72), 0.95)          # the outer coverall: a heavy woven cloth
    SHELL = mat("mk_shell", (0.86, 0.86, 0.84), 0.35)            # the hard parts
    DARK = mat("mk_dark", (0.10, 0.10, 0.11), 0.7)               # webbing, rubber
    STEEL = mat("mk_steel", (0.55, 0.56, 0.58), 0.35, 0.8)
    RED = mat("mk_red", (0.70, 0.07, 0.05), 0.5)
    GLASS = mat("mk_glass", (0.55, 0.38, 0.12), 0.05, 0.0, 0.45)
    LAMP = mat("mk_lamp", (1.0, 0.95, 0.85), 0.3, 0.0, 1.0, 8.0)
    GLOVE = mat("mk_glove", (0.30, 0.30, 0.31), 0.85)
    SCREEN = mat("mk_screen", (0.15, 0.55, 0.45), 0.2, 0.0, 1.0, 1.5)

    # ---------------- the figure: the coverall loose over her, gloves, boots ----------------
    cov = bpy.data.objects["Coverall"]; body = bpy.data.objects["Astronavigator.body"]
    cov.data.materials.clear(); cov.data.materials.append(CANVAS)
    sm = cov.modifiers.new("drape", 'SMOOTH'); sm.factor = 1.0; sm.iterations = 30          # no anatomy through the cloth
    d = cov.modifiers.new("loose", 'DISPLACE'); d.strength = 0.032; d.mid_level = 0.0      # a working cloth held off the body
    tx = bpy.data.textures.new("folds", 'CLOUDS'); tx.noise_scale = 0.06; tx.noise_depth = 2
    d2 = cov.modifiers.new("folds", 'DISPLACE'); d2.texture = tx; d2.strength = 0.012; d2.texture_coords = 'OBJECT'
    for o in bpy.data.objects:
        if o.name.startswith("Astronavigator.shoes"): o.data.materials.clear(); o.data.materials.append(DARK)
    # the hands (beyond the cuffs) become gloves: the body's own faces there, by position (the rest pose)
    body.data.materials.append(GLOVE); gi = len(body.data.materials) - 1
    for p in body.data.polygons:
        c = p.center
        if abs(c.x) > 0.385 and c.z < 1.12: p.material_index = gi

    def link(o, coll):
        coll.objects.link(o); return o

    def mesh_obj(name, bm, coll, m, smooth=True):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        o = bpy.data.objects.new(name, me); me.materials.append(m); return link(o, coll)

    def ellipsoid(name, c, r, coll, m, seg=48, ring=32, ico=False):
        bm = bmesh.new()
        if ico: bmesh.ops.create_icosphere(bm, subdivisions=2, radius=1.0)
        else: bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=ring, radius=1.0)
        bmesh.ops.scale(bm, vec=Vector(r), verts=bm.verts); bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts)
        return mesh_obj(name, bm, coll, m, smooth=not ico)

    def box(name, lo, hi, coll, m, bevel=0.0):
        bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
        bmesh.ops.scale(bm, vec=Vector([h - l for l, h in zip(lo, hi)]), verts=bm.verts)
        bmesh.ops.translate(bm, vec=Vector([(l + h) / 2 for l, h in zip(lo, hi)]), verts=bm.verts)
        o = mesh_obj(name, bm, coll, m, smooth=False)
        if bevel: b = o.modifiers.new("bev", 'BEVEL'); b.width = bevel; b.segments = 3
        return o

    def cyl(name, a, b, r, coll, m, seg=24):
        a, b = Vector(a), Vector(b); L = (b - a).length
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r, depth=L)
        q = Vector((0, 0, 1)).rotation_difference((b - a).normalized())
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=q.to_matrix()); bmesh.ops.translate(bm, vec=(a + b) / 2, verts=bm.verts)
        return mesh_obj(name, bm, coll, m)

    def torus(name, c, R, r, coll, m, scale=(1, 1, 1), rot=None):
        bm = bmesh.new()
        seg, ring = 48, 12
        vs = []
        for i in range(seg):
            a = 2 * math.pi * i / seg
            for j in range(ring):
                b = 2 * math.pi * j / ring
                p = Vector(((R + r * math.cos(b)) * math.cos(a), (R + r * math.cos(b)) * math.sin(a), r * math.sin(b)))
                vs.append(bm.verts.new(p))
        for i in range(seg):
            for j in range(ring):
                bm.faces.new((vs[i * ring + j], vs[((i + 1) % seg) * ring + j], vs[((i + 1) % seg) * ring + (j + 1) % ring], vs[i * ring + (j + 1) % ring]))
        bmesh.ops.scale(bm, vec=Vector(scale), verts=bm.verts)
        if rot: bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=rot)
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts)
        return mesh_obj(name, bm, coll, m)

    def cut(o, cutter, op='DIFFERENCE'):
        md = o.modifiers.new("cut", 'BOOLEAN'); md.object = cutter; md.operation = op; md.solver = 'EXACT'
        cutter.hide_render = True; cutter.hide_viewport = True

    def text(name, s, loc, rot, size, coll, m):
        cu = bpy.data.curves.new(name, 'FONT'); cu.body = s; cu.size = size; cu.align_x = 'CENTER'; cu.extrude = 0.0005
        o = bpy.data.objects.new(name, cu); o.location = loc; o.rotation_euler = rot; cu.materials.append(m); return link(o, coll)

    # landmarks of the figure (the rest pose, metres; front is -y)
    NECK, SHOULDER, CHEST, WAIST, HEAD = 1.475, 1.38, 1.27, 0.98, Vector((0, -0.03, 1.63))
    FRONT, BACK = -0.135, 0.125                                       # the chest's front and the back at CHEST (with the cloth)

    def common(coll):
        # the harness: the waist belt, the thigh loops, the crotch strap - webbing over the cloth
        torus("belt", (0, -0.005, WAIST), 0.205, 0.02, coll, DARK, scale=(1.0, 0.80, 1.6))
        box("buckle", (-0.045, -0.19, WAIST - 0.03), (0.045, -0.165, WAIST + 0.03), coll, STEEL, 0.004)
        for sx in (-1, 1):
            torus("thigh%d" % sx, (sx * 0.11, -0.02, 0.80), 0.112, 0.014, coll, DARK, scale=(1, 0.95, 1.5))
            box("pocket%d" % sx, (sx * 0.205 - 0.03, -0.08, 0.62), (sx * 0.205 + 0.03, 0.03, 0.76), coll, CANVAS, 0.008)   # thigh pockets
            # the load frame (the «Каркас» successor): a slim strut down the outside of each leg into the boot's heel,
            # flat actuator housings at the hip and the knee - both legs alike
            cyl("strut_up%d" % sx, (sx * 0.20, 0.0, WAIST), (sx * 0.175, 0.02, 0.53), 0.012, coll, STEEL)
            cyl("strut_lo%d" % sx, (sx * 0.175, 0.02, 0.53), (sx * 0.185, 0.04, 0.06), 0.011, coll, STEEL)
            box("act_hip%d" % sx, (sx * 0.20 - 0.025, -0.04, WAIST - 0.06), (sx * 0.20 + 0.025, 0.05, WAIST + 0.03), coll, SHELL, 0.01)
            box("act_knee%d" % sx, (sx * 0.178 - 0.022, -0.02, 0.48), (sx * 0.178 + 0.022, 0.06, 0.57), coll, SHELL, 0.01)
            box("sole%d" % sx, (sx * 0.17 - 0.06, -0.20, 0.0), (sx * 0.17 + 0.06, 0.08, 0.035), coll, DARK, 0.01)   # a lugged sole
            for k in range(5): box("lug%d_%d" % (sx, k), (sx * 0.17 - 0.06, -0.19 + k * 0.055, -0.006), (sx * 0.17 + 0.06, -0.165 + k * 0.055, 0.0), coll, DARK)
            box("cuff%d" % sx, (sx * 0.40 - 0.03, -0.16, 1.07), (sx * 0.40 + 0.04, -0.07, 1.13), coll, DARK, 0.008)   # the glove's cuff ring
        cyl("crotch", (0, -0.12, WAIST - 0.03), (0, 0.10, WAIST - 0.03), 0.012, coll, DARK)

    def hut(coll):
        """the hard upper torso (А, В): shell over the chest and the back, the neck ring, the arm bearings"""
        # conformal (the figure kept): the cloth's own torso as it is drawn, taken off it 2 cm out - no egg round her
        dg = bpy.context.evaluated_depsgraph_get(); ev = cov.evaluated_get(dg); me = ev.to_mesh()
        bm = bmesh.new(); bm.from_mesh(me); ev.to_mesh_clear(); bm.transform(cov.matrix_world)
        bm.normal_update()
        drop = [f for f in bm.faces if not (1.09 < f.calc_center_median().z < NECK + 0.01) or abs(f.calc_center_median().x) > (0.17 if f.calc_center_median().z > SHOULDER - 0.08 else 0.155)]
        bmesh.ops.delete(bm, geom=drop, context='FACES')
        bmesh.ops.smooth_vert(bm, verts=bm.verts, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
        bm.normal_update()
        for v in bm.verts: v.co += v.normal * 0.02
        s = mesh_obj("hut", bm, coll, SHELL)
        sd = s.modifiers.new("thick", 'SOLIDIFY'); sd.thickness = 0.012
        sm_ = s.modifiers.new("smooth", 'SUBSURF'); sm_.levels = 1
        torus("neckring", (0, -0.005, NECK + 0.005), 0.115, 0.016, coll, STEEL, scale=(1, 0.93, 1))
        for sx in (-1, 1):
            rot = Matrix.Rotation(math.radians(90), 3, 'Y') @ Matrix.Rotation(math.radians(sx * -18), 3, 'X')
            torus("armring%d" % sx, (sx * 0.200, -0.01, SHOULDER - 0.03), 0.068, 0.014, coll, STEEL, scale=(1, 1, 1), rot=Matrix.Rotation(math.radians(sx * 90 + sx * -25), 3, 'Y'))
        # Soviet red: a stripe down the centre of the back pack and the shoulder bands
        for sx in (-1, 1): box("band%d" % sx, (sx * 0.06 - 0.004, FRONT - 0.05, CHEST + 0.12), (sx * 0.06 + 0.004, FRONT - 0.04, CHEST + 0.19), coll, RED)

    def backpack_door(coll):
        """Orlan's rear entry: the life-support pack is the door - hinged on her right, the lever on her left"""
        y0 = BACK + 0.04
        box("pack", (-0.20, y0, 1.07), (0.20, y0 + 0.21, 1.58), coll, SHELL, 0.035)
        cyl("hinge", (0.205, y0 + 0.02, 1.12), (0.205, y0 + 0.02, 1.52), 0.016, coll, STEEL)
        cyl("lever", (-0.215, y0 + 0.05, 1.18), (-0.215, y0 + 0.05, 1.40), 0.012, coll, RED)
        box("lever_arm", (-0.235, y0 + 0.03, 1.38), (-0.205, y0 + 0.07, 1.41), coll, RED)
        box("vent", (-0.12, y0 + 0.212, 1.12), (0.12, y0 + 0.214, 1.20), coll, DARK)
        for k in range(4): box("rib%d" % k, (-0.16, y0 + 0.212, 1.25 + k * 0.06), (0.16, y0 + 0.218, 1.262 + k * 0.06), coll, SHELL)
        box("stripe", (-0.012, y0 + 0.212, 1.22), (0.012, y0 + 0.216, 1.56), coll, RED)
        text("stencil", "37 ЗВЁЗДНАЯ", (0.0, y0 + 0.218, 1.48), (math.radians(90), 0, math.radians(180)), 0.035, coll, RED)
        text("stencil2", "«ТАНТРА»", (0.0, y0 + 0.218, 1.44), (math.radians(90), 0, math.radians(180)), 0.028, coll, DARK)

    def console_chest(coll, narrow=False):
        """the chest console (Orlan's БСС), flat and set into the torso, tilted down to be read; the user's front plate"""
        w = 0.10 if narrow else 0.14
        o = box("console", (-w, -0.0325, -0.035), (w, 0.0325, 0.035), coll, SHELL, 0.012)
        o.rotation_euler = (math.radians(-28), 0, 0); o.location = (0, FRONT - 0.13, 1.16)
        # the face of it (tilted with it, 28 deg): the screen, the knobs, the red toggle - children of the console
        for nm, lo, hi, m in (("screen", (-w + 0.02, -0.0335, -0.022), (w - 0.06, -0.030, 0.026), SCREEN),
                              ("toggle", (-w + 0.008, -0.045, -0.02), (-w + 0.03, -0.0325, -0.005), RED)):
            c_ = box(nm, lo, hi, coll, m); c_.parent = o
        for k in range(3):
            kb = cyl("knob%d" % k, (w - 0.035, -0.0325, 0.02 - k * 0.02), (w - 0.035, -0.047, 0.02 - k * 0.02), 0.007, coll, DARK, 12); kb.parent = o

    def lamps_brow(coll, c, w, y, z):
        box("lampbar", (c.x - w, y - 0.004, z - 0.006), (c.x + w, y + 0.002, z + 0.006), coll, LAMP)

    def helmet_orlan(coll):
        """А: fixed to the torso; a shell longer than it is wide (the head turns inside, the nose has room), a big visor
        over the front and the sides, the top window (Orlan's), a brow ridge with the lamp bars, flat side pods (comms)"""
        c = Vector((0, -0.02, NECK + 0.17)); r = (0.150, 0.180, 0.200)
        sh = ellipsoid("helm", c, r, coll, SHELL)
        vc = box("visor_cut", (-0.40, -0.40, NECK + 0.085), (0.40, c.y - 0.03, NECK + 0.255), coll, SHELL)
        wc = box("window_cut", (-0.07, -0.13, NECK + 0.33), (0.07, 0.02, NECK + 0.5), coll, SHELL)
        cut(sh, vc); cut(sh, wc)
        g1 = ellipsoid("visor", c, (r[0] * 0.985, r[1] * 0.985, r[2] * 0.985), coll, GLASS)
        gi = box("visor_in", (-0.40, -0.40, NECK + 0.085), (0.40, c.y - 0.03, NECK + 0.255), coll, SHELL); cut(g1, gi, 'INTERSECT')
        g2 = ellipsoid("window", c, (r[0] * 0.985, r[1] * 0.985, r[2] * 0.985), coll, GLASS)
        gw = box("window_in", (-0.07, -0.13, NECK + 0.33), (0.07, 0.02, NECK + 0.5), coll, SHELL); cut(g2, gw, 'INTERSECT')
        box("brow", (-0.13, c.y - 0.185, NECK + 0.255), (0.13, c.y - 0.10, NECK + 0.285), coll, SHELL, 0.012)
        lamps_brow(coll, c, 0.11, c.y - 0.187, NECK + 0.27)
        for sx in (-1, 1): box("pod%d" % sx, (sx * 0.150 - 0.02, c.y - 0.05, NECK + 0.15), (sx * 0.150 + 0.02, c.y + 0.07, NECK + 0.24), coll, SHELL, 0.01)
        box("helmstripe", (-0.008, c.y - 0.02, c.z + 0.19), (0.008, c.y + 0.16, c.z + 0.202), coll, RED)

    def helmet_sokol(coll):
        """Б: a close helmet that turns with the head on a neck bearing - the skull's shape with 2-3 cm to spare, a wrap
        visor round the eyes and the cheeks, the chin guard, the lamps at the temples"""
        c = HEAD + Vector((0, 0.0, 0.01)); r = (0.118, 0.140, 0.145)
        sh = ellipsoid("helm", c, r, coll, SHELL)
        vc = box("visor_cut", (-0.30, -0.30, c.z - 0.075), (0.30, c.y + 0.02, c.z + 0.055), coll, SHELL); cut(sh, vc)
        g = ellipsoid("visor", c, (r[0] * 0.99, r[1] * 0.99, r[2] * 0.99), coll, GLASS)
        gi = box("visor_in", (-0.30, -0.30, c.z - 0.075), (0.30, c.y + 0.02, c.z + 0.055), coll, SHELL); cut(g, gi, 'INTERSECT')
        box("chin", (-0.07, c.y - 0.15, c.z - 0.13), (0.07, c.y - 0.06, c.z - 0.075), coll, SHELL, 0.02)
        torus("bearing", (0, -0.01, NECK + 0.02), 0.085, 0.015, coll, DARK, scale=(1, 0.95, 1))
        for sx in (-1, 1):
            box("temple%d" % sx, (sx * r[0] - 0.012, c.y - 0.08, c.z + 0.03), (sx * r[0] + 0.012, c.y - 0.01, c.z + 0.055), coll, SHELL, 0.004)
            box("lamp%d" % sx, (sx * r[0] - 0.013, c.y - 0.082, c.z + 0.036), (sx * r[0] + 0.013, c.y - 0.075, c.z + 0.049), coll, LAMP)
        box("helmstripe", (-0.007, c.y - 0.05, c.z + r[2] - 0.004), (0.007, c.y + 0.12, c.z + r[2] + 0.006), coll, RED)

    def helmet_facet(coll):
        """В: fixed to the torso, faceted - flat facets and edges, a faceted visor, the brow with the lamps"""
        c = Vector((0, -0.02, NECK + 0.17)); r = (0.150, 0.178, 0.195)
        sh = ellipsoid("helm", c, r, coll, SHELL, ico=True)
        vc = box("visor_cut", (-0.40, -0.40, NECK + 0.09), (0.40, c.y - 0.02, NECK + 0.25), coll, SHELL); cut(sh, vc)
        g = ellipsoid("visor", c, (r[0] * 0.985, r[1] * 0.985, r[2] * 0.985), coll, GLASS, ico=True)
        gi = box("visor_in", (-0.40, -0.40, NECK + 0.09), (0.40, c.y - 0.02, NECK + 0.25), coll, SHELL); cut(g, gi, 'INTERSECT')
        lamps_brow(coll, c, 0.10, c.y - 0.183, NECK + 0.262)
        box("helmstripe", (-0.008, c.y - 0.03, c.z + 0.185), (0.008, c.y + 0.15, c.z + 0.198), coll, RED)

    def soft_torso(coll):
        """Б: no hard torso - the cloth to the neck bearing; the consumables in two packs at the chest's sides on the
        harness (the centre left free), the pack on the back as a plain carrier, the controls on the left forearm"""
        for sx in (-1, 1):
            box("sidepack%d" % sx, (sx * 0.10 - 0.05, FRONT - 0.07, 1.16), (sx * 0.10 + 0.05, FRONT + 0.0, 1.32), coll, SHELL, 0.015)
            cyl("strap%d" % sx, (sx * 0.10, FRONT - 0.01, 1.33), (sx * 0.09, BACK + 0.02, 1.40), 0.012, coll, DARK)
            box("cart%d" % sx, (sx * 0.10 - 0.035, FRONT - 0.075, 1.20), (sx * 0.10 + 0.035, FRONT - 0.069, 1.28), coll, RED)
        box("pack", (-0.17, BACK + 0.02, 1.10), (0.17, BACK + 0.19, 1.50), coll, SHELL, 0.03)
        box("stripe", (-0.012, BACK + 0.19, 1.15), (0.012, BACK + 0.194, 1.48), coll, RED)
        text("stencil", "37 ЗВЁЗДНАЯ", (0.0, BACK + 0.196, 1.43), (math.radians(90), 0, math.radians(180)), 0.032, coll, RED)
        # the forearm panel (her left: +x)
        box("forearm", (0.36, -0.13, 1.12), (0.41, -0.04, 1.20), coll, SHELL, 0.01)
        box("forearm_scr", (0.365, -0.134, 1.135), (0.405, -0.131, 1.185), coll, SCREEN)

    VARIANTS = [
        ("A_orlan500", "А «Орлан-500»", lambda c: (hut(c), backpack_door(c), console_chest(c), helmet_orlan(c))),
        ("B_sokol500", "Б «Сокол-500»", lambda c: (soft_torso(c), helmet_sokol(c))),
        ("V_gran", "В «Грань»", lambda c: (hut(c), backpack_door(c), console_chest(c, narrow=True), helmet_facet(c))),
    ]

    # ---------------- the stage ----------------
    for o in list(bpy.data.objects):
        if o.type in ('LIGHT', 'CAMERA'): bpy.data.objects.remove(o)
    for eng in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE', 'CYCLES'):
        try: sc.render.engine = eng; break
        except Exception: pass
    if sc.render.engine == 'CYCLES': sc.cycles.samples = 64
    sc.render.resolution_x, sc.render.resolution_y = 1000, 1400
    sc.world = sc.world or bpy.data.worlds.new("w"); sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None) or sc.world.node_tree.nodes.new("ShaderNodeBackground")
    bg.inputs[0].default_value = (0.20, 0.21, 0.23, 1); bg.inputs[1].default_value = 0.6
    fl = bpy.data.meshes.new("floor"); fl.from_pydata([(-6, -6, 0), (6, -6, 0), (6, 6, 0), (-6, 6, 0)], [], [(0, 1, 2, 3)])
    fo = bpy.data.objects.new("floor", fl); sc.collection.objects.link(fo); fl.materials.append(mat("mk_floor", (0.35, 0.36, 0.38), 0.8))
    def light(name, kind, loc, energy, size=1.0):
        ld = bpy.data.lights.new(name, kind); ld.energy = energy
        if kind == 'AREA': ld.size = size
        o = bpy.data.objects.new(name, ld); o.location = loc; sc.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (Vector((0, 0, 1.1)) - Vector(loc)).to_track_quat('-Z', 'Y'); return o
    light("key", 'AREA', (-2.2, -2.6, 3.0), 260, 2.0); light("fill", 'AREA', (2.5, -1.5, 1.8), 70, 2.0); light("rim", 'AREA', (0.5, 3.0, 2.6), 160, 1.5)
    cd = bpy.data.cameras.new("cam"); cam = bpy.data.objects.new("cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    def shoot(path, loc, tgt, lens=50):
        cd.lens = lens; cam.location = loc; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = path; bpy.ops.render.render(write_still=True)

    for key, title, build in VARIANTS:
        coll = bpy.data.collections.new(key); sc.collection.children.link(coll)
        common(coll); build(coll)
        shoot(os.path.join(OUT, key + "_34.png"), (-1.55, -3.2, 1.45), (0, 0, 0.98), 45)
        shoot(os.path.join(OUT, key + "_side.png"), (3.4, 0.05, 1.1), (0, 0.0, 0.98), 45)
        shoot(os.path.join(OUT, key + "_back.png"), (1.3, 3.1, 1.5), (0, 0.05, 1.05), 45)
        shoot(os.path.join(OUT, key + "_head.png"), (-0.55, -1.05, 1.62), (0, -0.02, 1.58), 50)
        for o in list(coll.objects): bpy.data.objects.remove(o)
        bpy.data.collections.remove(coll)
    open(os.path.join(OUT, "done.txt"), "w").write("ok")

except Exception:
    open(os.path.join(OUT, "error.txt"), "w", encoding="utf-8").write(traceback.format_exc())
