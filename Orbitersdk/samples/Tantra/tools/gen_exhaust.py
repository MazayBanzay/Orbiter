"""Generate the anamezon exhaust textures (Textures/Tantra_exh_*.dds).

Layout follows Orbiter's stock Exhaust.dds, which D3D9Client maps onto two quads:
  * side view: left quarter of the texture (u 0..0.25), nozzle at the top (v = 0),
    the jet running down to v = 1;
  * end view (seen from behind): a round glow in the top-right quadrant
    (centre u = 0.75, v = 0.25).
D3D9Client blends exhausts with alpha (not additive): colour must NOT be premultiplied
by alpha, or the faint fringes darken the sky behind them instead of glowing.
"""
import os
import sys

import numpy as np

W = H = 256


def texture(core_rgb, edge_rgb, width, decay, cap_radius, gain=1.0, tail=0.0, cap_gain=1.0, cone=0.0, diamonds=0):
    rgb = np.zeros((H, W, 3))
    alpha = np.zeros((H, W))
    y, x = np.mgrid[0:H, 0:W]
    u, v = x / W, y / H

    # Side view: a thin beam along v inside u 0..0.25.
    side = u < 0.25
    # cone > 0: the width grows from `width` at the nozzle to `cone` at the far end
    # (a spreading jet), dimming as the same flux spreads over more area.
    sigma = width + (cone - width) * v if cone > 0 else width
    du = (u - 0.125) / sigma
    across = np.exp(-du ** 2) * (np.sqrt(width / sigma) if cone > 0 else 1.0)
    core = np.exp(-(du * 3) ** 2)
    along = np.exp(-v / decay) * (1 - tail) + tail
    along *= np.clip(v / 0.02, 0, 1) * np.clip((1 - v) / 0.1, 0, 1)   # soft ends
    if diamonds:
        # Shock diamonds of an under-expanded jet: bright Mach disks at even spacing,
        # the core pinched between them, fading downstream.
        phase = (v * diamonds) % 1.0
        disk = np.exp(-((phase - 0.85) / 0.06) ** 2)
        along *= 0.7 + 1.2 * disk * np.exp(-v * 1.5)
    a = across * along * gain
    for i in range(3):
        rgb[:, :, i] = np.where(side, core_rgb[i] * core + edge_rgb[i] * (1 - core), rgb[:, :, i])
    alpha = np.where(side, a, alpha)

    # End view: round glow centred at (0.75, 0.25).
    r = np.sqrt((u - 0.75) ** 2 + (v - 0.25) ** 2) / cap_radius
    cap = (u >= 0.5) & (v < 0.5)
    # cap_gain 0: no end-on disc (long thin glows would show it as a halo at the stern)
    glow = np.exp(-(r * 2.2) ** 2) * np.clip(1 - r, 0, 1) * gain * cap_gain
    gcore = np.exp(-(r * 6) ** 2)
    for i in range(3):
        rgb[:, :, i] = np.where(cap, core_rgb[i] * gcore + edge_rgb[i] * (1 - gcore), rgb[:, :, i])
    alpha = np.where(cap, glow, alpha)
    return np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)


def write_dds(path, rgb, alpha):
    h, w, _ = rgb.shape
    header = bytearray(128)
    header[0:4] = b"DDS "
    vals = {4: 124, 8: 0x100F, 12: h, 16: w, 20: w * 4, 76: 32, 80: 0x41, 88: 32,
            92: 0x00FF0000, 96: 0x0000FF00, 100: 0x000000FF, 104: 0xFF000000, 108: 0x1000}
    for off, val in vals.items():
        header[off:off + 4] = int(val).to_bytes(4, "little")
    bgra = np.empty((h, w, 4), np.uint8)
    bgra[:, :, 0] = (rgb[:, :, 2] * 255).astype(np.uint8)
    bgra[:, :, 1] = (rgb[:, :, 1] * 255).astype(np.uint8)
    bgra[:, :, 2] = (rgb[:, :, 0] * 255).astype(np.uint8)
    bgra[:, :, 3] = (alpha * 255).astype(np.uint8)
    with open(path, "wb") as f:
        f.write(header)
        f.write(bgra.tobytes())


TEXTURES = {
    # bright anamezon beam: white core, violet sheath, fading over its length
    "Tantra_exh_violet": texture((1.0, 0.97, 1.0), (0.70, 0.45, 1.0), 0.030, 0.45, 0.22, 1.0),
    # K-particle guide beam (kept for the pult; not drawn outside during start-up)
    "Tantra_exh_grey": texture((0.88, 0.9, 0.95), (0.55, 0.57, 0.63), 0.020, 0.6, 0.15, 0.7),
    # packet afterglow (vacuum) / air channel: nearly uniform along the length
    "Tantra_exh_glow": texture((0.80, 0.66, 1.0), (0.45, 0.30, 0.85), 0.045, 0.33, 0.25, 0.7, cap_gain=0.0),
    # planetary (ion-trigger) jet in vacuum: 300 km/s plasma of a light working body,
    # recombining as it expands in a cone - white core, Balmer pink-magenta fringe
    "Tantra_plan_vac": texture((1.0, 0.92, 1.0), (0.9, 0.38, 0.78), 0.03, 0.45, 0.1, 0.8, cone=0.08, cap_gain=0.25),
    # planetary jet in air: under-expanded, five Mach disks; hot air orange at the fringe
    "Tantra_plan_air": texture((1.0, 0.97, 0.98), (1.0, 0.6, 0.5), 0.035, 0.8, 0.2, 1.0, diamonds=5),
    # guide beam in air: thin N2 thread fading as the beam is absorbed (billboard = 3 e-folds)
    "Tantra_exh_thread": texture((0.85, 0.85, 1.0), (0.45, 0.45, 0.95), 0.018, 0.33, 0.1, 0.9, cap_gain=0.0),
}

if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")
    out = os.path.join(root, "Textures")
    for name, (rgb, alpha) in TEXTURES.items():
        write_dds(os.path.join(out, name + ".dds"), rgb, alpha)
        print("wrote", name + ".dds")
    if "--preview" in sys.argv:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        rgb, alpha = TEXTURES["Tantra_exh_violet"]
        plt.imsave(sys.argv[sys.argv.index("--preview") + 1], np.clip(rgb * alpha[:, :, None] * 2, 0, 1))
