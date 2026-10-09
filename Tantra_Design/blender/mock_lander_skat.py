# Lander of «Тантра», look B «Скат» (+ look A «Кристалл») - preview renders only, nothing for the game.
# powershell -File run.ps1 mock_lander_skat.py [shot,shot,...]   (no args = all shots)
# Data: ../lander_v1.json. JSON axes x fwd, y up, z right -> Blender (x, -z, y).
import bpy, bmesh, math, os, sys, json, traceback
from mathutils import Vector, Matrix
HERE = r"C:\Games\Orbiter-2024\Tantra_Design\blender"; OUT = os.path.join(HERE, "renders")
LOG = os.path.join(HERE, "mock_lander_skat.log"); log = []
def L(s):
    log.append(str(s)); open(LOG, "w", encoding="utf-8").write("\n".join(log))
D = json.load(open(os.path.join(HERE, "..", "lander_v1.json"), encoding="utf-8"))
LOOKS = {l["id"]: l for l in D["looks"]}

def J(p): return Vector((p[0], -p[2], p[1]))
def hexc(h): h = h.lstrip('#'); return tuple((int(h[i:i + 2], 16) / 255) ** 2.2 for i in (0, 2, 4))

# ---------------- materials ----------------
MATS = {}
def principled(m):
    return next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
def mat(name, col, rough=0.5, metal=0.0, emit=0.0, alpha=1.0, seams=False, wear=0.0):
    if name in MATS: return MATS[name]
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree; b = principled(m)
    b.inputs["Base Color"].default_value = (*col, 1); b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if emit > 0:
        b.inputs["Emission Color"].default_value = (*col, 1); b.inputs["Emission Strength"].default_value = emit
    if alpha < 1:
        b.inputs["Alpha"].default_value = alpha
        for a, v in (("surface_render_method", 'BLENDED'), ("blend_method", 'BLEND')):
            try: setattr(m, a, v)
            except Exception: pass
        try: m.use_backface_culling = False
        except Exception: pass
    if seams or wear > 0:
        tc = nt.nodes.new("ShaderNodeTexCoord")
        noise = nt.nodes.new("ShaderNodeTexNoise"); noise.inputs["Scale"].default_value = 14.0; noise.inputs["Detail"].default_value = 8
        nt.links.new(tc.outputs["Object"], noise.inputs["Vector"])
        mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'
        dark = tuple(c * 0.9 for c in col); light = tuple(min(1, c * 1.08 + 0.004) for c in col)
        mix.inputs[6].default_value = (*dark, 1); mix.inputs[7].default_value = (*light, 1)
        ramp = nt.nodes.new("ShaderNodeMapRange"); ramp.inputs[1].default_value = 0.25; ramp.inputs[2].default_value = 0.75
        nt.links.new(noise.outputs["Fac"], ramp.inputs[0]); nt.links.new(ramp.outputs[0], mix.inputs[0])
        rr = nt.nodes.new("ShaderNodeMapRange"); rr.inputs[3].default_value = rough - 0.12; rr.inputs[4].default_value = rough + 0.12
        nt.links.new(ramp.outputs[0], rr.inputs[0]); nt.links.new(rr.outputs[0], b.inputs["Roughness"])
        colsock = mix.outputs[2]
        if seams:
            sep = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(tc.outputs["Object"], sep.inputs[0]); acc = None
            for k, pitch in ((0, 1.5), (1, 1.2), (2, 1.0)):
                d = nt.nodes.new("ShaderNodeMath"); d.operation = 'DIVIDE'; d.inputs[1].default_value = pitch; nt.links.new(sep.outputs[k], d.inputs[0])
                f = nt.nodes.new("ShaderNodeMath"); f.operation = 'FRACT'; nt.links.new(d.outputs[0], f.inputs[0])
                s = nt.nodes.new("ShaderNodeMath"); s.operation = 'SUBTRACT'; s.inputs[1].default_value = 0.5; nt.links.new(f.outputs[0], s.inputs[0])
                a = nt.nodes.new("ShaderNodeMath"); a.operation = 'ABSOLUTE'; nt.links.new(s.outputs[0], a.inputs[0])
                g = nt.nodes.new("ShaderNodeMath"); g.operation = 'GREATER_THAN'; g.inputs[1].default_value = 0.4935; nt.links.new(a.outputs[0], g.inputs[0])
                if acc is None: acc = g
                else:
                    mx = nt.nodes.new("ShaderNodeMath"); mx.operation = 'MAXIMUM'; nt.links.new(acc.outputs[0], mx.inputs[0]); nt.links.new(g.outputs[0], mx.inputs[1]); acc = mx
            m2 = nt.nodes.new("ShaderNodeMix"); m2.data_type = 'RGBA'; nt.links.new(acc.outputs[0], m2.inputs[0])
            nt.links.new(colsock, m2.inputs[6]); m2.inputs[7].default_value = (*(c * 0.35 for c in col), 1); colsock = m2.outputs[2]
        nt.links.new(colsock, b.inputs["Base Color"])
    MATS[name] = m; return m

