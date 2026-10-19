# «Панцирь» - faceted chest shield and the «Ладонь» hand capsule: the master sketch for rev. M (user, 2026-10-17:
# "грудь и кисть: спереди, сбоку, сверху, с ярко выраженными гранями; 3D строится точно по эскизу").
# All coordinates in metres. Chest: x lateral (+ = figure's left), z height above the ground, y forward (+ = front).
# Hand: x forward from the wrist coupling face, y lateral (+ = thumb side), z up (back of the hand).
# Every facet is one flat plane, filled with its own tone (light = faces up/front, dark = faces down/sideways): the 3D
# must use the same planes with flat shading, dark seams and ≥ 20° between neighbouring facets, so the faces read.
import os
HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 2000, 1500
INK, THIN, NOTE, GRID, DIM = "#1d2733", "#5d6b78", "#2f6f9f", "#e2e6ea", "#b0523a"
T1, T2, T3, T4, T5 = "#e9e7e1", "#d2d0c9", "#b9b7b0", "#9e9c96", "#85837e"      # facet tones: lit -> dark
SHELL, MOT, PAD, RED, GHOST = "#8d969e", "#4a525a", "#2b2f33", "#c8463c", "#e59a55"
out = []

class View:
    def __init__(s, ox, oy, sc): s.ox, s.oy, s.sc = ox, oy, sc
    def p(s, a, b): return "%.1f,%.1f" % (s.ox + a * s.sc, s.oy - b * s.sc)
    def xy(s, a, b): return s.ox + a * s.sc, s.oy - b * s.sc
