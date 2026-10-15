/* AX3292 DE scanout: YUV420 video and indexed RGB565/A5 OSD.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * The KC02 panel scans 240x320 in portrait; its physical mounting is 90 CW.
 * No application addresses, sprites or menu state are consulted here.
 *
 * TODO(approximation): scanout is sampled on the model's tick, so panel timing,
 * tearing and backlight behaviour are not reproduced.  Alpha blending reads a
 * programmed palette rather than the panel's gamma.
 */
static void ax3292_key_on_cpu(CPUState *cpu, run_on_cpu_data data)
{
    Ax3292State *s = AX3292_MACHINE(qdev_get_machine());
    s->key_value = data.host_int;
}

static void ax3292_key_event(DeviceState *dev, QemuConsole *src,
                             QemuInputEvent *evt)
{
    Ax3292State *s = AX3292_MACHINE(qdev_get_machine());
    unsigned value;
    switch (qemu_input_linux_to_qcode(evt->key.key)) {
    case Q_KEY_CODE_UP: value = s->key_adc[0]; break;
    case Q_KEY_CODE_DOWN: value = s->key_adc[1]; break;
    case Q_KEY_CODE_LEFT: value = s->key_adc[2]; break;
    case Q_KEY_CODE_RIGHT: value = s->key_adc[3]; break;
    case Q_KEY_CODE_RET:
    case Q_KEY_CODE_KP_ENTER: value = s->key_adc[4]; break;
    case Q_KEY_CODE_ESC: value = s->key_adc[5]; break;
    case Q_KEY_CODE_SPC: value = s->key_adc[6]; break;
    case Q_KEY_CODE_P: value = s->key_adc[7]; break;
    case Q_KEY_CODE_F: value = s->key_adc[8]; break;
    case Q_KEY_CODE_TAB: value = s->key_adc[9]; break;
    case Q_KEY_CODE_M: value = s->key_adc[10]; break;
    default: return;
    }
    if (value > 1023) { return; } /* no corresponding button on this board */
    async_run_on_cpu(CPU(s->cpu), ax3292_key_on_cpu,
                     RUN_ON_CPU_HOST_INT(evt->key.down ? value : 1023));
}

static const QemuInputHandler ax3292_keyboard = {
    .name = "ax3292-keypad",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = ax3292_key_event,
};

static uint8_t ax3292_clip(int v)
{
    return MIN(255, MAX(0, v));
}

static void ax3292_display_update(void *opaque)
{
    Ax3292State *s = opaque;
    DisplaySurface *surface;
    if (!s->screen_pixels) {
        return;
    }
    /* SDL (and other GUI backends) must be called on the main thread.
     * Both this BH and the queued CPU producer own BQL, protecting pixels. */
    qemu_console_resize(s->console, s->screen_width, s->screen_height);
    surface = qemu_console_surface(s->console);
    for (unsigned y = 0; y < s->screen_height; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface_data(surface) +
                                    y * surface_stride(surface));
        for (unsigned x = 0; x < s->screen_width; x++) {
            uint8_t *p = s->screen_pixels + (y * s->screen_width + x) * 3;
            row[x] = 0xff000000u | (p[0] << 16) | (p[1] << 8) | p[2];
        }
    }
    qemu_console_update_full(s->console);
}

