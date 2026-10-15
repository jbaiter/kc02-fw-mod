/* GPIO thermal head + platen; records firmware output, not source images.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * KC02: PORTE DATA15/CLK13, PORTB LATCH5/POWER6, PORTA STROBE9.
 * Motor phase vectors {10,8,9,1,5,4,6,2}; four full steps per logical dot row.
 * PNG is ideal strobe duty, not a calibrated thermal/paper/temperature model.
 *
 * TODO(approximation): grayscale is normalised strobe duty with no thermal
 * energy or head-temperature model, and the platen advances four abstract steps
 * per dot row with no real mechanics.
 */
#include <png.h>

#define AX_PRINT_DOTS 384
#define AX_PRINT_ROWS 8192

typedef struct AxPrinter {
    const char *directory;
    bool paper, active, overflow, trace;
    uint8_t shift[AX_PRINT_DOTS], latch[AX_PRINT_DOTS];
    unsigned shift_pos, step, rows, jobs;
    int phase;
    unsigned last_vector;
    unsigned phases[8];
    uint16_t *hits;
    uint16_t strobes[AX_PRINT_ROWS];
    uint64_t clocks, latches, pulses, bad_steps;
} AxPrinter;

static bool ax_printer_png(FILE *file, uint8_t *pixels, unsigned height)
{
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); return false; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_init_io(png, file);
    png_set_IHDR(png, info, AX_PRINT_DOTS, height, 8, PNG_COLOR_TYPE_GRAY,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (unsigned y = 0; y < height; y++) {
        png_write_row(png, pixels + y * AX_PRINT_DOTS);
    }
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    return true;
}

static void ax_printer_finish(AxPrinter *p)
{
    p->active = false;
    if (!p->pulses || !p->rows) { return; } /* boot supply pulses are not jobs */
    if (p->overflow) {
        fprintf(stderr, "printer: discarded page exceeding bounded raster\n");
        return;
    }
    unsigned maximum = 1;
    for (unsigned y = 0; y < p->rows; y++) {
        maximum = MAX(maximum, p->strobes[y]);
    }
    uint8_t *pixels = g_try_malloc((size_t)p->rows * AX_PRINT_DOTS);
    if (!pixels) { fprintf(stderr, "printer: cannot allocate PNG raster\n"); return; }
    for (unsigned y = 0; y < p->rows; y++) {
        for (unsigned x = 0; x < AX_PRINT_DOTS; x++) {
            unsigned n = p->hits[y * AX_PRINT_DOTS + x];
            pixels[y * AX_PRINT_DOTS + x] = 255 - (n * 255 + maximum / 2) / maximum;
        }
    }
    char *path = NULL, *part = NULL;
    int fd;
    struct stat st;
    do {
        g_free(path); g_free(part);
        path = g_strdup_printf("%s/print-%05u.png", p->directory, ++p->jobs);
        part = g_strdup_printf("%s.part", path);
        if (!lstat(path, &st)) { fd = -1; errno = EEXIST; }
        else { fd = open(part, O_WRONLY | O_CREAT | O_EXCL, 0644); }
    } while (fd < 0 && errno == EEXIST && p->jobs < 1000000);
    if (fd < 0) {
        fprintf(stderr, "printer: cannot create %s: %s\n", path, strerror(errno));
        g_free(path); g_free(part); g_free(pixels); return;
    }
    FILE *file = fdopen(fd, "wb");
    bool ok = false;
    if (file) {
        ok = ax_printer_png(file, pixels, p->rows);
        if (fclose(file)) { ok = false; }
    } else { close(fd); }
    /* Publish a complete file atomically, without overwriting a competing
     * emulator's file or exposing an unfinished PNG to directory watchers. */
    if (ok && link(part, path)) { ok = false; }
    unlink(part);
    if (!ok) {
        fprintf(stderr, "printer: failed to publish PNG %s\n", path);
    } else {
        fprintf(stderr, "printer: %s %ux%u clocks=%" PRIu64
                " latches=%" PRIu64 " strobes=%" PRIu64 " max_per_row=%u steps=%u bad_steps=%" PRIu64 "\n",
                path, AX_PRINT_DOTS, p->rows, p->clocks, p->latches,
                p->pulses, maximum, p->step, p->bad_steps);
    }
    g_free(path); g_free(part); g_free(pixels);
}

