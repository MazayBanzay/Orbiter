# «Панцирь» (class 2 heavy suit, Orbitersdk/OrbiterCrew/HEAVY_SUIT.md) - five concept variants built from primitives
# on the astronavigator figure (rest pose of astronavigator_suit.blend). The .blend is only loaded, never saved.
#
#   blender -b ..\blender\astronavigator_suit.blend --python build_heavy_suit_concepts.py      -> pantsir_v<N>_front/back/open.png
#   python build_heavy_suit_concepts.py --sheet                                                  -> pantsir_variants.png (PIL)
#   python build_heavy_suit_concepts.py --sheet-e                                                -> pantsir_variants_e.png (E1..E5)
#   python build_heavy_suit_concepts.py --vsref <ref image>                                      -> pantsir_vD_vs_ref.png
#   python build_heavy_suit_concepts.py --sheet-c                                                -> pantsir_variants_c.png (C1, C2)
#   python build_heavy_suit_concepts.py --sheet-b                                                -> pantsir_variants_b.png (1b, 3b, 13)
# env: VARIANTS=1,3 or 1b,3b,13 (subset), QUICK=1 (small fast preview renders into _preview\)
# Blender axes: x across (+x = figure's left), y along (front = -y), z up; floor z = 0.
import math, os, sys, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = {1: "Капюшон-щель", 2: "Купол-перископ", 3: "Шторка", 4: "Латы-каркас", 5: "Гладкий-гало"}


def contact_sheet(keys=("1", "2", "3", "4", "5"), names=None, out="pantsir_variants.png", note=None):
    from PIL import Image, ImageDraw, ImageFont
    names = names or {str(k): v for k, v in NAMES.items()}; n = len(keys)
    ims = [Image.open(os.path.join(HERE, "pantsir_v%s_front.png" % k)).convert("RGB") for k in keys]
    w, h = ims[0].size; cw = int(w * 0.62); x0 = (w - cw) // 2; head = 110
    sheet = Image.new("RGB", (cw * n, h + head + 56), (16, 17, 20)); d = ImageDraw.Draw(sheet)
    f1 = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 54); f2 = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 26)
    for i, im in enumerate(ims):
        sheet.paste(im.crop((x0, 0, x0 + cw, h)), (i * cw, head))
        t = "%s  «%s»" % (keys[i], names[keys[i]]); tw = d.textlength(t, font=f1)
        d.text((i * cw + (cw - tw) / 2, 22), t, font=f1, fill=(235, 232, 225))
        if i: d.line([(i * cw, head), (i * cw, h + head)], fill=(40, 42, 48), width=3)
    d.text((18, h + head + 12), note or "«Панцирь», класс 2 - концепты: золотая МЛИ поверх жёсткой оболочки (4 г/см² + Ta), катушки поля до 2 кВт, 20 кВт·ч, силовой каркас", font=f2, fill=(150, 150, 150))
    sheet.save(os.path.join(HERE, out))


if __name__ == "__main__" and "--sheet" in sys.argv:
    contact_sheet(); sys.exit(0)
if __name__ == "__main__" and "--sheet-e" in sys.argv:
    contact_sheet(("E1", "E2", "E3", "E4", "E5"), {"E1": "Мантия", "E2": "Чешуя", "E3": "Крыло", "E4": "Испарина", "E5": "Контур"}, "pantsir_variants_e.png",
                  "«Панцирь» rev. 7: пять инженерных схем - тепловой и радиационный экран, приводы внутри, компактный шлем"); sys.exit(0)
if __name__ == "__main__" and "--vsref" in sys.argv:
    from PIL import Image, ImageDraw, ImageFont
    a = Image.open(os.path.join(HERE, "pantsir_vD_front.png")).convert("RGB")
    b = Image.open(sys.argv[sys.argv.index("--vsref") + 1]).convert("RGB"); b = b.resize((int(b.width * a.height / b.height), a.height))
    sheet = Image.new("RGB", (a.width + b.width + 20, a.height + 70), (16, 17, 20)); sheet.paste(a, (0, 70)); sheet.paste(b, (a.width + 20, 70))
    d = ImageDraw.Draw(sheet); f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 40)
    d.text((20, 14), "D (наш концепт)", font=f, fill=(235, 232, 225)); d.text((a.width + 40, 14), "референс пользователя", font=f, fill=(235, 232, 225))
    sheet.save(os.path.join(HERE, "pantsir_vD_vs_ref.png")); sys.exit(0)
if __name__ == "__main__" and "--sheet-c" in sys.argv:
    contact_sheet(("C1", "C2"), {"C1": "Приводы под тканью", "C2": "Встроенные стойки"}, "pantsir_variants_c.png",
                  "«Панцирь» rev. 5: широкий шлем в воротнике, бронзовый нагрудник, катушки внутри"); sys.exit(0)
if __name__ == "__main__" and "--sheet-b" in sys.argv:
    contact_sheet(("1b", "3b", "13"), {"1b": "Капюшон-щель", "3b": "Шторка", "13": "Щель + окно"}, "pantsir_variants_b.png",
                  "«Панцирь» rev. 3: гранёные оболочки; щиток в направляющих уходит в карман темени, гермозабрало на височных осях"); sys.exit(0)

import bpy
from mathutils import Vector, Quaternion
V = Vector
QUICK = os.environ.get("QUICK") == "1"
OUT = os.path.join(HERE, "_preview") if QUICK else HERE
LOG = os.path.join(HERE, "build_heavy_suit_concepts.log"); log = []
BLEND_DIR = os.path.abspath(os.path.join(HERE, "..", "blender"))

# ---------------------------------------------------------------- materials
M = {}


def mat(name, base, metal=0.0, rough=0.4, coat=0.0, emit=None, bump=0.0, bscale=60.0):
    m = bpy.data.materials.new("HS_" + name); m.use_nodes = True; nt = m.node_tree
    p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*base, 1); p.inputs["Metallic"].default_value = metal
    p.inputs["Roughness"].default_value = rough
    if coat: p.inputs["Coat Weight"].default_value = coat
    if emit:
        p.inputs["Emission Color"].default_value = (*emit[0], 1); p.inputs["Emission Strength"].default_value = emit[1]
    if bump:
        n = nt.nodes.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = bscale; n.inputs["Detail"].default_value = 6
        tc = nt.nodes.new("ShaderNodeTexCoord"); nt.links.new(tc.outputs["Object"], n.inputs["Vector"])
        b = nt.nodes.new("ShaderNodeBump"); b.inputs["Strength"].default_value = bump; b.inputs["Distance"].default_value = 0.003
        nt.links.new(n.outputs["Fac"], b.inputs["Height"]); nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    m.diffuse_color = (*base, 1); M[name] = m; return m


def make_materials():
    mat("ceramic", (0.86, 0.85, 0.81), rough=0.38, coat=0.25)
    mat("ceramic2", (0.70, 0.70, 0.68), rough=0.45)
    mat("gold", (1.0, 0.70, 0.28), metal=1.0, rough=0.28, bump=0.35, bscale=45)
    mat("goldsmooth", (1.0, 0.72, 0.32), metal=1.0, rough=0.16, bump=0.05, bscale=8)
    mat("dark", (0.035, 0.038, 0.045), rough=0.5, coat=0.15)
    mat("dark2", (0.09, 0.095, 0.105), rough=0.55)
    mat("metal", (0.62, 0.63, 0.65), metal=1.0, rough=0.3)
    mat("steel", (0.30, 0.31, 0.33), metal=1.0, rough=0.4)
    mat("orange", (0.95, 0.33, 0.04), rough=0.4, coat=0.2)
    mat("copper", (0.95, 0.52, 0.32), metal=1.0, rough=0.25)
    mat("visor", (1.0, 0.76, 0.36), metal=1.0, rough=0.05)
    mat("lens", (0.02, 0.03, 0.06), metal=0.3, rough=0.03, coat=1.0)
    mat("glow", (0.2, 0.7, 1.0), rough=0.3, emit=((0.25, 0.75, 1.0), 6.0))
    mat("lamp", (1, 1, 0.95), emit=((1.0, 0.95, 0.85), 8.0))
    mat("rad", (0.80, 0.82, 0.84), metal=0.6, rough=0.25)


# ---------------------------------------------------------------- mesh primitives
COLL = [None]


def mk(verts, faces, mname, loc=(0, 0, 0), q=None, scale=None, sharp=0.6, name="hs"):
    me = bpy.data.meshes.new(name); me.from_pydata([tuple(v) for v in verts], [], faces); me.validate()
    me.materials.append(M[mname])
    try:
        me.shade_smooth(); me.set_sharp_from_angle(angle=sharp)
    except Exception:
        for p in me.polygons: p.use_smooth = True
    o = bpy.data.objects.new(name, me); COLL[0].objects.link(o)
    o.location = V(loc); o.rotation_mode = 'QUATERNION'
    if q is not None: o.rotation_quaternion = q
    if scale is not None: o.scale = scale
    for g in GROUP: g.append(o)
    return o


def qz(axis):
    """rotation taking +Z onto axis"""
    return V(axis).normalized().to_track_quat('Z', 'Y')


FACET = [False]


def cyl(p1, p2, r1, r2=None, m="metal", seg=40, cap=True):
    p1, p2 = V(p1), V(p2); d = p2 - p1; L = d.length; r2 = r1 if r2 is None else r2
    if FACET[0] and seg >= 24:
        k = 1.0 / math.cos(math.pi / 8); seg = 8; r1 *= k; r2 *= k
    vs = []; fs = []
    for k, (z, r) in enumerate(((-L / 2, r1), (L / 2, r2))):
        for j in range(seg):
            a = 2 * math.pi * j / seg; vs.append((r * math.cos(a), r * math.sin(a), z))
    for j in range(seg):
        j1 = (j + 1) % seg; fs.append((j, j1, seg + j1, seg + j))
    if cap: fs.append(tuple(reversed(range(seg)))); fs.append(tuple(range(seg, 2 * seg)))
    return mk(vs, fs, m, (p1 + p2) / 2, qz(d))


def sph(c, r, m="metal", q=None, seg=48, rings=24):
    rx, ry, rz = (r, r, r) if not isinstance(r, (tuple, list)) else r
    fac = FACET[0] and seg >= 24
    if fac: seg, rings = 8, 6; rx, ry, rz = rx * 1.06, ry * 1.06, rz * 1.06
    vs = [(0, 0, -1)]; fs = []
    for i in range(1, rings):
        t = math.pi * i / rings - math.pi / 2
        for j in range(seg):
            a = 2 * math.pi * j / seg; vs.append((math.cos(t) * math.cos(a), math.cos(t) * math.sin(a), math.sin(t)))
    vs.append((0, 0, 1)); top = len(vs) - 1
    for j in range(seg): fs.append((0, 1 + (j + 1) % seg, 1 + j))
    for i in range(rings - 2):
        a0 = 1 + i * seg; b0 = a0 + seg
        for j in range(seg):
            j1 = (j + 1) % seg; fs.append((a0 + j, a0 + j1, b0 + j1, b0 + j))
    last = 1 + (rings - 2) * seg
    for j in range(seg): fs.append((last + j, last + (j + 1) % seg, top))
    return mk(vs, fs, m, c, q, (rx, ry, rz), sharp=0.3 if fac else 3.1)


def tor(c, axis, R, r, m="copper", nu=72, nv=18, sy=1.0):
    vs = []; fs = []
    for i in range(nu):
        a = 2 * math.pi * i / nu
        for j in range(nv):
            b = 2 * math.pi * j / nv; rr = R + r * math.cos(b)
            vs.append((rr * math.cos(a), rr * math.sin(a) * sy, r * math.sin(b)))
    for i in range(nu):
        for j in range(nv):
            i1 = (i + 1) % nu; j1 = (j + 1) % nv; fs.append((i * nv + j, i1 * nv + j, i1 * nv + j1, i * nv + j1))
    return mk(vs, fs, m, c, qz(axis), sharp=3.1)


