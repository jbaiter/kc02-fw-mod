/* KC02 custom UI routing patch - image-side implementation.
 *
 * Behaviour:
 *   - boot enters the camera screen (mode 3) instead of the main menu
 *   - UP, then POWER within 1000 ticks   -> settings      (mode 9)
 *   - DOWN, then POWER within 1000 ticks -> photo viewer  (mode 4)
 *   - POWER inside settings or the viewer -> back to the camera screen (mode 3)
 *
 * Sequence, not chord. The panel is an ADC resistor ladder on one analogue pin and resolves one key
 * at a time, so two simultaneous presses cannot be told apart. The second key must arrive within
 * ARM_WINDOW_TICKS or the arm expires and that POWER press is ignored.
 *
 * The stock UI loop, scheduler, dispatcher, preview pipeline and drivers are all kept. This does not
 * replace the mode machine; it routes between stock modes, so USB/dump and the other modes behave as
 * they did.
 *
 * Space: nothing is added to the image. Every piece is built into a slot that already exists, where
 * the stock handler it replaces lived, so the patch stays strictly in place - one piece per slot:
 *
 *   0x02009F08  100 B  (.slot_power)     camera POWER handler    -> camera_power_router
 *   0x02016C14  100 B  (.slot_settings)  settings POWER handler  -> arming block
 *   0x0200BD4C  100 B  (.slot_playback)  playback POWER handler  -> power_to_camera
 *
 * The zero-filled runs elsewhere in the image were deliberately not used. The largest, 5672 bytes at
 * 0x0207C640, is the print-style LUT that CHILD_MODE.md already records as a trap; the rest are
 * unverified and could be zero-at-build-time data tables.
 *
 * Timing reads the cached tick at 0x020CCDE8, which system_tick refreshes every ~10 ms, rather than
 * calling XOSTimeGet. That removes a call and a stack frame from both the router and the arming
 * block, which is what lets everything fit the existing slots. Window accuracy is ~10 ms.
 *
 * The route back to the camera does not reproduce the stock handlers' transition arming at
 * app_context+0x68/+0x69, so that mode change happens without the stock transition animation. The
 * mode switch itself is unchanged.
 */
#include <stdint.h>

typedef uint32_t u32;
typedef uint8_t u8;

#define UI_MODE_REQUEST 0x02008108u   /* ui_mode_request(mode, arg) */
#define TICK_CACHE 0x020CCDE8u        /* XOSTimeGet() value, refreshed by system_tick every ~10 ms */

#define ARM_BLOCK 0x020D9480u         /* verified-free RAM (child-mode patch uses the same page) */
#define ARM_STATE (ARM_BLOCK + 0u)
#define ARM_DEADLINE (ARM_BLOCK + 4u)

#define MODE_CAMERA 3u
#define MODE_VIEWER 4u
#define MODE_SETTINGS 9u

#define ARM_WINDOW_TICKS 1000u        /* 1 s; 1 tick is 1 ms on this firmware */

typedef void (*mode_request_t)(u32 mode, u32 argument);

#define SLOT_SETTINGS __attribute__((section(".slot_settings"), used, noinline))
#define SLOT_PLAYBACK __attribute__((section(".slot_playback"), used, noinline))

/* --- 0x02009F08: the camera POWER router ------------------------------------------------------ */

/* The reference logic, which is what the assembly below implements:
 *
 *     if (arm_state != 0) {                 // something is armed
 *         mode = arm_state;                 // the arming block stored the target mode
 *         arm_state = 0;                    // one shot either way
 *         now = tick_cache;
 *         if ((int32_t)(now - deadline) < 0) {   // still inside the 1 s window
 *             ui_mode_request(mode, 0);
 *         }
 *     }
 *
 * It is assembled rather than compiled because of the 100-byte slot: the C form passes the byte
 * budget (136 B) once the call to ui_mode_request needs a stack frame, and a frame here would also
 * clobber r2 and r9 for no reason. The hand-over is a jump through a register instead of a call, so
 * r9 still holds the address the dispatcher gave the handler and ui_mode_request's own `l.jr r9`
 * returns straight to the dispatcher.
 *
 * The press/release flag is not tested. The arm is cleared on the first call, so a release event
 * that arrives after the press cannot act a second time, and a release with nothing armed does
 * nothing anyway.
 *
 * r9 and r2 are left untouched; r6-r11 are the scratch registers used.
 */
