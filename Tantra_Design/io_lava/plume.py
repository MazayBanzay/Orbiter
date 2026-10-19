"""The Prometheus plume: a ballistic SO2 gas-and-frost umbrella over the western end of the flow field.

Physics:
  * no air: every grain flies a parabola. Grains leaving a source at one speed v fill the "safety paraboloid"
        z = H - r^2 / (4 H),  H = v^2 / (2 g)   (envelope of all trajectories), reaching the ground at r = 2H.
    Prometheus: H ~ 75-100 km (Voyager/Galileo), deposition ring ~250-300 km across. H = 80 km -> v ~ 540 m/s, r = 160 km.
  * material concentrates near the envelope (the canopy) and in the column over the source;
  * optically thin (normal optical depth ~0.01-0.05): it adds scattered sunlight, it hides almost nothing -> additive;
  * small grains scatter mostly forward (Henyey-Greenstein g ~ 0.5): bright looking towards the Sun, faint away from it;
  * a part of the plume is lit only when the Sun reaches its height: sunlit above h if the Sun is higher than
    -acos(R / (R + h)) at the source - at dawn the canopy lights up before the ground.
Source: the Galileo-era plume sat at the western end of the flow field, ~85 km west of the Voyager-era vent at the
patera: here 1.6 S, 155.8 W (+-0.3 deg; Galileo located it only to tens of km).

Writes Meshes/IoLava/Plume.msh, Textures/IoLava/plume_shell.dds, plume_column.dds, Config/Io/LavaField/Plume.cfg.
"""
import os, math, numpy as np
from PIL import Image
from noise import fbm, perlin
import iosite as S
import build

R_IO = S.R_IO
SRC_LNG, SRC_LAT = -155.8, -1.6
H = 80e3                     # canopy height [m]
RR = 2 * H                   # ground range of the envelope [m]
R_COL0, R_COL1, H_COL = 3e3, 14e3, 70e3   # column: radius at the vent, at its top; height
MESH_SCALE = 0.05            # the mesh is drawn 20x smaller around the anchor: the same angles from the site, but within
                             # the renderer's far plane (a 70-230 km mesh was not drawn); lighting uses the true positions
BANDS = [0, 10e3, 20e3, 35e3, 50e3, 65e3, 80e3]   # height bands lit separately by the Sun

def drop(r):                 # Io's surface falls away from the source's tangent plane
    return r * r / (2 * R_IO)

def tex_shell(n=512):
    """u = azimuth (repeats 4x), v = from the top (0) to the rim (1): streaks along the trajectories, denser near the
    rim where the line of sight grazes the canopy"""
    y, x = np.mgrid[0:n, 0:n] + 0.5
    u = x / n; v = y / n
    streak = 0.55 + 0.45 * fbm(u * 40, v * 3, 1.0, 0.02, 4, seed=601)            # long along v, thin across u
    clump = 0.7 + 0.3 * fbm(u * 8, v * 8, 1.0, 0.02, 4, seed=602)
    prof = 0.35 + 0.65 * v ** 1.5                                                 # rim brighter (grazing column)
    prof *= np.clip((1 - v) * 12, 0, 1)                                           # fades out at the ground
    a = np.clip(streak * clump * prof, 0, 1)
    col = np.array([0.80, 0.86, 1.0])                                             # small SO2 grains: bluish white
    return (np.clip(a[..., None] * col, 0, 1) * 255 + 0.5).astype(np.uint8)

def tex_column(n=256):
    y, x = np.mgrid[0:n, 0:n] + 0.5
    u = x / n; v = y / n                                                          # v: top (0) .. vent (1)
    streak = 0.5 + 0.5 * fbm(u * 30, v * 2, 1.0, 0.02, 4, seed=611)
    a = streak * (0.35 + 0.65 * v) * np.clip(v * 6, 0, 1)                          # dense low, thinning up
    col = np.array([0.85, 0.88, 1.0])
    return (np.clip(a[..., None] * col, 0, 1) * 255 + 0.5).astype(np.uint8)

