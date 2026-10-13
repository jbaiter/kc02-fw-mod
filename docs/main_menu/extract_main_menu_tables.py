#!/usr/bin/env python3
"""Extract hash-pinned KC02 main-menu tables and assets; no device access."""
import argparse
import csv
import hashlib
import json
import struct
from pathlib import Path

SHA256 = '218c6cdfe59932d197d2ee85e4ba5dcf05860c25ee31b2d3684c19ac1ec721a5'
BIAS = 0x01ffda00
MODE_DESCRIPTOR = 0x020c4c18
WINDOW_DESCRIPTOR = 0x020c4c2c
COORDINATES = 0x0207bb60
ICON_RESOURCES = 0x0207bb78
SFAT_BASE = 0x0d3200
WIDGET_STRIDE = 48
ITEM_MODES = [3, 2, 5, 4, 7, 9]  # Checked against 0x02013938 instructions.
ITEM_ACTIONS = ['camera', 'video_recording', 'audio_player', 'playback', 'game_menu', 'settings']

def extract(firmware, output):
    image = firmware.read_bytes()
    if hashlib.sha256(image).hexdigest() != SHA256:
        raise ValueError('firmware SHA256 mismatch')
    def read(address, length):
        offset = address - BIAS
        assert 0 <= offset <= len(image) - length, hex(address)
        return image[offset:offset + length]
    def hex32(value): return f'0x{value:08x}'
    mode = struct.unpack('<5I', read(MODE_DESCRIPTOR, 20))
    assert mode[2:] == (0x020130e4, 0x020130d0, 0x020130bc)
    keymap, widgets, handle, reopen = struct.unpack('<4I', read(WINDOW_DESCRIPTOR, 16))
    assert (keymap, widgets, handle, reopen) == (0x020c4c3c, 0x020c4c9c, 0, 0)
    keys = []
    for index in range(64):
        address = keymap + index * 8
        message, handler = struct.unpack('<2I', read(address, 8))
        keys.append(dict(address=hex32(address), message=message, handler=hex32(handler)))
        if message == 0x2e or not handler:
            assert (message, handler) == (0x2e, 0)
            break
    else: raise ValueError('missing keymap terminator')
    rows = []
    for index in range(64):
        address = widgets + index * WIDGET_STRIDE
        record = read(address, WIDGET_STRIDE)
        word = lambda off: struct.unpack_from('<I', record, off)[0]
        if not word(0): break
        x, y, width, height = struct.unpack_from('<hhHH', record, 6)
        rows.append(dict(address=hex32(address), create=hex32(word(0)),
            id=struct.unpack_from('<H', record, 4)[0], x=x, y=y, width=width, height=height,
            style=hex(struct.unpack_from('<H', record, 14)[0]), bg_color=record[16],
            image=hex32(word(20)), image_align=record[24], string=hex32(word(28)),
            string_align=record[32], font_color=record[33], font=record[34],
            private=hex32(word(36)), extra_28=hex32(word(40)), extra_2c=hex32(word(44)),
            raw_hex=record.hex()))
    else: raise ValueError('missing widget terminator')
    assert len(rows) == 7 and len(keys) == 12
    assert [r['id'] for r in rows] == [0xffff, 0, 1, 2, 3, 4, 5]
    output.mkdir(parents=True, exist_ok=True)
    (output / 'assets').mkdir(exist_ok=True)
    sfat_size = struct.unpack_from('<I', image, SFAT_BASE)[0]
    assert sfat_size == 784
    def asset(resource, stem):
        # nv_open/nv_size read SFAT_BASE + resource*8. Resource 1 is entry 0,
        # because the first eight bytes are the SFAT header, not an asset entry.
        assert 1 <= resource < sfat_size // 8
        relative, length = struct.unpack_from('<II', image, SFAT_BASE + resource * 8)
        offset = SFAT_BASE + relative
        assert 0 <= offset <= len(image) - length
        data = image[offset:offset + length]
        ext = '.jpg' if data[:2] == b'\xff\xd8' else '.bmp' if data[:2] == b'BM' else '.bin'
        path = Path('assets') / (stem + ext)
        (output / path).write_bytes(data)
        record = dict(resource_id=resource, sfat_zero_based_index=resource - 1,
                      file_offset=hex(offset), length=length, path=str(path),
                      sha256=hashlib.sha256(data).hexdigest())
        if ext == '.bmp':
            record['bitmap_width'], record['bitmap_height'] = struct.unpack_from('<ii', data, 18)
            assert (record['bitmap_width'], record['bitmap_height']) == (96, 96)
        return record
    background = asset(0x20, 'background')
    assert background['path'].endswith('.jpg')
    items = []
    for index, (mode_id, action) in enumerate(zip(ITEM_MODES, ITEM_ACTIONS)):
        x, y = struct.unpack('<hh', read(COORDINATES + index * 4, 4))
        resource, = struct.unpack('<I', read(ICON_RESOURCES + index * 4, 4))
        label = rows[index + 1]
        items.append(dict(index=index, action=action, target_mode=mode_id,
                          icon_x=x, icon_y=y, icon_width=96, icon_height=96,
                          label_widget_id=label['id'], label_resource=label['string'],
                          icon=asset(resource, f'{index}_{action}_selection')))
    data = dict(firmware_sha256=SHA256, cpu_to_file_bias=hex32(BIAS),
                mode_descriptor=hex32(MODE_DESCRIPTOR),
                mode_callbacks=dict(enter=hex32(mode[2]), exit=hex32(mode[3]), tick=hex32(mode[4])),
                window_descriptor=hex32(WINDOW_DESCRIPTOR), keymap_address=hex32(keymap),
                widgets_address=hex32(widgets), widget_stride=WIDGET_STRIDE,
                widget_terminator=hex32(address), initial_handle=handle, repeat_open=reopen,
                coordinate_table=hex32(COORDINATES), icon_resource_table=hex32(ICON_RESOURCES),
                background=background, items=items, keymap=keys, widgets=rows,
                globals=dict(selected='0x020d41d3', icon_width='0x020cceac', icon_height='0x020cceae',
                    icon_buffers='0x020cceb0', background_height='0x020ccec8',
                    background_width='0x020cceca', background_buffer='0x020ccecc',
                    blink_phase='0x020cced0', blink_timestamp='0x020cced4'))
    (output / 'tables.json').write_text(json.dumps(data, indent=2) + '\n')
    for name, records in [('keymap', keys), ('widgets', rows)]:
        with (output / (name + '.tsv')).open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(records[0]), delimiter='\t')
            writer.writeheader(); writer.writerows(records)
    with (output / 'items.tsv').open('w', newline='') as stream:
        fields = ['index', 'action', 'target_mode', 'icon_x', 'icon_y', 'label_widget_id', 'label_resource']
        writer = csv.DictWriter(stream, fields, delimiter='\t', extrasaction='ignore')
        writer.writeheader(); writer.writerows(items)
    print('Extracted root + 6 labels, 11 bindings, 6 selection BMPs and background JPEG; hash verified.')
    return data

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('firmware', type=Path); ap.add_argument('output', type=Path)
    args = ap.parse_args(); extract(args.firmware, args.output)