def rbox(c, size, m="dark", q=None, r=0.012, seg=3):
    import bmesh
    if FACET[0]: seg = 1
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts: v.co = V((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
    if r > 0: bmesh.ops.bevel(bm, geom=list(bm.edges), offset=min(r, 0.49 * min(size)), segments=seg, profile=0.5, affect='EDGES')
    me = bpy.data.meshes.new("box"); bm.to_mesh(me); bm.free()
    vs = [v.co.copy() for v in me.vertices]; fs = [tuple(p.vertices) for p in me.polygons]; bpy.data.meshes.remove(me)
    return mk(vs, fs, m, c, q, sharp=0.5)


def lathe(prof, m="ceramic", seg=72, sharp=3.1):
    """prof: [(z, rx, ry, cy)] bottom to top; rx = 0 closes to a pole"""
    vs = []; fs = []; rows = []; a_off = 0.0
    if FACET[0]:
        k = 1.0 / math.cos(math.pi / 8); seg = 8; a_off = math.pi / 8; sharp = 0.3
        prof = [(z, rx * k, ry * k, cy) for z, rx, ry, cy in prof]
    for z, rx, ry, cy in prof:
        if rx <= 1e-6: vs.append((0, cy, z)); rows.append([len(vs) - 1] * seg); continue
        base = len(vs)
        for j in range(seg):
            a = 2 * math.pi * j / seg + a_off; vs.append((rx * math.cos(a), cy + ry * math.sin(a), z))
        rows.append(list(range(base, base + seg)))
    for A, B in zip(rows, rows[1:]):
        for j in range(seg):
            j1 = (j + 1) % seg; f = [A[j], A[j1], B[j1], B[j]]
            f2 = []
            for x in f:
                if x not in f2: f2.append(x)
            if len(f2) >= 3: fs.append(tuple(f2))
    if prof[0][1] > 1e-6: fs.append(tuple(reversed(rows[0])))
    if prof[-1][1] > 1e-6: fs.append(tuple(rows[-1]))
    return mk(vs, fs, m, sharp=sharp)


def interp(prof, z):
    if z <= prof[0][0]: return prof[0][1:]
    for a, b in zip(prof, prof[1:]):
        if a[0] <= z <= b[0]:
            t = (z - a[0]) / max(b[0] - a[0], 1e-9); return tuple(a[i] + (b[i] - a[i]) * t for i in (1, 2, 3))
    return prof[-1][1:]


def inside(prof, p):
    if p.z < prof[0][0] or p.z > prof[-1][0]: return False
    rx, ry, cy = interp(prof, p.z)
    return rx > 1e-6 and (p.x / rx) ** 2 + ((p.y - cy) / ry) ** 2 < 1.0


def exit_t(prof, p1, p2):
    for i in range(201):
        t = i / 200
        if not inside(prof, p1.lerp(p2, t)): return t
    return 1.0


def surf(prof, z, ang, out=0.0):
    """point on the lathe surface at height z, angle ang from +x (ang = -90 deg = front), plus outward normal"""
    rx, ry, cy = interp(prof, z)
    p = V((rx * math.cos(ang), cy + ry * math.sin(ang), z))
    dz = 0.01; rx2, ry2, cy2 = interp(prof, z + dz)
    n = V((math.cos(ang) / rx, math.sin(ang) / ry, 0)).normalized()
    slope = (math.hypot(rx2 * math.cos(ang), ry2 * math.sin(ang)) - math.hypot(rx * math.cos(ang), ry * math.sin(ang))) / dz
    n = (n - V((0, 0, slope))).normalized()
    return p + n * out, n


def bellows(p1, p2, R, r=0.02, m="dark", step=0.034, core="dark2", R2=None):
    p1, p2 = V(p1), V(p2); d = p2 - p1; n = max(2, int(d.length / step)); R2 = R if R2 is None else R2
    cyl(p1, p2, R - r * 0.6, R2 - r * 0.6, m=core, seg=32)
    for i in range(n + 1): tor(p1.lerp(p2, i / n), d, R + (R2 - R) * i / n - r, r, m, nu=48, nv=12)


def tube_path(pts, r, m="steel"):
    pts = [V(p) for p in pts]
    for a, b in zip(pts, pts[1:]): cyl(a, b, r, m=m, seg=20)
    for p in pts: sph(p, r * 1.15, m, seg=20, rings=10)


def drive(c, axis, r=0.045, L=0.06, body="steel", cap="orange"):
    c = V(c); a = V(axis).normalized()
    cyl(c - a * L / 2, c + a * L / 2, r, m=body)
    cyl(c + a * L / 2, c + a * (L / 2 + 0.012), r * 0.8, m=cap)
    cyl(c + a * (L / 2 + 0.012), c + a * (L / 2 + 0.02), r * 0.35, m="metal")


def coil(c, axis, R, r, sy=1.0, house="copper", flange="dark"):
    """field coil: housing torus + two flanges + a thin glowing strip on the outside"""
    c = V(c); a = V(axis).normalized()
    tor(c, a, R, r, house, sy=sy)
    tor(c + a * r * 0.75, a, R, r * 0.42, flange, sy=sy, nv=12)
    tor(c - a * r * 0.75, a, R, r * 0.42, flange, sy=sy, nv=12)
    tor(c, a, R + r * 0.92, r * 0.16, "glow", sy=(R * sy + r * 0.92) / (R + r * 0.92), nv=8)


def gauntlet(J, s, m="dark2", cuff="metal", big=1.0):
    wr, he = J["WR"], J["HE"]; d = (he - wr).normalized()
    tor(wr + d * 0.005, d, 0.062 * big, 0.018 * big, cuff, nu=40, nv=12)
    c = wr + d * 0.085
    sph(c, (0.058 * big, 0.07 * big, 0.105 * big), m, qz(d) @ Quaternion((0, 0, 1), math.radians(-25 * s)))
    sph(c + V((-0.042 * s, -0.012, 0.0)) * big, (0.024 * big, 0.024 * big, 0.045 * big), m, qz(d + V((-0.6 * s, 0, 0))))


# ---------------------------------------------------------------- figure
ARM = None


def joints():
    global ARM
    ARM = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    def h(b): return ARM.matrix_world @ ARM.pose.bones[b].head
    def t(b): return ARM.matrix_world @ ARM.pose.bones[b].tail
    J = {"NECK": h("Neck"), "HEAD": h("Head"), "TOP": t("Head"), "PELVIS": h("Hips")}
    for s, S in ((1, "Left"), (-1, "Right")):
        J[s] = {"SH": h(S + "Arm"), "EL": h(S + "ForeArm"), "WR": h(S + "Hand"), "HE": t(S + "HandFinger1"),
                "HIP": h(S + "UpLeg"), "KN": h(S + "Leg"), "AN": h(S + "Foot"), "TOE": t(S + "ToeBase")}
    return J


def lateral(p1, p2, s, fwd=0.0):
    """unit vector perpendicular to the limb p1->p2 pointing to the figure's outside (s=+1 left), tilted forward by fwd"""
    d = (p2 - p1).normalized(); o = V((s, fwd, 0)); o = o - d * o.dot(d); return o.normalized()


# measured SuitBody cross-sections (z, rx, ry, cy) - bounding ellipse of the torso
TORSO = [(0.86, 0.200, 0.120, -0.020), (0.90, 0.205, 0.120, -0.020), (0.98, 0.192, 0.121, -0.027), (1.06, 0.164, 0.101, -0.048),
         (1.14, 0.140, 0.097, -0.056), (1.22, 0.208, 0.123, -0.064), (1.30, 0.205, 0.137, -0.053), (1.38, 0.210, 0.116, -0.026),
         (1.46, 0.154, 0.081, -0.013)]


def torso_off(z0, z1, k=1.16, add=0.035, n=6, round_=0.012):
    prof = []
    zs = [z0 + (z1 - z0) * i / n for i in range(n + 1)]
    for i, z in enumerate(zs):
        rx, ry, cy = interp(TORSO, z); e = round_ if i in (0, n) else 0.0
        prof.append((z, rx * k + add - e, ry * k + add - e, cy))
    return prof


# ---------------------------------------------------------------- shared pieces
def boots(J, s, m="dark", sole="orange", big=1.0, toe_cap="steel"):
    an, toe = J[s]["AN"], J[s]["TOE"]
    cx = (an.x + toe.x) / 2; cy = (an.y + toe.y) / 2 - 0.005
    w, l, h = 0.15 * big, 0.31 * big, 0.13 * big
    rbox((cx, cy, h / 2 + 0.03), (w, l, h), m, r=0.04)
    rbox((cx, cy, 0.018), (w + 0.012, l + 0.012, 0.036), sole, r=0.012)
    rbox((cx, cy - l / 2 + 0.03, h * 0.42 + 0.03), (w + 0.012, 0.075, h * 0.62), toe_cap, r=0.025)
    cyl((an.x, an.y + 0.01, h * 0.9), (an.x, an.y + 0.01, h + 0.11 * big), 0.075 * big, 0.072 * big, m)


def leg_drives(J, s, body="steel", cap="orange", strut="metal", off=0.13):
    hip, kn, an = J[s]["HIP"], J[s]["KN"], J[s]["AN"]
    o1 = lateral(hip, kn, s); o2 = lateral(kn, an, s)
    ph = hip + V((0, 0, -0.03)) + o1 * (off + 0.02); pk = kn + (o1 + o2).normalized() * off; pa = an + V((0, 0, 0.11)) + o2 * (off - 0.02)
    drive(ph, V((s, 0, 0)), 0.055, 0.06, body, cap); drive(pk, V((s, 0, 0)), 0.05, 0.055, body, cap)
    drive(pa, V((s, 0, 0)), 0.035, 0.04, body, cap)
    for a, b in ((ph, pk), (pk, pa)):
        d = (b - a).normalized(); n = V((s, 0, 0))
        for k in (-1, 1): cyl(a + V((0, 0.022 * k, 0)) - n * 0.02, b + V((0, 0.022 * k, 0)) - n * 0.02, 0.011, m=strut, seg=16)
        cyl(a.lerp(b, 0.2), a.lerp(b, 0.75), 0.02, 0.016, m=body, seg=20)          # linear actuator
    return ph, pk, pa


def arm_drives(J, s, body="steel", cap="orange", strut="metal", off=0.11):
    sh, el, wr = J[s]["SH"], J[s]["EL"], J[s]["WR"]
    o1 = lateral(sh, el, s); o2 = lateral(el, wr, s)
    ps = sh + o1 * (off + 0.04) + V((0, 0, 0.03)); pe = el + (o1 + o2).normalized() * off
    drive(ps, o1, 0.05, 0.05, body, cap); drive(pe, (o1 + o2).normalized(), 0.042, 0.045, body, cap)
    d = (pe - ps); cyl(ps + d * 0.15, ps + d * 0.85, 0.017, m=body, seg=16)
    pw = wr + o2 * (off - 0.03); cyl(pe + (pw - pe) * 0.15, pw, 0.013, m=strut, seg=16)
    return ps, pe


def pack_box(c, size, fins=7, fin_d=0.08, shell="ceramic", fin_m="rad", side_fins=False, vent_side=1):
    c = V(c); w, d, h = size
    rbox(c, size, shell, r=0.04)
    rbox(c + V((0, d / 2 + 0.01, 0)), (w * 0.86, 0.02, h * 0.86), "dark", r=0.008)
    for i in range(fins):
        x = -w * 0.38 + w * 0.76 * i / max(fins - 1, 1)
        rbox(c + V((x, d / 2 + 0.02 + fin_d / 2, 0)), (0.008, fin_d, h * 0.80), fin_m, r=0.003, seg=1)
    if side_fins:
        for k in (-1, 1):
            for i in range(5):
                z = -h * 0.32 + h * 0.64 * i / 4
                rbox(c + V((k * (w / 2 + 0.035), 0.0, z)), (0.07, d * 0.75, 0.008), fin_m, r=0.003, seg=1)
    # evaporator vent on top
    vb = c + V((vent_side * w * 0.28, d * 0.15, h / 2 - 0.01)); vt = vb + V((0, 0.05, 0.09))
    cyl(vb, vt, 0.032, 0.036, "steel"); tor(vt, vt - vb, 0.036, 0.008, "orange", nu=40, nv=10)
    cyl(vt - (vt - vb).normalized() * 0.004, vt + (vt - vb).normalized() * 0.002, 0.028, m="lens", seg=24)
    # battery cassette with orange stripe below
    rbox(c + V((0, 0.0, -h / 2 - 0.055)), (w * 0.9, d * 0.85, 0.11), "dark", r=0.02)
    rbox(c + V((0, d * 0.425 + 0.001, -h / 2 - 0.055)), (w * 0.9 - 0.04, 0.006, 0.02), "orange", r=0.002, seg=1)


def small_visor(c, n, R=0.055, m="visor", rim="steel"):
    c = V(c); n = V(n).normalized()
    cyl(c - n * 0.03, c + n * 0.012, R + 0.012, m=rim)
    tor(c + n * 0.012, n, R + 0.004, 0.016, rim, nu=48, nv=12)
    cyl(c + n * 0.002, c + n * 0.016, R, m=m)


# ---------------------------------------------------------------- variants


# ================================================================ shared design language (rev. 2, 2026-10-16)
# Quilted gold MLI over a rigid shield shell; joints are stacks of bearing rings; field coils sit under the cover as raised
# bands (or exposed, v4); an external servo frame in the «Каркас» logic (hip ring -> hip drive -> thigh link -> knee drive ->
# shin link -> stirrup on the boot sole, spine strut, shoulder yoke, arm links) carries the 130 kg to the ground.
# Every helmet has a quick-opening front: the parts built while GROUP is open move on the hinge in the open shot.
HC = V((0, -0.045, 1.635))                      # helmet centre (head bbox x +-0.10, y -0.17..0.07, z 1.47..1.76)
GROUP = []
HINGE = {}


def mat_mli(name, base, k=55.0, pattern="diamond", bump=0.55, sharpness=0.3):
    m = bpy.data.materials.new("HS_" + name); m.use_nodes = True; nt = m.node_tree
    p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Metallic"].default_value = 1.0
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    sp = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(geo.outputs["Position"], sp.inputs[0])
    sn = nt.nodes.new("ShaderNodeSeparateXYZ"); nt.links.new(geo.outputs["Normal"], sn.inputs[0])

    def Mth(op, a, b=None, clamp=False):
        n = nt.nodes.new("ShaderNodeMath"); n.operation = op; n.use_clamp = clamp
        for i, v in enumerate((a, b)):
            if v is None: continue
            if isinstance(v, (int, float)): n.inputs[i].default_value = v
            else: nt.links.new(v, n.inputs[i])
        return n.outputs[0]
    X, Y, Z = sp.outputs[0], sp.outputs[1], sp.outputs[2]
    def wave(a): return Mth('ABSOLUTE', Mth('SINE', Mth('MULTIPLY', a, k)))
    if pattern == "diamond":
        f = Mth('MULTIPLY', wave(Mth('ADD', X, Z)), wave(Mth('SUBTRACT', X, Z)))
        g = Mth('MULTIPLY', wave(Mth('ADD', Y, Z)), wave(Mth('SUBTRACT', Y, Z)))
    elif pattern == "bands":
        f = Mth('MULTIPLY', wave(Z), Mth('ADD', Mth('MULTIPLY', wave(Mth('MULTIPLY', Mth('ADD', X, Y), 0.35)), 0.35), 0.65)); g = f
    else:
        f = Mth('MULTIPLY', wave(X), wave(Z)); g = Mth('MULTIPLY', wave(Y), wave(Z))
    w = Mth('MULTIPLY', Mth('SUBTRACT', Mth('ABSOLUTE', sn.outputs[0]), 0.4), 4.0, clamp=True)
    h = Mth('ADD', f, Mth('MULTIPLY', Mth('SUBTRACT', g, f), w))
    h2 = Mth('POWER', h, sharpness)
    vm = nt.nodes.new("ShaderNodeVectorMath"); vm.operation = 'SCALE'; vm.inputs[0].default_value = base
    nt.links.new(Mth('ADD', Mth('MULTIPLY', h2, 0.6), 0.4), vm.inputs["Scale"]); nt.links.new(vm.outputs[0], p.inputs["Base Color"])
    nt.links.new(Mth('SUBTRACT', 0.42, Mth('MULTIPLY', h2, 0.24)), p.inputs["Roughness"])
    b = nt.nodes.new("ShaderNodeBump"); b.inputs["Strength"].default_value = bump; b.inputs["Distance"].default_value = 0.004
    nt.links.new(h2, b.inputs["Height"]); nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    m.diffuse_color = (*base, 1); M[name] = m; return m


def make_materials2():
    mat_mli("mli", (1.0, 0.70, 0.30), 52.0, "diamond")
    mat_mli("mli_big", (1.0, 0.68, 0.28), 38.0, "diamond", 0.6)
    mat_mli("mli_rect", (0.98, 0.69, 0.30), 45.0, "rect")
    mat_mli("mli_fine", (1.0, 0.73, 0.34), 75.0, "diamond", 0.3, 0.2)
    mat_mli("mli_band", (1.0, 0.71, 0.32), 40.0, "bands", 0.75, 0.35)
    mat("bronze", (0.42, 0.26, 0.11), metal=1.0, rough=0.35)
    mat("lgreen", (0.2, 1, 0.3), emit=((0.2, 1.0, 0.3), 12.0)); mat("lamber", (1, 0.6, 0.1), emit=((1.0, 0.55, 0.1), 12.0))
    mat("lred", (1, 0.15, 0.1), emit=((1.0, 0.12, 0.08), 12.0))
    g = mat("glass", (0.92, 0.95, 0.95), rough=0.02)
    p = next(n for n in g.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); p.inputs["Transmission Weight"].default_value = 1.0
    p.inputs["IOR"].default_value = 1.45
    g = mat("glass_gold", (1.0, 0.82, 0.5), rough=0.03)
    p = next(n for n in g.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); p.inputs["Transmission Weight"].default_value = 1.0
    p.inputs["IOR"].default_value = 1.5


def mk2(vs, fs, mats, fm, loc=(0, 0, 0), q=None, sharp=0.6, name="hs"):
    import bmesh
    me = bpy.data.meshes.new(name); me.from_pydata([tuple(v) for v in vs], [], fs)
    for mn in mats: me.materials.append(M[mn])
    me.polygons.foreach_set("material_index", fm)
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.recalc_face_normals(bm, faces=bm.faces); bm.to_mesh(me); bm.free(); me.validate()
    try:
        me.shade_smooth(); me.set_sharp_from_angle(angle=sharp)
    except Exception:
        for p in me.polygons: p.use_smooth = True
    o = bpy.data.objects.new(name, me); COLL[0].objects.link(o)
    o.location = V(loc); o.rotation_mode = 'QUATERNION'
    if q is not None: o.rotation_quaternion = q
    for g in GROUP: g.append(o)
    return o


def grid_solid(fo, fi, nu, nv, wrap=False):
    vs = []; fs = []; fm = []
    def idx(k, i, j): return k * (nu + 1) * (nv + 1) + j * (nu + 1) + i
    for f in (fo, fi):
        for j in range(nv + 1):
            for i in range(nu + 1): vs.append(tuple(f(i / nu, j / nv)))
    for j in range(nv):
        for i in range(nu):
            fs.append((idx(0, i, j), idx(0, i + 1, j), idx(0, i + 1, j + 1), idx(0, i, j + 1))); fm.append(0)
            fs.append((idx(1, i, j + 1), idx(1, i + 1, j + 1), idx(1, i + 1, j), idx(1, i, j))); fm.append(1)
    for i in range(nu):
        for j in (0, nv): fs.append((idx(0, i, j), idx(1, i, j), idx(1, i + 1, j), idx(0, i + 1, j))); fm.append(0)
    if not wrap:
        for j in range(nv):
            for i in (0, nu): fs.append((idx(0, i, j), idx(0, i, j + 1), idx(1, i, j + 1), idx(1, i, j))); fm.append(0)
    return vs, fs, fm


SE_N = [2.0]


def _se(t, n):
    c, s_ = math.cos(t), math.sin(t)
    if n == 2.0: return c, s_
    return math.copysign(abs(c) ** (2 / n), c), math.copysign(abs(s_) ** (2 / n), s_)


def shell(c, r, az, el, t=0.014, m="mli", m_in="dark2", q=None, nu=48, nv=24):
    """thick patch of an ellipsoid (superellipsoid with exponent SE_N) around c: azimuth from +x (front = -90), elevation in degrees"""
    a0, a1 = map(math.radians, az); e0, e1 = map(math.radians, el); n = SE_N[0]
    def P(rr):
        def f(u, v):
            ce, se_ = _se(e0 + (e1 - e0) * v, n); ca, sa = _se(a0 + (a1 - a0) * u, n)
            pt = (rr[0] * ce * ca, rr[1] * ce * sa, rr[2] * se_)
            return DEFORM[0](*pt) if DEFORM[0] else pt
        return f
    wrap = (a1 - a0) >= 2 * math.pi - 1e-6; sharp = 1.0
    if FACET[0]:
        nu = max(1, round(math.degrees(a1 - a0) / 30)); nv = max(1, round(math.degrees(e1 - e0) / 24)); sharp = 0.3
    vs, fs, fm = grid_solid(P(r), P(tuple(x - t for x in r)), nu, nv, wrap)
    return mk2(vs, fs, [m, m_in], fm, c, q, sharp=sharp)


def lathe_part(P, z0, z1, az, add=0.012, t=0.012, m="dark", m_in="dark2", nu=40, nv=8):
    a0, a1 = map(math.radians, az)
    def F(off):
        def f(u, v):
            z = z0 + (z1 - z0) * v; rx, ry, cy = interp(P, z); a = a0 + (a1 - a0) * u
            return ((rx + off) * math.cos(a), cy + (ry + off) * math.sin(a), z)
        return f
    vs, fs, fm = grid_solid(F(add), F(add - t), nu, nv)
    return mk2(vs, fs, [m, m_in], fm, sharp=1.0)


def bearing(p, d, R, n=3, m="metal", gap=0.022, r=0.011):
    p = V(p); d = V(d).normalized(); L = gap * (n - 1)
    cyl(p - d * (L / 2 + r), p + d * (L / 2 + r), R - 0.005, m="dark2", seg=40)
    for i in range(n): tor(p + d * (-L / 2 + gap * i), d, R + (0.005 if i % 2 else 0.0), r, m, nu=56, nv=12)


def c_cuff(p, d, o, R, g=120, r=0.011, m="steel"):
    d = d.normalized(); f = d.cross(o).normalized()
    tube_path([p + (o * math.cos(math.radians(a)) + f * math.sin(math.radians(a))) * R for a in range(-g, g + 1, 20)], r, m)


def torso_shell(k=1.16, add=0.05, m="mli", z0=0.84):
    P = torso_off(z0, 1.46, k, add, n=8, round_=0.0)
    P = [(z0 - 0.015, P[0][1] - 0.03, P[0][2] - 0.03, P[0][3])] + P + [(1.49, 0.2, 0.18, -0.035), (1.505, 0.17, 0.155, -0.04)]
    lathe(P, m)
    rx, ry, cy = interp(P, z0 + 0.005)
    if FACET[0]:
        pts = octo(P, z0 + 0.005, 0.005)
        for k in range(8): seg_box(pts[k], pts[(k + 1) % 8], 0.03, 0.035, "dark", ext=0.025)
    else: tor((0, cy, z0 + 0.005), (0, 0, 1), rx, 0.02, "dark", sy=ry / rx)          # shell rim
    return P


def coil_band(P, z, m="mli", r=0.026):
    rx, ry, cy = interp(P, z); tor((0, cy, z), (0, 0, 1), rx + 0.004, r, m, sy=(ry + 0.004) / (rx + 0.004))
    tor((0, cy, z), (0, 0, 1), rx + r * 0.95, 0.004, "glow", sy=(ry + r * 0.95) / (rx + r * 0.95), nv=8)


def collar_seal():
    tor((0, -0.045, 1.505), (0, 0, 1), 0.142, 0.024, "steel", sy=1.1)
    tor((0, -0.045, 1.53), (0, 0, 1), 0.132, 0.009, "orange", sy=1.1)


def chest_panel(P, z=1.26, w=0.17, h=0.1, lamps=True):
    p, n = surf(P, z, -math.pi / 2); up = V((0, 0, 1)) - n * n.z; up.normalize(); rt = up.cross(n).normalized()
    rbox(p + n * 0.012, (w, 0.03, h), "dark", n.to_track_quat('-Y', 'Z'), r=0.01)
    for i, c in enumerate(("lgreen", "lgreen", "lamber", "lred")):
        sph(p + n * 0.03 + rt * (-0.06 + 0.024 * i) + up * 0.022, 0.0075, c, seg=16, rings=8)
    cyl(p + n * 0.02 + rt * 0.045 - up * 0.015, p + n * 0.036 + rt * 0.045 - up * 0.015, 0.024, m="steel", seg=24)
    cyl(p + n * 0.034 + rt * 0.045 - up * 0.015, p + n * 0.039 + rt * 0.045 - up * 0.015, 0.018, m="lamp", seg=24)
    rbox(p + n * 0.03 - rt * 0.03 - up * 0.02, (0.07, 0.01, 0.022), "lens", n.to_track_quat('-Y', 'Z'), r=0.003, seg=1)
    if lamps:
        for k in (-1, 1):
            q_, nq = surf(P, 1.385, -math.pi / 2 + k * 0.62)
            cyl(q_ - nq * 0.01, q_ + nq * 0.03, 0.022, m="steel", seg=24); cyl(q_ + nq * 0.028, q_ + nq * 0.033, 0.016, m="lamp", seg=24)


def limbs(J, P, s, m="mli", ring="metal", rs=1.0, glove="dark2", boot=("dark", "orange", 1.15, "steel"), fn=None):
    j = J[s]; sh, el, wr = j["SH"], j["EL"], j["WR"]; d = (el - sh).normalized(); d2 = (wr - el).normalized()
    t = exit_t(P, sh, el) if fn is None else next((i / 200 for i in range(201) if not fn(sh.lerp(el, i / 200))), 1.0)
    pe = sh.lerp(el, t)
    bearing(pe, d, 0.112 * rs, 3, ring)
    cyl(pe + d * 0.03, el - d * 0.06, 0.1 * rs, 0.092 * rs, m)
    bi = (d + d2).normalized(); sph(el, 0.086 * rs, "dark2"); bearing(el, bi, 0.096 * rs, 3, ring)
    cyl(el + d2 * 0.06, wr - d2 * 0.03, 0.09 * rs, 0.08 * rs, m)
    bearing(wr - d2 * 0.025, d2, 0.084 * rs, 2, ring)
    gauntlet(j, s, glove, ring, 1.15)
    h_, k_, a_ = j["HIP"], j["KN"], j["AN"]; dl = (k_ - h_).normalized(); ds = (a_ - k_).normalized()
    top = h_.lerp(k_, (h_.z - 0.86) / (h_.z - k_.z)); hb = h_.lerp(k_, (h_.z - 0.79) / (h_.z - k_.z))
    cyl(top, k_ - dl * 0.07, 0.14 * rs, 0.12 * rs, m); bearing(hb, dl, 0.143 * rs, 2, ring)
    sph(k_, 0.112 * rs, "dark2"); bearing(k_, (dl + ds).normalized(), 0.124 * rs, 3, ring)
    cyl(k_ + ds * 0.065, a_ + V((0, 0, 0.2)), 0.122 * rs, 0.104 * rs, m)
    bearing(a_ + V((0, 0, 0.19)), (0, 0, 1), 0.11 * rs, 2, ring)
    boots(J, s, *boot)


def frame_ext(J, P, heavy=1.0, body="steel", cap="orange", front_bars=False):
    rr = 0.016 * heavy
    zr = 0.95; rx, ry, cy = interp(P, zr); RX, RY = rx + 0.04, ry + 0.04
    tor((0, cy, zr), (0, 0, 1), RX, rr * 1.1, body, sy=RY / RX)                                   # hip ring
    _, _, cyb = interp(P, 1.2); yb = cyb + interp(P, 1.2)[1]
    for k in (-1, 1):                                                                                 # spine strut
        tube_path([(k * 0.075, cy + RY, zr), (k * 0.075, yb + 0.02, 1.15), (k * 0.085, yb + 0.0, 1.47)], rr, body)
    for k in (0.98, 1.3): tube_path([(-0.075, yb + 0.02, k), (0.075, yb + 0.02, k)], rr * 0.8, body)
    yoke = {}
    for s in (1, -1):                                                                                 # shoulder yoke arcs
        x = s * 0.2
        def yfb(z, sign):
            rx_, ry_, cy_ = interp(P, z); return cy_ + sign * (ry_ * math.sqrt(max(0.0, 1 - (0.2 / rx_) ** 2)) + 0.022)
        yf, yb2 = yfb(1.30, -1), yfb(1.30, 1); mid = (yf + yb2) / 2; hd = (yb2 - yf) / 2
        arc = [(x, mid - hd * math.cos(math.pi * i / 10), 1.30 + 0.235 * math.sin(math.pi * i / 10)) for i in range(11)]
        tube_path(arc, rr, body); yoke[s] = V(arc[5])
        if front_bars:
            tube_path([(s * 0.12, interp(P, 0.96)[2] - interp(P, 0.96)[1] - 0.03, 0.96), (s * 0.12, yfb(1.12, -1) - 0.02, 1.12), (x * 0.95, yf, 1.30)], rr, body)
    for s in (1, -1):
        j = J[s]
        ph, pk, pa = leg_drives(J, s, body, cap, "metal", off=0.19 if heavy < 1.3 else 0.21)
        for p_ in (ph, pk): pass
        tube_path([V((s * RX * 0.97, cy, zr)), ph], rr, body)                                         # hip drive bracket
        an = j["AN"]; bx = s * (abs((an.x + j["TOE"].x) / 2) + 0.1)
        tube_path([pa, V((pa.x, pa.y, 0.07)), V((bx, pa.y, 0.07))], rr, body)                          # stirrup to the sole
        rbox((bx, pa.y - 0.02, 0.07), (0.022, 0.18, 0.07), body, r=0.008)
        h_, k_, a_ = j["HIP"], j["KN"], j["AN"]
        c_cuff(h_.lerp(k_, 0.55), k_ - h_, lateral(h_, k_, s), 0.16, r=rr * 0.75, m=body)
        c_cuff(k_.lerp(a_, 0.45), a_ - k_, lateral(k_, a_, s), 0.14, r=rr * 0.75, m=body)
        # arm: shoulder drive on the yoke, links to the elbow drive and a wrist cuff
        sh, el, wr = j["SH"], j["EL"], j["WR"]; o1 = lateral(sh, el, s); o2 = lateral(el, wr, s); d2 = (wr - el).normalized()
        p_s, n_s = surf(P, 1.40, 0.0 if s > 0 else math.pi); ps = p_s + n_s * 0.035
        drive(ps, n_s, 0.05 * heavy ** 0.5, 0.05, body, cap); tube_path([yoke[s], ps], rr, body)
        oe = (o1 + o2).normalized(); pe2 = el + oe * 0.135
        drive(pe2, oe, 0.045 * heavy ** 0.5, 0.045, body, cap)
        tube_path([ps + (pe2 - ps).normalized() * 0.03, pe2], rr, body)
        cyl(ps.lerp(pe2, 0.25), ps.lerp(pe2, 0.7), rr * 1.6, rr * 1.3, body, seg=20)
        pw = wr - d2 * 0.05 + o2 * 0.105; tube_path([pe2, pw], rr * 0.85, body)
        c_cuff(wr - d2 * 0.05, d2, o2, 0.105, r=rr * 0.7, m=body)


def helm_begin(): GROUP.append([])


def helm_end(i, pivot, axis=(1, 0, 0), angle=0.0, move=(0, 0, 0)):
    """records one opening stage of variant i; stages are applied in the order they were ended"""
    HINGE.setdefault(str(i), []).append((GROUP.pop(), V(pivot), V(axis).normalized(), math.radians(angle), V(move)))


def hpt(r, az, el):
    a, e = math.radians(az), math.radians(el)
    return HC + V((r[0] * math.cos(e) * math.cos(a), r[1] * math.cos(e) * math.sin(a), r[2] * math.sin(e)))


def inner_glass(r=(0.142, 0.165, 0.178), az=(-150, -30), el=(-30, 50)):
    shell(HC, r, az, el, 0.004, "glass", "glass", nu=32, nv=16)


def slit_block(c, w=0.19, h=0.11, d=0.075, tilt=12, slit=(0.15, 0.022), m="steel", vertical=False):
    q = Quaternion((1, 0, 0), math.radians(tilt)); c = V(c); fwd = q @ V((0, -1, 0)); up = q @ V((0, 0, 1))
    rbox(c, (w, d, h), m, q, r=0.018)
    rbox(c + fwd * (d / 2 + 0.002), (slit[0] + 0.022, 0.01, slit[1] + 0.02), "dark", q, r=0.005)
    rbox(c + fwd * (d / 2 + 0.006), (slit[0], 0.01, slit[1]), "visor", q, r=0.004)
    return q, fwd, up


# ---------------------------------------------------------------- the five variants
def v1(J):  # slit visor in a forward-swept armored hood; the hood front swings up over the crown
    P = torso_shell(1.16, 0.05, "mli"); collar_seal()
    coil_band(P, 1.05); coil_band(P, 1.425); chest_panel(P)
    lathe_part(P, 1.16, 1.36, (-140, -40), 0.014, 0.014, "dark")                  # exposed shield-shell breastplate edge
    for s in (1, -1): limbs(J, P, s, "mli")
    frame_ext(J, P)
    rx, ry, cy = interp(P, 1.22); pack_box((0, cy + ry + 0.035 + 0.125, 1.22), (0.48, 0.23, 0.58), 9, 0.09, "mli_big")
    R = (0.168, 0.195, 0.205)
    shell(HC, R, (-30, 210), (-38, 89.5), 0.016, "mli")
    for s in (1, -1):                                                                  # hinge hubs at the ears
        cyl(HC + V((s * 0.165, 0, 0)), HC + V((s * 0.2, 0, 0)), 0.04, m="steel"); cyl(HC + V((s * 0.2, 0, 0)), HC + V((s * 0.207, 0, 0)), 0.022, m="orange")
    helm_begin()
    shell(HC, tuple(x + 0.008 for x in R), (-152, -28), (-38, 86), 0.014, "mli")
    q, fwd, up = slit_block(HC + V((0, -0.222, 0.0)), 0.2, 0.115, 0.08, 14, (0.15, 0.02))
    rbox(HC + V((0, -0.215, 0.1)), (0.26, 0.13, 0.026), "mli", Quaternion((1, 0, 0), math.radians(20)), r=0.012)   # swept brow
    sh_c = HC + V((0, -0.262, 0.085)); qs = Quaternion((1, 0, 0), math.radians(-48))
    rbox(sh_c, (0.17, 0.014, 0.05), "steel", qs, r=0.006); rbox(sh_c + qs @ V((0, -0.008, -0.022)), (0.17, 0.006, 0.008), "orange", qs, r=0.002, seg=1)
    for s in (1, -1):
        cyl(HC + V((s * 0.09, -0.258, 0.06)), HC + V((s * 0.105, -0.258, 0.06)), 0.012, m="steel", seg=16)
    inner_glass()
    helm_end(1, HC, (1, 0, 0), -108)


def v2(J):  # domed hood with a vertical periscope slit block and twin side cameras; the front swings sideways like a door
    P = torso_shell(1.16, 0.05, "mli_big"); collar_seal()
    coil_band(P, 1.05, "mli_big"); coil_band(P, 1.425, "mli_big"); chest_panel(P, 1.24, 0.15, 0.12)
    lathe_part(P, 1.12, 1.40, (-150, -30), 0.016, 0.016, "steel")                 # armored chest plate over the cover
    for s in (1, -1): limbs(J, P, s, "mli_big", boot=("dark", "orange", 1.15, "dark2"))
    frame_ext(J, P)
    rx, ry, cy = interp(P, 1.22); pack_box((0, cy + ry + 0.035 + 0.11, 1.22), (0.46, 0.2, 0.6), 7, 0.08, "mli_big", side_fins=True)
    R = (0.175, 0.19, 0.225)
    shell(HC, R, (-30, 210), (-38, 89.5), 0.016, "mli_big"); shell(HC, R, (-150, -30), (68, 89.5), 0.016, "mli_big")
    tor(HC + V((0, 0, 0.17)), (0, 0, 1), 0.105, 0.012, "steel", sy=1.08)
    pv = hpt(tuple(x + 0.008 for x in R), -150, 0)
    for zz in (-0.09, 0.07): cyl(pv + V((0, 0, zz - 0.03)), pv + V((0, 0, zz + 0.03)), 0.016, m="steel", seg=20)   # door hinge knuckles
    helm_begin()
    shell(HC, tuple(x + 0.008 for x in R), (-150, -30), (-38, 69), 0.014, "mli_big")
    q = Quaternion((1, 0, 0), 0); c = HC + V((0, -0.212, 0.01))
    rbox(c, (0.08, 0.075, 0.16), "steel", r=0.018); rbox(c + V((0, 0.0, 0.095)), (0.09, 0.09, 0.03), "steel", Quaternion((1, 0, 0), math.radians(18)), r=0.01)
    rbox(c + V((0, -0.039, 0)), (0.036, 0.01, 0.13), "dark", r=0.004); rbox(c + V((0, -0.043, 0)), (0.016, 0.01, 0.105), "visor", r=0.004)
    for s in (1, -1):                                                                  # twin side cameras at the temples
        p = hpt(tuple(x + 0.008 for x in R), -90 + s * 42, 12)
        rbox(p + V((0, -0.005, 0)), (0.05, 0.05, 0.05), "dark", r=0.012)
        cyl(p + V((0, -0.02, 0)), p + V((0, -0.06, 0)), 0.02, m="steel", seg=24); cyl(p + V((0, -0.06, 0)), p + V((0, -0.066, 0)), 0.015, m="lens", seg=24)
    inner_glass()
    helm_end(2, pv, (0, 0, 1), -108)


def v3(J):  # flip-up armored shutter over a wide gold window; the window module flips up-forward on a brow hinge
    P = torso_shell(1.16, 0.05, "mli_rect"); collar_seal()
    coil_band(P, 1.05, "mli_rect"); coil_band(P, 1.425, "mli_rect"); chest_panel(P, 1.27, 0.19, 0.09)
    lathe_part(P, 0.9, 1.0, (-160, -20), 0.014, 0.014, "dark")
    for s in (1, -1): limbs(J, P, s, "mli_rect", ring="steel")
    frame_ext(J, P)
    rx, ry, cy = interp(P, 1.22); pc = V((0, cy + ry + 0.035 + 0.11, 1.22))
    pack_box(pc, (0.42, 0.2, 0.56), 5, 0.06, "mli_rect")
    for s in (1, -1):                                                                  # radiator wings on the pack
        qw = Quaternion((0, 0, 1), math.radians(-s * 30)); wc = pc + V((s * 0.32, 0.08, 0.0))
        rbox(wc, (0.26, 0.022, 0.5), "rad", qw, r=0.006)
        for kk in range(6): rbox(wc + qw @ V((-0.1 + 0.04 * kk, 0.018, 0)), (0.006, 0.02, 0.46), "steel", qw, r=0.002, seg=1)
        tube_path([pc + V((s * 0.2, 0.05, 0.2)), wc + qw @ V((-s * 0.1, 0, 0.2))], 0.012); tube_path([pc + V((s * 0.2, 0.05, -0.2)), wc + qw @ V((-s * 0.1, 0, -0.2))], 0.012)
    R = (0.17, 0.195, 0.205)
    shell(HC, R, (-30, 210), (-38, 89.5), 0.016, "mli_rect"); shell(HC, R, (-150, -30), (46, 89.5), 0.016, "mli_rect")
    pv = hpt(tuple(x + 0.012 for x in R), -90, 46)
    cyl(pv - V((0.11, 0, 0)), pv + V((0.11, 0, 0)), 0.015, m="steel");
    for s in (1, -1): cyl(pv + V((s * 0.11, 0, 0)), pv + V((s * 0.12, 0, 0)), 0.02, m="orange")
    helm_begin()
    Rm = tuple(x + 0.008 for x in R)
    shell(HC, Rm, (-150, -30), (-38, 46), 0.014, "mli_rect")
    shell(HC, tuple(x + 0.012 for x in R), (-140, -40), (-15, 22), 0.016, "steel")
    shell(HC, tuple(x + 0.018 for x in R), (-133, -47), (-10, 17), 0.008, "visor", "visor")
    qs = Quaternion((1, 0, 0), math.radians(-30))                                       # the shutter, half raised
    shell(HC, tuple(x + 0.03 for x in R), (-141, -39), (-14, 21), 0.012, "steel", q=qs)
    shell(HC, tuple(x + 0.034 for x in R), (-141, -39), (-14, -10), 0.006, "orange", q=qs)
    inner_glass()
    helm_end(3, pv, (1, 0, 0), -95)


def v4(J):  # boxy segmented plate torso with exposed coil rings; the external frame dominates; the face plate slides up on rails
    core = torso_shell(1.12, 0.03, "dark2"); collar_seal()
    P = [(z, rx * 1.03 + 0.02, ry * 1.03 + 0.02, cy) for z, rx, ry, cy in core]
    plates = ((0.84, 0.985), (1.025, 1.165), (1.19, 1.325), (1.36, 1.475))
    def box_in(p):
        for z0, z1 in plates:
            if z0 <= p.z <= z1:
                rx, ry, cy = interp(P, (z0 + z1) / 2); return abs(p.x) < rx + 0.01 and abs(p.y - cy) < ry + 0.01
        return False
    for z0, z1 in plates:
        rx, ry, cy = interp(P, (z0 + z1) / 2)
        rbox((0, cy, (z0 + z1) / 2), (2 * rx + 0.02, 2 * ry + 0.02, z1 - z0), "mli_rect", r=0.045)
        rbox((0, cy - ry - 0.012, (z0 + z1) / 2), (rx * 0.9, 0.012, (z1 - z0) * 0.8), "dark", r=0.004, seg=1)
    for z in (1.005, 1.343):
        rx, ry, cy = interp(P, z); coil((0, cy, z), (0, 0, 1), rx + 0.03, 0.03, sy=(ry + 0.03) / (rx + 0.03), house="copper")
    p0, n0 = surf(P, 1.255, -math.pi / 2); chest_panel([(z, rx + 0.012, ry + 0.012, cy) for z, rx, ry, cy in P], 1.255, 0.16, 0.1, lamps=False)
    for s in (1, -1):
        rx, ry, cy = interp(P, 1.42); q_ = V((s * 0.13, cy - ry - 0.012, 1.42))
        cyl(q_, q_ + V((0, -0.035, 0)), 0.022, m="steel", seg=24); cyl(q_ + V((0, -0.033, 0)), q_ + V((0, -0.038, 0)), 0.016, m="lamp", seg=24)
    for s in (1, -1): limbs(J, P, s, "mli_rect", ring="steel", rs=1.05, boot=("steel", "orange", 1.3, "dark"), fn=box_in)
    frame_ext(J, P, 1.45, "dark", "orange", front_bars=True)
    for k in (-1, 1):                                                                       # tanks on the frame + radiator stack
        cyl((k * 0.14, 0.33, 0.95), (k * 0.14, 0.33, 1.5), 0.1, m="mli_rect"); sph((k * 0.14, 0.33, 1.5), (0.1, 0.1, 0.05), "steel")
        sph((k * 0.14, 0.33, 0.95), (0.1, 0.1, 0.04), "steel"); tor((k * 0.14, 0.33, 1.3), (0, 0, 1), 0.1, 0.012, "orange")
    rbox((0, 0.26, 1.22), (0.5, 0.05, 0.56), "dark", r=0.02)
    for i in range(7): rbox((0, 0.35, 0.98 + i * 0.08), (0.13, 0.15, 0.008), "rad", r=0.003, seg=1)
    vb = V((0, 0.34, 1.53)); vt = vb + V((0, 0.06, 0.08)); cyl(vb, vt, 0.034, 0.04, "steel"); tor(vt, vt - vb, 0.04, 0.009, "orange")
    rbox((0, 0.3, 0.86), (0.42, 0.16, 0.1), "dark", r=0.02); rbox((0, 0.381, 0.86), (0.36, 0.004, 0.02), "orange", r=0.001, seg=1)
    # boxy hood: back, sides, top fixed; rails on the front corners
    c = HC + V((0, 0, 0.01))
    c = HC + V((0, 0, -0.01))
    rbox(c + V((0, 0.13, 0)), (0.30, 0.05, 0.30), "mli_rect", r=0.02)
    for s in (1, -1): rbox(c + V((s * 0.152, -0.03, 0)), (0.045, 0.36, 0.30), "mli_rect", r=0.02)
    rbox(c + V((0, -0.03, 0.163)), (0.35, 0.36, 0.045), "mli_rect", r=0.02)
    for s in (1, -1):
        rbox(c + V((s * 0.14, -0.205, 0.0)), (0.024, 0.024, 0.34), "steel", r=0.006)
        rbox(c + V((s * 0.14, -0.205, 0.175)), (0.036, 0.036, 0.018), "orange", r=0.005)
    helm_begin()
    rbox(c + V((0, -0.19, -0.005)), (0.26, 0.04, 0.29), "mli_rect", r=0.015)
    slit_block(c + V((0, -0.235, 0.0)), 0.22, 0.085, 0.06, 0, (0.17, 0.02))
    rbox(c + V((0, -0.226, 0.08)), (0.2, 0.024, 0.05), "steel", Quaternion((1, 0, 0), math.radians(-30)), r=0.006)
    for s in (1, -1):
        for zz in (-0.1, 0.1): rbox(c + V((s * 0.14, -0.222, zz)), (0.04, 0.02, 0.05), "dark", r=0.005)
    rbox(c + V((0, -0.168, 0.0)), (0.24, 0.005, 0.2), "glass", r=0.002, seg=1)
    helm_end(4, c, (1, 0, 0), 0, (0, 0, 0.2))


def v5(J):  # smooth lighter shell, frame hidden in the cover, field-coil halo in the shoulder yoke; the front retracts into the crown
    P = torso_shell(1.2, 0.055, "mli_fine"); collar_seal(); chest_panel(P, 1.26, 0.14, 0.08)
    rx, ry, cy = interp(P, 1.47)
    tor((0, -0.04, 1.475), (0, 0, 1), 0.3, 0.05, "mli_fine", sy=0.88)                         # shoulder-yoke halo coil
    tor((0, -0.04, 1.475), (0, 0, 1), 0.3 + 0.048, 0.006, "glow", sy=(0.3 * 0.88 + 0.048) / 0.348, nv=8)
    for s in (1, -1):
        tor((0, -0.04, 1.475 + s * 0.035), (0, 0, 1), 0.3, 0.012, "steel", sy=0.88)
        limbs(J, P, s, "mli_fine", ring="metal", rs=1.02, boot=("mli_fine", "orange", 1.18, "steel"))
        j = J[s]
        for a, b, rr in ((j["HIP"], j["KN"], 0.15), (j["KN"], j["AN"], 0.125)):        # drive covers (frame inside the cover)
            o = lateral(a, b, s); pp = a.lerp(b, 0.05 if a is j["HIP"] else 0.0) + o * rr
            sph(pp, (0.045, 0.07, 0.07), "mli_fine")
        p_s, n_s = surf(P, 1.36, 0.0 if s > 0 else math.pi); sph(p_s, (0.05, 0.08, 0.08), "mli_fine")
    rx, ry, cy = interp(P, 1.22); pc = V((0, cy + ry + 0.1, 1.22))
    sph(pc, (0.23, 0.14, 0.32), "mli_fine")
    for k in range(-3, 4):
        a = math.radians(90 + k * 15); dv = V((math.cos(a), math.sin(a), 0))
        rbox(pc + dv * 0.14, (0.008, 0.08, 0.44), "rad", Quaternion((0, 0, 1), a - math.pi / 2), r=0.003, seg=1)
    vb = pc + V((0.1, 0.03, 0.28)); vt = vb + V((0, 0.05, 0.08)); cyl(vb, vt, 0.03, 0.034, "steel"); tor(vt, vt - vb, 0.034, 0.008, "orange")
    rbox(pc + V((0, -0.02, -0.36)), (0.38, 0.18, 0.1), "dark", r=0.03)
    R = (0.17, 0.195, 0.215)
    shell(HC, R, (-30, 210), (-38, 89.5), 0.014, "mli_fine"); shell(HC, R, (-150, -30), (52, 89.5), 0.014, "mli_fine")
    for s in (1, -1): sph(HC + V((s * 0.168, 0.0, 0.0)), (0.012, 0.05, 0.05), "steel")
    helm_begin()
    Rm = tuple(x - 0.018 for x in R)
    shell(HC, Rm, (-152, -28), (-38, 54), 0.012, "mli_fine")
    shell(HC, tuple(x + 0.004 for x in Rm), (-126, -54), (-5, 9), 0.008, "dark")
    shell(HC, tuple(x + 0.008 for x in Rm), (-122, -58), (-2, 6), 0.006, "visor", "visor")
    for s in (1, -1):
        p = hpt(tuple(x + 0.004 for x in Rm), -90 + s * 40, -14); cyl(p, p + (p - HC).normalized() * 0.012, 0.012, m="lamp", seg=20)
    inner_glass((0.13, 0.155, 0.168), (-150, -30), (-30, 48))
    helm_end(5, HC, (1, 0, 0), -118)


# ================================================================ rev. 3: angular refinements of 1 and 3 + hybrid 13
# The user picked 1 «Капюшон-щель» and 3 «Шторка» but found them too round. With FACET on, the big primitives become hard
# shells: cylinders/spheres -> octagonal/faceted, rounded boxes -> chamfered, the torso lathe -> an octagonal cuirass
# (flat chest plane in front), helmet shells -> flat panels. The quilted MLI stays on the flat panels between rigid
# steel edges (corner strips, angular coil bands, crest ridge). Bearing-ring stacks and the frame rods stay round.
from mathutils import Matrix
KF = 1.0 / math.cos(math.pi / 8)


def seg_box(a, b, w, h, m, up=(0, 0, 1), ext=0.0, r=0.004):
    a, b = V(a), V(b); d = b - a; L = d.length; d.normalize(); up = V(up)
    z = (up - d * up.dot(d)).normalized(); x = d.cross(z)
    return rbox((a + b) / 2, (w, L + ext, h), m, Matrix((x, d, z)).transposed().to_quaternion(), r=r)


def octo(P, z, off=0.0):
    rx, ry, cy = interp(P, z)
    return [V(((rx * KF + off) * math.cos(a), cy + (ry * KF + off) * math.sin(a), z)) for a in (math.radians(22.5 + 45 * k) for k in range(8))]


def octo_band(P, z, m="mli", w=0.03, h=0.045, glow=True):
    pts = octo(P, z, 0.012)
    for k in range(8): seg_box(pts[k], pts[(k + 1) % 8], w, h, m, ext=w * 0.85)
    if glow:
        g = octo(P, z, 0.012 + w * 0.5)
        for k in range(8): seg_box(g[k], g[(k + 1) % 8], 0.006, 0.008, "glow", ext=0.004, r=0.001)


def corner_strips(P, z0=0.87, z1=1.46, m="steel"):
    zs = [z0 + (z1 - z0) * i / 8 for i in range(9)]
    rows = [octo(P, z, 0.003) for z in zs]
    for k in range(8):
        for a, b in zip(rows, rows[1:]):
            outw = V((a[k].x, a[k].y - interp(P, a[k].z)[2], 0))
            seg_box(a[k], b[k], 0.016, 0.014, m, up=outw, ext=0.01, r=0.003)


def crest(R, th0, th1, m="steel", step=12):
    ths = list(range(th0, th1, step)) + [th1]
    pts = [HC + V((0, -R[1] * math.cos(math.radians(t)), R[2] * math.sin(math.radians(t)))) for t in ths]
    pts = [p + (p - HC).normalized() * 0.008 for p in pts]
    for a, b in zip(pts, pts[1:]): seg_box(a, b, 0.024, 0.024, m, up=((a + b) / 2 - HC), ext=0.012, r=0.004)


def pauldron(J, P, s, m="mli"):
    j = J[s]; sh, el = j["SH"], j["EL"]; d = (el - sh).normalized(); pe = sh.lerp(el, exit_t(P, sh, el))
    o = lateral(sh, el, s); q = Matrix((d, o.cross(d), o)).transposed().to_quaternion()
    shell(pe + d * 0.03 - o * 0.015, (0.17, 0.145, 0.135), (0, 360), (6, 88), 0.016, m, "dark2", q)
    shell(pe + d * 0.03 - o * 0.015, (0.178, 0.153, 0.14), (0, 360), (6, 14), 0.01, "steel", "dark2", q)


def limb_plate(a, b, t0, t1, R, w, m="mli", side=0):
    a, b = V(a), V(b); d = (b - a).normalized()
    f = V((side, -1.0 if not side else -0.25, 0)); f = (f - d * f.dot(d)).normalized()
    c = a.lerp(b, (t0 + t1) / 2) + f * (R * KF + 0.004); L = (b - a).length * (t1 - t0)
    q = Matrix((f.cross(d), f, d)).transposed().to_quaternion()
    rbox(c, (w + 0.02, 0.012, L + 0.02), "steel", q, r=0.006)
    rbox(c + f * 0.009, (w, 0.012, L), m, q, r=0.005)


def angular_body(J, m="mli", pack="fins"):
    P = torso_shell(1.16, 0.05, m); collar_seal()
    octo_band(P, 1.05, m); octo_band(P, 1.425, m); corner_strips(P)
    p, n = surf(P, 1.265, -math.pi / 2); q = n.to_track_quat('-Y', 'Z')
    rbox(p + n * 0.01, (0.29, 0.02, 0.22), "steel", q, r=0.02)                                 # chest armour plane
    for k in (-1, 1): seg_box(p + n * 0.02 + V((k * 0.15, 0, 0.12)), p + n * 0.02 + V((k * 0.05, 0, 0.15)), 0.02, 0.02, "steel", up=n)
    chest_panel([(z, rx + 0.022, ry + 0.022, cy) for z, rx, ry, cy in P], 1.265)
    for s in (1, -1):
        limbs(J, P, s, m, ring="metal", boot=("dark", "orange", 1.18, "steel"))
        pauldron(J, P, s, m)
        j = J[s]
        limb_plate(j["HIP"], j["KN"], 0.22, 0.82, 0.13, 0.15, m)
        limb_plate(j["KN"], j["AN"], 0.12, 0.66, 0.113, 0.13, m)
        limb_plate(j["EL"], j["WR"], 0.18, 0.78, 0.085, 0.09, m, side=s)
        kd = (j["AN"] - j["HIP"]).normalized()
        rbox(j["KN"] + V((0, -0.115, 0.0)), (0.12, 0.03, 0.12), "steel", Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0))).transposed().to_quaternion() @ Quaternion((1, 0, 0), 0.0), r=0.02)
    frame_ext(J, P)
    rx, ry, cy = interp(P, 1.22); yb = cy + ry * KF + 0.035
    if pack == "wings":
        pc = V((0, yb + 0.11, 1.22)); pack_box(pc, (0.42, 0.2, 0.56), 5, 0.06, m)
        for s in (1, -1):
            qw = Quaternion((0, 0, 1), math.radians(-s * 30)); wc = pc + V((s * 0.32, 0.08, 0.0))
            rbox(wc, (0.26, 0.022, 0.5), "rad", qw, r=0.006)
            for kk in range(6): rbox(wc + qw @ V((-0.1 + 0.04 * kk, 0.018, 0)), (0.006, 0.02, 0.46), "steel", qw, r=0.002, seg=1)
            tube_path([pc + V((s * 0.2, 0.05, 0.2)), wc + qw @ V((-s * 0.1, 0, 0.2))], 0.012); tube_path([pc + V((s * 0.2, 0.05, -0.2)), wc + qw @ V((-s * 0.1, 0, -0.2))], 0.012)
    else:
        pack_box((0, yb + 0.125, 1.22), (0.48, 0.23, 0.58), 9, 0.09, "mli_big", side_fins=(pack == "sidefins"))
    return P


