/* KC02 camera-screen reconstruction, pinned to original.bin SHA256
 * 218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5.
 *
 * All 33 camera-region entry points are implemented here. The stock sUI,
 * sensor/LCD/MJPEG, filesystem, audio and printer engines remain imports.
 * Firmware-entry comments link each public body to its original instructions.
 * This remains an offline experimental module, not a validated replacement.
 */
#include "camera_screen.h"

#define IMPORT(ret, name, args, address) extern ret name args;
#include "firmware_imports.def"
#undef IMPORT

#define PTR(address) CAM_PTR((uint32_t)(address), 1)
#define PHOTO_DIR ((const char *)PTR(0x0207b859))
#define PHOTO_NAME_PATTERN PTR(0x0207b870)
#define TEMPLATE_RESOURCE_TABLE_ADDRESS 0x020c2550
#define STORAGE_POPUP_DESCRIPTOR_ADDRESS 0x020c4434

#define KEY_DISPATCH_EVENT 1
#define MAIN_MENU_MODE 6
#define RECORD_TEMPLATE_OPTION 8
#define SETTING_TIMESTAMP 0x0e
#define SETTING_RESOLUTION 0x18
#define UI_CAPTURE_STATE 7
#define IOCTL_CAPTURE_LIGHT 0x20000011
#define IOCTL_STATUS_LED 0x2000000a

#define AUDIO_ZOOM 0x28
#define AUDIO_COUNTDOWN 0x29
#define AUDIO_SHUTTER 0x2c
#define JPEG_SCRATCH_BYTES 0x80000
#define JPEG_SCRATCH_ALIGNMENT 0x40
#define JPEG_OUTPUT_ALIGNMENT 0x20
#define ENCODE_TIMEOUT_MS 2000U
#define INDICATOR_TIMEOUT_MS 800
#define STATUS_MESSAGE_DELAY_MS 2000
#define FILE_OPEN_READ 1
#define FILE_OPEN_CAPTURE 7 /* Preserve the stock flags; not host POSIX flags. */

/* Stock resource identifiers. No resource contents are rebuilt here. */
enum CameraResourceId {
    RESOURCE_BATTERY_EMPTY = 0x80000021,
    RESOURCE_BATTERY_ANIMATION_BASE = 0x80000022U,
    RESOURCE_LIGHT_CAPTURE = 0x80000026,
    RESOURCE_LIGHT_OFF = 0x8000002a,
    RESOURCE_LIGHT_CONTINUOUS = 0x8000002b,
    RESOURCE_SCENE_BURST_3 = 0x8000003f,
    RESOURCE_SCENE_BURST_5 = 0x80000040,
    RESOURCE_INDICATOR_SETTLING = 0x80000041,
    RESOURCE_INDICATOR_ZOOMING = 0x80000042,
    RESOURCE_SCENE_EDGE_A = 0x80000043,
    RESOURCE_SCENE_EDGE_B = 0x80000044,
    RESOURCE_SCENE_TIMER_10 = 0x80000045,
    RESOURCE_SCENE_TIMER_3 = 0x80000046,
    RESOURCE_SCENE_TIMER_5 = 0x80000047,
    RESOURCE_SCENE_TIMER_BURST = 0x80000048,
    RESOURCE_PRINT_OPTION = 0x8000004a,
    RESOURCE_STORAGE_PRESENT = 0x8000004c,
    RESOURCE_STORAGE_ABSENT = 0x8000004d,
    RESOURCE_COUNTDOWN_1 = 0x8000004e,
    RESOURCE_COUNTDOWN_10 = 0x8000004f,
    RESOURCE_COUNTDOWN_2 = 0x80000050,
    RESOURCE_COUNTDOWN_3 = 0x80000051,
    RESOURCE_COUNTDOWN_4 = 0x80000052,
    RESOURCE_COUNTDOWN_5 = 0x80000053,
    RESOURCE_COUNTDOWN_6 = 0x80000054,
    RESOURCE_COUNTDOWN_7 = 0x80000055,
    RESOURCE_COUNTDOWN_8 = 0x80000056,
    RESOURCE_COUNTDOWN_9 = 0x80000057,
    RESOURCE_PRINTING_TEXT = 0x81000056,
    RESOURCE_NO_PAPER_TEXT = 0x81000057,
    RESOURCE_LOW_SUPPLY_TEXT = 0x81000058,
    RESOURCE_STORAGE_ABSENT_TEXT = 0x81000059,
    RESOURCE_STORAGE_FULL_TEXT = 0x8100005a,
    RESOURCE_STATUS_TITLE = 0x8100007f,
    RESOURCE_RESOLUTION_1M = 0x8100009cU,
    RESOURCE_STORAGE_NOTIFICATION_2_TEXT = 0x8100009e,
    RESOURCE_CHARGING_TEXT = 0x810000bb,
};

/* Only these offsets of the opaque stock frame descriptor are used here. */
enum CameraFrameOffset {
    FRAME_Y_ADDRESS = 0x04,
    FRAME_UV_ADDRESS = 0x08,
    FRAME_WIDTH = 0x20,
    FRAME_HEIGHT = 0x22,
};

/* -------------------------------------------------------------------------
 * Shared frame, widget and notification helpers
 * ------------------------------------------------------------------------- */

static uint32_t frame_read_u32(const void *frame, size_t offset)
{
    return *(const uint32_t *)((const uint8_t *)frame + offset);
}

static uint32_t frame_read_u16(const void *frame, size_t offset)
{
    return *(const uint16_t *)((const uint8_t *)frame + offset);
}

static void apply_frame_effect(void *frame)
{
    fw_effect(PTR(frame_read_u32(frame, FRAME_Y_ADDRESS)),
              PTR(frame_read_u32(frame, FRAME_UV_ADDRESS)), frame_read_u16(frame, FRAME_WIDTH),
              frame_read_u16(frame, FRAME_HEIGHT));
}

static void apply_frame_edge_effect(void *frame)
{
    fw_edge(PTR(frame_read_u32(frame, FRAME_Y_ADDRESS)),
            PTR(frame_read_u32(frame, FRAME_UV_ADDRESS)), frame_read_u16(frame, FRAME_WIDTH),
            frame_read_u16(frame, FRAME_HEIGHT), PRESET != PRESET_EDGE_A);
}

