"""Replay measured preview geometry through the production DE/CSI backends.

Synthetic pixels only: private camera images stay outside the repository.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
MEASURED = json.loads((ROOT / 'tests/fixtures/hardware/original-2026-10-15.json').read_text())['preview']


def test_measured_preview_dma_and_scanout(tmp_path):
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        pytest.skip('C compiler unavailable')
    registers = dict(MEASURED['csi'], **MEASURED['de'])
    assignments = '\n'.join(f's.sfr[{address}/4] = {value};' for address, value in registers.items())
    source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define AX3292_RAM_SIZE (64u*1024*1024)
typedef struct CPU { struct { uint32_t picsr; } env; } CPUState;
typedef struct { int host_int; } run_on_cpu_data;
typedef struct { int unused; } DeviceState;
typedef struct { int unused; } QemuConsole;
typedef struct { uint8_t *data; unsigned stride; } DisplaySurface;
typedef struct { struct { int key, down; } key; } QemuInputEvent;
typedef struct {
    const char *name; int mask;
    void (*event)(DeviceState *, QemuConsole *, QemuInputEvent *);
} QemuInputHandler;
enum { Q_KEY_CODE_UP, Q_KEY_CODE_DOWN, Q_KEY_CODE_LEFT, Q_KEY_CODE_RIGHT,
       Q_KEY_CODE_RET, Q_KEY_CODE_KP_ENTER, Q_KEY_CODE_ESC, Q_KEY_CODE_SPC,
       Q_KEY_CODE_P, Q_KEY_CODE_F, Q_KEY_CODE_TAB, Q_KEY_CODE_M };
#define INPUT_EVENT_MASK_KEY 1
#define RUN_ON_CPU_HOST_INT(v) ((run_on_cpu_data){v})
#define CPU(p) (p)
#define g_autofree
#define g_malloc0(n) calloc(1,n)
#define g_free(p) free(p)
#define g_steal_pointer(p) __extension__ ({ __auto_type pp=(p); __auto_type v=*pp; *pp=NULL; v; })
static uint32_t ldl_le_p(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void *machine;
static void *qdev_get_machine(void) { return machine; }
static int qemu_input_linux_to_qcode(int key) { return key; }
static void async_run_on_cpu(CPUState *cpu, void (*fn)(CPUState *, run_on_cpu_data), run_on_cpu_data d) { fn(cpu,d); }
static void qemu_console_resize(QemuConsole *c, unsigned w, unsigned h) { }
static DisplaySurface *qemu_console_surface(QemuConsole *c) { return NULL; }
static void *surface_data(DisplaySurface *s) { return s->data; }
static unsigned surface_stride(DisplaySurface *s) { return s->stride; }
static void qemu_console_update_full(QemuConsole *c) { }
static void qemu_bh_schedule(void *bh) { }
static void g_strlcpy(char *dst, const char *src, size_t n) { snprintf(dst,n,"%s",src); }
typedef struct {
    bool sensor, sensor_3a, dual_sensor, panel_ccw;
    uint32_t sfr[0x10000/4], csi_lcd_size, key_value;
    unsigned i2c_phase[2], key_adc[11];
    bool i2c_selected[2]; uint8_t i2c_offset[2], sensor_regs[2][256];
    uint8_t *ram_ptr, *screen_pixels; CPUState *cpu;
    unsigned screen_width, screen_height; unsigned long tick_count;
    QemuConsole *console; void *display_bh; const char *fb_path;
} Ax3292State;
#define AX3292_MACHINE(p) ((Ax3292State *)(p))
#include "ax3292-de.h"
#include "ax3292-display.h"
#include "ax3292-sensor.h"
int main(void) {
    CPUState cpu = {0};
    Ax3292State s = {.sensor=true, .cpu=&cpu};
    machine = &s;
    s.ram_ptr = calloc(1,AX3292_RAM_SIZE); assert(s.ram_ptr);
    ASSIGNMENTS
    s.sfr[0xa800/4] = 0x31;
    s.sfr[0xc818/4] &= ~6u; /* isolate video from OSD palette in this test */
    assert(s.sfr[0xc810/4] == 0x013f00ef);
    assert(s.sfr[0xc820/4] == 0x013f00b3);
    unsigned raw_y=s.sfr[0xc828/4], raw_uv=s.sfr[0xc830/4];
    unsigned y=0x02000000|raw_y, uv=0x02000000|raw_uv;
    assert(ax3292_de_address_register(y)==raw_y);
    assert(ax3292_de_dma_address(raw_y)==y);
    /* Poison the low-address alias: reading it instead of SDRAM must fail. */
    memset(s.ram_ptr+raw_y,0xee,192*320);
    memset(s.ram_ptr+uv,128,192*160);
    for (unsigned row=0;row<320;row++) for (unsigned col=0;col<192;col++)
        s.ram_ptr[y+row*192+col] = col<180 ? 20+(row*3+col*5)%200 : 0xff;
    ax3292_scanout(&s,false);
    assert(s.screen_width==320 && s.screen_height==240);
    for (unsigned x=0;x<240;x++) for (unsigned row=0;row<320;row++) {
        unsigned index=x*320+319-row; /* portrait panel mounted 90 degrees */
        unsigned expected=(x>=30 && x<210) ? 20+(row*3+(x-30)*5)%200 : 0;
        for (unsigned c=0;c<3;c++) assert(s.screen_pixels[index*3+c]==expected);
    }
    uint8_t *first=malloc(320*240*3); assert(first);
    memcpy(first,s.screen_pixels,320*240*3);
    /* CPU pointers and hardware-masked readbacks refer to the same plane. */
    s.sfr[0xc828/4]=y; s.sfr[0xc830/4]=uv;
    ax3292_scanout(&s,false);
    assert(!memcmp(first,s.screen_pixels,320*240*3));
    free(first); free(s.screen_pixels);
    /* CSI uses full pointers and byte strides, unlike DE's word/offset fields. */
    unsigned cy=s.sfr[0xb060/4], cuv=s.sfr[0xb064/4];
    assert(s.sfr[0xb180/4]==0x00b40140 && s.sfr[0xb068/4]==320);
    s.ram_ptr[cuv+320*90]=0xa5;
    s.sfr[0xb008/4]=1u<<19; /* replay the scaler latch preceding the snapshot */
    ax3292_sensor_frame(&s);
    assert(!(s.sfr[0xb008/4] & (1u<<19)));
    assert(s.csi_lcd_size==0x00b40140);
    assert(s.ram_ptr[cy]!=s.ram_ptr[cy+319]);
    assert(s.ram_ptr[cuv+320*90]==0xa5);
    assert(s.sfr[0xb018/4]==0x200000);
    assert(cpu.env.picsr & (1u<<14));
    free(s.ram_ptr);
    return 0;
}
'''.replace('ASSIGNMENTS', assignments)
    file = tmp_path / 'test.c'
    file.write_text(source)
    binary = tmp_path / 'test'
    subprocess.run([cc, '-std=gnu11', '-Wall', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-const-variable', '-fsanitize=undefined',
                    '-I', str(ROOT / 'tools/emu/qemu/ax3292'), str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