static void ax_printer_start(AxPrinter *p)
{
    if (!p->hits) { p->hits = g_try_new0(uint16_t, AX_PRINT_ROWS * AX_PRINT_DOTS); }
    if (!p->hits) { fprintf(stderr, "printer: cannot allocate head raster\n"); return; }
    memset(p->hits, 0, AX_PRINT_ROWS * AX_PRINT_DOTS * sizeof(*p->hits));
    memset(p->strobes, 0, sizeof(p->strobes));
    memset(p->shift, 0, sizeof(p->shift));
    memset(p->latch, 0, sizeof(p->latch));
    p->shift_pos = p->step = p->rows = 0;
    p->phase = -1;
    p->last_vector = 0xff;
    p->clocks = p->latches = p->pulses = p->bad_steps = 0;
    p->overflow = false;
    p->active = true;
}

static void ax_printer_gpio(AxPrinter *p, unsigned spr, uint32_t old,
                            uint32_t value, const uint32_t *regs)
{
    if (!p->directory) { return; }
    if (spr == 0x8004 && ((old ^ value) & 0x40)) {
        if (value & 0x40) { ax_printer_start(p); }
        else { ax_printer_finish(p); }
    }
    if (!p->active) { return; }
    if (p->trace) {
        unsigned v = (regs[0x800c >> 2] & 1) |
                     ((regs[0x8010 >> 2] & 0x400) >> 9) |
                     ((regs[0x8000 >> 2] & 8) >> 1) |
                     ((regs[0x8010 >> 2] & 0x800) >> 8);
        if (v != p->last_vector) {
            fprintf(stderr, "printer-vector: %u spr=%04x value=%08x step=%u\n",
                    v, spr, value, p->step);
            p->last_vector = v;
        }
    }
    if (spr == 0x800c && !(old & 0x2000) && (value & 0x2000)) {
        p->shift[p->shift_pos] = !!(value & 0x8000);
        p->shift_pos = (p->shift_pos + 1) % AX_PRINT_DOTS;
        p->clocks++;
    }
    if (spr == 0x8004 && !(old & 0x20) && (value & 0x20)) {
        for (unsigned x = 0; x < AX_PRINT_DOTS; x++) {
            p->latch[x] = p->shift[(p->shift_pos + x) % AX_PRINT_DOTS];
        }
        p->latches++;
    }
    if ((spr == 0x8000 && ((old ^ value) & 8)) ||
        (spr == 0x800c && ((old ^ value) & 1)) ||
        (spr == 0x8010 && ((old ^ value) & 0xc00))) {
        unsigned vector = (regs[0x800c >> 2] & 1) |
                          ((regs[0x8010 >> 2] & 0x400) >> 9) |
                          ((regs[0x8000 >> 2] & 8) >> 1) |
                          ((regs[0x8010 >> 2] & 0x800) >> 8);
        for (unsigned i = 0; i < 8; i++) {
            if (vector != p->phases[i]) { continue; }            if (p->phase >= 0 && i != (unsigned)p->phase) {
                if (i == ((unsigned)p->phase + 1) % 8) { p->step++; }
                else if (i == ((unsigned)p->phase + 7) % 8) {
                    if (p->step) { p->step--; }
                } else { p->bad_steps++; }
            }
            p->phase = i;
            if (p->pulses) { p->rows = MAX(p->rows, p->step / 4 + 1); }
            break;
        }
    }
    if (spr == 0x8000 && !(old & 0x200) && (value & 0x200) && p->paper) {
        unsigned row = p->step / 4;
        if (row >= AX_PRINT_ROWS || p->strobes[row] == UINT16_MAX) {
            p->overflow = true; return;
        }
        p->rows = MAX(p->rows, row + 1);
        p->strobes[row]++;
        for (unsigned x = 0; x < AX_PRINT_DOTS; x++) {
            p->hits[row * AX_PRINT_DOTS + x] += p->latch[x];
        }
        p->pulses++;
    }
    if (p->rows > AX_PRINT_ROWS) { p->overflow = true; p->rows = AX_PRINT_ROWS; }
}
