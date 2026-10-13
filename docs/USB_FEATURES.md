# USB feature feasibility: WebUSB interface and printer-class interface

Firmware: `firmware/original.bin`, SHA-256
`218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5`.
Everything in "what the firmware has today" is read from the image or the related SDK. The feature
sections are plans, not implemented work.

## What the firmware has today

Three device descriptors are stored contiguously in the image, and the firmware picks one when it
changes mode:

| CPU address | VID:PID | bDeviceClass/Sub/Proto | Mode |
|---|---|---|---|
| `0x0208299C` | `1908:3283` | `EF/02/01` (IAD composite) | storage |
| `0x020829B0` | `1908:3282` | `EF/02/01` (IAD composite) | webcam |
| `0x020829C4` | `0219:3280` | `00/00/00` (per-interface) | vendor / flash tool |

Two configuration blobs sit just before them:

* `0x020827A4`, 470 bytes, 4 interfaces: UVC (interfaces 0-1, IAD) and UAC (interfaces 2-3, IAD),
  500 mA, endpoints `0x82`, `0x84` (x2 alts), `0x83`, all bulk IN.
* `0x0208297C`, 32 bytes, 1 interface: **interface 4, class `08`/`06`/`50`** = Mass Storage,
  SCSI transparent, Bulk-Only Transport, endpoints `0x81` (EP1 IN) and `0x01` (EP1 OUT), both bulk,
  `wMaxPacketSize` 512, 100 mA.

That last blob is the one we have been talking to. The `0xCD` vendor command is a **private SCSI
opcode carried over BOT** on that mass-storage interface, not a vendor-specific USB interface. The
device descriptor for that mode reports `bcdUSB = 0x0110` (USB 1.1) with one configuration.

Hardware and stack facts:

* The USB controller exposes **5 endpoints**: EP0 (64-byte FIFO) plus EP1-EP4 (1024/1024/1024/4096).
* The existing mass-storage configuration already uses **one endpoint number in both directions**
  (EP1 as `0x81` IN and `0x01` OUT), so a bidirectional pair on one endpoint number is proven
  possible on this controller.
* The related AX3292 SDK (`hal/dusb/`) implements MSC, UVC and UAC with a **static descriptor blob
  per composite type**, selected by `usbdsc.devlen`/`cfglen`. Its enumerator defines only device,
  configuration, string, interface, endpoint, device-qualifier and other-speed descriptors.
* **The SDK contains no BOS descriptor support, no WebUSB support and no printer-class support.**
  Neither feature is a matter of enabling an option.
* The image contains no BOS descriptor (`05 0F` appears twice, both coincidental data).

## 1. WebUSB interface

**Doable, and the natural design is a new vendor-specific interface plus a BOS descriptor.** Three
constraints shape it.

### The existing mass-storage interface can never be the WebUSB target

Mass Storage (`0x08`) is on the WebUSB **protected interface class** list, and the browser refuses
to claim protected interfaces. The list in the spec and in Chromium's implementation is:

`0x01` Audio, `0x03` HID, `0x08` Mass Storage, `0x09` Hub, `0x0B` Smart Card, `0x0E` Video,
`0x10` Audio/Video, `0xE0` Wireless Controller.

Independently, the OS mass-storage driver owns that interface on every platform, and WebUSB can only
claim an interface no kernel driver holds. So a browser cannot drive the current `0xCD` channel.
The WebUSB feature needs its **own interface with class `0xFF`** (vendor-specific), which is
claimable and which is exactly what the Chrome guidance recommends for devices that do not
implement a standard class.

Note that Printer (`0x07`) is **not** on the protected list, so a printer interface would be
WebUSB-claimable in principle, subject to no printer driver owning it.

### The BOS descriptor is the device-side work

To be recognised as WebUSB-capable the device must expose a Binary Object Store containing a
platform capability descriptor. The exact shape:

