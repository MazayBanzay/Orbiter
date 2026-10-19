"""Elevation-only rebuild into a staging folder, then atomic publish into the game:
global Elev L4-8 with sea beds, site Elev L10-18, site coarse Elev L4-9 (0.1 m steps)."""
import os, sys, time
os.environ["TITAN_ELEV_ONLY"] = "1"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from multiprocessing import Pool
import gen_titan_site as s, gen_titan_global as gg, build_isolated as bi
g = s.g


def global_elev(stage, src):
    gti = g.load_gti(src)
    gg.flatten_seas(gti, g.load_lakes(src)[0])
    out = os.path.join(stage, "Textures", "Titan", "Elev")
    for n in range(4, g.MAXLEVEL_ELEV + 1):
        nlat, nlng = 2 ** (n - 4), 2 ** (n - 3)
        for ilat in range(nlat):
            for ilng in range(nlng):
                lon0, lon1, lat0, lat1 = g.tile_bounds(n, ilat, ilng)
                k = np.arange(259) - 1
                LON, LAT = np.meshgrid(lon0 + k / 256 * (lon1 - lon0), lat0 + k / 256 * (lat1 - lat0))
                e = g.sample(gti, ((LON + 180) % 360) - 180, np.clip(LAT, -89.99, 89.99), order=1)
                gg.write_elv(os.path.join(out, f"{n:02d}", f"{ilat:06d}", f"{ilng:06d}.elv"), e, lon0, lon1, lat0, lat1)
    print("global elevation done", flush=True)


if __name__ == "__main__":
    root, src, stage, log = sys.argv[1:5]
    sys.stdout = sys.stderr = open(log, "a", encoding="utf8", buffering=1)
    t0 = time.time()
    # global elevation is left as it is (sea beds for all seas were not agreed)
    cache = os.path.join(s.HERE, "_titan_site_master_v3.npz")
    jobs = [(stage, src, cache, t) for n, half in s.LEVELS.items() for t in s.tiles_for(n, half)]
    print(f"{len(jobs)} site elevation tiles", flush=True)
    with Pool(10) as pool:
        for k, r in enumerate(pool.imap_unordered(s.make_tile, jobs, chunksize=4)):
            if k % 100 == 0:
                print(f"{k}/{len(jobs)} ({time.time() - t0:.0f} s)", flush=True)
    s.coarse_elev(stage, src)
    moved, failed = bi.publish(stage, root)
    print(f"published {moved} files, failed {len(failed)} ({time.time() - t0:.0f} s)", flush=True)
    print("all done", flush=True)
