#!/usr/bin/env python3
"""Draws the 1024 px macOS app icon (installer/macos/AppIcon-1024.png).

    build-macos/py/bin/python installer/macos/make-icon.py

The output is committed; build-macos.sh turns it into AppIcon.icns with sips
and iconutil, so a release build needs no Pillow. resources/icon.png is only
256 px - too small for a Retina Dock - so the brush X is keyed out of the
large resources/github-splash.png instead: its blue parts (B high, R low) are
the X, the white letters and the navy ground are not.

The shape follows Apple's icon grid: an 824 px rounded square centred on the
1024 canvas, leaving the margin the Dock and Finder expect.
"""
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

SIZE = 1024
BODY = 824
RADIUS = 185
BLUE = (30, 155, 255)


def main():
    off = (SIZE - BODY) // 2
    body = Image.new("RGB", (BODY, BODY))
    d = ImageDraw.Draw(body)
    for y in range(BODY):  # navy, lighter at the top
        t = y / (BODY - 1)
        d.line([(0, y), (BODY, y)], fill=(int(16 - 12 * t), int(30 - 22 * t), int(62 - 44 * t)))
    bands = Image.new("RGBA", (BODY, BODY), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bands)
    for x0, w, a in ((150, 120, 60), (300, 56, 40)):
        bd.polygon([(x0, BODY), (x0 + w, BODY), (x0 + w + 520, 0), (x0 + 520, 0)], fill=(*BLUE, a))
    body.paste(bands, (0, 0), bands)

    # The X: blue, not white, not navy.
    splash = Image.open(os.path.join(REPO, "resources", "github-splash.png")).convert("RGB")
    x = splash.crop((1060, 110, 1470, 480))
    r, g, b = x.split()
    blue = ImageChops.subtract(b, r)  # X ~ 246, white ~ 0, navy ~ 23, bands ~ 142
    mask = blue.point(lambda v: max(0, min(255, (v - 170) * 255 // 50)))
    # Also drop the bands, which are blue but dark: require brightness too.
    mask = ImageChops.multiply(mask, g.point(lambda v: max(0, min(255, (v - 95) * 255 // 40))))
    # Two neighbours share the crop: a corner of the bright band (top left)
    # and the end of the wordmark's underline, which meets the X's lower-left
    # tail. Cut both out by area (crop coordinates).
    md = ImageDraw.Draw(mask)
    md.rectangle([0, 0, 34, 40], fill=0)
    md.rectangle([0, 282, 40, 318], fill=0)
    scale = 620 / max(x.size)
    x = x.resize((int(x.width * scale), int(x.height * scale)), Image.LANCZOS)
    mask = mask.resize(x.size, Image.LANCZOS)
    pos = ((BODY - x.width) // 2 + 6, (BODY - x.height) // 2)
    glow = Image.new("RGBA", (BODY, BODY), (0, 0, 0, 0))
    glow.paste(Image.new("RGBA", x.size, (*BLUE, 200)), pos, mask)
    glow = glow.filter(ImageFilter.GaussianBlur(28))
    body.paste(glow, (0, 0), glow)
    body.paste(x, pos, mask)

    # The four face-button dots of the wordmark's A, as a small signature.
    dd = ImageDraw.Draw(body)
    cx, cy, r0, sp = 640, 650, 15, 34
    for dx, dy in ((0, -sp), (-sp, 0), (sp, 0), (0, sp)):
        dd.ellipse([cx + dx - r0, cy + dy - r0, cx + dx + r0, cy + dy + r0], fill=(235, 242, 252))

    shape = Image.new("L", (BODY, BODY), 0)
    ImageDraw.Draw(shape).rounded_rectangle([0, 0, BODY - 1, BODY - 1], radius=RADIUS, fill=255)
    icon = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    # A soft drop shadow, as on Apple's own icons.
    shadow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    shadow.paste(Image.new("RGBA", (BODY, BODY), (0, 0, 0, 110)), (off, off + 12), shape)
    icon = Image.alpha_composite(icon, shadow.filter(ImageFilter.GaussianBlur(18)))
    icon.paste(body, (off, off), shape)
    # A hairline rim keeps the edge visible on a dark Dock.
    rim = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ImageDraw.Draw(rim).rounded_rectangle([off, off, off + BODY - 1, off + BODY - 1],
                                          radius=RADIUS, outline=(90, 140, 210, 120), width=3)
    icon = Image.alpha_composite(icon, rim)
    icon.save(os.path.join(HERE, "AppIcon-1024.png"), optimize=True)
    print("wrote AppIcon-1024.png")


if __name__ == "__main__":
    main()
