# Helmet lamps for the borazon suit (the new type, astronavigator_suit2.blend): three mock-ups on the helmet's own side
# ports (Helmhp boss / Helmhr red ring / Helmhc cap, x +-0.155, y -0.074, z 1.63 - where the game's lamps are, cfg LampPos
# +-0.162 0.707 0.11). The ports face sideways; a lamp must throw forward: each variant turns the port's light forward.
#   A «Порт-фара»   - the port's cap replaced by a short forward-facing head: a chamfered block, sapphire lens in a black bezel
#   B «Скуловая планка» - a slim wedge from the port forward along the shell, a lens strip in it (wide flood)
#   C «Шарнирная»   - a small ball lamp in a socket on the port, a hood over it (no glare on the visor), aimable
#   blender -b astronavigator_suit2.blend --python helmet_lamps.py -> renders\hl_<A|B|C>_<front|q34|side>.png
import bpy, bmesh, math, os
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath)); OUT = os.path.join(HERE, "renders")
LOG = open(os.path.join(OUT, "hl_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
for n in ("SunShade", "ShadeArms"):                    # no shade on this type (the visor's own filter)
    if n in bpy.data.objects: bpy.data.objects[n].hide_render = True

def mat(name, col, metal=0.0, rough=0.4, emit=None, strength=0.0, alpha=1.0, trans=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Metallic"].default_value = metal; b.inputs["Roughness"].default_value = rough
    if emit: b.inputs["Emission Color"].default_value = (*emit, 1); b.inputs["Emission Strength"].default_value = strength
    if trans: b.inputs["Transmission Weight"].default_value = trans
    return m
SHELL = bpy.data.materials.get("HelmetShell")
DARK = mat("HL_dark", (0.025, 0.026, 0.03), metal=0.6, rough=0.35)          # black anodised bezel
STEEL = mat("HL_steel", (0.55, 0.56, 0.58), metal=0.9, rough=0.25)
RED = bpy.data.materials.get("HV_red") or mat("HL_red", (0.55, 0.03, 0.03))
LENS = mat("HL_lens", (0.9, 0.93, 1.0), rough=0.05, emit=(1.0, 0.93, 0.82), strength=60.0)   # the emitter behind sapphire, lit
REFL = mat("HL_refl", (0.8, 0.8, 0.82), metal=1.0, rough=0.08)              # the reflector cup
parts = []

def obj(name, bm, m, smooth=True):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(m)
    for p in me.polygons: p.use_smooth = smooth
    o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); parts.append(o); return o

def cyl(name, r0, r1, length, m, M, seg=32, smooth=True):
    """a frustum along local -y (forward) from 0 to -length, radius r0 -> r1"""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=seg, radius1=r0, radius2=r1, depth=length,
                          matrix=Matrix.Translation((0, -length / 2, 0)) @ Matrix.Rotation(math.radians(90), 4, 'X'))
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts); return obj(name, bm, m, smooth)

def box(name, size, m, M, bevel=0.0):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
    if bevel > 0: bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=3, affect='EDGES')
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts); return obj(name, bm, m, smooth=bevel > 0)

def sphere(name, r, m, M):
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=r)
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts); return obj(name, bm, m)

def lamp_light(name, pos, spot=0.7):
    L = bpy.data.lights.new(name, 'SPOT'); L.energy = 6.0; L.spot_size = spot; L.spot_blend = 0.5; L.color = (1.0, 0.93, 0.82)
    L.shadow_soft_size = 0.004
    o = bpy.data.objects.new(name, L); sc.collection.objects.link(o)
    o.location = pos; o.rotation_euler = (math.radians(-(90 - 6)), 0, 0)   # forward (-y), 6 deg down
    parts.append(o); return o

PORT = Vector((0.155, -0.074, 1.630))   # the port's centre (the +x side); mirrored for -x

def side(s):
    """placement on side s (+1 / -1): the port centre, x mirrored"""
    return Matrix.Translation((s * PORT.x, PORT.y, PORT.z)) @ Matrix.Scale(1, 4)

def variant_A(s):
    # «Порт-фара»: the cap off; on the boss a chamfered head 30 x 26 mm, 24 mm deep, its face forward; a 18 mm sapphire lens
    # in a black bezel ring, a polished reflector cup behind it; the red port ring stays as the trim
    # a hex head (the port's own hex, 34 mm across) standing 26 mm forward off the boss, a 22 mm lens in its face
    M = side(s) @ Matrix.Translation((s * 0.010, 0.010, 0)) @ Matrix.Rotation(math.radians(30), 4, 'Y')
    cyl("A_head%+d" % s, 0.0185, 0.0175, 0.026, SHELL, M, seg=6, smooth=False)
    cyl("A_bezel%+d" % s, 0.0135, 0.0135, 0.0035, DARK, M @ Matrix.Translation((0, -0.026, 0)))
    cyl("A_refl%+d" % s, 0.0115, 0.0080, 0.003, REFL, M @ Matrix.Translation((0, -0.0275, 0)))
    cyl("A_lens%+d" % s, 0.0110, 0.0110, 0.0012, LENS, M @ Matrix.Translation((0, -0.0293, 0)))
    lamp_light("A_light%+d" % s, (M @ Matrix.Translation((0, -0.04, 0))).to_translation(), 1.2)

