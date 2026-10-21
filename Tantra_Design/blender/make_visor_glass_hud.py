# The helmet glass's HUD layer for OrbiterCrew (SuitHud's «Буран» MFDs drawn on the visor itself, 2026-10-20).
# Takes the visor group (VisorB) of the suit mesh and writes Meshes\Tantra\VisorGlassHud.msh: the same glass, moved
# 1.5 mm in towards the eye, with texture coordinates seen from the eye - u by the yaw, v by the pitch - the same angular
# layout SuitHud draws its glass canvas in (GLASS_YW, GLASS_PT, GLASS_PB in SuitHud.cpp: change both together).
# Two groups, the same geometry: 0 seen from inside (full light), 1 seen from outside (dim); the module hides one or the
# other by the view. Vertices are in the suit's rest frame; the module carries them with the helmet (bone Spine1).
#   python make_visor_glass_hud.py [suit mesh] [eye x y z]
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'Meshes', 'Tantra', 'AstronavigatorSuit2.msh')
EYE = tuple(float(a) for a in sys.argv[2:5]) if len(sys.argv) >= 5 else (0.0, 0.700, 0.150)
OUT = os.path.join(ROOT, 'Meshes', 'Tantra', 'VisorGlassHud.msh')
YW, PT, PB = math.radians(105), math.radians(50), math.radians(-72)   # down to the window's bottom (-70)
INSET = 0.0015


def visor_group(path):
    lines = open(path, encoding='latin-1').read().split('\n')
    label, i = '', 0
    while i < len(lines):
        l = lines[i].strip()
        if l.startswith('LABEL'):
            label = l[5:].strip()
        if l.startswith('GEOM'):
            nv, nt = map(int, l.split()[1:3])
            if label == 'VisorB':
                vs = [list(map(float, lines[i + 1 + k].split()[:6])) for k in range(nv)]
                ts = [list(map(int, lines[i + 1 + nv + k].split()[:3])) for k in range(nt)]
                return vs, ts
            i += nv + nt
        i += 1
    raise SystemExit('no VisorB group in ' + path)


vs, ts = visor_group(SRC)
rows = []
for x, y, z, nx, ny, nz in vs:
    dx, dy, dz = x - EYE[0], y - EYE[1], z - EYE[2]
    d = math.sqrt(dx * dx + dy * dy + dz * dz)
    k = (d - INSET) / d
    px, py, pz = EYE[0] + dx * k, EYE[1] + dy * k, EYE[2] + dz * k
    yaw, pitch = math.atan2(dx, dz), math.atan2(dy, math.hypot(dx, dz))
    u, v = 0.5 + yaw / (2 * YW), (PT - pitch) / (PT - PB)
    u, v = min(max(u, 0.002), 0.998), min(max(v, 0.002), 0.998)   # outside the drawn window: its black edge (no wrap)
    rows.append('%.5f %.5f %.5f %.4f %.4f %.4f %.5f %.5f' % (px, py, pz, -nx, -ny, -nz, u, v))
tris = ['%d %d %d' % (a, c, b) for a, b, c in ts] + ['%d %d %d' % (a, b, c) for a, b, c in ts]   # both faces

with open(OUT, 'w', encoding='ascii', newline='\r\n') as f:
    f.write('MSHX1\nGROUPS 2\n')
    for name, mat in (('GlassHudIn', 1), ('GlassHudOut', 2)):
        f.write('LABEL %s\nMATERIAL %d\nTEXTURE 1\nFLAG 5\nGEOM %d %d\n' % (name, mat, len(rows), len(tris)))
        f.write('\n'.join(rows) + '\n' + '\n'.join(tris) + '\n')
    f.write('MATERIALS 2\nGlassHudIn\nGlassHudOut\n')
    f.write('MATERIAL GlassHudIn\n1 1 1 1\n0 0 0 1\n0 0 0 1 0\n1 1 1 1\n')
    f.write('MATERIAL GlassHudOut\n0.85 0.85 0.85 1\n0 0 0 1\n0 0 0 1 0\n0.85 0.85 0.85 1\n')
    f.write('TEXTURES 1\nTantra\\VisorClear.dds\n')
print('%s: %d vertices, %d triangles a group' % (OUT, len(rows), len(tris)))
