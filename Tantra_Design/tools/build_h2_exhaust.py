# The hydrogen exhaust textures for the jet pack's capsules (the user, 2026-10-18: «выхлоп - пламя; это водород»), laid out
# as Orbiter's own Exhaust.dds: the flame in the left strip (u 0..0.24, v 0 at the nozzle -> 1 at the tail), the exit's
# glow spot round (0.74, 0.25). The jet is metallic hydrogen relaxed to ~4000 K, 10 km/s:
#   h2vac.dds  - vacuum: nearly invisible - a short faint violet-blue core just past the exit, fading in the first third
#   h2air.dds  - air: the hydrogen burning on with the oxygen - a pale blue flame with the shock diamonds of the over-
#                expanded nozzle (four bright knots, fading down the jet)
#   h2inert.dds - dense air without oxygen (Titan, Venus): the core and faint warm knots, no flame
#   h2fors.dds - air on the «форсаж»: the colder, denser jet - shorter knots closer together, a little warmer in tint
# Run: python build_h2_exhaust.py  -> Textures\Tantra\h2vac.dds, h2air.dds, h2fors.dds
import os, math
import numpy as np
from PIL import Image
OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Textures", "Tantra"))
N = 1024   # (2026-10-18, the user: «факелы очень грубо сделаны»: 256 px DXT5 put the jet in a 61 px strip of 4x4 blocks with
           #  5:6:5 colour - stepped columns. Now 1024 px, uncompressed A8R8G8B8 with a mip chain, dithered)
u = (np.arange(N) + 0.5) / N; v = (np.arange(N) + 0.5) / N
U, V = np.meshgrid(u, v)