def ear_hubs():
    for s in (1, -1):
        cyl(HC + V((s * 0.165, 0, 0)), HC + V((s * 0.205, 0, 0)), 0.042, m="steel"); cyl(HC + V((s * 0.205, 0, 0)), HC + V((s * 0.212, 0, 0)), 0.024, m="orange")


RA = (0.172, 0.2, 0.21)
# Helmet (rev. 4, the engineered opening): the hood never moves - it is part of the rigid collar structure. In the face
# opening sit two layers: (b) the inner pressure visor - curved thick glass in a metal frame, on pivots at the temples;
# (a) the outer shield (radiation / heat armour) running in two curved guide rails. Opening: equalise the pressure with
# the side valve -> the shield slides up the rails into the crown pocket -> the visor flips up on its pivots behind it.
# Both move concentrically with the hood (rotation about the temple axis = sliding along the curved rails), so nothing
# heavy swings outside the silhouette.
FO = ((-140, -40), (-30, 40))                     # the face opening (azimuth, elevation)


def ring_strips(r_out, t, az, el, w, m):
    (a0, a1), (e0, e1) = az, el
    shell(HC, r_out, (a0 - w, a1 + w), (e0 - w, e0 + w * 0.6), t, m)
    shell(HC, r_out, (a0 - w, a1 + w), (e1 - w * 0.6, e1 + w), t, m)
    shell(HC, r_out, (a0 - w, a0 + w * 0.8), (e0, e1), t, m)
    shell(HC, r_out, (a1 - w * 0.8, a1 + w), (e0, e1), t, m)