def shell_mesh(nr=28, na=48):
    """paraboloid shell, both faces, split into height bands -> list of (label, verts, tris)"""
    rs = np.linspace(0, RR, nr + 1) ** 1.0
    groups = []
    for b0, b1 in zip(BANDS[:-1], BANDS[1:]):
        verts, tris = [], []
        # radii where the envelope is between heights b0 and b1
        r_lo = 2 * math.sqrt(max(H * (H - b1), 0)); r_hi = 2 * math.sqrt(max(H * (H - b0), 0))
        rr = np.linspace(r_lo, r_hi, max(3, int(nr * (r_hi - r_lo) / RR) + 2))
        for i, r in enumerate(rr):
            z = H - r * r / (4 * H) - drop(r)
            for j in range(na + 1):
                a = 2 * math.pi * j / na
                x, y, zz = to_mesh(r * math.sin(a), z, r * math.cos(a), FR)
                verts.append(f'{x:.1f} {y:.1f} {zz:.1f} 0 1 0 {4 * j / na:.4f} {r / RR:.4f}')
        w = na + 1
        for i in range(len(rr) - 1):
            for j in range(na):
                p, q, s, t = i * w + j, i * w + j + 1, (i + 1) * w + j, (i + 1) * w + j + 1
                tris += [f'{p} {s} {q}', f'{q} {s} {t}', f'{p} {q} {s}', f'{q} {t} {s}']   # both faces
        groups.append((f'shell_{int(b0 / 1e3)}_{int(b1 / 1e3)}', verts, tris, (b0 + b1) / 2))
    return groups

def column_mesh(nh=8, na=24):
    verts, tris = [], []
    hs = np.linspace(0, H_COL, nh + 1)
    for i, h in enumerate(hs):
        r = R_COL0 + (R_COL1 - R_COL0) * (h / H_COL) ** 0.7
        for j in range(na + 1):
            a = 2 * math.pi * j / na
            x, y, zz = to_mesh(r * math.sin(a), h, r * math.cos(a), FR)
            verts.append(f'{x:.1f} {y:.1f} {zz:.1f} 0 1 0 {3 * j / na:.4f} {1 - h / H_COL:.4f}')
    w = na + 1
    for i in range(nh):
        for j in range(na):
            p, q, s, t = i * w + j, i * w + j + 1, (i + 1) * w + j, (i + 1) * w + j + 1
            tris += [f'{p} {s} {q}', f'{q} {s} {t}', f'{p} {q} {s}', f'{q} {t} {s}']
    return [('column', verts, tris, H_COL / 2)]

def _frames():
    """unit vectors (east, north, up) and ground positions of the plume source and the LavaField anchor (sphere)"""
    import field
    def enu(lat, lng):
        U = np.array([math.cos(lat) * math.cos(lng), math.cos(lat) * math.sin(lng), math.sin(lat)])
        E = np.array([-math.sin(lng), math.cos(lng), 0.0])
        N = np.cross(U, E)
        return E, N, U
    lat_s, lng_s = math.radians(SRC_LAT), math.radians(SRC_LNG)
    h_s = float(build.stock_elev(np.array(lat_s), np.array(lng_s)))
    la_a, ln_a = S.to_latlng(*field.FAN)
    h_a = float(build.elev_l17(field.FAN[0], field.FAN[1])[0])
    Es, Ns, Us = enu(lat_s, lng_s); Ea, Na, Ua = enu(la_a, ln_a)
    return (Es, Ns, Us, (R_IO + h_s) * Us), (Ea, Na, Ua, (R_IO + h_a) * Ua)

def to_anchor(e, u, n, fr):
    """a point given in the source's local frame (east, up, north) -> the LavaField vessel frame (east, up, north)"""
    (Es, Ns, Us, Ps), (Ea, Na, Ua, Pa) = fr
    P = Ps + e * Es + n * Ns + u * Us - Pa
    return P @ Ea, P @ Ua, P @ Na

