# The impeller jet pack «Вихрь»'s sounds (OrbiterCrew Sound.cpp): two seamless 4 s loops, synthesised in the frequency
# domain on the loop's own frequency grid (every component a whole number of cycles in the loop: no click at the seam).
#   XRSound\OrbiterCrew\jet\fan.wav   - (2026-10-16: the honeycomb cells, see below) the ducted fans and the turboshaft at full speed: the blade passing tone (7 blades,
#     ~150 rev/s: 1050 Hz and its harmonics), the shaft's hum, the broadband rush of the duct, the turbine's whine high up.
#     The module plays it slower with the rotors' speed and the gas (the tips are held to 0.85 of the speed of sound).
#   XRSound\OrbiterCrew\jet\boost.wav - the hubs' rocket boost: a hot hydrogen jet's roar with its crackle
# Run: python build_jetfan_sounds.py
import os, wave
import numpy as np
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "XRSound", "OrbiterCrew", "jet")
SR, SEC = 44100, 4.0
N = int(SR * SEC)
rng = np.random.default_rng(16)
f = np.fft.rfftfreq(N, 1.0 / SR)

def shaped_noise(gain):                                   # noise with the magnitude spectrum gain(f), periodic in the loop
    spec = gain(f) * np.exp(2j * np.pi * rng.random(f.size)); spec[0] = 0
    x = np.fft.irfft(spec, N)
    return x / (np.max(np.abs(x)) + 1e-12)

def tone(freq, amp, jitter=0.0):                           # a partial on the loop's grid (whole cycles), a slow wobble
    k = max(1, round(freq * SEC)); fr = k / SEC
    t = np.arange(N) / SR
    wob = 1.0 + jitter * np.sin(2 * np.pi * (1 / SEC) * 3 * t + rng.random() * 6.28)
    return amp * wob * np.sin(2 * np.pi * fr * t + rng.random() * 6.28)

def band(fc, bw):
    return lambda fr: np.exp(-0.5 * ((fr - fc) / bw) ** 2)

def save(name, x, peak=0.85):
    x = x / (np.max(np.abs(x)) + 1e-12) * peak
    os.makedirs(OUT, exist_ok=True)
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype("<i2").tobytes())
    print(name, len(x) / SR, "s")

# ---- the cells («Соты»): 70 small rotors (5 blades, ~500 rev/s) - a choir of close blade-passing tones round 2.5 kHz
# beating against each other, their harmonics, the air's hiss through the honeycomb, the power unit's low hum ----
rush = shaped_noise(lambda fr: (fr > 80) * (band(3200, 2200)(fr) + 0.35 * band(900, 500)(fr)))
fan = 0.50 * rush
for k in range(18):                                        # the cells' tones, a little apart (each its own speed)
    f0 = 2500.0 * (1 + rng.normal(0, 0.025))
    fan += tone(f0, 0.10, jitter=0.06) + tone(2 * f0, 0.035, jitter=0.04)
fan += tone(60.0, 0.10, 0.02) + tone(180.0, 0.05)          # the power unit's generator hum
fan += 0.08 * shaped_noise(band(7500, 2500))               # the cells' high hiss
save("fan.wav", fan)

# ---- the boost: a roar (low, broad) and its crackle (short random pops), a little of the hiss of the hot gas ----
roar = shaped_noise(lambda fr: (fr > 25) * (fr / 120.0) / (1 + (fr / 120.0) ** 1.8))
hiss = shaped_noise(band(3000, 1800))
crack = np.zeros(N)
for _ in range(int(SEC * 90)):
    i = rng.integers(0, N - 400); L = rng.integers(40, 300)
    crack[i:i + L] += rng.normal(0, 1, L) * np.exp(-np.arange(L) / (L / 4)) * rng.uniform(0.3, 1.0)
crack = np.fft.irfft(np.fft.rfft(crack) * (f > 400), N)
boost = 0.8 * roar + 0.18 * hiss + 0.35 * crack / (np.max(np.abs(crack)) + 1e-12)
save("boost.wav", boost)
