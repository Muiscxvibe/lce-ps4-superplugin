"""Run the real retail 1.95 code audio195 relies on, in Unicorn.

1. AIL_WAV_info 0xe22110 (the Miles RIFF parser AIL_set_sample_file uses for PCM) on the WAV
   image audio195 builds: format 1 / data pointer / length / rate / 16 bits / 1 channel.
2. basic_string<wchar_t>::assign 0x67840 on a SoundEvent laid out exactly as a195_register_events
   does (empty wstring, capacity 7 at +0x20): a 10-char key is stored (heap buffer) with size 10.
3. SoundEvent::register 0x6672c0 on that object: it stores the counter [0x1430568] into +0x28,
   passes (registry 0x1430570, id, ResourceLocation, ev) to the map insert 0x6673d0, bumps the
   counter and returns ev.
4. The stock SoundEngine::play 0x7c2a80 returns without touching Miles for a null SoundEvent
   (the forward of a null event from a195_play_thunk is harmless).
Stubs (out of scope, each listed where used): operator new 0xe6cfd0 (bump allocator), free 0xe6cfb0, wmemmove/wmemcpy
imports 0xe6d110/0xe6d120,
the Miles API lock/unlock 0xe34eb0/0xe34fa0, ResourceLocation helpers 0xcda2f0/0xcda0b0/0xcdab00 and
the registry insert 0x6673d0 (recorded). This tests code paths and layouts, not audible output.
"""
import struct, sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE
from unicorn.x86_const import *

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
img = IMAGE.read_bytes()
HEAP, STACK, DONE, OBJ = 0x8000000, 0x4000000, 0x5000000, 0x6000000
checks = fails = 0


def check(c, what):
    global checks, fails
    checks += 1
    if not c:
        fails += 1
        print('FAIL', what)


def machine():
    u = Uc(UC_ARCH_X86, UC_MODE_64)
    u.mem_map(0, 0x1500000)
    u.mem_write(0, img[0x4000:0x4000 + 0x10c0354])
    u.mem_write(0x1200000, img[0x10c8000:0x10c8000 + 0x14d928])
    u.mem_write(0x1400000, img[0x1218000:0x1218000 + 0x8ac8])
    for a in (HEAP, STACK, DONE, OBJ):
        u.mem_map(a, 0x100000)
    # stack-protector canary pointer used by these functions ([0x134bae8] -> canary)
    return u


def q(u, a): return struct.unpack('<Q', u.mem_read(a, 8))[0]
def d(u, a): return struct.unpack('<I', u.mem_read(a, 4))[0]
def wq(u, a, v): u.mem_write(a, struct.pack('<Q', v))
def wd(u, a, v): u.mem_write(a, struct.pack('<I', v))


def call(u, fn, args=(), stubs=None, limit=400000):
    stubs = stubs or {}
    regs = [UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8, UC_X86_REG_R9]
    for r, v in zip(regs, args):
        u.reg_write(r, v & 0xffffffffffffffff)
    sp = STACK + 0xf000
    wq(u, sp, DONE)
    u.reg_write(UC_X86_REG_RSP, sp)
    seen = []

    def hook(m, a, size, _):
        if a == DONE:
            m.emu_stop(); return
        if a in stubs:
            seen.append(a)
            rv = stubs[a](m)
            m.reg_write(UC_X86_REG_RAX, rv or 0)
            s = m.reg_read(UC_X86_REG_RSP)
            m.reg_write(UC_X86_REG_RIP, q(m, s)); m.reg_write(UC_X86_REG_RSP, s + 8)
    h = u.hook_add(UC_HOOK_CODE, hook)
    u.emu_start(fn, DONE + 0x10, count=limit)
    u.hook_del(h)
    return u.reg_read(UC_X86_REG_RAX), seen


heap = [HEAP]
def alloc(m):
    n = m.reg_read(UC_X86_REG_RDI); p = heap[0]; heap[0] += (n + 15) & ~15
    m.mem_write(p, bytes(n)); return p


def memmove(m):   # wmemmove/wmemcpy PLT imports 0xe6d110 / 0xe6d120 (count in 2-byte wchar_t units; not in the image)
    dst, src, n = m.reg_read(UC_X86_REG_RDI), m.reg_read(UC_X86_REG_RSI), m.reg_read(UC_X86_REG_RDX)
    m.mem_write(dst, bytes(m.mem_read(src, 2 * n))); return dst


STR = {0xe6cfd0: alloc, 0xe6cfb0: lambda m: 0, 0xe6d110: memmove, 0xe6d120: memmove}


def wav(pcm, rate):   # identical layout to a195_wav_build (checked byte-for-byte against the C build by test.sh)
    return (b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16)
            + b'data' + struct.pack('<I', len(pcm)) + pcm)


def canary(u):
    wq(u, 0x134bae8, OBJ + 0xff00)      # GOT slot of the stack guard -> a readable zero word


