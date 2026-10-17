/* SPDX-License-Identifier: MIT */
#ifndef LUMA_INPUT_H
#define LUMA_INPUT_H
#include "luma.h"
typedef struct {
    Key candidate, stable;
    u32 changed_at, down_at;
    int held;
} Input;
/* Debounce 12 ms, emit once on press. Release/hold emit nothing; *held is zero.
 * The retained Input layout keeps existing observation/debug ABIs stable. */
Key input_poll(Input *in, Key raw, u32 now, int *held);
#endif
