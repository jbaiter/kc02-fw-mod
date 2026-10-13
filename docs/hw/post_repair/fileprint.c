/* Print a JPEG that already lives on the SD card, through the firmware's own VFS
   and print pipeline: open -> fs_size -> kc02_print_file -> close.

   INPUT[0] = energy (run_A strobe energy)
   INPUT[1] = layout/threshold byte (1 selects the layout + run_B path)
   INPUT[2] = p6 (0 = use the live app-context byte 0)
   INPUT[3] = go (0 = probe only: open/size/close, no printing)
   name     = NUL-terminated path at packet offset 0 (e.g. "PHOTO/PHO00045.JPG")

   Only read-only file access happens. Results are mirrored into the mailbox before
   and after the print call, because the print pipeline reconfigures the display
   and scaler and can drop the USB function mid-job. */
#ifndef TEST_ID
#define TEST_ID 0x0F114100
#endif
#include "common.h"
typedef int (*open_t)(const char *name, u32 op);
typedef int (*close_t)(int fd);
typedef u32 (*fs_size_t)(int fd);
typedef int (*print_file_t)(int fd, u32 energy, u32 layout, u32 p6);
#define OPEN 0x0206C5B8u
#define CLOSE 0x0206C680u
#define FS_SIZE 0x0206CAF8u
#define PRINT_FILE 0x0205ADF0u
#define APP_CTX 0x020D416Cu
#define MAILBOX 0x020DB300u
#define MAILBOX_MAGIC 0x4D424F58u

void test_entry(void *buffer, u32 length, u32 parameter)
{
    volatile u32 *in = INPUT(buffer);
    volatile u32 *out = OUTPUT(buffer);
    volatile u32 *mb = (volatile u32 *)MAILBOX;
    const char *name = (const char *)buffer;
    u32 energy = in[0], layout = in[1], p6 = in[2], go = in[3];
    int fd = ((open_t)OPEN)(name, 1u);          /* 1 == FA_READ */
    u32 size = 0;
    int rc = 0;

    out[0] = (u32)fd;
    mb[0] = MAILBOX_MAGIC; mb[1] = (u32)fd;
    CACHE_clean((void *)MAILBOX, 0x40, 0);      /* survive a USB drop during decode */
    if (fd >= 0) {
        size = ((fs_size_t)FS_SIZE)(fd);
        out[1] = size;
        if (p6 == 0u) {
            p6 = *(volatile unsigned char *)(APP_CTX);
        }
        if (go && size >= 0x200u && size < 0x400000u) {
            rc = ((print_file_t)PRINT_FILE)(fd, energy, layout, p6);
            out[2] = (u32)rc;
        }
        ((close_t)CLOSE)(fd);
    }
    out[3] = energy; out[4] = layout; out[5] = p6;
    out[6] = *(volatile unsigned char *)(APP_CTX);
    mb[0] = MAILBOX_MAGIC; mb[1] = (u32)fd; mb[2] = size; mb[3] = (u32)rc; mb[4] = out[6];
    CACHE_clean((void *)MAILBOX, 0x40, 0);
    finish(buffer, length, parameter, go ? 3 : 2);
}
