#!/usr/bin/env python3
"""Generates ClaudeFor3DS's home menu icon and banner art.

    python3 tools/make_assets.py

Writes resources/icon.png (48x48) and resources/banner.png (256x128). The spark is drawn
procedurally with the same geometry the app uses (ui::claudeLogo), supersampled for smooth edges.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "resources")

LENGTHS = [1.0, 0.86, 0.95, 0.8, 0.98, 0.84, 0.93, 0.82, 1.0, 0.88, 0.9, 0.83]
JITTER = [0.0, 0.05, -0.03, 0.04, -0.02, 0.03, 0.0, -0.04, 0.02, -0.03, 0.04, 0.0]

RED = (217, 71, 43)
RED_DARK = (170, 48, 26)
CREAM = (245, 240, 230)


def spark(draw, cx, cy, r, color, spin=0.0, squash=1.0):
    """Twelve tapered rays with round tips. squash < 1 narrows it horizontally (3D turn)."""
    base = r * 0.16
    tip = r * 0.085
    for i in range(12):
        a = spin + i * (2 * math.pi / 12) + JITTER[i] - math.pi / 2
        length = r * LENGTHS[i]
        ca, sa = math.cos(a), math.sin(a)
        px, py = -sa, ca
        bx, by = cx + ca * r * 0.12 * squash, cy + sa * r * 0.12
        tx, ty = cx + ca * (length - tip) * squash, cy + sa * (length - tip)
        pts = [
            (bx + px * base * squash, by + py * base),
            (tx + px * tip * squash, ty + py * tip),
            (tx - px * tip * squash, ty - py * tip),
            (bx - px * base * squash, by - py * base),
        ]
        draw.polygon(pts, fill=color)
        draw.ellipse([tx - tip * squash, ty - tip, tx + tip * squash, ty + tip], fill=color)
    c = r * 0.2
    draw.ellipse([cx - c * squash, cy - c, cx + c * squash, cy + c], fill=color)


def make_icon(path):
    s = 8  # supersample
    size = 48 * s
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([2 * s, 2 * s, size - 2 * s, size - 2 * s], radius=10 * s, fill=CREAM)
    spark(d, size / 2, size / 2, 17.5 * s, RED)
    img = img.resize((48, 48), Image.LANCZOS)
    img.save(path)


def make_banner(path):
    """Static banner: a shaded, slightly turned spark (used when no animated CGFX is built)."""
    s = 4
    w, h = 256 * s, 128 * s
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    shadow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse([w / 2 - 70 * s, 108 * s, w / 2 + 70 * s, 122 * s], fill=(0, 0, 0, 90))
    shadow = shadow.filter(ImageFilter.GaussianBlur(6 * s))
    img.alpha_composite(shadow)
    d = ImageDraw.Draw(img)
    # Back face for depth, then the front face.
    spark(d, w / 2 + 4 * s, 60 * s, 52 * s, RED_DARK, spin=0.12, squash=0.82)
    spark(d, w / 2, 58 * s, 52 * s, RED, spin=0.12, squash=0.82)
    img = img.resize((256, 128), Image.LANCZOS)
    img.save(path)


def main():
    os.makedirs(RES, exist_ok=True)
    make_icon(os.path.join(RES, "icon.png"))
    make_banner(os.path.join(RES, "banner.png"))
    print("wrote resources/icon.png and resources/banner.png")


if __name__ == "__main__":
    sys.exit(main())
