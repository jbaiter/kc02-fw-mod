/* SPDX-License-Identifier: MIT
 * RGB565 drawing at native resolution. Shape coverage uses a 4x4 subpixel
 * grid only near edges, not a supersampled framebuffer or a blur pass.
 */
#ifndef LUMA_PAINT_H
#define LUMA_PAINT_H
#include "luma.h"
u16 blend(u16 background, u16 color, unsigned alpha);
void box(u16 *frame, int x, int y, int w, int h, u16 color);
void round_box(u16 *frame, int x, int y, int w, int h, int radius, u16 color);
void ring(u16 *frame, int cx, int cy, int radius, int width, u16 color);
/* Eighth-pixel radii for smooth animation without floating-point arithmetic. */
void ring8(u16 *frame, int cx, int cy, int radius8, int width8, u16 color);
#endif
