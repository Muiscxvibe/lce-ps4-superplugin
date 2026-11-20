"""Check the draft's furnace vtable pins against the known retail image.

This is a read-only check of the named fields, not an execution test.
"""
import hashlib
import struct
from pathlib import Path

image = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
data = image.read_bytes()
assert hashlib.sha256(data).hexdigest() == 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f', 'unexpected image'
phoff = struct.unpack_from('<Q', data, 0x20)[0]
phnum = struct.unpack_from('<H', data, 0x38)[0]
segments = []
for i in range(phnum):
    kind, _flags, offset, va, _pa, size, _mem, _align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * 56)
    if size and kind in (1, 0x61000010):
        segments.append((va, offset, size))

def word(address):
    for va, offset, size in segments:
        if va <= address and address + 8 <= va + size:
            return struct.unpack_from('<Q', data, offset + address - va)[0]
    raise ValueError(f'unmapped address {address:#x}')

vtable = 0x125b190
pins = {
    vtable - 32: 0x50,
    vtable - 24: 0x50,
    vtable - 16: 0,
    vtable - 8: 0x1262790,
    vtable + 0x40: 0x321750,
    vtable + 0x60: 0x322b30,
    vtable + 0x188: 0x3233b0,
    0x125b158 - 16: (1 << 64) - 0x1e8,
    0x125b158 - 8: 0x1261e40,
    0x125b158 + 0x10: 0x31ec10,
    0x125b658 - 16: 0,
    0x125b658 - 8: 0x12627d0,
    0x125b658 + 0x30: 0x324200,
    0x125b708 - 16: 0,
    0x125b708 - 8: 0x12627f0,
    0x125b708 + 0x68: 0x325100,
    0x125b4e0 - 16: (1 << 64) - 0x50,
    0x125b4e0 - 8: 0x1262790,
    0x125b4e0 + 0x88: 0x323650,
    0x125b4e0 + 0xb8: 0x3237d0,
}
for address, expected in pins.items():
    actual = word(address)
    assert actual == expected, f'{address:#x}: got {actual:#x}, expected {expected:#x}'
print(f'fuel195 image pins: {len(pins) + 1} checks passed')
