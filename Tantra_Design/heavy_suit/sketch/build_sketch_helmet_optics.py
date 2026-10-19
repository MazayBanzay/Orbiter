# «Панцирь» - helmet optics, three variants (2026-10-18). The old helmet read as a 20th-century box (gold plate + slit).
# Engineering basis: outside up to lava radiation -> little glass. Primary vision is SYNTHETIC (armoured sensor ports ->
# the helmet's projection display); a small multilayer direct-vision window is the backup for the eyes.
#   A «Щель и оптический пояс»     - low narrow slit with a sliding shutter; a brow band of sensor ports
#   B «Сенсорная маска»            - faceted face shield, the stereo pair where the eyes are, a periscopic backup window
#   C «Фотонное окно под шторкой»  - a wider amber photonic window behind retracting armoured louvres, sensors in the frame
# The opening kinematics V1 stays: the front/top shell swings 65° up-back on a hinge behind the crown; split chin ->
# cheekbones -> behind the ears -> nape, a seal ring along the split. Outer width 0.256 m = 1.6 x a 0.16 m head.
# Each row: front closed, side closed, front open (shutters / window), side open (V1, 65°), a port section and a
# window section in mm, pros/cons. Scale 1 m = 1000 px; origin of every view = the eyes. Rendered by headless Edge to PNG.
import os, math, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
S = 1000.0
W, H = 2300, 2660
INK, THIN, NOTE, ACC, GRID = "#1d2733", "#5d6b78", "#2f6f9f", "#c0692a", "#e2e6ea"
SHELL, SHELL_D, SHELL_L = "#d6d9dc", "#c1c6cb", "#e4e6e8"          # light grey enamel, its facets
GRA, GRA2, GRA3 = "#3a4048", "#262b31", "#59626c"                  # dark graphite enamel
RED, SKIN, HAIR, DARK = "#c22e22", "#e8c6a8", "#3a2e28", "#1f262e"
BZ, SAPH, PANE, VAC, WARM = "#cfd8de", "#d9e6ee", "#b9d0de", "#f6f8fa", "#fff1c4"
out = []
OX = OY = 0.0

def P(x, y): return OX + x * S, OY - y * S
def pts(l): return " ".join("%.1f,%.1f" % P(x, y) for x, y in l)
def dash_attr(d): return ' stroke-dasharray="%s"' % d if d else ""
def poly(l, fill="none", stroke=INK, w=1.6, op=1.0, dash=None):
    out.append('<polygon points="%s" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f" stroke-linejoin="round"%s/>' % (pts(l), fill, op, stroke, w, dash_attr(dash)))
def pline(l, stroke=THIN, w=1.2, dash=None, op=1.0):
    out.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%.1f" stroke-opacity="%.2f" stroke-linejoin="round"%s/>' % (pts(l), stroke, w, op, dash_attr(dash)))
def circ(c, r, fill, stroke=INK, w=1.0, op=1.0):
    x, y = P(*c); out.append('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f"/>' % (x, y, r * S, fill, op, stroke, w))
def ell(c, rx, ry, fill, stroke=INK, w=1.0, op=1.0, dash=None):
    x, y = P(*c); out.append('<ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="%s" fill-opacity="%.2f" stroke="%s" stroke-width="%.1f"%s/>' % (x, y, rx * S, ry * S, fill, op, stroke, w, dash_attr(dash)))
def rect(x0, y0, x1, y1, fill, stroke=INK, w=1.0, op=1.0, dash=None): poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], fill, stroke, w, op, dash)
def txt(x, y, s, size=14, color=NOTE, anchor="start", weight="normal", fam="Segoe UI, Arial"):
    out.append('<text x="%.1f" y="%.1f" font-family="%s" font-size="%g" fill="%s" text-anchor="%s" font-weight="%s">%s</text>' % (x, y, fam, size, color, anchor, weight, s))
def mtxt(x, y, s, size=9, color=GRA, anchor="middle"):            # stencilled marking on the shell (metres)
    px, py = P(x, y); txt(px, py, s, size, color, anchor, "bold", "Consolas, monospace")
def mir(l): return l + [(-x, y) for x, y in reversed(l)]
def flip(l): return [(-x, y) for x, y in l]
def hexagon(c, r): return [(c[0] + r * math.cos(math.radians(30 + 60 * k)), c[1] + r * math.sin(math.radians(30 + 60 * k))) for k in range(6)]

out.append('<defs>'
           '<linearGradient id="amb" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f3c95e"/><stop offset="0.42" stop-color="#cf8e2e"/>'
           '<stop offset="0.72" stop-color="#a0628a"/><stop offset="1" stop-color="#6a4c9c"/></linearGradient>'
           '<linearGradient id="ambv" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f3c95e"/><stop offset="0.5" stop-color="#cf8e2e"/>'
           '<stop offset="1" stop-color="#7a52a0"/></linearGradient>'
           '<pattern id="vac" width="6" height="6" patternUnits="userSpaceOnUse"><rect width="6" height="6" fill="#f6f8fa"/><circle cx="3" cy="3" r="0.8" fill="#9aa6b0"/></pattern>'
           '</defs>')

