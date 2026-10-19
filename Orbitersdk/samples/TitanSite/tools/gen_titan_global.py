"""Titan for Orbiter 2024 (TileFormat 2): global Surf/Mask (levels 1-9) and Elev (4-8) in Textures/Titan.

Same sources and tile layout as Orbitersdk/samples/Tantra/tools/gen_titan_tiles.py (written for the
Orbiter 2016 install; its data stays in "Orbiter 2016/Install/Titan_src"):
  Cassini ISS 4 km mosaic PIA19658 (brightness), Birch et al. 2017 seas/lakes, Miller et al. 2021
  rivers, Cassini RADAR GTDR "GTI" topography.

Differences from the Tantra builder:
  * the tiles hold surface ALBEDO (sRGB-encoded, slight red slope of tholin-coated ground), not the
    orange haze-filtered light: the orange of the light comes from the atmosphere rendering
    (Config/GC/Titan.atm.cfg, owned by the atmosphere work), so it is not baked twice;
  * sea level: the seas of a hemisphere share one level (an equipotential surface), taken as the
    median GTI along their shorelines (-617 m in the north). The 10th percentile inside the seas
    (-926 m) sat ~400 m below the shore terrain: GTI is interpolated across the seas.

Usage: gen_titan_global.py <orbiter 2024 root> <Titan_src folder>
"""
import os
import sys

import numpy as np
from scipy.ndimage import binary_dilation

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Tantra", "tools"))
import gen_titan_tiles as g  # noqa: E402

TINT = np.array([1.12, 1.0, 0.84])     # reflectance rises toward red (Huygens DISR surface spectrum)
A_DARK, A_BRIGHT, A_LIQUID = 0.055, 0.20, 0.010   # dune/sediment, ice-rich uplands, methane-ethane


def albedo_to_srgb8(a_rgb):
    return np.clip(np.power(np.clip(a_rgb, 0, 1), 1 / 2.2) * 255 + 0.5, 0, 255).astype(np.uint8)


def surface_rgb(iss, lon, lat, feats, detail):
    t = g.sample(iss, lon, lat, order=3)
    if detail:
        t = np.clip(t + 0.08 * g.detail_noise(lon, lat), 0, 1)
    a = A_DARK + (A_BRIGHT - A_DARK) * np.clip(t, 0, 1) ** 1.1
    liq, emp, riv = feats
    a = a * (1 + 0.15 * emp) * (1 - 0.25 * riv)
    a = a * (1 - liq) + A_LIQUID * liq
    return albedo_to_srgb8(a[..., None] * TINT)


BED_SLOPE, BED_MAX = 0.01, 15.0     # sea bed: 1 m per 100 m from the shore, at most 15 m (as the landing site)
GLOBAL_BEDS = False                 # beds for all seas: off (not agreed with the user); seas stay flat at their level
ELEV_RES = 0.1                      # [m] per int16 step in the elevation files (Titan.cfg ElevationResolution)


def flatten_seas(gti, liquid):
    """Seas of each hemisphere: surface at the median height of their shoreline (returned), bed below it
    (distance to the shore on the GTI grid, 1 m per 100 m, at most 15 m), so that the liquid shading,
    which takes the depth from the terrain, sees the same depth on every level and every sea."""
    from scipy.ndimage import distance_transform_edt
    sea = g.raster(gti.shape[1], gti.shape[0], -180, 180, -90, 90, liquid) > 0.5
    px_m = 2575e3 * np.radians(180.0 / gti.shape[0])
    levels = []
    for hemi in (slice(0, 720), slice(720, 1440)):
        m = sea[hemi]
        ring = binary_dilation(m, iterations=2) & ~m
        level = float(np.median(gti[hemi][ring])) if ring.any() else 0.0
        dist = (distance_transform_edt(m) - 0.5).clip(0) * px_m
        gti[hemi][m] = level - (np.minimum(BED_MAX, BED_SLOPE * dist[m]) if GLOBAL_BEDS else 0.0)
        levels.append(level)
    return levels


