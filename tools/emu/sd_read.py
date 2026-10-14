#!/usr/bin/env python3
"""Read files from a partitioned FAT16 emulator image, without modifying it.

Inspect a stopped emulator's image so directory and FAT reads are consistent.
"""
from pathlib import Path
import struct


def jpeg_packets(payload):
    """Separate KC02's JRX wrapper; do not rewrite the saved file.

    Its final APP9 trailer contains JRX\\0 and a big-endian original-image
    offset. The preceding stitched preview ends with a restart marker rather
    than EOI; add EOI only to the exported validation packet, not to SD bytes.
    """
    if payload[:2] != b"\xff\xd8":
        raise ValueError("not JPEG data")
    if len(payload) < 12 or payload[-12:-4] != b"\xff\xe9\x00\x0aJRX\x00":
        return {"main": payload}
    offset = int.from_bytes(payload[-4:], "big")
    if not 2 <= offset < len(payload) - 14 or payload[offset:offset + 2] != b"\xff\xd8":
        raise ValueError("invalid JRX original-image offset")
    preview = payload[:offset]
    if not preview.endswith(b"\xff\xd9"):
        preview += b"\xff\xd9"
    return {"preview": preview, "main": payload[offset:-12]}


class FAT16:
    def __init__(self, path: Path):
        self.file = Path(path).open("rb")
        try:
            mbr = self._read(0, 512)
            if mbr[510:] != b"\x55\xaa" or mbr[450] not in (4, 6, 14):
                raise ValueError("expected a FAT16 MBR partition")
            start, sectors = struct.unpack_from("<II", mbr, 454)
            boot = self._read(start * 512, 512)
            bps = struct.unpack_from("<H", boot, 11)[0]
            spc, fats = boot[13], boot[16]
            reserved = struct.unpack_from("<H", boot, 14)[0]
            entries = struct.unpack_from("<H", boot, 17)[0]
            fat_sectors = struct.unpack_from("<H", boot, 22)[0]
            if (bps != 512 or boot[510:] != b"\x55\xaa" or not spc or
                    spc & (spc - 1) or not entries or not fat_sectors or not fats):
                raise ValueError("unsupported FAT16 BPB")
            root_sectors = (entries * 32 + 511) // 512
            self.cluster_bytes = spc * 512
            self.root_offset = (start + reserved + fats * fat_sectors) * 512
            self.root_length = entries * 32
            self.data_offset = self.root_offset + root_sectors * 512
            self.clusters = (sectors - reserved - fats * fat_sectors - root_sectors) // spc
            if not 4085 <= self.clusters < 65525:
                raise ValueError("not a FAT16 cluster count")
            self.fat = self._read((start + reserved) * 512, fat_sectors * 512)
            if len(self.fat) < (self.clusters + 2) * 2:
                raise ValueError("short FAT")
        except BaseException:
            self.file.close()
            raise

    def _read(self, offset, length):
        self.file.seek(offset)
        data = self.file.read(length)
        if len(data) != length:
            raise ValueError("truncated image")
        return data

    def _chain(self, cluster):
        seen = set()
        while cluster < 0xfff8:
            if not 2 <= cluster < self.clusters + 2 or cluster in seen:
                raise ValueError("invalid or cyclic FAT chain")
            seen.add(cluster)
            yield self._read(self.data_offset + (cluster - 2) * self.cluster_bytes,
                             self.cluster_bytes)
            cluster = struct.unpack_from("<H", self.fat, cluster * 2)[0]

    def _directory(self, cluster=0):
        data = (b"".join(self._chain(cluster)) if cluster else
                self._read(self.root_offset, self.root_length))
        result = {}
        for offset in range(0, len(data), 32):
            e = data[offset:offset + 32]
            if e[0] == 0:
                break
            if e[0] == 0xe5 or e[11] & 8:  # deleted, volume label or LFN
                continue
            stem = e[:8].decode("ascii").rstrip()
            suffix = e[8:11].decode("ascii").rstrip()
            name = stem + ("." + suffix if suffix else "")
            if name in (".", ".."):
                continue
            result[name] = {"name": name, "directory": bool(e[11] & 0x10),
                            "cluster": struct.unpack_from("<H", e, 26)[0],
                            "size": struct.unpack_from("<I", e, 28)[0]}
        return result

    def entries(self, directory=""):
        cluster = 0
        for component in directory.split("/") if directory else []:
            entry = self._directory(cluster)[component.upper()]
            if not entry["directory"] or entry["cluster"] < 2:
                raise ValueError("not a subdirectory")
            cluster = entry["cluster"]
        return self._directory(cluster)

    def read_file(self, path):
        directory, _, name = path.rpartition("/")
        entry = self.entries(directory)[name.upper()]
        if entry["directory"]:
            raise ValueError("not a file")
        if not entry["size"]:
            return b""
        data = b"".join(self._chain(entry["cluster"]))
        if len(data) < entry["size"]:
            raise ValueError("short file chain")
        return data[:entry["size"]]

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.file.close()
