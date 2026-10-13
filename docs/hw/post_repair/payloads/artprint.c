/* Larger-format print payload for the KC02.

   Pattern 10: a 20 cm full-width maze, treasure chest in the middle cell and a
               centimetre scale down the left margin.
   Pattern 11: a dot-resolution and scale card.

   Canvas layout matches ramprint.c: luma, row-major, canvas[head * w + line].
   head = 0..383 is across the paper, line = 0..w-1 is along the paper.
   0x00 = ink, 0xFF = paper. The geometry assumes 8 feed steps per millimetre; the
   printed centimetre scale lets that be checked on the paper with a rule.

   INPUT[0] = w (feed lines, 8..1800)    INPUT[1] = energy 0..3
   INPUT[2] = threshold mode             INPUT[3] = 0 build only / 1 print
   INPUT[4] = param6 (0 = live app byte)  INPUT[5] = pattern (10 maze, 11 card)
   INPUT[6] = maze seed
*/
#ifndef TEST_ID
#define TEST_ID 0x0A721000
#endif
#include "common.h"
#include "art_font.inc"

typedef void *(*alloc_t)(u32 size, u32 align);
typedef void (*free_t)(void *buffer);
typedef void (*fn0_t)(void);
typedef int (*run_a_t)(void *canvas, u32 w, u32 h, u32 energy, u32 thr, u32 p6);
#define MALLOC_FN 0x0203E5C4u
#define FREE_FN 0x0203E694u
#define PRN_INIT 0x02059F84u
#define RUN_A 0x0205A6C4u
#define APP_CTX 0x020D416Cu
#define MAILBOX 0x020DB300u
#define MAILBOX_MAGIC 0x4D424F58u

#define HEAD_DOTS 384u
#define LINES_PER_MM 8u
#define HEADER_LINES 24u

#define MAZE_COLS 9u
#define MAZE_ROWS 40u
#define CELL_X 40u
#define CELL_Y 40u
#define MAZE_X0 20u
#define MAZE_Y0 HEADER_LINES
#define MAZE_CELLS (MAZE_COLS * MAZE_ROWS)
#define CHEST_COL 4u
#define CHEST_ROW 20u
#define WALL_THICKNESS 2u

static uint8_t *canvas;
static u32 canvas_w;

static void put(u32 x, u32 y)
{
    if (x < HEAD_DOTS && y < canvas_w) {
        canvas[x * canvas_w + y] = 0x00u;
    }
}
static void erase(u32 x, u32 y)
{
    if (x < HEAD_DOTS && y < canvas_w) {
        canvas[x * canvas_w + y] = 0xFFu;
    }
}
static void box(u32 x0, u32 y0, u32 x1, u32 y1)
{
    for (u32 y = y0; y <= y1; ++y) {
        for (u32 x = x0; x <= x1; ++x) {
            put(x, y);
        }
    }
}
static void clear_box(u32 x0, u32 y0, u32 x1, u32 y1)
{
    for (u32 y = y0; y <= y1; ++y) {
        for (u32 x = x0; x <= x1; ++x) {
            erase(x, y);
        }
    }
}
static void rect_outline(u32 x0, u32 y0, u32 x1, u32 y1, u32 thickness)
{
    box(x0, y0, x1, y0 + thickness - 1u);
    box(x0, y1 - thickness + 1u, x1, y1);
    box(x0, y0, x0 + thickness - 1u, y1);
    box(x1 - thickness + 1u, y0, x1, y1);
}
static void glyph(u32 x, u32 y, char character, u32 scale)
{
    const unsigned char *rows = (const unsigned char *)0;
    for (u32 index = 0; index < sizeof(ART_FONT) / sizeof(ART_FONT[0]); ++index) {
        if ((char)ART_FONT[index][0] == character) {
            rows = ART_FONT[index] + 1;
            break;
        }
    }
    if (rows == (const unsigned char *)0) {
        return;
    }
    for (u32 row = 0; row < ART_FONT_ROWS; ++row) {
        unsigned char bits = rows[row];
        for (u32 column = 0; column < 5u; ++column) {
            if ((bits >> (4u - column)) & 1u) {
                box(x + column * scale, y + row * scale,
                    x + column * scale + scale - 1u, y + row * scale + scale - 1u);
            }
        }
    }
}
static u32 text(u32 x, u32 y, const char *message, u32 scale)
{
    u32 cursor = x;
    for (const char *walk = message; *walk != '\0'; ++walk) {
        glyph(cursor, y, *walk, scale);
        cursor += 6u * scale;
    }
    return cursor;
}
static u32 text_u32(u32 x, u32 y, u32 value, u32 scale)
{
    char digits[11];
    u32 count = 0u;
    if (value == 0u) {
        digits[count++] = '0';
    }
    while (value != 0u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    u32 cursor = x;
    while (count != 0u) {
        glyph(cursor, y, digits[--count], scale);
        cursor += 6u * scale;
    }
    return cursor;
}

/* Centimetre scale: 12-dot marks every cm, 5-dot marks every 2 mm. */
static void feed_scale(u32 first_line, u32 lines, u32 x_base)
{
    for (u32 line = first_line; line <= first_line + lines; ++line) {
        u32 offset = line - first_line;
        if (offset % (10u * LINES_PER_MM) == 0u) {
            box(x_base, line, x_base + 11u, line);
        } else if (offset % (2u * LINES_PER_MM) == 0u) {
            box(x_base, line, x_base + 4u, line);
        }
    }
}

/* Wall bits: 1 north, 2 east, 4 south, 8 west. */
static uint32_t random_state;
static u32 next_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state >> 8;
}