def plume_mat(col, axis=2):
    if "plume" in MATS: return MATS["plume"]
    m = bpy.data.materials.new("plume"); m.use_nodes = True; nt = m.node_tree
    for n in list(nt.nodes):
        if n.type != 'OUTPUT_MATERIAL': nt.nodes.remove(n)
    out = next(n for n in nt.nodes if n.type == 'OUTPUT_MATERIAL')
    em = nt.nodes.new("ShaderNodeEmission"); em.inputs[0].default_value = (*col, 1); em.inputs[1].default_value = 6
    tr = nt.nodes.new("ShaderNodeBsdfTransparent"); lw = nt.nodes.new("ShaderNodeLayerWeight"); lw.inputs[0].default_value = 0.35
    mr = nt.nodes.new("ShaderNodeMapRange"); mr.inputs[3].default_value = 0.55; mr.inputs[4].default_value = 1.0
    nt.links.new(lw.outputs["Facing"], mr.inputs[0])
    mx = nt.nodes.new("ShaderNodeMixShader"); 
    tc = nt.nodes.new("ShaderNodeTexCoord"); sp = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(tc.outputs["Generated"], sp.inputs[0])
    inv = nt.nodes.new("ShaderNodeMath"); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0; nt.links.new(mr.outputs[0], inv.inputs[1])
    pw = nt.nodes.new("ShaderNodeMath"); pw.operation = 'POWER'; pw.inputs[1].default_value = 1.6; nt.links.new(sp.outputs[axis], pw.inputs[0])
    mu = nt.nodes.new("ShaderNodeMath"); mu.operation = 'MULTIPLY'; nt.links.new(inv.outputs[0], mu.inputs[0]); nt.links.new(pw.outputs[0], mu.inputs[1])
    fac = nt.nodes.new("ShaderNodeMath"); fac.operation = 'SUBTRACT'; fac.inputs[0].default_value = 1.0; nt.links.new(mu.outputs[0], fac.inputs[1])
    nt.links.new(fac.outputs[0], mx.inputs[0]); nt.links.new(em.outputs[0], mx.inputs[1]); nt.links.new(tr.outputs[0], mx.inputs[2])
    nt.links.new(mx.outputs[0], out.inputs[0])
    for a, v in (("surface_render_method", 'BLENDED'), ("blend_method", 'BLEND')):
        try: setattr(m, a, v)
        except Exception: pass
    MATS["plume"] = m; return m

# ---------------- mesh helpers (JSON space) ----------------
OBJS = []
def mk(name, verts, faces, m, M=None, bevel=0.0, smooth=False, solid=0.0, tag="model"):
    M = M or Matrix.Identity(4)
    me = bpy.data.meshes.new(name); me.from_pydata([J(M @ Vector(v)) for v in verts], [], faces)
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    for p in me.polygons: p.use_smooth = smooth
    me.materials.append(m)
    o = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(o)
    if solid:
        s = o.modifiers.new("solid", 'SOLIDIFY'); s.thickness = solid; s.offset = 0
    if bevel > 0:
        b = o.modifiers.new("bev", 'BEVEL'); b.width = bevel; b.segments = 2; b.limit_method = 'ANGLE'; b.angle_limit = math.radians(25)
        try: b.harden_normals = False
        except Exception: pass
    o["tag"] = tag; OBJS.append(o); return o

def frame(axis):
    a = {"x": Vector((1, 0, 0)), "y": Vector((0, 1, 0)), "z": Vector((0, 0, 1))}[axis] if isinstance(axis, str) else Vector(axis).normalized()
    e1 = a.orthogonal().normalized(); e2 = a.cross(e1); return a, e1, e2

def lathe(prof, center, axis, seg=32, cap0=False, cap1=False, rot=0.0):
    """prof: list of (r, h) along axis from center."""
    a, e1, e2 = frame(axis); c = Vector(center); V = []; F = []
    for r, h in prof:
        for j in range(seg):
            t = 2 * math.pi * j / seg + rot; V.append(c + a * h + (e1 * math.cos(t) + e2 * math.sin(t)) * r)
    n = len(prof)
    for i in range(n - 1):
        for j in range(seg): F.append((i * seg + j, i * seg + (j + 1) % seg, (i + 1) * seg + (j + 1) % seg, (i + 1) * seg + j))
    if cap0: F.append(tuple(range(seg))[::-1])
    if cap1: F.append(tuple(range((n - 1) * seg, n * seg)))
    return V, F

