# THE VISOR GLASS of the chosen helmet (glass b), fitted and in five materials (the user, 2026-10-18: «стекло шлема не
# подогнано; лицо пожелтело и плохо видно; там будет сапфировое стекло? ... может иметь бирюзовый лёгкий или сапфировый
# оттенок»). The fit: the glass was the window's exact size 4 mm OUTSIDE the shell - the cut's jagged edge and the chin
# showed gaps; now it runs ~10 mm under the shell's edge all round, 2 mm inside its surface, the grey rim over the seam.
# The colour: the old glass was amber (0.55, 0.42, 0.25) at 30 % opacity - it tinted her face yellow and dark; the
# visor is a laminate (sapphire or ALON outer 3-4 mm, a clear polymer pressure layer), nearly colourless; the gold
# belongs to the sun shade only.  Render only.
#   blender -b astronavigator_suit.blend --python helmet_glass_variants.py   (env HG_OUT)
import bpy, bmesh, math, os, traceback
from mathutils import Vector
from mathutils.bvhtree import BVHTree
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.environ.get("HG_OUT") or os.path.join(HERE, "..", "suit_mockups", "visor_glass"); os.makedirs(OUT, exist_ok=True)
LOGF = open(os.path.join(OUT, "hg_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
try:
    src = open(os.path.join(HERE, "helmet_cheek.py"), encoding="utf-8").read()
    src = src[:src.index("REST = {n:")].replace('LOG = open(os.path.join(OUT, "hk_log.txt"), "w", encoding="utf-8")', "LOG = LOGF")
    HK = {"LOGF": LOGF, "__name__": "hk"}; exec(compile(src, "helmet_cheek.py", "exec"), HK)
    W, S, N, th_of, FRONT = HK["W"], HK["S"], HK["N"], HK["th_of"], HK["FRONT"]
    def cut_fine(w):
        """the window cut clean: the shell's faces near its edge split fine first (3 passes), then the faces whose
        centres fall inside the window go - the edge lies within ~5 mm of the window line, under the rim"""
        for n in HK["CUT"]:
            o = bpy.data.objects[n]; me = o.data; bm = bmesh.new(); bm.from_mesh(me); M = o.matrix_world
            for _ in range(3):
                near = [e for e in bm.edges if any(w.inside(M @ v.co, 0.03) for v in e.verts) and not all(w.inside(M @ v.co, -0.012) for v in e.verts)]
                if not near: break
                bmesh.ops.subdivide_edges(bm, edges=near, cuts=1, use_grid_fill=True)
            dead = [f for f in bm.faces if w.inside(M @ f.calc_center_median(), 0.0)]
            bmesh.ops.delete(bm, geom=dead, context='FACES'); bm.to_mesh(me); bm.free(); me.update()
    bpy.data.objects["Astronavigator"].data.pose_position = 'REST'; bpy.context.view_layer.update()
    SHELLB = BVHTree.FromObject(bpy.data.objects["Helmet"], bpy.context.evaluated_depsgraph_get())   # the shell before the cut
    cut_fine(W); HK["rim"](W); HK["hexport"](1); HK["hexport"](-1); HK["shield"]()
    for n in ("HelmPods", "Cuirass", "MarkChest", "PlateLugs", "ShoulderArcs", "Pack", "MarkPack", "SunShade", "ShadeArms", "Visor"):
        if n in bpy.data.objects: bpy.data.objects[n].hide_render = True
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True

    def glass_mat(name, col, alpha, rough=0.02, spec=0.35):
        m = bpy.data.materials.new(name); m.use_nodes = True
        b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = 0.0; b.inputs["Roughness"].default_value = rough
        b.inputs["Alpha"].default_value = alpha
        for k, v in (("Specular IOR Level", spec), ("IOR", 1.77)):
            if k in b.inputs: b.inputs[k].default_value = v
        try: m.surface_render_method = 'BLENDED'
        except Exception: m.blend_method = 'BLEND'
        m.diffuse_color = (*col, alpha); return m
    def fitted_glass(name, mat, margin_deg=4.0, margin_z=0.010, off=-0.002, out=0.0, margin_bot=0.030):
        """the window's glass, grown under the shell's edge, a hair inside its surface (out > 0: a shade outside)"""
        bm = bmesh.new(); rows = []; n, m = 48, 16; h = W.h + math.radians(margin_deg)
        for j in range(m + 1):
            row = []
            for i in range(n + 1):
                u = -1 + 2 * i / n; ph = FRONT + u * h; uw = max(-1, min(1, u * h / W.h))
                z0 = W.z0(uw) - margin_bot; z1 = W.top + margin_z
                z = z0 + (z1 - z0) * j / m; th = th_of(z)
                p = S(th, ph); n0 = N(th, ph)                                  # on the real shell's outer face (it narrows to the neck)
                loc, nrm, _, _ = SHELLB.ray_cast(p + n0 * 0.08, -n0, 0.16)
                if loc is None: loc, nrm = p, n0
                nrm = nrm if nrm.dot(p - HK["C"]) > 0 else -nrm
                row.append(bm.verts.new(loc + nrm * (off + out)))
            rows.append(row)
        for a, b in zip(rows, rows[1:]):
            for i in range(n): bm.faces.new((a[i], a[i + 1], b[i + 1], b[i]))
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(mat)
        for p in me.polygons: p.use_smooth = True
        o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); return o
    GOLD = bpy.data.materials.new("HG_gold"); GOLD.use_nodes = True
    gb = next(n for n in GOLD.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    gb.inputs["Base Color"].default_value = (0.95, 0.72, 0.30, 1); gb.inputs["Metallic"].default_value = 1.0; gb.inputs["Roughness"].default_value = 0.12
    gb.inputs["Alpha"].default_value = 0.55
    try: GOLD.surface_render_method = 'BLENDED'
    except Exception: GOLD.blend_method = 'BLEND'

    VARIANTS = [
        ("1", "сапфир, просветлённый", glass_mat("HG_sapph_clear", (0.90, 0.94, 0.98), 0.10), False),
        ("2", "сапфир, лёгкий синий", glass_mat("HG_sapph_tint", (0.66, 0.80, 0.98), 0.17), False),
        ("3", "лёгкая бирюза", glass_mat("HG_turq", (0.62, 0.92, 0.88), 0.17), False),
        ("4", "оксинитрид алюминия, нейтральный", glass_mat("HG_alon", (0.95, 0.95, 0.93), 0.10), False),
        ("5", "сапфир + золотой щиток", glass_mat("HG_sapph_tint5", (0.66, 0.80, 0.98), 0.17), True),
    ]
    try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception: sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 560, 560; sc.render.film_transparent = False
    try: sc.eevee.use_raytracing = True
    except Exception: pass
    if sc.world is None: sc.world = bpy.data.worlds.new("HG_world")
    sc.world.use_nodes = True
    bg = next((n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND'), None)
    if bg: bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1); bg.inputs[1].default_value = 1.0
    for nm, en, rot in (("HG_key", 3.0, (42, 0, 145)), ("HG_fill", 1.4, (65, 0, -40)), ("HG_front", 1.8, (60, 0, 15)), ("HG_rim", 1.0, (110, 0, -160))):
        sun = bpy.data.lights.new(nm, 'SUN'); sun.energy = en; sun.angle = 0.4; so = bpy.data.objects.new(nm, sun); sc.collection.objects.link(so)
        so.rotation_euler = tuple(math.radians(a) for a in rot)
    cd = bpy.data.cameras.new("HG_cam"); cd.lens = 70; cam = bpy.data.objects.new("HG_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0.0, -0.04, 1.62))
    VIEWS = {"front": T + Vector((0.0, -0.80, 0.05)), "q34": T + Vector((-0.50, -0.62, 0.10)), "side": T + Vector((0.80, -0.06, 0.03))}
    for key, title, mat, shade in VARIANTS:
        objs = [fitted_glass("visor%s" % key, mat)]
        if shade:                                           # the gold sun shade half down, over the glass
            o = fitted_glass("shade", GOLD, margin_deg=2.0, margin_z=0.004, off=0.0, out=0.014)
            me = o.data; bm = bmesh.new(); bm.from_mesh(me)
            zcut = W.top - 0.55 * (W.top - W.bot)
            bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.calc_center_median().z < zcut], context='FACES'); bm.to_mesh(me); bm.free()
            objs.append(o)
        for vn, loc in VIEWS.items():
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "hg_%s_%s.png" % (key, vn)); bpy.ops.render.render(write_still=True)
        for o in objs: bpy.data.objects.remove(o, do_unlink=True)
        print("rendered", key, title, file=LOGF); LOGF.flush()
    open(os.path.join(OUT, "hg_notes.txt"), "w", encoding="utf-8").write("\n".join("%s|%s" % (k, t) for k, t, m, s in VARIANTS))
    print("DONE", file=LOGF)
except Exception:
    traceback.print_exc(file=LOGF); print("FAILED", file=LOGF)
LOGF.close()
