#!/usr/bin/env python3
"""Draw the print pattern at a camera photo's native geometry, in 3-component colour,
so the gallery has nothing to object to (resolution AND encoder profile match a real photo)."""
from PIL import Image, ImageDraw
import struct, sys

cam_w, cam_h = (int(sys.argv[1]), int(sys.argv[2])) if len(sys.argv) > 2 else (1280, 720)
out = sys.argv[3] if len(sys.argv) > 3 else '/tmp/kc02-board-re/hw/PHO09997.JPG'
W, H = cam_w, cam_h
scale = W / 384.0                      # features designed for a 384-dot-wide print
pitch = max(3, int(round(8 * scale)))  # 8 print dots == 1 mm at 203 dpi
L = max(1, int(round(scale)))          # line width that maps to ~1 print dot

im = Image.new('RGB', (W, H), (255, 255, 255))
d = ImageDraw.Draw(im)
def bar(x0, y0, x1, y1):
    x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
    x0, x1 = max(0, min(x0, x1)), min(W - 1, max(x0, x1))
    y0, y1 = max(0, min(y0, y1)), min(H - 1, max(y0, y1))
    if x1 >= x0 and y1 >= y0:
        d.rectangle([x0, y0, x1, y1], fill=(0, 0, 0))

bar(0, 0, W - 1, L - 1); bar(0, H - L, W - 1, H - 1)          # top / bottom frame
bar(0, 0, L - 1, H - 1); bar(W - L, 0, W - 1, H - 1)          # left(1 dot) / right frame
for x in range(0, W, pitch):                                   # tick ruler
    longt = (x // pitch) % 8 == 0
    tl = int((20 if longt else 8) * scale)
    bar(x, L, x + L - 1, L + tl - 1)
    bar(x, H - L - tl, x + L - 1, H - L - 1)
for frac in (0.60, 0.68, 0.76):                                # three solid full-width bars
    bar(0, H * frac, W - 1, H * frac + int(20 * scale))
cx, cy = W // 2, int(H * 0.28)                                 # centre cross
bar(cx - L, cy - int(40 * scale), cx + L - 1, cy + int(40 * scale))
bar(cx - int(40 * scale), cy - L, cx + int(40 * scale), cy + L - 1)
# 7-segment "384"
def seg7(x, y, w, h, t, digit):
    S = {'3': "abcdg", '8': "abcdefg", '4': "bcfg"}[digit]
    if 'a' in S: bar(x, y, x + w, y + t - 1)
    if 'b' in S: bar(x + w - t + 1, y, x + w, y + h // 2)
    if 'c' in S: bar(x + w - t + 1, y + h // 2, x + w, y + h)
    if 'd' in S: bar(x, y + h - t + 1, x + w, y + h)
    if 'e' in S: bar(x, y + h // 2, x + t - 1, y + h)
    if 'f' in S: bar(x, y, x + t - 1, y + h // 2)
    if 'g' in S: bar(x, y + h // 2 - t // 2, x + w, y + h // 2 + t // 2)
dw, dh, gap, t = int(60 * scale), int(100 * scale), int(25 * scale), int(12 * scale)
x0 = cx - (3 * dw + 2 * gap) // 2
for i, ch in enumerate("384"):
    seg7(x0 + i * (dw + gap), int(H * 0.06), dw, dh, t, ch)

im.save(out, quality=92, subsampling=2, optimize=True, progressive=False)
b = open(out, 'rb').read(); i = 2; got = None
while i < len(b) - 1:
    if b[i] != 0xFF: i += 1; continue
    m = b[i + 1]
    if m in (0xD8, 0xD9) or 0xD0 <= m <= 0xD7: i += 2; continue
    ln = struct.unpack_from('>H', b, i + 2)[0]; seg = b[i + 4:i + 2 + ln]
    if m in (0xC0, 0xC2):
        prec, h, w, nf = struct.unpack_from('>BHHB', seg, 0)
        got = (('SOF0' if m == 0xC0 else 'SOF2'), w, h, [(seg[6+3*k], f'{seg[7+3*k]>>4}x{seg[7+3*k]&15}') for k in range(nf)])
    i += 2 + ln
print(f"  wrote {out}  {len(b)} bytes  {got}   (pitch {pitch}px -> ~{pitch*384.0/W:.1f} print dots)")
