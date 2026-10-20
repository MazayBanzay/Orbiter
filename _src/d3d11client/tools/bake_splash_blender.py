# Splash flipbook bake (D3D11 client effects): a real liquid simulation (Blender's Mantaflow FLIP) of liquid methane
# thrown off a wheel's rim in Titan's gravity, rendered from the side as the frames of a flipbook.
# Run through Blender (args via environment, the launcher drops "--" arguments):
#   SPLASH_OUT=<folder for the frames>  SPLASH_KIND=wheel|step  [SPLASH_RES=96]
#   blender-launcher.exe -b --python bake_splash_blender.py
# Output: <out>/n_####.png (RGB the view-space normal 0.5 + 0.5 n, A the coverage), then tools\pack_splash.py packs
# them 8 x 8 into Textures\D3D11Client\splash_<kind>.dds.
import bpy, os, math

OUT = os.environ.get("SPLASH_OUT", os.path.join(os.environ.get("TEMP", "."), "splash"))
KIND = os.environ.get("SPLASH_KIND", "wheel")
RES = int(os.environ.get("SPLASH_RES", "96"))
os.makedirs(OUT, exist_ok=True)
log = open(os.path.join(OUT, "bake.log"), "w")
def L(*a): log.write(" ".join(str(x) for x in a) + "\n"); log.flush()

