# The experimental modular jet pack «Трилистник», v2 (2026-10-17; the user chose «три канальных вентилятора в кольцах
# вокруг капсулы, с видимыми лопастями и поворотными лопатками в выходе» after the honeycomb cells showed no mechanism).
# The base is «Наплечный»'s own core (worn: the suit figure's Jet parts; its booms and pods hidden). Per side:
#   the root - fixed on the pitch bearing on the core's top, out to the root joint;
#   the root joint - two axes: the arm's own axis (the vector's pitch: the arm and the gondola turn as one body) and the
#     fold axis (inclined: the arm swings back and down to stow);
#   the gondola - rigid on the arm's end: the hub with the capsule pod hanging under it (the base's boost, 500 N), three
#     ducted fans round it (D 0.228 m each; their rotors spin), the exit vanes in each duct (the side vector, +-15 deg).
# No translations: the folded pack is where its joints put it. The air's way over and under every fan is checked, and the
# fold is searched for (and checked through its whole swing) against her body, her arms' walking swing, the helmet, the
# core, the kit and the other side.
#   Meshes\Tantra\JetPackTrefoilWorn.msh - her model frame at rest (x right, y up, z forward), deployed
#   Meshes\Tantra\JetPackTrefoil.msh     - the pack lying about: folded, with a plain core, origin at the core's centre
#   Config\Tantra\JetPackTrefoil.geo     - KIN 2: hinges, gondolas' hubs, the fold (twist + axis-angle), each group's role,
#                                          the rotors' and the vanes' own centres and axes
# Run: python build_jetpack_trefoil.py   (numpy; prints the checks and the fold it chose)
import os, sys, math
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
WORN = os.path.join(ROOT, "Meshes", "Tantra", "JetPackTrefoilWorn.msh")
DROP = os.path.join(ROOT, "Meshes", "Tantra", "JetPackTrefoil.msh")
GEO = os.path.join(ROOT, "Config", "Tantra", "JetPackTrefoil.geo")
MATS = [("White", (0.88, 0.89, 0.90)), ("Dark", (0.05, 0.055, 0.06)), ("Metal", (0.60, 0.62, 0.64)), ("Red", (0.78, 0.09, 0.07)),
        ("Plug", (0.30, 0.24, 0.20)), ("Blade", (0.32, 0.34, 0.37))]       # Plug: the capsule's nozzle plug (glows with the boost)
MI = {n: i for i, (n, _) in enumerate(MATS)}

# ---- the layout (her model frame, metres) ----
CORE_C, CORE_S = (0.0, 0.355, -0.2156), (0.40, 0.475, 0.215)    # «Наплечный»'s shell (Config\Tantra\JetPack.geo)
Z0 = CORE_C[2]
BRG = (0.0, CORE_C[1] + CORE_S[1] / 2 + 0.045, Z0)              # the pitch bearing on the core's top
ZA = -0.276                                                     # the arms' line: 6 cm behind the core's centre
H = (0.17, BRG[1] + 0.04, ZA)                                   # the right root joint
P = (0.66, 0.62, ZA)                                            # the right gondola's hub (the fans' plane)
RI, RO, DH = 0.114, 0.135, 0.12                                 # a duct: inner and outer radius, depth (D 0.228: the area of 3 cells)
DF = RO + 0.078                                                 # the fans' centres off the hub (the slots 0.10 m: the arm runs one)
FAN_ANG = (0, 120, 240)                                         # plan angles (0 outboard; the arm comes in at 180)
CAP_H, CAP_R = 0.27, 0.058                                      # the capsule (as «Наплечный»'s JetPod)

groups, order, meta = {}, [], {}                                # meta: name -> (role, side, extra)
def grp(name, mi, role="core", side=-1, extra=None):
    if name not in groups: groups[name] = [mi, [], []]; order.append(name); meta[name] = (role, side, extra)
    return groups[name]

