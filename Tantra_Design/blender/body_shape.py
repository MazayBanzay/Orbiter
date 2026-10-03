# The person's body beyond build_human.py, shared by every garment build (one person, one body in any clothes).
# Taken from build_coverall.py («Экипаж» owns the values; 2026-10-03 copy, the figure accepted by the user).
# build_suit.py uses it. «Экипаж» will move the source here; until then re-copy before a suit rebuild.
import os, gzip
import numpy as np
from mathutils import Vector
log = []
# ---- the person's body, beyond build_human.py (TRIAL, applied here only; the same values belong to the suit build too,
# so the person has one body in every garment - for the Architect to take over once accepted). The rig is not refitted:
# both changes stay within ~1.5 cm and move no joint, so every clip stays valid.
BREAST_DIST, BREAST_LOW, BREAST_DOWN = 1.0, 0.6, 0.3
BODY_TARGETS = [("arms/l-upperarm-shoulder-muscle-decr", 1.0), ("arms/r-upperarm-shoulder-muscle-decr", 1.0),   # MakeHuman targets: a flatter deltoid
                # user 2026-10-02: hips and legs a little slimmer, finer (build_human's hip widening taken back,
                # thighs and calves narrower; circumference only - no joint moves)
                ("hip/hip-scale-horiz-incr", -0.12), ("buttocks/buttocks-volume-incr", -0.05),
                ("legs/measure-thigh-circ-decr", 0.35), ("legs/measure-calf-circ-decr", 0.15),
                ("breast/breast-dist-decr", -0.20), ("breast/breast-volume-vert-up", -0.25)]   # build_human's own bust edits taken back
# (2026-10-02) the bust is MakeHuman's own macro shape only - no local breast targets: stacked (point, distance, volume up/
# down, translate) they deformed it into cones (user: "ужасно"). Rejected variant kept in build_coverall_bust_targets_rejected.py
# user 2026-10-02: a smaller bust. build_human has cupsize 0.72; the macro breast targets' share is moved to CUP
# (MakeHuman's own min/average/max weighting), with the other macros (young, muscle 0.60, weight 0.48, firmness 0.78) as built
TIP_R, TIP_ITERS = 0.06, 30   # the breast's tip under the cloth: rounded over 6 cm
CUP_DOME = False   # the invented ellipsoid cup is off: the body's own shape is set right instead
CUP_BUILT, CUP = 0.72, 0.64   # an adult woman's bust (0.42-0.50 read as a girl's, user 2026-10-02); MakeHuman's own shape
CUP_RX, CUP_RZ, CUP_RY, CUP_LIFT = 0.074, 0.066, 0.052, 0.004   # m: the moulded cup's half-width, half-height, depth; its front ahead of the apex
NIPPLE_R = 0.028   # m: nipple and areola relief smoothed within this radius of the breast apex
MACRO = {"muscle": 0.60, "weight": 0.48, "firmness": 0.78}
SLOPE_DROP = 0.010      # m, the shoulder cap lowered over the acromion: the trapezius falls ~20-25 deg (MakeHuman has no target for it)
MPFB_TARGETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bx", "mpfb", "data", "targets")
def smooth01(e0, e1, x): t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)

