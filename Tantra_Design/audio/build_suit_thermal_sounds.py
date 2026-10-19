# The suit's machinery heard inside the helmet (Orbitersdk/OrbiterCrew/HEAVY_SUIT.md), synthesised (no recordings):
#   suit/compressor.wav  the heat pump's compressor: a motor's hum with its harmonics, the pistons' chug, a thin whine
#                        (looped; the module raises its volume and its speed as the COP falls in heat - it labours)
#   suit/evaporator.wav  water boiling off: a hiss with bubbles bursting (looped)
#   suit/coils.wav       the field's coils: a transformer hum with a little buzz (looped)
#   suit/creak_01..06    the shell's layers working against each other in deep cold or strong heat: ticks and creaks
# Run: python build_suit_thermal_sounds.py   -> XRSound\OrbiterCrew\suit\
import os, wave
import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "XRSound", "OrbiterCrew", "suit")
SR = 44100
rng = np.random.default_rng(16102026)

def save(name, x, peak=0.6):
    x = np.asarray(x, dtype=np.float64)
    x = x / (np.max(np.abs(x)) + 1e-9) * peak
    os.makedirs(OUT, exist_ok=True)
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())

def band(x, lo, hi):
    """a band-pass by FFT (brick wall, soft edges)"""
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR)
    m = np.clip((f - lo * 0.8) / (lo * 0.2 + 1e-9), 0, 1) * np.clip((hi * 1.2 - f) / (hi * 0.2 + 1e-9), 0, 1)
    return np.fft.irfft(X * m, len(x))

def loop_safe(x, fade=0.05):
    """make a loop seamless: the tail cross-faded into the head"""
    n = int(fade * SR); x = x.copy()
    ramp = np.linspace(0, 1, n)
    x[:n] = x[:n] * ramp + x[-n:] * (1 - ramp)
    return x[:-n]

def compressor(sec=4.0):
    t = np.arange(int(sec * SR)) / SR
    f0 = 30.0                                                     # the motor, heavy and slow (whole periods over the loop)
    hum = sum(a * np.sin(2 * np.pi * f0 * k * t + rng.uniform(0, 6)) for k, a in ((1, 1.0), (2, 0.6), (3, 0.35), (4, 0.2), (6, 0.1)))
    chug = 0.45 + 0.55 * np.clip(np.sin(2 * np.pi * 6.25 * t), 0, None) ** 4   # the pistons, 375 rpm - a heavy, slow beat
    whine = 0.05 * np.sin(2 * np.pi * 640 * t + 3 * np.sin(2 * np.pi * 0.5 * t))
    hiss = 0.18 * band(rng.standard_normal(len(t)), 150, 1500) * chug
    return loop_safe(hum * chug * 0.5 + whine + hiss)

def evaporator(sec=5.0):
    n = int(sec * SR); t = np.arange(n) / SR
    hiss = band(rng.standard_normal(n), 1500, 9000) * (0.7 + 0.3 * np.sin(2 * np.pi * 0.31 * t))
    bub = np.zeros(n)
    for _ in range(int(sec * 35)):                               # bubbles: short damped chirps
        i = rng.integers(0, n - 2000); f = rng.uniform(500, 1600); L = rng.integers(400, 1500)
        k = np.arange(L) / SR
        bub[i:i + L] += rng.uniform(0.2, 1.0) * np.sin(2 * np.pi * f * k * (1 + 4 * k)) * np.exp(-k * rng.uniform(80, 200))
    return loop_safe(0.6 * hiss + 0.5 * bub)

def coils(sec=3.0):
    t = np.arange(int(sec * SR)) / SR
    hum = np.sin(2 * np.pi * 50 * t) + 0.7 * np.sin(2 * np.pi * 100 * t) + 0.3 * np.sin(2 * np.pi * 150 * t)
    buzz = 0.15 * np.sign(np.sin(2 * np.pi * 100 * t)) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.7 * t))
    return loop_safe(band(hum + buzz, 40, 1800))

def creak(k):
    L = int(rng.uniform(0.15, 0.6) * SR); t = np.arange(L) / SR
    if k % 2:   # a tick: the shell's panel snapping between its layers
        x = band(rng.standard_normal(L), 800, 6000) * np.exp(-t * 60)
        x += 0.6 * np.sin(2 * np.pi * rng.uniform(300, 700) * t) * np.exp(-t * 25)
    else:       # a creak: stick-slip of the layers, a rough low tone with jitter
        f = rng.uniform(90, 220) * (1 + 0.15 * np.sin(2 * np.pi * rng.uniform(3, 9) * t))
        ph = 2 * np.pi * np.cumsum(f) / SR
        x = np.sign(np.sin(ph)) * (0.5 + 0.5 * rng.random(L)) * np.sin(np.pi * t / t[-1]) ** 2
        x = band(x, 120, 3000)
    fade = np.ones(L); fade[-200:] = np.linspace(1, 0, 200)
    return x * fade

save("compressor.wav", compressor(), 0.5)
save("evaporator.wav", evaporator(), 0.5)
save("coils.wav", coils(), 0.4)
for k in range(6): save("creak_%02d.wav" % (k + 1), creak(k), 0.6)
print("written to", OUT)
