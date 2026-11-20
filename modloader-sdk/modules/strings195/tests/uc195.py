"""Unicorn harness for the retail 1.95 image (shared by strings195 and enchants195).

Maps the pinned analysis ELF at base 0 (its relocations are already applied
for base 0), runs real retail functions, and stubs ONLY the PLT imports
(0xe6c000..0xe6e800: operator new/delete, wmemcpy, wcslen, atomics, throws).
Any PLT import without a stub stops the run with an error, so a test can
never silently skip native code.

Bridge: `Bridge(machine, lib)` connects a host-compiled SDK module (.so)
to the emulator: the module's memory reads/writes go to emulated memory and
its native calls run the real retail code in Unicorn.
"""
import ctypes, struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UC_HOOK_INSN_INVALID, UcError
from unicorn.x86_const import *
import capstone
from capstone import x86 as csx

IMAGE = Path('/home/codey/minecraft-ps4-research/private-direct-legacy-capture/legacy-195-analysis.elf')
IMAGE_SHA256 = 'c675e6ce601b6457a1c1466b0da3d9ccd7415f6b0db9f8e9da7b6e511816029f'
HEAP, HEAP_SIZE = 0x10000000, 0x4000000
STACK, DONE, GUARD = 0x4000000, 0x5000000, 0x5100000
PLT_LO, PLT_HI = 0xe6c000, 0xe6e800

NEW, DELETE, NEW_ARR, DELETE_ARR = 0xe6cfd0, 0xe6cfb0, 0xe6cfc0, 0xe6cfa0
WMEMCPY, MEMCPY, WCSLEN, STACK_FAIL = 0xe6d120, 0xe6d090, 0xe6d0e0, 0xe6d030
WMEMCHR = 0xe6d190
VSWPRINTF = 0xe6d470      # vswprintf(buf, n, fmt, va_list): %ls %s %d %i %u %x %% supported
DYNAMIC_CAST = 0xe6d020   # __dynamic_cast(ptr, src_ti, dst_ti, hint) (Itanium RTTI walk below)
WSTRING_HASH = 0x68b350   # hash<wstring>(const wstring&): leaf, AVX2-vectorised for size >= 8
ATOMIC_SUB, ATOMIC_ADD = 0xe6ce20, 0xe6ce10   # (int*, v, order) fetch_sub / fetch_add