# 1. AIL_WAV_info on our image
u = machine(); canary(u)
pcm = bytes(range(256)) * 8
image = wav(pcm, 22050)
if len(sys.argv) > 1:   # test.sh passes the C-built image to prove both builders agree
    image = Path(sys.argv[1]).read_bytes()
    check(image == wav(Path(sys.argv[1]).read_bytes()[44:], 22050), 'C a195_wav_build == python reference')
u.mem_write(OBJ, image)
info = OBJ + 0x8000
u.mem_write(info, bytes(0x40))
r, seen = call(u, 0xe22110, (OBJ, info), stubs={0xe34eb0: lambda m: 0, 0xe34fa0: lambda m: 0})
check(r & 0xffffffff == 1, 'AIL_WAV_info accepts the audio195 WAV')
check(d(u, info) == 1, 'format WAVE_FORMAT_PCM (1)')
check(q(u, info + 8) == OBJ + 44, 'data_ptr = image + 44')
check(d(u, info + 0x10) == len(image) - 44, 'data_len')
check(d(u, info + 0x14) == 22050, 'rate')
check(d(u, info + 0x18) == 16, 'bits 16 (the 0xe25802 cmp $0x10 branch)')
check(d(u, info + 0x1c) == 1, 'channels 1')
# a truncated / non-RIFF image is refused by the same parser
u.mem_write(OBJ, image[:8] + b'JUNK' + image[12:])
r, _ = call(u, 0xe22110, (OBJ, info), stubs={0xe34eb0: lambda m: 0, 0xe34fa0: lambda m: 0})
check(r & 0xffffffff == 0, 'AIL_WAV_info rejects an image without WAVE')

# 2. wstring assign 0x67840 on a fresh SoundEvent layout
u = machine(); canary(u); heap[0] = HEAP
ev = OBJ + 0x100
u.mem_write(ev, bytes(0x38)); wq(u, ev + 0x20, 7)
name = 'mymod:bell'.encode('utf-16-le')
u.mem_write(OBJ + 0x400, name + b'\0\0')
call(u, 0x67840, (ev, OBJ + 0x400, 10), stubs=STR)
check(q(u, ev + 0x18) == 10, 'wstring size 10')
check(q(u, ev + 0x20) >= 10, 'wstring capacity grown')
buf = q(u, ev + 8)
check(HEAP <= buf < HEAP + 0x100000 and bytes(u.mem_read(buf, 22)) == name + b'\0\0', 'wstring heap buffer holds the UTF-16 key')
# short names stay in the inline buffer (+8)
ev2 = OBJ + 0x200
u.mem_write(ev2, bytes(0x38)); wq(u, ev2 + 0x20, 7)
u.mem_write(OBJ + 0x500, 'a:b'.encode('utf-16-le') + b'\0\0')
call(u, 0x67840, (ev2, OBJ + 0x500, 3), stubs=STR)
check(q(u, ev2 + 0x18) == 3 and bytes(u.mem_read(ev2 + 8, 8)) == 'a:b'.encode('utf-16-le') + b'\0\0', 'short wstring inline at +8')

# 3. SoundEvent::register 0x6672c0
wd(u, ev + 0x28, 0xffffffff); u.mem_write(ev + 0x2c, struct.pack('<fI', 16.0, 1))
wd(u, 0x1430568, 600)
ins = []
def insert(m):
    ins.append((m.reg_read(UC_X86_REG_RDI), m.reg_read(UC_X86_REG_RSI) & 0xffffffff, m.reg_read(UC_X86_REG_RCX)))
    return 0
r, seen = call(u, 0x6672c0, (ev,), stubs={**STR, 0xcda2f0: lambda m: m.reg_read(UC_X86_REG_RDI),
                                        0xcda0b0: lambda m: m.reg_read(UC_X86_REG_RDI), 0xcdab00: lambda m: 0, 0x6673d0: insert})
check(r == ev, 'register returns the event')
check(d(u, ev + 0x28) == 600, 'id = counter [0x1430568]')
check(d(u, 0x1430568) == 601, 'counter incremented')
check(ins == [(0x1430570, 600, ev)], 'map insert (registry 0x1430570, id, rl, ev)')
check(struct.unpack('<f', u.mem_read(ev + 0x2c, 4))[0] == 16.0 and d(u, ev + 0x30) == 1, 'range/type untouched')

# 4. stock play with a null SoundEvent returns before Miles (+0x240 never read)
u = machine(); canary(u)
eng = OBJ
u.mem_write(eng, bytes(0x300)); wq(u, eng, 0x12ffc90)
touched = []
r, seen = call(u, 0x7c2a80, (eng, 0, 0), stubs={0xe6cfd0: alloc, 0x63c4c0: lambda m: touched.append(1)})
check(not touched, 'stock play(null event) returns early')

print(f'audio195 unicorn: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
