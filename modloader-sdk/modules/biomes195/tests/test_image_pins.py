"""Pin every 1.95 RVA, layout and instruction biomes195 relies on against the analysis image."""
import re, struct, sys
from pathlib import Path
import hashlib
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OPT_SYNTAX_ATT

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
img = IMAGE.read_bytes()
checks = fails = 0


def check(c, what):
    global checks, fails
    checks += 1
    if not c:
        fails += 1
        print('FAIL', what)


phoff = struct.unpack_from('<Q', img, 0x20)[0]
segs = []
for i in range(struct.unpack_from('<H', img, 0x38)[0]):
    t, fl, off, va, pa, fs, ms, al = struct.unpack_from('<IIQQQQQQ', img, phoff + i * 56)
    segs.append((t, fl, off, va, fs, ms))
LOADS = [s for s in segs if s[4] and s[0] in (1, 0x61000010)]


def v2o(v):
    for t, fl, off, va, fs, ms in LOADS:
        if va <= v < va + fs: return off + v - va
    raise KeyError(hex(v))


def q(v): return struct.unpack_from('<Q', img, v2o(v))[0]


cs = Cs(CS_ARCH_X86, CS_MODE_64); cs.syntax = CS_OPT_SYNTAX_ATT


def ins(v):
    i = next(cs.disasm(img[v2o(v):v2o(v) + 16], v))
    return (i.mnemonic + ' ' + i.op_str).replace(' ', '')


def pin(v, text):
    got = ins(v)
    check(got == text.replace(' ', ''), f'{v:#x}: expected {text!r}, got {got!r}')


def rtti(vt):
    ti = q(vt - 8); nm = q(ti + 8); o = v2o(nm)
    return img[o:img.index(b'\0', o)].decode()


