"""Sounds of the carriage (erection / lowering of the 52 kt ship on its legs), synthesised - no samples.

Physics behind each: a structure this size does NOT ring - its joints, its hydraulics and the ground damp every blow
within a fraction of a second (a free-hanging casting rings like a bell; a 52 kt ship standing on its legs thuds).
A blow = a dull thump of the mass + a short dense crunch of steel (many modes, ~0.1 s each) + a low rumble running
through the mass. The drives are superconducting and all but silent: what is heard is the load (rumble, moans of the
structure, the rollers). The rails: stick-slip judder. The cups crush the ground. The MR struts groan and hiss.
Two versions each: _ext through the air (a ground echo, the highs taken by the air) and _int through the hull
(low-passed, its body). Loops are seamless.

  python tools/gen_lift_sounds.py            -> XRSound/Tantra/lift_*.wav of the Orbiter install this tree is in
"""
import os
import wave

import numpy as np
from scipy import signal

SR = 44100
rng = np.random.default_rng(1954)   # fixed: the same files every time
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "XRSound", "Tantra"))


def t_axis(sec):
    return np.arange(int(sec * SR)) / SR


def bp(x, lo, hi, order=4):
    sos = signal.butter(order, [lo, hi], btype="band", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def lp(x, f, order=4):
    sos = signal.butter(order, f, btype="low", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def hp(x, f, order=2):
    sos = signal.butter(order, f, btype="high", fs=SR, output="sos")
    return signal.sosfilt(sos, x)


def peak(x, f, q):
    b, a = signal.iirpeak(f, q, fs=SR)
    return signal.lfilter(b, a, x)


def modes(t, freqs, taus, amps, t0=0.0):
    """A struck structure: a sum of decaying modes from time t0."""
    y = np.zeros_like(t)
    tt = t - t0
    on = tt >= 0
    for f, tau, a in zip(freqs, taus, amps):
        ph = rng.uniform(0, 2 * np.pi)
        y[on] += a * np.exp(-tt[on] / tau) * np.sin(2 * np.pi * f * tt[on] * (1 + 0.004 * np.exp(-tt[on] / 0.3)) + ph)
    return y


def heavy_hit(t, t0, gain=1.0, size=1.0):
    """A massive damped structure struck: thump + short dense steel crunch + a rumble through the mass. size > 1: lower, longer."""
    tt = np.clip(t - t0, 0, None)
    on = t >= t0
    att = (1 - np.exp(-tt / 0.003)) * on
    f = (20 + 28 * np.exp(-tt / 0.06)) / size
    thump = np.sin(2 * np.pi * np.cumsum(f * on) / SR) * np.exp(-tt / (0.25 * size)) * att
    body = bp(rng.standard_normal(len(t)), 18 / size, 220 / size, 2) * np.exp(-tt / (0.3 * size)) * att
    crunch = np.zeros_like(t)
    for _ in range(45):                       # many short modes: steel, not a bell
        fm = np.exp(rng.uniform(np.log(40), np.log(520))) / size
        crunch += rng.uniform(0.3, 1.0) / np.sqrt(fm / 40) * np.exp(-tt / rng.uniform(0.03, 0.16)) * np.sin(2 * np.pi * fm * tt + rng.uniform(0, 6.28)) * on
    knock = bp(rng.standard_normal(len(t)), 120, 1100) * np.exp(-tt / 0.018) * on
    tail = bp(rng.standard_normal(len(t)), 14, 70, 2) * np.exp(-tt / (1.0 * size)) * on
    # the steel resonating - not a bell: a dense dirty cluster of low tones in slightly detuned pairs (they beat and
    # rattle), gone within a second; a bell is a few clean tones ringing for seconds
    res = np.zeros_like(t)
    for _ in range(9):
        fm = np.exp(rng.uniform(np.log(35), np.log(260))) / size
        tau = rng.uniform(0.3, 0.7) * size
        for df in (0.0, rng.uniform(0.6, 2.5)):
            res += rng.uniform(0.5, 1.0) / np.sqrt(fm / 35) * np.exp(-tt / tau) * np.sin(2 * np.pi * (fm + df) * tt + rng.uniform(0, 6.28)) * on
    res = lp(res * (1 + 0.5 * bp(rng.standard_normal(len(t)), 5, 30, 1)), 400)   # roughened: it rattles, not sings
    return gain * (1.2 * thump + 1.0 * body + 0.35 * crunch / 6 + 0.25 * knock + 0.45 * tail + 0.12 * res)


def burst(t, t0, dur, lo, hi):
    n = rng.standard_normal(len(t))
    env = np.exp(-np.clip(t - t0, 0, None) / dur) * (t >= t0)
    return bp(n, lo, hi) * env


def reverb(x, sec, tone_lo, tone_hi, wet, resonances=()):
    """Exponential noise tail; for the hull, ringing at its own resonances."""
    ti = t_axis(sec)
    ir = rng.standard_normal(len(ti)) * np.exp(-ti / (sec / 5))
    ir = bp(ir, tone_lo, tone_hi, 2)
    for f, q in resonances:
        ir = ir + 0.6 * peak(ir, f, q)
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-12
    y = signal.fftconvolve(x, ir)[: len(x)]
    return (1 - wet) * x + wet * y / (np.max(np.abs(y)) + 1e-12) * np.max(np.abs(x))


def echo(x, delay, gain, lo=60, hi=900):
    d = int(delay * SR)
    y = x.copy()
    y[d:] += gain * bp(x[:-d], lo, hi, 2)
    return y


def make_loop(x, fade):
    """Seamless loop: the tail crossfaded into the head."""
    n = int(fade * SR)
    head, tail = x[:n].copy(), x[-n:]
    w = np.linspace(0, 1, n)
    out = x[: len(x) - n].copy()
    out[:n] = head * w + tail * (1 - w)
    return out


def norm(x, dbfs):
    return x / (np.max(np.abs(x)) + 1e-12) * 10 ** (dbfs / 20)


def air(x):
    """Outside: the air takes the highs off over the distance a 178 m ship is heard from."""
    return lp(x, 3500, 2)


def fade_out(x, sec=1.0):
    n = int(sec * SR)
    y = x.copy()
    y[-n:] *= np.linspace(1, 0, n) ** 2
    return y


def compress(x, k):
    """Soft saturation: raises the average level (what the ear hears as loudness) under the same peak."""
    x = x / (np.max(np.abs(x)) + 1e-12)
    return np.tanh(k * x) / np.tanh(k)


def save(name, x, dbfs=-1.0, oneshot=True):
    os.makedirs(OUT, exist_ok=True)
    x = compress(x, 2.6 if oneshot else 1.8)
    if oneshot:
        x = fade_out(x)
    y = np.clip(norm(x, dbfs), -1, 1)
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((y * 32767).astype("<i2").tobytes())
    print(f"{name}: {len(y) / SR:.1f} s")


INT_HULL = ((38, 1.5), (61, 1.8), (94, 2.0))   # the hull's body from inside: broad, damped (no ringing)


# --- the load itself: the drives are superconducting and all but silent - what is heard is steel carrying kilotons:
# a shapeless infrasonic rumble (no tone, no rhythm), slow groans of the structure gliding in pitch, the guide rollers of
# the telescopes rolling, sparse ticks of stress settling. Nothing periodic - no engine.
def drive_hum():
    sec = 9.0
    t = t_axis(sec)
    # rumble: low noise with a slow, irregular swell
    swell = 1.0 + sum(rng.uniform(0.08, 0.2) * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) for f in (0.11, 0.23, 0.37, 0.61))
    rumble = bp(rng.standard_normal(len(t)), 14, 75, 2) * swell
    # the guide rollers of the telescope stages: a dull roll, unsteady
    roll = bp(rng.standard_normal(len(t)), 60, 240, 2) * (0.6 + 0.4 * np.abs(np.sin(2 * np.pi * 0.29 * t + 1.0)))
    # groans: the structure under the load, here and there, each a slow glide
    groans = np.zeros_like(t)
    for _ in range(5):                        # a band of noise sliding slowly: steel straining, not singing
        t0, dur = rng.uniform(0, sec - 2.5), rng.uniform(1.6, 3.2)
        f0 = rng.uniform(40, 110)
        u = np.clip((t - t0) / dur, 0, 1)
        env = np.sin(np.pi * u) ** 2 * ((t >= t0) & (t <= t0 + dur))
        seg = rng.standard_normal(len(t)) * env
        lo_band = bp(seg, f0 * 0.7, f0 * 1.4, 2)
        hi_band = bp(seg, f0 * 1.2, f0 * 2.4, 2)
        groans += (lo_band * (1 - u) + hi_band * u) * rng.uniform(0.5, 1.0) if rng.uniform() < 0.5 else (lo_band * u + hi_band * (1 - u)) * rng.uniform(0.5, 1.0)
    # stress settling: sparse ticks and pings
    ticks = np.zeros_like(t)
    for _ in range(7):
        t0 = rng.uniform(0, sec)
        ticks += burst(t, t0, rng.uniform(0.01, 0.03), 90, 700) * rng.uniform(0.3, 1.0)   # a dull knock
    ext = 1.0 * rumble + 0.45 * roll + 0.9 * groans + 0.25 * ticks
    ext = echo(reverb(ext, 2.2, 30, 2000, 0.35), 0.24, 0.25)
    inn = lp(1.0 * rumble + 0.35 * roll + 1.0 * groans + 0.15 * ticks, 220)
    inn = reverb(inn, 3.0, 20, 300, 0.4, INT_HULL)
    save("lift_hum_ext.wav", make_loop(air(ext), 0.8), -3.0, False)
    save("lift_hum_int.wav", make_loop(inn, 0.8), -3.0, False)


# --- a telescope stage reaching its stop and locking: heavier than a latch, shorter than the trunnion lock ---
def stage_lock():
    sec = 5.0
    t = t_axis(sec)
    y = heavy_hit(t, 0.0, 1.0, 1.0) + heavy_hit(t, 0.09, 0.35, 0.8)
    ext = echo(reverb(y, 2.0, 40, 2500, 0.3), 0.28, 0.3)
    inn = reverb(lp(y, 360), 3.0, 25, 400, 0.45, INT_HULL)
    save("lift_stage_ext.wav", air(ext))
    save("lift_stage_int.wav", inn)


# --- the struts taking the load: a creaking groan (stick-slip through resonances) and the hiss of the MR valves ---
def strut_groan():
    sec = 5.5
    t = t_axis(sec)
    imp = np.zeros_like(t)
    tt = 0.0
    while tt < sec:
        rate = 16 + 8 * np.sin(2 * np.pi * 0.35 * tt) + rng.uniform(-3, 3)
        i = int(tt * SR)
        if i < len(imp):
            imp[i] = rng.uniform(0.5, 1.0)
        tt += 1.0 / rate
    creak = peak(imp, 155, 18) + 0.7 * peak(imp, 310, 22) + 0.4 * peak(imp, 520, 25) + 0.25 * peak(imp, 860, 30)
    groan = np.sin(2 * np.pi * (52 + 6 * np.sin(2 * np.pi * 0.2 * t)) * t) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.35 * t))
    hiss = bp(rng.standard_normal(len(t)), 800, 2600) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.9 * t) ** 2)
    ext = 0.8 * creak + 0.9 * groan + 0.06 * hiss
    ext = reverb(ext, 1.4, 60, 3000, 0.25)
    inn = reverb(lp(1.2 * creak + 0.9 * groan, 380), 2.2, 40, 400, 0.35, INT_HULL)
    save("lift_strut_ext.wav", make_loop(air(ext), 0.5), -4.0, False)
    save("lift_strut_int.wav", make_loop(inn, 0.5), -4.0, False)


