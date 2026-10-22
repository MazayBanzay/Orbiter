# The vehicles' dust bodies for the D3D11 client's vacuum soil (src\Regolith.cpp): for each vehicle in
# Config\D3D11Client\DustBodies.json a distance field round its mesh's static parts (all groups but the turning wheels and
# what the dust cannot reach), in the vessel's own frame (x right, y up, z forward), and its wheels (hubs, radius, width).
# The client loads it by the vessel's class name; the dust thrown by any vehicle bounces off every vehicle that has one.
#   python _src\d3d11client\tools\build_dust_sdf.py            (all of them)
# The file (Meshes\...\<name>.dust): "RDF1", int32 nx ny nz, float min xyz, cell, band, int32 wheels, float R, W,
# float hubs[wheels][3], then nx*ny*nz bytes (the distance / band x 255, x fastest).
import json, os, re, struct, sys
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))


def parse(path):
    L = open(path, encoding='latin-1').read().split('\n')
    i, groups = 0, []
    while i < len(L):
        s = L[i].strip()
        if s.startswith('GROUPS'):
            i += 1
            label = ''
            while i < len(L):
                s = L[i].strip()
                if s.startswith('MATERIALS'): break
                if s.startswith('LABEL'): label = s[6:].strip()
                elif s.startswith('GEOM'):
                    nv, nt = map(int, s.split()[1:3])
                    V = np.array([[float(x) for x in L[i + 1 + k].split()[:3]] for k in range(nv)])
                    T = np.array([[int(x) for x in L[i + 1 + nv + k].split()[:3]] for k in range(nt)])
                    groups.append((label, V, T))
                    i += nv + nt
                    label = ''
                i += 1
            continue
        i += 1
    return groups


def pt_tri_dist(P, a, b, c):
    """Distances from the points P (n x 3) to the triangle abc (Ericson, Real-Time Collision Detection 5.1.5), vectorized."""
    ab, ac, ap = b - a, c - a, P - a
    d1, d2 = ap @ ab, ap @ ac
    bp = P - b; d3, d4 = bp @ ab, bp @ ac
    cp = P - c; d5, d6 = cp @ ab, cp @ ac
    va = d3 * d6 - d5 * d4; vb = d5 * d2 - d1 * d6; vc = d1 * d4 - d3 * d2
    denom = va + vb + vc
    with np.errstate(divide='ignore', invalid='ignore'):
        v = vb / denom; w = vc / denom
    Q = a + np.outer(v, ab) + np.outer(w, ac)
    t = np.clip(d1 / np.where(d1 - d3 == 0, 1, d1 - d3), 0, 1); m = (vc <= 0) & (d1 >= 0) & (d3 <= 0); Q[m] = (a + np.outer(t, ab))[m]
    t = np.clip(d2 / np.where(d2 - d6 == 0, 1, d2 - d6), 0, 1); m = (vb <= 0) & (d2 >= 0) & (d6 <= 0); Q[m] = (a + np.outer(t, ac))[m]
    den = (d4 - d3) + (d5 - d6); t = np.clip((d4 - d3) / np.where(den == 0, 1, den), 0, 1)
    m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0); Q[m] = (b + np.outer(t, c - b))[m]
    m = (d1 <= 0) & (d2 <= 0); Q[m] = a
    m = (d3 >= 0) & (d4 <= d3); Q[m] = b
    m = (d6 >= 0) & (d5 <= d6); Q[m] = c
    return np.linalg.norm(P - Q, axis=1)


def field(V, T, cell, band):
    lo = V.min(0) - band; hi = V.max(0) + band
    dims = np.ceil((hi - lo) / cell).astype(int) + 1
    D = np.full(dims, band, dtype=np.float32)
    for (i, j, k) in T:
        a, b, c = V[i], V[j], V[k]
        tlo = np.floor((np.minimum(np.minimum(a, b), c) - band - lo) / cell).astype(int).clip(0, dims - 1)
        thi = np.ceil((np.maximum(np.maximum(a, b), c) + band - lo) / cell).astype(int).clip(0, dims - 1)
        g = np.meshgrid(*[np.arange(tlo[q], thi[q] + 1) for q in range(3)], indexing='ij')
        idx = np.stack([x.ravel() for x in g], 1)
        d = pt_tri_dist(lo + idx * cell, a, b, c)
        D[idx[:, 0], idx[:, 1], idx[:, 2]] = np.minimum(D[idx[:, 0], idx[:, 1], idx[:, 2]], d)
    return lo, dims, D


def build(name, spec):
    groups = parse(os.path.join(ROOT, spec['mesh']))
    skip = re.compile(spec['skip'])
    V, T, n = [], [], 0
    for (label, gv, gt) in groups:
        if skip.match(label): continue
        V.append(gv); T.append(gt + n); n += len(gv)
    V, T = np.concatenate(V), np.concatenate(T)
    cell, band = spec.get('cell', 0.08), spec.get('band', 0.32)
    lo, dims, D = field(V, T, cell, band)
    w = spec['wheels']
    out = os.path.join(ROOT, spec['out'])
    with open(out, 'wb') as f:
        f.write(b'RDF1')
        f.write(struct.pack('<3i', *dims.tolist()))
        f.write(struct.pack('<5f', *lo.tolist(), cell, band))
        f.write(struct.pack('<i2f', len(w['hubs']), w['R'], w['W']))
        for h in w['hubs']: f.write(struct.pack('<3f', *h))
        f.write((np.clip(D / band, 0, 1) * 255).round().astype(np.uint8).transpose(2, 1, 0).tobytes())   # x fastest
    print(f'{name}: {len(T)} triangles, field {dims.tolist()} of {cell} m -> {spec["out"]}')


if __name__ == '__main__':
    cfg = json.load(open(os.path.join(ROOT, 'Config', 'D3D11Client', 'DustBodies.json'), encoding='utf-8'))
    for name, spec in cfg.items():
        if len(sys.argv) > 1 and name not in sys.argv[1:]: continue
        build(name, spec)