check(hashlib.sha256(img).hexdigest() == 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f', 'image hash')

# Biome array, base ctor, props layout
pin(0x840aa, 'leaq 0x1385f0f(%rip), %rcx')                 # &biomes[0] = 0x1409fc0
check(0x840aa + 7 + 0x1385f0f == 0x1409fc0, 'biome array RVA')
pin(0x840cd, 'movq %r13, (%rcx,%rax,8)')                    # biomes[id] = this
pin(0x83ed2, 'movl %esi, 0x68(%r13)')                       # id
pin(0x8405c, 'vmovups 0x28(%r12), %xmm0')                   # props depth/scale/temp/rain -> +0x10
pin(0x84066, 'vmovups %xmm0, 0x10(%r13)')
pin(0x8406c, 'movb 0x3c(%r12), %al'); pin(0x84071, 'movb %al, 0x20(%r13)')
pin(0x84075, 'movb 0x3d(%r12), %al'); pin(0x8407a, 'movb %al, 0x21(%r13)')
pin(0x8407e, 'movl 0x40(%r12), %eax'); pin(0x84083, 'movl %eax, 0x24(%r13)')
pin(0x84057, 'callq 0x67a10')                               # name wstring copy into +0x28
pin(0x8408f, 'jb 0x8409b'); pin(0x8409e, 'callq 0xe6cfb0')  # props freed by the ctor
pin(0x83fd1, 'movl %eax, 0x50(%r13)'); pin(0x83fe5, 'movl %eax, 0x54(%r13)')   # top block id/data
pin(0x83ffc, 'movl %eax, 0x58(%r13)'); pin(0x84025, 'movl %eax, 0x5c(%r13)')   # filler block id/data
pin(0x8414d, 'movq %rbx, 0x60(%r13)')                       # decorator
pin(0x840bf, 'movl $0, 0x190(%r13)')

# PlainsBiome
pin(0x53d15b, 'movl $0xb, %edx'); pin(0x53d16a, 'callq 0x83e60')
pin(0x53d16f, 'leaq 0xd65a2a(%rip), %rax'); check(0x53d16f + 7 + 0xd65a2a + 0x10 == 0x12a2bb0, 'PlainsBiome vptr')
pin(0x53d182, 'movb %r14b, 0x1d0(%rbx)')
check(rtti(0x12a2bb0) == '11PlainsBiome', 'PlainsBiome RTTI')
check(all(q(0x12a2bb0 + 8 * i) for i in range(27)) and q(0x12a2bb0 + 8 * 27) == 0, 'PlainsBiome vtable 27 slots')
SLOTS = {5: 0x85120, 12: 0x85450, 13: 0x854b0, 21: 0x85a80, 19: 0x85990, 11: 0x53d4c0}
for s, f in SLOTS.items():
    check(q(0x12a2bb0 + 8 * s) == f, f'PlainsBiome slot {s} = {f:#x}')
pin(0x85a96, 'movl 0x24(%rbx), %edx'); pin(0x85a99, 'movl 0x68(%rbx), %esi')   # water: by id, parent fallback
pin(0x851a5, 'movl 0x194(%rbx), %esi')                       # sky colour enum
# static init: plains = new PlainsBiome(1, false, props) -> Biome::plains 0x140a810, then its colour fields
pin(0x80434, 'callq 0x53d140'); pin(0x80439, 'leaq 0x138a3d0(%rip), %rbx'); check(0x80439 + 7 + 0x138a3d0 == 0x140a810, 'plains global')
pin(0x8046f, 'movl $0x5c, 0x198(%r14)'); pin(0x8047a, 'movq %rax, 8(%r14)'); pin(0x8047e, 'movq %rcx, 0x190(%r14)')
pin(0x7f96b, 'callq 0x67840')                                # props name built with wstring assign
pin(0x7f9b0, 'movl $0xffffff, 0x38(%rbx)'); pin(0x7f9b7, 'movw $0x100, 0x3c(%rbx)'); pin(0x7f9bd, 'movl $0xffffffff, 0x40(%rbx)')

# BiomeInitLayer
check(rtti(0x12163b8) == '14BiomeInitLayer', 'BiomeInitLayer RTTI')
check(q(0x12163b8 + 0x20) == 0x8a290, 'BiomeInitLayer slot +0x20 = getArea 0x8a290')
pin(0x89ef1, 'leaq 0x118c4b0(%rip), %rax')
for off, n, at in ((0x38, 6, 0x89ff8), (0x48, 6, 0x8a0bd), (0x58, 4, 0x8a100), (0x68, 4, 0x8a127)):
    pin(at, f'movq %rax, {off:#x}(%r13)')
pin(0x89ffc, 'movl $6, 0x40(%r13)'); pin(0x8a0c1, 'movl $6, 0x50(%r13)')
pin(0x8a104, 'movl $4, 0x60(%r13)'); pin(0x8a12b, 'movl $4, 0x70(%r13)')
check(img[v2o(0x89fd0):v2o(0x89fd0) + 5] == b'\xe8' + struct.pack('<i', 0xe6cfc0 - 0x89fd5), 'pools allocated with new[] 0xe6cfc0')
for at, ln, p in ((0x8a3c9, 0x40, 0x38), (0x8a3df, 0x50, 0x48), (0x8a3f5, 0x60, 0x58), (0x8a3b3, 0x70, 0x68)):
    pin(at, f'movl {ln:#x}(%r14), %esi')                     # length read first ...
for at, p in ((0x8a3d5, 0x38), (0x8a3eb, 0x48), (0x8a401, 0x58), (0x8a3bf, 0x68)):
    pin(at, f'movq {p:#x}(%r14), %rcx')                      # ... then the pointer
pin(0x8a40e, 'movl 0x68(%rax), %eax')                        # -> Biome id
pin(0x8a1c7, 'movq 0x38(%rbx), %rdi'); pin(0x8a1d0, 'callq 0xe6cfa0')   # dtor: delete[] pools
pin(0x3e27f3, 'callq 0x89ec0')                               # Layer::getDefaultLayers
pin(0x8d507, 'callq 0x3e0d20')                               # BiomeSource ctor
pin(0x1fd893, 'callq 0x8d750')                               # Dimension::init -> new BiomeSource(level)
# the getArea slot is RELRO data (not segment 0), referenced once (+ its relocation)
relro = [s for s in segs if s[0] == 0x61000010][0]
check(relro[3] <= 0x12163d8 < relro[3] + relro[5] and not (relro[1] & 2), 'slot 0x12163d8 lies in PT_SCE_RELRO (R)')
text = [s for s in segs if s[0] == 1 and s[1] & 1][0]
check(not (text[3] <= 0x12163d8 < text[3] + text[5]), 'slot is outside segment 0 (game_text_sha256)')
hits = [m.start() for m in re.finditer(re.escape(struct.pack('<Q', 0x8a290)), img)]
check(len(hits) == 2 and v2o(0x12163d8) in hits, 'getArea referenced only by its vtable slot + relocation')
blob = img[text[2]:text[2] + text[4]]
calls = [m.start() for m in re.finditer(b'[\xe8\xe9]', blob)
         if m.start() + 5 <= len(blob) and struct.unpack_from('<i', blob, m.start() + 1)[0] == 0x8a290 - (text[3] + m.start() + 5)]
check(calls == [], 'no direct call to getArea (only virtual dispatch)')

# mutation / getBiome guards that make the free id ranges safe
pin(0x5b874e, 'cmpl $0x7f, %r13d'); pin(0x5b875d, 'subl $-0x80, %ebx'); pin(0x5b8762, 'callq 0x859e0')
pin(0x859e0, 'cmpl $0x100, %edi')
pin(0x3e45d0, 'cmpl $0x31, %edi')                            # isOcean only for ids <= 49

print(f'test_image_pins: {checks} checks, {fails} failures')
sys.exit(1 if fails else 0)
