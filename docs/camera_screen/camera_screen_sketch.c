/* Readable model of the KC02 camera screen, reconstructed from original.bin.
 * NOT a tested firmware replacement. External fw_* adapters are undefined;
 * their declarations express observed operations, not verified firmware ABI.
 * Full decompilation, including Up/Down and capture/print, is in the raw directory.
 * Names for app fields are descriptive inferences; offsets are observed.
 */
#include <stdint.h>
#include <stddef.h>

typedef uint32_t Window;
typedef struct Frame Frame; /* Firmware frame layout remains opaque here. */
#define APP_BASE UINT32_C(0x020d416c)
#define APP8(off) (*(volatile uint8_t *)(uintptr_t)(APP_BASE + (off)))
#define FRAME_HANDSHAKE APP8(0x5c) /* ISR ready=0, task acquired=1, submitted=2 */
#define FRAME_HANDOFF_ENABLED APP8(0x58)
#define EFFECT_GROUP APP8(0x59)
#define PRINT_FLAG APP8(0x5f) /* Print-option meaning inferred from OK handler. */
#define ZOOM_INDEX APP8(0x08)
#define INVALID_RESOURCE UINT32_C(0x7fffffff)

/* This is the firmware's 48-byte table format, NOT the SDK's 40-byte type.
 * Constructor accesses verify the common prefix. Retain trailing bytes opaque:
 * the string constructor reads +0x28..+0x2b and +0x2d; meanings unresolved.
 * Store target addresses as u32 rather than host-native function pointers.
 */
typedef struct {
    uint32_t create;
    uint16_t id;
    int16_t x, y;
    uint16_t width, height, style;
    uint8_t bg_color, pad11[3];
    uint32_t image;
    uint8_t image_align, pad19[3];
    uint32_t string;
    uint8_t string_align, font_color, font, pad23;
    uint32_t private_data;
    uint8_t extra[8];
} FirmwareWidget;
_Static_assert(sizeof(FirmwareWidget) == 48, "firmware widget stride");
_Static_assert(offsetof(FirmwareWidget, string) == 0x1c, "string offset");
_Static_assert(offsetof(FirmwareWidget, extra) == 0x28, "extension offset");

/* Observed stock operations. See raw/functions.tsv and raw/calls.tsv. */
extern void fw_mjpeg_encode_init(void);                     /* 02040384 */
extern void fw_call_020487a4(void); /* Argument contract unresolved. */
extern void fw_record_config(uint32_t key, uint32_t value); /* 0205609c */
extern void fw_frame_mode(uint32_t value);                  /* 0204606c */
extern void fw_color_preset(uint32_t value);                /* 020588c0 */
extern void fw_crop_and_mjpeg_start(uint32_t zoom, uint32_t channel); /* 020049f8 */
extern void fw_ui_state_apply(uint32_t value);              /* 02004944 */
extern void fw_layer_init(uint32_t layer);                  /* 02004588 */
extern void fw_layer_uninit(uint32_t layer);                /* 020041b0 */
extern Window fw_window_open(uint32_t descriptor, uint32_t argc); /* 02012304 */
extern void fw_delay(uint32_t ticks);                       /* 020218b4 */
extern void fw_csi_enable(uint32_t on);                      /* 02034ee4 */
extern Frame *fw_current_frame(void);                       /* 02022720; reads 020cd788 */
extern void fw_submit_frame(Frame *frame);                  /* 02039e44 */
extern void fw_rearm_csi_buffer(void);                      /* 0203a84c; alloc/config/DMA kick */
extern void fw_apply_software_effect(Frame *frame);         /* adapter around 02022740 */
extern void fw_apply_group3_effect(Frame *frame);           /* adapter around 020460b4 */
extern void fw_indicator_timeout(Window root);             /* 0200aa8c */
extern Window fw_child(Window root, uint16_t id);            /* 02025154 */
extern void fw_set_resource(Window widget, uint32_t resource); /* winSetResid */
extern void fw_set_visible(Window widget, uint32_t visible); /* winSetVisible */
extern void fw_present(uint32_t force);                     /* 0205b8c4 */
extern void fw_request_mode(uint32_t mode, uint32_t arg);    /* 02008108 */
extern Window fw_camera_root(void); /* Runtime descriptor word at 020c2b48. */
extern void fw_call_020075e8(void); /* Entry helper; ABI unresolved. */
extern void fw_refresh_resolution(Window root);            /* 0200a030 */
extern void fw_refresh_storage(Window root);               /* 02009bc0 */
extern void fw_refresh_battery(Window root);               /* 02008bec */
extern void fw_refresh_scene(Window root);                 /* 02008d98 */
extern void fw_refresh_print_flag(Window root);            /* 02009d5c */
extern void fw_refresh_remaining(Window root);             /* 0200917c */
extern void fw_refresh_indicator(Window root);             /* 020094b8 */
extern void fw_refresh_light(Window root);                 /* 02008f0c */
extern void fw_hide_countdown(Window root);                /* 02009358(root,0) */
extern void fw_hide_status_dialog(Window root);            /* 02009ccc(root,0,0) */
extern void fw_exit_stub(void);                             /* 020568d4 */
extern void fw_record_reset(void);                          /* 02054f0c */
extern void fw_camera_open_tail(Window root); /* 0200a37c: audio/buffer/transition setup. */

