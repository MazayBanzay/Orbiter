"""Titan landing site for Orbiter 2024: detailed tiles (levels 10-18) at a drowned river valley (ria)
on the eastern shore of a peninsula in southern Kraken Mare, ~59.33 N 40.49 E.

What comes from data (Orbiter 2016/Install/Titan_src, same as gen_titan_global.py):
  * shoreline: Birch et al. 2017 polar map, sea polygons (Fm) - the inlets of the rias are in it;
  * rivers: Miller et al. 2021 networks - the valleys that drain into the inlets;
  * brightness: Cassini ISS 4 km mosaic; large-scale height: Cassini RADAR GTDR (GTI);
  * sea level: as the global tiles, the median GTI along the northern sea shores.
What is synthesised (the data stop at ~0.3-4 km): everything finer - a fractal coastline around
the mapped one, meandering valleys and channels along the mapped rivers, rugged ice-bedrock hills
inland (Kraken shores are dissected uplands in SAR), cobble fans at the river mouths (the
Huygens landing site look: rounded ice cobbles on dark organic sand, at the scale of the micro
textures), a dark wet margin and a perfectly flat liquid surface.

One continuous function of (lon, lat) gives height and albedo, so every level samples the same
terrain and the detail fades into the global GTI height and ISS colour 45-60 km from the centre.

Tiles: Surf DXT1 512x512 (albedo, sRGB, slight red slope - the orange light is the atmosphere's),
Mask DXT5 512x512 (alpha 0 = liquid, smooth edge; rgb 0), Elev 259x259 int16 at 0.5 m.

Usage: gen_titan_site.py <orbiter 2024 root> <Titan_src folder> [workers] [levels, e.g. 10,11]
"""
import os
import struct
import sys
import time
from multiprocessing import Pool

import numpy as np
from scipy.ndimage import distance_transform_edt, map_coordinates

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "Tantra", "tools"))
import gen_titan_tiles as g      # noqa: E402  data loaders, tile layout, DDS writer
import gen_titan_global as gg    # noqa: E402  albedo convention

R = 2575.0                                  # km
LAT_C, LON_C = 59.3335, 40.4935             # site centre: a mapped river reaches the head of a ria
COSC = np.cos(np.radians(LAT_C))
RES = 0.025                                 # master raster spacing [km]
HALF = 66.0                                 # master raster half-size [km]
FADE0, FADE1 = 45.0, 60.0                   # detail fades into the global tiles [km]
# half-size [km] of the area covered at each level (levels 10-13 cover the whole fade zone)
LEVELS = {10: 64, 11: 64, 12: 64, 13: 64, 14: 40, 15: 24, 16: 12, 17: 6, 18: 2.6}
# Seabed: the client shades terrain below AtmoFX_LiquidLevel under the liquid mask as bed seen through
# the liquid (no separate surface geometry), so the depth is capped to keep the parallax small.
SEABED = True
BED_SLOPE = 0.01        # 1 m per 100 m from the waterline (radar depth profiles of Ligeia/Kraken)
BED_MAX = 15.0          # m: enough to wade and submerge; deeper would read as a basin from the shore
ELEV_ONLY = os.environ.get("TITAN_ELEV_ONLY") == "1"     # rebuild elevation only (surface and masks unchanged)
MASK_ONLY = os.environ.get("TITAN_MASK_ONLY") == "1"   # rebuild masks only (surface and height unchanged)
ELEV_SCALE = 0.1                            # [m] per int16 step (Titan.cfg ElevationResolution 0.1: 0.5 m steps made a wall at the waterline)


# ------------------------------------------------------------------------------- frames
def to_xy(lon, lat):
    return R * COSC * np.radians(lon - LON_C), R * np.radians(lat - LAT_C)


def to_lonlat(x, y):
    return LON_C + np.degrees(x / (R * COSC)), LAT_C + np.degrees(y / R)


