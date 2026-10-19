"""The lava field itself: elevation, colour, age, temperature and crust for any set of local points.

Local frame: x east, y north [m] from the core centre (iosite.py). `pix` = sample spacing [m]; detail smaller than ~2
samples is replaced by its area average, so every tile level sees the same field at its own resolution.

Layout of the 1.4 km core (southern margin of the Prometheus flow field, flows to the north, frost plain south):
  * a small lava shield (~60 m) with a spatter cone (~40 m) on top, summit pit 32 m across holding a crusted lava
    lake; the cone is breached to the SSW and the breach feeds a lava tube running SSW down the shield;
  * the tube feeds an active inflating pahoehoe sheet lobe that has spread onto the frost plain; its front is a
    chain of breakout toes (minutes old, incandescent) - the walking site W = (-175, -175) is right there;
  * older flow units (weeks ... years) cover the north, frost/sulfur plain covers the south.
Temperatures come from the 1-D cooling model (cooling.py) through the age of each surface.
"""
import numpy as np
from noise import fbm, ridged, worley, cellrand, band, perlin
import cooling

TB = cooling.table()
YEAR, DAY, HOUR = 3.156e7, 86400.0, 3600.0
T_NIGHT = 95.0                 # frost plain at night [K] (Io night side ~90-100 K)

SHIELD = np.array([60.0, 200.0]); R_SHIELD = 360.0
H_SHIELD, H_CONE, R_CONE = 58.0, 38.0, 125.0
R_PIT, D_PIT = 16.0, 24.0            # pit radius / depth below the summit
BREACH = np.array([np.sin(np.radians(200)), np.cos(np.radians(200))])
# the active flow: one centreline from the lake edge out through the breach, down the cone and shield flank, spreading
# into a lobate sheet whose front reaches the walking site. (TUBE: kept as the name other code uses for this line)
FLOW = np.array([[54.5, 185.0], [40.8, 147.4], [31, 112], [17, 66], [-4, 22], [-32, -22], [-64, -64],
                 [-100, -104], [-132, -138], [-158, -166], [-185, -195]], float)
TUBE = FLOW
# side branches split off the main flow at the foot of the shield (pahoehoe fields branch): (path, s on the main
# where they leave it, sheet half-width, seed)
BRANCHES = [
    (np.array([[-32, -22], [-36, -60], [-48, -100], [-58, -140], [-62, -165]], float), None, 38.0, 101),
    (np.array([[-64, -64], [-96, -70], [-130, -82], [-160, -96]], float), None, 30.0, 202),
]
FAN = np.array([-108.0, -112.0])     # inside the sheet: reference point for the front (lights, previews)
S_SHEET = 205.0                      # m along the flow where the channel opens into the sheet (base of the shield)
W_SITE = np.array([-174.6, -174.6])  # centre of the high-detail zone (an L18 tile centre)
V_FRONT = 4.0 / HOUR                 # pahoehoe sheet front advance [m/s] (Hawaiian sheet lobes: 1-10 m/h)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def seg_dist(x, y, P):
    """distance to a polyline and the along-line coordinate (m from its start)"""
    best = np.full(x.shape, 1e9); along = np.zeros(x.shape); s0 = 0.0
    for a, b in zip(P[:-1], P[1:]):
        d = b - a; L = np.hypot(*d)
        t = np.clip(((x - a[0]) * d[0] + (y - a[1]) * d[1]) / (L * L), 0, 1)
        dist = np.hypot(x - a[0] - t * d[0], y - a[1] - t * d[1])
        m = dist < best
        best = np.where(m, dist, best); along = np.where(m, s0 + t * L, along); s0 += L
    return best, along


def ages_to_T(age):
    """surface temperature [K] for an age [s]; beyond the table (1 yr) the conductive tail ~ t^-1/2"""
    Ts = cooling.surface_T(age, TB)
    old = age > 3.0e7
    tail = T_NIGHT + (cooling.surface_T(3.0e7, TB) - T_NIGHT) * np.sqrt(3.0e7 / np.maximum(age, 3.0e7))
    return np.where(old, tail, Ts)


