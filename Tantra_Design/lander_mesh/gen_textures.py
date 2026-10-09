# -*- coding: utf-8 -*-
"""Процедурные текстуры «Грани» (тёмные, сдержанные, без декора): out/textures/*.png + *.dds (Pillow, без сжатия).
Масштаб: 1 текстура = 2 × 2 м (UV — кубическая проекция по нормали грани в gen_lander.py)."""
import os
import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out", "textures")
os.makedirs(OUT, exist_ok=True)
N = 1024
rng = np.random.default_rng(7)


def noise(scale, octaves=4):
    """Плитчатый (бесшовный) шум суммой октав: периодическая интерполяция случайной сетки."""
    acc = np.zeros((N, N))
    amp = 1.0
    for o in range(octaves):
        k = scale * 2 ** o
        g = rng.random((k, k))
        im = Image.fromarray((g * 255).astype(np.uint8)).resize((N, N), Image.BICUBIC)
        # периодичность: смешиваем со сдвигом на полпериода
        a = np.asarray(im, float) / 255
        acc += amp * a
        amp *= 0.5
    acc -= acc.min(); acc /= acc.max()
    return acc


def save(name, rgb):
    img = Image.fromarray(np.clip(rgb * 255, 0, 255).astype(np.uint8), "RGB")
    img.save(os.path.join(OUT, name + ".png"))
    img.save(os.path.join(OUT, name + ".dds"))


def seams(period_px, width_px, depth):
    m = np.ones((N, N))
    idx = np.arange(N)
    line = ((idx % period_px) < width_px)
    m[line, :] -= depth; m[:, line] -= depth
    return np.clip(m, 0, 1)


def tint(base, var, n):
    return np.stack([base[i] * (1 + var * (n - 0.5)) for i in range(3)], -1)


# 1. боразоно-циркониевый лак (корпус, верх, крылья): тёмный сине-серый, едва заметная «апельсиновая корка», швы панелей 2 м (край текстуры)
n = noise(8) * 0.6 + noise(64, 2) * 0.4
s = seams(N, 3, 0.25)
save("lak_korpus", tint((0.115, 0.123, 0.146), 0.10, n) * s[..., None])
# 2. борная керамика днища и низа крыла: темнее, плитки 0,25 м (8 на текстуру), тёмный плиточный шов
n = noise(16) * 0.5 + noise(128, 2) * 0.5
tile = rng.normal(0, 0.035, (8, 8))
tv = np.kron(tile, np.ones((N // 8, N // 8)))
s = seams(N // 8, 4, 0.45)
save("bor_dnische", tint((0.078, 0.071, 0.064), 0.18, n) * (1 + tv)[..., None] * s[..., None])
# 3. пористый боразон носа: светлее-серый, мелкие тёмные поры
n = noise(32) * 0.5 + 0.5 * noise(256, 1)
pores = (rng.random((N, N)) < 0.012).astype(float)
pores = np.asarray(Image.fromarray((pores * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(3)), float) / 255
save("boraz_nos", tint((0.29, 0.275, 0.255), 0.12, n) * (1 - 0.55 * pores)[..., None])
# 4. бериллиевая бронза с боразонным покрытием (рамы, опоры, обрамления): тёплый тёмный металл, продольная шлифовка
n = noise(4, 2) * 0.4 + 0.6 * np.repeat(rng.random((N, 1)), N, 1)
n = np.asarray(Image.fromarray((n * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1)), float) / 255
save("bronza_boraz", tint((0.36, 0.30, 0.22), 0.16, n))
# 5. катушки: плотная намотка (полосы 4 px), медь под боразоновым покрытием
idx = np.arange(N)
wind = 0.85 + 0.15 * np.cos(idx * 2 * np.pi / 4)
save("katushka", tint((0.55, 0.42, 0.22), 0.08, noise(16, 2)) * wind[:, None, None])
print("textures:", sorted(os.listdir(OUT)))