class Machine:
    def __init__(self):
        img = IMAGE.read_bytes()
        u = Uc(UC_ARCH_X86, UC_MODE_64)
        u.ctl_set_cpu_model(UC_CPU_X86_HASWELL)          # AVX/BMI1 (the PS4 Jaguar has both)
        u.mem_map(0, 0x1500000)
        u.mem_write(0, img[0x4000:0x4000 + 0x10c0354])
        u.mem_write(0x1200000, img[0x10c8000:0x10c8000 + 0x14d928])
        u.mem_write(0x1400000, img[0x1218000:0x1218000 + 0x8ac8])
        for a, n in ((STACK, 0x100000), (DONE, 0x1000), (GUARD, 0x1000), (HEAP, HEAP_SIZE)):
            u.mem_map(a, n)
        u.reg_write(UC_X86_REG_CR4, u.reg_read(UC_X86_REG_CR4) | (1 << 18) | (1 << 9))
        try: u.reg_write(UC_X86_REG_XCR0, 7)
        except Exception: pass
        # stack-guard GOT slot 0x134bae8 -> a readable guard word
        u.mem_write(0x134bae8, struct.pack('<Q', GUARD))
        self.u = u
        self.brk = HEAP
        self.live = {}          # addr -> size (game heap)
        self.freed = []
        self.calls = []         # (rva) of every stubbed import, for audits
        self.depth = 0
        self.stubs = {
            NEW: self._new, NEW_ARR: self._new, DELETE: self._delete, DELETE_ARR: self._delete,
            WMEMCPY: self._wmemcpy, 0xe6d110: self._wmemcpy, MEMCPY: self._memcpy, WCSLEN: self._wcslen,
            ATOMIC_SUB: self._atomic(-1), ATOMIC_ADD: self._atomic(1),
            STACK_FAIL: self._fail('stack_chk_fail'),
            DYNAMIC_CAST: self._dynamic_cast, VSWPRINTF: self._vswprintf, WMEMCHR: self._wmemchr, 0xe6d100: self._wmemcmp, 0xe6d0a0: self._memcpy,   # memmove (read-then-write is overlap-safe)
            0xe6cf70: self._fail('length_error'), 0xe6cf80: self._fail('out_of_range'),
        }
        self.extra = {}
        # Leaf functions replaced by an exact model because their long-input
        # path is VEX.256 code Unicorn cannot run. Each is cross-checked
        # against the real function on inputs that take its scalar path.
        self.replace = {WSTRING_HASH: self._wstring_hash}
        self.ymm_emulated = 0
        self.cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); self.cs.detail = True
        u.hook_add(UC_HOOK_INSN_INVALID, self._invalid)

    # ---- 256-bit AVX: Unicorn's TCG (QEMU 5) has no VEX.256; the retail code
    # uses a handful of zero/copy forms (vmovdqu/vmovups ymm load/store,
    # vxorps/vpxor ymm self-zero, vzeroupper). They are emulated exactly here.
    _CSREG = None
    def _uc_reg(self, csreg):
        name = self.cs.reg_name(csreg).upper()
        return getattr(__import__('unicorn.x86_const', fromlist=['x']), 'UC_X86_REG_' + name)
    def _addr(self, ins, op):
        mem = op.mem; a = mem.disp
        if mem.base:
            a += (ins.address + ins.size) if self.cs.reg_name(mem.base) == 'rip' else self.u.reg_read(self._uc_reg(mem.base))
        if mem.index: a += self.u.reg_read(self._uc_reg(mem.index)) * mem.scale
        return a & 0xffffffffffffffff
    def _invalid(self, u, _ud=None):
        rip = u.reg_read(UC_X86_REG_RIP)
        ins = next(self.cs.disasm(bytes(u.mem_read(rip, 16)), rip), None)
        if ins is None: return False
        ops = ins.operands
        if ins.mnemonic == 'vzeroupper':
            for i in range(16): u.reg_write(UC_X86_REG_YMM0 + i, u.reg_read(UC_X86_REG_XMM0 + i) & ((1 << 128) - 1))
        elif ins.mnemonic in ('vmovdqu', 'vmovups', 'vmovdqa', 'vmovaps') and len(ops) == 2 and (ops[0].size == 32 or ops[1].size == 32):
            dst, src = ops
            if src.type == csx.X86_OP_MEM: val = int.from_bytes(u.mem_read(self._addr(ins, src), 32), 'little')
            else: val = u.reg_read(self._uc_reg(src.reg))
            if dst.type == csx.X86_OP_MEM: u.mem_write(self._addr(ins, dst), val.to_bytes(32, 'little'))
            else: u.reg_write(self._uc_reg(dst.reg), val)
        elif ins.mnemonic in ('vxorps', 'vpxor', 'vxorpd') and len(ops) == 3 and ops[0].size == 32 and \
                ops[1].type == ops[2].type == csx.X86_OP_REG and ops[1].reg == ops[2].reg:
            u.reg_write(self._uc_reg(ops[0].reg), 0)
        else:
            return False
        self.ymm_emulated += 1
        u.reg_write(UC_X86_REG_RIP, rip + ins.size)
        return True

    # ---- memory ----
    def q(self, a): return struct.unpack('<Q', self.u.mem_read(a, 8))[0]
    def d(self, a): return struct.unpack('<I', self.u.mem_read(a, 4))[0]
    def f(self, a): return struct.unpack('<f', self.u.mem_read(a, 4))[0]
    def wq(self, a, v): self.u.mem_write(a, struct.pack('<Q', v & 0xffffffffffffffff))
    def wd(self, a, v): self.u.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def alloc(self, n, fill=0):
        p = self.brk; self.brk += (max(n, 1) + 31) & ~15
        if self.brk > HEAP + HEAP_SIZE: raise MemoryError('emulated heap exhausted')
        self.u.mem_write(p, bytes([fill]) * n); self.live[p] = n
        return p
    def wstr_read(self, p, n=None):
        out = []
        while n is None or len(out) < n:
            c = struct.unpack('<H', self.u.mem_read(p + 2 * len(out), 2))[0]
            if n is None and c == 0: break
            out.append(c)
        return struct.pack(f'<{len(out)}H', *out).decode('utf-16-le', 'surrogatepass')
    def wstr_put(self, s):
        b = s.encode('utf-16-le') + b'\0\0'
        p = self.alloc(len(b)); self.u.mem_write(p, b); return p
    def std_wstring(self, s):
        """A real game wstring (0x28) built by the game's assign 0x67840."""
        w = self.alloc(0x28)
        self.u.mem_write(w, struct.pack('<QQQQQ', 0, 0, 0, 0, 7))
        self.call(0x67840, (w, self.wstr_put(s), len(s)))
        return w
    def wstring_value(self, w):
        size, cap = self.q(w + 0x18), self.q(w + 0x20)
        p = self.q(w + 8) if cap >= 8 else w + 8
        return self.wstr_read(p, size)

    # ---- stubs (PLT imports only) ----
    def _new(self, m): return self.alloc(m.reg_read(UC_X86_REG_RDI))
    def _delete(self, m):
        p = m.reg_read(UC_X86_REG_RDI)
        if p:
            if p not in self.live: raise RuntimeError(f'delete of non-heap pointer {p:#x}')
            del self.live[p]; self.freed.append(p)
        return 0
    def _wmemcpy(self, m):
        dst, src, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
        if n: m.mem_write(dst, bytes(m.mem_read(src, 2 * n)))
        return dst
    def _memcpy(self, m):
        dst, src, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
        if n: m.mem_write(dst, bytes(m.mem_read(src, n)))
        return dst
    def _wmemchr(self, m):
        p, c, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
        c &= 0xffff
        for i in range(n):
            if struct.unpack('<H', m.mem_read(p + 2 * i, 2))[0] == c: return p + 2 * i
        return 0
    @staticmethod
    def wstring_hash_model(units):
        """0x68b350 scalar loop 0x68b4d8..0x68b4ff: sum of c_i*31 ^ (n-1-i) for i < n-1, plus c_{n-1}."""
        n, h = len(units), 0
        for i, c in enumerate(units):
            h = (h + (((c * 31) ^ (n - 1 - i)) if i < n - 1 else c)) & 0xffffffff
        return h
    def _wstring_hash(self, m):
        w = m.reg_read(UC_X86_REG_RDI)
        size, cap = self.q(w + 0x18), self.q(w + 0x20)
        p = self.q(w + 8) if cap >= 8 else w + 8
        units = struct.unpack(f'<{size}H', bytes(m.mem_read(p, 2 * size))) if size else ()
        return self.wstring_hash_model(units)
    def _wmemcmp(self, m):
        a, b, n = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX))
        for i in range(n):
            x = struct.unpack('<H', m.mem_read(a + 2 * i, 2))[0]; y = struct.unpack('<H', m.mem_read(b + 2 * i, 2))[0]
            if x != y: return 1 if x > y else 0xffffffff
        return 0
    def _looks_ti(self, v):
        try:
            nm = self.q(v + 8)
            return 0xe00000 <= nm < 0x1200000 and chr(self.u.mem_read(nm, 1)[0]).isdigit() or \
                   (0xe00000 <= nm < 0x1200000 and self.u.mem_read(nm, 1)[0] == ord('N'))
        except Exception:
            return False
    def _ti_bases(self, ti):
        """(base_ti, offset) pairs: __si_class_type_info (+0x10 base) or __vmi (+0x10 flags, +0x14 n, +0x18 {ti, offset_flags})."""
        v = self.q(ti + 0x10)
        if v == 0: return []
        if self._looks_ti(v): return [(v, 0)]
        n = self.d(ti + 0x14)
        if n > 32: return []
        out = []
        for i in range(n):
            b, of = self.q(ti + 0x18 + 16 * i), self.q(ti + 0x20 + 16 * i)
            if self._looks_ti(b) and not (of & 1): out.append((b, of >> 8))
        return out
    def _dynamic_cast(self, m):
        ptr, dst = m.reg_read(UC_X86_REG_RDI), m.reg_read(UC_X86_REG_RDX)
        if not ptr: return 0
        vt = self.q(ptr)
        ott = struct.unpack('<q', struct.pack('<Q', self.q(vt - 16)))[0]
        full, ti = ptr + ott, self.q(vt - 8)
        todo = [(ti, 0)]
        while todo:
            t, off = todo.pop()
            if t == dst: return full + off
            todo.extend((b, off + o) for b, o in self._ti_bases(t))
        return 0
    def _vswprintf(self, m):
        buf, cap, fmt, va = (m.reg_read(r) for r in (UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX))
        f = self.wstr_read(fmt)
        gp, _fp, over, save = struct.unpack('<IIQQ', bytes(m.mem_read(va, 24)))
        def arg():
            nonlocal gp, over
            if gp < 48: v = self.q(save + gp); gp += 8
            else: v = self.q(over); over += 8
            return v
        out, i = '', 0
        while i < len(f):
            c = f[i]
            if c != '%': out += c; i += 1; continue
            spec = f[i + 1:i + 3]
            if spec == 'ls': out += self.wstr_read(arg()); i += 3
            elif f[i + 1] == 's': out += bytes(iter(lambda p=[arg()]: (m.mem_read(p[0], 1)[0], p.__setitem__(0, p[0] + 1))[0], 0)).decode('latin1'); i += 2
            elif f[i + 1] in 'diux':
                v = arg() & 0xffffffff
                if f[i + 1] in 'di' and v & 0x80000000: v -= 1 << 32
                out += (format(v, 'x') if f[i + 1] == 'x' else str(v)); i += 2
            elif f[i + 1] == '%': out += '%'; i += 2
            else: raise RuntimeError(f'vswprintf: unsupported format {f!r}')
        m.mem_write(va, struct.pack('<II', gp, _fp)); m.mem_write(va + 8, struct.pack('<Q', over))
        if len(out) >= cap: return 0xffffffff
        m.mem_write(buf, out.encode('utf-16-le') + b'\0\0')
        return len(out)
    def _wcslen(self, m):
        p = m.reg_read(UC_X86_REG_RDI); n = 0
        while struct.unpack('<H', m.mem_read(p + 2 * n, 2))[0]: n += 1
        return n
    def _atomic(self, sign):
        def f(m):
            p, v = m.reg_read(UC_X86_REG_RDI), m.reg_read(UC_X86_REG_RSI) & 0xffffffff
            old = struct.unpack('<i', m.mem_read(p, 4))[0]
            m.mem_write(p, struct.pack('<i', old + sign * v))
            return old & 0xffffffff
        return f
    def _fail(self, what):
        def f(m): raise RuntimeError(f'retail code raised {what}')
        return f

    # ---- execution ----
    def call(self, fn, args=(), stack=(), xmm=(), limit=5_000_000):
        u = self.u
        saved = None
        if self.depth:
            saved = {r: u.reg_read(r) for r in (UC_X86_REG_RSP, UC_X86_REG_RIP, UC_X86_REG_RBP, UC_X86_REG_RBX,
                                                UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15)}
        regs = [UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_R8, UC_X86_REG_R9]
        for r, v in zip(regs, args): u.reg_write(r, v & 0xffffffffffffffff)
        for i, v in enumerate(xmm):
            u.reg_write(UC_X86_REG_XMM0 + i, struct.unpack('<I', struct.pack('<f', v))[0])
        sp = STACK + 0xf0000 - 0x20000 * self.depth
        sp &= ~0xf
        if len(stack) % 2: sp -= 8            # keep rsp+8 16-aligned at entry
        for i, v in enumerate(stack): self.wq(sp + 8 + 8 * i, v)
        self.wq(sp, DONE)
        u.reg_write(UC_X86_REG_RSP, sp)
        err = []

        def hook(m, a, size, _):
            if a == DONE: m.emu_stop(); return
            if PLT_LO <= a < PLT_HI or a in self.replace:
                st = self.replace.get(a) or self.extra.get(a) or self.stubs.get(a)
                if not st:
                    err.append(f'unstubbed import {a:#x}'); m.emu_stop(); return
                self.calls.append(a)
                try: rv = st(m)
                except Exception as e: err.append(str(e)); m.emu_stop(); return
                m.reg_write(UC_X86_REG_RAX, (rv or 0) & 0xffffffffffffffff)
                s = m.reg_read(UC_X86_REG_RSP)
                m.reg_write(UC_X86_REG_RIP, self.q(s)); m.reg_write(UC_X86_REG_RSP, s + 8)
        self.depth += 1
        h = u.hook_add(UC_HOOK_CODE, hook)
        try:
            u.emu_start(fn, DONE + 0x10, count=limit)
        except UcError as e:
            err.append(f'{e} at rip={u.reg_read(UC_X86_REG_RIP):#x}')
        finally:
            u.hook_del(h); self.depth -= 1
        rv = u.reg_read(UC_X86_REG_RAX)
        if u.reg_read(UC_X86_REG_RIP) != DONE and not err: err.append(f'did not return (rip={u.reg_read(UC_X86_REG_RIP):#x})')
        if saved:
            for r, v in saved.items(): u.reg_write(r, v)
        if err: raise RuntimeError(f'call {fn:#x}: ' + '; '.join(err))
        return rv