def boxv(center, dims):
    c = Vector(center); hx, hy, hz = (d / 2 for d in dims)
    V = [c + Vector((sx * hx, sy * hy, sz * hz)) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return V, F

def loft(rings, cap=True):
    n = len(rings[0]); V = [Vector(p) for r in rings for p in r]; F = []
    for i in range(len(rings) - 1):
        for j in range(n): F.append((i * n + j, i * n + (j + 1) % n, (i + 1) * n + (j + 1) % n, (i + 1) * n + j))
    if cap: F.append(tuple(range(n))[::-1]); F.append(tuple(range((len(rings) - 1) * n, len(rings) * n)))
    return V, F

def oct_sec(s, chine=False):
    x, hw, yt, yb, ch = s["x"], s["hw"], s["yt"], s["yb"], s["ch"]
    if chine:   # waverider: flat belly, knife chine low on the side, faceted roof
        cw = hw + 0.7 * min(1, hw / 2.0); yc = yb + 0.28 * (yt - yb); cb = min(ch * 0.6, hw * 0.5)
        pts = [(yb, -(hw - cb)), (yb, hw - cb), (yc, cw), (yt - ch, hw - 0.05), (yt, hw - ch), (yt, -(hw - ch)), (yt - ch, -(hw - 0.05)), (yc, -cw)]
    else:
        pts = [(yb, -(hw - ch)), (yb, hw - ch), (yb + ch, hw), (yt - ch, hw), (yt, hw - ch), (yt, -(hw - ch)), (yt - ch, -hw), (yb + ch, -hw)]
    return [(x, y, z) for y, z in pts]

def hw_at(secs, x):
    for a, b in zip(secs, secs[1:]):
        if a["x"] <= x <= b["x"]:
            t = (x - a["x"]) / (b["x"] - a["x"]); return a["hw"] + t * (b["hw"] - a["hw"])
    return secs[-1]["hw"]

def mover_M(G, group, mode):
    for mv in G.get("movers", []):
        if mv["group"] == group:
            ang = mv.get(mode + "_deg", 0) or 0; p = Vector(mv["pivot"])
            return Matrix.Translation(p) @ Matrix.Rotation(math.radians(ang), 4, Vector(mv["axis"])) @ Matrix.Translation(-p)
    return Matrix.Identity(4)

# ---------------- the lander ----------------
ROLE = {"tank": (0.55, 0.77, 0.91), "charges": (0.89, 0.62, 0.12), "shield": (0.30, 0.37, 0.49), "coil": (0.62, 0.38, 0.17),
        "cabin": (0.85, 0.88, 0.91), "seat": (0.91, 0.42, 0.10), "console": (0.45, 0.66, 0.85), "gear": (0.50, 0.52, 0.55)}

def build(look="B", var=1, mode="hover", thrust=True, cut=False):
    G = LOOKS[look]["geometry"]; pal = LOOKS[look]["palette"]
    hullc = hexc(pal["hull"]); dm = hexc(pal["dark_metal"]); glow = hexc(pal["glow"])
    if var == 3: hullc = (0.80, 0.81, 0.80)
    if var == 4: hullc = (0.07, 0.075, 0.08)
    M_h = mat("hull%d%s" % (var, look), hullc, 0.62 if var != 4 else 0.38, 0.0 if var != 4 else 0.75, seams=True, wear=0.3)
    M_dm = mat("dmetal", tuple(max(c, 0.045) for c in dm), 0.42, 0.8, wear=0.2); M_cup = mat("cup", (0.05, 0.05, 0.055), 0.3, 0.9)
    M_coil = mat("coil", ROLE["coil"], 0.3, 1.0); M_glow = mat("glow", glow, 0.3, 0, emit=40)
    M_sh = mat("shield", ROLE["shield"], 0.45, 0.6); M_gear = mat("gear", ROLE["gear"], 0.35, 0.9)
    M_door = mat("door%d" % var, tuple(c * 0.75 for c in hullc), 0.55, 0.2)
    M_cry = mat("iridium", (0.34, 0.35, 0.38), 0.24, 1.0)
    secs = G["hull_sections"]; crystal = (var == 2 or look == "A")
    # hull
    use = [s for s in secs if not crystal or s["x"] <= 7.0 + 1e-6]
    V, F = loft([oct_sec(s, chine=(var == 4)) for s in use])
    hull = mk("hull", V, F, M_h, bevel=0.05)
    if crystal:   # faceted crystal nose: staggered octagon rings -> convex hull, flat facets
        s7 = [s for s in secs if abs(s["x"] - 7.0) < 1e-6][0]; tip_x = secs[-1]["x"] + 0.7
        pts = [Vector(p) for p in oct_sec(s7)]
        for x, k, rot in ((8.1, 0.86, 1), (9.1, 0.58, 0), (9.9, 0.30, 1), (10.4, 0.12, 0)):
            ym = (s7["yt"] + s7["yb"]) / 2 - 0.25 * (x - 7) / 3.5
            for j in range(8):
                t = 2 * math.pi * (j + 0.5 * rot) / 8 + math.pi / 8
                pts.append(Vector((x, ym + math.sin(t) * (s7["yt"] - s7["yb"]) / 2 * k, math.cos(t) * s7["hw"] * k)))
        pts.append(Vector((tip_x, (s7["yt"] + s7["yb"]) / 2 - 0.35, 0)))
        bm = bmesh.new(); vs = [bm.verts.new(p) for p in pts]; bmesh.ops.convex_hull(bm, input=vs)
        me = bpy.data.meshes.new("cry"); bm.to_mesh(me); bm.free()
        mk("crystal", [v.co for v in me.vertices], [tuple(p.vertices) for p in me.polygons], M_cry, bevel=0.015)
    hull["cut"] = True
    # primitives
    pod_seg = 8 if look == "A" else 40
    for p in G["primitives"]:
        role, kind, c, d = p.get("role"), p["kind"], p["center"], p["dims"]; grp = p.get("group", "body")
        M = mover_M(G, grp, mode); pid = p["id"]
        if role == "pod":
            h = d[1] / 2
            prof = [(0.57, 0.40 - (c[1] - 0.85) - 0.0), (0.6, -h + 0.05), (0.68, -h - 0.02), (0.75, -h + 0.08), (0.75, h - 0.35), (0.70, h - 0.12), (0.55, h + 0.02), (0.3, h + 0.08)]
            prof[0] = (0.57, -0.45 + 0.05)
            V, F = lathe(prof, c, "y", pod_seg, cap1=True, rot=math.pi / 8 if pod_seg == 8 else 0)
            o = mk(pid, V, F, M_dm, M, bevel=0.02 if pod_seg == 8 else 0, smooth=(pod_seg > 8)); o["pod"] = True
        elif role == "cup":
            y0 = -d[1] / 2; prof = [(d[0] * (1 - 0.78 * (i / 6) ** 0.7), y0 + d[1] * i / 6) for i in range(7)]
            V, F = lathe(prof, c, "y", 40); mk(pid, V, F, M_cup, M, smooth=True, solid=0.03)
            V, F = lathe([(0.0001, d[1] / 2 - 0.06), (d[0] * 0.24, d[1] / 2 - 0.06)], c, "y", 24); mk(pid + "_throat", V, F, M_glow if thrust else M_coil, M)
            if thrust:
                V, F = lathe([(0.45, 0.0), (0.5, -1.6), (0.62, -3.2)], (c[0], c[1] - 0.45, c[2]), "y", 32)
                mk(pid + "_plume", [v for v in V], F, plume_mat(glow, 2 if mode in ("hover", "transition") else 0), M, smooth=True, tag="plume")
        elif kind == "torus":
            R, r = d; a, e1, e2 = frame("y"); V = []; F = []; n1, n2 = 48, 12
            for i in range(n1):
                t = 2 * math.pi * i / n1; cc = Vector(c) + (e1 * math.cos(t) + e2 * math.sin(t)) * R; rd = (e1 * math.cos(t) + e2 * math.sin(t))
                for j in range(n2):
                    u = 2 * math.pi * j / n2; V.append(cc + rd * math.cos(u) * r + a * math.sin(u) * r)
            for i in range(n1):
                for j in range(n2): F.append((i * n2 + j, i * n2 + (j + 1) % n2, ((i + 1) % n1) * n2 + (j + 1) % n2, ((i + 1) % n1) * n2 + j))
            o = mk(pid, V, F, M_coil, M, smooth=True); o["coil"] = True
            # second winding (data: 2 windings per hinge) shown as a thinner ring under the first
        elif role == "gear":
            h = d[1] / 2; V, F = lathe([(d[0], -h + 0.1), (d[0], h)], c, "y", 16, True, True); mk(pid, V, F, M_gear, M, smooth=True)
            V, F = lathe([(0.12, -h + 0.12), (0.38, -h + 0.06), (0.38, -h), (0.12, -h)], c, "y", 24, False, False)
            V, F = boxv((c[0], c[1] - h + 0.05, c[2]), (0.75, 0.1, 0.55)); mk(pid + "_pad", V, F, M_gear, M, bevel=0.03)
        elif role == "door":
            if pid.startswith("door"):
                z = -(hw_at(secs, c[0]) + 0.012) if c[2] < 0 else hw_at(secs, c[0]) + 0.012
                R = min(d[0], d[1]) / 2 / math.cos(math.pi / 8)
                V, F = lathe([(R, -0.03), (R, 0.03)], (c[0], c[1], z), "z", 8, True, True, rot=math.pi / 8); mk(pid, V, F, M_door, M, bevel=0.012)
            elif not cut:   # cargo hatch: flush panel in the roof
                V, F = boxv(c, (d[0], 0.03, d[2])); mk(pid, V, F, M_door, M, bevel=0.01)
        elif role == "shield":
            V, F = boxv(c, [max(x, 0.03) for x in d]); mk(pid, V, F, M_sh, M)
        elif role == "structure":
            V, F = boxv(c, d); o = mk(pid, V, F, M_h, M, bevel=min(d) * 0.3); o["cut"] = pid.startswith(("fairing", "pylon"))
        elif role == "other" and pid.startswith("rcs"):
            V, F = boxv(c, d); mk(pid, V, F, M_dm, M, bevel=0.03)
        elif cut:   # internals only in the cutaway
            if role == "seat":
                cx, cy, cz = c; lx, ly, lz = d
                V, F = boxv((cx, cy - ly / 2 + 0.25, cz), (lx * 0.75, 0.12, lz * 0.9)); mk(pid + "_s", V, F, mat("seat", ROLE["seat"], 0.6), bevel=0.03, tag="int")
                V, F = boxv((cx - lx / 2 + 0.1, cy + 0.1, cz), (0.12, ly * 0.85, lz * 0.9)); mk(pid + "_b", V, F, mat("seat", ROLE["seat"], 0.6), bevel=0.03, tag="int")
                V, F = boxv((cx, cy - ly / 2 + 0.1, cz), (0.2, 0.2, 0.2)); mk(pid + "_p", V, F, M_gear, tag="int")
            elif role == "cabin":
                cx, cy, cz = c; lx, ly, lz = d; top = min(cy + ly / 2, 0.25); y0 = cy - ly / 2; Mc = mat("cabin", ROLE["cabin"], 0.5)
                for ce, dd in (((cx, y0, cz), (lx, 0.06, lz)), ((cx - lx / 2, (y0 + top) / 2, cz), (0.06, top - y0, lz)), ((cx + lx / 2, (y0 + top) / 2, cz), (0.06, top - y0, lz)),
                               ((cx, (y0 + top) / 2, cz - lz / 2), (lx, top - y0, 0.06))):
                    V, F = boxv(ce, dd); mk(pid + "_w", V, F, Mc, tag="int")
            elif role == "console":
                V, F = boxv(c, d); mk(pid, V, F, mat("console", ROLE["console"], 0.3, emit=1.5), bevel=0.03, tag="int")
            elif kind == "cylinder" and role == "tank":
                h = d[1] / 2; prof = [(0.0001, -h), (d[0] * 0.7, -h + 0.08), (d[0], -h + 0.3), (d[0], h - 0.3), (d[0] * 0.7, h - 0.08), (0.0001, h)]
                V, F = lathe(prof, c, "x", 32); mk(pid, V, F, mat("tank", ROLE["tank"], 0.35, 0.3), smooth=True, tag="int")
            elif role == "charges":
                V, F = boxv(c, d); mk(pid, V, F, mat("charges", ROLE["charges"], 0.4, 0.2, emit=0.3), bevel=0.05, tag="int")
            elif role == "other" and kind == "box":
                if pid == "cargo_bay":
                    V, F = boxv((c[0], c[1] - d[1] / 2, c[2]), (d[0], 0.06, d[2])); mk(pid, V, F, mat("cargo", (0.6, 0.62, 0.66), 0.6, alpha=0.18), tag="int")
                else:
                    V, F = boxv(c, d); mk(pid, V, F, mat("unit", (0.42, 0.44, 0.48), 0.5, 0.5), bevel=0.05, tag="int")
    # wings with a symmetric profile
    for w in G["wings"]:
        s = -1 if w["side"] == "L" else 1; rl = Vector(w["root_le"]); span = w["span"]; rc = w["root_chord"]; tcd = w["tip_chord"]
        sw = math.radians(w["le_sweep_deg"]); th = w["thickness"]; M = mover_M(G, w["group"], mode); rings = []
        for k, f in enumerate((0.0, 0.5, 1.0)):
            le = rl + Vector((-span * f * math.tan(sw), 0, s * span * f)); ch = rc + (tcd - rc) * f; T = th * (1 - 0.4 * f); N = 14; top = []; bot = []
            for i in range(N + 1):
                u = 0.5 * (1 - math.cos(math.pi * i / N))
                ht = 5 * T * (0.2969 * math.sqrt(u) - 0.126 * u - 0.3516 * u ** 2 + 0.2843 * u ** 3 - 0.1036 * u ** 4)
                top.append(le + Vector((-ch * u, ht, 0))); bot.append(le + Vector((-ch * u, -ht, 0)))
            rings.append(top + bot[1:-1][::-1])
        V, F = loft(rings); mk(w["group"], V, F, M_h, M, smooth=True)
        tip = M @ (rl + Vector((-span * math.tan(sw) - tcd * 0.5, 0, s * (span + 0.03))))
        V, F = lathe([(0.0001, -0.07), (0.07, 0), (0.0001, 0.07)], tip, "y", 12)
        mk("nav_" + w["side"], V, F, mat("nav" + w["side"], (0.9, 0.05, 0.03) if s < 0 else (0.05, 0.9, 0.15), 0.3, emit=25), smooth=True)
    return G

def mannequin(at):   # 1.8 m scale figure, JSON coords, feet at at
    x, y, z = at; Mq = mat("man", (0.75, 0.72, 0.66), 0.7)
    for dz in (-0.1, 0.1):
        V, F = lathe([(0.07, 0), (0.08, 0.86)], (x, y, z + dz), "y", 12, True, True); mk("leg", V, F, Mq, smooth=True, tag="man")
    V, F = lathe([(0.16, 0.86), (0.19, 1.2), (0.2, 1.45), (0.08, 1.52)], (x, y, z), "y", 16, True, True); mk("torso", V, F, Mq, smooth=True, tag="man")
    for dz in (-0.25, 0.25):
        V, F = lathe([(0.05, 0.72), (0.055, 1.42)], (x, y, z + dz), "y", 10, True, True); mk("arm", V, F, Mq, smooth=True, tag="man")
    V, F = lathe([(0.0001, 1.57), (0.09, 1.62), (0.11, 1.69), (0.09, 1.76), (0.0001, 1.8)], (x, y, z), "y", 16); mk("head", V, F, Mq, smooth=True, tag="man")

# ---------------- stage ----------------
def clear():
    OBJS.clear()
    for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)