# --- a lock / a stop: a massive steel structure struck - the pins seat, the hull rings ---
def clunk():
    sec = 5.0
    t = t_axis(sec)
    y = heavy_hit(t, 0.0, 1.0, 1.3) + heavy_hit(t, 0.13, 0.5, 1.0)          # the stop, then the pins seat
    ext = echo(reverb(y, 2.4, 40, 3000, 0.3), 0.33, 0.3)
    inn = reverb(lp(y, 420), 3.5, 25, 450, 0.45, INT_HULL)
    save("lift_clunk_ext.wav", air(ext))
    save("lift_clunk_int.wav", inn)


# --- a cup on the ground: R 12 m of ribs and skirt under thousands of tonnes - an infrasonic thud, the ground crushed
# and pressed (a long low shake of the soil), the ribs ringing; several cups never touch at once ---
def cup_hit(t, t0, gain):
    tt = np.clip(t - t0, 0, None)
    on = t >= t0
    f = 18 + 30 * np.exp(-tt / 0.15)
    thud = np.sin(2 * np.pi * np.cumsum(f * on) / SR) * np.exp(-tt / 0.9) * (1 - np.exp(-tt / 0.006)) * on
    shake = bp(rng.standard_normal(len(t)), 12, 45, 2) * np.exp(-tt / 1.4) * on
    crush = burst(t, t0 + 0.01, 0.4, 120, 1300)
    for _ in range(40):
        tc = t0 + rng.uniform(0.0, 0.9)
        crush += burst(t, tc, 0.014, 250, 2000) * rng.uniform(0.1, 0.4) * np.exp(-(tc - t0) / 0.4)
    return gain * (1.3 * thud + 0.9 * shake + 0.45 * crush + 0.7 * heavy_hit(t, t0 + 0.01, 1.0, 1.5))