def evaluate(x, y, pix, want_color=True, base=None):
    out = {}
    # ---------------------------------------------------------------- large-scale relief
    dx, dy = x - SHIELD[0], y - SHIELD[1]
    r = np.hypot(dx, dy)
    # wobble the outline: an Io shield is built of overlapping flows, not a cone of revolution
    ang = np.arctan2(dx, dy)
    wob = 1 + 0.12 * fbm(np.cos(ang) * 2 + 50, np.sin(ang) * 2, 1.5, 0.4, 2, seed=11)
    rs = r / wob
    h_sh = H_SHIELD * np.clip(1 - (rs / R_SHIELD) ** 2, 0, 1) ** 1.6
    h_cone = H_CONE * np.clip(1 - rs / R_CONE, 0, 1) ** 1.25
    # spatter rampart around the pit and the pit itself (steep walls, lake floor)
    ramp = 6.0 * np.exp(-((r - R_PIT - 9) / 7.0) ** 2) * (1 + 0.5 * fbm(x, y, 8, pix, 3, seed=5))
    summit = H_SHIELD + H_CONE
    pit = smooth(R_PIT + 7, R_PIT - 2, r)
    # breach: a notch through the SSW rim feeding the tube
    along_b = dx * BREACH[0] + dy * BREACH[1]; side_b = np.abs(-dx * BREACH[1] + dy * BREACH[0])
    notch = smooth(9, 3, side_b) * smooth(R_PIT - 4, R_PIT + 2, along_b) * smooth(R_PIT + 40, R_PIT + 10, along_b)
    h = h_sh + h_cone + ramp * (1 - pit)
    h = h * (1 - pit) + (summit - D_PIT) * pit
    lake_level = summit - D_PIT
    floor = lake_level - 1.0 - 0.30 * np.clip(along_b - R_PIT, 0, None)     # channel floor down the cone flank
    h = h - notch * np.clip(h - floor, 0, None)

    # ------------------------------------------------------------ units: frost plain / old field / active lobe
    edge = y - (-60 - 0.25 * x + 70 * fbm(x, y, 320, pix, 3, seed=21) + 14 * fbm(x, y, 45, pix, 3, seed=22)
                + 3 * fbm(x, y, 9, pix, 3, seed=23))                         # >0: old flow field, <0: frost
    old_field = smooth(-6, 6, edge)
    on_shield = smooth(R_SHIELD * 1.02, R_SHIELD * 0.85, rs)
    old_field = np.maximum(old_field, on_shield)

    # the active flow: the main line from the lake through the breach, and two side branches at the foot of the shield.
    # Each: distance to its centreline (dt), the way along it (st), half-width with lobate margins, tongues at its end.
    L_all = float(np.sum(np.hypot(*np.diff(FLOW, axis=0).T)))
    def branch(P, s0, wmax, seed):
        d_, s_ = seg_dist(x, y, P); s_ = s_ + s0
        Lb = s0 + float(np.sum(np.hypot(*np.diff(P, axis=0).T)))
        w_ = 4.0 + 6.0 * smooth(0, 150, s_) + wmax * smooth(S_SHEET - 20, Lb - 40, s_) ** 1.3
        sh_ = smooth(S_SHEET - 30, S_SHEET + 30, s_)
        we = w_ * (1 + (0.12 + 0.30 * sh_) * fbm(x, y, 60, pix, 2, seed=seed + 3) + (0.05 + 0.15 * sh_) * fbm(x, y, 15, pix, 2, seed=seed + 2))             + 1.5 * fbm(x, y, 5, pix, 2, seed=seed + 4)
        Lf = Lb - 25 + 26 * fbm(x, y, 70, pix, 2, seed=seed + 6) + 8 * fbm(x, y, 20, pix, 2, seed=seed + 7)
        return d_, s_, we, Lf, np.minimum(we - d_, Lf - s_), sh_
    br = [branch(FLOW, 0.0, 50.0, 0)]
    for P, _, wmax, seed in BRANCHES:
        _, s_at = seg_dist(np.array([P[0, 0]]), np.array([P[0, 1]]), FLOW)
        br.append(branch(P, float(s_at[0]), wmax, seed))
    # each point belongs to the branch it lies deepest inside
    k = np.argmax(np.stack([b_[4] for b_ in br]), axis=0)
    pick = lambda i: np.choose(k, [b_[i] for b_ in br])
    dt, st, w_eff, L_front, behind, sheet = (pick(i) for i in range(6))
    active = smooth(-0.6, 0.6, behind)
    # breakout toes along the margins of the sheets: small lobes 2-6 m, minutes old
    f1, f2, tid, _, _ = worley(x, y, 4.5, seed=77)
    bump = (cellrand(tid, 1) < 0.6) * np.clip(1 - (f1 / 0.6) ** 2, 0, 1) ** 0.5 * (2.0 + 3.5 * cellrand(tid, 2)) * sheet
    bump = bump * smooth(-4.5, -1.0, behind)                              # toes stay attached to the front
    toe_shape = smooth(-0.3, 0.3, behind + bump) * smooth(1.5, -0.3, behind)
    toe = toe_shape * band(4.5, pix) + smooth(-1.5, 1.5, behind + 1.6) * smooth(1.5, -0.3, behind) * 0.5 * sheet * (1 - band(4.5, pix))
    active = np.maximum(active, toe)
    rf = st                                                                # flow-wise coordinate (ropes run across it)
    lobe_tail = (1 - sheet) * active                                       # the channel part
    # channel relief: levees +1.5 m along the margins, the channel floor 0.5 m down
    lev = 1.5 * np.exp(-((dt - 0.85 * w_eff) / (0.25 * w_eff + 0.5)) ** 2) - 0.5 * smooth(0.6 * w_eff, 0.2 * w_eff, dt)
    h += lev * lobe_tail

    # ---------------------------------------------------------------- ages [s]
    # old field: patchwork of units years old; some months-old lobes near the tube and on the shield flanks
    wx = x + 70 * fbm(x, y, 260, pix, 3, seed=33) + 14 * fbm(x, y, 40, pix, 3, seed=35) + 3 * fbm(x, y, 7, pix, 3, seed=37)
    wy = y + 70 * fbm(x, y, 260, pix, 3, seed=34) + 14 * fbm(x, y, 40, pix, 3, seed=36) + 3 * fbm(x, y, 7, pix, 3, seed=38)
    c1, c2, uid, _, _ = worley(wx / 2.0, wy, 75, seed=31)
    unit_edge = smooth(0.0, 0.10, c2 - c1)                                  # 0 at a unit margin
    u = cellrand(uid, 2)
    age_old = 10 ** (np.log10(2 * YEAR) + u * np.log10(10))                  # 2..20 years
    near_tube = smooth(120, 40, dt)
    age_mid = 10 ** (np.log10(30 * DAY) + cellrand(uid, 3) * np.log10(8))    # 1..8 months
    age_field = np.where((cellrand(uid, 4) < 0.35 + 0.5 * near_tube), age_mid, age_old)
    # active flow ages. Sheet: the crust is as old as the time since the margin passed (front speed), with a hotter
    # feeder lane down its middle. Channel: fast lava - incandescent crust rafts in the middle, levee crust hours old.
    age_sheet = np.clip(np.minimum(L_front - st, 3.0 * (w_eff - dt)), 0, None) / V_FRONT + 120 * (1 + fbm(x, y, 6, pix, 2, seed=8))
    # the feeder lane: a meandering, soft-edged band of younger crust down the middle of each sheet
    lane = smooth(7.0, 2.5, dt + 5.0 * fbm(x, y, 40, pix, 2, seed=11) + 1.5 * fbm(x, y, 8, pix, 2, seed=12))
    age_lane = 1.5 * HOUR + 1800 * fbm(x, y, 30, pix, 2, seed=10)
    age_sheet = np.exp(np.log(age_sheet) * (1 - lane) + np.log(np.minimum(age_lane, age_sheet)) * lane)
    raft = cellrand(worley(x, y, 3.0, seed=57)[2], 1)
    age_chan = np.where(dt < 0.55 * w_eff, 10 ** (1.0 + 1.3 * raft), 1.5 * HOUR + 4 * HOUR * smooth(0.55 * w_eff, w_eff, dt))
    age_act = np.where(sheet > 0.5, age_sheet, age_chan)
    age_toe = 10 ** (0.3 + 3.2 * cellrand(tid, 5))                          # 2 s .. 50 min
    age_act = np.where(toe_shape > 0.5, np.minimum(age_toe, age_act), age_act)

    sky = np.zeros_like(x)                                                 # (no tube now: an open channel)
    # vent lake: crust plates minutes old, molten seams
    p1, p2, pid, _, _ = worley(x, y, 5.5, seed=55)
    lake = pit * smooth(R_PIT - 1, R_PIT - 4, r)
    seam = smooth(0.10, 0.03, p2 - p1)
    age_lake = 30 + cellrand(pid, 6) * 3000
    chan = notch * smooth(5, 2, side_b)
    age_lake = np.where(seam > 0.5, 1.0, age_lake)

    age = np.where(active > 0.5, age_act, age_field)
    age = np.where(sky > 0.5, 1.0, age)
    age = np.where(lake > 0.5, age_lake, age)
    age = np.where(chan > 0.5, np.minimum(age, 5 + 60 * cellrand(pid, 7)), age)
    lava = np.maximum(np.maximum(old_field, active), np.maximum(lake, chan))
    out['age'] = np.where(lava > 0.5, age, np.inf)

    # ---------------------------------------------------------------- lobe relief (inflation, tumuli, old lobes)
    infl = np.where(active > 0, (0.6 + 2.6 * smooth(0, 70, np.clip(L_front - st, 0, None)) * smooth(0, 12, behind)) * sheet, 0)
    h_lobe = infl * active
    # tumuli on the inflated part of the active lobe and on the older field
    t1, t2, tmid, tvx, tvy = worley(x / 1.6, y, 30, seed=91)
    tum_r = 0.38 * (0.7 + 0.6 * cellrand(tmid, 8))
    tum = smooth(tum_r, 0.0, t1)                                           # a soft dome, no hard shoulders
    tum_h = 1.5 + 2.0 * cellrand(tmid, 10)
    p_tum = np.maximum(0.45 * active * smooth(25, 45, behind), 0.12 * old_field * (1 - on_shield))
    tum_zone = (cellrand(tmid, 9) < p_tum).astype(float)
    h_tum = tum * tum_h * tum_zone
    # older lobes: hummocky, ridged relief 0.5-2 m, gentle on the frost plain (buried flows)
    h_old = (1.6 * ridged(x, y, 60, pix, 4, seed=13) - 0.8) * old_field + (0.8 * fbm(x, y, 150, pix, 4, seed=14)) * (1 - old_field)
    h_unit = (3.0 - 0.9 * np.log10(np.maximum(age_field, DAY) / DAY) / 4.0) * (0.5 + 0.5 * cellrand(uid, 14))
    h_unit = np.where(cellrand(uid, 15) < 0.5, 0.0, np.clip(h_unit, 0.4, 3.0)) * unit_edge * old_field * (1 - on_shield)
    h += h_lobe + h_tum + (h_old + h_unit) * (1 - active)

    # ---------------------------------------------------------------- fine relief (texture only)
    hf = np.zeros_like(x)
    # ropy folds perpendicular to the flow direction (radial from the vent / the fan)
    flow_r = np.where(active > 0.5, rf, r)
    rope_wl = 0.45
    rope = np.sin(2 * np.pi * (flow_r + 0.6 * fbm(x, y, 3, pix, 3, seed=40)) / rope_wl) * band(rope_wl, pix)
    rope_mask = smooth(0.3, 0.7, fbm(x, y, 12, pix, 3, seed=41) + 0.5)
    hf += 0.06 * rope * rope_mask * (lava > 0.5)
    # toes / small lobes on all lava, 1-4 m
    s1, s2, sid, _, _ = worley(x, y, 2.2, seed=61)
    hf += 0.25 * np.clip(1 - s1 / 0.7, 0, 1) ** 1.5 * band(2.2, pix) * (lava > 0.5)
    hf += 0.5 * fbm(x, y, 20, pix, 5, seed=62) * (lava > 0.5)
    hf += 0.15 * fbm(x, y, 8, pix, 5, seed=63) * (lava <= 0.5)
    # polygonal cooling cracks and tumulus axial cracks
    # natural crust cracks: plate boundaries bent by a warp (curved, not straight Voronoi edges), two orders - plates
    # ~3 m and a finer net ~0.9 m inside them -, width varying along a crack, parts of it closed. Lines thinner than a
    # pixel are drawn ~1 px wide at reduced intensity (energy-conserving); unresolved nets become their mean glow.
    wxk = x + 0.7 * fbm(x, y, 3.0, pix, 3, seed=72); wyk = y + 0.7 * fbm(x, y, 3.0, pix, 3, seed=73)
    k1, k2, kid, _, _ = worley(wxk, wyk, 3.0, seed=71)
    w1 = 0.07 * (0.5 + 0.9 * np.clip(fbm(x, y, 2.0, pix, 2, seed=74) + 0.5, 0, 1))       # 3.5 .. 10 cm
    open1 = smooth(-0.25, 0.05, fbm(x, y, 4.0, pix, 2, seed=75))                         # ~70 % of the length open
    wd1 = np.maximum(w1, 1.2 * pix)
    c1 = smooth(wd1 / 3.0, wd1 * 0.4 / 3.0, k2 - k1) * (w1 / wd1) * open1
    res1 = band(1.4, pix)                          # plate net drawn while 0.35 m/px or finer
    c1 = c1 * res1 + (2 * 0.07 / 3.0 * 0.7) * (1 - res1)
    wxs = x + 0.25 * fbm(x, y, 1.0, pix, 2, seed=76); wys = y + 0.25 * fbm(x, y, 1.0, pix, 2, seed=77)
    q1, q2, _, _, _ = worley(wxs, wys, 0.9, seed=78)
    w2 = 0.02 * (0.5 + np.clip(fbm(x, y, 1.0, pix, 2, seed=79) + 0.5, 0, 1))             # 1 .. 3 cm
    open2 = smooth(0.0, 0.25, fbm(x, y, 1.5, pix, 2, seed=80))                           # ~45 % open
    wd2 = np.maximum(w2, 1.2 * pix)
    c2 = smooth(wd2 / 0.9, wd2 * 0.4 / 0.9, q2 - q1) * (w2 / wd2) * open2
    res2 = band(0.5, pix)                          # the fine net only at the finest level
    c2 = c2 * res2 + (2 * 0.02 / 0.9 * 0.45) * (1 - res2)
    crack = np.clip(c1 + 0.6 * c2, 0, 1)           # the fine cracks are shallower: dimmer
    crack = crack * (lava > 0.5)
    # tumulus: axial cleft along a random azimuth, 0.3-0.8 m wide
    ta = cellrand(tmid, 11) * np.pi
    perp = np.abs(tvx * 1.6 * np.sin(ta) - tvy * np.cos(ta)) * 30.0                 # m from the tumulus axis
    ax_w = 0.3 + 0.5 * cellrand(tmid, 12)                                    # cleft width [m]
    wd = np.maximum(ax_w, 1.2 * pix)
    ax = (tum > 0.25) * smooth(wd / 2, wd / 2 * 0.4, perp) * (ax_w / wd)
    res = band(30.0, pix * 4)                      # cleft drawn while the tumulus spans >= ~8 px
    ax = ax * res + (tum > 0.25) * (ax_w * 0.8 / 30.0) * (1 - res)
    ax = ax * tum_zone
    hf -= 0.3 * crack + 0.8 * ax
    out['h'] = h
    out['hf'] = hf

    # ---------------------------------------------------------------- temperatures
    a = np.where(np.isfinite(out['age']), out['age'], 1e12)
    Ts = ages_to_T(a)
    crust = cooling.crust(a, TB)
    Ts = np.where(lava > 0.5, Ts, T_NIGHT)
    # crack walls: seen down to about half the crust (polygonal cracks) or near the crust base (tumulus clefts)
    Tc = cooling.T_at_depth(a, np.clip(0.5 * crust, 0, 3), TB)
    Tax = cooling.T_at_depth(a, np.clip(0.85 * crust, 0, 6), TB)
    young = a < 60 * DAY
    f_c = np.where(young, crack, 0) ; f_a = np.where(young, ax, 0)
    f_c = np.clip(f_c, 0, 1); f_a = np.clip(f_a, 0, 1 - f_c)
    out['T_contact'] = Ts
    out['T_rad'] = ((1 - f_c - f_a) * Ts ** 4 + f_c * Tc ** 4 + f_a * Tax ** 4) ** 0.25
    out['crust'] = np.where(lava > 0.5, crust, 0)
    out['lava'] = lava; out['active'] = active
    kind = np.zeros(x.shape, np.uint8)
    kind[lava > 0.5] = 1
    kind[(lava > 0.5) & (a < 120 * DAY)] = 2
    kind[(lava > 0.5) & (a < 7 * DAY)] = 3
    kind[(lava > 0.5) & (a < 6 * HOUR)] = 4
    kind[(lava > 0.5) & (a < 120)] = 5
    kind[(sky > 0.5) | (seam * lake > 0.5)] = 6
    kind[(r < R_CONE) & (lava > 0.5) & (kind < 3) & (pit < 0.5)] = 7
    out['kind'] = kind
    out['f_c'] = f_c; out['f_a'] = f_a; out['Tc'] = Tc; out['Tax'] = Tax; out['Ts'] = Ts

    if want_color:
        out['rgb'] = colour(x, y, pix, out, r, old_field, active, a, uid, lava, hf, base)
    return out