static void carve_maze(uint8_t *wall, uint8_t *seen, int32_t *stack, u32 seed)
{
    random_state = seed | 1u;
    for (u32 index = 0; index < MAZE_CELLS; ++index) {
        wall[index] = 0x0Fu;
        seen[index] = 0u;
    }
    int32_t *top = stack;
    int32_t start = (int32_t)(CHEST_ROW * MAZE_COLS + CHEST_COL);
    *top++ = start;
    seen[start] = 1u;
    while (top != stack) {
        int32_t current = *(top - 1);
        u32 row = (u32)current / MAZE_COLS;
        u32 column = (u32)current % MAZE_COLS;
        int32_t options[4];
        u32 mask[4];
        u32 count = 0u;
        if (row > 0u && !seen[(row - 1u) * MAZE_COLS + column]) {
            options[count] = (int32_t)((row - 1u) * MAZE_COLS + column); mask[count++] = 1u;
        }
        if (row + 1u < MAZE_ROWS && !seen[(row + 1u) * MAZE_COLS + column]) {
            options[count] = (int32_t)((row + 1u) * MAZE_COLS + column); mask[count++] = 4u;
        }
        if (column > 0u && !seen[row * MAZE_COLS + column - 1u]) {
            options[count] = (int32_t)(row * MAZE_COLS + column - 1u); mask[count++] = 8u;
        }
        if (column + 1u < MAZE_COLS && !seen[row * MAZE_COLS + column + 1u]) {
            options[count] = (int32_t)(row * MAZE_COLS + column + 1u); mask[count++] = 2u;
        }
        if (count == 0u) {
            --top;
            continue;
        }
        u32 choice = next_random() % count;
        int32_t next = options[choice];
        u32 opposite = (mask[choice] == 1u) ? 4u : (mask[choice] == 4u) ? 1u
                     : (mask[choice] == 2u) ? 8u : 2u;
        wall[current] &= (uint8_t)~(uint8_t)mask[choice];
        wall[next] &= (uint8_t)~(uint8_t)opposite;
        seen[next] = 1u;
        *top++ = next;
    }
}

static void draw_chest(u32 cx, u32 cy, u32 half_w, u32 half_h)
{
    u32 left = cx - half_w, right = cx + half_w;
    u32 top = cy - half_h, bottom = cy + half_h;
    u32 seam = top + (half_h * 3u) / 4u;
    for (u32 row = 0; row <= (seam - top); ++row) {          /* hollow domed lid */
        u32 inset = (row < 6u) ? (6u - row) * 3u : 0u;
        box(left + inset, top + row, left + inset + 2u, top + row);
        box(right - inset - 2u, top + row, right - inset, top + row);
    }
    box(left + 18u, top, right - 18u, top + 1u);             /* dome crest */
    box(left, seam, right, seam + 2u);                       /* lid seam */
    rect_outline(left, seam + 3u, right, bottom, 3u);        /* body */
    box(cx - (half_w / 2u) - 2u, seam + 3u, cx - (half_w / 2u) + 1u, bottom);
    box(cx + (half_w / 2u) - 1u, seam + 3u, cx + (half_w / 2u) + 2u, bottom);
    box(cx - 9u, seam - 3u, cx + 9u, seam + 9u);             /* lock */
    clear_box(cx - 3u, seam, cx + 3u, seam + 5u);
}

