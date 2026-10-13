/* Readable KC02 main-menu reconstruction: all 15 entries 0x020130bc..0x02013a78.
 * Firmware SHA256: 218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5.
 * Uses stock sUI/display/resource engines. Offline compilation is not deployment
 * or target-ABI validation. The raw decompilation retains the original labels.
 */
#include "main_menu_screen.h"
#define IMPORT(ret, name, args, address) extern ret name args;
#include "firmware_imports.def"
#undef IMPORT
#define PTR(address) MENU_PTR(address, 1)
#define MENU_BACKGROUND_RESOURCE 0x20
#define MENU_ICON_SIZE 96
#define MENU_BMP_SCRATCH_BYTES 0x6c00 /* 96 * 96 * 3, excluding the BMP header. */
#define MENU_BUFFER_ALIGNMENT 0x20
#define MENU_ICON_KEY_Y 0x8a
#define MENU_ICON_KEY_UV 0x81

/* Verified tables at 0x0207bb60 and 0x0207bb78. The JPEG contains all normal
 * icons; these BMPs overlay the selected cell only during blink phase zero. */
static const int16_t icon_positions[MENU_ITEM_COUNT][2] = {
    {12, 18}, {112, 18}, {212, 18}, {12, 126}, {112, 126}, {212, 126},
};
static const uint32_t icon_resources[MENU_ITEM_COUNT] = {0x22, 0x25, 0x1f, 0x23, 0x21, 0x24};

static uint32_t frame_u32(const void *frame, size_t offset)
{
    return *(const uint32_t *)((const uint8_t *)frame + offset);
}
static uint16_t frame_u16(const void *frame, size_t offset)
{
    return *(const uint16_t *)((const uint8_t *)frame + offset);
}

/* -------------------------------------------------------------------------
 * Lifecycle: no mode tick work; animation arrives as window message 0x09.
 * ------------------------------------------------------------------------- */

/* Firmware entry: 0x020130bc. */
void main_menu_mode_tick(void)
{
}

/* Firmware entry: 0x020130d0. Buffers are freed by the window-exit callback. */
void main_menu_mode_exit(void)
{
}

/* Firmware entry: 0x020130e4. */
void main_menu_mode_enter(void)
{
    menu_fw_csi_enable(0);
    menu_fw_ui_state(7);
    (void)menu_fw_window_open(&menu_descriptor, 0);
}

/* Firmware entry: 0x02013120. Message 0x0a is a no-op here. */
uint32_t main_menu_noop(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    (void)event;
    (void)flags;
    return 0;
}

/* -------------------------------------------------------------------------
 * Background/video-plane rendering, separate from the sUI label widgets.
 * ------------------------------------------------------------------------- */

/* Firmware entry: 0x02013138. */
void main_menu_render(uint32_t phase)
{
    void *frame = menu_fw_frame_get(0);
    if (!frame) {
        return;
    }
    uint16_t width, height;
    (void)menu_fw_lcd_resolution(&width, &height);
    menu_fw_frame_config(frame, 0, 0, width, height);

    void *destination = PTR(frame_u32(frame, 0x04));
    if (phase == 0) {
        (void)menu_fw_copy(destination, PTR(MENU_BACKGROUND_BUFFER),
                           (uint32_t)MENU_BACKGROUND_HEIGHT * MENU_BACKGROUND_WIDTH * 3 / 2);
        uint32_t selected = MENU_SELECTED;
        /* Preserve the stock height/width argument order. Both are 96 here;
         * rectangular icons require a fresh audit of clipping/stride arguments. */
        menu_fw_blit(destination, frame_u16(frame, 0x20), frame_u16(frame, 0x22),
                     icon_positions[selected][0], icon_positions[selected][1], MENU_ICON_HEIGHT,
                     MENU_ICON_WIDTH, PTR(MENU_ICON_BUFFER(selected)), MENU_ICON_HEIGHT,
                     MENU_ICON_WIDTH, MENU_ICON_KEY_Y, MENU_ICON_KEY_UV);
        menu_fw_cache_flush(destination,
                            (uint32_t)frame_u16(frame, 0x20) * frame_u16(frame, 0x22) * 3 / 2);
    } else {
        (void)menu_fw_copy(destination, PTR(MENU_BACKGROUND_BUFFER),
                           (uint32_t)MENU_BACKGROUND_HEIGHT * MENU_BACKGROUND_WIDTH * 3 / 2);
        menu_fw_cache_flush(destination,
                            (uint32_t)frame_u16(frame, 0x20) * frame_u16(frame, 0x22) * 3 / 2);
    }
    /* r3=layer zero, r4=frame; the raw decompiler shifts these incorrectly. */
    (void)menu_fw_frame_show(0, frame, 0, 0, 0xff);
}

