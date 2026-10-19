# THE BACK BLOCK (life support) of Suit_T10, 10 architectures (the user, 2026-10-18: «редизайн блока скафандра на спине
# в соответствии с выбранным стилем и технологией... блок питания, кислород, газы для систем, охладитель - в соответствии
# с системами»; the jet pack is NOT considered - it is reworked after this).
# Its contents from the game's class Suit_T10: battery 6 kWh (supply 1 kW; the MPU cell's cassette format), O2 1.0 kg,
# N2 1.5 kg (cold-gas RCS, purge), CO2 sorbent 1.6 kg, water 2 kg, heat pump (cooling up to 700 W, COP 3 -> ~930 W to
# reject), the magnetic field's power unit (600 W), the drives' controller. Volumes: O2 2.6 L + N2 4.8 L at 30 MPa
# (~9 L with bottles), battery ~6 L, sorbent ~5 L, water ~3 L, pump and electronics ~3 L: ~26 L, ~24 kg.
# Heat in vacuum: 857 W/m2 per face at 360 K -> the full load needs > 1 m2 of radiating face; the old block has ~0.3 m2.
# The 10 differ in where the masses sit and how the heat leaves. Soviet +500 years: white shell, light-metal frame,
# the gases' colour code (O2 blue, N2 black), red marking. Render only, on astronavigator_suit2.blend (the new type + «Бор»).
#   blender -b astronavigator_suit2.blend --python pack_concepts.py  (env PC_OUT, PC_ONLY "1,4")
import bpy, bmesh, math, os, traceback
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.environ.get("PC_OUT") or os.path.join(HERE, "..", "suit_mockups", "back_block"); os.makedirs(OUT, exist_ok=True)
ONLY = [s for s in os.environ.get("PC_ONLY", "").split(",") if s]
LOG = open(os.path.join(OUT, "pc_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
try:
    arm = bpy.data.objects["Astronavigator"]; arm.data.pose_position = 'REST'
    for o in bpy.data.objects:
        if o.name in ("Pack", "MarkPack") or o.name.startswith("Jet"): o.hide_render = True
    def mk(name, col, metal, rough, coat=0.0):
        m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
        if "Coat Weight" in b.inputs: b.inputs["Coat Weight"].default_value = coat
        m.diffuse_color = (*col, 1); return m
    SHELL = mk("PC_shell", (0.84, 0.85, 0.86), 0.0, 0.42)
    FRAME = mk("PC_frame", (0.62, 0.63, 0.65), 1.0, 0.32)
    DARK = mk("PC_dark", (0.10, 0.105, 0.11), 0.3, 0.5)
    RAD = mk("PC_rad", (0.78, 0.80, 0.82), 0.7, 0.25)          # radiator: silvered, white-coated fins
    RED = mk("PC_red", (0.78, 0.09, 0.07), 0.1, 0.35)
    O2C = mk("PC_o2", (0.12, 0.35, 0.72), 0.2, 0.35)           # oxygen: blue (the Soviet colour code)
    N2C = mk("PC_n2", (0.06, 0.06, 0.06), 0.2, 0.4)            # nitrogen: black
    H2O = mk("PC_h2o", (0.20, 0.45, 0.30), 0.1, 0.4)           # water: green
    AMB = mk("PC_cbn", (0.30, 0.068, 0.0), 0.0, 0.16, 0.6)     # the set's borazon (edge guards)
    parts = []
    def link(o): sc.collection.objects.link(o); parts.append(o); return o
    def obj(name, bm, m, smooth=False, bevel=0.0):
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        for f in me.polygons: f.use_smooth = smooth
        me.materials.append(m); o = link(bpy.data.objects.new(name, me))
        if bevel:
            b = o.modifiers.new("b", 'BEVEL'); b.width = bevel; b.segments = 3; b.limit_method = 'ANGLE'
            o.modifiers.new("n", 'WEIGHTED_NORMAL')
        return o
    def box(name, c, s, m, bevel=0.006, rot=None):
        bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
        bmesh.ops.scale(bm, vec=Vector(s), verts=bm.verts)
        if rot: bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=rot)
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, bevel=bevel)
    def cyl(name, c, r, L, axis, m, seg=28, bevel=0.004, caps=True):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=caps, segments=seg, radius1=r, radius2=r, depth=L)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector(axis)).to_matrix())
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); o = obj(name, bm, m, smooth=True, bevel=bevel); return o
    def bottle(name, c, r, L, axis, band):
        """a gas bottle: domed ends, a coloured band"""
        ax = Vector(axis).normalized(); c = Vector(c)
        cyl(name, c, r, L - 2 * r * 0.6, ax, FRAME)
        for sg in (-1, 1):
            bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=12, radius=r)
            bmesh.ops.scale(bm, vec=(1, 1, 0.6), verts=bm.verts)
            bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(ax).to_matrix())
            bmesh.ops.translate(bm, vec=c + ax * sg * (L / 2 - r * 0.6), verts=bm.verts); obj(name + "e", bm, FRAME, smooth=True)
        cyl(name + "b", c, r * 1.02, L * 0.35, ax, band, bevel=0.002)
    def hexprism(name, c, R, d, m, axis=(0, 1, 0), rot=0.0):
        bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=6, radius1=R, radius2=R, depth=d)
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(rot, 3, 'Z'))
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Vector((0, 0, 1)).rotation_difference(Vector(axis)).to_matrix())
        bmesh.ops.translate(bm, vec=Vector(c), verts=bm.verts); return obj(name, bm, m, bevel=0.005)
    def fins(name, x0, x1, z0, z1, y0, depth, pitch, m, vertical=True, t=0.0025):
        n = int(((x1 - x0) if vertical else (z1 - z0)) / pitch)
        for k in range(n + 1):
            if vertical: box(name, ((x0 + k * pitch), y0 + depth / 2, (z0 + z1) / 2), (t, depth, z1 - z0), m, bevel=0.0008)
            else: box(name, ((x0 + x1) / 2, y0 + depth / 2, z0 + k * pitch), (x1 - x0, depth, t), m, bevel=0.0008)
    def handle(c, w=0.06):
        c = Vector(c); cyl("hdl", c + Vector((0, 0.018, 0)), 0.005, w, (1, 0, 0), FRAME)
        for sx in (-1, 1): cyl("hdl", c + Vector((sx * w / 2, 0.009, 0)), 0.005, 0.018, (0, 1, 0), FRAME)
    def stripe(c, w, h=0.012): box("stripe", c, (w, 0.002, h), RED, bevel=0.0005)
    def mount():                          # the mounting plate on her back: the light frame the block hangs on
        box("mount", (0, 0.096, 1.27), (0.30, 0.012, 0.40), FRAME, bevel=0.004)
    Y0 = 0.10                              # the block's front face (her back's top surface ~0.087)

    CONCEPTS = []
    def concept(k, title, note):
        def d(f): CONCEPTS.append((k, title, note, f)); return f
        return d

    @concept("1", "Плита", "моноблок 46x13x44 см, слоями: батарея внизу, баллоны вверху, тыл - радиатор горизонтальными рёбрами ~0,35 м2; пиковая нагрузка - только в атмосфере")
    def c1():
        mount(); box("shell", (0, Y0 + 0.065, 1.28), (0.46, 0.13, 0.44), SHELL, bevel=0.018)
        fins("fin", -0.20, 0.20, 1.12, 1.44, Y0 + 0.13, 0.018, 0.022, RAD, vertical=False)
        box("lid", (0, Y0 + 0.132, 1.465), (0.40, 0.006, 0.05), FRAME, bevel=0.002); stripe((0, Y0 + 0.136, 1.465), 0.30)
        for sx in (-1, 1): box("hatch", (sx * 0.232, Y0 + 0.065, 1.30), (0.006, 0.09, 0.26), FRAME, bevel=0.002)

    @concept("2", "Кассеты", "открытая рама и сменные кассеты: батарея (формат ячейки МПУ), O2, N2, вода, поперёк - поглотитель; кассеты меняются на станции; радиаторы на боковинах")
    def c2():
        mount()
        for x in (-0.22, 0.22): box("post", (x, Y0 + 0.065, 1.28), (0.016, 0.13, 0.46), FRAME, bevel=0.003)
        for z in (1.06, 1.50): box("rail", (0, Y0 + 0.065, z), (0.46, 0.13, 0.016), FRAME, bevel=0.003)
        cells = [(-0.11, 1.17, (0.19, 0.11, 0.19), SHELL, None, "БАТ"), (0.11, 1.17, (0.19, 0.11, 0.19), SHELL, H2O, "H2O"),
                 (-0.11, 1.39, (0.19, 0.11, 0.19), SHELL, O2C, "O2"), (0.11, 1.39, (0.19, 0.11, 0.19), SHELL, N2C, "N2")]
        for x, z, s, m, band, lab in cells:
            box("cas", (x, Y0 + 0.058, z), s, m, bevel=0.008)
            if band: box("band", (x, Y0 + 0.115, z + 0.07), (s[0] - 0.02, 0.004, 0.02), band, bevel=0.001)
            handle((x, Y0 + 0.115, z - 0.02))
        box("sorb", (0, Y0 + 0.058, 1.28), (0.40, 0.11, 0.025), DARK, bevel=0.004)
        for sx in (-1, 1):                 # the radiators on the flanks: the cassettes stay open from behind
            box("radp", (sx * 0.236, Y0 + 0.065, 1.28), (0.006, 0.13, 0.42), RAD, bevel=0.002)
            for z in [1.09 + 0.024 * k for k in range(17)]: box("sf", (sx * 0.247, Y0 + 0.065, z), (0.018, 0.12, 0.0025), RAD, bevel=0.0008)

    @concept("3", "Пояс", "массы на поясе: баллоны и батарея в модулях на кольце таза (их несёт каркас, центр масс низко); на спине тонкий блок поглотителя и насоса + радиатор")
    def c3():
        mount(); box("thin", (0, Y0 + 0.03, 1.30), (0.36, 0.06, 0.34), SHELL, bevel=0.012)
        box("radp", (0, Y0 + 0.064, 1.30), (0.34, 0.006, 0.30), RAD, bevel=0.002)
        fins("fin", -0.16, 0.16, 1.17, 1.43, Y0 + 0.067, 0.010, 0.026, RAD, vertical=False)
        for sx in (-1, 1):
            bottle("o2" if sx > 0 else "n2", (sx * 0.20, 0.06, 0.94), 0.045, 0.22, (0, 1, 0.0), O2C if sx > 0 else N2C)
        box("batt", (0, 0.135, 0.985), (0.26, 0.07, 0.12), SHELL, bevel=0.01); handle((0, 0.17, 0.985))
        stripe((0, 0.172, 1.03), 0.18, 0.008)

    @concept("4", "Хребет", "колонна вдоль позвоночника: связка из 4 баллонов и кассет (ширина 18 см, руки свободны) + два радиаторных крыла, раскрываются по бокам до 0,9 м2")
    def c4():
        mount(); box("spine", (0, Y0 + 0.06, 1.28), (0.18, 0.12, 0.46), SHELL, bevel=0.02)
        for sx, m in ((-1, O2C), (1, N2C)): bottle("b", (sx * 0.05, Y0 + 0.13, 1.31), 0.032, 0.30, (0, 0, 1), m)
        for sx in (-1, 1):
            box("hinge", (sx * 0.095, Y0 + 0.06, 1.28), (0.012, 0.03, 0.40), FRAME, bevel=0.003)
            box("wing", (sx * 0.30, Y0 + 0.07, 1.30), (0.40, 0.008, 0.36), RAD, bevel=0.003, rot=Matrix.Rotation(sx * math.radians(-12), 3, 'Z'))
        stripe((0, Y0 + 0.122, 1.47), 0.12)

    @concept("5", "Коромысло", "баллоны поперёк лопаток под защитой плеч (O2 и N2, по 2 шт.), батарея и поглотитель в поясничном блоке; середина спины - радиатор; масса ближе к оси тела")
    def c5():
        mount()
        for z, m in ((1.45, O2C), (1.38, N2C)): bottle("y", (0, Y0 + 0.05, z), 0.034, 0.44, (1, 0, 0), m)
        for sx in (-1, 1): box("saddle", (sx * 0.20, Y0 + 0.05, 1.415), (0.03, 0.09, 0.13), FRAME, bevel=0.004)
        box("radp", (0, Y0 + 0.02, 1.25), (0.34, 0.012, 0.16), RAD, bevel=0.002)
        fins("fin", -0.16, 0.16, 1.18, 1.32, Y0 + 0.026, 0.015, 0.024, RAD)
        box("lumb", (0, Y0 + 0.05, 1.11), (0.40, 0.10, 0.12), SHELL, bevel=0.014); handle((0, Y0 + 0.10, 1.11))
        stripe((0, Y0 + 0.102, 1.155), 0.30, 0.008)

    @concept("6", "Раковина", "единая оболочка-радиатор: вертикальные рёбра по всей спинке и бокам (~0,7 м2 с рёбрами), под ней кассеты; советская оребрённая форма, рёбра ещё и жёсткость")
    def c6():
        mount(); box("shell", (0, Y0 + 0.055, 1.28), (0.42, 0.11, 0.44), SHELL, bevel=0.03)
        fins("fin", -0.19, 0.19, 1.09, 1.47, Y0 + 0.11, 0.025, 0.019, RAD)
        for sx in (-1, 1): fins("sfin", sx * 0.212, sx * 0.212, 1.10, 1.46, Y0 + 0.02, 0.07, 1.0, RAD, vertical=False)
        for sx in (-1, 1):
            for z in (1.12, 1.18, 1.24, 1.30, 1.36, 1.42): box("sf", (sx * 0.222, Y0 + 0.055, z), (0.025, 0.09, 0.0025), RAD, bevel=0.0008)
        box("crest", (0, Y0 + 0.14, 1.48), (0.40, 0.012, 0.02), FRAME, bevel=0.003); stripe((0, Y0 + 0.147, 1.48), 0.28, 0.008)

    @concept("7", "Крылья", "компактный блок 38x10x40 см + две складные радиаторные панели 0,35x0,45 м (двусторонние, 0,63 м2 на сторону): раскрыты - полный отбор 700 Вт в вакууме, сложены - вдоль боков")
    def c7():
        mount(); box("core", (0, Y0 + 0.05, 1.28), (0.38, 0.10, 0.40), SHELL, bevel=0.016)
        for sx in (-1, 1):
            cyl("hinge", (sx * 0.195, Y0 + 0.10, 1.28), 0.009, 0.40, (0, 0, 1), FRAME)
            box("wing", (sx * 0.38, Y0 + 0.14, 1.30), (0.35, 0.006, 0.45), RAD, bevel=0.002, rot=Matrix.Rotation(sx * math.radians(-20), 3, 'Z'))
            box("spar", (sx * 0.38, Y0 + 0.143, 1.30), (0.35, 0.012, 0.012), FRAME, bevel=0.002, rot=Matrix.Rotation(sx * math.radians(-20), 3, 'Z'))
        stripe((0, Y0 + 0.102, 1.44), 0.26)

    @concept("8", "Испаритель", "блок с сублиматором: пиковое тепло уходит испарением воды (2,8 МДж/кг; 300 Вт сверх радиатора ~0,4 кг/ч), бак воды 4 кг; радиатор только на базовую нагрузку")
    def c8():
        mount(); box("shell", (0, Y0 + 0.06, 1.30), (0.42, 0.12, 0.40), SHELL, bevel=0.02)
        box("water", (0, Y0 + 0.07, 1.075), (0.34, 0.10, 0.09), SHELL, bevel=0.02); box("wb", (0, Y0 + 0.121, 1.075), (0.30, 0.004, 0.02), H2O, bevel=0.001)
        box("subl", (0, Y0 + 0.122, 1.33), (0.30, 0.004, 0.24), DARK, bevel=0.002)
        for z in (1.24, 1.30, 1.36, 1.42): box("vent", (0, Y0 + 0.126, z), (0.28, 0.004, 0.006), FRAME, bevel=0.0008)
        stripe((0, Y0 + 0.124, 1.475), 0.30)

    @concept("9", "Позвонки", "блок из трёх сегментов на позвонках Spine1 / Spine / LowerBack с перекрытием: гнётся с ней (наклон 45°), сегменты - газы, поглотитель, батарея; радиатор на каждом")
    def c9():
        mount()
        segs = [(1.42, 0.13, O2C), (1.28, 0.15, None), (1.13, 0.13, None)]
        for k, (z, h, band) in enumerate(segs):
            d = 0.11 - 0.012 * k
            box("seg", (0, Y0 + d / 2 + 0.004 * k, z), (0.40 - 0.02 * k, d, h), SHELL, bevel=0.016)
            fins("fin", -0.16 + 0.01 * k, 0.16 - 0.01 * k, z - h / 2 + 0.02, z + h / 2 - 0.02, Y0 + d + 0.004 * k, 0.012, 0.024, RAD)
            for sx in (-1, 1): cyl("knuckle", (sx * (0.205 - 0.01 * k), Y0 + 0.05, z - h / 2), 0.012, 0.03, (1, 0, 0), FRAME)
            if band: box("band", (0, Y0 + d + 0.006, z + h / 2 - 0.012), (0.20, 0.003, 0.012), band, bevel=0.001)
        stripe((0, Y0 + 0.124, 1.48), 0.24, 0.008)

    @concept("10", "Ферма", "несущая ферма из светлых трубок; агрегаты открыто, каждый в своей обойме (баллоны вертикально по бокам, батарея-кассета в центре, насос и поглотитель над ней); лёгкая, ремонтопригодная, радиатор - ферменные панели")
    def c10():
        mount()
        for sx in (-1, 1):
            for z in (1.07, 1.49): cyl("tube", (sx * 0.20, Y0 + 0.07, z), 0.008, 0.14, (0, 1, 0), FRAME)
            for yy in (Y0 + 0.01, Y0 + 0.13): cyl("tube", (sx * 0.20, yy, 1.28), 0.008, 0.42, (0, 0, 1), FRAME)
            bottle("bt", (sx * 0.15, Y0 + 0.07, 1.28), 0.042, 0.34, (0, 0, 1), O2C if sx > 0 else N2C)
        for z in (1.07, 1.49):
            for yy in (Y0 + 0.01, Y0 + 0.13): cyl("tube", (0, yy, z), 0.008, 0.40, (1, 0, 0), FRAME)
        box("batt", (0, Y0 + 0.06, 1.17), (0.18, 0.10, 0.17), SHELL, bevel=0.01); handle((0, Y0 + 0.11, 1.17))
        box("pump", (0, Y0 + 0.06, 1.37), (0.18, 0.10, 0.16), DARK, bevel=0.008)
        for sx in (-1, 1): box("radp", (sx * 0.21, Y0 + 0.07, 1.28), (0.004, 0.13, 0.40), RAD, bevel=0.001)
        box("radt", (0, Y0 + 0.135, 1.28), (0.12, 0.004, 0.40), RAD, bevel=0.001); stripe((0, Y0 + 0.139, 1.45), 0.10, 0.008)

    # ---- the render: the helmet sheet's light; her back ----
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 560, 640
    if sc.world is None: sc.world = bpy.data.worlds.new("PC_world")
    sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None)
    if bg: bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("PC_key", 3.0, (42, 0, 145)), ("PC_fill", 1.4, (65, 0, -40)), ("PC_back", 2.0, (55, 0, 200)), ("PC_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("PC_cam"); cd.sensor_fit = 'VERTICAL'; cd.lens = 50
    cam = bpy.data.objects.new("PC_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0.0, 0.10, 1.22))
    VIEWS = {"q34": T + Vector((1.25, 1.75, 0.35)), "side": T + Vector((2.15, -0.05, 0.10)), "back": T + Vector((0.0, 2.15, 0.15))}
    for key, title, note, fn in CONCEPTS:
        if ONLY and key not in ONLY: continue
        fn()
        for vn, loc in VIEWS.items():
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "pc_%s_%s.png" % (key.zfill(2), vn)); bpy.ops.render.render(write_still=True)
        for o in parts: bpy.data.objects.remove(o, do_unlink=True)
        parts.clear(); print("rendered", key, title, file=LOG); LOG.flush()
    open(os.path.join(OUT, "pc_notes.txt"), "w", encoding="utf-8").write("\n".join("%s|%s|%s" % (k, t, n) for k, t, n, f in CONCEPTS))
    print("DONE", file=LOG)
except Exception:
    traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