static void render_maze(u32 seed, u32 lines)
{
    const u32 maze_lines = MAZE_ROWS * CELL_Y;
    (void)lines;
    uint8_t *wall = (uint8_t *)((alloc_t)MALLOC_FN)(MAZE_CELLS, 0x20u);
    uint8_t *seen = (uint8_t *)((alloc_t)MALLOC_FN)(MAZE_CELLS, 0x20u);
    int32_t *stack = (int32_t *)((alloc_t)MALLOC_FN)(MAZE_CELLS * 4u, 0x20u);
    if (wall == (uint8_t *)0x0 || seen == (uint8_t *)0x0 || stack == (int32_t *)0x0) {
        if (wall != (uint8_t *)0x0) { ((free_t)FREE_FN)(wall); }
        if (seen != (uint8_t *)0x0) { ((free_t)FREE_FN)(seen); }
        if (stack != (int32_t *)0x0) { ((free_t)FREE_FN)(stack); }
        return;
    }
    carve_maze(wall, seen, stack, seed);
    ((free_t)FREE_FN)(seen);
    ((free_t)FREE_FN)(stack);
    /* Open the 3x3 block around the chest into one treasure room. */
    for (u32 row = CHEST_ROW - 1u; row <= CHEST_ROW + 1u; ++row) {
        for (u32 column = CHEST_COL - 1u; column <= CHEST_COL + 1u; ++column) {
            u32 index = row * MAZE_COLS + column;
            if (column < CHEST_COL + 1u) {
                wall[index] &= (uint8_t)~2u;
                wall[index + 1u] &= (uint8_t)~8u;
            }
            if (row < CHEST_ROW + 1u) {
                wall[index] &= (uint8_t)~4u;
                wall[index + MAZE_COLS] &= (uint8_t)~1u;
            }
        }
    }

    u32 left = MAZE_X0 - 3u, right = MAZE_X0 + MAZE_COLS * CELL_X;
    box(left, MAZE_Y0 - 3u, right, MAZE_Y0 - 2u);                       /* frame */
    box(left, MAZE_Y0 + maze_lines, right, MAZE_Y0 + maze_lines + 1u);
    box(left, MAZE_Y0 - 3u, left + 1u, MAZE_Y0 + maze_lines + 1u);
    box(right, MAZE_Y0 - 3u, right + 1u, MAZE_Y0 + maze_lines + 1u);

    for (u32 row = 0; row < MAZE_ROWS; ++row) {
        for (u32 column = 0; column < MAZE_COLS; ++column) {
            u32 index = row * MAZE_COLS + column;
            u32 x = MAZE_X0 + column * CELL_X;
            u32 y = MAZE_Y0 + row * CELL_Y;
            if (wall[index] & 1u) { box(x, y, x + CELL_X, y + WALL_THICKNESS - 1u); }
            if (wall[index] & 8u) { box(x, y, x + WALL_THICKNESS - 1u, y + CELL_Y); }
            if (row + 1u == MAZE_ROWS && (wall[index] & 4u)) {
                box(x, y + CELL_Y, x + CELL_X, y + CELL_Y + WALL_THICKNESS - 1u);
            }
            if (column + 1u == MAZE_COLS && (wall[index] & 2u)) {
                box(x + CELL_X, y, x + CELL_X + WALL_THICKNESS - 1u, y + CELL_Y);
            }
        }
    }
    ((free_t)FREE_FN)(wall);

    /* entry and exit in the chest column */
    u32 doorway = MAZE_X0 + CHEST_COL * CELL_X + CELL_X / 2u;
    clear_box(doorway - 8u, MAZE_Y0 - 3u, doorway + 8u, MAZE_Y0 + CELL_Y - 1u);
    clear_box(doorway - 8u, MAZE_Y0 + maze_lines - CELL_Y + 1u, doorway + 8u, MAZE_Y0 + maze_lines + 1u);
    draw_chest(MAZE_X0 + CHEST_COL * CELL_X + CELL_X / 2u,
               MAZE_Y0 + CHEST_ROW * CELL_Y + CELL_Y / 2u, 55u, 40u);

    text(MAZE_X0, 3u, "MAZE 20CM", 2u);
    u32 cursor = text(MAZE_X0 + 168u, 3u, "SEED", 2u);
    text_u32(cursor, 3u, seed, 2u);
    feed_scale(MAZE_Y0, maze_lines, 2u);
}

