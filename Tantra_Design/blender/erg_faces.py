# Tantra: face sketches for Erg Noor, commander of the 37th star expedition (MPFB / MakeHuman, CC0 assets).
# Canon (the novel): a usually pale face, stern and impassive; eyes with a sharp, constant fire; dark hair with early grey;
# quick, precise, springy movement; a voice with metallic notes. Nothing more is fixed - the variants explore the rest.
# Renders a front, three-quarter and profile portrait per variant into renders/erg/. Saves nothing else.
import bpy, os, traceback
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "renders", "erg"); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "erg_faces.log"); log = []

def EYE(t, w): return [("l-eye-" + t, w), ("r-eye-" + t, w)]
def CHEEK(t, w): return [("l-cheek-" + t, w), ("r-cheek-" + t, w)]
GREY_EYES = {"IrisMajorColor": [0.30, 0.34, 0.37, 1.0], "IrisMinorColor": [0.12, 0.14, 0.16, 1.0], "IrisSection4Color": [0.05, 0.06, 0.07, 1.0]}
BROWN_EYES = {"IrisMajorColor": [0.26, 0.15, 0.07, 1.0], "IrisMinorColor": [0.09, 0.05, 0.02, 1.0], "IrisSection4Color": [0.04, 0.02, 0.01, 1.0]}
# common: a lean, fit man of about forty (MakeHuman age 0.5 = 25 y, 1.0 = 90 y), tall, pale
BASE = {"gender": 1.0, "age": 0.66, "muscle": 0.62, "weight": 0.42, "proportions": 1.0, "height": 0.72,
        "cupsize": 0.5, "firmness": 0.5, "race": {"asian": 0.0, "caucasian": 1.0, "african": 0.0}}
