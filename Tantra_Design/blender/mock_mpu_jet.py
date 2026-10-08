# МПУ for low gravity - a look only, nothing for the game.
# blender --background astronavigator_coverall.blend --python mock_mpu_jet.py
# Thrust the way the Tantra does it, scaled down: no bells. Each end of the spine carries a flush pod fairing with two
# concave magnetic reflector cups opening down (plasma confined and pushed out by the field; the vector is steered by the
# field, +-10 deg, and by the pod swinging out/down on its arm, as the ship's flank pods). The spine's flanks carry two
# small flush cups a side, opening outwards (side thrust, yaw). Propellant: metallic hydrogen in the spine.
# The cabin: low, a wedge nose with wrap-round glazing, chamfered roof, tapering tail; octagonal hatch.
# Renders: side (her for scale), three-quarter, a hop (pods swung down, cups lit) on regolith.
import bpy, bmesh, math, os, sys, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
src = open(os.path.join(HERE, "mock_mpu.py"), encoding="utf-8").read()
exec(src[src.index("import bpy"):src.index("try:\n    for o in bpy.data.objects")])
LOG = os.path.join(HERE, "mock_mpu_jet.log"); log = []
M_PLUME, M_GLOW, M_COIL = 9, 10, 11

def emis(name, col, strength, alpha=0.55):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    e = nt.nodes.new("ShaderNodeEmission"); e.inputs[0].default_value = (*col, 1); e.inputs[1].default_value = strength
    tr = nt.nodes.new("ShaderNodeBsdfTransparent"); mix = nt.nodes.new("ShaderNodeMixShader"); mix.inputs[0].default_value = alpha
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(tr.outputs[0], mix.inputs[1]); nt.links.new(e.outputs[0], mix.inputs[2]); nt.links.new(mix.outputs[0], out.inputs[0])
    try: m.blend_method = 'BLEND'
    except Exception: pass
    return m

def lathe(bm, c, ax, prof, mi, seg=36):
    """surface of revolution about ax through c: prof = [(along, radius)]"""
    c, ax = Vector(c), Vector(ax).normalized()
    loft(bm, [ring(c + ax * h, ax, max(r, 1e-3), seg) for h, r in prof], mi, cap=False)

def mcup(bm, c, ax, R, depth, lit=0.0, plume=0.0):
    """a magnetic reflector cup opening along ax: parabolic dish, the coil band round its rim, the focus glow"""
    c, ax = Vector(c), Vector(ax).normalized()
    prof = [(depth * (1 - (t * t)) * -1, R * t) for t in [0.05 + 0.95 * k / 9 for k in range(10)]]   # dish: back at -depth
    lathe(bm, c, ax, prof, M_METAL)
    lathe(bm, c, ax, [(-0.02, R), (0.03, R), (0.03, R + 0.07), (-0.06, R + 0.07)], M_COIL)          # coil ring at the rim
    if lit > 0:
        lathe(bm, c, ax, [(-depth * 0.75, 0.0), (-depth * 0.45, R * 0.35), (-depth * 0.15, 0.0)], M_GLOW, 20)
    if plume > 0:
        lathe(bm, c, ax, [(0.0, R * 0.55), (plume * 0.4, R * 0.9), (plume, R * 1.6)], M_PLUME, 24)

