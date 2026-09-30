"""OrbiterCrew: build the crew sound set from CC0 recordings.

Sources (Tantra_Design/assets/audio/src, see SOURCES.txt there; all CC0):
  Kenney "Impact Sounds"            footsteps on concrete, heavy soft impacts
  OwlishMedia "Sound Effects Pack"  hard-floor steps, running steps, breathing recovery (female, male)

Output (Orbiter root, loaded through XRSound): XRSound/OrbiterCrew/
  steps/walk_NN.wav     single steps, coverall / boots on a hard floor
  steps/run_NN.wav      single running steps (cut from continuous running recordings)
  steps/suit_NN.wav     suit boots in air: lower, heavier, with a body thud
  steps/suitvac_NN.wav  the same heard from inside the suit in vacuum: only what the body conducts
  voice/<voice>/breath_NN.wav         one exhale each, loudness-normalised; intensity listed in breath.txt
  voice/<voice>/helmet_breath_NN.wav  the same inside a closed helmet (close early reflections)
  suit/fan.wav          suit ventilation loop

Run: python build_crew_sounds.py
"""
import glob
import os
import zipfile
import io

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt, find_peaks, resample_poly

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "assets", "audio", "src")
ORBITER = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ORBITER, "XRSound", "OrbiterCrew")
SR = 44100
rng = np.random.default_rng(37)