# ---------------------------------------------------------------- sensor / lamp symbols
def port(c, r, kind, opened, sq=1.0):
    """kind: st stereo, pe peripheral (angled, sq squashes x), th thermal, li lidar. Closed = armoured shutter / iris."""
    x, y = c
    if kind == "st":
        poly(hexagon(c, r * 1.55), GRA2, INK, 1.0)
        if opened:
            circ(c, r, "#0e141a", INK, 0.8); circ(c, r * 0.58, "#1f3a52", "none", 0)
            circ((x - r * 0.35, y + r * 0.35), r * 0.2, "#a9d8ff", "none", 0, 0.9)
        else:
            circ(c, r, GRA, INK, 0.8)
            for k in range(6):
                a = math.radians(60 * k + 15); pline([(x, y), (x + r * math.cos(a), y + r * math.sin(a))], GRA3, 1.0)
    elif kind == "pe":
        ell(c, r * 1.5 * sq, r * 1.5, GRA2, INK, 1.0)
        if opened:
            ell(c, r * sq, r, "#0e141a", INK, 0.8); ell(c, r * 0.55 * sq, r * 0.55, "#1f3a52", "none", 0)
            circ((x - r * 0.3 * sq, y + r * 0.35), r * 0.18, "#a9d8ff", "none", 0, 0.9)
        else:
            ell(c, r * sq, r, GRA, INK, 0.8); pline([(x, y - r), (x, y + r)], GRA3, 1.0)
    elif kind == "th":
        rect(x - r * 1.45, y - r * 1.45, x + r * 1.45, y + r * 1.45, GRA2, INK, 1.0)
        if opened:
            rect(x - r, y - r, x + r, y + r, "#2b1517", INK, 0.8); circ(c, r * 0.6, "#6b2a24", "none", 0)
            circ((x - r * 0.3, y + r * 0.3), r * 0.18, "#ffb0a0", "none", 0, 0.8)
        else:
            rect(x - r, y - r, x + r, y + r, GRA, INK, 0.8); pline([(x - r, y - r), (x + r, y + r)], GRA3, 1.0)
    elif kind == "li":
        rect(x - r * 2.0, y - r * 1.2, x + r * 2.0, y + r * 1.2, GRA2, INK, 1.0)
        if opened:
            rect(x - r * 1.6, y - r * 0.8, x - r * 0.1, y + r * 0.8, "#16302a", INK, 0.7); circ((x - r * 0.85, y), r * 0.3, "#3f7a62", "none", 0)
            circ((x + r * 0.85, y), r * 0.8, "#0e141a", INK, 0.7); circ((x + r * 0.75, y + r * 0.25), r * 0.18, "#a9d8ff", "none", 0, 0.9)
        else:
            rect(x - r * 1.6, y - r * 0.8, x + r * 1.6, y + r * 0.8, GRA, INK, 0.7); pline([(x, y - r * 0.8), (x, y + r * 0.8)], GRA3, 1.0)

def lamp(c, w, h, opened):
    x, y = c
    rect(x - w / 2 - 0.003, y - h / 2 - 0.003, x + w / 2 + 0.003, y + h / 2 + 0.003, GRA2, INK, 1.0)     # the recess
    if opened:
        rect(x - w / 2, y - h / 2, x + w / 2, y + h / 2, WARM, "#8f7a40", 0.8)
        pline([(x - w / 2 + 0.002, y + h / 2 - 0.002), (x + w / 2 - 0.002, y + h / 2 - 0.002)], "#ffffff", 1.2)
    else:
        rect(x - w / 2, y - h / 2, x + w / 2, y + h / 2, GRA, INK, 0.8)
        for k in (1, 2): pline([(x - w / 2, y - h / 2 + h * k / 3), (x + w / 2, y - h / 2 + h * k / 3)], GRA3, 0.9)

def amber(l, eyes=None):
    """the photonic direct-vision window: amber with a gold -> violet sheen; the eyes faintly behind it"""
    poly(l, "url(#amb)", INK, 1.2)
    if eyes:
        for e in eyes: ell(e, 0.010, 0.0045, "#2a1d2e", "none", 0, 0.55)
    ys = [p[1] for p in l]; xs = [p[0] for p in l]
    y0, y1, x0, x1 = min(ys), max(ys), min(xs), max(xs)
    pline([(x0 + (x1 - x0) * 0.12, y1 - (y1 - y0) * 0.25), (x0 + (x1 - x0) * 0.42, y1 - (y1 - y0) * 0.25)], "#fff6dc", 1.6, op=0.8)

# ---------------------------------------------------------------- common shell (front view, side view)
FRONT = mir([(0.0, 0.158), (0.07, 0.152), (0.110, 0.120), (0.128, 0.055), (0.128, -0.035), (0.117, -0.105), (0.092, -0.150), (0.0, -0.160)])
F_SIDEFACET = [(0.110, 0.120), (0.128, 0.055), (0.128, -0.035), (0.117, -0.105), (0.104, -0.098), (0.112, -0.03), (0.112, 0.05), (0.098, 0.108)]
F_CROWN = [(-0.07, 0.152), (0.07, 0.152), (0.110, 0.120), (0.098, 0.108), (-0.098, 0.108), (-0.110, 0.120)]
F_CHIN = [(-0.092, -0.150), (0.092, -0.150), (0.117, -0.105), (0.104, -0.098), (-0.104, -0.098), (-0.117, -0.105)]
SIDE = [(0.100, -0.158), (0.135, -0.110), (0.148, -0.020), (0.145, 0.060), (0.120, 0.120), (0.060, 0.155), (-0.040, 0.158),
        (-0.110, 0.132), (-0.145, 0.072), (-0.152, -0.030), (-0.132, -0.120), (-0.100, -0.158)]
HINGE = (-0.112, 0.122)
SPLIT = [(0.100, -0.158), (0.040, -0.060), (-0.050, 0.020), (-0.105, 0.098), HINGE]
MOVE = SIDE[:8] + list(reversed(SPLIT))[:-1]
FIXED = [HINGE, (-0.145, 0.072), (-0.152, -0.030), (-0.132, -0.120), (-0.100, -0.158)] + SPLIT[:-1]
HEAD_S = [(0.045, -0.105), (0.07, -0.08), (0.078, -0.06), (0.072, -0.05), (0.085, -0.033), (0.098, -0.015), (0.082, -0.005), (0.08, 0.03),
          (0.072, 0.07), (0.035, 0.105), (-0.035, 0.11), (-0.085, 0.07), (-0.10, 0.0), (-0.08, -0.06), (-0.04, -0.085), (-0.02, -0.135), (0.02, -0.135)]

