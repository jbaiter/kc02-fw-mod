/* Synthetic H63P/BF3A03 + CSI LCD/JPEG DMA, no application-state writes.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TODO(approximation): the sensors generate bar/gradient patterns instead of
 * register-accurate BF3A03/H63P output.  Exposure, gain and timing registers are
 * stored per bank but do not change the image; only orientation (register 0x1e)
 * and the PE11 bank select affect it.
 */
static unsigned ax3292_sensor_index(Ax3292State *s)
{
    /* Z-12 sensorInit (02047dd8): PE11 selects complementary camera power
     * states; PE10 is toggled during reset. Never consult the UI's camera flag. */
    return s->dual_sensor && !(s->sfr[0x8010 >> 2] & 0x800);
}

static void ax3292_i2c_command(Ax3292State *s, unsigned bus, uint32_t v)
{
    unsigned base = bus ? 0x90cc : 0x9090;
    unsigned camera = ax3292_sensor_index(s);
    uint8_t tx = s->sfr[(base + 12) >> 2];
    uint32_t status = s->sfr[(base + 20) >> 2];
    if (v & 1) { status &= ~1u; }
    if (v & 0x80) { s->i2c_phase[bus] = 0; }
    if (v & 0x10) {
        switch (s->i2c_phase[bus]) {
        case 0:
            /* Hardware measured 2026-10-15: the original H63P answers at 8-bit
             * write address 0x80 (7-bit 0x40), not 0x40. A software-IIC read of
             * register 0x0b returns 0x48 at 0x80 and never acknowledges at 0x40,
             * which is also why stock's hal_iic08bitAddrReadData(0x40,0x0b) read
             * 0xff. See docs/hw/2026-10-15.md. */
            s->i2c_selected[bus] = s->sensor &&
                ((tx & ~1u) == (s->sensor_3a ? 0xdc : 0x80));
            s->i2c_phase[bus] = (tx & 1) ? 2 : 1;
            break;
        case 1:
            s->i2c_offset[bus] = tx;
            s->i2c_phase[bus] = 2;
            break;
        default:
            if (s->i2c_selected[bus]) {
                s->sensor_regs[camera][s->i2c_offset[bus]++] = tx;
            }
            break;
        }
        status = (s->i2c_selected[bus] ? 0 : 0x80) | 1;
    }
    if (v & 0x20) {
        unsigned reg = s->i2c_offset[bus]++;
        s->sfr[(base + 8) >> 2] = s->i2c_selected[bus] ?
                (s->sensor_3a && reg == 0xfc ? 0x3a :
                 !s->sensor_3a && reg == 0x0b ? 0x48 : s->sensor_regs[camera][reg]) : 0xff;
        status |= 1;
    }
    if (v & 0x40) { status |= 1; }
    s->sfr[(base + 16) >> 2] = 0; /* command strobes self-clear */
    s->sfr[(base + 20) >> 2] = status;
}

static bool ax3292_sensor_fill(Ax3292State *s, uint32_t ybase,
                              uint32_t uvbase, unsigned w, unsigned h,
                              unsigned stride)
{
    static const uint8_t bars[8][3] = {
        {235,128,128}, {210,16,146}, {170,166,16}, {145,54,34},
        {106,202,222}, {81,90,240}, {41,240,110}, {16,128,128}
    };
    unsigned camera = ax3292_sensor_index(s);
    unsigned orientation = s->sensor_3a ? s->sensor_regs[camera][0x1e] : 0;
    unsigned mark;

    if (!stride) { stride = w; }
    if (!ybase || !uvbase || w < 16 || w > 4096 || h < 2 || h > 4096 ||
        ((w | h) & 1) || stride > 4096 ||
        (uint64_t)ybase + (uint64_t)stride * (h - 1) + w > AX3292_RAM_SIZE ||
        (uint64_t)uvbase + (uint64_t)stride * (h / 2 - 1) + w > AX3292_RAM_SIZE) {
        return false;
    }
    /* Stride is a row-start increment, not an allocation size. Overlapping
     * rows still transfer and complete in hardware (seen while Z-12 restores
     * preview after capture). Bound the last actual write, including width. */
    mark = (s->tick_count / 5) % (w - 12);
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            unsigned sx = orientation & 0x20 ? w - 1 - x : x;
            unsigned sy = orientation & 0x10 ? h - 1 - y : y;
            const uint8_t *bar = bars[camera ? sy * 8 / h : sx * 8 / w];
            bool gradient = camera ? sx >= w * 3 / 4 : sy >= h * 3 / 4;
            uint8_t lum = gradient ? (camera ? sy * 255 / (h - 1) :
                                               sx * 255 / (w - 1)) : bar[0];
            if (((sy >= h - MIN(h, 20)) ||
                 (sy >= h * 13 / 16 && sy < h * 14 / 16)) &&
                sx >= mark && sx < mark + 12) {
                lum = 255;
            }
            /* An asymmetric white patch distinguishes the selfie image even
             * after guest-programmed sensor mirroring and panel rotation. */
            if (camera && sx >= w / 8 && sx < w / 4 &&
                sy >= h / 8 && sy < h / 4) { lum = 255; }
            s->ram_ptr[ybase + y * stride + x] = lum;
            if (!(y & 1) && !(x & 1)) {
                s->ram_ptr[uvbase + y / 2 * stride + x] = gradient ? 128 : bar[1];
                s->ram_ptr[uvbase + y / 2 * stride + x + 1] = gradient ? 128 : bar[2];
            }
        }
    }
    return true;
}

