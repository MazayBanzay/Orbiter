"""Export the D3D9Client fork (AtmoFX: Jupiter/Io/Titan volumes, liquids, heat shimmer, particles, HUD)
from the working clone _src/orbiter into this folder, so the fork lives in the main repository:

  D3D9Client.patch   changes to stock files, against orbitersim/orbiter tag 2024 (git diff)
  src/               the fork's own files (new .cpp/.h and shaders), copied whole
  build/             build_client.bat, install_client.bat, the D3DX/xnamath shim, the effect test loader

Rebuild the fork: clone https://github.com/orbitersim/orbiter (tag 2024; OVP/D3D9Client, Orbitersdk/include,
cmake are enough) into _src/orbiter, `git apply D3D9Client.patch`, copy src/* over OVP/D3D9Client, copy build/*
to _src/build_d3d9, get Microsoft.DXSDK.D3DX 9.29.952.8 from nuget.org into _src/deps/d3dx, run build_client.bat.

Usage: python export_fork.py   (from anywhere; paths are relative to the Orbiter root)
"""
import os
import shutil
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
CLONE = os.path.join(ROOT, "_src", "orbiter")
BUILD = os.path.join(ROOT, "_src", "build_d3d9")


def git(*args):
    return subprocess.run(["git", "-C", CLONE, *args], capture_output=True, check=True).stdout


def main():
    patch = git("diff", "--binary", "2024", "--", "OVP/D3D9Client")
    with open(os.path.join(HERE, "D3D9Client.patch"), "wb") as f:
        f.write(patch)
    new = [p for p in git("ls-files", "--others", "--exclude-standard", "OVP/D3D9Client").decode().splitlines() if p]
    for p in new:
        dst = os.path.join(HERE, "src", os.path.relpath(p, "OVP/D3D9Client"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(CLONE, p), dst)
    for rel in ("build_client.bat", "install_client.bat", "shim/xnamath.h", "shim/D3D9ClientConfig.h",
                "fxtest/build.bat", "fxtest/fxtest.cpp"):
        src = os.path.join(BUILD, rel)
        if os.path.exists(src):
            dst = os.path.join(HERE, "build", rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
    print(f"patch {len(patch)} bytes, {len(new)} own files")


if __name__ == "__main__":
    main()
