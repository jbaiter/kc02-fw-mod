#!/usr/bin/env python3
"""Build the 383/384 printer test pattern.

Design rationale: the firmware's head shift-out loop starts at bitmap index 1 and stops before
`rows`, emitting 383 clocks and never driving index 0.  So the test must make a *missing edge
column* obvious at ruler scale, not at 0.125 mm scale:

  * 2-px black FRAME at the very edge  -> if index 0 is never clocked, the left frame line is
    missing (and if the skipped index is a paper line, the corresponding top/bottom line is thin).
  * TICKS every 8 px (2 px wide, 8 px long), long ticks every 64 px.  Measured from the paper
    edge to the first tick: 0.125 mm if column 0 prints, 1.0 mm if column 0 is dropped.
    That is the ruler-scale signal.
  * CONTROL lines at x=4 and x=379 (2 px) inside the frame, which must always print.
  * 16x16 corner block top-left, centre cross, and a 7-segment "384" so orientation is unambiguous.
  * Dashed line at mid-height to reveal paper-feed-direction mapping.
"""
from PIL import Image, ImageDraw

def seg7(d, x, y, w, h, t, digit):
    SEG = {'0':"abcdef",'1':"bc",'2':"abdeg",'3':"abcdg",'4':"bcfg",'5':"acdfg",
           '6':"acdefg",'7':"abc",'8':"abcdefg",'9':"abcdfg"}[digit]
    if 'a' in SEG: d.rectangle([x, y, x+w, y+t-1], fill=0)
    if 'b' in SEG: d.rectangle([x+w-t+1, y, x+w, y+h//2], fill=0)
    if 'c' in SEG: d.rectangle([x+w-t+1, y+h//2, x+w, y+h], fill=0)
    if 'd' in SEG: d.rectangle([x, y+h-t+1, x+w, y+h], fill=0)
    if 'e' in SEG: d.rectangle([x, y+h//2, x+t-1, y+h], fill=0)
    if 'f' in SEG: d.rectangle([x, y, x+t-1, y+h//2], fill=0)
    if 'g' in SEG: d.rectangle([x, y+h//2-t//2, x+w, y+h//2+t//2+1], fill=0)

def build(w, h, path):
    img = Image.new('L', (w, h), 255)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w-1, 1], fill=0); d.rectangle([0, h-2, w-1, h-1], fill=0)   # top/bottom frame (2 px)
    d.rectangle([0, 0, 0, h-1], fill=0)                                            # LEFT: exactly 1 px
    d.rectangle([w-2, 0, w-1, h-1], fill=0)                                        # right frame (2 px)
    # solid full-width bars: their printed width is the head's driven-dot count x dot pitch
    d.rectangle([0, h-84, w-1, h-64], fill=0)
    d.rectangle([0, h-54, w-1, h-34], fill=0)
    d.rectangle([0, h-24, w-1, h-14], fill=0)
    for x in range(0, w, 8):                       # ticks every 8 px
        long_tick = (x % 64 == 0)
        d.rectangle([x, 2, min(x+1, w-1), 2+(20 if long_tick else 8)], fill=0)
        d.rectangle([x, h-3-(20 if long_tick else 8), min(x+1, w-1), h-3], fill=0)
    for x in (4, w-5):                             # control lines just inside the frame
        d.rectangle([x, 30, x+1, h-31], fill=0)
    d.rectangle([24, 24, 39, 39], fill=0)          # orientation block, top-left
    cx, cy = w//2, h//2
    d.rectangle([cx-1, cy-20, cx, cy+20], fill=0); d.rectangle([cx-20, cy-1, cx+20, cy], fill=0)
    for x in range(40, w-40, 16):                  # dashed mid line
        d.rectangle([x, cy+40, x+7, cy+41], fill=0)
    t, dw, dh, gap = 6, 24, 40, 10                 # "384"
    x0 = cx - (3*dw + 2*gap)//2; y0 = 48
    for i, ch in enumerate("384"):
        seg7(d, x0 + i*(dw+gap), y0, dw, dh, t, ch)
    img.save(path, quality=100, subsampling=0)
    img.save(str(path).replace('.jpg', '.png'))
    return img

for (w, h, name) in ((384, 384, 't384x384'), (384, 320, 't384x320'), (672, 384, 't672x384')):
    p = f'/home/jbaiter/.src/kc02-fw-mod/docs/hw/print_test/{name}.jpg'
    im = build(w, h, p)
    px = im.load(); black = sum(1 for y in range(h) for x in range(w) if px[x, y] < 128)
    print(f"  {name}.jpg  {w}x{h}  black px {black} ({100*black/(w*h):.1f}%)")
