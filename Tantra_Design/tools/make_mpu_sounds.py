# МПУ sounds (synthesised, original). The hub motors: one clean traction-drive loop the module plays at the speed's
# rate (XRSound 3 SetPlaybackSpeed), the inverter's start whistle. Tyres rolling, a suspension knock, the platform's lift drive.
# Loops are seamless (whole periods in the loop length). -> XRSound\MPU\*.wav, 44.1 kHz mono 16 bit.
import os, wave
import numpy as np
SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "XRSound", "MPU")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)

def save(name, x, peak=0.85):
    x = x / (np.max(np.abs(x)) + 1e-9) * peak
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())

def loop_noise(n, lo, hi, tilt=1.0):
    f = np.fft.rfftfreq(n, 1 / SR); spec = np.zeros(len(f), complex)
    band = (f >= lo) & (f <= hi)
    spec[band] = (rng.normal(size=band.sum()) + 1j * rng.normal(size=band.sum())) / np.maximum(f[band], 1.0) ** (0.5 * tilt)
    return np.fft.irfft(spec, n)

def snap(f, n): return round(f * n / SR) * SR / n            # a frequency that closes the loop

def motor(n, f0, inverter, growl):
    """a heavy electric drive: the fundamental and strong low harmonics (odd ones louder - the stator), a slow
    amplitude ripple (pole passing), a rumble under load, a faint inverter tone"""
    t = np.arange(n) / SR; x = np.zeros(n)
    for k, a in ((1, 1.0), (2, 0.55), (3, 0.70), (4, 0.25), (5, 0.40), (6, 0.12), (7, 0.18), (9, 0.08)):
        x += a * np.sin(2 * np.pi * snap(f0 * k, n) * t + rng.uniform(0, 2 * np.pi))
    ripple = 1.0 + growl * np.sin(2 * np.pi * snap(f0 / 4, n) * t)            # the growl: pole passing
    x *= ripple
    x += 0.55 * loop_noise(n, 20, 160, 1.6)                                     # load rumble (deep)
    x += inverter * np.sin(2 * np.pi * snap(inverter_f(f0), n) * t)            # the inverter, faint
    return np.tanh(1.4 * x / np.max(np.abs(x))) # a little saturation: weight, not buzz

def inverter_f(f0): return 14 * f0

N = SR * 3
# ---- the traction drive (2026-10-08, the user: «not an electric motor - a grinder»): XRSound 3 sets the playback
# speed, so the drive is ONE clean loop recorded at 40 km/h and the module plays it faster or slower with the speed.
# A permanent-magnet hub motor through a 1:12 reduction, 8 pole pairs, the wheel D1.6 m: at 40 km/h the wheel turns
# 133 rpm, the motor 1590 rpm, its electrical frequency 212 Hz; the gear mesh (30 teeth) 795 Hz. Clean tones - the
# magnetic orders 1, 2, 6, 12 and the gear's whine with its sidebands; a slight pole ripple; no saturation (that buzz
# was the "grinder"). 8 motors slightly apart in tune (manufacturing spread) give the body of a big drive.
N = SR * 4
t = np.arange(N) / SR
def tone(f, a): return a * np.sin(2 * np.pi * snap(f, N) * t + rng.uniform(0, 2 * np.pi))
drv = np.zeros(N)
for m in range(8):                                   # eight motors, each a hair off the others
    d = 1.0 + (m - 3.5) * 0.0018
    fe = 212.0 * d
    drv += tone(fe, 1.0) + tone(2 * fe, 0.32) + tone(6 * fe, 0.10) + tone(12 * fe, 0.03)
    fg = 795.0 * d
    drv += tone(fg, 0.22) + tone(fg - fe / 8, 0.05) + tone(fg + fe / 8, 0.05)
