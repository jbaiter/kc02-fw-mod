/* SPDX-License-Identifier: MIT */
#ifndef AX_TIME_H
#define AX_TIME_H
#include <stdint.h>
/* Original KC02 TT rate estimated against stock XOSTimeGet, 2026-10-15.
 * Not an external clock calibration. Sample more often than the ~33 s wrap.
 * Preserve sub-ms ticks and extend the RAW counter before converting units. */
#define AX_TT_TICKS_PER_MS 128000u
typedef struct {
    uint32_t last, millis, fraction;
} AxTime;
static inline uint32_t ax_time_ms(AxTime *clock, uint32_t counter)
{
    uint32_t delta = counter - clock->last;
    uint32_t fraction = clock->fraction + delta % AX_TT_TICKS_PER_MS;
    clock->last = counter;
    clock->millis += delta / AX_TT_TICKS_PER_MS + fraction / AX_TT_TICKS_PER_MS;
    clock->fraction = fraction % AX_TT_TICKS_PER_MS;
    return clock->millis;
}
#endif