static void set_widget_resource(CameraWindow root, uint32_t id, camera_word value)
{
    fw_set_resid(fw_child(root, id), value);
}

static void set_widget_visible(CameraWindow root, uint32_t id, uint32_t visible)
{
    fw_visible(fw_child(root, id), visible);
}

static uint32_t selected_template_resource(void)
{
    return MEM32(TEMPLATE_RESOURCE_TABLE_ADDRESS + (uint32_t)TEMPLATE_INDEX * 4);
}

static int zoom_is_restricted(void)
{
    return GROUP == EFFECT_GROUP_SOFTWARE || GROUP == EFFECT_GROUP_TEMPLATE ||
           (PRESET >= PRESET_EDGE_A && PRESET <= PRESET_EDGE_B);
}

static uint32_t jpeg_quality_for_width(uint32_t width)
{
    /* These are stock quality selectors, not JPEG quality percentages. */
    return width <= 1280 ? 3 : width <= 2560 ? 4 : width <= 3200 ? 6 : 0;
}

static void post_storage_notification(void)
{
    (void)fw_queue_post(UI_MESSAGE_QUEUE, MSG_STORAGE);
}

/* -------------------------------------------------------------------------
 * Mode lifecycle and the stock CSI/LCD preview handshake
 * ------------------------------------------------------------------------- */

/* Firmware entry: 0x020082f4. */
void camera_mode_enter(void)
{
    CAMERA_MODE_ACTIVE = 1;
    fw_encoder_init();
    fw_sensor_adjust();
    fw_record_config(RECORD_TEMPLATE_OPTION, 0);
    fw_frame_mode(0);
    TEMPLATE_INDEX = 0;
    COLOR_MATRIX_INDEX = 0;
    fw_color_preset(0);
    ZOOM = 0;
    fw_crop_start(0, 0);
    SAVED_UI_STATE = 0;
    fw_ui_state(0);
    fw_layer_init(0);
    FRAME_ENABLED = 1;
    GROUP = EFFECT_GROUP_CAPTURE;
    PRESET = PRESET_SINGLE;
    (void)fw_window_open(&camera_descriptor, 0);
    fw_delay(50);
    fw_csi_enable(1);
    fw_delay(50);
}

/* Firmware entry: 0x020083c8. */
void camera_mode_exit(void)
{
    CAMERA_MODE_ACTIVE = 0;
    FRAME_ENABLED = 0;
    GROUP = EFFECT_GROUP_CAPTURE;
    PRESET = PRESET_SINGLE;
    fw_record_config(RECORD_TEMPLATE_OPTION, 0);
    fw_frame_mode(0);
    COLOR_MATRIX_INDEX = 0;
    fw_color_preset(0);

    if (FRAME_STATE == PREVIEW_READY) {
        FRAME_STATE = PREVIEW_ACQUIRED;
        void *frame = fw_frame_get();
        if (frame) {
            FRAME_STATE = PREVIEW_SUBMITTED;
            fw_frame_submit(frame);
            fw_frame_rearm();
        }
    }

    fw_ui_state(SAVED_UI_STATE);
    fw_layer_uninit(0);
    fw_exit_stub();
    fw_record_reset();
    if (STORAGE_STATE == STORAGE_FULL) {
        STORAGE_STATE = STORAGE_READY;
    }
}

/* Firmware entry: 0x020084a0. */
void camera_mode_tick(void)
{
    camera_indicator_timeout(camera_descriptor.handle);
    if (FRAME_STATE != PREVIEW_READY) {
        return;
    }

    FRAME_STATE = PREVIEW_ACQUIRED;
    void *frame = fw_frame_get();
    if (!frame) {
        /* Preserve the stock null-frame branch: state stays ACQUIRED. */
        return;
    }

    if (GROUP == EFFECT_GROUP_SOFTWARE) {
        apply_frame_effect(frame);
    } else if (GROUP == EFFECT_GROUP_TEMPLATE) {
        fw_group3_effect(PTR(frame_read_u32(frame, FRAME_Y_ADDRESS)));
    }

    FRAME_STATE = PREVIEW_SUBMITTED;
    fw_frame_submit(frame);
    fw_frame_rearm();
}

/* -------------------------------------------------------------------------
 * File-backed capture and memory-backed encode/decode/re-encode
 * ------------------------------------------------------------------------- */

/* Firmware entry: 0x02008550. r3 is ignored; r4 is the filename output pointer. */
int32_t camera_open_file(uint32_t unused, char **name)
{
    (void)unused;
    if (STORAGE_STATE != STORAGE_READY) {
        post_storage_notification();
        return -1;
    }
    if (fw_space_check(PHOTO_FILE_MANAGER, PHOTO_DIR, 0x1400) < 0) {
        STORAGE_STATE = STORAGE_FULL;
        post_storage_notification();
        return -2;
    }

    char *path = fw_filename_create(PHOTO_FILE_MANAGER, PHOTO_NAME_PATTERN, PHOTO_DIR);
    if (!path) {
        STORAGE_STATE = STORAGE_FULL;
        post_storage_notification();
        return -2;
    }

    fw_sd_lock();
    int32_t file_descriptor = fw_open(path, FILE_OPEN_CAPTURE);
    if (name) {
        *name = path;
    }
    return file_descriptor;
}

/* Firmware entry: 0x02008630. */
int32_t camera_capture_file(void)
{
    fw_watermark_close((int8_t)WATERMARK_ID);
    WATERMARK_ID = (uint8_t)fw_watermark_open();
    uint32_t timestamp_enabled = (uint8_t)fw_setting_value(SETTING_TIMESTAMP);
    uint32_t dimensions = fw_setting_value(SETTING_RESOLUTION);
    uint32_t width = dimensions >> 16;
    uint32_t height = dimensions & 0xffff;
    uint32_t encoder_flag = fw_encode_flag() != 0;
    fw_watermark_position(0x1e0);

    char *filename = 0;
    int32_t file_descriptor = camera_open_file(0, &filename);
    int32_t result = -1;
    if (file_descriptor >= 0) {
        FRAME_ENABLED = 0;
        CAPTURE_BUSY = 1;
        int32_t encode_result =
            fw_encode_file(filename, file_descriptor, width, height, jpeg_quality_for_width(width),
                           timestamp_enabled, encoder_flag, 0x14, 0x294, 0, 0);
        CAPTURE_BUSY = 0;
        FRAME_ENABLED = 1;
        if (encode_result < 0) {
            fw_close(file_descriptor);
            (void)fw_unlink(filename);
        } else {
            fw_account_space(UINT32_MAX, fw_file_size(file_descriptor));
            fw_close(file_descriptor);
            /* The stock file manager expects the basename after "PHOTO/". */
            fw_file_add(PHOTO_FILE_MANAGER, PHOTO_DIR, filename + 6);
            fw_sd_stop();
            result = 0;
        }
    }

    /* Keep the stock unconditional unlock, including file-opening failures. */
    fw_sd_unlock();
    return result;
}

