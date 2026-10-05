# OrbiterCrew - the seated figure on «Тантра»'s bridge as the game shows it: the skinned mesh written by
# tests\hands_trace.exe (the figure's own code: Skin, Motion, SeatArms), the bridge from Meshes\Tantra\TantraVC.msh with
# the commander's seat at the desk and the yoke out (moved as TantraYoke.cpp moves it). Renders through her eyes and
# from outside into <dir>\view_<scene>_<camera>.png.
# Usage: powershell -File Tantra_Design\blender\run.ps1 -Script <this> -Rest "<dir>,<scene1>+<scene2>..."
import bpy, os, sys, math, traceback
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
DIR = argv[0]
WANT = argv[1].split("+") if len(argv) > 1 else None
O = os.path.join("C:" + os.sep, "Games", "Orbiter-2024")
LOG = os.path.join(DIR, "seat_view.log"); log = []
SEAT0 = [64, 65, 66, 67, 68, 22, 23, 93, 84, 85, 86]; SEAT_BASE = 84
YOKE_COL = [46, 47]; YOKE_HUB = [48, 49, 50, 51, 52, 53]
import re
_IL = open(os.path.join("C:" + os.sep, "Games", "Orbiter-2024", "Orbitersdk", "samples", "Tantra", "orbiter2016", "InteriorLayout.h"), encoding="utf-8", errors="replace").read()
def _k(n): return np.array([float(x) for x in re.search(n + r"\[3\] = \{([^}]*)\}", _IL).group(1).split(",")])
yB, yH0, yU0 = _k("kYokeB"), _k("kYokeH0"), _k("kYokeU0")   # the yoke as built (TantraYoke.cpp moves it from there)


def read_msh(p):
    L = open(p).read().split('\n'); i = 0; groups = []; cur = {}; textures = []; mats = []
    while i < len(L):
        t = L[i].split()
        if not t: i += 1; continue
        if t[0] == 'LABEL' and len(t) > 1: cur['label'] = t[1]
        elif t[0] == 'MATERIAL' and len(t) > 1 and t[1].isdigit(): cur['mat'] = int(t[1])
        elif t[0] == 'TEXTURE' and len(t) > 1 and t[1].isdigit(): cur['tex'] = int(t[1])
        elif t[0] == 'GEOM':
            nv, nt = int(t[1]), int(t[2])
            rows = [L[i + 1 + k].split()[:8] for k in range(nv)]
            V = np.array([list(map(float, r)) + [0.0] * (8 - len(r)) for r in rows]) if nv else np.zeros((0, 8))
            F = np.array([list(map(int, L[i + 1 + nv + k].split()[:3])) for k in range(nt)]) if nt else np.zeros((0, 3), int)
            groups.append(dict(cur, V=V, F=F)); cur = {}; i += nv + nt
        elif t[0] == 'MATERIAL' and len(t) > 1:
            d = L[i + 1].split(); mats.append([float(x) for x in d[:4]] if len(d) >= 3 else [0.7, 0.7, 0.7, 1]); i += 4
        elif t[0] == 'TEXTURES':
            n = int(t[1]); textures = [L[i + 1 + k].split()[0] for k in range(n)]; i += n
        i += 1
    return groups, textures, mats


def B(P):   # Orbiter (left-handed: x right, y up, z forward) -> Blender (right-handed, z up): a mirror
    P = np.atleast_2d(P); return np.stack([-P[:, 0], -P[:, 2], P[:, 1]], 1)


def rot_to(a, b):
    v = np.cross(a, b); c = float(np.dot(a, b))
    if c < -0.9999: return np.diag([1, -1, -1.0])
    k = 1.0 / (1.0 + c)
    return np.array([[v[0] * v[0] * k + c, v[0] * v[1] * k - v[2], v[0] * v[2] * k + v[1]], [v[1] * v[0] * k + v[2], v[1] * v[1] * k + c, v[1] * v[2] * k - v[0]], [v[2] * v[0] * k - v[1], v[2] * v[1] * k + v[0], v[2] * v[2] * k + c]])


