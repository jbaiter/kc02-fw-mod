# KC02 firmware documentation map

Canonical image: `firmware/original.bin`,
`sha256 218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`.
Address mapping: `CPU = file_offset + 0x01FFDA00` for the application, with a different origin for
the boot section ([BOOT_MAP.md](BOOT_MAP.md)).

**Read [DOC_AUDIT.md](DOC_AUDIT.md) for what is current, what is superseded, and what is still
unresolved.** Items below marked *historical* are kept as a record and contain conclusions that
later work replaced.

## Start here

| Document | What it covers |
|---|---|
| [../README.md](../README.md) | Project overview: platform, toolchain, flashing, recovery, and the retraction of the original JieLi/pi32 hypothesis |
| [BOARD_RE.md](BOARD_RE.md) | Board reverse engineering: SoC, pin map, keypad, printer driver, sensor, battery |
| [FIRMWARE_MAP.md](FIRMWARE_MAP.md) | Firmware layout, subsystem map, data tables |
| [NEXT_STEPS.md](NEXT_STEPS.md) | Dated running log of what was done and what is next; numbered sections are the project history |
| [DOC_AUDIT.md](DOC_AUDIT.md) | Documentation audit: fixes applied, open disagreements, coverage table |
| [NAMING_RECONCILIATION.md](NAMING_RECONCILIATION.md) | One canonical name per address, with aliases and the evidence that decides between them |
| [custom_ui/README.md](custom_ui/README.md) | Custom UI routing patch: boot to camera, two-tap sequences, POWER returns to camera; builds a custom image |
| [HARDWARE_OBSERVATION.md](HARDWARE_OBSERVATION.md) | Method and results of read-only observation on real hardware, including the region-table correction |
| [NEW_BUILDS.md](NEW_BUILDS.md) | The two newer units (MYSTILUCK X8D, HMOLLIMT Z-12): mapping, asset tables, what is still unmapped |
| [BLACKBOX_DUMP.md](BLACKBOX_DUMP.md) | Dumping a unit whose firmware addresses are unknown, via a firmware-independent code stage |

## Structure and subsystems

| Document | What it covers |
|---|---|
| [BOOT_MAP.md](BOOT_MAP.md) | Boot section vs application, addresses and entry points |
| [APP_MAP.md](APP_MAP.md) | Application entry points and call paths |
| [UI_STATE_MACHINE.md](UI_STATE_MACHINE.md) | Modes, transitions, key dispatch, screen dispatch |
| [CAMERA_SCREEN.md](CAMERA_SCREEN.md) | Camera screen: 33 entries, tables, rebuilt module |
| [MAIN_MENU_SCREEN.md](MAIN_MENU_SCREEN.md) | Main menu: 15 entries, widget table, rebuilt module |
| [PRINT_PIPELINE.md](PRINT_PIPELINE.md) | Capture and print pipeline, and §3b: the head's 32-level tone ramp |
| [SDK_MAP.md](SDK_MAP.md) | How much of the image is identified from the related vendor SDK |
| [STRUCTS.md](STRUCTS.md) | Structures recovered from the SDK's debug info |
| [report_protos.md](report_protos.md) | Prototype table and its provenance |
| [NAMING.md](NAMING.md) | Naming method and quality tiers |
| [RAM_MAP_MEASURED.md](RAM_MAP_MEASURED.md) | Measured RAM map, including the free window used for RAM payloads |
| [FIRMWARE_NOTES.md](FIRMWARE_NOTES.md) | *Historical.* Early log; **partly superseded** - platform and Ghidra setup are wrong, see its banner |

## Hardware tooling and print work

| Path | What it is |
|---|---|
| [hw/README.md](hw/README.md) | How the hardware scripts fit together |
| [hw/observe_state.py](hw/observe_state.py) | Read-only state observer; region table is evidence-based |
| [hw/scan_ram_access.py](hw/scan_ram_access.py) | Derives global addresses and widths from the listing |
| [hw/execute_payload.py](hw/execute_payload.py) | Guarded RAM payload executor |
| [hw/post_repair/README.md](hw/post_repair/README.md) | Post-repair print results, per-dot grayscale, orientation, mazes, checkerboards, tone cards |
| [hw/print_test/README.md](hw/print_test/README.md) | The printed 384-column test pattern and how to read it |
| [toolchain/](../toolchain/) | Reconstructed GCC 4.9.1 no-delay OpenRISC toolchain |
| [TOOLCHAIN_TESTS.md](TOOLCHAIN_TESTS.md) | On-device validation suite for that toolchain |

## Planning

| Document | What it covers |
|---|---|
| [USB_FEATURES.md](USB_FEATURES.md) | WebUSB and USB printer-class feasibility, with the descriptor and BOS requirements |
| [CHILD_MODE.md](CHILD_MODE.md) | Child-lock / child-mode patch and its validation |
| [HARDWARE_SESSION_GUIDE.md](HARDWARE_SESSION_GUIDE.md) | Step-by-step guide for attaching to the camera and dumping |
| [EMULATOR_PLAN.md](EMULATOR_PLAN.md) | Plan for an AX3292 firmware emulator (Stage 3: app boot + display + SPI flash) |

## Historical

These are records of earlier phases. They are not wrong about their own experiments, but their
conclusions may since have been superseded.

| Document | Note |
|---|---|
| [FIRMWARE_NOTES.md](FIRMWARE_NOTES.md) | Pre-correction platform claim; see the banner at the top |
| [HARDWARE_TEST_RESULTS.md](HARDWARE_TEST_RESULTS.md) | Night 1 and night 2 logs; the "refused while charging" item carries an inline correction |
| [HARDWARE_OBSERVATION.md](HARDWARE_OBSERVATION.md) | Sections on the printer's USB-only power limits predate the battery repair and are marked superseded |
| [NEXT_STEPS.md](NEXT_STEPS.md) | Sections 1-20 are history; sections 21-22 are current |
| [ASSET_CATALOG.md](ASSET_CATALOG.md), [IMAGE_GEN_RULES.md](IMAGE_GEN_RULES.md) | Asset-side notes from the earliest phase |
| [TOOLCHAIN_TESTS.md](TOOLCHAIN_TESTS.md) | Toolchain validation record |

## Things to know before trusting a document

* **Hashes.** Early documents quote `md5 7236c4c1…`, which is not the canonical image. Use the
  sha256 above.
* **Addresses.** Application CPU addresses include the `+0x01FFDA00` mapping. Early documents
  sometimes quote raw file offsets without it.
* **Hardware state.** Results recorded before the battery-connector repair were taken on USB-only
  power, which caps the printer at roughly 8 dots per line. Those results are marked where they
  appear.
* **Decompiler labels.** Ghidra labels are hypotheses. `ui_close_all_windows` and `winDestroy` are
  painters, and `isp_csi_enable` is interior to its caller; see [CAMERA_SCREEN.md](CAMERA_SCREEN.md).
