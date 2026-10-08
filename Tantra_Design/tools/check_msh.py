# Checks an Orbiter .msh for junk geometry: degenerate triangles, edges longer than the vehicle, vertices outside its
# box, indices out of range, NaN.  python check_msh.py Meshes\MPU\EMPU.msh [box_x box_y_lo box_y_hi box_z]  -> exit 1 on a fault
import math, sys

def check(path, bx=2.6, by0=-1.2, by1=3.8, bz=5.2, maxEdge=6.0):
    L = open(path, encoding="ascii", errors="replace").read().split("\n")
    i = 0; faults = 0; groups = 0; label = ""
    while i < len(L):
        t = L[i].strip()
        if t.startswith("LABEL"): label = t[6:]
        if t.startswith("GEOM"):
            nv, nf = map(int, t.split()[1:3]); groups += 1
            V = [list(map(float, L[i + 1 + k].split()[:3])) for k in range(nv)]
            F = [list(map(int, L[i + 1 + nv + k].split()[:3])) for k in range(nf)]
            for k, (x, y, z) in enumerate(V):
                if any(math.isnan(c) for c in (x, y, z)) or abs(x) > bx or y < by0 or y > by1 or abs(z) > bz:
                    print("%s: vertex %d out of the box: %.3f %.3f %.3f" % (label, k, x, y, z)); faults += 1
            for k, (a, b, c) in enumerate(F):
                if max(a, b, c) >= nv or min(a, b, c) < 0:
                    print("%s: face %d index out of range" % (label, k)); faults += 1; continue
                P = [V[a], V[b], V[c]]
                e = [math.dist(P[0], P[1]), math.dist(P[1], P[2]), math.dist(P[2], P[0])]
                ux, uy, uz = (P[1][j] - P[0][j] for j in range(3)); vx, vy, vz = (P[2][j] - P[0][j] for j in range(3))
                area = 0.5 * math.sqrt((uy * vz - uz * vy) ** 2 + (uz * vx - ux * vz) ** 2 + (ux * vy - uy * vx) ** 2)
                if max(e) > maxEdge: print("%s: face %d edge %.2f m" % (label, k, max(e))); faults += 1
                elif area < 1e-9 and max(e) > 1e-4: print("%s: face %d degenerate (area 0, edge %.3f)" % (label, k, max(e))); faults += 1
            i += 1 + nv + nf; continue
        i += 1
    print("%s: %d groups, %d faults" % (path, groups, faults))
    return faults

if __name__ == "__main__":
    a = [float(x) for x in sys.argv[2:6]] if len(sys.argv) >= 6 else []
    sys.exit(1 if check(sys.argv[1], *a) else 0)
