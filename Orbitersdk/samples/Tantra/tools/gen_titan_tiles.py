"""Titan for Orbiter 2016 (TileFormat 2): Surf, Mask and Elev layers in Textures/Titan.

Sources (Install/Titan_src):
  * Cassini ISS global mosaic 4 km/px, PIA19658 (USGS): surface brightness. Equirectangular,
    4040x2020, east longitude 0..360 from the left edge (checked on Menrva, 20N 87W).
  * Birch et al. 2017 polar geomorphologic maps (Zenodo 21685404, CC BY 4.0): seas (Fm),
    filled lakes (Fl), empty lake basins (El). Polar stereographic in metres, R = 2575 km;
    the x axis points to 90E, longitude grows east in the north and west in the south
    (fitted against the ISS darkness of the seas; Ontario Lacus lands on 72S 177E).
  * Miller et al. 2021 river networks (same archive): polylines, x/R + 180 deg = east
    longitude, the USGS frame of the ISS mosaic.
  * Cassini RADAR GTDR, model GTI (tensioned-spline interpolation of all altimetry and
    SARTopo through T77, 8 px/deg, west longitude): elevation.

Tile layout (Doc/PlanetTextures.pdf): levels 1-3 whole planet in a 128/256/512 square;
from level 4, 512x512 tiles, nlat = 2^(n-4), nlng = 2^(n-3), ilat 0 in the north, ilng 0
at 180W. Surf DXT1; Mask DXT1 with 1-bit alpha (0 = liquid); Elev *.elv 259x259 int16.

Colour: Titan's ground is lit by sunlight filtered through the haze, orange (Huygens DISR
colour views); that light is baked into the tiles, since the client's scattering cannot
tint the ground that strongly. Liquids are near black, empty lake beds a little brighter.

Usage: gen_titan_tiles.py <orbiter root>
"""
import io
import os
import struct
import sys
import zipfile

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import map_coordinates

Image.MAX_IMAGE_PIXELS = None
R = 2575e3

DARK = np.array([0.15, 0.095, 0.05])     # dune sand in orange light
BRIGHT = np.array([0.62, 0.45, 0.27])    # ice-rich uplands in orange light
LIQUID = np.array([0.035, 0.025, 0.018])  # methane/ethane seas and lakes
POLAR_LAT = 54.0                          # levels 8-9 only poleward of this
MAXLEVEL_GLOBAL, MAXLEVEL_POLAR, MAXLEVEL_ELEV = 7, 9, 8


# --------------------------------------------------------------------------- sources
def shp_records(z, name):
    b = z.read(name)
    off, out = 100, []
    while off < len(b):
        _, cl = struct.unpack(">ii", b[off:off + 8])
        off += 8
        rec = b[off:off + cl * 2]
        off += cl * 2
        t, = struct.unpack("<i", rec[:4])
        if t not in (3, 5):
            continue
        np_, npt = struct.unpack("<ii", rec[36:44])
        parts = list(struct.unpack("<%di" % np_, rec[44:44 + 4 * np_])) + [npt]
        pts = np.frombuffer(rec[44 + 4 * np_:44 + 4 * np_ + 16 * npt], dtype="<f8").reshape(npt, 2)
        out += [pts[parts[i]:parts[i + 1]] for i in range(np_)]
    return out


def polar_to_lonlat(p, north):
    x, y = p[:, 0], p[:, 1]
    colat = 2 * np.arctan(np.hypot(x, y) / (2 * R))
    lat = np.degrees(np.pi / 2 - colat) * (1 if north else -1)
    az = np.degrees(np.arctan2(y, x))
    lon = ((az if north else -az) + 90 + 180) % 360 - 180
    return np.c_[lon, lat]


