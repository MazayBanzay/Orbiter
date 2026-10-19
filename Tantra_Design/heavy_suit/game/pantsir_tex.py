# Pantsir game asset: tiled surface textures for D3D9Client (colour + companions _norm/_refl/_rghn/_heat), generated
# with numpy and saved as PNG through Blender images (rows bottom-first, the old suit's normal-map convention:
# red = +u, green = +v of the file as Blender stores it). One texture tile = TILE metres of surface.
import bpy, os, math
import numpy as np
TILE = 0.25
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tex")


def _save(name, arr):
    os.makedirs(OUT, exist_ok=True); h, w = arr.shape[:2]
    im = bpy.data.images.new(name, w, h, alpha=False)
    if arr.ndim == 2: arr = np.repeat(arr[..., None], 3, 2)
    rgba = np.concatenate([np.clip(arr, 0, 1), np.ones((h, w, 1))], 2).astype(np.float32)
    im.pixels.foreach_set(rgba.ravel()); im.filepath_raw = os.path.join(OUT, name + ".png"); im.file_format = 'PNG'; im.save()
    return im


def _srgb(c): return np.array(c) / 255.0


def _noise(n, scale_px, seed, octaves=3):
    """periodic smooth noise (tiles seamlessly): band-limited random field by FFT, ~scale_px feature size"""
    rng = np.random.default_rng(seed); out = np.zeros((n, n))
    for o in range(octaves):
        f = np.fft.fftfreq(n); fx, fy = np.meshgrid(f, f); r = np.hypot(fx, fy) * (scale_px / (2 ** o))
        spec = np.exp(-(r - 1.0) ** 2 * 2.0) * (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n)))
        fld = np.real(np.fft.ifft2(spec)); out += fld / (fld.std() + 1e-9) / (2 ** o)
    return out / out.std()


def _normal(h_m, n, depth_scale=1.0):
    """height field in metres over one TILE -> tangent-space normal map (old suit convention)"""
    px = TILE / n; gy, gx = np.gradient(h_m * depth_scale, px)
    nn = np.stack([-gx, -gy, np.ones_like(h_m)], -1); nn /= np.linalg.norm(nn, axis=-1, keepdims=True)
    return nn * 0.5 + 0.5


def _hex_dimples(n, cell=0.0119):
    """hex dimple dome height (0..1) tiling in a TILE: 21 columns x 12 row-pairs (12 mm cells, 1 % anisotropy)"""
    k = int(round(TILE / cell)); R = TILE / (k * math.sqrt(3)); m = int(round(TILE / (3 * R)))
    sx, sy = TILE / k, TILE / m
    ys, xs = (np.mgrid[0:n, 0:n] + 0.5) * (TILE / n); best = None
    for ox, oy in ((0.0, 0.0), (sx / 2, sy / 2)):
        i = np.round((xs - ox) / sx); j = np.round((ys - oy) / sy); dx = xs - (ox + i * sx); dy = (ys - (oy + j * sy)) * (3 * R / sy)
        d2 = dx * dx + dy * dy
        best = (d2, dx, dy) if best is None else tuple(np.where(d2 < best[0], a, b) for a, b in zip((d2, dx, dy), best))
    _, dx, dy = best
    nrm = np.maximum(np.abs(dx), 0.5 * np.abs(dx) + 0.8660254 * np.abs(dy)) / (sx / 2)
    return np.clip(1 - nrm ** 2, 0, 1) ** 0.6


def _const(name, v, n=64):
    return _save(name, np.full((n, n), float(v)))


def _maps(key, metl, smooth, n=256, var=None):
    """Metalness workflow companions: _metl (.g metalness) and _rghn (.g SMOOTHNESS, 1 = mirror) - every material has both"""
    _const("surf_" + key + "_metl", metl)
    if var is None: _const("surf_" + key + "_rghn", smooth)
    else: _save("surf_" + key + "_rghn", np.clip(smooth + var, 0, 1))