def collar_front():
    poly([(-0.118, -0.185), (0.118, -0.185), (0.150, -0.215), (0.158, -0.255), (-0.158, -0.255), (-0.150, -0.215)], SHELL, INK, 1.6)
    poly([(-0.112, -0.156), (0.112, -0.156), (0.118, -0.185), (-0.118, -0.185)], GRA, INK, 1.4)          # rigid collar ring of the torso
    pline([(-0.115, -0.170), (0.115, -0.170)], RED, 1.6)
    mtxt(0.105, -0.235, "ПАНЦИРЬ-2", 9, RED)
def collar_side():
    poly([(-0.125, -0.185), (0.115, -0.185), (0.135, -0.215), (0.14, -0.255), (-0.14, -0.255), (-0.135, -0.215)], SHELL, INK, 1.6)
    poly([(-0.120, -0.156), (0.110, -0.156), (0.115, -0.185), (-0.125, -0.185)], GRA, INK, 1.4)
    pline([(-0.122, -0.170), (0.112, -0.170)], RED, 1.6)

def shell_front():
    collar_front()
    poly(FRONT, SHELL, INK, 2.0)
    poly(F_CROWN, SHELL_L, INK, 1.0); poly(F_CHIN, SHELL_D, INK, 1.0)
    for s in (1, -1):
        poly(F_SIDEFACET if s > 0 else flip(F_SIDEFACET), SHELL_D, INK, 1.0)
        l = [(0.06, -0.150), (0.104, -0.098), (0.112, -0.03), (0.121, 0.02)]                            # the split seam up the cheek
        pline(l if s > 0 else flip(l), ACC, 1.4)
    pline([(0, 0.152), (0, 0.108)], THIN, 1.0)
    rect(0.030, 0.124, 0.062, 0.132, RED, INK, 0.6)                                                       # red index tag
    mtxt(0.0, -0.136, "П-2", 9, GRA)

def shell_side_closed():
    collar_side()
    poly(SIDE, SHELL, INK, 2.0)
    pline([(0.120, 0.120), (0.095, 0.104), (-0.03, 0.130), (-0.10, 0.118)], THIN, 1.0)                    # crown chamfer
    pline([(0.135, -0.110), (0.115, -0.10), (0.06, -0.135)], THIN, 1.0)                                   # jaw facet
    pline(SPLIT, ACC, 1.4)
    rect(-0.140, -0.075, -0.128, -0.040, RED, INK, 0.6); mtxt(-0.095, -0.105, "ШЛ-07", 8, GRA)
    circ(HINGE, 0.009, SHELL_D, INK, 1.2)
    pline(HEAD_S + HEAD_S[:1], THIN, 1.0, "4,3", 0.7)                                                     # the head inside, for scale

def shell_side_open(feat):
    collar_side()
    poly(HEAD_S, SKIN, INK, 1.1)                                                                          # the head: only the part the
    poly([(0.072, 0.07), (0.035, 0.105), (-0.035, 0.11), (-0.085, 0.07), (-0.10, 0.0), (-0.07, 0.035), (0.0, 0.07), (0.05, 0.06)], HAIR, INK, 0.8)
    ell((0.062, 0.0), 0.007, 0.004, "#4b5966", INK, 0.6)                                                  # fixed back wall leaves open
    poly(FIXED, SHELL, INK, 2.0)
    pline(SPLIT, ACC, 3.4)                                                                                # the seal ring, edge-on
    rect(-0.140, -0.075, -0.128, -0.040, RED, INK, 0.6); mtxt(-0.095, -0.105, "ШЛ-07", 8, GRA)
    hx, hy = P(*HINGE)
    # the arc of the brow point (closed -> 65°)
    a0 = math.atan2(-0.020 - HINGE[1], 0.148 - HINGE[0]); r = math.hypot(0.148 - HINGE[0], -0.020 - HINGE[1])
    a1 = a0 + math.radians(65)
    x0, y0 = P(HINGE[0] + r * math.cos(a0), HINGE[1] + r * math.sin(a0)); x1, y1 = P(HINGE[0] + r * math.cos(a1), HINGE[1] + r * math.sin(a1))
    out.append('<path d="M %.1f %.1f A %.1f %.1f 0 0 0 %.1f %.1f" fill="none" stroke="%s" stroke-width="1.4" stroke-dasharray="5,4"/>' % (x0, y0, r * S, r * S, x1, y1, ACC))
    am = (a0 + a1) / 2; txt(*P(HINGE[0] + (r + 0.018) * math.cos(am), HINGE[1] + (r + 0.018) * math.sin(am)), s="65°", size=14, color=ACC, weight="600")
    out.append('<g transform="rotate(-65 %.1f %.1f)">' % (hx, hy))
    poly(MOVE, SHELL, INK, 2.0)
    pline([(0.120, 0.120), (0.095, 0.104), (-0.03, 0.130), (-0.10, 0.118)], THIN, 1.0)
    pline(SPLIT[:-1] + [HINGE], ACC, 3.4)                                                                 # its half of the seal
    feat(True)
    out.append('</g>')
    circ(HINGE, 0.010, SHELL_D, INK, 1.4); circ(HINGE, 0.004, GRA, INK, 0.8)

