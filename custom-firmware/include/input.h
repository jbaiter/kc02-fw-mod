/* SPDX-License-Identifier: MIT */
#ifndef LUMA_INPUT_H
#define LUMA_INPUT_H
#include "luma.h"
typedef struct {
    Key candidate, stable;
    u32 changed_at, down_at;
    int held;
} Input;
/* Debounce 30 ms; hold OK for 650 ms emits one ink event, never a shutter. */
Key input_poll(Input *in, Key raw, u32 now, int *held);
#endif
