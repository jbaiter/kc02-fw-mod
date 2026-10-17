/* SPDX-License-Identifier: MIT */
#include "input.h"
Key input_poll(Input *in, Key raw, u32 now, int *held)
{
    *held = 0;
    if (raw != in->candidate) { in->candidate = raw; in->changed_at = now; }
    if (in->stable != in->candidate && now-in->changed_at >= 12u) {
        in->stable = in->candidate;
        in->down_at = now;
        in->held = 0;
        /* One action on a debounced press, never on release or after a hold.
         * OK must capture immediately; print-mode changes have their own key. */
        return in->stable;
    }
    return KEY_NONE;
}
