"""Second Titan site: the Huygens landing site, 10.573 S 192.335 W (167.665 E), levels 8-18.

Huygens (14 Jan 2005) came down on a dark plain at the edge of a bright icy highland cut by short,
branching channels; the ground was damp organic sand with rounded water-ice cobbles 5-15 cm.
Large linear dune fields (Belet, Shangri-La) cover the dark equatorial terrain.

From data: the bright/dark pattern of the Cassini ISS 4 km mosaic (bright = icy highland, dark =
dune-covered plains), large-scale height from GTI. There are no mapped rivers or seas here.
Synthesised: linear dunes on the dark terrain - trending ~WSW-ENE (azimuth ~80 deg, the prevailing
Titan dune direction), spacing ~2-3 km, 40-100 m high, symmetric profile, sinuous crests,
Y-junctions and terminations; flat brighter interdunes; dissected icy hills with a branching gully
network on the bright terrain; a flat cobble plain around the landing point (no dunes within ~6 km,
as in the DISR panoramas).

Reuses the tile machinery of gen_titan_site.py with its own centre, levels and terrain function.
Usage: gen_titan_huygens.py <orbiter 2024 root> <Titan_src folder> [workers]
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_titan_site as s  # noqa: E402

# --- the site: patch the shared generator (also runs in every worker process on import)
s.LAT_C, s.LON_C = -10.573, 167.665
s.COSC = np.cos(np.radians(s.LAT_C))
s.LEVELS = {8: 64, 9: 64, 10: 64, 11: 64, 12: 64, 13: 64, 14: 40, 15: 24, 16: 12, 17: 6, 18: 2.6}
g, gg, fbm, ridged_mf, smoothstep = s.g, s.gg, s.fbm, s.ridged_mf, s.smoothstep
CACHE = os.path.join(s.HERE, "_titan_huygens_master.npz")


def prepare(src):
    """No seas or rivers here: the 'master' rasters only hold a far shore distance."""
    if os.path.exists(CACHE):
        return
    gti = g.load_gti(src)
    level = gg.flatten_seas(gti, g.load_lakes(src)[0])[1]
    n = 8
    np.savez(CACHE, sd=np.full((n, n), 999.0, np.float32), dr=np.full((n, n), 999.0, np.float32), level=level, n=n)


def fields(lon, lat, px, liq=None, extra=False):
    x, y = s.to_xy(lon, lat)
    M = s._M
    lmin = 2.0 * px
    gti = g.sample(M["gti"], lon, lat, order=3)
    t = np.clip(g.sample(M["iss"], lon, lat, order=3) + 0.08 * g.detail_noise(lon, lat), 0, 1)
    a_glob = gg.A_DARK + (gg.A_BRIGHT - gg.A_DARK) * t ** 1.1
    r = np.hypot(x, y)
    # terrain type from the ISS brightness: 0 dark (dunes) .. 1 bright (icy highland), soft boundary
    tb = smoothstep(0.52, 0.62, t + 0.05 * fbm(x, y, 6.0, max(lmin, 0.5), lambda l: 1.0, seed=31))
    r_pl = r + 3.0 * fbm(x, y, 8.0, max(lmin, 0.3), lambda l: (l / 8.0) ** 0.6, seed=43)
    plain = 1 - smoothstep(4.0, 9.0, r_pl)                  # the landing plain: no dunes, low relief, ragged edge

    # linear dunes: crest lines across a WSW-ENE trend, warped for sinuous crests and junctions
    az = np.radians(80.0)
    ux, uy = np.cos(az), -np.sin(az)                        # unit vector across the crests (~N-S)
    wx = fbm(x, y, 16.0, max(lmin, 0.8), lambda l: 0.06 * l * (l / 16.0) ** 0.6, seed=32)
    wy = fbm(x, y, 16.0, max(lmin, 0.8), lambda l: 0.06 * l * (l / 16.0) ** 0.6, seed=33)
    across = (x + wx) * ux + (y + wy) * uy
    # constant spacing plus a bounded phase shift (dividing a large coordinate by a varying spacing
    # scrambles the phase far from the centre into a maze)
    ph = across / 2.4 + 0.35 * fbm(x, y, 20.0, max(lmin, 1.0), lambda l: 1.0, seed=34)
    prof = 1 - np.abs(2 * (ph - np.floor(ph)) - 1)          # 0 interdune .. 1 crest, symmetric
    prof = smoothstep(0.25, 1.0, prof) ** 1.3               # flat interdunes ~40 % of the spacing
    hgt = 70.0 * (1 + 0.4 * fbm(x, y, 8.0, max(lmin, 0.4), lambda l: 1.0, seed=35))
    cut = smoothstep(-0.75, -0.35, fbm(x, y, 12.0, max(lmin, 0.6), lambda l: 1.0, seed=36))  # rare terminations: crests run for tens of km
    dunes = hgt * prof * cut * (1 - tb) * (1 - plain)
    # icy highland: ridged hills with a branching gully network, cut deeper where higher
    ridges = ridged_mf(x + wx, y + wy, 6.0, max(lmin, 0.03), seed=37)
    hills = (90.0 * ridges + 30.0 * fbm(x, y, 10.0, max(lmin, 0.5), lambda l: 0.5, seed=38)) * tb
    crease = fbm(x + 0.5 * wy, y + 0.5 * wx, 3.5, max(lmin, 0.25), lambda l: (l / 3.5) ** 0.7, seed=39, ridged=True) / 1.5
    gul = smoothstep(0.45, 0.95, crease) * tb
    hills -= 0.45 * hills * gul
    rough = fbm(x, y, 0.4, lmin, lambda l: 4.0 * l ** 0.8, seed=40) * (0.4 + 0.6 * tb)
    h = gti + dunes + hills * (1 - 0.7 * plain) + rough

    # albedo: dune sand dark, interdunes brighter (gravel, ice), highland bright, gully fill dark
    gy, gx = np.gradient(h - rough, px * 1000.0)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    expo = np.clip(smoothstep(8.0, 24.0, slope) * tb + 0.5 * smoothstep(0.4, 0.8, ridges) * tb, 0, 1)
    dune_cover = prof * cut * (1 - tb) * (1 - plain)
    a = a_glob * (1 - 0.30 * dune_cover + 0.20 * (1 - prof) * (1 - tb) * (1 - plain) + 0.45 * expo - 0.35 * gul)
    cob = 0.11 + 0.025 * fbm(x, y, 0.2, lmin, lambda l: 1.0, seed=41)
    a += (cob - a) * 0.7 * plain                              # the Huygens cobble plain
    a *= 1 + fbm(x, y, 1.0, lmin, lambda l: 0.06 * (l / 1.0) ** 0.25, seed=42)

    f = 1 - smoothstep(s.FADE0, s.FADE1, r)
    h = gti + f * (h - gti)
    a = a_glob + f * (a - a_glob)
    sd = np.full(x.shape, 999.0)
    if not extra:
        return h, a, sd
    w = np.clip(np.maximum.reduce([0.45 * plain, 0.6 * gul, 0.25 * (1 - prof) * (1 - tb)]) * (1 - 0.8 * expo), 0, 1) * f
    cls = np.zeros(x.shape, np.uint8)                                    # 0 sand
    cls[(plain > 0.5) | ((gul > 0.5) & (tb > 0.5)) | ((prof < 0.05) & (tb < 0.5) & (f > 0.5))] = 1   # cobbles
    cls[expo > 0.6] = 2                                                  # ice bedrock
    return h, a, sd, {"wet": w, "cls": cls}


s.fields = fields


if __name__ == "__main__":
    root, src = sys.argv[1], sys.argv[2]
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    prepare(src)
    s._M.clear()

    def run():
        cache = CACHE
        jobs = []
        for n, half in s.LEVELS.items():
            jobs += [(root, src, cache, tt) for tt in s.tiles_for(n, half)]
        print(f"{len(jobs)} tiles, levels {min(s.LEVELS)}-{max(s.LEVELS)}", flush=True)
        from multiprocessing import Pool
        import time
        t0 = time.time()
        with Pool(workers) as pool:
            for k, (name, lo, hi, wet) in enumerate(pool.imap_unordered(s.make_tile, jobs, chunksize=4)):
                if k % 50 == 0:
                    print(f"{k}/{len(jobs)} {name} elev {lo:.0f}..{hi:.0f} m ({time.time() - t0:.0f} s)", flush=True)
        s.load(cache, src)
        out = os.path.join(root, "Textures", "Titan", "Elev")
        for n in range(5, 8):     # coarse ancestors (8-9 are site levels; level 4 spans both sites, keep Kraken's)
            for (_, i, j) in s.tiles_for(n, s.HALF):
                lon0, lon1, lat0, lat1 = g.tile_bounds(n, i, j)
                kk = np.arange(259) - 1
                LON, LAT = np.meshgrid(lon0 + kk / 256 * (lon1 - lon0), lat0 + kk / 256 * (lat1 - lat0))
                he, _, _ = fields(((LON + 180) % 360) - 180, np.clip(LAT, -89.99, 89.99), s.R * np.radians(lat1 - lat0) / 256)
                s.write_elv(os.path.join(out, f"{n:02d}", f"{i:06d}", f"{j:06d}.elv"), he, lon0, lon1, lat0, lat1)
        print("done", flush=True)
    run()