# ------------------------------------------------------------------------------- noise
_P = np.random.default_rng(2005).permutation(4096).astype(np.int64)
_P = np.concatenate([_P, _P])
_ANG = np.random.default_rng(14).random(4096) * 2 * np.pi
_GX, _GY = np.cos(_ANG), np.sin(_ANG)


def perlin(x, y, seed=0):
    """2-D gradient noise, about -1..1, period 4096 cells."""
    xi, yi = np.floor(x), np.floor(y)
    fx, fy = x - xi, y - yi
    xi = xi.astype(np.int64) & 4095
    yi = (yi.astype(np.int64) + seed * 977) & 4095

    def grad(ix, iy, dx, dy):
        h = _P[_P[ix] + iy]
        return _GX[h] * dx + _GY[h] * dy
    u = fx * fx * fx * (fx * (fx * 6 - 15) + 10)
    v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    n00 = grad(xi, yi, fx, fy)
    n10 = grad((xi + 1) & 4095, yi, fx - 1, fy)
    n01 = grad(xi, (yi + 1) & 4095, fx, fy - 1)
    n11 = grad((xi + 1) & 4095, (yi + 1) & 4095, fx - 1, fy - 1)
    return 1.414 * ((n00 * (1 - u) + n10 * u) * (1 - v) + (n01 * (1 - u) + n11 * u) * v)


def fbm(x, y, lmax, lmin, amp_of, seed, ridged=False):
    """Sum of octaves with wavelengths lmax, lmax/2, ... >= lmin [km]; amp_of(lambda) per octave.
    Octaves finer than the sampling (lmin) are left out, so coarse tiles do not alias."""
    out = np.zeros_like(x)
    lam, k = lmax, 0
    while lam >= lmin:
        c, s = np.cos(0.7 * k), np.sin(0.7 * k)          # rotate octaves: no grid alignment
        n = perlin((c * x - s * y) / lam + 31.7 * k, (s * x + c * y) / lam - 17.3 * k, seed + k)
        out += amp_of(lam) * ((1 - np.abs(n)) ** 2 if ridged else n)
        lam, k = lam / 2, k + 1
    return out


def ridged_mf(x, y, lmax, lmin, seed, gain=2.0):
    """Ridged multifractal (Musgrave): sharp crests, each octave weighted by the one above. ~0..1."""
    out = np.zeros_like(x)
    w = np.ones_like(x)
    lam, k, amp, norm = lmax, 0, 1.0, 0.0
    while lam >= lmin:
        c, s = np.cos(0.9 * k + 0.3), np.sin(0.9 * k + 0.3)
        n = perlin((c * x - s * y) / lam + 13.1 * k, (s * x + c * y) / lam + 7.7 * k, seed + 31 * k)
        r = (1 - np.abs(n)) ** 2
        r *= w
        w = np.clip(r * gain, 0, 1)
        out += r * amp
        norm += amp
        lam, k, amp = lam / 2, k + 1, amp * 0.55
    return out / max(norm, 1e-9)


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


# ------------------------------------------------------------------------------- master data
def prepare(src, cache):
    if os.path.exists(cache):
        return
    t0 = time.time()
    liquid, empty, rivers = g.load_lakes(src)
    gti = g.load_gti(src)
    level = gg.flatten_seas(gti, liquid)[0]       # northern sea level, as the global tiles
    n = int(round(2 * HALF / RES))
    lon0, lat0 = to_lonlat(-HALF, -HALF)
    lon1, lat1 = to_lonlat(HALF, HALF)
    wet = g.raster(n, n, lon0, lon1, lat0, lat1, liquid) > 0.5
    riv = g.raster(n, n, lon0, lon1, lat0, lat1, rivers, line=True, line_w=1) > 0.5
    d_land = distance_transform_edt(~wet) * RES        # land: distance to the sea
    d_sea = distance_transform_edt(wet) * RES          # sea: distance to the land
    sd = np.where(wet, -d_sea, d_land).astype(np.float32)
    dr = (distance_transform_edt(~riv) * RES).astype(np.float32)
    np.savez(cache, sd=sd, dr=dr, level=level, n=n)
    print(f"master rasters {n}x{n} at {RES * 1000:.0f} m, sea level {level:.1f} m, {time.time() - t0:.0f} s", flush=True)


