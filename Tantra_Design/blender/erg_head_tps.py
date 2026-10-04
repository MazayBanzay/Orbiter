# Tantra: Erg Noor's head fitted to the user's drawings with a smooth landmark warp (thin-plate spline, U(r) = r).
# Landmarks on the model are found from its geometry; their targets come from the drawings (renders/erg/ref,
# git-ignored): the profile gives the midline (y, z), the front drawing (head turned 15 deg) the widths (x).
# The neck and shoulders are pinned, so only the head changes. A smooth warp has no seams or shelves.
# Output: renders/erg/tps_side.png, tps_front.png (pixel-aligned with the drawings) and erg_head.blend.
import bpy, os, math, traceback
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "renders", "erg", "ref"); OUT = os.path.join(HERE, "renders", "erg")
LOG = os.path.join(HERE, "erg_head_tps.log"); log = []
src = open(os.path.join(HERE, "erg_faces.py"), encoding="utf-8").read().split("\ntry:\n")[0]
g = {"__file__": os.path.join(HERE, "erg_faces.py")}; exec(src, g)

# ---- the drawings (743 x 1024 px each; same eye-to-chin, so one scale) ----
EYE_ROW, CHIN_ROW = 455.0, 800.0                    # profile.webp
S = 0.125 / (CHIN_ROW - EYE_ROW)                    # m per pixel: eye-to-chin 12.5 cm
F_EYE_ROW, F_EYE_MID, YAW = 460.0, 462.5, math.radians(15)   # front.webp
# profile landmarks (row; the column is read from the traced front line): read off the drawing
P_ROWS = {"glabella": 405, "nasion": 440, "nose_tip": 560, "subnasale": 592, "upper_lip": 640, "stomion": 660,
          "lower_lip": 680, "sulcus": 712, "pogonion": 752, "menton": 800}
GONION_P = (335.0, 700.0)                            # profile: the jaw's angle (col, row)
EAR_P = (262.0, 488.0)                               # profile: the ear's centre
# front contours (row: col) - image-left is his right side (x < 0), image-right his left side
FRONT_L = {520: 200.0, 700: 240.0}                   # zygion row, gonion row
FRONT_R = {520: 645.0, 700: 612.0}
HAIR_TOP, HAIR_BACK = 0.016, 0.008


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)


def tps_fit(P, Q, lam=1e-6):
    n = len(P); K = np.linalg.norm(P[:, None] - P[None], axis=2) + lam * np.eye(n)
    A = np.zeros((n + 4, n + 4)); A[:n, :n] = K; A[:n, n] = 1; A[:n, n + 1:] = P; A[n, :n] = 1; A[n + 1:, :n] = P.T
    b = np.zeros((n + 4, 3)); b[:n] = Q - P
    return np.linalg.solve(A, b)


def tps_apply(coef, P, X):
    n = len(P); out = np.empty_like(X)
    for i in range(0, len(X), 4000):
        x = X[i:i + 4000]; U = np.linalg.norm(x[:, None] - P[None], axis=2)
        out[i:i + 4000] = x + U @ coef[:n] + coef[n] + x @ coef[n + 1:]
    return out


