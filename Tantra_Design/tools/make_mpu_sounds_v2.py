# МПУ sounds, second pass (2026-10-19, the user: «пока он игрушечный. звук и движения и торможения и контакта - удара -
# это не удар подвески совсем»). Synthesised, original. Writes only the sounds below; make_mpu_sounds.py keeps the rest.
# What a 9-16 t electric 8x8 on airless steel-mesh wheels sounds like, part by part:
#  - drive.wav (loop, recorded at 40 km/h, played at the speed's rate): the hub motors' magnetic orders and the reduction's
#    gear mesh, but under a real machine's mechanical noise - bearings, the gears' broadband, the coolant pumps, the air -
#    with slow random level drift (no clean organ tone);
#  - drive_load.wav (loop, same rate): what torque adds - the pole-passing growl, the iron's magnetostriction buzz, the gear
#    flanks rattling under load; its level follows the force;
#  - roll_gravel / roll_sand / roll_ice.wav (loops, recorded at 8 m/s, played at the speed's rate): eight mesh tyres on the
#    ground - stones crunching and popping out (granular: hundreds of short bursts a second, a few heavy thumps), sand's
#    hiss and soft crunch, ice's sharp cracks and the steel mesh's scrape;
#  - rattle.wav (loop): the frame's loose things on rough ground - small metal impacts, panels buzzing;
#  - susp1..4.wav: one wheel's suspension stroke: the body's low boom (the chassis' first modes, 35-60 Hz), the arm
#    bushings' and the knuckle's clunk (150-450 Hz), a steel tick, the damper's oil hiss - four takes;
#  - bumpstop.wav: the suspension bottoming out on its stop: a hard thud and a long steel clank with a rattle;
#  - skid.wav (loop): locked wheels scrubbing the ground - stick-slip and crunch;
#  - brake.wav (loop): the friction brakes - the pads' groan (stick-slip at ~90 Hz and its harmonics), a squeal near a stop.
# Loops close seamlessly (whole periods). -> XRSound\MPU\*.wav, 44.1 kHz mono 16 bit.
import os, wave
import numpy as np
SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "XRSound", "MPU")
rng = np.random.default_rng(1019)