_M = {}


def load(cache, src):
    if not _M:
        z = np.load(cache)
        _M.update(sd=z["sd"], dr=z["dr"], level=float(z["level"]), n=int(z["n"]))
        _M["iss"] = g.load_iss(src)
        _M["gti"] = g.load_gti(src)
        _M["liquid"] = g.load_lakes(src)[0]
        gg.flatten_seas(_M["gti"], _M["liquid"])   # as the global elevation tiles
    return _M


def master(name, x, y):
    n = _M["n"]
    c = (x + HALF) / RES - 0.5
    r = (HALF - y) / RES - 0.5
    return map_coordinates(_M[name], [r, c], order=1, mode="nearest")


# ------------------------------------------------------------------------------- terrain
def fields(lon, lat, px, liq=None, extra=False):
    """Height [m], albedo, signed shore distance [km] at lon/lat; px = sample spacing [km].
    extra=True adds a dict: "wet" - how wet the ground can get 0..1 (mask R; the time of rain is the
    atmosphere's), "cls" - ground class 0 sand, 1 cobbles, 2 ice bedrock, 3 wet margin, 4 liquid.
    liq: the sea polygons rasterised on the same grid - used outside the master raster, where the
    distance field would only repeat its edge (all of it lies beyond the fade, at the global look)."""
    x, y = to_xy(lon, lat)
    sd0 = master("sd", x, y)
    if liq is not None:
        inside = (np.abs(x) < HALF - 0.5) & (np.abs(y) < HALF - 0.5)
        sd0 = np.where(inside, sd0, np.where(liq > 0.5, -5.0, 5.0))
    L = _M["level"]
    lmin = 2.0 * px
    s = sd0 + fbm(x, y, 3.0, lmin, lambda l: 0.10 * l, seed=1)        # fractal coast
    # meanders: displace where the river raster is read (adding noise to the distance itself
    # splits the channel into closed loops around its zero level)
    mx = fbm(x, y, 1.2, max(lmin, 0.05), lambda l: 0.12 * l, seed=2)
    my = fbm(x, y, 1.2, max(lmin, 0.05), lambda l: 0.12 * l, seed=21)
    dr = master("dr", x + mx, y + my)
    gti = g.sample(_M["gti"], lon, lat, order=3)

    # land relief: dissected ice-bedrock uplands (ridged multifractal on warped coordinates)
    base = L + (np.maximum(gti, L) - L) * smoothstep(0.0, 5.0, s)
    rise = 45.0 * (1 - np.exp(-np.maximum(s, 0) / 1.2))
    wx = fbm(x, y, 8.0, max(lmin, 0.5), lambda l: 0.15 * l, seed=11)
    wy = fbm(x, y, 8.0, max(lmin, 0.5), lambda l: 0.15 * l, seed=12)
    ridges = ridged_mf(x + wx, y + wy, 9.0, max(lmin, 0.03), seed=3)            # 0..1
    up = smoothstep(0.2, 2.8, s)
    broad = fbm(x, y, 16.0, max(lmin, 0.5), lambda l: 0.5 * (l / 16.0) ** 0.7, seed=13)    # -1..1
    hills = (120.0 * ridges + 55.0 * (broad + 1.0)) * up
    rough = fbm(x, y, 0.4, lmin, lambda l: 6.0 * l ** 0.8, seed=4)
    hland = base + rise + hills + rough
    # gully network: creases of a second ridged field, cut deeper where the land stands higher
    crease = fbm(x + 0.5 * wy, y + 0.5 * wx, 2.4, max(lmin, 0.3), lambda l: (l / 2.4) ** 0.5, seed=7, ridged=True) / 1.6
    gul = smoothstep(0.4, 0.95, crease) * up
    hland -= 0.25 * (hland - L - rise) * gul
    # mapped rivers: valley floor falls gently to the shore, a channel along its axis
    vm = np.exp(-(dr / 0.7) ** 2)
    floor = L + 6.0 * np.clip(s, 0, None) + 0.3 * rough
    cm = np.exp(-(dr / 0.025) ** 2)
    h = hland + (floor - hland) * 0.85 * vm - 2.5 * cm
    # beach: heights come down to the waterline
    h = L + (h - L) * smoothstep(0.0, 0.08, s)                       # flush with the liquid at the waterline
    # under the liquid: the bed (SEABED) or, before the client could draw a liquid surface, flat
    bed = L - np.minimum(BED_MAX, -s * 1000.0 * BED_SLOPE * (1 + 0.25 * fbm(x, y, 2.0, max(lmin, 0.05), lambda l: 1.0, seed=50)))
    h = np.where(s > 0, h, bed if SEABED else L)

    # albedo: the global (ISS) base, modulated by geology - exposed ice bright, sediment dark
    t = np.clip(g.sample(_M["iss"], lon, lat, order=3) + 0.08 * g.detail_noise(lon, lat), 0, 1)
    a_glob = gg.A_DARK + (gg.A_BRIGHT - gg.A_DARK) * t ** 1.1
    gy, gx = np.gradient(np.where(s > 0, h - rough, h), px * 1000.0)   # landform slope, not the small bumps
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    expo = np.clip(smoothstep(6.0, 22.0, slope) + 0.6 * smoothstep(0.35, 0.8, ridges) * up, 0, 1)
    sed = np.clip(0.8 * gul + vm * smoothstep(0.0, 3.0, s) + 0.6 * (1 - up), 0, 1)       # gullies, valleys, coastal lowland
    a = a_glob * (1 + 0.55 * expo - 0.35 * sed)
    a *= 1 + fbm(x, y, 1.0, lmin, lambda l: 0.07 * (l / 1.0) ** 0.25, seed=5)   # faint texture only
    fan = vm * (1 - smoothstep(0.5, 2.0, s)) * smoothstep(0.0, 0.05, s)
    cob = 0.12 + 0.03 * fbm(x, y, 0.2, lmin, lambda l: 1.0, seed=6)
    a += (cob - a) * np.clip(0.8 * fan + 0.9 * cm, 0, 1)               # cobble fans and channel beds
    a *= 1 - 0.4 * (1 - smoothstep(0.0, 0.015, s))                    # wet margin
    a = np.where(s > 0, a, gg.A_LIQUID)

    # fade into the global tiles
    r = np.hypot(x, y)
    f = 1 - smoothstep(FADE0, FADE1, r)
    h = gti + f * (h - gti)
    a_g = np.where(sd0 > 0, a_glob, gg.A_LIQUID)
    a = a_g + f * (a - a_g)
    if not extra:
        return h, a, s
    # wetness: shore, coastal lowland, valley floors, channels, fans; dry on slopes and crests
    w = np.maximum.reduce([1 - smoothstep(0.0, 0.4, s), 0.55 * (1 - up), 0.7 * vm, cm, 0.9 * fan])
    w = np.clip(w * (1 - 0.8 * expo), 0, 1)
    w_g = 1 - smoothstep(0.0, 0.4, sd0)
    w = np.where(s > 0, w_g + f * (w - w_g), 1.0)
    cls = np.zeros(s.shape, np.uint8)                                     # 0 sand
    cls[(fan > 0.35) | (cm > 0.4) | ((vm > 0.5) & (s < 3.0))] = 1        # cobbles
    cls[expo > 0.6] = 2                                                   # ice bedrock
    cls[(s > 0) & (s < 0.05)] = 3                                         # wet margin
    cls[s <= 0] = 4                                                       # liquid
    # liquid depth (no bathymetry in the data): the bed falls ~0.6 deg from the shore, as in the
    # radar depth profiles of Ligeia and Kraken (~10 m at ~1 km out), with a faint unevenness
    depth = np.where(s < 0, np.minimum(BED_MAX, -s * 1000.0 * BED_SLOPE * (1 + 0.25 * fbm(x, y, 2.0, max(lmin, 0.05), lambda l: 1.0, seed=50))), 0.0)
    return h, a, s, {"wet": w, "cls": cls, "depth": np.maximum(depth, 0.0)}


