"""Real Sun images for the D3D11 client's Sun (src\\Sun.cpp, shaders\\sun.hlsl), from Helioviewer (api.helioviewer.org).

For each date given (YYYY-MM-DD[THH:MM]) the images closest to it are fetched as the archive's own JPEG 2000 frames (not
the coloured screenshots) with their metadata, and turned into maps the client reads:
  wl.bin      white light, the disc seen from Earth: SDO/HMI continuum (from Dec 2010) or SOHO/MDI continuum (1996-2011).
              Its contrast against the mean limb darkening (spots < 1, faculae > 1) on a heliographic grid of the
              Earth-facing hemisphere: longitude from the central meridian -90..90, latitude -90..90 (1024 x 1024, RG8:
              R contrast / 2, G how well it was seen - 0 near the limb).
  ha.bin      the same for GONG H-alpha 656.3 nm (from Dec 2015): the network, plage, filaments.
  halimb.bin  GONG H-alpha off the limb: position angle (from solar north, through east) 0..360 x height 0..0.12 solar
              radii (1024 x 128, R8: brightness against the disc's centre x 4) - the prominences as seen from Earth.
  c2.bin      SOHO/LASCO C2 white-light corona: position angle x radius (1024 x 256, R8: the
              brightness against its mean at that radius / 4; 2.2..6.0 radii) - the streamers' shape as seen from Earth.
  meta.txt    each map's date (MJD) and where Earth was (heliocentric ecliptic longitude): the client turns the disc
              maps with the Sun's differential rotation from then to the simulation's time; the limb and corona maps
              hold only near Earth's line of sight.
Output: Modules\\D3D11Client\\sun\\<YYYYMMDD>\\ (the client takes the set nearest the simulation date).
Data: SDO (NASA/SDO and the AIA, EVE, HMI science teams), SOHO (ESA/NASA), GONG (NSO/AURA/NSF) - public, via
Helioviewer. Run with python -I; the downloads are kept in tools\\sun_cache (untrusted files: parsed by Pillow only).
"""
import io, json, math, os, sys, urllib.request
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ORB = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
CACHE = os.path.join(HERE, 'sun_cache')
API = 'https://api.helioviewer.org/v2/'
SRC = {'hmi': 18, 'mdi': 7, 'gong': 94, 'c2': 4}


