# Рендер реального меша лэндера «Грань» Т1Б-А по режимам (из lander_mesh/out/lander_mesh.json — тот же меш, что Lander.msh).
# Запуск: powershell -File run.ps1 render_lander_modes.py  -> renders/lander_A_<режим>_<вид>.png, лог render_lander_modes.log
import bpy, json, math, os
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "lander_mesh", "out", "lander_mesh.json")
OUT = os.path.join(HERE, "renders")
LOG = os.path.join(HERE, "render_lander_modes.log"); log = []
def L(s):
    log.append(str(s)); open(LOG, "w", encoding="utf-8").write("\n".join(log))

try:
    D = json.load(open(SRC, encoding="utf-8"))
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1400, 900
    sc.view_settings.view_transform = 'AgX' if 'AgX' in [i.identifier for i in sc.view_settings.bl_rna.properties['view_transform'].enum_items] else 'Filmic'
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    nt = world.node_tree; nt.nodes.clear()   # имена узлов локализованы (русский интерфейс) — создаём заново
    bg = nt.nodes.new("ShaderNodeBackground"); wo = nt.nodes.new("ShaderNodeOutputWorld"); nt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.045, 0.05, 0.06, 1); bg.inputs[1].default_value = 0.8

    def to_b(p):   # спецификация x вперёд, y вверх, z вправо -> Blender X, Y, Z (вверх)
        return Vector((p[0], -p[2], p[1]))

    mats = {}
    for name, m in D["materials"].items():
        c = m["color"]; rgb = tuple(int(c[i:i + 2], 16) / 255 for i in (1, 3, 5))
        mt = bpy.data.materials.new(name); mt.use_nodes = True
        mnt = mt.node_tree
        mnt.nodes.clear()
        b = mnt.nodes.new("ShaderNodeBsdfPrincipled"); mo = mnt.nodes.new("ShaderNodeOutputMaterial"); mnt.links.new(b.outputs[0], mo.inputs[0])
        b.inputs["Base Color"].default_value = (*[v ** 2.2 for v in rgb], 1)
        b.inputs["Roughness"].default_value = 0.45 if name in ("metall", "katushka") else 0.6
        b.inputs["Metallic"].default_value = 0.8 if name in ("metall", "katushka") else 0.0
        mats[name] = mt

    def rot(axis, deg):
        a = Vector(axis).normalized()
        return Matrix.Rotation(math.radians(deg), 3, a)

    group_anim = {}
    for e in D["anim"]:
        for g in e["groups"]:
            group_anim[g] = e

    def build(mode):
        for o in [o for o in bpy.data.objects if o.name.startswith("G_")]:
            bpy.data.objects.remove(o, do_unlink=True)
        for g in D["groups"]:
            if g["ntri"] == 0:
                continue
            V = [Vector(g["v"][i:i + 3]) for i in range(0, len(g["v"]), 3)]
            N = [Vector(g["n"][i:i + 3]) for i in range(0, len(g["n"]), 3)]
            e = group_anim.get(g["name"])
            if e and e["modes"].get(mode, 0):
                R = rot(e["axis"], e["modes"][mode]); P = Vector(e["pivot"])
                V = [R @ (v - P) + P for v in V]; N = [R @ n for n in N]
            me = bpy.data.meshes.new("G_" + g["name"])
            I = g["i"]
            me.from_pydata([tuple(to_b(v)) for v in V], [], [tuple(I[k:k + 3]) for k in range(0, len(I), 3)])
            me.update()
            try:
                me.normals_split_custom_set_from_vertices([tuple(to_b(n).normalized()) if n.length > 1e-9 else (0, 0, 1) for n in N])
            except Exception as ex:
                L("normals " + g["name"] + ": " + str(ex))
            me.materials.append(mats[g["mat"]])
            ob = bpy.data.objects.new("G_" + g["name"], me); sc.collection.objects.link(ob)

    def light(name, loc, energy, color=(1, 1, 1)):
        l = bpy.data.lights.new(name, 'SUN'); l.energy = energy; l.color = color
        o = bpy.data.objects.new(name, l); o.location = loc; sc.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y')
    light("Key", (20, -14, 18), 3.2); light("Fill", (-18, -10, 6), 0.9, (0.8, 0.86, 1.0)); light("Rim", (-10, 16, 10), 1.6, (0.85, 0.9, 1.0)); light("Under", (10, -12, -16), 1.4, (0.9, 0.92, 1.0))
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")); sc.collection.objects.link(cam); sc.camera = cam

    SHOTS = {   # вид: (положение камеры, цель, фокус)
        "34": ((26, 24, 13), (0.5, 0, -0.4), 50),          # сверху-спереди-слева (люк)
        "below": ((22, -26, -12), (0, 0, -0.8), 50),       # снизу-спереди-справа: чаши, шасси, щиток
        "front": ((48, 0, 2.0), (0, 0, -0.2), 68),
        "top": ((0.8, 0, 50), (0.8, 0, 0), 55),
    }
    MODES = D["modes"]
    for mode in MODES:
        build(mode)
        for v, (loc, tgt, lens) in SHOTS.items():
            if v == "top" and mode not in ("glide", "cruise", "stowed"):
                continue
            cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
            cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
            bpy.context.view_layer.update()
            sc.render.filepath = os.path.join(OUT, "lander_A_%s_%s.png" % (mode, v))
            bpy.ops.render.render(write_still=True)
            L("rendered %s %s" % (mode, v))
    L("DONE")
except Exception as ex:
    import traceback
    L("ERROR " + traceback.format_exc())
