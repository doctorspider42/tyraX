#!/usr/bin/env python3
"""Draws the TyraX disk-image window background (installer/macos/).

    build-macos/py/bin/python installer/macos/make-dmg-background.py

Writes dmg-background.png (660x440, the window in points) and
dmg-background@2x.png (Retina) beside this script; both are committed, so a
release build needs no Pillow - rerun this only to change the art. The
geometry is dmg_layout.py's, which dmg-settings.py reads too.

The look is resources/github-splash.png's: navy ground, the blue diagonal
bands, faint controller outlines, the wordmark - plus a brush-stroke arrow in
the logo's blue from the app to the Applications folder.

Finder draws each icon's name itself, black in Light Mode and white in Dark
Mode, and offers no text colour for a background picture. So each name sits on
a mid-tone plate that keeps both readable instead of a navy that hides one.
"""
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# Shared with dmg-settings.py (points).
from dmg_layout import APP_POS, APPS_POS, ICON_SIZE, WINDOW  # noqa: E402

NAVY_TOP = (9, 17, 36)
NAVY_BOTTOM = (3, 7, 16)
BLUE = (30, 155, 255)
LINE = (38, 92, 170)


def font(size, bold=False):
    for path, index in (("/System/Library/Fonts/HelveticaNeue.ttc", 1 if bold else 0),
                        ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 0)):
        try:
            return ImageFont.truetype(path, size, index=index)
        except OSError:
            continue
    return ImageFont.load_default()


def draw(scale):
    W, H = WINDOW[0] * scale, WINDOW[1] * scale
    s = lambda v: int(round(v * scale))  # noqa: E731

    img = Image.new("RGB", (W, H))
    px = ImageDraw.Draw(img)
    for y in range(H):  # vertical navy gradient
        t = y / (H - 1)
        px.line([(0, y), (W, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(NAVY_TOP, NAVY_BOTTOM)))

    # The splash's two diagonal bands, quiet enough to sit behind the arrow.
    bands = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bands)
    for x0, width, alpha in ((s(120), s(70), 40), (s(210), s(34), 26)):
        bd.polygon([(x0, H), (x0 + width, H), (x0 + width + s(330), 0), (x0 + s(330), 0)],
                   fill=(*BLUE, alpha))
    img.paste(bands, (0, 0), bands)

    # Faint controller outlines, like the splash's corners.
    deco = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dd = ImageDraw.Draw(deco)
    lw = max(1, s(1))
    col = (*LINE, 120)
    for cx, cy in ((54, 352), (92, 314), (130, 352), (92, 390)):
        dd.ellipse([s(cx - 17), s(cy - 17), s(cx + 17), s(cy + 17)], outline=col, width=lw)
    dd.arc([s(-120), s(-150), s(120), s(90)], 0, 90, fill=col, width=lw)
    dd.line([(s(560), s(30)), (s(640), s(60)), (s(612), s(130)), (s(690), s(170))], fill=col, width=lw)
    dd.rounded_rectangle([s(540), s(330), s(720), s(470)], radius=s(40), outline=col, width=lw)
    dd.ellipse([s(590), s(372), s(626), s(408)], outline=col, width=lw)
    img.paste(deco, (0, 0), deco)

    # The wordmark, keyed out of the splash. Its green channel separates the
    # parts cleanly: navy ground ~10, the diagonal bands ~74, the blue X and
    # underline ~147, the white letters 253 - so a ramp from 85 to 140 keeps
    # the mark (antialiased edges included) and drops everything behind it.
    splash = Image.open(os.path.join(REPO, "resources", "github-splash.png")).convert("RGB")
    logo = splash.crop((392, 120, 1460, 480))
    lw_pt = 300
    logo = logo.resize((s(lw_pt), int(s(lw_pt) * logo.height / logo.width)), Image.LANCZOS)
    mask = logo.getchannel("G").point(lambda g: max(0, min(255, (g - 85) * 255 // 55)))
    lx, ly = (W - logo.width) // 2, s(22)
    halo = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    halo.paste(Image.new("RGBA", logo.size, (*BLUE, 90)), (lx, ly), mask)
    halo = halo.filter(ImageFilter.GaussianBlur(s(10)))
    img.paste(halo, (0, 0), halo)
    img.paste(logo, (lx, ly), mask)

    # Brush-stroke arrow from the app to Applications, with a glow.
    x0 = APP_POS[0] + ICON_SIZE / 2 + 18
    x1 = APPS_POS[0] - ICON_SIZE / 2 - 18
    y = APP_POS[1]
    head = 26
    stroke = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(stroke)
    rnd = random.Random(42)  # the same art on every run
    for i in range(46):  # bristles: thin jittered lines make the shaft
        off = (i - 23) / 23.0 * 6.5
        start = x0 + rnd.uniform(-6, 10) + abs(off) * 1.5
        end = x1 - head + rnd.uniform(-4, 8)
        a = int(255 * (1 - abs(off) / 8.5) * rnd.uniform(0.75, 1.0))
        sd.line([(s(start), s(y + off + rnd.uniform(-0.6, 0.6))),
                 (s(end), s(y + off * 0.8 + rnd.uniform(-0.6, 0.6)))],
                fill=(*BLUE, max(a, 0)), width=max(1, s(1.1)))
    tip = (s(x1), s(y))
    sd.polygon([tip, (s(x1 - head - 4), s(y - 17)), (s(x1 - head + 3), s(y)),
                (s(x1 - head - 4), s(y + 17))], fill=(*BLUE, 255))
    glow = stroke.filter(ImageFilter.GaussianBlur(s(7)))
    img.paste(glow, (0, 0), glow)
    img.paste(stroke, (0, 0), stroke)

    # Plates under the names Finder draws (see the module docstring).
    plates = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    pd = ImageDraw.Draw(plates)
    for cx, cy in (APP_POS, APPS_POS):
        top = cy + ICON_SIZE / 2 + 6
        pd.rounded_rectangle([s(cx - 64), s(top), s(cx + 64), s(top + 22)], radius=s(11),
                             fill=(132, 150, 178, 235))
    img.paste(plates, (0, 0), plates)

    # The instruction.
    td = ImageDraw.Draw(img)
    msg = "Drag TyraX into Applications"
    f = font(s(15), bold=True)
    tw = td.textlength(msg, font=f)
    td.text(((W - tw) / 2, s(372)), msg, font=f, fill=(226, 236, 250))
    sub = "PS2 games, made on your Mac"
    f2 = font(s(11.5))
    tw2 = td.textlength(sub, font=f2)
    td.text(((W - tw2) / 2, s(396)), sub, font=f2, fill=(128, 150, 186))
    return img


if __name__ == "__main__":
    draw(1).save(os.path.join(HERE, "dmg-background.png"), optimize=True)
    draw(2).save(os.path.join(HERE, "dmg-background@2x.png"), optimize=True)
    print("wrote dmg-background.png and dmg-background@2x.png")
