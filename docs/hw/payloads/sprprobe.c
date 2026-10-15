/* Bounded peripheral readback under stock firmware. INPUT[0] selects a bank.
 * No mode changes, delays, FIFO reads or peripheral writes. All addresses are
 * AX3292 SPR byte selectors from ax329x.h, NOT CPU memory addresses.
 * OUTPUT = bank, pair_count, {selector, value} x 12. */
#include "common.h"
static const u32 registers[][12] = {
    /* DE: frame, screen, enable, video geometry/buffers, OSD0, current Y. */
    {0xc800, 0xc810, 0xc818, 0xc820, 0xc824, 0xc828,
     0xc82c, 0xc830, 0xc834, 0xc844, 0xc848, 0xc87c},
    /* Panel controller configuration (not panel-internal command readback). */
    {0xa800, 0xa804, 0xa808, 0xa80c, 0xa810, 0xa834,
     0xa838, 0xa840, 0xa844, 0xa85c, 0xa868, 0xa86c},
    /* ADC, SD controller, free-running counter. No conversion/SD command. */
    {0x9170, 0x9174, 0x9178, 0x9880, 0x9884, 0x9888,
     0x988c, 0x9890, 0x9894, 0x9898, 0x989c, 0x9908},
    /* CSI mode, DMA setup, LCD buffers/stride, sensor-size setup. */
    {0xb000, 0xb008, 0xb014, 0xb018, 0xb01c, 0xb030,
     0xb034, 0xb060, 0xb064, 0xb068, 0xb078, 0xb180},
    /* Timer configuration and SPI configuration, no FIFO/data-buffer reads. */
    {0x9900, 0x9904, 0x9908, 0x9800, 0x9804, 0x9808,
     0x98a8, 0x9000, 0x9004, 0x9008, 0x900c, 0x924c},
    /* Rotation engine + picture geometry. Read-only: no rotate start/reset.
     * Source/dest plane addresses prove whether CSI->DE rotation is hardware. */
    {0x8980, 0x8984, 0x8988, 0x898c, 0x8990, 0x8994,
     0x8998, 0x899c, 0x89a0, 0x89a4, 0x89a8, 0x8800},
};
void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *out = OUTPUT(buffer);
    u32 bank = INPUT(buffer)[0];
    for (u32 i = 0; i < 26; ++i) out[i] = 0;
    out[0] = bank;
    if (bank < sizeof(registers) / sizeof(registers[0])) {
        out[1] = 12;
        for (u32 i = 0; i < 12; ++i) {
            u32 selector = registers[bank][i];
            out[2 + i * 2] = selector;
            out[3 + i * 2] = *(volatile __sfr u32 *)selector;
        }
    }
    finish(buffer, length, parameter, out[1]);
}
