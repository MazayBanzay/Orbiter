# «Панцирь» rev. S: own UV unwrap + Cycles bakes for the game asset (after build_pantsir_game_S.py, before export_skin.py).
#   UVBAKE_STAGE=bake : blender -b Tantra_Design\blender\astronavigator_pantsir.blend --python uvbake_pantsir.py
#        smart UV per atlas (seams on the >38 deg plate edges, one texel density per atlas), then the bakes into
#        game\tex\bake\<atlas>_<pass>.npz (float16, Blender row order = bottom first):
#          A   = AO 10 cm, AO 1.5 cm (cavity), pointiness            (emission bake)
#          B   = bevel edge mask, rest-pose z, material id / 16      (emission bake)
#          C   = world-space noise fine / medium / large             (emission bake: seamless across UV islands)
#          N   = tangent-space normal with a 4 mm Bevel shader node, DirectX green (normal_g NEG_Y)
#          S   = the stencils (collection PantsirStencils) selected-to-active: r = red paint, g = dark, b = white
#        and the blend is saved with the new UVs.
#   compose_pantsir.py (system Python) -> game\tex\pz_*.png + Textures\Tantra\AstronavigatorPantsir\pz_*.dds
#   UVBAKE_STAGE=apply : points the game materials at game\tex\pz_*.png (+ the companions for the Blender renders), saves.
# Log: game\uvbake_pantsir.log
import bpy, os, sys, math, json, time, traceback
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); TEX = os.path.join(HERE, "tex"); BAKE = os.path.join(TEX, "bake")
LOG = os.path.join(HERE, "uvbake_pantsir.log"); log = []
STAGE = os.environ.get("UVBAKE_STAGE", "bake")
# atlas -> (size, Orbiter materials); the glow materials (Borazon 6, Sole 12, Rad 15) stay separate materials: they may share
# a file (Metalness.fx: heat = saturate(_heat.g - 1 + specialfx^3) per material, _emis x the material's own emission)
ATLAS = {"pz_shell": (4096, ("Enamel", "Red", "Rubber")), "pz_frame": (2048, ("Graphite", "Metal")),
         "pz_boraz": (1024, ("Borazon", "Mirror")), "pz_sole": (1024, ("Sole",)), "pz_rad": (1024, ("Rad",)),
         "pz_visor": (512, ("GoldVisor", "Slit"))}
MID = {"Enamel": 1, "Red": 2, "Rubber": 3, "Graphite": 4, "Metal": 5, "Borazon": 6, "Mirror": 7, "Sole": 8, "Rad": 9, "GoldVisor": 10, "Slit": 11}
PAD_PX = int(os.environ.get("UV_PAD", "4"))            # island spacing in texels (bake margin the same)
UV_ANGLE = float(os.environ.get("UV_ANGLE", "55"))      # smart UV: plate faces within this angle share an island (chamfers join)
UV_SHAPE = os.environ.get("UV_SHAPE", "CONCAVE")


def say(*a):
    log.append(" ".join(str(x) for x in a)); open(LOG, "w", encoding="utf-8").write("\n".join(log) + "\n")


def suit_objects():
    c = bpy.data.collections["PantsirGame"]
    return [o for o in c.objects if o.type == 'MESH']


ONAME = {}


def oname(o):
    if o.name not in ONAME: ONAME[o.name] = o.data.materials[0]["orb_name"]
    return ONAME[o.name]


def atlas_of(o): return next(a for a, (_, ms) in ATLAS.items() if oname(o) in ms)


def uv_preview():
    """coverage of each atlas (one grey per object) -> bake\<atlas>_uv.npz, for a look at the packing"""
    for atlas, (size, mats) in ATLAS.items():
        n = 512; img = np.zeros((n, n), np.float32)
        for k, o in enumerate([o for o in suit_objects() if oname(o) in mats]):
            me = o.data; uv = me.uv_layers.active.data; me.calc_loop_triangles()
            for t in me.loop_triangles:
                P = np.array([uv[li].uv for li in t.loops]) * n
                x0, y0 = np.floor(P.min(0)).astype(int); x1, y1 = np.ceil(P.max(0)).astype(int)
                xs, ys = np.meshgrid(np.arange(max(x0, 0), min(x1, n)) + 0.5, np.arange(max(y0, 0), min(y1, n)) + 0.5)
                if not xs.size: continue
                (ax, ay), (bx, by), (cx, cy) = P
                d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
                if abs(d) < 1e-12: continue
                l1 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / d; l2 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / d
                ins = (l1 >= 0) & (l2 >= 0) & (l1 + l2 <= 1)
                sub = img[max(y0, 0):min(y1, n), max(x0, 0):min(x1, n)]; sub[ins] += 0.3 + 0.7 * ((k * 0.618) % 1)
        np.savez_compressed(os.path.join(BAKE, atlas + "_uv.npz"), a=img)