static void render_card(u32 lines)
{
    text(16u, 6u, "DOT TEST 384", 2u);
    text(16u, 26u, "10MM SQUARES", 1u);
    box(0u, 44u, HEAD_DOTS - 1u, 49u);                 /* full head width */
    rect_outline(16u, 60u, 95u, 139u, 2u);             /* 10 mm outline */
    box(128u, 60u, 207u, 139u);                        /* 10 mm solid */
    for (u32 row = 0; row < 20u; ++row) {              /* dot pitch grid 16 x 8 */
        for (u32 column = 0; column < 23u; ++column) {
            u32 x = 24u + column * 16u;
            u32 y = 170u + row * 8u;
            box(x, y, x + 1u, y + 1u);
        }
    }
    text(24u, 340u, "DOT PITCH ACROSS 16 ALONG 8", 1u);
    text(16u, 360u, "ACROSS GAP 1-12 DOTS", 1u);
    for (u32 gap = 1u; gap <= 12u; ++gap) {
        u32 x = 20u + (gap - 1u) * 30u;
        box(x, 376u, x, 400u);
        box(x + 1u + gap, 376u, x + 1u + gap, 400u);
    }
    text(16u, 420u, "ALONG GAP 1-12 LINES", 1u);
    for (u32 gap = 1u; gap <= 12u; ++gap) {
        u32 y = 440u + (gap - 1u) * 14u;
        box(20u, y, 150u, y);
        box(20u, y + 1u + gap, 150u, y + 1u + gap);
    }
    box(180u, 440u, 359u, 440u + 10u * LINES_PER_MM - 1u);   /* 10 mm reference */
    box(180u, 530u, 359u, 530u + 5u * LINES_PER_MM - 1u);    /* 5 mm reference */
    text(184u, 580u, "10MM AND 5MM ALONG FEED", 1u);
    if (lines > 620u) {
        feed_scale(0u, 620u, 2u);
        rect_outline(0u, 0u, HEAD_DOTS - 1u, 619u, 2u);
    }
}

/* Pattern 12: the smallest checkerboard the head allows, on a full-width square, plus a
   pitch ladder (1,2,3,4,6,8 dot cells) so the resolving pitch can be read off the paper. */
#define LADDER_SQUARES 6u
#define LADDER_SIDE 60u
#define LADDER_SLOT 64u
#define LADDER_GAP 24u

static void render_checker(u32 lines)
{
    (void)lines;
    for (u32 y = 0; y < HEAD_DOTS; ++y) {                    /* full-width 1-dot checker */
        for (u32 x = 0; x < HEAD_DOTS; ++x) {
            if (((x + y) & 1u) == 0u) {
                put(x, y);
            }
        }
    }
    static const u32 pitch[LADDER_SQUARES] = {1u, 2u, 3u, 4u, 6u, 8u};
    u32 base = HEAD_DOTS + LADDER_GAP;
    for (u32 index = 0; index < LADDER_SQUARES; ++index) {
        u32 cell = pitch[index];
        u32 x0 = index * LADDER_SLOT + (LADDER_SLOT - LADDER_SIDE) / 2u;
        for (u32 y = 0; y < LADDER_SIDE; ++y) {
            for (u32 x = 0; x < LADDER_SIDE; ++x) {
                if ((((x / cell) + (y / cell)) & 1u) == 0u) {
                    put(x0 + x, base + y);
                }
            }
        }
        text_u32(x0 + LADDER_SIDE / 2u - 6u, base + LADDER_SIDE + 12u, cell, 2u);
    }
    text(16u, base + LADDER_SIDE + 44u, "CELL SIZE IN DOTS", 1u);
}


/* Pattern 13: the same 16-step tone ramp rendered three ways side by side, so the printed
   sheet shows native per-dot grayscale against two 1-bit dithers.
     column A (0-127)   true luma        -> the head's 32-level strobe ramp
     column B (128-255) 8x8 Bayer order  -> one-pixel ordered dither
     column C (256-383) 2x2 clustered    -> 2-dot clustered dither, 5 levels per cell
   Print once with threshold mode 0 (ramp) and once with 1 (fixed 128) to compare modes. */
