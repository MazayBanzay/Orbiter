"""1-D cooling of a basaltic lava sheet on Io (vacuum, night side): what the surface, a crack and the crust look
like as a function of age since the lava was exposed.

Physics: implicit conduction in a half-space of lava erupted at the liquidus, latent heat released between liquidus
and solidus (apparent heat capacity), top surface radiating to a ~0 K sky (no air on Io: radiation is the only
surface loss; ~100 K frost background matters only below ~150 K). Bottom is held at the eruption temperature
(a thick sheet / tube-fed lobe is still molten inside for the ages that matter here).

Numbers (Io silicate lavas, Galileo NIMS/SSI colour temperatures 1100-1600 K; Prometheus ~1450 K, basaltic):
  T_erupt 1450 K, solidus 1270 K, k 1.2 W/m/K (vesicular basalt), rho 2400 kg/m3, cp 1200 J/kg/K,
  latent heat 4.0e5 J/kg, emissivity 0.95.
"""
import numpy as np, os

T_ERUPT, T_SOL = 1450.0, 1270.0
K, RHO, CP, LAT, EPS = 1.2, 2400.0, 1200.0, 4.0e5, 0.95
SIGMA = 5.670374e-8
T_SKY = 0.0

def _cp_eff(T):
    return CP + np.where((T > T_SOL) & (T <= T_ERUPT + 1), LAT / (T_ERUPT - T_SOL), 0.0)

def simulate(t_end=3.2e7, n=140, dz0=4e-4, grow=1.065):
    dz = dz0 * grow ** np.arange(n)            # cell thicknesses, fine at the top
    z = np.concatenate([[0], np.cumsum(dz)])[:-1] + dz / 2   # cell centres [m]
    T = np.full(n, T_ERUPT)
    ages = np.logspace(-2, np.log10(t_end), 600)
    out_T = np.zeros((len(ages), n)); out_Ts = np.zeros(len(ages))
    t, dt, j = 0.0, 1e-4, 0
    while j < len(ages):
        step = min(dt, ages[j] - t) if ages[j] > t else dt
        c = RHO * _cp_eff(T) * dz / step         # heat capacity per area / dt
        # conductances between cells
        g = K / ((dz[:-1] + dz[1:]) / 2)
        a = np.zeros(n); b = c.copy(); cc = np.zeros(n); d = c * T
        b[:-1] += g; b[1:] += g; a[1:] = -g; cc[:-1] = -g
        # top: radiation, linearised about the current surface (half-cell extrapolation)
        Ts = T[0]
        hr = 4 * EPS * SIGMA * Ts ** 3
        b[0] += hr; d[0] += hr * Ts - EPS * SIGMA * (Ts ** 4 - T_SKY ** 4)
        # bottom: held at eruption temperature
        gb = K / (dz[-1] / 2); b[-1] += gb; d[-1] += gb * T_ERUPT
        # tridiagonal solve
        for i in range(1, n):
            m = a[i] / b[i - 1]; b[i] -= m * cc[i - 1]; d[i] -= m * d[i - 1]
        Tn = np.empty(n); Tn[-1] = d[-1] / b[-1]
        for i in range(n - 2, -1, -1): Tn[i] = (d[i] - cc[i] * Tn[i + 1]) / b[i]
        T = Tn; t += step
        if abs(t - ages[j]) < 1e-12 or t >= ages[j]:
            # surface temperature: extrapolate the top half cell with the radiative flux
            q = EPS * SIGMA * T[0] ** 4
            out_Ts[j] = max(T[0] - q * (dz[0] / 2) / K, 50.0)
            out_T[j] = T; j += 1
        dt = min(dt * 1.02, max(1e-4, t * 0.02))
    thick = np.array([np.interp(T_SOL, np.maximum.accumulate(row), z) if row.min() < T_SOL else 0.0 for row in out_T])
    return dict(age=ages, Ts=out_Ts, z=z, T=out_T, crust=thick)

_CACHE = os.path.join(os.path.dirname(__file__), 'cooling_table.npz')

def table(rebuild=False):
    if rebuild or not os.path.exists(_CACHE):
        np.savez_compressed(_CACHE, **simulate())
    return dict(np.load(_CACHE))

def surface_T(age, tb=None):
    tb = tb or table()
    return np.interp(np.log10(np.maximum(age, 1e-2)), np.log10(tb['age']), tb['Ts'])

def crust(age, tb=None):
    tb = tb or table()
    return np.interp(np.log10(np.maximum(age, 1e-2)), np.log10(tb['age']), tb['crust'])

def T_at_depth(age, depth, tb=None):
    """Temperature at a depth below the surface (crack walls), vectorised over age/depth arrays."""
    tb = tb or table()
    la = np.log10(np.maximum(age, 1e-2)); ta = np.log10(tb['age'])
    i = np.clip(np.searchsorted(ta, la) - 1, 0, len(ta) - 2)
    w = np.clip((la - ta[i]) / (ta[i + 1] - ta[i]), 0, 1)
    zi = np.interp(depth, tb['z'], np.arange(len(tb['z'])))
    k = np.clip(zi.astype(int), 0, len(tb['z']) - 2); f = zi - k
    T0 = tb['T'][i, k] * (1 - f) + tb['T'][i, k + 1] * f
    T1 = tb['T'][i + 1, k] * (1 - f) + tb['T'][i + 1, k + 1] * f
    return T0 * (1 - w) + T1 * w

if __name__ == '__main__':
    tb = table(rebuild=True)
    for a, name in [(1, '1 s'), (10, '10 s'), (60, '1 min'), (600, '10 min'), (3600, '1 h'), (6 * 3600, '6 h'),
                    (86400, '1 d'), (7 * 86400, '1 wk'), (30 * 86400, '1 mo'), (3.15e7, '1 yr')]:
        print(f'{name:>6}: surface {surface_T(a, tb):6.0f} K ({surface_T(a, tb) - 273.15:5.0f} C), '
              f'crust {crust(a, tb) * 100:6.1f} cm, T at 0.3 m {T_at_depth(np.array(a), np.array(0.3), tb):5.0f} K')