def to_mesh(e, u, n, fr):
    x, y, z = to_anchor(e, u, n, fr)
    return MESH_SCALE * x, MESH_SCALE * y, MESH_SCALE * z

def build_plume():
    global FR
    FR = _frames()
    root = build.ROOT
    os.makedirs(os.path.join(root, 'Meshes', 'IoLava'), exist_ok=True)
    os.makedirs(os.path.join(root, 'Textures', 'IoLava'), exist_ok=True)
    Image.fromarray(tex_shell()).save(os.path.join(root, 'Textures', 'IoLava', 'plume_shell.dds'), pixel_format='DXT1')
    Image.fromarray(tex_column()).save(os.path.join(root, 'Textures', 'IoLava', 'plume_column.dds'), pixel_format='DXT1')
    groups = shell_mesh() + column_mesh()
    bs = chr(92)
    with open(os.path.join(root, 'Meshes', 'IoLava', 'Plume.msh'), 'w') as f:
        f.write(f'MSHX1\nGROUPS {len(groups)}\n')
        for gi, (name, verts, tris, _) in enumerate(groups):
            tex = 2 if name == 'column' else 1
            # 13 = no shadow + no lighting + additive: an optically thin, sunlit haze adds light, hides little
            f.write(f'LABEL {name}\nMATERIAL {gi + 1}\nTEXTURE {tex}\nFLAG 13\nGEOM {len(verts)} {len(tris)}\n')
            f.write('\n'.join(verts) + '\n' + '\n'.join(tris) + '\n')
        f.write(f'MATERIALS {len(groups)}\n' + ''.join(f'{g[0]}\n' for g in groups))   # one per band: lit separately
        for g in groups:
            f.write(f'MATERIAL {g[0]}\n1 1 1 1\n0 0 0 1\n0 0 0 1 0\n1 1 1 1\n')
        f.write(f'TEXTURES 2\nIoLava{bs}plume_shell.dds\nIoLava{bs}plume_column.dds\n')
    # The plume mesh is the SECOND mesh of the IoLava\LavaField vessel (its frame: east, up, north at the anchor).
    # A separate 160-km plume vessel made Orbiter's core serve the whole area from coarse (level-10) elevation:
    # the crew then walked on the old smooth Io under the drawn relief.
    lines = [f'PLUME_PRM {1.0} {0.5} {3.0} {R_IO:.0f}']
    for gi, g in enumerate(groups):
        x, y, z = to_anchor(0.0, g[3], 0.0, FR)                # band centre over the vent, in the vessel frame
        lines.append(f'PLUME_BAND {gi} {g[3]:.0f} {x:.1f} {y:.1f} {z:.1f}')
    cfgp = os.path.join(build.CFG, 'Effects.cfg')
    keep = [l for l in open(cfgp, encoding='utf-8').read().splitlines() if not l.startswith('PLUME_')]
    open(cfgp, 'w', encoding='utf-8').write('\n'.join(keep + lines) + '\n')
    nt = sum(len(g[2]) for g in groups)
    vx, vy, vz = to_anchor(0.0, 0.0, 0.0, FR)
    print(f'plume: {len(groups)} groups, {nt} triangles; vent at {vx / 1e3:.1f} km E, {vz / 1e3:.1f} km N, {vy:.0f} m up of the anchor')
    print('\n'.join(lines))

if __name__ == '__main__':
    build_plume()


# ---------------------------------------------------------------- the canopy as Io's cloud layer
CLOUD_ALT = 60e3             # one layer height for the umbrella (canopy 50-80 km; Config/Io.cfg CloudAlt)
CLOUD_MAX_LVL = 10           # 10: 175 m/px over the canopy

