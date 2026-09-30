"""Tantra ship mesh pipeline: gen_mesh.py (form + rig) -> Blender refinement -> verification.

    python tools/build_mesh.py [--preview out.png] [--no-blender]

Writes Meshes/Tantra/Tantra.msh and orbiter2010/MeshLayout.h. The Blender pass only adds local
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
    gm.write_layout(legs, comps, os.path.join(HERE, "..", "orbiter2010", "MeshLayout.h"))
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
        mid = (38.0 - gm.CAR_S0) / (gm.CAR_S1 - gm.CAR_S0)
        k = 1 - 0.3 / gm.MAST_LMAX
        rest = {"lid_port": 1, "lid_starboard": 1, "track_port": mid, "track_starboard": mid}
        for s in ("port", "starboard"):
            rest[f"mast_len_{s}"] = (gm.MAST_LMAX - 18.8) / (gm.MAST_LMAX - gm.MAST_SEG)
        for i, g in enumerate(legs):
            if g["lower"]:
                rest.update({f"leg{i}_swing": g["phi_rest"] / g["phi_stand"], f"leg{i}_ext": g["e_rest"] / 10,
                             f"leg{i}_foot_rest": 1})
        turn = {"lid_port": 1, "lid_starboard": 1, "track_port": mid, "track_starboard": mid, "crest_lateral": 1,
                "crest_dorsal": 1, "pod_retract": 1, "pitch_port": 0.5, "pitch_starboard": 0.5}
        for s in ("port", "starboard"):
            turn[f"mast_len_{s}"] = (gm.MAST_LMAX - 52.8) / (gm.MAST_LMAX - gm.MAST_SEG)
        stand = {"slide_port": 1, "slide_starboard": 1, "mast_len_port": 1, "mast_len_starboard": 1,
                 "track_port": mid, "track_starboard": mid, "crest_lateral": 1, "crest_dorsal": 1, "pod_retract": 1}
        for i, g in enumerate(legs):
            stand.update({f"leg{i}_swing": 1, f"leg{i}_ext": g["e_stand"] / 10, f"leg{i}_foot_stand": 1})
        poses = [("resting level", rest, 0, 0, (10, -120)), ("turning 45 deg", turn, 45, 34, (8, -80)),
                 ("standing", stand, 90, 30, (8, -60))]
        gm.preview(groups, comps, sys.argv[sys.argv.index("--preview") + 1], poses)


if __name__ == "__main__":
    main()