/* Firmware entry: 0x020132e4. IDs 0..5 are captions, not icon widgets. */
void main_menu_set_labels_visible(MenuWindow root, uint32_t visible)
{
    for (uint32_t id = 0; id < MENU_ITEM_COUNT; ++id) {
        menu_fw_visible(menu_fw_child(root, id), visible != 0);
    }
}

/* Firmware entry: 0x02013434. */
uint32_t main_menu_window_exit(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    (void)event;
    (void)flags;
    if (MENU_BACKGROUND_BUFFER) {
        menu_fw_free(PTR(MENU_BACKGROUND_BUFFER));
        MENU_BACKGROUND_BUFFER = 0;
    }
    for (uint32_t index = 0; index < MENU_ITEM_COUNT; ++index) {
        if (MENU_ICON_BUFFER(index)) {
            menu_fw_free(PTR(MENU_ICON_BUFFER(index)));
            MENU_ICON_BUFFER(index) = 0;
        }
    }
    return 0;
}

/* Firmware entry: 0x020134b8. Resource IDs are NV IDs, not zero-based SFAT indices. */
uint32_t main_menu_window_enter(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)event;
    (void)flags;
    MENU_BLINK_PHASE = 0;
    if (MENU_SELECTED > 5) {
        MENU_SELECTED = 0;
    }
    (void)menu_fw_lcd_resolution((uint16_t *)PTR(0x020cceca), (uint16_t *)PTR(0x020ccec8));
    MENU_BACKGROUND_BUFFER = menu_fw_alloc(
        (uint32_t)MENU_BACKGROUND_HEIGHT * MENU_BACKGROUND_WIDTH * 3 / 2, MENU_BUFFER_ALIGNMENT);
    if (!MENU_BACKGROUND_BUFFER) {
        return 0;
    }
    (void)menu_fw_decode_background(
        MENU_BACKGROUND_RESOURCE, PTR(MENU_BACKGROUND_BUFFER),
        PTR(MENU_BACKGROUND_BUFFER + (uint32_t)MENU_BACKGROUND_WIDTH * MENU_BACKGROUND_HEIGHT),
        MENU_BACKGROUND_WIDTH, MENU_BACKGROUND_HEIGHT);
    MENU_ICON_HEIGHT = MENU_ICON_SIZE;
    MENU_ICON_WIDTH = MENU_ICON_SIZE;
    uint32_t scratch = menu_fw_alloc(MENU_BMP_SCRATCH_BYTES, MENU_BUFFER_ALIGNMENT);
    if (!scratch) {
        return 0;
    }
    for (uint32_t index = 0; index < MENU_ITEM_COUNT; ++index) {
        MENU_ICON_BUFFER(index) = menu_fw_alloc(
            (uint32_t)MENU_ICON_WIDTH * MENU_ICON_HEIGHT * 3 / 2, MENU_BUFFER_ALIGNMENT);
        if (!MENU_ICON_BUFFER(index)) {
            /* Stock behavior: return success, leaving the scratch allocation
             * leaked. Exit later frees only the background/known icon buffers.
             * Retained for reconstruction fidelity, not endorsed as safe code. */
            return 0;
        }
        (void)menu_fw_decode_icon(
            icon_resources[index], PTR(scratch), PTR(MENU_ICON_BUFFER(index)),
            PTR(MENU_ICON_BUFFER(index) + (uint32_t)MENU_ICON_HEIGHT * MENU_ICON_WIDTH),
            MENU_ICON_HEIGHT, MENU_ICON_WIDTH);
    }
    menu_fw_free(PTR(scratch));
    main_menu_render(MENU_BLINK_PHASE);
    MENU_BLINK_TIMESTAMP = menu_fw_time();
    main_menu_set_labels_visible(root, 1);
    return 0;
}

