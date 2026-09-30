# Tantra: look at the figure the way Orbiter shows it - from the exported files only (.msh, .skin, .clip), with the
# same CPU skinning as OrbiterCrew (pose * rest^-1, four weights), textured from the game's DDS files.
# Renders every frame of the walk and run clips from the front-three-quarter and the side into renders/game_<variant>/.
# Usage: run.ps1 -Script game_view.py -Rest <coverall|suit>
import bpy, os, sys, math, traceback
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
O = r"C:\Games\Orbiter 2016"
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
VARIANT = argv[0] if argv else "coverall"
MESH, SKIN, CLIPS = {"coverall": ("AstronavigatorSkin", "Astronavigator", "anim"), "suit": ("AstronavigatorSuit", "AstronavigatorSuit", "anim_suit")}[VARIANT]
OUT = os.path.join(HERE, "renders", "game_" + VARIANT); os.makedirs(OUT, exist_ok=True)
LOG = os.path.join(HERE, "game_view.log"); log = []


def read_msh(p):
    L = open(p).read().split('\n'); i = 0; groups = []; cur = {}; textures = []
    while i < len(L):
        t = L[i].split()
        if not t: i += 1; continue
        if t[0] in ('MATERIAL', 'TEXTURE') and len(t) > 1 and t[1].isdigit(): cur['mat' if t[0] == 'MATERIAL' else 'tex'] = int(t[1])
        elif t[0] == 'GEOM':
            nv, nt = int(t[1]), int(t[2])
            V = np.array([list(map(float, L[i + 1 + k].split()[:8])) for k in range(nv)])
            F = np.array([list(map(int, L[i + 1 + nv + k].split()[:3])) for k in range(nt)])
            groups.append(dict(cur, V=V, F=F)); cur = {}; i += nv + nt
        elif t[0] == 'TEXTURES':
            n = int(t[1]); textures = [L[i + 1 + k].split()[0] for k in range(n)]; i += n
        i += 1
    return groups, textures


MORPHS = []   # (group, vertex indices, deltas (n, 6))
NAMES = []; PARENTS = []


def read_skin(p):
    L = [l.split() for l in open(p).read().split('\n') if l.strip()]; i = 0; bones = []; W = []
    while i < len(L):
        t = L[i]
        if t[0] == 'MORPH':
            n = int(t[3]); rows = np.array([list(map(float, L[i + 1 + k])) for k in range(n)])
            MORPHS.append((t[1], int(t[2]), rows[:, 0].astype(int), rows[:, 1:7])); i += n + 1; continue
        if t[0] == 'BONES':
            for k in range(int(t[1])):
                r = L[i + 1 + k]; bones.append((np.array(list(map(float, r[2:11]))).reshape(3, 3), np.array(list(map(float, r[11:14])))))
                NAMES.append(r[0]); PARENTS.append(int(r[1]))
            i += int(t[1])
        elif t[0] == 'GROUP':
            nv = int(t[2]); W.append(np.array([list(map(float, L[i + 1 + k])) for k in range(nv)])); i += nv
        i += 1
    return bones, W


def read_clip(p):
    L = [l.split() for l in open(p).read().split('\n') if l.strip()]; hdr = {}; frames = []; i = 0
    while i < len(L):
        t = L[i]
        if t[0] == 'FRAME':
            nb = int(hdr['BONES']); frames.append(np.array([list(map(float, L[i + 1 + k])) for k in range(nb)])); i += nb
        elif len(t) == 2: hdr[t[0]] = t[1]
        i += 1
    return hdr, frames


def qmat(q):
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def skinned(bones, W, groups, frame):
    S = [(qmat(f[:4]) @ R0.T, None) for (R0, T0), f in zip(bones, frame)]
    S = [(R, f[4:7] - R @ T0) for (R, _), (R0, T0), f in zip(S, bones, frame)]
    Rs = np.stack([s[0] for s in S]); Ts = np.stack([s[1] for s in S])
    out = []
    for g, w in zip(groups, W):
        P = g['V'][:, :3]; acc = np.zeros_like(P)
        for k in range(4):
            b = w[:, k].astype(int); wk = w[:, 4 + k][:, None]
            acc += wk * (np.einsum('nij,nj->ni', Rs[b], P) + Ts[b])
        out.append(acc)
    return out


