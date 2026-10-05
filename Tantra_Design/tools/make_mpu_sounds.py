# МПУ sounds (synthesised, original): hub-motor whine low / high, tyre rolling rumble, a suspension knock, the platform's
# lift drive. Loops are seamless (whole periods in the loop length). -> XRSound\MPU\*.wav, 44.1 kHz mono 16 bit.
import os, wave
import numpy as np
SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "XRSound", "MPU")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)

def save(name, x, peak=0.8):
    x = x / (np.max(np.abs(x)) + 1e-9) * peak
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())

def loop_noise(n, lo, hi):
    """band-limited noise that loops: built in the frequency domain over exactly n samples"""
    f = np.fft.rfftfreq(n, 1 / SR); spec = np.zeros(len(f), complex)
    band = (f >= lo) & (f <= hi)
    spec[band] = (rng.normal(size=band.sum()) + 1j * rng.normal(size=band.sum())) / np.sqrt(np.maximum(f[band], 1.0) / lo)
    return np.fft.irfft(spec, n)

def tone_loop(n, f0, harm):
    t = np.arange(n) / SR; x = np.zeros(n)
    for k, a in harm:
        fk = round(f0 * k * n / SR) * SR / n            # snapped so the loop closes
        x += a * np.sin(2 * np.pi * fk * t + rng.uniform(0, 2 * np.pi))
    return x

N = SR * 2
# hub motors: an electric whine (the pole frequency and its harmonics) over a little bearing hiss
save("motor_lo.wav", tone_loop(N, 140, [(1, 1.0), (2, 0.45), (3, 0.25), (6, 0.12)]) + 0.15 * loop_noise(N, 200, 3000))
save("motor_hi.wav", tone_loop(N, 470, [(1, 1.0), (2, 0.35), (3, 0.18), (5, 0.08)]) + 0.12 * loop_noise(N, 800, 6000), 0.7)
# rolling: low rumble of the ground through the tyres, the chevrons ticking
t = np.arange(N) / SR
ticks = (np.sin(2 * np.pi * (round(18 * N / SR) * SR / N) * t) > 0.97).astype(float) * loop_noise(N, 300, 2500)
save("roll.wav", loop_noise(N, 25, 400) + 0.35 * ticks)
# a suspension knock: a short damped thud
n = int(SR * 0.35); t = np.arange(n) / SR
save("knock.wav", (np.sin(2 * np.pi * 70 * t) + 0.5 * rng.normal(size=n) * np.exp(-t * 60)) * np.exp(-t * 14), 0.9)
# the platform's lift drive: a geared servo hum
save("lift.wav", tone_loop(N, 220, [(1, 1.0), (2, 0.5), (4, 0.3)]) * (1 + 0.15 * np.sin(2 * np.pi * 4 * np.arange(N) / SR)) + 0.2 * loop_noise(N, 300, 4000), 0.6)
print("written to", os.path.abspath(OUT), sorted(os.listdir(OUT)))