def V(*a): return np.array(a, dtype=float)
def unit(a): a = np.asarray(a, float); return a / (np.linalg.norm(a) or 1.0)
def rot(axis, ang):
    a = unit(axis); c, s = math.cos(ang), math.sin(ang); x, y, z = a
    return np.array([[c + x * x * (1 - c), x * y * (1 - c) - z * s, x * z * (1 - c) + y * s],
                     [y * x * (1 - c) + z * s, c + y * y * (1 - c), y * z * (1 - c) - x * s],
                     [z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c)]])

def poly(g, pts, n):
    b = len(g[1]); n = tuple(unit(n))
    for p in pts: g[1].append((tuple(map(float, p)), n))
    for k in range(1, len(pts) - 1): g[2].append((b, b + k, b + k + 1)); g[2].append((b, b + k + 1, b + k))

def grid(g, rings, normals, closed=True):
    b = len(g[1]); nu = len(rings[0])
    for r, nr in zip(rings, normals):
        for p, n in zip(r, nr): g[1].append((tuple(map(float, p)), tuple(map(float, unit(n)))))
    for i in range(len(rings) - 1):
        for j in range(nu if closed else nu - 1):
            a0 = b + i * nu + j; a1 = b + i * nu + (j + 1) % nu; c0 = a0 + nu; c1 = a1 + nu
            for t in ((a0, a1, c1), (a0, c1, c0)): g[2].append(t); g[2].append((t[0], t[2], t[1]))

def ellipsoid(g, c, r, nu=24, nv=12):
    c = V(*c); rings, nrm = [], []
    for i in range(nv + 1):
        th = math.pi * i / nv
        u = [V(math.sin(th) * math.cos(2 * math.pi * j / nu), math.cos(th), math.sin(th) * math.sin(2 * math.pi * j / nu)) for j in range(nu)]
        rings.append([c + q * V(*r) for q in u]); nrm.append([q / V(*r) for q in u])
    grid(g, rings, nrm)

def lathe(g, c, prof, nu=32, axis=(0, 1, 0)):
    """revolve (r, y) about the axis through c; flat normals per band"""
    c = V(*c); A = unit(axis); T = unit(np.cross(A, (1, 0, 0) if abs(A[0]) < 0.9 else (0, 0, 1))); S = np.cross(A, T)
    for (r0, y0), (r1, y1) in zip(prof, prof[1:]):
        dn = V(y1 - y0, -(r1 - r0))                              # the band's 2D normal (radial, axial)
        for j in range(nu):
            a0, a1 = 2 * math.pi * j / nu, 2 * math.pi * (j + 1) / nu
            e0 = T * math.cos(a0) + S * math.sin(a0); e1 = T * math.cos(a1) + S * math.sin(a1); em = unit(e0 + e1)
            q = [c + e0 * r0 + A * y0, c + e1 * r0 + A * y0, c + e1 * r1 + A * y1, c + e0 * r1 + A * y1]
            poly(g, q, em * dn[0] + A * dn[1] if np.linalg.norm(dn) > 0 else A)

def sweep(g, pts, chord, thick, n=24, k=16, ex=3.0):
    P_ = [V(*p) for p in pts]; P_ = [2 * P_[0] - P_[1]] + P_ + [2 * P_[-1] - P_[-2]]
    C, segs = [], len(P_) - 3
    for i in range(n + 1):
        t = i / n * segs; kk = min(int(t), segs - 1); u = t - kk; p0, p1, p2, p3 = P_[kk:kk + 4]
        C.append(0.5 * (2 * p1 + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3))
    rings, nrm = [], []
    for i, c in enumerate(C):
        t = i / n; tan = unit(C[min(i + 1, n)] - C[max(i - 1, 0)]); w = unit(np.cross((0, 1, 0), tan)); h = np.cross(tan, w)
        end = min(1.0, (min(t, 1 - t) * n / 3.0) ** 0.5 + 0.45)
        a = (chord(t) if callable(chord) else chord) / 2 * end; b = (thick(t) if callable(thick) else thick) / 2 * end
        rr, nn = [], []
        for j in range(k):
            th = 2 * math.pi * j / k; cs, sn = math.cos(th), math.sin(th)
            rr.append(c + w * a * math.copysign(abs(cs) ** (2 / ex), cs) + h * b * math.copysign(abs(sn) ** (2 / ex), sn))
            nn.append(w * cs / max(a, 1e-4) + h * sn / max(b, 1e-4))
        rings.append(rr); nrm.append(nn)
    grid(g, rings, nrm)
    poly(g, rings[0], C[0] - C[1]); poly(g, rings[-1], C[-1] - C[-2])

