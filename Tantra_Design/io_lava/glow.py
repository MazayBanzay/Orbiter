"""Visible incandescence of hot rock: blackbody luminance [cd/m2] and sRGB chromaticity, from Planck's law and the
CIE 1931 colour-matching functions (Wyman, Sloan & Shirley 2013 analytic fit)."""
import numpy as np

H, C, KB = 6.62607015e-34, 2.99792458e8, 1.380649e-23
LAM = np.arange(380e-9, 781e-9, 5e-9)

def _g(l, mu, s1, s2):
    s = np.where(l < mu, s1, s2); return np.exp(-0.5 * ((l - mu) / s) ** 2)

_l = LAM * 1e9
XB = 1.056 * _g(_l, 599.8, 37.9, 31.0) + 0.362 * _g(_l, 442.0, 16.0, 26.7) - 0.065 * _g(_l, 501.1, 20.4, 26.2)
YB = 0.821 * _g(_l, 568.8, 46.9, 40.5) + 0.286 * _g(_l, 530.9, 16.3, 31.1)
ZB = 1.217 * _g(_l, 437.0, 11.8, 36.0) + 0.681 * _g(_l, 459.0, 26.0, 13.8)
M = np.array([[3.2406, -1.5372, -0.4986], [-0.9689, 1.8758, 0.0415], [0.0557, -0.2040, 1.0570]])

_TT = np.arange(300.0, 2001.0, 5.0)
def _planck(T):
    return 2 * H * C ** 2 / LAM[None] ** 5 / np.expm1(H * C / (LAM[None] * KB * T[:, None]))
_B = _planck(_TT) * 0.95                       # emissivity of basalt
_dl = 5e-9
_X = (_B * XB).sum(1) * _dl * 683; _Y = (_B * YB).sum(1) * _dl * 683; _Z = (_B * ZB).sum(1) * _dl * 683
_rgb = (M @ np.stack([_X, _Y, _Z])).T
_rgb = np.clip(_rgb, 0, None); _rgb /= _rgb.max(1, keepdims=True)
_logY = np.log10(np.maximum(_Y, 1e-30))

def luminance(T):
    """cd/m2 of a surface at T [K]"""
    return 10 ** np.interp(T, _TT, _logY, left=-30)

def chroma(T):
    """linear sRGB, max channel = 1"""
    T = np.clip(T, _TT[0], _TT[-1])
    return np.stack([np.interp(T, _TT, _rgb[:, i]) for i in range(3)], -1)

DRAPER = 798.0                                  # K, faintest visible glow in the dark
Y_LO, Y_HI = luminance(DRAPER), luminance(1450.0)

NIGHT_EXP = 0.6                                 # tone curve of the glow mask (the AtmoFX fork adds it to the colour: no need to over-boost dim cracks)
def night_rgb(Y, rgb_lin):
    """Mask-layer night light: log-luminance between the Draper point and molten lava -> 0..1, times the colour"""
    b = np.clip(np.log10(np.maximum(Y, 1e-30) / Y_LO) / np.log10(Y_HI / Y_LO), 0, 1)
    return rgb_lin * (b ** NIGHT_EXP)[..., None]

Y_DAY_WHITE = 1361 / 5.2 ** 2 * 93 / np.pi      # cd/m2 of a white Lambert surface in Io sunlight (~1500)
def day_add(Y, rgb_lin):
    """what incandescence adds to the sunlit albedo texture (only molten lava competes with Io daylight)"""
    return rgb_lin * np.clip(Y / Y_DAY_WHITE, 0, 1)[..., None]

def to_srgb(lin):
    lin = np.clip(lin, 0, 1)
    return np.where(lin <= 0.0031308, 12.92 * lin, 1.055 * lin ** (1 / 2.4) - 0.055)

if __name__ == '__main__':
    for T in (700, 798, 900, 1000, 1100, 1200, 1300, 1450, 1600):
        print(T, f'{luminance(T):10.3g} cd/m2', np.round(to_srgb(chroma(T)) * 255).astype(int))
    print('Io daylight white', Y_DAY_WHITE)