# ---------------------------------------------------------------- variant A «Щель и оптический пояс»
def A_front(opened):
    shell_front()
    poly(mir([(0, 0.082), (0.10, 0.080), (0.122, 0.072), (0.128, 0.055), (0.128, 0.035), (0.110, 0.030), (0, 0.030)]), GRA, INK, 1.2)   # brow band
    pline([(-0.11, 0.025), (0.11, 0.025)], RED, 1.4)
    poly(mir([(0, 0.020), (0.084, 0.020), (0.094, -0.022), (0.074, -0.094), (0, -0.108)]), SHELL_L, INK, 1.0)                      # face plate
    pline([(0, -0.03), (0, -0.108)], THIN, 0.9)
    for s in (1, -1): pline([(s * 0.084, 0.020), (s * 0.06, -0.03), (s * 0.074, -0.094)], THIN, 0.9)
    for s in (1, -1): port((s * 0.0325, 0.056), 0.0085, "st", opened); port((s * 0.114, 0.052), 0.007, "pe", opened, 0.6)
    port((-0.072, 0.056), 0.0075, "th", opened); port((0.072, 0.056), 0.0062, "li", opened)
    rect(-0.060, -0.016, 0.060, 0.012, GRA2, INK, 1.2)                                                     # the slit recess
    if opened:
        amber([(-0.055, -0.012), (0.055, -0.012), (0.055, 0.008), (-0.055, 0.008)], [(-0.0325, -0.002), (0.0325, -0.002)])
        rect(-0.055, -0.052, 0.055, -0.024, "none", THIN, 1.0, dash="4,3")                                 # shutter, down in the chin (hidden)
    else:
        rect(-0.055, -0.012, 0.055, 0.008, GRA, INK, 1.0); pline([(-0.055, -0.002), (0.055, -0.002)], GRA3, 1.0)
        rect(0.044, -0.010, 0.052, 0.006, RED, "none", 0)
    for s in (1, -1): lamp((s * 0.088, -0.002), 0.016, 0.010, opened)

def A_side(opened):
    poly([(0.1465, 0.030), (0.1455, 0.080), (0.06, 0.088), (0.0, 0.082), (0.0, 0.030), (0.06, 0.026)], GRA, INK, 1.2)
    pline([(0.146, 0.024), (0.04, 0.020)], RED, 1.4)
    port((0.088, 0.055), 0.008, "pe", opened, 0.75)
    port((0.030, 0.056), 0.006, "th", opened)
    rect(0.138, -0.016, 0.149, 0.012, GRA2, INK, 1.0)
    if opened: rect(0.143, -0.012, 0.150, 0.008, "url(#ambv)", INK, 0.8)
    else: rect(0.143, -0.012, 0.150, 0.008, GRA, INK, 0.8)
    pline([(0.140, -0.018), (0.132, -0.075)], GRA3, 1.2)                                                  # shutter guide

# ---------------------------------------------------------------- variant B «Сенсорная маска»
MASK = mir([(0, 0.088), (0.072, 0.085), (0.104, 0.045), (0.108, -0.025), (0.082, -0.092), (0.035, -0.122), (0, -0.126)])
def B_front(opened):
    shell_front()
    rect(-0.048, 0.086, 0.048, 0.106, GRA2, INK, 1.2)                                                      # periscope hood
    poly(MASK, GRA, INK, 1.6)
    for s in (1, -1):                                                                                     # mask facets
        pline([(s * 0.072, 0.085), (s * 0.052, 0.030), (s * 0.075, -0.030), (s * 0.082, -0.092)], GRA3, 1.0)
        pline([(s * 0.052, 0.030), (s * 0.016, 0.022), (s * 0.010, -0.040), (s * 0.035, -0.122)], GRA3, 1.0)
        pline([(s * 0.104, 0.045), (s * 0.052, 0.030)], GRA3, 1.0)
    pline([(-0.016, 0.022), (0.016, 0.022)], GRA3, 1.0)
    pline([(-0.06, -0.103), (0.06, -0.103)], RED, 1.4)
    if opened: amber([(-0.036, 0.090), (0.036, 0.090), (0.036, 0.101), (-0.036, 0.101)])
    else: rect(-0.036, 0.090, 0.036, 0.101, GRA, INK, 0.8); rect(0.026, 0.092, 0.033, 0.099, RED, "none", 0)
    for s in (1, -1):
        port((s * 0.0325, 0.0), 0.011, "st", opened)
        port((s * 0.093, 0.010), 0.0075, "pe", opened, 0.6)
        lamp((s * 0.118, 0.012), 0.006, 0.030, opened)
    port((-0.045, -0.062), 0.008, "th", opened); port((0.045, -0.062), 0.0065, "li", opened)

def B_side(opened):
    poly([(0.128, -0.125), (0.137, -0.10), (0.148, -0.02), (0.145, 0.06), (0.136, 0.086), (0.098, 0.086), (0.106, 0.02), (0.100, -0.06), (0.086, -0.120)], GRA, INK, 1.4)
    poly([(0.126, 0.086), (0.154, 0.089), (0.155, 0.106), (0.122, 0.108)], GRA2, INK, 1.2)                 # periscope hood
    if opened: rect(0.151, 0.091, 0.156, 0.102, "url(#ambv)", INK, 0.8)
    pline([(0.140, -0.102), (0.098, -0.104)], RED, 1.4)
    port((0.122, 0.012), 0.0075, "pe", opened, 0.7)
    pline([(0.156, 0.096), (0.108, 0.096), (0.108, 0.026), (0.068, 0.006)], "#c98a2e", 1.4, "3,2")          # periscope light path

