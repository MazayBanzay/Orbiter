# Tantra: Erg Noor's head fitted to the user's drawings (renders/erg/ref: profile.webp, front.webp; git-ignored).
# Start: the MakeHuman male of erg_faces.py variant F (its topology keeps blink, skinning and animation working).
# Then the face's midline is moved onto the profile traced from the drawing (prof_front.npy: per drawing row, the
# front-most pencil pixel), with a lateral falloff narrow at the nose and wide at forehead and chin, fading towards
# the ears. Scale: eye-to-chin 12.5 cm (an adult man) = the drawing's eye-to-chin distance; anchors: eye line, nasion.
# Output: renders/erg/fit_*.png (orthographic side and front, with the drawing's lines laid over) + erg_head.blend.
import bpy, os, math, traceback
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.join(HERE, "renders", "erg", "ref"); OUT = os.path.join(HERE, "renders", "erg")
LOG = os.path.join(HERE, "erg_head_fit.log"); log = []
src = open(os.path.join(HERE, "erg_faces.py"), encoding="utf-8").read().split("\ntry:\n")[0]
g = {"__file__": os.path.join(HERE, "erg_faces.py")}; exec(src, g)   # EYE, CHEEK, VARIANTS_ALL, build, ...

# drawing landmarks (profile.webp, 743 x 1024 px): eye centre row, chin bottom row; the face looks to image-right
EYE_ROW, CHIN_ROW = 455, 800
HAIRLINE_ROW = 235            # where the forehead leaves the hair
S = 0.125 / (CHIN_ROW - EYE_ROW)   # metres per drawing pixel


def bake(body):
    bpy.ops.object.select_all(action='DESELECT'); bpy.context.view_layer.objects.active = body; body.select_set(True)
    if body.data.shape_keys: bpy.ops.object.shape_key_remove(all=True, apply_mix=True)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)