def stage(ground=False):
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 1600, 1000
    try: sc.eevee.taa_render_samples = 48
    except Exception: pass
    for a in ("use_gtao", "use_shadows"):
        try: setattr(sc.eevee, a, True)
        except Exception: pass
    try: sc.view_settings.view_transform = 'AgX'
    except Exception: pass
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True; nt = world.node_tree
    bg = next(n for n in nt.nodes if n.type == "BACKGROUND"); wo = next(n for n in nt.nodes if n.type == "OUTPUT_WORLD"); nt.links.new(bg.outputs[0], wo.inputs[0])
    if ground:
        bg.inputs[0].default_value = (0.42, 0.55, 0.75, 1); bg.inputs[1].default_value = 0.9
    else:
        bg.inputs[0].default_value = (0.010, 0.012, 0.018, 1); bg.inputs[1].default_value = 1.0
    def sun(name, d, e, ang=0.6, col=(1, 1, 1)):
        l = bpy.data.lights.new(name, 'SUN'); l.energy = e; l.angle = math.radians(ang); l.color = col
        o = bpy.data.objects.new(name, l); sc.collection.objects.link(o); o.rotation_mode = 'QUATERNION'
        o.rotation_quaternion = (-Vector(d).normalized()).to_track_quat('-Z', 'Y')
    sun("Key", (0.6, -0.25, 0.75), 5.0 if not ground else 4.0, col=(1, 0.97, 0.92))
    sun("Fill", (-0.6, -0.4, -0.3), 0.35, 5, (0.7, 0.8, 1.0))
    if ground:
        sun("Sky", (0.2, -0.5, 1.0), 0.6, 20, (0.6, 0.75, 1.0))
        me = bpy.data.meshes.new("Ground"); R = 300; z0 = -3.0
        me.from_pydata([(-R, -R, z0), (R, -R, z0), (R, R, z0), (-R, R, z0)], [], [(0, 1, 2, 3)])
        g = bpy.data.objects.new("Ground", me); sc.collection.objects.link(g)
        me.materials.append(mat("ground", (0.36, 0.31, 0.26), 0.95, wear=1.0))
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam")); sc.collection.objects.link(cam); sc.camera = cam
    cam.data.clip_end = 2000; return cam

