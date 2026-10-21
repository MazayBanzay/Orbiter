# The stand for the view from inside the helmet (OrbiterCrew HelmetView, the D3D11 client): the GAME's mesh
# (Meshes\Tantra\AstronavigatorSuit2.msh, rest pose) seen as the game draws it - back faces culled, the parts the game
# hides in that view hidden (the visor's glass, the shade, the head) - from the eye point, with the game's field of view,
# along the look directions. Renders renders\hv_<name>.png and a sheet hv_sheet.png (system Python makes the sheet).
#   blender -b --factory-startup --python helmet_view_stand.py      (args through the env: HV_EYE "x y z" game frame,
#   HV_FOV deg vertical, HV_VIEWS "name:yaw:pitch,...", HV_HIDE extra label prefixes, HV_MESH the .msh)
import bpy, bmesh, math, os
from mathutils import Vector, Euler
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "renders")
MSH = os.environ.get("HV_MESH", os.path.join(ROOT, "Meshes", "Tantra", "AstronavigatorSuit2.msh"))
EYE = [float(v) for v in os.environ.get("HV_EYE", "0 0.700 0.110").split()]
FOV = float(os.environ.get("HV_FOV", "90"))
VIEWS = [(v.split(":")[0], float(v.split(":")[1]), float(v.split(":")[2])) for v in os.environ.get("HV_VIEWS",
         "ahead:0:0,left:-70:0,right:70:0,up:0:30,downleft:-45:-35,downright:45:-35,down:0:-40").split(",")]
HIDE = ["Visor", "SunShade", "ShadeArms", "Skin", "Brows", "Lashes", "Eyes", "Hair"] + [h for h in os.environ.get("HV_HIDE", "").split(",") if h]
SHOW = [h for h in os.environ.get("HV_SHOW", "").split(",") if h]   # labels shown though a HIDE prefix matches
LOG = open(os.path.join(OUT, "hv_log.txt"), "w", encoding="utf-8")

for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
sc = bpy.context.scene

def g2b(x, y, z): return Vector((-x, -z, y + 0.93))     # the game's frame (LH, y up, z forward) -> Blender's

L = [l.rstrip("\r") for l in open(MSH, encoding="latin-1").read().split("\n")]
mi = next(i for i, l in enumerate(L) if l.startswith("MATERIALS")); nm = int(L[mi].split()[1]); names = L[mi + 1:mi + 1 + nm]
mats = {}
for k, n in enumerate(names):
    j = L.index("MATERIAL " + n); d = [float(t) for t in L[j + 1].split()[:4]]
    m = bpy.data.materials.new(n); m.diffuse_color = (d[0], d[1], d[2], 1.0); m.use_backface_culling = True
    mats[k + 1] = m
i = 2; lab = ""; mat = 0; shown = hidden = 0
while not L[i].startswith("MATERIALS"):
    l = L[i]
    if l.startswith("LABEL"): lab = l[6:]; mat = 0
    elif l.startswith("MATERIAL "): mat = int(l.split()[1])
    elif l.startswith("GEOM"):
        nv, nt = map(int, l.split()[1:3])
        if any(lab.startswith(h) for h in HIDE) and lab not in SHOW: hidden += 1
        else:
            bm = bmesh.new()
            vs = [bm.verts.new(g2b(*map(float, L[i + 1 + k].split()[:3]))) for k in range(nv)]
            for t in range(nt):
                a, b, c = map(int, L[i + 1 + nv + t].split()[:3])
                try: bm.faces.new((vs[a], vs[c], vs[b]))        # the export reversed the order: back again
                except ValueError: pass
            me = bpy.data.meshes.new(lab); bm.to_mesh(me); bm.free()
            if mat in mats: me.materials.append(mats[mat])
            o = bpy.data.objects.new(lab, me); sc.collection.objects.link(o); shown += 1
        i += nv + nt
    i += 1
LOG.write("groups shown %d, hidden %d\n" % (shown, hidden))

sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'; sh.color_type = 'MATERIAL'; sh.show_backface_culling = True; sh.show_cavity = True
sh.cavity_type = 'WORLD'; sh.background_type = 'VIEWPORT'; sh.background_color = (0.55, 0.42, 0.25)   # Titan's haze beyond
sc.render.resolution_x = 960; sc.render.resolution_y = 540
cd = bpy.data.cameras.new("eye"); cd.sensor_fit = 'VERTICAL'; cd.angle_y = math.radians(FOV); cd.clip_start = 0.015
cam = bpy.data.objects.new("eye", cd); sc.collection.objects.link(cam); sc.camera = cam
cam.location = g2b(*EYE)
for name, yaw, pitch in VIEWS:
    # a camera looking along -Z: turned up to the horizon (90 about X), then yaw about Z (left positive), pitch
    cam.rotation_euler = Euler((math.radians(90 + pitch), 0, math.radians(180 - yaw)), 'XYZ')   # (yaw right positive)
    sc.render.filepath = os.path.join(OUT, "hv_%s.png" % name)
    bpy.ops.render.render(write_still=True); LOG.write("view %s yaw %.0f pitch %.0f\n" % (name, yaw, pitch)); LOG.flush()
LOG.write("done\n"); LOG.close()
