"""Titan micro texture level C without dunes: Textures/D3D9Titan_C.dds (1.25 m/px, 2.56 km tile).

The Tantra version (Orbitersdk/samples/Tantra/tools/gen_microtex_titan.py, level_c) draws two
east-west linear dunes per tile. A micro texture is global, so those crests repeat every 1.3 km
over the whole moon, including the dissected ice uplands of the polar sea shores where there are
no dunes - they read as identical rows. This level is isotropic instead: rolling eroded ground,
sharper bedrock ribs and a fine rill network, all tileable (spectral synthesis), no direction.
Levels A and B are kept from the Tantra script.

Usage: gen_microtex_titan_c.py <orbiter root>
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Tantra", "tools"))
import gen_microtex_titan as m  # noqa: E402


def level_c():
    rolling = m.spectral(3.6, kmin=1, seed=41)                      # broad swells
    ribs = (1 - np.abs(m.spectral(3.0, kmin=3, seed=42))) ** 3      # ridged: bedrock ribs
    rills = np.exp(-(m.spectral(2.9, kmin=4, seed=43) / 0.05) ** 2) # zero lines -> rills
    fine = m.spectral(3.0, kmin=24, seed=44)
    h = 22.0 * rolling + 14.0 * ribs - 3.0 * rills + 0.4 * fine
    z = (ribs - ribs.mean()) / ribs.std()
    lum = 3.5 * np.tanh(0.8 * z) - 1.5 * rills + 2 * m.spectral(2.8, kmin=2, seed=45)   # no dark specks
    return m.encode(h, lum, 0.6)


if __name__ == "__main__":
    out = os.path.join(sys.argv[1], "Textures", "D3D9Titan_C.dds")
    m.save_dds_dxt5_mips(level_c(), out)
    print("written", out)
