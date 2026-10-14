#!/usr/bin/env python3
"""Create a new sparse, partitioned FAT16 SD image. Never overwrite a file."""
from __future__ import annotations
import argparse
from pathlib import Path
import struct
import os
import io


def viewer_images() -> list[tuple[str, bytes]]:
    """Baseline 4:2:0 JPEGs; names follow KC02's PHO + five digits pattern."""
    from PIL import Image, ImageDraw

    images = []
    colours = [(255, 255, 255), (255, 255, 0), (0, 255, 255),
               (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
    for index, (size, caption) in enumerate([
        ((640, 480), "1: COLOUR BARS"),
        ((800, 600), "2: RGB GRADIENT"),
        ((640, 480), "3: TOP / BOTTOM / LEFT / RIGHT"),
        ((480, 640), "4: PORTRAIT"),
    ], 1):
        im = Image.new("RGB", (320, 240), (30, 50, 90))
        draw = ImageDraw.Draw(im)
        if index == 1:
            for i, colour in enumerate(colours):
                draw.rectangle((i * 40, 0, i * 40 + 39, 239), fill=colour)
        elif index == 2:
            im.putdata([(x * 255 // 319, y * 255 // 239,
                         (x + y) * 255 // 558)
                        for y in range(240) for x in range(320)])
            draw = ImageDraw.Draw(im)
        elif index == 3:
            draw.rectangle((8, 35, 310, 80), fill=(230, 40, 40))
            draw.rectangle((8, 165, 310, 207), fill=(40, 60, 230))
            draw.text((135, 48), "TOP", fill="white")
            draw.text((125, 180), "BOTTOM", fill="white")
            draw.text((12, 110), "LEFT", fill="white")
            draw.text((270, 110), "RIGHT", fill="white")
            draw.polygon([(145, 145), (175, 145), (160, 90)], fill="yellow")
        else:
            draw.ellipse((85, 45, 235, 195), fill=(250, 160, 40))
            draw.ellipse((112, 82, 130, 100), fill="black")
            draw.ellipse((185, 82, 203, 100), fill="black")
            draw.arc((112, 105, 205, 165), 0, 180, fill="black", width=5)
        draw.rectangle((0, 0, 319, 25), fill="black")
        draw.text((8, 7), caption, fill="white")
        draw.rectangle((0, 216, 319, 239), fill="black")
        draw.text((8, 222), f"PHO{index:05d}.JPG  {size[0]}x{size[1]}", fill="white")
        out = io.BytesIO()
        im.resize(size).save(out, "JPEG", quality=85, subsampling=2,
                             progressive=False, optimize=False)
        images.append((f"PHO{index:05d}.JPG", out.getvalue()))
    return images


def _seed_viewer(f, start, spc, fat_sectors, files):
    """Populate only the fresh FAT16 volume created below, never a mounted card."""
    cluster_bytes = spc * 512
    root = (start + 1 + 2 * fat_sectors) * 512
    data = root + 512 * 32
    fat = bytearray(fat_sectors * 512)
    fat[:4] = b"\xf8\xff\xff\xff"
    struct.pack_into("<H", fat, 4, 0xffff)  # PHOTO directory, cluster 2

    def entry(name, attr, cluster, size=0):
        e = bytearray(32)
        e[:11] = name.ljust(11, b" ")
        e[11] = attr
        struct.pack_into("<HHHI", e, 22, 0, 0x5821, cluster, size)  # 2024-01-01
        return e

    directory = bytearray(cluster_bytes)
    directory[:32] = entry(b".", 0x10, 2)
    directory[32:64] = entry(b"..", 0x10, 0)
    next_cluster = 3
    for i, (filename, payload) in enumerate(files):
        count = (len(payload) + cluster_bytes - 1) // cluster_bytes
        stem, suffix = filename.split(".")
        directory[(i + 2) * 32:(i + 3) * 32] = entry(
            (stem.ljust(8) + suffix).encode("ascii"), 0x20, next_cluster, len(payload))
        for cluster in range(next_cluster, next_cluster + count):
            following = cluster + 1 if cluster < next_cluster + count - 1 else 0xffff
            struct.pack_into("<H", fat, cluster * 2, following)
        f.seek(data + (next_cluster - 2) * cluster_bytes)
        f.write(payload)
        next_cluster += count
    f.seek(data)
    f.write(directory)
    f.seek(root + 32)
    f.write(entry(b"PHOTO", 0x10, 2))
    for sector in (start + 1, start + 1 + fat_sectors):
        f.seek(sector * 512)
        f.write(fat)


def create_image(path: Path, size_mib: int = 64, *, test_images: bool = False) -> dict:
    if not 16 <= size_mib <= 2048:
        raise ValueError("size must be 16..2048 MiB")
    if path.exists():
        raise FileExistsError(path)
    files = viewer_images() if test_images else []
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
        if files:
            _seed_viewer(f, start, spc, fat_sectors, files)
        f.flush()
        os.fsync(f.fileno())
    return {"path": str(path), "size_mib": size_mib, "partition_lba": start,
            "sectors_per_cluster": spc, "fat_sectors": fat_sectors, "clusters": clusters,
            "test_images": [f"PHOTO/{name}" for name, _ in files]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--size-mib", type=int, default=64)
    parser.add_argument("--test-images", action="store_true",
                        help="seed PHOTO/PHO00001..00004.JPG (requires Pillow)")
    args = parser.parse_args()
    print(create_image(args.image, args.size_mib, test_images=args.test_images))


if __name__ == "__main__":
    main()