# ---- Localizer builders (real code: 0xd59ed0 language nodes, stock insert sequence) ----
def list_walk(m, head):
    out, p = [], m.q(head)
    while p != head and len(out) < 100: out.append(p); p = m.q(p)
    return out


def new_localizer(m, langs):
    """A Localizer object whose outer language list holds real nodes built by 0xd59ed0."""
    L = m.alloc(0xd8)
    head = m.alloc(0x40)
    m.wq(head, head); m.wq(head + 8, head)
    m.wq(L + 0x10, head); m.wq(L + 0x18, 0)
    nodes = {}
    for name in langs:
        key = m.std_wstring(name)
        ref = m.alloc(8); m.wq(ref, key)
        tup = m.alloc(8)
        before = set(list_walk(m, head))
        m.call(0xd59ed0, (L + 8, 0xf17246, ref, tup))
        new = [n for n in list_walk(m, head) if n not in before]
        assert len(new) == 1
        nodes[name] = new[0]
    first = nodes[langs[0]]
    m.wq(L + 0xa0, first + 0x38); m.wq(L + 0x98, first + 0x88)
    return L, nodes


def stock_insert(m, smap, sid, text):
    """The stock loader's own sequence (0xd38f3f emplace + 0xd38f77 _Insert)."""
    pair = m.alloc(0x30)
    m.u.mem_write(pair, struct.pack('<IIQQQQQ', sid, 0, 0, 0, 0, 0, 7)[:0x30])
    m.call(0x67840, (pair + 8, m.wstr_put(text), len(text)))
    m.call(0xd5a9e0, (smap, pair))
    node = m.q(m.q(smap + 8))
    ret = m.alloc(0x20)
    m.call(0x7ed670, (ret, smap, node + 0x10), stack=(0, 0, node))
    return m.u.mem_read(ret + 0x18, 1)[0]