#define TONE_ROWS 16u
#define TONE_ROW_H 24u
#define TONE_COL_W 128u

static const unsigned char BAYER8[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 },
};

static void tone(u32 x, u32 y, u32 value)
{
    if (x < HEAD_DOTS && y < canvas_w) {
        canvas[x * canvas_w + y] = (uint8_t)value;
    }
}

static void render_tone(u32 lines)
{
    (void)lines;
    text(6u, 2u, "GRAY", 2u);
    text(TONE_COL_W + 6u, 2u, "BAYER", 2u);
    text(2u * TONE_COL_W + 6u, 2u, "2X2", 2u);
    u32 top = 22u;
    for (u32 row = 0; row < TONE_ROWS; ++row) {
        u32 darkness = row * 17u;                      /* 0 = white paper, 255 = black */
        for (u32 y = top; y < top + TONE_ROW_H; ++y) {
            for (u32 x = 0; x < TONE_COL_W; ++x) {
                tone(x, y, 255u - darkness);
                tone(TONE_COL_W + x, y, (darkness > (u32)BAYER8[y & 7u][x & 7u] * 4u) ? 0u : 255u);
                u32 dots = (darkness * 4u + 127u) / 255u;
                u32 cell = (y & 1u) * 2u + (x & 1u);
                tone(2u * TONE_COL_W + x, y, (cell < dots) ? 0u : 255u);
            }
        }
        top += TONE_ROW_H;
    }
    text(6u, top + 6u, "ROWS: WHITE TO BLACK, 16 STEPS", 1u);
    text(6u, top + 20u, "THRESHOLD 0 = RAMP 1 = FIXED 128", 1u);
}

/* Must sit at the start of the code image: the RAM dispatcher jumps to CODE_ADDR. */
__attribute__((section(".entry"), used, noinline))
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    volatile u32 *mb = (volatile u32 *)MAILBOX;
    alloc_t sysMalloc = (alloc_t)MALLOC_FN;
    fn0_t printerInit = (fn0_t)PRN_INIT;
    run_a_t printRun = (run_a_t)RUN_A;
    u32 w = in[0], energy = in[1], threshold = in[2], go = in[3], p6 = in[4];
    u32 pattern = in[5], seed = in[6];

    if (w < 8u) { w = 48u; }
    if (w > 1800u) { w = 1800u; }
    canvas = (uint8_t *)sysMalloc(w * HEAD_DOTS, 0x20u);
    canvas_w = w;
    out[0] = (u32)canvas;
    out[1] = w;
    if (canvas == (uint8_t *)0x0) {
        out[2] = 0xE1u;
        finish(buffer, length, parameter, 2);
        return;
    }
    for (u32 index = 0; index < w * HEAD_DOTS; ++index) {
        canvas[index] = 0xFFu;
    }
    if (pattern == 10u) {
        render_maze(seed, w);
    } else if (pattern == 12u) {
        render_checker(w);
    } else if (pattern == 13u) {
        render_tone(w);
    } else {
        render_card(w);
    }
    u32 ink = 0u;
    u32 hash = 2166136261u;
    for (u32 index = 0; index < w * HEAD_DOTS; ++index) {
        u32 value = canvas[index];
        if (value == 0u) { ++ink; }
        hash = (hash ^ value) * 16777619u;
    }
    out[3] = ink;
    out[4] = hash;
    out[5] = *(volatile unsigned char *)(APP_CTX + 0x60u);
    out[6] = *(volatile unsigned char *)(APP_CTX + 0x61u);
    if (go) {
        if (p6 == 0u) { p6 = *(volatile unsigned char *)(APP_CTX); }
        printerInit();
        out[7] = (u32)printRun(canvas, w, HEAD_DOTS, energy, threshold, p6);
    }
    ((free_t)FREE_FN)(canvas);              /* run_A is finished with the canvas */
    canvas = (uint8_t *)0x0;
    mb[0] = MAILBOX_MAGIC; mb[1] = go; mb[2] = out[7]; mb[3] = out[0]; mb[4] = out[3];
    CACHE_clean((void *)MAILBOX, 0x40, 0);
    finish(buffer, length, parameter, go ? 3 : 1);
}