/* Firmware entry: 0x02013698. Tick units have not been measured here. */
uint32_t main_menu_blink(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    (void)event;
    (void)flags;
    if (MENU_BLINK_TIMESTAMP + MENU_BLINK_INTERVAL_TICKS <= menu_fw_time()) {
        MENU_BLINK_PHASE = MENU_BLINK_PHASE == 0;
        main_menu_render(MENU_BLINK_PHASE);
        MENU_BLINK_TIMESTAMP = menu_fw_time();
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Navigation: flags=0 under event=1; arrows operate on a six-item index.
 * ------------------------------------------------------------------------- */

static void repaint_after_navigation(void)
{
    MENU_BLINK_PHASE = 0;
    main_menu_render(0);
    MENU_BLINK_TIMESTAMP = menu_fw_time();
}

/* Firmware entry: 0x02013714. RIGHT advances, wrapping index 5 -> 0. */
uint32_t main_menu_right(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    if (event == 1 && *flags == 0) {
        if (MENU_SELECTED < 5) {
            ++MENU_SELECTED;
        } else {
            MENU_SELECTED = 0;
        }
        repaint_after_navigation();
    }
    return 0;
}

/* Firmware entry: 0x0201379c. LEFT reverses, wrapping index 0 -> 5. */
uint32_t main_menu_left(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    if (event == 1 && *flags == 0) {
        if (MENU_SELECTED == 0) {
            MENU_SELECTED = 5;
        } else {
            --MENU_SELECTED;
        }
        repaint_after_navigation();
    }
    return 0;
}

/* Firmware entry: 0x02013828. DOWN swaps the rows in the same column. */
uint32_t main_menu_down(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    if (event == 1 && *flags == 0) {
        MENU_SELECTED = MENU_SELECTED + (MENU_SELECTED < 3 ? 3 : -3);
        repaint_after_navigation();
    }
    return 0;
}

/* Firmware entry: 0x020138b0. UP performs the same row swap as DOWN. */
uint32_t main_menu_up(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    (void)root;
    if (event == 1 && *flags == 0) {
        MENU_SELECTED = MENU_SELECTED + (MENU_SELECTED < 3 ? 3 : -3);
        repaint_after_navigation();
    }
    return 0;
}

/* Firmware entry: 0x02013938. Captions disappear before requesting a new mode. */
uint32_t main_menu_ok(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    if (event != 1 || *flags != 0) {
        return 0;
    }
    main_menu_set_labels_visible(root, 0);
    menu_fw_present(1);
    uint32_t mode;
    if (MENU_SELECTED == 0) {
        mode = 3; /* Camera. */
    } else if (MENU_SELECTED == 1) {
        mode = 2; /* Video recording. */
    } else if (MENU_SELECTED == 2) {
        menu_fw_mode_request(5, 0); /* Audio player; extra transition state. */
        if (MENU_TRANSITION_STATE == 0) {
            MENU_TRANSITION_STATE = 1;
            MENU_TRANSITION_PHASE = 0;
        }
        return 0;
    } else if (MENU_SELECTED == 3) {
        mode = 4; /* Playback. */
    } else if (MENU_SELECTED == 4) {
        mode = 7; /* Game menu. */
    } else if (MENU_SELECTED == 5) {
        mode = 9; /* Settings. */
    } else {
        return 0;
    }
    menu_fw_mode_request(mode, 0);
    return 0;
}

/* Firmware entry: 0x02013a14. Message 0x1f requests playback directly. */
uint32_t main_menu_playback_shortcut(MenuWindow root, uint32_t event, const uint32_t *flags)
{
    if (event == 1 && *flags == 0) {
        main_menu_set_labels_visible(root, 0);
        menu_fw_present(1);
        menu_fw_mode_request(4, 0);
    }
    return 0;
}

/* Writable widget/descriptor data; the framework installs this message map. */
#include "main_menu_widgets.inc"
const MenuBinding menu_bindings[12] = {
    {MENU_MSG_ENTER, main_menu_window_enter},
    {MENU_MSG_EXIT, main_menu_window_exit},
    {MENU_MSG_UP, main_menu_up},
    {MENU_MSG_DOWN, main_menu_down},
    {MENU_MSG_LEFT, main_menu_left},
    {MENU_MSG_RIGHT, main_menu_right},
    {MENU_MSG_PLAYBACK_SHORTCUT, main_menu_playback_shortcut},
    {MENU_MSG_OK, main_menu_ok},
    {MENU_MSG_ALTERNATE_OK, main_menu_ok},
    {MENU_MSG_BLINK, main_menu_blink},
    {MENU_MSG_NOOP, main_menu_noop},
    {MENU_MSG_END, 0},
};
MenuDescriptor menu_descriptor = {menu_bindings, menu_widgets, 0, 0, {0, 0, 0}};