class Checks:
    def __init__(self, name): self.name, self.n, self.fails = name, 0, 0
    def __call__(self, cond, what):
        self.n += 1
        if not cond: self.fails += 1; print('FAIL', what)
    def done(self):
        print(f'{self.name}: {self.n} checks, {self.fails} failures')
        return 1 if self.fails else 0


# ---- module bridge (host .so built by test.sh) ----
READ_T = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint64)
WRITE_T = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint64)
CALL_T = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.c_uint,
                          ctypes.POINTER(ctypes.c_uint64), ctypes.c_uint)


class Bridge:
    def __init__(self, mach, lib_path):
        self.m = mach
        self.lib = ctypes.CDLL(str(lib_path))
        self.native_calls = []
        self.errors = []

        def rd(a, dst, n):
            try: ctypes.memmove(dst, bytes(mach.u.mem_read(a, n)), n); return 1
            except Exception: return 0
        def wr(a, src, n):
            try: mach.u.mem_write(a, ctypes.string_at(src, n)); return 1
            except Exception: return 0
        def cl(fn, regs, n, stack, ns):
            args = [regs[i] for i in range(n)]
            st = [stack[i] for i in range(ns)]
            self.native_calls.append(fn)
            try: return mach.call(fn, args, st)
            except Exception as e:
                self.errors.append(str(e)); return 0
        self._cb = (READ_T(rd), WRITE_T(wr), CALL_T(cl))
        self.lib.bridge_set(*self._cb)