/* Request layout observed at 0x020089ac..0x020089e4; padding stays initialized. */
typedef struct {
    uint8_t source, mode;
    uint16_t reserved2;
    uint32_t y, uv;
    uint16_t format, reserved_e;
    uint32_t compressed;
    uint16_t width, height;
} DecodeRequest;

_Static_assert(sizeof(DecodeRequest) == 24, "decoder request size");
_Static_assert(offsetof(DecodeRequest, compressed) == 0x10, "decoder input offset");

/* Firmware entry: 0x020087e8.
 * First encode a sensor frame, decode it into the capture planes, apply effects,
 * then re-encode it for RAM printing. Successful output belongs to the caller.
 * Allocation/timeout/decode guards avoid the original's undefined error paths.
 */
int32_t camera_encode_frame(void **buffer, uint32_t *length)
{
    if (!buffer || !length) {
        return -1;
    }
    *buffer = 0;
    *length = 0;

    /* Configure the encoder and start the first pass. */
    fw_watermark_close((int8_t)WATERMARK_ID);
    WATERMARK_ID = (uint8_t)fw_watermark_open();
    uint32_t timestamp_enabled = (uint8_t)fw_setting_value(SETTING_TIMESTAMP);
    uint32_t dimensions = fw_setting_value(SETTING_RESOLUTION);
    uint32_t width = dimensions >> 16;
    uint32_t height = dimensions & 0xffff;
    uint32_t encoder_flag = fw_encode_flag() != 0;
    fw_capture_size_snapshot(width, height);
    if (fw_encoder_mem_init() < 0) {
        return -1;
    }

    uint16_t sensor_width = 0, sensor_height = 0;
    fw_csi_resolution(&sensor_width, &sensor_height);
    (void)fw_encode_start(sensor_width, sensor_height, jpeg_quality_for_width(width), 0,
                          encoder_flag, 0);
    void *jpeg_buffer = fw_alloc(JPEG_SCRATCH_BYTES, JPEG_SCRATCH_ALIGNMENT);
    void *stream_chunk = 0;
    void *stream_data = 0;
    uint32_t jpeg_length = 0, stream_metadata_a = 0, stream_metadata_b = 0;
    uint32_t start = fw_time();
    int32_t result = -1;
    if (!jpeg_buffer) {
        result = -2;
        goto done;
    }

    do {
        stream_chunk =
            fw_encoded_get(0, &stream_data, &jpeg_length, &stream_metadata_a, &stream_metadata_b);
        if (stream_chunk) {
            break;
        }
    } while (fw_time() <= start + ENCODE_TIMEOUT_MS);
    if (!stream_chunk) {
        goto done;
    }
    if (jpeg_length > JPEG_SCRATCH_BYTES) {
        fw_encoded_release(stream_chunk);
        goto done;
    }
    (void)fw_copy(jpeg_buffer, stream_chunk, jpeg_length);
    fw_encoded_release(stream_chunk);

    /* Decode the first-pass JPEG into the stock capture framebuffer. */
    if (timestamp_enabled) {
        fw_watermark_update(width, height, 0);
    }
    CAPTURE_UV = CAPTURE_Y + (uint32_t)CAPTURE_WIDTH * CAPTURE_HEIGHT;
    fw_cache_writeback(jpeg_buffer, jpeg_length);
    fw_csi_enable(1);
    fw_ui_state(UI_CAPTURE_STATE);
    fw_delay(20);
    (void)fw_time();

    DecodeRequest request;
    /* Volatile byte stores avoid an implicit freestanding memset dependency. */
    volatile uint8_t *request_bytes = (volatile uint8_t *)&request;
    for (size_t i = 0; i < sizeof(request); ++i) {
        request_bytes[i] = 0;
    }
    request.mode = 1;
    request.y = CAPTURE_Y;
    request.uv = CAPTURE_UV;
    request.format = 2;
    request.compressed = (uint32_t)(uintptr_t)jpeg_buffer;
    request.width = CAPTURE_WIDTH;
    request.height = CAPTURE_HEIGHT;
    int32_t decode_result = fw_jpeg_decode(&request);
    if (decode_result >= 0) {
        uint16_t decoded_width = 0, decoded_height = 0;
        (void)fw_time();
        fw_decode_resolution(&decoded_width, &decoded_height);
    }
    fw_csi_enable(1);
    fw_ui_state(SAVED_UI_STATE);
    fw_free(jpeg_buffer);
    jpeg_buffer = 0;
    if (decode_result < 0) {
        goto done;
    }

    /* Apply the selected effect and optional timestamp before the second pass. */
    if (CAPTURE_TRANSFORM_FLAG == 1) {
        fw_image_transform(PTR(CAPTURE_Y));
    }
    if (GROUP == EFFECT_GROUP_SOFTWARE) {
        fw_effect(PTR(CAPTURE_Y), PTR(CAPTURE_UV), CAPTURE_WIDTH, CAPTURE_HEIGHT);
    } else if (GROUP == EFFECT_GROUP_CAPTURE && PRESET >= PRESET_EDGE_A &&
               PRESET <= PRESET_EDGE_B) {
        fw_edge(PTR(CAPTURE_Y), PTR(CAPTURE_UV), CAPTURE_WIDTH, CAPTURE_HEIGHT,
                PRESET != PRESET_EDGE_A);
    }
    if (timestamp_enabled) {
        fw_watermark_stamp(PTR(CAPTURE_Y), 0x14, 0x280, 0, 0);
    }
    fw_cache_writeback(PTR(CAPTURE_Y), (uint32_t)CAPTURE_WIDTH * CAPTURE_HEIGHT * 3 / 2);
    (void)fw_encode_start(sensor_width, sensor_height, jpeg_quality_for_width(width),
                          timestamp_enabled, encoder_flag, 1);
    start = fw_time();
    do {
        stream_chunk =
            fw_encoded_get(0, &stream_data, &jpeg_length, &stream_metadata_a, &stream_metadata_b);
        if (stream_chunk) {
            break;
        }
    } while (fw_time() <= start + ENCODE_TIMEOUT_MS);
    if (!stream_chunk) {
        goto done;
    }

    /* Preserve the stock release-before-copy order. The host mock cannot prove
     * that the real stream buffer remains valid after this release. */
    fw_encoded_release(stream_chunk);
    jpeg_buffer = fw_alloc(jpeg_length, JPEG_OUTPUT_ALIGNMENT);
    if (!jpeg_buffer) {
        result = -2;
        goto done;
    }
    (void)fw_copy(jpeg_buffer, stream_data, jpeg_length);
    fw_cache_flush(jpeg_buffer, jpeg_length);
    *buffer = jpeg_buffer;
    *length = jpeg_length;
    jpeg_buffer = 0; /* Ownership transfers to the caller. */
    result = 0;

done:
    if (jpeg_buffer) {
        fw_free(jpeg_buffer);
    }
    fw_encoder_uninit();
    return result;
}

