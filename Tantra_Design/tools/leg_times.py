"""Leg times for the Tantra (2026-10-16): the drive as in the code (thrust and exhaust x (1 - b^2), Tantra.cpp RocketFactor),
4 chambers, anamezon only, jet 0.98 c. Usage: python leg_times.py"""
import math
C = 2.998e8; VE = 0.98 * C; LY = 9.4607e15; G0 = 9.80665
E = 16.01e6                                  # carried all the way: dry, traps, the screen's iron and argon

def burn(F, m, b0, b1, dt):
    """from b0 to b1 (accelerate if b1 > b0, brake otherwise); returns t, proper time, distance, mass"""
    t = tau = x = 0.0; b = b0; sgn = 1 if b1 > b0 else -1; mdot = 4 * F / VE
    while (b < b1) if sgn > 0 else (b > b1):
        a = 4 * F * (1 - b * b) / m
        x += b * C * dt; tau += math.sqrt(1 - b * b) * dt; t += dt
        b += sgn * a * dt / C; m -= mdot * dt
    return t, tau, x, m

def leg(name, F, extra, fuel, v, dists, dt):
    m0 = E + extra + 4 * fuel
    t1, s1, x1, m1 = burn(F, m0, 0.0, v, dt)
    t2, s2, x2, m2 = burn(F, m1, v, 0.0, dt)
    g = 1 / math.sqrt(1 - v * v)
    print(f"{name}: start {m0/1e6:.1f} kt, {4*F/m0/G0:.1f} g at start; to {v} c {t1/86400:.2f} d, braking {t2/86400:.2f} d, "
          f"left {(m2-E-extra)/4e6:+.1f} kt/trap")
    for d in dists:
        xc = d * LY - x1 - x2
        tc = xc / (v * C)
        T = (t1 + t2 + tc) / 86400 / 365.25; S = (s1 + s2 + tc / g) / 86400 / 365.25
        print(f"    {d:4} ly: Earth {T:6.2f} yr, crew {S:6.2f} yr")
    return t1 + t2

D = (5, 10, 15, 20, 30)
for model, F, dt in (("model 0 Efremov", 6.83e10, 5.0), ("model 1 lower power", 2.29e9, 120.0)):
    print("==", model)
    leg("  out  Triton -> Zirda, full charges 30.3 kt", F, 30.3e6, 57.6e6, 0.70, D, dt)
    leg("  back dark planet -> Triton, charges spent", F, 0.0, 57.6e6, 0.834, D, dt)
