"""Vectorised, seedable 2-D noise in metres: gradient (Perlin) fBm and cellular (Worley) noise.
Every function takes the pixel size so that features smaller than ~2 px fade out (no aliasing between tile levels)."""
import numpy as np

def _hash(ix, iy, seed):
    h = (ix.astype(np.int64) * 374761393 + iy.astype(np.int64) * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFFFFFF

def _u01(h):
    return (h & 0xFFFFFF) / float(0x1000000)

def perlin(x, y, seed=0):
    ix, iy = np.floor(x), np.floor(y)
    fx, fy = x - ix, y - iy
    ix = ix.astype(np.int64); iy = iy.astype(np.int64)
    def grad(dx, dy, ox, oy):
        a = _u01(_hash(ix + ox, iy + oy, seed)) * 2 * np.pi
        return np.cos(a) * dx + np.sin(a) * dy
    u = fx * fx * fx * (fx * (fx * 6 - 15) + 10); v = fy * fy * fy * (fy * (fy * 6 - 15) + 10)
    n00 = grad(fx, fy, 0, 0); n10 = grad(fx - 1, fy, 1, 0)
    n01 = grad(fx, fy - 1, 0, 1); n11 = grad(fx - 1, fy - 1, 1, 1)
    return ((n00 * (1 - u) + n10 * u) * (1 - v) + (n01 * (1 - u) + n11 * u) * v) * 1.41

def band(wavelength, pix):
    """weight of a feature of this wavelength at this pixel size: 1 when >= 4 px, 0 below 2 px"""
    return np.clip(wavelength / pix / 2.0 - 1.0, 0.0, 1.0)

def fbm(x, y, wl, pix, octaves=6, gain=0.5, seed=0, lac=2.0):
    s = np.zeros_like(x); amp, norm = 1.0, 0.0
    for o in range(octaves):
        w = band(wl, pix)
        if w > 0:
            s += amp * w * perlin(x / wl, y / wl, seed + 17 * o)
        norm += amp; amp *= gain; wl /= lac
    return s / norm

def ridged(x, y, wl, pix, octaves=4, seed=0):
    s = np.zeros_like(x); amp, norm = 1.0, 0.0
    for o in range(octaves):
        w = band(wl, pix)
        if w > 0:
            s += amp * w * (1 - np.abs(perlin(x / wl, y / wl, seed + 31 * o)))
        norm += amp; amp *= 0.5; wl /= 2.0
    return s / norm

def worley(x, y, cell, seed=0, jitter=0.9):
    """F1, F2 distances (in cell units), id of the nearest cell, vector to its feature point (cell units)"""
    gx, gy = x / cell, y / cell
    ix, iy = np.floor(gx).astype(np.int64), np.floor(gy).astype(np.int64)
    f1 = np.full(x.shape, 9.0); f2 = np.full(x.shape, 9.0); idn = np.zeros(x.shape, np.int64)
    vx = np.zeros(x.shape); vy = np.zeros(x.shape)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx, cy = ix + ox, iy + oy
            h = _hash(cx, cy, seed)
            px = cx + 0.5 + jitter * (_u01(h) - 0.5)
            py = cy + 0.5 + jitter * (_u01(_hash(cx, cy, seed + 7)) - 0.5)
            d = np.hypot(gx - px, gy - py)
            closer = d < f1
            f2 = np.where(closer, f1, np.minimum(f2, d))
            idn = np.where(closer, h, idn)
            vx = np.where(closer, gx - px, vx); vy = np.where(closer, gy - py, vy)
            f1 = np.where(closer, d, f1)
    return f1, f2, idn, vx, vy

def cellrand(idn, k=0):
    return _u01(_hash(idn, np.full_like(idn, k), 991))
