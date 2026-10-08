# Persistent child mode (experimental)

`tools/patch_child_mode.py` builds a 4 MiB `DestBin.bin` for the stock SD updater.
It does not connect to the camera or flash it. **This persistent implementation
has passed offline checks, not a hardware installation test.** Keep your working
backup and an external SPI recovery method before the first installation.

## Behaviour

- Without an active USB storage connection, boot into the camera (mode 3), not
  the main menu (mode 6).
- In camera mode, ignore an ordinary POWER/MENU press.
- Tap UP, then POWER to open the main menu. Consume the UP arm on that accepted
  POWER press, so returning to camera mode locks the menu again.
- Preserve the stock UP action, other camera controls, global shutdown/idle
  handling, and stock USB/SD mode. The patch does not unlock the USB screen.

This follows the researched RAM prototype's one-shot sequence. **There is no
sequence timeout, and unrelated buttons do not cancel the arm.** UP followed by
POWER much later still opens the menu. This is a convenience guard, not a secure
parental lock or a restriction on every possible UI transition.

The gate also retains a runtime adult-override byte, initially zero. This tool
has no USB setter for it. The old research tool's `adult-on` address is different
and must not be used with this implementation.

## Build and check

Only the researched boot/application layout is accepted. The tool checks full
SHA-256 hashes after normalizing its own patch, rather than relying on a few
routine prologues. Asset-only themes outside the loaded application are allowed;
other code or initialized-data changes are not. There is no safety override.

Start with your camera's verified, working `firmware/original.bin`:

```sh
# Review compatibility, patch spans and output hash without creating a file.
python3 tools/patch_child_mode.py --dry-run

# First-stage rehearsal: change only the boot-mode immediate (one byte).
python3 tools/patch_child_mode.py --boot-only
# Output: patched/child_mode/boot_only/DestBin.bin

# Full child mode.
python3 tools/patch_child_mode.py
# Output: patched/child_mode/DestBin.bin

# Show the generated assembly.
python3 tools/patch_child_mode.py --listing
```

For a theme image, choose it explicitly. This retains its assets:

```sh
python3 tools/patch_child_mode.py -i patched/DestBin.bin --dry-run
python3 tools/patch_child_mode.py -i patched/DestBin.bin -o patched/themed_child_mode/DestBin.bin
```

The output size stays 4,194,304 bytes. Existing output files are protected unless
`--force` is supplied; the input cannot be its own output. The tool recognizes
stock, its deliberate boot-only stage, and its complete patch. It rejects other
partial/mixed states. Applying the same stage twice is idempotent.

## First installation and rollback

A theme-only update does not prove that this code patch works. First test the
boot-only image with stock assets, then consider the full image. Both are
experimental. Do not proceed without accepting the possibility of SPI recovery.

Use the existing SD update process:

1. Keep the camera off while preparing a FAT32 SD card. Copy the chosen image to
   the card root as `DestBin.bin`, then eject/unmount the card.
2. Insert the card, use a charged battery, disconnect the USB data cable, and
   power on. Do not interrupt the update or remove the card.
3. After the update finishes and the camera powers down, remove `DestBin.bin`
   from the card before the next boot to avoid another update attempt.
4. For the boot-only stage, verify that the camera starts in camera mode and
   that ordinary POWER/MENU still opens the menu. For the full patch, verify
   POWER alone is blocked and UP followed by POWER opens the menu.
5. Check normal camera/UP actions and shutdown, then confirm the same behaviour
   after a complete power cycle. Stock USB mode and firmware dumping should
   remain available; inspect a fresh dump rather than assuming a successful
   update display establishes correct programming.

The builder leaves the bootloader and stock SD updater unchanged. The updater
runs before the initial-mode selection, so child mode does not deliberately
block the normal update path. This does not guarantee recovery from a bad flash.

To remove the patch while retaining the image's theme and state record:

```sh
python3 tools/patch_child_mode.py --restore -i patched/child_mode/DestBin.bin
# Output: patched/child_mode/restore/DestBin.bin
```