drv *= 1.0 + 0.04 * np.sin(2 * np.pi * snap(212.0 / 8, N) * t)
drv += 0.10 * loop_noise(N, 300, 3000, 1.2) / np.max(np.abs(loop_noise(N, 300, 3000, 1.2)) + 1e-9) * np.max(np.abs(drv)) / 3.0   # the cooling air
save("drive.wav", drv, 0.8)
# the inverter's song on the start: a switching carrier (1.05 kHz) with its sidebands at twice a low electrical
# frequency - the whistle of a starting tram; the module fades it out as the speed grows
inv = tone(1050.0, 1.0) + tone(1050.0 - 2 * 18, 0.45) + tone(1050.0 + 2 * 18, 0.45) + tone(2100.0, 0.25) + tone(3150.0, 0.08)
save("inverter.wav", inv, 0.6)
# the power's hum: what hundreds of kilowatts sound like - the magnetostriction of the motors' iron and the inverter bus's
# choke at 100 Hz and its harmonics (twice a 50 Hz frame), a slow beating between the eight, and a deep air-cooling rush
hum = np.zeros(N)
for k, (f, a_) in enumerate(((100.0, 1.0), (200.0, 0.55), (300.0, 0.35), (400.0, 0.18), (50.0, 0.45))):
    for dd in (-0.4, 0.0, 0.5):
        hum += a_ * np.sin(2 * np.pi * snap(f + dd, N) * t + rng.uniform(0, 2 * np.pi))
hum *= 1.0 + 0.12 * np.sin(2 * np.pi * snap(0.75, N) * t)
hum += 0.9 * loop_noise(N, 25, 140, 1.2) / np.max(np.abs(loop_noise(N, 25, 140, 1.2)) + 1e-9) * np.max(np.abs(hum)) * 0.25
save("hum.wav", hum, 0.8)
# rolling: low rumble of the ground through the wheels, the tread blocks ticking
t = np.arange(N) / SR
ticks = (np.sin(2 * np.pi * snap(14, N) * t) > 0.97).astype(float) * loop_noise(N, 200, 1500)
save("roll.wav", loop_noise(N, 18, 300, 1.4) + 0.25 * ticks)
# a suspension knock: a short heavy thud
n = int(SR * 0.4); tt = np.arange(n) / SR
save("knock.wav", (np.sin(2 * np.pi * 48 * tt) + 0.4 * rng.normal(size=n) * np.exp(-tt * 50)) * np.exp(-tt * 10), 0.95)
# the platform's lift drive: a geared servo, low
save("lift.wav", motor(N, 75.0, 0.02, 0.10) * 0.8, 0.6)

# ================= the rest of the machine's sounds (2026-10-08): heavy, steel and gravel =================
def nz(n, lo, hi, tilt=0.0):
    x = loop_noise(n, lo, hi, tilt); return x / (np.max(np.abs(x)) + 1e-9)

def ringing(n, freqs, taus, amps, t0=0.0):
    """struck metal: damped sinusoids (a body's modes), starting at t0 seconds, in a buffer of n samples"""
    out = np.zeros(n); k0 = int(t0 * SR)
    if k0 >= n: return out
    tt = np.arange(n - k0) / SR
    for f, tau, a_ in zip(freqs, taus, amps):
        out[k0:] += a_ * np.sin(2 * np.pi * f * tt + rng.uniform(0, 2 * np.pi)) * np.exp(-tt / tau)
    return out

def burst(n, t0, dur, lo, hi, tau):
    """a short burst of band noise (a grating, a seal, a splash of gravel)"""
    out = np.zeros(n); k0 = int(t0 * SR); m = min(int(dur * SR), n - k0)
    if m <= 0: return out
    out[k0:k0 + m] = nz(m, lo, hi) * np.exp(-np.arange(m) / SR / tau)
    return out

# ---- the brake on the ground: the pads and the wheels on the regolith - a deep drone with its beat (the stick-slip of
# heavy shoes), a gravel crackle, a load of low noise; looped, the module plays it at the speed's rate (slower = heavier)
n3 = SR * 3; t3 = np.arange(n3) / SR
stick = 0.5 * (1.0 + np.sin(2 * np.pi * snap(6.0, n3) * t3))
drone = (np.sin(2 * np.pi * snap(46.0, n3) * t3) * (0.7 + 0.3 * np.sin(2 * np.pi * snap(2.0, n3) * t3)) + 0.5 * np.sin(2 * np.pi * snap(92.0, n3) * t3 + 1.0))
grind = nz(n3, 90, 1000, 0.8) * (0.5 + 0.5 * stick)
imp = np.zeros(n3); imp[rng.integers(0, n3, size=2400)] = rng.uniform(0.3, 1.0, size=2400)
kern = np.zeros(n3); kk = int(0.006 * SR); kern[:kk] = nz(kk, 400, 4500) * np.exp(-np.arange(kk) / SR / 0.0018)
crackle = np.fft.irfft(np.fft.rfft(imp) * np.fft.rfft(kern), n3)
brake = 0.9 * nz(n3, 28, 200, 1.0) + 0.8 * grind + 0.7 * drone + 0.9 * crackle / (np.max(np.abs(crackle)) + 1e-9) + 0.12 * nz(n3, 1500, 5000, 0.3)
save("brake.wav", np.tanh(1.1 * brake / np.max(np.abs(brake))), 0.9)