def helmet2(key, hood_m="mli", shield="slit", visor_m="glass_gold", shield_m="mli"):
    R = RA; Rp = lambda d: tuple(x + d for x in R)
    (a0, a1), (e0, e1) = FO
    # fixed hood with the face opening
    shell(HC, R, (-30, 210), (-38, 89.5), 0.016, hood_m)
    shell(HC, R, (-150, -30), (e1, 89.5), 0.016, hood_m); shell(HC, R, (-150, -30), (-38, e0), 0.016, hood_m)
    shell(HC, R, (-150, a0), (e0, e1), 0.016, hood_m); shell(HC, R, (a1, -30), (e0, e1), 0.016, hood_m)
    ring_strips(Rp(0.006), 0.012, FO[0], FO[1], 2.5, "orange")                                 # face-opening seal
    # crown pocket housing (raised armoured hump from the brow over the crown) with closed side walls, crest on top
    shell(HC, Rp(0.078), (-150, -30), (e1, 112), 0.012, hood_m)
    for a in ((-152, -149), (-31, -28)): shell(HC, Rp(0.078), a, (e1, 112), 0.078, "steel")
    shell(HC, Rp(0.08), (-151, -29), (e1, e1 + 3), 0.016, "steel")                              # the pocket lip
    crest(Rp(0.078), e1 + 4, 112); crest(R, 112, 166)
    for a in ((-148, -144), (-36, -32)): shell(HC, Rp(0.066), a, (e0 - 2, e1), 0.036, "steel")   # guide rails
    ear_hubs()                                                                                     # visor pivots
    vp = hpt(R, -22, -20); vn = (vp - HC).normalized()                                             # equalising valve
    cyl(vp, vp + vn * 0.035, 0.02, m="steel", seg=20); cyl(vp + vn * 0.035, vp + vn * 0.042, 0.026, m="dark", seg=20)
    rbox(vp + vn * 0.046 + V((0, -0.02, 0)), (0.012, 0.05, 0.012), "orange", r=0.003, seg=1)
    for s in (1, -1): seg_box(HC + V((s * 0.1, -0.205, -0.13)), HC + V((s * 0.125, -0.125, -0.15)), 0.03, 0.05, "steel", up=(s, -0.4, 0))
    # (a) outer shield - stage 1
    helm_begin()
    sh_az = (-146, -34); sh_el = (e0 - 2, e1 + 2); ro = Rp(0.06); t = 0.026
    if shield == "louvre":
        bands = ((sh_el[0], -10), (-7, 15), (18, sh_el[1]))
        for b in bands: shell(HC, ro, sh_az, b, t, shield_m)
        for b in ((-10, -7), (15, 18)): shell(HC, Rp(0.044), sh_az, b, 0.01, "dark")
        for b in bands: shell(HC, Rp(0.062), sh_az, (b[1] - 1.5, b[1]), 0.008, "steel")
    else:
        shell(HC, ro, sh_az, (sh_el[0], 4), t, shield_m); shell(HC, ro, sh_az, (12, sh_el[1]), t, shield_m)
        shell(HC, Rp(0.05), (-120, -60), (3, 13), 0.012, "visor", "visor")                       # the slit
        for a in ((-146, -120), (-60, -34)): shell(HC, ro, a, (4, 12), t, "steel")
        shell(HC, Rp(0.062), (-123, -57), (2, 4), 0.006, "steel"); shell(HC, Rp(0.062), (-123, -57), (12, 14), 0.006, "steel")
    shell(HC, Rp(0.062), sh_az, (sh_el[0], sh_el[0] + 3), 0.008, "orange")
    for a in ((-146, -143), (-37, -34)): shell(HC, Rp(0.062), a, sh_el, 0.008, "steel")
    helm_end(key, HC, (1, 0, 0), -66)
    # (b) inner pressure visor on the temple pivots - stage 2
    helm_begin()
    shell(HC, Rp(0.02), (a0 + 2, a1 - 2), (e0 + 2, e1 - 2), 0.01, visor_m, visor_m)
    ring_strips(Rp(0.026), 0.016, (a0 + 1, a1 - 1), (e0 + 1, e1 - 1), 3, "steel")
    shell(HC, Rp(0.026), (-178, a0 - 1), (-4, 6), 0.012, "steel"); shell(HC, Rp(0.026), (a1 + 1, -2), (-4, 6), 0.012, "steel")
    helm_end(key, HC, (1, 0, 0), -66)


def v1b(J):  # v1 made angular: quilted shield with a narrow gold slit, faceted hood, crown pocket as a swept brow hump
    angular_body(J, "mli"); helmet2("1b", "mli", "slit", "glass_gold", "mli")


def v3b(J):  # v3 made angular: steel louvred shield over the wide gold-coated pressure visor
    angular_body(J, "mli_rect", pack="wings"); helmet2("3b", "mli_rect", "louvre", "visor", "steel")


def v13(J):  # hybrid: slit shield (v1) over the wide visor (v3) in light gold-tinted glass - the face shows at half state
    angular_body(J, "mli", pack="sidefins"); helmet2("13", "mli", "slit", "glass_gold", "steel")


# ================================================================ rev. 5: «C» - massive, broad, calm masses (C1, C2)
# Feedback on 1b/3b/13: too many hoops/arcs/tubes ("snowman"). Target: broad and heavy, not round, not tall. A wide low
# helmet dome sits in a raised collar that runs into the back pack; broad quilted shoulder yoke; one bronze bib plate on
# the chest (it carries the 4 g/cm2 + Ta shield); field coils inside (one subtle seam band); thick soft quilted limbs with
# flat seal rings only at wrists and ankles; dark heavy gloves; flat heavy boots. Same two-layer helmet opening:
# the shield (visor block + slatted jaw guard) slides up into the crown pocket between the inner hood and the outer dome,
# then the inner pressure visor flips up on its temple pivots; an equalising valve on the side.
R1C = (0.205, 0.2, 0.18)                          # inner hood (pressure side)
R2C = (0.31, 0.31, 0.265)                        # outer dome; the 0.09 gap is the crown pocket
REC = ((-130, -50), (-42, 22))                     # the recess (dark face frame) in the outer dome


def slathe(prof, m="mli_band", n=2.6, seg=64, sharp=3.1):
    """lathe with a superellipse section |x/rx|^n + |y/ry|^n = 1 (squarer than an ellipse): broad, not round"""
    vs = []; fs = []; rows = []
    def se(a):
        c, s = math.cos(a), math.sin(a)
        return math.copysign(abs(c) ** (2 / n), c), math.copysign(abs(s) ** (2 / n), s)
    for z, rx, ry, cy in prof:
        if rx <= 1e-6: vs.append((0, cy, z)); rows.append([len(vs) - 1] * seg); continue
        base = len(vs)
        for j in range(seg):
            c, s = se(2 * math.pi * j / seg); vs.append((rx * c, cy + ry * s, z))
        rows.append(list(range(base, base + seg)))
    for A, B in zip(rows, rows[1:]):
        for j in range(seg):
            j1 = (j + 1) % seg; f = []
            for x in (A[j], A[j1], B[j1], B[j]):
                if x not in f: f.append(x)
            if len(f) >= 3: fs.append(tuple(f))
    if prof[0][1] > 1e-6: fs.append(tuple(reversed(rows[0])))
    if prof[-1][1] > 1e-6: fs.append(tuple(rows[-1]))
    return mk(vs, fs, m, sharp=sharp)


TORSO_C = [(0.80, 0.215, 0.165, -0.03), (0.83, 0.245, 0.185, -0.03), (0.92, 0.262, 0.198, -0.03), (1.05, 0.272, 0.205, -0.03),
           (1.18, 0.29, 0.215, -0.03), (1.28, 0.32, 0.225, -0.03), (1.36, 0.38, 0.24, -0.03), (1.42, 0.415, 0.24, -0.02),
           (1.47, 0.415, 0.215, 0.015), (1.51, 0.39, 0.19, 0.04), (1.54, 0.32, 0.17, 0.05), (1.55, 0.0, 0.0, 0.05)]
YOKE_C = [(1.33, 0.30, 0.215, -0.03), (1.37, 0.40, 0.25, -0.025), (1.43, 0.435, 0.265, -0.02), (1.50, 0.43, 0.265, -0.015),
          (1.535, 0.40, 0.255, -0.01), (1.55, 0.34, 0.235, -0.01), (1.555, 0.0, 0.0, -0.01)]


def body_C(J, servo="hidden"):
    slathe(TORSO_C, "mli", n=3.2)
    sph((0, -0.02, 0.75), (0.12, 0.15, 0.13), "mli_band")                                              # crotch gusset
    slathe([(0.795, 0.218, 0.168, -0.03), (0.82, 0.25, 0.19, -0.03)], "dark2", n=3.2)                # hem
    slathe([(1.02, 0.273, 0.207, -0.03), (1.034, 0.274, 0.208, -0.03)], "dark2", n=3.2)                # the one seam band (coils inside)
    # bronze bib plate: the shield mass, with a grille, two indicator lights and a lamp
    yb = -0.03 - 0.215 - 0.02
    q = Quaternion((1, 0, 0), math.radians(-6))
    rbox((0, yb, 1.21), (0.40, 0.04, 0.36), "bronze", q, r=0.03)
    for i in range(4): rbox((0, yb - 0.025 - i * 0.0018, 1.085 + i * 0.03), (0.26, 0.012, 0.012), "dark", q, r=0.004)
    for k, c in ((-0.11, "lgreen"), (-0.075, "lamber")): sph((k, yb - 0.026, 1.33), 0.009, c, seg=16, rings=8)
    cyl((0.1, yb - 0.018, 1.32), (0.1, yb - 0.04, 1.32), 0.03, m="dark", seg=32); cyl((0.1, yb - 0.039, 1.32), (0.1, yb - 0.044, 1.32), 0.022, m="lamp", seg=32)
    for s in (1, -1):
        j = J[s]; sh, el, wr = j["SH"], j["EL"], j["WR"]; d = (el - sh).normalized(); d2 = (wr - el).normalized()
        cyl(sh + d * 0.05, el, 0.118, 0.108, "mli_band"); sph(el, 0.108, "mli_band")
        cyl(el, wr - d2 * 0.02, 0.104, 0.09, "mli_band")
        cyl(wr - d2 * 0.035, wr + d2 * 0.0, 0.098, 0.098, "steel", seg=48)                            # wrist seal ring
        gauntlet(j, s, "dark", "dark2", 1.35)
        h_, k_, a_ = j["HIP"], j["KN"], j["AN"]; off = V((s * 0.006, 0, 0))
        dl = (k_ - h_).normalized(); top = h_.lerp(k_, (h_.z - 0.86) / (h_.z - k_.z)) + off
        cyl(top, k_ + off, 0.13, 0.12, "mli_band"); sph(k_ + off, 0.12, "mli_band")
        cyl(k_ + off, a_ + V((0, 0, 0.1)), 0.122, 0.112, "mli_band")
        cyl(a_ + V((0, 0, 0.13)), a_ + V((0, 0, 0.155)), 0.12, m="steel", seg=48)                      # ankle seal ring
        an, toe = j["AN"], j["TOE"]; cx = (an.x + toe.x) / 2 + s * 0.005; cyb = (an.y + toe.y) / 2 - 0.01
        rbox((cx, cyb, 0.075), (0.2, 0.36, 0.13), "dark", r=0.035)
        rbox((cx, cyb, 0.012), (0.21, 0.37, 0.024), "dark2", r=0.008)
        cyl((an.x, an.y + 0.005, 0.1), (an.x, an.y + 0.005, 0.15), 0.115, m="dark", seg=48)
        o = lateral(h_, k_, s); ok = lateral(k_, a_, s)
        if servo == "hidden":                                                                          # drives under the fabric
            sph(h_ + o * 0.12 + V((0, 0, -0.06)), (0.05, 0.1, 0.1), "mli_band", qz(o))
            sph(k_ + off + o * 0.1, (0.04, 0.08, 0.08), "mli_band", qz(o))
        else:                                                                                          # slim integrated struts
            ph = h_ + o * 0.155 + V((0, 0, -0.05)); pk = k_ + off + ok * 0.135; pa = V((cx + s * 0.11, an.y, 0.1))
            rbox(ph, (0.06, 0.13, 0.13), "bronze", r=0.02); rbox(pk, (0.05, 0.1, 0.1), "bronze", r=0.018)
            seg_box(ph, pk, 0.025, 0.04, "dark2", up=(s, 0, 0), r=0.006); seg_box(pk, pa, 0.022, 0.035, "dark2", up=(s, 0, 0), r=0.006)
            rbox(pa, (0.03, 0.08, 0.06), "dark2", r=0.01)
            oa = lateral(sh, el, s); pe = el + oa * 0.12
            pu = sh.lerp(el, 0.45) + oa * 0.128
            rbox(pe, (0.05, 0.09, 0.09), "bronze", Matrix((d.cross(oa), d, oa)).transposed().to_quaternion(), r=0.016)
            seg_box(pu, pe, 0.022, 0.032, "dark2", up=oa, r=0.006)
    # back: hump from the helmet into a flat wide pack with flush fins
    sph((0, 0.17, 1.56), (0.31, 0.17, 0.13), "mli")
    pc = V((0, 0.30, 1.2)); rbox(pc, (0.58, 0.2, 0.64), "mli_band", r=0.05)
    rbox(pc + V((0, 0.1, 0.0)), (0.48, 0.012, 0.52), "dark", r=0.01)
    for i in range(11): rbox(pc + V((-0.21 + 0.042 * i, 0.112, 0.0)), (0.012, 0.014, 0.48), "rad", r=0.003)
    vb = pc + V((0.2, 0.04, 0.32)); cyl(vb, vb + V((0, 0.02, 0.04)), 0.022, m="steel", seg=24); cyl(vb + V((0, 0.02, 0.04)), vb + V((0, 0.021, 0.045)), 0.016, m="lens", seg=24)
    rbox(pc + V((0, 0.0, -0.36)), (0.5, 0.17, 0.08), "dark", r=0.02)


def hpt2(r, az, el):
    ce, se_ = _se(math.radians(el), SE_N[0]); ca, sa = _se(math.radians(az), SE_N[0])
    return HC + V((r[0] * ce * ca, r[1] * ce * sa, r[2] * se_))


def helmet_C(key, dome_m="mli"):
    SE_N[0] = 2.5
    try: _helmet_C(key, dome_m)
    finally: SE_N[0] = 2.0


def _helmet_C(key, dome_m):
    Ro = lambda dd: tuple(x + dd for x in R1C)
    (ra0, ra1), (re0, re1) = REC
    # outer dome with the rectangular recess (dark face frame); the recess walls; inner hood with the face opening
    shell(HC, R2C, (-50, 230), (-50, 89.5), 0.016, dome_m)
    shell(HC, R2C, (ra0, ra1), (re1, 89.5), 0.016, dome_m)
    shell(HC, R2C, (ra0, ra1), (-50, re0), 0.016, dome_m)
    for a in ((ra0 - 3, ra0), (ra1, ra1 + 3)): shell(HC, R2C, a, (re0, re1), 0.09, "dark", "dark")         # side walls
    shell(HC, R2C, (ra0, ra1), (re0 - 3, re0), 0.09, "dark", "dark")                                        # bottom wall
    shell(HC, tuple(x + 0.004 for x in R2C), (ra0 - 3, ra1 + 3), (re1, re1 + 4), 0.02, "bronze")            # brow lip of the pocket
    for az_, el_ in (((ra0, ra1), (re1 - 3, re1)), ((ra0, ra1), (re0, re0 + 4)), ((ra0, -124), (re0 + 4, re1 - 3)), ((-56, ra1), (re0 + 4, re1 - 3))):
        shell(HC, R1C, az_, el_, 0.014, "dark2")                                                               # recess floor around the face opening
    ring_strips(Ro(0.008), 0.01, (-124, -56), (re0 + 4, re1 - 3), 2, "orange")                               # face seal
    for a in ((ra0 + 0.5, ra0 + 3), (ra1 - 3, ra1 - 0.5)): shell(HC, Ro(0.09), a, (re0, re1), 0.03, "steel")  # guide rails
    for s in (1, -1):                                                                                          # temple pivot bosses
        cyl(HC + V((s * 0.285, 0, 0)), HC + V((s * 0.315, 0, 0)), 0.05, m="dark2", seg=40)
        cyl(HC + V((s * 0.315, 0, 0)), HC + V((s * 0.322, 0, 0)), 0.03, m="bronze", seg=40)
    vp = hpt2(R2C, -24, -8); vn = (vp - HC).normalized()                                                     # equalising valve
    cyl(vp - vn * 0.01, vp + vn * 0.03, 0.022, m="steel", seg=24); cyl(vp + vn * 0.03, vp + vn * 0.038, 0.03, m="dark", seg=24)
    rbox(vp + vn * 0.042 + V((0, -0.022, 0)), (0.012, 0.055, 0.012), "orange", r=0.003)
    # stage 1: the shield = protruding visor block with a slit + slatted jaw guard on a common plate
    helm_begin()
    shell(HC, Ro(0.045), (ra0 + 3, ra1 - 3), (re0 + 1, re1 - 1), 0.02, "bronze")
    c = hpt2(Ro(0.045), -90, 8); qb = Quaternion((1, 0, 0), math.radians(8)); fb = qb @ V((0, -1, 0))
    rbox(c + fb * 0.014, (0.26, 0.032, 0.075), "bronze", qb, r=0.012)
    rbox(c + fb * 0.031, (0.22, 0.006, 0.022), "dark", qb, r=0.003); rbox(c + fb * 0.033, (0.19, 0.006, 0.012), "visor", qb, r=0.002)
    for i in range(4):
        z = -0.07 - i * 0.026; p = HC + V((0, -R1C[1] * math.cos(math.asin(max(-0.95, z / R1C[2]))) - 0.055, z))
        rbox(p + V((0, 0.012, 0)), (0.2 - 0.02 * i, 0.016, 0.012), "dark", r=0.004)
    helm_end(key, HC, (1, 0, 0), -47)
    # stage 2: the inner pressure visor (curved thick glass in a frame) on the temple pivots
    helm_begin()
    shell(HC, Ro(0.026), (-124, -56), (re0 + 4, re1 - 3), 0.012, "glass_gold", "glass_gold")
    shell(HC, Ro(0.03), (-126, -54), (re1 - 4, re1 - 1), 0.01, "steel"); shell(HC, Ro(0.03), (-126, -54), (re0 + 2, re0 + 5), 0.01, "steel")
    for a in ((-126, -122), (-58, -54)): shell(HC, Ro(0.03), a, (re0 + 2, re1 - 1), 0.01, "steel")
    helm_end(key, HC, (1, 0, 0), -62)


def vC1(J):  # drives fully hidden under the quilt (slight bulges at hips and knees)
    body_C(J, "hidden"); helmet_C("C1")


def vC2(J):  # minimal integrated drives: one slim strut per leg with boxy hip/knee housings, short arm strut
    body_C(J, "struts"); helmet_C("C2")


# ================================================================ rev. 6: «D» - after the user's reference (logic, not a copy)
# Massive forward-leaning figure; a huge low helmet leaning forward like a beetle's head, almost as wide as the shoulders,
# ribbed rounded-wedge top, a big protruding visor housing with a narrow slit, a jaw plate with a small grille; a dark
# gunmetal chest yoke with warm backlit grilles and one blue-white lamp, the belly in stacked woven bands; very thick
# shapeless limbs of crumpled gold foil (the suit figure mesh posed, inflated, relaxed and crumpled), metal wrist cuffs,
# big dark gloves, heavy boots; the drives are inside. Hard parts are built in the rest frame and carried by the bones.
DEFORM = [None]
HC0 = V((0, -0.045, 1.635))
RO = (0.335, 0.32, 0.34)                          # outer helmet (before the wedge deform)
FOD = ((-126, -54), (-32, 26))                     # the face opening


def mat_foil(name, base, rough=0.2, scale=38.0, bump=0.55):
    m = bpy.data.materials.new("HS_" + name); m.use_nodes = True; nt = m.node_tree
    p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*base, 1); p.inputs["Metallic"].default_value = 1.0 if sum(base) > 0.5 else 0.2
    p.inputs["Roughness"].default_value = rough
    try: p.inputs["Anisotropic"].default_value = 0.5
    except Exception: pass
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    vo = nt.nodes.new("ShaderNodeTexVoronoi"); vo.feature = 'DISTANCE_TO_EDGE'; vo.inputs["Scale"].default_value = scale
    nz = nt.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = scale * 3; nz.inputs["Detail"].default_value = 8
    nt.links.new(geo.outputs["Position"], vo.inputs["Vector"]); nt.links.new(geo.outputs["Position"], nz.inputs["Vector"])
    b1 = nt.nodes.new("ShaderNodeBump"); b1.inputs["Strength"].default_value = bump; b1.inputs["Distance"].default_value = 0.002
    b2 = nt.nodes.new("ShaderNodeBump"); b2.inputs["Strength"].default_value = bump * 0.5; b2.inputs["Distance"].default_value = 0.001
    nt.links.new(vo.outputs["Distance"], b1.inputs["Height"]); nt.links.new(nz.outputs["Fac"], b2.inputs["Height"])
    nt.links.new(b1.outputs["Normal"], b2.inputs["Normal"]); nt.links.new(b2.outputs["Normal"], p.inputs["Normal"])
    m.diffuse_color = (*base, 1); M[name] = m; return m


def make_materials_D():
    mat_foil("foil", (1.0, 0.72, 0.34))
    mat_foil("glove", (0.05, 0.05, 0.055), rough=0.55, scale=30, bump=0.4)
    mat_mli("woven", (0.86, 0.62, 0.27), 260.0, "rect", 0.35, 0.5)
    mat_mli("ribbed", (0.92, 0.68, 0.3), 105.0, "bands", 0.9, 0.45)
    mat("gunmetal", (0.17, 0.15, 0.12), metal=1.0, rough=0.33)
    mat("warmglow", (1.0, 0.6, 0.25), emit=((1.0, 0.58, 0.22), 5.0))
    mat("bluelamp", (0.8, 0.9, 1.0), emit=((0.75, 0.88, 1.0), 30.0))


def pose_figure():
    arm = ARM
    if arm.animation_data and arm.animation_data.action:                                              # the walk action would override
        log.append("action %s detached (in memory only)" % arm.animation_data.action.name); arm.animation_data.action = None
    arm.data.pose_position = 'POSE'                                                                  # the .blend shows REST (in memory only)
    bpy.context.view_layer.update()
    old = {pb.name: pb.matrix.copy() for pb in arm.pose.bones}
    def rot(bname, axis, deg):
        pb = arm.pose.bones[bname]; h = pb.head.copy()
        pb.matrix = Matrix.Translation(h) @ Matrix.Rotation(math.radians(deg), 4, axis) @ Matrix.Translation(-h) @ pb.matrix
        bpy.context.view_layer.update()
    for b, d in (("LowerBack", 4), ("Spine", 3), ("Spine1", 3), ("Neck", 3)): rot(b, 'X', d)
    rot("LeftArm", 'Y', 7); rot("RightArm", 'Y', -7); rot("LeftForeArm", 'X', 22); rot("RightForeArm", 'X', 22)
    for S in ("Left", "Right"):
        rot(S + "UpLeg", 'X', -6); rot(S + "Leg", 'X', 12); rot(S + "Foot", 'X', -6)
    return old


def delta(bname, old):
    pb = ARM.pose.bones[bname]; return ARM.matrix_world @ pb.matrix @ old[bname].inverted()


