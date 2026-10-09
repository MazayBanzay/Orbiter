# Мокап удлинённого лэндера «Грань»: внешний вид и разрез с внутренностями (из lander_mesh/out_L/lander_L_<L>.json).
# Запуск: powershell -File run.ps1 render_lander_L.py -Rest 249   -> renders/lander_L249_<вид>.png, лог render_lander_L.log
import bpy, json, math, os, sys
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
TAG = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "249"
SRC = os.path.join(HERE, "..", "lander_mesh", "out_L", "lander_L_%s.json" % TAG)
OUT = os.path.join(HERE, "renders")
LOG = os.path.join(HERE, "render_lander_L_%s.log" % TAG); log = []
def L(s):
    log.append(str(s)); open(LOG, "w", encoding="utf-8").write("\n".join(log))

try:
    D = json.load(open(SRC, encoding="utf-8"))
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    sc.render.film_transparent = False
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    nt = world.node_tree; nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground"); wo = nt.nodes.new("ShaderNodeOutputWorld"); nt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.045, 0.05, 0.06, 1); bg.inputs[1].default_value = 0.8

    to_b = lambda p: Vector((p[0], -p[2], p[1]))   # x вперёд, y вверх, z вправо -> Blender X, Y, Z вверх

    def make_mat(name, hexcol, alpha=1.0, metal=False):
        rgb = tuple(int(hexcol[i:i + 2], 16) / 255 for i in (1, 3, 5))
        mt = bpy.data.materials.new(name); mt.use_nodes = True
        mn = mt.node_tree; mn.nodes.clear()
        b = mn.nodes.new("ShaderNodeBsdfPrincipled"); mo = mn.nodes.new("ShaderNodeOutputMaterial"); mn.links.new(b.outputs[0], mo.inputs[0])
        b.inputs["Base Color"].default_value = (*[v ** 2.2 for v in rgb], 1)
        b.inputs["Roughness"].default_value = 0.5
        b.inputs["Metallic"].default_value = 0.7 if metal else 0.0
        if alpha < 1.0:
            b.inputs["Alpha"].default_value = alpha
            for attr, val in (("surface_render_method", "BLENDED"), ("blend_method", "BLEND")):
                try: setattr(mt, attr, val)
                except Exception: pass
            try: mt.use_backface_culling = False
            except Exception: pass
        return mt

    MATS, MATS_T = {}, {}
    for name, m in D["materials"].items():
        MATS[name] = make_mat(name, m["color"], 1.0, name in ("metall", "katushka"))
        MATS_T[name] = make_mat(name + "_t", "#9aa3ad", 0.10)

    ganim = {}
    for e in D["anim"]:
        for g in e["groups"]:
            ganim[g] = e

    def build(mode, cut):
        for o in [o for o in bpy.data.objects if o.name.startswith("G_")]:
            bpy.data.objects.remove(o, do_unlink=True)
        for g in D["groups"]:
            if not g["ntri"]:
                continue
            interior = g.get("interior", False)
            if interior and not cut:
                continue
            V = [Vector(g["v"][i:i + 3]) for i in range(0, len(g["v"]), 3)]
            N = [Vector(g["n"][i:i + 3]) for i in range(0, len(g["n"]), 3)]
            e = ganim.get(g["name"])
            if e and e["modes"].get(mode, 0):
                R = Matrix.Rotation(math.radians(e["modes"][mode]), 3, Vector(e["axis"]).normalized()); P = Vector(e["pivot"])
                V = [R @ (v - P) + P for v in V]; N = [R @ n for n in N]
            me = bpy.data.meshes.new("G_" + g["name"]); I = g["i"]
            me.from_pydata([tuple(to_b(v)) for v in V], [], [tuple(I[k:k + 3]) for k in range(0, len(I), 3)]); me.update()
            try:
                me.normals_split_custom_set_from_vertices([tuple(to_b(n).normalized()) if n.length > 1e-9 else (0, 0, 1) for n in N])
            except Exception:
                pass
            me.materials.append(MATS[g["mat"]] if (interior or not cut) else MATS_T[g["mat"]])
            ob = bpy.data.objects.new("G_" + g["name"], me); sc.collection.objects.link(ob)

    def light(name, loc, energy, color=(1, 1, 1)):
        l = bpy.data.lights.new(name, 'SUN'); l.energy = energy; l.color = color
        o = bpy.data.objects.new(name, l); o.location = loc; sc.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (Vector((0, 0, 0)) - Vector(loc)).to_track_quat('-Z', 'Y')
    light("Key", (20, 14, 18), 3.2); light("Fill", (-18, 10, 6), 1.0, (0.8, 0.86, 1.0)); light("Rim", (-10, -16, 10), 1.6); light("Under", (10, 12, -16), 1.2)
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")); sc.collection.objects.link(cam); sc.camera = cam

    xc = 0.5 * (-9.015 + (10.785 + D["L_meta"]["PLUG"]))
    SHOTS = [   # имя, режим, разрез, камера, цель, фокус
        ("ext34", "glide", False, (xc + 24, 26, 13), (xc, 0, -0.4), 45),
        ("extside", "glide", False, (xc, 55, 0.2), (xc, 0, 0.2), 55),
        ("exttop", "stowed", False, (xc, 0, 60), (xc, 0, 0), 55),
        ("cutside", "glide", True, (xc, 55, 0.6), (xc, 0, 0.6), 55),
        ("cuttop", "cruise", True, (xc, 0, 60), (xc, 0, 0), 55),
        ("cut34", "cruise", True, (xc + 20, 22, 16), (xc, 0, 0.3), 45),
    ]
    for name, mode, cut, loc, tgt, lens in SHOTS:
        build(mode, cut)
        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(OUT, "lander_L%s_%s.png" % (TAG, name))
        bpy.ops.render.render(write_still=True)
        L("rendered " + name)
    L("DONE")
except Exception:
    import traceback
    L("ERROR " + traceback.format_exc())