def cups(name, starts):
    sec = starts[-1][0] + 5.5
    t = t_axis(sec)
    y = sum(cup_hit(t, t0, g) for t0, g in starts)
    ext = echo(reverb(y, 2.2, 25, 2200, 0.3), 0.29, 0.35, 25, 600)
    inn = reverb(lp(y, 240), 3.2, 18, 300, 0.42, INT_HULL)
    save(f"lift_{name}_ext.wav", air(ext))
    save(f"lift_{name}_int.wav", inn)


def ground_boom():
    cups("boom", [(0.0, 1.0)])                                                    # one cup (kangaroo, a single foot)
    cups("cups2", [(0.0, 1.0), (0.23, 0.85)])                                     # the two blades
    cups("cups4", [(0.0, 1.0), (0.17, 0.8), (0.41, 0.9), (0.66, 0.7)])           # the four stern legs


# --- the locking bolts: huge breech-like bolts driven home - a sharp steel snap, the bolt slamming, the mass behind;
# a series (the bolts of a joint never go at once). Short metal sounds: a snap, not a ring ---
def bolt(t, t0, gain):
    tt = np.clip(t - t0, 0, None)
    on = t >= t0
    snap = bp(rng.standard_normal(len(t)), 900, 4500) * np.exp(-tt / 0.007) * on
    clack = np.zeros_like(t)
    for _ in range(14):                       # the steel snap: high modes, very short
        fm = np.exp(rng.uniform(np.log(550), np.log(2600)))
        clack += rng.uniform(0.3, 1.0) * np.exp(-tt / rng.uniform(0.012, 0.05)) * np.sin(2 * np.pi * fm * tt + rng.uniform(0, 6.28)) * on
    slam = bp(rng.standard_normal(len(t)), 110, 700) * np.exp(-tt / 0.045) * on
    return gain * (0.8 * snap + 0.12 * clack + 0.9 * slam + 0.8 * heavy_hit(t, t0 + 0.004, 1.0, 0.75))


