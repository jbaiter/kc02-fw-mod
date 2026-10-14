"""Read-only card inspection, JRX packet extraction and launcher failures."""
import io
from pathlib import Path
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/emu"))
from create_sd import create_image, viewer_images
from sd_read import FAT16, jpeg_packets
import run


def test_reader_extracts_seeded_files_without_changing_card(tmp_path):
    pytest.importorskip("PIL.Image")
    card = tmp_path / "sd.img"
    create_image(card, 16, test_images=True)
    with FAT16(card) as reader:
        assert list(reader.entries("PHOTO")) == [name for name, _ in viewer_images()]
        for name, payload in viewer_images():
            assert reader.read_file("PHOTO/" + name) == payload


def test_reader_rejects_truncated_mbr(tmp_path):
    card = tmp_path / "bad.img"
    card.write_bytes(b"bad")
    with pytest.raises(ValueError, match="truncated"):
        FAT16(card)


def test_reader_rejects_cyclic_chain(tmp_path):
    pytest.importorskip("PIL.Image")
    card = tmp_path / "sd.img"
    create_image(card, 16, test_images=True)
    with FAT16(card) as reader:
        # Deliberately corrupt the private reader's snapshot, not the disk.
        fat = bytearray(reader.fat)
        fat[4:6] = b"\x02\x00"
        reader.fat = bytes(fat)
        with pytest.raises(ValueError, match="cyclic"):
            reader.entries("PHOTO")


def test_jrx_packets_are_independently_decodable():
    Image = pytest.importorskip("PIL.Image")
    out = io.BytesIO()
    Image.new("RGB", (16, 16), "green").save(out, "JPEG")
    original = out.getvalue()
    preview = original[:-2]
    wrapper = preview + original + b"\xff\xe9\x00\x0aJRX\x00" + len(preview).to_bytes(4, "big")
    packets = jpeg_packets(wrapper)
    assert packets == {"main": original, "preview": original}
    assert jpeg_packets(original) == {"main": original}
    for payload in packets.values():
        Image.open(io.BytesIO(payload)).load()
    assert wrapper[len(preview):len(preview) + 2] == b"\xff\xd8"


@pytest.mark.parametrize("offset", [0, 1, 65535])
def test_rejects_invalid_jrx_offset(offset):
    with pytest.raises(ValueError, match="offset"):
        jpeg_packets(b"\xff\xd8data\xff\xe9\x00\x0aJRX\x00" + offset.to_bytes(4, "big"))


def test_launcher_shows_startup_error_and_propagates_exit_status(tmp_path, capsys):
    fake = tmp_path / "qemu"
    fake.write_text("#!/bin/sh\necho 'Cannot open/lock SD image: already in use' >&2\nexit 1\n")
    fake.chmod(0o755)
    firmware = tmp_path / "firmware.bin"
    firmware.write_bytes(b"firmware")
    code = run.main([str(firmware), "--qemu", str(fake), "--interactive",
                     "--app-offset", "0", "--app-end", "8"])
    output = capsys.readouterr()
    assert code == 1
    assert "Cannot open/lock SD image" in output.err
    assert "0 SPR accesses" not in output.out