# ------------------------------------------------------------------------------- tiles
def tiles_for(n, half):
    lon0, lat0 = to_lonlat(-half, -half)
    lon1, lat1 = to_lonlat(half, half)
    nlat, nlng = 2 ** (n - 4), 2 ** (n - 3)
    i0 = int(np.floor((90 - lat1) / (180 / nlat)))
    i1 = int(np.floor((90 - lat0) / (180 / nlat)))
    j0 = int(np.floor((lon0 + 180) / (360 / nlng)))
    j1 = int(np.floor((lon1 + 180) / (360 / nlng)))
    return [(n, i, j) for i in range(i0, i1 + 1) for j in range(j0, j1 + 1)]


def write_elv(path, elev, lon0, lon1, lat0, lat1):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    e = np.clip(np.round(elev / ELEV_SCALE), -32767, 32767).astype(np.int16)
    hdr = struct.pack("<4siiiiii9d", b"ELE\x01", 100, -16, 259, 259, 1, 1, ELEV_SCALE, 0.0,
                      np.radians(lat0), np.radians(lat1), np.radians(lon0), np.radians(lon1),
                      float(e.min()) * ELEV_SCALE, float(e.max()) * ELEV_SCALE, float(e.mean()) * ELEV_SCALE)
    with open(path, "wb") as f:
        f.write(hdr)
        f.write(e.astype("<i2").tobytes())


