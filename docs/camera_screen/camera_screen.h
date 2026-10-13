/* Recompiled KC02 camera mode. Firmware-specific, offline experimental module. */
#ifndef KC02_CAMERA_SCREEN_H
#define KC02_CAMERA_SCREEN_H

#include <stddef.h>
#include <stdint.h>

/* Native-sized handles let the same controller run against host mocks.
 * On the camera, handles, pointers and callback-table entries are 32 bits. */
typedef uintptr_t camera_word;
typedef camera_word CameraWindow;
typedef uint32_t (*CameraHandler)(CameraWindow root, uint32_t event, const uint32_t *flags);

/* IDs in the stock camera's 320x240 widget tree. */
enum CameraWidgetId {
    WIDGET_RESOLUTION = 0x04,
    WIDGET_STORAGE = 0x08,
    WIDGET_BATTERY = 0x0a,
    WIDGET_CLOCK = 0x0c,
    WIDGET_SCENE = 0x0f,
    WIDGET_COUNTDOWN = 0x10,
    WIDGET_PRINT_OPTION = 0x11,
    WIDGET_STATUS_BACKGROUND = 0x13,
    WIDGET_STATUS_TITLE = 0x14,
    WIDGET_STATUS_TEXT = 0x15,
    WIDGET_ZOOM = 0x16,
    WIDGET_ZOOM_INDICATOR = 0x17,
    WIDGET_REMAINING_PHOTOS = 0x18,
    WIDGET_LIGHT = 0x19,
    WIDGET_ALTERNATE_LIGHT = 0x1a,
};

enum CameraMessage {
    MSG_STORAGE = 0x01,
    MSG_BATTERY = 0x02,
    MSG_BATTERY_CONDITIONAL = 0x03,
    MSG_NOOP = 0x06,
    MSG_CLOCK = 0x0a,
    MSG_WINDOW_ENTER = 0x0e,
    MSG_WINDOW_EXIT = 0x0f,
    MSG_WINDOW_REFRESH = 0x10,
    MSG_OK = 0x1a,
    MSG_UP = 0x1b,
    MSG_DOWN = 0x1c,
    MSG_LEFT = 0x1d,
    MSG_RIGHT = 0x1e,
    MSG_ALTERNATE_OK = 0x23,
    MSG_BACK = 0x24,
    MSG_TABLE_END = 0x2e,
};

/* These name the observed handler branches, not inferred press/release events.
 * ui_key_dispatch shifts the queued high word down before passing &flags. */
enum CameraKeyFlag {
    KEY_FLAG_PRIMARY = 0,
    KEY_FLAG_ZOOM_STEP = 1,
    KEY_FLAG_PRESET_STEP = 2,
};

enum CameraEffectGroup {
    EFFECT_GROUP_CAPTURE = 0,
    EFFECT_GROUP_COLOR_MATRIX = 1,
    EFFECT_GROUP_SOFTWARE = 2,
    EFFECT_GROUP_TEMPLATE = 3,
};

enum CameraCapturePreset {
    PRESET_SINGLE = 0,
    PRESET_BURST_3 = 1,
    PRESET_BURST_5 = 2,
    PRESET_TIMER_3 = 3,
    PRESET_TIMER_5 = 4,
    PRESET_TIMER_10 = 5,
    PRESET_TIMER_5_BURST_3 = 6,
    PRESET_EDGE_A = 7,
    PRESET_EDGE_B = 8,
};

enum CameraPreviewState {
    PREVIEW_READY = 0,
    PREVIEW_ACQUIRED = 1,
    PREVIEW_SUBMITTED = 2,
};

enum CameraLightMode {
    LIGHT_OFF = 0,
    LIGHT_DURING_CAPTURE = 1,
    LIGHT_CONTINUOUS = 2,
};

enum CameraStorageState {
    STORAGE_ABSENT = 0,
    STORAGE_NOTIFICATION_2 = 2, /* Notification code; exact meaning unresolved. */
    STORAGE_FULL = 3,
    STORAGE_READY = 4,
};

enum CameraIndicatorState {
    INDICATOR_HIDDEN = 0,
    INDICATOR_ZOOMING = 1,
    INDICATOR_SETTLING = 2,
};

typedef struct {
    uint32_t message;
    CameraHandler handler;
} CameraBinding;

/* This is the firmware's 48-byte record, NOT the SDK's 40-byte type.
 * Use target addresses rather than native pointers inside creation records.
 * Preserve padding and the unknown extension when regenerating stock widgets. */
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
} CameraWidget;

typedef struct {
    const CameraBinding *bindings;
    CameraWidget *widgets;
    CameraWindow handle; /* Stock win_open fills this at runtime. */
    uint8_t repeat_open, reserved[3];
} CameraDescriptor;

_Static_assert(sizeof(CameraWidget) == 48, "firmware widget stride");
_Static_assert(offsetof(CameraWidget, extra) == 0x28, "firmware extension");
#ifndef CAMERA_HOST_TEST
_Static_assert(sizeof(void *) == 4, "32-bit target required");
_Static_assert(sizeof(CameraBinding) == 8, "firmware binding size");
_Static_assert(sizeof(CameraDescriptor) == 16, "firmware descriptor size");
#endif

