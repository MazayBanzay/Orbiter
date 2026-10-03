import bpy, os
HERE = os.path.dirname(__file__)
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_body.blend"))
L = []
for o in bpy.data.objects:
    if o.type != 'MESH': continue
    for s in o.material_slots:
        m = s.material
        if not m or not m.use_nodes: L.append("%s: %s (no nodes)" % (o.name, m and m.name)); continue
        imgs = [(n.name, n.image.name if n.image else None, n.image.filepath if n.image else None) for n in m.node_tree.nodes if n.type == 'TEX_IMAGE']
        L.append("%s | %s | nodes %s | imgs %s" % (o.name, m.name, sorted({n.type for n in m.node_tree.nodes}), imgs))
open(os.path.join(HERE, "probe_mats.log"), "w").write("\n".join(L))
