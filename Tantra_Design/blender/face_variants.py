# Tantra: face examples for the user to choose from - MakeHuman face targets on the current body (coverall blend),
# brows and lashes carried along with the skin under them. Renders a front and a three-quarter portrait per variant
# into renders/face_var/. Changes nothing on disk except the renders (the blend is not saved).
import bpy, os, gzip, traceback
from mathutils import Vector, kdtree
HERE = os.path.dirname(os.path.abspath(__file__)); sys_path = __import__("sys").path; sys_path.insert(0, HERE); import render_util
TG = os.path.join(HERE, "..", "bx", "mpfb", "data", "targets")
OUT = os.path.join(HERE, "renders", "face_var"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "face_variants.log"); log = []

LR = lambda t, w: [("cheek/l-" + t, w), ("cheek/r-" + t, w)]
EYE = lambda t, w: [("eyes/l-eye-" + t, w), ("eyes/r-eye-" + t, w)]
# softer: a lighter brow ridge and jaw, finer nose, slimmer neck, fuller upper lip, eyes a little more open
SOFT = [("forehead/forehead-nubian-decr", 0.9), ("chin/chin-bones-decr", 0.9), ("chin/chin-width-decr", 0.5), ("chin/chin-prominent-decr", 0.5),
        ("nose/nose-scale-horiz-decr", 0.5), ("nose/nose-volume-decr", 0.6), ("nose/nose-point-width-decr", 0.6), ("nose/nose-width1-decr", 0.5),
        ("neck/neck-scale-horiz-decr", 0.6), ("neck/measure-neck-circ-decr", 0.5), ("mouth/mouth-upperlip-volume-incr", 0.4),
        ("mouth/mouth-angles-up", 0.35)] + LR("cheek-bones-decr", 0.3) + EYE("height2-incr", 0.6)
VARIANTS = {
    "A_now": [],
    "B_soft": SOFT,
    # expressive: full lips with a clear cupid's bow, a short upturned nose, arched brows, high cheekbones, pointed chin
    "C_expressive": SOFT + [("mouth/mouth-lowerlip-volume-incr", 0.5), ("mouth/mouth-upperlip-volume-incr", 0.3), ("mouth/mouth-cupidsbow-incr", 0.7),
                            ("mouth/mouth-philtrum-volume-decr", 0.4), ("nose/nose-scale-vert-decr", 0.4), ("nose/nose-point-up", 0.5),
                            ("eyebrows/eyebrows-angle-up", 0.6), ("chin/chin-triangle", 0.5), ("head/head-invertedtriangular", 0.4)] + LR("cheek-bones-incr", 0.6),
    # young: rounder soft face, fuller cheeks, short chin, small nose, full lower lip
    "D_young": SOFT + [("head/head-round", 0.4), ("nose/nose-scale-vert-decr", 0.5), ("nose/nose-point-up", 0.3), ("chin/chin-height-decr", 0.5),
                       ("mouth/mouth-lowerlip-volume-incr", 0.5), ("mouth/mouth-scale-horiz-decr", 0.2)] + LR("cheek-volume-incr", 0.5),
}


def load(t):
    d = {}
    for l in gzip.open(os.path.join(TG, t + ".target.gz"), "rt"):
        f = l.split()
        if len(f) >= 4 and not l.startswith("#"): d[int(f[0])] = tuple(float(x) for x in f[1:4])
    return d


try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    body = next(o for o in bpy.data.objects if o.type == 'MESH' and o.name.endswith('.body'))
    k = 0.1 / body.matrix_world.to_scale().x
    base = [v.co.copy() for v in body.data.vertices]
    M = body.matrix_world
    kd = kdtree.KDTree(len(base))
    for i, c in enumerate(base): kd.insert(M @ c, i)
    kd.balance()
    riders = [o for o in bpy.data.objects if o.type == 'MESH' and any(s in o.name.lower() for s in ("eyebrow", "eyelash"))]
    rbase = {o.name: [v.co.copy() for v in o.data.vertices] for o in riders}
    near = {o.name: [kd.find(o.matrix_world @ c)[1] for c in rbase[o.name]] for o in riders}
    cam = render_util.setup_stage()
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 700, 800
    cache = {}
    for name, targets in VARIANTS.items():
        for v, c in zip(body.data.vertices, base): v.co = c.copy()
        for t, w in targets:
            if w == 0: continue
            d = cache.setdefault(t, load(t))
            for i, (dx, dy, dz) in d.items():
                if i < len(base): body.data.vertices[i].co += Vector((dx, -dz, dy)) * (w * k)
        body.data.update()
        for o in riders:   # brows and lashes ride on the skin under them
            Mi = o.matrix_world.inverted_safe().to_3x3()
            for v, c, j in zip(o.data.vertices, rbase[o.name], near[o.name]):
                v.co = c + Mi @ (M.to_3x3() @ (body.data.vertices[j].co - base[j]))
            o.data.update()
        render_util.shoot(cam, OUT, [(name + "_front", (0, -0.95, 1.62), 1.615, 85), (name + "_tq", (0.62, -0.72, 1.63), 1.615, 85)], log)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