/* -------------------------------------------------------------------------
 * Overlay updates and system notifications
 * ------------------------------------------------------------------------- */

/* Firmware entry: 0x02008bd4. */
uint32_t camera_msg_noop(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    (void)event;
    (void)flags;
    return 0;
}

/* Firmware entry: 0x02008bec. */
void camera_refresh_battery(CameraWindow root)
{
    uint32_t icon;
    if (BATTERY_ICON_ANIMATION_ENABLED) {
        BATTERY_ICON_PHASE = (uint8_t)(BATTERY_ICON_PHASE + 1);
        if (BATTERY_ICON_PHASE > 3) {
            BATTERY_ICON_PHASE = 0;
        }
        icon = RESOURCE_BATTERY_ANIMATION_BASE + BATTERY_ICON_PHASE;
    } else {
        icon = RESOURCE_BATTERY_EMPTY + (BATTERY_LEVEL < 4 ? BATTERY_LEVEL : 4);
    }
    set_widget_resource(root, WIDGET_BATTERY, icon);
    set_widget_visible(root, WIDGET_BATTERY, 1);
}

/* Firmware entry: 0x02008d3c. */
uint32_t camera_msg_battery_conditional(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)event;
    (void)flags;
    if (BATTERY_ICON_ANIMATION_ENABLED == 0) {
        camera_refresh_battery(root);
    }
    return 0;
}

/* Firmware entry: 0x02008d74. */
uint32_t camera_msg_battery(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)event;
    (void)flags;
    camera_refresh_battery(root);
    return 0;
}

/* Firmware entry: 0x02008d98. */
void camera_refresh_scene(CameraWindow root)
{
    static const uint32_t scene_icons[9] = {
        0,
        RESOURCE_SCENE_BURST_3,
        RESOURCE_SCENE_BURST_5,
        RESOURCE_SCENE_TIMER_3,
        RESOURCE_SCENE_TIMER_5,
        RESOURCE_SCENE_TIMER_10,
        RESOURCE_SCENE_TIMER_BURST,
        RESOURCE_SCENE_EDGE_A,
        RESOURCE_SCENE_EDGE_B,
    };
    if (PRESET > PRESET_EDGE_B) {
        return;
    }
    if (PRESET) {
        set_widget_resource(root, WIDGET_SCENE, scene_icons[PRESET]);
    }
    set_widget_visible(root, WIDGET_SCENE, PRESET != PRESET_SINGLE);
}

/* Firmware entry: 0x02008f0c. */
void camera_refresh_light(CameraWindow root)
{
    if (LIGHT == LIGHT_OFF || LIGHT == LIGHT_CONTINUOUS) {
        set_widget_resource(root, WIDGET_LIGHT,
                            LIGHT == LIGHT_OFF ? RESOURCE_LIGHT_OFF : RESOURCE_LIGHT_CONTINUOUS);
        set_widget_visible(root, WIDGET_ALTERNATE_LIGHT, 0);
        set_widget_visible(root, WIDGET_LIGHT, 1);
    } else if (LIGHT == LIGHT_DURING_CAPTURE) {
        set_widget_resource(root, WIDGET_ALTERNATE_LIGHT, RESOURCE_LIGHT_CAPTURE);
        set_widget_visible(root, WIDGET_LIGHT, 0);
        set_widget_visible(root, WIDGET_ALTERNATE_LIGHT, 1);
    }
}

/* Firmware entry: 0x02009050. Explicit termination avoids a stale stack byte. */
void camera_refresh_zoom(CameraWindow root)
{
    uint32_t display_zoom = (uint8_t)(ZOOM + 1);
    char text[4] = {
        (char)('0' + (display_zoom / 10) % 10),
        (char)('0' + display_zoom % 10),
        'X',
        0,
    };
    set_widget_resource(root, WIDGET_ZOOM, (camera_word)text);
    set_widget_visible(root, WIDGET_ZOOM, 1);
    fw_present(1);
}

/* Firmware entry: 0x02009108. */
uint32_t camera_msg_clock(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)event;
    (void)flags;
    set_widget_resource(root, WIDGET_CLOCK, (camera_word)fw_rtc_string(fw_rtc_get()));
    if (BATTERY_ICON_ANIMATION_ENABLED == 1) {
        camera_refresh_battery(root);
    }
    return 0;
}