# the canon's stern, impassive face - for every variant: a firmer brow ridge with the brows set low, deeper-set eyes,
# leaner cheeks, the mouth's corners a touch down
STERN = [("forehead-nubian-incr", 0.45), ("eyebrows-trans-down", 0.35), ("mouth-angles-down", 0.25), ("head-fat-decr", 0.4)]         + EYE("push1-in", 0.35) + CHEEK("volume-decr", 0.35)
VARIANTS_ALL = {
    # A: the commander - a hard, angular face: square jaw, deep-set eyes under a firm brow, straight nose, thin mouth
    "A_commander": dict(race=None, hair="short02", grey=0.30, eyes=GREY_EYES, brows="eyebrow009", targets=[
        ("head-square", 0.35), ("chin-bones-incr", 0.4), ("chin-prominent-incr", 0.3), ("forehead-nubian-incr", 0.3),
        ("nose-hump-decr", 0.4), ("nose-scale-horiz-decr", 0.2), ("mouth-scale-vert-decr", 0.3), ("mouth-angles-down", 0.15)]
        + EYE("push1-in", 0.3) + CHEEK("bones-incr", 0.3)),
    # B: the ascetic - long narrow face, high cheekbones, hollow cheeks, thin lips: years of flight show
    "B_ascetic": dict(race=None, hair="short01", grey=0.45, eyes=GREY_EYES, brows="eyebrow012", targets=[
        ("head-oval", 0.4), ("head-scale-horiz-decr", 0.25), ("head-fat-decr", 0.6), ("chin-triangle", 0.2),
        ("nose-scale-vert-incr", 0.25), ("nose-point-width-decr", 0.3), ("mouth-lowerlip-volume-decr", 0.3), ("mouth-upperlip-volume-decr", 0.3)]
        + CHEEK("bones-incr", 0.6) + CHEEK("volume-decr", 0.6) + EYE("push1-in", 0.4)),
    # C: the scientist - a high forehead, finer jaw, a little younger and calmer; a thinker more than a soldier
    "C_scientist": dict(race=None, hair="short04", grey=0.20, eyes=BROWN_EYES, brows="eyebrow001", age=0.56, targets=[
        ("forehead-scale-vert-incr", 0.4), ("head-invertedtriangular", 0.3), ("chin-width-decr", 0.2), ("nose-greek-incr", 0.3),
        ("mouth-angles-up", 0.1)] + EYE("height2-incr", 0.15)),
    # D: Efremov's mixed humanity - a touch of Eurasian features, dark eyes, strong cheekbones, straight dark hair
    "D_eurasian": dict(race={"asian": 0.25, "caucasian": 0.70, "african": 0.05}, hair="short02", grey=0.25, eyes=BROWN_EYES, brows="eyebrow008", targets=[
        ("head-rectangular", 0.25), ("chin-bones-incr", 0.3), ("nose-hump-decr", 0.3)] + CHEEK("bones-incr", 0.5) + EYE("push1-in", 0.2)),
    # E: from the user's reference drawings (features understood, not copied): hair swept back over a high, receding
    # hairline; a broad square jaw and strong cheekbones; a straight nose with a defined tip; low straight brows over
    # deep-set eyes; a strong neck. Canon colour kept: dark hair with early grey (the colour drawing is fair-haired).
    "E1_reference_smile": dict(race=None, hair="short04", grey=0.30, eyes=GREY_EYES, brows="eyebrow009", nostern=("mouth-angles-down",), targets=[
        ("head-square", 0.5), ("chin-width-incr", 0.35), ("chin-bones-incr", 0.55), ("chin-prominent-incr", 0.3), ("chin-height-incr", 0.15),
        ("nose-hump-decr", 0.4), ("nose-point-width-incr", 0.15), ("nose-scale-vert-incr", 0.1), ("forehead-scale-vert-incr", 0.3),
        ("neck-scale-horiz-incr", 0.4), ("mouth-angles-up", 0.25), ("mouth-lowerlip-volume-incr", 0.15)]
        + CHEEK("bones-incr", 0.55) + EYE("push1-in", 0.45)),
    "E2_reference_stern": dict(race=None, hair="short04", grey=0.30, eyes=GREY_EYES, brows="eyebrow009", targets=[
        ("head-square", 0.5), ("chin-width-incr", 0.35), ("chin-bones-incr", 0.55), ("chin-prominent-incr", 0.3), ("chin-height-incr", 0.15),
        ("nose-hump-decr", 0.4), ("nose-point-width-incr", 0.15), ("nose-scale-vert-incr", 0.1), ("forehead-scale-vert-incr", 0.3),
        ("neck-scale-horiz-incr", 0.4), ("mouth-lowerlip-volume-incr", 0.15)]
        + CHEEK("bones-incr", 0.55) + EYE("push1-in", 0.45)),
    # F: built to the user's drawings (front, profile, full figure): short face with a broad, heavy square jaw and a
    # strong chin; high forehead with receding temples, hair swept back and cut short at the nape; low straight brows
    # over deep-set eyes; strong cheekbones with lines down the cheeks; straight nose with a broad tip; thin upper and
    # fuller lower lip in a slight smile; a thick, strong neck; ears set out. Canon colour: dark hair, early grey.
    "F_drawings": dict(race=None, hair="short04", grey=0.30, eyes=GREY_EYES, brows="eyebrow009", age=0.60, muscle=0.78, trim_nape=True,
        nostern=("mouth-angles-down", "l-eye-push1-in", "r-eye-push1-in"), targets=[
        ("head-square", 0.55), ("head-scale-horiz-incr", 0.05),
        ("chin-width-incr", 0.7), ("chin-bones-incr", 0.9), ("chin-prominent-incr", 0.6), ("chin-height-incr", 0.3), ("neck-double-decr", 0.6),
        ("forehead-scale-vert-incr", 0.35), ("forehead-nubian-incr", 0.5), ("eyebrows-trans-down", 0.45),
        ("nose-hump-decr", 0.5), ("nose-point-width-incr", 0.4), ("nose-flaring-incr", 0.3), ("nose-scale-depth-incr", 0.35),
        ("mouth-angles-up", 0.6), ("mouth-upperlip-volume-decr", 0.2), ("mouth-lowerlip-volume-incr", 0.25), ("mouth-scale-horiz-incr", 0.15),
        ("neck-scale-horiz-incr", 0.45), ("measure-neck-circ-incr", 0.35), ("head-fat-decr", 0.7)]
        + CHEEK("bones-incr", 0.6) + CHEEK("volume-decr", 0.4) + EYE("height1-incr", 0.25) + EYE("height2-incr", 0.15)
        + [("l-ear-scale-incr", 0.3), ("r-ear-scale-incr", 0.3), ("l-ear-wing-decr", 0.35), ("r-ear-wing-decr", 0.35)]),   # the ears lie close (front drawing)
}

