# measure the body's bust against anatomical references (no changes)
import bpy, os, sys, math
import numpy as np
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE)
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
L = []
for name in ("Astronavigator.body", "Coverall"):
    o = bpy.data.objects[name]; M = o.matrix_world
    if name == "Coverall":
        V = np.array([M @ v.co for v in o.data.vertices])
    else:
        g = o.vertex_groups['body'].index
        V = np.array([M @ v.co for v in o.data.vertices if any(x.group == g and x.weight > 0 for x in v.groups)])
    zs = V[:, 2]; L.append("%s: height %.3f (min %.3f max %.3f)" % (name, zs.max() - zs.min(), zs.min(), zs.max()))
    for sd in (-1, 1):
        m = (sd * V[:, 0] > 0.03) & (sd * V[:, 0] < 0.16) & (V[:, 2] > 1.10) & (V[:, 2] < 1.40) & (V[:, 1] < 0)
        a = V[m][np.argmin(V[m][:, 1])]; L.append("  apex %+d: x %.3f y %.3f z %.3f" % (sd, *a))
    a = V[(V[:, 0] > 0.03) & (V[:, 0] < 0.16) & (V[:, 2] > 1.10) & (V[:, 2] < 1.40) & (V[:, 1] < 0)]; a = a[np.argmin(a[:, 1])]
    prof = []
    for z in np.arange(1.10, 1.46, 0.01):
        m = (np.abs(V[:, 0] - a[0]) < 0.008) & (np.abs(V[:, 2] - z) < 0.005) & (V[:, 1] < 0)
        if m.any(): prof.append((z, V[m][:, 1].min()))
    L.append("  profile through apex x %.3f: " % a[0] + " ".join("%.2f:%.3f" % q for q in prof))
    # sternum profile at x~0: front surface y per z
    for z in np.arange(1.05, 1.50, 0.02):
        m = (np.abs(V[:, 0]) < 0.012) & (np.abs(V[:, 2] - z) < 0.006) & (V[:, 1] < 0)
        ax = 0.105
        m2 = (np.abs(np.abs(V[:, 0]) - ax) < 0.008) & (np.abs(V[:, 2] - z) < 0.006) & (V[:, 1] < 0)
        L.append("  z %.2f  mid y %s   x=%.3f y %s" % (z, "%.3f" % V[m][:, 1].min() if m.any() else "-", ax, "%.3f" % V[m2][:, 1].min() if m2.any() else "-"))
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
for b in ("Neck", "Spine1", "Spine", "LeftShoulder", "LeftArm", "Hips"):
    if b in arm.data.bones: L.append("bone %s head z %.3f x %.3f" % (b, (arm.matrix_world @ arm.data.bones[b].head_local).z, (arm.matrix_world @ arm.data.bones[b].head_local).x))
open(os.path.join(HERE, "probe_bust.log"), "w").write("\n".join(L))
