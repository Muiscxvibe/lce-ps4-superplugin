"""Execute the real retail tick after experimental mod-fuel ignition.

Only shared-pointer copying and recipe eligibility are stubbed; this checks
the stock tick's timer and cook-progress behavior, not the full integration.
"""
import hashlib
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RAX, UC_X86_REG_RSP, UC_X86_REG_RIP

image = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf').read_bytes()
assert hashlib.sha256(image).hexdigest() == 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f', 'unexpected image'
u = Uc(UC_ARCH_X86, UC_MODE_64)
u.mem_map(0, 0x1500000)
u.mem_write(0, image[0x4000:0x4000 + 0x10c0354])
u.mem_write(0x1200000, image[0x10c8000:0x10c8000 + 0x14d928])
u.mem_write(0x1400000, image[0x1218000:0x1218000 + 0x8ac8])
u.mem_map(0x4000000, 0x100000)
u.mem_map(0x5000000, 0x100000)
u.mem_map(0x6000000, 0x100000)
be, level, slots, stack = 0x6000000, 0x6001000, 0x6002000, 0x6003000
done, sp = 0x5000000, 0x400f000

def wq(address, value): u.mem_write(address, struct.pack('<Q', value))
def wi(address, value): u.mem_write(address, struct.pack('<I', value))
def q(address): return struct.unpack('<Q', u.mem_read(address, 8))[0]
def i(address): return struct.unpack('<I', u.mem_read(address, 4))[0]

wq(be, 0x125b190)
wq(be + 0x20, level)
wq(be + 0x58, slots)
wq(slots, stack)
wq(slots + 0x18, stack)
wi(be + 0xb8, 2)  # 1 tick fuel + the experimental wrapper's compensation
wi(be + 0xbc, 1)
wi(be + 0xc4, 200)
wq(sp, done)
u.reg_write(UC_X86_REG_RSP, sp)
u.reg_write(UC_X86_REG_RDI, be)
seen = []

def hook(m, address, _size, _user):
    if address == done:
        m.emu_stop()
        return
    if address == 0x66b00:  # shared_ptr copy, without a control block
        dst = m.reg_read(UC_X86_REG_RDI)
        wq(dst, m.reg_read(UC_X86_REG_RSI))
        wq(dst + 8, m.reg_read(UC_X86_REG_RDX))
    elif address == 0x322060:  # assume the input has a valid smelting recipe
        seen.append('can_burn')
        m.reg_write(UC_X86_REG_RAX, 1)
    else:
        return
    rsp = m.reg_read(UC_X86_REG_RSP)
    m.reg_write(UC_X86_REG_RIP, q(rsp))
    m.reg_write(UC_X86_REG_RSP, rsp + 8)

u.hook_add(UC_HOOK_CODE, hook)
u.emu_start(0x321750, done + 1, count=100000)
assert u.reg_read(UC_X86_REG_RIP) == done, 'retail tick did not return'
assert seen == ['can_burn'], f'unexpected recipe calls: {seen}'
assert i(be + 0xb8) == 1, 'burn timer should have one tick left'
assert i(be + 0xbc) == 1, 'burn duration changed'
assert i(be + 0xc0) == 1, 'furnace did not advance cooking'
print('fuel195 retail tick: 5 checks passed')
