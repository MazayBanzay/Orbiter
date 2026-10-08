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
print("written to", os.path.abspath(OUT), sorted(os.listdir(OUT)))
