# The jet pack M1 «Гекса» on the figure in Blender, from its game mesh (Meshes\Tantra\JetPackHexaWorn.msh, written by
# tools/build_jetpack_hexa.py) - the same geometry the game draws. No render, no save: for other scripts on
# astronavigator_suit.blend:
#     exec(open(r"...\jetpack_hexa_build.py", encoding="utf-8").read(), globals())
#     objs = build_hexa(folded=False)          # deployed; folded=True: the telescope in, swung back 90 deg, lifted 9 cm
#     ...                                       # remove_hexa(objs) to take it away again
# It hides the figure's own jet pack (every object named Jet*); the suit's Pack / BackRails stay as they are.
# Blender frame (x right, y back, z up). The body seen from behind: a hexagon, its vertices top z 1.530 and bottom 1.010,
# x +-0.225 at its widest (z 1.14..1.40); the outer hexagon from her back y 0.108 to 0.238, the deep core (hexagon R 0.15,
# z 1.12..1.42) to y 0.348. Hinge pins x +-0.204, y 0.186, up to z 1.61. Deployed the capsules sit at x +-0.597, z 1.35..1.59;
# folded they stand behind the body's upper corners (x +-0.26, y up to ~0.41, z up to ~1.68).
import bpy, bmesh, math, os
from mathutils import Vector

_ROOT = r"C:\Games\Orbiter-2024"
_COL = {"JH_White": (0.88, 0.89, 0.90), "JH_Dark": (0.05, 0.055, 0.06), "JH_Metal": (0.55, 0.56, 0.58), "JH_Red": (0.78, 0.09, 0.07),
        "JH_Grey": (0.28, 0.29, 0.31), "JH_GlowR": (0.17, 0.17, 0.18), "JH_GlowL": (0.17, 0.17, 0.18)}

def _mat(name):
    m = bpy.data.materials.get("HX_" + name)
    if m: return m
    m = bpy.data.materials.new("HX_" + name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*_COL.get(name, (0.8, 0.8, 0.8)), 1); b.inputs["Roughness"].default_value = 0.4
    b.inputs["Metallic"].default_value = 0.8 if name == "JH_Metal" else 0.1
    return m

def _read(path):
    L = open(path).read().split("\n"); i = 0; groups = []; mi = 0; label = ""
    names = []
    while i < len(L):
        s = L[i]
        if s.startswith("LABEL"): label = s.split()[1]
        elif s.startswith("MATERIAL ") and len(s.split()) == 2: mi = int(s.split()[1]) - 1
        elif s.startswith("GEOM"):
            nv, nf = map(int, s.split()[1:3])
            vs = [tuple(map(float, L[i + 1 + k].split()[:3])) for k in range(nv)]
            fs = [tuple(map(int, L[i + 1 + nv + k].split())) for k in range(nf)]
            groups.append((label, mi, vs, fs)); i += 1 + nv + nf; continue
        elif s.startswith("MATERIALS"):
            n = int(s.split()[1]); names = [L[i + 1 + k].strip() for k in range(n)]; break
        i += 1
    return groups, names

def _roles(path):
    r = {}; geo = {}
    for s in open(path, encoding="utf-8", errors="ignore"):
        p = s.split()
        if not p: continue
        if p[0] == "GROUP": r[int(p[1])] = (p[2], int(p[3]))
        elif p[0] in ("STROKE", "LIFT", "SWING"): geo[p[0]] = float(p[1])
        elif p[0] == "HINGE": geo.setdefault("HINGE", []).append(tuple(map(float, p[1:4])))
    return r, geo

def build_hexa(folded=False, hide_figure_pack=True):
    if hide_figure_pack:
        for o in bpy.data.objects:
            if o.name.startswith("Jet"): o.hide_render = True; o.hide_viewport = True
    groups, mnames = _read(os.path.join(_ROOT, "Meshes", "Tantra", "JetPackHexaWorn.msh"))
    roles, geo = _roles(os.path.join(_ROOT, "Config", "Tantra", "JetPackHexa.geo"))
    hinge = {1: Vector(geo["HINGE"][0]), -1: Vector(geo["HINGE"][1])}
    stroke, lift, swing = geo.get("STROKE", 0.202), geo.get("LIFT", 0.09), math.radians(geo.get("SWING", 90))
    lift = globals().get("HEXA_LIFT_OVERRIDE", lift)          # a caller may try the fold without the screw's lift
    objs = []
    for gi, (label, mi, vs, fs) in enumerate(groups):
        role, side = roles.get(gi, ("core", -1))
        pts = []
        for x, y, z in vs:
            p = Vector((x, y, z))
            if folded and side >= 0:
                sg = 1 if side == 0 else -1
                k = 0.5 if role == "tele1" else 1.0 if role in ("tele2", "ring") else 0.0
                p = p + Vector((-sg * stroke * k, 0, 0))
                a = sg * swing; c, s = math.cos(a), math.sin(a); d = p - hinge[sg]
                p = hinge[sg] + Vector((c * d.x + s * d.z, d.y, -s * d.x + c * d.z)) + Vector((0, lift, 0))
            pts.append((p.x, -(p.z - 0.0014), p.y + 0.93))            # model frame -> Blender
        bm = bmesh.new(); bv = [bm.verts.new(q) for q in pts]
        for a, b_, c_ in fs:
            try: bm.faces.new((bv[a], bv[b_], bv[c_]))
            except ValueError: pass
        me = bpy.data.meshes.new("Hexa_" + label); bm.to_mesh(me); bm.free()
        me.materials.append(_mat(mnames[mi] if mi < len(mnames) else "JH_White"))
        o = bpy.data.objects.new("Hexa_" + label, me); bpy.context.scene.collection.objects.link(o); objs.append(o)
    return objs

def remove_hexa(objs):
    for o in objs: bpy.data.objects.remove(o, do_unlink=True)