try:
    v = dict(g["VARIANTS_ALL"]["F_drawings"])
    # the drawings' face is narrower than F's: no extra jaw width, a milder square
    tg = [(t, w) for t, w in v["targets"] if t not in ("chin-width-incr", "head-scale-horiz-incr")]
    v["targets"] = [(t, 0.3 if t == "head-square" else w) for t, w in tg]
    g["log"] = log; g["build"](v)
    body = next(o for o in bpy.data.objects if o.type == 'MESH' and o.name.endswith('.body'))
    for o in [o for o in bpy.data.objects if o.type == 'MESH' and v["hair"] in o.name]: o.hide_render = True
    bpy.ops.object.select_all(action='DESELECT'); bpy.context.view_layer.objects.active = body; body.select_set(True)
    if body.data.shape_keys: bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
    M = body.matrix_world; me = body.data
    W = np.array([(M @ x.co)[:] for x in me.vertices])
    gi = body.vertex_groups['body'].index
    skin = np.array([any(gg.group == gi and gg.weight > 0 for gg in x.groups) for x in me.vertices])
    eyes = next(o for o in bpy.data.objects if o.type == 'MESH' and 'low-poly' in o.name)
    E = np.array([(eyes.matrix_world @ x.co)[:] for x in eyes.data.vertices]); eye_z = float(E[:, 2].mean())

    # ---- model midline profile ----
    mid = skin & (np.abs(W[:, 0]) < 0.006) & (W[:, 1] < -0.05)
    zs = np.arange(eye_z - 0.17, eye_z + 0.10, 0.002)
    yf = np.array([W[mid & (np.abs(W[:, 2] - z) < 0.003), 1].min() if (mid & (np.abs(W[:, 2] - z) < 0.003)).any() else np.nan for z in zs])
    ok = ~np.isnan(yf); zr = zs - eye_z
    def front_y(dz): return float(np.interp(dz, zr[ok], yf[ok]))
    def pick(lo, hi, fn):   # height (rel. to eye) in [lo, hi] where fn(yf) is extreme
        s_ = ok & (zr > lo) & (zr < hi); i = fn(yf[s_]); return float(zr[s_][i])
    m = {}
    m["nasion"] = pick(-0.008, 0.02, np.argmax); m["glabella"] = pick(0.005, 0.035, np.argmin)
    m["nose_tip"] = pick(-0.065, -0.02, np.argmin); m["subnasale"] = pick(m["nose_tip"] - 0.03, m["nose_tip"] - 0.006, np.argmax)
    m["upper_lip"] = pick(m["subnasale"] - 0.03, m["subnasale"] - 0.004, np.argmin)
    m["lower_lip"] = pick(m["upper_lip"] - 0.03, m["upper_lip"] - 0.008, np.argmin)
    m["stomion"] = pick(m["lower_lip"], m["upper_lip"], np.argmax)
    m["sulcus"] = pick(m["lower_lip"] - 0.03, m["lower_lip"] - 0.006, np.argmax)
    m["pogonion"] = pick(m["sulcus"] - 0.04, m["sulcus"] - 0.005, np.argmin)
    yc = front_y(m["pogonion"]); m["menton"] = float(zr[ok & (zr < m["pogonion"]) & (zr > -0.17) & (yf < yc + 0.012)].min())
    pf = np.load(os.path.join(REF, "prof_front.npy")); py, px = pf[:, 0].astype(float), pf[:, 1].astype(float)
    x_eye = float(np.interp(EYE_ROW, py, px)); y_anchor = front_y(0.0)
    def to_y(col): return y_anchor - (col - x_eye) * S
    def to_z(row): return eye_z - (row - EYE_ROW) * S
    P, Q = [], []
    for k, row in P_ROWS.items():
        zm = eye_z + m[k]; ym = front_y(m[k])
        zt = to_z(row); yt = to_y(float(np.interp(row, py, px)))
        if abs(ym - y_anchor) > 0.045 or abs(yt - ym) > 0.03:   # a bad pick on the model: leave that landmark out
            log.append("%-10s skipped (model pick %.1f mm off the face line)" % (k, (ym - y_anchor) * 1000)); continue
        P.append((0.0, ym, zm)); Q.append((0.0, yt, zt))
        log.append("%-10s model %+6.1f mm / %+6.1f  ->  drawing %+6.1f / %+6.1f" % (k, m[k] * 1000, (ym - y_anchor) * 1000, (zt - eye_z) * 1000, (yt - y_anchor) * 1000))
    # ---- lateral landmarks: zygion and gonion (x from the front drawing, seen 15 deg turned) ----
    ca, sa = math.cos(YAW), math.sin(YAW)
    u_e = float((E[:, 0] * ca - E[:, 1] * sa).mean())
    def x_from_front(col, y): return ((col - F_EYE_MID) * S + u_e + y * sa) / ca
    for name, zlo, zhi, ylo, yhi, frow in (("zygion", -0.035, -0.015, -1.0, y_anchor + 0.07, 520), ("gonion", -0.105, -0.08, y_anchor + 0.06, y_anchor + 0.13, 700)):
        for sd, cont in ((-1, FRONT_L), (1, FRONT_R)):
            sel = skin & (sd * W[:, 0] > 0.02) & (W[:, 2] > eye_z + zlo) & (W[:, 2] < eye_z + zhi) & (W[:, 1] > ylo) & (W[:, 1] < yhi)
            pm = W[sel][np.argmax(sd * W[sel, 0])]
            if name == "gonion": yt, zt = to_y(GONION_P[0]), to_z(GONION_P[1])
            else: yt, zt = pm[1], pm[2]
            xt = x_from_front(cont[frow], yt)
            P.append(tuple(pm)); Q.append((xt, yt, zt))
            log.append("%s %+d: x %.1f -> %.1f mm, y %.1f -> %.1f, z %.1f -> %.1f" % (name, sd, pm[0] * 1000, xt * 1000, (pm[1] - y_anchor) * 1000, (yt - y_anchor) * 1000, (pm[2] - eye_z) * 1000, (zt - eye_z) * 1000))
    # symmetric widths: average both sides' |x|
    for a_ in (len(P) - 4, len(P) - 2):
        xa = (abs(Q[a_][0]) + abs(Q[a_ + 1][0])) / 2
        Q[a_] = (-xa, Q[a_][1], Q[a_][2]); Q[a_ + 1] = (xa, Q[a_ + 1][1], Q[a_ + 1][2])
    # ---- ears ----
    for sd in (-1, 1):
        lat = skin & (sd * W[:, 0] > 0.05) & (W[:, 2] > eye_z - 0.08) & (W[:, 2] < eye_z + 0.02)
        xm = float((sd * W[lat, 0]).max()); c = W[lat & (sd * W[:, 0] > xm - 0.012)].mean(0)
        P.append(tuple(c)); Q.append((c[0], to_y(EAR_P[0]), to_z(EAR_P[1])))
    # ---- skull: top and back under the drawn hair ----
    pb = np.load(os.path.join(REF, "prof_back.npy")); by, bx = pb[:, 0].astype(float), pb[:, 1].astype(float)
    crown = skin & (np.abs(W[:, 0]) < 0.01) & (W[:, 2] > eye_z)
    top = W[crown][np.argmax(W[crown, 2])]
    P.append(tuple(top)); Q.append((0.0, top[1], to_z(min(by.min(), py.min())) - HAIR_TOP))
    backz = eye_z + 0.03
    bsel = skin & (np.abs(W[:, 0]) < 0.01) & (np.abs(W[:, 2] - backz) < 0.006); back = W[bsel][np.argmax(W[bsel, 1])]
    P.append(tuple(back)); Q.append((0.0, to_y(float(np.interp(EYE_ROW - 0.03 / S, by, bx))) - HAIR_BACK, back[2]))
    for s_name, (pp, qq) in zip(("ear-", "ear+", "vertex", "back"), list(zip(P[-4:], Q[-4:]))):
        log.append("%-6s moves %.1f mm" % (s_name, np.linalg.norm(np.array(qq) - np.array(pp)) * 1000))
    # ---- pins: the eyes stay (they define the anchor), neck and shoulders stay ----
    for e in (E[E[:, 0] < 0].mean(0), E[E[:, 0] > 0].mean(0)): P.append(tuple(e)); Q.append(tuple(e))
    rng = np.random.default_rng(3)
    low = np.where(skin & (W[:, 2] < eye_z - 0.22) & (W[:, 2] > eye_z - 0.45))[0]
    for i in rng.choice(low, 60, replace=False): P.append(tuple(W[i])); Q.append(tuple(W[i]))
    P = np.array(P); Q = np.array(Q)
    coef = tps_fit(P, Q)
    W2 = tps_apply(coef, P, W)
    # below the head the warp fades to nothing (the pins already hold it; this guards the body far away)
    f = smooth(eye_z - 0.30, eye_z - 0.20, W[:, 2])[:, None]
    W2 = W + (W2 - W) * f
    Mi = M.inverted()
    for x, p_ in zip(me.vertices, W2): x.co = Mi @ Vector(p_)
    me.update()
    for o in bpy.data.objects:
        if o.type != 'MESH' or o is body: continue
        X = np.array([(o.matrix_world @ x.co)[:] for x in o.data.vertices])
        if not len(X): continue
        X2 = tps_apply(coef, P, X); f2 = smooth(eye_z - 0.30, eye_z - 0.20, X[:, 2])[:, None]; X2 = X + (X2 - X) * f2
        Oi = o.matrix_world.inverted()
        for x, p_ in zip(o.data.vertices, X2): x.co = Oi @ Vector(p_)
        o.data.update()
    res = np.linalg.norm(tps_apply(coef, P, P) - Q, axis=1)
    log.append("landmarks %d, residual max %.2f mm" % (len(P), res.max() * 1000))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "erg_head.blend"))

    # ---- renders aligned with the drawings ----
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 743, 1024
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    nt = world.node_tree; bg = nt.nodes.get("Background") or nt.nodes.new("ShaderNodeBackground")
    wo = nt.nodes.get("World Output") or nt.nodes.new("ShaderNodeOutputWorld"); nt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.25, 0.26, 0.28, 1); bg.inputs[1].default_value = 0.6
    for nm, loc, en in (("Key", (-1.0, -1.4, eye_z + 0.6), 500), ("Fill", (1.2, -1.0, eye_z), 160), ("Rim", (0.2, 1.4, eye_z + 0.4), 200)):
        l = bpy.data.lights.new(nm, 'AREA'); l.energy = en; l.size = 1.2; o = bpy.data.objects.new(nm, l); o.location = loc
        sc.collection.objects.link(o); o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (Vector((0, 0, eye_z)) - Vector(loc)).to_track_quat('-Z', 'Y')
    cam_d = bpy.data.cameras.new("Cam"); cam_d.type = 'ORTHO'; cam_d.ortho_scale = 1024 * S; cam_d.sensor_fit = 'VERTICAL'
    cam = bpy.data.objects.new("Cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam; cam.rotation_mode = 'QUATERNION'
    cy_m = y_anchor - (371.5 - x_eye) * S; cz_m = eye_z - (512 - EYE_ROW) * S
    cam.location = (-1.0, cy_m, cz_m); cam.rotation_quaternion = (Vector((1, 0, 0))).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(OUT, "tps_side.png"); bpy.ops.render.render(write_still=True)
    u_c = u_e + (371.5 - F_EYE_MID) * S; z_c = eye_z - (512 - F_EYE_ROW) * S
    fdir = Vector((sa, ca, 0.0)); P_c = Vector((u_c * ca, -u_c * sa, z_c))
    cam.location = P_c - fdir * 1.0; cam.rotation_quaternion = fdir.to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(OUT, "tps_front.png"); bpy.ops.render.render(write_still=True)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