def cylinder(g, c, axis, r, h, nu=24, caps=True):
    lathe(g, V(*c) - unit(axis) * h / 2, [(r, 0), (r, h)] if not caps else [(0, 0), (r, 0), (r, h), (0, h)], nu, axis)

# ---- the core's fittings: the bearing, its fairing; the kit's shell under the core (its face 0.21 m behind her) ----
cylinder(grp("Bearing", MI["Metal"]), BRG, (1, 0, 0), 0.055, 0.22, 28)
for s in (1, -1): cylinder(grp("BearingRed", MI["Red"]), (s * 0.112, BRG[1], Z0), (1, 0, 0), 0.058, 0.012, 28, caps=False)
ellipsoid(grp("BearingFair", MI["White"]), (0, BRG[1] - 0.02, Z0), (0.20, 0.05, 0.12))
KY, KZ, KR = CORE_C[1] - CORE_S[1] / 2 - 0.03, Z0 - 0.085, 0.09
ellipsoid(grp("Kit", MI["White"]), (0, KY, KZ), (0.19, 0.06, KR))
cylinder(grp("KitRed", MI["Red"]), (0, KY, KZ), (0, 1, 0), KR, 0.012, 28, caps=False)
for k, (vv, nn) in enumerate(groups["KitRed"][1]): groups["KitRed"][1][k] = ((vv[0] * 0.19 / KR, vv[1], vv[2]), nn)

def fan_centre(sd, k):
    a = math.radians(FAN_ANG[k]); return V(sd * (P[0] + DF * math.cos(a)), P[1], P[2] - DF * math.sin(a))