def load_lakes(src):
    z = zipfile.ZipFile(os.path.join(src, "shapefiles.zip"))
    base = "shapefiles/Birch et al. (2017) Polar Mapping/%s/%s_%s.shp"
    liquid, empty = [], []
    for pole in ("NORTH", "SOUTH"):
        for layer, dest in (("Fm", liquid), ("Fl", liquid), ("El", empty)):
            try:
                recs = shp_records(z, base % (pole, layer, pole))
            except KeyError:
                continue
            dest += [polar_to_lonlat(p, pole == "NORTH") for p in recs if len(p) >= 3]
    rivers = []
    for p in shp_records(z, "shapefiles/Miller et al. (2021) Rivers Mapping/global_channels.shp"):
        lon = (np.degrees(p[:, 0] / R) + 180 + 180) % 360 - 180
        rivers.append(np.c_[lon, np.degrees(p[:, 1] / R)])
    return liquid, empty, rivers


def load_iss(src):
    a = np.asarray(Image.open(os.path.join(src, "Titan_ISS_P19658_Mosaic_Global_4km.tif")), dtype=np.float32)
    a = np.roll(a, a.shape[1] // 2, axis=1)  # 0..360E -> -180..180
    lo, hi = np.percentile(a, [0.5, 99.5])
    return np.clip((a - lo) / (hi - lo), 0, 1)


def load_gti(src):
    import gzip
    z = zipfile.ZipFile(os.path.join(src, "gtdr-data.zip"))
    half = []
    for c in ("090", "270"):  # 180W..0W and 360W..180W -> east -180..0 and 0..180
        raw = gzip.decompress(z.read(f"gtdr-data/GTIED00N{c}_T077_V01.IMG.gz"))
        a = np.frombuffer(raw[5760:5760 + 1440 * 1440 * 4], dtype="<f4").reshape(1440, 1440).astype(np.float32)
        half.append(a)
    g = np.concatenate(half, axis=1)
    g[~np.isfinite(g) | (np.abs(g) > 1e5)] = np.nan
    if np.isnan(g).any():
        g[np.isnan(g)] = np.nanmean(g)
    return g  # 1440 x 2880, lat 90 at the top, lon -180 at the left, metres


# --------------------------------------------------------------------------- sampling
def sample(grid, lon, lat, order=1):
    """Bilinear/bicubic lookup in an equirectangular grid (lon -180..180, lat 90..-90)."""
    h, w = grid.shape[:2]
    x = (lon + 180.0) / 360.0 * w - 0.5
    y = (90.0 - lat) / 180.0 * h - 0.5
    pad = 4
    g = np.concatenate([grid[:, -pad:], grid, grid[:, :pad]], axis=1)
    return map_coordinates(g, [y, x + pad], order=order, mode="nearest")


def detail_noise(lon, lat, seed=7):
    """Deterministic multi-octave value noise in planet coordinates (seamless across tiles)."""
    out = np.zeros_like(lon)
    for k, (scale, amp) in enumerate(((2.0, 0.5), (0.7, 0.3), (0.25, 0.2))):  # degrees
        u, v = lon / scale, lat / scale
        iu, iv = np.floor(u), np.floor(v)
        fu, fv = u - iu, v - iv
        fu, fv = fu * fu * (3 - 2 * fu), fv * fv * (3 - 2 * fv)

        def h(a, b):
            return (np.sin(a * 127.1 + b * 311.7 + seed * 17.0 + k * 3.1) * 43758.5453) % 1.0

        n = (h(iu, iv) * (1 - fu) + h(iu + 1, iv) * fu) * (1 - fv) + (h(iu, iv + 1) * (1 - fu) + h(iu + 1, iv + 1) * fu) * fv
        out += amp * (n - 0.5)
    return out


# --------------------------------------------------------------------------- tiles
def tile_bounds(n, ilat, ilng):
    nlat, nlng = 2 ** (n - 4), 2 ** (n - 3)
    lat1 = 90 - 180 / nlat * ilat
    lat0 = lat1 - 180 / nlat
    lon0 = -180 + 360 / nlng * ilng
    return lon0, lon0 + 360 / nlng, lat0, lat1


def unwrap_feature(p, closed):
    """Continuous longitudes; a ring around a pole is closed through the pole."""
    lon = np.degrees(np.unwrap(np.radians(p[:, 0])))
    lat = p[:, 1]
    if closed and abs(lon[-1] - lon[0]) > 180:
        pole = 90.0 if lat.mean() > 0 else -90.0
        lon = np.r_[lon, lon[-1], lon[0]]
        lat = np.r_[lat, pole, pole]
    return lon, lat


_prepared = {}


def prepare(feats, line):
    """Unwrap once and keep bounding boxes, so tiles only touch nearby features."""
    key = (id(feats), line)
    if key not in _prepared:
        out = []
        for p in feats:
            lon, lat = unwrap_feature(p, not line)
            out.append((lon, lat, lon.min(), lon.max(), lat.min(), lat.max()))
        _prepared[key] = out
    return _prepared[key]


def raster(width, height, lon0, lon1, lat0, lat1, feats, line=False, line_w=1):
    """Rasterise features into an equirectangular window; returns a float mask 0..1."""
    sx, sy = width / (lon1 - lon0), height / (lat1 - lat0)
    img = Image.new("L", (width, height), 0)
    d = ImageDraw.Draw(img)
    for lon, lat, xmin, xmax, ymin, ymax in prepare(feats, line):
        if ymax < lat0 or ymin > lat1:
            continue
        for shift in range(-720, 721, 360):
            if xmax + shift < lon0 or xmin + shift > lon1:
                continue
            ll = lon + shift
            xy = list(zip((ll - lon0) * sx, (lat1 - lat) * sy))
            if line:
                d.line(xy, fill=255, width=line_w)
            elif len(xy) >= 3:
                d.polygon(xy, fill=255)
    return np.asarray(img, dtype=np.float32) / 255.0


def draw_features(size, lon0, lon1, lat0, lat1, liquid, empty, rivers, river_w):
    """Lakes and rivers in a tile: (liquid, empty, river) masks."""
    return [raster(size, size, lon0, lon1, lat0, lat1, liquid),
            raster(size, size, lon0, lon1, lat0, lat1, empty),
            raster(size, size, lon0, lon1, lat0, lat1, rivers, line=True, line_w=river_w)]


def surface_rgb(iss, lon, lat, feats, detail):
    t = sample(iss, lon, lat, order=3)
    if detail:
        t = np.clip(t + 0.08 * detail_noise(lon, lat), 0, 1)
    rgb = DARK + (BRIGHT - DARK) * (np.clip(t, 0, 1) ** 1.1)[..., None]
    liq, emp, riv = feats
    rgb = rgb * (1 + 0.15 * emp[..., None])
    rgb = rgb * (1 - 0.25 * riv[..., None])
    rgb = rgb * (1 - liq[..., None]) + LIQUID * liq[..., None]
    return np.clip(rgb * 255, 0, 255).astype(np.uint8)


def save_dds(arr, path, alpha=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img = Image.fromarray(np.dstack([arr, alpha]) if alpha is not None else arr)
    buf = io.BytesIO()
    img.save(buf, "DDS", pixel_format="DXT1")
    with open(path, "wb") as f:
        f.write(buf.getvalue())


def grid(size, lon0, lon1, lat0, lat1):
    x = lon0 + (np.arange(size) + 0.5) / size * (lon1 - lon0)
    y = lat1 - (np.arange(size) + 0.5) / size * (lat1 - lat0)
    return np.meshgrid(x, y)


def write_elv(path, elev, lon0, lon1, lat0, lat1):
    """elev: 259x259 metres, row 0 = bottom (min latitude), column 1 = left edge."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    e = np.round(elev).astype(np.int16)
    offset = 0.0
    hdr = struct.pack("<4siiiiii9d", b"ELE\x01", 100, -16, 259, 259, 1, 1, 1.0, offset,
                      np.radians(lat0), np.radians(lat1), np.radians(lon0), np.radians(lon1),
                      float(e.min()), float(e.max()), float(e.mean()))
    assert len(hdr) == 100
    with open(path, "wb") as f:
        f.write(hdr)
        f.write(e.astype("<i2").tobytes())


# --------------------------------------------------------------------------- build
def build(root):
    src = os.path.join(root, "Install", "Titan_src")
    out = os.path.join(root, "Textures", "Titan")
    iss = load_iss(src)
    liquid, empty, rivers = load_lakes(src)
    gti = load_gti(src)
    print(f"sources: ISS {iss.shape}, lakes {len(liquid)} liquid / {len(empty)} empty, rivers {len(rivers)}, GTI {gti.shape}")

    # Seas lie flat: inside the liquid outlines the surface is the level of their shores.
    sea = raster(gti.shape[1], gti.shape[0], -180, 180, -90, 90, liquid) > 0.5
    for hemi in (slice(0, 720), slice(720, 1440)):
        m = sea[hemi]
        if m.any():
            level = np.percentile(gti[hemi][m], 10)
            gti[hemi][m] = level
    print("sea level set; GTI range %.0f..%.0f m" % (gti.min(), gti.max()))

    counts = {"Surf": 0, "Mask": 0, "Elev": 0}

    def surf_and_mask(n, ilat, ilng, size=512):
        lon0, lon1, lat0, lat1 = tile_bounds(n, ilat, ilng)
        lon, lat = grid(size, lon0, lon1, lat0, lat1)
        feats = draw_features(size, lon0, lon1, lat0, lat1, liquid, empty, rivers, 1 if n < 8 else 2)
        rgb = surface_rgb(iss, lon, lat, feats, detail=n >= 7)
        save_dds(rgb, os.path.join(out, "Surf", f"{n:02d}", f"{ilat:06d}", f"{ilng:06d}.dds"))
        counts["Surf"] += 1
        liq = feats[0] > 0.5
        if liq.any() or n <= 4:
            alpha = np.where(liq, 0, 255).astype(np.uint8)
            save_dds(np.zeros_like(rgb), os.path.join(out, "Mask", f"{n:02d}", f"{ilat:06d}", f"{ilng:06d}.dds"), alpha)
            counts["Mask"] += 1

    # Levels 1-3: whole planet in a square.
    for n, size in ((1, 128), (2, 256), (3, 512)):
        lon, lat = grid(size, -180, 180, -90, 90)
        feats = draw_features(size, -180, 180, -90, 90, liquid, empty, rivers, 1)
        rgb = surface_rgb(iss, lon, lat, feats, detail=False)
        save_dds(rgb, os.path.join(out, "Surf", f"{n:02d}", "000000", "000000.dds"))
        alpha = np.where(feats[0] > 0.5, 0, 255).astype(np.uint8)
        save_dds(np.zeros_like(rgb), os.path.join(out, "Mask", f"{n:02d}", "000000", "000000.dds"), alpha)
        counts["Surf"] += 1
        counts["Mask"] += 1

    for n in range(4, MAXLEVEL_POLAR + 1):
        nlat, nlng = 2 ** (n - 4), 2 ** (n - 3)
        for ilat in range(nlat):
            lon0, lon1, lat0, lat1 = tile_bounds(n, ilat, 0)
            if n > MAXLEVEL_GLOBAL and min(abs(lat0), abs(lat1)) < POLAR_LAT:
                continue
            for ilng in range(nlng):
                surf_and_mask(n, ilat, ilng)
        print(f"level {n}: Surf {counts['Surf']}, Mask {counts['Mask']}")

    # Elevation: 259x259 nodes, one-node padding, rows from the bottom.
    for n in range(4, MAXLEVEL_ELEV + 1):
        nlat, nlng = 2 ** (n - 4), 2 ** (n - 3)
        for ilat in range(nlat):
            for ilng in range(nlng):
                lon0, lon1, lat0, lat1 = tile_bounds(n, ilat, ilng)
                i = np.arange(259) - 1
                lon = lon0 + i / 256 * (lon1 - lon0)
                lat = lat0 + i / 256 * (lat1 - lat0)
                LON, LAT = np.meshgrid(lon, lat)
                LAT = np.clip(LAT, -89.99, 89.99)
                e = sample(gti, ((LON + 180) % 360) - 180, LAT, order=3)
                write_elv(os.path.join(out, "Elev", f"{n:02d}", f"{ilat:06d}", f"{ilng:06d}.elv"), e, lon0, lon1, lat0, lat1)
                counts["Elev"] += 1
        print(f"elev level {n}: {counts['Elev']}")
    print("done:", counts)


if __name__ == "__main__":
    build(sys.argv[1])
