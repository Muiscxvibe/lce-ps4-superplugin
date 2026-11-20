"""Pure-Python JVM class-file parser and bytecode decoder (static analysis only).

Nothing here loads or executes Java code: a class file is parsed as bytes
(JVMS chapter 4) and its Code attributes are decoded into instruction lists.
Malformed input raises ClassFormatError; callers treat that as "skip class".
"""
import struct

MAX_CLASS_BYTES = 8 << 20


class ClassFormatError(Exception):
    pass


# ---- constant pool --------------------------------------------------------
CP_UTF8, CP_INT, CP_FLOAT, CP_LONG, CP_DOUBLE, CP_CLASS, CP_STRING = 1, 3, 4, 5, 6, 7, 8
CP_FIELD, CP_METHOD, CP_IMETHOD, CP_NAT, CP_MHANDLE, CP_MTYPE, CP_DYNAMIC, CP_INDY = 9, 10, 11, 12, 15, 16, 17, 18
CP_MODULE, CP_PACKAGE = 19, 20

ACC_PUBLIC, ACC_PRIVATE, ACC_PROTECTED, ACC_STATIC, ACC_FINAL = 0x1, 0x2, 0x4, 0x8, 0x10
ACC_INTERFACE, ACC_ABSTRACT, ACC_SYNTHETIC, ACC_ENUM, ACC_NATIVE = 0x200, 0x400, 0x1000, 0x4000, 0x100


def mutf8(b):
    """Java 'modified UTF-8' -> str (handles 0xC0 0x80 and surrogate pairs)."""
    try:
        return b.decode('utf-8')
    except UnicodeDecodeError:
        pass
    out, i, n = [], 0, len(b)
    while i < n:
        c = b[i]
        if c < 0x80:
            out.append(c); i += 1
        elif c & 0xE0 == 0xC0 and i + 1 < n:
            out.append(((c & 0x1F) << 6) | (b[i + 1] & 0x3F)); i += 2
        elif c & 0xF0 == 0xE0 and i + 2 < n:
            out.append(((c & 0x0F) << 12) | ((b[i + 1] & 0x3F) << 6) | (b[i + 2] & 0x3F)); i += 3
        else:
            out.append(0xFFFD); i += 1
    s, j = [], 0
    while j < len(out):  # combine surrogates
        u = out[j]
        if 0xD800 <= u < 0xDC00 and j + 1 < len(out) and 0xDC00 <= out[j + 1] < 0xE000:
            s.append(chr(0x10000 + ((u - 0xD800) << 10) + (out[j + 1] - 0xDC00))); j += 2
        else:
            s.append(chr(u) if not 0xD800 <= u < 0xE000 else '�'); j += 1
    return ''.join(s)


class ConstantPool:
    def __init__(self, entries):
        self.e = entries  # index -> tuple(tag, ...)

    def __len__(self):
        return len(self.e)

    def tag(self, i):
        x = self.e[i] if 0 < i < len(self.e) else None
        return x[0] if x else None

    def utf8(self, i):
        x = self._get(i, CP_UTF8)
        return x[1]

    def _get(self, i, *tags):
        if not 0 < i < len(self.e) or self.e[i] is None:
            raise ClassFormatError(f'bad constant pool index {i}')
        x = self.e[i]
        if tags and x[0] not in tags:
            raise ClassFormatError(f'constant {i}: tag {x[0]} not in {tags}')
        return x

    def class_name(self, i):
        if i == 0:
            return None
        return self.utf8(self._get(i, CP_CLASS)[1])

    def nat(self, i):
        x = self._get(i, CP_NAT)
        return self.utf8(x[1]), self.utf8(x[2])

    def member_ref(self, i):
        """Fieldref/Methodref/InterfaceMethodref -> (owner, name, desc)."""
        x = self._get(i, CP_FIELD, CP_METHOD, CP_IMETHOD)
        n, d = self.nat(x[2])
        return self.class_name(x[1]), n, d

    def method_handle(self, i):
        x = self._get(i, CP_MHANDLE)
        owner, name, desc = self.member_ref(x[2])
        return x[1], owner, name, desc  # (ref kind, owner, name, desc)

    def loadable(self, i):
        """ldc operand -> ('int'|'float'|'long'|'double'|'str'|'class'|'mtype'|'mhandle'|'dynamic', value)."""
        x = self._get(i)
        t = x[0]
        if t == CP_INT:
            return 'int', x[1]
        if t == CP_FLOAT:
            return 'float', x[1]
        if t == CP_LONG:
            return 'long', x[1]
        if t == CP_DOUBLE:
            return 'double', x[1]
        if t == CP_STRING:
            return 'str', self.utf8(x[1])
        if t == CP_CLASS:
            return 'class', self.utf8(x[1])
        if t == CP_MTYPE:
            return 'mtype', self.utf8(x[1])
        if t == CP_MHANDLE:
            return 'mhandle', self.method_handle(i)
        if t == CP_DYNAMIC:
            return 'dynamic', self.nat(x[2])
        raise ClassFormatError(f'constant {i} (tag {t}) is not loadable')

    def strings(self):
        """All CONSTANT_String values (cheap prefilter / texture-name search)."""
        out = []
        for x in self.e:
            if x and x[0] == CP_STRING:
                try:
                    out.append(self.utf8(x[1]))
                except ClassFormatError:
                    pass
        return out

    def class_refs(self):
        out = set()
        for x in self.e:
            if x and x[0] == CP_CLASS:
                try:
                    out.add(self.utf8(x[1]))
                except ClassFormatError:
                    pass
        return out

    def member_refs(self):
        out = []
        for i, x in enumerate(self.e):
            if x and x[0] in (CP_FIELD, CP_METHOD, CP_IMETHOD):
                try:
                    out.append((x[0],) + self.member_ref(i))
                except ClassFormatError:
                    pass
        return out