# ---------------------------------------------------------------- variant C «Фотонное окно под шторкой»
CFRAME = mir([(0, 0.068), (0.100, 0.066), (0.118, 0.036), (0.118, -0.046), (0.100, -0.068), (0, -0.070)])
CWIN = mir([(0, 0.036), (0.068, 0.036), (0.088, 0.020), (0.088, -0.022), (0.068, -0.032), (0, -0.032)])
def C_front(opened):
    shell_front()
    poly(CFRAME, GRA, INK, 1.6)
    pline([(-0.10, 0.073), (0.10, 0.073)], RED, 1.4)
    if opened:
        amber(CWIN, [(-0.0325, 0.0), (0.0325, 0.0)])
        for s in (1, -1): pline([(s * 0.068, 0.036), (s * 0.068, -0.032)], "#fff0d0", 1.0, op=0.7)
        rect(-0.086, 0.030, 0.086, 0.036, GRA2, INK, 0.8)                                                  # louvres stacked up in the brow bar
    else:
        poly(CWIN, GRA2, INK, 1.2)
        n = 6
        for k in range(n):
            y1 = 0.036 - k * 0.068 / n; y0 = y1 - 0.068 / n
            rect(-0.088, y0 + 0.0012, 0.088, y1, GRA, INK, 0.7)
            pline([(-0.086, y1 - 0.0016), (0.086, y1 - 0.0016)], GRA3, 1.0)
        rect(0.074, -0.030, 0.084, -0.022, RED, "none", 0)
    for s in (1, -1):
        port((s * 0.0325, 0.051), 0.0075, "st", True)
        port((s * 0.104, -0.004), 0.007, "pe", True, 0.6)
        lamp((s * 0.086, -0.051), 0.018, 0.009, opened)
    port((-0.040, -0.051), 0.0072, "th", True); port((0.040, -0.051), 0.006, "li", True)

def C_side(opened):
    poly([(0.148, -0.070), (0.157, -0.062), (0.158, 0.062), (0.148, 0.070), (0.116, 0.068), (0.116, -0.068)], GRA, INK, 1.4)
    pline([(0.147, 0.076), (0.10, 0.074)], RED, 1.4)
    if opened:
        rect(0.152, -0.032, 0.158, 0.036, "url(#ambv)", INK, 0.8); rect(0.150, 0.030, 0.160, 0.036, GRA2, INK, 0.7)
    else:
        for k in range(6):
            y1 = 0.036 - k * 0.068 / 6
            poly([(0.151, y1 - 0.0105), (0.161, y1 - 0.0085), (0.161, y1), (0.151, y1 - 0.002)], GRA2, INK, 0.7)
    port((0.134, -0.004), 0.007, "pe", True, 0.7)

# ---------------------------------------------------------------- sections (pixel space, K px per mm)
K = 7.0
WALL = [("эмаль + керамика ZrB₂–SiC", 5, "#c9ccd0"), ("экранно-вакуумная изоляция + аэрогель", 18, "#efe4c2"),
        ("экран W–B₄C (гамма, нейтроны)", 5, "#8a929b"), ("гермооболочка Ti-композит", 2, "#a9b9c4")]
def marker(x, y, n):
    out.append('<circle cx="%.1f" cy="%.1f" r="9" fill="#ffffff" stroke="%s" stroke-width="1.2"/>' % (x, y, INK))
    txt(x, y + 4.5, str(n), 11.5, INK, "middle", "600")
def prect(x, y, w, h, fill, stroke=INK, sw=1.0, extra=""):
    out.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s" stroke="%s" stroke-width="%.1f"%s/>' % (x, y, w, h, fill, stroke, sw, extra))
def pl(l, stroke, w=1.4, dash=None, marker_end=False):
    out.append('<polyline points="%s" fill="none" stroke="%s" stroke-width="%.1f"%s%s/>' % (" ".join("%.1f,%.1f" % p for p in l), stroke, w, dash_attr(dash),
               ' marker-end="url(#arr%s)"' % stroke.strip("#") if marker_end else ""))
ARROWS = set()
def arrow_def(c):
    if c in ARROWS: return
    ARROWS.add(c)
    out.append('<defs><marker id="arr%s" markerWidth="8" markerHeight="8" refX="6" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="%s"/></marker></defs>' % (c.strip("#"), c))