from bpy_extras.object_utils import world_to_camera_view
def fit(cam, direction, lens=50, margin=0.05, tags=("model", "int"), extra=(), target=None):
    sc = bpy.context.scene; bpy.context.view_layer.update(); pts = []
    for o in OBJS:
        if o.get("tag") in tags: pts += [o.matrix_world @ Vector(c) for c in o.bound_box]
    pts += [Vector(p) for p in extra]
    lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts))); hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
    tgt = target or (lo + hi) / 2; dv = Vector(direction).normalized(); cam.data.lens = lens
    def ok(dist):
        cam.location = tgt + dv * dist; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (-dv).to_track_quat('-Z', 'Y'); bpy.context.view_layer.update()
        for p in pts:
            v = world_to_camera_view(sc, cam, p)
            if v.z <= 0 or not (margin <= v.x <= 1 - margin and margin * 1.6 <= v.y <= 1 - margin * 1.6): return False
        return True
    a, b = 3.0, 400.0
    for _ in range(30):
        m = (a + b) / 2
        if ok(m): b = m
        else: a = m
    ok(b)
    # recentre on projected bbox
    vs = [world_to_camera_view(sc, cam, p) for p in pts]; cx = (min(v.x for v in vs) + max(v.x for v in vs)) / 2 - 0.5; cy = (min(v.y for v in vs) + max(v.y for v in vs)) / 2 - 0.5
    cam.data.shift_x = cx; cam.data.shift_y = cy * sc.render.resolution_y / sc.render.resolution_x
    return cam