def canopy_rgba(lat, lng):
    """the umbrella seen along the vertical: RGBA (alpha = how much of the sky it covers) for arrays of lat/lng [rad]"""
    lat_s, lng_s = math.radians(SRC_LAT), math.radians(SRC_LNG)
    # great-circle distance and bearing from the vent
    d = np.arccos(np.clip(np.sin(lat) * math.sin(lat_s) + np.cos(lat) * math.cos(lat_s) * np.cos(lng - lng_s), -1, 1))
    r = d * R_IO / 1e3                                                     # km
    brg = np.arctan2(np.sin(lng - lng_s) * np.cos(lat), math.cos(lat_s) * np.sin(lat) - math.sin(lat_s) * np.cos(lat) * np.cos(lng - lng_s))
    Hk, Rk = H / 1e3, RR / 1e3
    shell = 0.10 * np.sqrt(1 + (r / (2 * Hk)) ** 2)                        # uniform shell, seen vertically: denser outwards
    rays = 0.65 + 0.7 * np.clip(fbm(np.cos(brg) * 30, np.sin(brg) * 30 + r * 0.02, 1.0, 0.01, 4, seed=701) + 0.5, 0, 1)
    patch = 0.75 + 0.5 * np.clip(fbm(r * np.cos(brg), r * np.sin(brg), 40.0, 0.5, 4, seed=702) + 0.5, 0, 1)
    rim = 0.14 * np.exp(-((r - 0.93 * Rk) / 10.0) ** 2)                    # curtain of falling frost over the deposit ring
    col = 0.40 * np.clip(1 - r / 16.0, 0, 1) ** 0.6                        # the dense column over the vent
    a = (shell * rays * patch + rim) * np.clip((Rk + 4 - r) / 8, 0, 1) + col
    a = np.clip(a, 0, 0.6)
    rgba = np.zeros(lat.shape + (4,), np.uint8)
    rgba[..., 0] = 222; rgba[..., 1] = 230; rgba[..., 2] = 255                   # small SO2 grains: bluish white
    rgba[..., 3] = (a * 255 + 0.5).astype(np.uint8)
    return rgba

def build_cloud():
    root = os.path.join(build.ROOT, 'Textures', 'Io', 'Cloud')
    def save(L, a, b, rgba):
        d = os.path.join(root, f'{L:02d}', f'{a:06d}'); os.makedirs(d, exist_ok=True)
        Image.fromarray(rgba).save(os.path.join(d, f'{b:06d}.dds'), pixel_format='DXT5')
    # levels 1-3: whole-planet maps; level 4: the two hemispheres (the quadtree roots are required)
    for L, n in ((1, 128), (2, 256), (3, 512)):
        f = (np.arange(n) + 0.5) / n
        LNG, LAT = np.meshgrid(-math.pi + f * 2 * math.pi, math.pi / 2 - f * math.pi)
        save(L, 0, 0, canopy_rgba(LAT, LNG))
    for b in (0, 1):
        LAT, LNG = S.surf_grid(4, 0, b, 512); save(4, 0, b, canopy_rgba(LAT, LNG))
    n = 0
    lat_s, lng_s = math.radians(SRC_LAT), math.radians(SRC_LNG)
    span = (RR + 10e3) / R_IO
    for L in range(5, CLOUD_MAX_LVL + 1):
        a0, b0 = S.tile_of(L, lat_s + span, lng_s - span / math.cos(lat_s))
        a1, b1 = S.tile_of(L, lat_s - span, lng_s + span / math.cos(lat_s))
        for a in range(a0, a1 + 1):
            for b in range(b0, b1 + 1):
                LAT, LNG = S.surf_grid(L, a, b, 512)
                save(L, a, b, canopy_rgba(LAT, LNG)); n += 1
    print(f'cloud layer: levels 1-4 + {n} canopy tiles (levels 5-{CLOUD_MAX_LVL}) in Textures/Io/Cloud')

if __name__ == '__main__' and 'cloud' in __import__('sys').argv:
    build_cloud()
