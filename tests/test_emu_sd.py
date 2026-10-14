"""Validate the writable emulated card's on-disk FAT16 layout."""
import importlib.util
from pathlib import Path
import struct

import pytest

path = Path(__file__).resolve().parents[1] / "tools/emu/create_sd.py"
spec = importlib.util.spec_from_file_location("emu_create_sd", path)
sd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sd)


@pytest.mark.parametrize("size", [16, 64, 512, 2048])
def test_partition_and_fat16_layout(tmp_path, size):
    image = tmp_path / "sd.img"
    result = sd.create_image(image, size)
    assert image.stat().st_size == size * 1024 * 1024
    with image.open("rb") as f:
        mbr = f.read(512)
        assert mbr[510:] == b"\x55\xaa"
        start, sectors = struct.unpack_from("<II", mbr, 454)
        assert mbr[450] == 6 and start == 2048
        assert start + sectors == size * 2048
        f.seek(start * 512)
        boot = f.read(512)
        assert boot[510:] == b"\x55\xaa"
        assert struct.unpack_from("<H", boot, 11)[0] == 512
        assert boot[13] == result["sectors_per_cluster"]
        assert 4085 <= result["clusters"] < 65525
        assert result["fat_sectors"] * 512 >= (result["clusters"] + 2) * 2
        assert boot[54:62] == b"FAT16   "
        assert boot[16] == 2
        for sector in (start + 1, start + 1 + result["fat_sectors"]):
            f.seek(sector * 512)
            assert f.read(4) == b"\xf8\xff\xff\xff"
        f.seek((start + 1 + 2 * result["fat_sectors"]) * 512)
        assert f.read(12) == b"KC02EMU    \x08"


def test_never_overwrites_existing_image(tmp_path):
    image = tmp_path / "sd.img"
    image.write_bytes(b"existing user data")
    with pytest.raises(FileExistsError):
        sd.create_image(image)
    assert image.read_bytes() == b"existing user data"


@pytest.mark.parametrize("size", [0, 8, 2049])
def test_invalid_size_does_not_create_image(tmp_path, size):
    image = tmp_path / "sd.img"
    with pytest.raises(ValueError):
        sd.create_image(image, size)
    assert not image.exists()
