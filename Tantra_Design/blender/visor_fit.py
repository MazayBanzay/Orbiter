# THE VISOR GLASS, fitted (2026-10-18; chosen: sapphire with a light blue tint). exec'd into a namespace that holds HK
# (helmet_cheek.py's definitions, exec'd before its render loop), bpy, bmesh, math, Vector, BVHTree:
#   SHELLB = shell_bvh()            the helmet's outer shell, taken BEFORE the cut
#   cut_fine(W)                     the window cut clean: the faces near its edge split fine, then the inside ones go
#   fitted_glass(name, mat, W, SHELLB, ...)   the glass ~10 mm under the shell's edge (30 mm at the chin), 2 mm inside
#                                   its outer face, laid on the real shell (it narrows to the neck); the grey rim covers the seam
#   sapphire_blue(name)             the material: sapphire laminate (sapphire, an electrochromic layer, a UV-cut polymer
#                                   pressure layer, an IR-reflecting film) - nearly colourless, a light blue cast
def shell_bvh():
    bpy.data.objects["Astronavigator"].data.pose_position = 'REST'; bpy.context.view_layer.update()
    return BVHTree.FromObject(bpy.data.objects["Helmet"], bpy.context.evaluated_depsgraph_get())

def cut_fine(w, passes=3, band=0.03):
    for n in HK["CUT"]:
        o = bpy.data.objects[n]; me = o.data; bm = bmesh.new(); bm.from_mesh(me); M = o.matrix_world
        for _ in range(passes):
            near = [e for e in bm.edges if any(w.inside(M @ v.co, band) for v in e.verts) and not all(w.inside(M @ v.co, -0.012) for v in e.verts)]
            if not near: break
            bmesh.ops.subdivide_edges(bm, edges=near, cuts=1, use_grid_fill=True)
        dead = [f for f in bm.faces if w.inside(M @ f.calc_center_median(), 0.0)]
        bmesh.ops.delete(bm, geom=dead, context='FACES'); bm.to_mesh(me); bm.free(); me.update()

def fitted_glass(name, mat, W, SHELLB, margin_deg=4.0, margin_z=0.010, margin_bot=0.030, off=-0.002, out=0.0):
    S, N, th_of, FRONT, C = HK["S"], HK["N"], HK["th_of"], HK["FRONT"], HK["C"]
    bm = bmesh.new(); rows = []; n, m = 48, 16; h = W.h + math.radians(margin_deg)
    for j in range(m + 1):
        row = []
        for i in range(n + 1):
            u = -1 + 2 * i / n; ph = FRONT + u * h; uw = max(-1, min(1, u * h / W.h))
            z0 = W.z0(uw) - margin_bot; z1 = W.top + margin_z
            z = z0 + (z1 - z0) * j / m; th = th_of(z)
            p = S(th, ph); n0 = N(th, ph)
            loc, nrm, _, _ = SHELLB.ray_cast(p + n0 * 0.08, -n0, 0.16)
            if loc is None: loc, nrm = p, n0
            nrm = nrm if nrm.dot(p - C) > 0 else -nrm
            row.append(bm.verts.new(loc + nrm * (off + out)))
        rows.append(row)
    for a, b in zip(rows, rows[1:]):
        for i in range(n): bm.faces.new((a[i], a[i + 1], b[i + 1], b[i]))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(mat)
    for p_ in me.polygons: p_.use_smooth = True
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    return o

def chin_fill(W, SHELLB, name="HelmChin", old_bot=1.523, old_half_deg=55.0):
    """the helmet's own old opening (the old visor's: flat bottom z 1.523, +-55 deg) lies under the new window's
    rising lower edge - a strip up to ~5 cm at the sides showed the inside. Closed with shell: a 3 mm patch on the
    shell's surface from under the old bottom up to the new edge (under the rim)"""
    S, N, th_of, FRONT, C = HK["S"], HK["N"], HK["th_of"], HK["FRONT"], HK["C"]
    h = math.radians(old_half_deg + 4.0); n, m = 44, 8
    bm = bmesh.new(); top, bot = [], []
    # per column: the real shell's offset from the ellipsoid just under the old opening, carried up the column - the
    # patch continues the shell smoothly over the opening (no mix of hits and misses)
    D = []
    for i in range(n + 1):
        a = -h + 2 * h * i / n; ph = FRONT + a; th = th_of(old_bot - 0.010); p = S(th, ph); n0 = N(th, ph)
        loc, nrm, _, _ = SHELLB.ray_cast(p + n0 * 0.08, -n0, 0.16)
        D.append((loc - p).dot(n0) if loc is not None else 0.0)
    for j in range(m + 1):
        rt, rb = [], []
        for i in range(n + 1):
            a = -h + 2 * h * i / n; ph = FRONT + a; uw = max(-1, min(1, a / W.h))
            z0 = old_bot - 0.010; z1 = W.z0(uw) + 0.005
            z = z0 + (z1 - z0) * j / m; th = th_of(z); n0 = N(th, ph); base = S(th, ph) + n0 * D[i]
            rt.append(bm.verts.new(base + n0 * 0.0005)); rb.append(bm.verts.new(base - n0 * 0.0025))
        top.append(rt); bot.append(rb)
    for j in range(m):
        for i in range(n):
            bm.faces.new((top[j][i], top[j][i + 1], top[j + 1][i + 1], top[j + 1][i])); bm.faces.new((bot[j][i], bot[j + 1][i], bot[j + 1][i + 1], bot[j][i + 1]))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    white = bpy.data.materials.get("HelmetShell") or HK["WHITE"]
    me.materials.append(white)
    for p_ in me.polygons: p_.use_smooth = True
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    return o

def sapphire_blue(name="VisorSapphire"):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name); m.use_nodes = True
    b = next(n_ for n_ in m.node_tree.nodes if n_.type == 'BSDF_PRINCIPLED')
    b.inputs["Base Color"].default_value = (0.66, 0.80, 0.98, 1); b.inputs["Metallic"].default_value = 0.0; b.inputs["Roughness"].default_value = 0.02
    b.inputs["Alpha"].default_value = 0.17
    for k, v in (("Specular IOR Level", 0.35), ("IOR", 1.77)):
        if k in b.inputs: b.inputs[k].default_value = v
    try: m.surface_render_method = 'BLENDED'
    except Exception: m.blend_method = 'BLEND'
    m.diffuse_color = (0.66, 0.80, 0.98, 0.17); return m