def get(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def closest(src, date):
    return json.loads(get(API + 'getClosestImage/?date=%sZ&sourceId=%d' % (date, src)))


def jp2(meta):
    os.makedirs(CACHE, exist_ok=True)
    fn = os.path.join(CACHE, '%s.jp2' % meta['id'])
    if not os.path.exists(fn):
        data = get(API + 'getJP2Image/?id=%s' % meta['id'])
        open(fn, 'wb').write(data)
        print('  downloaded %s (%.1f MB)' % (os.path.basename(fn), len(data) / 1e6))
    im = Image.open(fn)
    a = np.asarray(im.convert('F') if im.mode not in ('L', 'I;16', 'F') else im, dtype=np.float32)
    return a


def mjd(date):     # 'YYYY-MM-DD HH:MM:SS'
    y, mo, d = int(date[0:4]), int(date[5:7]), int(date[8:10])
    hh, mm, ss = (int(date[11:13]), int(date[14:16]), int(date[17:19])) if len(date) >= 19 else (12, 0, 0)
    if mo <= 2: y -= 1; mo += 12
    A = y // 100; B = 2 - A + A // 4
    jd = int(365.25 * (y + 4716)) + int(30.6001 * (mo + 1)) + d + B - 1524.5 + (hh + mm / 60 + ss / 3600) / 24
    return jd - 2400000.5


def orientation(m):
    """Meeus ch. 29: B0 (the disc centre's heliographic latitude) and Earth's heliocentric ecliptic longitude [deg]."""
    jd = m + 2400000.5
    T = (jd - 2451545.0) / 36525
    L0 = 280.46646 + 36000.76983 * T
    M = math.radians(357.52911 + 35999.05029 * T)
    C = (1.914602 - 0.004817 * T) * math.sin(M) + 0.019993 * math.sin(2 * M)
    lam = L0 + C                                  # the Sun's true geocentric longitude
    K = 73.6667 + 1.3958333 * (jd - 2396758) / 36525
    I = math.radians(7.25)
    B0 = math.degrees(math.asin(math.sin(math.radians(lam - K)) * math.sin(I)))
    return B0, (lam + 180) % 360


def disc_map(img, meta, B0):
    """The disc's contrast against its mean limb darkening, on a heliographic grid of the visible hemisphere."""
    h, w = img.shape
    cx, cy, R = meta['refPixelX'] - 1 + meta.get('offsetX', 0), meta['refPixelY'] - 1 + meta.get('offsetY', 0), meta['rsun']
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.hypot(xx - cx, (h - 1 - yy) - cy) / R          # JPEG 2000 rows top-down, the archive's y up
    mu = np.sqrt(np.clip(1 - r * r, 0, 1))
    inside = r < 0.995
    # the mean limb darkening: the median in rings of mu (spots and faculae fall out of a median)
    bins = np.clip((mu * 200).astype(int), 0, 199)
    prof = np.zeros(200, np.float32)
    for b in range(200):
        sel = inside & (bins == b)
        if sel.sum() > 30: prof[b] = np.median(img[sel])
    good = prof > 0
    prof = np.interp(np.arange(200), np.nonzero(good)[0], prof[good])
    con = np.where(inside, img / np.maximum(prof[bins], 1e-6), 0)
    N = 1024
    lat = np.radians(np.linspace(-90, 90, N)).reshape(-1, 1)
    lon = np.radians(np.linspace(-90, 90, N)).reshape(1, -1)
    b0 = math.radians(B0)
    x = np.cos(lat) * np.sin(lon)
    y = np.sin(lat) * math.cos(b0) - np.cos(lat) * np.cos(lon) * math.sin(b0)
    z = np.sin(lat) * math.sin(b0) + np.cos(lat) * np.cos(lon) * math.cos(b0)
    px = np.clip(np.round(cx + x * R).astype(int), 0, w - 1)
    py = np.clip(np.round(h - 1 - (cy + y * R)).astype(int), 0, h - 1)
    val = con[py, px]
    seen = np.clip((z - 0.12) / 0.2, 0, 1)
    out = np.zeros((N, N, 2), np.uint8)
    out[..., 0] = np.clip(val / 2 * 255, 0, 255)
    out[..., 1] = np.clip(seen * 255, 0, 255) * (val > 0)
    return out[::-1], prof[-1]       # rows: latitude +90 first


def polar_map(img, meta, r0, r1, nr, rot_deg):
    """Position angle (from north through east, i.e. counter-clockwise on a north-up image) x radius [solar radii]."""
    h, w = img.shape
    cx, cy, R = meta['refPixelX'] - 1, meta['refPixelY'] - 1, meta['rsun']
    pa = np.radians(np.linspace(0, 360, 1024, endpoint=False)).reshape(1, -1) + math.radians(rot_deg)
    rr = np.linspace(r0, r1, nr).reshape(-1, 1)
    x = cx - np.sin(pa) * rr * R                  # east is to the left on a north-up image
    y = cy + np.cos(pa) * rr * R
    px = np.clip(np.round(x).astype(int), 0, w - 1)
    py = np.clip(np.round(h - 1 - y).astype(int), 0, h - 1)
    return img[py, px]


def save(fn, arr):
    h, w = arr.shape[:2]
    c = 1 if arr.ndim == 2 else arr.shape[2]
    with open(fn, 'wb') as f:
        f.write(np.array([w, h, c], np.uint32).tobytes())
        f.write(np.ascontiguousarray(arr, np.uint8).tobytes())


def run(date):
    if len(date) == 10: date += 'T12:00:00'
    out = os.path.join(ORB, 'Modules', 'D3D11Client', 'sun', date[:10].replace('-', ''))
    os.makedirs(out, exist_ok=True)
    meta_lines = ['; real Sun maps for %s (tools\\sun_fetch.py; Helioviewer: SDO, SOHO, GONG)' % date]
    want = mjd(date.replace('T', ' '))
    print(date)
    # white light: HMI, or MDI before it
    m = closest(SRC['hmi'], date)
    if abs(mjd(m['date']) - want) > 20:
        m = closest(SRC['mdi'], date)
    if abs(mjd(m['date']) - want) <= 20:
        B0, el = orientation(mjd(m['date']))
        img = jp2(m)
        mp, _ = disc_map(img, m, B0)
        save(os.path.join(out, 'wl.bin'), mp)
        meta_lines.append('wl %.5f %.4f ; %s %s' % (mjd(m['date']), el, m['name'], m['date']))
        print('  white light: %s %s' % (m['name'], m['date']))
    # H-alpha: the disc and the limb
    m = closest(SRC['gong'], date)
    if abs(mjd(m['date']) - want) <= 20:
        B0, el = orientation(mjd(m['date']))
        img = jp2(m)
        mp, centre = disc_map(img, m, B0)
        save(os.path.join(out, 'ha.bin'), mp)
        limb = polar_map(img, m, 1.0, 1.12, 128, m.get('rotation', 0))     # (GONG's frame ends at ~1.13 radii)
        # the instrument's scattered light falls off smoothly with height: each height's median round the limb is taken
        # away, leaving what stands out of it - the prominences (and the spicules' rim at the bottom rows)
        limb = limb - np.median(limb, axis=1, keepdims=True)
        limb = np.clip(limb / max(centre, 1e-6) * 4 * 255, 0, 255).astype(np.uint8)
        save(os.path.join(out, 'halimb.bin'), limb)
        meta_lines.append('ha %.5f %.4f ; %s %s' % (mjd(m['date']), el, m['name'], m['date']))
        print('  H-alpha: %s %s' % (m['name'], m['date']))
    # the corona: LASCO C2
    m = closest(SRC['c2'], date)
    if abs(mjd(m['date']) - want) <= 30:
        B0, el = orientation(mjd(m['date']))
        img = jp2(m)
        pm = polar_map(img, m, 2.2, 6.0, 256, m.get('rotation', 0))
        mean = np.maximum(np.median(pm, axis=1, keepdims=True), 1e-6)
        save(os.path.join(out, 'c2.bin'), np.clip(pm / mean / 4 * 255, 0, 255).astype(np.uint8))
        meta_lines.append('c2 %.5f %.4f ; %s %s' % (mjd(m['date']), el, m['name'], m['date']))
        print('  corona: %s %s' % (m['name'], m['date']))
    open(os.path.join(out, 'meta.txt'), 'w').write('\n'.join(meta_lines) + '\n')
    print('  ->', out)


if __name__ == '__main__':
    for d in sys.argv[1:] or ['2026-10-20']:
        run(d)
