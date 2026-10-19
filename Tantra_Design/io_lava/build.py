"""Build the Io lava-field tile overrides and the thermal data.

    python build.py            all layers
    python build.py elev       only elevation tiles
    python build.py thermal    only Config/Io/LavaField rasters
    python build.py surf       surface + mask tiles

Writes loose quadtree tiles next to the stock archive (Orbiter reads cache first, then archive: D3D9Client
PlanetTileLoadFlags = 3); the stock Textures/Io/Archive/*.tree are only read, never written.
  Textures/Io/Surf/LL/AAAAAA/BBBBBB.dds   512x512 DXT1, levels 10..20
  Textures/Io/Mask/LL/AAAAAA/BBBBBB.dds   512x512 DXT1 night lights (RGB), land everywhere; levels 1..4 black roots
  Textures/Io/Elev/LL/AAAAAA/BBBBBB.elv   259x259 int16, scale 0.5 m, levels 10..17 (Orbiter's elevation limit)
"""
import io, os, sys, math, struct, time, numpy as np
from PIL import Image
import iosite as S, field, glow
from ztree import ZTree
from field import smooth

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TEX = os.path.join(ROOT, 'Textures', 'Io')
CFG = os.path.join(ROOT, 'Config', 'Io', 'LavaField')
ELEV_SCALE = 0.5                                     # Config/Io.cfg ElevationResolution
CORE = S.tile_metres(16) / 2                         # 698 m
REG = CORE * 7                                       # regional detail box: 7x7 level-16 tiles (+-4.9 km)
W = field.W_SITE; WH = S.tile_metres(18) / 2         # high-detail (level 20) box: the level-18 tile around W
# where the IoLava\LavaField vessel stands: a FLAT spot on the lobe (Orbiter puts a landed vessel's origin on the ground
# under it and lays the touchdown plane along that ground - a sloped spot or uneven touchdown heights tilt every mesh)
ANCHOR = np.array([-122.0, -116.0])
TD_FLAT = [(0.0, 20.0), (-17.32, -10.0), (17.32, -10.0)]   # nose, left rear, right rear (this order: up is up)

# ---------------------------------------------------------------- stock background (level 7 of the archive)
_st = {}
def _stock(layer):
    if layer not in _st:
        tr = ZTree(os.path.join(TEX, 'Archive', layer + '.tree'))
        lat0, lng0 = S.LAT0, S.LNG0
        a, b = S.tile_of(7, lat0, lng0)
        d = tr.read(7, a, b)
        if layer == 'Surf':
            arr = np.asarray(Image.open(io.BytesIO(d)).convert('RGB'), np.float32) / 255
        else:
            arr = np.frombuffer(d[100:], '<i2').reshape(259, 259).astype(np.float64) * struct.unpack('<d', d[28:36])[0]
        _st[layer] = (S.tile_bounds(7, a, b), arr)
    return _st[layer]

def stock_surf(lat, lng):
    (la0, la1, ln0, ln1), A = _stock('Surf')
    u = (lng - ln0) / (ln1 - ln0) * 512 - 0.5; v = (la1 - lat) / (la1 - la0) * 512 - 0.5
    return _bilin(A, v, u)

def stock_elev(lat, lng):
    (la0, la1, ln0, ln1), A = _stock('Elev')
    u = 1 + (lng - ln0) / (ln1 - ln0) * 256; v = 1 + (lat - la0) / (la1 - la0) * 256
    return _bilin(A, v, u)

def _bilin(A, v, u):
    i = np.clip(np.floor(v).astype(int), 0, A.shape[0] - 2); j = np.clip(np.floor(u).astype(int), 0, A.shape[1] - 2)
    fv = np.clip(v - i, 0, 1); fu = np.clip(u - j, 0, 1)
    if A.ndim == 3: fv = fv[..., None]; fu = fu[..., None]
    return (A[i, j] * (1 - fu) + A[i, j + 1] * fu) * (1 - fv) + (A[i + 1, j] * (1 - fu) + A[i + 1, j + 1] * fu) * fv

# ---------------------------------------------------------------- one tile, evaluated directly
def regional(x, y, pix, base):
    """stock colour with flow-field / frost detail (zero-mean, so the tone stays the stock's); fades out towards
    the edge of the regional box. Flow units are drawn as lobes stretched E-W: Prometheus flows run west."""
    from noise import fbm
    wr = smooth(REG, REG - 1500, np.maximum(np.abs(x), np.abs(y)))
    lum = base.mean(-1)
    dark = smooth(0.42, 0.30, lum)                    # stock flow field is dark
    wx = x / 2.5 + 250 * fbm(x, y, 1200, pix, 3, seed=201); wy = y + 250 * fbm(x, y, 1200, pix, 3, seed=202)
    m_dark = (0.16 * fbm(wx, wy, 500, pix, 5, seed=203) + 0.06 * fbm(x / 2, y, 120, pix, 4, seed=204)
              + 0.03 * fbm(x, y, 25, pix, 4, seed=207))
    m_frost = 0.06 * fbm(x, y, 400, pix, 4, seed=205) + 0.04 * fbm(x, y, 40, pix, 4, seed=206)
    m = (dark * m_dark + (1 - dark) * m_frost) * wr
    return np.clip(base * (1 + m)[..., None], 0, 1)

def core_blend(x, y, pix):
    """colour weight of the core: a rounded, ragged patch that is exactly 0 on the core tile's edges"""
    from noise import fbm
    rr = (np.abs(x) ** 4 + np.abs(y) ** 4) ** 0.25 + 40 * np.clip(fbm(x, y, 220, pix, 3, seed=301) * 2, -1, 1)
    return smooth(CORE - 45, CORE - 330, rr)

