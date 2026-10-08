"""Read-only RAM survey of a declared address window. Never writes or executes.

This exists to answer one question with device evidence: which SDRAM ranges are
untouched, so a larger scratch/execution window can be justified. It is
deliberately narrower than the dumper:

* only ``CALLBACK_SKIP`` (the stock memory-transfer routine) is used, so no RAM
  code runs;
* the direction byte is fixed to 0x80 (device -> host); any write path raises;
* every address must fall inside ``WINDOW``, declared below;
* the pinned firmware signatures must match before the first transfer;
* the loop stops at the first transport error and never retries.

Reads are non-destructive, but they still require the camera to be idle, so the
same "stop and power-cycle on failure" rule as the test suite applies.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import dump_firmware as dump

# Declared survey window. Keep it inside SDRAM and clear of code and the
# documented patch regions.
WINDOW = (0x020C6000, 0x02800000)
CHUNK = 2048


class SurveyTransport(dump.CameraTransport):
    """Stock transport with a read-only, window-checked command path."""

    def _command(self, buffer, length, callback=dump.CALLBACK_SKIP, offset=0, payload=None):
        if not self.claimed:
            raise dump.DumpError("USB interface has not been claimed.")
        if payload is not None:
            raise dump.DumpError("Survey is read-only; RAM writes are not permitted.")
        if callback != dump.CALLBACK_SKIP or offset:
            raise dump.DumpError("Survey must not execute RAM code.")
        pinned = {(a, len(b)) for a, b, _ in dump.ROUTINE_SIGNATURES}
        if (buffer, length) not in pinned and not (WINDOW[0] <= buffer and buffer + length <= WINDOW[1]):
            raise dump.DumpError(
                f"Read 0x{buffer:08X}+{length} is outside the declared survey window."
            )
        self.tag = (self.tag + 1) & 0xFFFFFFFF
        cdb = bytes([0xCD]) + struct.pack("<III", dump.MEMORY_TRANSFER_FN, buffer, callback)
        cbw = struct.pack("<4sIIBBB", b"USBC", self.tag, length, 0x80, 0, 16) + cdb + b"\0\0\0"
        self.log(f"survey read tag={self.tag} buffer=0x{buffer:08X} length={length}")
        self._write(cbw)
        data = self._read_exact(length)
        csw = self._read_exact(13)
        signature, tag, residue, status = struct.unpack("<4sIIB", csw)
        if signature != b"USBS" or tag != self.tag or residue != 0 or status != 0:
            raise dump.DumpError(
                f"Invalid USB status (signature={signature!r} tag={tag} residue={residue} "
                f"status={status}); power-cycle before retrying."
            )
        return data


def survey(start, end, output, timeout):
    usb, backend = dump.load_usb()
    cameras, webcams = dump.find_cameras(usb, backend)
    device = dump.select_camera(cameras, webcams, 1)
    report = dict(window=[start, end], chunk=CHUNK, started=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  device=dump.device_label(device), reads=0, bytes=0, status="running")
    output.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    try:
        with SurveyTransport(usb, device, timeout) as dev:
            dump.check_firmware(dev)
            report["firmware_signatures"] = [
                dict(address=a, sha256=hashlib.sha256(b).hexdigest(), name=n)
                for a, b, n in dump.ROUTINE_SIGNATURES
            ]
            for address in range(start, end, CHUNK):
                length = min(CHUNK, end - address)
                data = dev._command(address, length)
                if len(data) != length:
                    raise dump.DumpError("short read")
                blob += data
                report["reads"] += 1
                report["bytes"] += length
                report["last_address"] = address
        report["status"] = "pass"
    except Exception as exc:
        report["status"] = "fail"
        report["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        (output / "survey.bin").write_bytes(bytes(blob))
        report["dump_sha256"] = hashlib.sha256(bytes(blob)).hexdigest()
        report["finished"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (output / "survey.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description="Read-only RAM survey (no writes, no execution).")
    parser.add_argument("--start", type=lambda v: int(v, 0), default=WINDOW[0])
    parser.add_argument("--end", type=lambda v: int(v, 0), default=WINDOW[1])
    parser.add_argument("--output", default=None)
    parser.add_argument("--timeout", type=int, default=5000)
    args = parser.parse_args()
    if not WINDOW[0] <= args.start < args.end <= WINDOW[1]:
        raise SystemExit("requested range is outside the declared survey window")
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = Path(args.output) if args.output else \
        Path("patched/toolchain-tests/runs") / (stamp + "-survey")
    paths = [a for a, _, _ in dump.ROUTINE_SIGNATURES]
    print("Read-only survey: no RAM writes, no RAM execution.")
    print(f"window 0x{args.start:08X}..0x{args.end:08X}; signatures checked at " +
          ", ".join(f"0x{a:08X}" for a in paths))
    report = survey(args.start, args.end, output, args.timeout)
    print(f"{report['status']}: {report['reads']} reads, {report['bytes']} bytes -> {output}")


if __name__ == "__main__":
    main()