static void ax3292_scanout(Ax3292State *s, bool capture)
{
    uint32_t size = s->sfr[0xc810 >> 2];
    unsigned w = (size & 0xffff) + 1, h = (size >> 16) + 1;
    uint32_t enabled = s->sfr[0xc818 >> 2];
    uint32_t yaddr = ax3292_de_dma_address(s->sfr[0xc828 >> 2]);
    uint32_t uvaddr = ax3292_de_dma_address(s->sfr[0xc830 >> 2]);
    unsigned ys = s->sfr[0xc82c >> 2] * 4, uvs = s->sfr[0xc834 >> 2] * 4;
    uint32_t vsize = s->sfr[0xc820 >> 2], vpos = s->sfr[0xc824 >> 2];
    unsigned vw = (vsize & 0xffff) + 1, vh = (vsize >> 16) + 1;
    int vx = (int16_t)vpos, vy = (int16_t)(vpos >> 16);
    g_autofree uint8_t *rgb = NULL;

    if (w > 1024 || h > 1024 || !(s->sfr[0xa800 >> 2] & 1)) {
        return;
    }
    rgb = g_malloc0(w * h * 3);
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            uint8_t col[3] = { 0, 0, 0 };
            int px = (int)x - vx, py = (int)y - vy;
            unsigned scan = (enabled >> 8) & 7;
            int dx = (scan == 2 || scan == 3 || scan == 4) ? -1 : 1;
            int dy = (scan == 1 || scan == 3 || scan == 5) ? -1 : 1;
            int sx = px, sy = py;
            if (scan == 4 || scan == 5) { sx = py; sy = px; }
            /* HAL supplies the first scanned pixel, already offset to the
             * bottom/right for reverse scan (hal_lcdAddrCalculate). */
            int64_t yo = (int64_t)yaddr + dy * (int64_t)sy * ys + dx * sx;
            int64_t uvo = (int64_t)(uvaddr & ~1u) +
                          dy * (int64_t)(sy / 2) * uvs + dx * (sx & ~1);
            if ((enabled & 1) && px >= 0 && py >= 0 &&
                px < vw && py < vh && yaddr && uvaddr &&
                yo >= 0 && uvo >= 0 &&
                yo < AX3292_RAM_SIZE && uvo + 1 < AX3292_RAM_SIZE) {
                int lum = s->ram_ptr[yo];
                int u = s->ram_ptr[uvo] - 128, v = s->ram_ptr[uvo + 1] - 128;
                col[0] = ax3292_clip(lum + ((359 * v) >> 8));
                col[1] = ax3292_clip(lum - ((88 * u + 183 * v) >> 8));
                col[2] = ax3292_clip(lum + ((454 * u) >> 8));
            }
            for (unsigned layer = 0; layer < 2; layer++) {
                unsigned base = 0xc83c + layer * 0x10;
                uint32_t osize = s->sfr[base >> 2];
                uint32_t pos = s->sfr[(base + 4) >> 2];
                uint32_t addr = ax3292_de_dma_address(s->sfr[(base + 8) >> 2]);
                unsigned ow = (osize & 0xffff) + 1, oh = (osize >> 16) + 1;
                int ox = (int)x - (int16_t)pos;
                int oy = (int)y - (int16_t)(pos >> 16);
                uint64_t off = (uint64_t)addr + oy * (uint64_t)ow + ox;
                unsigned alpha_cfg = (s->sfr[0xc85c >> 2] >> (layer * 16)) & 0x3f;
                uint32_t palette;
                unsigned alpha, r, g, b;

                if (!(enabled & (2 << layer)) || !addr || ox < 0 || oy < 0 ||
                    ox >= ow || oy >= oh || off >= AX3292_RAM_SIZE) {
                    continue;
                }
                palette = ldl_le_p(s->ram_ptr + 0x50000 + layer * 2014 +
                                   s->ram_ptr[off] * 4);
                alpha = (palette >> 16) & 31;
                if (alpha_cfg & 0x20) {
                    alpha = (alpha_cfg & 15) * 31 / 15;
                }
                r = ((palette >> 11) & 31) * 255 / 31;
                g = ((palette >> 5) & 63) * 255 / 63;
                b = (palette & 31) * 255 / 31;
                col[0] = (col[0] * (31 - alpha) + r * alpha) / 31;
                col[1] = (col[1] * (31 - alpha) + g * alpha) / 31;
                col[2] = (col[2] * (31 - alpha) + b * alpha) / 31;
            }
            unsigned pixel = s->panel_ccw ? (w - x - 1) * h + y : x * h + h - y - 1;
            memcpy(rgb + pixel * 3, col, 3);
        }
    }
    if (capture && s->fb_path) {
        char path[1024], tmp[1060];
        FILE *f;
        /* Existing indexed capture paths use a single %lu conversion. */
        if (strchr(s->fb_path, '%')) {
            snprintf(path, sizeof(path), s->fb_path, s->tick_count);
        } else {
            g_strlcpy(path, s->fb_path, sizeof(path));
        }
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        f = fopen(tmp, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", h, w);
            fwrite(rgb, 3, w * h, f);
            fclose(f);
            rename(tmp, path);
        }
    }
    g_free(s->screen_pixels);
    s->screen_pixels = g_steal_pointer(&rgb);
    s->screen_width = h;
    s->screen_height = w;
    qemu_bh_schedule(s->display_bh);
}