# Orbiter draws the strip wsize wide (the module's Wd) from the anchor at the nozzle's exit plane; the jet must leave the
# whole lip (0.081 m across): its half-width at v=0 is R_LIP / (Wd/2) in the strip's units, then it grows with the spread
R_LIP = 0.0405
def smooth(e0, e1, t):
    t = np.clip((t - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def plume(wd, length, half_angle, fall, col, alpha, knots=0, knot_gap=0.15, knot_first=0.10, knot_amp=0.0, core_amp=0.0, col_end=None, core_len=0.10):
    """the jet in u 0..0.24: from the lip's width at the exit, spreading at half_angle over its length; gaussian (soft)
    across, an exponential fall along, a smooth tail to nothing; optional shock knots and a bright core near the exit"""
    x = ((U - 0.01) / 0.23 - 0.5) * 2                                         # -1..1 across the strip (D3D9Client maps u 0.01..0.24)
    hw = (R_LIP + V * length * math.tan(math.radians(half_angle))) / wd        # the jet's half-width, strip units (wd = wsize: the client
                                                                              # draws the strip +-wsize wide - D3D9Effect::RenderExhaust)
    across = np.exp(-(x / (0.75 * hw)) ** 2)
    along = np.exp(-V / fall) * smooth(0.0, 0.012 + 0.03 * np.clip(np.abs(x) / hw, 0, 1.5) ** 2, V) * (1 - smooth(0.45, 1.0, V))   # a rounded start at the lip
    k = np.zeros_like(V)
    for i in range(knots):
        c = knot_first + i * knot_gap
        # a shock diamond: a lens along the axis (the user: «некачественно» - the old flat gaussians read as rungs across
        # the strip); its length is ~0.8 of the cell gap, its width ~0.45 of the jet's
        dv = np.abs(V - c) / (0.40 * knot_gap); dx = np.abs(x) / (0.45 * hw)
        k += knot_amp * (0.78 ** i) * np.exp(-3.0 * (dv + dx) ** 2)
    core = core_amp * np.exp(-(x / (0.35 * hw)) ** 2) * np.exp(-V / core_len)
    win = np.clip(1 - x * x, 0, 1) ** 2
    dens = along * across + k * (1 - smooth(0.45, 1.0, V)) + core
    a = (1 - np.exp(-dens * alpha * 1.6)) * win * (U >= 0.01) * (U < 0.24)                   # soft compression, no flat saturated top
    rgb = np.array(col)[None, None, :] * np.ones_like(V)[..., None]
    if col_end is not None:
        t = np.clip(V / 0.6, 0, 1)[..., None]; rgb = rgb * (1 - t) + np.array(col_end) * t
    rgb = np.clip(rgb * (0.8 + 0.2 * across[..., None]) + (k + core)[..., None] * 0.5, 0, 1)
    return rgb, a

def spot(col, r, alpha):
    d = np.hypot(U - 0.74, V - 0.25) / r
    a = np.clip(np.exp(-d ** 2) * alpha, 0, 1)
    return np.stack([np.full_like(V, c) for c in col], -1), a

def write_dds(path, img):
    """A8R8G8B8 DDS with the full mip chain; the levels averaged by alpha (straight colour keeps no dark fringes)"""
    import struct
    levels = [img]
    while levels[-1].shape[0] > 1:
        m = levels[-1]; h, w = m.shape[0] // 2, m.shape[1] // 2
        q = m.reshape(h, 2, w, 2, 4); a = q[..., 3]; aw = a.sum((1, 3))
        rgb = (q[..., :3] * a[..., None]).sum((1, 3)) / np.maximum(aw, 1e-6)[..., None]
        rgb = np.where(aw[..., None] > 1e-6, rgb, q[..., :3].mean((1, 3)))
        levels.append(np.concatenate([rgb, (aw / 4)[..., None]], -1))
    n0 = img.shape[0]
    hdr = struct.pack("<4s7I44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8, n0, n0, n0 * 4, 0, len(levels))
    pf = struct.pack("<2I4s5I", 32, 0x41, b"\0\0\0\0", 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack("<5I", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    rng = np.random.default_rng(5)
    with open(path, "wb") as f:
        f.write(hdr + pf + caps)
        for m in levels:
            q = np.clip(m * 255 + rng.uniform(-0.5, 0.5, m.shape), 0, 255).astype(np.uint8)   # dithered: no 8-bit steps
            f.write(q[..., [2, 1, 0, 3]].tobytes())

def save(name, layers):
    rgb = np.zeros((N, N, 3)); a = np.zeros((N, N)); wsum = np.full((N, N), 1e-6)
    for c, al in layers:                                  # straight (not premultiplied) colour: the blend multiplies by alpha itself
        rgb += c * al[..., None]; wsum += al; a = 1 - (1 - a) * (1 - al)
    rgb = np.where(wsum[..., None] > 1e-3, rgb / wsum[..., None], layers[0][0])
    img = np.concatenate([rgb, a[..., None]], -1)
    p = os.path.join(OUT, name + ".dds"); write_dds(p, img)
    Image.fromarray((img * 255).astype(np.uint8)).resize((256, 256), Image.LANCZOS).save(os.path.join(os.path.dirname(os.path.abspath(__file__)), name + "_preview.png"))
    print(p)

os.makedirs(OUT, exist_ok=True)
# no glow spot: the exit is drawn by the glowing nozzle itself (a round sprite there showed as a disc off the nozzle)
# vacuum (2026-10-18, the user: «в космосе он явно будет виден и длиннее»): the jet leaves at ~2000-4000 K partly dissociated;
# the atoms recombining and the hot gas give a pink-violet light (H-alpha with H-beta) at the exit, bluer and fainter as it
# cools; nothing confines it - underexpanded, it spreads and runs long (the module draws it 1.4 m)
# (the module's sizes, L / wsize (the strip's half-width): vacuum 1.40/0.30, air 0.80/0.08, «форсаж» 1.20/0.13, inert 0.35/0.07)
save("h2vac", [plume(0.30, 1.40, 8.0, 0.40, (1.00, 0.55, 0.95), 0.75, core_amp=0.6, col_end=(0.55, 0.62, 1.00))])
# (air and «форсаж»: a continuous pale core the whole way, the diamonds on it; the «форсаж» jet - four times the flow at
#  half the speed - is longer (1.2 m), wider, its cells longer, its tint a little warmer)
save("h2air", [plume(0.08, 0.80, 3.0, 0.45, (0.80, 0.90, 1.00), 0.60, knots=5, knot_gap=0.13, knot_first=0.09, knot_amp=0.45, core_amp=0.45, core_len=0.45)])
save("h2fors", [plume(0.13, 1.20, 4.0, 0.45, (0.92, 0.93, 1.00), 0.70, knots=6, knot_gap=0.11, knot_first=0.07, knot_amp=0.55, core_amp=0.55, core_len=0.50)])
# h2inert - a dense atmosphere without oxygen (Titan, Venus): nothing to burn - the faint violet core, and the shock
# diamonds of the overexpanded jet lit only by the gas's own heating: warm-white knots, close together, no blue flame
# (2026-10-18, Titan: 1.47 bar of N2 at 94 K, 5.4 kg/m3 - the 10 km/s jet is stopped and mixed within ~0.3 m: a short
#  bright core at the exit, tight diamonds, cooling fast from warm white to a dull violet; no condensation trail - the jet
#  is pure H2, which does not condense at 94 K, and it only warms the methane-nitrogen air)
save("h2inert", [plume(0.07, 0.35, 3.0, 0.16, (1.00, 0.90, 0.95), 0.60, knots=6, knot_gap=0.085, knot_first=0.05, knot_amp=0.50, core_amp=0.65, core_len=0.14, col_end=(0.55, 0.45, 0.75))])

# ---- the end-on view (the user, 2026-10-18: «плоские потоки»): the strip is a ribbon turned to the camera about the jet's
# axis - seen along the axis it is a line. The client's second quad (the "flare", +-7 wsize, always facing the camera, the
# texture's square u 0.504-0.996, v 0.004-0.496) carries the jet seen end-on: the cone's light summed along the line of
# sight - a bright centre falling off to the cone's far radius. These are separate textures (the strip left empty); the
# module fades the strip and raises these as the camera comes round to the axis.
#   module wsize: vacuum 0.045, air 0.012, «форсаж» 0.02, inert 0.012 (the flare's half-size 7 x that)
def endglow(c_in, c_out, r0, amp, ring=0.0):
    rn = np.hypot(U - 0.75, V - 0.25) / 0.246                                  # 0 centre .. 1 the quad's edge
    I = np.exp(-rn / r0) + ring * np.exp(-((rn - 0.35) / 0.12) ** 2)
    a = np.clip(amp * I, 0, 1) * np.clip(1 - rn, 0, 1) ** 1.5 * (U > 0.5) * (V < 0.5)
    t = np.clip(rn / 0.6, 0, 1)[..., None]
    return np.array(c_in) * (1 - t) + np.array(c_out) * t, a
save("h2vac_e", [endglow((1.00, 0.62, 0.98), (0.55, 0.62, 1.00), 0.22, 0.9)])
save("h2air_e", [endglow((0.92, 0.96, 1.00), (0.75, 0.85, 1.00), 0.18, 0.8)])
save("h2fors_e", [endglow((0.97, 0.96, 1.00), (0.80, 0.86, 1.00), 0.22, 0.9)])
save("h2inert_e", [endglow((0.98, 0.88, 0.98), (0.80, 0.75, 0.95), 0.18, 0.6)])