# ---- the ladder: a geared servo (a rising whine, a deep gear rumble, the chain's ratchet) and the stop's heavy clunk
def ladder(up):
    n = int(2.1 * SR); tt = np.arange(n) / SR
    f = (650.0 - 300.0 * tt / 1.5) if up else (350.0 + 300.0 * tt / 1.5)
    ph = 2 * np.pi * np.cumsum(f) / SR
    on = ((tt > 0.05) & (tt < 1.5)).astype(float) * np.minimum(1.0, (tt - 0.05) / 0.1) * np.minimum(1.0, (1.5 - tt) / 0.12)
    x = on * 0.75 * nz(n, 30, 190, 1.0)                                       # the gear's rumble only - no whistling tone
    for k in np.arange(0.1, 1.45, 1.0 / 26.0):                               # the ratchet's clicks
        x += ringing(n, [1500.0, 3400.0], [0.007, 0.005], [0.35 * rng.uniform(0.6, 1.0), 0.15], k)
    clunk = ringing(n, [92.0, 240.0, 610.0, 1450.0], [0.22, 0.12, 0.08, 0.05], [1.0, 0.55, 0.35, 0.2], 1.52)
    x += (0.7 if up else 1.0) * clunk + burst(n, 1.52, 0.25, 80, 900, 0.05) * 0.6
    return x
save("ladder_dn.wav", ladder(False), 0.9); save("ladder_up.wav", ladder(True), 0.85)

# ---- the airlock's cycle: the outer hatch's latches, the seal's hiss, the swing's thud, the pump spooling up and the
# pressure hissing out, the valve, the inner latch
n = int(6.2 * SR); tt = np.arange(n) / SR
x = ringing(n, [210.0, 540.0, 1300.0, 2900.0], [0.18, 0.12, 0.08, 0.05], [1.0, 0.6, 0.4, 0.2], 0.0)
x += 0.55 * burst(n, 0.35, 1.3, 1500, 7000, 0.45)
x += ringing(n, [66.0, 140.0, 330.0], [0.32, 0.2, 0.12], [1.0, 0.6, 0.3], 1.25) + 0.5 * burst(n, 1.25, 0.4, 40, 400, 0.12)
pump = np.zeros(n); m = (tt > 1.9) & (tt < 4.2)
fp = 55.0 + 40.0 * np.clip((tt - 1.9) / 1.2, 0, 1)
php = 2 * np.pi * np.cumsum(fp) / SR
ramp = np.clip((tt - 1.9) / 0.8, 0, 1) * np.clip((4.2 - tt) / 0.5, 0, 1)
pump = ramp * (0.5 * np.sin(php) + 0.25 * np.sin(2 * php) + 0.18 * np.sin(3 * php))
x += 0.9 * pump + 0.35 * ramp * nz(n, 600, 4200, 0.4)
x += ringing(n, [150.0, 420.0, 980.0], [0.1, 0.07, 0.05], [0.8, 0.5, 0.3], 4.25)          # the valve
x += ringing(n, [190.0, 500.0, 1250.0, 2700.0], [0.16, 0.1, 0.07, 0.04], [1.0, 0.6, 0.4, 0.2], 4.55) + 0.5 * burst(n, 4.6, 0.9, 1500, 6000, 0.3)
x += ringing(n, [60.0, 130.0], [0.3, 0.18], [1.0, 0.55], 5.1)
save("airlock.wav", x, 0.9)

