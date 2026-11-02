/* AX3292 device-side USB (DUSB) register map, generated from tools/emu/sfr_map.tsv.
 * The USB SPRs are NOT contiguous (0x9840-0x9878, 0x9980-0x998C, 0xA004-0xA180,
 * 0xD804-0xD850) and are interleaved with LCD/JPEG/timer registers, so they are
 * matched by explicit lookup, never by range. */
#ifndef AX3292_USB_H
#define AX3292_USB_H

#include "qemu/osdep.h"

#define AXU_COUNT 74

typedef enum Ax3292UsbSpr {
    AXU_USB20CON0                    = 0x9840,
    AXU_USB20CON1                    = 0x9844,
    AXU_USB20CON2                    = 0x9848,
    AXU_USB20E0ADR                   = 0x984C,
    AXU_USB20E1TADR                  = 0x9850,
    AXU_USB20E1RADR                  = 0x9854,
    AXU_USB20E2TADR                  = 0x9858,
    AXU_USB20E2RADR                  = 0x985C,
    AXU_USB20E3TADR                  = 0x9860,
    AXU_USB20E3RADR                  = 0x9864,
    AXU_USB20E4TADR                  = 0x9868,
    AXU_USB20E4RADR                  = 0x986C,
    AXU_USB20TST0                    = 0x9870,
    AXU_USB20TST1                    = 0x9874,
    AXU_USB20TST_OUT                 = 0x9878,
    AXU_USB20E5TADR                  = 0x9980,
    AXU_USB20E5RADR                  = 0x9984,
    AXU_USB20E6TADR                  = 0x9988,
    AXU_USB20E6RADR                  = 0x998C,
    AXU_USB20_POWER                  = 0xA004,
    AXU_USB20_INTRINL                = 0xA008,
    AXU_USB20_INTRINH                = 0xA00C,
    AXU_USB20_INTROUTL               = 0xA010,
    AXU_USB20_INTROUTH               = 0xA014,
    AXU_USB20_INTRINEL               = 0xA018,
    AXU_USB20_INTRINEH               = 0xA01C,
    AXU_USB20_INTROUTEL              = 0xA020,
    AXU_USB20_INTROUTEH              = 0xA024,
    AXU_USB20_INTRUSB                = 0xA028,
    AXU_USB20_INTRUSBE               = 0xA02C,
    AXU_USB20_FRAME1                 = 0xA030,
    AXU_USB20_FRAME2                 = 0xA034,
    AXU_USB20_INDEX                  = 0xA038,
    AXU_USB20_TESTMODE               = 0xA03C,
    AXU_USB20_INMAXPL                = 0xA040,
    AXU_USB20_INMAXPH                = 0xA044,
    AXU_USB20_CSR0                   = 0xA048,
    AXU_USB20_CSR01                  = 0xA04C,
    AXU_USB20_OUTMAXPL               = 0xA050,
    AXU_USB20_OUTMAXPH               = 0xA054,
    AXU_USB20_OUTCSR1                = 0xA058,
    AXU_USB20_OUTCSR2                = 0xA05C,
    AXU_USB20_COUNT0                 = 0xA060,
    AXU_USB20_OUTCOUNT2              = 0xA064,
    AXU_USB20_TXTYPE                 = 0xA068,
    AXU_USB20_NAKLIMIT0              = 0xA06C,
    AXU_USB20_RXTYPE                 = 0xA070,
    AXU_USB20_RXINTERVAL             = 0xA074,
    AXU_USB20_FIFO0                  = 0xA080,
    AXU_USB20_FIFO1                  = 0xA090,
    AXU_USB20_FIFO2                  = 0xA0A0,
    AXU_USB20_FIFO3                  = 0xA0B0,
    AXU_USB20_FIFO4                  = 0xA0C0,
    AXU_USB20_FIFO5                  = 0xA0D0,
    AXU_USB20_FIFO6                  = 0xA0E0,
    AXU_USB20_DEVCTL                 = 0xA180,
    AXU_USB11CON1                    = 0xD804,
    AXU_USB11CON2                    = 0xD808,
    AXU_USB11CON3                    = 0xD80C,
    AXU_USB11CON4                    = 0xD810,
    AXU_USB11DPDM                    = 0xD814,
    AXU_USB11E0ADR                   = 0xD818,
    AXU_USB11E1TADR                  = 0xD81C,
    AXU_USB11E1RADR                  = 0xD820,
    AXU_USB11E2TADR                  = 0xD824,
    AXU_USB11E2RADR                  = 0xD828,
    AXU_USB11E3TADR                  = 0xD82C,
    AXU_USB11E3RADR                  = 0xD830,
    AXU_USB11DATA                    = 0xD834,
    AXU_USB11MCPCON                  = 0xD840,
    AXU_USB11MCPPND                  = 0xD844,
    AXU_USB11MCPSCADR                = 0xD848,
    AXU_USB11MCPTGADR                = 0xD84C,
    AXU_USB11MCPLENGTH               = 0xD850,
} Ax3292UsbSpr;