def mesh_obj(name, V, F, mat):
    me = bpy.data.meshes.new(name); me.from_pydata(B(V).tolist(), [], F[:, [0, 2, 1]].tolist()); me.update()
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob); return ob


def bsdf(m):
    m.use_nodes = True; nt = m.node_tree
    for nd in list(nt.nodes): nt.nodes.remove(nd)
    b = nt.nodes.new("ShaderNodeBsdfPrincipled"); o = nt.nodes.new("ShaderNodeOutputMaterial"); nt.links.new(b.outputs[0], o.inputs[0])
    return nt, b


def flat_mat(name, rgba):
    m = bpy.data.materials.new(name); nt, b = bsdf(m)
    b.inputs["Base Color"].default_value = (rgba[0], rgba[1], rgba[2], 1); b.inputs["Roughness"].default_value = 0.6
    return m


def tex_mat(name, path):
    m = bpy.data.materials.new(name); nt, b = bsdf(m)
    b.inputs["Roughness"].default_value = 0.7
    if path and os.path.exists(path):
        tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = bpy.data.images.load(path)
        nt.links.new(tn.outputs[0], b.inputs["Base Color"])
    else: b.inputs["Base Color"].default_value = (0.8, 0.8, 0.8, 1)
    return m


def look(cam, eye, target, up_hint=(0, 0, 1)):
    cam.location = Vector(eye); d = Vector(target) - Vector(eye)
    cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = d.to_track_quat('-Z', 'Y')


