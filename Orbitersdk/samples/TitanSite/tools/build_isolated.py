"""Run the Titan site tile build without disturbing a running Orbiter.

* Tiles are written into a staging folder, never into the game's Textures\\Titan while they are
  being made; when everything is ready each file is moved in with an atomic replace (temp file +
  os.replace), so the game can never read a half-written tile.
* All processes run at IDLE priority on a few workers: the game always gets the CPU first.
* Resumable: a tile whose three files (Surf, Mask, Elev) are already in the game folder, complete
  (exact file sizes) and newer than --since, is skipped.

Usage: build_isolated.py <orbiter root> <Titan_src> <stage dir> <since: unix time> [workers]
"""
import ctypes
import os
import shutil
import sys
import time
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_titan_site as s  # noqa: E402

SIZES = {"Surf": 131200, "Mask": 262272, "Elev": 100 + 259 * 259 * 2}   # DXT1 512, DXT5 512, ELE 259x259
EXT = {"Surf": ".dds", "Mask": ".dds", "Elev": ".elv"}


def idle():
    k32 = ctypes.windll.kernel32
    k32.GetCurrentProcess.restype = ctypes.c_void_p              # pseudo-handle -1: must not be truncated
    k32.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    k32.SetPriorityClass(k32.GetCurrentProcess(), 0x40)          # IDLE_PRIORITY_CLASS


def done(root, n, i, j, since):
    for layer, size in SIZES.items():
        p = os.path.join(root, "Textures", "Titan", layer, f"{n:02d}", f"{i:06d}", f"{j:06d}{EXT[layer]}")
        if not os.path.exists(p) or os.path.getsize(p) != size or os.path.getmtime(p) < since:
            return False
    return True


def publish(stage, root):
    src_root = os.path.join(stage, "Textures", "Titan")
    moved, failed = 0, []
    for d, _, files in os.walk(src_root):
        for f in files:
            src = os.path.join(d, f)
            dst = os.path.join(root, "Textures", "Titan", os.path.relpath(src, src_root))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            tmp = dst + ".tmp"
            shutil.copyfile(src, tmp)
            for _ in range(50):                      # the game may hold the file open for a moment
                try:
                    os.replace(tmp, dst)
                    moved += 1
                    break
                except PermissionError:
                    time.sleep(0.2)
            else:
                failed.append(dst)
    return moved, failed


def main(root, src, stage, since, workers):
    idle()
    cache = os.path.join(s.HERE, "_titan_site_master_v3.npz")
    s.prepare(src, cache)
    jobs = []
    for n, half in s.LEVELS.items():
        for (_, i, j) in s.tiles_for(n, half):
            if not (done(root, n, i, j, since) or done(stage, n, i, j, since)):
                jobs.append((stage, src, cache, (n, i, j)))
    print(f"{len(jobs)} tiles left to build into {stage} (idle priority, {workers} workers)", flush=True)
    t0 = time.time()
    with Pool(workers, initializer=idle) as pool:
        for k, (name, lo, hi, wet) in enumerate(pool.imap_unordered(s.make_tile, jobs, chunksize=2)):
            if k % 25 == 0:
                print(f"{k}/{len(jobs)} {name} ({time.time() - t0:.0f} s)", flush=True)
    s.coarse_elev(stage, src)
    moved, failed = publish(stage, root)
    print(f"published {moved} files into the game folder, failed {len(failed)}", flush=True)
    for f in failed:
        print("FAILED", f, flush=True)
    print("all done", flush=True)


if __name__ == "__main__":
    if len(sys.argv) > 6:                                   # own log file: the runner has no console
        sys.stdout = sys.stderr = open(sys.argv[6], "a", encoding="utf8", buffering=1)
    main(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), int(sys.argv[5]) if len(sys.argv) > 5 else 4)