| Field | Value |
|---|---|
| BOS `bLength`, `bDescriptorType`, `wTotalLength` | `0x05`, `0x0F`, `0x001D` for one capability and no landing pages |
| capability `bLength` | `0x18` (24) |
| capability `bDescriptorType` | `0x10` (device capability) |
| capability `bDevCapabilityType` | `0x04` (platform) |
| capability `bReserved` | `0` |
| `PlatformCapabilityUUID` | `{3408b638-09a9-47a0-8bfd-a0768815b665}`, stored **little-endian** as `38 B6 08 34 A9 09 A0 47 8B FD A0 76 88 15 B6 65` |
| `bcdVersion`, `bNumLandingPageURLs` | `0x0100`, `0` (landing pages optional) |

This needs **BOS to be added to the control-endpoint `GET_DESCRIPTOR` dispatch** (there is no code
path for descriptor type `0x0F` today) and **`bcdUSB` raised from `0x0110` to `0x0210`**, because
BOS is a USB 2.1 concept. On Windows, Chrome additionally needs WinUSB bound to the vendor
interface, normally via MS OS descriptors (WCID); on Linux and macOS a driverless vendor interface
is claimable directly.

### The real problem is not USB, it is the trust boundary

Today every safety property lives in the **host** Python harness, not in the firmware: fixed upload
blob addresses, the canary, routine body signatures, a read-only observer mode, whitelisted writes,
the measured free window. The firmware's `0xCD` command itself will perform whatever memory write or
RAM call the host asks for. A browser tool moves that trust boundary onto a web page.

WebUSB does require a user gesture and a permission prompt, and the origin must be HTTPS. That is
not enough on its own. If a WebUSB interface exposes the same primitives, then any page the user
approves could read the entire flash (all their photos) and, worse, write RAM or trigger an update.
A responsible design would be:

* read-only by default, with the read surface limited to declared regions;
* no arbitrary RAM execution exposed at all on the WebUSB path;
* any write requiring an explicit **physical** confirmation on the device (a button held for N
  seconds), not just a browser prompt;
* a versioned protocol so a page cannot silently change semantics.

Backup and inspection from a page are clearly achievable. Flashing from a page is achievable but is
a deliberate security decision, not a free feature.

### Practical notes

* Only Chromium-based browsers implement WebUSB. Firefox and Safari do not, so "from their
  browsers" means Chrome/Edge on desktop.
* The firmware re-enumerates per mode, so the clean shape is a **new mode** that presents its own
  VID:PID with the vendor interface and BOS, leaving the stock three descriptor sets untouched.
* The vendor interface needs a bulk IN/OUT pair. EP1 is taken by mass storage in that configuration;
  EP2, EP3 or EP4 would carry it.

## 2. Printer-class interface

**The descriptor work is easy. The stated goal is not, because the printer has no page description
language and no rasteriser.**

### Descriptor and class side

USB Printer Class (`bInterfaceClass = 0x07`) with two bulk endpoints plus the three class-specific
control requests: `GET_DEVICE_ID` (0), `GET_PORT_STATUS` (1), `SOFT_RESET` (2). No SDK support
exists, so it is new code, but it is well-specified and small.

`GET_DEVICE_ID` matters more than it looks: that string is how an OS or driver decides it can talk
to the device. Advertising something like
`MFG:HiMont;CMD:ESCPOS;MDL:KC02;` lets the device match an existing ESC/POS driver rather than
requiring us to write one. `GET_PORT_STATUS` gives the Paper Empty bit, which we can drive from the
paper sensor we already read (`kc02_sensor_read`, `0 = paper present`).

### Why the goal is harder than the descriptors

The firmware's print path takes a **384-dot wide luma canvas** and drives the head with 32
sub-strobes per line. There is no font engine, no vector or text rendering, no page composition and
no page buffer. Nothing in the firmware turns "a document" into that canvas. So the host must do
the rasterising, and the device must accept a raster stream.

The pragmatic protocol is **ESC/POS raster** (`GS v 0 m xL xH yL yH d1..dk`), the de-facto standard
for thermal printers. It is understood by Linux `usblp` plus thermal drivers, by CUPS filters, by
POS software and by Android apps such as RawBT. Two caveats:

