"""Sounds of the ship breaking (synthesised, no samples) - the same physics as tools/gen_lift_sounds.py: a 52 kt structure
thuds, it does not ring; what breaks tears and screeches, what falls off rattles down.

  crash_ext / crash_int  - the hull hitting the ground: a deep blow, the ground giving, steel tearing and screeching,
                           a long rattle of torn pieces falling and settling
  leg_break_ext / _int   - a leg's joint failing: a sharp crack of the joint, the structure groaning, the leg falling
                           and hitting the ground a moment later
  petal_snap_ext / _int  - a petal's strut or hinge failing: a crack, a short metal clang, the petal scraping

  python tools/gen_crash_sounds.py           -> XRSound/Tantra/crash_*.wav, leg_break_*.wav, petal_snap_*.wav
"""
import numpy as np

import gen_lift_sounds as L
from gen_lift_sounds import SR, bp, lp, hp, heavy_hit, burst, reverb, echo, air, save, t_axis, INT_HULL

rng = np.random.default_rng(2026)


def screech(t, t0, dur, f0, f1, gain):
    """Steel tearing and scraping - no tone at all (a gliding tone is a laser, not metal): dense crackle of the fibres
    parting (thousands of tiny clicks), the grinding band of plate over plate falling slowly in pitch, all of it
    stuttering with stick-slip."""
    tt = np.clip(t - t0, 0, None)
    on = (t >= t0) & (t <= t0 + dur)
    env = np.sin(np.pi * np.clip(tt / dur, 0, 1)) ** 0.6 * on
    clicks = (rng.random(len(t)) < 900.0 / SR) * rng.standard_normal(len(t))           # the fibres: sparse impulses
    crackle = bp(clicks, 600, 5000) * 3.0
    fc = f0 * (f1 / f0) ** np.clip(tt / dur, 0, 1)
    grind = np.zeros_like(t)
    n = rng.standard_normal(len(t))
    for lo_f, hi_f in ((0.6, 1.4), (1.6, 2.6)):                                         # two broad bands, noise only
        band = bp(n, max(60.0, f0 * lo_f * 0.5), f0 * hi_f, 2)
        grind += band
    grind = lp(grind, float(np.mean(fc)) * 2.5)
    slip = 0.55 + 0.45 * (bp(rng.standard_normal(len(t)), 8, 40, 1) > 0)             # stick-slip: irregular, not a buzz
    return gain * env * slip * (0.7 * grind + crackle)


def rattle(t, t0, dur, n, size, gain):
    """Torn pieces falling and hitting: sparse knocks thinning out."""
    y = np.zeros_like(t)
    for k in range(n):
        tk = t0 + dur * (rng.uniform(0, 1) ** 1.8)
        y += heavy_hit(t, tk, gain=rng.uniform(0.05, 0.25) * (1 - (tk - t0) / dur * 0.7), size=size * rng.uniform(0.25, 0.6))
    return gain * y


def crack(t, t0, gain):
    tt = np.clip(t - t0, 0, None)
    return gain * hp(rng.standard_normal(len(t)), 300) * np.exp(-tt / 0.006) * (t >= t0)


def crash():
    t = t_axis(7.0)
    x = heavy_hit(t, 0.05, gain=1.6, size=2.4)                    # the hull on the ground
    x += burst(t, 0.05, 0.9, 12, 90) * 1.2                        # the ground giving way
    x += crack(t, 0.06, 0.9) + crack(t, 0.19, 0.6)
    x += screech(t, 0.12, 1.6, 900, 240, 0.35) + screech(t, 0.4, 1.2, 1400, 500, 0.22) + screech(t, 0.9, 1.8, 600, 160, 0.25)
    x += rattle(t, 0.3, 5.5, 26, 1.0, 1.0)                        # what tore off coming down
    x += heavy_hit(t, 0.75, gain=0.7, size=1.6)                   # the hull settling back
    ext = air(echo(reverb(x, 2.4, 20, 600, 0.25), 0.35, 0.35))
    inn = lp(reverb(x, 2.0, 20, 300, 0.45, INT_HULL), 380)
    save("crash_ext.wav", ext, -0.5)
    save("crash_int.wav", inn, -0.5)


def leg_break():
    t = t_axis(4.5)
    x = crack(t, 0.02, 1.2) + crack(t, 0.08, 0.7)
    x += heavy_hit(t, 0.02, gain=0.8, size=1.2)
    x += screech(t, 0.05, 1.1, 700, 180, 0.3)                    # the joint giving
    x += heavy_hit(t, 1.25, gain=1.3, size=1.8)                   # the leg hits the ground
    x += burst(t, 1.25, 0.6, 15, 120)
    x += rattle(t, 1.35, 2.6, 12, 0.9, 0.8)
    ext = air(echo(reverb(x, 1.8, 25, 900, 0.22), 0.3, 0.3))
    inn = lp(reverb(x, 1.5, 25, 320, 0.4, INT_HULL), 400)
    save("leg_break_ext.wav", ext, -1.0)
    save("leg_break_int.wav", inn, -2.0)


def petal_snap():
    t = t_axis(2.2)
    x = crack(t, 0.01, 1.0)
    x += heavy_hit(t, 0.01, gain=0.6, size=0.7)
    x += screech(t, 0.05, 0.6, 1100, 420, 0.25)                   # the petal scraping onto its stop
    x += rattle(t, 0.15, 1.2, 5, 0.6, 0.6)
    ext = air(reverb(x, 1.2, 40, 1500, 0.2))
    inn = lp(reverb(x, 1.0, 40, 400, 0.35, INT_HULL), 450)
    save("petal_snap_ext.wav", ext, -2.0)
    save("petal_snap_int.wav", inn, -6.0)


if __name__ == "__main__":
    L.rng = rng                       # heavy_hit draws from the lift module's generator: this file's own seed
    crash()
    leg_break()
    petal_snap()
    print("->", L.OUT)