# ---- switches: a heavy industrial toggle (the click's high ring over a low thump), and a keypad's soft tick
n = int(0.22 * SR); c = ringing(n, [1900.0, 3700.0, 5200.0], [0.012, 0.008, 0.005], [1.0, 0.5, 0.3]) + 1.2 * ringing(n, [140.0, 260.0], [0.045, 0.03], [1.0, 0.4]) + 0.6 * burst(n, 0.0, 0.02, 800, 6000, 0.004)
save("click.wav", c, 0.8)
n = int(0.09 * SR); save("tick.wav", ringing(n, [3000.0, 5600.0], [0.006, 0.004], [1.0, 0.4]) + 0.3 * ringing(n, [400.0], [0.012], [1.0]), 0.55)

# ---- a crash: the boom, the sheet metal's clang, the debris
n = int(2.6 * SR); tt = np.arange(n) / SR
x = 1.3 * np.sin(2 * np.pi * 46.0 * tt) * np.exp(-tt / 0.45) + 1.0 * nz(n, 35, 260, 1.0) * np.exp(-tt / 0.22)
x += ringing(n, [183.0, 340.0, 780.0, 1250.0, 2100.0, 3300.0], [0.7, 0.5, 0.45, 0.3, 0.2, 0.12], [0.55, 0.5, 0.4, 0.3, 0.2, 0.12], 0.01)
for k in np.sort(rng.uniform(0.04, 1.2, size=45)):
    x += rng.uniform(0.15, 0.5) * burst(n, k, 0.05, 300, 5000, 0.012)
save("crash.wav", np.tanh(1.2 * x / np.max(np.abs(x))), 0.95)

# ---- the coupling: the pin driven into the jaw (a thud, the steel's long ring), the latch's click; the pin drawn out
n = int(1.6 * SR); tt = np.arange(n) / SR
x = ringing(n, [85.0, 190.0], [0.12, 0.08], [1.0, 0.6]) + ringing(n, [620.0, 1710.0, 3200.0], [0.55, 0.3, 0.15], [0.7, 0.45, 0.25], 0.0) + 0.5 * burst(n, 0.0, 0.04, 500, 6000, 0.008)
x += ringing(n, [2400.0, 4100.0], [0.02, 0.012], [0.5, 0.3], 0.14)
save("couple.wav", x, 0.9)
n = int(1.3 * SR); tt = np.arange(n) / SR
slide = burst(n, 0.0, 0.35, 400, 3200, 0.2) * np.clip(tt / 0.1, 0, 1)
x = 0.6 * slide + ringing(n, [150.0, 520.0, 1450.0], [0.2, 0.15, 0.09], [0.8, 0.5, 0.3], 0.36) + 0.4 * burst(n, 0.36, 0.1, 500, 4000, 0.02)
save("uncouple.wav", x, 0.8)

# ---- the warning (three firm tones) and the reverse beeper
n = int(1.1 * SR); tt = np.arange(n) / SR; x = np.zeros(n)
for k in range(3):
    seg = (tt >= k * 0.3) & (tt < k * 0.3 + 0.18)
    tone = np.sin(2 * np.pi * 880.0 * tt) + 0.4 * np.sin(2 * np.pi * 2640.0 * tt)
    env = np.minimum(1.0, (tt - k * 0.3) / 0.01) * np.minimum(1.0, (k * 0.3 + 0.18 - tt) / 0.02)
    x += seg * tone * env
save("alarm.wav", np.tanh(1.4 * x), 0.75)
n = int(0.30 * SR); tt = np.arange(n) / SR
save("beep.wav", (np.sin(2 * np.pi * 1000.0 * tt) + 0.3 * np.sin(2 * np.pi * 3000.0 * tt)) * np.minimum(1.0, tt / 0.01) * np.minimum(1.0, (0.22 - tt).clip(0) / 0.02) * (tt < 0.22), 0.65)

# ---- the wind round the body, looped: a gusting low band over a hiss
n4 = SR * 4; t4 = np.arange(n4) / SR
gust = 1.0 + 0.35 * np.sin(2 * np.pi * snap(0.5, n4) * t4) + 0.2 * np.sin(2 * np.pi * snap(1.25, n4) * t4 + 1.0)
save("wind.wav", (nz(n4, 60, 900, 1.0) * gust + 0.3 * nz(n4, 900, 3600, 0.5) * gust))
print("written to", os.path.abspath(OUT), sorted(os.listdir(OUT)))