M_CER = 12
def corner_pod(bm, sx, sy, pitch=0.0, roll=0.0, lit=0.0, plume=0.0, stowed=False):
    """one of four vectored thrusters on a two-axis gimbal: the outer fork pitches the pod about x (0 fires down, 90 deg
    fore/aft, 180 up - downforce), the inner ring rolls it about the arm's line +-30 deg (side force, yaw). One magnetic
    cup on its face. Outboard of the hull and the wheels; ceramic tiles on the faces the plume's light reaches.
    stowed: the arm swung in and up - the pod sits over the rail end, inside the chassis' length."""
    yr0, yr1 = sy * 3.55, sy * 4.30; dz = -0.48 if stowed else 0.0
    box(bm, (sx * 0.15, min(yr0, yr1), 1.36 + dz), (sx * 1.25, max(yr0, yr1), 1.50 + dz), M_HULL)          # clamp half on the rail end
    p0 = Vector((sx * 0.9, sy * 4.15, 1.45 + dz))
    c = Vector((sx * 0.80, sy * 3.95, 2.05 + dz)) if stowed else Vector((sx * 1.62, sy * 5.05, 1.55))
    if stowed: pitch, roll = 0.0, 0.0
    hinge = Vector((sx * 0.9, sy * 4.15, 1.62 + dz))
    cyl(bm, p0, hinge, 0.11, M_METAL, 12)                                                          # arm hinge post
    cyl(bm, hinge, c - Vector((sx * 0.48, 0, 0)) if not stowed else c - Vector((0, sy * 0.30, 0.30)), 0.09, M_METAL, 12)   # arm
    if not stowed:
        box(bm, (min(hinge.x, c.x - sx * 0.48) , min(hinge.y, c.y) - 0.10, hinge.z + 0.02), (max(hinge.x, c.x - sx * 0.48), max(hinge.y, c.y) + 0.10, hinge.z + 0.05), M_CER)   # tiles on the arm
    for side in (-1, 1):                                                                           # outer fork (pitch)
        q = c + Vector((side * 0.52, 0, 0))
        box(bm, (q.x - 0.04, q.y - 0.11, q.z - 0.11), (q.x + 0.04, q.y + 0.11, q.z + 0.32), M_METAL)
        box(bm, (q.x - 0.045 * side - 0.005, q.y - 0.11, q.z - 0.11), (q.x - 0.045 * side + 0.005, q.y + 0.11, q.z + 0.32), M_CER)
    box(bm, (c.x - 0.56, c.y - 0.11, c.z + 0.24), (c.x + 0.56, c.y + 0.11, c.z + 0.32), M_METAL)
    cyl(bm, c - Vector((0.52, 0, 0)), c + Vector((0.52, 0, 0)), 0.07, M_METAL, 12)                # pitch trunnion
    cp, sp, cr, sr = math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
    d0 = Vector((sr, 0, -cr))                                                                 # roll first (about y) ...
    d = Vector((d0.x, d0.y * cp - d0.z * sp * sy, d0.y * sp * sy + d0.z * cp))                     # ... then pitch (about x)
    axr = Vector((0, cp, sp * sy))                                                                 # the roll axis after pitch
    lathe(bm, c, axr, [(-0.06, 0.47), (0.06, 0.47), (0.06, 0.43), (-0.06, 0.43)], M_METAL, 40)     # inner gimbal ring (roll)
    cyl(bm, c - axr * 0.47, c - axr * 0.40, 0.05, M_METAL, 10); cyl(bm, c + axr * 0.40, c + axr * 0.47, 0.05, M_METAL, 10)
    cyl(bm, c - d * 0.22, c + d * 0.16, 0.38, M_HULL, 32)                                          # pod can
    cyl(bm, c - d * 0.02, c + d * 0.02, 0.39, M_RED, 32)
    cyl(bm, c + d * 0.13, c + d * 0.16, 0.395, M_CER, 32)                                          # ceramic collar at the cup
    mcup(bm, c + d * 0.20, d, 0.32, 0.15, lit, plume)
def steps(bm, deployed):
    """boarding steps on the left between the axles: stowed - a flat pack on the spine's side, its bottom 0.8 m above the
    ground (higher than the spine's belly); parked - three treads swing out and down to 0.30 m"""
    if not deployed:
        box(bm, (-1.47, -0.45, 0.80), (-1.41, 0.45, 1.30), M_METAL)
        for k in range(3): box(bm, (-1.475, -0.42, 0.86 + k * 0.15), (-1.465, 0.42, 0.88 + k * 0.15), M_SEAM)
        return
    for yy in (-0.44, 0.44):                                                           # stringers, hinged at the deck
        cyl(bm, (-1.44, yy, 1.30), (-1.95, yy, 0.30), 0.03, M_METAL, 10)
    for k, f in enumerate((0.25, 0.55, 0.85)):                                         # three treads
        x = -1.44 - 0.51 * f; z = 1.30 - 1.00 * f
        box(bm, (x - 0.17, -0.42, z - 0.03), (x + 0.17, 0.42, z + 0.01), M_METAL)
