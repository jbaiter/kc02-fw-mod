/* SPDX-License-Identifier: MIT */
#include "input.h"
Key input_poll(Input *in, Key raw, u32 now, int *held)
{
    *held = 0;
    if (raw != in->candidate) { in->candidate = raw; in->changed_at = now; }
    if (in->stable != in->candidate && now-in->changed_at >= 30) {
        Key released = in->stable;
        int consumed = in->held;
        in->stable = in->candidate;
        in->down_at = now;
        in->held = 0;
        if (released != KEY_NONE && !consumed) { return released; }
    }
    if (in->stable == KEY_OK && !in->held && now-in->down_at >= 650) {
        in->held = 1; *held = 1; return KEY_OK;
    }
    return KEY_NONE;
}
