#!/usr/bin/env python3
"""Create a new sparse, partitioned FAT16 SD image. Never overwrite a file."""
from __future__ import annotations
import argparse
from pathlib import Path
import struct
import os


def create_image(path: Path, size_mib: int = 64) -> dict:
    if not 16 <= size_mib <= 2048:
        raise ValueError("size must be 16..2048 MiB")
    total = size_mib * 2048
    start = 2048
    sectors = total - start
    spc = 1
    while sectors // spc > 65000:
        spc *= 2
    fat_sectors = 1
    while True:
        clusters = (sectors - 1 - 32 - 2 * fat_sectors) // spc
        needed = ((clusters + 2) * 2 + 511) // 512
        if needed <= fat_sectors:
            break
        fat_sectors = needed
    mbr = bytearray(512)
    struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\xfe\xff\xff", 6,
                     b"\xfe\xff\xff", start, sectors)
    mbr[510:] = b"\x55\xaa"
    boot = bytearray(512)
    boot[:11] = b"\xeb\x3c\x90MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, spc, 1, 2, 512,
                     sectors if sectors < 65536 else 0, 0xf8, fat_sectors, 32, 64,
                     start, sectors if sectors >= 65536 else 0)
    boot[36] = 0x80
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x32920219)
    boot[43:54] = b"KC02EMU    "
    boot[54:62] = b"FAT16   "
    boot[510:] = b"\x55\xaa"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as f:
        f.truncate(size_mib * 1024 * 1024)
        f.write(mbr)
        f.seek(start * 512)
        f.write(boot)
        for offset in (1, 1 + fat_sectors):
            f.seek((start + offset) * 512)
            f.write(b"\xf8\xff\xff\xff")
        # Volume-label root entry; the remaining sparse sectors read as zero.
        f.seek((start + 1 + 2 * fat_sectors) * 512)
        f.write(b"KC02EMU    \x08" + bytes(20))
        f.flush()
        os.fsync(f.fileno())
    return {"path": str(path), "size_mib": size_mib, "partition_lba": start,
            "sectors_per_cluster": spc, "fat_sectors": fat_sectors, "clusters": clusters}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--size-mib", type=int, default=64)
    args = parser.parse_args()
    print(create_image(args.image, args.size_mib))


if __name__ == "__main__":
    main()
