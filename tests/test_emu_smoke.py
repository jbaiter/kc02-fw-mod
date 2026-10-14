"""Fast checks for the headless interactive-emulator smoke harness."""
import importlib.util
from pathlib import Path
import sys

import pytest

EMU = Path(__file__).resolve().parents[1] / "tools" / "emu"
sys.path.insert(0, str(EMU))
spec = importlib.util.spec_from_file_location("emu_smoke", EMU / "smoke.py")
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def test_read_rgb_frame(tmp_path):
    path = tmp_path / "frame.ppm"
    pixels = bytes([0, 127, 255, 10, 20, 30])
    path.write_bytes(b"P6\n2 1\n255\n" + pixels)
    assert smoke.read_ppm(path) == (2, 1, pixels)


@pytest.mark.parametrize("data", [
    b"P5\n1 1\n255\n\x00",  # obsolete rotate-plane capture
    b"P6\n1 1\n255\n\x00",  # truncated frame
    b"P6\n0 1\n255\n",     # invalid dimensions
    b"P6\n1 1\n256\n\x00\x00\x00",
])
def test_rejects_invalid_frames(tmp_path, data):
    path = tmp_path / "frame.ppm"
    path.write_bytes(data)
    with pytest.raises(ValueError):
        smoke.read_ppm(path)


def test_key_events_use_short_real_time_press(monkeypatch):
    events = []
    sleeps = []
    qmp = object.__new__(smoke.QMP)
    monkeypatch.setattr(qmp, "call", lambda command, **args: events.append((command, args)))
    monkeypatch.setattr(smoke.time, "sleep", sleeps.append)
    qmp.key("right")
    assert sleeps == [0.2]
    assert [event[1]["events"][0]["data"]["down"] for event in events] == [True, False]
    assert all(event[0] == "input-send-event" for event in events)
    assert all(event[1]["events"][0]["data"]["key"] ==
               {"type": "qcode", "data": "right"} for event in events)
