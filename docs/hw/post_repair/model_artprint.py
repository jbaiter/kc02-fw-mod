#!/usr/bin/env python3
"""Independent host model of artprint.c: renders the same canvas, computes the same
FNV-1a hash and ink count, and writes a PNG preview. Matching the device hash proves
the payload drew exactly what this preview shows.
"""
from pathlib import Path
from PIL import Image

HEAD_DOTS = 384
LINES_PER_MM = 8
HEADER_LINES = 24
MAZE_COLS, MAZE_ROWS, CELL_X, CELL_Y = 9, 40, 40, 40
MAZE_X0, MAZE_Y0 = 20, HEADER_LINES
CHEST_COL, CHEST_ROW = 4, 20
WALL_THICKNESS = 2
MAZE_LINES = MAZE_ROWS * CELL_Y
MASK32 = 0xFFFFFFFF

FONT = {
    'A': ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    'B': ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    'C': ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    'D': ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    'E': ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    'F': ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    'G': ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    'H': ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    'I': ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    'J': ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    'K': ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    'L': ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    'N': ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    'O': ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    'Q': ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    'R': ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'T': ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    'U': ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    'V': ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    'W': ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    'X': ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    'Y': ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    'Z': ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    '0': ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    '1': ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    '2': ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    '3': ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    '4': ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    '5': ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    '6': ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    '7': ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    '8': ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    '9': ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    ' ': ["00000"] * 7,
    '.': ["00000", "00000", "00000", "00000", "00000", "00000", "00100"],
    '-': ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    '/': ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    ':': ["00000", "00100", "00100", "00000", "00100", "00100", "00000"],
    '+': ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    '=': ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
}


