#!/usr/bin/env python3
"""
Generate the branded ISO Drums DMG background (light theme).

Why light: Finder draws icon *label* text in black and does not recolor it for
a dark window background, so a dark installer renders filenames black-on-black.
A light background keeps labels crisp, and — because Finder's own uncovered
window area and status bar are also light — any slack between the background
height and the live content area blends into white instead of showing a seam.

No scrollbar: the window (see dmg_settings.py) is made comfortably taller than
everything we draw, and this canvas is kept WELL under the content area, so no
icon, label, or background pixel ever sits below the fold. The extra white at
the bottom is invisible on a white base.

Renders @2x and @1x and combines them into a HiDPI background.tiff. Coordinates
are coordinated with the icon positions in scripts/dmg_settings.py.
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES  = os.path.join(ROOT, "Resources")
OUT  = os.path.join(RES, "dmg")
os.makedirs(OUT, exist_ok=True)

S = 2                       # retina scale
W, H = 680 * S, 500 * S     # canvas @2x — kept under the window content area

# ── palette (light) ──────────────────────────────────────────────────────
WHITE   = (255, 255, 255)
INK     = (26, 26, 31)      # wordmark
BODY    = (74, 72, 68)
MUTED   = (140, 135, 128)
GOLD    = (176, 141, 87)
DIVIDER = (228, 223, 214)
PLAIT   = (128, 123, 116)   # medium gray plus; dots knock out to white

def font(name, px):
    return ImageFont.truetype(os.path.join(RES, "fonts", name), px * S)

INTER_B, INTER_M, INTER_R = "Inter-Bold.otf", "Inter-Medium.otf", "Inter-Regular.otf"

# ── base: white with a whisper of warmth up top, fading to pure white ─────
img = Image.new("RGB", (W, H), WHITE)
warm = Image.new("RGB", (W, H), (250, 247, 242))
mask = Image.new("L", (W, H), 0)
md = ImageDraw.Draw(mask)
for y in range(H):
    a = max(0, int(85 * (1 - y / (H * 0.55))))
    md.line([(0, y), (W, y)], fill=a)
img = Image.composite(warm, img, mask)
d = ImageDraw.Draw(img)

def paste_logo(path, target_w_pt, cx_pt, cy_pt, rgb, opacity=1.0):
    logo = Image.open(path).convert("RGBA")
    tw = int(target_w_pt * S)
    th = int(tw * logo.height / logo.width)
    logo = logo.resize((tw, th), Image.LANCZOS)
    solid = Image.new("RGBA", logo.size, rgb + (255,))
    alpha = logo.split()[3]
    if opacity < 1.0:
        alpha = alpha.point(lambda p: int(p * opacity))
    solid.putalpha(alpha)
    img.paste(solid, (int(cx_pt * S - tw / 2), int(cy_pt * S - th / 2)), solid)

def paste_plait(path, target_w_pt, cx_pt, cy_pt, plus_rgb):
    """Two-tone recolor of the Plait lockup: the white plus + wordmark become
    `plus_rgb`, and the black dots become white so they read as knocked-out
    holes on the light background (a flat single-color recolor would merge the
    dots into the plus and lose them)."""
    logo = Image.open(path).convert("RGBA")
    src = logo.load()
    out = Image.new("RGBA", logo.size, (0, 0, 0, 0))
    dst = out.load()
    for y in range(logo.height):
        for x in range(logo.width):
            r, g, b, a = src[x, y]
            if a < 40:
                continue
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            dst[x, y] = (plus_rgb + (a,)) if lum > 128 else (255, 255, 255, a)
    tw = int(target_w_pt * S)
    th = int(tw * out.height / out.width)
    out = out.resize((tw, th), Image.LANCZOS)
    img.paste(out, (int(cx_pt * S - tw / 2), int(cy_pt * S - th / 2)), out)

def ctext(cx_pt, y_pt, text, fnt, fill, tracking=0.0):
    if tracking == 0:
        w = d.textlength(text, font=fnt)
        d.text((cx_pt * S - w / 2, y_pt * S), text, font=fnt, fill=fill)
    else:
        widths = [d.textlength(c, font=fnt) for c in text]
        total = sum(widths) + tracking * S * (len(text) - 1)
        x = cx_pt * S - total / 2
        for c, w in zip(text, widths):
            d.text((x, y_pt * S), c, font=fnt, fill=fill)
            x += w + tracking * S

# ── wordmark (recolored dark) ────────────────────────────────────────────
paste_logo(os.path.join(RES, "logo-iso-drums-horiz-white.png"), 236, 340, 68, INK)

# ── install: app  →(gold arrow)→  Applications  (icons at y=195) ─────────
ay = 195 * S
x0, x1 = 250 * S, 428 * S
d.line([(x0, ay), (x1, ay)], fill=GOLD, width=5 * S)
hd = 15 * S
d.polygon([(x1 + hd, ay), (x1 - 3, ay - hd), (x1 - 3, ay + hd)], fill=GOLD)

# ── divider ──────────────────────────────────────────────────────────────
d.line([(150 * S, 300 * S), (530 * S, 300 * S)], fill=DIVIDER, width=max(1, S))

# ── plug-ins caption (icons sit below at y=400) ──────────────────────────
ctext(340, 316, "PRODUCING IN A DAW?", font(INTER_B, 10), GOLD, tracking=2.2)
ctext(340, 334, "Copy the AU + VST3 into your Plug-Ins folder", font(INTER_R, 13), MUTED)

# ── footer: Plait mark (two-tone so the five dots read; below the labels) ─
paste_plait(os.path.join(RES, "logo-plait-horiz-white.png"), 88, 340, 490, PLAIT)

# ── export @2x + @1x, combine to HiDPI tiff ──────────────────────────────
p2 = os.path.join(OUT, "background@2x.png")
p1 = os.path.join(OUT, "background.png")
img.save(p2)
img.resize((W // S, H // S), Image.LANCZOS).save(p1)
print("wrote", p1, "and", p2)
