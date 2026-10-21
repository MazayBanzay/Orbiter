# «Панцирь»: after export_skin.py - clear the helmet window. The frame and bezel sheets (wrapped_sheet with the window as
# a hole) triangulate across the hole at its chamfered corners: big triangles in front of the glass, seen as an "X" over
# the visor. Drop every cap face (except the glass itself and the lenses) inside the window opening and in front of the
# glass. Window opening in the Orbiter model frame (rest pose): |x| < 0.078, 0.678 < y < 0.742, glass at z ~0.187
import sys
P = sys.argv[1] if len(sys.argv) > 1 else r"C:\Games\Orbiter-2024\Meshes\Tantra\AstronavigatorPantsir.msh"
raw = open(P, encoding="ascii", newline="").read(); nl = "\r\n" if "\r\n" in raw else "\n"; L = raw.split(nl)
# the opening from the glass itself (its outer faces): its box shrunk 4 mm, and the glass's front plane
gl = []; j = 0; lb = ""
while j < len(L):
    t = L[j]
    if t.startswith("LABEL"): lb = t.split()[1] if len(t.split()) > 1 else ""
    if t.startswith("GEOM"):
        nv_, nt_ = map(int, t.split()[1:3])
        if lb == "HelmCapGd": gl = [list(map(float, v.split()[:6])) for v in L[j + 1:j + 1 + nv_] if float(v.split()[5]) > 0.5]
        j += nv_ + nt_
    j += 1
X0, X1 = min(v[0] for v in gl) + 0.004, max(v[0] for v in gl) - 0.004
Y0, Y1 = min(v[1] for v in gl) + 0.004, max(v[1] for v in gl) - 0.004
ZG = max(v[2] for v in gl) + 0.002
print("window opening x %.3f..%.3f y %.3f..%.3f, glass front z %.3f" % (X0, X1, Y0, Y1, ZG))
out = []; i = 0; lab = ""; dropped = 0
while i < len(L):
    s = L[i]
    if s.startswith("LABEL"): lab = s.split()[1] if len(s.split()) > 1 else ""
    if s.startswith("GEOM") and lab.startswith("HelmCap") and lab not in ("HelmCapGd", "HelmCapSl") and not lab.startswith(("HelmCapLv", "HelmCapLu")):
        nv, nt = map(int, s.split()[1:3]); V = L[i + 1:i + 1 + nv]; F = L[i + 1 + nv:i + 1 + nv + nt]
        P3 = [list(map(float, v.split()[:3])) for v in V]; keep = []
        for f in F:
            c = [sum(P3[int(x)][j] for x in f.split()) / 3 for j in range(3)]
            # in the opening: in front of the glass (the frame's triangles across it) and behind it down to 45 mm (the inner
            # shell's wall, built without the window's hole - a grille seen through the glass and from inside)
            if X0 < c[0] < X1 and Y0 < c[1] < Y1 and (c[2] > ZG or c[2] > ZG - 0.045): dropped += 1; continue
            keep.append(f)
        out.append("GEOM %d %d" % (nv, len(keep))); out += V + keep; i += 1 + nv + nt; continue
    out.append(s); i += 1
open(P, "w", encoding="ascii", newline="").write(nl.join(out)); print("window cleared: %d faces dropped" % dropped)