VARIANTS = {k: v for k, v in VARIANTS_ALL.items() if k.startswith('F')}   # this round: built to the drawings


def build(v):
    import addon_utils
    addon_utils.enable("bl_ext.tantra.mpfb", default_set=True)
    from bl_ext.tantra.mpfb.services.humanservice import HumanService
    info = HumanService._create_default_human_info_dict()
    ph = dict(BASE); ph["race"] = v["race"] or BASE["race"]
    if "age" in v: ph["age"] = v["age"]
    if "muscle" in v: ph["muscle"] = v["muscle"]
    info["phenotype"] = ph
    info["proxy"] = ""
    info["eyes"] = "low-poly/low-poly.mhclo"; info["eyebrows"] = "%s/%s.mhclo" % (v["brows"], v["brows"])
    info["eyelashes"] = "eyelashes01/eyelashes01.mhclo"   # short lashes: a man's eye
    info["hair"] = "%s/%s.mhclo" % (v["hair"], v["hair"])
    info["skin_mhmat"] = "middleage_caucasian_male/middleage_caucasian_male.mhmat"; info["skin_material_type"] = "MAKESKIN"
    info["eyes_material_type"] = "PROCEDURAL_EYES"; info["eyes_material_settings"] = v["eyes"]
    info["alternative_materials"] = {}; info["name"] = "ErgNoor"; info["rig"] = ""
    tg = {}
    for t, w in [x for x in STERN if x[0] not in v.get("nostern", ())] + v["targets"]: tg[t] = max(tg.get(t, 0.0), w)   # the variant may push a common target further
    info["targets"] = [{"target": t, "value": w} for t, w in tg.items()]
    st = HumanService.get_default_deserialization_settings(); st["subdiv_levels"] = 1; st["override_skin_model"] = "MAKESKIN"
    return HumanService.deserialize_from_dict(info, st)


def tint_hair(grey, asset):
    """dark brown hair with early grey: each hair texture multiplied towards near-black, then mixed with its own grey"""
    dark = (0.09, 0.07, 0.055, 1.0)
    for o in bpy.data.objects:
        if o.type != 'MESH' or asset.lower() not in o.name.lower(): continue   # MPFB names the hair object after its asset
        for slot in o.material_slots:
            m = slot.material
            if not m or not m.use_nodes: continue
            nt = m.node_tree
            for tex in [n for n in nt.nodes if n.type == 'TEX_IMAGE']:
                # only the links that carry the colour itself (the alpha path of the hair cards stays untouched)
                outs = [l for l in nt.links if l.from_node == tex and l.from_socket.name == "Color"
                        and (l.to_socket.name == "Base Color" or (l.to_node.type in ('MIX', 'MIX_RGB') and l.to_socket.type == 'RGBA'))]
                if not outs: continue
                mul = nt.nodes.new("ShaderNodeMix"); mul.data_type = 'RGBA'; mul.blend_type = 'MULTIPLY'
                mul.inputs[0].default_value = 1.0; mul.inputs[7].default_value = dark
                bw = nt.nodes.new("ShaderNodeRGBToBW")
                mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.inputs[0].default_value = grey
                nt.links.new(tex.outputs["Color"], mul.inputs[6]); nt.links.new(tex.outputs["Color"], bw.inputs[0])
                nt.links.new(mul.outputs[2], mix.inputs[6]); nt.links.new(bw.outputs[0], mix.inputs[7])
                for l in outs:
                    to = l.to_socket; nt.links.remove(l); nt.links.new(mix.outputs[2], to)
                log.append("hair tint %s / %s" % (o.name, m.name))