def section(x0, y0, title, oh, items, notes, kind):
    """a wall section: outside on the left. items: (n, mm_from_outer_face, mm_thick, fill, label). kind: port | window"""
    txt(x0, y0 + 18, title, 15, INK, weight="600")
    xw, yt, hh = x0 + 115, y0 + 40, 250
    yc = yt + hh / 2
    x = xw
    for name, mm, fill in WALL:
        prect(x, yt, mm * K, yc - oh - yt, fill); prect(x, yc + oh, mm * K, yt + hh - yc - oh, fill)
        x += mm * K
    prect(xw, yc - oh, 30 * K, 2 * oh, VAC, "none", 0)
    pl([(xw, yc - oh), (xw + 30 * K, yc - oh)], INK, 1.0); pl([(xw, yc + oh), (xw + 30 * K, yc + oh)], INK, 1.0)
    pl([(xw, yt + hh + 14), (xw + 30 * K, yt + hh + 14)], THIN, 1.0)
    for xx in (xw, xw + 30 * K): pl([(xx, yt + hh + 8), (xx, yt + hh + 20)], THIN, 1.0)
    txt(xw + 15 * K, yt + hh + 32, "стенка 30 мм", 12, THIN, "middle")
    txt(xw - 8, yt - 8, "снаружи", 12, THIN, "end"); txt(xw + 30 * K + 8, yt - 8, "внутри", 12, THIN)
    arrow_def("#d0a020"); arrow_def(RED)
    for n, at, th, fill, lab in items:
        px = xw + at * K
        if fill == "lens":
            out.append('<ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" fill="%s" stroke="%s" stroke-width="1"/>' % (px, yc, th * K / 2, oh * 0.75, "#c8dbe8", INK))
        elif fill == "sensor":
            prect(px, yc - oh * 0.6, th * K, oh * 1.2, "#2a3440"); prect(px + th * K, yc - oh * 0.75, 1.2 * K, oh * 1.5, "#8a929b")
        elif fill == "slats":
            for k in range(5):
                yy = yc - oh + k * 2 * oh / 5
                out.append('<polygon points="%.1f,%.1f %.1f,%.1f %.1f,%.1f %.1f,%.1f" fill="%s" stroke="%s" stroke-width="0.8"/>' %
                           (px, yy + 2, px + th * K, yy - 4, px + th * K, yy + 2 * oh / 5 - 6, px, yy + 2 * oh / 5, GRA, INK))
        elif fill == "shutter":
            prect(px, yc - oh - 6, th * K, 2 * oh + 12, GRA)
        else:
            prect(px, yc - oh, th * K, 2 * oh, fill)
        marker(px + max(th, 1.5) * K / 2, yc - oh - 22 - (8 if fill == "shutter" else 0), n)
    if kind == "window":                                                                           # seals at the pane edges
        for yy in (yc - oh - 4, yc + oh - 4):
            prect(xw + 0.5 * K, yy, 14 * K, 8, "#3a3a3a", "none", 0)
        # visible light passes, IR goes back from the Bragg mirror
        pl([(xw - 105, yc - 18), (xw + 30 * K + 28, yc - 18)], "#d0a020", 2.0, marker_end=True)
        pl([(xw - 105, yc + 22), (xw + 5.0 * K, yc + 22), (xw - 95, yc + 62)], RED, 2.0, marker_end=True)
        txt(xw - 108, yc - 26, "видимый", 12, "#a07a10"); txt(xw - 108, yc + 84, "ИК назад", 12, RED)
    else:
        pl([(xw - 105, yc), (xw + 22 * K, yc)], "#d0a020", 1.6, "5,3", marker_end=True)
    # notes on the right
    nx = x0 + 420
    for k, s in enumerate(notes):
        if s.startswith("#"):
            continue
        txt(nx, y0 + 52 + k * 21, s, 13, INK if s[:1].isdigit() else THIN)
    yy = y0 + 52 + len(notes) * 21 + 4
    for name, mm, fill in WALL:
        prect(nx, yy - 10, 14, 12, fill); txt(nx + 20, yy, "%s · %d мм" % (name, mm), 12, THIN); yy += 18

def periscope(x0, y0, title):
    """B: the periscope window - outer pane high under the hood, a cold mirror, down, a mirror, the inner pane, the eye"""
    txt(x0, y0 + 18, title, 15, INK, weight="600")
    arrow_def("#d0a020"); arrow_def(RED)
    bx, by, bw, bh = x0 + 115, y0 + 40, 230, 250
    prect(bx, by, bw, bh, "#c9ccd0")                                                                  # the mask block (schematic)
    cy1, cy2, ch = by + 55, by + 190, 40                                                              # upper and lower channel
    mx = bx + 120
    prect(bx, cy1 - ch / 2, mx - bx + 20, ch, VAC, "none", 0)                                          # upper channel
    prect(mx - 20, cy1 - ch / 2, 40, cy2 - cy1 + ch, VAC, "none", 0)                                   # vertical channel
    prect(mx - 20, cy2 - ch / 2, bx + bw - mx + 20, ch, VAC, "none", 0)                                # lower channel
    prect(mx + 30, cy1 - 40, 70, 80, "#8a929b"); txt(mx + 65, cy1 + 4, "W", 16, "#ffffff", "middle", "700")
    prect(bx - 3 * K, cy1 - ch / 2 - 6, 3 * K, ch + 12, GRA); marker(bx - 1.5 * K, cy1 - ch / 2 - 22, 1)
    prect(bx, cy1 - ch / 2, 4 * K, ch, BZ); marker(bx + 2 * K, cy1 + ch / 2 + 20, 2)
    prect(bx + 4 * K, cy1 - ch / 2, 2 * K, ch, "url(#ambv)"); marker(bx + 5 * K + 12, cy1 + ch / 2 + 20, 3)
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#6aa0c0" stroke-width="6"/>' % (mx - 20, cy1 - ch / 2, mx + 20, cy1 + ch / 2))
    marker(mx + 2, cy1 - ch / 2 - 22, 4)
    out.append('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="#9aa6b0" stroke-width="5"/>' % (mx - 20, cy2 - ch / 2, mx + 20, cy2 + ch / 2))
    marker(mx - 34, cy2 + 4, 5)
    prect(bx + bw - 6 * K, cy2 - ch / 2, 6 * K, ch, PANE); marker(bx + bw - 3 * K, cy2 + ch / 2 + 20, 6)
    marker(mx + 65, cy1 + 58, 7)
    # the eye
    ex, ey = bx + bw + 40, cy2
    out.append('<path d="M %.1f %.1f Q %.1f %.1f %.1f %.1f Q %.1f %.1f %.1f %.1f z" fill="#ffffff" stroke="%s" stroke-width="1.2"/>' %
               (ex - 18, ey, ex, ey - 14, ex + 18, ey, ex, ey + 14, ex - 18, ey, INK))
    out.append('<circle cx="%.1f" cy="%.1f" r="6" fill="#4b5966"/>' % (ex - 6, ey))
    pl([(bx - 105, cy1 - 6), (mx - 6, cy1 - 6), (mx - 6, cy2 - 6), (ex - 26, cy2 - 6)], "#d0a020", 2.0, marker_end=True)
    pl([(bx - 105, cy1 + 10), (mx + 28, cy1 + 10)], RED, 2.0, marker_end=True)
    txt(bx - 108, cy1 - 16, "видимый", 12, "#a07a10"); txt(bx - 108, cy1 + 30, "ИК, гамма", 12, RED)
    txt(bx + 4, by + bh + 22, "прямой линии глаз — наружу нет (излом 90° дважды)", 12, THIN)
    nx = x0 + 420
    for k, s in enumerate(["1  бронешторка, сдвиг вверх · 3 мм", "2  окно: кубический нитрид бора · 4 мм",
                           "3  брэгговский ИК-отражатель · 2 мм", "4  «холодное» фотонное зеркало · 3 мм:", "    видимый отражает, ИК пропускает в поглотитель",
                           "5  зеркало (диэлектрик) · 2 мм", "6  внутреннее гермостекло, сапфир · 6 мм", "7  поглотитель W + отвод тепла в контур",
                           "апертура 72 × 11 мм · стекла 8 см²"]):
        txt(nx, y0 + 52 + k * 21, s, 13, INK if s[:1].isdigit() else THIN)