def bolts():
    sec = 4.0
    t = t_axis(sec)
    y = np.zeros_like(t)
    t0 = 0.0
    for i in range(6):
        y += bolt(t, t0, rng.uniform(0.8, 1.0))
        t0 += rng.uniform(0.14, 0.24)
    ext = echo(reverb(y, 1.6, 50, 4000, 0.25), 0.27, 0.35, 60, 1500)
    inn = reverb(lp(y, 900), 2.2, 30, 900, 0.35, INT_HULL)
    save("lift_bolts_ext.wav", lp(ext, 6000, 2))
    save("lift_bolts_int.wav", inn)


# --- the four stern legs swinging out of the nacelles and running down: the hinges groaning under the swing, the
# telescopes rolling out, the slide of the sections; a loop while they move ---
def legs_out():
    sec = 7.0
    t = t_axis(sec)
    roll = bp(rng.standard_normal(len(t)), 45, 260, 2) * (0.7 + 0.3 * np.abs(np.sin(2 * np.pi * 0.43 * t)))
    slide = bp(rng.standard_normal(len(t)), 250, 1100, 2) * (0.4 + 0.6 * np.abs(np.sin(2 * np.pi * 0.17 * t + 0.6)))
    hinge = np.zeros_like(t)
    for _ in range(6):                        # the hinges straining: noise bands sliding (steel, not voice)
        t0, dur = rng.uniform(0, sec - 2.0), rng.uniform(1.2, 2.6)
        f0 = rng.uniform(50, 140)
        u = np.clip((t - t0) / dur, 0, 1)
        env = np.sin(np.pi * u) ** 2 * ((t >= t0) & (t <= t0 + dur))
        seg = rng.standard_normal(len(t)) * env
        hinge += bp(seg, f0 * (0.8 + 0.3 * rng.uniform()), f0 * 2.0, 2) * rng.uniform(0.6, 1.0)
    knocks = np.zeros_like(t)
    for _ in range(9):
        knocks += heavy_hit(t, rng.uniform(0, sec - 0.5), rng.uniform(0.08, 0.2), 0.7)
    ext = 0.9 * roll + 0.35 * slide + 1.0 * hinge + 0.6 * knocks
    ext = echo(reverb(ext, 1.8, 40, 2500, 0.3), 0.23, 0.25)
    inn = reverb(lp(0.9 * roll + 1.0 * hinge + 0.6 * knocks, 260), 2.6, 25, 300, 0.4, INT_HULL)
    save("lift_legs_ext.wav", make_loop(air(ext), 0.6), -3.0, False)
    save("lift_legs_int.wav", make_loop(inn, 0.6), -3.0, False)