/* Firmware entry: 0x0200917c. */
void camera_refresh_remaining(CameraWindow root)
{
    uint32_t dimensions = fw_setting_value(SETTING_RESOLUTION);
    uint32_t pixels = (dimensions >> 16) * (dimensions & 0xffff);
    /* Preserve the stock space estimate and units; this is not a JPEG byte count. */
    uint32_t estimated_space_per_photo = pixels <= 0xe1000    ? 120
                                         : pixels <= 0x384000 ? 260
                                         : pixels <= 0x57e400 ? 340
                                         : pixels <= 0x7d3e80 ? 400
                                                              : 500;
    uint32_t remaining_photos =
        STORAGE_STATE == STORAGE_READY ? STORAGE_CAPACITY_ESTIMATE / estimated_space_per_photo : 0;
    char text[7];
    text[6] = 0;
    for (int digit = 5; digit >= 0; --digit) {
        text[digit] = (char)('0' + remaining_photos % 10);
        remaining_photos /= 10;
    }
    set_widget_resource(root, WIDGET_REMAINING_PHOTOS, (camera_word)text);
    set_widget_visible(root, WIDGET_REMAINING_PHOTOS, 1);
    fw_present(1);
}

/* Firmware entry: 0x02009358. */
void camera_countdown(CameraWindow root, uint32_t show, uint32_t seconds)
{
    static const uint32_t countdown_icons[10] = {
        RESOURCE_COUNTDOWN_1, RESOURCE_COUNTDOWN_2,  RESOURCE_COUNTDOWN_3, RESOURCE_COUNTDOWN_4,
        RESOURCE_COUNTDOWN_5, RESOURCE_COUNTDOWN_6,  RESOURCE_COUNTDOWN_7, RESOURCE_COUNTDOWN_8,
        RESOURCE_COUNTDOWN_9, RESOURCE_COUNTDOWN_10,
    };
    if (!show) {
        set_widget_visible(root, WIDGET_COUNTDOWN, 0);
        fw_present(1);
        return;
    }
    /* The stock caller reaches zero after the final decrement. Hide the icon
     * instead of reproducing its stale/uninitialized resource register. */
    if (seconds < 1 || seconds > 10) {
        set_widget_visible(root, WIDGET_COUNTDOWN, 0);
        fw_present(1);
        return;
    }
    set_widget_resource(root, WIDGET_COUNTDOWN, countdown_icons[seconds - 1]);
    set_widget_visible(root, WIDGET_COUNTDOWN, 1);
    fw_present(1);
    (void)fw_audio_play(AUDIO_COUNTDOWN, 1, 0);
}

/* Firmware entry: 0x0200947c. */
uint32_t camera_window_exit(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    (void)event;
    (void)flags;
    fw_glyphs_free();
    LIGHT = LIGHT_OFF;
    CAMERA_KEY_GATE = 0;
    return 0;
}

/* Firmware entry: 0x020094b8. */
void camera_refresh_indicator(CameraWindow root)
{
    if (ZOOM_INDICATOR_STATE == INDICATOR_HIDDEN) {
        set_widget_visible(root, WIDGET_ZOOM_INDICATOR, 0);
        return;
    }
    if (ZOOM_INDICATOR_STATE == INDICATOR_ZOOMING) {
        set_widget_resource(root, WIDGET_ZOOM_INDICATOR, RESOURCE_INDICATOR_ZOOMING);
    } else if (ZOOM_INDICATOR_STATE == INDICATOR_SETTLING) {
        set_widget_resource(root, WIDGET_ZOOM_INDICATOR, RESOURCE_INDICATOR_SETTLING);
    }
    set_widget_visible(root, WIDGET_ZOOM_INDICATOR, 1);
}

/* -------------------------------------------------------------------------
 * Zoom and the bidirectional capture/matrix/software/template preset cycle
 * ------------------------------------------------------------------------- */

static int handle_zoom_step(CameraWindow root, uint32_t event, const uint32_t *flags, int delta)
{
    if (event != KEY_DISPATCH_EVENT || !flags || *flags != KEY_FLAG_ZOOM_STEP) {
        return 0;
    }
    if (zoom_is_restricted()) {
        return 1;
    }
    if (delta > 0) {
        if (ZOOM < 15) {
            ++ZOOM;
        }
    } else {
        if (ZOOM) {
            --ZOOM;
        }
    }
    fw_crop_start(ZOOM, 0);
    if (GROUP == EFFECT_GROUP_TEMPLATE) {
        (void)fw_template_resize(selected_template_resource());
    }
    ZOOM_INDICATOR_STATE = INDICATOR_ZOOMING;
    camera_refresh_indicator(root);
    camera_refresh_zoom(root);
    return 1;
}

static int finish_zoom_indicator(CameraWindow root)
{
    if (ZOOM_INDICATOR_STATE != INDICATOR_ZOOMING) {
        return 0;
    }
    INDICATOR_TIME = fw_time();
    ZOOM_INDICATOR_STATE = INDICATOR_SETTLING;
    camera_refresh_indicator(root);
    fw_audio_buffer(PTR(AUDIO_BUFFER), AUDIO_LENGTH);
    return 1;
}

static void refresh_after_preset_change(CameraWindow root)
{
    if (zoom_is_restricted() && ZOOM) {
        ZOOM = 0;
        fw_crop_start(0, 0);
        (void)fw_template_resize(selected_template_resource());
        camera_refresh_zoom(root);
    }
    camera_refresh_scene(root);
}