# ---- each side ----
for sd, S in ((1, "R"), (-1, "L")):
    side = 0 if sd > 0 else 1
    h = V(sd * H[0], H[1], H[2]); p = V(sd * P[0], P[1], P[2])
    # the root (fixed): from the bearing out to the joint's fixed half
    sweep(grp("Root" + S, MI["White"]), [(0, BRG[1], Z0), (sd * 0.09, BRG[1] + 0.025, (Z0 + ZA) / 2), (sd * (H[0] - 0.03), H[1], ZA)], 0.12, 0.065, n=10)
    cylinder(grp("RootJoint" + S, MI["Metal"]), h - V(sd * 0.025, 0, 0), (1, 0, 0), 0.052, 0.05, 24)
    # the moving half of the joint, the arm (it turns with the gondola), down into the fans' plane along the slot at 180
    arm = grp("Arm" + S, MI["White"], "boom", side)
    cylinder(grp("ArmJoint" + S, MI["Metal"], "boom", side), h + V(sd * 0.025, 0, 0), (1, 0, 0), 0.05, 0.05, 24)
    cylinder(grp("ArmRed" + S, MI["Red"], "boom", side), h + V(sd * 0.052, 0, 0), (1, 0, 0), 0.053, 0.012, 24, caps=False)
    xin = sd * (P[0] - DF * 0.5 - RO * 0.9)
    sweep(arm, [h + V(sd * 0.04, 0, 0), V(sd * (H[0] + 0.6 * (abs(xin) - H[0])), P[1] + 0.035, ZA), V(xin, P[1] + 0.005, ZA), p], 0.08, 0.06, n=26)
    # the gondola: the hub, its red band, the three fans, the struts hub-duct, the capsule under the hub
    ellipsoid(grp("Hub" + S, MI["White"], "ring", side), p + V(0, 0.01, 0), (0.07, 0.05, 0.07))
    cylinder(grp("HubRed" + S, MI["Red"], "ring", side), p, (0, 1, 0), 0.072, 0.012, 28, caps=False)
    for k in range(3):
        fc = fan_centre(sd, k)
        duct = grp("Duct%s%d" % (S, k), MI["White"], "ring", side); din = grp("DuctIn%s" % S, MI["Dark"], "ring", side)
        lathe(duct, fc, [(RI + 0.002, DH / 2 - 0.004), (RI + 0.012, DH / 2), (RO - 0.006, DH / 2), (RO, DH / 2 - 0.012), (RO, -DH / 2 + 0.01), (RO - 0.008, -DH / 2), (RI + 0.004, -DH / 2)], 36)
        lathe(din, fc, [(RI, -DH / 2), (RI, DH / 2 - 0.004)], 36)
        cylinder(grp("DuctRed%s" % S, MI["Red"], "ring", side), fc + V(0, 0.012, 0), (0, 1, 0), RO + 0.002, 0.014, 36, caps=False)
        motor = grp("Motor%s" % S, MI["Metal"], "ring", side)
        ellipsoid(motor, fc + V(0, -0.005, 0), (0.032, 0.05, 0.032), 16, 8)
        for j in range(3):                                       # the stators under the rotor hold the motor
            a = math.radians(90 + 120 * j); e = V(math.cos(a), 0, math.sin(a))
            q0, q1 = fc + e * 0.03 + V(0, -0.03, 0), fc + e * (RI - 0.002) + V(0, -0.03, 0)
            poly(motor, [q0 + V(0, 0.012, 0), q1 + V(0, 0.012, 0), q1 - V(0, 0.012, 0), q0 - V(0, 0.012, 0)], np.cross(e, (0, 1, 0)))
        # the rotor: 7 twisted blades on its hub, spinning about the duct's axis (its own centre)
        rot_g = grp("Rotor%s%d" % (S, k), MI["Blade"], "rotor", side, (tuple(fc), (0, 1, 0), 1 if (k + side) % 2 == 0 else -1))
        ellipsoid(rot_g, fc + V(0, 0.022, 0), (0.034, 0.02, 0.034), 12, 6)
        for b in range(7):
            a = 2 * math.pi * b / 7; e = V(math.cos(a), 0, math.sin(a)); t_ = V(-math.sin(a), 0, math.cos(a))
            for r0, r1, tw0, tw1, c0, c1 in ((0.03, 0.07, 0.65, 0.45, 0.022, 0.02), (0.07, RI - 0.004, 0.45, 0.25, 0.02, 0.018)):
                def bl(r, tw, ch, sgn): return fc + V(0, 0.015, 0) + e * r + t_ * (sgn * ch * math.cos(tw)) + V(0, sgn * ch * math.sin(tw), 0)
                poly(rot_g, [bl(r0, tw0, c0, -1), bl(r1, tw1, c1, -1), bl(r1, tw1, c1, 1), bl(r0, tw0, c0, 1)], np.cross(e, t_ * math.cos(tw0) + V(0, math.sin(tw0), 0)))
        # the exit vanes: four slats across the exit, each turning about its own fore-aft line (the side vector)
        for j, xo in enumerate((-0.075, -0.025, 0.025, 0.075)):
            half = math.sqrt(max(0.0, (RI - 0.004) ** 2 - xo * xo)); cvx = fc + V(xo, -DH / 2 + 0.018, 0)
            vg = grp("Vane%s%d_%d" % (S, k, j), MI["Metal"], "vane", side, (tuple(cvx), (0, 0, 1), 1))
            poly(vg, [cvx + V(0, 0.017, -half), cvx + V(0, 0.017, half), cvx + V(0, -0.017, half), cvx + V(0, -0.017, -half)], (1, 0, 0))
        # the strut from the hub to the duct (outside the duct: the hub sits in the middle of the three)
        a = math.radians(FAN_ANG[k]); e = V(sd * math.cos(a), 0, -math.sin(a))
        sweep(grp("Strut" + S, MI["White"], "ring", side), [p + e * 0.05, p + e * (DF - RO + 0.01)], 0.05, 0.045, n=6, k=12)
    # the capsule pod under the hub (the base's boost): the egg, its red ring, the dark exit and the plug
    cc = p + V(0, -0.03 - CAP_H / 2, 0)
    prof = [(0.0, 0.5), (0.45, 0.47), (0.75, 0.38), (0.92, 0.24), (1.0, 0.08), (1.0, -0.12), (0.97, -0.30), (0.90, -0.42), (0.80, -0.5)]
    lathe(grp("Pod" + S, MI["White"], "ring", side), cc, [(r * CAP_R, y * CAP_H) for r, y in prof], 24)
    cylinder(grp("PodRed" + S, MI["Red"], "ring", side), cc + V(0, 0.005, 0), (0, 1, 0), CAP_R * 1.03, 0.018, 24, caps=False)
    lathe(grp("PodDark" + S, MI["Dark"], "ring", side), cc, [(0.8 * CAP_R, -0.5 * CAP_H - 0.001), (0.38 * CAP_R, -0.5 * CAP_H - 0.001)], 24)
    lathe(grp("Plug" + S, MI["Plug"], "ring", side), cc, [(0.38 * CAP_R, -0.5 * CAP_H - 0.002), (0.0, -0.5 * CAP_H - 0.05)], 16)