def colour(x, y, pix, o, r, old_field, active, a, uid, lava, hf, base=None):
    def C(*v): return np.array(v, float) / 255.0
    n1 = fbm(x, y, 30, pix, 5, seed=101); n2 = fbm(x, y, 4, pix, 4, seed=102); n3 = fbm(x, y, 200, pix, 3, seed=103)
    # frost plain: stock-like yellowish with white SO2 frost patches and faint orange sulfur
    frost = C(150, 146, 104)[None, None] * (1 + 0.10 * n1[..., None] + 0.08 * n3[..., None] + 0.03 * n2[..., None])
    frost = frost * (1 + 0.07 * fbm(x, y, 1.6, pix, 4, seed=106))[..., None]
    if base is not None:                       # keep the stock mosaic's tone of the plain, add our detail
        frost = frost * np.clip(base / C(150, 146, 104)[None, None], 0.5, 1.5)
    white = smooth(0.1, 0.45, fbm(x, y, 70, pix, 4, seed=104)) * 0.45
    frost = frost * (1 - white[..., None]) + C(212, 208, 188)[None, None] * white[..., None]
    orange = smooth(0.25, 0.5, fbm(x, y, 90, pix, 4, seed=105))
    frost = frost * (1 - 0.35 * orange[..., None]) + C(165, 118, 78)[None, None] * 0.35 * orange[..., None]
    # lava: colour by age; dusting of sulfur/frost grows with age
    la = np.log10(np.maximum(a, 1))
    fresh, wk, mo, yr = C(30, 30, 33), C(40, 39, 37), C(60, 57, 48), C(78, 74, 58)
    t1 = smooth(np.log10(DAY), np.log10(14 * DAY), la)[..., None]
    t2 = smooth(np.log10(14 * DAY), np.log10(150 * DAY), la)[..., None]
    t3 = smooth(np.log10(150 * DAY), np.log10(4 * YEAR), la)[..., None]
    lv = fresh * (1 - t1) + wk * t1
    lv = lv * (1 - t2) + mo * t2
    lv = lv * (1 - t3) + yr * t3
    lv = lv * (1 + 0.10 * n2[..., None] + 0.06 * n1[..., None])
    dust = np.clip(smooth(np.log10(60 * DAY), np.log10(10 * YEAR), la) * (0.15 + 0.35 * smooth(-0.1, 0.4, n1)), 0, 0.5)
    lv = lv * (1 - dust[..., None]) + C(170, 162, 112)[None, None] * dust[..., None]
    # glassy sheen on fresh pahoehoe (silvery on convex ropes/toes)
    sheen = np.clip(hf * 4, 0, 1) * (1 - t1[..., 0]) * 0.5
    lv = lv + C(40, 42, 46)[None, None] * sheen[..., None]
    # red sulfur deposits around the vent (S2 from the vent gases; Io's red rings around active vents)
    red = np.exp(-(r / 170.0) ** 2) * (0.55 + 0.3 * n3)
    lv = lv * (1 - 0.6 * red[..., None]) + C(118, 58, 38)[None, None] * 0.6 * red[..., None]
    frost = frost * (1 - 0.5 * red[..., None]) + C(160, 80, 48)[None, None] * 0.5 * red[..., None]
    # frost next to hot lava sublimates (darker substrate), fresh SO2 frost settles further out (plume fallout)
    hot_d = np.clip(o['T_contact'] - 300, 0, None)
    fx, fy = x - FAN[0], y - FAN[1]
    dfront = np.hypot(fx, fy)
    halo = smooth(140, 90, dfront) * (1 - active)
    ring = smooth(110, 160, dfront) * smooth(420, 220, dfront)
    frost = frost * (1 - 0.35 * halo[..., None]) + C(92, 88, 70)[None, None] * 0.35 * halo[..., None]
    frost = frost * (1 - 0.2 * ring[..., None]) + C(222, 222, 210)[None, None] * 0.2 * ring[..., None]
    m = np.clip(lava, 0, 1)[..., None]
    rgb = frost * (1 - m) + lv * m
    # fine-relief shading (no sun direction: slopes and clefts darker, crests lighter)
    gy, gx = np.gradient(hf, pix)
    slope = 1 / np.sqrt(1 + 0.6 * (gx * gx + gy * gy))
    rgb = rgb * (0.75 + 0.25 * slope)[..., None] * (1 + 0.8 * np.clip(hf, -0.3, 0.3))[..., None]
    return np.clip(rgb, 0, 1)
