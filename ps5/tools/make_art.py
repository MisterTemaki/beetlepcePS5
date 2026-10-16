#!/usr/bin/env python3
# Beetle PCE PS5: the app's home-screen art, drawn from scratch (no logos, no trademarks' artwork).
#   make_art.py <sce_sys dir> <font.ttf> [bc7enc] [--icon] [--background]
#     --icon         draws a provisional icon-source.png and icon0.png (the icon is now the project's own art:
#                    don't use it)
#     --background   draws a provisional background-source.png (the background is now the project's own art:
#                    don't use it)
#     bc7enc         then pic0.dds / pic1.dds from background-source.png: 3840x2160, BC7 DX10 DDS without mipmaps
#                    (bc7enc_rdo), with the header the home screen takes (as ps5-native-app-boilerplate's
#                    tools/prepare-assets.sh writes it)
# The picture: a HuCard -- the PC Engine's credit-card-sized game card -- in front of a CD, over a dusk sky and a
# grid floor.
# SPDX-License-Identifier: MIT
import math
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter, ImageFont

out, font_path = sys.argv[1], sys.argv[2]
args = [a for a in sys.argv[3:] if not a.startswith('--')]
bc7enc = args[0] if args else None
want_icon = '--icon' in sys.argv
want_bg = '--background' in sys.argv

ORANGE = (255, 122, 40)
AMBER = (255, 196, 80)
CYAN = (90, 220, 255)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def gradient(w, h, stops):
    im = Image.new('RGB', (1, h))
    px = im.load()
    for y in range(h):
        t = y / (h - 1)
        for i in range(len(stops) - 1):
            if stops[i][0] <= t <= stops[i + 1][0]:
                u = (t - stops[i][0]) / max(1e-9, stops[i + 1][0] - stops[i][0])
                px[0, y] = lerp(stops[i][1], stops[i + 1][1], u)
                break
    return im.resize((w, h))


def scene(w, h, horizon, scale):
    """A dusk sky with stars, a glow on the horizon, a grid floor."""
    im = gradient(w, h, [(0.0, (6, 8, 30)), (horizon * 0.7, (34, 18, 76)), (horizon, (120, 40, 90)),
                         (horizon + 0.001, (14, 8, 30)), (1.0, (4, 3, 12))])
    d = ImageDraw.Draw(im, 'RGBA')
    hy = int(h * horizon)
    rnd = random.Random(1987)  # the PC Engine's year
    for _ in range(int(w * h / 8000)):
        x, y = rnd.randrange(w), rnd.randrange(int(hy * 0.85))
        r = rnd.choice([1, 1, 1, 2]) * scale
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, rnd.randrange(80, 220)))
    glow = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([w * 0.1, hy - h * 0.18, w * 0.9, hy + h * 0.1], fill=ORANGE + (150,))
    glow = glow.filter(ImageFilter.GaussianBlur(h * 0.08))
    im.paste(glow, (0, 0), glow)
    d = ImageDraw.Draw(im, 'RGBA')
    line = (255, 120, 60, 170)
    lw = max(1, int(3 * scale))
    for i in range(1, 24):
        t = (i / 24) ** 2.1
        y = hy + int((h - hy) * t)
        d.line([(0, y), (w, y)], fill=line, width=lw)
    for i in range(-30, 31):
        d.line([(w / 2 + i * w * 0.05, hy), (w / 2 + i * w * 0.55, h)], fill=line, width=lw)
    d.line([(0, hy), (w, hy)], fill=AMBER + (255,), width=lw * 2)
    return im


def disc(layer, cx, cy, r):
    """A CD: rainbow sheen, the clear hub."""
    d = ImageDraw.Draw(layer, 'RGBA')
    for i in range(72):
        a0, a1 = i * 5, (i + 1) * 5
        hue = (math.sin(math.radians(i * 10)) + 1) / 2
        col = lerp((190, 200, 230), lerp((120, 220, 255), (255, 160, 220), hue), 0.45 + 0.4 * abs(math.sin(math.radians(i * 5))))
        d.pieslice([cx - r, cy - r, cx + r, cy + r], a0, a1, fill=col + (255,))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(240, 245, 255, 255), width=max(2, int(r * 0.02)))
    d.ellipse([cx - r * 0.34, cy - r * 0.34, cx + r * 0.34, cy + r * 0.34], fill=(205, 210, 225, 255))
    d.ellipse([cx - r * 0.14, cy - r * 0.14, cx + r * 0.14, cy + r * 0.14], fill=(0, 0, 0, 0))


