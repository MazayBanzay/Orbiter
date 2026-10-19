"""Opaque "skins" draped exactly on the level-17 ground the crew walks on (what D3D9Client draws close up):
  * the summit pit + the breach channel (vessel IoLava\\Vent): lit cinder walls, an unlit lava lake and channel;
  * the active lobe (carried by IoLava\\LavaField): unlit - dark crust, red cracks, orange toes, yellow melt.
An unlit opaque surface shows its texture at full strength; the old additive layer only added a little light to the
dark ground. Every outer edge gets a skirt down into the ground, so an edge never hangs over a void.

    python skin.py vent      python skin.py lobe
"""
import os, sys, math, numpy as np
from PIL import Image
import field, glow, build
import iosite as S

BS = chr(92)
VENT_ORIGIN = (54.5, 196.0)        # flat pit floor (slope 0.01 deg) next to the pit centre: the Vent vessel stands there

def drape(x0, y1, n, cell, keep, group_of, ox, oy, href, lift, skirt):
    """grid cells [i, j] (row 0 = north edge at y1) kept by keep[i, j]; group_of[i, j] -> group name.
    -> {group: (verts, tris)} in the vessel frame (east, up, north from (ox, oy) at height href)"""
    xs = x0 + np.arange(n + 1) * cell; ys = y1 - np.arange(n + 1) * cell
    X, Y = np.meshgrid(xs, ys)
    H = build.elev_l17(X.ravel(), Y.ravel()).reshape(X.shape) + lift
    gy, gx = np.gradient(H, cell)
    nrm = np.stack([-gx, np.ones_like(H), gy], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    span = n * cell
    out, idx = {}, {}
    def vert(g, i, j, dy=0.0, tag=0):
        key = (i, j, tag)
        V, T = out.setdefault(g, ([], [])); d = idx.setdefault(g, {})
        if key not in d:
            d[key] = len(V)
            nn = nrm[i, j] if tag == 0 else (0, 1, 0)
            V.append(f'{X[i, j] - ox:.3f} {H[i, j] - href + dy:.3f} {Y[i, j] - oy:.3f} {nn[0]:.4f} {nn[1]:.4f} {nn[2]:.4f} '
                     f'{(X[i, j] - x0) / span:.5f} {(y1 - Y[i, j]) / span:.5f}')
        return d[key]
    for i in range(n):
        for j in range(n):
            if not keep[i, j]: continue
            g = group_of[i, j]
            nw, ne, sw, se = vert(g, i, j), vert(g, i, j + 1), vert(g, i + 1, j), vert(g, i + 1, j + 1)
            out[g][1].extend([f'{sw} {nw} {ne}', f'{sw} {ne} {se}'])
            # skirts on the outer edges: both faces, straight down into the ground
            for (a, b, ni, nj) in (((i, j), (i, j + 1), i - 1, j), ((i, j + 1), (i + 1, j + 1), i, j + 1),
                                   ((i + 1, j + 1), (i + 1, j), i + 1, j), ((i + 1, j), (i, j), i, j - 1)):
                if 0 <= ni < n and 0 <= nj < n and keep[ni, nj]: continue
                ta, tb = vert(g, *a), vert(g, *b)
                ba, bb = vert(g, *a, dy=-skirt, tag=1), vert(g, *b, dy=-skirt, tag=1)
                out[g][1].extend([f'{ta} {tb} {bb}', f'{ta} {bb} {ba}', f'{ta} {bb} {tb}', f'{ta} {ba} {bb}'])
    return out

def drape_clip(x0, y1, n, cell, val, ox, oy, href, lift):
    """grid draped on the level-17 ground, every triangle clipped along val == 0 (inside where val > 0): the edge
    follows the outline smoothly instead of 1 m steps. -> (verts, tris) of one group, vessel frame"""
    xs = x0 + np.arange(n + 1) * cell; ys = y1 - np.arange(n + 1) * cell
    X, Y = np.meshgrid(xs, ys)
    H = build.elev_l17(X.ravel(), Y.ravel()).reshape(X.shape) + lift
    span = n * cell
    V, T, cache = [], [], {}
    def add(px, py, ph):
        key = (round(px, 3), round(py, 3))
        if key not in cache:
            cache[key] = len(V)
            V.append(f'{px - ox:.3f} {ph - href:.3f} {py - oy:.3f} 0 1 0 {(px - x0) / span:.5f} {(y1 - py) / span:.5f}')
        return cache[key]
    def P(i, j): return (X[i, j], Y[i, j], H[i, j], val[i, j])
    for i in range(n):
        for j in range(n):
            q = [P(i, j), P(i, j + 1), P(i + 1, j + 1), P(i + 1, j)]                 # nw ne se sw
            if all(p_[3] <= 0 for p_ in q): continue
            for tri in ((q[3], q[0], q[1]), (q[3], q[1], q[2])):                       # sw nw ne, sw ne se (clockwise from above)
                poly = []
                for k in range(3):
                    a_, b_ = tri[k], tri[(k + 1) % 3]
                    if a_[3] > 0: poly.append(a_[:3])
                    if (a_[3] > 0) != (b_[3] > 0):
                        t = a_[3] / (a_[3] - b_[3])
                        poly.append(tuple(a_[m] + t * (b_[m] - a_[m]) for m in range(3)))
                if len(poly) < 3: continue
                ids = [add(*p_) for p_ in poly]
                for k in range(1, len(ids) - 1):
                    T.append(f'{ids[0]} {ids[k]} {ids[k + 1]}')
    return V, T

def write_mesh(path, groups, mats, texs):
    """groups: list of (label, material index 1.., texture index 1.., flag, verts, tris)"""
    with open(path, 'w') as f:
        f.write(f'MSHX1\nGROUPS {len(groups)}\n')
        for label, mi, ti, flag, V, T in groups:
            f.write(f'LABEL {label}\nMATERIAL {mi}\nTEXTURE {ti}\n' + (f'FLAG {flag}\n' if flag else '') + f'GEOM {len(V)} {len(T)}\n')
            f.write('\n'.join(V) + '\n' + '\n'.join(T) + '\n')
        f.write(f'MATERIALS {len(mats)}\n' + ''.join(m[0] + '\n' for m in mats))
        for name, body in mats:
            f.write(f'MATERIAL {name}\n{body}\n')
        f.write(f'TEXTURES {len(texs)}\n' + ''.join(t + '\n' for t in texs))

ROCK = '1 1 1 1\n0.2 0.2 0.2 1\n0.1 0.1 0.1 1 10\n0 0 0 1'
LAVA = '1 1 1 1\n0 0 0 1\n0 0 0 1 0\n1 1 1 1'

def lava_texture(x0, x1, y0, y1, n, floor=(0, 0, 0)):
    """glowing pixels in their glow colour (exposure), the rest the crust's own colour (dark)"""
    step = (x1 - x0) / n
    xs = x0 + (np.arange(n) + 0.5) * step; ys = y1 - (np.arange(n) + 0.5) * step
    img = np.zeros((n, n, 3), np.uint8)
    for r0 in range(0, n, 128):
        X, Y = np.meshgrid(xs, ys[r0:r0 + 128])
        o = field.evaluate(X, Y, step)
        Ys = np.zeros(X.shape); cY = np.zeros(X.shape + (3,))
        for f, T in ((1 - o['f_c'] - o['f_a'], o['Ts']), (o['f_c'], o['Tc']), (o['f_a'], o['Tax'])):
            yl = f * glow.luminance(T); Ys += yl; cY += glow.chroma(T) * yl[..., None]
        g = build.glow8(Ys, cY).astype(int)
        base = (np.clip(o['rgb'], 0, 1) * 255).astype(int)
        img[r0:r0 + 128] = np.clip(np.maximum(np.maximum(g, base), floor), 0, 255).astype(np.uint8)
    return img

def build_vent(R=None, cell=0.5, lift=0.04, skirt=8.0):
    R = R or field.R_PIT + 10
    c = field.SHIELD.copy()
    lake = field.H_SHIELD + field.H_CONE - field.D_PIT + float(build.stock_elev(*[np.array(v) for v in S.to_latlng(*c)]))
    o0 = np.array(VENT_ORIGIN)
    href = float(build.elev_l17(o0[0], o0[1])[0])   # Orbiter puts the origin on the ground under the vessel

    # box: the pit and the breach corridor (towards SSW, out to R_PIT + 62 m)
    reach = field.R_PIT + 62
    half = reach + 2
    x0, y1 = c[0] - half, c[1] + half
    n = int(2 * half / cell)
    xc = x0 + (np.arange(n) + 0.5) * cell; yc = y1 - (np.arange(n) + 0.5) * cell
    Xc, Yc = np.meshgrid(xc, yc)
    dx, dy = Xc - c[0], Yc - c[1]
    along = dx * field.BREACH[0] + dy * field.BREACH[1]; side = np.abs(-dx * field.BREACH[1] + dy * field.BREACH[0])
    corridor = (along > field.R_PIT - 4) & (along < reach) & (side < 12)
    keep = (np.hypot(dx, dy) <= R) | corridor
    o = field.evaluate(Xc, Yc, cell, want_color=False)
    Hc = build.elev_l17(Xc.ravel(), Yc.ravel()).reshape(Xc.shape)
    lava_cell = (Hc < lake + 1.0) & (np.hypot(dx, dy) <= R) | (corridor & (o['kind'] >= 5)) | (corridor & (side < 4) & (along < field.R_PIT + 40))
    group_of = np.where(lava_cell, 'lava', 'walls')
    out = drape(x0, y1, n, cell, keep, group_of, o0[0], o0[1], href, lift, skirt)
    tex = os.path.join(build.TEX, '..', 'IoLava')
    rock = field.evaluate(*np.meshgrid(np.linspace(x0, x0 + n * cell, 1024), np.linspace(y1, y1 - n * cell, 1024)), n * cell / 1024)['rgb']
    Image.fromarray((np.clip(rock, 0, 1) * 255).astype(np.uint8)).save(os.path.join(tex, 'vent_rock.dds'), pixel_format='DXT1')
    Image.fromarray(lava_texture(x0, x0 + n * cell, y1 - n * cell, y1, 1024, floor=(70, 8, 0))).save(os.path.join(tex, 'vent_lake.dds'), pixel_format='DXT1')
    write_mesh(os.path.join(build.ROOT, 'Meshes', 'IoLava', 'VentPit.msh'),
               [('walls', 1, 1, 0, *out['walls']), ('lava', 2, 2, 69, *out['lava'])],   # 69 = 0x45: no shadow, no lighting, HDR emissive (0x40, AtmoFX D3D9 fork)
               [('rock', ROCK), ('lava', LAVA)], [f'IoLava{BS}vent_rock.dds', f'IoLava{BS}vent_lake.dds'])
    la, ln = S.to_latlng(*o0)
    with open(os.path.join(build.CFG, 'Vent.cfg'), 'w', encoding='utf-8') as f:
        f.write('; Summit pit + breach lining for Modules/IoLava.dll (IoLava' + BS + 'Vent). Generated by skin.py vent.\n')
        f.write(f'POS {math.degrees(ln):.7f} {math.degrees(la):.7f}\nMESH IoLava{BS}VentPit\n')
        for e, n_ in build.TD_FLAT:
            f.write(f'TOUCHDOWN {e:.3f} {n_:.3f} 0.000\n')
    print(f'vent: walls {len(out["walls"][1])} tris, lava {len(out["lava"][1])} tris')

def build_lobe(cell=1.0, lift=0.05, tex_n=2048):
    """the active flow (all branches) as an unlit opaque skin on the LavaField vessel (origin: the anchor on the
    ground), clipped exactly along the flow outline; it lies on the ground, so no skirts"""
    a = build.ANCHOR.copy()
    h0 = float(build.elev_l17(a[0], a[1])[0])
    # the box around the whole active flow below the cone (the cone's channel is the Vent lining)
    x0, x1, y0, y1 = -235.0, 70.0, -235.0, 70.0
    n = int((x1 - x0) / cell)
    y1 = y0 + n * cell
    xs = x0 + np.arange(n + 1) * cell; ys = y1 - np.arange(n + 1) * cell
    X, Y = np.meshgrid(xs, ys)
    val = np.zeros(X.shape)
    for r0 in range(0, n + 1, 64):
        val[r0:r0 + 64] = field.evaluate(X[r0:r0 + 64], Y[r0:r0 + 64], cell, want_color=False)['active'] - 0.5
    # leave the cone to the pit lining: nothing inside 140 m of the summit
    val = np.where(np.hypot(X - field.SHIELD[0], Y - field.SHIELD[1]) < 140, -1.0, val)
    V, T = drape_clip(x0, y1, n, cell, val, a[0], a[1], h0, lift)
    tex = os.path.join(build.TEX, '..', 'IoLava')
    Image.fromarray(lava_texture(x0, x0 + n * cell, y1 - n * cell, y1, tex_n)).save(os.path.join(tex, 'lobe_skin.dds'), pixel_format='DXT1')
    write_mesh(os.path.join(build.ROOT, 'Meshes', 'IoLava', 'LobeSkin.msh'),
               [('lobe', 1, 1, 69, V, T)], [('lava', LAVA)], [f'IoLava{BS}lobe_skin.dds'])
    p = os.path.join(build.CFG, 'Effects.cfg')
    L = [l for l in open(p, encoding='utf-8').read().splitlines() if not l.startswith('SKIN_MESH')]
    L.append(f'SKIN_MESH IoLava{BS}LobeSkin')
    open(p, 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    print(f'lobe skin: {len(T)} tris, {len(V)} vertices')

if __name__ == '__main__':
    if 'vent' in sys.argv: build_vent()
    if 'lobe' in sys.argv: build_lobe()