class Reader:
    __slots__ = ('b', 'p')

    def __init__(self, b, p=0):
        self.b, self.p = b, p

    def take(self, n):
        if n < 0 or self.p + n > len(self.b):
            raise ClassFormatError('truncated class file')
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def u1(self):
        return self.take(1)[0]

    def u2(self):
        return struct.unpack('>H', self.take(2))[0]

    def u4(self):
        return struct.unpack('>I', self.take(4))[0]


class Attribute:
    __slots__ = ('name', 'data')

    def __init__(self, name, data):
        self.name, self.data = name, data


class Code:
    def __init__(self, max_stack, max_locals, code, handlers, attrs):
        self.max_stack, self.max_locals, self.code = max_stack, max_locals, code
        self.handlers = handlers  # [(start, end, handler, catch_type)]
        self.attrs = attrs
        self._insns = None

    def insns(self, cp):
        if self._insns is None:
            self._insns = decode(self.code, cp)
        return self._insns


class Member:
    def __init__(self, access, name, desc, attrs, cp):
        self.access, self.name, self.desc, self.attrs = access, name, desc, attrs
        self.code = None
        self.annotations = []
        self.signature = None
        for a in attrs:
            if a.name == 'Code':
                self.code = parse_code(a.data, cp)
            elif a.name in ('RuntimeVisibleAnnotations', 'RuntimeInvisibleAnnotations'):
                self.annotations += parse_annotations(a.data, cp)
            elif a.name == 'Signature' and len(a.data) == 2:
                self.signature = cp.utf8(struct.unpack('>H', a.data)[0])

    @property
    def is_static(self):
        return bool(self.access & ACC_STATIC)

    def __repr__(self):
        return f'<{self.name}{self.desc}>'