def qmul(a, b):
    w1, x1, y1, z1 = a; w2, x2, y2, z2 = b
    return np.array([w1*w2 - x1*x2 - y1*y2 - z1*z2, w1*x2 + x1*w2 + y1*z2 - z1*y2, w1*y2 - x1*z2 + y1*w2 + z1*x2, w1*z2 + x1*y2 - y1*x2 + z1*w2])


def turn(fr, bone, axis, ang):
    """Skin::Turn: rotate a bone's subtree about its joint (model frame)"""
    b = NAMES.index(bone); sub = [i for i in range(len(NAMES)) if any(j == b for j in chain(i))]
    r = np.array([np.cos(ang / 2), *(np.array(axis) * np.sin(ang / 2))]); R = qmat(r); piv = fr[b, 4:7].copy()
    for i in sub:
        fr[i, :4] = qmul(r, fr[i, :4]); fr[i, 4:7] = piv + R @ (fr[i, 4:7] - piv)


def chain(i):
    while i >= 0: yield i; i = PARENTS[i]


def coverall_layer(fr, wrun):
    fr = fr.copy(); F = (0, 0, 1); arms = 0.01 + 0.05 * wrun; st = 0.015 * wrun
    turn(fr, "LeftArm", F, -arms); turn(fr, "RightArm", F, arms)
    turn(fr, "LeftUpLeg", F, -st); turn(fr, "RightUpLeg", F, st)
    turn(fr, "LeftFoot", F, st); turn(fr, "RightFoot", F, -st)
    return fr


def to_blender(P):   # Orbiter (x right, y up, z fwd, origin 0.93 m above the soles) -> Blender (z up, -y forward)
    return np.stack([-P[:, 0], -P[:, 2], P[:, 1] + 0.93], 1)