Install that image through the same SD procedure if the camera still boots. If
it cannot run the SD updater, restore the working full backup with the external
SPI programmer. Power-cycling alone does not remove a persistent patch.

## Design and ownership

The app mapping is `file_offset = CPU_address - 0x02000000 + 0x2600`.
The patch has three allowed write spans, totalling 108 bytes:

| File offset | CPU address | Length | Replacement |
|---|---|---:|---|
| `0x002AAC` | `0x020004AC` | 4 | Initial non-USB mode: 6 -> 3 |
| `0x00BE90` | `0x02009890` | 4 | UP-handler entry jumps to its arm trampoline |
| `0x00C508` | `0x02009F08` | 100 | Replace the entire POWER handler |

The 100-byte footprint contains:

- `0x02009F08..0x02009F5B`: POWER gate and stock-equivalent mode-request logic.
- `0x02009F5C..0x02009F67`: UP trampoline, including the displaced stack save.
- `0x02009F68..0x02009F6B`: initially zero control word; byte 0 is the arm and
  byte 1 the adult override. Both occupy the loaded application in RAM, not
  heap/BSS scratch. Runtime stores do not change their source bytes in flash.

The layout does not extend the application or occupy an assumed padding area.
In particular, it leaves the print-style LUT at file `0x07EC40` untouched. The
old persistent recipe incorrectly proposed that LUT as free stub space. It also
leaves the dump buffer and the `0x020D9xxx` experimental staging regions alone.

### Call/return discipline

The stock primary key dispatcher calls through `r14` at `0x02007A98`, with
`r4=1` and `r9=0x02007A9C` on entry. The hook list binds UP (`0x1B`) to
`0x02009890` and POWER (`0x24`) to `0x02009F08`. The full application hash guards
these assumptions. A static scan of the reference text found no external direct
branches into the POWER footprint; the only aligned pointers to the handler
entries were their key-hook bindings.

The UP trampoline stores the dispatch flag as the arm, replays the displaced
`l.sw -12(r1),r18`, and jumps to `0x02009894`. It does not change the link,
arguments, stack pointer, or the rest of the UP handler.

The leaf POWER gate retains the stock flag and payload checks. A halfword load
checks `arm | adult_override<<8`; a passing request clears only the arm byte.
It uses caller-scratch registers `r6/r7`, preserves the stack and callee-saved
registers, sets the handler return value `r11` to zero, and tail-jumps to the
unchanged `ui_mode_request(6, 0)`. That routine does not modify `r11` or the camera
state bytes at `0x020D41D4/5`. Moving those stock byte updates before the tail
call gives the same final state without needing the original stack frame.

All instructions are four-byte little-endian OpenRISC-compatible instructions,
with **no delay slots**. `--listing` includes all instruction addresses, encoded
bytes and the final data word. Do not use the legacy pi32 decoder for this patch.

## Offline validation and remaining risks

Development checks include:

- Ghidra 12.1.4 with `KC02:LE:32:nodelay`, in a separate analysis-project copy:
  the new gate, branch targets, UP trampoline and boot selection decode as
  intended. The UP decompilation retains its stock body after the added arm.
- An independent instruction execution model in `tests/test_patch_child_mode.py`:
  locked/armed/adult paths, payload filtering, one-shot consumption, actual stock
  `ui_mode_request` instructions, stock-equivalent camera/mode side effects,
  register/stack preservation and replay of the displaced UP instruction.
- Image apply/restore round trips; boot-only upgrade/restore; exact three-span
  diff bounds; header validation; unknown-layout and partial-patch rejection;
  preservation of the LUT, assets, state record and tail; no-clobber and
  interrupted-output cleanup.

```sh
python3 -m unittest discover -s tests -v
```

The execution model covers the relevant instruction subset, not the complete
SoC, interrupts, caches or peripheral behaviour. Earlier hardware tests proved
the research RAM child-lock prototype, not this in-place persistent layout.
Hardware testing must still establish RAM mutability of the embedded control
word, instruction/data cache behaviour, UI behaviour and reliable cold boot.
An SD update can erase much more than the 108-byte logical patch. A power loss
or incorrect image can still require external SPI recovery.
