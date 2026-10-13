/* Host-only controller tests. No stock driver/peripheral code executes. */
#include "main_menu_screen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define IMPORT(ret, name, args, address) extern ret name args;
#include "firmware_imports.def"
#undef IMPORT

static unsigned char memory[0x400000];
static uint32_t frame[12];
static uint32_t clock_value, selected_mode, render_count, blit_count, labels[6];
static uint32_t allocation_calls, fail_allocation, active_allocations, next_address;
static uint32_t allocations[32];
static uint32_t last_x, last_y, last_resource, assertions;
static int frame_available;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        assert(condition);                                                                         \
        ++assertions;                                                                              \
    } while (0)

void *menu_test_memory(uint32_t address, size_t size)
{
    if (!address)
        return 0;
    assert(address >= 0x02000000 && address <= 0x02400000);
    assert(size <= 0x02400000 - address);
    return memory + address - 0x02000000;
}
static void reset(void)
{
    memset(memory, 0, sizeof(memory));
    memset(frame, 0, sizeof(frame));
    memset(labels, 0, sizeof(labels));
    memset(allocations, 0, sizeof(allocations));
    clock_value = 1234;
    selected_mode = 0;
    render_count = 0;
    blit_count = 0;
    allocation_calls = 0;
    fail_allocation = 0;
    active_allocations = 0;
    next_address = 0x02200000;
    frame_available = 0;
    frame[1] = 0x02180000;
}
void menu_fw_csi_enable(uint32_t enabled)
{
    CHECK(enabled == 0);
}
void menu_fw_ui_state(uint32_t state)
{
    CHECK(state == 7);
}
MenuWindow menu_fw_window_open(MenuDescriptor *descriptor, uint32_t argc, ...)
{
    CHECK(descriptor == &menu_descriptor && argc == 0);
    descriptor->handle = 0x1001;
    (void)main_menu_window_enter(0x1001, 0, 0);
    return descriptor->handle;
}
void *menu_fw_frame_get(uint32_t layer)
{
    assert(layer == 0);
    return frame_available ? frame : 0;
}
int32_t menu_fw_lcd_resolution(uint16_t *width, uint16_t *height)
{
    *width = 320;
    *height = 240;
    return 0;
}
void menu_fw_frame_config(void *f, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    CHECK(f == frame && x == 0 && y == 0 && width == 320 && height == 240);
    uint16_t *dimensions = (uint16_t *)((unsigned char *)f + 0x20);
    dimensions[0] = width;
    dimensions[1] = height;
}
void *menu_fw_copy(void *destination, const void *source, uint32_t length)
{
    CHECK(length == 115200);
    return memcpy(destination, source, length);
}
void menu_fw_blit(void *destination, uint32_t destination_width, uint32_t destination_height,
                  int32_t x, int32_t y, uint32_t clip_width, uint32_t clip_height, void *source,
                  uint32_t source_width, uint32_t source_height, uint32_t key_y, uint32_t key_uv)
{
    CHECK(destination == menu_test_memory(0x02180000, 1));
    CHECK(destination_width == 320 && destination_height == 240);
    CHECK(clip_width == 96 && clip_height == 96 && source_width == 96 && source_height == 96);
    CHECK(source == menu_test_memory(MENU_ICON_BUFFER(MENU_SELECTED), 1));
    CHECK(key_y == 0x8a && key_uv == 0x81);
    last_x = x;
    last_y = y;
    ++blit_count;
}
void menu_fw_cache_flush(void *buffer, uint32_t length)
{
    CHECK(buffer == menu_test_memory(0x02180000, 1) && length == 115200);
}
int32_t menu_fw_frame_show(uint32_t layer, void *f, uint32_t width, uint32_t height,
                           uint32_t rotation)
{
    CHECK(layer == 0 && f == frame && width == 0 && height == 0 && rotation == 0xff);
    ++render_count;
    return 0;
}
MenuWindow menu_fw_child(MenuWindow root, uint32_t id)
{
    CHECK(root == 0x1001 && id < 6);
    return id + 1;
}
void menu_fw_visible(MenuWindow widget, uint32_t visible)
{
    CHECK(widget >= 1 && widget <= 6);
    labels[widget - 1] = visible;
}
void menu_fw_free(void *buffer)
{
    uint32_t address = (uint32_t)((unsigned char *)buffer - memory) + 0x02000000;
    for (uint32_t index = 0; index < 32; ++index) {
        if (allocations[index] == address) {
            allocations[index] = 0;
            --active_allocations;
            return;
        }
    }
    assert(!"free of an unknown allocation");
}
uint32_t menu_fw_alloc(uint32_t length, uint32_t alignment)
{
    ++allocation_calls;
    CHECK(alignment == 32);
    if (allocation_calls == fail_allocation)
        return 0;
    uint32_t address = next_address;
    next_address += (length + 31) & ~31U;
    assert(next_address < 0x02400000);
    for (uint32_t index = 0; index < 32; ++index) {
        if (!allocations[index]) {
            allocations[index] = address;
            ++active_allocations;
            return address;
        }
    }
    assert(!"allocation slots exhausted");
    return 0;
}
int32_t menu_fw_decode_background(uint32_t resource, void *y, void *uv, uint32_t width,
                                  uint32_t height)
{
    CHECK(resource == 0x20 && width == 320 && height == 240);
    CHECK((unsigned char *)uv - (unsigned char *)y == 320 * 240);
    memset(y, 0x11, 115200);
    return 0;
}
uint32_t menu_fw_decode_icon(uint32_t resource, void *scratch, void *y, void *uv, uint32_t width,
                             uint32_t height)
{
    CHECK(scratch != 0 && width == 96 && height == 96);
    CHECK((unsigned char *)uv - (unsigned char *)y == 96 * 96);
    last_resource = resource;
    memset(y, resource, 13824);
    return 0;
}
uint32_t menu_fw_time(void)
{
    return clock_value;
}
void menu_fw_present(uint32_t force)
{
    CHECK(force == 1);
}
void menu_fw_mode_request(uint32_t mode, uint32_t argument)
{
    CHECK(argument == 0);
    selected_mode = mode;
}