def render(name):
    sc = bpy.context.scene; sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); L("rendered " + name)

def cutaway():
    # cutter: whole roof above y 0.25 + right side (z>0) above y -1.2, JSON space
    prof = [(-12, 0.25), (0, 0.25), (0, -1.2), (12, -1.2), (12, 9), (-12, 9)]   # (z, y)
    V = [(x, y, z) for x in (-12, 13) for z, y in prof]; n = len(prof)
    F = [tuple(range(n))[::-1], tuple(range(n, 2 * n))] + [(j, (j + 1) % n, n + (j + 1) % n, n + j) for j in range(n)]
    Mr = mat("section", (0.72, 0.16, 0.10), 0.6)
    cutter = mk("cutter", V, F, Mr, tag="cutter"); cutter.hide_render = True; cutter.display_type = 'WIRE'
    Ms = mat("section", (0.72, 0.16, 0.10), 0.6)
    for o in list(OBJS):
        if o.get("cut") and not o.name.startswith("hull"):
            bb = [o.matrix_world @ Vector(c) for c in o.bound_box]
            if sum(p.y for p in bb) / 8 < 0: o.hide_render = True; o["tag"] = "x"
    hull = bpy.data.objects["hull"]; me = hull.data; me.materials.append(Ms)
    bm = bmesh.new(); bm.from_mesh(me)
    for co, no in (((0, 0, 0.25), (0, 0, 1)), ((0, 0, 0), (0, 1, 0)), ((0, 0, -1.2), (0, 0, 1))):
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=co, plane_no=no)
    dead = [f for f in bm.faces if (lambda c: c.z > 0.25 - 1e-4 or (c.y < 1e-4 and c.z > -1.2 - 1e-4))(f.calc_center_median())]
    bmesh.ops.delete(bm, geom=dead, context='FACES'); bm.to_mesh(me); bm.free()
    sm = hull.modifiers.new("shell", 'SOLIDIFY'); sm.thickness = 0.10; sm.offset = -1; sm.use_rim = True; sm.material_offset_rim = 1
    # right pods: front half removed to show cup + coil
    for o in list(OBJS):
        if o.get("pod") and o.name in ("pod_1_shell", "pod_3_shell"):
            bb = [o.matrix_world @ Vector(c) for c in o.bound_box]; cx = sum(p.x for p in bb) / 8
            V, F = boxv((0, 0, 0), (1, 1, 1)); cm = mk("pc_" + o.name, [(cx + 1.0 + v.x * 2, v.y * 6 + 0.5, -(sum(p.y for p in bb) / 8) + v.z * 3) for v in V], F, Mr, tag="cutter")
            cm.hide_render = True
            b = o.modifiers.new("cut", 'BOOLEAN'); b.operation = 'DIFFERENCE'; b.object = cm; b.solver = 'EXACT'
            try: b.material_mode = 'TRANSFER'
            except Exception: pass

