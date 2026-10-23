#!/usr/bin/env python3
"""Reject mismatched ELF, chip files, ZIP and MAME hashes before launch."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
import zlib


def verify(project):
    build = project / 'build'
    rom = build / 'rom'
    elf = build / 'rom.elf'
    cart = rom / 'gngl1r1.zip'
    for marker in (b'FULL LEVEL 1 R1', b'MAZAPEGUL'):
        if marker not in elf.read_bytes():
            raise ValueError(f'ELF marker missing: {marker!r}')
    software = ET.parse(rom / 'neogeo.xml').find("software[@name='gngl1r1']")
    if software is None:
        raise ValueError('gngl1r1 absent from MAME software list')
    expected = {f'gngl1r1-{chip}.{chip}': size for chip, size in (
        ('p1', 1048576), ('s1', 131072), ('m1', 131072),
        ('c1', 2097152), ('c2', 2097152),
        ('v1', 524288), ('v2', 524288), ('v3', 524288), ('v4', 524288))}
    hashes = {}
    entries = {entry.attrib['name']: entry.attrib for entry in software.iter('rom')}
    if set(entries) != set(expected):
        raise ValueError('MAME chip list differs from required cartridge contents')
    with zipfile.ZipFile(cart) as archive:
        if len(archive.namelist()) != len(expected) or set(archive.namelist()) != set(expected):
            raise ValueError('ZIP chip list differs from required cartridge contents')
        for name, size in expected.items():
            data = (rom / name).read_bytes()
            if len(data) != size or archive.read(name) != data:
                raise ValueError(f'ZIP/chip size or content mismatch: {name}')
            entry = entries[name]
            if (int(entry['size'], 0) != size
                    or entry['sha1'] != hashlib.sha1(data).hexdigest()
                    or int(entry['crc'], 16) != zlib.crc32(data)):
                raise ValueError(f'MAME hash mismatch: {name}')
            hashes[name] = hashlib.sha256(data).hexdigest()
    # Recreate P1 directly from this ELF, then apply Neo Geo word swapping.
    with tempfile.TemporaryDirectory() as temporary:
        binary = Path(temporary) / 'p1.bin'
        subprocess.run(['m68k-neogeo-elf-objcopy', '-O', 'binary', '-S', '-R', '.text2',
                        '--gap-fill', '0xff', '--pad-to', '1048576', str(elf), str(binary)],
                       check=True)
        data = bytearray(binary.read_bytes())
        data[0::2], data[1::2] = data[1::2], data[0::2]
        if data != (rom / 'gngl1r1-p1.p1').read_bytes():
            raise ValueError('P1 does not originate from the current ELF')
    hashes['rom.elf'] = hashlib.sha256(elf.read_bytes()).hexdigest()
    hashes['gngl1r1.zip'] = hashlib.sha256(cart.read_bytes()).hexdigest()
    (build / 'verified-rom-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    print('Verified ELF -> P1 -> cartridge ZIP -> MAME CRC/SHA1; SHA256 manifest saved.')


if __name__ == '__main__':
    try:
        verify(Path(sys.argv[1]).resolve())
    except (OSError, ValueError, KeyError, ET.ParseError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        sys.exit(f'ROM verification failed: {error}')
