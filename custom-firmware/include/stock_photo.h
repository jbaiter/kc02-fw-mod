/* SPDX-License-Identifier: MIT */
#ifndef LUMA_STOCK_PHOTO_H
#define LUMA_STOCK_PHOTO_H
#include "stock_fs.h"
#define STOCK_PHOTO_WRITE_TOKEN 0x53445048u /* explicit host opt-in, packet word 14 */
/* RAM-run adapter only. NULL ops selects pinned original-KC02 routines;
 * non-NULL ops permits native tests without any hardware access. */
void stock_photo_init(int allow_writes, u8 *read_buffer, unsigned capacity,
                      const StockFsOps *ops);
unsigned stock_photo_count(void);
int stock_photo_save(const u16 *pixels);
int stock_photo_load(unsigned index, u16 *pixels);
int stock_photo_poll(App *app, u32 now, int display_ready);
const StockFs *stock_photo_state(void);
#endif