int main(void)
{
    MenuHandler arrows[] = {main_menu_right, main_menu_left, main_menu_down, main_menu_up};
    for (uint32_t key = 0; key < 4; ++key) {
        for (uint32_t selected = 0; selected < 6; ++selected) {
            for (uint32_t event = 0; event < 2; ++event) {
                for (uint32_t flags = 0; flags < 3; ++flags) {
                    reset();
                    MENU_SELECTED = selected;
                    MENU_BLINK_PHASE = 1;
                    MENU_BLINK_TIMESTAMP = 100;
                    (void)arrows[key](0x1001, event, &flags);
                    uint32_t expected = selected;
                    if (event == 1 && flags == 0) {
                        if (key == 0)
                            expected = (selected + 1) % 6;
                        else if (key == 1)
                            expected = (selected + 5) % 6;
                        else
                            expected = (selected + 3) % 6;
                    }
                    CHECK(MENU_SELECTED == expected);
                    CHECK(MENU_BLINK_PHASE == (event == 1 && flags == 0 ? 0 : 1));
                    CHECK(MENU_BLINK_TIMESTAMP == (event == 1 && flags == 0 ? 1234 : 100));
                }
            }
        }
    }
    uint32_t modes[] = {3, 2, 5, 4, 7, 9};
    for (uint32_t selected = 0; selected < 6; ++selected) {
        for (uint32_t transition = 0; transition < 3; ++transition) {
            reset();
            MENU_SELECTED = selected;
            MENU_TRANSITION_STATE = transition;
            MENU_TRANSITION_PHASE = 42;
            uint32_t flags = 0;
            (void)main_menu_ok(0x1001, 1, &flags);
            CHECK(selected_mode == modes[selected]);
            CHECK(MENU_TRANSITION_STATE == (selected == 2 && transition == 0 ? 1 : transition));
            CHECK(MENU_TRANSITION_PHASE == (selected == 2 && transition == 0 ? 0 : 42));
            for (uint32_t id = 0; id < 6; ++id)
                CHECK(labels[id] == 0);
        }
    }
    reset();
    uint32_t flags = 0;
    (void)main_menu_playback_shortcut(0x1001, 1, &flags);
    CHECK(selected_mode == 4);
    reset();
    frame_available = 1;
    MENU_SELECTED = 9;
    main_menu_mode_enter();
    CHECK(MENU_SELECTED == 0 && active_allocations == 7 && allocation_calls == 8);
    CHECK(MENU_BACKGROUND_WIDTH == 320 && MENU_BACKGROUND_HEIGHT == 240);
    CHECK(MENU_ICON_WIDTH == 96 && MENU_ICON_HEIGHT == 96 && last_resource == 0x24);
    CHECK(render_count == 1 && blit_count == 1);
    for (uint32_t id = 0; id < 6; ++id)
        CHECK(labels[id] == 1);
    for (uint32_t selected = 0; selected < 6; ++selected) {
        MENU_SELECTED = selected;
        main_menu_render(0);
        CHECK(last_x == 12 + selected % 3 * 100);
        CHECK(last_y == (selected < 3 ? 18 : 126));
    }
    uint32_t blits_before = blit_count;
    main_menu_render(1);
    CHECK(blit_count == blits_before);
    CHECK(*(unsigned char *)menu_test_memory(0x02180000, 1) == 0x11);
    MENU_BLINK_TIMESTAMP = 100;
    MENU_BLINK_PHASE = 0;
    clock_value = 399;
    (void)main_menu_blink(0x1001, 0, 0);
    CHECK(MENU_BLINK_PHASE == 0);
    clock_value = 400;
    (void)main_menu_blink(0x1001, 0, 0);
    CHECK(MENU_BLINK_PHASE == 1 && MENU_BLINK_TIMESTAMP == 400);
    clock_value = 700;
    (void)main_menu_blink(0x1001, 0, 0);
    CHECK(MENU_BLINK_PHASE == 0 && MENU_BLINK_TIMESTAMP == 700);
    main_menu_mode_tick();
    main_menu_mode_exit();
    CHECK(active_allocations == 7);
    (void)main_menu_window_exit(0x1001, 0, 0);
    CHECK(active_allocations == 0 && MENU_BACKGROUND_BUFFER == 0);
    for (uint32_t id = 0; id < 6; ++id)
        CHECK(MENU_ICON_BUFFER(id) == 0);
    for (uint32_t failure = 1; failure <= 8; ++failure) {
        reset();
        fail_allocation = failure;
        (void)main_menu_window_enter(0x1001, 0, 0);
        (void)main_menu_window_exit(0x1001, 0, 0);
        CHECK(active_allocations == (failure >= 3 ? 1 : 0)); /* Stock scratch leak. */
    }
    printf("PASS: %u host assertions; driver calls are mocked.\n", assertions);
    return 0;
}