def select(objs, active=None):
    if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    for o in bpy.context.view_layer.objects: o.select_set(False)
    for o in objs: o.select_set(True)
    bpy.context.view_layer.objects.active = active or objs[0]


def unwrap():
    ts = bpy.context.scene.tool_settings; ts.use_uv_select_sync = True
    dens = {}
    for atlas, (size, mats) in ATLAS.items():
        objs = [o for o in suit_objects() if oname(o) in mats]
        select(objs); bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(UV_ANGLE), margin_method='SCALED', island_margin=0.0, area_weight=0.0,
                                 correct_aspect=True, scale_to_bounds=False)
        bpy.ops.uv.select_all(action='SELECT'); bpy.ops.uv.average_islands_scale()
        bpy.ops.uv.pack_islands(rotate=True, scale=True, merge_overlap=False, margin_method='FRACTION', margin=PAD_PX / size,
                                shape_method=UV_SHAPE)
        bpy.ops.object.mode_set(mode='OBJECT')
        a3 = auv = 0.0
        for o in objs:
            me = o.data; uv = me.uv_layers.active.data
            for p in me.polygons:
                a3 += p.area
                if p.loop_total == 3:
                    (u0, v0), (u1, v1), (u2, v2) = (uv[li].uv for li in p.loop_indices)
                    auv += abs((u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0)) / 2
        dens[atlas] = size * math.sqrt(auv / a3)
        say("UV %-9s %4d px  %-28s objects %3d  3D %.3f m2  UV fill %.1f %%  -> %.0f px/m (%.2f mm/texel)" % (
            atlas, size, "+".join(mats), len(objs), a3, 100 * auv, dens[atlas], 1000 / dens[atlas]))
    json.dump(dens, open(os.path.join(BAKE, "density.json"), "w"), indent=1)
    nv = 0                                                   # export vertices as export_skin.py splits them (position, UV, normal)
    for o in suit_objects():
        me = o.data; uv = me.uv_layers.active.data; cn = me.corner_normals; me.calc_loop_triangles(); keys = set()
        for t in me.loop_triangles:
            for li, vi in zip(t.loops, t.vertices):
                n = cn[li].vector; keys.add((vi, round(uv[li].uv[0], 5), round(uv[li].uv[1], 5), round(n[0], 3), round(n[1], 3), round(n[2], 3)))
        nv += len(keys)
    say("suit export vertices (estimate, without the head): %d" % nv)


def gpu():
    sc = bpy.context.scene; sc.render.engine = 'CYCLES'
    try:
        pr = bpy.context.preferences.addons['cycles'].preferences
        for t in ('OPTIX', 'CUDA'):
            pr.compute_device_type = t; pr.get_devices()
            ds = [d for d in pr.devices if d.type == t]
            if ds:
                for d in pr.devices: d.use = (d.type == t)
                sc.cycles.device = 'GPU'; say("Cycles on", t, [d.name for d in ds]); return
    except Exception as e: say("GPU setup failed:", e)
    sc.cycles.device = 'CPU'; say("Cycles on CPU")


IMGS = {}


def image(atlas, pas):
    k = (atlas, pas)
    if k not in IMGS:
        n = ATLAS[atlas][0]; im = bpy.data.images.new("bk_%s_%s" % k, n, n, alpha=False, float_buffer=True)
        im.colorspace_settings.name = 'Non-Color'; im.generated_color = (0, 0, 0, 1); IMGS[k] = im
    return IMGS[k]


def save_npz(atlas, pas):
    im = IMGS[(atlas, pas)]; n = im.size[0]
    a = np.empty(n * n * 4, np.float32); im.pixels.foreach_get(a)
    np.savez_compressed(os.path.join(BAKE, "%s_%s.npz" % (atlas, pas)), a=a.reshape(n, n, 4)[..., :3].astype(np.float16))


