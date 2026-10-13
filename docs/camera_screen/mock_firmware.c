/* Host-only model of imports. It tests controller logic, NOT peripheral drivers
 * or the OpenRISC calling convention. The real stock helpers are never run. */
#include "camera_screen.h"
#include "mock_ids.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#define IMPORT(ret,name,args,address) extern ret name args;
#include "firmware_imports.def"
#undef IMPORT
static unsigned char memory[0x400000];
static uint32_t calls[MOCK_IMPORT_COUNT];
static camera_word last[MOCK_IMPORT_COUNT][16],override[MOCK_IMPORT_COUNT];
static unsigned char overridden[MOCK_IMPORT_COUNT];
static camera_word resources[32];
static char texts[32][128];
static uint32_t visibility[32],ticks,tick_step;
static uint32_t frame[12];
static int have_frame,allocations;
static void *owned[64];
static const unsigned char jpeg[8]={0xff,0xd8,1,2,3,4,0xff,0xd9};
static char filename[]="PHOTO/PHO00049.JPG";
void *camera_test_memory(uint32_t address,size_t size)
{
    if (!address) return 0;
    assert(address>=0x02000000 && address<=0x02400000);
    assert(size<=0x02400000-address);
    return memory+(address-0x02000000);
}
void mock_reset(void)
{
    for (size_t i=0;i<64;++i) { if (owned[i]) free(owned[i]); owned[i]=0; }
    allocations=0; memset(memory,0,sizeof(memory)); memset(calls,0,sizeof(calls));
    memset(last,0,sizeof(last)); memset(override,0,sizeof(override)); memset(overridden,0,sizeof(overridden));
    memset(resources,0,sizeof(resources)); memset(texts,0,sizeof(texts)); memset(visibility,0,sizeof(visibility));
    ticks=1234; tick_step=10; have_frame=0; memset(frame,0,sizeof(frame)); camera_descriptor.handle=0;
    frame[1]=0x02180000;frame[2]=0x02190000;
    ((uint16_t *)((unsigned char *)frame+0x20))[0]=320;
    ((uint16_t *)((unsigned char *)frame+0x20))[1]=240;
    MEM16(0x020d4204)=320; MEM16(0x020d4206)=240; MEM32(0x020d4210)=0x02180000;
}
void mock_set(uint32_t id,camera_word value)
{ assert(id<MOCK_IMPORT_COUNT);override[id]=value;overridden[id]=1; }
void mock_clock(uint32_t value,uint32_t step) { ticks=value;tick_step=step; }
void mock_frame(uint32_t present) { have_frame=present!=0; }
uint32_t mock_calls(uint32_t id) { assert(id<MOCK_IMPORT_COUNT);return calls[id]; }
camera_word mock_arg(uint32_t id,uint32_t arg) { assert(id<MOCK_IMPORT_COUNT && arg<16);return last[id][arg]; }
camera_word mock_resource(uint32_t id) { assert(id<32);return resources[id]; }
const char *mock_text(uint32_t id) { assert(id<32);return texts[id]; }
uint32_t mock_visibility(uint32_t id) { assert(id<32);return visibility[id]; }
int mock_allocations(void) { return allocations; }
static camera_word mock_call(enum MockImport id,camera_word *a,size_t n)
{
    assert(n<=16);++calls[id];memcpy(last[id],a,n*sizeof(*a));
    /* Side effects are intentionally small and inspectable. */
    switch(id) {
    case MOCK_fw_set_resid:
        assert(a[0]>=1 && a[0]<=32);resources[a[0]-1]=a[1];
        if (a[1]>UINT32_MAX) { strncpy(texts[a[0]-1],(const char *)a[1],127);texts[a[0]-1][127]=0; }
        break;
    case MOCK_fw_visible: assert(a[0]>=1 && a[0]<=32);visibility[a[0]-1]=(uint32_t)a[1];break;
    case MOCK_fw_window_open:
        if ((CameraDescriptor *)a[0]==&camera_descriptor) {
            camera_descriptor.handle=0x1001;
            (void)camera_window_enter(0x1001,0,0);
        }
        break;
    case MOCK_fw_csi_resolution: *(uint16_t *)a[0]=1280;*(uint16_t *)a[1]=720;break;
    case MOCK_fw_decode_resolution: *(uint16_t *)a[0]=320;*(uint16_t *)a[1]=240;break;
    case MOCK_fw_copy: memcpy((void *)a[0],(void *)a[1],(size_t)a[2]);break;
    case MOCK_fw_free: {
        if (!a[0]) break;
        int found=0;
        for(size_t i=0;i<64;++i) if(owned[i]==(void *)a[0]) { free(owned[i]);owned[i]=0;--allocations;found=1;break; }
        assert(found || !a[0]);break;
    }
    default: break;
    }
    if (overridden[id]) return override[id];
    switch(id) {
    case MOCK_fw_child: return a[1]+1;
    case MOCK_fw_window_open: return 0x1001;
    case MOCK_fw_frame_get: return have_frame?(camera_word)frame:0;
    case MOCK_fw_config_get: return a[0]==0x18?0x8100009c:0;
    case MOCK_fw_setting_value: return a[0]==0x18?((1280U<<16)|720U):0;
    case MOCK_fw_filename_create: case MOCK_fw_file_path: return (camera_word)filename;
    case MOCK_fw_open: return 4;
    case MOCK_fw_watermark_open: return 1;
    case MOCK_fw_file_size: return sizeof(jpeg);
    case MOCK_fw_time: { uint32_t r=ticks;ticks+=tick_step;return r; }
    case MOCK_fw_copy: return a[0];
    case MOCK_fw_load_audio: *(uint32_t *)a[1]=8;return 0x02100000;
    case MOCK_fw_rtc_get: return (camera_word)camera_test_memory(0x020cea14,8);
    case MOCK_fw_rtc_string: return (camera_word)"2026/01/02 03:04:05";
    case MOCK_fw_alloc: {
        void *p=malloc((size_t)a[0]);assert(p);
        for(size_t i=0;i<64;++i) if(!owned[i]) { owned[i]=p;++allocations;return (camera_word)p; }
        abort();
    }
    case MOCK_fw_encoded_get:
        *(void **)a[1]=(void *)jpeg;*(uint32_t *)a[2]=sizeof(jpeg);
        *(uint32_t *)a[3]=0;*(uint32_t *)a[4]=0;return (camera_word)jpeg;
    default: return 0;
    }
}
#include "mock_wrappers.inc"