/* 020082f4: continuous sensor preview + camera overlay. */
void camera_mode_enter_model(void)
{
    *(volatile uint32_t *)(uintptr_t)0x020ccd8c = 1;
    fw_mjpeg_encode_init();
    fw_call_020487a4();
    fw_record_config(8, 0);
    fw_frame_mode(0);
    APP8(0x5e) = 0;
    APP8(0x5a) = 0;
    fw_color_preset(0);
    ZOOM_INDEX = 0;
    fw_crop_and_mjpeg_start(0, 0);
    APP8(0x07) = 0;
    fw_ui_state_apply(0);
    fw_layer_init(0); /* Calls hal_lcdPreviewStart(sensor_width,sensor_height). */
    FRAME_HANDOFF_ENABLED = 1;
    EFFECT_GROUP = 0;
    APP8(0x5b) = 0;
    fw_window_open(0x020c2b40, 0); /* Constructs widget table at 020c2bd0. */
    fw_delay(50);
    fw_csi_enable(1);
    fw_delay(50);
}

/* 020083c8: mode-manager exit, before destroying desktop child windows. */
void camera_mode_exit_model(void)
{
    *(volatile uint32_t *)(uintptr_t)0x020ccd8c = 0;
    FRAME_HANDOFF_ENABLED = 0;
    EFFECT_GROUP = 0;
    APP8(0x5b) = 0;
    fw_record_config(8, 0);
    fw_frame_mode(0);
    APP8(0x5a) = 0;
    fw_color_preset(0);
    if (FRAME_HANDSHAKE == 0) {
        FRAME_HANDSHAKE = 1;
        Frame *frame = fw_current_frame();
        if (frame != NULL) {
            FRAME_HANDSHAKE = 2;
            fw_submit_frame(frame);
            fw_rearm_csi_buffer();
        }
    }
    fw_ui_state_apply(APP8(0x07));
    fw_layer_uninit(0);
    fw_exit_stub();
    fw_record_reset();
    if (APP8(0x04) == 3)
        APP8(0x04) = 4;
}

/* 020084a0: task-side half of the preview buffer handshake.
 * The CSI/LCD callback at 0203a2f0 publishes the next frame and resets state to 0.
 * Keep fw_rearm_csi_buffer: despite its old "commit" label, this operation
 * prepares the NEXT capture buffer, rather than drawing overlay widgets.
 */
void camera_mode_tick_model(void)
{
    fw_indicator_timeout(fw_camera_root());
    if (FRAME_HANDSHAKE != 0)
        return;
    FRAME_HANDSHAKE = 1;
    Frame *frame = fw_current_frame();
    if (frame == NULL)
        return; /* Preserve the original's state=1 result on this path. */
    if (EFFECT_GROUP == 2)
        fw_apply_software_effect(frame);
    else if (EFFECT_GROUP == 3)
        fw_apply_group3_effect(frame);
    FRAME_HANDSHAKE = 2;
    fw_submit_frame(frame);
    fw_rearm_csi_buffer();
}

/* 0200a37c, message 0x0e: initialize the overlay's existing children. */
void camera_window_enter_model(Window root)
{
    *(volatile uint32_t *)(uintptr_t)0x020ccd98 = 0;
    fw_call_020075e8();
    fw_hide_status_dialog(root);
    fw_refresh_resolution(root);
    fw_refresh_storage(root);
    fw_refresh_battery(root);
    fw_refresh_scene(root);
    fw_hide_countdown(root);
    fw_refresh_print_flag(root);
    fw_refresh_remaining(root);
    *(volatile uint8_t *)(uintptr_t)0x020c8780 = 1;
    fw_refresh_indicator(root);
    /* Full raw body includes buffer setup, initial visibility, transition state
     * and audio call, delegated here rather than guessing their contracts. */
    fw_camera_open_tail(root);
}

/* 02009de0: LEFT toggles the option consumed by the OK/print handler. */
void camera_left_model(Window root, uint32_t event, const uint32_t *flags)
{
    if (event != 1 || *flags != 0)
        return;
    if (PRINT_FLAG == 0) {
        PRINT_FLAG = 1;
        if (EFFECT_GROUP == 0 && APP8(0x5b) != 0)
            APP8(0x5b) = 7;
    } else {
        PRINT_FLAG = 0;
    }
    fw_refresh_print_flag(root);
    fw_refresh_scene(root);
}

/* Preserve the complex state machines by referring to their full raw exports.
 * "video_key_next/prev" are inherited labels; the camera keymap uses them.
 * 02009890 UP: flags=1 raises zoom (clamp 15); flags=2 reverses preset order.
 * 02009568 DOWN: flags=1 lowers zoom (clamp 0); flags=2 advances preset order.
 * Both disable zoom for software/template groups and edge presets 7/8.
 * 0200a4bc OK: countdown/burst selection, still capture, save and optional print,
 * light/audio feedback and status dialogs. See raw/0200a4bc.c (complete body).
 */
typedef struct { uint32_t message, handler; } CameraBinding;
const CameraBinding camera_bindings_model[] = {
    {0x0e, 0x0200a37c}, /* window enter */
    {0x0f, 0x0200947c}, /* window exit */
    {0x10, 0x0200a310}, /* refresh on window/mode change */
    {0x1b, 0x02009890}, /* UP */
    {0x1c, 0x02009568}, /* DOWN */
    {0x1e, 0x02009f6c}, /* RIGHT: three-state light control */
    {0x1d, 0x02009de0}, /* LEFT: print-option flag */
    {0x1a, 0x0200a4bc}, /* OK capture/save/print */
    {0x23, 0x0200a4bc}, /* OK alias */
    {0x24, 0x02009f08}, /* short POWER/BACK: request main menu (mode 6) */
    {0x01, 0x02009e70}, /* storage-state notification/dialog */
    {0x02, 0x02008d74}, /* battery/USB refresh */
    {0x03, 0x02008d3c}, /* conditional battery refresh */
    {0x06, 0x02008bd4}, /* no-op */
    {0x0a, 0x02009108}, /* RTC/status update */
    {0x2e, 0}
};