def cabin2(bm):
    """low crew cabin: wedge nose, wrap-round glazing, chamfered roof, tapering tail"""
    cs = [(-3.35, 1.30, 1.42, 1.95, 0.25), (-2.85, 1.62, 1.40, 2.62, 0.40), (-1.95, 1.72, 1.38, 3.02, 0.55),
          (2.20, 1.72, 1.38, 3.02, 0.55), (3.05, 1.55, 1.40, 2.80, 0.50), (3.35, 1.25, 1.42, 2.35, 0.35)]
    loft(bm, [oct_ring(y, hw, z0, z1, ch) for y, hw, z0, z1, ch in cs], M_WHITE)
    for yy in (-0.85, 0.65, 2.20):                                                    # panel seams
        loft(bm, [oct_ring(yy - 0.012, 1.732, 1.375, 3.03, 0.55), oct_ring(yy + 0.012, 1.732, 1.375, 3.03, 0.55)], M_SEAM)
    # wrap-round glazing: a band over the sloped nose and round the front corners
    for sx in (-1, 1):                                                                 # side windows, two panes a side
        for y0, y1 in ((-1.85, -1.05), (-0.70, 0.35)):
            quad(bm, [(sx * 1.727, y0, 2.30), (sx * 1.727, y1, 2.30), (sx * 1.727, y1, 2.78), (sx * 1.727, y0, 2.78)][::sx], M_GLASS)
    quad(bm, [(-0.95, -3.31, 2.01), (0.95, -3.31, 2.01), (1.18, -2.90, 2.56), (-1.18, -2.90, 2.56)], M_GLASS)   # windscreen on the wedge
    quad(bm, [(-1.12, -2.82, 2.64), (1.12, -2.82, 2.64), (1.10, -2.05, 3.00), (-1.10, -2.05, 3.00)], M_GLASS)   # roof glazing ahead
    for sx in (-1, 1): box(bm, (sx * 1.722 - 0.01, -1.6, 2.06), (sx * 1.722 + 0.01, 3.0, 2.09), M_RED)
    oc = Vector((-1.735, 1.05, 2.18)); r8 = 0.52                                       # octagonal side hatch
    hv = [bm.verts.new(O + oc + Vector((0, r8 * math.cos(math.pi / 8 + k * math.pi / 4), r8 * math.sin(math.pi / 8 + k * math.pi / 4) * 1.25))) for k in range(8)]
    hv2 = [bm.verts.new(v.co + Vector((-0.035, 0, 0))) for v in hv]
    for k in range(8): bm.faces.new((hv[k], hv[(k + 1) % 8], hv2[(k + 1) % 8], hv2[k])).material_index = M_METAL
    bm.faces.new(hv2[::-1]).material_index = M_HULL
    for sx in (-0.7, 0.7): box(bm, (sx - 0.35, -2.05, 3.02), (sx + 0.35, -1.85, 3.06), M_LAMP)   # roof light bars
    box(bm, (-0.5, 1.8, 3.02), (0.5, 2.6, 3.12), M_HULL)                              # sensor / comms fairing

def build(bm, lit=0.0, plume=0.0, deployed=False, pitch=0.0, roll=0.0, stowed=False, cab=True):
    chassis(bm, stowed=stowed)
    if cab: cabin2(bm)
    if not stowed: steps(bm, deployed)
    for sx in (-1, 1):
        for sy in (-1, 1): corner_pod(bm, sx, sy, pitch, roll, lit, plume, stowed)
    box(bm, (-1.43, 0.95, 0.70), (-1.40, 1.25, 0.80), M_RED)                         # refuelling port

