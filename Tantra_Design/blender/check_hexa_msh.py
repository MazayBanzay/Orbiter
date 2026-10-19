# Check the game mesh of the «Гекса» jet pack on her figure: reads Meshes\Tantra\JetPackHexaWorn.msh (her model frame) and
# the folded JetPackHexa.msh as written, maps them to the Blender frame (x, -(z - 0.0014), y + 0.93) and renders.
#   blender --background astronavigator_suit.blend --python check_hexa_msh.py -> renders\hexa_<worn|drop>_<view>.png
import bpy, bmesh, math, os
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders"); ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LOG = open(os.path.join(OUT, "hexa_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
if os.environ.get("HX_BLOCK"):          # the suit's back block under the pack (the Монолит sits on it)
    G = {"__name__": "x", "SB_BUILD_ONLY": True, "SB_SUBSURF": 1}
    exec(open(os.path.join(HERE, "suit_back_block.py"), encoding="utf-8").read(), G)
    for o in G["inside"]: bpy.data.objects.remove(o, do_unlink=True)
for o in bpy.data.objects:
    if o.name.startswith("Jet"): o.hide_render = True
COL = [(0.88, 0.89, 0.90), (0.05, 0.055, 0.06), (0.55, 0.56, 0.58), (0.78, 0.09, 0.07), (0.28, 0.29, 0.31)] + [(1.0, 0.55, 0.15)] * 4
mats = []
for i, c in enumerate(COL):
    m = bpy.data.materials.new("HX%d" % i); m.use_nodes = True; b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*c, 1); b.inputs["Roughness"].default_value = 0.4
    if i >= 5:
        try: b.inputs["Emission Color"].default_value = (1.0, 0.45, 0.1, 1); b.inputs["Emission Strength"].default_value = 3.0
        except Exception: pass
    mats.append(m)
made = []
def load(path, offset=Vector((0, 0, 0))):
    L = open(path).read().split("\n"); i = 0
    mirror = False
    while i < len(L):
        if L[i].startswith("LABEL"): mirror = L[i].split()[1].startswith("Mono")   # written with the true mirror (x her right)
        if L[i].startswith("MATERIAL ") and i + 1 < len(L) and L[i + 1].startswith("TEXTURE"):
            mi = int(L[i].split()[1]) - 1
        if L[i].startswith("GEOM"):
            nv, nf = map(int, L[i].split()[1:3]); bm = bmesh.new(); vs = []
            for k in range(nv):
                x, y, z = map(float, L[i + 1 + k].split()[:3]); x, y, z = x + offset.x, y + offset.y, z + offset.z
                vs.append(bm.verts.new((-x if mirror else x, -(z - 0.0014), y + 0.93)))
            for k in range(nf):
                a, b_, c = map(int, L[i + 1 + nv + k].split())
                try: bm.faces.new((vs[a], vs[b_], vs[c]))
                except ValueError: pass
            me = bpy.data.meshes.new("hx"); bm.to_mesh(me); bm.free(); me.materials.append(mats[mi])
            o = bpy.data.objects.new("hx", me); sc.collection.objects.link(o); made.append(o)
            i += 1 + nv + nf; continue
        i += 1
try:
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 600, 600; sc.render.film_transparent = True
    for nm, en, rot in (("HX_key", 3.0, (42, 0, 145)), ("HX_fill", 1.4, (65, 0, -40)), ("HX_low", 0.9, (120, 0, 160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("HX_cam"); cam = bpy.data.objects.new("HX_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    V = {"rear": (Vector((1.5, 2.2, 1.6)), Vector((0, 0.25, 1.30))), "side": (Vector((2.6, 0.25, 1.3)), Vector((0, 0.25, 1.30))), "under": (Vector((0.9, 0.5, 1.05)), Vector((0.6, 0.18, 1.40)))}
    for tag, path, off in (("worn", "Meshes/Tantra/JetPackHexaWorn.msh", Vector((0, 0, 0))), ("drop", "Meshes/Tantra/JetPackHexa.msh", Vector((0.0, 0.37, -0.2486)))):
        load(os.path.join(ROOT, path), off)
        for vn, (loc, tgt) in V.items():
            cd.lens = 45 if vn != "under" else 70; cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "hexa_%s_%s.png" % (tag, vn)); bpy.ops.render.render(write_still=True); print("rendered", tag, vn, file=LOG); LOG.flush()
        for o in made: bpy.data.objects.remove(o, do_unlink=True)
        made.clear()
    print("DONE", file=LOG)
except Exception:
    import traceback; traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
