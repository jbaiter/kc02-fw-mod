#!/usr/bin/env python3
"""Offline target build of the reconstructed menu, without USB/image mutation.
--base is a link address, not proof of free RAM. Retains the stock firmware engines.
"""
import argparse
import csv
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path
from extract_main_menu_tables import BIAS, SHA256

HERE = Path(__file__).resolve().parent

def generate_widgets(image, output):
    assert hashlib.sha256(image).hexdigest() == SHA256
    blob = image[0x020c4c9c - BIAS:0x020c4c9c - BIAS + 8 * 48]
    lines = ['/* Generated stock records: root, six labels and terminator. */',
             'MenuWidget menu_widgets[8] = {']
    for index in range(8):
        record = blob[index * 48:(index + 1) * 48]
        word = lambda off: struct.unpack_from('<I', record, off)[0]
        half = lambda off: struct.unpack_from('<H', record, off)[0]
        signed = lambda off: struct.unpack_from('<h', record, off)[0]
        array = lambda b: '{' + ', '.join(f'0x{x:02x}' for x in b) + '}'
        fields = dict(create=f'0x{word(0):08x}U', id=str(half(4)), x=str(signed(6)), y=str(signed(8)),
                      width=str(half(10)), height=str(half(12)), style=hex(half(14)),
                      bg_color=str(record[16]), pad11=array(record[17:20]), image=hex(word(20)),
                      image_align=str(record[24]), pad19=array(record[25:28]), string=hex(word(28)),
                      string_align=str(record[32]), font_color=str(record[33]), font=str(record[34]),
                      pad23=str(record[35]), private_data=hex(word(36)), extra=array(record[40:48]))
        lines.append('    { /* 0x%08x, widget ID 0x%04x */' % (0x020c4c9c + index * 48, half(4)))
        lines.extend(f'        .{name} = {value},' for name, value in fields.items())
        lines.append('    },')
    lines.append('};')
    output.write_text('\n'.join(lines) + '\n')
    return blob

def elf_data(path):
    blob = path.read_bytes()
    assert blob[:7] == b'\x7fELF\x01\x01\x01'
    assert struct.unpack_from('<H', blob, 18)[0] == 92
    assert struct.unpack_from('<I', blob, 36)[0] & 1
    offset = struct.unpack_from('<I', blob, 32)[0]
    size, count, names_index = struct.unpack_from('<HHH', blob, 46)
    headers = [struct.unpack_from('<10I', blob, offset + i * size) for i in range(count)]
    name_header = headers[names_index]
    names = blob[name_header[4]:name_header[4] + name_header[5]]
    sections = {}
    for header in headers:
        name = names[header[0]:].split(b'\0')[0].decode()
        sections[name] = dict(address=header[3], size=header[5],
                             data=blob[header[4]:header[4] + header[5]] if header[1] != 8 else b'')
    return sections