def save_dxt5(rgba, path):
    import io
    from PIL import Image
    os.makedirs(os.path.dirname(path), exist_ok=True)
    buf = io.BytesIO()
    Image.fromarray(rgba).save(buf, "DDS", pixel_format="DXT5")
    with open(path, "wb") as f:
        f.write(buf.getvalue())


def elev_tile(out, name, lon0, lon1, lat0, lat1):
    k = np.arange(259) - 1
    LON, LAT = np.meshgrid(lon0 + k / 256 * (lon1 - lon0), lat0 + k / 256 * (lat1 - lat0))
    he, _, s_ = fields(LON, LAT, R * np.radians(lat1 - lat0) / 256)
    write_elv(os.path.join(out, "Elev", name + ".elv"), he, lon0, lon1, lat0, lat1)
    return name, float(he.min()), float(he.max()), float((s_ < 0).mean())


def make_tile(job):
    root, src, cache, (n, i, j) = job
    load(cache, src)
    out = os.path.join(root, "Textures", "Titan")
    lon0, lon1, lat0, lat1 = g.tile_bounds(n, i, j)
    name = f"{n:02d}/{i:06d}/{j:06d}"
    # surface + mask, 512 px
    if ELEV_ONLY:
        return elev_tile(out, name, lon0, lon1, lat0, lat1)
    lon, lat = g.grid(512, lon0, lon1, lat0, lat1)
    px = R * np.radians(lat1 - lat0) / 512
    liq = g.raster(512, 512, lon0, lon1, lat0, lat1, _M["liquid"])
    h, a, s, ex = fields(lon, lat, px, liq, extra=True)
    if not MASK_ONLY:
        g.save_dds(gg.albedo_to_srgb8(a[..., None] * gg.TINT), os.path.join(out, "Surf", name + ".dds"))
    alpha = np.clip(255 * smoothstep(-px, px, s), 0, 255).astype(np.uint8)
    wet = np.clip(ex["wet"] * 255 + 0.5, 0, 255).astype(np.uint8)          # R = wetness
    dep = np.clip(ex["depth"] / 10.0 * 255 + 0.5, 0, 255).astype(np.uint8)  # G = liquid depth / 10 m
    z = np.zeros((512, 512), np.uint8)
    save_dxt5(np.dstack([wet, dep, z, alpha]), os.path.join(out, "Mask", name + ".dds"))
    if MASK_ONLY:
        return name, 0.0, 0.0, float((s < 0).mean())
    # elevation, 259 nodes with one-node padding, rows from the bottom
    k = np.arange(259) - 1
    LON, LAT = np.meshgrid(lon0 + k / 256 * (lon1 - lon0), lat0 + k / 256 * (lat1 - lat0))
    he, _, _ = fields(LON, LAT, R * np.radians(lat1 - lat0) / 256)
    write_elv(os.path.join(out, "Elev", name + ".elv"), he, lon0, lon1, lat0, lat1)
    return name, float(he.min()), float(he.max()), float((s < 0).mean())


