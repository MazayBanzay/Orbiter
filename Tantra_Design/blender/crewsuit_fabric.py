# Crew suit fabric for the game (2026-10-02): a fine hexagonal cell weave over the whole suit and a normal map.
# Runs on astronavigator_crewsuit.blend (concept_cut.py's output). The garment gets a fresh UV without overlaps at one
# texel density; into it go
#   - the asset's own normal map (its seams and folds), baked across from the asset's UV,
#   - a hexagonal cell relief computed in the UV's own space (regular hexagons everywhere, HEX_MM across),
#   - the colour textures: white and red, each with a faint shading in the cell grooves.
# The two materials (white, red) then carry textures, so export_skin.py writes Coverall_SuitWhite/Red(.dds, _norm.dds).
import bpy, bmesh, os, sys, math
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); TEXD = os.path.join(HERE, "textures")
LOG = os.path.join(HERE, "crewsuit_fabric.log"); log = ["started"]
open(LOG, "w").write("started")
def mark(t):
    log.append(t); open(LOG, "w").write(chr(10).join(log))
TEX = 4096
VAR = os.environ.get('FAB_VAR', '')   # preview variant tag (no save of the blend)
LINE_TX = float(os.environ.get('FAB_LINE', '0.55'))                 # thread line half-width, texels (~0.9 mm at 0.75 mm/texel)
HEX_MM = float(os.environ.get('FAB_HEX', '7.0'))                  # cell size across flats
GROOVE = float(os.environ.get('FAB_GROOVE', '0.00012'))              # m: depth of the thread lines between the cells
WHITE, RED = (0.80, 0.80, 0.78), (0.018, 0.019, 0.022)    # linear, as the cut concept
A_NORM = os.path.join(HERE, "..", "mpfbu", "data", "clothes", "elvs_racing_fire_suit_female1", "femnormals1.png")

try:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
    cov = bpy.data.objects["Coverall"]; me = cov.data
    old_uv = me.uv_layers.active.name
    mark('opened')
    # ---- a fresh UV, one texel density ----
    for o in bpy.context.view_layer.objects: o.select_set(False)
    bpy.context.view_layer.objects.active = cov; cov.select_set(True)
    src = cov.copy(); src.data = me.copy(); src.name = "NormSrc"; bpy.context.scene.collection.objects.link(src)
    for m in list(src.modifiers): src.modifiers.remove(m)
    for m in list(cov.modifiers):
        if m.type == 'ARMATURE': m.show_render = m.show_viewport = False     # bake in the rest pose
    fab = me.uv_layers.new(name="Fab"); me.uv_layers.active = fab
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.003, scale_to_bounds=False)
    bpy.ops.uv.pack_islands(margin=0.003, rotate=True)
    bpy.ops.object.mode_set(mode='OBJECT')
    mark('uv done')
    # metres per UV unit (the median over faces: smart project keeps the scale uniform)
    fab = me.uv_layers["Fab"]   # (the edit-mode round trip reallocated the layers: the old reference crashed Blender)
    me.calc_loop_triangles(); uvd = fab.data; M = cov.matrix_world
    k = []
    for t in list(me.loop_triangles)[::7]:
        a3 = t.area
        u = [uvd[l].uv for l in t.loops]; a2 = abs((u[1] - u[0]).cross(u[2] - u[0])) / 2
        if a2 > 1e-12: k.append(math.sqrt(a3 / a2))
    m_per_uv = float(np.median(k)); texel = m_per_uv / TEX
    mark('scale done')
    log.append("UV: %.3f m per unit, texel %.2f mm" % (m_per_uv, texel * 1000))
    # ---- bake the asset's normal map across (emission of the source's image into the new UV) ----
    img_a = bpy.data.images.load(A_NORM); img_a.colorspace_settings.name = 'Non-Color'
    ms = bpy.data.materials.new("NS"); ms.use_nodes = True; nt = ms.node_tree
    for n in list(nt.nodes): nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial"); em = nt.nodes.new("ShaderNodeEmission"); ti = nt.nodes.new("ShaderNodeTexImage"); ti.image = img_a
    uvn = nt.nodes.new("ShaderNodeUVMap"); uvn.uv_map = old_uv; nt.links.new(uvn.outputs[0], ti.inputs[0])
    nt.links.new(ti.outputs[0], em.inputs[0]); nt.links.new(em.outputs[0], out.inputs[0])
    src.data.materials.clear(); src.data.materials.append(ms)
    for p in src.data.polygons: p.material_index = 0
    baked = bpy.data.images.new("asset_n", TEX, TEX, alpha=False); baked.colorspace_settings.name = 'Non-Color'
    keep_mats = [m for m in me.materials]; keep_idx = [p.material_index for p in me.polygons]
    mt = bpy.data.materials.new("ND"); mt.use_nodes = True; tb = mt.node_tree.nodes.new("ShaderNodeTexImage"); tb.image = baked; mt.node_tree.nodes.active = tb
    me.materials.clear(); me.materials.append(mt)
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'; sc.cycles.samples = 1; sc.cycles.device = 'CPU'
    sc.render.bake.use_selected_to_active = True; sc.render.bake.cage_extrusion = 0.002; sc.render.bake.max_ray_distance = 0.005; sc.render.bake.margin = 16
    for o in bpy.context.view_layer.objects: o.select_set(False)
    src.select_set(True); cov.select_set(True); bpy.context.view_layer.objects.active = cov
    bpy.ops.object.bake(type='EMIT', use_selected_to_active=True)
    mark('baked')
    an = np.array(baked.pixels[:], np.float32).reshape(TEX, TEX, 4)[..., :3] * 2 - 1
    bpy.data.objects.remove(src, do_unlink=True)
    me.materials.clear()
    for m in keep_mats: me.materials.append(m)
    for p, i in zip(me.polygons, keep_idx): p.material_index = i      # (clearing the slots reset the indices)
    # coverage mask (where the UV has faces), rasterized from the UV triangles
    cover = np.zeros((TEX, TEX), bool); redmap = np.zeros((TEX, TEX), bool)
    for t in me.loop_triangles:
        u = np.array([uvd[l].uv for l in t.loops]) * TEX
        x0, y0 = np.floor(u.min(0)).astype(int); x1, y1 = np.ceil(u.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, TEX - 1), min(y1, TEX - 1)
        if x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        a_, b_, c_ = u; den = (b_[1] - c_[1]) * (a_[0] - c_[0]) + (c_[0] - b_[0]) * (a_[1] - c_[1])
        if abs(den) < 1e-12: continue
        w0 = ((b_[1] - c_[1]) * (gx - c_[0]) + (c_[0] - b_[0]) * (gy - c_[1])) / den
        w1 = ((c_[1] - a_[1]) * (gx - c_[0]) + (a_[0] - c_[0]) * (gy - c_[1])) / den
        m = (w0 >= -0.01) & (w1 >= -0.01) & (1 - w0 - w1 >= -0.01)
        iy_, ix_ = (gy[m] - 0.5).astype(int), (gx[m] - 0.5).astype(int); cover[iy_, ix_] = True
        if me.polygons[t.polygon_index].material_index == 1: redmap[iy_, ix_] = True
    # ---- hexagonal cells in the UV space: distance to the nearest cell edge -> a groove along the edges ----
    s = HEX_MM / 1000 / texel                                  # cell across flats, in texels
    yy, xx = np.mgrid[0:TEX, 0:TEX].astype(np.float32) + 0.5
    # hex lattice: centres on a triangular grid; distance to the nearest centre in the hex metric
    q = (xx * (2 / 3)) / (s / math.sqrt(3)); r_ = (-xx / 3 + math.sqrt(3) / 3 * yy) / (s / math.sqrt(3))
    # cube rounding
    X, Z = q, r_; Y = -X - Z
    rx, ry, rz = np.round(X), np.round(Y), np.round(Z)
    dx, dy, dz = np.abs(rx - X), np.abs(ry - Y), np.abs(rz - Z)
    fx = (dx > dy) & (dx > dz); fy = ~fx & (dy > dz)
    rx = np.where(fx, -ry - rz, rx); ry = np.where(fy, -rx - rz, ry); rz = np.where(~fx & ~fy, -rx - ry, rz)
    ex, ey, ez = X - rx, Y - ry, Z - rz
    hexd = np.maximum.reduce([np.abs(ex), np.abs(ey), np.abs(ez)])           # 0 at the centre, 0.5 at the edge
    edge = 0.5 - hexd                                                         # 0 at the edge
    d_tx = edge * s / 0.866                                                    # distance to the cell edge, texels
    groove = np.clip(1.0 - d_tx / LINE_TX, 0, 1)                               # thin thread line, antialiased
    H = -GROOVE * groove
    gy_, gx_ = np.gradient(H, texel)
    nh = np.stack([-gx_, -gy_, np.ones_like(H)], -1); nh /= np.linalg.norm(nh, axis=-1, keepdims=True)
    # combine with the asset's normal map (slopes add); outside the islands: flat
    # (the asset's own normal map carries its race-suit lettering and logos: not used - the folds are in the geometry)
    ac = np.broadcast_to(np.array([0, 0, 1], np.float32), an.shape)
    sl = ac[..., :2] / np.maximum(ac[..., 2:3], 0.2) + nh[..., :2] / nh[..., 2:3]
    n = np.concatenate([sl, np.ones((TEX, TEX, 1), np.float32)], -1); n /= np.linalg.norm(n, axis=-1, keepdims=True)
    def save(name, arr, srgb):
        im = bpy.data.images.new(name, TEX, TEX, alpha=False)
        if not srgb: im.colorspace_settings.name = 'Non-Color'
        im.pixels.foreach_set(np.concatenate([arr, np.ones((TEX, TEX, 1), np.float32)], -1).astype(np.float32).ravel())
        im.filepath_raw = os.path.join(TEXD, name + ".png"); im.file_format = 'PNG'; im.save(); return im
    nimg = (n * 0.5 + 0.5).astype(np.float32)
    # one colour texture for the whole suit (white and red by face, the red grown 3 texels into the gutters so mip levels
    # do not bleed white into the red edges), the thread lines a little darker; one normal map
    rm = redmap.copy()
    for _ in range(3): rm |= (np.roll(rm, 1, 0) | np.roll(rm, -1, 0) | np.roll(rm, 1, 1) | np.roll(rm, -1, 1)) & ~cover
    col = np.where(rm[..., None], np.array(RED, np.float32), np.array(WHITE, np.float32))
    shade = (1 - float(os.environ.get('FAB_SHADE', '0.14')) * groove)[..., None]
    img = save("crewsuit_fabric", (col * shade).astype(np.float32), True); save("crewsuit_fabric_norm", nimg, False)
    for m in me.materials:
        nt = m.node_tree; b = next(x for x in nt.nodes if x.type == 'BSDF_PRINCIPLED')
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = img; nt.links.new(t.outputs[0], b.inputs["Base Color"])
        tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = bpy.data.images.load(os.path.join(TEXD, "crewsuit_fabric_norm.png"))
        tn.image.colorspace_settings.name = 'Non-Color'; nm = nt.nodes.new("ShaderNodeNormalMap"); nm.uv_map = "Fab"
        nt.links.new(tn.outputs[0], nm.inputs["Color"]); nt.links.new(nm.outputs[0], b.inputs["Normal"])
    me.uv_layers.remove(me.uv_layers[old_uv]); me.uv_layers.active = me.uv_layers["Fab"]
    for m in list(cov.modifiers):
        if m.type == 'ARMATURE': m.show_render = m.show_viewport = True
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    if not VAR: bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
    log.append("hex %.1f mm = %.1f texels; groove %.2f mm; saved" % (HEX_MM, s, GROOVE * 1000))
    # close-up render
    sys.path.insert(0, HERE); import render_util
    cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 900
    log.append("materials %s, faces red %d" % ([m.name for m in me.materials], sum(1 for p in me.polygons if p.material_index == 1)))
    for nm_, loc, tgt in (("cf_macro", (0.10, -0.33, 1.13), (0.07, -0.10, 1.10)), ("cf_close", (0.30, -0.50, 1.25), (0.08, -0.08, 1.18)),
                          ("cf_mid", (0.9, -1.4, 1.1), (0, 0, 1.0))):
        cam.location = loc; cam.data.lens = 50; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(HERE, "renders", nm_ + VAR + ".png"); bpy.ops.render.render(write_still=True)
except Exception:
    import traceback; log.append(traceback.format_exc())
open(LOG, "w").write("\n".join(log))
