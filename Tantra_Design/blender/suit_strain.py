# SKIN STRAIN AND RELATIVE MOTION of her suit (SuitBody) over a range-of-motion test set - the basis for cutting the chest
# protection into rigid segments (the user, 2026-10-18: «пластины - изменяемые... анатомический костюм... анализ и
# исследование»). The principle: rigid parts on skin that does not stretch (lines of non-extension, Iberall / BioSuit),
# seams where it stretches; each rigid segment carried by the bone under it, its clearance = how far her skin moves
# towards it relative to that bone.
# Poses on her own rig (the game's skinning): arms forward 90, overhead 160, sideways 90, across the chest, trunk bend 45,
# twist 36, side bend 24, a lift (bend + arms forward). Breathing is not in the rig: +10 mm (chest circumference +6 cm).
#   blender -b astronavigator_suit.blend --python suit_strain.py  (env SS_OUT) -> ss_*.png, ss_report.txt, ss_data.json
import bpy, bmesh, math, os, json, traceback
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(bpy.data.filepath))
OUT = os.environ.get("SS_OUT") or os.path.join(HERE, "..", "suit_mockups", "strain"); os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, "ss_log.txt"), "w", encoding="utf-8")
sc = bpy.context.scene
try:
    arm = bpy.data.objects["Astronavigator"]; arm.data.pose_position = 'POSE'
    body = bpy.data.objects["SuitBody"]; me0 = body.data
    def reset():
        for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
        bpy.context.view_layer.update()
    def rot(bone, axis, deg):
        """turn the bone about a world axis through its head (armature at the origin, unrotated)"""
        pb = arm.pose.bones[bone]; h = pb.head.copy()
        R = Matrix.Rotation(math.radians(deg), 4, Vector(axis))
        pb.matrix = Matrix.Translation(h) @ R @ Matrix.Translation(-h) @ pb.matrix; bpy.context.view_layer.update()
    def arms(fn):
        for side, sg in (("Left", 1), ("Right", -1)): fn(side, sg)
    def p_fwd90(): arms(lambda s, g: rot(s + "Arm", (1, 0, 0), -90))
    def p_up160(): arms(lambda s, g: (rot(s + "Shoulder", (0, 1, 0), -g * 12), rot(s + "Arm", (1, 0, 0), -150)))
    def p_side90(): arms(lambda s, g: rot(s + "Arm", (0, 1, 0), -g * 45))
    def p_across(): arms(lambda s, g: (rot(s + "Arm", (1, 0, 0), -90), rot(s + "Arm", (0, 0, 1), g * 40)))
    def p_bend(): [rot(b, (1, 0, 0), 15) for b in ("LowerBack", "Spine", "Spine1")]
    def p_twist(): [rot(b, (0, 0, 1), 12) for b in ("LowerBack", "Spine", "Spine1")]
    def p_side(): [rot(b, (0, 1, 0), 8) for b in ("LowerBack", "Spine", "Spine1")]
    def p_lift(): p_bend(); arms(lambda s, g: rot(s + "Arm", (1, 0, 0), -60))
    POSES = [("руки вперёд 90°", p_fwd90), ("руки вверх 160°", p_up160), ("руки в стороны 90°", p_side90), ("руки скрестно", p_across),
             ("наклон вперёд 45°", p_bend), ("поворот 36°", p_twist), ("наклон вбок 24°", p_side), ("подъём груза", p_lift)]
    CARRIERS = ["Spine1", "Spine", "LowerBack"]

    reset(); dg = bpy.context.evaluated_depsgraph_get()
    def verts():
        e = body.evaluated_get(bpy.context.evaluated_depsgraph_get()); m = e.to_mesh(); vs = [body.matrix_world @ v.co for v in m.vertices]; e.to_mesh_clear(); return vs
    V0 = verts(); E = [tuple(e.vertices) for e in me0.edges]; L0 = [(V0[a] - V0[b]).length for a, b in E]
    REST_M = {c: (arm.matrix_world @ arm.pose.bones[c].matrix).copy() for c in CARRIERS}
    nv = len(V0); stretch = [0.0] * nv; compress = [0.0] * nv
    move = {c: [0.0] * nv for c in CARRIERS}; toward = {c: [0.0] * nv for c in CARRIERS}
    per_pose = []
    for title, fn in POSES:
        reset(); fn(); V = verts()
        ps = [0.0] * nv
        for (a, b), l0 in zip(E, L0):
            if l0 < 1e-6: continue
            r = (V[a] - V[b]).length / l0 - 1.0
            for k in (a, b):
                if r > stretch[k]: stretch[k] = r
                if -r > compress[k]: compress[k] = -r
                if abs(r) > ps[k]: ps[k] = abs(r)
        for c in CARRIERS:                                   # her skin seen from the bone that would carry a plate
            M = arm.matrix_world @ arm.pose.bones[c].matrix; Rm = REST_M[c] @ M.inverted()
            for i in range(nv):
                d = (Rm @ V[i]) - V0[i]; move[c][i] = max(move[c][i], d.length)
                toward[c][i] = max(toward[c][i], -d.y)       # forward (-y) = into a plate in front
        per_pose.append((title, ps)); print("pose", title, file=LOG); LOG.flush()
    reset(); arm.data.pose_position = 'REST'; bpy.context.view_layer.update()

    # ---- the report: the chest and abdomen front, by height and width ----
    rep = ["max strain (stretch / compress) and skin motion relative to the carrier bone, front (y < -0.06), per cell z x |x|"]
    cells = {}
    for i, p in enumerate(V0):
        if p.y > -0.06 or p.z < 0.98 or p.z > 1.47 or abs(p.x) > 0.17: continue
        key = (round(p.z / 0.03) * 0.03, round(abs(p.x) / 0.04) * 0.04)
        car = "Spine1" if p.z > 1.30 else ("Spine" if p.z > 1.16 else "LowerBack")
        c = cells.setdefault(key, [0, 0, 0, 0, car]); c[0] = max(c[0], stretch[i]); c[1] = max(c[1], compress[i]); c[2] = max(c[2], move[car][i]); c[3] = max(c[3], toward[car][i])
    for (z, x), (s, cpr, mv, tw, car) in sorted(cells.items(), key=lambda k: (-k[0][0], k[0][1])):
        rep.append("z %.2f |x| %.2f  stretch %4.1f%%  compress %4.1f%%  move %4.0f mm  toward %4.0f mm  (%s)" % (z, x, s * 100, cpr * 100, mv * 1000, tw * 1000, car))
    rep.append(""); rep.append("worst pose per region (front chest z 1.22-1.44 |x|<0.15):")
    for title, ps in per_pose:
        vals = [ps[i] for i, p in enumerate(V0) if p.y < -0.06 and 1.22 < p.z < 1.44 and abs(p.x) < 0.15]
        rep.append("  %-22s max %4.1f%%  mean %4.1f%%" % (title, max(vals) * 100, sum(vals) / len(vals) * 100))
    open(os.path.join(OUT, "ss_report.txt"), "w", encoding="utf-8").write("\n".join(rep))
    json.dump({"stretch": stretch, "compress": compress, "move": move, "toward": toward}, open(os.path.join(OUT, "ss_data.json"), "w"))

    # ---- the maps: a copy of her suit coloured per vertex ----
    for o in sc.objects:
        if o.type in ('MESH', 'CURVE') and o is not body: o.hide_render = True
    body.hide_render = True
    def ramp(t):                       # 0 blue -> green -> yellow -> red at 1
        t = max(0.0, min(1.0, t)); pts = [(0, (0.15, 0.25, 0.85)), (0.33, (0.15, 0.75, 0.35)), (0.66, (0.95, 0.85, 0.15)), (1.0, (0.90, 0.12, 0.08))]
        for (a, ca), (b, cb) in zip(pts, pts[1:]):
            if t <= b: w = (t - a) / (b - a); return tuple(ca[k] + (cb[k] - ca[k]) * w for k in range(3))
        return pts[-1][1]
    def heat(name, vals, top):
        me = bpy.data.meshes.new(name); bm = bmesh.new(); bm.from_mesh(me0); bm.to_mesh(me); bm.free()
        for v, p in zip(me.vertices, V0): v.co = p
        ca = me.color_attributes.new("heat", 'FLOAT_COLOR', 'POINT')
        for i, v in enumerate(vals): ca.data[i].color = (*ramp(v / top), 1)
        o = bpy.data.objects.new(name, me); sc.collection.objects.link(o); return o
    sc.render.engine = 'BLENDER_WORKBENCH'; sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'VERTEX'
    sh.show_shadows = False; sh.show_cavity = True; sc.render.resolution_x, sc.render.resolution_y = 700, 900
    cd = bpy.data.cameras.new("SS_cam"); cd.lens = 60; cam = bpy.data.objects.new("SS_cam", cd); sc.collection.objects.link(cam); sc.camera = cam
    T = Vector((0, -0.05, 1.20))
    MAPS = [("stretch", stretch, 0.25), ("compress", compress, 0.25), ("move_Spine1", move["Spine1"], 0.06), ("toward_Spine1", toward["Spine1"], 0.03),
            ("toward_Spine", toward["Spine"], 0.03)]
    for nm, vals, top in MAPS:
        o = heat("ss_" + nm, vals, top)
        for vn, loc in (("front", T + Vector((0, -1.9, 0.10))), ("side", T + Vector((1.9, -0.25, 0.08)))):
            cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (T - loc).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "ss_%s_%s.png" % (nm, vn)); bpy.ops.render.render(write_still=True)
        bpy.data.objects.remove(o, do_unlink=True)
    print("DONE", file=LOG)
except Exception:
    traceback.print_exc(file=LOG); print("FAILED", file=LOG)
LOG.close()