/* SPR -> dense index (0..AXU_COUNT-1), or -1 when the SPR is not a USB register. */
static inline int ax3292_usb_index(uint32_t spr)
{
    switch (spr) {
    case 0x9840: return 0;   /* USB20CON0 */
    case 0x9844: return 1;   /* USB20CON1 */
    case 0x9848: return 2;   /* USB20CON2 */
    case 0x984C: return 3;   /* USB20E0ADR */
    case 0x9850: return 4;   /* USB20E1TADR */
    case 0x9854: return 5;   /* USB20E1RADR */
    case 0x9858: return 6;   /* USB20E2TADR */
    case 0x985C: return 7;   /* USB20E2RADR */
    case 0x9860: return 8;   /* USB20E3TADR */
    case 0x9864: return 9;   /* USB20E3RADR */
    case 0x9868: return 10;   /* USB20E4TADR */
    case 0x986C: return 11;   /* USB20E4RADR */
    case 0x9870: return 12;   /* USB20TST0 */
    case 0x9874: return 13;   /* USB20TST1 */
    case 0x9878: return 14;   /* USB20TST_OUT */
    case 0x9980: return 15;   /* USB20E5TADR */
    case 0x9984: return 16;   /* USB20E5RADR */
    case 0x9988: return 17;   /* USB20E6TADR */
    case 0x998C: return 18;   /* USB20E6RADR */
    case 0xA004: return 19;   /* USB20_Power */
    case 0xA008: return 20;   /* USB20_IntrInl */
    case 0xA00C: return 21;   /* USB20_IntrInh */
    case 0xA010: return 22;   /* USB20_IntrOutl */
    case 0xA014: return 23;   /* USB20_IntrOuth */
    case 0xA018: return 24;   /* USB20_IntrInel */
    case 0xA01C: return 25;   /* USB20_IntrIneh */
    case 0xA020: return 26;   /* USB20_IntrOutel */
    case 0xA024: return 27;   /* USB20_IntrOuteh */
    case 0xA028: return 28;   /* USB20_IntrUSB */
    case 0xA02C: return 29;   /* USB20_IntrUSBE */
    case 0xA030: return 30;   /* USB20_Frame1 */
    case 0xA034: return 31;   /* USB20_Frame2 */
    case 0xA038: return 32;   /* USB20_Index */
    case 0xA03C: return 33;   /* USB20_Testmode */
    case 0xA040: return 34;   /* USB20_InMaxPl */
    case 0xA044: return 35;   /* USB20_InMaxPh */
    case 0xA048: return 36;   /* USB20_CSR0 */
    case 0xA04C: return 37;   /* USB20_CSR01 */
    case 0xA050: return 38;   /* USB20_OutMaxPl */
    case 0xA054: return 39;   /* USB20_OutMaxPh */
    case 0xA058: return 40;   /* USB20_OutCSR1 */
    case 0xA05C: return 41;   /* USB20_OutCSR2 */
    case 0xA060: return 42;   /* USB20_Count0 */
    case 0xA064: return 43;   /* USB20_OutCount2 */
    case 0xA068: return 44;   /* USB20_TxType */
    case 0xA06C: return 45;   /* USB20_NAKLimit0 */
    case 0xA070: return 46;   /* USB20_RxType */
    case 0xA074: return 47;   /* USB20_RxInterVal */
    case 0xA080: return 48;   /* USB20_FIFO0 */
    case 0xA090: return 49;   /* USB20_FIFO1 */
    case 0xA0A0: return 50;   /* USB20_FIFO2 */
    case 0xA0B0: return 51;   /* USB20_FIFO3 */
    case 0xA0C0: return 52;   /* USB20_FIFO4 */
    case 0xA0D0: return 53;   /* USB20_FIFO5 */
    case 0xA0E0: return 54;   /* USB20_FIFO6 */
    case 0xA180: return 55;   /* USB20_Devctl */
    case 0xD804: return 56;   /* USB11CON1 */
    case 0xD808: return 57;   /* USB11CON2 */
    case 0xD80C: return 58;   /* USB11CON3 */
    case 0xD810: return 59;   /* USB11CON4 */
    case 0xD814: return 60;   /* USB11DPDM */
    case 0xD818: return 61;   /* USB11E0ADR */
    case 0xD81C: return 62;   /* USB11E1TADR */
    case 0xD820: return 63;   /* USB11E1RADR */
    case 0xD824: return 64;   /* USB11E2TADR */
    case 0xD828: return 65;   /* USB11E2RADR */
    case 0xD82C: return 66;   /* USB11E3TADR */
    case 0xD830: return 67;   /* USB11E3RADR */
    case 0xD834: return 68;   /* USB11DATA */
    case 0xD840: return 69;   /* USB11MCPCON */
    case 0xD844: return 70;   /* USB11MCPPND */
    case 0xD848: return 71;   /* USB11MCPSCADR */
    case 0xD84C: return 72;   /* USB11MCPTGADR */
    case 0xD850: return 73;   /* USB11MCPLENGTH */
    default: return -1;
    }
}

typedef struct Ax3292Usb {
    uint32_t reg[AXU_COUNT];
    uint8_t  ep0_buf[64];
    uint32_t ep0_len;      /* total remaining */
    uint32_t ep0_ptr;      /* guest address of the source buffer */
    bool     ep0_in;
    uint8_t  ep_fifo[5][4096];
    uint32_t ep_len[5];
    uint32_t ep_pos[5];
    bool     attached;
    bool     suspended;
    uint32_t addr;
    uint16_t frame;
} Ax3292Usb;

#endif