def bake_material(src, atlas, pas):
    """a temporary material: the target image node (active) + the pass's emission / bevel normal"""
    m = bpy.data.materials.new("bk_%s_%s" % (src["orb_name"], pas)); m.use_nodes = True; nt = m.node_tree; N = nt.nodes; L = nt.links
    for n in list(N): N.remove(n)
    out = N.new("ShaderNodeOutputMaterial"); it = N.new("ShaderNodeTexImage"); it.image = image(atlas, pas); N.active = it; it.select = True
    geo = N.new("ShaderNodeNewGeometry")
    if pas == "N":
        bv = N.new("ShaderNodeBevel"); bv.inputs["Radius"].default_value = 0.004; bv.samples = 16
        bs = N.new("ShaderNodeBsdfPrincipled"); L.new(bv.outputs["Normal"], bs.inputs["Normal"]); L.new(bs.outputs[0], out.inputs[0])
        return m
    em = N.new("ShaderNodeEmission"); em.inputs["Strength"].default_value = 1.0; L.new(em.outputs[0], out.inputs[0])
    cmb = N.new("ShaderNodeCombineXYZ"); L.new(cmb.outputs[0], em.inputs["Color"])
    if pas == "A":
        for k, d in ((0, 0.10), (1, 0.015)):
            ao = N.new("ShaderNodeAmbientOcclusion"); ao.samples = 8; ao.inputs["Distance"].default_value = d
            L.new(ao.outputs["AO"], cmb.inputs[k])
        L.new(geo.outputs["Pointiness"], cmb.inputs[2])
    elif pas == "B":
        bv = N.new("ShaderNodeBevel"); bv.inputs["Radius"].default_value = 0.004; bv.samples = 16
        dp = N.new("ShaderNodeVectorMath"); dp.operation = 'DOT_PRODUCT'; L.new(bv.outputs["Normal"], dp.inputs[0]); L.new(geo.outputs["Normal"], dp.inputs[1])
        e = N.new("ShaderNodeMath"); e.operation = 'SUBTRACT'; e.inputs[0].default_value = 1.0; L.new(dp.outputs["Value"], e.inputs[1])
        L.new(e.outputs[0], cmb.inputs[0])
        sp = N.new("ShaderNodeSeparateXYZ"); L.new(geo.outputs["Position"], sp.inputs[0]); L.new(sp.outputs[2], cmb.inputs[1])
        cmb.inputs[2].default_value = MID[src["orb_name"]] / 16.0
    elif pas == "C":
        for k, (sc_, det) in enumerate(((70.0, 4.0), (14.0, 3.0), (3.0, 2.0))):
            nz = N.new("ShaderNodeTexNoise"); nz.noise_dimensions = '3D'; nz.inputs["Scale"].default_value = sc_
            nz.inputs["Detail"].default_value = det; nz.inputs["Roughness"].default_value = 0.55
            L.new(geo.outputs["Position"], nz.inputs["Vector"]); L.new(nz.outputs["Fac"], cmb.inputs[k])
    elif pas == "S":
        cmb.inputs[0].default_value = cmb.inputs[1].default_value = cmb.inputs[2].default_value = 0.0
    return m


def stencil_material(kind):
    m = bpy.data.materials.new("bk_stn_" + kind); m.use_nodes = True; nt = m.node_tree; N = nt.nodes
    for n in list(N): N.remove(n)
    out = N.new("ShaderNodeOutputMaterial"); em = N.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = {"red": (1, 0, 0, 1), "dark": (0, 1, 0, 1), "white": (0, 0, 1, 1)}[kind]
    nt.links.new(em.outputs[0], out.inputs[0]); return m


def bake_all():
    sc = bpy.context.scene; gpu(); suit = suit_objects(); orig = {o.name: o.data.materials[0] for o in suit}
    stn = list(bpy.data.collections["PantsirStencils"].objects)
    for o in stn: o.hide_render = True
    bk = sc.render.bake; bk.margin = PAD_PX; bk.margin_type = 'EXTEND'; bk.use_clear = True; bk.target = 'IMAGE_TEXTURES'
    if sc.world: sc.world.light_settings.distance = 0.10
    SPP = {"A": 32, "B": 24, "C": 4, "N": 32}
    for pas in ("A", "B", "C", "N"):
        t0 = time.time(); tmp = {}
        for o in suit:
            src = orig[o.name]; key = (src.name, pas)
            if key not in tmp: tmp[key] = bake_material(src, atlas_of(o), pas)
            o.data.materials[0] = tmp[key]
        sc.cycles.samples = SPP[pas]; select(suit); bk.use_selected_to_active = False
        if pas == "N":
            bk.normal_space = 'TANGENT'; bk.normal_r = 'POS_X'; bk.normal_g = 'NEG_Y'; bk.normal_b = 'POS_Z'
            bpy.ops.object.bake(type='NORMAL')
        else:
            bpy.ops.object.bake(type='EMIT')
        for a in ATLAS: save_npz(a, pas)
        say("bake %s: %.0f s" % (pas, time.time() - t0))
    # stencils: selected to active, one target object at a time (its islands only; no margin, so no other island is touched)
    t0 = time.time(); kinds = {}
    for o in stn:
        mn = o.data.materials[0].name if o.data.materials and o.data.materials[0] else ""
        kind = "red" if "red" in mn else "white" if "white" in mn else "dark"
        if kind not in kinds: kinds[kind] = stencil_material(kind)
        for i in range(len(o.data.materials)): o.data.materials[i] = kinds[kind]
        o.hide_render = False
    def bbox(o):
        ps = [o.matrix_world @ v.co for v in o.data.vertices]
        return [min(p[i] for p in ps) for i in range(3)], [max(p[i] for p in ps) for i in range(3)]
    sb = {o.name: bbox(o) for o in stn}
    bk.margin = 0; bk.use_clear = False; bk.use_selected_to_active = True; bk.cage_extrusion = 0.006; bk.max_ray_distance = 0.016
    sc.cycles.samples = 4; nb = 0
    for o in suit:
        lo, hi = bbox(o)
        near = [s for s in stn if all(sb[s.name][0][i] < hi[i] + 0.01 and sb[s.name][1][i] > lo[i] - 0.01 for i in range(3))]
        if not near: continue
        o.data.materials[0] = bake_material(orig[o.name], atlas_of(o), "S")
        select(near + [o], active=o); bpy.ops.object.bake(type='EMIT'); nb += 1
    for a in ATLAS:
        if (a, "S") not in IMGS: image(a, "S")
        save_npz(a, "S")
    say("bake S: %d target objects, %d stencils, %.0f s" % (nb, len(stn), time.time() - t0))
    for o in stn: o.hide_render = True
    for o in suit: o.data.materials[0] = orig[o.name]
    for m in [m for m in bpy.data.materials if m.name.startswith("bk_")]: bpy.data.materials.remove(m)
    for im in list(IMGS.values()): bpy.data.images.remove(im)