def save(name, x, peak=0.85):
    x = x - np.mean(x)
    x = x / (np.max(np.abs(x)) + 1e-9) * peak
    try:
        with wave.open(os.path.join(OUT, name), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
            w.writeframes((x * 32767).astype(np.int16).tobytes())
    except OSError as e:                                   # held open by XRSound while Orbiter runs
        print("NOT WRITTEN (in use):", name, e)


def snap(f, n): return max(1, round(f * n / SR)) * SR / n


def band(n, lo, hi, tilt=0.0):
    """periodic band noise (loops cleanly), 1/f^(tilt/2) in amplitude"""
    f = np.fft.rfftfreq(n, 1 / SR); spec = np.zeros(len(f), complex)
    m = (f >= lo) & (f <= hi)
    spec[m] = (rng.normal(size=m.sum()) + 1j * rng.normal(size=m.sum())) / np.maximum(f[m], 1.0) ** (0.5 * tilt)
    x = np.fft.irfft(spec, n)
    return x / (np.max(np.abs(x)) + 1e-9)


def drift(n, depth, rates=(0.3, 0.7, 1.3)):
    """a slow random level drift that closes the loop"""
    t = np.arange(n) / SR; d = np.zeros(n)
    for r in rates: d += np.sin(2 * np.pi * snap(r, n) * t + rng.uniform(0, 2 * np.pi))
    return 1.0 + depth * d / len(rates)


def modes(n, freqs, taus, amps, t0=0.0):
    """struck structure: damped sinusoids from t0"""
    out = np.zeros(n); k0 = int(t0 * SR)
    if k0 >= n: return out
    tt = np.arange(n - k0) / SR
    for f, tau, a in zip(freqs, taus, amps):
        out[k0:] += a * np.sin(2 * np.pi * f * tt + rng.uniform(0, 2 * np.pi)) * np.exp(-tt / tau)
    return out


def grains(n, rate, lo, hi, dmin, dmax, ampPow=1.6, wrap=True):
    """granular: `rate` short band bursts a second, random length and a heavy-tailed size (a few big stones)"""
    out = np.zeros(n)
    k = int(rate * n / SR)
    for _ in range(k):
        L = int(rng.uniform(dmin, dmax) * SR)
        a = rng.pareto(ampPow) + 0.2
        # each grain broadband (a crunch has no pitch): from a third of its centre to three times it, falling as 1/f -
        # narrow bands here rang as rapid keys being struck (the user, 2026-10-19, «клавиши, частый»)
        f = np.fft.rfftfreq(L, 1 / SR); spec = np.zeros(len(f), complex)
        c = rng.uniform(lo, hi)
        m = (f > c / 3.0) & (f < (std_hi := min(c * 3.0, 16000.0)))
        spec[m] = (rng.normal(size=m.sum()) + 1j * rng.normal(size=m.sum())) / np.maximum(f[m] / c, 0.3) ** 0.5
        g = np.fft.irfft(spec, L); g = g / (np.max(np.abs(g)) + 1e-9) * np.exp(-np.arange(L) / (L / 3.0))
        s0 = rng.integers(0, n)
        idx = (s0 + np.arange(L)) % n if wrap else np.arange(s0, min(n, s0 + L))
        out[idx] += a * g[:len(idx)]
    return out / (np.max(np.abs(out)) + 1e-9)


def thumps(n, rate, flo, fhi, tau):
    """heavy low hits (a big stone under a wheel), looped"""
    out = np.zeros(n); t = np.arange(n) / SR
    for _ in range(int(rate * n / SR)):
        s0 = rng.integers(0, n); f = rng.uniform(flo, fhi); a = rng.uniform(0.3, 1.0)
        L = int(5 * tau * SR); tt = np.arange(L) / SR
        idx = (s0 + np.arange(L)) % n
        out[idx] += a * np.sin(2 * np.pi * f * tt) * np.exp(-tt / tau) * (1 - np.exp(-tt / 0.004))
    return out / (np.max(np.abs(out)) + 1e-9)


N = SR * 4
t = np.arange(N) / SR


def tone(f, a, n=N):
    tt = np.arange(n) / SR
    return a * np.sin(2 * np.pi * snap(f, n) * tt + rng.uniform(0, 2 * np.pi))


# ---------------- the drive at 40 km/h: wheel 133 rpm, motor 1590 rpm (1:12), electrical 212 Hz (8 pole pairs),
# gear mesh 795 Hz (30 teeth). Each of eight motors a hair apart, each order's level drifting; the mechanics' noise on top
fe0, fg0 = 212.0, 795.0
drv = np.zeros(N)
for m in range(8):
    d = 1.0 + (m - 3.5) * 0.0021
    fe, fg = fe0 * d, fg0 * d
    drv += (tone(fe, 0.35) + tone(2 * fe, 0.22) + tone(6 * fe, 0.09) + tone(12 * fe, 0.03)) * drift(N, 0.35)
    drv += (tone(fg, 0.20) + tone(2 * fg, 0.06) + tone(fg - fe / 8, 0.06) + tone(fg + fe / 8, 0.06)) * drift(N, 0.5)
lvl = np.max(np.abs(drv))
drv += 0.55 * lvl * band(N, 250, 4500, 1.0) * drift(N, 0.25)        # the gears' and bearings' broadband
drv += 0.30 * lvl * band(N, 600, 7000, 0.4) * drift(N, 0.15)        # the coolant pumps' and the fans' air
drv += 0.45 * lvl * band(N, 25, 180, 1.4) * drift(N, 0.3)           # the structure's rumble
save("drive.wav", np.tanh(1.2 * drv / np.max(np.abs(drv))), 0.8)

# ---------------- the load: pole passing (fe/8 = 26 Hz) growl, the iron's buzz at 2 fe with its sidebands, the gear flanks
# rattling (mesh-rate impulses with random strength)
ld = np.zeros(N)
for m in range(8):
    d = 1.0 + (m - 3.5) * 0.0021
    fe = fe0 * d
    am = 1.0 + 0.6 * np.sin(2 * np.pi * snap(fe / 8, N) * t + rng.uniform(0, 6.28))
    ld += (tone(fe, 0.5) + tone(2 * fe, 0.6) + tone(4 * fe, 0.25)) * am * drift(N, 0.3)
rat = np.zeros(N); step = SR / fg0
pos = 0.0
while pos < N:
    rat[int(pos)] = rng.uniform(0.2, 1.0) ** 2; pos += step
kern = modes(int(0.004 * SR), [2300.0, 3900.0], [0.0008, 0.0006], [1.0, 0.6])
rat = np.fft.irfft(np.fft.rfft(rat) * np.fft.rfft(kern, N), N)
ld = ld / np.max(np.abs(ld)) + 0.35 * rat / (np.max(np.abs(rat)) + 1e-9) + 0.4 * band(N, 40, 400, 1.2) * drift(N, 0.4)
save("drive_load.wav", np.tanh(1.5 * ld / np.max(np.abs(ld))), 0.8)

# ---------------- rolling at 8 m/s on gravel / cobbles: 8 wheels x ~60 stones a second each under the tread, a few
# heavy ones thumping, the mesh tyres' own low rumble
g = grains(N, 480, 700, 6500, 0.002, 0.012) * 0.8 + grains(N, 60, 250, 1500, 0.008, 0.03) * 0.6
roll_g = g + 0.9 * thumps(N, 7, 45, 110, 0.06) + 0.6 * band(N, 18, 220, 1.5) * drift(N, 0.3)
save("roll.wav", np.tanh(1.1 * roll_g / np.max(np.abs(roll_g))), 0.85)          # the default (gravel, regolith, crust)
# sand (damp, Titan's dunes; loam): a soft hiss, dull crunch, heavy damping of the highs
s = 0.7 * band(N, 200, 2500, 1.2) * drift(N, 0.35, (0.8, 1.7, 3.1)) + 0.5 * grains(N, 300, 300, 1800, 0.004, 0.02)
s += 0.7 * band(N, 18, 160, 1.6) * drift(N, 0.3) + 0.4 * thumps(N, 2, 40, 80, 0.08)
save("roll_sand.wav", s, 0.8)
# ice (Titan's bedrock, frozen ground): sharp cracks (broadband, no pitch), the steel mesh scraping, the dull knock of the
# tread on a hard floor (noise through a low band, not a tone - a tone here read as a key being struck)
def icecracks(n, rate):
    out = np.zeros(n)
    for _ in range(int(rate * n / SR)):
        L = int(rng.uniform(0.0008, 0.005) * SR); s0 = rng.integers(0, n)
        g = band(L, 1500, 14000, 0.2) * np.exp(-np.arange(L) / (L / 4.0)) * (rng.pareto(1.3) + 0.2)
        out[(s0 + np.arange(L)) % n] += g
    return out / (np.max(np.abs(out)) + 1e-9)
def knocks(n, rate, lo, hi, tau):
    out = np.zeros(n)
    for _ in range(int(rate * n / SR)):
        L = int(5 * tau * SR); s0 = rng.integers(0, n)
        g = band(L, lo, hi, 1.0) * np.exp(-np.arange(L) / SR / tau) * rng.uniform(0.3, 1.0)
        out[(s0 + np.arange(L)) % n] += g
    return out / (np.max(np.abs(out)) + 1e-9)
i_ = 0.8 * icecracks(N, 110) + 0.4 * band(N, 1500, 8000, 0.5) * drift(N, 0.5, (1.1, 2.3, 4.7))
i_ += 0.5 * knocks(N, 6, 60, 700, 0.025) + 0.4 * band(N, 18, 150, 1.5)
save("roll_ice.wav", i_, 0.8)

# ---------------- metal that is struck, not tuned (the user, 2026-10-19, on the first pass: «клавиши» - the rattle and
# the clunks were a few clean damped tones each, a toy xylophone). A real steel part rings in dozens of crowded, inharmonic
# modes that die within milliseconds to a tenth of a second, set off by a noisy contact - it reads as a clank, not a note
def clank(n, t0, lo, hi, count, tmin, tmax, noise=0.6, nlen=0.006):
    out = np.zeros(n); k0 = int(t0 * SR)
    if k0 >= n: return out
    L = n - k0; tt = np.arange(L) / SR
    for _ in range(count):
        f = np.exp(rng.uniform(np.log(lo), np.log(hi))); tau = np.exp(rng.uniform(np.log(tmin), np.log(tmax)))
        out[k0:] += rng.uniform(0.3, 1.0) * np.sin(2 * np.pi * f * tt + rng.uniform(0, 6.28)) * np.exp(-tt / tau)
    out /= (np.max(np.abs(out)) + 1e-9)
    m = min(int(nlen * SR), L)
    if m > 8:
        out[k0:k0 + m] += noise * band(m, lo, min(hi * 2, 15000), 0.3) * np.exp(-np.arange(m) / SR / (nlen / 3))
    return out


# ---------------- the frame's rattle on rough ground: loose steel (hatches, rails, cable trays, the ladder) knocking - short
# noisy clanks bunched in clusters as the wheels go over bumps, no pitch to them; the panels' buzz under it
n = N; r = np.zeros(n)
t0 = 0.0
while t0 < n / SR - 0.2:
    t0 += rng.exponential(0.12)                                           # a bump
    for _ in range(rng.integers(2, 7)):                                   # what it shakes loose
        tc = t0 + rng.uniform(0.0, 0.08)
        if tc < n / SR - 0.15:
            r += rng.uniform(0.15, 1.0) ** 2 * clank(n, tc, 600, 7000, 30, 0.002, 0.03, 1.0, 0.004)
r = r / (np.max(np.abs(r)) + 1e-9)
r += 0.35 * band(n, 80, 900, 1.0) * drift(n, 0.6, (2.1, 4.3, 7.7))        # panels and the frame buzzing
save("rattle.wav", r, 0.7)

# ---------------- one suspension stroke (a wheel over a rock, a rut's edge): the chassis' low boom (a thud, 35-60 Hz, the
# only tonal part - a big body's first modes), the wishbones' bushings and the knuckle - a dull clunk of crowded modes, the
# damper's oil hiss. Four takes
for k in range(4):
    n = int(0.9 * SR)
    fb = rng.uniform(36, 58)
    x = 1.0 * modes(n, [fb, fb * 1.62], [0.20, 0.10], [1.0, 0.35]) * (1 - np.exp(-np.arange(n) / SR / 0.003))
    x += 0.75 * clank(n, 0.003, 110, 900, 40, 0.008, 0.07, 0.8, 0.01)
    x += 0.15 * clank(n, 0.005, 1200, 5000, 25, 0.002, 0.012, 1.0, 0.003)
    h = np.zeros(n); L = int(0.12 * SR); h[:L] = band(L, 400, 2600, 0.6) * np.exp(-np.arange(L) / SR / 0.04)
    x += 0.15 * h
    save("susp%d.wav" % (k + 1), np.tanh(1.3 * x / np.max(np.abs(x))), 0.9)

# ---------------- bottoming out on the stop: a hard thud, a heavy clank of the arm on its stop and the frame, things
# rattling loose after
n = int(1.4 * SR)
x = 1.4 * modes(n, [44.0, 71.0], [0.28, 0.14], [1.0, 0.5]) * (1 - np.exp(-np.arange(n) / SR / 0.002))
x += 1.1 * clank(n, 0.002, 150, 3500, 60, 0.02, 0.3, 1.0, 0.012)
tc = 0.05
while tc < 0.7:
    x += rng.uniform(0.08, 0.3) * clank(n, tc, 600, 6000, 20, 0.002, 0.02, 1.0, 0.003); tc += rng.exponential(0.06)
save("bumpstop.wav", np.tanh(1.4 * x / np.max(np.abs(x))), 0.95)

# ---------------- locked wheels scrubbing (at 8 m/s): broadband scrape with stick-slip (~25-40 Hz, irregular) and crunch
n = N
ss = 0.5 + 0.5 * np.sin(2 * np.pi * snap(31.0, n) * t + 0.6 * np.sin(2 * np.pi * snap(3.3, n) * t))
sk = 0.9 * band(n, 120, 4500, 0.9) * ss * drift(n, 0.3) + 0.6 * grains(n, 380, 600, 5000, 0.002, 0.01) + 0.5 * band(n, 20, 160, 1.4)
save("skid.wav", np.tanh(1.2 * sk / np.max(np.abs(sk))), 0.85)

# ---------------- the friction brakes (a loop): the pads' groan - stick-slip near 90 Hz with its harmonics, wandering -
# a squeal that comes and goes (~2.7 kHz), the hiss of the pads
n = SR * 3; tt = np.arange(n) / SR
fm = 88.0 * (1.0 + 0.06 * np.sin(2 * np.pi * snap(0.7, n) * tt))
ph = 2 * np.pi * np.cumsum(fm) / SR
groan = sum(a * np.sin(k * ph) for k, a in ((1, 1.0), (2, 0.7), (3, 0.5), (4, 0.3), (5, 0.2)))
sq_env = np.clip(np.sin(2 * np.pi * snap(0.45, n) * tt) * 2.0 - 1.0, 0, 1) ** 2
squeal = sq_env * (np.sin(2 * np.pi * snap(2710.0, n) * tt) + 0.3 * np.sin(2 * np.pi * snap(5420.0, n) * tt))
br = 0.8 * groan / np.max(np.abs(groan)) * drift(n, 0.3) + 0.25 * squeal + 0.35 * band(n, 800, 6000, 0.5) + 0.5 * band(n, 25, 200, 1.3)
save("brake.wav", np.tanh(1.2 * br / np.max(np.abs(br))), 0.85)
print("written:", sorted(f for f in os.listdir(OUT) if f.endswith(".wav")))
