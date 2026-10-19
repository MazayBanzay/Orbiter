"""Quick look at the core (or any box): colour, night glow, shaded relief, temperature and units maps -> previews/."""
import sys, os, time, numpy as np
from PIL import Image
import field, glow

def render(x0, x1, y0, y1, pix, name):
    xs = np.arange(x0 + pix / 2, x1, pix); ys = np.arange(y1 - pix / 2, y0, -pix)
    X, Y = np.meshgrid(xs, ys)
    t = time.time(); o = field.evaluate(X, Y, pix); print(name, X.shape, f'{time.time() - t:.1f} s')
    os.makedirs('previews', exist_ok=True)
    Yl = (1 - o['f_c'] - o['f_a']) * glow.luminance(o['Ts']) + o['f_c'] * glow.luminance(o['Tc']) + o['f_a'] * glow.luminance(o['Tax'])
    Tg = np.where(Yl > 0, np.interp(np.log10(np.maximum(Yl, 1e-30)), np.log10(glow.luminance(np.arange(700, 1700.0))), np.arange(700, 1700.0)), 0)
    night = glow.night_rgb(Yl, glow.chroma(Tg))
    day = glow.to_srgb(np.clip(o['rgb'] ** 2.2 + glow.day_add(Yl, glow.chroma(Tg)), 0, 1))
    Image.fromarray((day * 255).astype(np.uint8)).save(f'previews/{name}_day.png')
    Image.fromarray((glow.to_srgb(night) * 255).astype(np.uint8)).save(f'previews/{name}_night.png')
    h = o['h']; gy, gx = np.gradient(h, pix)
    L = np.array([-0.5, 0.5, 0.7]); L /= np.linalg.norm(L)
    sh = np.clip((-gx * L[0] + gy * L[1] + L[2]) / np.sqrt(gx * gx + gy * gy + 1), 0, 1)
    hn = (h - h.min()) / (np.ptp(h) + 1e-9)
    rel = np.stack([sh * (0.5 + 0.5 * hn), sh * 0.9, sh * (1 - 0.4 * hn)], -1)
    Image.fromarray((rel * 255).astype(np.uint8)).save(f'previews/{name}_relief.png')
    T = o['T_rad'] - 273.15
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(1, 2, figsize=(14, 6.5))
    im = ax[0].imshow(np.where(o['lava'] > 0.5, T, np.nan), extent=[x0, x1, y0, y1], cmap='inferno', vmin=-180, vmax=1200)
    plt.colorbar(im, ax=ax[0], label='T_rad, °C'); ax[0].set_title('radiative surface temperature (lava only)')
    im = ax[1].imshow(o['kind'], extent=[x0, x1, y0, y1], cmap='tab10', vmin=0, vmax=9, interpolation='nearest')
    plt.colorbar(im, ax=ax[1], label='kind'); ax[1].set_title('units: 0 frost 1 yrs 2 mo 3 days 4 hrs 5 toe 6 molten 7 cone')
    for a in ax: a.plot(*field.W_SITE, 'c+', ms=14)
    fig.tight_layout(); fig.savefig(f'previews/{name}_thermal.png', dpi=70)
    print('h range', h.min(), h.max(), ' T_rad max', o['T_rad'].max(), ' kinds', np.bincount(o['kind'].ravel()))
    return o

if __name__ == '__main__':
    a = sys.argv[1] if len(sys.argv) > 1 else 'core'
    if a == 'core': render(-698, 698, -698, 698, 2.0, 'core')
    elif a == 'site': render(-225, -125, -225, -125, 0.17, 'site100')
    elif a == 'vent': render(10, 110, 150, 250, 0.17, 'vent100')

def regional_view(half=6000, pix=12.0, name='regional'):
    """the composite (stock + regional detail + core) as the low tile levels see it"""
    import build, iosite as S
    xs = np.arange(-half + pix / 2, half, pix); ys = np.arange(half - pix / 2, -half, -pix)
    X, Y = np.meshgrid(xs, ys)
    lat, lng = S.to_latlng(X, Y)
    base = build.stock_surf(lat, lng)
    rgb = build.regional(X, Y, pix, base)
    wc = build.core_blend(X, Y, pix)
    m = wc > 0
    r = np.nonzero(m.any(1))[0]; c = np.nonzero(m.any(0))[0]
    sl = (slice(r[0], r[-1] + 1), slice(c[0], c[-1] + 1))
    o = field.evaluate(X[sl], Y[sl], pix, base=base[sl])
    rgb[sl] = rgb[sl] * (1 - wc[sl][..., None]) + o['rgb'] * wc[sl][..., None]
    Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).save(f'previews/{name}.png')

if __name__ == '__main__' and len(sys.argv) > 1 and sys.argv[1] == 'regional':
    regional_view()
    regional_view(1500, 3.0, 'regional_near')