def main():
    # Titan: g 1.352 m/s2; liquid methane ~450 kg/m3, sigma ~0.017 N/m, kinematic viscosity ~4e-7 m2/s (water 1e-6)
    G = 1.352
    FPS, FRAMES = int(os.environ.get('SPLASH_FPS', '15')), 64     # 64 frames over the whole flight (up and back down)

    L('start', KIND, RES)
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)   # (read_factory_settings ends the script)
    sc = bpy.context.scene
    sc.gravity = (0, 0, -G)
    sc.render.fps = FPS
    sc.frame_start, sc.frame_end = 1, FRAMES

    # the domain: x along the throw (0..2.6 m), y across (-0.6..0.6), z up (0..2.6)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(1.8, 0, 2.1))
    dom = bpy.context.object; dom.name = "Domain"; dom.scale = (6.0, 1.6, 4.2)      # x -1.2..4.8, z 0..4.2: nothing of the fan meets a wall (a clipped bake shows straight edges)
    bpy.ops.object.transform_apply(scale=True)
    L('mod = dom.modifiers.new')
    mod = dom.modifiers.new("Fluid", 'FLUID'); mod.fluid_type = 'DOMAIN'
    ds = mod.domain_settings
    ds.domain_type = 'LIQUID'
    ds.resolution_max = RES
    L('ds.cache_directory')
    ds.cache_directory = os.path.join(OUT, "cache")
    ds.cache_type = 'ALL'
    ds.cache_frame_start, ds.cache_frame_end = 1, FRAMES
    ds.use_mesh = True
    ds.mesh_scale = 2
    ds.use_spray_particles = False
    ds.time_scale = 1.0
    try:
        ds.use_viscosity = False   # methane ~4e-7 m2/s: below what the grid resolves
    except Exception as e: L("viscosity:", e)
    # (Mantaflow's surface tension at any useful value blew the blobs apart - left at 0: at these scales gravity and
    #  inertia rule anyway, Bond number ~ (rho g L^2 / sigma) >> 1 for a 10 cm sheet)
    try: pass
    except Exception as e: L("tension:", e)
    L('ds.use_collision_border_bottom')
    ds.use_collision_border_bottom = True
    ds.use_collision_border_top = False; ds.use_collision_border_front = False; ds.use_collision_border_back = False
    ds.use_collision_border_left = False; ds.use_collision_border_right = False   # the spray leaves the box, not sticks to it
    ds.flip_ratio = 0.97

    # the flow: the stream off the rim for its first frames
    def flow(loc, size, v):
        bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
        fl = bpy.context.object; fl.scale = size
        bpy.ops.object.transform_apply(scale=True)
        fm = fl.modifiers.new("Fluid", 'FLUID'); fm.fluid_type = 'FLOW'
        fs = fm.flow_settings
        fs.flow_type = 'LIQUID'; fs.flow_behavior = 'GEOMETRY'; fs.flow_source = 'MESH'   # a body thrown at frame 1
        fs.use_initial_velocity = True
        fs.velocity_coord = tuple(c * float(os.environ.get('SPLASH_VK', '1')) for c in v)
        fl.hide_render = True
    import random
    random.seed(3)
    if KIND == "wheel":
        # the liquid on a rim of radius 0.4 m turning at 3.2 m/s, shed over its upper rear quarter (where the tread leaves
        # the surface it carries a film; each part leaves along the rim's tangent there): a fan of parcels, each with its
        # own speed and direction - the sheet stretches between them and tears
        R, U = 0.4, float(os.environ.get('SPLASH_U', '2.4'))   # the rim's speed (the client scales it by Froude: L ~ v^2/g, t ~ v/g)
        cx, cz = 0.4, 0.3                                    # the hub (the wheel rolls to -x; its rear is +x)
        for k in range(12):
            a = math.radians(-40 + 60 * k / 11)                # the rear quarter, below to just above the hub
            px, pz = cx + R * math.cos(a), max(cz + R * math.sin(a), 0.06)
            tx, tz = -math.sin(a), math.cos(a)                 # the rim's velocity there: bottom +x, rear up
            for j in range(4):                                 # across the tread: the film flung a little sideways too
                y = -0.11 + 0.22 * (j + 0.5) / 4
                u = U * random.uniform(0.8, 1.12)
                vy = y / 0.11 * random.uniform(0.15, 0.45) + random.uniform(-0.1, 0.1)
                flow((px, y, pz), (0.08, 0.06, 0.07), (tx * u, vy, tz * u))     # a film ~7 cm thick lifted by the chevrons
    elif KIND == "drop":
        flow((1.4, 0, 1.5), (0.2, 0.2, 0.2), (0, 0, 0))
    else:
        # a foot stepping in: a crown pushed up and out round it
        for k in range(10):
            a = 2 * math.pi * k / 10
            flow((1.4 + 0.12 * math.cos(a), 0.12 * math.sin(a), 0.1), (0.05, 0.05, 0.05), (0.7 * math.cos(a), 0.7 * math.sin(a), 1.5 * random.uniform(0.8, 1.1)))
    # bake
    bpy.context.view_layer.objects.active = dom
    dom.select_set(True)
    L("baking", KIND, "res", RES)
    if os.environ.get("SPLASH_NOBAKE"):        # the cache from a previous run (only the camera or the material changed)
        r = "cache reused"
    else:
        with bpy.context.temp_override(active_object=dom, object=dom, selected_objects=[dom]):
            r = bpy.ops.fluid.bake_all()
    L("bake:", r)
    dg = bpy.context.evaluated_depsgraph_get()
    for f in range(1, FRAMES + 1, 3):
        sc.frame_set(f)
        me = dom.evaluated_get(bpy.context.evaluated_depsgraph_get()).to_mesh()
        if len(me.vertices):
            zs = [v.co.z for v in me.vertices]; xs = [v.co.x for v in me.vertices]
            L("frame", f, "verts", len(me.vertices), "x %.2f..%.2f z %.2f..%.2f" % (min(xs), max(xs), min(zs), max(zs)))
        else: L("frame", f, "no mesh")
    if os.environ.get("SPLASH_PROBE"): return

    # the material: the view-space normal as colour (0.5 + 0.5 n), opaque
    mat = bpy.data.materials.new("N"); mat.use_nodes = True
    nt = mat.node_tree; nt.nodes.clear()
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    vt = nt.nodes.new("ShaderNodeVectorTransform"); vt.vector_type = 'NORMAL'; vt.convert_from = 'WORLD'; vt.convert_to = 'CAMERA'
    mul = nt.nodes.new("ShaderNodeVectorMath"); mul.operation = 'MULTIPLY_ADD'
    mul.inputs[1].default_value = (0.5, 0.5, -0.5)   # camera space looks down -z: towards the eye = +0.5 in B
    mul.inputs[2].default_value = (0.5, 0.5, 0.5)
    em = nt.nodes.new("ShaderNodeEmission")
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(geo.outputs["Normal"], vt.inputs[0]); nt.links.new(vt.outputs[0], mul.inputs[0])
    nt.links.new(mul.outputs[0], em.inputs[0]); nt.links.new(em.outputs[0], out.inputs[0])
    dom.data.materials.append(mat)

    # camera: orthographic, from the side (looking along +y), the domain filling the frame
    cam_d = bpy.data.cameras.new("Cam"); cam_d.type = 'ORTHO'; cam_d.ortho_scale = 6.0
    cam = bpy.data.objects.new("Cam", cam_d); sc.collection.objects.link(cam)
    cam.location = (1.8, -8, 3.0); cam.rotation_euler = (math.radians(90), 0, 0)    # the frame: x -1.2..4.8, z 0..6
    sc.camera = cam
    r = sc.render
    r.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    r.resolution_x = r.resolution_y = 256
    r.film_transparent = True
    r.image_settings.file_format = 'PNG'; r.image_settings.color_mode = 'RGBA'; r.image_settings.color_depth = '8'
    sc.view_settings.view_transform = 'Standard'
    r.filepath = os.path.join(OUT, "n_")
    def hide_empty(scene, depsgraph=None):
        try:
            me = dom.evaluated_get(bpy.context.evaluated_depsgraph_get()).data
            dom.hide_render = len(me.vertices) <= 8
        except Exception: pass
    bpy.app.handlers.frame_change_post.append(hide_empty)
    bpy.ops.render.render(animation=True)
    L("rendered", FRAMES, "frames to", OUT)
    log.close()

try:
    main()
except Exception:
    L(traceback.format_exc()); log.close()