def soft_body(J):
    suit = bpy.data.objects["SuitBody"]; dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(suit.evaluated_get(dg)); me.transform(suit.matrix_world)
    o = bpy.data.objects.new("SoftBody", me); COLL[0].objects.link(o)
    me.materials.clear(); me.materials.append(M["foil"]); me.materials.append(M["glove"])
    for p in me.polygons:
        c = p.center; idx = 0
        for s in (1, -1):
            wr, el = J[s]["WR"], J[s]["EL"]; d = (wr - el).normalized()
            if (c - wr).dot(d) > -0.015 and (c - wr).length < 0.3: idx = 1
        if c.z < 0.14: idx = 1
        p.material_index = idx; p.use_smooth = True
    vg = o.vertex_groups.new(name="puff")                                                              # limbs puff more than the torso
    for v in me.vertices:
        c = v.co; w = 0.6
        if c.z < 0.86: w = 0.95
        if c.z < 0.16: w = 0.9
        for s_ in (1, -1):
            sh, el = J[s_]["SH"], J[s_]["EL"]
            if (c - sh).dot((el - sh).normalized()) > 0.06 and c.x * s_ > 0.17 and c.z > 0.9: w = 1.6
            wr = J[s_]["WR"]
            if (c - wr).dot((wr - el).normalized()) > 0.0 and (c - wr).length < 0.3: w = 0.55
        vg.add([v.index], w, 'REPLACE')
    m1 = o.modifiers.new("inflate", "DISPLACE"); m1.strength = 0.105; m1.mid_level = 0.0; m1.direction = 'NORMAL'; m1.vertex_group = "puff"
    m2 = o.modifiers.new("relax", "SMOOTH"); m2.factor = 0.9; m2.iterations = 14
    m3 = o.modifiers.new("sub", "SUBSURF"); m3.levels = 2; m3.render_levels = 2
    t2 = bpy.data.textures.new("folds", "CLOUDS"); t2.noise_scale = 0.07; t2.noise_depth = 2
    m5 = o.modifiers.new("folds", "DISPLACE"); m5.texture = t2; m5.texture_coords = 'GLOBAL'; m5.strength = 0.035; m5.mid_level = 0.5
    t1 = bpy.data.textures.new("crumple", "VORONOI"); t1.noise_scale = 0.03; t1.weight_1 = -1.0; t1.weight_2 = 1.0; t1.noise_intensity = 1.5
    m4 = o.modifiers.new("crumple", "DISPLACE"); m4.texture = t1; m4.texture_coords = 'GLOBAL'; m4.strength = 0.016; m4.mid_level = 0.3
    return o


TORSO_D = [(z, (rx + 0.055) * 1.1 + 0.02, (ry + 0.055) * 1.13 + 0.02, cy) for z, rx, ry, cy in TORSO]


def torso_D():
    P = TORSO_D
    lathe([(0.84, interp(P, 0.86)[0] * 0.95, interp(P, 0.86)[1] * 0.95, interp(P, 0.86)[2])] +
          [(z, rx - 0.012, ry - 0.012, cy) for z, rx, ry, cy in P if 0.86 <= z <= 1.2] + [(1.21, interp(P, 1.21)[0] - 0.012, interp(P, 1.21)[1] - 0.012, interp(P, 1.21)[2])], "gunmetal")
    for z0, z1 in ((0.85, 0.955), (0.965, 1.07), (1.08, 1.19)):                                    # stacked woven belly bands
        prof = []
        for t, dd in ((0.0, -0.006), (0.15, 0.006), (0.5, 0.014), (0.85, 0.006), (1.0, -0.006)):
            z = z0 + (z1 - z0) * t; rx, ry, cy = interp(P, z); prof.append((z, rx + dd, ry + dd, cy))
        lathe(prof, "woven")
    yk = []
    for z, k in ((1.17, 0.0), (1.22, 0.012), (1.32, 0.018), (1.42, 0.012), (1.47, -0.04), (1.50, -0.11), (1.515, -0.2)):
        rx, ry, cy = interp(P, min(z, 1.46)); yk.append((z, max(rx + k, 0.12), max(ry + k, 0.1), cy))
    for az in ((-158, -22), (32, 148)):                                                                # chest and back yoke plates
        a0_, a1_ = map(math.radians, az)
        def fo(u, v, off=0.0):
            z = 1.17 + (1.46 - 1.17) * v; rx, ry, cy = interp(yk, z); a = a0_ + (a1_ - a0_) * u
            return ((rx + off) * math.cos(a), cy + (ry + off) * math.sin(a), z)
        vs, fs, fm = grid_solid(fo, lambda u, v: fo(u, v, -0.03), 40, 10); mk2(vs, fs, ["gunmetal", "gunmetal"], fm, sharp=1.0)
    lathe([(z, rx * 0.86, ry * 0.86, cy) for z, rx, ry, cy in yk], "foil")                              # soft core between the plates
    tor((0, interp(P, 0.845)[2], 0.845), (0, 0, 1), interp(P, 0.845)[0], 0.022, "gunmetal", sy=interp(P, 0.845)[1] / interp(P, 0.845)[0])
    # front of the yoke: upper backlit grille, lower panel with grille and the blue-white lamp, two small warm lights
    def front(z): rx, ry, cy = interp(P, min(z, 1.46)); return V((0, cy - ry - 0.012, z))
    def grille(c, w, h, n, vertical=True):
        rbox(c + V((0, -0.006, 0)), (w + 0.05, 0.03, h + 0.045), "gunmetal", r=0.018)
        rbox(c + V((0, -0.016, 0)), (w, 0.02, h), "warmglow", r=0.006)
        for i in range(n):
            if vertical: rbox(c + V((-w / 2 + w * (i + 0.5) / n, -0.03, 0)), (0.007, 0.012, h), "gunmetal", r=0.002)
            else: rbox(c + V((0, -0.03, -h / 2 + h * (i + 0.5) / n)), (w, 0.012, 0.006), "gunmetal", r=0.002)
    grille(front(1.375) + V((0, -0.014, 0)), 0.40, 0.11, 18)
    grille(front(1.25) + V((0.03, 0, 0)), 0.22, 0.07, 4, vertical=False)
    lp = front(1.25) + V((-0.15, -0.02, 0)); cyl(lp, lp + V((0, -0.025, 0)), 0.028, m="gunmetal", seg=32)
    cyl(lp + V((0, -0.024, 0)), lp + V((0, -0.03, 0)), 0.02, m="bluelamp", seg=32)
    for k in (-0.035, 0.035): rbox(front(1.455) + V((k, -0.03, 0)), (0.045, 0.012, 0.02), "warmglow", r=0.003)
    # flat wide back pack with flush fins and a small vent
    rx, ry, cy = interp(P, 1.2); pc = V((0, cy + ry + 0.13, 1.2))
    rbox(pc, (0.56, 0.24, 0.62), "gunmetal", r=0.05)
    rbox(pc + V((0, 0.121, 0.0)), (0.46, 0.01, 0.5), "woven", r=0.01)
    for i in range(10): rbox(pc + V((-0.2 + 0.0445 * i, 0.13, 0.0)), (0.012, 0.014, 0.46), "rad", r=0.003)
    vb = pc + V((0.19, 0.06, 0.31)); cyl(vb, vb + V((0, 0.02, 0.04)), 0.02, m="steel", seg=24)


def helmet_D():
    (a0, a1), (e0, e1) = FOD
    def wedge(x, y, z):
        if z > 0:
            z = z + 0.05 * max(-0.6, min(1.0, y / 0.3)) * (z / 0.34)                                      # crown rises to the back
            y = y - 0.06 * (z / 0.34) ** 2                                                                 # the top leans forward (wedge)
        else:
            f = 1.0 + 0.45 * (-z / 0.34); x *= f; y *= f                                                   # the lower edge flares onto the shoulders
        return (x, y, z)
    SE_N[0] = 2.3; DEFORM[0] = wedge
    shell(HC0, RO, (a1, 360 + a0), (-66, 89.5), 0.018, "ribbed", "dark2")
    shell(HC0, RO, (a0, a1), (e1, 89.5), 0.018, "ribbed", "dark2")
    shell(HC0, RO, (a0, a1), (-66, e0), 0.018, "woven", "dark2")
    for a in ((a0 - 3, a0), (a1, a1 + 3)): shell(HC0, RO, a, (e0, e1), 0.1, "gunmetal", "gunmetal")      # face-frame walls (rails)
    shell(HC0, RO, (a0, a1), (e0 - 3, e0), 0.1, "gunmetal", "gunmetal"); shell(HC0, RO, (a0, a1), (e1, e1 + 3), 0.1, "gunmetal", "gunmetal")
    shell(HC0, tuple(r + 0.006 for r in RO), (a0 - 4, a1 + 4), (e0 - 4, e0 - 1), 0.012, "bronze")
    DEFORM[0] = None
    for s in (1, -1):                                                                                   # temple pivot bosses
        cyl(HC0 + V((s * 0.33, 0, 0)), HC0 + V((s * 0.375, 0, 0)), 0.06, m="gunmetal", seg=40)
        cyl(HC0 + V((s * 0.375, 0, 0)), HC0 + V((s * 0.382, 0, 0)), 0.035, m="bronze", seg=40)
    vp = HC0 + V((0.3, -0.13, -0.09)); vn = V((1, -0.4, 0)).normalized()                             # equalising valve
    cyl(vp, vp + vn * 0.04, 0.022, m="steel", seg=24); cyl(vp + vn * 0.04, vp + vn * 0.048, 0.03, m="gunmetal", seg=24)
    rbox(vp + vn * 0.052 + V((0, -0.022, 0)), (0.012, 0.055, 0.012), "orange", r=0.003)
    SE_N[0] = 2.0
    # stage 1: the shield = visor housing (protruding, slit) + mouth plate with a small grille; slides up the rails
    helm_begin()
    shell(HC0, (0.27, 0.268, 0.25), (a0 + 1, a1 - 1), (e0 + 1, e1 - 1), 0.02, "gunmetal")
    vh = HC0 + V((0, -0.335, 0.03))
    rbox(vh + V((0, 0.015, 0)), (0.6, 0.1, 0.17), "woven", Quaternion((1, 0, 0), math.radians(6)), r=0.055, seg=4)
    rbox(vh + V((0, -0.062, 0.0)), (0.38, 0.02, 0.095), "gunmetal", Quaternion((1, 0, 0), math.radians(6)), r=0.03, seg=3)
    rbox(vh + V((0, -0.071, 0.0)), (0.3, 0.012, 0.026), "dark", Quaternion((1, 0, 0), math.radians(6)), r=0.008)
    rbox(vh + V((0, -0.075, 0.0)), (0.27, 0.008, 0.012), "visor", Quaternion((1, 0, 0), math.radians(6)), r=0.004)
    mp = HC0 + V((0, -0.295, -0.095))
    rbox(mp, (0.3, 0.06, 0.1), "gunmetal", Quaternion((1, 0, 0), math.radians(-12)), r=0.03)
    rbox(mp + V((0, -0.031, -0.005)), (0.15, 0.01, 0.035), "warmglow", Quaternion((1, 0, 0), math.radians(-12)), r=0.004)
    for i in range(6): rbox(mp + V((-0.0625 + 0.025 * i, -0.037, -0.005)), (0.006, 0.008, 0.035), "gunmetal", Quaternion((1, 0, 0), math.radians(-12)), r=0.001)
    helm_end("D", HC0, (1, 0, 0), -50)
    # stage 2: the inner pressure visor on the temple pivots
    helm_begin()
    shell(HC0, (0.24, 0.24, 0.225), (a0 + 3, a1 - 3), (e0 + 3, e1 - 3), 0.012, "glass_gold", "glass_gold")
    for az_, el_ in (((a0 + 2, a1 - 2), (e1 - 5, e1 - 2)), ((a0 + 2, a1 - 2), (e0 + 2, e0 + 5)), ((a0 + 2, a0 + 6), (e0 + 2, e1 - 2)), ((a1 - 6, a1 - 2), (e0 + 2, e1 - 2))):
        shell(HC0, (0.248, 0.248, 0.232), az_, el_, 0.018, "gunmetal")
    for s in (1, -1): shell(HC0, (0.248, 0.248, 0.232), ((-178, a0 + 2) if s < 0 else (a1 - 2, -2)), (-5, 6), 0.012, "gunmetal")
    helm_end("D", HC0, (1, 0, 0), -72)


def carry(build, D):
    g = []; GROUP.append(g)
    try: build()
    finally: GROUP.remove(g)
    bpy.context.view_layer.update()
    for o in g: o.matrix_world = D @ o.matrix_world
    return g


HC_W = [None]


def vD(J):
    old = pose_figure()
    suit = bpy.data.objects["SuitBody"]; dg = bpy.context.evaluated_depsgraph_get()
    zmin = min((suit.matrix_world @ v.co).z for v in suit.evaluated_get(dg).to_mesh().vertices)
    ARM.location.z += (-0.02 - zmin) + 0.06; bpy.context.view_layer.update()
    J = joints()
    sb = soft_body(J); bpy.context.view_layer.update(); dg = bpy.context.evaluated_depsgraph_get()
    ev = sb.evaluated_get(dg); mz = min((sb.matrix_world @ v.co).z for v in ev.to_mesh().vertices); ev.to_mesh_clear()
    sb.location.z -= mz + 0.01; ARM.location.z -= mz + 0.01; bpy.context.view_layer.update()            # stand on the floor
    J = joints()
    for s in (1, -1):                                                                                  # metal wrist cuffs, soles
        wr, el = J[s]["WR"], J[s]["EL"]; d = (wr - el).normalized()
        tor(wr - d * 0.01, d, 0.105, 0.02, "metal"); tor(wr - d * 0.045, d, 0.11, 0.014, "metal")
        an, toe = J[s]["AN"], J[s]["TOE"]
    Dsp = delta("Spine", old); Dhd = delta("Head", old)
    carry(torso_D, Dsp)
    n0 = len(HINGE.get("D", []))
    carry(helmet_D, Dhd)
    st = HINGE["D"]
    for k in range(n0, len(st)):
        objs, pv, ax, ang, mv = st[k]; st[k] = (objs, Dhd @ pv, (Dhd.to_3x3() @ ax).normalized(), ang, mv)
    HC_W[0] = Dhd @ HC0


# ================================================================ rev. 7: «E» - five engineering schemes, fitted to the body
# HEAVY_SUIT.md «Облик: пять инженерных схем»: Мантия, Чешуя, Крыло, Испарина, Контур. No bubbles, no power armour:
# a slim heavy human silhouette, compact helmet (<= 1.6 head widths) on a collar, drives inside. Everything soft or hard
# is derived from the suit figure mesh itself: the under-suit is the body offset 1.2 cm; plates are body regions chosen by
# bone weights, cut by clean planes, smoothed, offset 2-6 cm, solidified and bevelled; scales are tiles instanced on the
# offset surface along the limb axes; fins follow the plate surface found by ray casts from the limb axis.
import bmesh, random
from mathutils.bvhtree import BVHTree
BASE = {}
SPINE_B = {"Hips", "LowerBack", "Spine", "Spine1", "Neck", "Neck1", "LHipJoint", "RHipJoint", "Head"}
HAND_B = {"LeftHand", "LThumb", "LeftFingerBase", "LeftHandFinger1", "RightHand", "RThumb", "RightFingerBase", "RightHandFinger1"}
HCE = V((0, -0.045, 1.63)); R1E = (0.125, 0.155, 0.165); R2E = (0.165, 0.195, 0.205)
FOE = ((-124, -56), (-30, 22))
WINGS = {}


def make_materials_E():
    m = mat("fabric", (0.10, 0.10, 0.11), rough=0.75, bump=0.15, bscale=400)
    mat("titanium", (0.58, 0.59, 0.61), metal=1.0, rough=0.28)
    try: next(n for n in M["titanium"].node_tree.nodes if n.type == "BSDF_PRINCIPLED").inputs["Anisotropic"].default_value = 0.8
    except Exception: pass
    mat("goldfilm", (1.0, 0.76, 0.38), metal=1.0, rough=0.1)
    mat("satin", (0.87, 0.86, 0.83), rough=0.32, coat=0.35)
    mat("satin2", (0.74, 0.74, 0.73), rough=0.38, coat=0.2)
    mat("porous", (0.80, 0.78, 0.74), rough=0.92, bump=0.25, bscale=900)
    mat("hotglow", (1.0, 0.4, 0.1), emit=((1.0, 0.35, 0.08), 3.0))
    mat("accent", (0.3, 0.9, 1.0), emit=((0.3, 0.85, 1.0), 10.0))
    mat("glove_dark", (0.05, 0.05, 0.055), rough=0.6)