def trim_nape(asset):
    """cut the hair short at the back below the occiput (the drawings: a short nape, the neck open)"""
    import bmesh
    for o in bpy.data.objects:
        if o.type != 'MESH' or asset.lower() not in o.name.lower(): continue
        M = o.matrix_world; Mi = M.inverted()
        bm = bmesh.new(); bm.from_mesh(o.data)
        P = [M @ v.co for v in bm.verts]
        top = max(p.z for p in P); cy = sum(p.y for p in P) / len(P)
        cut = top - 0.205   # the hair reaches the ear lobes at the back, cut straight there
        dead = [v for v, p in zip(bm.verts, P) if p.y > cy - 0.01 and p.z < cut]
        bmesh.ops.delete(bm, geom=dead, context='VERTS')
        c = Vector((sum(p.x for p in P) / len(P), cy, top - 0.11))   # roughly the skull's centre
        for v in bm.verts:   # volume: the swept-back hair stands up over the forehead and the crown
            p = M @ v.co; h = max(0.0, min(1.0, (p.z - (top - 0.10)) / 0.10)); front = max(0.0, min(1.0, (cy + 0.02 - p.y) / 0.10))
            v.co = Mi @ (c + (p - c) * (1.0 + 0.10 * h * front))   # only over the forehead, no helmet at the sides
        bm.to_mesh(o.data); bm.free(); o.data.update()
        log.append("nape trimmed on %s: %d verts below %.3f" % (o.name, len(dead), cut))


def stage():
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 640, 760
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    nt = world.node_tree; bg = nt.nodes.get("Background") or nt.nodes.new("ShaderNodeBackground")
    wo = nt.nodes.get("World Output") or nt.nodes.new("ShaderNodeOutputWorld"); nt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.05, 0.055, 0.065, 1); bg.inputs[1].default_value = 0.5
    dg = bpy.context.evaluated_depsgraph_get(); zs = []
    for o in bpy.data.objects:   # the shaped, masked body (shape keys and the helper mask applied)
        if o.type == 'MESH' and o.name.lower().endswith('.body'):
            e = o.evaluated_get(dg); me = e.to_mesh(); zp = [o.matrix_world @ v.co for v in me.vertices]; zs += [p.z for p in zp]; e.to_mesh_clear()
    top = max(zs)
    hz = [p for p in zp if p.z > top - 0.25]
    head = Vector((sum(p.x for p in hz) / len(hz), sum(p.y for p in hz) / len(hz), top - 0.14))
    def light(name, loc, energy, size, color=(1, 1, 1)):
        l = bpy.data.lights.new(name, 'AREA'); l.energy = energy; l.size = size; l.color = color
        o = bpy.data.objects.new(name, l); o.location = head + Vector(loc); sc.collection.objects.link(o)
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (head - o.location).to_track_quat('-Z', 'Y')
    light("Key", (1.0, -1.4, 0.6), 60, 0.8); light("Fill", (-1.4, -0.9, 0.1), 18, 1.5, (0.85, 0.9, 1.0)); light("Rim", (0.4, 1.4, 0.6), 45, 0.8, (0.8, 0.88, 1.0))
    cam_d = bpy.data.cameras.new("Cam"); cam_d.lens = 85; cam = bpy.data.objects.new("Cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam
    return cam, head


try:
    for name, v in VARIANTS.items():
        bpy.ops.wm.read_homefile(use_empty=True)
        build(v)
        if v.get("trim_nape"): trim_nape(v["hair"])
        tint_hair(v["grey"], v["hair"]); tint_hair(0.1, v["brows"])   # brows dark like the hair
        cam, head = stage()
        for view, off in (("front", (0, -0.95, 0.02)), ("tq", (0.62, -0.72, 0.04)), ("profile", (0.95, 0, 0.02)), ("drawfront", (-0.32, -1.05, 0.02)), ("drawprofile", (-1.12, 0.0, 0.0)), ("back", (0.25, 1.1, 0.0))):
            cam.location = head + Vector(off)
            cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (head - cam.location).to_track_quat('-Z', 'Y')
            bpy.context.view_layer.update()
            bpy.context.scene.render.filepath = os.path.join(OUT, "%s_%s.png" % (name, view)); bpy.ops.render.render(write_still=True)
        log.append("rendered " + name)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