try:
    v = dict(g["VARIANTS_ALL"]["F_drawings"]); v["hair"] = "short04"
    g["log"] = log; g["build"](v)
    body = next(o for o in bpy.data.objects if o.type == 'MESH' and o.name.endswith('.body'))
    for o in [o for o in bpy.data.objects if o.type == 'MESH' and o is not body and v["hair"] in o.name]: o.hide_render = True   # hair later
    bake(body)
    M = body.matrix_world; Mi = M.inverted(); me = body.data
    W = np.array([(M @ x.co)[:] for x in me.vertices])
    # visible skin only (the 'body' vertex group; the rest are MakeHuman helpers)
    gi = body.vertex_groups['body'].index
    skin = np.array([any(gg.group == gi and gg.weight > 0 for gg in x.groups) for x in me.vertices])
    eyes = next(o for o in bpy.data.objects if o.type == 'MESH' and 'low-poly' in o.name)
    E = np.array([(eyes.matrix_world @ x.co)[:] for x in eyes.data.vertices]); eye_z = float(E[:, 2].mean())
    log.append("eye z %.4f, scale %.3e m/px" % (eye_z, S))
    # model midline profile: front-most (min y) skin vertex per 2 mm of height, |x| < 4 mm
    mid = skin & (np.abs(W[:, 0]) < 0.006) & (W[:, 2] > eye_z - 0.20) & (W[:, 2] < eye_z + 0.12) & (W[:, 1] < -0.05)   # the face half only
    zs = np.arange(eye_z - 0.17, eye_z + 0.10, 0.002)
    def model_front(Wm):
        out = np.full(len(zs), np.nan)
        for i, z in enumerate(zs):
            s_ = mid & (np.abs(Wm[:, 2] - z) < 0.003)
            if s_.any(): out[i] = Wm[s_, 1].min()
        return out
    yf = model_front(W)
    # ---- 1. vertical: eye line, nose tip and chin bottom of the model onto the drawing's ----
    pf = np.load(os.path.join(REF, "prof_front.npy")); py, px = pf[:, 0].astype(float), pf[:, 1].astype(float)
    def dz_of_row(r): return -(r - EYE_ROW) * S
    r_nt = float(py[(py > 520) & (py < 610)][np.argmax(px[(py > 520) & (py < 610)])])
    ch = (py > 690) & (py < 795); px_ch = px[ch].max()
    r_mn = float(py[(py > 690) & (py < 900) & (px > px_ch - 0.012 / S)].max())
    def valid(a): return ~np.isnan(a)
    vz = valid(yf)
    z_rel = zs - eye_z
    sel = vz & (z_rel > -0.065) & (z_rel < -0.015); m_nt = float(z_rel[sel][np.argmin(yf[sel])])
    selc = vz & (z_rel > -0.125) & (z_rel < -0.080); y_c = float(yf[selc].min())
    m_mn = float(z_rel[vz & (z_rel > -0.15) & (z_rel < -0.08) & (yf < y_c + 0.012)].min())
    d_nt, d_mn = dz_of_row(r_nt), dz_of_row(r_mn)
    log.append("landmarks (mm from the eye line): nose tip model %.1f drawing %.1f; chin bottom model %.1f drawing %.1f"
               % (m_nt * 1000, d_nt * 1000, m_mn * 1000, d_mn * 1000))
    def remap(zr):   # model height (rel. to the eye) -> drawing height, piecewise linear; below the chin a shift fading out
        if zr >= 0: return zr
        if zr >= m_nt: return zr * d_nt / m_nt
        if zr >= m_mn: return d_nt + (zr - m_nt) * (d_mn - d_nt) / (m_mn - m_nt)
        return zr + (d_mn - m_mn) * max(0.0, 1 - (m_mn - zr) / 0.06)
    yc = float(np.nanmedian(yf[vz]))
    for i in range(len(W)):
        if not skin[i] or W[i, 2] > eye_z or W[i, 2] < eye_z - 0.26: continue
        wd = 1 - float(smooth(0.0, 0.12, W[i, 1] - yc)); wl = math.exp(-0.5 * (W[i, 0] / 0.09) ** 2)
        W[i, 2] += (eye_z + remap(W[i, 2] - eye_z) - W[i, 2]) * wd * wl
    VZ = {}
    for o in bpy.data.objects:
        if o.type == 'MESH' and o is not body:
            for x in o.data.vertices:
                p_ = o.matrix_world @ x.co
                if p_.z < eye_z:
                    wd = 1 - float(smooth(0.0, 0.12, p_.y - yc)); wl = math.exp(-0.5 * (p_.x / 0.09) ** 2)
                    p_.z += (eye_z + remap(p_.z - eye_z) - p_.z) * wd * wl; x.co = o.matrix_world.inverted() @ p_
    yf = model_front(W)
    # ---- 2. the front line: the drawing's profile, anchored at the eye row on the model's front line there ----
    x_eye = float(np.interp(EYE_ROW, py, px))
    y_anchor = float(np.interp(eye_z, zs[~np.isnan(yf)], yf[~np.isnan(yf)]))
    rows = EYE_ROW - (zs - eye_z) / S                      # drawing row for each model height
    ok = (rows > HAIRLINE_ROW) & (rows < r_mn - 6) & ~np.isnan(yf)
    target = y_anchor - (np.interp(rows, py, px) - x_eye) * S
    d = np.where(ok, target - yf, 0.0)
    for i in range(0, len(zs), 5):
        log.append("  z-eye %+6.1f mm row %6.1f  model %s  target %+.4f  ok %d" % ((zs[i] - eye_z) * 1000, rows[i], "%+.4f" % yf[i] if not np.isnan(yf[i]) else "  nan  ", target[i], ok[i]))
    d = np.convolve(d, np.ones(5) / 5, mode='same')          # 1 cm of smoothing along the height
    log.append("profile delta: min %.1f mm, max %.1f mm over %d rows" % (d.min() * 1000, d.max() * 1000, ok.sum()))
    # lateral falloff: narrow at the nose and lips, wide at forehead and chin
    def sigma(z):
        r = z - eye_z
        return float(np.interp(r, [-0.15, -0.11, -0.075, -0.05, -0.02, 0.0, 0.05, 0.10],
                                  [0.040, 0.034, 0.024, 0.016, 0.014, 0.022, 0.050, 0.060]))
    head = skin & (W[:, 2] > eye_z - 0.21)
    dz = np.interp(W[:, 2], zs, d, left=0, right=0)
    sig = np.array([sigma(z) for z in W[:, 2]])
    ymid = np.interp(W[:, 2], zs[~np.isnan(yf)], yf[~np.isnan(yf)])
    w_lat = np.exp(-0.5 * (W[:, 0] / sig) ** 2)
    w_depth = 1 - smooth(0.0, 0.10, W[:, 1] - ymid)           # the face surface moves, the back of the head stays
    move = np.where(head, dz * w_lat * w_depth, 0.0)
    W2 = W.copy(); W2[:, 1] += move
    for x, p in zip(me.vertices, W2): x.co = Mi @ Vector(p)
    me.update()
    # the eyes, brows and lashes ride with the skin around them
    for o in bpy.data.objects:
        if o.type != 'MESH' or o is body: continue
        for x in o.data.vertices:
            p = o.matrix_world @ x.co
            r = float(np.interp(p.z, zs, d, left=0, right=0)) * math.exp(-0.5 * (p.x / sigma(p.z)) ** 2)
            ym = float(np.interp(p.z, zs[~np.isnan(yf)], yf[~np.isnan(yf)]))
            r *= 1 - float(smooth(0.0, 0.10, p.y - ym))
            x.co = o.matrix_world.inverted() @ (p + Vector((0, r, 0)))
        o.data.update()
    # ---- 3. the skull inside the drawn hair: back of the head and the top (hair thickness taken off) ----
    HAIR_BACK, HAIR_TOP = 0.008, 0.016       # m: the swept-back hair stands higher on top than at the back
    pb = np.load(os.path.join(REF, "prof_back.npy")); by, bx = pb[:, 0].astype(float), pb[:, 1].astype(float)
    def to_y(col): return y_anchor - (col - x_eye) * S
    top_row = float(min(by.min(), py.min()))
    z_top_t = eye_z + (EYE_ROW - top_row) * S - HAIR_TOP
    W = W2
    crown = skin & (W[:, 2] > eye_z - 0.02)
    z_top_m = float(W[crown, 2].max()); k_top = (z_top_t - eye_z) / (z_top_m - eye_z)
    yb_pivot = y_anchor + 0.09                # behind this the skull is scaled towards the drawn back line
    for i in np.where(skin & (W[:, 2] > eye_z - 0.13))[0]:
        z = W[i, 2]
        if z > eye_z:   # the top comes down behind the hairline only: the forehead keeps the drawn line
            wb = float(smooth(y_anchor + 0.0, y_anchor + 0.11, W[i, 1]))   # a long, soft transition: no shelf on the forehead
            W[i, 2] = eye_z + (z - eye_z) * (1 + (k_top - 1) * float(smooth(0.0, 0.06, z - eye_z)) * wb)
    zr = np.arange(eye_z - 0.12, z_top_t, 0.003)
    yb_m = np.array([W[skin & (np.abs(W[:, 2] - z) < 0.004) & (np.abs(W[:, 0]) < 0.03), 1].max() if (skin & (np.abs(W[:, 2] - z) < 0.004) & (np.abs(W[:, 0]) < 0.03)).any() else np.nan for z in zr])
    yb_t = np.array([to_y(float(np.interp(EYE_ROW - (z - eye_z) / S, by, bx))) - HAIR_BACK for z in zr])
    kb = np.where(np.isnan(yb_m), 1.0, (yb_t - yb_pivot) / np.maximum(yb_m - yb_pivot, 1e-3))
    kb = np.where(zr > eye_z - 0.055, kb, 1.0)   # above the nape cut only (lower down the drawn line is the collar)
    kb = np.clip(np.convolve(kb, np.ones(5) / 5, mode='same'), 0.55, 1.0)   # the skull only gets shorter
    for i in np.where(skin & (W[:, 2] > eye_z - 0.12) & (W[:, 1] > yb_pivot))[0]:
        k = float(np.interp(W[i, 2], zr, kb)); W[i, 1] = yb_pivot + (W[i, 1] - yb_pivot) * k
    log.append("skull: top %.1f mm lower (k %.3f); back scale %.3f..%.3f" % ((z_top_m - z_top_t) * 1000, k_top, kb.min(), kb.max()))
    # ---- 4. the ears where the drawing has them ----
    ear_t = np.array([0.0, to_y(262.0), eye_z - (488 - EYE_ROW) * S])          # drawn ear centre (col, row) -> model
    for sd in (-1, 1):
        lat = skin & (sd * W[:, 0] > 0.05) & (W[:, 2] > eye_z - 0.08) & (W[:, 2] < eye_z + 0.02)
        xm = float((sd * W[lat, 0]).max()); shell = lat & (sd * W[:, 0] > xm - 0.012)   # the ear shell: the outermost 12 mm
        c = W[shell].mean(0)
        dv = np.array([0.0, ear_t[1] - c[1], ear_t[2] - c[2]])
        r = np.linalg.norm(W - c, axis=1); w = 1 - smooth(0.022, 0.055, r)
        W[skin] += (w[:, None] * dv)[skin]
        log.append("ear %+d moved %.1f mm back, %.1f mm up" % (sd, dv[1] * 1000, dv[2] * 1000))
    # ---- 5. widths from the front drawing (front.webp: same eye-to-chin as the profile, so the same scale; the head
    #         turned ~15 deg to image right). Per row: face width between the drawn contours vs the model's silhouette
    #         seen from the same direction (ears left out); the face is scaled across, three passes. ----
    F_EYE_ROW, F_EYE_MID, YAW = 460.0, 462.5, math.radians(15)
    fr = np.load(os.path.join(REF, "front_R.npy")); fry, frx = fr[:, 0].astype(float), fr[:, 1].astype(float)
    FL = np.array([(420, 225), (470, 205), (520, 200), (570, 205), (620, 210), (660, 220), (700, 240), (730, 290), (760, 350), (790, 440), (805, 520)], float)
    ca, sa = math.cos(YAW), math.sin(YAW)
    ears = []
    for sd in (-1, 1):
        lat = skin & (sd * W[:, 0] > 0.05) & (W[:, 2] > eye_z - 0.08) & (W[:, 2] < eye_z + 0.02)
        xm = float((sd * W[lat, 0]).max()); ears.append(W[lat & (sd * W[:, 0] > xm - 0.012)].mean(0))
    rows_f = np.arange(430, 800, 6.0)
    for it in range(3):
        noear = skin & (np.linalg.norm(W - ears[0], axis=1) > 0.035) & (np.linalg.norm(W - ears[1], axis=1) > 0.035)
        u = W[:, 0] * ca - W[:, 1] * sa
        u_e = float(np.mean([e[0] * ca - e[1] * sa for e in ears]) * 0) + float((E[:, 0] * ca - E[:, 1] * sa).mean())
        pxm = F_EYE_MID + (u - u_e) / S; rowm = F_EYE_ROW - (W[:, 2] - eye_z) / S
        ks = []
        for r in rows_f:
            band = noear & (np.abs(rowm - r) < 3.5) & (W[:, 1] < y_anchor + 0.12)
            if band.sum() < 4: ks.append(1.0); continue
            wm = pxm[band].max() - pxm[band].min()
            wd = float(np.interp(r, fry, frx)) - float(np.interp(r, FL[:, 0], FL[:, 1]))
            ks.append(wd / wm)
        ks = np.array(ks); ks = np.convolve(np.pad(ks, 4, mode='edge'), np.ones(9) / 9, mode='valid')   # ~3 cm of smoothing
        ks = np.clip(ks, 0.65, 1.3)
        zk = eye_z - (rows_f - F_EYE_ROW) * S
        # beyond the traced rows the change fades out smoothly: up the forehead (6 cm) and down into the neck (4 cm)
        zk = np.concatenate([[zk.min() - 0.04], zk, [zk.max() + 0.06]]); ks = np.concatenate([[1.0], ks, [1.0]])
        order = np.argsort(zk)
        for i in np.where(skin & (W[:, 2] > eye_z - 0.20) & (W[:, 2] < eye_z + 0.13))[0]:
            k = float(np.interp(W[i, 2], zk[order], ks[order], left=1.0, right=1.0))
            W[i, 0] *= k
        log.append("front widths pass %d: k %.3f..%.3f" % (it, ks.min(), ks.max()))
    for x, p_ in zip(me.vertices, W): x.co = Mi @ Vector(p_)
    me.update()
    W2 = W
    yf2 = model_front(W2)
    err = np.where(ok, target - yf2, np.nan)
    log.append("fit error after: mean %.1f mm, max %.1f mm" % (np.nanmean(np.abs(err)) * 1000, np.nanmax(np.abs(err)) * 1000))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "erg_head.blend"))

    # orthographic side view, framed so 1 drawing pixel = S metres: the drawing can be laid straight over
    sc = bpy.context.scene
    engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in engines else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 743, 1024
    world = bpy.data.worlds.new("W"); sc.world = world; world.use_nodes = True
    nt = world.node_tree; bg = nt.nodes.get("Background") or nt.nodes.new("ShaderNodeBackground")
    wo = nt.nodes.get("World Output") or nt.nodes.new("ShaderNodeOutputWorld"); nt.links.new(bg.outputs[0], wo.inputs[0])
    bg.inputs[0].default_value = (0.9, 0.9, 0.9, 1); bg.inputs[1].default_value = 0.8
    for nm, loc, en in (("Key", (-1.0, -1.2, eye_z + 0.5), 400), ("Fill", (-1.5, 0.8, eye_z), 150)):
        l = bpy.data.lights.new(nm, 'AREA'); l.energy = en; l.size = 1.5; o = bpy.data.objects.new(nm, l); o.location = loc
        sc.collection.objects.link(o); o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = (Vector((0, 0, eye_z)) - Vector(loc)).to_track_quat('-Z', 'Y')
    cam_d = bpy.data.cameras.new("Cam"); cam_d.type = 'ORTHO'; cam_d.ortho_scale = 1024 * S   # vertical fit (portrait)
    cam = bpy.data.objects.new("Cam", cam_d); sc.collection.objects.link(cam); sc.camera = cam; cam_d.sensor_fit = 'VERTICAL'
    # image centre (371.5, 512) in drawing px -> model point: the eye row/anchor column mapping
    cy_m = y_anchor - (371.5 - x_eye) * S; cz_m = eye_z - (512 - EYE_ROW) * S
    cam.location = (-1.0, cy_m, cz_m); cam.rotation_mode = 'QUATERNION'
    cam.rotation_quaternion = (Vector((0, cy_m, cz_m)) - cam.location).to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(OUT, "fit_side.png"); bpy.ops.render.render(write_still=True)
    log.append("camera side at y %.4f z %.4f, ortho %.4f" % (cy_m, cz_m, cam_d.ortho_scale))
    # the front drawing, pixel for pixel: orthographic, turned 15 deg, eye midpoint and eye row on the drawing's
    u_e = float((E[:, 0] * ca - E[:, 1] * sa).mean())
    u_c = u_e + (371.5 - F_EYE_MID) * S; z_c = eye_z - (512 - F_EYE_ROW) * S
    f = Vector((sa, ca, 0.0)); P_c = Vector((u_c * ca, -u_c * sa, z_c))
    cam.location = P_c - f * 1.0; cam.rotation_quaternion = f.to_track_quat('-Z', 'Y')
    sc.render.filepath = os.path.join(OUT, "fit_front.png"); bpy.ops.render.render(write_still=True)
except Exception:
    log.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