# ---------------------------------------------------------------- the sheet
VARS = [
    ("A  «Щель и оптический пояс»", A_front, A_side,
     [("шлем закрыт: задвижка и ирисы", ""), ("сбоку · закрыт", ""), ("щель и порты открыты", ""), ("сбоку · открыт (V1, 65°)", "")],
     ["+ лицо читается: глаза за щелью, «бровь» датчиков над ними; стереопара 65 мм в поясе, тепловизор и лидар в нём же, периферия на висках",
      "+ стекла мало: щель 110 × 20 мм (22 см²) — притока тепла и дозы почти нет; задвижка уходит вниз в подбородок",
      "− прямой обзор узкий: ~95° × 22°, под ноги — только камерами; пояс датчиков один — удар в лоб выводит всё синтетическое зрение",
      "− отказ электроники: остаётся щель (ручной привод задвижки) — ходить можно, работать руками трудно"],
     ("Порт стереокамеры · разрез", 26,
      [(1, -3, 3, "shutter", ""), (2, 0, 3, BZ, ""), (3, 3, 1.5, "url(#ambv)", ""), (4, 11, 3, "lens", ""), (5, 21, 2.5, "sensor", "")],
      ["1  ирис-шторка, 6 лепестков W–Re · 3 мм", "2  окно: кубический нитрид бора (боразон) · 3 мм", "3  фотонный фильтр: режет ИК > 0,75 мкм и УФ · 1,5 мм",
       "4  объектив в вакуумной полости", "5  матрица на термостабильной плате", "окно Ø 12 мм · 1,1 см²"], "port"),
     ("Щель прямого обзора · разрез", 42,
      [(1, -6, 6, "shutter", ""), (2, 0, 4, BZ, ""), (3, 4, 2, "url(#ambv)", ""), (4, 6, 6, "url(#vac)", ""), (5, 12, 6, PANE, "")],
      ["1  задвижка W-сплав, ход вниз · 6 мм", "2  боразон (cBN) · 4 мм", "3  брэгговский ИК-отражатель на сапфире · 2 мм",
       "    (янтарь, отлив золото → фиолет)", "4  вакуумный зазор · 6 мм", "5  внутреннее гермостекло, сапфир-ламинат · 6 мм", "пакет 18 мм · щель 110 × 20 мм"], "window")),
    ("B  «Сенсорная маска»", B_front, B_side,
     [("шлем закрыт: шторки портов и перископа", ""), ("сбоку · закрыт (перископ пунктиром)", ""), ("порты и перископ открыты", ""), ("сбоку · открыт (V1, 65°)", "")],
     ["+ стереопара ровно на месте глаз (65 мм): синтетическое зрение без параллакса, голова «смотрит» привычно; тепловизор и лидар на скулах",
      "+ лучшая защита: перископ без прямой линии — гамма и нейтроны упираются в вольфрам, ИК уходит в поглотитель; стекла 8 см²",
      "− резервный обзор самый узкий: ~40° × 15° через перископ, обзор вниз — только камерами",
      "− полная зависимость от дисплея; маска ближе всех к «машинному лицу» — держать человеческие пропорции, без лишних «глаз»"],
     ("Порт тепловизора 3–5 мкм · разрез", 26,
      [(1, -3, 3, "shutter", ""), (2, 0, 3, SAPH, ""), (3, 3, 1.5, "#8fb0b8", ""), (4, 11, 3, "lens", ""), (5, 21, 2.5, "sensor", "")],
      ["1  шторка-заслонка, сдвиг вбок · 3 мм", "2  окно: сапфир (прозрачен до 5 мкм) · 3 мм", "3  фотонный фильтр 3–5 мкм, видимый отсекает · 1,5 мм",
       "4  объектив (германий) в вакуумной полости", "5  матрица, термоэлектрическое охлаждение", "окно 14 × 14 мм · 2 см²"], "port"),
     ("Перископ резервного обзора · схема", 0, None, None, "periscope")),
    ("C  «Фотонное окно под шторкой»", C_front, C_side,
     [("жалюзи закрыты, датчики рамки работают", ""), ("сбоку · закрыт", ""), ("жалюзи убраны вверх, окно открыто", ""), ("сбоку · открыт (V1, 65°)", "")],
     ["+ лучший прямой обзор: окно 176 × 68 мм, ~120° × 55° — можно работать руками без камер; лицо видно, вид самый «человеческий»",
      "+ жалюзи — и броня, и тень; все датчики в рамке и работают при закрытых жалюзи; брэгг-слой отражает ИК даже при открытом окне",
      "− стекла в 5 раз больше (110 см²): у лавы и в буре — только закрытым; доза через окно заметна, в радиационном поле закрывать",
      "− отказ: механика 6 ламелей боится пыли и удара — ручной привод, ламели убираются вверх даже при заклинивании одной"],
     ("Порт лидара 1,55 мкм · разрез", 26,
      [(1, -3, 3, "shutter", ""), (2, 0, 3, BZ, ""), (3, 3, 1.5, "#9ab89a", ""), (4, 11, 3, "lens", ""), (5, 21, 2.5, "sensor", "")],
      ["1  бронешторка (часть рамки жалюзи) · 3 мм", "2  окно: боразон · 3 мм", "3  узкополосный фильтр 1,55 мкм · 1,5 мм",
       "4  оптика излучателя и приёмника", "5  модуль времени пролёта (ToF)", "окна 2 × Ø 8 мм"], "port"),
     ("Фотонное окно · разрез", 60,
      [(1, -8, 8, "slats", ""), (2, 0, 5, BZ, ""), (3, 5, 3, "url(#ambv)", ""), (4, 8, 8, "url(#vac)", ""), (5, 16, 10, PANE, "")],
      ["1  жалюзи: 6 ламелей W–ZrB₂ · 8 мм, уходят вверх", "2  боразон (cBN) · 5 мм", "3  брэгговский ИК-отражатель на сапфире · 3 мм",
       "    (янтарь, отлив золото → фиолет)", "4  вакуумный зазор · 8 мм", "5  внутреннее гермостекло, сапфир-ламинат · 10 мм", "пакет 26 мм · окно 176 × 68 мм"], "window")),
]

