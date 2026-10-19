# Pantsir: verify the exported game mesh and skin (system Python): parse, counts, materials, labels, weights
import os
R = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
msh = open(os.path.join(R, "Meshes", "Tantra", "AstronavigatorPantsir.msh")).read().split("\n")
i = 0; assert msh[0].strip() == "MSHX1"; ng = int(msh[1].split()[1]); i = 2; groups = []
while len(groups) < ng:
    lab = mat = tex = None
    while not msh[i].startswith("GEOM"):
        k = msh[i].split()
        if k[0] == "LABEL": lab = k[1]
        elif k[0] == "MATERIAL": mat = int(k[1])
        elif k[0] == "TEXTURE": tex = int(k[1])
        i += 1
    nv, nt = map(int, msh[i].split()[1:3]); i += 1
    V = [list(map(float, msh[i + j].split())) for j in range(nv)]; i += nv
    T = [list(map(int, msh[i + j].split())) for j in range(nt)]; i += nt
    assert all(0 <= a < nv for t in T for a in t), lab
    groups.append((lab, mat, tex, nv, nt, V))
nm = int(msh[i].split()[1]); i += 1; mnames = [msh[i + j].strip() for j in range(nm)]; i += nm
while not msh[i].startswith("TEXTURES"): i += 1
ntx = int(msh[i].split()[1]); texs = [msh[i + 1 + j].strip() for j in range(ntx)]
print("msh: %d groups, %d verts, %d tris, %d materials, %d textures" % (ng, sum(g[3] for g in groups), sum(g[4] for g in groups), nm, ntx))
ys = [v[1] for g in groups for v in g[5]]; print("model y range %.4f .. %.4f (soles; origin 0.93 above the person's soles)" % (min(ys), max(ys)))
for k, n in enumerate(mnames):
    labs = [g[0] for g in groups if g[1] == k + 1]
    print("  material %2d %-10s groups: %s" % (k + 1, n, " ".join(labs)))
print("  textures: " + ", ".join(texs))
bad = [g[0] for g in groups if g[0].split("_")[0] in ("Visor", "SunShade", "ShadeArms")]
print("labels: HelmCap %d, Helm(fixed) %d, NeckSeal %d, forbidden %s" % (sum(g[0].startswith("HelmCap") for g in groups), sum(g[0].startswith("Helm") and not g[0].startswith("HelmCap") for g in groups), sum(g[0].startswith("NeckSeal") for g in groups), bad))
sk = open(os.path.join(R, "Config", "Tantra", "AstronavigatorPantsir.skin")).read().split("\n")
nb = int(sk[2].split()[1]); ref = int(open(os.path.join(R, "Config", "Tantra", "AstronavigatorSuit.skin")).read().split("\n")[2].split()[1])
bn = [sk[3 + j].split()[0] for j in range(nb)]
rb = [l.split()[0] for l in open(os.path.join(R, "Config", "Tantra", "AstronavigatorSuit.skin")).read().split("\n")[3:3 + ref]]
print("skin: %d bones (AstronavigatorSuit.skin %d), same names/order %s" % (nb, ref, bn == rb))
j = 3 + nb; assert sk[j].startswith("GROUPS"); sg = int(sk[j].split()[1]); j += 1; unw = 0; tot = 0; mism = 0
for g in range(sg):
    assert sk[j].startswith("LABEL"); j += 1; _, gi, n = sk[j].split(); n = int(n); j += 1
    if n != groups[g][3]: mism += 1
    for k in range(n):
        w = list(map(float, sk[j + k].split()[4:])); tot += 1
        if sum(w) < 0.99: unw += 1
    j += n
print("skin groups %d (msh %d), vertex count mismatches %d, vertices %d, unweighted %d" % (sg, ng, mism, tot, unw))
