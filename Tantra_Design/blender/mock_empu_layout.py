# Э.МПУ - the layout of its systems as coloured blocks with labels (function first, the skin after). A look only.
# blender --background astronavigator_coverall.blend --python mock_empu_layout.py
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
src = open(os.path.join(HERE, "mock_mpu.py"), encoding="utf-8").read()
exec(src[src.index("import bpy"):src.index("try:\n    for o in bpy.data.objects")])
LOG = os.path.join(HERE, "mock_empu_layout.log"); log = []
FONT = None
labels = []

def blk(name, lo, hi, col, alpha=1.0, label=None, lpos=None):
    b = bmesh.new(); box(b, lo, hi, 0, 0.02)
    m = mat("L_" + name, col, 0.6, 0.0, alpha)
    o = obj(name, b, [m])
    if label: labels.append((label, lpos or ((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2] + 0.15)))
    return o

def tank(name, c, r, L, col, label=None, axis='y'):
    b = bmesh.new(); c = Vector(c); u = Vector((0, 1, 0)) if axis == 'y' else Vector((0, 0, 1))
    cyl(b, c - u * L / 2, c + u * L / 2, r, 0, 24)
    o = obj(name, b, [mat("L_" + name, col, 0.4, 0.3)])
    if label: labels.append((label, (c.x, c.y, c.z + r + 0.2)))
    return o

def text(s, pos, size=0.22):
    cu = bpy.data.curves.new("T", 'FONT'); cu.body = s; cu.size = size; cu.align_x = 'CENTER'
    if FONT: cu.font = FONT
    o = bpy.data.objects.new("T", cu); bpy.context.scene.collection.objects.link(o)
    o.location = O + Vector(pos)
    m = bpy.data.materials.new("Txt"); m.use_nodes = True
    bs = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bs.inputs["Base Color"].default_value = (1, 1, 1, 1)
    try: bs.inputs["Emission Color"].default_value = (1, 1, 1, 1); bs.inputs["Emission Strength"].default_value = 3.0
    except Exception: pass
    cu.materials.append(m)
    return o

