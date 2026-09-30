"""Synthesize Tantra sounds for OrbiterSound (22050 Hz, 16-bit mono PCM).

Loops are built to be seamless: tones use frequencies that fit the loop length
and noise is generated as a periodic signal (random-phase spectrum).
Descriptions follow Efremov: the anamezon engines give a barely audible,
hard-to-bear high-frequency vibration; the ion-trigger (planetary) motors
give ringing blows turning into continuous thunder.
"""
import os
import sys
import wave

import numpy as np

SR = 22050
rng = np.random.default_rng(37)


def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def periodic_noise(n, lo, hi):
    """Band-limited noise that loops seamlessly over n samples."""
    spec = np.zeros(n // 2 + 1, complex)
    f = np.fft.rfftfreq(n, 1 / SR)
    band = (f >= lo) & (f <= hi)
    spec[band] = np.exp(2j * np.pi * rng.random(band.sum()))
    x = np.fft.irfft(spec, n)
    return x / (np.abs(x).max() or 1)


def loop_tone(t, f, dur):
    f = round(f * dur) / dur  # snap to the loop length
    return np.sin(2 * np.pi * f * t)


def fade(x, fin=0.01, fout=0.05):
    n_in, n_out = int(fin * SR), int(fout * SR)
    if n_in:
        x[:n_in] *= np.linspace(0, 1, n_in)
    if n_out:
        x[-n_out:] *= np.linspace(1, 0, n_out)
    return x


def save(path, x, peak=0.9):
    x = x / (np.abs(x).max() or 1) * peak
    data = (x * 32767).astype("<i2").tobytes()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data)


def ana_run():
    dur = 2.0
    t = t_axis(dur)
    whine = loop_tone(t, 6200, dur) * (0.6 + 0.4 * loop_tone(t, 7, dur))
    whine += 0.5 * loop_tone(t, 3100, dur) + 0.25 * loop_tone(t, 9300, dur)
    hiss = periodic_noise(len(t), 4000, 10000)
    buzz = np.sign(loop_tone(t, 40, dur)) * 0.3 * periodic_noise(len(t), 60, 400)  # pulse train of micro-explosions
    rumble = loop_tone(t, 55, dur) * (0.7 + 0.3 * loop_tone(t, 3, dur))
    return 0.35 * whine + 0.25 * hiss + 0.35 * buzz + 0.45 * rumble


def ion_run():
    dur, rate = 2.4, 5.0  # 12 blows per loop
    n = int(dur * SR)
    t = t_axis(dur)
    out = 0.35 * periodic_noise(n, 30, 250)  # thunder bed
    partials = [(420, 1.0), (1130, 0.7), (2240, 0.5), (3390, 0.35), (4870, 0.2)]
    blow_len = int(0.35 * SR)
    tb = np.arange(blow_len) / SR
    clang = sum(a * np.sin(2 * np.pi * f * tb) * np.exp(-tb * (9 + f / 400)) for f, a in partials)
    clang += 1.5 * np.sin(2 * np.pi * 70 * tb) * np.exp(-tb * 18)
    for k in range(int(dur * rate)):
        start = int(k / rate * SR)
        idx = (start + np.arange(blow_len)) % n  # wrap the tail around the loop
        out[idx] += clang * (0.85 + 0.3 * rng.random())
    return out


def field_up():
    dur = 4.0
    t = t_axis(dur)
    f = 80 + 820 * (t / dur) ** 1.5
    phase = 2 * np.pi * np.cumsum(f) / SR
    spiral = 0.5 + 0.5 * np.sin(2 * np.pi * np.cumsum(2 + 14 * t / dur) / SR)  # quickening swirl
    x = (np.sin(phase) + 0.4 * np.sin(2 * phase)) * (0.4 + 0.6 * spiral) * np.minimum(1, t / 0.5)
    return fade(x, 0.02, 0.3)


def beam_up():
    dur = 2.0
    t = t_axis(dur)
    saw = 2 * ((150 * t) % 1) - 1
    crackle = (rng.random(len(t)) < 0.004 + 0.02 * t / dur) * rng.standard_normal(len(t))
    x = (0.5 * saw + 0.8 * crackle) * np.minimum(1, t / 0.3)
    return fade(x, 0.01, 0.2)


def ignite():
    dur = 1.5
    t = t_axis(dur)
    crack = rng.standard_normal(len(t)) * np.exp(-t * 60)
    ring = np.sin(2 * np.pi * 3000 * t) * np.exp(-t * 4) + 0.5 * np.sin(2 * np.pi * 6200 * t) * np.exp(-t * 3)
    boom = np.sin(2 * np.pi * 45 * t) * np.exp(-t * 5)
    return fade(1.2 * crack + 0.5 * ring + 0.9 * boom, 0.0, 0.2)


def siren():
    dur = 4.0
    t = t_axis(dur)
    f = 500 + 600 * (0.5 - 0.5 * np.cos(2 * np.pi * t / 2.0))  # two wails
    phase = 2 * np.pi * np.cumsum(f) / SR
    x = np.sin(phase) + 0.3 * np.sin(3 * phase)
    return fade(x, 0.05, 0.3)


def shutdown():
    dur = 2.5
    t = t_axis(dur)
    f = 800 + 5400 * np.exp(-t * 1.8)
    phase = 2 * np.pi * np.cumsum(f) / SR
    x = np.sin(phase) * np.exp(-t * 1.2)
    return fade(x, 0.0, 0.3)


def _tick(dur, base=1800.0):
    tb = np.arange(int(dur * SR)) / SR
    x = sum(a * np.sin(2 * np.pi * f * tb) * np.exp(-tb * (40 + f / 150)) for f, a in
            ((base, 1.0), (base * 2.76, 0.6), (base * 5.4, 0.3)))
    return x + 0.6 * np.sin(2 * np.pi * 140 * tb) * np.exp(-tb * 50)


def att_attack():
    """Attitude micro-motor: one ion-trigger pulse (replaces the stock gas puff)."""
    return fade(_tick(0.3), 0.0, 0.05)


def att_sustain():
    """Seamless 1 s loop of rapid ion-trigger pulses."""
    dur, rate = 1.0, 14.0
    n = int(dur * SR)
    out = 0.15 * periodic_noise(n, 200, 1500)
    tick = _tick(0.12)
    for k in range(int(dur * rate)):
        idx = (int(k / rate * SR) + np.arange(len(tick))) % n
        out[idx] += tick * (0.8 + 0.4 * rng.random())
    return out


SOUNDS = {
    "ana_run.wav": ana_run,
    "ion_run.wav": ion_run,
    "field_up.wav": field_up,
    "beam_up.wav": beam_up,
    "ignite.wav": ignite,
    "siren.wav": siren,
    "shutdown.wav": shutdown,
    "att_attack.wav": att_attack,
    "att_sustain.wav": att_sustain,
}

if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..", "..", "..")
    out = os.path.join(root, "Sound", "_CustomVesselsSounds", "Tantra")
    os.makedirs(out, exist_ok=True)
    for name, fn in SOUNDS.items():
        save(os.path.join(out, name), fn())
        print("wrote", name)
