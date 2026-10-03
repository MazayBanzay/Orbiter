# Crew suit fabric as a TILED hexagonal weave (2026-10-03, user: "добавь нити"): thin threads need far more texels
# than one texture over the whole suit gives (0.75 mm/texel). So each colour group gets a small seamless tile
# (TILE_PX square, TILE_MM across) repeated over the cloth: the garment's uniform UV is scaled so 1 UV unit = one tile.
# The colour boundary stays geometry (the cut faces); each group (white / accent) has its own tile in its colour.
# Writes textures/crewsuit_tile_{white,accent}.png + _norm.png, saves astronavigator_crewsuit.blend, renders close-ups.
import bpy, os, sys, math
import numpy as np
from mathutils import Vector
HERE = os.path.dirname(__file__); TEXD = os.path.join(HERE, "textures")
LOG = os.path.join(HERE, "crewsuit_tile.log"); log = []
def mark(t):
    log.append(t); open(LOG, "w").write(chr(10).join(log))
PREVIEW = os.environ.get("TILE_PREVIEW", "")            # a tag: render only, do not save the blend
ACCENT = tuple(float(x) for x in os.environ.get("TILE_ACCENT", "0.0,0.085,0.10").split(","))   # dark teal (linear)
WHITE = (0.80, 0.80, 0.78)
HEX_MM = float(os.environ.get("TILE_HEX", "6.0"))       # cell across flats
LINE_MM = float(os.environ.get("TILE_LINE", "0.35"))    # thread width
DEPTH = float(os.environ.get("TILE_DEPTH", "0.00008"))  # m, groove depth
SHADE = float(os.environ.get("TILE_SHADE", "0.12"))     # thread darkening
TILE_PX = 1024
# a square tile that repeats the hex lattice exactly: 4 cells across x (pitch 1.5 R), 7 rows up y (pitch sqrt3 R / 2 * 2)
R = HEX_MM / math.sqrt(3)                               # circumradius (flat-top hexagons)
KX, KY = 8, 7                                           # columns (1.5 R each) and rows (sqrt3 R each): 12 R vs 12.12 R
TILE_MM = KX * 1.5 * R

def hex_tile():
    n = TILE_PX; u = (np.arange(n) + 0.5) / n
    X, Y = np.meshgrid(u * KX * 1.5 * R, u * KY * math.sqrt(3) * R)          # mm, the tile stretched 1 % in y to close
    # nearest centre on the flat-top hex lattice (axial coordinates), periodic over the tile
    q = X / (1.5 * R); r_ = (Y / (math.sqrt(3) * R)) - q / 2
    x_, z_ = q, r_; y_ = -x_ - z_
    rx, ry, rz = np.round(x_), np.round(y_), np.round(z_)
    dx, dy, dz = np.abs(rx - x_), np.abs(ry - y_), np.abs(rz - z_)
    fx = (dx > dy) & (dx > dz); fy = ~fx & (dy > dz)
    rx = np.where(fx, -ry - rz, rx); rz = np.where(~fx & ~fy, -rx - ry, rz)
    cx = rx * 1.5 * R; cy = (rz + rx / 2) * math.sqrt(3) * R
    px, py = X - cx, Y - cy
    # distance to the hexagon's edge (flat-top: the apothem directions 30, 90, 150 deg)
    ap = R * math.sqrt(3) / 2
    d = np.maximum.reduce([np.abs(py), np.abs(px * math.sqrt(3) / 2 + py / 2), np.abs(px * math.sqrt(3) / 2 - py / 2)])
    edge = ap - d                                                            # mm to the thread
    g = np.clip(1 - edge / (LINE_MM / 2), 0, 1); g = g * g * (3 - 2 * g)      # the thread, soft-edged
    return g

