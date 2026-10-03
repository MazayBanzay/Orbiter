# Flight-suit variants for the user to choose from (2026-10-02): a looser fit and colour schemes, PREVIEW ONLY.
# Loads astronavigator_coverall.blend, inflates a copy of the coverall per variant (ease by region along the normals,
# then hollows filled outwards only - the cloth spans them), recolours the fabric texture, adds pocket patches,
# renders three-quarter and side views. Writes renders/var_*.png. Changes no game file.
import bpy, bmesh, os, sys, math
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); sys.path.insert(0, HERE); import render_util
OUT = os.path.join(HERE, "renders"); LOG = os.path.join(HERE, "preview_variants.log"); log = []

def s01(e0, e1, x): t = min(max((x - e0) / (e1 - e0), 0.0), 1.0); return t * t * (3 - 2 * t)

# name, ease (m), fabric rgb, panel rgb, pockets
VARIANTS = [
    ("A_regular_light", 0.025, (0.86, 0.85, 0.82), (0.17, 0.21, 0.30), False),
    ("B_relaxed_light", 0.050, (0.86, 0.85, 0.82), (0.17, 0.21, 0.30), False),
    ("A_sage_pockets", 0.025, (0.56, 0.58, 0.50), (0.27, 0.30, 0.25), True),
    ("A_steel_pockets", 0.025, (0.52, 0.56, 0.60), (0.18, 0.20, 0.24), True),
]

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_coverall.blend"))
    cov0 = bpy.data.objects["Coverall"]
    src_img = next(n.image for n in cov0.data.materials[0].node_tree.nodes if n.type == 'TEX_IMAGE')
    W, H_ = src_img.size; px0 = np.array(src_img.pixels[:], np.float32).reshape(H_, W, 4)
    lum = px0[..., :3].mean(2); blue = (px0[..., 2] - px0[..., 0]) > 0.06
    zipm = lum < 0.2
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x, sc.render.resolution_y = 600, 900

    def inflate(ob, ease):
        bm = bmesh.new(); bm.from_mesh(ob.data); bm.normal_update()
        body_faces = [f for f in bm.faces if f.material_index == 0]
        vs = {v for f in body_faces for v in f.verts}
        E = {}
        for v in vs:
            x, y, z = v.co; ax = abs(x)
            sleeve = ax > 0.19 and z > 0.85
            e = ease * (0.75 if sleeve else 1.0)
            e *= s01(1.47, 1.40, z)                                   # collar stays
            e *= 1 - 0.65 * s01(0.035, 0.0, abs(z - 1.03))           # waist band gathers
            if sleeve:                                                # cuff gathers near the wrist
                e *= 1 - 0.7 * s01(0.42, 0.52, ax) if z > 1.0 else 1 - 0.7 * s01(0.95, 0.88, z)
            if z < 0.95 and not sleeve and ax < 0.06: e *= 0.5        # crotch: no ballooning between the legs
            E[v] = e
        for v, e in E.items(): v.co += v.normal * e
        # fill the hollows outwards only (a cloth spans them): not between the legs or under the arms
        zone = [v for v in vs if not (v.co.z < 0.92 and abs(v.co.x) < 0.07) and not (abs(v.co.x) > 0.17 and 1.15 < v.co.z < 1.40 and abs(v.co.y) < 0.06)]
        for it in range(int(60 + 4000 * ease)):
            if it % 10 == 0: bm.normal_update()
            nw = []
            for v in zone:
                if not v.link_edges: continue
                c = sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges); d = (c - v.co).dot(v.normal)
                if d > 0: nw.append((v, v.co + v.normal * d * 0.8))
            for v, co in nw: v.co = co
        # a few plain smoothing passes take the body's small relief out of the cloth
        for _ in range(4):
            nw = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.3)) for v in zone if v.link_edges]
            for v, co in nw: v.co = co
        bm.to_mesh(ob.data); bm.free(); ob.data.update()

    def pocket(name, target, center, normal_dir, w, h, mat):
        me = bpy.data.meshes.new(name); bm = bmesh.new()
        bmesh.ops.create_grid(bm, x_segments=8, y_segments=8, size=0.5); bm.to_mesh(me); bm.free()
        o = bpy.data.objects.new(name, me); sc.collection.objects.link(o)
        o.scale = (w, h, 1); o.location = Vector(center) + Vector(normal_dir).normalized() * 0.08
        o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Vector(normal_dir).to_track_quat('Z', 'Y')
        sw = o.modifiers.new("sw", 'SHRINKWRAP'); sw.target = target; sw.wrap_method = 'PROJECT'; sw.use_project_z = True; sw.use_negative_direction = True; sw.use_positive_direction = True; sw.offset = 0.004
        so = o.modifiers.new("so", 'SOLIDIFY'); so.thickness = 0.006
        me.materials.append(mat); return o

    for name, ease, fab, pan, pockets in VARIANTS:
        ob = cov0.copy(); ob.data = cov0.data.copy(); ob.name = "V_" + name; sc.collection.objects.link(ob)
        for m in list(ob.modifiers):
            if m.type == 'ARMATURE': ob.modifiers.remove(m)
        inflate(ob, ease)
        px = px0.copy(); f = np.array(fab, np.float32); p = np.array(pan, np.float32)
        fabm = ~blue & ~zipm
        px[fabm, :3] = px0[fabm, :3] / np.array([0.88, 0.87, 0.84], np.float32) * f
        px[blue, :3] = px0[blue, :3] / np.array([0.17, 0.21, 0.30], np.float32) * p
        img = bpy.data.images.new("tex_" + name, W, H_); img.pixels.foreach_set(px.ravel())
        mats = []
        for i, m0 in enumerate(ob.data.materials):
            m = m0.copy(); mats.append(m)
            for n in m.node_tree.nodes:
                if n.type == 'TEX_IMAGE': n.image = img
                if n.type == 'BSDF_PRINCIPLED':
                    n.inputs["Roughness"].default_value = 0.92                      # matte twill
                    if "Specular IOR Level" in n.inputs: n.inputs["Specular IOR Level"].default_value = 0.2
                    if i > 0: n.inputs["Base Color"].default_value = (*(f if i == 1 else p), 1)
        for i, m in enumerate(mats): ob.data.materials[i] = m
        pk = []
        if pockets:
            pm = mats[0].copy()
            for n in pm.node_tree.nodes:
                if n.type == 'TEX_IMAGE': pm.node_tree.nodes.remove(n)
            next(n for n in pm.node_tree.nodes if n.type == 'BSDF_PRINCIPLED').inputs["Base Color"].default_value = (*(p * 1.15), 1)
            for sd in (-1, 1):
                pk.append(pocket("pk%d" % sd, ob, (sd * 0.17, -0.06, 0.70), (sd * 0.8, -0.6, 0), 0.13, 0.16, pm))
            pk.append(pocket("pks", ob, (0.24, -0.02, 1.22), (0.9, -0.3, 0), 0.07, 0.09, pm))
        cov0.hide_render = True; ob.hide_render = False
        for o in bpy.data.objects:
            if o.name.startswith("V_") and o != ob: o.hide_render = True
        for view, loc, tz in (("tq", (2.2, -3.4, 1.15), 0.92), ("side", (3.9, -0.15, 1.05), 0.92)):
            cam.location = loc; cam.data.lens = 60; cam.rotation_mode = 'QUATERNION'
            cam.rotation_quaternion = (Vector((0, 0, tz)) - Vector(loc)).to_track_quat('-Z', 'Y')
            sc.render.filepath = os.path.join(OUT, "var_%s_%s.png" % (name, view)); bpy.ops.render.render(write_still=True)
        for o in pk: bpy.data.objects.remove(o, do_unlink=True)
        log.append("rendered " + name)
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