__asm__(
    ".section .slot_power,\"ax\",@progbits\n"
    ".globl camera_power_router\n"
    ".type camera_power_router,@function\n"
    ".balign 4\n"
    "camera_power_router:\n"
    "    l.movhi r4,0x020d\n"
    "    l.ori   r4,r4,0x9480\n"          /* &arm block */
    "    l.lbz   r6,0(r4)\n"              /* mode = arm_state */
    "    l.sfeqi r6,0\n"
    "    l.bf    camera_power_router_ret\n"
    "    l.sb    0(r4),r0\n"              /* clear the arm: one shot */
    "    l.movhi r7,0x020c\n"
    "    l.ori   r7,r7,0xcde8\n"          /* &cached tick */
    "    l.lwz   r8,0(r7)\n"              /* now */
    "    l.lwz   r11,4(r4)\n"             /* deadline */
    "    l.sub   r8,r8,r11\n"
    "    l.sflts r8,r0\n"                 /* signed: negative means inside the window */
    "    l.bnf   camera_power_router_ret\n"
    "    l.nop\n"
    "    l.ori   r3,r6,0\n"               /* mode */
    "    l.addi  r4,r0,0\n"               /* argument */
    "    l.movhi r5,0x0200\n"
    "    l.ori   r5,r5,0x8108\n"          /* ui_mode_request */
    "    l.jr    r5\n"                    /* hand over, keeping r9 */
    "    l.nop\n"
    "camera_power_router_ret:\n"
    "    l.jr    r9\n"
    "    l.nop\n"
    ".size camera_power_router,.-camera_power_router\n"
);

/* --- 0x0200BD4C: POWER that returns to the camera screen -------------------------------------- */

/* Used for both the settings and the playback POWER entry, by pointing both keymap entries here. */
SLOT_PLAYBACK
void power_to_camera(void *ctx, u32 event, u32 *flags)
{
    (void)ctx;
    if (event == 1u && *flags != 0u) {
        ((mode_request_t)UI_MODE_REQUEST)(MODE_CAMERA, 0u);
    }
}

/* --- 0x02016C14: the arming block ------------------------------------------------------------- */

/* Two entry points, each of which names its target mode and the stock handler to hand over to. The
 * keymap entries for camera messages 0x1B (UP) and 0x1C (DOWN) point at camera_arm_up and
 * camera_arm_down respectively.
 *
 * The hand-over is a jump through a register, not a call: r9 still holds the return address the
 * dispatcher gave the handler, so the stock handler's own `l.jr r9` returns straight to the
 * dispatcher. That is why no stack frame and no instruction replay are needed, and why r9 must not
 * be touched here. r6-r12 are scratch across calls in this firmware; r3/r4/r5 carry the arguments
 * and are left alone.
 */
extern void camera_arm_up(void *ctx, u32 event, u32 *flags);
extern void camera_arm_down(void *ctx, u32 event, u32 *flags);

__asm__(
    ".section .slot_settings,\"ax\",@progbits\n"
    ".globl camera_arm_up\n"
    ".globl camera_arm_down\n"
    ".type camera_arm_up,@function\n"
    ".type camera_arm_down,@function\n"
    ".balign 4\n"
    "camera_arm_up:\n"
    "    l.movhi r6,0x0200\n"
    "    l.ori   r6,r6,0x9890\n"                 /* stock camera UP  handler */
    "    l.addi  r7,r0,9\n"                      /* target mode: settings */
    "    l.j     camera_arm_common\n"
    "    l.nop\n"
    ".balign 4\n"
    "camera_arm_down:\n"
    "    l.movhi r6,0x0200\n"
    "    l.ori   r6,r6,0x9568\n"                 /* stock camera DOWN handler */
    "    l.addi  r7,r0,4\n"                      /* target mode: photo viewer */
    "    l.j     camera_arm_common\n"
    "    l.nop\n"
    ".balign 4\n"
    "camera_arm_common:\n"
    "    l.movhi r8,0x020d\n"
    "    l.ori   r8,r8,0x9480\n"                 /* &arm block */
    "    l.sb    0(r8),r7\n"                     /* arm_state = target mode */
    "    l.movhi r10,0x020c\n"
    "    l.ori   r10,r10,0xcde8\n"               /* &cached tick */
    "    l.lwz   r11,0(r10)\n"
    "    l.addi  r11,r11,1000\n"
    "    l.sw    4(r8),r11\n"                    /* deadline = now + 1 s */
    "    l.jr    r6\n"                           /* hand over to the stock handler */
    "    l.nop\n"
    ".size camera_arm_up,.-camera_arm_up\n"
    ".size camera_arm_down,.-camera_arm_down\n"
);
