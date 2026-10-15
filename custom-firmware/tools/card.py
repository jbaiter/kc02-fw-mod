#!/usr/bin/env python3
"""Create a PRIVATE FAT16 development card. Never modifies an existing image."""
import argparse
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'tools/emu'))
from create_sd import create_image


def seed(path, files):
    # Only called immediately after exclusive creation; this is not an editor
    # for existing, mounted, or user-owned card images.
    with path.open('r+b') as f:
        f.seek(2048*512)
        boot = f.read(512)
        spc, fats = boot[13], boot[16]
        fat_sectors = struct.unpack_from('<H',boot,22)[0]
        root = (2048+1+fats*fat_sectors)*512
        data = root+32*512
        fat = bytearray(fat_sectors*512)
        fat[:4] = b'\xf8\xff\xff\xff'
        cluster = 2
        for i,(name,payload) in enumerate(files,1):
            stem, ext = name.split('.')
            if not (1 <= len(stem) <= 8 and len(ext) <= 3 and name == name.upper() and
                    all(c.isalnum() or c in '._' for c in name) and len(name) <= 12):
                raise ValueError('requires uppercase 8.3 filenames')
            count = (len(payload)+spc*512-1)//(spc*512)
            if cluster+count >= len(fat)//2 or data+(cluster+count-2)*spc*512 > path.stat().st_size:
                raise ValueError('fixture files do not fit')
            if i >= 512:
                raise ValueError('root directory full')
            for c in range(cluster,cluster+count):
                struct.pack_into('<H',fat,c*2,c+1 if c+1 < cluster+count else 0xffff)
            entry = bytearray(32)
            entry[:11] = (stem.ljust(8)+ext.ljust(3)).encode('ascii')
            entry[11] = 0x20
            struct.pack_into('<HHI',entry,24,0x5821,cluster,len(payload))
            f.seek(root+i*32)
            f.write(entry)
            f.seek(data+(cluster-2)*spc*512)
            f.write(payload)
            cluster += count
        for mirror in range(fats):
            f.seek((2049+mirror*fat_sectors)*512)
            f.write(fat)
        f.flush()
        os.fsync(f.fileno())


def demo_photos():
    """Original artwork fixtures, not screenshots pretending to be captures."""
    import io
    from PIL import Image, ImageDraw
    images = []
    for i, (sky, hills, sun) in enumerate([
            ((36,62,82),(24,42,47),(247,202,137)),
            ((156,114,114),(64,68,92),(244,208,160))],1):
        im = Image.new('RGB',(320,240))
        im.putdata([tuple(min(255,c+y//6) for c in sky) for y in range(240) for x in range(320)])
        d = ImageDraw.Draw(im)
        d.ellipse((212,28,256,72), fill=sun)
        d.polygon([(0,174),(70,75),(126,143),(196,93),(320,188),(320,240),(0,240)],fill=hills)
        d.polygon([(48,102),(70,75),(89,97),(76,95),(66,106)], fill=(214,215,203))
        d.polygon([(0,214),(95,171),(160,195),(251,163),(320,202),(320,240),(0,240)],fill=(20,37,40))
        out = io.BytesIO()
        im.save(out, 'BMP')
        images.append((f'LUM{i:05d}.BMP',out.getvalue()))
    return images


def create(path, theme=ROOT/'theme', demo=False):
    config = (theme/'THEME.INI').read_bytes()
    files = [('THEME.INI',config)]
    for asset in sorted(theme.glob('*.BMP')):
        files.append((asset.name,asset.read_bytes()))
    if demo:
        files += demo_photos()
    create_image(path)
    seed(path,files)
    return path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path)
    p.add_argument('--theme',type=Path,default=ROOT/'theme')
    p.add_argument('--demo-photos',action='store_true')
    a=p.parse_args()
    print(create(a.image,a.theme,a.demo_photos))

if __name__ == '__main__':
    main()