class ClassFile:
    def __init__(self, data):
        if len(data) > MAX_CLASS_BYTES:
            raise ClassFormatError('class file too large')
        r = Reader(data)
        if r.u4() != 0xCAFEBABE:
            raise ClassFormatError('bad magic')
        self.minor, self.major = r.u2(), r.u2()
        n = r.u2()
        entries = [None] * n
        i = 1
        while i < n:
            tag = r.u1()
            if tag == CP_UTF8:
                ln = r.u2()
                entries[i] = (tag, mutf8(r.take(ln)))
            elif tag == CP_INT:
                entries[i] = (tag, struct.unpack('>i', r.take(4))[0])
            elif tag == CP_FLOAT:
                entries[i] = (tag, struct.unpack('>f', r.take(4))[0])
            elif tag == CP_LONG:
                entries[i] = (tag, struct.unpack('>q', r.take(8))[0]); i += 1
            elif tag == CP_DOUBLE:
                entries[i] = (tag, struct.unpack('>d', r.take(8))[0]); i += 1
            elif tag in (CP_CLASS, CP_STRING, CP_MTYPE, CP_MODULE, CP_PACKAGE):
                entries[i] = (tag, r.u2())
            elif tag in (CP_FIELD, CP_METHOD, CP_IMETHOD, CP_NAT, CP_DYNAMIC, CP_INDY):
                entries[i] = (tag, r.u2(), r.u2())
            elif tag == CP_MHANDLE:
                entries[i] = (tag, r.u1(), r.u2())
            else:
                raise ClassFormatError(f'unknown constant tag {tag} at {i}')
            i += 1
        self.cp = cp = ConstantPool(entries)
        self.access = r.u2()
        self.name = cp.class_name(r.u2())
        self.super_name = cp.class_name(r.u2())
        self.interfaces = [cp.class_name(r.u2()) for _ in range(r.u2())]
        self.fields = [self._member(r) for _ in range(r.u2())]
        self.methods = [self._member(r) for _ in range(r.u2())]
        self.attrs = self._attrs(r)
        self.bootstrap = []
        self.annotations = []
        self.signature = None
        self.inner = []
        for a in self.attrs:
            if a.name == 'BootstrapMethods':
                br = Reader(a.data)
                for _ in range(br.u2()):
                    ref = br.u2()
                    args = [br.u2() for _ in range(br.u2())]
                    self.bootstrap.append((ref, args))
            elif a.name in ('RuntimeVisibleAnnotations', 'RuntimeInvisibleAnnotations'):
                self.annotations += parse_annotations(a.data, cp)
            elif a.name == 'Signature' and len(a.data) == 2:
                self.signature = cp.utf8(struct.unpack('>H', a.data)[0])

    def _attrs(self, r):
        out = []
        for _ in range(r.u2()):
            name = self.cp.utf8(r.u2())
            out.append(Attribute(name, r.take(r.u4())))
        return out

    def _member(self, r):
        access, name, desc = r.u2(), self.cp.utf8(r.u2()), self.cp.utf8(r.u2())
        return Member(access, name, desc, self._attrs(r), self.cp)

    def method(self, name, desc=None):
        for m in self.methods:
            if m.name == name and (desc is None or m.desc == desc):
                return m
        return None

    def bootstrap_info(self, idx):
        """invokedynamic bootstrap index -> (bsm (kind, owner, name, desc), [loadable args])."""
        if not 0 <= idx < len(self.bootstrap):
            raise ClassFormatError('bad bootstrap index')
        ref, args = self.bootstrap[idx]
        return self.cp.method_handle(ref), [self.cp.loadable(a) for a in args]

    @property
    def is_interface(self):
        return bool(self.access & ACC_INTERFACE)

    @property
    def is_enum(self):
        return bool(self.access & ACC_ENUM)

    def __repr__(self):
        return f'<class {self.name} extends {self.super_name}>'


def parse_code(data, cp):
    r = Reader(data)
    max_stack, max_locals = r.u2(), r.u2()
    code = r.take(r.u4())
    handlers = []
    for _ in range(r.u2()):
        s, e, h, t = r.u2(), r.u2(), r.u2(), r.u2()
        handlers.append((s, e, h, cp.class_name(t) if t else None))
    attrs = []
    for _ in range(r.u2()):
        name = cp.utf8(r.u2())
        attrs.append(Attribute(name, r.take(r.u4())))
    return Code(max_stack, max_locals, code, handlers, attrs)


def parse_annotations(data, cp):
    """-> [(type_descriptor, {element: value})] ; values are simplified."""
    r = Reader(data)
    out = []
    try:
        for _ in range(r.u2()):
            out.append(_annotation(r, cp))
    except (ClassFormatError, IndexError):
        pass
    return out


def _annotation(r, cp):
    t = cp.utf8(r.u2())
    vals = {}
    for _ in range(r.u2()):
        k = cp.utf8(r.u2())
        vals[k] = _elem(r, cp)
    return t, vals


def _elem(r, cp):
    tag = chr(r.u1())
    if tag in 'BCDFIJSZs':
        i = r.u2()
        x = cp.e[i] if 0 < i < len(cp.e) else None
        return x[1] if x else None
    if tag == 'e':
        return (cp.utf8(r.u2()), cp.utf8(r.u2()))
    if tag == 'c':
        return cp.utf8(r.u2())
    if tag == '@':
        return _annotation(r, cp)
    if tag == '[':
        return [_elem(r, cp) for _ in range(r.u2())]
    raise ClassFormatError(f'bad annotation element tag {tag!r}')