CALLOUTS = [(1, "Кокпит: 2 кресла", (8.45, -0.3, 0.6)), (2, "Пульт (фотонный + ручной резерв)", (9.2, 0.1, -0.5)), (3, "Салон: 12 кресел", (5.4, -0.2, 0.55)),
            (4, "Тамбур, гермоперегородка", (7.65, -0.4, 0.0)), (5, "Люк восьмигранный (левый борт)", (7.65, 0.3, -2.4)), (6, "Баки аргона (2)", (-1.2, -0.3, 1.1)),
            (7, "Грузовой отсек 10 т", (-0.3, 0.2, -0.6)), (8, "Заряды, 2 инжектора", (-6, 0.4, 0)), (9, "Пуск, 2 криокулера", (-6.3, -0.4, 0.6)),
            (10, "Гондола 1: чаша и катушка", (4.6, 1.3, 3.5)), (11, "Гондола 3 в обтекателе плеча", (-4.6, 0.6, 5.0)), (12, "Теневой экран W", (5.2, 1.3, 2.65)),
            (13, "Основная опора шасси", (-3.4, -2.6, 1.8)), (14, "Поворотное крыло", (0.5, -1.42, 7.0)), (15, "Плечо (щель консоли, синхровал)", (0.3, -1.42, 3.4)),
            (16, "Передняя опора", (8, -2.4, 0))]

