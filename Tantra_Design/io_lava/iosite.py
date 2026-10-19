"""Where the lava field is on Io and how Orbiter's tile quadtree maps onto it (Orbiter Developer Manual 5.1)."""
import math, numpy as np

R_IO = 1.821e6                      # Config/Io.cfg Size [m]

# The core is exactly one level-16 tile (1397 m square): the one whose centre lies on the southern margin of the
# central Prometheus flow field (dark on the stock Galileo/Voyager mosaic, bright frost/sulfur plain to the south),
# ~40 km SW of Prometheus Patera. Orbiter longitudes are east-positive: 153.61 W = -153.61.
CORE_LVL, CORE_ILAT, CORE_ILNG = 16, 2088, 600

def nlat(lvl): return 2 ** (lvl - 4)
def nlng(lvl): return 2 ** (lvl - 3)

def tile_bounds(lvl, ilat, ilng):
    """(latmin, latmax, lngmin, lngmax) in radians"""
    dlat = math.pi / nlat(lvl); dlng = 2 * math.pi / nlng(lvl)
    latmax = math.pi / 2 - ilat * dlat
    lngmin = -math.pi + ilng * dlng
    return latmax - dlat, latmax, lngmin, lngmin + dlng

_b = tile_bounds(CORE_LVL, CORE_ILAT, CORE_ILNG)
LAT0 = (_b[0] + _b[1]) / 2           # site origin = centre of the core tile
LNG0 = (_b[2] + _b[3]) / 2
COSLAT0 = math.cos(LAT0)

def to_xy(lat, lng):
    """local east/north metres from the core centre (equirectangular; error < 1 mm over a few km)"""
    return R_IO * COSLAT0 * (lng - LNG0), R_IO * (lat - LAT0)

def to_latlng(x, y):
    return LAT0 + y / R_IO, LNG0 + x / (R_IO * COSLAT0)

def tile_of(lvl, lat, lng):
    ilat = int((math.pi / 2 - lat) / (math.pi / nlat(lvl)))
    ilng = int((lng + math.pi) / (2 * math.pi / nlng(lvl)))
    return ilat, ilng

def tiles_covering(lvl, x0, y0, x1, y1):
    """all tiles of a level that intersect the local box [x0,x1]x[y0,y1] (metres)"""
    la0, ln0 = to_latlng(x0, y0); la1, ln1 = to_latlng(x1, y1)
    a1, b0 = tile_of(lvl, la0, ln0); a0, b1 = tile_of(lvl, la1, ln1)
    return [(lvl, a, b) for a in range(a0, a1 + 1) for b in range(b0, b1 + 1)]

def tile_metres(lvl):
    return R_IO * math.pi / nlat(lvl)

def surf_grid(lvl, ilat, ilng, n=512):
    """pixel-centre lat/lng of a 512x512 surface tile, row 0 = north"""
    latmin, latmax, lngmin, lngmax = tile_bounds(lvl, ilat, ilng)
    f = (np.arange(n) + 0.5) / n
    lat = latmax - f * (latmax - latmin); lng = lngmin + f * (lngmax - lngmin)
    LNG, LAT = np.meshgrid(lng, lat)
    return LAT, LNG

def elev_grid(lvl, ilat, ilng):
    """node lat/lng of a 259x259 .elv tile, row 0 = south (min lat), node 1 / 257 = tile edges"""
    latmin, latmax, lngmin, lngmax = tile_bounds(lvl, ilat, ilng)
    f = (np.arange(259) - 1) / 256.0
    lat = latmin + f * (latmax - latmin); lng = lngmin + f * (lngmax - lngmin)
    LNG, LAT = np.meshgrid(lng, lat)
    return LAT, LNG

if __name__ == '__main__':
    print(f'core tile {CORE_LVL}/{CORE_ILAT}/{CORE_ILNG}: centre lat {math.degrees(LAT0):.4f}, '
          f'lng {math.degrees(LNG0):.4f} ({-math.degrees(LNG0):.4f} W), size {tile_metres(16):.0f} m')
    for l in range(10, 21):
        print(l, f'{tile_metres(l):9.1f} m/tile  {tile_metres(l) / 512:7.3f} m/px  {tile_metres(l) / 256:7.3f} m/elev-node')