def eval_tile(L, a, b, n=512):
    """-> albedo sRGB (n,n,3), glow luminance Y (n,n) [cd/m2], glow colour*Y (n,n,3) [linear]"""
    lat, lng = S.surf_grid(L, a, b, n)
    x, y = S.to_xy(lat, lng)
    pix = S.tile_metres(L) / n
    base = stock_surf(lat, lng)
    rgb = regional(x, y, pix, base)
    Y = np.zeros(x.shape); cY = np.zeros(x.shape + (3,)); G = np.zeros(x.shape)
    wc = core_blend(x, y, pix)
    if wc.max() > 0:
        o = field.evaluate(x, y, pix, base=base)
        rgb = rgb * (1 - wc[..., None]) + o['rgb'] * wc[..., None]
        for f, T in ((1 - o['f_c'] - o['f_a'], o['Ts']), (o['f_c'], o['Tc']), (o['f_a'], o['Tax'])):
            yl = f * glow.luminance(T) * wc
            Y += yl; cY += glow.chroma(T) * yl[..., None]
        G = gloss(o) * wc
    return rgb.astype(np.float32), Y.astype(np.float32), cY.astype(np.float32), G.astype(np.float32)

def gloss(o):
    """glassy sheen of the crust, 0..1 (AtmoFX fork: AtmoFX_LavaGloss): fresh pahoehoe glass 1 for its first day,
    dulling (oxidation, sulfur dust) to 0 by ~2 months; exposed melt 0.6 (its glow dominates); frost and old flows 0"""
    age = np.where(np.isfinite(o['age']), o['age'], 1e12)
    g = 1.0 - field.smooth(np.log10(field.DAY), np.log10(60 * field.DAY), np.log10(np.maximum(age, 1.0)))
    g = np.where(o['kind'] >= 5, np.minimum(g, 0.6), g)
    return np.where(o['lava'] > 0.5, g, 0.0)

