"""Pin every RVA/offset audio195 relies on against the retail 1.95 image (static, capstone)."""
import hashlib, re, struct, sys
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
SHA = 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f'
HDR = Path(__file__).resolve().parent.parent / 'audio195.h'
d = IMAGE.read_bytes()
checks, fails = 0, []


def check(c, what):
    global checks
    checks += 1
    if not c:
        fails.append(what); print('FAIL', what)


check(hashlib.sha256(d).hexdigest() == SHA, 'image sha256')
phoff = struct.unpack_from('<Q', d, 0x20)[0]; phnum = struct.unpack_from('<H', d, 0x38)[0]
segs = []
for i in range(phnum):
    t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', d, phoff + i * 56)
    if fs and t in (1, 0x61000010):
        segs.append((va, off, fs))


def v2o(v):
    for va, off, fs in segs:
        if va <= v < va + fs:
            return off + v - va
    raise ValueError(hex(v))


def q(v): return struct.unpack_from('<Q', d, v2o(v))[0]
def cstr(v):
    o = v2o(v); return d[o:d.index(b'\0', o)].decode('latin1')


cs = Cs(CS_ARCH_X86, CS_MODE_64)
def ins(a, n=0x40):
    return [(i.address, i.mnemonic, i.op_str) for i in cs.disasm(d[v2o(a):v2o(a) + n], a)]
def text(a, n=0x40): return '\n'.join(f'{x:x} {m} {o}' for x, m, o in ins(a, n))


hdr = HDR.read_text()
def H(name): return int(re.search(rf'#define {name}\s+(0x[0-9a-f]+|\d+)u', hdr).group(1), 0)

# SoundEngine class, vtable, slots
vt = H('A195_RVA_SE_VTABLE')
check(cstr(q(q(vt - 8) + 8)) == '11SoundEngine' and q(vt - 16) == 0, 'RTTI 11SoundEngine behind 0x12ffc90, offset-to-top 0')
n = 0
while 0x1000 < q(vt + 8 * n) < 0x10c4000: n += 1
check(n == H('A195_SE_SLOTS') == 22, f'22 SoundEngine slots (found {n})')
check(q(vt + 8 * H('A195_SLOT_PLAY')) == H('A195_RVA_PLAY') == 0x7c2a80, 'slot +0x28 play 0x7c2a80')
check(q(vt + 8 * H('A195_SLOT_PLAYUI')) == H('A195_RVA_PLAYUI') == 0x7c2c10, 'slot +0x38 playUI 0x7c2c10')
check('lea rax, [rip + 0xb3d4ca]' in text(0x7c27af, 8), 'ctor 0x7c27a0 sets vptr 0x12ffc80+0x10')
t = text(0xc72100, 0x20)
check('call 0x7c27a0' in t and 'mov qword ptr [rbx + 0x298], r13' in t, 'Minecraft+0x298 = new SoundEngine (0xc72106/0xc72114)')
# play: impl at +0x240, queue via impl vtable +0x20, null event early return, inst bookkeeping
t = text(0x7c2a80, 0x170)
check('test rbx, rbx' in t and 'je 0x7c2be4' in t, 'play returns early for a null SoundEvent')
check('mov rdi, qword ptr [r15 + 0x240]' in t and 'call qword ptr [rax + 0x20]' in t, 'play queues through [SoundEngine+0x240] vtable +0x20')
check('call 0xd2ba20' in t and 'mov dword ptr [r14 + 0xc], 0' in t, 'play marks inst (0xd2ba20(inst,1), inst+0xc = 0)')
for r in ('xmm0', 'xmm1', 'xmm2', 'xmm3', 'xmm4'):
    check(f'vmovss dword ptr [rbp - ' in t and r in t, f'play takes float {r}')
# driver: opened 48000 Hz / 16-bit / 2 channels, stored at impl+0x10
t = text(0xdf7dee, 0x20)
check('mov edi, 0xbb80' in t and 'mov esi, 0x10' in t and 'mov edx, 2' in t and 'call 0xe1f810' in t, 'AIL_open_digital_driver(48000,16,2)')
check('mov qword ptr [rbx + 0x10], rax' in text(0xdf7e0a, 8), 'HDIGDRIVER at impl+0x10')
# volume / mute / listener
t = text(0x7c1e2b, 0x28)
check('[r13 + 0x13c]' in t and 'cmp dword ptr [r13 + 0x134], 0' in t and '[r13 + 0x138]' in t, 'UI volume +0x13c, mute +0x134, world volume +0x138')
t = text(0x7c224e, 0x48)
for off in ('0x154', '0x158', '0x15c', '0x160', '0x168'):
    check(f'[rbx + {off}]' in t, f'listener 0 field +{off}')
check('mov dword ptr [rbx + 0x164], 0' in t, 'listener forward y = 0')
# Miles wrappers: each one logs its own name (format string) - the identification of every AIL address
names = {'0xe1f970': 'AIL_allocate_sample_handle', '0xe1fe30': 'AIL_set_sample_file', '0xe20c50': 'AIL_set_sample_volume_levels',
         '0xe205a0': 'AIL_set_sample_playback_rate_factor', '0xe20360': 'AIL_start_sample', '0xe20f30': 'AIL_sample_status',
         '0xe204b0': 'AIL_end_sample', '0xe22110': 'AIL_WAV_info'}
for a, nm in names.items():
    a = int(a, 16); found = False
    for x, m, o in ins(a, 0x80):
        mm = re.match(r'rdi, \[rip \+ (0x[0-9a-f]+)\]', o)
        if m == 'lea' and mm:
            s = cstr(x + int(mm.group(1), 16) + 7)
            if s.startswith('@' + nm + '('): found = True; break
    check(found, f'{nm} at {a:#x} (its log format string)')
check('%ld)' in cstr(0xf315d4), 'AIL_set_sample_file(handle, image, block) 3 args')
check(cstr(0xf3198d) == '@AIL_set_sample_volume_levels(0x%lX,%f,%f)', 'volume levels (handle, left, right)')
# set_sample_file parses RIFF/WAVE PCM through AIL_WAV_info
check('call 0xe22110' in text(0xe256ca, 0x10), 'AIL_set_sample_file -> AIL_WAV_info for RIFF images')
# SoundEvent construction and registration
t = text(0x6672c0, 0xe0)
check('[rip + 0xdc9286]' in t and 'mov dword ptr [r15 + 0x28], eax' in t, '0x6672c0 stores the counter 0x1430568 into ev+0x28')
check('[rip + 0xdc91f8]' in t and 'call 0x6673d0' in t and 'inc dword ptr [r13]' in t, '0x6672c0 inserts into 0x1430570 and increments the counter')
t = text(0x63c54b, 0x98)
check('mov edi, 0x38' in t and 'call 0xe6cfd0' in t, 'stock SoundEvent = operator new(0x38)')
check('mov dword ptr [rbx + 0x28], 0xffffffff' in t and 'movabs r15, 0x141800000' in t and 'mov qword ptr [rbx + 0x2c], r15' in t, 'stock ev +0x28 -1, +0x2c 16.0f, +0x30 1')
check('call 0x6672c0' in t, 'stock static ctor registers via 0x6672c0')
check(d[v2o(0xeaf600):v2o(0xeaf600) + 8] == 'back'.encode('utf-16-le'), 'SoundEvent names are UTF-16 (wchar_t 2 bytes)')
print(f'audio195 image pins: {checks} checks, {len(fails)} failures')
sys.exit(1 if fails else 0)
