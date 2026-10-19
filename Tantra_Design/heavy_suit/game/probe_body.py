# Pantsir game asset: measure the person's body in the rest pose (read only): cross-sections per segment
import bpy, os, math, traceback
import numpy as np
from mathutils import Vector as V
HERE = os.path.dirname(os.path.abspath(__file__)); OUT = os.path.join(HERE, "probe_body.log"); L = []
try:
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'REST'
    body = bpy.data.objects["Astronavigator.body"]
    for m in body.modifiers:
        if m.type == 'MASK': m.show_viewport = False
    body.hide_viewport = False; bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get(); ev = body.evaluated_get(dg); me = ev.to_mesh()
    P = np.array([body.matrix_world @ v.co for v in me.vertices]); L.append("body verts %d bbox min %s max %s" % (len(P), P.min(0).round(3), P.max(0).round(3)))
    names = {g.index: g.name for g in body.vertex_groups}
    dom = []
    for v in body.data.vertices:
        best = max(v.groups, key=lambda g: g.weight, default=None); dom.append(names[best.group] if best else "-")
    dom = np.array(dom); uniq, cnt = np.unique(dom, return_counts=True)
    keep = ~np.isin(dom, ["HelperGeometry", "JointCubes"]); P = P[keep]; dom = dom[keep]
    def segd(Q, a, b):
        d = b - a; t = np.clip(((Q - a) @ d) / (d @ d), 0, 1); return np.linalg.norm(Q - (a + np.outer(t, d)), axis=1)
    armpts = []
    for sd in ("Left", "Right"):
        for n0, n1 in (("Arm", "ForeArm"), ("ForeArm", "Hand"), ("Hand", "HandFinger1")):
            bb = arm.data.bones; a_ = np.array(bb[sd + n0].head_local); b_ = np.array(bb[sd + n1].tail_local if n1 == "HandFinger1" else bb[sd + n1].head_local); armpts.append((a_, b_))
    ARMD = np.min([segd(P, a_, b_) for a_, b_ in armpts], axis=0)
    L.append("dominant groups: " + ", ".join("%s:%d" % (u, c) for u, c in zip(uniq, cnt)))
    def bonept(n, tail=False):
        b = arm.data.bones[n]; return np.array(arm.matrix_world @ (b.tail_local if tail else b.head_local))
    # sections perpendicular to a bone: half extents along lateral (x-ish) and depth (y-ish) axes
    def section(sel, a, b, t, half=0.012):
        d = (b - a) / np.linalg.norm(b - a); c = a + (b - a) * t
        Q = P[sel]; s = (Q - c) @ d; Q = Q[np.abs(s) < half]
        if len(Q) < 5: return None
        ex = np.cross(d, [0, 1, 0]); ex /= np.linalg.norm(ex); ey = np.cross(ex, d)     # ex ~ lateral, ey ~ depth (front = -y)
        u = (Q - c) @ ex; w = (Q - c) @ ey
        return (u.min(), u.max(), w.min(), w.max(), len(Q))
    groups_of = lambda keys: np.isin(dom, [k for k in uniq if any(k.startswith(x) for x in keys)])
    for tag, bone, child, keys in (("thigh", "LeftUpLeg", "LeftLeg", ["LeftUpLeg", "thigh.L", "upperleg01.L", "upperleg02.L"]),
                                    ("shin", "LeftLeg", "LeftFoot", ["LeftLeg", "shin.L", "lowerleg01.L", "lowerleg02.L"]),
                                    ("upperarm", "LeftArm", "LeftForeArm", ["LeftArm", "upperarm01.L", "upperarm02.L"]),
                                    ("forearm", "LeftForeArm", "LeftHand", ["LeftForeArm", "lowerarm01.L", "lowerarm02.L"])):
        a, b = bonept(bone), bonept(child); sel = groups_of(keys)
        if sel.sum() < 20:                                                       # fall back: everything near the bone
            d = (b - a) / np.linalg.norm(b - a); s = (P - a) @ d; r = np.linalg.norm((P - a) - np.outer(s, d), axis=1)
            sel = (s > -0.05) & (s < np.linalg.norm(b - a) + 0.05) & (r < 0.12) & (P[:, 0] > 0.0)
        L.append("%s: bone %s len %.3f, %d verts" % (tag, bone, np.linalg.norm(b - a), sel.sum()))
        for t in (0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0):
            r = section(sel, a, b, t)
            if r: L.append("   t=%.2f lat %.3f..%.3f (w %.3f)  depth %.3f..%.3f (d %.3f)  n=%d" % (t, r[0], r[1], r[1] - r[0], r[2], r[3], r[3] - r[2], r[4]))
    # torso / head horizontal sections (exclude arms: |x| inside the armpits)
    for z in (0.80, 0.85, 0.90, 0.95, 1.00, 1.05, 1.10, 1.20, 1.30, 1.35, 1.40, 1.45, 1.50, 1.55, 1.60, 1.65, 1.70, 1.74):
        Q = P[np.abs(P[:, 2] - z) < 0.008]
        Q = P[(np.abs(P[:, 2] - z) < 0.008) & (ARMD > 0.075)]
        if len(Q): L.append("z=%.2f x %.3f..%.3f (w %.3f)  y %.3f..%.3f (d %.3f)  n=%d" % (z, Q[:, 0].min(), Q[:, 0].max(), np.ptp(Q[:, 0]), Q[:, 1].min(), Q[:, 1].max(), np.ptp(Q[:, 1]), len(Q)))
    # inner thigh gap: the legs' innermost x at heights below the crotch
    for z in (0.70, 0.75, 0.78, 0.80, 0.82):
        Q = P[(np.abs(P[:, 2] - z) < 0.006) & (P[:, 0] > 0)]
        if len(Q): L.append("crotch z=%.2f left leg inner x min %.3f" % (z, Q[:, 0].min()))
    # foot
    Q = P[(P[:, 2] < 0.12) & (P[:, 0] > 0)]
    L.append("left foot x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f" % (Q[:, 0].min(), Q[:, 0].max(), Q[:, 1].min(), Q[:, 1].max(), Q[:, 2].min(), Q[:, 2].max()))
    vg = [g.name for g in body.vertex_groups]; L.append("vertex groups (%d): %s" % (len(vg), ", ".join(vg[:60])))
    for n in ("Helmet", "NeckSeal", "Cuirass", "BootL"):
        o = bpy.data.objects.get(n)
        if o: L.append("old %s groups: %s" % (n, [g.name for g in o.vertex_groups]))
except Exception:
    L.append(traceback.format_exc())
open(OUT, "w", encoding="utf-8").write("\n".join(L) + "\n")
bpy.ops.wm.quit_blender()