def body_extras(body, arm, log=log):
    """The person's shape beyond build_human.py, on the body itself (skin and everything cut from it follow)."""
    import gzip
    me = body.data; k = 0.1 / body.matrix_world.to_scale().x   # MakeHuman targets are in decimetres, y up, z forward
    for t, w in BODY_TARGETS:
        n = 0
        for l in gzip.open(os.path.join(MPFB_TARGETS, t + ".target.gz"), "rt"):
            f = l.split()
            if len(f) < 4 or l.startswith("#"): continue
            i = int(f[0])
            if i < len(me.vertices):
                dx, dy, dz = (float(x) * w * k for x in f[1:4]); me.vertices[i].co += Vector((dx, -dz, dy)); n += 1
        log.append("body target %s x%.2f: %d verts" % (t, w, n))
    def mix3(v, names):   # MakeHuman macro weighting: min/average/max around 0.5
        return {names[0]: max(0.0, 1 - v / 0.5), names[1]: 1 - abs(v - 0.5) / 0.5, names[2]: max(0.0, (v - 0.5) / 0.5)}
    wm = mix3(MACRO["muscle"], ("minmuscle", "averagemuscle", "maxmuscle")); ww = mix3(MACRO["weight"], ("minweight", "averageweight", "maxweight"))
    wf = mix3(MACRO["firmness"], ("minfirmness", "averagefirmness", "maxfirmness"))
    c0 = mix3(CUP_BUILT, ("mincup", "averagecup", "maxcup")); c1 = mix3(CUP, ("mincup", "averagecup", "maxcup"))
    acc = {}; nt = 0
    for m_, a in wm.items():
        for w_, b in ww.items():
            for c_ in c0:
                for f_, d in wf.items():
                    wgt = a * b * (c1[c_] - c0[c_]) * d
                    fn = os.path.join(MPFB_TARGETS, "breast", "female-young-%s-%s-%s-%s.target.gz" % (m_, w_, c_, f_))
                    if abs(wgt) < 1e-6 or not os.path.exists(fn): continue
                    nt += 1
                    for l in gzip.open(fn, "rt"):
                        f = l.split()
                        if len(f) < 4 or l.startswith("#"): continue
                        i = int(f[0]); dx, dy, dz = (float(x) * wgt * k for x in f[1:4])
                        o = acc.get(i, (0, 0, 0)); acc[i] = (o[0] + dx, o[1] - dz, o[2] + dy)
    for i, d in acc.items():
        if i < len(me.vertices): me.vertices[i].co += Vector(d)
    log.append("bust: cup %.2f -> %.2f from %d macro targets, %d verts" % (CUP_BUILT, CUP, nt, len(acc)))
    # user 2026-10-02: no nipples on the body itself - under any garment (coverall, suit) nothing pokes out. The nipple
    # and areola relief is relaxed into the breast's own curve (within NIPPLE_R of the apex, fading out); the bust's shape stays.
    import bmesh as _bm
    bmn = _bm.new(); bmn.from_mesh(me); dl = bmn.verts.layers.deform.active; gb = body.vertex_groups['body'].index
    skin = [v for v in bmn.verts if v[dl].get(gb, 0) > 0]
    Mw = body.matrix_world; n_tot = 0
    for sd in (-1, 1):
        cand = [v for v in skin if 0.03 < sd * (Mw @ v.co).x < 0.16 and 1.10 < (Mw @ v.co).z < 1.40 and (Mw @ v.co).y < 0]
        if not cand: continue
        c = min(cand, key=lambda v: (Mw @ v.co).y).co.copy()
        zone = [(v, float(smooth01(NIPPLE_R, NIPPLE_R * 0.4, (v.co - c).length / k * 0.1))) for v in skin if (v.co - c).length / k * 0.1 < NIPPLE_R]
        for _ in range(40):
            nw = [(v, v.co.lerp(sum((e.other_vert(v).co for e in v.link_edges), Vector()) / len(v.link_edges), 0.5 * w)) for v, w in zone if v.link_edges]
            for v, co in nw: v.co = co
        n_tot += len(zone)
    bmn.to_mesh(me); bmn.free(); me.update()
    log.append("nipples smoothed into the breast: %d verts" % n_tot)
    # shoulder line: the base mesh runs almost level from the neck to the shoulder edge (~11 deg) - a square, padded look.
    # Lower the shoulder cap above the joint, most over the acromion, nothing at the neck or down the arm.
    M = body.matrix_world; Mi = M.inverted()
    jz = (arm.matrix_world @ arm.data.bones['LeftArm' if 'LeftArm' in arm.data.bones else 'upperarm_l'].head_local).z
    n = 0
    for v in me.vertices:
        p = M @ v.co; ax = abs(p.x)
        d = SLOPE_DROP * float(smooth01(0.09, 0.17, ax) * smooth01(0.27, 0.21, ax) * smooth01(jz - 0.06, jz + 0.02, p.z))
        if d > 0: v.co = Mi @ (p - Vector((0, 0, d))); n += 1
    me.update(); log.append("shoulder slope: %d verts, up to %.0f mm" % (n, SLOPE_DROP * 1000))