def shot(name, look, var, mode, direction, ground=False, thrust=True, lens=50, cut=False, hangar=False, man=False, margin=0.05):
    clear(); MATS.clear(); cam = stage(ground)
    G = build(look, var, mode, thrust and not ground, cut)
    extra = []
    if man: mannequin((10.2, -3.0, -3.2))
    if cut: cutaway()
    if hangar:
        bpy.context.view_layer.update(); pts = [o.matrix_world @ Vector(c) for o in OBJS if o.get("tag") == "model" for c in o.bound_box]
        lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts))); hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
        c = (lo + hi) / 2; X, Y, Z = 20.2, 16.0, 5.4; z0 = lo.z
        Mw = mat("hwire", (0.35, 0.75, 0.95), 0.4, emit=3); Mg = mat("hglass", (0.5, 0.75, 0.9), 0.3, alpha=0.07)
        cs = [Vector((c.x + sx * X / 2, c.y + sy * Y / 2, z0 + sz * Z)) for sx in (-1, 1) for sy in (-1, 1) for sz in (0, 1)]
        for i in range(8):
            for k in range(i + 1, 8):
                if sum(1 for q in range(3) if (i >> q & 1) != (k >> q & 1)) == 1:
                    a, b = cs[i], cs[k]; me = bpy.data.meshes.new("e"); d = b - a; L_ = d.length
                    o = bpy.data.objects.new("edge", me); bpy.context.scene.collection.objects.link(o)
                    bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=0.04, radius2=0.04, depth=L_); bm.to_mesh(me); bm.free(); me.materials.append(Mw)
                    o.location = (a + b) / 2; o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = d.to_track_quat('Z', 'Y')
        me = bpy.data.meshes.new("hbox"); me.from_pydata([tuple(p) for p in cs], [], [(0, 2, 6, 4), (0, 1, 3, 2)]); me.materials.append(Mg)
        o = bpy.data.objects.new("hbox", me); bpy.context.scene.collection.objects.link(o); extra = [tuple(p) for p in cs]
    fit(cam, direction, lens, margin, tags=("model", "int", "man") if (man or cut) else ("model",), extra=extra)
    render(name)
    if cut:
        sc = bpy.context.scene; res = []
        for n, t, p in CALLOUTS:
            v = world_to_camera_view(sc, cam, J(p)); res.append({"n": n, "t": t, "x": round(v.x * 100, 2), "y": round((1 - v.y) * 100, 2)})
        json.dump(res, open(os.path.join(OUT, name + "_callouts.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=0)

F34 = (1.0, -1.05, 0.55)    # Blender: 3/4 front from the right, above
SHOTS = {
    "lander_skat_v1": dict(look="B", var=1, mode="hover", direction=F34),
    "lander_skat_v1_ground": dict(look="B", var=1, mode="hover", direction=(1.0, 0.75, 0.16), ground=True, man=True, lens=35),
    "lander_skat_v2": dict(look="B", var=2, mode="hover", direction=F34),
    "lander_skat_v3": dict(look="B", var=3, mode="hover", direction=F34),
    "lander_skat_v4": dict(look="B", var=4, mode="hover", direction=(1.0, -0.9, 0.22)),
    "lander_skat_v1_flight": dict(look="B", var=1, mode="entry", direction=(0.12, -1.0, 0.12), lens=70, margin=0.04),
    "lander_skat_v1_stowed": dict(look="B", var=1, mode="stowed", direction=(0.9, -1.0, 0.9), thrust=False, hangar=True),
    "lander_skat_cut": dict(look="B", var=1, mode="hover", direction=(0.75, -0.9, 1.15), thrust=False, cut=True, lens=45, margin=0.03),
    "lander_crystal_hover": dict(look="A", var=1, mode="hover", direction=F34),
    "lander_crystal_flight": dict(look="A", var=1, mode="entry", direction=(0.12, -1.0, 0.12), lens=70, margin=0.04),
}

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
names = argv or list(SHOTS)
L("start " + ",".join(names))
for n in names:
    try: shot(n, **SHOTS[n])
    except Exception: L("FAIL " + n + "\n" + traceback.format_exc())
L("done")
