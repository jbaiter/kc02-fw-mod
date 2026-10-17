"""Cooperative photo-file jobs against fake FatFs callbacks, never a camera."""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT=Path(__file__).resolve().parents[1]


@pytest.fixture(scope='module')
def harness(tmp_path_factory):
    cc=shutil.which('cc')
    if not cc:
        pytest.skip('C compiler unavailable')
    binary=tmp_path_factory.mktemp('stock-fs')/'test'
    subprocess.run([cc,'-std=c99','-Wall','-Wextra','-Werror','-DLUMA_BENCH_RUN',
                    '-fsanitize=undefined','-fno-sanitize-recover=undefined',
                    '-I'+str(ROOT/'custom-firmware/include'),
                    *(str(ROOT/'custom-firmware/src'/n) for n in
                      ('stock_fs.c','stock_photo.c','ui.c','theme.c','paint.c')),
                    str(ROOT/'tests/fixtures/stock_fs_harness.c'),'-o',str(binary)],check=True)
    return binary


@pytest.mark.parametrize('mode',range(1,12),ids=[
    'verified-roundtrip','short-write','readback-mismatch','wrong-medium-route',
    'read-only','new-viewer-selection','close-failure','opaque-object-guard',
    'press-shutter-save-viewer-and-reinitialize','only-real-stills-are-listed',
    'undecodable-still-is-not-shown'])
def test_photo_file_job(harness,mode):
    subprocess.run([str(harness),str(mode)],check=True)
