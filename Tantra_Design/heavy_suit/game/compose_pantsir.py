# «Панцирь» rev. S: compose the game maps from the Cycles bakes (uvbake_pantsir.py UVBAKE_STAGE=bake -> tex\bake\*.npz)
#   python compose_pantsir.py  -> game\tex\pz_<atlas>[_rghn|_metl|_norm|_heat|_emis].png
#                                 + Textures\Tantra\AstronavigatorPantsir\pz_*.dds
# Per texel: the material (bake B.z = id / 16), the ambient occlusion (A.x 10 cm, A.y 1.5 cm cavity), the bevel edge mask
# (B.x), the rest-pose height (B.y) and three world-space noises (C) drive:
#   albedo   the material's colour x occlusion; chipped enamel on worn edges down to the metal under it; grime in cavities;
#            dust up the boots and shins; the stencils (S: r red paint, g dark, b white) painted on, worn with the enamel
#   _rghn    smoothness (G): the material's, rougher with grime and dust, chips like bare metal
#   _metl    metalness: the material's, chips 1
#   _norm    the 4 mm bevel normal bake (tangent space, DirectX green)
#   _heat/_emis (borazon, sole, radiator atlases): the incandescence maps the DLL's glow uses, by material
import os, numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); TEX = os.path.join(HERE, "tex"); BAKE = os.path.join(TEX, "bake")
DDS = os.path.abspath(os.path.join(HERE, "..", "..", "..", "Textures", "Tantra", "AstronavigatorPantsir"))
ATLAS = ["pz_shell", "pz_frame", "pz_boraz", "pz_sole", "pz_rad", "pz_visor"]
MID = {"Enamel": 1, "Red": 2, "Rubber": 3, "Graphite": 4, "Metal": 5, "Borazon": 6, "Mirror": 7, "Sole": 8, "Rad": 9, "GoldVisor": 10, "Slit": 11}
# colour (sRGB), smoothness, metalness, heat, emis (0..1) - the colours and values of the tiling maps they replace
MAT = {"Enamel": ((176, 180, 178), 0.40, 0.00, 0, 0), "Red": ((181, 30, 16), 0.36, 0.00, 0, 0),
       "Rubber": ((14, 15, 14), 0.10, 0.00, 0, 0), "Graphite": ((66, 70, 75), 0.38, 0.30, 0, 0),
       "Metal": ((186, 190, 196), 0.62, 1.00, 0, 0), "Borazon": ((116, 34, 6), 0.72, 0.00, 224 / 255, 194 / 255),
       "Mirror": ((186, 158, 118), 0.86, 0.92, 0, 0), "Sole": ((60, 60, 58), 0.14, 0.00, 197 / 255, 182 / 255),
       "Rad": ((150, 154, 160), 0.40, 0.61, 177 / 255, 177 / 255), "GoldVisor": ((196, 136, 58), 0.97, 0.78, 0, 0),
       "Slit": ((8, 8, 16), 0.90, 0.00, 0, 0)}
BARE = np.array((168, 170, 174)) / 255.0; GRIME = np.array((58, 54, 48)) / 255.0; DUST = np.array((150, 128, 100)) / 255.0
PAINT = {0: np.array((176, 28, 18)) / 255.0, 1: np.array((28, 28, 30)) / 255.0, 2: np.array((228, 228, 222)) / 255.0}
WEARS = ("Enamel", "Red")                        # painted: chips to bare metal
DIRTY = ("Enamel", "Red", "Graphite", "Metal", "Rubber", "Sole", "Rad", "Borazon", "Mirror")


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)


def load(atlas, pas):
    return np.flipud(np.load(os.path.join(BAKE, "%s_%s.npz" % (atlas, pas)))["a"].astype(np.float32))   # top row first


def save(arr, name, dds_fmt):
    im = Image.fromarray(np.clip(arr * 255 + 0.5, 0, 255).astype(np.uint8))
    im.save(os.path.join(TEX, name + ".png"))
    if dds_fmt: im.save(os.path.join(DDS, name + ".dds"), pixel_format=dds_fmt)
    else: im.convert("RGBA").save(os.path.join(DDS, name + ".dds"))