try:
    for o in bpy.data.objects:
        if o.name.startswith("Jet"): o.hide_render = True
    MATS = [mat("PHull", (0.11, 0.12, 0.135), 0.6, 0.3), mat("PWhite", (0.85, 0.86, 0.87), 0.45), mat("PMetal", (0.32, 0.33, 0.35), 0.3, 0.8),
            mat("PGlass", (0.05, 0.09, 0.13), 0.05, 0.4), mat("PTread", (0.05, 0.05, 0.055), 0.85), mat("PRed", (0.72, 0.12, 0.10), 0.4),
            mat("PLamp", (1.0, 0.96, 0.85), 0.15), mat("PSeam", (0.04, 0.04, 0.045), 0.7), mat("PBlade", (0.20, 0.22, 0.26), 0.35, 0.6),
            emis("PPlume", (0.55, 0.45, 1.0), 2.0, 0.18), emis("PGlow", (0.80, 0.75, 1.0), 25.0, 0.9), mat("PCoil", (0.55, 0.30, 0.15), 0.35, 0.9), mat("PCeramic", (0.78, 0.76, 0.72), 0.9)]
    sys.path.insert(0, HERE); import render_util
    O = Vector((3.2, 6.9, 0.0))
    b = bmesh.new(); build(b, deployed=True); veh = obj("MPU_jet", b, MATS)
    cam = render_util.setup_stage()
    fl = bpy.data.objects.get("Floor")
    if fl: fl.scale = (8, 8, 1)
    sun = bpy.data.lights.new("PSun", 'SUN'); sun.energy = 3.0; sun.angle = 0.05
    so = bpy.data.objects.new("PSun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (Vector((0, 0, 0)) - Vector((-9, -6, 10))).to_track_quat('-Z', 'Y')
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = 1600, 900; cam.data.clip_end = 400
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = 13.5
    cam.location = (-60, 5.9, 1.9); cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_side.png"); bpy.ops.render.render(write_still=True)
    cam.data.type = 'PERSP'; cam.data.lens = 35
    loc = Vector((-9.5, -7.5, 4.6)); tgt = Vector((O.x, O.y - 1.0, 1.4))
    cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_34.png"); bpy.ops.render.render(write_still=True)
    # the hop: 3 m up, pods swung 0.35 m down, cups lit, faint plasma
    bpy.data.objects.remove(veh, do_unlink=True)
    b = bmesh.new(); build(b, lit=1.0, plume=2.6); veh = obj("MPU_hop", b, MATS); veh.location.z = 3.0
    if fl: fl.scale = (40, 40, 1); fl.data.materials[0].diffuse_color = (0.33, 0.32, 0.30, 1)
    next(n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND').inputs[1].default_value = 0.02
    loc = Vector((-12.0, -9.0, 1.6)); tgt = Vector((O.x, O.y, 2.8))
    cam.location = loc; cam.data.lens = 32; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_hop.png"); bpy.ops.render.render(write_still=True)
    # under view of the cups
    loc = Vector((O.x - 3.0, O.y - 9.5, 0.4)); tgt = Vector((O.x, O.y - 4.7, 3.4))
    cam.location = loc; cam.data.lens = 40; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_under.png"); bpy.ops.render.render(write_still=True)
    # driving: steps stowed, side view on regolith (what hangs below the chassis)
    bpy.data.objects.remove(veh, do_unlink=True)
    b = bmesh.new(); build(b); veh = obj("MPU_drive", b, MATS)
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = 13.5
    cam.location = (-60, 5.9, 1.9); cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
    next(n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND').inputs[1].default_value = 0.6
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_side_drive.png"); bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(veh, do_unlink=True)
    b = bmesh.new(); build(b, lit=1.0, plume=2.4, pitch=math.pi); veh = obj("MPU_press", b, MATS)
    next(n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND').inputs[1].default_value = 0.02
    cam.data.type = 'PERSP'; cam.data.lens = 32
    loc = Vector((-11.0, -8.5, 3.2)); tgt = Vector((O.x, O.y, 2.0))
    cam.location = loc; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_press.png"); bpy.ops.render.render(write_still=True)
    # side force: hovering, all four pods rolled 25 deg to one side (front view)
    bpy.data.objects.remove(veh, do_unlink=True)
    b = bmesh.new(); build(b, lit=1.0, plume=2.4, roll=25 * math.pi / 180); veh = obj("MPU_side", b, MATS); veh.location.z = 3.0
    loc = Vector((O.x - 2.0, O.y - 17.0, 3.0)); tgt = Vector((O.x, O.y, 3.0))
    cam.location = loc; cam.data.lens = 40; cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_sideforce.png"); bpy.ops.render.render(write_still=True)
    # stowed for the hangar: chassis knelt, no cabin, pods folded over the rail ends (3.0 m under the shuttle's belly)
    bpy.data.objects.remove(veh, do_unlink=True)
    b = bmesh.new(); build(b, stowed=True, cab=False); box(b, (-2.4, -5.0, 2.98), (2.4, 5.0, 3.0), M_RED); veh = obj("MPU_stow", b, MATS)
    next(n for n in sc.world.node_tree.nodes if n.type == 'BACKGROUND').inputs[1].default_value = 0.6
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = 13.5
    cam.location = (-60, 5.9, 1.9); cam.rotation_quaternion = Vector((1, 0, 0)).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, "mpu_jet_stowed.png"); bpy.ops.render.render(write_still=True)
    log.append("rendered side, 34, hop, under, side_drive, press, sideforce, stowed")
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
