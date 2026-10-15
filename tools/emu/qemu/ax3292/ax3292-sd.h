/* AX3292 SD0 command controller and SDHC sector DMA.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Contract from ax32xx_sd0{SendCmd,Recv,Send,WaitPend,CRCCheck}.
 *
 * TODO(approximation): commands and sector DMA complete immediately.  There is
 * no clock-rate model, no CRC validation of host data, and no multi-block or
 * busy-line timing behaviour.
 */
#include <sys/file.h>

static void ax3292_sd_dma(Ax3292State *s)
{
    uint32_t addr = s->sfr[0x9890 >> 2];
    uint32_t length = s->sfr[0x9894 >> 2];
    uint64_t offset = (uint64_t)s->sd_lba * 512;
    bool ok;
    if (!s->sd_armed || !s->sd_transfer || !s->sd_image) {
        return;
    }
    s->sd_armed = false;
    if (s->sd_transfer == 3 && length == 64 && addr &&
        (uint64_t)addr + length <= AX3292_RAM_SIZE) {
        /* CMD6 switch-function status: normal speed only. The guest checks
         * status byte 16 and falls back to its 24 MHz normal-speed clock. */
        memset(s->ram_ptr + addr, 0, length);
        s->sd_transfer = 0;
        s->eng_sta |= 0x108000u;
        return;
    }
    ok = length && !(length % 512) && addr &&
         (uint64_t)addr + length <= AX3292_RAM_SIZE &&
         offset + length <= s->sd_size;
    if (ok) {
        ok = fseeko(s->sd_image, offset, SEEK_SET) == 0;
    }
    if (ok && s->sd_transfer == 1) {
        ok = fread(s->ram_ptr + addr, 1, length, s->sd_image) == length;
    } else if (ok) {
        ok = fwrite(s->ram_ptr + addr, 1, length, s->sd_image) == length &&
             fflush(s->sd_image) == 0;
    }
    if (s->sd_trace) {
        fprintf(stderr, "sd0: %s lba=%u bytes=%u ram=%08x %s\n",
                s->sd_transfer == 1 ? "read" : "write", s->sd_lba,
                length, addr, ok ? "OK" : "ERROR");
    }
    s->eng_sta &= ~0x710000u;
    s->eng_sta |= 0x108000u; /* DAT0 high, transfer pending */
    if (ok) {
        s->eng_sta |= 0x400000u; /* write CRC token accepted; read CRC clean */
        s->sd_lba += length / 512;
    } else {
        s->eng_sta |= 0x110000u; /* read CRC/error and rejected write token */
    }
}

static void ax3292_sd_command(Ax3292State *s)
{
    unsigned cmd = s->sfr[0x9898 >> 2] & 63;
    uint32_t arg = s->sfr[0x989c >> 2];
    s->sd_response = 0;
    s->eng_sta |= 0x120000u; /* command complete, DAT0 high */
    if (!s->sd_image) {
        return;
    }
    switch (cmd) {
    case 0:
    case 12:
        s->sd_transfer = 0;
        s->sd_armed = false;
        break;
    case 8: s->sd_response = arg & 0xfff; break;
    case 1:
    case 41: s->sd_response = 0xc0ff8000u; break;
    case 3: s->sd_response = 0x00010000u; break;
    case 13: s->sd_response = 0x900; break; /* TRAN + READY_FOR_DATA */
    case 55: s->sd_response = 0x120; break;
    case 6:
        s->sd_response = 0x900;
        if ((arg & 0x00ffff00) == 0x00ffff00) {
            s->sd_transfer = 3;
            ax3292_sd_dma(s);
        }
        break;
    case 2:
    case 9: {
        uint8_t response[16] = { 0 };
        uint32_t dest = s->sfr[0x988c >> 2];
        if (cmd == 9) {
            uint32_t csize = s->sd_size / (512 * 1024) - 1;
            response[0] = 0x40; /* CSD v2 */
            response[3] = 0x32;
            response[5] = 9; /* READ_BL_LEN = 512 */
            response[7] = (csize >> 16) & 63;
            response[8] = csize >> 8;
            response[9] = csize;
            response[15] = 1;
        } else {
            memcpy(response, "\x03KC02EMU", 8);
        }
        /* SD controller stores each wire-order R2 word little-endian.
         * This maps C_SIZE bytes 7/8/9 to buffer offsets 4/11/10. */
        if (dest && (uint64_t)dest + 16 <= AX3292_RAM_SIZE) {
            for (unsigned i = 0; i < 16; i++) {
                s->ram_ptr[dest + i] = response[(i & ~3) + 3 - (i & 3)];
            }
        }
        break;
    }
    case 17:
    case 18:
        s->sd_lba = arg;
        s->sd_transfer = 1;
        ax3292_sd_dma(s);
        break;
    case 24:
    case 25:
        s->sd_lba = arg;
        s->sd_transfer = 2;
        ax3292_sd_dma(s);
        break;
    default: s->sd_response = 0x900; break;
    }
    if (s->sd_trace) {
        fprintf(stderr, "sd0: CMD%u arg=%08x response=%08x\n", cmd, arg, s->sd_response);
    }
}

static void ax3292_sd_control(Ax3292State *s, uint32_t v)
{
    /* W1C pending bits are command strobes, not readable control state. */
    if (v & 0x20000) { s->eng_sta &= ~0x20000u; }
    if (v & 0x8000) { s->eng_sta &= ~0x8000u; }
    s->sfr[0x9884 >> 2] = v & ~0x28030u;
    if (v & 0x28000) {
        return;
    }
    if (v & 0x10) {
        s->sd_armed = true;
        ax3292_sd_dma(s);
    } else if (v & 7) {
        ax3292_sd_command(s);
    }
}

static void ax3292_sd_open(Ax3292State *s, const char *path)
{
    struct stat st;
    s->sd_image = fopen(path, "r+b");
    if (!s->sd_image || fstat(fileno(s->sd_image), &st) < 0 ||
        !S_ISREG(st.st_mode) || st.st_size < 512 * 1024 ||
        st.st_size % (512 * 1024) || st.st_size > 2LL * 1024 * GiB ||
        flock(fileno(s->sd_image), LOCK_EX | LOCK_NB) < 0) {
        error_report("Cannot open/lock SD image '%s': need a writable regular file,"
                     " a multiple of 512 KiB, not in use by another emulator", path);
        exit(1);
    }
    s->sd_size = st.st_size;
    s->eng_sta |= 0x100000u;
    fprintf(stderr, "sd0: attached writable image %s (%" PRIu64 " bytes)\n", path, s->sd_size);
}