class Canvas:
    def __init__(self, lines):
        self.lines = lines
        self.data = bytearray(b'\xff' * (lines * HEAD_DOTS))

    def set(self, x, y, value):
        if 0 <= x < HEAD_DOTS and 0 <= y < self.lines:
            self.data[x * self.lines + y] = value & 0xFF

    def put(self, x, y):
        if 0 <= x < HEAD_DOTS and 0 <= y < self.lines:
            self.data[x * self.lines + y] = 0

    def erase(self, x, y):
        if 0 <= x < HEAD_DOTS and 0 <= y < self.lines:
            self.data[x * self.lines + y] = 0xFF

    def box(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y)

    def clear_box(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.erase(x, y)

    def rect_outline(self, x0, y0, x1, y1, t):
        self.box(x0, y0, x1, y0 + t - 1); self.box(x0, y1 - t + 1, x1, y1)
        self.box(x0, y0, x0 + t - 1, y1); self.box(x1 - t + 1, y0, x1, y1)

    def glyph(self, x, y, char, scale):
        rows = FONT.get(char)
        if rows is None:
            return
        for row, line in enumerate(rows):
            for column, bit in enumerate(line):
                if bit == '1':
                    self.box(x + column * scale, y + row * scale,
                             x + column * scale + scale - 1, y + row * scale + scale - 1)

    def text(self, x, y, message, scale):
        cursor = x
        for char in message:
            self.glyph(cursor, y, char, scale)
            cursor += 6 * scale
        return cursor

    def text_u32(self, x, y, value, scale):
        return self.text(x, y, str(value), scale)

    def feed_scale(self, first_line, lines, x_base):
        for line in range(first_line, first_line + lines + 1):
            offset = line - first_line
            if offset % (10 * LINES_PER_MM) == 0:
                self.box(x_base, line, x_base + 11, line)
            elif offset % (2 * LINES_PER_MM) == 0:
                self.box(x_base, line, x_base + 4, line)


def carve_maze(seed):
    wall = [0x0F] * (MAZE_COLS * MAZE_ROWS)
    seen = [0] * (MAZE_COLS * MAZE_ROWS)
    state = seed | 1

    def rnd():
        nonlocal state
        state = (state * 1664525 + 1013904223) & MASK32
        return state >> 8

    stack = [CHEST_ROW * MAZE_COLS + CHEST_COL]
    seen[stack[0]] = 1
    while stack:
        current = stack[-1]
        row, column = divmod(current, MAZE_COLS)
        options = []
        if row > 0 and not seen[(row - 1) * MAZE_COLS + column]:
            options.append(((row - 1) * MAZE_COLS + column, 1))
        if row + 1 < MAZE_ROWS and not seen[(row + 1) * MAZE_COLS + column]:
            options.append(((row + 1) * MAZE_COLS + column, 4))
        if column > 0 and not seen[row * MAZE_COLS + column - 1]:
            options.append((row * MAZE_COLS + column - 1, 8))
        if column + 1 < MAZE_COLS and not seen[row * MAZE_COLS + column + 1]:
            options.append((row * MAZE_COLS + column + 1, 2))
        if not options:
            stack.pop()
            continue
        nxt, mask = options[rnd() % len(options)]
        opposite = {1: 4, 4: 1, 2: 8, 8: 2}[mask]
        wall[current] &= ~mask & 0xFF
        wall[nxt] &= ~opposite & 0xFF
        seen[nxt] = 1
        stack.append(nxt)
    return wall


def draw_chest(c, cx, cy, half_w, half_h):
    left, right = cx - half_w, cx + half_w
    top, bottom = cy - half_h, cy + half_h
    seam = top + (half_h * 3) // 4
    for row in range(seam - top + 1):
        inset = (6 - row) * 3 if row < 6 else 0
        c.box(left + inset, top + row, left + inset + 2, top + row)
        c.box(right - inset - 2, top + row, right - inset, top + row)
    c.box(left + 18, top, right - 18, top + 1)
    c.box(left, seam, right, seam + 2)
    c.rect_outline(left, seam + 3, right, bottom, 3)
    c.box(cx - (half_w // 2) - 2, seam + 3, cx - (half_w // 2) + 1, bottom)
    c.box(cx + (half_w // 2) - 1, seam + 3, cx + (half_w // 2) + 2, bottom)
    c.box(cx - 9, seam - 3, cx + 9, seam + 9)
    c.clear_box(cx - 3, seam, cx + 3, seam + 5)


def render_maze(c, seed):
    wall = carve_maze(seed)
    for row in range(CHEST_ROW - 1, CHEST_ROW + 2):
        for column in range(CHEST_COL - 1, CHEST_COL + 2):
            index = row * MAZE_COLS + column
            if column < CHEST_COL + 1:
                wall[index] &= ~2 & 0xFF
                wall[index + 1] &= ~8 & 0xFF
            if row < CHEST_ROW + 1:
                wall[index] &= ~4 & 0xFF
                wall[index + MAZE_COLS] &= ~1 & 0xFF
    left = MAZE_X0 - 3
    right = MAZE_X0 + MAZE_COLS * CELL_X
    c.box(left, MAZE_Y0 - 3, right, MAZE_Y0 - 2)
    c.box(left, MAZE_Y0 + MAZE_LINES, right, MAZE_Y0 + MAZE_LINES + 1)
    c.box(left, MAZE_Y0 - 3, left + 1, MAZE_Y0 + MAZE_LINES + 1)
    c.box(right, MAZE_Y0 - 3, right + 1, MAZE_Y0 + MAZE_LINES + 1)
    for row in range(MAZE_ROWS):
        for column in range(MAZE_COLS):
            index = row * MAZE_COLS + column
            x = MAZE_X0 + column * CELL_X
            y = MAZE_Y0 + row * CELL_Y
            if wall[index] & 1:
                c.box(x, y, x + CELL_X, y + WALL_THICKNESS - 1)
            if wall[index] & 8:
                c.box(x, y, x + WALL_THICKNESS - 1, y + CELL_Y)
            if row + 1 == MAZE_ROWS and wall[index] & 4:
                c.box(x, y + CELL_Y, x + CELL_X, y + CELL_Y + WALL_THICKNESS - 1)
            if column + 1 == MAZE_COLS and wall[index] & 2:
                c.box(x + CELL_X, y, x + CELL_X + WALL_THICKNESS - 1, y + CELL_Y)
    doorway = MAZE_X0 + CHEST_COL * CELL_X + CELL_X // 2
    c.clear_box(doorway - 8, MAZE_Y0 - 3, doorway + 8, MAZE_Y0 + CELL_Y - 1)
    c.clear_box(doorway - 8, MAZE_Y0 + MAZE_LINES - CELL_Y + 1, doorway + 8, MAZE_Y0 + MAZE_LINES + 1)
    draw_chest(c, MAZE_X0 + CHEST_COL * CELL_X + CELL_X // 2,
               MAZE_Y0 + CHEST_ROW * CELL_Y + CELL_Y // 2, 55, 40)
    c.text(MAZE_X0, 3, "MAZE 20CM", 2)
    cursor = c.text(MAZE_X0 + 168, 3, "SEED", 2)
    c.text_u32(cursor, 3, seed, 2)
    c.feed_scale(MAZE_Y0, MAZE_LINES, 2)


def render_card(c):
    c.text(16, 6, "DOT TEST 384", 2)
    c.text(16, 26, "10MM SQUARES", 1)
    c.box(0, 44, HEAD_DOTS - 1, 49)
    c.rect_outline(16, 60, 95, 139, 2)
    c.box(128, 60, 207, 139)
    for row in range(20):
        for column in range(23):
            x = 24 + column * 16
            y = 170 + row * 8
            c.box(x, y, x + 1, y + 1)
    c.text(24, 340, "DOT PITCH ACROSS 16 ALONG 8", 1)
    c.text(16, 360, "ACROSS GAP 1-12 DOTS", 1)
    for gap in range(1, 13):
        x = 20 + (gap - 1) * 30
        c.box(x, 376, x, 400)
        c.box(x + 1 + gap, 376, x + 1 + gap, 400)
    c.text(16, 420, "ALONG GAP 1-12 LINES", 1)
    for gap in range(1, 13):
        y = 440 + (gap - 1) * 14
        c.box(20, y, 150, y)
        c.box(20, y + 1 + gap, 150, y + 1 + gap)
    c.box(180, 440, 359, 440 + 10 * LINES_PER_MM - 1)
    c.box(180, 530, 359, 530 + 5 * LINES_PER_MM - 1)
    c.text(184, 580, "10MM AND 5MM ALONG FEED", 1)
    if c.lines > 620:
        c.feed_scale(0, 620, 2)
        c.rect_outline(0, 0, HEAD_DOTS - 1, 619, 2)



LADDER_SQUARES, LADDER_SIDE, LADDER_SLOT, LADDER_GAP = 6, 60, 64, 24


def render_checker(c):
    for y in range(HEAD_DOTS):
        for x in range(HEAD_DOTS):
            if (x + y) % 2 == 0:
                c.put(x, y)
    base = HEAD_DOTS + LADDER_GAP
    for index, cell in enumerate((1, 2, 3, 4, 6, 8)):
        x0 = index * LADDER_SLOT + (LADDER_SLOT - LADDER_SIDE) // 2
        for y in range(LADDER_SIDE):
            for x in range(LADDER_SIDE):
                if ((x // cell) + (y // cell)) % 2 == 0:
                    c.put(x0 + x, base + y)
        c.text_u32(x0 + LADDER_SIDE // 2 - 6, base + LADDER_SIDE + 12, cell, 2)
    c.text(16, base + LADDER_SIDE + 44, "CELL SIZE IN DOTS", 1)



TONE_ROWS, TONE_ROW_H, TONE_COL_W = 16, 24, 128
BAYER8 = [[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26], [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22], [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25], [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]


def render_tone(c):
    c.text(6, 2, "GRAY", 2)
    c.text(TONE_COL_W + 6, 2, "BAYER", 2)
    c.text(2 * TONE_COL_W + 6, 2, "2X2", 2)
    top = 22
    for row in range(TONE_ROWS):
        darkness = row * 17
        for y in range(top, top + TONE_ROW_H):
            for x in range(TONE_COL_W):
                c.set(x, y, 255 - darkness)
                c.set(TONE_COL_W + x, y, 0 if darkness > BAYER8[y & 7][x & 7] * 4 else 255)
                dots = (darkness * 4 + 127) // 255
                cell = (y & 1) * 2 + (x & 1)
                c.set(2 * TONE_COL_W + x, y, 0 if cell < dots else 255)
        top += TONE_ROW_H
    c.text(6, top + 6, "ROWS: WHITE TO BLACK, 16 STEPS", 1)
    c.text(6, top + 20, "THRESHOLD 0 = RAMP 1 = FIXED 128", 1)


def summarise(c):
    ink = sum(1 for value in c.data if value == 0)
    digest = 2166136261
    for value in c.data:
        digest = ((digest ^ value) * 16777619) & MASK32
    return ink, digest


def preview(c, path):
    image = Image.new('L', (HEAD_DOTS, c.lines), 255)
    pixels = image.load()
    for y in range(c.lines):
        for x in range(HEAD_DOTS):
            pixels[x, y] = c.data[x * c.lines + y]
    image.save(path)
    return len(c.data)


if __name__ == '__main__':
    out = Path('/tmp/kc02-board-re/hw/post-repair')
    for name, lines, builder in (('maze', 1624, lambda c: render_maze(c, 3292)),
                                 ('card', 660, render_card),
                                 ('checker', 504, render_checker),
                                 ('tone', 440, render_tone)):
        canvas = Canvas(lines)
        builder(canvas)
        ink, digest = summarise(canvas)
        preview(canvas, out / f'model_{name}.png')
        print(f'{name:5s} lines={lines} ink={ink} hash=0x{digest:08x}')