static void ax3292_sensor_frame(Ax3292State *s)
{
    /* ISP_CTRL_LS_KS latches line-scaler configuration, then self-clears.
     * It must complete even while CSI is stopped for a RAM JPEG operation. */
    if (s->sfr[0xb008 >> 2] & (1u << 19)) {
        s->csi_lcd_size = s->sfr[0xb180 >> 2];
        s->sfr[0xb008 >> 2] &= ~(1u << 19);
    }
    uint32_t dma = s->sfr[0xb018 >> 2];
    uint32_t events = 0;
    if (!s->sensor) { return; }

    /* DVP frame/size events continue when individual output DMAs are gated.
     * The SDK arms hal_mjpegStart from CSI_IRQ_SEN_STATE_INT; tying this
     * event to LCD DMA deadlocks capture after a manual LCD frame finishes. */
    if (s->sfr[0xb000 >> 2] & 1) {
        events |= 0x08;
        s->sfr[0xb078 >> 2] = s->sfr[0xb01c >> 2];
    }
    if ((s->sfr[0xb000 >> 2] & 1) && (dma & 0x40000)) {
        /* Register writes stage the next scaler setup. In particular,
         * csiScaler writes JPEG dimensions without kicking LS when no scale
         * is needed. Such writes must not resize an active LCD transfer. */
        uint32_t size = s->csi_lcd_size;
        if (ax3292_sensor_fill(s, s->sfr[0xb060 >> 2], s->sfr[0xb064 >> 2],
                              size & 0xffff, size >> 16, s->sfr[0xb068 >> 2])) {
            events |= 0x0a; /* sensor size valid, LCD frame end */
            if (dma & 0x200000) {
                s->sfr[0xb018 >> 2] &= ~0x40000u; /* manual LCD DMA completes */
            }
        }
    }
    /* JPEG input frame completion is independent of LCD DMA. The firmware
     * stops CSI after taking its snapshot, then encodes that RAM again after
     * applying effects. Do not overwrite those buffers while CSI is stopped. */
    if ((s->sfr[0xb000 >> 2] & 1) && (dma & 0x180000)) {
        unsigned base = dma & 0x80000 ? 0xb050 : 0xb040;
        uint32_t size = s->sfr[0xb878 >> 2]; /* configured JPEG source size */
        unsigned w = size >> 16, h = size & 0xffff;
        unsigned lines = s->sfr[(base + 8) >> 2] & 0x7ff;
        unsigned bytes = s->sfr[(base + 12) >> 2];
        unsigned stride = lines ? bytes / lines : w;
        /* The full-frame input buffers used by KC02 photos. Streaming short
         * line rings are not silently treated as a full frame. */
        if (lines >= h && ax3292_sensor_fill(s, s->sfr[base >> 2],
                     s->sfr[(base + 4) >> 2], w, h, stride)) {
            events |= dma & 0x80000 ? 0x09 : 0x208;
        }
    }
    s->sfr[0xb00c >> 2] |= events;
    if ((s->sfr[0xb014 >> 2] & 0x80000000u) &&
        (s->sfr[0xb014 >> 2] & s->sfr[0xb00c >> 2] & 0x7fffffff)) {
        s->cpu->env.picsr |= 1u << 14;
    }
}