def down2(A):
    return A.reshape(A.shape[0] // 2, 2, A.shape[1] // 2, 2, *A.shape[2:]).mean((1, 3))

# ---------------------------------------------------------------- the pyramid
def box_hits(L, a, b, cx, cy, half):
    la0, la1, ln0, ln1 = S.tile_bounds(L, a, b)
    x0, y0 = S.to_xy(la0, ln0); x1, y1 = S.to_xy(la1, ln1)
    return x1 > cx - half and x0 < cx + half and y1 > cy - half and y0 < cy + half

# the whole active flow - sheet, branches, the channel down the cone, the summit pit - at the finest level (0.17 m/px)
FLOW_BOX = (-245.0, 100.0, -245.0, 235.0)                 # x0, x1, y0, y1 [m]

def rect_hits(L, a, b, x0, x1, y0, y1):
    la0, la1, ln0, ln1 = S.tile_bounds(L, a, b)
    tx0, ty0 = S.to_xy(la0, ln0); tx1, ty1 = S.to_xy(la1, ln1)
    return tx1 > x0 + 1 and tx0 < x1 - 1 and ty1 > y0 + 1 and ty0 < y1 - 1

def max_level(L, a, b):
    if box_hits(L, a, b, W[0], W[1], WH - 1) or rect_hits(L, a, b, *FLOW_BOX): return 20
    if box_hits(L, a, b, 0, 0, CORE - 1): return 19
    if box_hits(L, a, b, 0, 0, REG - 1): return 16
    return None

count = {'surf': 0, 'mask': 0}
def gen(L, a, b, write=True):
    deep = max_level(L, a, b)
    if deep is None:                                  # outside everything: plain evaluation, not written
        return eval_tile(L, a, b)
    if L >= deep:
        rgb, Y, cY, G = eval_tile(L, a, b)
    else:
        rgb = np.zeros((512, 512, 3), np.float32); Y = np.zeros((512, 512), np.float32); cY = np.zeros((512, 512, 3), np.float32)
        G = np.zeros((512, 512), np.float32)
        for qa in (0, 1):
            for qb in (0, 1):
                child = (L + 1, 2 * a + qa, 2 * b + qb)
                sl = (slice(qa * 256, qa * 256 + 256), slice(qb * 256, qb * 256 + 256))
                if child in _HALF:                                # done by a worker process (build_surf)
                    rgb[sl], Y[sl], cY[sl], G[sl] = _HALF.pop(child)
                else:
                    cr, cyl, ccy, cg = gen(*child)
                    rgb[sl] = down2(cr); Y[sl] = down2(cyl); cY[sl] = down2(ccy); G[sl] = down2(cg)
    if write:
        save_surf(L, a, b, rgb, Y, cY)
        save_mask(L, a, b, Y, cY, G)
    return rgb, Y, cY, G

def tile_path(layer, L, a, b, ext):
    d = os.path.join(TEX, layer, f'{L:02d}', f'{a:06d}')
    os.makedirs(d, exist_ok=True)
    return os.path.join(d, f'{b:06d}.{ext}')

RAW_FROM_LEVEL = 19          # the finest levels uncompressed (A8R8G8B8): DXT1's 4x4 blocks are 0.7 m steps at level 20

def save_dds(path, rgb8, raw=False):
    if raw:
        rgb8 = np.asarray(rgb8, np.uint8)
        rgba = np.concatenate([rgb8, np.full(rgb8.shape[:2] + (1,), 255, np.uint8)], -1)   # alpha 255: land (mask)
        Image.fromarray(rgba).save(path)
    else:
        Image.fromarray(rgb8).save(path, pixel_format='DXT1')

def glow_albedo(surf8, mask8):
    """D3D9Client multiplies the night lights by the surface texture (NewPlanet.hlsl: cTex.rgb *= ... + cNgt), so a
    glowing pixel on black lava stays black. Give glowing pixels the colour of their glow in the albedo texture.
    (Side effect: in daylight the glowing cracks show as dark-red lines, which Io's sunlight would really hide.)"""
    m = mask8.astype(np.float64) / 255
    return np.maximum(surf8, (np.clip(m, 0, 1) ** 0.4 * 255 + 0.5).astype(np.uint8))

def night8(Y, cY):
    chrom = cY / np.maximum(Y, 1e-30)[..., None]
    return (glow.to_srgb(glow.night_rgb(Y, chrom)) * 255 + 0.5).astype(np.uint8)

SURF_GLOW_HACKS = False      # stock D3D9Client only: bake the glow into the albedo. The AtmoFX fork adds the mask itself
                             # (Config/GC/Io.atm.cfg AtmoFX_MaskEmissive), so the albedo stays plain rock.

def save_surf(L, a, b, rgb, Y, cY):
    chrom = cY / np.maximum(Y, 1e-30)[..., None]
    lin = np.clip(rgb.astype(np.float64), 0, 1) ** 2.2 + (glow.day_add(Y, chrom) if SURF_GLOW_HACKS else 0)
    s8 = (glow.to_srgb(lin) * 255 + 0.5).astype(np.uint8)
    if SURF_GLOW_HACKS and Y.max() >= glow.Y_LO: s8 = glow_albedo(s8, night8(Y, cY))
    save_dds(tile_path('Surf', L, a, b, 'dds'), s8, raw=L >= RAW_FROM_LEVEL)
    count['surf'] += 1

LAVA_GLOSS = True            # True only with the AtmoFX fork's AtmoFX_LavaGloss = 1 (Config/GC/Io.atm.cfg): then the mask
                             # alpha is the glass sheen 0..1; without it the client reads alpha as water (sky reflections)

def save_mask(L, a, b, Y, cY, G=None):
    if Y.max() < glow.Y_LO and not (LAVA_GLOSS and G is not None and G.max() > 0.02):
        path = tile_path('Mask', L, a, b, 'dds')               # nothing to show: blank a tile left by an older build
        if os.path.exists(path): save_empty_mask(path, Y.shape[0]); count['mask'] += 1
        return
    rgb8 = night8(Y, cY)
    if LAVA_GLOSS and G is not None:
        a8 = (np.clip(G, 0, 1) * 255 + 0.5).astype(np.uint8)
        rgba = np.concatenate([rgb8, a8[..., None]], -1)
        if L >= RAW_FROM_LEVEL: Image.fromarray(rgba).save(tile_path('Mask', L, a, b, 'dds'))
        else: Image.fromarray(rgba).save(tile_path('Mask', L, a, b, 'dds'), pixel_format='DXT5')
    else:
        save_dds(tile_path('Mask', L, a, b, 'dds'), rgb8, raw=L >= RAW_FROM_LEVEL)
    count['mask'] += 1

def save_empty_mask(path, n):
    """black mask tile; with LAVA_GLOSS its alpha must be 0 (alpha = sheen, DXT1's opaque 255 would gloss all of Io)"""
    if LAVA_GLOSS: Image.fromarray(np.zeros((n, n, 4), np.uint8)).save(path, pixel_format='DXT5')
    else: save_dds(path, np.zeros((n, n, 3), np.uint8))

def mask_roots():
    """an empty (black, all-land) Mask layer down to the level-4 quadtree roots, as the format requires"""
    for L, n in ((1, 128), (2, 256), (3, 512)):
        save_empty_mask(tile_path('Mask', L, 0, 0, 'dds'), n)
    for b in (0, 1):
        save_empty_mask(tile_path('Mask', 4, 0, b, 'dds'), 512)

_HALF = {}                   # leaf tile -> its half-size (rgb, Y, cY), filled by the worker processes

def _leaves(L, a, b, out):
    deep = max_level(L, a, b)
    if deep is None: return
    if L >= deep: out.append((L, a, b)); return
    for qa in (0, 1):
        for qb in (0, 1):
            _leaves(L + 1, 2 * a + qa, 2 * b + qb, out)

def _leaf_job(t):
    m0 = count['mask']
    rgb, Y, cY, G = eval_tile(*t)
    save_surf(*t, rgb, Y, cY)
    save_mask(*t, Y, cY, G)
    return t, (down2(rgb), down2(Y), down2(cY), down2(G)), count['mask'] - m0

def build_surf(workers=None):
    """the finest (leaf) tiles in parallel - they are nearly all of the work - then the coarser levels from them"""
    import multiprocessing as mp
    mask_roots()
    t = time.time()
    tops = S.tiles_covering(10, -REG, -REG, REG, REG)
    leaves = []
    for top in tops: _leaves(*top, leaves)
    workers = workers or max(1, (os.cpu_count() or 2) - 1)
    nmask = 0
    with mp.Pool(workers) as pool:
        for k, (tl, half, nm) in enumerate(pool.imap_unordered(_leaf_job, leaves, chunksize=1)):
            _HALF[tl] = half; nmask += nm
            if (k + 1) % 20 == 0: print(f'  leaves {k + 1}/{len(leaves)}, {time.time() - t:.0f} s', flush=True)
    for top in tops:
        gen(*top)
    print(f'surf: {count["surf"] + len(leaves)} tiles ({len(leaves)} leaves on {workers} processes), '
          f'mask: {count["mask"] + nmask} tiles, {time.time() - t:.0f} s')

# ---------------------------------------------------------------- elevation
def save_elv(L, a, b, h):
    la0, la1, ln0, ln1 = S.tile_bounds(L, a, b)
    z = np.clip(np.round(h / ELEV_SCALE), -32767, 32766).astype('<i2')
    hh = z * ELEV_SCALE
    hdr = struct.pack('<4siiiiiiddddddddd', b'ELE\x01', 100, -16, 259, 259, 1, 1, ELEV_SCALE, 0.0,
                      la0, la1, ln0, ln1, float(hh.min()), float(hh.max()), float(hh.mean()))
    with open(tile_path('Elev', L, a, b, 'elv'), 'wb') as f:
        f.write(hdr); f.write(z.tobytes())

def elev_tile(L, a, b):
    lat, lng = S.elev_grid(L, a, b)
    x, y = S.to_xy(lat, lng)
    h = stock_elev(lat, lng)
    pix = S.tile_metres(L) / 256
    wc = smooth(CORE, CORE - 110, np.maximum(np.abs(x), np.abs(y)))
    m = wc > 0
    if m.any():
        # evaluate on the bounding rows/cols of the core only
        r = np.nonzero(m.any(1))[0]; c = np.nonzero(m.any(0))[0]
        sl = (slice(r[0], r[-1] + 1), slice(c[0], c[-1] + 1))
        o = field.evaluate(x[sl], y[sl], pix, want_color=False)
        h[sl] += o['h'] * wc[sl]
    save_elv(L, a, b, h)

def build_elev():
    t = time.time(); n = 0
    # from level 8: the stock archive ends at 7, and Orbiter's core (elevmgr.cpp) uses a cached coarser tile unless the
    # NEXT level exists in that quadrant - a gap (no 8, 9) let the stock level-7 tile shadow the whole field
    for L in range(8, 19):   # 18: 1.36 m nodes (Config/Io.cfg MaxPatchResolution 21 -> Orbiter core and D3D9Client read it)
        for (_, a, b) in S.tiles_covering(L, -CORE + 1, -CORE + 1, CORE - 1, CORE - 1):
            elev_tile(L, a, b); n += 1
    print(f'elev: {n} tiles, {time.time() - t:.0f} s')

# ---------------------------------------------------------------- thermal rasters for the suit
def thermal_raster(name, cx, cy, half, step):
    n = int(round(2 * half / step))
    xs = cx - half + (np.arange(n) + 0.5) * step; ys = cy + half - (np.arange(n) + 0.5) * step
    Tc = np.zeros((n, n), np.uint16); Tr = np.zeros((n, n), np.uint16); cr = np.zeros((n, n), np.uint16); kd = np.zeros((n, n), np.uint8)
    for i0 in range(0, n, 256):
        X, Yy = np.meshgrid(xs, ys[i0:i0 + 256])
        o = field.evaluate(X, Yy, step, want_color=False)
        lava = o['lava'] > 0.5
        Tc[i0:i0 + 256] = np.where(lava, np.round(o['T_contact'] * 10), 0)
        Tr[i0:i0 + 256] = np.where(lava, np.round(o['T_rad'] * 10), 0)
        cr[i0:i0 + 256] = np.round(np.clip(o['crust'], 0, 65) * 1000)
        kd[i0:i0 + 256] = o['kind']
    lat_n, lng_w = S.to_latlng(cx - half, cy + half)
    os.makedirs(CFG, exist_ok=True)
    with open(os.path.join(CFG, name + '.raw'), 'wb') as f:
        f.write(Tc.astype('<u2').tobytes()); f.write(Tr.astype('<u2').tobytes()); f.write(cr.astype('<u2').tobytes()); f.write(kd.tobytes())
    return dict(name=name, n=n, step=step, lat_n=math.degrees(lat_n), lng_w=math.degrees(lng_w),
                dlat=math.degrees(step / S.R_IO), dlng=math.degrees(step / (S.R_IO * S.COSLAT0)),
                Tmax=Tr.max() / 10, cx=cx, cy=cy, half=half)

def build_thermal():
    t = time.time()
    rs = [thermal_raster('core', 0, 0, CORE, 1.0), thermal_raster('site', W[0], W[1], WH, 0.25)]
    with open(os.path.join(CFG, 'LavaField.cfg'), 'w', encoding='utf-8') as f:
        f.write('; Io lava field (Prometheus, southern flow margin): surface temperature for suit tests.\n'
                '; Generated by Tantra_Design/io_lava/build.py - do not edit, rebuild instead.\n'
                '; Each RASTER is a north-up grid: row 0 = northern edge, column 0 = western edge, cell centres at\n'
                ';   lat = LAT_N - (row + 0.5) * DLAT,  lng = LNG_W + (col + 0.5) * DLNG   [deg, east-positive]\n'
                '; .raw = 4 planes of N*N little-endian values, in this order:\n'
                ';   uint16 T_contact [0.1 K]  crust surface the boot touches (0 = not lava: use the body\'s table)\n'
                ';   uint16 T_rad     [0.1 K]  radiative temperature incl. glowing cracks (0 = not lava)\n'
                ';   uint16 crust     [mm]     solid crust above the melt (0 = not lava / molten surface)\n'
                ';   uint8  kind      0 frost plain, 1 flow years old, 2 months, 3 days-weeks, 4 hours,\n'
                ';                    5 breakout toe (minutes, incandescent), 6 molten (skylight, lake seam), 7 cone/spatter\n'
                '; The finer raster wins where both cover a point. Night-side values (sky ~0 K); the frost plain is\n'
                '; 0 so the day/night table (tvSurfaceTemps) stays in charge there.\n'
                f'BODY = Io\nT_ERUPT = {field.cooling.T_ERUPT:.0f}\nT_SOLIDUS = {field.cooling.T_SOL:.0f}\n'
                f'SITE_LAT = {math.degrees(S.to_latlng(*W)[0]):.6f}\nSITE_LNG = {math.degrees(S.to_latlng(*W)[1]):.6f}\n')
        for r in rs:
            f.write(f'RASTER {r["name"]}.raw N {r["n"]} LAT_N {r["lat_n"]:.8f} LNG_W {r["lng_w"]:.8f} '
                    f'DLAT {r["dlat"]:.10f} DLNG {r["dlng"]:.10f} STEP_M {r["step"]}\n')
    print('thermal:', [(r['name'], r['n'], r['Tmax']) for r in rs], f'{time.time() - t:.0f} s')

# ---------------------------------------------------------------- dynamic effects (Modules/IoLava.dll)
def _disc_emission(cx, cy, a, step=0.2):
    """luminance averaged over a disc of radius a (energy average, cd/m2) and its colour (linear sRGB, max 1)"""
    xs = np.arange(cx - a, cx + a, step) + step / 2; ys = np.arange(cy - a, cy + a, step) + step / 2
    X, Y = np.meshgrid(xs, ys); inside = np.hypot(X - cx, Y - cy) <= a
    o = field.evaluate(X, Y, step, want_color=False)
    Ysum = np.zeros(X.shape); cY = np.zeros(X.shape + (3,))
    for f, T in ((1 - o['f_c'] - o['f_a'], o['Ts']), (o['f_c'], o['Tc']), (o['f_a'], o['Tax'])):
        yl = f * glow.luminance(T); Ysum += yl; cY += glow.chroma(T) * yl[..., None]
    L = Ysum[inside].mean(); c = cY[inside].sum(0) / max(Ysum[inside].sum(), 1e-30)
    return L, c / max(c.max(), 1e-9), float(o['h'][inside].mean())

def build_effects():
    """light emitters at the hottest spots and SO2 sublimation sources along the front -> Config/Io/LavaField/Effects.cfg.
    Positions are local metres (east, north, up) from ANCHOR, which is where the IoLava vessel stands."""
    anchor = ANCHOR.copy()
    la0, ln0 = S.to_latlng(*anchor)
    h0 = float(elev_l17(anchor[0], anchor[1])[0])          # the tiles' terrain, as Orbiter has it
    def ground(x, y):
        return float(elev_l17(x, y)[0])
    # the front: walk out from the tube mouth along a bearing until the lobe ends
    def front_point(brg):
        ds = np.arange(60, 140, 0.25)
        x = field.FAN[0] + ds * np.sin(np.radians(brg)); y = field.FAN[1] + ds * np.cos(np.radians(brg))
        o = field.evaluate(x[None], y[None], 0.17, want_color=False)
        i = np.argmax((o['lava'][0] <= 0.5) & (ds > 70))
        return ds[i]
    items = []
    for brg in (205, 225, 245):
        d = front_point(brg) - 2.0
        cx = field.FAN[0] + d * np.sin(np.radians(brg)); cy = field.FAN[1] + d * np.cos(np.radians(brg))
        items.append(('TOES', cx, cy, 5.0, 1.2))
    items.append(('LAKE', field.SHIELD[0], field.SHIELD[1], field.R_PIT, 3.0))
    # the two skylights nearest to the site
    _, st_end = field.seg_dist(np.array([field.FAN[0]]), np.array([field.FAN[1]]), field.TUBE)
    pts = []
    for k in range(1, 20):
        sv = k * 55.0
        if sv < field.R_PIT + 70 or abs(sv - st_end[0]) < 30 or sv > st_end[0]: continue
        acc = 0.0
        for a, b in zip(field.TUBE[:-1], field.TUBE[1:]):
            L = np.hypot(*(b - a))
            if acc + L >= sv: p = a + (b - a) * (sv - acc) / L; break
            acc += L
        pts.append(p)
    pts.sort(key=lambda p: np.hypot(*(p - field.W_SITE)))
    for p in pts[:2]: items.append(('SKYLIGHT', p[0], p[1], 2.2, 0.8))
    lines = []
    for kind, cx, cy, a, lift in items:
        L, c, hm = _disc_emission(cx, cy, a)
        b = float(np.clip(np.log10(max(L, 1e-30) / glow.Y_LO) / np.log10(glow.Y_HI / glow.Y_LO), 0, 1))
        z = ground(cx, cy) + lift - h0
        lines.append(f'LIGHT {kind:8s} {cx - anchor[0]:9.2f} {cy - anchor[1]:9.2f} {z:7.2f}  A {a:5.2f}  LUM {L:10.4g}  TONE {b:.3f}  RGB {c[0]:.3f} {c[1]:.3f} {c[2]:.3f}')
        if kind == 'TOES' and False:   # SO2 sublimation particles: off - see README (Orbiter particles have no gravity: they hovered as fog)
            ux, uy = cx - field.FAN[0], cy - field.FAN[1]; n = np.hypot(ux, uy); sx, sy = cx + 5 * ux / n, cy + 5 * uy / n
            lines.append(f'SUBLIM          {sx - anchor[0]:9.2f} {sy - anchor[1]:9.2f} {ground(sx, sy) - h0:7.2f}')
    la, ln = S.to_latlng(*anchor)
    with open(os.path.join(CFG, 'Effects.cfg'), 'w', encoding='utf-8') as f:
        f.write('; Io lava field dynamic effects for Modules/IoLava.dll. Generated by Tantra_Design/io_lava/build.py effects.\n'
                '; ANCHOR = where the IoLava\\LavaField vessel stands (lng lat deg); entries in metres east north up from it.\n'
                '; LIGHT: a glowing patch of radius A seen as a point light: illuminance ~ 1 / (1 + d^2/A^2) (on-axis disc),\n'
                ';   LUM = its mean visible luminance [cd/m2], TONE = the same log tone map as the Mask night lights (0..1),\n'
                ';   RGB = blackbody colour. SUBLIM: SO2 frost sublimating where the lobe front overruns the plain.\n'
                f'TONE_RANGE {glow.Y_LO:.6g} {glow.Y_HI:.6g}\n'
                f'ANCHOR {math.degrees(ln):.7f} {math.degrees(la):.7f}\n')
        f.write('\n'.join(lines) + '\n')
    print('\n'.join(lines)); print('anchor', math.degrees(ln), math.degrees(la), 'h0', h0)

def glow_patch():
    """apply glow_albedo to the Surf tiles already built (fast: no field evaluation); idempotent"""
    import glob
    n = 0
    for mp in glob.glob(os.path.join(TEX, 'Mask', '*', '*', '*.dds')):
        L = int(os.path.normpath(mp).split(os.sep)[-3])
        if L < 10: continue
        sp = mp.replace(os.sep + 'Mask' + os.sep, os.sep + 'Surf' + os.sep)
        if not os.path.exists(sp): continue
        s8 = np.asarray(Image.open(sp).convert('RGB')); m8 = np.asarray(Image.open(mp).convert('RGB'))
        save_dds(sp, glow_albedo(s8, m8)); n += 1
    print(f'glow patch: {n} surf tiles')

# ---------------------------------------------------------------- glow mesh (Meshes/IoLava/GlowField.msh)
_l17 = {}
def elev_l17(x, y):
    """terrain height [m] from the level-17 .elv tiles (what Orbiter itself stands things on), bilinear"""
    la, ln = S.to_latlng(np.asarray(x, float), np.asarray(y, float))
    out = np.zeros(np.shape(la))
    la = np.atleast_1d(la); ln = np.atleast_1d(ln); out = np.atleast_1d(out)
    for i in range(la.size):
        a, b = S.tile_of(17, la.flat[i], ln.flat[i])
        if (a, b) not in _l17:
            d = open(tile_path('Elev', 17, a, b, 'elv'), 'rb').read()
            sc = struct.unpack('<d', d[28:36])[0]
            _l17[(a, b)] = (np.frombuffer(d[100:], '<i2').reshape(259, 259) * sc, S.tile_bounds(17, a, b))
        z, (la0, la1, ln0, ln1) = _l17[(a, b)]
        u = 1 + (ln.flat[i] - ln0) / (ln1 - ln0) * 256; v = 1 + (la.flat[i] - la0) / (la1 - la0) * 256
        out.flat[i] = _bilin(z, np.array(v), np.array(u))
    return out

def elev_lvl(L, x, y):
    """terrain height from the level-L .elv tiles (bilinear), arrays"""
    la, ln = S.to_latlng(np.asarray(x, float), np.asarray(y, float))
    out = np.zeros(np.shape(la))
    for i in range(np.size(la)):
        a, b = S.tile_of(L, la.flat[i], ln.flat[i])
        key = (L, a, b)
        if key not in _l17:
            d = open(tile_path('Elev', L, a, b, 'elv'), 'rb').read()
            _l17[key] = (np.frombuffer(d[100:], '<i2').reshape(259, 259) * struct.unpack('<d', d[28:36])[0], S.tile_bounds(L, a, b))
        z, (la0, la1, ln0, ln1) = _l17[key]
        out.flat[i] = _bilin(z, np.array(1 + (la.flat[i] - la0) / (la1 - la0) * 256), np.array(1 + (ln.flat[i] - ln0) / (ln1 - ln0) * 256))
    return out

def glow_image(x0, x1, y0, y1, n):
    """additive glow texture of a box: sRGB of (blackbody colour x night tone), row 0 = north"""
    step = (x1 - x0) / n
    xs = x0 + (np.arange(n) + 0.5) * step; ys = y1 - (np.arange(n) + 0.5) * step
    img = np.zeros((n, n, 3), np.uint8)
    for r0 in range(0, n, 128):
        X, Y = np.meshgrid(xs, ys[r0:r0 + 128])
        o = field.evaluate(X, Y, step, want_color=False)
        Ys = np.zeros(X.shape); cY = np.zeros(X.shape + (3,))
        for f, T in ((1 - o['f_c'] - o['f_a'], o['Ts']), (o['f_c'], o['Tc']), (o['f_a'], o['Tax'])):
            yl = f * glow.luminance(T); Ys += yl; cY += glow.chroma(T) * yl[..., None]
        img[r0:r0 + 128] = glow8(Ys, cY)
    return img

def glow8(Y, cY):
    """the glow mesh texture: D3D9Client caps an unlit group at its texture (saturate(diffuse+emissive)), so the
    brightness lives in the texture. Exposure like an eye or a camera at night: the hotter, the further each channel
    saturates - molten lava goes orange-yellow, toes bright orange, cracks red."""
    chrom = cY / np.maximum(Y, 1e-30)[..., None]
    b = np.clip(np.log10(np.maximum(Y, 1e-30) / glow.Y_LO) / np.log10(glow.Y_HI / glow.Y_LO), 0, 1)
    E = 2.0 + 10.0 * b ** 3
    lin = 1 - np.exp(-E[..., None] * chrom * b[..., None])
    return (glow.to_srgb(lin) * 255 + 0.5).astype(np.uint8)

def build_glowmesh(lift=0.3, cell=2.0):
    anchor = ANCHOR.copy()
    h0 = float(elev_l17(anchor[0], anchor[1])[0])
    boxes = [('lobe', 'TOES', anchor[0] - 130, anchor[0] + 130, anchor[1] - 130, anchor[1] + 130, 2048)]
    r = field.R_PIT + 3
    # (the lake itself is the floor of the IoLava\Vent pit lining: build.py vent)
    bc = field.SHIELD + field.BREACH * (field.R_PIT + 28)                    # the lava channel through the cone's breach
    boxes.append(('breach', 'BREACH', bc[0] - 34, bc[0] + 34, bc[1] - 34, bc[1] + 34, 512))
    _, st_end = field.seg_dist(np.array([field.FAN[0]]), np.array([field.FAN[1]]), field.TUBE)
    k = 0
    for j in range(1, 30):
        sv = j * 55.0
        if sv < field.R_PIT + 60 or sv > st_end[0] - 25: continue
        acc = 0.0
        for a_, b_ in zip(field.TUBE[:-1], field.TUBE[1:]):
            L = np.hypot(*(b_ - a_))
            if acc + L >= sv: p_ = a_ + (b_ - a_) * (sv - acc) / L; break
            acc += L
        boxes.append((f'sky{k}', 'SKYLIGHT', p_[0] - 4, p_[0] + 4, p_[1] - 4, p_[1] + 4, 128)); k += 1
    os.makedirs(os.path.join(ROOT, 'Meshes', 'IoLava'), exist_ok=True)
    groups = []
    for name, kind, x0, x1, y0, y1, n in boxes:
        t = time.time()
        img = glow_image(x0, x1, y0, y1, n)
        Image.fromarray(img).save(os.path.join(TEX, '..', 'IoLava', f'glow_{name}.dds'), pixel_format='DXT1')
        nc = int(round((x1 - x0) / cell)); px = n / nc
        lum = img.astype(int).sum(-1)
        keep = np.array([[lum[int(i * px):int((i + 1) * px), int(j * px):int((j + 1) * px)].max() > 12 for j in range(nc)] for i in range(nc)])
        # vertices on the cell corners (row 0 = north edge), shared
        need = np.zeros((nc + 1, nc + 1), bool)
        for i, j in zip(*np.nonzero(keep)): need[i:i + 2, j:j + 2] = True
        ii, jj = np.nonzero(need)
        vx = x0 + jj * cell; vy = y1 - ii * cell
        vh = elev_l17(vx, vy) + lift
        if kind in ('LAKE', 'BREACH'):   # far away D3D9Client draws the summit coarse: keep above every level's ground
            vh = np.maximum(vh, np.max([elev_lvl(L, vx, vy) for L in (13, 14, 15, 16)], 0) + lift)
        idx = -np.ones((nc + 1, nc + 1), int); idx[ii, jj] = np.arange(len(ii))
        verts = [f'{vx[q] - anchor[0]:.3f} {vh[q] - h0:.3f} {vy[q] - anchor[1]:.3f} 0 1 0 {(vx[q] - x0) / (x1 - x0):.5f} {(y1 - vy[q]) / (y1 - y0):.5f}' for q in range(len(ii))]
        tris = []
        for i, j in zip(*np.nonzero(keep)):
            nw, ne, sw, se = idx[i, j], idx[i, j + 1], idx[i + 1, j], idx[i + 1, j + 1]
            tris += [f'{sw} {nw} {ne}', f'{sw} {ne} {se}']
        groups.append((name, kind, verts, tris))
        print(f'glow {name}: {len(verts)} vertices, {len(tris)} triangles, {time.time() - t:.0f} s')
    with open(os.path.join(ROOT, 'Meshes', 'IoLava', 'GlowField.msh'), 'w') as f:
        f.write(f'MSHX1\nGROUPS {len(groups)}\n')
        for gi, (name, kind, verts, tris) in enumerate(groups):
            # 13 = no shadow (1) + no lighting (4) + additive with the background (8): black is see-through
            f.write(f'LABEL glow_{name}\nMATERIAL {gi + 1}\nTEXTURE {gi + 1}\nFLAG 77\nGEOM {len(verts)} {len(tris)}\n')
            f.write('\n'.join(verts) + '\n' + '\n'.join(tris) + '\n')
        f.write(f'MATERIALS {len(groups)}\n' + ''.join(f'glow_{g[0]}\n' for g in groups))
        for g in groups:
            f.write(f'MATERIAL glow_{g[0]}\n1 1 1 1\n0 0 0 1\n0 0 0 1 0\n1 1 1 1\n')
        f.write(f'TEXTURES {len(groups)}\n' + ''.join(f'IoLava\\glow_{g[0]}.dds\n' for g in groups))
    # touchdown: three ground points 60 m out, so Orbiter rests the vessel level and the mesh on the terrain
    extra = [f'TOUCHDOWN {e:9.3f} {n_:9.3f} {0.0:8.3f}' for e, n_ in TD_FLAT]
    extra.append('MESH IoLava\\GlowField')
    extra += [f'GLOWGROUP {gi} {"LAKE" if g[1] == "BREACH" else g[1]}' for gi, g in enumerate(groups)]
    cfgp = os.path.join(CFG, 'Effects.cfg')
    lines = [l for l in open(cfgp, encoding='utf-8').read().splitlines() if not l.startswith(('TOUCHDOWN', 'MESH', 'GLOWGROUP'))]
    open(cfgp, 'w', encoding='utf-8').write('\n'.join(lines + extra) + '\n')
    print('\n'.join(extra))

# ---------------------------------------------------------------- the summit pit lining (IoLava\Vent)
def build_vent(R=None, cell=0.5, lift=0.04):
    """an opaque skin over the pit exactly on the level-17 ground the crew walks on: lit cinder walls, an unlit lava
    lake floor (crust plates, molten seams). Hides the planet's see-through when the camera goes into the pit."""
    import math as m_
    R = R or field.R_PIT + 10
    c = field.SHIELD.copy()
    lake = field.H_SHIELD + field.H_CONE - field.D_PIT + float(stock_elev(*[np.array(v) for v in S.to_latlng(*c)]))
    # touchdown: three rim points; the vessel origin sits on their mean height (their plane at the centre)
    tp = [(c[0] + 34 * np.sin(np.radians(b)), c[1] + 34 * np.cos(np.radians(b))) for b in (0, 240, 120)]
    th = [float(elev_l17(x, y)[0]) for x, y in tp]
    h_ref = sum(th) / 3
    n = int(2 * R / cell)
    xs = c[0] - R + np.arange(n + 1) * cell; ys = c[1] + R - np.arange(n + 1) * cell
    X, Y = np.meshgrid(xs, ys)
    H = elev_l17(X.ravel(), Y.ravel()).reshape(X.shape) + lift
    inside = np.hypot(X - c[0], Y - c[1]) <= R
    gy, gx = np.gradient(H, cell)                                   # rows go south: d/dy = -gradient along rows
    nrm = np.stack([-gx, np.ones_like(H), gy], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    # textures over the box
    rock = field.evaluate(*np.meshgrid(np.linspace(c[0] - R, c[0] + R, 512), np.linspace(c[1] + R, c[1] - R, 512)), 2 * R / 512)['rgb']
    Image.fromarray((np.clip(rock, 0, 1) * 255).astype(np.uint8)).save(os.path.join(TEX, '..', 'IoLava', 'vent_rock.dds'), pixel_format='DXT1')
    lava = glow_image(c[0] - R, c[0] + R, c[1] - R, c[1] + R, 512)
    lava = np.maximum(lava, np.array([70, 8, 0], np.uint8))         # even the oldest plate is dull red-hot (minutes old)
    Image.fromarray(lava).save(os.path.join(TEX, '..', 'IoLava', 'vent_lake.dds'), pixel_format='DXT1')
    groups = {'walls': ([], []), 'lake': ([], [])}
    vidx = {'walls': {}, 'lake': {}}
    def vert(g, i, j):
        key = (i, j)
        if key not in vidx[g]:
            vidx[g][key] = len(groups[g][0])
            x, y, h = X[i, j], Y[i, j], H[i, j]
            u, v = (x - (c[0] - R)) / (2 * R), ((c[1] + R) - y) / (2 * R)
            nn = nrm[i, j]
            groups[g][0].append(f'{x - c[0]:.3f} {h - h_ref:.3f} {y - c[1]:.3f} {nn[0]:.4f} {nn[1]:.4f} {nn[2]:.4f} {u:.5f} {v:.5f}')
        return vidx[g][key]
    for i in range(n):
        for j in range(n):
            if not (inside[i, j] and inside[i + 1, j + 1] and inside[i, j + 1] and inside[i + 1, j]): continue
            g = 'lake' if H[i:i + 2, j:j + 2].mean() < lake + 1.0 else 'walls'
            nw, ne, sw, se = vert(g, i, j), vert(g, i, j + 1), vert(g, i + 1, j), vert(g, i + 1, j + 1)
            groups[g][1].extend([f'{sw} {nw} {ne}', f'{sw} {ne} {se}'])
    os.makedirs(os.path.join(ROOT, 'Meshes', 'IoLava'), exist_ok=True)
    bs = chr(92)
    with open(os.path.join(ROOT, 'Meshes', 'IoLava', 'VentPit.msh'), 'w') as f:
        f.write('MSHX1\nGROUPS 2\n')
        f.write(f'LABEL walls\nMATERIAL 1\nTEXTURE 1\nGEOM {len(groups["walls"][0])} {len(groups["walls"][1])}\n')
        f.write('\n'.join(groups['walls'][0]) + '\n' + '\n'.join(groups['walls'][1]) + '\n')
        # 5 = no shadow + no lighting: the molten lake shines by itself
        f.write(f'LABEL lake\nMATERIAL 2\nTEXTURE 2\nFLAG 5\nGEOM {len(groups["lake"][0])} {len(groups["lake"][1])}\n')
        f.write('\n'.join(groups['lake'][0]) + '\n' + '\n'.join(groups['lake'][1]) + '\n')
        f.write('MATERIALS 2\nrock\nlava\n')
        f.write('MATERIAL rock\n1 1 1 1\n0.2 0.2 0.2 1\n0.1 0.1 0.1 1 10\n0 0 0 1\n')
        f.write('MATERIAL lava\n1 1 1 1\n0 0 0 1\n0 0 0 1 0\n1 1 1 1\n')
        f.write(f'TEXTURES 2\nIoLava{bs}vent_rock.dds\nIoLava{bs}vent_lake.dds\n')
    la, ln = S.to_latlng(*c)
    with open(os.path.join(CFG, 'Vent.cfg'), 'w', encoding='utf-8') as f:
        f.write('; Summit pit lining for Modules/IoLava.dll (IoLava' + bs + bs + 'Vent). Generated by build.py vent.\n')
        f.write(f'POS {m_.degrees(ln):.7f} {m_.degrees(la):.7f}\nMESH IoLava{bs}VentPit\n')
        for (x, y), h in zip(tp, th):
            f.write(f'TOUCHDOWN {x - c[0]:.3f} {y - c[1]:.3f} {h - h_ref:.3f}\n')
    print(f'vent: walls {len(groups["walls"][1])} tris, lake {len(groups["lake"][1])} tris; lake level {lake:.1f} m, '
          f'floor min {H[inside].min():.1f} m; POS {m_.degrees(ln):.7f} {m_.degrees(la):.7f}')

# ---------------------------------------------------------------- footprints through thin crust (IoLava.dll)
FP_AGE_MAX = 1800.0          # s: by then the exposed melt is below the Draper point

def build_footprints(nslots=64):
    """texture strip: u = log age (1 s .. FP_AGE_MAX) -> the colour of melt exposed that long ago (cooling.py) ;
    mesh: nslots quads, collapsed; the module moves them under the crew's feet where the crust broke"""
    import cooling
    n = 256
    u = (np.arange(n) + 0.5) / n
    age = 10 ** (u * np.log10(FP_AGE_MAX))
    T = cooling.surface_T(age)
    Y = glow.luminance(T); cY = glow.chroma(T) * Y[:, None]
    row = glow8(Y[None, :], cY[None, :])[0]
    img = np.repeat(row[None, :, :], 16, 0)
    # a soft footprint outline across v: dark rim, hot middle
    v = (np.arange(16) + 0.5) / 16
    img = (img * np.clip(1 - np.abs(v - 0.5) * 1.6, 0, 1)[:, None, None] ** 0.5).astype(np.uint8)
    Image.fromarray(img).save(os.path.join(TEX, '..', 'IoLava', 'footprint_cool.dds'), pixel_format='DXT1')
    bs = chr(92)
    with open(os.path.join(ROOT, 'Meshes', 'IoLava', 'Footprints.msh'), 'w') as f:
        f.write(f'MSHX1\nGROUPS 1\nLABEL footprints\nMATERIAL 1\nTEXTURE 1\nFLAG 77\nGEOM {4 * nslots} {2 * nslots}\n')
        for k in range(nslots):
            for (uu, vv) in ((0, 0), (0, 1), (1, 1), (1, 0)):
                f.write(f'0 -1000 0 0 1 0 {uu} {vv}\n')
        for k in range(nslots):
            b = 4 * k
            f.write(f'{b} {b + 1} {b + 2}\n{b} {b + 2} {b + 3}\n')
        f.write('MATERIALS 1\nfootprint\nMATERIAL footprint\n1 1 1 1\n0 0 0 1\n0 0 0 1 0\n1 1 1 1\n')
        f.write(f'TEXTURES 1\nIoLava{bs}footprint_cool.dds\n')
    cfgp = os.path.join(CFG, 'Effects.cfg')
    L = [l for l in open(cfgp, encoding='utf-8').read().splitlines() if not l.startswith('FOOTPRINT')]
    L += [f'FOOTPRINT_MESH IoLava{bs}Footprints', f'FOOTPRINT_PRM {nslots} {FP_AGE_MAX:.0f} 0.02 0.05']
    open(cfgp, 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    print('footprints: strip', row[[0, 64, 128, 192, 255]].tolist())

if __name__ == '__main__':
    what = sys.argv[1:] or ['elev', 'thermal', 'surf', 'effects']
    if 'elev' in what: build_elev()
    if 'thermal' in what: build_thermal()
    if 'surf' in what: build_surf()
    if 'effects' in what: build_effects()
    if 'glowpatch' in what: glow_patch()
    if 'glowmesh' in what: build_glowmesh()
    if 'vent' in what: build_vent()
    if 'footprints' in what: build_footprints()