def variant_B(s):
    # «Скуловая планка»: a slim wedge from the port forward along the shell to the visor's frame, 70 mm, 16 mm tall; a lens
    # strip (three emitters behind one long sapphire window) - a wide flood, low, out of the visor's view
    # a tapered hex fairing out of the port, 62 mm forward along the shell (flattened: 34 -> 20 mm wide, 22 -> 12 tall),
    # its blunt nose a dark window with a strip of three emitters - a wide low flood
    M = side(s) @ Matrix.Translation((s * 0.002, 0.012, -0.002)) @ Matrix.Rotation(s * math.radians(-10), 4, 'Z') @ Matrix.Scale(0.62, 4, Vector((0, 0, 1)))
    cyl("B_fair%+d" % s, 0.019, 0.011, 0.062, SHELL, M @ Matrix.Rotation(math.radians(30), 4, 'Y'), seg=6, smooth=False)
    box("B_window%+d" % s, (0.016, 0.003, 0.014), DARK, M @ Matrix.Translation((0, -0.0625, 0)), bevel=0.0012)
    for k in range(3):
        cyl("B_led%d%+d" % (k, s), 0.0024, 0.0024, 0.0012, LENS, M @ Matrix.Translation((-0.0048 + 0.0048 * k, -0.0642, 0)), seg=16)
    lamp_light("B_light%+d" % s, (M @ Matrix.Translation((0, -0.07, 0))).to_translation(), 1.8)

def variant_C(s):
    # «Шарнирная»: on the port a socket ring and a 22 mm ball lamp in it (aimable +-15 deg), its 14 mm lens forward; a short
    # hood over the top so the beam's glare stays off the visor
    M = side(s) @ Matrix.Translation((s * 0.014, -0.004, 0))
    cyl("C_socket%+d" % s, 0.014, 0.013, 0.010, DARK, M @ Matrix.Rotation(s * math.radians(90), 4, 'Z') @ Matrix.Translation((0, 0.005, 0)))
    sphere("C_ball%+d" % s, 0.013, STEEL, M @ Matrix.Translation((s * 0.004, -0.004, 0)))
    cyl("C_lens%+d" % s, 0.0088, 0.0088, 0.0015, LENS, M @ Matrix.Translation((s * 0.004, -0.0165, 0)))
    box("C_hood%+d" % s, (0.020, 0.016, 0.003), DARK, M @ Matrix.Translation((s * 0.004, -0.012, 0.0125)) @ Matrix.Rotation(math.radians(-8), 4, 'X'), bevel=0.001)
    lamp_light("C_light%+d" % s, (M @ Matrix.Translation((s * 0.004, -0.025, 0))).to_translation(), 0.9)

VARIANTS = {"A": variant_A, "B": variant_B, "C": variant_C}

# the cameras: close on the helmet
cam_data = bpy.data.cameras.new("HL_cam"); cam_data.lens = 85
cam = bpy.data.objects.new("HL_cam", cam_data); sc.collection.objects.link(cam); sc.camera = cam
TARGET = Vector((0.0, -0.06, 1.63))
VIEWS = {"front": Vector((0.0, -0.95, 1.66)), "q34": Vector((0.62, -0.72, 1.70)), "side": Vector((0.95, -0.12, 1.65))}
def aim(pos):
    cam.location = pos; d = TARGET - pos; cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
sc.render.resolution_x = 720; sc.render.resolution_y = 720; sc.render.film_transparent = False
try: sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception: sc.render.engine = 'BLENDER_EEVEE'
hc = [bpy.data.objects[n] for n in ("Helmhc", "Helmhc1") if n in bpy.data.objects]
for key, fn in VARIANTS.items():
    for o in hc: o.hide_render = key in ("A", "C")       # the port cap gives way to the lamp head / socket
    start = len(parts)
    for s in (1, -1): fn(s)
    for vname, pos in VIEWS.items():
        aim(pos); sc.render.filepath = os.path.join(OUT, "hl_%s_%s.png" % (key, vname))
        bpy.ops.render.render(write_still=True); LOG.write("rendered %s %s\n" % (key, vname)); LOG.flush()
    for o in parts[start:]: bpy.data.objects.remove(o, do_unlink=True)
    del parts[start:]
LOG.write("done\n"); LOG.close()