# ======================= the checks =======================
def pts_of(names):
    return np.array([v for nm in names for v, _ in groups[nm][1]])
RIGHT = [n for n in order if meta[n][1] == 0]
ALLR = pts_of(RIGHT)
hR, pR = V(*H), V(*P)
aArm = unit(pR - hR)

# (1) the air's way: over each fan (its duct's bore) 0.30 m and under it 0.60 m - no part of the pack, not her body
def body_pts():
    out = []
    for x in np.linspace(-0.20, 0.20, 9):
        for y in np.linspace(-0.55, 0.45, 31):
            for z in np.linspace(-0.11, 0.12, 7): out.append((x, y, z))
    for i in range(400):
        a, b = 2 * math.pi * (i % 20) / 20, math.pi * (i // 20) / 20
        out.append((0.17 * math.sin(b) * math.cos(a), 0.76 + 0.17 * math.cos(b), 0.17 * math.sin(b) * math.sin(a)))
    for sg in (1, -1):
        for t in np.linspace(0, 1, 25):
            c = (sg * (0.24 + 0.08 * t), 0.47 - 0.62 * t, 0.02)
            for k in range(12): out.append((c[0] + 0.06 * math.cos(2 * math.pi * k / 12), c[1], c[2] + 0.06 * math.sin(2 * math.pi * k / 12)))
    return np.array(out)
BODY = body_pts()
bad = 0
for sd, S in ((1, "R"), (-1, "L")):
    side = 0 if sd > 0 else 1
    others = [n for n in order if not (n.startswith(("Duct", "Motor", "Rotor", "Vane")) and meta[n][1] == side)]
    O = pts_of(others)
    for k in range(3):
        fc = fan_centre(sd, k)
        def inbore(Q): return np.hypot(Q[:, 0] - fc[0], Q[:, 2] - fc[2]) < RI - 0.005
        dy = O[:, 1] - fc[1]
        up = np.sum(inbore(O) & (dy > DH / 2) & (dy < 0.30)); dn = np.sum(inbore(O) & (dy < -DH / 2) & (dy > -0.60))
        db = BODY[:, 1] - fc[1]; bu = np.sum(inbore(BODY) & (db > -0.60) & (db < 0.30))
        print("fan %s%d at x %+.2f z %+.2f: intake %s, jet %s, her body %s" % (S, k, fc[0], fc[2], "free" if not up else "BLOCKED %d" % up, "free" if not dn else "BLOCKED %d" % dn, "clear" if not bu else "IN THE WAY %d" % bu))
        bad += bool(up or dn or bu)
if bad: sys.exit("the air's way is blocked: fix the layout")

# (2) the fold: the arm's twist TW (about the arm's own axis) and the root joint's swing FA about the axis U - searched
# so that the folded pack, and every pose on the way there, keeps off her body, the helmet, the core, the kit and the other
# side; folded it also keeps out of her arms' walking swing. The module turns the sides the same way (JetPack::PoseFan).
def pose(Q, f, TW, U, FA, tilt=0.0, sd=1):
    a = aArm * V(sd, 1, 1); h = hR * V(sd, 1, 1); u = V(U[0], sd * U[1], sd * U[2])
    R = rot(u, FA * f) @ rot(a, sd * (tilt + TW * f))
    return (Q - h) @ R.T + h

SUB = ALLR[::4]
near_h = np.linalg.norm(SUB - hR, axis=1) < 0.075              # the joint itself sits in the core's top
def collisions(Q, walking):
    x, y, z = Q[:, 0], Q[:, 1], Q[:, 2]; ax = np.abs(x)
    torso = (ax < 0.25) & (z > -0.16) & (y > -0.65) & (y < 0.58)
    helm = np.linalg.norm(Q - V(0, 0.76, 0), axis=1) < 0.23
    core = (ax < 0.22) & (y > 0.10) & (y < 0.66) & (z > -0.345) & (z < -0.09) & ~near_h
    kit = (ax < 0.21) & (np.abs(y - KY) < 0.075) & (np.abs(z - KZ) < KR + 0.02)
    other = x < 0.03                                            # the left side mirrors it: the two may not meet
    arms = (ax > 0.17) & (ax < 0.43) & (z > -0.45) & (z < 0.45) & (y > -0.55) & (y < 0.52) if walking else np.zeros(len(Q), bool)
    return int(np.sum(torso | helm | core | kit | other | arms))

best = None
for TW in range(-90, 91, 30):
    for th in range(0, 91, 15):
        for ph in range(0, 360, 30):
            U = (math.sin(math.radians(th)) * math.cos(math.radians(ph)), math.cos(math.radians(th)), math.sin(math.radians(th)) * math.sin(math.radians(ph)))
            if th == 0 and ph: continue
            for FA in [a for a in range(-170, 171, 10) if a]:
                tw, fa = math.radians(TW), math.radians(FA)
                Qf = pose(SUB, 1.0, tw, U, fa)
                if collisions(Qf, True): continue
                if any(collisions(pose(SUB, f, tw, U, fa), False) for f in (0.2, 0.4, 0.6, 0.8)): continue
                depth = -Qf[:, 2].min() - 0.345; width = Qf[:, 0].max(); top = Qf[:, 1].max(); low = Qf[:, 1].min()
                if top > 1.10 or low < -0.75: continue
                score = 2 * width + 0.5 * depth + 2 * max(0.0, top - 1.0)   # narrow first (doors, her arms), then short, then low
                if best is None or score < best[0]: best = (score, TW, U, FA, depth, width, top, low)
if best is None: sys.exit("no fold keeps off her: fix the layout")
_, TW, U, FA, depth, width, top, low = best
print("fold: twist %+d deg about the arm, then %+d deg about (%.2f %.2f %.2f): %.2f m behind the core, %.2f m wide, from %.2f to %.2f m (model y)"
      % (TW, FA, *U, depth, 2 * width, low, top))
# the poses for a look in Blender with her figure (OBJ in blender's frame: x, -(z - 0.01), y + 0.93)
POSES = {"deployed": (0.0, 0.0, 0.0), "pitch35": (0.0, 35.0, 0.0), "fold50": (0.5, 0.0, 0.0), "folded": (1.0, 0.0, 0.0)}
# the vector's pitch in flight (+-35 deg): the arm and the gondola turn clear of her, the helmet and the core
for tl in (-35, 35):
    n = collisions(pose(SUB, 0.0, 0, U, 0, math.radians(tl)), False)
    print("pitch %+d deg: %s" % (tl, "clear" if not n else "HITS %d" % n))
    if n: sys.exit("the pitch hits her: fix the layout")

# ======================= write =======================
def write(path, names, xf, centre=(0, 0, 0)):
    with open(path, "w", encoding="ascii", newline="\r\n") as f:
        f.write("MSHX1\nGROUPS %d\n" % len(names))
        for name in names:
            mi, vs, fs = groups[name]
            f.write("LABEL %s\nMATERIAL %d\nTEXTURE 0\nGEOM %d %d\n" % (name, mi + 1, len(vs), len(fs)))
            Q, N = xf(name, np.array([v for v, _ in vs]), np.array([n for _, n in vs]))
            for q, m in zip(Q, N): f.write("%.4f %.4f %.4f %.4f %.4f %.4f\n" % (*(q - V(*centre)), *m))
            for a, b, c in fs: f.write("%d %d %d\n" % (a, c, b))
        f.write("MATERIALS %d\n" % len(MATS))
        for n, _ in MATS: f.write("JT_%s\n" % n)
        for n, col in MATS:
            sp = 0.5 if n in ("Metal", "Plug", "Blade") else 0.3 if n == "White" else 0.1
            f.write("MATERIAL JT_%s\n%.3f %.3f %.3f 1\n%.3f %.3f %.3f 1\n%.2f %.2f %.2f 1 20\n0 0 0 1\n" % (n, *col, *col, sp, sp, sp))
        f.write("TEXTURES 0\n")

worn = list(order)
write(WORN, worn, lambda nm, Q, N: (Q, N))
def folded(nm, Q, N):
    role, side, _ = meta[nm]
    if side < 0: return Q, N
    sd = 1 if side == 0 else -1
    a = aArm * V(sd, 1, 1); h = hR * V(sd, 1, 1); u = V(U[0], sd * U[1], sd * U[2])
    R = rot(u, math.radians(FA)) @ rot(a, sd * math.radians(TW))
    return (Q - h) @ R.T + h, N @ R.T
ellipsoid(grp("CoreShell", MI["White"]), CORE_C, (CORE_S[0] / 2, CORE_S[1] / 2, CORE_S[2] / 2), 28, 14)
write(DROP, worn + ["CoreShell"], folded, CORE_C)

with open(GEO, "w", newline="\n") as f:
    f.write("# OrbiterCrew experimental modular jet pack Trefoil v2 in the crew member's model frame (written by build_jetpack_trefoil.py)\n")
    f.write("KIN 2\nSHELL %.4f %.4f %.4f %.4f %.4f %.4f\n" % (*CORE_C, *CORE_S))
    for sd in (1, -1):                                            # HINGE: the root joint; PIVOT: the hub (the arm's axis runs between)
        f.write("HINGE %.4f %.4f %.4f\nPIVOT %.4f %.4f %.4f\n" % (sd * H[0], H[1], H[2], sd * P[0], P[1], P[2]))
    f.write("FOLD_TWIST %.1f\nFOLD_V %.4f %.4f %.4f %.1f\n" % (TW, *U, FA))
    f.write("ROTOR_Y %.4f\nBOOST_Y %.4f\nGLOW_MAT %d\n" % (P[1], P[1] - 0.03 - CAP_H - 0.05, MI["Plug"]))
    for k, nm in enumerate(worn):
        role, side, ex = meta[nm]
        if role in ("rotor", "vane"):
            (cx, cy, cz), (ax, ay, az), d = ex
            f.write("GROUP %d %s %d %.4f %.4f %.4f %.4f %.4f %.4f %d\n" % (k, role, side, cx, cy, cz, ax, ay, az, d))
        else: f.write("GROUP %d %s %d\n" % (k, role, side))
print("groups", len(worn), "verts", sum(len(groups[n][1]) for n in worn))
OBJ = os.path.join(HERE, "..", "blender", "trefoil_poses"); os.makedirs(OBJ, exist_ok=True)
for pn, (f_, tl, _) in POSES.items():
    with open(os.path.join(OBJ, pn + ".obj"), "w") as fo:
        base = 1
        for nm in worn:
            mi, vs, fs = groups[nm]; role, side, ex = meta[nm]
            Q = np.array([v for v, _ in vs])
            if side >= 0:
                sd = 1 if side == 0 else -1
                a = aArm * V(sd, 1, 1); h = hR * V(sd, 1, 1); u = V(U[0], sd * U[1], sd * U[2])
                R = rot(u, math.radians(FA) * f_) @ rot(a, sd * (math.radians(tl) + math.radians(TW) * f_))
                Q = (Q - h) @ R.T + h
            fo.write("o %s\nusemtl %s\n" % (nm, MATS[mi][0]))
            for q in Q: fo.write("v %.4f %.4f %.4f\n" % (q[0], -(q[2] - 0.01), q[1] + 0.93))
            for a_, b_, c_ in fs[::2]: fo.write("f %d %d %d\n" % (a_ + base, b_ + base, c_ + base))
            base += len(vs)
