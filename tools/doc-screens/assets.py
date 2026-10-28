#!/usr/bin/env python3
"""Demo pictures for the documentation screenshots (numpy only).

Writes two raw RGB565 (little endian) files, the formats the board takes:

- ``picture.rgb565``: 480 x 272, the picture card and the full-screen
  picture (protocol.md "Picture") -- a drawn sunset over hills and a lake;
- ``cover.rgb565``: 120 x 120, the cover of the playing track
  (protocol.md "Cover") -- an abstract drawing.

Both are drawn here from formulas, so the screenshots carry no photo or
album cover of anyone else. ``--png`` also writes PNG previews (needs
Pillow).

Usage: assets.py <output directory> [--png]
"""

import os
import sys

import numpy as np


def smooth_noise(width, seed, octaves=5):
    """1-D fractal noise in 0..1, ``width`` samples."""
    rng = np.random.default_rng(seed)
    out = np.zeros(width)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        n = 4 * (2 ** o) + 1
        pts = rng.random(n)
        xs = np.linspace(0, n - 1, width)
        i = np.floor(xs).astype(int).clip(0, n - 2)
        t = xs - i
        t = t * t * (3 - 2 * t)
        out += amp * (pts[i] * (1 - t) + pts[i + 1] * t)
        total += amp
        amp *= 0.5
    return out / total


def lerp(a, b, t):
    return a + (b - a) * t[..., None]


def picture(w=480, h=272):
    y, x = np.mgrid[0:h, 0:w].astype(float)
    horizon = 0.72 * h
    # Sky: deep blue at the top, through violet and rose to a warm horizon.
    stops = [(0.0, (34, 46, 98)), (0.30, (92, 70, 140)), (0.52, (214, 108, 120)),
             (0.70, (250, 160, 96)), (0.82, (255, 206, 128))]
    ty = np.clip(y / horizon, 0, 1)
    sky = np.zeros((h, w, 3))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        m = (ty >= t0) & (ty <= t1)
        t = np.clip((ty - t0) / (t1 - t0), 0, 1)
        sky[m] = lerp(np.array(c0, float), np.array(c1, float), t)[m]
    sky[ty > stops[-1][0]] = stops[-1][1]
    # The sun and its glow.
    sx, sy, sr = 0.64 * w, 0.42 * h, 20.0
    d = np.hypot(x - sx, y - sy)
    glow = np.exp(-(d / 70.0) ** 2)[..., None]
    sky = sky * (1 - 0.55 * glow) + np.array([255, 214, 150]) * 0.55 * glow
    sun = np.clip((sr - d) / 1.5, 0, 1)[..., None]
    sky = sky * (1 - sun) + np.array([255, 240, 200]) * sun
    # A few thin clouds.
    for cy, cx, cw, seed in ((58, 120, 150, 3), (84, 330, 120, 4), (40, 380, 90, 5)):
        band = np.exp(-((y - cy) / 4.0) ** 2) * np.exp(-((x - cx) / cw) ** 2)
        band *= 0.6 + 0.4 * smooth_noise(w, seed)[None, :]
        sky = sky * (1 - 0.5 * band[..., None]) + np.array([255, 190, 170]) * 0.5 * band[..., None]
    img = sky
    # Three ridges, the far ones paler (aerial perspective).
    ridges = [(0.62, 0.16, (132, 96, 140), 11), (0.67, 0.12, (86, 62, 104), 12),
              (0.72, 0.07, (40, 34, 60), 13)]
    for base, amp, col, seed in ridges:
        top = (base - amp * smooth_noise(w, seed)) * h
        m = (y >= top[None, :]) & (y < horizon)
        shade = np.clip((y - top[None, :]) / 60.0, 0, 1)[..., None]
        c = np.array(col, float) * (1 - 0.25 * shade)
        img = np.where(m[..., None], c, img)
    # The lake: the sky mirrored, darker, with ripples.
    below = y >= horizon
    my = np.clip(2 * horizon - y, 0, h - 1).astype(int)
    ripple = (np.sin(y * 1.7 + np.sin(x / 23.0) * 2.0) * 1.5).astype(int)
    mx = np.clip(x.astype(int) + ripple, 0, w - 1)
    mirror = img[my, mx] * 0.62 + np.array([20, 24, 48]) * 0.38
    streak = np.exp(-((x - sx) / (10 + (y - horizon) * 0.5)) ** 2) * (
        0.5 + 0.5 * np.sin(y * 2.3)) * np.exp(-(y - horizon) / 60.0)
    mirror = mirror + np.array([255, 200, 140]) * 0.35 * streak[..., None]
    img = np.where(below[..., None], mirror, img)
    # Reeds in the near left corner.
    rng = np.random.default_rng(21)
    for _ in range(26):
        rx = rng.uniform(0, 120)
        rh = rng.uniform(30, 80)
        lean = rng.uniform(-0.15, 0.15)
        t = np.clip((h - y) / rh, 0, 1)
        m = (np.abs(x - (rx + lean * (h - y))) < 1.2 * (1 - t) + 0.4) & (h - y < rh)
        img[m] = (24, 22, 36)
    return np.clip(img, 0, 255)


def cover(size=120):
    y, x = np.mgrid[0:size, 0:size].astype(float) / (size - 1)
    # Waves in bands, from a night blue at the top to coral at the bottom,
    # and a pale moon: a drawn poster, not anyone's artwork.
    palette = np.array([(24, 34, 74), (40, 64, 120), (32, 112, 150), (44, 160, 160),
                        (240, 140, 110), (232, 96, 96), (190, 60, 88)], float)
    wave = y + 0.05 * np.sin(x * 11 + y * 3) + 0.03 * np.sin(x * 23)
    band = np.clip((wave * len(palette)).astype(int), 0, len(palette) - 1)
    img = palette[band]
    moon = np.clip((0.15 - np.hypot(x - 0.68, y - 0.26)) * 80, 0, 1)
    img = img * (1 - moon[..., None]) + np.array([252, 236, 200]) * moon[..., None]
    return np.clip(img, 0, 255)


def to_rgb565(img):
    r = img[..., 0].astype(np.uint16) >> 3
    g = img[..., 1].astype(np.uint16) >> 2
    b = img[..., 2].astype(np.uint16) >> 3
    return ((r << 11) | (g << 5) | b).astype("<u2").tobytes()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    pic, cov = picture(), cover()
    with open(os.path.join(out, "picture.rgb565"), "wb") as f:
        f.write(to_rgb565(pic))
    with open(os.path.join(out, "cover.rgb565"), "wb") as f:
        f.write(to_rgb565(cov))
    if "--png" in sys.argv:
        from PIL import Image
        Image.fromarray(pic.astype(np.uint8)).save(os.path.join(out, "picture.png"))
        Image.fromarray(cov.astype(np.uint8)).save(os.path.join(out, "cover.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
