"""Tantra ship mesh pipeline: gen_mesh.py (form + rig) -> Blender refinement -> verification.

    python tools/build_mesh.py [--preview out.png] [--no-blender]

Writes Meshes/Tantra/Tantra.msh and orbiter2016/MeshLayout.h. The Blender pass only adds local
detail; verify_mesh() fails the build if a group is missing, reordered or has moved.
Blender is started through the Microsoft Store launcher, which returns immediately: the
refinement script reports through a marker file.
"""
import json
import math
import os
import subprocess
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
BUILD = os.path.join(HERE, "..", "build", "mesh")
MSH = os.path.join(ROOT, "Meshes", "Tantra", "Tantra.msh")
LAUNCHER = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WindowsApps", "blender-launcher.exe")

sys.path.insert(0, HERE)
import gen_mesh as gm  # noqa: E402


def run_blender(raw, out, done, timeout=600):
    if os.path.exists(done):
        os.remove(done)
    subprocess.run([LAUNCHER, "--background", "--factory-startup", "--python", os.path.join(HERE, "blender_refine.py"),
                    "--", raw, out, done], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    while not os.path.exists(done):
        if time.time() - t0 > timeout:
            raise RuntimeError("Blender did not finish")
        time.sleep(1.0)
    time.sleep(0.5)
    with open(done) as f:
        msg = f.read()
    if not msg.startswith("OK"):
        raise RuntimeError("Blender refinement failed:\n" + msg)
    return msg.strip()


def read_msh(path):
    groups = []
    with open(path) as f:
        lines = f.read().splitlines()
    i = 0
    while i < len(lines):
        if lines[i].startswith("LABEL "):
            name = lines[i][6:].strip()
            mat = int(lines[i + 1].split()[1])
            nv, nt = map(int, lines[i + 3].split(";")[0].split()[1:3])
            v = np.array([list(map(float, lines[i + 4 + k].split()[:3])) for k in range(nv)])
            t = [tuple(map(int, lines[i + 4 + nv + k].split())) for k in range(nt)]
            g = gm.Group(name, mat)
            g.v, g.t = list(v), t
            groups.append(g)
            i += 4 + nv + nt
        else:
            i += 1
    return groups


def verify(raw_groups, groups, tol=0.8):
    """Same names and order; every group's bounding box within `tol` m of the raw one."""
    problems = []
    if [g.name for g in groups] != [g.name for g in raw_groups]:
        problems.append("group names/order differ from the module contract")
    for a, b in zip(raw_groups, groups):
        A, B = np.array(a.v), np.array(b.v)
        d = max(np.abs(A.min(0) - B.min(0)).max(), np.abs(A.max(0) - B.max(0)).max())
        if d > tol:
            problems.append(f"{a.name}: bounding box moved by {d:.2f} m")
    return problems


def main():
    raw_groups, legs = gm.build()
    comps = gm.rig(legs)
    os.makedirs(BUILD, exist_ok=True)
    raw = os.path.join(BUILD, "tantra_raw.json")
    gm.write_json(raw_groups, raw)
    gm.write_debris(raw_groups, ROOT)                 # debris meshes + vessel configs (raw geometry is enough)
    gm.write_layout(legs, comps, os.path.join(HERE, "..", "orbiter2016", "MeshLayout.h"))
    os.makedirs(os.path.dirname(MSH), exist_ok=True)
    trap = gm.Group("trap", gm.MAT["trap_shell"])     # stand-alone container vessel mesh (TantraTrap)
    gm.trap_geom(trap, 0.0, 0.0, -12.4 - gm.STERN_Z, 12.4 - gm.STERN_Z)
    gm.write_msh([trap], os.path.join(os.path.dirname(MSH), "TantraTrap.msh"))
    if "--no-blender" in sys.argv:
        gm.write_msh(raw_groups, MSH)
        print("raw mesh written (no Blender pass)")
        return
    tmp = os.path.join(BUILD, "Tantra_refined.msh")
    print(run_blender(raw, tmp, os.path.join(BUILD, "blender_done.txt")))
    groups = read_msh(tmp)
    problems = verify(raw_groups, groups)
    if problems:
        print("VERIFY FAILED:\n  " + "\n  ".join(problems))
        sys.exit(1)
    os.replace(tmp, MSH)
    print(f"verified, installed {MSH}")
    if "--preview" in sys.argv:
        poses = gm.preview_poses(legs)
        gm.preview(groups, comps, sys.argv[sys.argv.index("--preview") + 1], poses)


if __name__ == "__main__":
    main()