try:
    try: FONT = bpy.data.fonts.load(r"C:\Windows\Fonts\arial.ttf")
    except Exception: FONT = None
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    MATS = [mat("PHull", (0.11, 0.12, 0.135), 0.6, 0.3), mat("PWhite", (0.85, 0.86, 0.87), 0.45), mat("PMetal", (0.32, 0.33, 0.35), 0.3, 0.8),
            mat("PGlass", (0.08, 0.13, 0.18), 0.05, 0.5), mat("PTread", (0.05, 0.05, 0.055), 0.85), mat("PRed", (0.72, 0.12, 0.10), 0.4),
            mat("PLamp", (1.0, 0.96, 0.85), 0.15), mat("PSeam", (0.04, 0.04, 0.045), 0.7), mat("PBlade", (0.20, 0.22, 0.26), 0.35, 0.6)]
    sys.path.insert(0, HERE); import render_util
    O = Vector((3.4, 6.9, 0.0))
    b = bmesh.new(); chassis(b); obj("Chassis", b, MATS)
    # the pressure volume (translucent): cabin + habitat, the airlock behind, narrower
    blk("Press", (-1.45, -3.90, 1.55), (1.45, 2.55, 3.40), (0.55, 0.75, 0.95), 0.25, "ГЕРМООБЪЁМ 70 кПа: кабина + жилой", (0, -0.6, 3.95))
    blk("Lock", (-0.90, 2.70, 1.55), (0.90, 4.20, 3.40), (0.95, 0.80, 0.30), 0.35, "ШЛЮЗ (дверь в торце)", (0, 3.45, 3.95))
    # the nose: the cab's lower glazing sloping down - the ground by the front wheels in view
    blk("Cab", (-1.45, -4.25, 1.55), (1.45, -3.90, 2.60), (0.30, 0.55, 0.85), 0.6, "НОС: нижнее остекление (обзор колёс)", (0, -4.6, 2.9))
    # side bays between the shell and the skin: gases and water on the left, thermal on the right
    tank("O2a", (1.70, -1.6, 1.80), 0.18, 1.2, (0.30, 0.55, 0.95), "O2 / N2")
    tank("O2b", (1.70, -0.2, 1.80), 0.18, 1.2, (0.30, 0.55, 0.95))
    blk("CO2", (1.50, 0.55, 1.45), (1.88, 1.55, 2.25), (0.35, 0.80, 0.45), 1.0, "регенерация CO2", (1.9, 1.05, 2.5))
    tank("WaterL", (1.68, 2.0, 1.75), 0.22, 0.8, (0.20, 0.45, 0.80), "вода")
    blk("Pumps", (-1.88, -1.9, 1.45), (-1.50, -0.6, 2.25), (0.95, 0.45, 0.25), 1.0, "контур: насосы, т/обменник", (-1.9, -1.25, 2.5))
    blk("Heat", (-1.88, -0.4, 1.45), (-1.50, 0.8, 2.10), (0.95, 0.25, 0.20), 1.0, "нагреватели", (-1.9, 0.2, 2.35))
    tank("WaterR", (-1.68, 1.6, 1.75), 0.22, 1.0, (0.20, 0.45, 0.80), "вода")
    # in the spine: the battery and the metallic hydrogen tank (low: the centre of gravity)
    blk("Batt", (-0.9, -2.6, 0.62), (0.9, 0.4, 1.28), (0.95, 0.85, 0.20), 1.0, None)
    tank("H2", (0, 2.0, 0.95), 0.32, 2.4, (0.75, 0.75, 0.80), None)
    labels.append(("в хребте: батарея 1 МВт·ч, бак водорода УВТ", (0, -0.3, 0.25)))
    # the roof: radiators (tilting), the solar array, the mast (antenna, lidar)
    blk("Rad1", (-1.45, -2.4, 3.48), (-0.05, 1.8, 3.52), (0.92, 0.92, 0.95), 1.0, "радиаторы (наклон к небу)", (-0.75, -0.3, 3.85))
    blk("Rad2", (0.05, -2.4, 3.48), (1.45, 1.8, 3.52), (0.92, 0.92, 0.95), 1.0)
    blk("Solar", (-1.45, 1.9, 3.48), (1.45, 4.1, 3.52), (0.05, 0.08, 0.30), 1.0, "солнечная панель + крылья", (0, 3.0, 3.85))
    b = bmesh.new(); cyl(b, (0.0, -3.5, 3.40), (0.0, -3.5, 4.10), 0.04, 0, 8); cyl(b, (0.0, -3.5, 4.10), (0.0, -3.62, 4.35), 0.28, 0, 20)
    obj("Mast", b, [mat("L_mast", (0.85, 0.85, 0.85), 0.5)]); labels.append(("мачта: антенна, лидар, камеры", (0, -3.5, 4.6)))
    # cargo: two outside lockers either side of the airlock, a roof basket; heavy cargo goes on coupled trailers
    blk("CargoL", (0.95, 2.70, 1.42), (1.80, 4.25, 2.70), (0.85, 0.50, 0.15), 1.0, "груз", (1.4, 3.5, 3.0))
    blk("CargoR", (-1.80, 2.70, 1.42), (-0.95, 4.25, 2.70), (0.85, 0.50, 0.15), 1.0, "груз", (-1.4, 3.5, 3.0))
    # the couplings front and rear, low on the spine: a hitch head and a power / data plug - МПУ, Э.МПУ, tankers in a train
    for sy in (-1, 1):
        b = bmesh.new()
        cyl(b, (0, sy * 4.40, 0.95), (0, sy * 4.95, 0.95), 0.10, 0, 12); cyl(b, (0, sy * 4.95, 0.95), (0, sy * 5.10, 0.95), 0.22, 0, 20)
        cyl(b, (0.45, sy * 4.40, 1.05), (0.45, sy * 4.75, 1.05), 0.06, 0, 10)
        obj("Hitch%d" % sy, b, [mat("L_hitch%d" % sy, (0.95, 0.75, 0.10), 0.4, 0.6)])
        labels.append(("СЦЕПКА + разъём питания/данных", (0, sy * 5.25, 1.45)))
    # the stern stairs (out, parked)
    b = bmesh.new()
    for k in range(4): box(b, (-0.40, 4.45 + k * 0.28, 1.10 - k * 0.27), (0.40, 4.75 + k * 0.28, 1.15 - k * 0.27), 0)
    obj("Stairs", b, [mat("L_st", (0.4, 0.4, 0.42), 0.4, 0.7)])
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (10, 10, 1)
    sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
    so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1800, 1100; cam.data.clip_end = 400
    texts = [text(s, p) for s, p in labels]
    for name, loc in (("front", Vector((-8.5, -8.5, 6.5))), ("rear", Vector((8.0, 10.5, 6.0)))):
        cam.data.type = 'PERSP'; cam.data.lens = 30; cam.rotation_mode = 'QUATERNION'
        tgt = Vector((O.x, O.y, 2.0)); cam.location = O + loc
        cam.rotation_quaternion = (tgt - cam.location).to_track_quat('-Z', 'Y')
        for t in texts: t.rotation_mode = 'QUATERNION'; t.rotation_quaternion = cam.rotation_quaternion
        bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "empu_layout_%s.png" % name); bpy.ops.render.render(write_still=True)
    log.append("rendered layout")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
