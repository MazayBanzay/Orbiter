# «Панцирь»: what the person sees from inside the helmet, the head turning inside the fixed helmet (rest pose, game asset)
#   A  direct view through the photonic window (louvres open): eye 18 mm behind the glass, the head turns about the neck,
#      the frame, the side modules and the inner shell come into view; the glass's amber tint
#   B  synthetic vision (louvres closed): the image of the frame's cameras (stereo pair in the top bar, wide cameras in the
#      side modules) on the helmet display, a crop that follows the head - from the camera nearest the gaze
# -> mock_view_A_<yaw>.png, mock_view_B_<yaw>.png   (Titan-like stage: haze sky, dark ground, rocks, a rover-size box)
import bpy, os, sys, math
from mathutils import Vector as V, Matrix, Euler
HERE = os.path.dirname(os.path.abspath(__file__))
import traceback
try:
    sc = bpy.context.scene; sc.render.engine = 'BLENDER_EEVEE_NEXT'
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'REST'
    for o in bpy.data.objects:   # her own head: the camera is in it
        if o.type == 'MESH' and any(k in o.name for k in ("female1605", "eyebrow", "eyelash", "low-poly", "toigo")): o.hide_render = True
    # the stage
    if sc.world is None: sc.world = bpy.data.worlds.new('W')
    sc.world.use_nodes = True
    w = sc.world.node_tree; bg = next(n for n in w.nodes if n.type == 'BACKGROUND'); bg.inputs[0].default_value = (0.62, 0.40, 0.16, 1); bg.inputs[1].default_value = 1.2
    sun = bpy.data.lights.new("S", 'SUN'); sun.energy = 2.0; sun.color = (1.0, 0.78, 0.5); so = bpy.data.objects.new("S", sun); sc.collection.objects.link(so); so.rotation_euler = (math.radians(55), 0, math.radians(-30))
    def mat(name, c, r=0.8):
        m = bpy.data.materials.new(name); m.use_nodes = True; p = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); p.inputs["Base Color"].default_value = (*c, 1); p.inputs["Roughness"].default_value = r; return m
    bpy.ops.mesh.primitive_plane_add(size=400, location=(0, 0, 0)); g = bpy.context.object; g.data.materials.append(mat("gnd", (0.16, 0.09, 0.05)))
    import random; random.seed(3)
    for k in range(60):
        a = random.uniform(-math.pi, math.pi); d = random.uniform(3, 40)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=random.uniform(0.08, 0.4), location=(d * math.sin(a), -d * math.cos(a), 0.05)); bpy.context.object.data.materials.append(mat("rock%d" % k, (0.22, 0.14, 0.08)))
    bpy.ops.mesh.primitive_cube_add(size=1, location=(-5, -9, 1.0)); r = bpy.context.object; r.scale = (2.2, 4.5, 2.0); r.data.materials.append(mat("rover", (0.55, 0.55, 0.52), 0.5))
    bpy.ops.mesh.primitive_cube_add(size=1, location=(6, -6, 0.9)); r = bpy.context.object; r.scale = (0.5, 0.5, 1.8); r.data.materials.append(mat("mast", (0.8, 0.3, 0.1), 0.5))
    fog = bpy.data.materials.new("fog"); fog.use_nodes = True; nt = fog.node_tree
    for n in list(nt.nodes):
        if n.type == 'BSDF_PRINCIPLED': nt.nodes.remove(n)
    mo = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
    vs = nt.nodes.new("ShaderNodeVolumeScatter"); vs.inputs["Color"].default_value = (0.9, 0.6, 0.3, 1); vs.inputs["Density"].default_value = 0.012
    nt.links.new(vs.outputs[0], mo.inputs["Volume"])
    bpy.ops.mesh.primitive_cube_add(size=300, location=(0, 0, 50)); fb = bpy.context.object; fb.data.materials.append(fog)
    # the eye and the neck (Orbiter frame EyePos 0 0.69 0.17 -> Blender (0, -0.17, 1.62)); the head turns about the top of the neck
    # the window from inside: the amber photonic pane passes ~55 % (tinted), mirrors a little
    gm = bpy.data.materials.new("glass_in"); gm.use_nodes = True; N = gm.node_tree.nodes; Lk = gm.node_tree.links
    for n in list(N):
        if n.type != 'OUTPUT_MATERIAL': N.remove(n)
    out_ = next(n for n in N if n.type == 'OUTPUT_MATERIAL'); tr = N.new("ShaderNodeBsdfTransparent"); tr.inputs[0].default_value = (0.95, 0.62, 0.30, 1)
    gl = N.new("ShaderNodeBsdfGlossy"); gl.inputs["Roughness"].default_value = 0.02; mx = N.new("ShaderNodeMixShader"); mx.inputs[0].default_value = 0.08
    Lk.new(tr.outputs[0], mx.inputs[1]); Lk.new(gl.outputs[0], mx.inputs[2]); Lk.new(mx.outputs[0], out_.inputs["Surface"])
    try: gm.surface_render_method = 'BLENDED'
    except Exception: gm.blend_method = 'BLEND'
    for o in bpy.data.objects:
        if o.type == 'MESH' and o.name.startswith("HelmCapGd"):
            for i_ in range(len(o.data.materials)): o.data.materials[i_] = gm
    # the window's frame sheets triangulate across the opening (removed from the game mesh by fix_visor_msh.py): the same here
    import bmesh
    for o in bpy.data.objects:
        if o.type == 'MESH' and o.name.startswith("HelmCap") and not o.name.startswith(("HelmCapGd", "HelmCapSl")):
            bm = bmesh.new(); bm.from_mesh(o.data); M_ = o.matrix_world
            kill = [f for f in bm.faces if (lambda c: abs(c.x) < 0.083 and 1.604 < c.z < 1.666 and c.y < -0.141)(M_ @ f.calc_center_median())]
            bmesh.ops.delete(bm, geom=kill, context='FACES'); bm.to_mesh(o.data); bm.free()
    EYE = V((0.0, -0.118, 1.638)); NECK = V((0.0, -0.010, 1.560))   # her eyes (the head mesh: nose tip at y -0.167), the turn about the neck's top
    cam_d = bpy.data.cameras.new("C"); cam_d.clip_start = 0.004; cam = bpy.data.objects.new("C", cam_d); sc.collection.objects.link(cam); sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    def look(pos, yaw, pitch, fov):
        cam.location = pos; cam_d.angle = math.radians(fov)
        cam.rotation_euler = Euler((math.radians(90 + pitch), 0, math.radians(yaw)), 'XYZ')   # Blender: -Y forward = (90,0,0) looks along -Y? see below
        q = (Matrix.Rotation(math.radians(yaw), 3, 'Z') @ Matrix.Rotation(math.radians(-pitch), 3, 'X') @ V((0, -1, 0))).to_track_quat('-Z', 'Y')
        cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = q
    def shot(name):
        sc.render.filepath = os.path.join(HERE, name + ".png"); bpy.ops.render.render(write_still=True)
    VIEWS = [(0, 0), (40, 0), (70, 0), (0, -25)]
    # A: the eye on the turned head, looking with it (an 85 deg field, the eye's useful one)
    for yaw, pitch in VIEWS:
        R = Matrix.Rotation(math.radians(yaw), 3, 'Z') @ Matrix.Rotation(math.radians(-pitch), 3, 'X')
        look(NECK + R @ (EYE - NECK), yaw, pitch, 85); shot("mock_view_A_y%d_p%d" % (yaw, pitch))
    # B: the louvres closed; the display's crop from the camera nearest the gaze: the stereo pair (top bar, centre) to +-35 deg,
    # the wide camera of that side's module beyond (they see +-110 deg); the helmet itself not in its image
    for o in bpy.data.objects:
        if o.type == 'MESH' and o.name.startswith("HelmCap"): o.hide_render = True
    STEREO = V((0.0, -0.205, 1.666)); SIDE = {1: V((-0.128, -0.150, 1.640)), -1: V((0.128, -0.150, 1.640))}
    for yaw, pitch in VIEWS:
        pos = STEREO if abs(yaw) <= 35 else SIDE[1 if yaw > 0 else -1]
        look(pos, yaw, pitch, 85); shot("mock_view_B_y%d_p%d" % (yaw, pitch))
    
except Exception:
    open(os.path.join(HERE, 'mock_helmview_err.log'), 'w').write(traceback.format_exc())
bpy.ops.wm.quit_blender()