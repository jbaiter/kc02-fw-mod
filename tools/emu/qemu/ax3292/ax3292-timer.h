/* AX3292 TT counter (0x9900/04/08).
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Hardware evidence: docs/hw/2026-10-15.md. TTCTL 0x42 reads as 2;
 * 0/0x40 stop and clear, 2 resumes from zero, disabled TTCNT reads zero.
 * The counter is NOT an unconditional microsecond clock.
 *
 * TODO(approximation): 128 MHz is estimated against the stock XOS millisecond
 * clock on original KC02, not calibrated against an external reference. Other
 * TT modes and TTPR rollover/IRQ behaviour have not been measured.
 */
typedef struct Ax3292TickTimer {
    uint32_t control, preload, count;
    uint64_t last_ns, fraction;
} Ax3292TickTimer;

static void ax3292_tt_update(Ax3292TickTimer *t, uint64_t now_ns)
{
    if ((t->control & 3) == 2) {
        uint64_t scaled = (now_ns - t->last_ns) * 128 + t->fraction;
        t->count += scaled / 1000;
        t->fraction = scaled % 1000;
    }
    t->last_ns = now_ns;
}

static uint32_t ax3292_tt_read(Ax3292TickTimer *t, uint32_t spr, uint64_t now_ns)
{
    ax3292_tt_update(t, now_ns);
    switch (spr) {
    case 0x9900: return t->control;
    case 0x9904: return t->preload;
    default: return t->count;
    }
}

static void ax3292_tt_write(Ax3292TickTimer *t, uint32_t spr,
                            uint32_t value, uint64_t now_ns)
{
    ax3292_tt_update(t, now_ns);
    switch (spr) {
    case 0x9900:
        if ((value & 0x40) || (value & 3) != 2) {
            t->count = 0;
            t->fraction = 0;
        }
        t->control = value & ~0x40u;
        break;
    case 0x9904: t->preload = value; break;
    case 0x9908: t->count = value; t->fraction = 0; break;
    }
}
