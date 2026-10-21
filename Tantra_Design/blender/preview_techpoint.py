# Preview renders of Meshes\TVehicles\TechPoint.msh (build_techpoint.py) for the user: the Orbiter mesh read as it is, its
# textures (colour only), a regolith ground, a low Sun. blender -b --python preview_techpoint.py -> renders\techpoint_*.png
import bpy, math, os
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(__file__)); ORB = os.path.abspath(os.path.join(HERE, "..", ".."))
MSH = os.path.join(ORB, "Meshes", "TVehicles", "TechPoint.msh"); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(HERE, "preview_techpoint.log"), "w")
try:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    L = open(MSH, encoding="latin-1").read().splitlines()
    i = 0; groups = []; mats = []; texs = []
    while i < len(L):
        s = L[i]
        if s.startswith("LABEL"):
            lab = s.split(None, 1)[1]; mi = int(L[i + 1].split()[1]); ti = int(L[i + 2].split()[1]); nv, nf = map(int, L[i + 3].split()[1:3])
            V = [list(map(float, L[i + 4 + k].split())) for k in range(nv)]
            F = [tuple(map(int, L[i + 4 + nv + k].split())) for k in range(nf)]
            groups.append((lab, mi, ti, V, F)); i += 4 + nv + nf
        elif s.startswith("MATERIALS"):
            n = int(s.split()[1]); i += 1 + n
            for k in range(n):
                col = list(map(float, L[i + 1].split())); em = list(map(float, L[i + 4].split()))
                mats.append((col, em)); i += 5
        elif s.startswith("TEXTURES"):
            n = int(s.split()[1]); texs = [L[i + 1 + k].strip() for k in range(n)]; i += 1 + n
        else: i += 1
    cache = {}
    def material(mi, ti):
        key = (mi, ti)
        if key in cache: return cache[key]
        m = bpy.data.materials.new("m%d_%d" % key); m.use_nodes = True
        bs = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); col, em = mats[mi - 1]
        bs.inputs["Base Color"].default_value = (col[0], col[1], col[2], 1)
        bs.inputs["Roughness"].default_value = 0.6
        if col[3] < 0.99: bs.inputs["Alpha"].default_value = col[3]; m.blend_method = "BLEND" if hasattr(m, "blend_method") else None
        if max(em[:3]) > 0.01:
            bs.inputs["Emission Color"].default_value = (em[0], em[1], em[2], 1); bs.inputs["Emission Strength"].default_value = 3.0
        if ti and ti <= len(texs) and "screen" not in texs[ti - 1]:
            p = os.path.join(ORB, "Textures", texs[ti - 1])
            if os.path.exists(p):
                im = bpy.data.images.load(p); tn = m.node_tree.nodes.new("ShaderNodeTexImage"); tn.image = im
                mix = m.node_tree.nodes.new("ShaderNodeMixRGB"); mix.blend_type = "MULTIPLY"; mix.inputs[0].default_value = 1.0
                mix.inputs[1].default_value = (col[0], col[1], col[2], 1)
                m.node_tree.links.new(tn.outputs[0], mix.inputs[2]); m.node_tree.links.new(mix.outputs[0], bs.inputs["Base Color"])
        if ti and ti <= len(texs) and "screen" in texs[ti - 1]:
            bs.inputs["Base Color"].default_value = (0.02, 0.03, 0.03, 1); bs.inputs["Emission Color"].default_value = (0.1, 0.6, 0.7, 1); bs.inputs["Emission Strength"].default_value = 1.0
        cache[key] = m; return m
    for lab, mi, ti, V, F in groups:
        me = bpy.data.meshes.new(lab)
        verts = [(v[0], v[2], v[1]) for v in V]                      # Orbiter (x, y, z) -> Blender (x, z-up): x, z, y
        faces = [(a, c, b) for a, b, c in F]                          # the handedness flips: so does the winding
        me.from_pydata(verts, [], faces)
        if len(V[0]) >= 8:
            uvl = me.uv_layers.new()
            for poly in me.polygons:
                for li in poly.loop_indices:
                    vi = me.loops[li].vertex_index; uvl.data[li].uv = (V[vi][6], -V[vi][7])
        me.materials.append(material(mi, ti))
        ob = bpy.data.objects.new(lab, me); bpy.context.scene.collection.objects.link(ob)
    # the ground: regolith grey, a large plane at 0
    bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, 0.0))
    g = bpy.context.active_object; gm = bpy.data.materials.new("regolith"); gm.use_nodes = True
    gb = next(n for n in gm.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); gb.inputs["Base Color"].default_value = (0.20, 0.19, 0.18, 1); gb.inputs["Roughness"].default_value = 0.95
    nz = gm.node_tree.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 0.6
    ramp = gm.node_tree.nodes.new("ShaderNodeValToRGB"); ramp.color_ramp.elements[0].color = (0.13, 0.125, 0.12, 1); ramp.color_ramp.elements[1].color = (0.26, 0.25, 0.235, 1)
    gm.node_tree.links.new(nz.outputs[0], ramp.inputs[0]); gm.node_tree.links.new(ramp.outputs[0], gb.inputs["Base Color"])
    g.data.materials.append(gm)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; next(n for n in w.node_tree.nodes if n.type == "BACKGROUND").inputs[0].default_value = (0.004, 0.004, 0.006, 1)
    sun = bpy.data.lights.new("sun", "SUN"); sun.energy = 4.0; so = bpy.data.objects.new("sun", sun); sc.collection.objects.link(so)
    so.rotation_euler = (math.radians(68), 0, math.radians(-35))         # ~22 deg over the horizon from the south-west
    def shot(name, eye, tgt, lens=28):
        cam = bpy.data.cameras.new(name); cam.lens = lens; co = bpy.data.objects.new(name, cam); sc.collection.objects.link(co)
        co.location = Vector(eye); d = Vector(tgt) - Vector(eye); co.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        sc.camera = co; sc.render.filepath = os.path.join(OUT, "techpoint_%s.png" % name); bpy.ops.render.render(write_still=True)
        LOG.write("wrote %s\n" % sc.render.filepath); LOG.flush()
    # Blender coords: (x, y=Orbiter z, z=up)
    only = os.environ.get("TP_SHOTS", "")
    if not only:
        shot("aerial", (-55, -55, 38), (2, 2, 0))
        shot("yard", (-22, -24, 1.7), (2, 0, 3), 24)
        shot("pad", (30, -95, 14), (2, -40, 2), 30)
    if only == "hangar":
        for x, z in [(x, z) for z in (1.0, 9.0) for x in (5.0, 12.0, 19.0)]:
            l = bpy.data.lights.new("w", "SPOT"); l.energy = 3000; l.spot_size = math.radians(120)
            lo = bpy.data.objects.new("w", l); lo.location = (x, z, 6.8); sc.collection.objects.link(lo)
        so.data.energy = 0.3
        shot("hangar2", (1.5, -2.5, 1.8), (14, 12, 4.5), 20)
        raise SystemExit
    shot("window_out", (-15.5, -2.5, 2.0), (-13.8, 4.0, 1.7), 35)
    hl = bpy.data.lights.new("hall", "POINT"); hl.energy = 600; ho = bpy.data.objects.new("hall", hl); ho.location = (-10.0, 8.0, 3.3); sc.collection.objects.link(ho)
    shot("window_in", (-11.5, 7.5, 1.7), (-13.8, 4.2, 1.75), 30)
    if only: raise SystemExit
    # inside the hangar, a lamp on: the work lights as small area lamps (preview only)
    for x, z in [(x, z) for z in (1.0, 9.0) for x in (5.0, 12.0, 19.0)]:
        l = bpy.data.lights.new("w", "SPOT"); l.energy = 2500; l.spot_size = math.radians(120)
        lo = bpy.data.objects.new("w", l); lo.location = (x, z, 7.6); sc.collection.objects.link(lo)
    so.data.energy = 0.0
    shot("hangar", (2.0, 12.5, 2.0), (16, 0, 1.0), 20)
    shot("hall", (-5.0, 5.0, 1.9), (-14, 10.5, 1.2), 18)
except Exception:
    import traceback; LOG.write(traceback.format_exc())
LOG.close()