/* Firmware entry: 0x02009568. Forward cycle: capture -> matrix -> software -> template. */
uint32_t camera_down(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    if (event != KEY_DISPATCH_EVENT || !flags) {
        return 0;
    }
    if (handle_zoom_step(root, event, flags, -1) || *flags != KEY_FLAG_PRESET_STEP) {
        return 0;
    }
    if (finish_zoom_indicator(root)) {
        return 0;
    }

    if (GROUP == EFFECT_GROUP_CAPTURE) {
        if (PRESET < PRESET_EDGE_B) {
            if (!PRINT_OPTION) {
                ++PRESET;
                goto finish;
            }
            if (PRESET != PRESET_EDGE_A) {
                PRESET = PRESET_EDGE_A;
                goto finish;
            }
        }
        PRESET = PRESET_SINGLE;
        GROUP = EFFECT_GROUP_COLOR_MATRIX;
        COLOR_MATRIX_INDEX = 1;
        fw_color_preset(1);
    } else if (GROUP == EFFECT_GROUP_COLOR_MATRIX) {
        if (COLOR_MATRIX_INDEX > 4) {
            COLOR_MATRIX_INDEX = 0;
            fw_color_preset(0);
            SOFTWARE_EFFECT_INDEX = 0;
            GROUP = EFFECT_GROUP_SOFTWARE;
        } else {
            ++COLOR_MATRIX_INDEX;
            fw_color_preset(COLOR_MATRIX_INDEX);
        }
    } else if (GROUP == EFFECT_GROUP_SOFTWARE) {
        if (SOFTWARE_EFFECT_INDEX < 7) {
            ++SOFTWARE_EFFECT_INDEX;
        } else {
            GROUP = EFFECT_GROUP_TEMPLATE;
            fw_frame_mode(1);
            fw_record_config(RECORD_TEMPLATE_OPTION, 1);
            TEMPLATE_INDEX = 0;
            (void)fw_template_load(selected_template_resource());
        }
    } else if (GROUP == EFFECT_GROUP_TEMPLATE) {
        if (TEMPLATE_INDEX > 7) {
            fw_record_config(RECORD_TEMPLATE_OPTION, 0);
            fw_frame_mode(0);
            GROUP = EFFECT_GROUP_CAPTURE;
            PRESET = PRESET_SINGLE;
        } else {
            ++TEMPLATE_INDEX;
            (void)fw_template_load(selected_template_resource());
        }
    }

finish:
    refresh_after_preset_change(root);
    return 0;
}

/* Firmware entry: 0x02009890. Reverse the same preset cycle. */
uint32_t camera_up(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    if (event != KEY_DISPATCH_EVENT || !flags) {
        return 0;
    }
    if (handle_zoom_step(root, event, flags, 1) || *flags != KEY_FLAG_PRESET_STEP) {
        return 0;
    }
    if (finish_zoom_indicator(root)) {
        return 0;
    }

    if (GROUP == EFFECT_GROUP_CAPTURE) {
        if (PRESET) {
            PRESET = PRINT_OPTION ? PRESET_SINGLE : (uint8_t)(PRESET - 1);
            goto finish;
        }
        GROUP = EFFECT_GROUP_TEMPLATE;
        fw_frame_mode(1);
        fw_record_config(RECORD_TEMPLATE_OPTION, 1);
        TEMPLATE_INDEX = 8;
        (void)fw_template_load(selected_template_resource());
    } else if (GROUP == EFFECT_GROUP_COLOR_MATRIX) {
        if (COLOR_MATRIX_INDEX) {
            --COLOR_MATRIX_INDEX;
            fw_color_preset(COLOR_MATRIX_INDEX);
            if (!COLOR_MATRIX_INDEX) {
                fw_color_preset(0);
                GROUP = EFFECT_GROUP_CAPTURE;
                PRESET = PRINT_OPTION ? PRESET_EDGE_A : PRESET_EDGE_B;
            }
        }
    } else if (GROUP == EFFECT_GROUP_SOFTWARE) {
        if (!SOFTWARE_EFFECT_INDEX) {
            GROUP = EFFECT_GROUP_COLOR_MATRIX;
            COLOR_MATRIX_INDEX = 5;
            fw_color_preset(5);
        } else {
            --SOFTWARE_EFFECT_INDEX;
        }
    } else if (GROUP == EFFECT_GROUP_TEMPLATE) {
        if (!TEMPLATE_INDEX) {
            fw_record_config(RECORD_TEMPLATE_OPTION, 0);
            fw_frame_mode(0);
            SOFTWARE_EFFECT_INDEX = 7;
            GROUP = EFFECT_GROUP_SOFTWARE;
        } else {
            --TEMPLATE_INDEX;
            (void)fw_template_load(selected_template_resource());
        }
    }

finish:
    refresh_after_preset_change(root);
    return 0;
}

/* -------------------------------------------------------------------------
 * Status dialogs, storage and remaining button/window callbacks
 * ------------------------------------------------------------------------- */

/* Firmware entry: 0x02009bc0. */
void camera_refresh_storage(CameraWindow root)
{
    set_widget_resource(root, WIDGET_STORAGE,
                        STORAGE_STATE == STORAGE_ABSENT ? RESOURCE_STORAGE_ABSENT
                                                        : RESOURCE_STORAGE_PRESENT);
}

/* Firmware entry: 0x02009c1c. */
void camera_show_status(CameraWindow root, camera_word title, camera_word text)
{
    set_widget_visible(root, WIDGET_STATUS_BACKGROUND, 1);
    set_widget_resource(root, WIDGET_STATUS_TITLE, title);
    set_widget_visible(root, WIDGET_STATUS_TITLE, 1);
    set_widget_resource(root, WIDGET_STATUS_TEXT, text);
    set_widget_visible(root, WIDGET_STATUS_TEXT, 1);
}

/* Firmware entry: 0x02009ccc. */
void camera_status_visibility(CameraWindow root, uint32_t show, camera_word title, camera_word text)
{
    if (show) {
        camera_show_status(root, title, text);
    } else {
        set_widget_visible(root, WIDGET_STATUS_BACKGROUND, 0);
        set_widget_visible(root, WIDGET_STATUS_TITLE, 0);
        set_widget_visible(root, WIDGET_STATUS_TEXT, 0);
    }
}

/* Firmware entry: 0x02009d5c. */
void camera_refresh_print(CameraWindow root)
{
    if (PRINT_OPTION) {
        set_widget_resource(root, WIDGET_PRINT_OPTION, RESOURCE_PRINT_OPTION);
    }
    set_widget_visible(root, WIDGET_PRINT_OPTION, PRINT_OPTION != 0);
}

/* Firmware entry: 0x02009de0. */
uint32_t camera_left(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    if (event == KEY_DISPATCH_EVENT && flags && *flags == KEY_FLAG_PRIMARY) {
        if (!PRINT_OPTION) {
            PRINT_OPTION = 1;
            if (GROUP == EFFECT_GROUP_CAPTURE && PRESET) {
                PRESET = PRESET_EDGE_A;
            }
        } else {
            PRINT_OPTION = 0;
        }
        camera_refresh_print(root);
        camera_refresh_scene(root);
    }
    return 0;
}