def hucard(layer, cx, cy, wcard, angle):
    """The card: rounded, its label, the contacts along its edge; drawn upright, then turned."""
    hcard = wcard * 0.63
    pad = int(wcard * 0.2)
    W, H = int(wcard + 2 * pad), int(hcard + 2 * pad)
    c = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(c)
    x0, y0, x1, y1 = pad, pad, pad + wcard, pad + hcard
    r = wcard * 0.05
    d.rounded_rectangle([x0, y0, x1, y1], radius=r, fill=(236, 232, 224, 255), outline=(90, 80, 70, 255),
                        width=max(2, int(wcard * 0.008)))
    # the label: a dark field with an orange band
    lx0, ly0, lx1, ly1 = x0 + wcard * 0.07, y0 + hcard * 0.1, x1 - wcard * 0.07, y0 + hcard * 0.68
    d.rounded_rectangle([lx0, ly0, lx1, ly1], radius=r * 0.6, fill=(28, 22, 60, 255))
    d.rectangle([lx0, ly1 - hcard * 0.14, lx1, ly1 - hcard * 0.07], fill=ORANGE + (255,))
    # a little pixel-art star on the label
    px = (ly1 - ly0) * 0.075
    sx, sy = (lx0 + lx1) / 2 - 3.5 * px, ly0 + (ly1 - ly0) * 0.12
    star = ["...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", "##...##"]
    for j, row in enumerate(star):
        for i, ch in enumerate(row):
            if ch == '#':
                d.rectangle([sx + i * px, sy + j * px, sx + (i + 1) * px - 1, sy + (j + 1) * px - 1], fill=AMBER + (255,))
    # the contacts
    n = 19
    cw = (wcard * 0.84) / n
    for i in range(n):
        cx0 = x0 + wcard * 0.08 + i * cw
        d.rectangle([cx0 + cw * 0.15, y1 - hcard * 0.2, cx0 + cw * 0.85, y1 - hcard * 0.04], fill=(214, 170, 64, 255))
    shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle([x0 + wcard * 0.03, y0 + wcard * 0.04, x1 + wcard * 0.03, y1 + wcard * 0.04],
                                             radius=r, fill=(0, 0, 0, 150))
    shadow = shadow.filter(ImageFilter.GaussianBlur(wcard * 0.025))
    shadow.alpha_composite(c)
    rot = shadow.rotate(angle, resample=Image.BICUBIC, expand=True)
    layer.alpha_composite(rot, (int(cx - rot.width / 2), int(cy - rot.height / 2)))


def text_outline(d, xy, s, font, fill, outline, width):
    x, y = xy
    for dx in range(-width, width + 1):
        for dy in range(-width, width + 1):
            if dx * dx + dy * dy <= width * width:
                d.text((x + dx, y + dy), s, font=font, fill=outline)
    d.text((x, y), s, font=font, fill=fill)


def scanlines(im, step, alpha):
    d = ImageDraw.Draw(im, 'RGBA')
    for y in range(0, im.height, step):
        d.line([(0, y), (im.width, y)], fill=(0, 0, 0, alpha))
    return im


def icon(size):
    im = scene(size, size, 0.66, size / 512).convert('RGBA')
    layer = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    disc(layer, size * 0.63, size * 0.5, size * 0.25)
    hucard(layer, size * 0.42, size * 0.56, size * 0.56, 8)
    im.alpha_composite(layer)
    d = ImageDraw.Draw(im, 'RGBA')
    f = ImageFont.truetype(font_path, int(size * 0.17))
    t = 'Beetle PCE'
    tw = d.textlength(t, font=f)
    text_outline(d, ((size - tw) / 2, size * 0.05), t, f, (255, 255, 255, 255), (60, 14, 40, 255), max(2, size // 90))
    f2 = ImageFont.truetype(font_path, int(size * 0.11))
    t2 = 'PS5'
    tw2 = d.textlength(t2, font=f2)
    text_outline(d, ((size - tw2) / 2, size * 0.8), t2, f2, ORANGE + (255,), (20, 6, 30, 255), max(2, size // 120))
    return scanlines(im.convert('RGB'), max(2, size // 170), 36)


def background(w, h):
    im = scene(w, h, 0.62, w / 1920).convert('RGBA')
    layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    disc(layer, w * 0.56, h * 0.62, h * 0.16)
    hucard(layer, w * 0.45, h * 0.68, h * 0.36, 6)
    im.alpha_composite(layer)
    return scanlines(im.convert('RGB'), max(2, h // 540), 30)


os.makedirs(out, exist_ok=True)
if want_icon:
    ic = icon(1254)
    ic.save(os.path.join(out, 'icon-source.png'))
    ic.resize((512, 512), Image.LANCZOS).save(os.path.join(out, 'icon0.png'))
if want_bg:
    background(3840, 2160).resize((1920, 1080), Image.LANCZOS).save(os.path.join(out, 'background-source.png'))
if bc7enc:
    bg = Image.open(os.path.join(out, 'background-source.png')).convert('RGB').resize((3840, 2160), Image.LANCZOS)
    tmp = tempfile.mkdtemp()
    src = os.path.join(tmp, 'bg.png')
    bg.save(src)
    subprocess.run([bc7enc, '-q', '-g', src, os.path.join(tmp, 'bg.dds')], check=True, cwd=tmp)
    # the header the console's home screen takes: mip count 1 (with its flag), depth 1, DX10 alpha mode "straight"
    dds = bytearray(open(os.path.join(tmp, 'bg.dds'), 'rb').read())
    assert dds[:4] == b'DDS ' and dds[84:88] == b'DX10'
    struct.pack_into('<I', dds, 8, struct.unpack_from('<I', dds, 8)[0] | 0x20000)
    struct.pack_into('<I', dds, 24, 1)
    struct.pack_into('<I', dds, 28, 1)
    struct.pack_into('<I', dds, 144, 1)
    for name in ('pic0.dds', 'pic1.dds'):
        open(os.path.join(out, name), 'wb').write(dds)
    shutil.rmtree(tmp)
print('art written to', out)