try:
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 800, 520
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    wnt = world.node_tree
    bg = wnt.nodes.get("Background") or wnt.nodes.new("ShaderNodeBackground")
    wo = wnt.nodes.get("World Output") or wnt.nodes.new("ShaderNodeOutputWorld"); wnt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.20, 0.22, 0.25, 1); bg.inputs[1].default_value = 0.8
    # scenes from hands_trace
    scenes = {}
    for line in open(os.path.join(DIR, "scenes.txt")):
        t = line.split()
        if not t: continue
        nm = t[0]; f = lambda k, n: np.array(list(map(float, t[t.index(k) + 1:t.index(k) + 1 + n])))
        Ts = [i for i, x in enumerate(t) if x == 'T']
        tg = [dict(on=int(t[i + 1]), pos=np.array(list(map(float, t[i + 2:i + 5]))), palm=np.array(list(map(float, t[i + 5:i + 8])))) for i in Ts]
        scenes[nm] = dict(origin=f('origin', 3), eye=f('eye', 3), hc=f('hc', 1)[0], adj=f('adj', 1)[0], H=f('H', 3), u=f('u', 3), Rh=f('Rh', 9).reshape(3, 3), T=tg)
    names = [n for n in (WANT or scenes.keys()) if n in scenes]
    # the figure
    fg, ftex, _ = read_msh(os.path.join(O, "Meshes", "Tantra", "AstronavigatorSkin.msh"))
    fobjs = []
    for gi, g in enumerate(fg):
        ti = g.get('tex', 0)
        m = tex_mat("f%d" % gi, os.path.join(O, "Textures", ftex[ti - 1]) if ti and ti <= len(ftex) else None)
        ob = mesh_obj("fig%d" % gi, g['V'][:, :3], g['F'], m)
        for p in ob.data.polygons: p.use_smooth = True
        fobjs.append(ob)
    nfig = sum(len(g['V']) for g in fg)
    # the bridge
    vg, _, vmats = read_msh(os.path.join(O, "Meshes", "Tantra", "TantraVC.msh"))
    vobjs = {}
    for gi, g in enumerate(vg):
        V = g['V'][:, :3]
        if len(V) == 0 or len(g['F']) == 0: continue
        lo, hi = V.min(0), V.max(0)
        if hi[0] < -3 or lo[0] > 3 or hi[1] < 0.8 or lo[1] > 3.6 or hi[2] < 76.5 or lo[2] > 83.0: continue
        rgba = vmats[g['mat'] - 1] if g.get('mat', 0) and g['mat'] <= len(vmats) else [0.6, 0.6, 0.6, 1]
        vobjs[gi] = (mesh_obj("vc%d" % gi, V, g['F'], flat_mat("vm%d" % gi, rgba)), V.copy())
    for nm, (pos, e) in {"L1": ((0, 2.9, 79.6), 260), "L2": ((-1.4, 2.6, 80.9), 160), "L3": ((1.4, 2.6, 80.9), 160), "L4": ((0, 2.4, 78.6), 140)}.items():
        l = bpy.data.lights.new(nm, 'POINT'); l.energy = e; l.shadow_soft_size = 0.4
        o = bpy.data.objects.new(nm, l); o.location = Vector(B(np.array(pos))[0]); sc.collection.objects.link(o)
    cd = bpy.data.cameras.new("Cam"); cam = bpy.data.objects.new("Cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    cd.sensor_fit = 'VERTICAL'; cd.clip_start = 0.02
    marks = []
    for nm in names:
        S = scenes[nm]
        raw = np.fromfile(os.path.join(DIR, nm + ".f32"), dtype=np.float32)
        rawfp = np.fromfile(os.path.join(DIR, nm + "_fp.f32"), dtype=np.float32)
        if raw.size != nfig * 3: log.append("%s: %d floats, the mesh has %d vertices" % (nm, raw.size, nfig)); continue
        # the bridge as the ship has it in this scene
        for gi, (ob, V0) in vobjs.items():
            V = V0.copy()
            if gi in SEAT0: V = V + np.array([0, 0 if gi == SEAT_BASE else S['hc'], 1.0 + S['adj']])
            if gi in YOKE_COL: V = yB + (rot_to(yU0, S['u']) @ (V - yB).T).T
            if gi in YOKE_HUB: V = S['H'] + (S['Rh'] @ (V - yH0).T).T
            ob.data.vertices.foreach_set("co", B(V).ravel()); ob.data.update()
        for o in marks: bpy.data.objects.remove(o, do_unlink=True)
        marks = []
        for k, tg in enumerate(S['T']):
            if not tg['on']: continue
            bpy.ops.mesh.primitive_uv_sphere_add(radius=0.006, location=Vector(B(tg['pos'])[0])); mk = bpy.context.active_object
            mk.data.materials.append(flat_mat("mk%d" % k, (1, 0.1, 0.1, 1))); marks.append(mk)
        def put(arr):
            P = arr.reshape(-1, 3).astype(float) + S['origin']; o = 0
            for ob, g in zip(fobjs, fg):
                n = len(g['V']); ob.data.vertices.foreach_set("co", B(P[o:o + n]).ravel()); ob.data.update(); o += n
        hip = S['origin'] + np.array([0, 0.0, 0])
        eye = S['eye']
        views = {
            # through her eyes: ahead and down at the console, and down at the hands
            "fp": (eye, eye + np.array([0, -0.45, 1.0]), 52, True),
            "fpdown": (eye, eye + np.array([0, -1.0, 0.75]), 60, True),
            "fpgame": (eye, eye + np.array([0, -0.62, 1.0]), 70, True),
            # from outside: the front-right three-quarter, her right side, over her left shoulder
            "front": (hip + np.array([0.75, 0.55, 1.05]), hip + np.array([0, 0.25, 0.25]), 45, False),
            "side": (hip + np.array([1.35, 0.35, 0.25]), hip + np.array([0, 0.20, 0.25]), 40, False),
            "back": (hip + np.array([-0.55, 0.95, -0.75]), hip + np.array([0, 0.15, 0.35]), 45, False),
        }
        for k, tg in enumerate(S['T']):   # close to each hand, from in front of it and from its outer side
            if tg['on']: views["hand%d" % k] = (tg['pos'] + np.array([(-1 if k == 0 else 1) * 0.16, 0.12, 0.22]), tg['pos'], 38, False)
        for vn, (e_, tgt, fov, fp) in views.items():
            put(rawfp if fp else raw)
            cd.angle = math.radians(fov)
            look(cam, B(e_)[0], B(tgt)[0])
            sc.render.filepath = os.path.join(DIR, "view_%s_%s.png" % (nm, vn)); bpy.ops.render.render(write_still=True)
        log.append("rendered " + nm)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