def poly(v, pts, fill, stroke=INK, w=1.8, op=1.0, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    out.append('<polygon points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' %
               (" ".join(v.p(*q) for q in pts), fill, op, stroke, w, d))
def line(v, a, b, stroke=INK, w=1.2, dash=None):
    d = ' stroke-dasharray="%s"' % dash if dash else ""
    x1, y1 = v.xy(*a); x2, y2 = v.xy(*b)
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="%.1f"%s/>' % (x1, y1, x2, y2, stroke, w, d))
def circ(v, c, r, fill, w=1.2):
    x, y = v.xy(*c); out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" stroke="%s" stroke-width="%.1f"/>' % (x, y, r * v.sc, fill, INK, w))
def txt(x, y, s, size=14, color=NOTE, anchor="start", weight="normal"):
    out.append('<text x="%.1f" y="%.1f" font-family="Segoe UI, Arial" font-size="%d" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, size, color, anchor, weight, s))
def vtxt(v, a, b, s, size=12, color=DIM, anchor="middle"):
    x, y = v.xy(a, b); txt(x, y, s, size, color, anchor)
def mir(pts): return [(-a, b) for a, b in pts]

# ======================================= CHEST =====================================================================
# Shield plates (front view, left half; mirrored). Rows from the top, each overlaps the one below by ~1/3 (shingles):
#   C1 clavicle plate  z 1.445..1.37  tilted back 30° toward the collar
#   C2 pectoral plate  z 1.395..1.215 two planes: inner (yaw 18° out from the sternum ridge) + outer wrap (yaw 55°)
#   C3 rib plate       z 1.240..1.120 narrows toward the waist, pitched 10° down-forward
#   C4 belly plate     z 1.140..1.040 narrowest, pitched 18° down-back (tucks toward the pelvis ring)
# The sternum ridge (x = 0) is a real crease: 22 mm proud on C2, 14 mm on C3, 8 mm on C4.
C1 = [(0.0, 1.445), (0.150, 1.445), (0.215, 1.405), (0.205, 1.370), (0.0, 1.372)]
C2i = [(0.0, 1.395), (0.180, 1.395), (0.195, 1.225), (0.0, 1.215)]
C2o = [(0.180, 1.395), (0.215, 1.405), (0.272, 1.360), (0.262, 1.250), (0.195, 1.225)]
C3i = [(0.0, 1.240), (0.180, 1.240), (0.170, 1.125), (0.0, 1.118)]
C3o = [(0.180, 1.240), (0.250, 1.250), (0.236, 1.140), (0.170, 1.125)]
C4i = [(0.0, 1.140), (0.165, 1.140), (0.150, 1.048), (0.0, 1.040)]
C4o = [(0.165, 1.140), (0.228, 1.150), (0.212, 1.060), (0.150, 1.048)]
ROWS = [(C4i, T2, C4o, T4), (C3i, T1, C3o, T3), (C2i, T1, C2o, T3), (C1, T2, None, None)]   # draw bottom-up (upper overlaps)

def chest_front(v):
    # torso shell outline (enamel) behind
    hut = [(0.0, 1.575), (0.165, 1.545), (0.275, 1.47), (0.28, 1.42), (0.262, 1.36), (0.245, 1.30), (0.238, 1.20), (0.24, 1.02), (0.0, 1.02)]
    poly(v, hut + mir(hut[::-1]), SHELL, w=2.2)
    poly(v, [(-0.17, 1.545), (0.17, 1.545), (0.165, 1.575), (-0.165, 1.575)], MOT, w=1.4)                   # collar bearing
    for inner, ti, outer, to in ROWS:
        for pts, t in ((inner, ti), (outer, to)):
            if pts is None: continue
            poly(v, pts, t, w=2.0); poly(v, mir(pts), t, w=2.0)
        lo = min(q[1] for q in inner)
        xr = max(q[0] for q in (outer or inner))
        line(v, (-xr + 0.01, lo + 0.006), (xr - 0.01, lo + 0.006), INK, 2.6)                             # the folded lip (dark)
    line(v, (0, 1.445), (0, 1.04), INK, 2.4)                                                              # sternum crease
    for z in (1.43, 1.33, 1.25, 1.17, 1.09):
        for xs in (0.03, 0.15):
            circ(v, (xs, z), 0.004, MOT); circ(v, (-xs, z), 0.004, MOT)                                   # post heads
    vtxt(v, 0.09, 1.40, "C1", 13, INK); vtxt(v, 0.09, 1.30, "C2", 13, INK); vtxt(v, 0.23, 1.31, "C2o", 11, INK)
    vtxt(v, 0.09, 1.175, "C3", 13, INK); vtxt(v, 0.09, 1.09, "C4", 13, INK)
    vtxt(v, -0.09, 1.50, "ПАНЦИРЬ-2", 12, RED)
    line(v, (-0.272, 0.995), (0.272, 0.995), DIM, 1.0); vtxt(v, 0, 0.975, "0,544 м по экрану на уровне C2", 12)

def chest_side(v):
    # y forward to the right; profile of the shell and the shingled plates (front edge)
    shell = [(-0.14, 1.545), (0.12, 1.545), (0.165, 1.47), (0.163, 1.36), (0.168, 1.22), (0.138, 1.02), (-0.16, 1.02), (-0.18, 1.40)]
    poly(v, shell, SHELL, w=2.2)
    # plates: each a facet in profile (y at the ridge), with a lip turned inward at the lower edge
    P = [([(0.130, 1.452), (0.205, 1.372), (0.198, 1.364), (0.140, 1.440)], T2, "C1 · наклон назад 30°"),
         ([(0.205, 1.398), (0.207, 1.215), (0.193, 1.210), (0.192, 1.392)], T1, "C2 · вертикаль, ребро +22 мм"),
         ([(0.198, 1.242), (0.180, 1.118), (0.166, 1.115), (0.185, 1.238)], T1, "C3 · вперёд-вниз 10° (обратный скат)"),
         ([(0.178, 1.142), (0.150, 1.040), (0.137, 1.040), (0.165, 1.140)], T2, "C4 · назад-вниз 18°")]
    for k, (pts, t, lab) in enumerate(P):
        poly(v, pts, t, w=2.0)
        lo = min(pts, key=lambda q: q[1]); line(v, lo, (lo[0] - 0.014, lo[1] + 0.004), INK, 2.6)          # inward lip
        x, y = v.xy(pts[1][0] + 0.02, (pts[0][1] + pts[1][1]) / 2); txt(x, y, lab, 12, DIM)
    for z, y in ((1.40, 0.15), (1.30, 0.17), (1.18, 0.15), (1.09, 0.13)):
        line(v, (y, z), (y + 0.022, z), MOT, 3.0)                                                         # standoff posts
    vtxt(v, 0.0, 0.975, "сбоку: зазор 15–22 мм внутри габарита, губа загнута внутрь", 12)

def chest_top(v):
    # plan section at z = 1.30 (through C2): y forward up the page, x lateral
    shell = [(0.0, 0.168), (0.18, 0.15), (0.245, 0.09), (0.25, -0.05), (0.18, -0.17), (0.0, -0.18)]
    poly(v, shell + mir(shell[::-1]), SHELL, w=2.2)
    pi = [(0.0, 0.207), (0.180, 0.192), (0.180, 0.180), (0.0, 0.195)]
    po = [(0.180, 0.192), (0.262, 0.120), (0.255, 0.112), (0.180, 0.180)]
    for pts, t in ((pi, T1), (po, T3)):
        poly(v, pts, t, w=2.0); poly(v, mir(pts), t, w=2.0)
    for xs, ys in ((0.06, 0.17), (0.15, 0.165), (0.22, 0.12)):
        line(v, (xs, ys), (xs, ys + 0.017), MOT, 3.0); line(v, (-xs, ys), (-xs, ys + 0.017), MOT, 3.0)
    vtxt(v, 0.0, 0.225, "ребро грудины", 12); vtxt(v, 0.10, 0.235, "18°", 12); vtxt(v, 0.25, 0.165, "55°", 12)
    poly(v, [(-0.20, -0.18), (0.20, -0.18), (0.20, -0.215), (-0.20, -0.215)], "#6f7a84", w=1.2)                 # the door (open to the sky)
    vtxt(v, 0, -0.245, "дверь-радиатор сзади - без экрана", 12)
    vtxt(v, 0, -0.29, "сверху (разрез z = 1,30, через C2)", 12, THIN)

# ======================================= HAND CAPSULE ===============================================================
# Top view (x forward, y lateral, thumb side +y). Back plate = 4 facets: two metacarpal planes either side of a central
# crease (each tilted 12° sideways), a thumb-side chamfer and a little-finger-side chamfer (40°), plus the knuckle bevel
# (the front strip, sloping down 35°). The thenar mound is a separate 3-facet block on the thumb side underneath.
HX = [0.0, 0.020, 0.115, 0.145, 0.160]          # cuff end, cuff/plate joint, widest, bevel start, knuckle line
def hand_top(v):
    poly(v, [(-0.06, -0.050), (-0.02, -0.048), (-0.02, 0.048), (-0.06, 0.050)], SHELL, w=2.0)              # forearm end
    poly(v, [(-0.02, -0.052), (0.0, -0.052), (0.0, 0.052), (-0.02, 0.052)], MOT, w=1.6)                    # coupling ring
    circ(v, (-0.010, 0.058), 0.005, RED)
    # thenar mound (seen past the edge on the thumb side)
    poly(v, [(0.030, 0.056), (0.110, 0.066), (0.118, 0.082), (0.080, 0.090), (0.045, 0.074)], T4, w=1.8)
    # back facets: crease at y = 0.004
    cr = 0.004
    A = [(0.0, cr), (0.0, 0.046), (0.115, 0.056), (0.145, 0.050), (0.145, cr)]                  # thumb-side metacarpal plane
    B = [(0.0, cr), (0.0, -0.046), (0.115, -0.060), (0.145, -0.054), (0.145, cr)]               # little-finger-side plane
    Ac = [(0.0, 0.046), (0.0, 0.050), (0.115, 0.062), (0.115, 0.056)]                           # thin chamfer strips
    Bc = [(0.0, -0.046), (0.0, -0.050), (0.115, -0.066), (0.115, -0.060)]
    K = [(0.145, 0.050), (0.160, 0.044), (0.160, -0.050), (0.145, -0.054)]                       # knuckle bevel
    poly(v, A, T1, w=2.2); poly(v, B, T2, w=2.2); poly(v, Ac, T3, w=1.6); poly(v, Bc, T4, w=1.6); poly(v, K, T3, w=2.0)
    line(v, (0.0, cr), (0.145, cr), INK, 2.4)                                                   # the crease
    for xx, yy in ((0.03, 0.03), (0.03, -0.03), (0.11, 0.035), (0.11, -0.04)): circ(v, (xx, yy), 0.003, MOT)
    poly(v, [(0.090, -0.040), (0.120, -0.040), (0.120, -0.030), (0.090, -0.030)], RED, w=0.8)   # grip-lock indicator
    # knuckle ridge + effector
    poly(v, [(0.160, -0.050), (0.168, -0.050), (0.168, 0.044), (0.160, 0.044)], MOT, w=1.6)
    def dig(y0, wd, segs, ang=0.0):
        x = 0.168
        for k, L in enumerate(segs):
            w0, w1 = wd * (1 - 0.06 * k), wd * (1 - 0.06 * (k + 1))
            poly(v, [(x + 0.002, y0 - w0 / 2), (x + L - 0.006, y0 - w1 / 2), (x + L, y0 - w1 / 2 + 0.004), (x + L, y0 + w1 / 2 - 0.004), (x + L - 0.006, y0 + w1 / 2), (x + 0.002, y0 + w0 / 2)], T2, w=1.8)
            line(v, (x + 0.004, y0 - w0 * 0.2), (x + L - 0.006, y0 - w1 * 0.2), THIN, 0.8); line(v, (x + 0.004, y0 + w0 * 0.2), (x + L - 0.006, y0 + w1 * 0.2), THIN, 0.8)
            circ(v, (x, y0), min(wd, 0.024) * 0.34, MOT); x += L
    dig(0.026, 0.022, (0.055, 0.040, 0.032))                                                    # index
    dig(-0.018, 0.044, (0.052, 0.040, 0.032))                                                   # broad digit
    line(v, (0.17, -0.004), (0.29, -0.006), THIN, 0.9); line(v, (0.17, -0.032), (0.29, -0.030), THIN, 0.9)
    # thumb seen from above: out of the thenar, forward and under
    poly(v, [(0.100, 0.082), (0.148, 0.070), (0.150, 0.056), (0.104, 0.066)], T3, w=1.8); circ(v, (0.104, 0.075), 0.007, MOT)
    poly(v, [(0.150, 0.068), (0.186, 0.052), (0.184, 0.040), (0.150, 0.056)], T3, w=1.8); circ(v, (0.150, 0.062), 0.006, MOT)
    for a, lab in ((0.0, "0"), (0.115, "0,115"), (0.160, "0,16")): vtxt(v, a, -0.095, lab, 11)
    vtxt(v, 0.0, 0.070, "0,092", 11); vtxt(v, 0.115, -0.080, "0,126", 11)
    vtxt(v, 0.20, 0.105, "большой", 11); vtxt(v, 0.07, 0.105, "бугор (тенар)", 11)
    vtxt(v, 0.12, -0.125, "сверху: две плоскости у срединной складки, фаски по краям, скос к костяшкам", 12, THIN)

def hand_side(v):
    # side from the thumb side: x forward, z up
    poly(v, [(-0.06, -0.040), (-0.02, -0.037), (-0.02, 0.040), (-0.06, 0.042)], SHELL, w=2.0)
    poly(v, [(-0.02, -0.042), (0.0, -0.042), (0.0, 0.045), (-0.02, 0.045)], MOT, w=1.6)
    top = [(0.0, 0.040), (0.060, 0.042), (0.145, 0.026), (0.160, 0.014)]                          # back: flat, then 12° slope, bevel 35°
    poly(v, [(0.0, 0.040), (0.060, 0.042), (0.060, 0.030), (0.0, 0.028)], T1, w=2.0)
    poly(v, [(0.060, 0.042), (0.145, 0.026), (0.145, 0.014), (0.060, 0.030)], T2, w=2.0)
    poly(v, [(0.145, 0.026), (0.160, 0.014), (0.160, 0.004), (0.145, 0.014)], T3, w=2.0)
    poly(v, [(0.0, 0.028), (0.160, 0.004), (0.160, -0.026), (0.110, -0.030), (0.0, -0.034)], SHELL, w=1.8)   # capsule wall
    poly(v, [(0.0, -0.034), (0.110, -0.030), (0.160, -0.026), (0.160, -0.032), (0.110, -0.038), (0.0, -0.040)], T4, w=1.8)  # palm plate, slightly concave
    poly(v, [(0.030, -0.036), (0.110, -0.034), (0.120, -0.046), (0.085, -0.055), (0.045, -0.050)], T3, w=2.0)   # thenar mound
    poly(v, [(0.160, -0.028), (0.168, -0.028), (0.168, 0.012), (0.160, 0.012)], MOT, w=1.6)                      # knuckle ridge
    import math
    def chain(x, z, a, segs, th):
        for k, L in enumerate(segs):
            r = math.radians(a); ux, uz = math.cos(r), math.sin(r); nx, nz = -uz, ux; t = th * (1 - 0.08 * k)
            poly(v, [(x + nx * t / 2, z + nz * t / 2), (x + ux * L + nx * t * 0.42, z + uz * L + nz * t * 0.42), (x + ux * L - nx * t * 0.42, z + uz * L - nz * t * 0.42), (x - nx * t / 2, z - nz * t / 2)], T2, w=1.8)
            poly(v, [(x + ux * L * 0.2 - nx * t * 0.5, z + uz * L * 0.2 - nz * t * 0.5), (x + ux * L * 0.85 - nx * t * 0.45, z + uz * L * 0.85 - nz * t * 0.45),
                     (x + ux * L * 0.85 - nx * t * 0.25, z + uz * L * 0.85 - nz * t * 0.25), (x + ux * L * 0.2 - nx * t * 0.3, z + uz * L * 0.2 - nz * t * 0.3)], PAD, w=0.6)
            circ(v, (x, z), t * 0.36, MOT); x, z = x + ux * L, z + uz * L; a -= 18
    chain(0.168, -0.004, -6, (0.055, 0.040, 0.032), 0.024)
    chain(0.100, -0.050, -40, (0.048, 0.038), 0.022)                                                              # thumb from the thenar, under
    # the man's hand inside (ghost)
    poly(v, [(0.005, -0.018), (0.12, -0.014), (0.150, -0.010), (0.150, 0.006), (0.12, 0.012), (0.005, 0.020)], GHOST, stroke=GHOST, w=1.4, op=0.2, dash="5,3")
    vtxt(v, 0.03, 0.058, "плоско", 11); vtxt(v, 0.10, 0.050, "скат 12°", 11); vtxt(v, 0.165, 0.030, "35°", 11)
    vtxt(v, 0.075, -0.075, "тенар", 11); vtxt(v, 0.12, -0.135, "сбоку (со стороны большого): толщина 0,075 у запястья, 0,045 у костяшек", 12, THIN)

def hand_front(v):
    # section at x = 0.10 looking forward: y lateral (thumb side on the right), z up
    sec_back = [(-0.064, 0.008), (-0.058, 0.026), (0.004, 0.036), (0.060, 0.026), (0.066, 0.008)]
    poly(v, [(-0.058, 0.026), (0.004, 0.036), (0.004, 0.026), (-0.052, 0.018)], T2, w=2.0)                 # little-side plane
    poly(v, [(0.004, 0.036), (0.060, 0.026), (0.054, 0.018), (0.004, 0.026)], T1, w=2.0)                  # thumb-side plane
    poly(v, [(-0.064, 0.008), (-0.058, 0.026), (-0.052, 0.018), (-0.058, 0.004)], T4, w=1.6)
    poly(v, [(0.066, 0.008), (0.060, 0.026), (0.054, 0.018), (0.060, 0.004)], T3, w=1.6)
    poly(v, [(-0.058, 0.004), (0.060, 0.004), (0.060, -0.026), (0.0, -0.022), (-0.058, -0.026)], SHELL, w=1.8)
    poly(v, [(-0.058, -0.026), (0.0, -0.022), (0.060, -0.026), (0.060, -0.032), (0.0, -0.028), (-0.058, -0.032)], T4, w=1.6)   # palm plate, concave
    poly(v, [(0.050, -0.030), (0.085, -0.036), (0.090, -0.050), (0.060, -0.054), (0.040, -0.040)], T3, w=1.8)               # thenar
    poly(v, [(-0.040, -0.012), (0.040, -0.012), (0.040, 0.014), (-0.040, 0.014)], GHOST, stroke=GHOST, w=1.4, op=0.2, dash="5,3")
    line(v, (0.004, 0.036), (0.004, 0.026), INK, 2.4)
    vtxt(v, 0.0, -0.085, "разрез x = 0,10 (взгляд вперёд): складка, две плоскости по 12°, фаски 40°", 12, THIN)

# ======================================= LAYOUT =====================================================================
out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
txt(30, 44, "«Панцирь» · мастер-эскиз для 3D (рев. M): грудной экран и капсула кисти «Ладонь» · каждая грань - своя плоскость", 23, INK, weight="600")
txt(30, 70, "тон грани = её наклон к свету (светлое - вверх/вперёд, тёмное - вбок/вниз) · тёмные линии - швы и загнутые губы · кружки - головки стоек · размеры в метрах", 14, THIN)
out.append('<rect x="5" y="85" width="%d" height="700" fill="none" stroke="%s"/>' % (W - 10, GRID))
txt(25, 115, "Грудной экран: 4 ряда внахлёст (C1 ключичный, C2 грудной, C3 рёберный, C4 брюшной)", 20, INK, weight="600")
chest_front(View(330, 1730, 1000)); txt(330, 775, "спереди", 13, THIN, "middle")
chest_side(View(900, 1730, 1000))
chest_top(View(1650, 440, 1000))
out.append('<rect x="5" y="795" width="%d" height="695" fill="none" stroke="%s"/>' % (W - 10, GRID))
txt(25, 825, "Капсула кисти «Ладонь»: тыльная плита из 4 граней + скос к костяшкам, бугор большого пальца, вогнутая ладонь", 20, INK, weight="600")
hand_top(View(130, 1110, 1700))
hand_side(View(830, 1080, 1700))
hand_front(View(1750, 1080, 1700))
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
open(os.path.join(HERE, "pantsir_chest_hand.svg"), "w", encoding="utf-8").write(svg)
print("ok")