def apply():
    """the game materials -> the composed atlases (albedo first: export_skin.py takes the first linked image node's file),
    plus roughness / metalness / normal (DirectX green flipped back for Blender) / emission for the Cycles check renders"""
    done = set()
    for o in suit_objects():
        m = o.data.materials[0]
        if m.name in done: continue
        done.add(m.name); atlas = atlas_of(o); nt = m.node_tree; N = nt.nodes; L = nt.links
        p = next(n for n in N if n.type == 'BSDF_PRINCIPLED'); it = next(n for n in N if n.type == 'TEX_IMAGE')
        for n in [n for n in N if n.type in ('TEX_IMAGE', 'NORMAL_MAP', 'SEPARATE_COLOR', 'MATH', 'COMBINE_COLOR') and n != it]: N.remove(n)
        def img(suffix, color):
            path = os.path.join(TEX, atlas + suffix + ".png")
            im = bpy.data.images.load(path, check_existing=True); im.reload()
            im.colorspace_settings.name = 'sRGB' if color else 'Non-Color'; return im
        it.image = img("", True); L.new(it.outputs["Color"], p.inputs["Base Color"])
        def tex(suffix):
            t = N.new("ShaderNodeTexImage"); t.image = img(suffix, False); return t
        r = tex("_rghn"); sep = N.new("ShaderNodeSeparateColor"); L.new(r.outputs["Color"], sep.inputs[0])
        inv = N.new("ShaderNodeMath"); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0; L.new(sep.outputs["Green"], inv.inputs[1])
        L.new(inv.outputs[0], p.inputs["Roughness"])
        mt = tex("_metl"); sm = N.new("ShaderNodeSeparateColor"); L.new(mt.outputs["Color"], sm.inputs[0]); L.new(sm.outputs["Green"], p.inputs["Metallic"])
        nm = tex("_norm"); sn = N.new("ShaderNodeSeparateColor"); L.new(nm.outputs["Color"], sn.inputs[0])
        fg = N.new("ShaderNodeMath"); fg.operation = 'SUBTRACT'; fg.inputs[0].default_value = 1.0; L.new(sn.outputs["Green"], fg.inputs[1])
        cc = N.new("ShaderNodeCombineColor"); L.new(sn.outputs["Red"], cc.inputs[0]); L.new(fg.outputs[0], cc.inputs[1]); L.new(sn.outputs["Blue"], cc.inputs[2])
        nmap = N.new("ShaderNodeNormalMap"); nmap.space = 'TANGENT'; L.new(cc.outputs[0], nmap.inputs["Color"]); L.new(nmap.outputs["Normal"], p.inputs["Normal"])
        p.inputs["Metallic"].default_value = 0.0
        say("material %-10s -> %s.png (+_rghn/_metl/_norm)" % (m["orb_name"], atlas))


try:
    os.makedirs(BAKE, exist_ok=True); say("stage", STAGE, bpy.data.filepath)
    if STAGE == "uv":
        unwrap(); uv_preview(); bpy.ops.wm.quit_blender()
    if STAGE == "bake":
        if bpy.context.object and bpy.context.object.mode != 'OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
        unwrap(); uv_preview(); bake_all()
    elif STAGE == "apply":
        apply()
    bpy.ops.wm.save_mainfile(); say("saved", bpy.data.filepath)
except Exception:
    say(traceback.format_exc())
bpy.ops.wm.quit_blender()