def load(path_or_file, name=None):
    d, sr = sf.read(path_or_file, always_2d=True)
    x = d.mean(1)
    if sr != SR:
        g = np.gcd(sr, SR)
        x = resample_poly(x, SR // g, sr // g)
    return x


def kenney(name):
    z = zipfile.ZipFile(os.path.join(SRC, "kenney_impact-sounds.zip"))
    return load(io.BytesIO(z.read("Audio/" + name + ".ogg")))


def owl(name):
    return load(os.path.join(SRC, "owlish", name + ".wav"))


def lp(x, f, order=4):
    return sosfilt(butter(order, f, "low", fs=SR, output="sos"), x)


def hp(x, f, order=2):
    return sosfilt(butter(order, f, "high", fs=SR, output="sos"), x)


def slow(x, factor):
    """play 'factor' times slower: lower and longer, like a heavier body"""
    up = int(round(100 * factor))
    return resample_poly(x, up, 100)


def env(x, win=0.02):
    n = max(1, int(SR * win))
    return np.sqrt(np.convolve(x * x, np.ones(n) / n, "same"))


def trim(x, rel=0.02, pre=0.01, fade=0.03):
    """cut silence around the sound, short fade in, gentle fade out"""
    e = env(x, 0.005)
    on = np.nonzero(e > rel * e.max())[0]
    if len(on) == 0:
        return x
    a = max(0, on[0] - int(pre * SR)); b = min(len(x), on[-1] + int(fade * SR))
    y = x[a:b].copy()
    fi = min(len(y), int(0.003 * SR)); fo = min(len(y), int(fade * SR))
    y[:fi] *= np.linspace(0, 1, fi)
    y[-fo:] *= np.linspace(1, 0, fo) ** 2
    return y


def norm_peak(x, peak=0.7):
    m = np.abs(x).max()
    return x * (peak / m) if m > 0 else x


def norm_rms(x, rms=0.08, peak_cap=0.9):
    e = env(x, 0.05).max()
    y = x * (rms / e) if e > 0 else x
    m = np.abs(y).max()
    return y * (peak_cap / m) if m > peak_cap else y


def save(rel, x):
    path = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sf.write(path, np.clip(x, -1, 1).astype(np.float32), SR, subtype="PCM_16")


def cut_onsets(x, min_gap=0.22, length=0.32, rel=0.25):
    """split a continuous recording of steps into single steps at the transients"""
    e = env(hp(x, 120), 0.004)
    pk, _ = find_peaks(e, height=rel * e.max(), distance=int(min_gap * SR))
    out = []
    for p in pk:
        a = max(0, p - int(0.015 * SR)); b = min(len(x), p + int(length * SR))
        out.append(trim(x[a:b], rel=0.01))
    return out


def variants(base, n, spread=0.06):
    """a few more takes from each step: slight speed (pitch) and tone changes, never identical"""
    out = []
    for k in range(n):
        f = 1 + rng.uniform(-spread, spread)
        y = slow(base, f)
        if rng.random() < 0.5:
            y = lp(y, rng.uniform(5000, 9000), 2)
        out.append(y)
    return out


def build_steps():
    walk = [kenney("footstep_concrete_%03d" % i) for i in range(5)]
    walk += [owl("hard-footstep%d" % i) for i in range(1, 5)]
    walk += [owl("step%d" % i) for i in range(1, 5)]
    walk = [norm_peak(trim(hp(w, 60))) for w in walk]
    allw = []
    for w in walk:
        allw += [w] + variants(w, 1)
    for i, w in enumerate(allw):
        save("steps/walk_%02d.wav" % i, norm_peak(w))

    run = []
    for f in ("running-shoes-1", "running-shoes-2"):
        run += cut_onsets(owl(f))
    run = [norm_peak(hp(r, 60)) for r in run if len(r) > 0.08 * SR]
    for i, r in enumerate(run):
        save("steps/run_%02d.wav" % i, r)

    thuds = [kenney("impactSoft_heavy_%03d" % i) for i in range(5)]
    suit = []
    for i, w in enumerate(allw):
        s = slow(w, rng.uniform(1.22, 1.32))                   # heavier foot: lower and longer
        s = lp(s, 3000, 2)
        t = lp(slow(thuds[i % 5], 1.45), 260, 4)                # the body's mass landing, felt more than heard
        n = max(len(s), len(t)); y = np.zeros(n)
        y[:len(s)] += norm_peak(s, 0.7); y[:len(t)] += norm_peak(t, 0.55)
        y = trim(y, rel=0.01, fade=0.06)
        suit.append(norm_peak(y))
        save("steps/suit_%02d.wav" % i, suit[-1])
        vac = lp(y, 320, 4) + 0.15 * lp(hp(y, 900), 2500, 2)    # through the boot, the legs and the suit
        save("steps/suitvac_%02d.wav" % i, norm_peak(trim(vac, rel=0.01, fade=0.08), 0.8))
    return len(allw), len(run), len(suit)


def breaths_from(x, min_dist=0.45):
    """one segment per exhale: from the valley before the peak to where it has died away"""
    e = env(x, 0.03)
    pk, _ = find_peaks(e, prominence=0.03 * e.max(), distance=int(min_dist * SR))
    out = []
    for j, p in enumerate(pk):
        lo = pk[j - 1] if j > 0 else 0
        a = lo + int(np.argmin(e[lo:p])) if p > lo else 0
        b = p
        while b < len(e) - 1 and e[b] > 0.12 * e[p] and b - p < int(0.9 * SR):
            b += 1
        seg = x[a:b + int(0.05 * SR)].copy()
        fo = min(len(seg), int(0.08 * SR)); seg[-fo:] *= np.linspace(1, 0, fo) ** 2
        fi = min(len(seg), int(0.02 * SR)); seg[:fi] *= np.linspace(0, 1, fi)
        out.append((e[p], seg))
    return out


def helmet(x):
    """inside a closed helmet: very close reflections (1-5 ms), a little more body, less air"""
    y = x.copy()
    for d, g in ((0.0012, 0.35), (0.0029, 0.25), (0.0044, 0.16), (0.0061, 0.1)):
        k = int(d * SR); y[k:] += g * x[:-k]
    y = lp(y, 7000, 2) + 0.25 * lp(y, 400, 2)
    return y


def build_voice(name, sources):
    items = []
    for x in sources:
        segs = breaths_from(hp(x, 80))
        top = max(l for l, _ in segs)
        items += [(l / top, s) for l, s in segs]
    lines = []
    for i, (inten, s) in enumerate(sorted(items, key=lambda t: -t[0])):
        s = norm_rms(s, 0.08)
        save("voice/%s/breath_%02d.wav" % (name, i), s)
        save("voice/%s/helmet_breath_%02d.wav" % (name, i), norm_rms(helmet(s), 0.08))
        lines.append("%02d %.3f %.3f" % (i, inten, len(s) / SR))
    with open(os.path.join(OUT, "voice", name, "breath.txt"), "w") as f:
        f.write("; index  intensity(0..1, 1 = hardest)  length s\n" + "\n".join(lines) + "\n")
    return len(items)


def build_fan():
    n = int(4.0 * SR)
    w = rng.standard_normal(n + SR)
    pink = np.cumsum(w); pink -= np.convolve(pink, np.ones(2000) / 2000, "same")   # rough 1/f
    y = lp(hp(pink, 150), 2500, 2)
    y = y[SR // 2: SR // 2 + n]
    x = int(0.5 * SR)                                    # crossfade the ends into a seamless loop
    y[:x] = y[:x] * np.linspace(0, 1, x) + y[-x:] * np.linspace(1, 0, x)
    y = y[:-x]
    save("suit/fan.wav", norm_rms(y, 0.05))


if __name__ == "__main__":
    for old in glob.glob(os.path.join(OUT, "**", "*.wav"), recursive=True):
        os.remove(old)
    nw, nr, ns = build_steps()
    nf = build_voice("female1", [owl("breath-female"), owl("breath-female2")])
    nm = build_voice("male1", [owl("breath-male")])
    build_fan()
    print("walk %d  run %d  suit %d  female1 %d  male1 %d" % (nw, nr, ns, nf, nm))