def coarse_elev(root, src):
    """Elevation tiles of levels 4-9 that touch the site, from the site function (it fades into GTI).
    D3D9Client builds a tile without its own elevation file by interpolating the nearest ANCESTOR
    WHOSE DATA ARE ALREADY LOADED (Surfmgr2.cpp, SurfTile::LoadElevationData, has_elevfile). After a
    zoom-out only the global levels stay loaded, so new site tiles could be shaped by the global GTI
    (shore land ~120 m above the site terrain) and stand above the vessel the core keeps on the
    site files. With these, every ancestor near the site carries the same terrain, only coarser."""
    cache = os.path.join(HERE, "_titan_site_master_v3.npz")
    load(cache, src)
    out = os.path.join(root, "Textures", "Titan", "Elev")
    for n in range(4, 10):
        for (_, i, j) in tiles_for(n, HALF):
            lon0, lon1, lat0, lat1 = g.tile_bounds(n, i, j)
            k = np.arange(259) - 1
            LON, LAT = np.meshgrid(lon0 + k / 256 * (lon1 - lon0), lat0 + k / 256 * (lat1 - lat0))
            LAT = np.clip(LAT, -89.99, 89.99)
            LON = ((LON + 180) % 360) - 180
            he, _, _ = fields(LON, LAT, R * np.radians(lat1 - lat0) / 256)
            write_elv(os.path.join(out, f"{n:02d}", f"{i:06d}", f"{j:06d}.elv"), he, lon0, lon1, lat0, lat1)
            print(f"elev {n:02d}/{i:06d}/{j:06d}: {he.min():.0f}..{he.max():.0f} m", flush=True)


def main(root, src, workers=10, only=None):
    cache = os.path.join(HERE, "_titan_site_master_v3.npz")
    prepare(src, cache)
    jobs = []
    for n, half in LEVELS.items():
        if only and n not in only:
            continue
        jobs += [(root, src, cache, t) for t in tiles_for(n, half)]
    print(f"{len(jobs)} tiles, levels {min(LEVELS)}-{max(LEVELS)}", flush=True)
    t0 = time.time()
    with Pool(workers) as pool:
        for k, (name, lo, hi, wet) in enumerate(pool.imap_unordered(make_tile, jobs, chunksize=4)):
            if k % 50 == 0:
                print(f"{k}/{len(jobs)} {name} elev {lo:.0f}..{hi:.0f} m liquid {wet * 100:.0f}% ({time.time() - t0:.0f} s)", flush=True)
    print("done", flush=True)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 10,
         [int(v) for v in sys.argv[4].split(',')] if len(sys.argv) > 4 else None)   # e.g. 10,11,12,13