/* Host tests translate CPU addresses into mock memory. Target accesses stay
 * volatile: the stock callbacks and drivers also update this shared state. */
#ifdef CAMERA_HOST_TEST
void *camera_test_memory(uint32_t address, size_t size);
#define CAM_PTR(address, size) camera_test_memory((address), (size))
#else
#define CAM_PTR(address, size) ((void *)(uintptr_t)(address))
#endif
#define MEM8(address) (*(volatile uint8_t *)CAM_PTR((address), 1))
#define MEM16(address) (*(volatile uint16_t *)CAM_PTR((address), 2))
#define MEM32(address) (*(volatile uint32_t *)CAM_PTR((address), 4))
#define APP8(offset) MEM8(0x020d416cU + (offset))
#define APP32(offset) MEM32(0x020d416cU + (offset))

/* Controller-owned fields in the stock application context. Names describe
 * usage here; they do not establish a complete recovered context structure. */
#define BATTERY_LEVEL APP8(0x00)
#define BATTERY_ICON_ANIMATION_ENABLED APP8(0x02)
#define STORAGE_STATE APP8(0x04)
#define SAVED_UI_STATE APP8(0x07)
#define ZOOM APP8(0x08)
#define LIGHT_DEVICE APP8(0x10)
#define LED_DEVICE APP8(0x11)
#define PHOTO_FILE_MANAGER APP32(0x38)
#define PHOTO_BROWSER_MANAGER APP32(0x3c)
#define STORAGE_CAPACITY_ESTIMATE APP32(0x4c)
#define FRAME_ENABLED APP8(0x58)
#define GROUP APP8(0x59)
#define COLOR_MATRIX_INDEX APP8(0x5a)
#define PRESET APP8(0x5b)
#define FRAME_STATE APP8(0x5c)
#define SOFTWARE_EFFECT_INDEX APP8(0x5d)
#define TEMPLATE_INDEX APP8(0x5e)
#define PRINT_OPTION APP8(0x5f)
#define PRINT_LAYOUT APP8(0x60)
#define PRINT_DENSITY APP8(0x61)
#define TRANSITION_STATE APP8(0x68)
#define TRANSITION_PHASE APP8(0x69)
#define BATTERY_ICON_PHASE APP8(0x6a)
#define ZOOM_INDICATOR_STATE APP8(0x6d)
#define UI_MESSAGE_QUEUE APP32(0x74)

/* Additional shared globals used by the camera workflow. */
#define CAMERA_MODE_ACTIVE MEM32(0x020ccd8c)
#define CAPTURE_BUSY MEM32(0x020ccd90)
#define LIGHT MEM32(0x020ccd98U)
#define AUDIO_LENGTH MEM32(0x020ccd9c)
#define AUDIO_BUFFER MEM32(0x020ccda0)
#define INDICATOR_TIME MEM32(0x020ccda4)
#define CAMERA_KEY_GATE MEM8(0x020c8780)
#define CAPTURE_TRANSFORM_FLAG MEM8(0x020d41fa)
#define CAPTURE_WIDTH MEM16(0x020d4204)
#define CAPTURE_HEIGHT MEM16(0x020d4206)
#define CAPTURE_Y MEM32(0x020d4210)
#define CAPTURE_UV MEM32(0x020d4214)
#define WATERMARK_ID MEM8(0x020e3181)

extern CameraDescriptor camera_descriptor;
extern CameraWidget camera_widgets[20];
extern const CameraBinding camera_bindings[16];

/* Mode lifecycle: the stock scheduler calls these, not a USB payload loop. */
void camera_mode_enter(void);
void camera_mode_exit(void);
void camera_mode_tick(void);

/* Capture helpers. The file opener retains its ignored first ABI argument.
 * Successful memory encoding transfers the allocated output to the caller. */
int32_t camera_open_file(uint32_t unused, char **name);
int32_t camera_capture_file(void);
int32_t camera_encode_frame(void **buffer, uint32_t *length);

/* Overlay updates. */
void camera_refresh_battery(CameraWindow root);
void camera_refresh_scene(CameraWindow root);
void camera_refresh_light(CameraWindow root);
void camera_refresh_zoom(CameraWindow root);
void camera_refresh_remaining(CameraWindow root);
void camera_countdown(CameraWindow root, uint32_t show, uint32_t seconds);
void camera_refresh_indicator(CameraWindow root);
void camera_refresh_storage(CameraWindow root);
void camera_show_status(CameraWindow root, camera_word title, camera_word text);
void camera_status_visibility(CameraWindow root, uint32_t show, camera_word title,
                              camera_word text);
void camera_refresh_print(CameraWindow root);
void camera_refresh_resolution(CameraWindow root);
void camera_indicator_timeout(CameraWindow root);

/* Window/message callbacks retain the firmware's three-argument ABI. */
uint32_t camera_msg_noop(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_msg_battery_conditional(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_msg_battery(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_msg_clock(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_window_exit(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_down(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_up(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_left(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_msg_storage(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_back(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_right(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_window_refresh(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_window_enter(CameraWindow root, uint32_t event, const uint32_t *flags);
uint32_t camera_ok(CameraWindow root, uint32_t event, const uint32_t *flags);

#endif /* KC02_CAMERA_SCREEN_H */
