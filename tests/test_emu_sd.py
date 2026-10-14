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


@pytest.mark.parametrize("size", [16, 64, 2048])
def test_viewer_images_have_valid_fat_chains_and_baseline_jpeg(tmp_path, size):
    import io
    Image = pytest.importorskip("PIL.Image")
    image = tmp_path / "viewer.img"
    result = sd.create_image(image, size, test_images=True)
    assert result["test_images"] == [f"PHOTO/PHO{i:05d}.JPG" for i in range(1, 5)]
    spc = result["sectors_per_cluster"]
    cb = spc * 512
    fs = result["fat_sectors"]
    root = (2048 + 1 + 2 * fs) * 512
    data = root + 512 * 32
    with image.open("rb") as f:
        f.seek((2048 + 1) * 512)
        fat = f.read(fs * 512)
        f.seek((2048 + 1 + fs) * 512)
        assert f.read(fs * 512) == fat
        f.seek(root + 32)
        e = f.read(32)
        assert e[:12] == b"PHOTO      \x10"
        assert struct.unpack_from("<H", e, 26)[0] == 2
        f.seek(data)
        directory = f.read(cb)
        seen = {2}
        for i, dimensions in enumerate([(640, 480), (800, 600), (640, 480), (480, 640)]):
            e = directory[(i + 2) * 32:(i + 3) * 32]
            assert e[:11] == f"PHO{i + 1:05d}JPG".encode()
            cluster, length = struct.unpack_from("<HI", e, 26)
            payload = bytearray()
            while cluster < 0xfff8:
                assert 2 <= cluster < result["clusters"] + 2
                assert cluster not in seen
                seen.add(cluster)
                f.seek(data + (cluster - 2) * cb)
                payload.extend(f.read(cb))
                cluster = struct.unpack_from("<H", fat, cluster * 2)[0]
            payload = payload[:length]
            assert payload[:2] == b"\xff\xd8" and payload[-2:] == b"\xff\xd9"
            with Image.open(io.BytesIO(payload)) as jpeg:
                jpeg.load()
                assert jpeg.size == dimensions
                assert jpeg.mode == "RGB"
                assert not jpeg.info.get("progressive")


def test_seed_option_does_not_overwrite_existing_image(tmp_path):
    image = tmp_path / "sd.img"
    image.write_bytes(b"existing user data")
    with pytest.raises(FileExistsError):
        sd.create_image(image, test_images=True)
    assert image.read_bytes() == b"existing user data"


@pytest.mark.parametrize("size", [0, 8, 2049])
def test_invalid_size_does_not_create_image(tmp_path, size):
    image = tmp_path / "sd.img"
    with pytest.raises(ValueError):
        sd.create_image(image, size)
    assert not image.exists()