* ESC/POS raster is 1-bit, while our head has genuine per-dot grayscale (about 32 heat levels from
  the strobe ramp). 1-bit input means we dither on the device and give up the grayscale advantage.
  A vendor extension carrying 8-bit luma rows would use the head properly, at the cost of needing a
  custom driver.
* True driverless printing is **IPP-over-USB** (class `07`, subclass `04`, protocol `01`, carrying
  HTTP, IPP and PWG-Raster or URF). That means an HTTP server, an IPP parser, a compressed raster
  decoder and page rasterisation on this MCU. That is a large project and is the only route to
  printing from iOS, whose USB printing goes through IPP.

### Physical reality

* Roll paper, 48 mm printable width, 384 dots at 203 dpi, no page-size handshake, and the user tears
  the strip off.
* The head runs at roughly **11 mm/s** (measured: 1624 feed lines in 18.5 s). A 20 cm page is about
  19 s; a receipt is a few seconds. Fine for receipts and labels, slow for documents.
* A page at 48 x 200 mm is 384 x 1600 dots: 76,800 bytes at 1 bpp, or 614 KB at 8 bpp. The host
  sends far faster than the head consumes, so the firmware must either NAK indefinitely (risking
  host timeouts) or buffer. We know the heap can supply roughly 600 KB, so one grayscale page fits;
  double buffering does not.
* Dense and full-width content needs the battery connected. That is now demonstrated rather than
  suspected, but it is a constraint for unattended printing.

### Realistic outcomes

| Goal | Feasible | What it needs |
|---|---|---|
| Text and receipt printing from Linux with a thermal driver | yes | printer class + ESC/POS raster |
| Printing from an Android app that speaks ESC/POS | yes | same |
| Appearing as a CUPS printer | yes | printer class + a small CUPS filter or PPD |
| Windows print dialog with no driver install | partly | relies on the `GET_DEVICE_ID` string matching an existing driver |
| iOS printing | no, not on this route | IPP-over-USB, a much larger project |
| True driverless from any OS | no | IPP-over-USB |

## Combining both

A composite device carrying mass storage, a vendor interface and a printer interface is
architecturally consistent with what the firmware already does, since it already builds a
4-interface UVC+UAC composite with IADs. Constraints to respect:

* **Endpoint budget.** EP0 plus four usable endpoints. A vendor pair and a printer pair is two more
  endpoint numbers; in a configuration that also keeps mass storage on EP1, that leaves EP4 spare.
* **IADs.** Windows requires Interface Association Descriptors for multi-interface functions; the
  existing blob already uses them.
* **Do not modify the stock three descriptor sets.** Add a new mode/descriptor blob so that stock
  hosts keep seeing what they see today.
* If a printer interface is added, the OS printer driver may claim it, which would then prevent
  WebUSB from claiming that particular interface. Keep the WebUSB channel on the vendor interface.

## What to verify before committing to either

1. Whether the KC02's own USB stack (Buildwin, not literally the SDK's) has a control-endpoint
   `GET_DESCRIPTOR` dispatch that can be extended for BOS, and where its descriptor tables live
   relative to `0x020827A4`.
2. Whether EP2/EP3/EP4 can each be used bidirectionally and independently; EP1 proves the mechanism
   in principle but the FIFO sizes and interrupt handlers still need reading.
3. How much room is left in the image for two new functions and their descriptor blobs, and whether
   another mode can be added without disturbing the mode table.
4. On real hosts: that the printer interface binds where expected, and that the vendor interface is
   claimable by the browser on each OS.

## The blocker that applies to both

Neither feature can be tested until there is a **verified updater and recovery path**. Both require
replacing the USB stack and descriptor set, and a malformed descriptor set makes the device
un-enumerable, which on this hardware means no USB path back in. That is precisely the risk the
current no-flash policy exists to avoid. Sequence accordingly: stable power, verified SD-based
update and recovery, then USB changes in RAM first where possible, and only then persistent
flashing.