def build(args):
    if args.base < 0 or args.base > 0xffffffff or args.base % 32:
        raise ValueError('base must be a 32-byte aligned 32-bit address')
    image = args.firmware.read_bytes()
    if hashlib.sha256(image).hexdigest() != SHA256:
        raise ValueError('wrong firmware SHA256')
    args.out.mkdir(parents=True, exist_ok=True)
    widget_bytes = generate_widgets(image, args.out / 'main_menu_widgets.inc')
    imports = []
    for line in (HERE / 'firmware_imports.def').read_text().splitlines():
        match = re.fullmatch(r'IMPORT\((.+?),\s*(\w+),\s*\((.*)\),\s*(0x[0-9a-f]+)\)', line)
        if match:
            result, name, parameters, address = match.groups()
            assert not 0x020130bc <= int(address, 16) < 0x02013a78
            imports.append(dict(name=name, return_type=result, parameters=parameters, address=address))
    assert len(imports) == 19
    linker = args.out / 'main_menu.ld'
    linker.write_text('\n'.join([
        'OUTPUT_FORMAT("elf32-or1kle")', 'OUTPUT_ARCH(or1k)', 'ENTRY(main_menu_mode_enter)',
        *(f"{r['name']} = {r['address']};" for r in imports),
        'SECTIONS {', f' . = 0x{args.base:08x};',
        ' .text : { *(.text .text.*) }', ' . = ALIGN(32);',
        ' .rodata : { *(.rodata .rodata.*) }', ' . = ALIGN(32);',
        ' .data : { *(.data .data.*) }', ' . = ALIGN(32);',
        ' .bss : { *(.bss .bss.*) *(COMMON) }',
        ' /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }', '}']) + '\n')
    elf = args.out / 'main_menu.elf'; obj = args.out / 'main_menu.o'; binary = args.out / 'main_menu.bin'
    tool = lambda name: args.tool_prefix + name
    commands = [
        [tool('gcc'), '-mno-delay', '-mhard-div', '-Os', '-std=gnu11', '-ffreestanding',
         '-fno-builtin', '-fno-common', '-fno-pic', '-fno-stack-protector', '-ffunction-sections',
         '-fdata-sections', '-Wall', '-Wextra', '-Werror', '-I' + str(args.out), '-I' + str(HERE),
         '-c', str(HERE / 'main_menu_screen_model.c'), '-o', str(obj)],
        [tool('ld'), '-T', str(linker), '-Map=' + str(args.out / 'main_menu.map'), str(obj), '-o', str(elf)],
        [tool('objcopy'), '-O', 'binary', str(elf), str(binary)]]
    for command in commands: subprocess.run(command, check=True)
    text = subprocess.check_output([tool('nm'), '-n', str(elf)], text=True)
    (args.out / 'main_menu.nm').write_text(text)
    symbols = {parts[2]:int(parts[0], 16) for line in text.splitlines() if len(parts := line.split()) == 3}
    assert not subprocess.check_output([tool('nm'), '-u', str(elf)], text=True).strip()
    sections = elf_data(elf)
    assert sections.get('.bss', {}).get('size', 0) == 0
    def data_at(symbol, length):
        address = symbols[symbol]
        section = next(s for s in sections.values() if s['address'] <= address < s['address'] + s['size'])
        start = address - section['address']; return section['data'][start:start + length]
    assert data_at('menu_widgets', 384) == widget_bytes
    keys = [(0xe,'main_menu_window_enter'),(0xf,'main_menu_window_exit'),(0x1b,'main_menu_up'),
            (0x1c,'main_menu_down'),(0x1d,'main_menu_left'),(0x1e,'main_menu_right'),
            (0x1f,'main_menu_playback_shortcut'),(0x1a,'main_menu_ok'),(0x23,'main_menu_ok'),
            (9,'main_menu_blink'),(10,'main_menu_noop'),(0x2e,None)]
    bindings = data_at('menu_bindings', 96)
    for index, (message, name) in enumerate(keys):
        assert struct.unpack_from('<II', bindings, index * 8) == (message, symbols[name] if name else 0)
    with (HERE / 'entrypoints.tsv').open() as stream:
        entries = list(csv.DictReader(stream, delimiter='\t'))
    assert len(entries) == 15
    replacements = {row['symbol']:dict(original=row['original_address'], replacement=f"0x{symbols[row['symbol']]:08x}") for row in entries}
    hooks = []
    for slot, symbol in [(0x020c4c20,'main_menu_mode_enter'),(0x020c4c24,'main_menu_mode_exit'),(0x020c4c28,'main_menu_mode_tick')]:
        assert struct.unpack_from('<I', image, slot - BIAS)[0] == int(replacements[symbol]['original'],16)
        hooks.append(dict(slot=f'0x{slot:08x}', **replacements[symbol]))
    disassembly = subprocess.check_output([tool('objdump'), '-d', str(elf)], text=True)
    (args.out / 'main_menu.dis').write_text(disassembly)
    for target in re.findall(r'\bl\.jal\s+(?:0x)?([0-9a-f]+)', disassembly):
        assert not 0x020130bc <= int(target,16) < 0x02013a78
    sources = ['main_menu_screen_model.c','main_menu_screen.h','firmware_imports.def',
               'entrypoints.tsv','extract_main_menu_tables.py','build_main_menu.py']
    manifest = dict(firmware_sha256=SHA256, base=f'0x{args.base:08x}', binary_size=binary.stat().st_size,
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), replacements=replacements,
        imports=imports, mode_hook_candidates=hooks, build_commands=commands,
        source_sha256={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in sources},
        widget_bytes_identical=True, keymap_points_to_recompiled_handlers=True,
        undefined_symbols=[], bss_size=0, no_delay_elf32_little_endian_openrisc=True,
        deployment_allowed=False, placement_verified=False, on_device_tested=False,
        limitations=['Stock allocation-failure behavior, including a scratch-buffer leak, is retained.',
                     'Resource bytes and driver engines remain in the original firmware.',
                     'Compilation does not validate target ABI or reserve the link address.'])
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {elf}: {binary.stat().st_size} bytes; all 15 menu entries; no undefined symbols.')
    print('PASS: exact widget records and relocated keymap. OFFLINE ONLY; no deployment.')
    return manifest

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--firmware', type=Path, default=HERE.parent.parent / 'firmware/original.bin')
    ap.add_argument('--base', type=lambda value:int(value,0), required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--tool-prefix', default='/tmp/ax3292-toolchain/prefix/bin/or1k-elf-')
    build(ap.parse_args())
