#!/usr/bin/env python3
"""Generate grayscale print cards for the KC02 (384-dot head).

Card 1 is a characterization card: a 16-step wedge, a radial gradient and a
stroke ladder, so the printed output shows the printer's own tone reproduction.
Card 2 is a picture: sky gradient, moon, mountain silhouettes and stars.
Both are saved as baseline grayscale JPEGs, which is what the firmware decodes.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw

OUT = Path('/tmp/kc02-art')
OUT.mkdir(parents=True, exist_ok=True)
SIDE = 384


def block_text(draw, text, x, y, w, h, t):
    """Small 5x7 block font, scaled. Enough for a legible caption."""
    GLYPHS = {
        'K': ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
        'C': ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
        '0': ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
        '2': ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
        '3': ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
        '9': ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
        'A': ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
        'X': ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
        'H': ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
        'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
        'T': ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
        ' ': ["00000"] * 7,
        '.': ["00000", "00000", "00000", "00000", "00000", "00000", "00100"],
    }
    px = w / 5.0
    py = h / 7.0
    cursor = x
    for char in text:
        rows = GLYPHS.get(char)
        if rows is None:
            cursor += 6 * px
            continue
        for ry, row in enumerate(rows):
            for rx, bit in enumerate(row):
                if bit == '1':
                    draw.rectangle([cursor + rx * px, y + ry * py,
                                    cursor + (rx + 1) * px - 1, y + (ry + 1) * py - 1], fill=0)
        cursor += 6 * px


def card_characterisation():
    img = Image.new('L', (SIDE, SIDE), 255)
    d = ImageDraw.Draw(img)
    for box in ([0, 0, SIDE - 1, 2], [0, SIDE - 3, SIDE - 1, SIDE - 1],
                [0, 0, 1, SIDE - 1], [SIDE - 3, 0, SIDE - 1, SIDE - 1]):
        d.rectangle(box, fill=0)
    block_text(d, "KC02 AX3292", 20, 10, 10, 14, 0)
    d.rectangle([20, 32, SIDE - 21, 33], fill=0)
    for step in range(16):                       # 16-step tone wedge
        level = int(255 - step * 255 / 15)
        x = 18 + step * 22
        d.rectangle([x, 42, x + 20, 86], fill=level)
        d.rectangle([x, 42, x + 20, 86], outline=0)
    cx, cy, radius = 96, 168, 58                # radial gradient disc
    for y in range(cy - radius, cy + radius + 1):
        for x in range(cx - radius, cx + radius + 1):
            dist = math.hypot(x - cx, y - cy)
            if dist <= radius:
                img.putpixel((x, y), int(20 + 235 * dist / radius))
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], outline=0)
    for x in range(182, SIDE - 22):             # linear gradient block
        d.rectangle([x, 122, x, 214], fill=int(20 + 235 * (x - 182) / (SIDE - 23 - 182)))
    d.rectangle([182, 122, SIDE - 23, 214], outline=0)
    y = 236                                     # stroke ladder 1..10 px, spaced
    for width in (1, 2, 3, 4, 6, 8, 10):
        d.rectangle([22, y, SIDE - 23, y + width - 1], fill=0)
        y += width + 3
    block_text(d, "TONE  STROKE  384", 22, SIDE - 30, 8, 13, 0)
    for tick in range(0, SIDE, 8):              # 1 mm feed reference on both edges
        length = 14 if (tick % 64 == 0) else 6
        d.rectangle([3, tick, 2 + length, tick], fill=0)
        d.rectangle([SIDE - 2 - length, tick, SIDE - 4, tick], fill=0)
    return img


def card_picture():
    img = Image.new('L', (SIDE, SIDE), 255)
    d = ImageDraw.Draw(img)
    horizon = 236
    for y in range(0, horizon):                 # night sky, lighter at the horizon
        d.rectangle([0, y, SIDE - 1, y], fill=int(30 + 130 * (y / horizon)))
    mx, my, mr = 274, 74, 40                    # moon
    d.ellipse([mx - mr - 2, my - mr - 2, mx + mr + 2, my + mr + 2], fill=120)
    d.ellipse([mx - mr, my - mr, mx + mr, my + mr], fill=250)
    for ring in range(3):
        d.ellipse([mx - 16 + ring * 5, my - 10 + ring * 3,
                   mx - 6 + ring * 5, my - 2 + ring * 3], outline=170)
    for index in range(48):                     # stars
        sx = 6 + (index * 71) % (SIDE - 12)
        sy = 4 + (index * 43) % (horizon - 60)
        d.rectangle([sx, sy, sx, sy], fill=252)
    ridges = [(150, [(0, 176), (58, 128), (104, 164), (168, 96), (214, 150), (300, 112), (SIDE - 1, 158)]),
              (96, [(0, 210), (74, 158), (140, 196), (216, 140), (286, 186), (SIDE - 1, 176)]),
              (44, [(0, 236), (96, 200), (188, 230), (272, 196), (SIDE - 1, 224)])]
    for level, peaks in ridges:
        d.polygon([(0, horizon)] + peaks + [(SIDE - 1, horizon)], fill=level)
        for index in range(0, len(peaks) - 1):  # snow caps
            (ax, ay), (bx, by) = peaks[index], peaks[index + 1]
            if ay < by and ay < horizon - 40:
                cap = 16
                d.polygon([(ax, ay), (ax - cap // 2, ay + cap), (ax + cap // 2, ay + cap)], fill=236)
    d.rectangle([0, horizon, SIDE - 1, horizon + 1], fill=200)
    for y in range(horizon + 2, SIDE):          # still water, banded reflection
        band = (y - horizon) // 7
        d.rectangle([0, y, SIDE - 1, y], fill=max(20, 90 - band * 12))
        if (y - horizon) % 7 == 0:
            d.rectangle([12 + (band * 29) % 200, y, 96 + (band * 29) % 200, y], fill=170)
    block_text(d, "KC02", 18, SIDE - 28, 9, 13, 250)
    return img


for name, builder in (('ARTCHART', card_characterisation), ('ARTPIC', card_picture)):
    image = builder()
    image.save(OUT / f'{name}.png')
    # 3-component YCbCr 4:2:0: the stock MJPEG decoder rejects single-component grayscale.
    image.convert('RGB').save(OUT / f'{name}.JPG', quality=92, subsampling=2,
                              optimize=True, progressive=False)
    dark = sum(1 for pixel in image.getdata() if pixel < 128)
    print(f'  {name}.JPG  384x384  mean {sum(image.getdata())/ (SIDE*SIDE):5.1f}  dark px {dark}')
