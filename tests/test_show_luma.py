"""Offline guards for the live RAM-run harness; no camera access."""
import importlib.util
from pathlib import Path
from pathlib import Path
from unittest.mock import patch

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('show_luma_test', ROOT / 'docs/hw/show_luma.py')
show = importlib.util.module_from_spec(spec)
spec.loader.exec_module(show)


def record(bss_offset=0x168f04):
    base = 0x0235a200
    return dict(base=base, extent=0x170000, bss_end=base+bss_offset,
                stack_top=base+0x170000-16)


def test_scratch_follows_actual_bss_not_old_fixed_offset():
    r = record()
    address = show.capture_scratch(r)
    assert address >= r['bss_end'] > r['base'] + 0x168000
    assert address % 32 == 0
    assert address + 512 <= r['stack_top'] - 16384


@pytest.mark.parametrize('bss_offset', [0x16c000, 0x180000, -0x1000])
def test_scratch_refuses_overlap_or_outside_window(bss_offset):
    with pytest.raises(show.dump.DumpError, match='no safe CSI snapshot scratch'):
        show.capture_scratch(record(bss_offset))


def test_scratch_rejects_nonpositive_size():
    with pytest.raises(show.dump.DumpError):
        show.capture_scratch(record(), size=0)


def test_stock_signature_reads_do_not_authorise_writes():
    t = object.__new__(show.LumaRamTransport)
    t.window = (0x0235a200, 0x2c0000)
    t.blobs = {}
    for address, signature, _ in show.LUMA_STOCK_SITES:
        assert t._approved_read(address, len(signature)) == address
        assert t._approved_write(address, signature) is None
        assert t._approved_read(address, len(signature) - 1) is None


def test_stock_signature_mismatch_closes_execution_gate():
    t = object.__new__(show.LumaRamTransport)
    t.firmware_ok = True
    t.read_region = lambda address, size: b'\x00' * size
    with patch.object(show.rri.RamTransport, 'check_firmware', return_value=None):
        with pytest.raises(show.dump.DumpError, match='signature differs'):
            t.check_firmware()
    assert not t.firmware_ok


def args(**kw):
    base = dict(hold_frames=40, exp=0, gain=0x100, test_pattern=False,
                overlay=False, size=0, scratch=0x02370000, photos=False,
                jpeg_check=False)
    base.update(kw)
    return type('Args', (), base)()


def run_record():
    return dict(entry=0x02350000, start=0x02350010, beacon=0x02360000,
                bss_start=0x02350100, bss_end=0x02390000,
                sensor_y=0x02391000, sensor_uv=0x023a0000)


def test_packet_words_keep_every_permission_explicit():
    words = show.run_words(run_record(), args())
    assert len(words) == 16                     # the whole input area, no more
    assert words[14] == 0 and words[15] == 0     # no SD writes, no JPEG trial
    assert show.run_words(run_record(), args(photos=True))[14] == show.PHOTO_WRITE_TOKEN
    assert show.run_words(run_record(), args(jpeg_check=True))[15] == (show.JPEG_CHECK_TOKEN | 3)
    show.hw.build_packet(words=words)           # must fit before the reply header


def test_bounded_and_takeover_packets_differ_only_in_entry_and_hold():
    rec = run_record()
    bounded, takeover = show.run_words(rec, args()), show.run_words(rec, args(), True)
    assert takeover[0] == rec['start'] and bounded[0] == rec['entry']
    assert takeover[4] == 0 and bounded[4] == 40
    assert takeover[1:] == bounded[1:] or takeover[5:] == bounded[5:]


def test_photo_preflight_refuses_unhealthy_filesystem():
    good = dict(output=[1, 0x4C554D42], magic_ok=True)
    health = dict(de_timeouts=0, capture_fault=0,
                  photo_fs=dict(ready=1, count=3, error=0, writable=1))
    show.require_photo_preflight(good, health)
    for broken in ({'de_timeouts': 1}, {'capture_fault': 0x2000},
                   {'photo_fs': dict(ready=0, count=3, error=0, writable=1)},
                   {'photo_fs': dict(ready=1, count=3, error=-4, writable=1)},
                   {'photo_fs': dict(ready=1, count=3, error=0, writable=0)}):
        bad = dict(health)
        bad.update(broken)
        with pytest.raises(show.dump.DumpError, match='photo preflight refused'):
            show.require_photo_preflight(good, bad)
    # An empty card is still a usable preflight: the point is that the filesystem
    # answers, not that it already holds photos.
    empty = dict(health, photo_fs=dict(ready=1, count=0, error=0, writable=1))
    show.require_photo_preflight(good, empty)


def test_photo_writes_now_use_the_full_resolution_encoder_path():
    """--photos is allowed again, but it must still be an explicit word and the
    takeover must still be gated behind the read-only preflight."""
    words = show.run_words(run_record(), args(photos=True))
    assert words[14] == show.PHOTO_WRITE_TOKEN
    assert words[15] == 0            # enabling writes must not start a JPEG trial
    source = Path(show.__file__).read_text()
    assert 'require_photo_preflight(parsed_check,health)' in source


def test_bounded_jpeg_trial_cannot_be_combined_with_takeover():
    with patch('sys.argv', ['show_luma.py', '--payload-dir', '/nonexistent',
                            '--output', '/nonexistent', '--jpeg-check', '--takeover']):
        with pytest.raises(SystemExit):
            show.main()


def jpeg_words(**kw):
    words = dict(state=3, fault=0, length=0x21000, csi_events=0x200,
                 jpeg_events=0x40, picmr=0)
    words.update(kw)
    return words


def test_completed_jpeg_result_is_accepted():
    assert show.check_jpeg_result(jpeg_words()) == ''
    assert show.check_jpeg_result(jpeg_words(length=0x10000)) == ''
    # A full 512 KiB buffer is legal for the encoder but far above stock's own
    # budget for this width, so it must be reported rather than silently passed.
    assert 'band' in show.check_jpeg_result(jpeg_words(length=0x80000))


def test_incomplete_or_faulted_jpeg_result_is_refused():
    with pytest.raises(show.dump.DumpError, match='did not finish'):
        show.check_jpeg_result(jpeg_words(state=2))
    for fault in (1, 3, 4, 5, 7, 8, 9):
        with pytest.raises(show.dump.DumpError, match=f'fault {fault}'):
            show.check_jpeg_result(jpeg_words(fault=fault))


def test_jpeg_result_must_not_touch_the_interrupt_mask():
    # ax32xx_mjpegDecodeEnable-style code would unmask IRQ 0x16 here.
    with pytest.raises(show.dump.DumpError, match='interrupt mask'):
        show.check_jpeg_result(jpeg_words(picmr=1 << 22))


def test_jpeg_result_length_must_fit_its_buffer():
    with pytest.raises(show.dump.DumpError, match='outside its buffer'):
        show.check_jpeg_result(jpeg_words(length=0))
    with pytest.raises(show.dump.DumpError, match='outside its buffer'):
        show.check_jpeg_result(jpeg_words(length=0x80001))


def test_jpeg_result_size_outside_the_stock_band_is_reported_not_fatal():
    note = show.check_jpeg_result(jpeg_words(length=0x4000))
    assert 'band' in note and '16,384' in note
    note = show.check_jpeg_result(jpeg_words(length=0x50000))
    assert 'band' in note