/* Firmware entry: 0x02009e70. win_open's extra arguments are stack-passed. */
uint32_t camera_msg_storage(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)event;
    (void)flags;
    camera_refresh_storage(root);
    camera_refresh_remaining(root);
    uint32_t text = STORAGE_STATE == STORAGE_ABSENT           ? RESOURCE_STORAGE_ABSENT_TEXT
                    : STORAGE_STATE == STORAGE_FULL           ? RESOURCE_STORAGE_FULL_TEXT
                    : STORAGE_STATE == STORAGE_NOTIFICATION_2 ? RESOURCE_STORAGE_NOTIFICATION_2_TEXT
                                                              : 0;
    if (text) {
        (void)fw_window_open((CameraDescriptor *)PTR(STORAGE_POPUP_DESCRIPTOR_ADDRESS), 2, text,
                             2U);
    }
    return 0;
}

/* Firmware entry: 0x02009f08. Keep the actual r4/r5 guard, not the raw prototype. */
uint32_t camera_back(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    if (event == KEY_DISPATCH_EVENT && flags && *flags == KEY_FLAG_PRIMARY) {
        fw_mode_request(MAIN_MENU_MODE, 0);
        if (!TRANSITION_STATE) {
            TRANSITION_PHASE = 0;
            TRANSITION_STATE = 2;
        }
    }
    return 0;
}

/* Firmware entry: 0x02009f6c. */
uint32_t camera_right(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    if (event == KEY_DISPATCH_EVENT && flags && *flags == KEY_FLAG_PRIMARY) {
        if (LIGHT == LIGHT_OFF) {
            LIGHT = LIGHT_DURING_CAPTURE;
        } else if (LIGHT == LIGHT_DURING_CAPTURE) {
            LIGHT = LIGHT_CONTINUOUS;
        } else {
            LIGHT = LIGHT_OFF;
            (void)fw_board_ioctl((int8_t)LIGHT_DEVICE, IOCTL_CAPTURE_LIGHT, 0);
        }
        if (LIGHT == LIGHT_CONTINUOUS) {
            (void)fw_board_ioctl((int8_t)LIGHT_DEVICE, IOCTL_CAPTURE_LIGHT, 1);
        }
        camera_refresh_light(root);
    }
    return 0;
}

/* Firmware entry: 0x0200a030. */
void camera_refresh_resolution(CameraWindow root)
{
    /* Stock string addresses, ordered 1M, 2M, 3M, 5M, 8M, 10M, 12M,
     * 16M, 18M, 20M, 24M, 40M, 48M. Several point into shared strings. */
    static const uint32_t resolution_text_addresses[13] = {
        0x0207ba28, 0x0207ba36, 0x0207ba2b, 0x0207ba2e, 0x0207ba3e, 0x0207ba31, 0x0207ba35,
        0x0207ba39, 0x0207ba3d, 0x0207ba41, 0x0207ba45, 0x0207ba49, 0x0207ba4d,
    };
    uint32_t resolution_key = (uint32_t)fw_config_get(SETTING_RESOLUTION);
    uint32_t index = RESOURCE_RESOLUTION_1M - resolution_key;
    set_widget_resource(
        root, WIDGET_RESOLUTION,
        (camera_word)PTR(index < 13 ? resolution_text_addresses[index] : 0x0207ba51));
}

/* Firmware entry: 0x0200a310. */
uint32_t camera_window_refresh(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)event;
    (void)flags;
    camera_refresh_resolution(root);
    camera_refresh_storage(root);
    camera_refresh_battery(root);
    camera_countdown(root, 0, 0);
    camera_status_visibility(root, 0, 0, 0);
    camera_refresh_print(root);
    return 0;
}

/* Firmware entry: 0x0200a37c. */
uint32_t camera_window_enter(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    (void)event;
    (void)flags;
    LIGHT = LIGHT_OFF;
    (void)fw_init_camera_name();
    camera_status_visibility(root, 0, 0, 0);
    camera_refresh_resolution(root);
    camera_refresh_storage(root);
    camera_refresh_battery(root);
    camera_refresh_scene(root);
    camera_countdown(root, 0, 0);
    camera_refresh_print(root);
    camera_refresh_remaining(root);
    CAMERA_KEY_GATE = 1;
    camera_refresh_indicator(root);
    if (!AUDIO_BUFFER) {
        AUDIO_BUFFER = (uint32_t)(uintptr_t)fw_load_audio(AUDIO_ZOOM, (uint32_t *)PTR(0x020ccd9c));
    }
    if (!LIGHT) {
        set_widget_visible(root, WIDGET_ALTERNATE_LIGHT, 0);
    }
    camera_refresh_light(root);
    if (!TRANSITION_STATE) {
        TRANSITION_PHASE = 0;
        TRANSITION_STATE = 1;
    }
    fw_glyphs_init(16, 32, 13);
    return 0;
}

/* -------------------------------------------------------------------------
 * Capture/print orchestration: audio, lighting, countdown, file and RAM paths
 * ------------------------------------------------------------------------- */

static void set_capture_light(uint32_t on)
{
    if (LIGHT == LIGHT_DURING_CAPTURE) {
        (void)fw_board_ioctl((int8_t)LIGHT_DEVICE, IOCTL_CAPTURE_LIGHT, on);
    }
}

static void set_status_led(uint32_t on)
{
    (void)fw_board_ioctl((int8_t)LED_DEVICE, IOCTL_STATUS_LED, on);
}

static void show_print_result(CameraWindow root, int32_t status)
{
    uint32_t text = status == 8    ? RESOURCE_NO_PAPER_TEXT
                    : status == 9  ? RESOURCE_LOW_SUPPLY_TEXT
                    : status == 14 ? RESOURCE_CHARGING_TEXT
                                   : 0;
    if (text) {
        camera_show_status(root, RESOURCE_STATUS_TITLE, text);
        fw_present(1);
        fw_delay(STATUS_MESSAGE_DELAY_MS);
    }
}

static void show_printing_dialog(CameraWindow root)
{
    camera_show_status(root, RESOURCE_STATUS_TITLE, RESOURCE_PRINTING_TEXT);
    fw_present(1);
}

