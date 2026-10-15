/* Stock KC02 firmware reference: USB.
 *
 * The vendor USB device controller driver is IN THE SDK and should be read there,
 * not transcribed here:
 *
 *   ax32xx/src/ax32xx_usb.c, ax32xx/inc/ax32xx_usb.h   controller driver
 *   ax32_platform_demo/taskUsbDevice.c                 the device task/descriptors
 *
 * What this file records is the part that is NOT in the SDK: the vendor command
 * the camera exposes over Mass Storage, because the entire hardware test
 * toolchain in this repo is built on it, and its behaviours are measured rather
 * than documented.
 */
#include <stdint.h>

typedef uint8_t  u8;
typedef uint32_t u32;

/* ---------------------------------------------------------------------------
 * TRANSPORT
 *
 * The camera presents interface 4 as Mass Storage and speaks Bulk-Only Transport,
 * but treats the CBW's CDB as a VENDOR command: opcode 0xCD, followed by a
 * function pointer, a destination/source address, a callback and an offset.
 *
 *   byte 0        opcode 0xCD
 *   bytes 1..4    function pointer to call on the device
 *   bytes 5..8    RAM address (source for a write, destination for a read)
 *   bytes 9..12   callback pointer
 *   bytes 13..15  offset, 3 bytes little-endian
 *
 * Direction bit 0x80 in the CBW selects device -> host. The host side lives in
 * tools/dump_firmware.py (CameraTransport) and is the reference implementation.
 *
 * Two functions are pinned by the tooling:
 *   0x0204C624  MEMORY_TRANSFER_FN   does the actual copy
 *   0x0202B058  CACHE_CLEAN_FN       flushes the range before the host reads it
 *
 * The transfer length is bounded in practice at 2048 bytes, which is what the
 * whole bench harness is sized around.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * MEASURED BEHAVIOURS, none of which are in any header
 *
 * 1. MODE SWITCH RE-ENUMERATES USB. Asking for camera mode tears down the
 *    in-flight BOT transaction, so the handle that made the request comes back
 *    with a timeout and the camera's BOT state machine is left mid-transaction
 *    and does not recover on its own. A pyusb dev.reset() cannot clear it either,
 *    because by then the device is gone ("Entity not found"). The only reliable
 *    recovery is a USB replug or a power cycle.                       VERIFIED
 *
 * 2. CAMERA MODE DROPS OFF THE BUS ENTIRELY. After a camera-mode entry the device
 *    is absent from lsusb -- not merely presenting a different function. There is
 *    no webcam personality (1908:3282) to talk to either. So a probe CANNOT be
 *    run over USB while the camera is in camera mode; that is why the sensor
 *    bring-up had to move into on-device code.                        VERIFIED
 *
 * 3. USB IS FLAKY AFTER RAM EXPERIMENTS. Repeated short drops needing a replug
 *    were observed across sessions, and no cause was established. Logged in
 *    ~/kc02-hw/2026-10-15/session-202910/usb-drop-observations.md.
 *
 * 4. THE KERNEL OWNERSHIP MUST BE HANDED OVER. The stock firmware services
 *    interface 4 itself, so the host must detach usb-storage, claim the interface,
 *    and reattach afterwards. tools/dump_firmware.py does this; doing it twice in
 *    quick succession is what the safety harness is designed to prevent.
 * ------------------------------------------------------------------------ */

/* ---------------------------------------------------------------------------
 * Luma's own USB code, for contrast: custom-firmware/src/ax_usb.c,
 * usb_device.c, msc.c, webusb.c implement a device stack for standalone Luma.
 * Compiled defaults leave USB DETACHED, deliberately: no production device
 * identity exists, and an untested controller path must not claim a stock
 * camera's USB identity on a shared bus.
 * ------------------------------------------------------------------------ */
