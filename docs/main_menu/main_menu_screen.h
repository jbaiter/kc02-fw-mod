/* KC02 main-menu reconstruction; target layout is specific to original.bin. */
#ifndef KC02_MAIN_MENU_SCREEN_H
#define KC02_MAIN_MENU_SCREEN_H
#include <stddef.h>
#include <stdint.h>

typedef uintptr_t MenuWindow;
typedef uint32_t (*MenuHandler)(MenuWindow root, uint32_t event, const uint32_t *flags);
typedef struct {
    uint32_t message;
    MenuHandler handler;
} MenuBinding;

/* Same verified 48-byte stock creation record used by the camera screen.
 * Padding and the unknown eight-byte extension must stay intact. */
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
} MenuWidget;
typedef struct {
    const MenuBinding *bindings;
    MenuWidget *widgets;
    MenuWindow handle;
    uint8_t repeat_open, reserved[3];
} MenuDescriptor;
_Static_assert(sizeof(MenuWidget) == 48, "stock widget stride");
_Static_assert(offsetof(MenuWidget, extra) == 0x28, "stock widget extension");
#ifndef MENU_HOST_TEST
_Static_assert(sizeof(void *) == 4, "32-bit target required");
_Static_assert(sizeof(MenuBinding) == 8, "stock binding size");
_Static_assert(sizeof(MenuDescriptor) == 16, "stock window descriptor size");
#endif

#ifdef MENU_HOST_TEST
void *menu_test_memory(uint32_t address, size_t size);
#define MENU_PTR(address, size) menu_test_memory((uint32_t)(address), (size))
#else
#define MENU_PTR(address, size) ((void *)(uintptr_t)(address))
#endif
#define MENU_MEM8(address) (*(volatile uint8_t *)MENU_PTR(address, 1))
#define MENU_MEM16(address) (*(volatile uint16_t *)MENU_PTR(address, 2))
#define MENU_MEM32(address) (*(volatile uint32_t *)MENU_PTR(address, 4))

/* Stock globals, shared with the retained framework. Addresses are CPU VMAs.
 * Width/height ordering differs between the icon and background globals. */
#define MENU_SELECTED MENU_MEM8(0x020d41d3)
#define MENU_TRANSITION_STATE MENU_MEM8(0x020d41d4)
#define MENU_TRANSITION_PHASE MENU_MEM8(0x020d41d5)
#define MENU_ICON_WIDTH MENU_MEM16(0x020cceac)
#define MENU_ICON_HEIGHT MENU_MEM16(0x020cceae)
#define MENU_ICON_BUFFER(index) MENU_MEM32(0x020cceb0 + (uint32_t)(index) * 4)
#define MENU_BACKGROUND_HEIGHT MENU_MEM16(0x020ccec8)
#define MENU_BACKGROUND_WIDTH MENU_MEM16(0x020cceca)
#define MENU_BACKGROUND_BUFFER MENU_MEM32(0x020ccecc)
#define MENU_BLINK_PHASE MENU_MEM8(0x020cced0)
#define MENU_BLINK_TIMESTAMP MENU_MEM32(0x020cced4)
#define MENU_ITEM_COUNT 6
#define MENU_BLINK_INTERVAL_TICKS 300U

enum MenuMessage {
    MENU_MSG_BLINK = 0x09,
    MENU_MSG_NOOP = 0x0a,
    MENU_MSG_ENTER = 0x0e,
    MENU_MSG_EXIT = 0x0f,
    MENU_MSG_OK = 0x1a,
    MENU_MSG_UP = 0x1b,
    MENU_MSG_DOWN = 0x1c,
    MENU_MSG_LEFT = 0x1d,
    MENU_MSG_RIGHT = 0x1e,
    MENU_MSG_PLAYBACK_SHORTCUT = 0x1f,
    MENU_MSG_ALTERNATE_OK = 0x23,
    MENU_MSG_END = 0x2e,
};

extern MenuWidget menu_widgets[8]; /* Root, six captions, terminator. */
extern const MenuBinding menu_bindings[12];
extern MenuDescriptor menu_descriptor;
void main_menu_mode_tick(void);
void main_menu_mode_exit(void);
void main_menu_mode_enter(void);
void main_menu_render(uint32_t phase);
void main_menu_set_labels_visible(MenuWindow root, uint32_t visible);
uint32_t main_menu_noop(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_window_exit(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_window_enter(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_blink(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_right(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_left(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_down(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_up(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_ok(MenuWindow root, uint32_t event, const uint32_t *flags);
uint32_t main_menu_playback_shortcut(MenuWindow root, uint32_t event, const uint32_t *flags);
#endif