/* Firmware entry: 0x0200a4bc. This is local orchestration, not the old OK handler. */
uint32_t camera_ok(CameraWindow root, uint32_t event, const uint32_t *flags)
{
    if (event != KEY_DISPATCH_EVENT || !flags || *flags != KEY_FLAG_PRIMARY) {
        return 0;
    }

    /* Capture presets select the burst size and optional initial countdown. */
    uint32_t shots = 1, seconds = 0;
    if (GROUP == EFFECT_GROUP_CAPTURE) {
        if (PRESET == PRESET_BURST_3) {
            shots = 3;
        } else if (PRESET == PRESET_BURST_5) {
            shots = 5;
        } else if (PRESET == PRESET_TIMER_3) {
            seconds = 3;
        } else if (PRESET == PRESET_TIMER_5) {
            seconds = 5;
        } else if (PRESET == PRESET_TIMER_10) {
            seconds = 10;
        } else if (PRESET == PRESET_TIMER_5_BURST_3) {
            seconds = 5;
            shots = 3;
        }
    }

    /* Keep servicing the stock scheduler/preview during the countdown. */
    if (seconds) {
        uint32_t start = fw_time();
        fw_delay(100);
        fw_service();
        camera_countdown(root, 1, seconds);
        for (;;) {
            fw_service();
            if ((uint32_t)(fw_time() - start) > 999) {
                --seconds;
                camera_countdown(root, 1, seconds);
                if (!seconds) {
                    break;
                }
                start = fw_time();
            }
            camera_mode_tick();
        }
        camera_countdown(root, 0, 0);
    }

    for (uint32_t shot = 0; shot < shots; ++shot) {
        camera_mode_tick();
        if (STORAGE_STATE == STORAGE_FULL || STORAGE_STATE == STORAGE_READY) {
            /* File-backed capture; optionally print the saved photo afterward. */
            set_status_led(0);
            set_capture_light(1);
            if (GROUP == EFFECT_GROUP_CAPTURE && PRESET >= PRESET_EDGE_A &&
                PRESET <= PRESET_EDGE_B) {
                void *frame = fw_frame_get();
                if (frame) {
                    apply_frame_edge_effect(frame);
                    fw_frame_submit(frame);
                }
            }
            fw_csi_enable(0);
            fw_layer_uninit(0);
            int32_t capture_result = camera_capture_file();
            (void)fw_audio_play(AUDIO_SHUTTER, 1, 0);
            while (fw_audio_status() == 1) {
                fw_delay(10);
            }
            fw_csi_enable(1);
            fw_layer_init(0);
            set_status_led(1);
            set_capture_light(0);

            if (capture_result == 0) {
                fw_service();
                fw_csi_enable(0);
                fw_ui_state(UI_CAPTURE_STATE);
                if (!PRINT_OPTION) {
                    fw_delay(1000);
                } else if (fw_manager_service(PHOTO_BROWSER_MANAGER) > 0) {
                    uint32_t length = 0;
                    uint32_t index = (uint32_t)fw_manager_service(PHOTO_BROWSER_MANAGER);
                    char *path = fw_file_path(PHOTO_BROWSER_MANAGER, index, &length);
                    if (path) {
                        int32_t file_descriptor = fw_open(path, FILE_OPEN_READ);
                        if (file_descriptor >= 0) {
                            /* Preserve the stock close/dialog/reopen sequence. */
                            fw_close(file_descriptor);
                            show_printing_dialog(root);
                            file_descriptor = fw_open(path, FILE_OPEN_READ);
                            if (file_descriptor >= 0) {
                                show_print_result(root,
                                                  fw_print_file(file_descriptor, PRINT_DENSITY,
                                                                PRINT_LAYOUT, BATTERY_LEVEL));
                                fw_close(file_descriptor);
                                camera_status_visibility(root, 0, 0, 0);
                                fw_present(1);
                            }
                        }
                    }
                }
                fw_service();
                fw_csi_enable(1);
                fw_ui_state(SAVED_UI_STATE);
            }
        } else if (PRINT_OPTION) {
            /* No storage path: encode and print an allocated JPEG from RAM. */
            void *image = 0;
            uint32_t length = 0;
            set_capture_light(1);
            set_status_led(0);
            int32_t encode_result = camera_encode_frame(&image, &length);
            set_status_led(1);
            set_capture_light(0);
            if (encode_result == 0) {
                show_printing_dialog(root);
                fw_delay(1000);
                show_print_result(
                    root, fw_print_image(image, PRINT_DENSITY, PRINT_LAYOUT, BATTERY_LEVEL));
                fw_free(image);
                camera_status_visibility(root, 0, 0, 0);
                fw_present(1);
            }
        } else {
            post_storage_notification();
        }
    }
    camera_refresh_remaining(root);
    return 0;
}

/* Firmware entry: 0x0200aa8c. */
void camera_indicator_timeout(CameraWindow root)
{
    if (ZOOM_INDICATOR_STATE == INDICATOR_SETTLING &&
        (uint32_t)(fw_time() - INDICATOR_TIME) > INDICATOR_TIMEOUT_MS) {
        ZOOM_INDICATOR_STATE = INDICATOR_HIDDEN;
        camera_refresh_indicator(root);
        camera_refresh_zoom(root);
    }
}

/* -------------------------------------------------------------------------
 * Window construction and dispatch tables
 * ------------------------------------------------------------------------- */

/* Stock win_open writes widgets[0].private_data and camera_descriptor.handle.
 * Creation records therefore remain writable; only the binding table is const. */
#include "camera_widgets.inc"

const CameraBinding camera_bindings[16] = {
    {MSG_WINDOW_ENTER, camera_window_enter},
    {MSG_WINDOW_EXIT, camera_window_exit},
    {MSG_WINDOW_REFRESH, camera_window_refresh},
    {MSG_UP, camera_up},
    {MSG_DOWN, camera_down},
    {MSG_RIGHT, camera_right},
    {MSG_LEFT, camera_left},
    {MSG_OK, camera_ok},
    {MSG_ALTERNATE_OK, camera_ok},
    {MSG_BACK, camera_back},
    {MSG_STORAGE, camera_msg_storage},
    {MSG_BATTERY, camera_msg_battery},
    {MSG_BATTERY_CONDITIONAL, camera_msg_battery_conditional},
    {MSG_NOOP, camera_msg_noop},
    {MSG_CLOCK, camera_msg_clock},
    {MSG_TABLE_END, 0},
};

CameraDescriptor camera_descriptor = {camera_bindings, camera_widgets, 0, 0, {0, 0, 0}};
