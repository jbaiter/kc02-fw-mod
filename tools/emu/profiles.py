"""Known dump boot handoffs; no application RAM or instruction patches."""
from dataclasses import dataclass
import hashlib
from pathlib import Path


@dataclass(frozen=True)
class Profile:
    name: str
    sha256: str
    app_offset: int
    app_end: int
    entry: int
    stack_top: int
    vbg_address: int
    panel_rotation: int = 90
    key_channel: int = 3
    sensor_3a: bool = False
    key_adc: tuple[int, ...] = (657, 510, 252, 776, 386, 12)
    extra_keys: tuple[int, ...] = (386, 1024, 1024, 1024, 1024)
    dual_sensor: bool = False
    flash_id: str = "ef4016"
    flash_uid: str = "0000000000000000"
    printer_phases: tuple[int, ...] = (10, 8, 9, 1, 5, 4, 6, 2)

    def options(self) -> dict[str, str]:
        """Board wiring and boot handoff, as QEMU machine properties.

        Values are colon separated because a comma would end the -M option.
        """
        return {
            "entry": hex(self.entry),
            "stack-top": hex(self.stack_top),
            "vbg-addr": hex(self.vbg_address),
            "panel-rotation": str(self.panel_rotation),
            "key-channel": str(self.key_channel),
            "key-values": ":".join(map(str, self.key_adc)),
            "extra-keys": ":".join(map(str, self.extra_keys)),
            "sensor-3a": "on" if self.sensor_3a else "off",
            "dual-sensor": "on" if self.dual_sensor else "off",
            "flash-id": self.flash_id,
            "flash-uid": self.flash_uid,
            "printer-phases": ":".join(map(str, self.printer_phases)),
        }


PROFILES = {
    "original": Profile("original", "218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5",
                        0x2600, 0xd3200, 0x0200046c, 0x027ffffc, 0x222c),
    "x8d": Profile("x8d", "3ed8c0f145708814397cf675101ee3ff6b8e280c4b09742af2e043c4cc1bc42e",
                   0x2200, 0x7f000, 0x02000470, 0x023ffffc, 0x1fec, 270, 3, True,
                   (411, 511, 411, 511, 788, 14), (252, 788, 132, 1024, 1024)),
    "z12": Profile("z12", "10da5dbf24e3e67a7133d22cca2252a4e9f3452fdc7dd35ac9c7869bbd6a00ee",
                   0x2600, 0x9a600, 0x020005ac, 0x021ffffc, 0x2230, 90, 3, True,
                   (620, 775, 328, 238, 512, 415), (512, 133, 1024, 238, 704), True,
                   "c84016", "4d37393830109b3f", (6, 14, 10, 11, 9, 13, 5, 7)),
}


def machine_args(*groups: dict[str, str], machine: str = "ax3292") -> list[str]:
    """argv fragment that selects the machine and sets its board properties.

    Later groups win.  Commas inside a value are doubled, which is how the -M
    parser escapes them, so a filesystem path cannot be misread as another
    option.
    """
    merged: dict[str, str] = {}
    for group in groups:
        merged.update(group)
    if not merged:
        return ["-M", machine]
    encoded = ",".join(f"{k}={str(v).replace(',', ',,')}" for k, v in merged.items())
    return ["-M", f"{machine},{encoded}"]


#: Diagnostics are inert unless this is set, so tests opt in explicitly.
DIAGNOSTIC_ENV = {"AX3292_DEBUG": "1"}


def detect(image: Path) -> Profile:
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    return next((p for p in PROFILES.values() if p.sha256 == digest), PROFILES["original"])