def base_mesh():
    if BASE: return BASE
    suit = bpy.data.objects["SuitBody"]; dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(suit.evaluated_get(dg)); me.transform(suit.matrix_world)
    names = {g.index: g.name for g in suit.vertex_groups}; bones = {b.name for b in ARM.data.bones}
    W = [{names[g.group]: g.weight for g in v.groups if names[g.group] in bones and g.weight > 0.01} for v in suit.data.vertices]
    hand = {"LeftHand", "LThumb", "LeftFingerBase", "LeftHandFinger1", "RightHand", "RThumb", "RightFingerBase", "RightHandFinger1"}
    bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()                    # a suit has no anatomy: smooth bust and hips away
    reg = [v for v in bm.verts if 0.66 < v.co.z < 1.47 and abs(v.co.x) < 0.27 and sum(W[v.index].get(n, 0) for n in hand) < 0.2
           and sum(W[v.index].get(n, 0) for n in ("LeftArm", "RightArm", "LeftForeArm", "RightForeArm")) < 0.5]
    for _ in range(150): bmesh.ops.smooth_vert(bm, verts=reg, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    bm.to_mesh(me); bm.free()
    BASE.update(me=me, W=W, bvh=BVHTree.FromPolygons([v.co.copy() for v in me.vertices], [tuple(p.vertices) for p in me.polygons]),
                bdir={b.name: (b.tail_local - b.head_local).normalized() for b in ARM.data.bones})
    return BASE


def ws(i, names): return sum(BASE["W"][i].get(n, 0.0) for n in names)


def bake(o, sharp=0.7):
    bpy.context.view_layer.update(); dg = bpy.context.evaluated_depsgraph_get()
    m2 = bpy.data.meshes.new_from_object(o.evaluated_get(dg)); o.modifiers.clear(); o.data = m2
    try: m2.shade_smooth(); m2.set_sharp_from_angle(angle=sharp)
    except Exception: pass
    for g in GROUP: g.append(o)
    return o


def under_suit(m="fabric", off=0.012):
    B = base_mesh(); me = B["me"].copy(); bm = bmesh.new(); bm.from_mesh(me); bm.normal_update()
    for v in bm.verts: v.co += v.normal * off
    bm.to_mesh(me); bm.free()
    me.materials.clear(); me.materials.append(M[m]); me.materials.append(M["glove_dark"])
    for p in me.polygons:
        p.material_index = 1 if sum(ws(i, HAND_B) for i in p.vertices) / len(p.vertices) > 0.5 else 0; p.use_smooth = True
    o = bpy.data.objects.new("UnderSuit", me); COLL[0].objects.link(o); return o


def plate(sel, planes=(), off=0.03, thick=0.012, m="titanium", m_in="gunmetal", smooth=4, bevel=0.004, keep_biggest=False):
    B = base_mesh(); bm = bmesh.new(); bm.from_mesh(B["me"]); bm.verts.ensure_lookup_table()
    keep = [sel(i, bm.verts[i].co) for i in range(len(bm.verts))]
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if sum(keep[v.index] for v in f.verts) * 2 < len(f.verts)], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    for co, no in planes:
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=V(co), plane_no=V(no).normalized(), clear_outer=True)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    if keep_biggest and bm.faces:
        bm.faces.ensure_lookup_table(); seen = set(); best = []
        for f in bm.faces:
            if f in seen: continue
            comp = []; stack = [f]; seen.add(f)
            while stack:
                g = stack.pop(); comp.append(g)
                for e in g.edges:
                    for h in e.link_faces:
                        if h not in seen: seen.add(h); stack.append(h)
            if len(comp) > len(best): best = comp
        bs = set(best); bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in bs], context='FACES')
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    for _ in range(smooth):
        bmesh.ops.smooth_vert(bm, verts=[v for v in bm.verts if not v.is_boundary], factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    if not bm.faces: bm.free(); return None
    bm.normal_update()
    for v in bm.verts: v.co += v.normal * off
    me = bpy.data.meshes.new("plate"); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new("plate", me); COLL[0].objects.link(o); me.materials.append(M[m]); me.materials.append(M[m_in])
    so = o.modifiers.new("s", "SOLIDIFY"); so.thickness = thick; so.offset = 1.0; so.use_even_offset = True; so.material_offset = 1
    if bevel:
        bv = o.modifiers.new("b", "BEVEL"); bv.width = bevel; bv.segments = 2; bv.limit_method = 'ANGLE'; bv.angle_limit = math.radians(40)
    return bake(o)


def SB(names, thr=0.35): return lambda i, co: ws(i, names) > thr


def limb_planes(a, b, t0, t1, half=None):
    d = (b - a).normalized(); L = (b - a).length; pl = [(a + d * L * t0, -d), (a + d * L * t1, d)]
    if half:
        f = V(half); f = (f - d * f.dot(d)).normalized(); pl.append((a, f))         # removes the half facing f
    return pl


def SIDE(s): return "Left" if s > 0 else "Right"


def boots_E(m="titanium", sole="gunmetal"):
    for s in (1, -1):
        c = V((s * 0.1765, -0.088, 0.064))
        rbox(c, (0.15, 0.29, 0.13), m, r=0.018, seg=2)
        rbox(c + V((0, 0, -0.054)), (0.158, 0.298, 0.026), sole, r=0.006, seg=1)
        rbox(c + V((0, -0.075, 0.045)), (0.13, 0.1, 0.04), sole, Quaternion((1, 0, 0), math.radians(-18)), r=0.01, seg=2)


def collar_E(m="titanium"):
    tor((0, -0.04, 1.47), (0, 0, 1), 0.128, 0.018, m, sy=1.12)
    tor((0, -0.04, 1.49), (0, 0, 1), 0.122, 0.008, "gunmetal", sy=1.12)


def helmet_E(key, outer="titanium", shield_m="titanium", window="slit", bands=False, ribs=False, hood=None):
    global HC
    HC = HCE; (a0, a1), (e0, e1) = FOE; Rp = lambda R, d: tuple(x + d for x in R)
    collar_E("titanium" if outer != "goldfilm" else "titanium")
    if bands:                                                                         # overlapping shingle bands
        for k, (b0, b1) in enumerate(((-40, -14), (-17, 8), (5, 30), (27, 55), (52, 89.5))):
            R = Rp(R2E, 0.003 * k)
            shell(HC, R, (a1, 360 + a0), (b0, b1), 0.01, outer)
            if b1 > e1: shell(HC, R, (a0, a1), (max(b0, e1), b1), 0.01, outer)
            if b0 < e0: shell(HC, R, (a0, a1), (b0, min(b1, e0)), 0.01, outer)
    else:
        shell(HC, R2E, (a1, 360 + a0), (-40, 89.5), 0.012, outer)
        shell(HC, R2E, (a0, a1), (e1, 89.5), 0.012, outer); shell(HC, R2E, (a0, a1), (-40, e0), 0.012, outer)
    for a in ((a0 - 2, a0), (a1, a1 + 2)): shell(HC, R2E, a, (e0, e1), 0.04, "gunmetal", "gunmetal")
    shell(HC, R2E, (a0, a1), (e0 - 2, e0), 0.04, "gunmetal", "gunmetal"); shell(HC, R2E, (a0, a1), (e1, e1 + 2), 0.04, "gunmetal", "gunmetal")
    ring_strips(Rp(R1E, 0.003), 0.006, (a0 + 1, a1 - 1), (e0 + 1, e1 - 1), 1.5, "orange")                     # face seal
    if ribs:
        for a in (40, 62, 90, 118, 140):
            shell(HC, Rp(R2E, 0.016), (a, a + 1.6), (-25, 89.5), 0.016, "porous", "porous")
        for a in (-118, -90, -62): shell(HC, Rp(R2E, 0.016), (a, a + 1.6), (e1 + 3, 89.5), 0.016, "porous", "porous")
    if hood:
        shell(HC, Rp(R2E, 0.032), (-62, 242), (-38, 82), 0.005, hood, "satin")
        shell(HC, Rp(R2E, 0.032), (-118, -62), (48, 82), 0.005, hood, "satin")
        shell(HC, Rp(R2E, 0.034), (-118, -62), (46, 50), 0.008, "titanium")
    for s in (1, -1):                                                                  # temple pivots
        cyl(HC + V((s * 0.158, 0, 0.0)), HC + V((s * 0.176, 0, 0.0)), 0.026, m="gunmetal", seg=32)
        cyl(HC + V((s * 0.176, 0, 0.0)), HC + V((s * 0.18, 0, 0.0)), 0.015, m="accent" if key != "E4" else "hotglow", seg=24)
    vp = HC + V((0.135, -0.1, -0.1)); vn = V((1, -0.5, -0.2)).normalized()            # equalising valve
    cyl(vp, vp + vn * 0.03, 0.012, m="titanium", seg=20); rbox(vp + vn * 0.034 + V((0, -0.012, 0)), (0.008, 0.03, 0.008), "orange", r=0.002)
    # stage 1: the shield slides up the rails into the crown pocket
    helm_begin()
    if window == "bands":
        shell(HC, Rp(R1E, 0.026), (a0 + 1, a1 - 1), (e0 + 1, 2), 0.01, shield_m); shell(HC, Rp(R1E, 0.028), (a0 + 1, a1 - 1), (6, e1 - 1), 0.01, shield_m)
        shell(HC, Rp(R1E, 0.02), (-110, -70), (1, 7), 0.004, "visor", "visor")
    else:
        shell(HC, Rp(R1E, 0.026), (a0 + 1, a1 - 1), (e0 + 1, e1 - 1), 0.01, shield_m)
        if window == "slit":
            shell(HC, Rp(R1E, 0.0275), (-114, -66), (1, 7), 0.004, "gunmetal"); shell(HC, Rp(R1E, 0.029), (-110, -70), (2.5, 5.5), 0.003, "visor", "visor")
        else:                                                                                   # camera band
            shell(HC, Rp(R1E, 0.0285), (-118, -62), (-1, 9), 0.004, "lens", "lens")
            for az in (-104, -90, -76):
                p = hpt2(Rp(R1E, 0.029), az, 4) if False else HC + V((Rp(R1E, 0.029)[0] * math.cos(math.radians(az)), Rp(R1E, 0.029)[1] * math.sin(math.radians(az)), 0.012))
                n = (p - HC); n.z = 0; n.normalize(); cyl(p - n * 0.002, p + n * 0.008, 0.009, m="titanium", seg=20); cyl(p + n * 0.007, p + n * 0.009, 0.006, m="accent", seg=20)
    helm_end(key, HC, (1, 0, 0), -62)
    # stage 2: the inner pressure visor on the temple pivots
    helm_begin()
    shell(HC, Rp(R1E, 0.01), (a0 + 2, a1 - 2), (e0 + 2, e1 - 2), 0.004, "glass", "glass")
    ring_strips(Rp(R1E, 0.013), 0.007, (a0 + 2, a1 - 2), (e0 + 2, e1 - 2), 2, "titanium")
    for s in (1, -1): shell(HC, Rp(R1E, 0.013), ((-178, a0) if s < 0 else (a1, -2)), (-3, 3), 0.006, "titanium")
    helm_end(key, HC, (1, 0, 0), -66)


# ---------------------------------------------------------------- the five schemes
def body_plates(J, m="titanium", m_in="gunmetal", off=0.028, thick=0.012, full_limbs=False, torso=True, smooth=4):
    out = []
    if torso:
        out.append(plate(SB({"Spine", "Spine1"}), [((0, 0, 1.07), (0, 0, -1)), ((0, 0, 1.44), (0, 0, 1)), ((0, -0.035, 0), (0, 1, 0)),
                                                   ((0.2, 0, 0), (1, 0, 0)), ((-0.2, 0, 0), (-1, 0, 0))], off, thick, m, m_in, smooth))
        out.append(plate(SB({"Spine", "Spine1"}), [((0, 0, 1.04), (0, 0, -1)), ((0, 0, 1.45), (0, 0, 1)), ((0, -0.025, 0), (0, -1, 0)),
                                                   ((0.19, 0, 0), (1, 0, 0)), ((-0.19, 0, 0), (-1, 0, 0))], off, thick, m, m_in, smooth))
        out.append(plate(SB({"LowerBack", "Spine", "Hips"}), [((0, 0, 0.94), (0, 0, -1)), ((0, 0, 1.05), (0, 0, 1))], off * 0.8, thick, m, m_in, smooth))
        out.append(plate(SB({"Hips", "LHipJoint", "RHipJoint", "LeftUpLeg", "RightUpLeg"}, 0.3), [((0, 0, 0.8), (0, 0, -1)), ((0, 0, 0.92), (0, 0, 1))], off * 0.8, thick, m, m_in, smooth))
    for s in (1, -1):
        S = SIDE(s); j = J[s]
        front = None if full_limbs else (0, 1, 0)
        out.append(plate(SB({S + "Shoulder", S + "Arm"}, 0.4), [(j["SH"] + (j["EL"] - j["SH"]) * 0.32, (j["EL"] - j["SH"])), ((0, 0, 1.3), (0, 0, -1))], off * 1.1, thick, m, m_in, smooth))
        out.append(plate(SB({S + "Arm"}), limb_planes(j["SH"], j["EL"], 0.38, 0.86, None if full_limbs else (-s, 0, 0)), off * 0.8, thick, m, m_in, smooth))
        out.append(plate(SB({S + "ForeArm"}), limb_planes(j["EL"], j["WR"], 0.1, 0.86), off * 0.8, thick, m, m_in, smooth))
        out.append(plate(SB({S + "UpLeg"}), limb_planes(j["HIP"], j["KN"], 0.18, 0.84, front), off, thick, m, m_in, smooth))
        out.append(plate(SB({S + "Leg"}), limb_planes(j["KN"], j["AN"], 0.1, 0.8, front), off, thick, m, m_in, smooth))
        out.append(plate(SB({S + "UpLeg", S + "Leg"}, 0.3), [(j["KN"] + V((0, 0, -0.05)), (0, 0, -1)), (j["KN"] + V((0, 0, 0.06)), (0, 0, 1)), (j["KN"] + V((0, -0.02, 0)), (0, 1, 0))], off * 1.3, thick, m, m_in, smooth))
    return [o for o in out if o]


def vE1(J):  # «Мантия»: thin pressure suit + reflective mantle with a hood on standoffs, sky-facing radiator on its back
    under_suit("fabric")
    for s in (1, -1):
        S = SIDE(s); j = J[s]
        plate(SB({S + "ForeArm"}), limb_planes(j["EL"], j["WR"], 0.12, 0.86), 0.02, 0.01, "titanium", "gunmetal")
        plate(SB({S + "Leg"}), limb_planes(j["KN"], j["AN"], 0.08, 0.82), 0.022, 0.01, "titanium", "gunmetal")
        plate(SB({S + "UpLeg"}), limb_planes(j["HIP"], j["KN"], 0.5, 0.86, (0, 1, 0)), 0.02, 0.01, "titanium", "gunmetal")
    hem = ((0, 0, 0.93), (0, -0.55, -0.835))
    planes = [hem, ((0, 0, 1.475), (0, 0, 1))]
    for s in (1, -1):
        j = J[s]; d = (j["EL"] - j["SH"]); planes.append((j["SH"] + d * 0.62, d))
    plate(SB({"Spine", "Spine1", "LowerBack", "Hips", "LeftShoulder", "RightShoulder", "LeftArm", "RightArm", "Neck"}, 0.3), planes,
          0.05, 0.006, "goldfilm", "satin", smooth=14, bevel=0.002, keep_biggest=True)
    q = Quaternion((1, 0, 0), math.radians(-38)); pc = V((0, 0.21, 1.27))                      # sky-facing radiator
    rbox(pc, (0.30, 0.026, 0.26), "titanium", q, r=0.006, seg=2)
    for i in range(9): rbox(pc + q @ V((-0.12 + 0.03 * i, 0.016, 0)), (0.006, 0.014, 0.23), "satin2", q, r=0.002, seg=1)
    boots_E("titanium"); helmet_E("E1", "titanium", "titanium", "slit", hood="goldfilm")


def scales_E(sel, s=0.04, L=0.062, w=0.056, t=0.006, off=0.022, lift=9.0, mats=("satin", "satin2")):
    B = base_mesh(); me = B["me"]; W = B["W"]; rnd = random.Random(7)
    order = sorted(range(len(me.vertices)), key=lambda i: -me.vertices[i].co.z)
    cells = {}; pts = []
    for i in order:
        co = me.vertices[i].co
        if not sel(i, co): continue
        k = (int(co.x // s), int(co.y // s), int(co.z // s)); ok = True
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for q in cells.get((k[0] + dx, k[1] + dy, k[2] + dz), ()):
                        if (q - co).length < s: ok = False; break
                    if not ok: break
                if not ok: break
            if not ok: break
        if not ok: continue
        cells.setdefault(k, []).append(co.copy()); pts.append(i)
    vs = []; fs = []; fm = []; sl = math.sin(math.radians(lift))
    for i in pts:
        v = me.vertices[i]; n = v.normal.copy(); bones = W[i]
        if not bones: continue
        b = max(bones.items(), key=lambda kv: kv[1])[0]
        down = V((0, 0, -1)) if b in SPINE_B else B["bdir"][b].copy()
        down = down - n * down.dot(n)
        if down.length < 1e-3: continue
        down.normalize(); X = down.cross(n).normalized(); p0 = v.co + n * off
        base = len(vs)
        for layer in (0, 1):
            for iv, vv in enumerate((0.0, L / 2, L)):
                for iu, uu in enumerate((-w / 2, 0.0, w / 2)):
                    h = -6.0 * uu * uu + vv * sl + (t if layer else 0.0)
                    vs.append(tuple(p0 + X * uu + down * (vv - 0.012) + n * h))
        def ix(layer, iu, iv): return base + layer * 9 + iv * 3 + iu
        mi = 0 if rnd.random() > 0.18 else 1
        for iv in range(2):
            for iu in range(2):
                fs.append((ix(1, iu, iv), ix(1, iu + 1, iv), ix(1, iu + 1, iv + 1), ix(1, iu, iv + 1))); fm.append(mi)
                fs.append((ix(0, iu, iv + 1), ix(0, iu + 1, iv + 1), ix(0, iu + 1, iv), ix(0, iu, iv))); fm.append(mi)
        ring = [(0, 0), (1, 0), (2, 0), (2, 1), (2, 2), (1, 2), (0, 2), (0, 1)]
        for k in range(8):
            (u0, v0), (u1, v1) = ring[k], ring[(k + 1) % 8]
            fs.append((ix(0, u0, v0), ix(0, u1, v1), ix(1, u1, v1), ix(1, u0, v0))); fm.append(mi)
    return mk2(vs, fs, list(mats), fm, sharp=0.6)


def vE2(J):  # «Чешуя»: overlapping gradient-ceramic scales over the whole body, helmet of the same bands, slit at a joint
    under_suit("fabric")
    skip = HAND_B | {"Neck1", "Head", "LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase"}
    scales_E(lambda i, co: co.z < 1.46 and co.z > 0.13 and ws(i, skip) < 0.3)
    collar_E(); boots_E("satin", "gunmetal")
    helmet_E("E2", "satin", "satin", "bands", bands=True)


def wing_E(s):
    hinge = V((s * 0.1, 0.2, 1.43))
    def F(off):
        def f(u, v):
            width = 0.33 * (1 - 0.4 * v ** 1.5); xa = 0.025 + width * u
            y = 0.205 + 0.03 * v - 0.22 * xa * xa + off
            z = 1.43 - 0.86 * v - 0.05 * u * v
            return (s * xa, y, z)
        return f
    g = []; GROUP.append(g)
    vs, fs, fm = grid_solid(F(0.0), F(-0.016), 16, 24); mk2(vs, fs, ["satin", "goldfilm"], fm, sharp=1.0)
    vs, fs, fm = grid_solid(lambda u, v: F(0.002)(u, 0.97 + 0.03 * v), lambda u, v: F(-0.018)(u, 0.97 + 0.03 * v), 16, 2); mk2(vs, fs, ["titanium", "titanium"], fm, sharp=1.0)
    cyl(hinge + V((-s * 0.03, 0, 0)), hinge + V((s * 0.05, 0, 0)), 0.022, m="gunmetal", seg=24)
    GROUP.remove(g)
    return g, hinge


def vE3(J):  # «Крыло»: fitted titanium plates + two hinged shoulder-blade shields (deployed in front views, folded in back)
    under_suit("fabric"); body_plates(J, "titanium", "gunmetal")
    for k in (-0.05, 0.05): cyl((k, -0.215, 1.33), (k, -0.225, 1.33), 0.008, m="accent", seg=16)
    WINGS["E3"] = []
    for s in (1, -1):
        g, h = wing_E(s); WINGS["E3"].append((g, h, V((0, 1, 0)), math.radians(-128 * s)))
    boots_E("titanium"); helmet_E("E3", "gunmetal", "gunmetal", "camera")


def fins_E(segs, n=6, h=0.022, off=0.04, glow=0.004):
    B = base_mesh(); bvh = B["bvh"]; vs = []; fs = []; fm = []
    for a, b, t0, t1, angs in segs:
        d = (b - a).normalized(); e1 = d.orthogonal().normalized(); e2 = d.cross(e1)
        for ang in angs:
            dr = (e1 * math.cos(ang) + e2 * math.sin(ang)).normalized(); row = []
            for k in range(14):
                c = a.lerp(b, t0 + (t1 - t0) * k / 13); hit = bvh.ray_cast(c, dr, 0.4)
                if hit[0] is None: continue
                row.append(hit[0] + dr * off)
            if len(row) < 3: continue
            base = len(vs)
            for p in row: vs += [tuple(p - dr * 0.004), tuple(p + dr * (h - glow)), tuple(p + dr * h)]
            for k in range(len(row) - 1):
                i0, i1 = base + 3 * k, base + 3 * (k + 1)
                fs.append((i0, i1, i1 + 1, i0 + 1)); fm.append(0); fs.append((i0 + 1, i1 + 1, i1 + 2, i0 + 2)); fm.append(1)
    o = mk2(vs, fs, ["porous", "hotglow"], fm, sharp=0.9)
    so = o.modifiers.new("s", "SOLIDIFY"); so.thickness = 0.004; so.offset = 0.0
    return bake(o)


def vE4(J):  # «Испарина»: rigid porous-ceramic shell (transpiration cooling) with longitudinal radiator fins along the limbs
    under_suit("fabric")
    body_plates(J, "porous", "satin2", off=0.03, thick=0.014, full_limbs=True, smooth=6)
    segs = []
    for s in (1, -1):
        j = J[s]; A = [math.radians(a) for a in range(0, 360, 60)]
        segs += [(j["SH"], j["EL"], 0.42, 0.84, A), (j["EL"], j["WR"], 0.14, 0.82, A), (j["HIP"], j["KN"], 0.22, 0.8, A), (j["KN"], j["AN"], 0.14, 0.78, A)]
    for z0 in (0.0,):
        segs.append((V((0, -0.03, 1.08)), V((0, -0.03, 1.42)), 0.0, 1.0, [math.radians(a) for a in (40, 65, 90, 115, 140)]))
    fins_E(segs, off=0.044)
    boots_E("porous", "gunmetal"); helmet_E("E4", "porous", "porous", "slit", ribs=True)


def vE5(J):  # «Контур»: the field does the work - light gold suit, rigid collar, spine bar and belt carry the coils
    under_suit("goldfilm", 0.012)
    plate(lambda i, co: abs(co.x) < 0.045 and co.y > -0.02 and ws(i, {"Spine", "Spine1", "LowerBack", "Neck"}) > 0.3,
          [((0, 0, 0.95), (0, 0, -1)), ((0, 0, 1.47), (0, 0, 1))], 0.026, 0.02, "titanium", "gunmetal", smooth=6)      # spine bar
    plate(SB({"LowerBack", "Hips", "Spine"}, 0.3), [((0, 0, 0.93), (0, 0, -1)), ((0, 0, 1.0), (0, 0, 1))], 0.026, 0.018, "titanium", "gunmetal")  # belt
    for s in (1, -1):
        S = SIDE(s); j = J[s]; d = j["EL"] - j["SH"]
        plate(SB({S + "Shoulder", S + "Arm"}, 0.4), [(j["SH"] + d * 0.22, d), ((0, 0, 1.33), (0, 0, -1)), ((0, -0.06, 0), (0, -1, 0)), ((0, 0.07, 0), (0, 1, 0))], 0.03, 0.014, "titanium", "gunmetal")
        plate(SB({"Hips", S + "UpLeg"}, 0.3), [((0, 0, 0.8), (0, 0, -1)), ((0, 0, 0.9), (0, 0, 1)), ((s * 0.17, 0, 0), (-s, 0, 0)), ((0, -0.07, 0), (0, -1, 0)), ((0, 0.05, 0), (0, 1, 0))], 0.03, 0.014, "titanium", "gunmetal")
        rbox(V((s * 0.21, -0.1, 0.965)), (0.02, 0.012, 0.012), "accent", r=0.003)
    rbox((0, 0.165, 1.2), (0.22, 0.06, 0.26), "titanium", r=0.01, seg=2)                         # battery cassette
    rbox((0, 0.196, 1.2), (0.18, 0.004, 0.2), "gunmetal", r=0.002, seg=1)
    for k in (-0.03, 0.0, 0.03): rbox((k, 0.2, 1.31), (0.012, 0.006, 0.006), "accent", r=0.002)
    boots_E("titanium"); helmet_E("E5", "goldfilm", "titanium", "slit")


# ---------------------------------------------------------------- rev. 7b: technical detailing (user's three rules)
# Anthropomorphic (true proportions, the anatomy smoothed out of the under-suit), technological (panel lines, bolts,
# standoffs, latches, markings, real fin geometry, hinge hardware), no fantasy (no cloak, no dragon scales, no wings).
BOLTS = []


def make_materials_E2():
    for name, base, k, sharp_, metal in (("fabric_seam", (0.13, 0.13, 0.14), 34.0, 0.06, 0.0), ("gold_seam", (1.0, 0.76, 0.38), 30.0, 0.06, 1.0),
                                          ("light_seam", (0.62, 0.63, 0.64), 34.0, 0.06, 0.0)):
        m = mat_mli(name, base, k, "rect", 0.35, sharp_)
        p = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); p.inputs["Metallic"].default_value = metal
    mat("marking", (0.95, 0.35, 0.05), rough=0.4); mat("marking_dark", (0.03, 0.03, 0.03), rough=0.5)


def under_suit2(m="fabric_seam", off=0.012, smooth=10):
    B = base_mesh(); me = B["me"].copy(); bm = bmesh.new(); bm.from_mesh(me); bm.verts.ensure_lookup_table()
    body = [v for v in bm.verts if ws(v.index, HAND_B) < 0.3 and 0.9 < v.co.z < 1.46 and abs(v.co.x) < 0.2]
    for _ in range(smooth): bmesh.ops.smooth_vert(bm, verts=body, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)  # no anatomy on a suit
    bm.normal_update()
    for v in bm.verts: v.co += v.normal * off
    bm.to_mesh(me); bm.free()
    me.materials.clear(); me.materials.append(M[m]); me.materials.append(M["glove_dark"])
    for p in me.polygons:
        p.material_index = 1 if sum(ws(i, HAND_B) for i in p.vertices) / len(p.vertices) > 0.5 else 0; p.use_smooth = True
    o = bpy.data.objects.new("UnderSuit", me); COLL[0].objects.link(o); return o


def spaced(points, dmin):
    out = []
    for p, n in points:
        if all((p - q).length >= dmin for q, _ in out): out.append((p, n))
    return out


def plate2(sel, planes=(), off=0.03, thick=0.012, m="titanium", m_in="gunmetal", smooth=6, bevel=0.004, keep_biggest=True,
           m_rim=None, bolts=0.075, standoffs=0.0, mark=False, latches=0):
    B = base_mesh(); bm = bmesh.new(); bm.from_mesh(B["me"]); bm.verts.ensure_lookup_table()
    keep = [sel(i, bm.verts[i].co) for i in range(len(bm.verts))]
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if sum(keep[v.index] for v in f.verts) * 2 < len(f.verts)], context='FACES')
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    for co, no in planes:
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=V(co), plane_no=V(no).normalized(), clear_outer=True)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    if keep_biggest and bm.faces:
        seen = set(); best = []
        for f in bm.faces:
            if f in seen: continue
            comp = []; stack = [f]; seen.add(f)
            while stack:
                g = stack.pop(); comp.append(g)
                for e in g.edges:
                    for h in e.link_faces:
                        if h not in seen: seen.add(h); stack.append(h)
            if len(comp) > len(best): best = comp
        bs = set(best); bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in bs], context='FACES')
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    if len(bm.faces) < 4: bm.free(); return None
    for _ in range(smooth):
        bmesh.ops.smooth_vert(bm, verts=[v for v in bm.verts if not v.is_boundary], factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    bm.normal_update()
    body_pts = [(v.co.copy(), v.normal.copy()) for v in bm.verts if not v.is_boundary]
    for v in bm.verts: v.co += v.normal * off
    bm.normal_update()
    C = sum((v.co for v in bm.verts), V()) / len(bm.verts); N = sum((v.normal for v in bm.verts), V()).normalized()
    bnd = [(v.co.copy(), v.normal.copy()) for v in bm.verts if v.is_boundary]
    me = bpy.data.meshes.new("plate"); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new("plate", me); COLL[0].objects.link(o)
    for mm in (m, m_in) + ((m_rim,) if m_rim else ()): me.materials.append(M[mm])
    so = o.modifiers.new("s", "SOLIDIFY"); so.thickness = thick; so.offset = 1.0; so.use_even_offset = False; so.material_offset = 1
    so.use_quality_normals = True
    if m_rim: so.material_offset_rim = 2
    if bevel:
        bv = o.modifiers.new("b", "BEVEL"); bv.width = bevel; bv.segments = 2; bv.limit_method = 'ANGLE'; bv.angle_limit = math.radians(40)
    bake(o)
    if bolts:                                                                           # fasteners along the edge
        for p, n in spaced(bnd, bolts):
            t = (C - p); t = (t - n * t.dot(n));
            if t.length < 1e-4: continue
            BOLTS.append((p + t.normalized() * 0.011 + n * (thick + 0.0008), n))
    if standoffs:                                                                       # visible standoff posts
        for p, n in spaced(body_pts[::3], standoffs):
            cyl(p + n * 0.012, p + n * (off + 0.001), 0.006, m="titanium", seg=12); cyl(p + n * 0.012, p + n * 0.016, 0.012, m="gunmetal", seg=12)
    if mark:                                                                            # small marking decal
        up = V((0, 0, 1)) - N * N.z; up = up.normalized() if up.length > 1e-3 else V((0, 1, 0)); rt = up.cross(N).normalized()
        q = Matrix((rt, N, up)).transposed().to_quaternion() if False else Matrix((rt, up.cross(rt), up)).transposed().to_quaternion()
        pc = C + N * (thick + 0.0015)
        rbox(pc + up * 0.03, (0.07, 0.002, 0.012), "marking", n_quat(N, up), r=0.001, seg=1)
        rbox(pc + up * 0.012, (0.04, 0.002, 0.006), "marking_dark", n_quat(N, up), r=0.001, seg=1)
    for k in range(latches):                                                            # latches on the edge
        if not bnd: break
        p, n = bnd[(k * len(bnd)) // max(latches, 1)]
        rbox(p + n * (thick * 0.5), (0.03, 0.018, 0.016), "gunmetal", n_quat(n, V((0, 0, 1))), r=0.004, seg=2)
    return o


def n_quat(n, up):
    n = V(n).normalized(); up = V(up) - n * n.dot(V(up))
    if up.length < 1e-4: up = n.orthogonal()
    up.normalize(); rt = up.cross(n).normalized()
    return Matrix((rt, n, up)).transposed().to_quaternion()        # local x = rt, local y = outward normal, local z = up


def flush_bolts(m="gunmetal"):
    vs = []; fs = []; fm = []
    for p, n in BOLTS:
        e1 = n.orthogonal().normalized(); e2 = n.cross(e1); base = len(vs); seg = 6
        for k, h in ((0, 0.0), (1, 0.0035)):
            for j in range(seg):
                a = 2 * math.pi * j / seg; vs.append(tuple(p + (e1 * math.cos(a) + e2 * math.sin(a)) * 0.0045 + n * h))
        for j in range(seg):
            j1 = (j + 1) % seg; fs.append((base + j, base + j1, base + seg + j1, base + seg + j)); fm.append(0)
        fs.append(tuple(base + seg + j for j in range(seg))); fm.append(0)
    BOLTS.clear()
    if vs: mk2(vs, fs, [m], fm, sharp=0.5)


def body_plates2(J, m="titanium", m_in="gunmetal", off=0.028, thick=0.012, full_limbs=False, smooth=10, mark=True):
    P = lambda *a, **k: plate2(*a, **k)
    P(SB({"Spine", "Spine1"}), [((0, 0, 1.08), (0, 0, -1)), ((0, 0, 1.43), (0, 0, 1)), ((0, -0.035, 0), (0, 1, 0)), ((0.19, 0, 0), (1, 0, 0)), ((-0.19, 0, 0), (-1, 0, 0))],
      off, thick, m, m_in, smooth + 6, mark=mark, latches=2)
    P(SB({"Spine", "Spine1"}), [((0, 0, 1.05), (0, 0, -1)), ((0, 0, 1.44), (0, 0, 1)), ((0, -0.025, 0), (0, -1, 0)), ((0.18, 0, 0), (1, 0, 0)), ((-0.18, 0, 0), (-1, 0, 0))],
      off, thick, m, m_in, smooth)
    P(SB({"LowerBack", "Spine", "Hips"}), [((0, 0, 0.945), (0, 0, -1)), ((0, 0, 1.045), (0, 0, 1))], off * 0.7, thick, m, m_in, smooth)
    P(SB({"Hips", "LHipJoint", "RHipJoint", "LeftUpLeg", "RightUpLeg"}, 0.3), [((0, 0, 0.8), (0, 0, -1)), ((0, 0, 0.925), (0, 0, 1))], off * 0.7, thick, m, m_in, smooth)
    for s in (1, -1):
        S = SIDE(s); j = J[s]; front = None if full_limbs else (0, 1, 0)
        P(SB({S + "Shoulder", S + "Arm"}, 0.4), [(j["SH"] + (j["EL"] - j["SH"]) * 0.3, (j["EL"] - j["SH"])), ((0, 0, 1.31), (0, 0, -1))], off, thick, m, m_in, smooth)
        P(SB({S + "Arm"}), limb_planes(j["SH"], j["EL"], 0.4, 0.86, None if full_limbs else (-s, 0, 0)), off * 0.7, thick, m, m_in, smooth)
        P(SB({S + "ForeArm"}), limb_planes(j["EL"], j["WR"], 0.12, 0.84), off * 0.7, thick, m, m_in, smooth)
        P(SB({S + "UpLeg"}), limb_planes(j["HIP"], j["KN"], 0.2, 0.82, front), off, thick, m, m_in, smooth, mark=mark and s > 0)
        P(SB({S + "Leg"}), limb_planes(j["KN"], j["AN"], 0.12, 0.8, front), off, thick, m, m_in, smooth)
        P(SB({S + "UpLeg", S + "Leg"}, 0.3), [(j["KN"] + V((0, 0, -0.05)), (0, 0, -1)), (j["KN"] + V((0, 0, 0.06)), (0, 0, 1)), (j["KN"] + V((0, -0.02, 0)), (0, 1, 0))],
          off * 1.2, thick, m, m_in, smooth)


def chest_kit(y=-0.205):
    """umbilical connectors + status lights on the chest (manufactured details)"""
    for k, c in ((-0.07, "titanium"), (0.07, "titanium")):
        cyl((k, y, 1.2), (k, y - 0.022, 1.2), 0.018, m=c, seg=24); cyl((k, y - 0.021, 1.2), (k, y - 0.026, 1.2), 0.011, m="gunmetal", seg=24)
    for k in (-0.02, 0.0, 0.02): rbox((k, y - 0.012, 1.3), (0.012, 0.006, 0.006), "accent", r=0.002, seg=1)


# ---- E1 «Мантия» -> rigid stand-off thermal shroud panels
def vE1(J):
    under_suit2("light_seam")
    G = dict(off=0.055, thick=0.006, m="goldfilm", m_in="satin", m_rim="titanium", smooth=16, bevel=0.002, bolts=0.09, standoffs=0.13, latches=2)
    plate2(SB({"Spine", "Spine1"}), [((0, 0, 1.08), (0, 0, -1)), ((0, 0, 1.43), (0, 0, 1)), ((0, -0.035, 0), (0, 1, 0)), ((0.2, 0, 0), (1, 0, 0)), ((-0.2, 0, 0), (-1, 0, 0))], mark=True, **G)
    plate2(SB({"Spine", "Spine1", "LowerBack"}), [((0, 0, 0.98), (0, 0, -1)), ((0, 0, 1.45), (0, 0, 1)), ((0, -0.025, 0), (0, -1, 0)), ((0.2, 0, 0), (1, 0, 0)), ((-0.2, 0, 0), (-1, 0, 0))], **G)
    plate2(SB({"LowerBack", "Spine", "Hips"}), [((0, 0, 0.93), (0, 0, -1)), ((0, 0, 1.055), (0, 0, 1)), ((0, -0.03, 0), (0, 1, 0))], **G)
    for s in (1, -1):
        S = SIDE(s); j = J[s]
        plate2(SB({S + "Shoulder", S + "Arm"}, 0.4), [(j["SH"] + (j["EL"] - j["SH"]) * 0.45, (j["EL"] - j["SH"])), ((0, 0, 1.29), (0, 0, -1))], **G)
        plate2(SB({S + "UpLeg"}), limb_planes(j["HIP"], j["KN"], 0.15, 0.8, (0, 1, 0)), **dict(G, off=0.045))
        plate2(SB({S + "Leg"}), limb_planes(j["KN"], j["AN"], 0.12, 0.8, (0, 1, 0)), **dict(G, off=0.04))
        plate2(SB({S + "ForeArm"}), limb_planes(j["EL"], j["WR"], 0.12, 0.84, (-s, 0, 0)), **dict(G, off=0.035))
    q = Quaternion((1, 0, 0), math.radians(-38)); pc = V((0, 0.235, 1.29))                    # sky-facing radiator with real fins
    rbox(pc, (0.30, 0.02, 0.26), "titanium", q, r=0.004, seg=2)
    for i in range(13): rbox(pc + q @ V((-0.132 + 0.022 * i, 0.016, 0)), (0.003, 0.014, 0.24), "satin2", q, r=0.001, seg=1)
    for k in (-0.11, 0.11): cyl(pc + q @ V((k, -0.01, -0.11)), V((k, 0.17, 1.17)), 0.007, m="titanium", seg=12)
    chest_kit(-0.27)
    flush_bolts(); boots_E("titanium"); helmet_E("E1", "titanium", "titanium", "slit", hood="goldfilm")


# ---- E2 «Чешуя» -> lamellar rows of rectangular shingle tiles fastened on a backing
def lamellar(segs, off=0.02, t=0.007, gap=0.004, lift=7.0):
    B = base_mesh(); bvh = B["bvh"]; vs = []; fs = []; fm = []
    sl = math.sin(math.radians(lift))
    def tile(p0, X, D, Nn, w, L):
        base = len(vs)
        for layer in (0, 1):
            for iv, vv in enumerate((0.0, L)):
                for iu, uu in enumerate((-w / 2, w / 2)):
                    vs.append(tuple(p0 + X * uu + D * vv + Nn * (vv * sl + (t if layer else 0.0))))
        def ix(l, iu, iv): return base + l * 4 + iv * 2 + iu
        fs.append((ix(1, 0, 0), ix(1, 1, 0), ix(1, 1, 1), ix(1, 0, 1))); fm.append(0)
        fs.append((ix(0, 0, 1), ix(0, 1, 1), ix(0, 1, 0), ix(0, 0, 0))); fm.append(1)
        for (a, b) in (((0, 0), (1, 0)), ((1, 0), (1, 1)), ((1, 1), (0, 1)), ((0, 1), (0, 0))):
            fs.append((ix(0, *a), ix(0, *b), ix(1, *b), ix(1, *a))); fm.append(1)
        for uu in (-w / 2 + 0.008, w / 2 - 0.008):                                       # two rivets on the top edge
            BOLTS.append((p0 + X * uu + D * 0.008 + Nn * (t + 0.0005), Nn))
    for a, b, rows, per, t0, t1, ang0, ang1 in segs:
        d = (b - a).normalized(); e1 = d.orthogonal().normalized(); e2 = d.cross(e1)
        rs = (t1 - t0) * (b - a).length / max(rows - 1, 1)
        for r in range(rows):
            c = a.lerp(b, t0 + (t1 - t0) * r / max(rows - 1, 1))
            for k in range(per):
                ang = ang0 + (ang1 - ang0) * (k + 0.5) / per
                dr = e1 * math.cos(ang) + e2 * math.sin(ang); hit = bvh.ray_cast(c, dr, 0.25)
                if hit[0] is None: continue
                p, n = hit[0], hit[1].normalized()
                if n.dot(dr) < 0: n = -n
                D = d - n * d.dot(n); D.normalize(); X = D.cross(n).normalized()
                w = (hit[3] + off) * (ang1 - ang0) / per - gap
                tile(p + n * off - D * 0.006, X, D, n, w, rs * 1.3)
    o = mk2(vs, fs, ["satin", "gunmetal"], fm, sharp=0.5)
    return o


def vE2(J):
    under_suit2("fabric_seam")
    T = 2 * math.pi; segs = []
    tor_a, tor_b = V((0, -0.035, 1.44)), V((0, -0.035, 0.84))
    segs.append((tor_a, tor_b, 11, 16, 0.0, 1.0, 0.0, T))
    for s in (1, -1):
        j = J[s]
        segs += [(j["SH"], j["EL"], 5, 9, 0.28, 0.88, 0.0, T), (j["EL"], j["WR"], 5, 9, 0.08, 0.84, 0.0, T),
                 (j["HIP"], j["KN"], 7, 11, 0.16, 0.84, 0.0, T), (j["KN"], j["AN"], 6, 10, 0.12, 0.82, 0.0, T)]
    lamellar(segs)
    chest_kit(-0.26); flush_bolts(); collar_E(); boots_E("satin", "gunmetal")
    helmet_E("E2", "satin", "satin", "bands", bands=True)


# ---- E3 «Крыло» -> two rigid shield panels on articulated arms with actuators (deployed in front, folded on the back)
def shield_panel(c, n, up, w=0.34, h=0.56):
    q = n_quat(n, up)
    rbox(c, (w, 0.022, h), "titanium", q, r=0.008, seg=2)
    rbox(c + n * 0.012, (w - 0.03, 0.006, h - 0.03), "satin", q, r=0.004, seg=1)
    rbox(c - n * 0.012, (w - 0.03, 0.004, h - 0.03), "goldfilm", q, r=0.003, seg=1)
    for k in range(1, 4): rbox(c + n * 0.016 + q @ V((0, 0, -h / 2 + h * k / 4)), (w - 0.03, 0.002, 0.003), "gunmetal", q, r=0.0005, seg=1)
    rbox(c + n * 0.017 + q @ V((w * 0.3, 0, h * 0.4)), (0.05, 0.002, 0.01), "marking", q, r=0.001, seg=1)
    for sx in (-1, 1):
        for sz in (-1, 1): BOLTS.append((c + n * 0.0115 + q @ V((sx * (w / 2 - 0.012), 0, sz * (h / 2 - 0.012))), n))


def arm_links(pts, r=0.016):
    for a, b in zip(pts, pts[1:]):
        seg_box(a, b, 0.03, 0.022, "titanium", up=(0, 0, 1), ext=0.02, r=0.004)
        mid = a.lerp(b, 0.5); cyl(a.lerp(b, 0.2) + V((0, 0, 0.022)), a.lerp(b, 0.75) + V((0, 0, 0.022)), 0.009, m="gunmetal", seg=12)   # actuator
    for p in pts: cyl(p + V((0, 0, -0.024)), p + V((0, 0, 0.024)), r, m="gunmetal", seg=16)                                       # hinge pins


def vE3(J):
    under_suit2("fabric_seam"); body_plates2(J, "titanium", "gunmetal")
    chest_kit(-0.24)
    folded, deployed = [], []
    for s in (1, -1):
        mount = V((s * 0.11, 0.17, 1.36))
        g = []; GROUP.append(g)
        rbox(mount, (0.07, 0.05, 0.08), "gunmetal", r=0.008, seg=2)
        arm_links([mount, V((s * 0.13, 0.215, 1.36))])
        shield_panel(V((s * 0.17, 0.25, 1.06)), V((0, 1, 0)), V((0, 0, 1)), 0.3, 0.6)
        GROUP.remove(g); folded += g
        g = []; GROUP.append(g)
        rbox(mount, (0.07, 0.05, 0.08), "gunmetal", r=0.008, seg=2)
        if s > 0:   # left: held out to the side-front against a low source (lava)
            e = V((0.34, 0.22, 1.45)); w_ = V((0.55, 0.02, 1.32))
            arm_links([mount, e, w_]); shield_panel(V((0.66, -0.12, 1.14)), V((0.85, -0.45, -0.25)).normalized(), V((0, 0, 1)), 0.34, 0.6)
        else:       # right: raised over the shoulder against the Sun
            e = V((-0.2, 0.26, 1.62)); w_ = V((-0.26, 0.06, 1.8))
            arm_links([mount, e, w_]); shield_panel(V((-0.3, -0.12, 1.9)), V((-0.35, -0.35, 0.87)).normalized(), V((0, -1, 0.3)), 0.34, 0.56)
        GROUP.remove(g); deployed += g
    WINGS["E3"] = (folded, deployed)
    flush_bolts(); boots_E("titanium"); helmet_E("E3", "gunmetal", "gunmetal", "camera")


# ---- E4 «Испарина»: porous ceramic shell, longitudinal heat-exchanger fins
def fins_E2(segs, h=0.022, off=0.044, maxd=0.16):
    B = base_mesh(); bvh = B["bvh"]; vs = []; fs = []; fm = []
    for a, b, t0, t1, angs in segs:
        d = (b - a).normalized(); e1 = d.orthogonal().normalized(); e2 = d.cross(e1)
        for ang in angs:
            dr = (e1 * math.cos(ang) + e2 * math.sin(ang)).normalized(); row = []
            for k in range(16):
                c = a.lerp(b, t0 + (t1 - t0) * k / 15); hit = bvh.ray_cast(c, dr, maxd)
                if hit[0] is None: row = []; break
                row.append(hit[0] + dr * off)
            if len(row) < 3: continue
            base = len(vs)
            for p in row: vs += [tuple(p - dr * 0.004), tuple(p + dr * (h - 0.004)), tuple(p + dr * h)]
            for k in range(len(row) - 1):
                i0, i1 = base + 3 * k, base + 3 * (k + 1)
                fs.append((i0, i1, i1 + 1, i0 + 1)); fm.append(0); fs.append((i0 + 1, i1 + 1, i1 + 2, i0 + 2)); fm.append(1)
            BOLTS.append((row[0] - dr * 0.002 + d * 0.0, dr)); BOLTS.append((row[-1] - dr * 0.002, dr))
    o = mk2(vs, fs, ["porous", "hotglow"], fm, sharp=0.9)
    so = o.modifiers.new("s", "SOLIDIFY"); so.thickness = 0.003; so.offset = 0.0
    return bake(o)


def vE4(J):
    under_suit2("fabric_seam")
    body_plates2(J, "porous", "satin2", off=0.03, thick=0.014, full_limbs=True, smooth=10)
    segs = []
    for s in (1, -1):
        j = J[s]; A = [math.radians(a) for a in range(15, 360, 45)]
        segs += [(j["SH"], j["EL"], 0.44, 0.84, A), (j["EL"], j["WR"], 0.16, 0.8, A), (j["HIP"], j["KN"], 0.24, 0.78, A), (j["KN"], j["AN"], 0.16, 0.76, A)]
    segs.append((V((0, -0.03, 1.1)), V((0, -0.03, 1.4)), 0.0, 1.0, [math.radians(a) for a in (35, 55, 75, 105, 125, 145)]))
    fins_E2(segs, off=0.045)
    chest_kit(-0.25); flush_bolts(); boots_E("porous", "gunmetal"); helmet_E("E4", "porous", "porous", "slit", ribs=True)


# ---- E5 «Контур»
def vE5(J):
    under_suit2("gold_seam", 0.012)
    plate2(lambda i, co: abs(co.x) < 0.045 and co.y > -0.02 and ws(i, {"Spine", "Spine1", "LowerBack", "Neck"}) > 0.3,
           [((0, 0, 0.95), (0, 0, -1)), ((0, 0, 1.47), (0, 0, 1))], 0.026, 0.02, "titanium", "gunmetal", smooth=6, bolts=0.06)
    plate2(SB({"LowerBack", "Hips", "Spine"}, 0.3), [((0, 0, 0.93), (0, 0, -1)), ((0, 0, 1.0), (0, 0, 1))], 0.026, 0.018, "titanium", "gunmetal", bolts=0.07, latches=2)
    for s in (1, -1):
        S = SIDE(s); j = J[s]; d = j["EL"] - j["SH"]
        plate2(SB({S + "Shoulder", S + "Arm"}, 0.4), [(j["SH"] + d * 0.22, d), ((0, 0, 1.33), (0, 0, -1)), ((0, -0.06, 0), (0, -1, 0)), ((0, 0.07, 0), (0, 1, 0))], 0.03, 0.014, "titanium", "gunmetal", mark=True)
        plate2(SB({"Hips", S + "UpLeg"}, 0.3), [((0, 0, 0.8), (0, 0, -1)), ((0, 0, 0.9), (0, 0, 1)), ((s * 0.17, 0, 0), (-s, 0, 0)), ((0, -0.07, 0), (0, -1, 0)), ((0, 0.05, 0), (0, 1, 0))], 0.03, 0.014, "titanium", "gunmetal")
        rbox(V((s * 0.21, -0.1, 0.965)), (0.02, 0.012, 0.012), "accent", r=0.003)
    rbox((0, 0.165, 1.2), (0.22, 0.06, 0.26), "titanium", r=0.01, seg=2)
    rbox((0, 0.196, 1.2), (0.18, 0.004, 0.2), "gunmetal", r=0.002, seg=1)
    for k in (-0.03, 0.0, 0.03): rbox((k, 0.2, 1.31), (0.012, 0.006, 0.006), "accent", r=0.002)
    chest_kit(-0.22); flush_bolts(); boots_E("titanium"); helmet_E("E5", "goldfilm", "titanium", "slit")


def wing_set(i, on):
    if i in WINGS:
        folded, deployed = WINGS[i]
        for o in folded: o.hide_render = on
        for o in deployed: o.hide_render = not on


# ---------------------------------------------------------------- rev. 7c: common base form after the reference's logic
# The user: the reference's forms are probably optimal - take them as the base, express them technically and in scale.
# Base (all five): a forward-leaning wedge/egg helmet ~0.42 m wide (head shield = torso shield + stand-off layer + room to
# turn the head), a horizontal visor housing at mid-height with a narrow slit, the helmet's lower front running down into a
# chest yoke with a backlit grille and a lamp, the field coil as a raised collar ring, heavy layered shoulders, a banded
# belly. The five variants differ only by the protection scheme applied on this base.
RF_O = (0.21, 0.235, 0.25)                      # outer helmet before the wedge deform
RF_I = (0.15, 0.17, 0.18)                       # inner pressure hood
FOF = ((-124, -56), (-32, 24))


def helmet_F(key, outer="titanium", trim="gunmetal", shield_m="titanium", housing_m="titanium", hood_extra=None, ribs=False, tiles=False):
    global HC
    HC = HCE; (a0, a1), (e0, e1) = FOF; Rp = lambda R, d: tuple(x + d for x in R)
    def wedge(x, y, z):
        if z > 0:
            z = z + 0.05 * max(-0.5, min(1.0, y / 0.24)) * (z / 0.25)                      # taller toward the back
            x = x * (1 - 0.18 * (z / 0.3) ** 2)                                               # narrowing at the crown
            y = y - 0.03 * (z / 0.25) ** 2                                                    # leaning forward
        else:
            f = 1.0 + 0.35 * (-z / 0.25); x *= f; y *= f                                      # lower edge opens onto the yoke
        return (x, y, z)
    SE_N[0] = 2.4; DEFORM[0] = wedge
    if tiles:
        for k, (b0, b1) in enumerate(((-62, -30), (-33, -2), (-5, 24), (21, 50), (47, 89.5))):
            R = Rp(RF_O, 0.004 * k)
            shell(HC, R, (a1, 360 + a0), (b0, b1), 0.012, outer, trim)
            if b1 > e1: shell(HC, R, (a0, a1), (max(b0, e1), b1), 0.012, outer, trim)
            if b0 < e0: shell(HC, R, (a0, a1), (b0, min(b1, e0)), 0.012, outer, trim)
    else:
        shell(HC, RF_O, (a1, 360 + a0), (-62, 89.5), 0.016, outer, trim)
        shell(HC, RF_O, (a0, a1), (e1, 89.5), 0.016, outer, trim); shell(HC, RF_O, (a0, a1), (-62, e0), 0.016, outer, trim)
        for el in (-30, 0, 30, 58): shell(HC, Rp(RF_O, 0.001), (a1 + 4, 356 + a0), (el, el + 1.2), 0.004, trim, trim)   # panel lines
    for a in ((a0 - 3, a0), (a1, a1 + 3)): shell(HC, RF_O, a, (e0, e1), 0.06, trim, trim)                     # face frame / guide rails
    shell(HC, RF_O, (a0, a1), (e0 - 3, e0), 0.06, trim, trim); shell(HC, RF_O, (a0 - 3, a1 + 3), (e1, e1 + 4), 0.07, trim, trim)
    if ribs:
        for a in (-20, 10, 40, 70, 90, 110, 140, 170, 200):
            shell(HC, Rp(RF_O, 0.014), (a, a + 1.4), (-40, 80), 0.014, outer, "hotglow")
    DEFORM[0] = None; SE_N[0] = 2.0
    if hood_extra:                                                                              # stand-off shroud on posts
        SE_N[0] = 2.4
        shell(HC, Rp(RF_O, 0.05), (-40, 220), (10, 86), 0.005, hood_extra, "satin")
        shell(HC, Rp(RF_O, 0.05), (-140, -40), (44, 86), 0.005, hood_extra, "satin")
        SE_N[0] = 2.0
        for az in (-20, 40, 90, 140, 200):
            for el in (25, 60):
                a_, e_ = math.radians(az), math.radians(el); dvec = V((math.cos(e_) * math.cos(a_), math.cos(e_) * math.sin(a_), math.sin(e_)))
                cyl(HC + V((dvec.x * RF_O[0], dvec.y * RF_O[1], dvec.z * RF_O[2])) * 1.0, HC + V((dvec.x * (RF_O[0] + 0.05), dvec.y * (RF_O[1] + 0.05), dvec.z * (RF_O[2] + 0.05))), 0.005, m="titanium", seg=10)
    ring_strips(Rp(RF_I, 0.004), 0.008, (a0 + 1, a1 - 1), (e0 + 1, e1 - 1), 1.5, "orange")                    # face seal
    for s in (1, -1):                                                                           # temple pivots (outside: covers)
        cyl(HC + V((s * 0.205, 0.0, 0.0)), HC + V((s * 0.225, 0.0, 0.0)), 0.04, m=trim, seg=32)
        cyl(HC + V((s * 0.225, 0.0, 0.0)), HC + V((s * 0.23, 0.0, 0.0)), 0.022, m="titanium", seg=24)
        for k in range(6):
            a = 2 * math.pi * k / 6; BOLTS.append((HC + V((s * 0.2255, 0.03 * math.cos(a), 0.03 * math.sin(a))), V((s, 0, 0))))
    vp = HC + V((0.19, -0.13, -0.1)); vn = V((1, -0.6, -0.1)).normalized()                     # equalising valve
    cyl(vp, vp + vn * 0.035, 0.014, m="titanium", seg=20); cyl(vp + vn * 0.035, vp + vn * 0.042, 0.02, m=trim, seg=20)
    rbox(vp + vn * 0.046 + V((0, -0.015, 0)), (0.008, 0.035, 0.008), "orange", r=0.002)
    # stage 1: the shield = visor housing with the slit + mouth plate; slides up the rails into the crown pocket
    helm_begin()
    shell(HC, Rp(RF_I, 0.04), (a0 + 1, a1 - 1), (e0 + 1, e1 - 1), 0.014, shield_m, trim)
    vh = HC + V((0, -0.235, 0.025)); qh = Quaternion((1, 0, 0), math.radians(4))
    rbox(vh, (0.30, 0.05, 0.085), housing_m, qh, r=0.014, seg=3)
    rbox(vh + qh @ V((0, -0.026, 0)), (0.24, 0.006, 0.03), trim, qh, r=0.004, seg=2)
    rbox(vh + qh @ V((0, -0.029, 0)), (0.21, 0.004, 0.012), "visor", qh, r=0.002, seg=1)
    for sx in (-1, 1): BOLTS.append((vh + qh @ V((sx * 0.135, -0.026, 0.03)), qh @ V((0, -1, 0))))
    mp = HC + V((0, -0.215, -0.085)); qm = Quaternion((1, 0, 0), math.radians(-14))
    rbox(mp, (0.2, 0.03, 0.07), trim, qm, r=0.01, seg=2)
    rbox(mp + qm @ V((0, -0.016, 0)), (0.12, 0.004, 0.026), "warmglow", qm, r=0.002, seg=1)
    for i in range(7): rbox(mp + qm @ V((-0.054 + 0.018 * i, -0.019, 0)), (0.004, 0.004, 0.026), trim, qm, r=0.0005, seg=1)
    helm_end(key, HC, (1, 0, 0), -58)
    # stage 2: the inner pressure visor on the temple pivots
    helm_begin()
    shell(HC, Rp(RF_I, 0.012), (a0 + 2, a1 - 2), (e0 + 2, e1 - 2), 0.006, "glass", "glass")
    ring_strips(Rp(RF_I, 0.016), 0.009, (a0 + 2, a1 - 2), (e0 + 2, e1 - 2), 2, "titanium")
    for s in (1, -1): shell(HC, Rp(RF_I, 0.016), ((-178, a0) if s < 0 else (a1, -2)), (-3, 3), 0.007, "titanium")
    helm_end(key, HC, (1, 0, 0), -64)


def base_form(J, hard="titanium", hard_in="gunmetal", suit="fabric_seam", grille=True, belly=True, shoulders=True, coil=True):
    under_suit2(suit)
    # chest yoke (rigid, carries the shield mass) with the backlit grille window and a lamp; back yoke
    plate2(lambda i, co: ws(i, {"Spine", "Spine1", "Neck", "LeftShoulder", "RightShoulder"}) > 0.35,
           [((0, 0, 1.15), (0, 0, -1)), ((0, 0, 1.5), (0, 0, 1)), ((0, -0.03, 0), (0, 1, 0)), ((0.21, 0, 0), (1, 0, 0)), ((-0.21, 0, 0), (-1, 0, 0))],
           0.04, 0.018, hard, hard_in, smooth=24, bolts=0.07, mark=False, latches=2)
    plate2(lambda i, co: ws(i, {"Spine", "Spine1", "Neck", "LeftShoulder", "RightShoulder"}) > 0.35,
           [((0, 0, 1.08), (0, 0, -1)), ((0, 0, 1.5), (0, 0, 1)), ((0, -0.02, 0), (0, -1, 0)), ((0.2, 0, 0), (1, 0, 0)), ((-0.2, 0, 0), (-1, 0, 0))],
           0.038, 0.016, hard, hard_in, smooth=20, bolts=0.08)
    if grille:
        y = -0.255; rbox((0, y, 1.335), (0.27, 0.025, 0.11), "gunmetal", r=0.012, seg=2)
        rbox((0, y - 0.011, 1.335), (0.23, 0.006, 0.07), "warmglow", r=0.003, seg=1)
        for i in range(15): rbox((-0.105 + 0.015 * i, y - 0.016, 1.335), (0.004, 0.006, 0.07), "gunmetal", r=0.0008, seg=1)
        cyl((-0.085, y + 0.005, 1.235), (-0.085, y - 0.02, 1.235), 0.022, m="gunmetal", seg=24); cyl((-0.085, y - 0.019, 1.235), (-0.085, y - 0.024, 1.235), 0.015, m="bluelamp", seg=24)
        for k in (0.04, 0.075): rbox((k, y - 0.004, 1.235), (0.022, 0.006, 0.012), "warmglow", r=0.002, seg=1)
        rbox((0.06, y - 0.003, 1.19), (0.07, 0.004, 0.012), "marking", r=0.001, seg=1)
    if coil:                                                                                    # field coil around the collar
        tor((0, -0.04, 1.49), (0, 0, 1), 0.2, 0.024, "gunmetal", sy=1.05)
        tor((0, -0.04, 1.49), (0, 0, 1), 0.2 + 0.023, 0.004, "glow", sy=1.05, nv=8)
    if belly:                                                                                    # banded belly
        for z0, z1 in ((0.905, 0.965), (0.975, 1.035), (1.045, 1.105)):
            plate2(SB({"LowerBack", "Spine", "Hips"}, 0.3), [((0, 0, z0), (0, 0, -1)), ((0, 0, z1), (0, 0, 1))], 0.032, 0.012, hard, hard_in, smooth=14, bolts=0.09)
    if shoulders:                                                                                # heavy layered shoulders
        for s in (1, -1):
            S = SIDE(s); j = J[s]; d = j["EL"] - j["SH"]
            plate2(SB({S + "Shoulder", S + "Arm"}, 0.4), [(j["SH"] + d * 0.42, d), ((0, 0, 1.27), (0, 0, -1))], 0.05, 0.02, hard, hard_in, smooth=12, bolts=0.07)
            plate2(SB({S + "Arm"}, 0.4), [(j["SH"] + d * 0.62, d), (j["SH"] + d * 0.36, -d), ((0, 0, 1.2), (0, 0, -1))], 0.04, 0.014, hard, hard_in, smooth=10, bolts=0.07)
    plate2(SB({"Hips", "LHipJoint", "RHipJoint", "LeftUpLeg", "RightUpLeg"}, 0.3), [((0, 0, 0.8), (0, 0, -1)), ((0, 0, 0.895), (0, 0, 1))], 0.03, 0.014, hard, hard_in, smooth=12, bolts=0.08)
    rx = V((0, 0.215, 1.2))                                                                      # back: battery / life support cassette
    rbox(rx, (0.34, 0.11, 0.42), hard, r=0.02, seg=2); rbox(rx + V((0, 0.056, 0)), (0.3, 0.004, 0.38), hard_in, r=0.003, seg=1)
    for k in (-0.1, 0.1): rbox(rx + V((k, 0.059, 0.17)), (0.05, 0.004, 0.012), "accent", r=0.002, seg=1)


def limb_plates(J, m, m_in, off=0.03, thick=0.012, front=True, smooth=8, mark=True):
    for s in (1, -1):
        S = SIDE(s); j = J[s]; fr = (0, 1, 0) if front else None
        plate2(SB({S + "ForeArm"}), limb_planes(j["EL"], j["WR"], 0.12, 0.84), off * 0.8, thick, m, m_in, smooth)
        plate2(SB({S + "UpLeg"}), limb_planes(j["HIP"], j["KN"], 0.2, 0.82, fr), off, thick, m, m_in, smooth, mark=mark and s > 0)
        plate2(SB({S + "Leg"}), limb_planes(j["KN"], j["AN"], 0.12, 0.8, fr), off, thick, m, m_in, smooth)
        plate2(SB({S + "UpLeg", S + "Leg"}, 0.3), [(j["KN"] + V((0, 0, -0.05)), (0, 0, -1)), (j["KN"] + V((0, 0, 0.06)), (0, 0, 1)), (j["KN"] + V((0, -0.02, 0)), (0, 1, 0))],
               off * 1.2, thick, m, m_in, smooth)


def vE1(J):  # «Мантия»: stand-off thermal shroud panels (gold film, titanium rims) on posts over the base
    base_form(J, "titanium", "gunmetal", "light_seam")
    G = dict(off=0.06, thick=0.005, m="goldfilm", m_in="satin", m_rim="titanium", smooth=14, bevel=0.002, bolts=0.1, standoffs=0.12, latches=2)
    for s in (1, -1):
        S = SIDE(s); j = J[s]
        plate2(SB({S + "UpLeg"}), limb_planes(j["HIP"], j["KN"], 0.12, 0.84, (0, 1, 0)), **G)
        plate2(SB({S + "Leg"}), limb_planes(j["KN"], j["AN"], 0.1, 0.82, (0, 1, 0)), **dict(G, off=0.05))
        plate2(SB({S + "ForeArm"}), limb_planes(j["EL"], j["WR"], 0.1, 0.84, (-s, 0, 0)), **dict(G, off=0.045))
        plate2(SB({S + "Arm"}), limb_planes(j["SH"], j["EL"], 0.5, 0.9, (-s, 0, 0)), **dict(G, off=0.05))
    q = Quaternion((1, 0, 0), math.radians(-35)); pc = V((0, 0.33, 1.38))                       # sky-facing radiator on posts
    rbox(pc, (0.32, 0.02, 0.24), "titanium", q, r=0.004, seg=2)
    for i in range(15): rbox(pc + q @ V((-0.14 + 0.02 * i, 0.016, 0)), (0.003, 0.014, 0.22), "satin2", q, r=0.001, seg=1)
    for k in (-0.12, 0.12): cyl(pc + q @ V((k, -0.01, -0.1)), V((k, 0.27, 1.32)), 0.008, m="titanium", seg=12)
    flush_bolts(); boots_E("titanium"); helmet_F("E1", "titanium", "gunmetal", "titanium", "titanium", hood_extra="goldfilm")


def vE2(J):  # «Чешуя»: tiled lamellar plates in fastened rows on the limbs, tiled helmet
    base_form(J, "satin", "gunmetal", "fabric_seam")
    T = 2 * math.pi; segs = []
    for s in (1, -1):
        j = J[s]
        segs += [(j["SH"], j["EL"], 4, 9, 0.55, 0.9, 0.0, T), (j["EL"], j["WR"], 5, 9, 0.08, 0.84, 0.0, T),
                 (j["HIP"], j["KN"], 7, 11, 0.12, 0.84, 0.0, T), (j["KN"], j["AN"], 6, 10, 0.1, 0.82, 0.0, T)]
    lamellar(segs)
    flush_bolts(); boots_E("satin", "gunmetal"); helmet_F("E2", "satin", "gunmetal", "satin", "satin", tiles=True)


def vE3(J):  # «Крыло»: articulated directional shield panels (deployed in front views, folded flat on the back)
    base_form(J, "titanium", "gunmetal", "fabric_seam"); limb_plates(J, "titanium", "gunmetal")
    folded, deployed = [], []
    for s in (1, -1):
        mount = V((s * 0.13, 0.27, 1.38))
        g = []; GROUP.append(g)
        rbox(mount, (0.07, 0.05, 0.08), "gunmetal", r=0.008, seg=2); arm_links([mount, V((s * 0.15, 0.31, 1.36))])
        shield_panel(V((s * 0.17, 0.33, 1.0)), V((0, 1, 0)), V((0, 0, 1)), 0.3, 0.6)
        GROUP.remove(g); folded += g
        g = []; GROUP.append(g)
        rbox(mount, (0.07, 0.05, 0.08), "gunmetal", r=0.008, seg=2)
        if s > 0:
            e = V((0.36, 0.3, 1.45)); w_ = V((0.58, 0.1, 1.34))
            arm_links([mount, e, w_]); shield_panel(V((0.68, -0.08, 1.16)), V((0.85, -0.45, -0.25)).normalized(), V((0, 0, 1)), 0.34, 0.6)
        else:
            e = V((-0.24, 0.33, 1.64)); w_ = V((-0.3, 0.12, 1.8))
            arm_links([mount, e, w_]); shield_panel(V((-0.36, -0.1, 1.86)), V((-0.25, -0.75, 0.62)).normalized(), V((0, 0.4, 1.0)), 0.34, 0.56)
        GROUP.remove(g); deployed += g
    WINGS["E3"] = (folded, deployed)
    flush_bolts(); boots_E("titanium"); helmet_F("E3", "gunmetal", "titanium", "gunmetal", "gunmetal")


def vE4(J):  # «Испарина»: porous transpiration ceramic, longitudinal heat-exchanger fins
    base_form(J, "porous", "satin2", "fabric_seam")
    limb_plates(J, "porous", "satin2", off=0.03, thick=0.014, front=False, smooth=10, mark=False)
    segs = []
    for s in (1, -1):
        j = J[s]; A = [math.radians(a) for a in range(15, 360, 45)]
        segs += [(j["EL"], j["WR"], 0.16, 0.8, A), (j["HIP"], j["KN"], 0.24, 0.78, A), (j["KN"], j["AN"], 0.16, 0.76, A)]
    fins_E2(segs, off=0.045)
    flush_bolts(); boots_E("porous", "gunmetal"); helmet_F("E4", "porous", "satin2", "porous", "porous", ribs=True)


def vE5(J):  # «Контур»: field-dominant - light gold suit, rigid collar/yoke, spine bar and belt carry the coils
    base_form(J, "titanium", "gunmetal", "gold_seam", belly=False, shoulders=False)
    plate2(lambda i, co: abs(co.x) < 0.045 and co.y > -0.02 and ws(i, {"Spine", "Spine1", "LowerBack"}) > 0.3,
           [((0, 0, 0.92), (0, 0, -1)), ((0, 0, 1.12), (0, 0, 1))], 0.03, 0.02, "titanium", "gunmetal", smooth=6, bolts=0.06)
    plate2(SB({"LowerBack", "Hips", "Spine"}, 0.3), [((0, 0, 0.93), (0, 0, -1)), ((0, 0, 1.0), (0, 0, 1))], 0.03, 0.02, "titanium", "gunmetal", bolts=0.07, latches=2)
    tor((0, -0.03, 0.965), (0, 0, 1), 0.205, 0.006, "glow", sy=0.78, nv=8)
    for s in (1, -1):
        S = SIDE(s); j = J[s]; d = j["EL"] - j["SH"]
        plate2(SB({S + "Shoulder", S + "Arm"}, 0.4), [(j["SH"] + d * 0.3, d), ((0, 0, 1.3), (0, 0, -1))], 0.04, 0.016, "titanium", "gunmetal", mark=s > 0)
        plate2(SB({"Hips", S + "UpLeg"}, 0.3), [((0, 0, 0.8), (0, 0, -1)), ((0, 0, 0.9), (0, 0, 1)), ((s * 0.17, 0, 0), (-s, 0, 0))], 0.032, 0.014, "titanium", "gunmetal")
    flush_bolts(); boots_E("titanium"); helmet_F("E5", "goldfilm", "titanium", "titanium", "titanium")


VARIANTS = {"1": v1, "2": v2, "3": v3, "4": v4, "5": v5, "1b": v1b, "3b": v3b, "13": v13, "C1": vC1, "C2": vC2, "D": vD,
            "E1": vE1, "E2": vE2, "E3": vE3, "E4": vE4, "E5": vE5}


def shoot_E(cam, i):
    sc = bpy.context.scene
    def one(name, loc, tgt, lens):
        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
        cam.rotation_quaternion = (V(tgt) - V(loc)).to_track_quat('-Z', 'Y'); bpy.context.view_layer.update()
        sc.render.filepath = os.path.join(OUT, "pantsir_v%s_%s.png" % (i, name)); bpy.ops.render.render(write_still=True); log.append("rendered %s %s" % (i, name))
    wing_set(i, True); one("front", (2.55, -3.95, 1.55), (0, 0, 0.97), 80); wing_set(i, False)
    one("back", (-2.55, 3.95, 1.65), (0, 0, 0.97), 80)
    set_open(i, True); one("open", HCE + V((0.85, -1.2, 0.16)), HCE + V((0, 0, -0.04)), 52); set_open(i, False)
FACETED = ("1b", "3b", "13")
NAMES2 = {1: "slit hood", 2: "periscope dome", 3: "shutter window", 4: "plate + frame", 5: "smooth halo"}


def set_world():
    sc = bpy.context.scene; w = sc.world; nt = w.node_tree
    bg = next(n for n in nt.nodes if n.type == 'BACKGROUND')
    tc = nt.nodes.new("ShaderNodeTexCoord"); sx = nt.nodes.new("ShaderNodeSeparateXYZ"); cr = nt.nodes.new("ShaderNodeValToRGB")
    nt.links.new(tc.outputs["Generated"], sx.inputs[0]); nt.links.new(sx.outputs["Z"], cr.inputs["Fac"]); nt.links.new(cr.outputs[0], bg.inputs[0])
    cr.color_ramp.elements[0].position = 0.35; cr.color_ramp.elements[0].color = (0.05, 0.045, 0.04, 1)
    cr.color_ramp.elements[1].position = 0.75; cr.color_ramp.elements[1].color = (0.22, 0.24, 0.28, 1)
    e = cr.color_ramp.elements.new(0.52); e.color = (0.55, 0.47, 0.38, 1)
    bg.inputs[1].default_value = 0.7
    fl = bpy.data.objects.get("Floor")
    if fl:
        fm = bpy.data.materials.new("HS_floor"); fm.use_nodes = True
        pb = next(n for n in fm.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        pb.inputs["Base Color"].default_value = (0.11, 0.105, 0.10, 1); pb.inputs["Roughness"].default_value = 0.8
        fl.data.materials.clear(); fl.data.materials.append(fm)


SAVED = {}


def set_open(i, on, stages=None):
    from mathutils import Matrix
    st = HINGE[i] if stages is None else HINGE[i][:stages]
    if on:
        for objs, pivot, axis, ang, move in st:
            T = Matrix.Translation(move) @ Matrix.Translation(pivot) @ Quaternion(axis, ang).to_matrix().to_4x4() @ Matrix.Translation(-pivot)
            for o in objs:
                SAVED.setdefault(o.name, o.matrix_world.copy()); o.matrix_world = T @ o.matrix_world
            bpy.context.view_layer.update()
    else:
        for n, mw in SAVED.items(): bpy.data.objects[n].matrix_world = mw
        SAVED.clear()
    bpy.context.view_layer.update()


def main():
    sys.path.insert(0, BLEND_DIR); import render_util
    os.makedirs(OUT, exist_ok=True)
    make_materials(); make_materials2(); make_materials_D(); make_materials_E(); make_materials_E2(); J = joints()
    log.append("joints: " + ", ".join("%s=%s" % (k, tuple(round(x, 3) for x in v)) for k, v in J[1].items()))
    cam = render_util.setup_stage(); set_world()
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 12 if QUICK else 48
    sc.cycles.use_denoising = True; sc.render.resolution_x = sc.render.resolution_y = 500 if QUICK else 1200
    sc.render.resolution_percentage = 100; sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Medium High Contrast'
    sc.cycles.max_bounces = 6
    base = [o for o in bpy.data.objects if o.type == 'MESH' and o.name != "Floor"]
    keep = [o for o in base if o.name == "SuitBody" or (o.parent is not None and o.parent.type == 'ARMATURE' and o.name != "Astronavigator.body")]
    for o in base: o.hide_render = o not in keep
    colls = {}
    want = os.environ.get("VARIANTS", "E1,E2,E3,E4,E5").split(",")
    if any(w.startswith("E") for w in want):                                                     # one dramatic cool rim light
        l = bpy.data.lights.new("RimE", 'AREA'); l.energy = 700; l.size = 1.2; l.color = (0.72, 0.84, 1.0)
        lo = bpy.data.objects.new("RimE", l); sc.collection.objects.link(lo); lo.location = (-1.6, 2.4, 2.6)
        lo.rotation_mode = 'QUATERNION'; lo.rotation_quaternion = (V((0, 0, 1.1)) - V(lo.location)).to_track_quat('-Z', 'Y')
    for i, fn in VARIANTS.items():
        if i not in want: continue
        c = bpy.data.collections.new("Pantsir_V%s" % i); sc.collection.children.link(c); COLL[0] = c; colls[i] = c
        FACET[0] = i in FACETED
        try:
            fn(J); log.append("V%s built: %d objects, hinge stages %s" % (i, len(c.objects), [len(st[0]) for st in HINGE.get(i, [])]))
        except Exception:
            log.append("V%s FAILED\n%s" % (i, traceback.format_exc())); GROUP.clear()
        FACET[0] = False
    shots = [("front", (2.55, -3.95, 1.55), 0.97, 80), ("back", (-2.55, 3.95, 1.65), 0.97, 80)]
    for i in colls:
        for k, c in colls.items(): c.hide_render = (k != i)
        if i == "D" or i.startswith("E"): bpy.data.objects["SuitBody"].hide_render = True
        if i.startswith("E"):
            shoot_E(cam, i); open(LOG, "w", encoding="utf-8").write("\n".join(log) + "\n"); continue
        render_util.shoot(cam, OUT, [("pantsir_v%s_%s" % (i, n), loc, tz, lens) for n, loc, tz, lens in shots], log)
        close = ((0.95, -1.35, 1.9), 1.7, 58) if i in ("1", "2", "3", "4", "5") else ((0.85, -1.3, 1.78), 1.62, 58)   # rev. 3: head higher
        if i in HINGE:
            if i == "D":
                h = HC_W[0]; cl = [(h + V((1.0, -1.45, 0.2)), h + V((0, 0, 0.0)), 50)]
                for stg, nm in ((1, "half"), (None, "open")):
                    set_open(i, True, stg)
                    for loc, tgt, lens in cl:
                        cam.location = loc; cam.data.lens = lens; cam.rotation_mode = 'QUATERNION'
                        cam.rotation_quaternion = (tgt - loc).to_track_quat('-Z', 'Y'); bpy.context.view_layer.update()
                        sc.render.filepath = os.path.join(OUT, "pantsir_vD_%s.png" % nm); bpy.ops.render.render(write_still=True); log.append("rendered pantsir_vD_" + nm)
                    set_open(i, False)
                continue
            if i in ("13", "C1", "C2"):
                set_open(i, True, 1); render_util.shoot(cam, OUT, [("pantsir_v%s_half" % i,) + close], log); set_open(i, False)
            set_open(i, True)
            render_util.shoot(cam, OUT, [("pantsir_v%s_open" % i,) + close], log)
            set_open(i, False)
        open(LOG, "w", encoding="utf-8").write("\n".join(log) + "\n")


try:
    main()
except Exception:
    log.append(traceback.format_exc())
finally:
    open(LOG, "w", encoding="utf-8").write("\n".join(log) + "\n")
    bpy.ops.wm.quit_blender()