out.append('<rect x="0" y="0" width="%d" height="%d" fill="#fbfbf8"/>' % (W, H))
out.insert(0, out.pop())   # background first
txt(30, 44, "«Панцирь» · шлем: оптика и окно — три варианта (раскрытие V1 сохранено: петля за макушкой, 65°)", 26, INK, weight="600")
txt(30, 72, "основное зрение синтетическое: бронированные порты → проекционный дисплей шлема · резервное — малое многослойное окно для глаз · "
            "ширина 0,256 м = 1,6 головы · стенка 30 мм · голова поворачивается внутри жёсткого шлема", 15, THIN)
txt(30, 96, "разъём: подбородок → скулы → за ушами → затылок, кольцо уплотнения по разъёму (оранжевое) · эмаль светло-серая / графит, красные метки, "
            "без болтов и заклёпок, без шлангов", 15, THIN)
# legend
OX, OY = 40, 0
lx = 40
for kind, lab in (("st", "стереопара 65 мм"), ("pe", "периферийная камера"), ("th", "тепловизор"), ("li", "лидар (ToF)"), ("lamp", "фара (утоплена)"),
                  ("win", "фотонное окно"), ("shut", "шторка / ирис закрыты"), ("seal", "разъём + уплотнение")):
    OX, OY = lx + 14, 130 - 0.0 * S
    OY = 130 + 0.0
    # metres -> pixels with origin at the symbol
    if kind in ("st", "pe", "th", "li"): port((0, 0), 0.008, kind, True, 0.6 if kind == "pe" else 1.0)
    elif kind == "lamp": lamp((0, 0), 0.018, 0.010, True)
    elif kind == "win": amber([(-0.016, -0.007), (0.016, -0.007), (0.016, 0.007), (-0.016, 0.007)])
    elif kind == "shut": port((0, 0), 0.008, "st", False)
    else: pline([(-0.016, 0), (0.016, 0)], ACC, 3.4)
    txt(lx + 40, 135, lab, 14, INK)
    lx += 40 + len(lab) * 8 + 40

for i, (title, ffront, fside, labels, caps, port_sec, win_sec) in enumerate(VARS):
    y0 = 165 + i * 828
    out.append('<rect x="5" y="%d" width="%d" height="815" fill="none" stroke="%s"/>' % (y0, W - 10, GRID))
    txt(25, y0 + 34, title, 24, INK, weight="600")
    oy = y0 + 405
    for j, ox in enumerate((195, 535, 875, 1215)):
        OX, OY = ox, oy
        if j == 0: ffront(False)
        elif j == 1: shell_side_closed(); fside(False)
        elif j == 2: ffront(True)
        else: shell_side_open(fside)
        txt(ox, oy + 285, labels[j][0], 13, THIN, "middle")
    if i == 0:                                                                                    # the width dimension, once
        OX, OY = 195, oy
        pline([(-0.128, 0.192), (0.128, 0.192)], THIN, 1.0)
        for s in (1, -1): pline([(s * 0.128, 0.182), (s * 0.128, 0.202)], THIN, 1.0)
        txt(195, oy - 0.200 * S, "0,256 м = 1,6 × голова", 13, THIN, "middle")
    for k, s in enumerate(caps):
        txt(25, y0 + 722 + k * 22, s, 15, NOTE if s.startswith("+") else "#9a3d2a")
    out.append('<line x1="1475" y1="%d" x2="1475" y2="%d" stroke="%s"/>' % (y0 + 10, y0 + 805, GRID))
    t, oh, items, notes, kind = port_sec
    section(1490, y0 + 45, t, oh, items, notes, kind)
    t, oh, items, notes, kind = win_sec
    if kind == "periscope": periscope(1490, y0 + 420, t)
    else: section(1490, y0 + 420, t, oh, items, notes, kind)

svg = '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (W, H, W, H, "\n".join(out))
svg_path = os.path.join(HERE, "pantsir_helmet_optics.svg"); png_path = os.path.join(HERE, "pantsir_helmet_optics.png")
open(svg_path, "w", encoding="utf-8").write(svg)
print("svg ok")
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
prof = os.path.join(os.environ.get("TEMP", HERE), "edge_sketch_profile")
if os.path.exists(EDGE):
    subprocess.run([EDGE, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--user-data-dir=" + prof, "--window-size=%d,%d" % (W, H),
                    "--screenshot=" + png_path, "file:///" + svg_path.replace("\\", "/")], check=False, timeout=120)
    print("png", os.path.exists(png_path))