# --- the hull starting or stopping on the rails: stick-slip judder of the trunnion carriages under 52 kt ---
def rail_judder():
    sec = 4.5
    t = t_axis(sec)
    y = np.zeros_like(t)
    tt = 0.05
    while tt < 3.2:
        rate = 6 + 7 * tt / 3.2
        y += heavy_hit(t, tt, rng.uniform(0.35, 0.7) * (1 - tt / 4), 0.8)
        y += 0.12 * burst(t, tt, 0.01, 150, 1400)
        tt += 1.0 / rate + rng.uniform(-0.01, 0.01)
    groan = bp(rng.standard_normal(len(t)), 25, 70, 2) * np.clip(t / 0.8, 0, 1) * np.exp(-np.clip(t - 3.0, 0, None) / 0.4)
    y += 0.5 * groan
    ext = echo(reverb(y, 1.8, 40, 2500, 0.3), 0.25, 0.25)
    inn = reverb(lp(y, 300), 2.8, 25, 350, 0.4, INT_HULL)
    save("lift_rail_ext.wav", air(ext))
    save("lift_rail_int.wav", inn)


# --- a blade sliding home into its pocket: a long scrape, then the stop and the lock ---
def stow():
    sec = 7.5
    t = t_axis(sec)
    env = np.clip(t / 1.2, 0, 1) * np.clip((4.3 - t) / 0.6, 0, 1)
    scrape = bp(rng.standard_normal(len(t)), 180, 900) * env * (0.7 + 0.3 * np.sin(2 * np.pi * 1.7 * t))
    scrape += 0.4 * bp(rng.standard_normal(len(t)), 60, 180) * env
    hit = heavy_hit(t, 4.3, 1.0, 1.2) + heavy_hit(t, 4.62, 0.55, 1.0)
    y = 0.55 * scrape + hit
    ext = echo(reverb(y, 2.2, 40, 3000, 0.3), 0.3, 0.3)
    inn = reverb(lp(y, 380), 3.2, 25, 420, 0.45, INT_HULL)
    save("lift_stow_ext.wav", air(ext))
    save("lift_stow_int.wav", inn)


if __name__ == "__main__":
    drive_hum()
    stage_lock()
    strut_groan()
    clunk()
    ground_boom()
    rail_judder()
    stow()
    bolts()
    legs_out()
    print("->", OUT)