for atlas in ATLAS:
    A, B, C, N = load(atlas, "A"), load(atlas, "B"), load(atlas, "C"), load(atlas, "N")
    S = load(atlas, "S") if os.path.exists(os.path.join(BAKE, atlas + "_S.npz")) else np.zeros_like(A)
    h, w = A.shape[:2]; mid = np.rint(B[..., 2] * 16).astype(int)
    alb = np.zeros((h, w, 3), np.float32); smo = np.zeros((h, w), np.float32); met = np.zeros((h, w), np.float32)
    heat = np.zeros((h, w), np.float32); emis = np.zeros((h, w), np.float32)
    ao, cav, edge, z = A[..., 0], A[..., 1], np.clip(B[..., 0] * 8, 0, 1), B[..., 1]
    nf, nm, nl = C[..., 0], C[..., 1], C[..., 2]
    occl = (0.62 + 0.38 * ao) * (0.80 + 0.20 * cav)   # contact shadows, not soot
    grime = np.clip((1 - cav) * 0.9 + (1 - ao) * 0.35, 0, 1) * sstep(0.35, 0.65, nm)
    dust = sstep(0.62, 0.12, z) * (0.35 + 0.5 * nl)
    chip = sstep(0.55, 0.72, edge * 0.85 + (nf - 0.5) * 0.7 + (nl - 0.5) * 0.35) * (edge > 0.05)
    used = np.zeros((h, w), bool)
    for name, (col, s0, m0, ht, em) in MAT.items():
        k = mid == MID[name]
        if not k.any(): continue
        used |= k
        base = np.array(col) / 255.0 * (0.94 + 0.12 * nl[k])[:, None]            # a little unevenness in the finish
        c = base; sm = np.full(k.sum(), s0, np.float32); mt = np.full(k.sum(), m0, np.float32)
        if name in WEARS:
            ch = chip[k][:, None]; c = c * (1 - ch) + BARE * ch
            sm = sm * (1 - ch[:, 0]) + 0.55 * ch[:, 0]; mt = mt * (1 - ch[:, 0]) + ch[:, 0]
        # the stencils: paint over the finish (worn off where the enamel chips)
        for ch_i in (0, 1, 2):
            p = np.clip(S[..., ch_i][k] * 1.4, 0, 1) * (1 - 0.8 * chip[k]) * (0.85 + 0.15 * nf[k])
            if p.any(): c = c * (1 - p[:, None]) + PAINT[ch_i] * p[:, None]; sm = sm * (1 - p) + 0.35 * p; mt = mt * (1 - p)
        if name in DIRTY:
            g = (grime[k] * 0.30)[:, None]; c = c * (1 - g) + GRIME * g; sm = sm - 0.25 * g[:, 0]
            d = (dust[k] * 0.4)[:, None]; c = c * (1 - d) + DUST * d; sm = sm - 0.3 * d[:, 0]; mt = mt * (1 - d[:, 0])
        alb[k] = c if name in ("GoldVisor", "Slit") else c * occl[k][:, None]   # glass: no baked shadow on it; smo[k] = np.clip(sm, 0.02, 0.98); met[k] = np.clip(mt, 0, 1)
        heat[k] = ht; emis[k] = em
    # the empty texels: the mean of the used ones (no black halos in the mip levels)
    for arr in (alb, smo, met, heat, emis):
        arr[~used] = arr[used].mean(axis=0) if used.any() else 0
    nrm = N.copy(); nrm[~used] = (0.5, 0.5, 1.0)
    for glass in ("GoldVisor", "Slit"): nrm[mid == MID[glass]] = (0.5, 0.5, 1.0)   # glass and lenses: optically flat, clean reflections
    if atlas == "pz_visor":
        # the visor pane bulges top to bottom (+-25 deg): its upper part mirrors the sky, the lower the ground, a horizon
        # line between them. From the baked rest-pose height (B.y): its span over the glass and its direction in the
        # texture (the island may lie turned); tangent space as the D3D11 client reads it: x along +u, y along +v (the mesh's v, down the image)
        g = mid == MID["GoldVisor"]
        if g.any():
            zz = B[..., 1]; gy, gx = np.gradient(zz)               # per pixel: along the rows (down the image) and along u
            du, dv = gx[g].mean(), gy[g].mean(); l = np.hypot(du, dv) or 1.0; du, dv = du / l, dv / l
            z0, z1 = np.percentile(zz[g], 2), np.percentile(zz[g], 98)
            t = np.clip((zz - z0) / max(z1 - z0, 1e-6), 0, 1); sx = (2 * t - 1) * np.sin(np.radians(25)); sz = np.sqrt(1 - sx * sx)
            nrm[g, 0] = 0.5 + 0.5 * sx[g] * du; nrm[g, 1] = 0.5 + 0.5 * sx[g] * dv; nrm[g, 2] = 0.5 + 0.5 * sz[g]
            print("visor bulge: height %.3f..%.3f m, up in the texture (%.2f, %.2f)" % (z0, z1, du, dv))
    save(alb, atlas, "DXT1")
    save(np.repeat(smo[..., None], 3, 2), atlas + "_rghn", "DXT1")
    save(np.repeat(met[..., None], 3, 2), atlas + "_metl", "DXT1")
    save(nrm, atlas + "_norm", None)
    if heat.max() > 0:
        save(np.repeat(heat[..., None], 3, 2), atlas + "_heat", "DXT1"); save(np.repeat(emis[..., None], 3, 2), atlas + "_emis", "DXT1")
    print("%s: %dx%d, %d%% used, materials %s" % (atlas, w, h, 100 * used.mean(), sorted(set(np.unique(mid)) - {0})))