# ---- descriptors ----------------------------------------------------------
def parse_method_desc(desc):
    """'(ILjava/lang/String;[F)V' -> (['I', 'Ljava/lang/String;', '[F'], 'V')."""
    if not desc.startswith('('):
        raise ClassFormatError(f'bad method descriptor {desc!r}')
    i, args = 1, []
    while desc[i] != ')':
        j = i
        while desc[j] == '[':
            j += 1
        if desc[j] == 'L':
            j = desc.index(';', j)
        args.append(desc[i:j + 1])
        i = j + 1
    return args, desc[i + 1:]


def desc_class(d):
    """'Lnet/x/Y;' -> 'net/x/Y'; arrays/primitives -> None."""
    return d[1:-1] if d.startswith('L') and d.endswith(';') else None


def slot_size(d):
    return 2 if d in ('J', 'D') else 1


# ---- opcodes ----------------------------------------------------------------
# operand formats: '' none, 'b' s1, 'B' u1 local, 'h' s2, 'C' u1 cp, 'H' u2 cp,
# 'j' s2 branch, 'J' s4 branch, plus special handling for the rest.
_OPS = [
    ('nop', ''), ('aconst_null', ''), ('iconst_m1', ''), ('iconst_0', ''), ('iconst_1', ''), ('iconst_2', ''), ('iconst_3', ''),
    ('iconst_4', ''), ('iconst_5', ''), ('lconst_0', ''), ('lconst_1', ''), ('fconst_0', ''), ('fconst_1', ''), ('fconst_2', ''),
    ('dconst_0', ''), ('dconst_1', ''), ('bipush', 'b'), ('sipush', 'h'), ('ldc', 'C'), ('ldc_w', 'H'), ('ldc2_w', 'H'),
    ('iload', 'B'), ('lload', 'B'), ('fload', 'B'), ('dload', 'B'), ('aload', 'B'),
    ('iload_0', ''), ('iload_1', ''), ('iload_2', ''), ('iload_3', ''), ('lload_0', ''), ('lload_1', ''), ('lload_2', ''), ('lload_3', ''),
    ('fload_0', ''), ('fload_1', ''), ('fload_2', ''), ('fload_3', ''), ('dload_0', ''), ('dload_1', ''), ('dload_2', ''), ('dload_3', ''),
    ('aload_0', ''), ('aload_1', ''), ('aload_2', ''), ('aload_3', ''),
    ('iaload', ''), ('laload', ''), ('faload', ''), ('daload', ''), ('aaload', ''), ('baload', ''), ('caload', ''), ('saload', ''),
    ('istore', 'B'), ('lstore', 'B'), ('fstore', 'B'), ('dstore', 'B'), ('astore', 'B'),
    ('istore_0', ''), ('istore_1', ''), ('istore_2', ''), ('istore_3', ''), ('lstore_0', ''), ('lstore_1', ''), ('lstore_2', ''), ('lstore_3', ''),
    ('fstore_0', ''), ('fstore_1', ''), ('fstore_2', ''), ('fstore_3', ''), ('dstore_0', ''), ('dstore_1', ''), ('dstore_2', ''), ('dstore_3', ''),
    ('astore_0', ''), ('astore_1', ''), ('astore_2', ''), ('astore_3', ''),
    ('iastore', ''), ('lastore', ''), ('fastore', ''), ('dastore', ''), ('aastore', ''), ('bastore', ''), ('castore', ''), ('sastore', ''),
    ('pop', ''), ('pop2', ''), ('dup', ''), ('dup_x1', ''), ('dup_x2', ''), ('dup2', ''), ('dup2_x1', ''), ('dup2_x2', ''), ('swap', ''),
    ('iadd', ''), ('ladd', ''), ('fadd', ''), ('dadd', ''), ('isub', ''), ('lsub', ''), ('fsub', ''), ('dsub', ''),
    ('imul', ''), ('lmul', ''), ('fmul', ''), ('dmul', ''), ('idiv', ''), ('ldiv', ''), ('fdiv', ''), ('ddiv', ''),
    ('irem', ''), ('lrem', ''), ('frem', ''), ('drem', ''), ('ineg', ''), ('lneg', ''), ('fneg', ''), ('dneg', ''),
    ('ishl', ''), ('lshl', ''), ('ishr', ''), ('lshr', ''), ('iushr', ''), ('lushr', ''), ('iand', ''), ('land', ''),
    ('ior', ''), ('lor', ''), ('ixor', ''), ('lxor', ''), ('iinc', 'iinc'),
    ('i2l', ''), ('i2f', ''), ('i2d', ''), ('l2i', ''), ('l2f', ''), ('l2d', ''), ('f2i', ''), ('f2l', ''), ('f2d', ''),
    ('d2i', ''), ('d2l', ''), ('d2f', ''), ('i2b', ''), ('i2c', ''), ('i2s', ''),
    ('lcmp', ''), ('fcmpl', ''), ('fcmpg', ''), ('dcmpl', ''), ('dcmpg', ''),
    ('ifeq', 'j'), ('ifne', 'j'), ('iflt', 'j'), ('ifge', 'j'), ('ifgt', 'j'), ('ifle', 'j'),
    ('if_icmpeq', 'j'), ('if_icmpne', 'j'), ('if_icmplt', 'j'), ('if_icmpge', 'j'), ('if_icmpgt', 'j'), ('if_icmple', 'j'),
    ('if_acmpeq', 'j'), ('if_acmpne', 'j'), ('goto', 'j'), ('jsr', 'j'), ('ret', 'B'),
    ('tableswitch', 'tswitch'), ('lookupswitch', 'lswitch'),
    ('ireturn', ''), ('lreturn', ''), ('freturn', ''), ('dreturn', ''), ('areturn', ''), ('return', ''),
    ('getstatic', 'H'), ('putstatic', 'H'), ('getfield', 'H'), ('putfield', 'H'),
    ('invokevirtual', 'H'), ('invokespecial', 'H'), ('invokestatic', 'H'), ('invokeinterface', 'iface'), ('invokedynamic', 'indy'),
    ('new', 'H'), ('newarray', 'u1'), ('anewarray', 'H'), ('arraylength', ''), ('athrow', ''),
    ('checkcast', 'H'), ('instanceof', 'H'), ('monitorenter', ''), ('monitorexit', ''), ('wide', 'wide'),
    ('multianewarray', 'multi'), ('ifnull', 'j'), ('ifnonnull', 'j'), ('goto_w', 'J'), ('jsr_w', 'J'),
]
OPNAMES = [n for n, _ in _OPS]
OPFMT = [f for _, f in _OPS]
OPCODE = {n: i for i, n in enumerate(OPNAMES)}
assert len(_OPS) == 202 and OPCODE['jsr_w'] == 201 and OPCODE['invokedynamic'] == 186