def make_all():
    """D3D9Client "Metalness" shader set (Config/GC/OrbiterCrew/AstronavigatorPantsir.cfg): metals take their colour from
    the albedo; _metl .g = metalness, _rghn .g = smoothness, _heat .g = glow threshold (1 = glows first), _norm relief"""
    made = {}
    # rev. R photonic mirror: dark reflector, a slow structural-colour drift gold -> magenta -> violet across the tile
    n = 512; ph = 0.5 + 0.5 * _noise(n, n * 0.9, 21, 1) / 2.5; ph = np.clip(ph, 0, 1)
    stops = np.array([[150, 112, 40], [128, 46, 92], [70, 46, 128]], dtype=float) / 255.0          # gold, magenta, violet (dark)
    t2 = ph * 2; i0 = np.clip(t2.astype(int), 0, 1); fr = (t2 - i0)[..., None]
    col = stops[i0] * (1 - fr) + stops[i0 + 1] * fr
    lum = col.mean(-1, keepdims=True); col = col * 0.6 + lum * 0.4                                  # restrained: 40 % toward grey
    made["mirror"] = _save("surf_mirror", col * 0.8); _maps("mirror", 0.9, 0.92)
    # rev. R borazon: deep amber-copper polished cBN, subtle depth variation; the glowing (heat) material
    n = 512; dv = _noise(n, n * 0.25, 22, 2); sp = _noise(n, 3.0, 23, 1)
    made["boraz"] = _save("surf_boraz", _srgb((150, 45, 10)) * (1 + 0.07 * dv + 0.02 * sp)[..., None])
    _save("surf_boraz_norm", _normal(dv * 0.00002, n)); _maps("boraz", 0.0, 0.85)
    hb = np.clip(0.88 + 0.06 * sp + 0.04 * dv, 0.75, 1.0); _save("surf_boraz_heat", hb); _save("surf_boraz_emis", 0.5 + (hb - 0.75) * 2.0)
    # ceramic plates: light grey, speckle, very faint relief; heat .g 0.8..1 (speckle runs hot first)
    n = 512; sp = _noise(n, 2.0, 7, 2); big = _noise(n, n * 0.3, 8, 1)
    made["ceram"] = _save("surf_ceram", _srgb((182, 180, 174)) * (1 + 0.035 * sp + 0.02 * big)[..., None])
    _save("surf_ceram_norm", _normal(sp * 0.00004, n))
    _maps("ceram", 0.0, 0.25)
    ht = np.clip(0.88 + 0.06 * sp + 0.04 * big, 0.75, 1.0); _save("surf_ceram_heat", ht)
    _save("surf_ceram_emis", 0.5 + (ht - 0.75) * 2.0)                     # emission pattern (x material emission): speckle 1.0, fill ~0.5
    # sole: ceramic studs on a 27 mm grid with aerogel fill between; studs glow first (1.0), fill 0.6
    n = 512; ys, xs = (np.mgrid[0:n, 0:n] + 0.5) * (TILE / n); p = 0.027
    stud = ((np.abs(((xs + p / 2) % p) - p / 2) < 0.0085) & (np.abs(((ys + p / 2) % p) - p / 2) < 0.0085)).astype(float)
    made["sole"] = _save("surf_sole", stud[..., None] * _srgb((205, 196, 180)) + (1 - stud[..., None]) * _srgb((86, 92, 98)))
    _maps("sole", 0.0, 0.15)
    _save("surf_sole_heat", 0.6 + 0.4 * stud); _save("surf_sole_emis", 0.5 + 0.5 * stud)
    # radiator fins: fin stock (metalness 0.6); heat: fin tips 1.0 (v = height, 4 fins per tile), roots 0.5
    n = 512; st = _noise(n, 3.0, 9, 1); vv = np.linspace(0, 1, n)[:, None] * np.ones((1, n))
    made["rad"] = _save("surf_rad", _srgb((150, 154, 160)) * (1 + 0.05 * st)[..., None])
    _maps("rad", 0.6, 0.4)
    rh = 0.5 + 0.5 * (0.5 + 0.5 * np.cos(2 * np.pi * vv * 4)) ** 2; _save("surf_rad_heat", rh); _save("surf_rad_emis", rh)
    # enamel shell, graphite frame, light steel, gold visor (with a faint _norm), and the flat kinds (red, rubber, slit)
    n = 256
    for key, col, metl, sm, rel in (("enamel", (122, 125, 124), 0.0, 0.40, 0.00003), ("graphite", (72, 76, 82), 0.3, 0.35, 0.00006),
                                    ("metal", (190, 195, 200), 1.0, 0.60, 0.00004), ("visor", (255, 196, 87), 1.0, 0.90, 0.0)):
        nz = _noise(n, 6.0, sum(map(ord, key)), 2)
        made[key] = _save("surf_" + key, _srgb(col) * (1 + 0.02 * nz)[..., None])
        _save("surf_" + key + "_norm", _normal(nz * rel, n)); _maps(key, metl, sm)
    for key, col, metl, sm in (("red", (180, 20, 12), 0.0, 0.35), ("rubber", (14, 14, 15), 0.0, 0.10), ("slit", (8, 10, 16), 0.0, 0.90)):
        made[key] = _save("surf_" + key, np.ones((64, 64, 3)) * _srgb(col)); _maps(key, metl, sm, n=64)
    return made