try:
    mark("start")
    g = hex_tile()
    texel_m = TILE_MM / 1000 / TILE_PX
    H = -DEPTH * g
    gy, gx = np.gradient(H, texel_m)
    nrm = np.stack([-gx, -gy, np.ones_like(H)], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    def save(name, arr, srgb):
        im = bpy.data.images.new(name, TILE_PX, TILE_PX, alpha=False)
        if not srgb: im.colorspace_settings.name = 'Non-Color'
        if srgb: arr = np.where(arr <= 0.0031308, 12.92 * arr, 1.055 * np.power(np.maximum(arr, 0), 1 / 2.4) - 0.055)   # linear colour -> sRGB file
        im.pixels.foreach_set(np.concatenate([arr, np.ones((TILE_PX, TILE_PX, 1), np.float32)], -1).astype(np.float32).ravel())
        im.filepath_raw = os.path.join(TEXD, name + ".png"); im.file_format = 'PNG'; im.save(); return im
    shade = (1 - SHADE * g)[..., None].astype(np.float32)
    imgs = {"white": save("crewsuit_tile_white", np.array(WHITE, np.float32) * shade, True),
            "accent": save("crewsuit_tile_accent", np.array(ACCENT, np.float32) * shade, True)}
    nimg = save("crewsuit_tile_norm", (nrm * 0.5 + 0.5).astype(np.float32), False)
    mark("tile %.1f mm, %d px, %.3f mm/px, thread %.2f mm" % (TILE_MM, TILE_PX, texel_m * 1000, LINE_MM))
    bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
    cov = bpy.data.objects["Coverall"]; me = cov.data
    def ld(n_, srgb=True):
        im = bpy.data.images.load(os.path.join(TEXD, n_ + ".png"))
        if not srgb: im.colorspace_settings.name = 'Non-Color'
        return im
    imgs = {"white": ld("crewsuit_tile_white"), "accent": ld("crewsuit_tile_accent")}; nimg = ld("crewsuit_tile_norm", False)
    for o in bpy.context.view_layer.objects: o.select_set(False)
    bpy.context.view_layer.objects.active = cov; cov.select_set(True)
    for m in list(cov.modifiers):
        if m.type == 'ARMATURE': m.show_viewport = False
    old = [l.name for l in me.uv_layers]
    uv = me.uv_layers.new(name="Tile"); me.uv_layers.active = uv
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.0, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    uv = me.uv_layers["Tile"]; me.calc_loop_triangles()
    k = []
    for t in list(me.loop_triangles)[::5]:
        u_ = [uv.data[l].uv for l in t.loops]; a2 = abs((u_[1] - u_[0]).cross(u_[2] - u_[0])) / 2
        if a2 > 1e-12: k.append(math.sqrt(t.area / a2))
    m_per_uv = float(np.median(k)); sc_ = m_per_uv / (TILE_MM / 1000)
    for l in uv.data: l.uv = l.uv * sc_                                       # 1 UV unit = one tile
    for n_ in old: me.uv_layers.remove(me.uv_layers[n_])
    me.uv_layers.active = me.uv_layers["Tile"]
    for m in list(cov.modifiers):
        if m.type == 'ARMATURE': m.show_viewport = True
    mark("UV scaled x%.1f (tile repeats)" % sc_)
    for m in me.materials:
        nt = m.node_tree
        for n in [n for n in nt.nodes if n.type in ('TEX_IMAGE', 'NORMAL_MAP')]: nt.nodes.remove(n)
        b = next(x for x in nt.nodes if x.type == 'BSDF_PRINCIPLED')
        t = nt.nodes.new("ShaderNodeTexImage"); t.image = imgs["accent" if "Red" in m.name else "white"]
        nt.links.new(t.outputs[0], b.inputs["Base Color"])
        tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = nimg; nm = nt.nodes.new("ShaderNodeNormalMap"); nm.uv_map = "Tile"
        nt.links.new(tn.outputs[0], nm.inputs["Color"]); nt.links.new(nm.outputs[0], b.inputs["Normal"])
        b.inputs["Roughness"].default_value = 0.75
    if not PREVIEW: bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend")); mark("saved")
    sys.path.insert(0, HERE); import render_util
    sc = bpy.context.scene; cam = render_util.setup_stage(); sc.render.resolution_x = sc.render.resolution_y = 900; sc.view_settings.exposure = -0.4
    for nm_, loc, tgt in (("tl_macro", (0.10, -0.30, 1.13), (0.07, -0.10, 1.10)), ("tl_close", (0.30, -0.55, 1.22), (0.06, -0.06, 1.12)),
                          ("tl_mid", (1.0, -1.6, 1.15), (0, 0, 1.0))):
        cam.location = loc; cam.data.lens = 50; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (Vector(tgt) - Vector(loc)).to_track_quat('-Z', 'Y')
        sc.render.filepath = os.path.join(HERE, "renders", nm_ + PREVIEW + ".png"); bpy.ops.render.render(write_still=True)
    mark("rendered")
except Exception:
    import traceback; log.append(traceback.format_exc()); open(LOG, "w").write(chr(10).join(log))