def write_elv(path, elev, lon0, lon1, lat0, lat1):
    """259x259 metres, rows from the bottom, int16 at ELEV_RES (the Tantra writer rounds to 1 m)."""
    import struct
    os.makedirs(os.path.dirname(path), exist_ok=True)
    e = np.clip(np.round(elev / ELEV_RES), -32767, 32767).astype(np.int16)
    hdr = struct.pack("<4siiiiii9d", b"ELE", 100, -16, 259, 259, 1, 1, ELEV_RES, 0.0,
                      np.radians(lat0), np.radians(lat1), np.radians(lon0), np.radians(lon1),
                      float(e.min()) * ELEV_RES, float(e.max()) * ELEV_RES, float(e.mean()) * ELEV_RES)
    with open(path, "wb") as f:
        f.write(hdr)
        f.write(e.astype("<i2").tobytes())


def build(root, src):
    out = os.path.join(root, "Textures", "Titan")
    iss = g.load_iss(src)
    liquid, empty, rivers = g.load_lakes(src)
    gti = g.load_gti(src)
    print("sea levels (north, south): %.0f, %.0f m" % tuple(flatten_seas(gti, liquid)), flush=True)
    counts = {"Surf": 0, "Mask": 0, "Elev": 0}

    def surf_and_mask(n, ilat, ilng, size=512):
        lon0, lon1, lat0, lat1 = g.tile_bounds(n, ilat, ilng)
        lon, lat = g.grid(size, lon0, lon1, lat0, lat1)
        feats = g.draw_features(size, lon0, lon1, lat0, lat1, liquid, empty, rivers, 1 if n < 8 else 2)
        rgb = surface_rgb(iss, lon, lat, feats, detail=n >= 7)
        g.save_dds(rgb, os.path.join(out, "Surf", f"{n:02d}", f"{ilat:06d}", f"{ilng:06d}.dds"))
        counts["Surf"] += 1
        liq = feats[0] > 0.5
        if liq.any() or n <= 4:
            alpha = np.where(liq, 0, 255).astype(np.uint8)
            g.save_dds(np.zeros_like(rgb), os.path.join(out, "Mask", f"{n:02d}", f"{ilat:06d}", f"{ilng:06d}.dds"), alpha)
            counts["Mask"] += 1

    for n, size in ((1, 128), (2, 256), (3, 512)):          # whole planet in one square
        lon, lat = g.grid(size, -180, 180, -90, 90)
        feats = g.draw_features(size, -180, 180, -90, 90, liquid, empty, rivers, 1)
        rgb = surface_rgb(iss, lon, lat, feats, detail=False)
        g.save_dds(rgb, os.path.join(out, "Surf", f"{n:02d}", "000000", "000000.dds"))
        alpha = np.where(feats[0] > 0.5, 0, 255).astype(np.uint8)
        g.save_dds(np.zeros_like(rgb), os.path.join(out, "Mask", f"{n:02d}", "000000", "000000.dds"), alpha)

    for n in range(4, g.MAXLEVEL_POLAR + 1):
        nlat, nlng = 2 ** (n - 4), 2 ** (n - 3)
        for ilat in range(nlat):
            lon0, lon1, lat0, lat1 = g.tile_bounds(n, ilat, 0)
            if n > g.MAXLEVEL_GLOBAL and min(abs(lat0), abs(lat1)) < g.POLAR_LAT:
                continue
            for ilng in range(nlng):
                surf_and_mask(n, ilat, ilng)
        print(f"level {n}: Surf {counts['Surf']}, Mask {counts['Mask']}", flush=True)

    for n in range(4, g.MAXLEVEL_ELEV + 1):                  # 259x259 nodes, padding, rows from the bottom
        nlat, nlng = 2 ** (n - 4), 2 ** (n - 3)
        for ilat in range(nlat):
            for ilng in range(nlng):
                lon0, lon1, lat0, lat1 = g.tile_bounds(n, ilat, ilng)
                i = np.arange(259) - 1
                LON, LAT = np.meshgrid(lon0 + i / 256 * (lon1 - lon0), lat0 + i / 256 * (lat1 - lat0))
                e = g.sample(gti, ((LON + 180) % 360) - 180, np.clip(LAT, -89.99, 89.99), order=3)
                write_elv(os.path.join(out, "Elev", f"{n:02d}", f"{ilat:06d}", f"{ilng:06d}.elv"), e, lon0, lon1, lat0, lat1)
                counts["Elev"] += 1
        print(f"elev level {n}: {counts['Elev']}", flush=True)
    print("done:", counts, flush=True)


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2])