class Insn:
    """pc, op (name), and decoded operands: arg (int / cp ref tuple), targets (branch pcs)."""
    __slots__ = ('pc', 'op', 'arg', 'arg2', 'targets', 'length')

    def __init__(self, pc, op, arg=None, arg2=None, targets=(), length=1):
        self.pc, self.op, self.arg, self.arg2, self.targets, self.length = pc, op, arg, arg2, targets, length

    def __repr__(self):
        a = '' if self.arg is None else f' {self.arg!r}'
        b = '' if self.arg2 is None else f' {self.arg2!r}'
        t = f' ->{list(self.targets)}' if self.targets else ''
        return f'{self.pc}: {self.op}{a}{b}{t}'


def decode(code, cp):
    """bytes -> [Insn]. cp refs are resolved: member ops get (owner, name, desc),
    ldc gets (kind, value), new/anewarray/checkcast get the class name."""
    out = []
    p, n = 0, len(code)
    while p < n:
        pc = p
        opc = code[p]
        if opc >= len(_OPS):
            raise ClassFormatError(f'bad opcode 0x{opc:02x} at {pc}')
        op, fmt = _OPS[opc]
        p += 1

        def need(k):
            if p + k > n:
                raise ClassFormatError(f'truncated {op} at {pc}')
        if fmt == '':
            out.append(Insn(pc, op))
        elif fmt == 'b':
            need(1); out.append(Insn(pc, op, struct.unpack('>b', code[p:p + 1])[0], length=2)); p += 1
        elif fmt in ('B', 'u1'):
            need(1); out.append(Insn(pc, op, code[p], length=2)); p += 1
        elif fmt == 'h':
            need(2); out.append(Insn(pc, op, struct.unpack('>h', code[p:p + 2])[0], length=3)); p += 2
        elif fmt == 'C':
            need(1); out.append(Insn(pc, op, cp.loadable(code[p]), length=2)); p += 1
        elif fmt == 'H':
            need(2)
            idx = struct.unpack('>H', code[p:p + 2])[0]
            p += 2
            if op in ('ldc_w', 'ldc2_w'):
                arg = cp.loadable(idx)
            elif op in ('new', 'anewarray', 'checkcast', 'instanceof'):
                arg = cp.class_name(idx)
            else:
                arg = cp.member_ref(idx)
            out.append(Insn(pc, op, arg, length=3))
        elif fmt == 'j':
            need(2); off = struct.unpack('>h', code[p:p + 2])[0]; p += 2
            out.append(Insn(pc, op, targets=(pc + off,), length=3))
        elif fmt == 'J':
            need(4); off = struct.unpack('>i', code[p:p + 4])[0]; p += 4
            out.append(Insn(pc, op, targets=(pc + off,), length=5))
        elif fmt == 'iinc':
            need(2); out.append(Insn(pc, op, code[p], struct.unpack('>b', code[p + 1:p + 2])[0], length=3)); p += 2
        elif fmt == 'iface':
            need(4); idx = struct.unpack('>H', code[p:p + 2])[0]; p += 4
            out.append(Insn(pc, op, cp.member_ref(idx), length=5))
        elif fmt == 'indy':
            need(4); idx = struct.unpack('>H', code[p:p + 2])[0]; p += 4
            x = cp._get(idx, CP_INDY)
            out.append(Insn(pc, op, cp.nat(x[2]), x[1], length=5))  # arg=(name, desc), arg2=bootstrap index
        elif fmt == 'multi':
            need(3); idx = struct.unpack('>H', code[p:p + 2])[0]
            out.append(Insn(pc, op, cp.class_name(idx), code[p + 2], length=4)); p += 3
        elif fmt == 'wide':
            need(1); sub = code[p]
            if sub >= len(_OPS):
                raise ClassFormatError('bad wide opcode')
            sop = _OPS[sub][0]
            if sop == 'iinc':
                need(5); idx, c = struct.unpack('>Hh', code[p + 1:p + 5]); p += 5
                out.append(Insn(pc, 'iinc', idx, c, length=6))
            else:
                need(3); idx = struct.unpack('>H', code[p + 1:p + 3])[0]; p += 3
                out.append(Insn(pc, sop, idx, length=4))
        elif fmt == 'tswitch':
            p += (-p) % 4
            need(12)
            default, lo, hi = struct.unpack('>iii', code[p:p + 12]); p += 12
            if hi < lo or hi - lo > 65536:
                raise ClassFormatError('bad tableswitch')
            need(4 * (hi - lo + 1))
            offs = struct.unpack(f'>{hi - lo + 1}i', code[p:p + 4 * (hi - lo + 1)]); p += 4 * (hi - lo + 1)
            keys = list(range(lo, hi + 1))
            out.append(Insn(pc, op, dict(zip(keys, [pc + o for o in offs])), pc + default,
                            targets=tuple([pc + default] + [pc + o for o in offs]), length=p - pc))
        elif fmt == 'lswitch':
            p += (-p) % 4
            need(8)
            default, npairs = struct.unpack('>ii', code[p:p + 8]); p += 8
            if not 0 <= npairs <= 65536:
                raise ClassFormatError('bad lookupswitch')
            need(8 * npairs)
            pairs = [struct.unpack('>ii', code[p + 8 * k:p + 8 * k + 8]) for k in range(npairs)]; p += 8 * npairs
            out.append(Insn(pc, op, {k: pc + o for k, o in pairs}, pc + default,
                            targets=tuple([pc + default] + [pc + o for _, o in pairs]), length=p - pc))
        else:  # pragma: no cover
            raise ClassFormatError(f'unhandled format {fmt}')
    return out


def disassemble(cf, m, limit=400):
    """Readable listing of a method (used in Lua stub comments without CFR)."""
    if not m.code:
        return ['(no code)']
    lines = []
    for ins in m.code.insns(cf.cp)[:limit]:
        if ins.op in ('getstatic', 'putstatic', 'getfield', 'putfield', 'invokevirtual', 'invokespecial', 'invokestatic', 'invokeinterface'):
            o, nme, d = ins.arg
            lines.append(f'{ins.pc:5}: {ins.op} {o.split("/")[-1]}.{nme}{d if ins.op.startswith("invoke") else ""}')
        elif ins.op in ('ldc', 'ldc_w', 'ldc2_w'):
            lines.append(f'{ins.pc:5}: {ins.op} {ins.arg[1]!r}')
        else:
            lines.append(repr(ins).rjust(0))
    if len(m.code.insns(cf.cp)) > limit:
        lines.append(f'... ({len(m.code.insns(cf.cp)) - limit} more instructions)')
    return lines
