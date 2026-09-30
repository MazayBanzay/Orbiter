import bpy, os, math
HERE = os.path.dirname(__file__); log = []
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, os.environ.get("PROBE_BLEND","astronavigator_coverall.blend")))
for o in bpy.data.objects:
    if o.type != 'MESH': continue
    zs = [(o.matrix_world @ v.co) for v in o.data.vertices]
    log.append("%-40s verts %6d  z %.3f..%.3f" % (o.name, len(zs), min(p.z for p in zs), max(p.z for p in zs)))
    if 'shoe' in o.name.lower() or o.name == 'Coverall':
        for z0 in (0.06, 0.09, 0.12, 0.15, 0.18, 0.21, 0.24):
            ring = [p for p in zs if abs(p.z - z0) < 0.01 and p.x > 0]
            if ring:
                cx = sum(p.x for p in ring) / len(ring); cy = sum(p.y for p in ring) / len(ring)
                r = max(math.hypot(p.x - cx, p.y - cy) for p in ring)
                log.append("   z %.2f: n %d  max radius %.3f" % (z0, len(ring), r))
open(os.path.join(HERE, "probe_boots.log"), "w").write("\n".join(log))
