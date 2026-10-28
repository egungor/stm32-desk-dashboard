#!/usr/bin/env python3
"""Composite images for the README from the screenshots (needs Pillow).

- ``hero.png``: the Dashboard, the Music tab and the Audio tab side by side,
  each in a dark bezel like the board's panel, on a transparent background
  (reads well on GitHub's light and dark pages);
- ``accents.png``: the six accent colours (``accent-*.png``) in a 3 x 2 grid.

The screenshots themselves are not changed.

Usage: contact.py <screens directory>
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFilter

BEZEL = 14
RADIUS = 18


def framed(img, scale=1.0):
    """The screen in a rounded dark bezel with a soft shadow (RGBA)."""
    if scale != 1.0:
        img = img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)
    bezel = round(BEZEL * scale)
    radius = round(RADIUS * scale)
    pad = round(24 * scale)
    w, h = img.width + 2 * bezel, img.height + 2 * bezel
    out = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    shadow = Image.new("L", out.size, 0)
    ImageDraw.Draw(shadow).rounded_rectangle((pad, pad + round(6 * scale), pad + w, pad + h + round(6 * scale)),
                                             radius, fill=110)
    shadow = shadow.filter(ImageFilter.GaussianBlur(round(10 * scale)))
    out.paste((0, 0, 0, 255), (0, 0), shadow)
    body = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius, fill=(22, 24, 28, 255), outline=(58, 62, 70, 255), width=1)
    body.paste(img.convert("RGBA"), (bezel, bezel))
    out.alpha_composite(body, (pad, pad))
    return out


def row(images, gap):
    w = sum(i.width for i in images) + gap * (len(images) - 1)
    h = max(i.height for i in images)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    x = 0
    for i in images:
        out.alpha_composite(i, (x, (h - i.height) // 2))
        x += i.width + gap
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    load = lambda n: Image.open(os.path.join(d, n + ".png")).convert("RGB")

    hero = row([framed(load("music-playing"), 0.8), framed(load("dash-main")), framed(load("audio-cards"), 0.8)],
               -18)
    hero.save(os.path.join(d, "hero.png"), optimize=True)

    accents = ["orange", "blue", "green", "purple", "pink", "yellow"]
    tiles = [framed(load("accent-" + a), 0.5) for a in accents]
    grid = row([row(tiles[:3], -6), ], 0)
    grid2 = row(tiles[3:], -6)
    out = Image.new("RGBA", (grid.width, grid.height + grid2.height - 6), (0, 0, 0, 0))
    out.alpha_composite(grid, (0, 0))
    out.alpha_composite(grid2, (0, grid.height - 6))
    out.save(os.path.join(d, "accents.png"), optimize=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