try:
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    groups, textures = read_msh(os.path.join(O, "Meshes", "Tantra", MESH + ".msh"))
    bones, W = read_skin(os.path.join(O, "Config", "Tantra", SKIN + ".skin"))
    objs = []
    for gi, g in enumerate(groups):
        V = to_blender(g['V'][:, :3]); F = g['F'][:, [0, 2, 1]]           # export reversed the winding
        me = bpy.data.meshes.new("g%d" % gi); me.from_pydata(V.tolist(), [], F.tolist()); me.update()
        uv = me.uv_layers.new(); uvs = np.stack([g['V'][:, 6], 1 - g['V'][:, 7]], 1)
        uv.data.foreach_set("uv", uvs[np.array([l.vertex_index for l in me.loops])].ravel())
        for p in me.polygons: p.use_smooth = True
        m = bpy.data.materials.new("m%d" % gi); m.use_nodes = True; nt = m.node_tree
        for nd in list(nt.nodes): nt.nodes.remove(nd)
        bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled"); outn = nt.nodes.new("ShaderNodeOutputMaterial"); nt.links.new(bsdf.outputs[0], outn.inputs[0])
        ti = g.get('tex', 0)
        if ti and ti <= len(textures):
            path = os.path.join(O, "Textures", textures[ti - 1])
            if os.path.exists(path):
                tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = bpy.data.images.load(path)
                nt.links.new(tn.outputs[0], bsdf.inputs["Base Color"]); nt.links.new(tn.outputs[1], bsdf.inputs["Alpha"])
                m.blend_method = 'HASHED' if hasattr(m, 'blend_method') else None
        else:
            bsdf.inputs["Base Color"].default_value = (0.8, 0.8, 0.8, 1)
        me.materials.append(m)
        ob = bpy.data.objects.new("g%d" % gi, me); sc.collection.objects.link(ob); objs.append(ob)
    cam = render_util.setup_stage()
    for o in bpy.data.objects:
        if o.name == "Floor": o.scale = (6, 6, 1)
    sc.render.resolution_x, sc.render.resolution_y = 360, 540
    views = {"front": Vector((1.6, -3.6, 1.2)), "side": Vector((4.0, 0.0, 1.0))}
    if os.environ.get("TANTRA_FACE"):
        hdr, frames = read_clip(os.path.join(O, "Config", "Tantra", CLIPS, "idle.clip")); fr = frames[0]
        base = [g['V'].copy() for g in groups]
        sc.render.resolution_x, sc.render.resolution_y = 500, 500
        hb = NAMES.index("Head")
        for state, w in (("open", 0.0), ("half", 0.5), ("closed", 1.0)):
            for g, b in zip(groups, base): g['V'] = b.copy()
            for nm_, gi, idx, d in MORPHS:
                if nm_ == 'blink': groups[gi]['V'][idx, :6] += w * d
            P = skinned(bones, W, groups, fr)
            for ob, p in zip(objs, P): ob.data.vertices.foreach_set("co", to_blender(p).ravel()); ob.data.update()
            hp = Vector(to_blender(fr[hb:hb + 1, 4:7])[0]) + Vector((0, 0, 0.08))
            cam.location = hp + Vector((0.12, -0.38, 0.02)); cam.data.lens = 60
            cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (hp - cam.location).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "face_%s.png" % state); bpy.ops.render.render(write_still=True)
        raise SystemExit
    HANDS = os.environ.get("TANTRA_HANDS")
    if HANDS:   # close-up of both hands in one run frame
        hclip = os.environ.get("TANTRA_HANDCLIP", "run")
        hdr, frames = read_clip(os.path.join(O, "Config", "Tantra", CLIPS, hclip + ".clip"))
        fr = frames[int(HANDS) % len(frames)]
        if VARIANT == "coverall": fr = coverall_layer(fr, 1.0 if hclip == "run" else 0.0)
        for nm_, gi, idx, d in MORPHS:
            if nm_ == 'fist' and hclip == "run": groups[gi]['V'][idx, :6] += d
        P = skinned(bones, W, groups, fr)
        for ob, p in zip(objs, P): ob.data.vertices.foreach_set("co", to_blender(p).ravel()); ob.data.update()
        sc.render.resolution_x, sc.render.resolution_y = 500, 500
        for side, bi in (("L", 12), ("R", 22)):   # LeftHand / RightHand bone indices
            hp = to_blender(fr[bi:bi + 1, 4:7])[0]; tgt = Vector(hp)
            for vn, off in (("front", Vector((0, -0.55, 0.05))), ("out", Vector((0.55 if side == "L" else -0.55, 0, 0.05)))):
                cam.location = tgt + off; cam.data.lens = 50
                cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (tgt - cam.location).to_track_quat('-Z', 'Y')
                sc.render.filepath = os.path.join(OUT, "hand_%s_%s.png" % (side, vn)); bpy.ops.render.render(write_still=True)
        raise SystemExit
    n = 0
    bind = [g['V'].copy() for g in groups]
    for clip in ("walk", "run"):
        hdr, frames = read_clip(os.path.join(O, "Config", "Tantra", CLIPS, clip + ".clip"))
        for g, b in zip(groups, bind): g['V'] = b.copy()
        if clip == "run":   # the runtime closes the hands while running
            for nm_, gi, idx, d in MORPHS:
                if nm_ == 'fist': groups[gi]['V'][idx, :6] += d
        for k, fr in enumerate(frames):
            if VARIANT == "coverall": fr = coverall_layer(fr, 1.0 if clip == "run" else 0.0)
            P = skinned(bones, W, groups, fr)
            for ob, p in zip(objs, P):
                ob.data.vertices.foreach_set("co", to_blender(p).ravel()); ob.data.update()
            for vn, loc in views.items():
                cam.location = loc; cam.data.lens = 50
                cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (Vector((0, 0, 0.9)) - loc).to_track_quat('-Z', 'Y')
                sc.render.filepath = os.path.join(OUT, "%s_%s_%02d.png" % (clip, vn, k)); bpy.ops.render.render(write_still=True); n += 1
    log.append("rendered %d frames" % n)
except SystemExit:
    log.append('hands rendered')
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
