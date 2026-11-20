"""A small abstract interpreter over JVM bytecode (static analysis; never executes mod code).

It walks a method's instructions linearly, tracking a symbolic operand stack and
locals.  Values are constants, object constructions (new/dup/<init> with their
arguments and every later call made on them), static field references, arrays,
lambdas (invokedynamic -> implementation method), and opaque results.  Branches
are not explored separately: the walk is flow-insensitive, which is what
registration code (straight-line builder chains) needs.  Loops whose exit test is
statically decidable (constant bounds, arrays of known length, enum values(),
collections built in analysed code) are unrolled, so content registered from
arrays, enums and lists is recovered one object per iteration.  Calls into the mod's
own classes are inlined with the actual argument values (bounded depth and a
step budget), so helpers like `register(name, item)` and subclass constructors
(`super(4, 0.3F, false)`) resolve to the objects they build.
"""
import struct

from .classfile import parse_method_desc, desc_class, ClassFormatError, ACC_STATIC

MAX_DEPTH = 5
ROOT_BUDGET = 150_000
TOTAL_BUDGET = 10_000_000
LOOP_MAX = 512          # iterations of one unrolled loop (per method activation)
UNROLL_MAX = 2048       # unrolled iterations per method activation, all loops together
MAX_CTX = 8             # allocation-context depth (call sites / loop iterations) kept in object sites
COLL_MAX = 4096         # elements modelled per collection


def f32(v):
    """Shortest decimal that round-trips to the same float32 (0.6000000238 -> 0.6)."""
    if not isinstance(v, float):
        return v
    try:
        want = struct.pack('>f', v)
    except (OverflowError, struct.error):
        return v
    for digits in range(1, 10):
        r = round(v, digits)
        if struct.pack('>f', r) == want:
            return r
    return v


class V:
    """Base symbolic value."""
    __slots__ = ()
    kind = 'unknown'

    def short(self):
        return '?'


class Unknown(V):
    __slots__ = ('desc', 'why')

    def __init__(self, desc=None, why=None):
        self.desc, self.why = desc, why

    def short(self):
        return f'?{self.why or ""}'


class Const(V):
    __slots__ = ('v', 't')
    kind = 'const'

    def __init__(self, v, t):
        self.v, self.t = v, t  # t: I J F D S(string) N(null) Z C

    def short(self):
        return repr(self.v)

    def __repr__(self):
        return f'Const({self.v!r})'


class ClassRef(V):
    __slots__ = ('name',)
    kind = 'class'

    def __init__(self, name):
        self.name = name

    def short(self):
        return self.name.split('/')[-1] + '.class'


class SField(V):
    __slots__ = ('owner', 'name', 'desc')
    kind = 'sfield'

    def __init__(self, owner, name, desc):
        self.owner, self.name, self.desc = owner, name, desc

    def short(self):
        return f'{self.owner.split("/")[-1]}.{self.name}'

    def __repr__(self):
        return f'SField({self.owner}.{self.name})'


class Obj(V):
    """An object constructed in analysed code (or a chainable call result)."""
    __slots__ = ('cls', 'args', 'ctor_desc', 'calls', 'fields', 'site', 'inits', 'tag', 'meta')
    kind = 'obj'
    _n = 0

    def __init__(self, cls, site=None):
        self.cls, self.site = cls, site
        self.args, self.ctor_desc = None, None
        self.calls = []   # [(name, desc, args, owner)]
        self.fields = {}
        self.inits = []   # super/this constructor calls seen while running subclass ctors: [(owner, desc, args)]
        self.tag = None   # extractor-specific label
        self.meta = {}

    def call_args(self, *names):
        """Last args of a call by any of the names (or None)."""
        for n, d, a, o in reversed(self.calls):
            if n in names:
                return a
        return None

    def all_calls(self, *names):
        return [(n, a) for n, d, a, o in self.calls if n in names]

    def short(self):
        return f'new {self.cls.split("/")[-1]}'

    def __repr__(self):
        return f'Obj({self.cls}, args={self.args})'


class Ret(Obj):
    """Result of a call we do not model; chainable like an object."""
    __slots__ = ('owner', 'mname', 'mdesc', 'recv')
    kind = 'ret'

    def __init__(self, owner, name, desc, recv, args, site=None):
        Obj.__init__(self, desc_class(parse_method_desc(desc)[1]) or 'java/lang/Object', site)
        self.owner, self.mname, self.mdesc, self.recv = owner, name, desc, recv
        self.args = args

    def short(self):
        return f'{self.owner.split("/")[-1]}.{self.mname}(...)'

    def __repr__(self):
        return f'Ret({self.owner}.{self.mname}, args={self.args})'


class Call(V):
    """Primitive result of a call we do not model (e.g. random.nextInt(16))."""
    __slots__ = ('owner', 'mname', 'mdesc', 'recv', 'args')
    kind = 'call'

    def __init__(self, owner, name, desc, recv, args):
        self.owner, self.mname, self.mdesc, self.recv, self.args = owner, name, desc, recv, args

    def short(self):
        return f'{self.owner.split("/")[-1]}.{self.mname}({", ".join(a.short() for a in self.args)})'


class Arr(V):
    __slots__ = ('etype', 'n', 'elems')
    kind = 'arr'

    def __init__(self, etype, n):
        self.etype, self.n, self.elems = etype, n, {}

    def values(self):
        if isinstance(self.n, int):
            return [self.elems.get(i, Unknown()) for i in range(min(self.n, 4096))]
        return [self.elems[k] for k in sorted(self.elems)]

    def short(self):
        return f'{self.etype}[{self.n}]'


class Lambda(V):
    """invokedynamic LambdaMetafactory: impl = (ref_kind, owner, name, desc)."""
    __slots__ = ('impl', 'captured', 'sam', 'sam_desc', 'cache')
    kind = 'lambda'

    def __init__(self, impl, captured, sam, sam_desc):
        self.impl, self.captured, self.sam, self.sam_desc = impl, captured, sam, sam_desc
        self.cache = None

    def short(self):
        return f'lambda->{self.impl[1].split("/")[-1]}.{self.impl[2]}'


class Param(V):
    __slots__ = ('idx', 'desc')
    kind = 'param'

    def __init__(self, idx, desc):
        self.idx, self.desc = idx, desc

    def short(self):
        return f'arg{self.idx}'


class SelfRef(V):
    """`this` when a method is interpreted on its own (no concrete object)."""
    __slots__ = ('cls',)
    kind = 'self'

    def __init__(self, cls):
        self.cls = cls

    def short(self):
        return 'this'


class FieldOf(V):
    __slots__ = ('obj', 'owner', 'name', 'desc')
    kind = 'field'

    def __init__(self, obj, owner, name, desc):
        self.obj, self.owner, self.name, self.desc = obj, owner, name, desc

    def short(self):
        return f'{self.obj.short()}.{self.name}'


class Arith(V):
    __slots__ = ('op', 'a', 'b')
    kind = 'arith'

    def __init__(self, op, a, b):
        self.op, self.a, self.b = op, a, b

    def short(self):
        return f'({self.a.short()} {self.op} {self.b.short() if self.b is not None else ""})'


class StrCat(V):
    __slots__ = ('parts',)
    kind = 'strcat'

    def __init__(self, parts):
        self.parts = parts

    def short(self):
        return '+'.join(p.short() for p in self.parts)


def const_val(v, *types):
    if isinstance(v, Const) and (not types or v.t in types):
        return v.v
    return None


def str_val(v):
    if isinstance(v, Const) and v.t == 'S':
        return v.v
    if isinstance(v, StrCat) and all(isinstance(p, Const) for p in v.parts):
        return ''.join(str(p.v) for p in v.parts)
    return None


class Event:
    """One invoke seen during interpretation."""
    __slots__ = ('op', 'owner', 'name', 'desc', 'recv', 'args', 'result', 'where', 'depth')

    def __init__(self, op, owner, name, desc, recv, args, result, where, depth):
        self.op, self.owner, self.name, self.desc = op, owner, name, desc
        self.recv, self.args, self.result, self.where, self.depth = recv, args, result, where, depth

    def __repr__(self):
        return f'Event({self.owner}.{self.name} args={[a.short() for a in self.args]} @ {self.where})'


class Frame:
    """Per-method trace: calls, compares, returns, putstatics (in pc order)."""

    def __init__(self, cls, method):
        self.cls, self.method = cls, method
        self.events = []
        self.compares = []   # (pc, op, a, b, target)
        self.returns = []    # (pc, value)
        self.stores = []     # (pc, owner, name, value)  putstatic / putfield
        self.news = []       # (pc, Obj)
        self.calls = []      # (pc, owner, name) of every invoke (loop-unrolling heuristics)
        self.truncated = False


INT_OPS = {'iadd': '+', 'isub': '-', 'imul': '*', 'idiv': '/', 'irem': '%', 'ishl': '<<', 'ishr': '>>', 'iushr': '>>>',
           'iand': '&', 'ior': '|', 'ixor': '^', 'ladd': '+', 'lsub': '-', 'lmul': '*', 'ldiv': '/', 'lrem': '%', 'lshl': '<<',
           'lshr': '>>', 'lushr': '>>>', 'land': '&', 'lor': '|', 'lxor': '^', 'fadd': '+', 'fsub': '-', 'fmul': '*', 'fdiv': '/',
           'frem': '%', 'dadd': '+', 'dsub': '-', 'dmul': '*', 'ddiv': '/', 'drem': '%'}


def _fold(op, a, b, t):
    try:
        if op == '+': r = a + b
        elif op == '-': r = a - b
        elif op == '*': r = a * b
        elif op == '/':
            if b == 0:
                return None
            r = a / b if t in 'FD' else int(a / b)
        elif op == '%':
            if b == 0:
                return None
            r = a % b if t in 'FD' else int(a - b * int(a / b))
        elif op == '<<': r = a << (b & (63 if t == 'J' else 31))
        elif op == '>>': r = a >> (b & (63 if t == 'J' else 31))
        elif op == '>>>':
            bits = 64 if t == 'J' else 32
            r = (a % (1 << bits)) >> (b & (bits - 1))
        elif op == '&': r = a & b
        elif op == '|': r = a | b
        elif op == '^': r = a ^ b
        else:
            return None
    except (TypeError, OverflowError, ValueError):
        return None
    if t == 'I':
        r = ((int(r) + (1 << 31)) % (1 << 32)) - (1 << 31)
    elif t == 'J':
        r = ((int(r) + (1 << 63)) % (1 << 64)) - (1 << 63)
    elif t == 'F':
        r = f32(float(r))
    return r


NOT_HANDLED = object()


def _category(op):
    if op == 'nop' or op == 'checkcast':
        return 'nop'
    if op == 'aconst_null' or op.startswith(('iconst_', 'lconst_', 'fconst_', 'dconst_')) or op in ('bipush', 'sipush'):
        return 'const'
    if op in ('ldc', 'ldc_w', 'ldc2_w'):
        return 'ldc'
    if op[1:5] == 'load' and op[0] in 'ilfda':
        return 'load'
    if op[1:6] == 'store' and op[0] in 'ilfda':
        return 'store'
    if op.endswith('aload') and len(op) == 6:
        return 'xaload'
    if op.endswith('astore') and len(op) == 7:
        return 'xastore'
    if op.startswith('invoke') and op != 'invokedynamic':
        return 'invoke'
    if op in ('getstatic', 'putstatic', 'getfield', 'putfield', 'new', 'invokedynamic', 'dup', 'pop'):
        return op
    return 'other'


from .classfile import OPNAMES as _OPNAMES  # noqa: E402
OPCAT = {n: _category(n) for n in _OPNAMES}
_CONSTS = {'aconst_null': (None, 'N'), 'iconst_m1': (-1, 'I'), 'iconst_0': (0, 'I'), 'iconst_1': (1, 'I'), 'iconst_2': (2, 'I'),
           'iconst_3': (3, 'I'), 'iconst_4': (4, 'I'), 'iconst_5': (5, 'I'), 'lconst_0': (0, 'J'), 'lconst_1': (1, 'J'),
           'fconst_0': (0.0, 'F'), 'fconst_1': (1.0, 'F'), 'fconst_2': (2.0, 'F'), 'dconst_0': (0.0, 'D'), 'dconst_1': (1.0, 'D')}


class Program:
    """All classes of one jar plus interpretation state shared across methods."""

    def __init__(self, classes, hook=None):
        self.classes = classes          # name -> ClassFile
        self.hook = hook                # hook(prog, ev_op, owner, name, desc, recv, args, depth) -> value | NOT_HANDLED
        self.statics = {}               # (owner, name) -> value
        self.events = []                # every Event (all roots, inlined calls included)
        self.frames = {}                # (cls, name, desc) -> Frame of the root run
        self.steps = 0
        self._root_done = set()
        self._static_busy = set()
        self._putstatic_sites = None
        self.errors = []
        self.on_new = None              # callback(obj) for every allocation
        self.ctx = ()                   # allocation context: call sites / loop iterations of the current activation
        self._serial = 0

    def serial(self):
        self._serial += 1
        return self._serial

    def site(self, *base):
        """Allocation site of an object: the instruction plus the context (inlined call sites, loop iterations)."""
        return base + self.ctx[-MAX_CTX:] if self.ctx else base

    # ---- hierarchy -----------------------------------------------------
    def is_mod(self, name):
        return name in self.classes

    def chain(self, name, limit=24):
        """[name, super, super.super, ...] through mod classes; ends at the first non-mod class."""
        out = []
        while name and limit > 0:
            out.append(name)
            cf = self.classes.get(name)
            if cf is None:
                break
            name = cf.super_name
            limit -= 1
        return out

    def vanilla_base(self, name):
        c = self.chain(name)
        return c[-1] if c else name

    def interfaces(self, name):
        out = set()
        for c in self.chain(name):
            cf = self.classes.get(c)
            if cf:
                out.update(cf.interfaces)
        return out

    def find_method(self, cls, name, desc):
        for c in self.chain(cls):
            cf = self.classes.get(c)
            if cf is None:
                return None, None
            m = cf.method(name, desc)
            if m is not None:
                return cf, m
        return None, None

    # ---- statics ---------------------------------------------------------
    def putstatic_sites(self):
        if self._putstatic_sites is None:
            sites = {}
            for cf in self.classes.values():
                for m in cf.methods:
                    if not m.code:
                        continue
                    try:
                        insns = m.code.insns(cf.cp)
                    except ClassFormatError:
                        continue
                    for ins in insns:
                        if ins.op == 'putstatic':
                            sites.setdefault((ins.arg[0], ins.arg[1]), []).append((cf.name, m.name, m.desc))
            self._putstatic_sites = sites
        return self._putstatic_sites

    def get_static(self, owner, name, desc):
        key = (owner, name)
        if key not in self.statics and self.is_mod(owner) and key not in self._static_busy:
            self._static_busy.add(key)
            try:
                cf = self.classes[owner]
                if cf.method('<clinit>'):
                    self.run_root(owner, '<clinit>', '()V')
                if key not in self.statics:
                    for site in self.putstatic_sites().get(key, [])[:8]:
                        self.run_root(*site)
                        if key in self.statics:
                            break
            finally:
                self._static_busy.discard(key)
        v = self.statics.get(key)
        if v is None or (isinstance(v, Const) and v.v is None):
            return SField(owner, name, desc)   # keep the name: null at this point (assigned later or by reflection)
        return v

    # ---- running -----------------------------------------------------------
    def run_root(self, cls, name, desc):
        key = (cls, name, desc)
        if key in self._root_done:
            return self.frames.get(key)
        self._root_done.add(key)
        cf = self.classes.get(cls)
        m = cf.method(name, desc) if cf else None
        if m is None or m.code is None:
            return None
        args = []
        for i, d in enumerate(parse_method_desc(m.desc)[0]):
            args.append(Param(i, d))
        this = None if m.access & ACC_STATIC else SelfRef(cls)
        fr = Frame(cls, m)
        self.frames[key] = fr
        saved, self.ctx = self.ctx, ()   # a root (clinit, zero-arg init helper) is context-free wherever it is triggered
        try:
            self.run(cf, m, args, this, 0, fr, ROOT_BUDGET)
        except (ClassFormatError, RecursionError, IndexError, KeyError, TypeError, ValueError) as e:
            self.errors.append(f'{cls}.{name}{desc}: {type(e).__name__}: {e}')
        finally:
            self.ctx = saved
        return fr

    def run_all(self, filter_fn=None, priority=None):
        order = list(self.classes.values())
        if priority is not None:
            order.sort(key=priority)   # stable: registration code first, so a budget cut-off loses the least content
        for cf in order:
            if filter_fn and not filter_fn(cf):
                continue
            for m in cf.methods:
                if m.code is not None and self.steps < TOTAL_BUDGET:
                    self.run_root(cf.name, m.name, m.desc)

    def invoke_lambda(self, lam, extra_args, depth):
        """Evaluate a lambda / method reference with extra (SAM) arguments -> return value."""
        kind, owner, name, desc = lam.impl
        args = list(lam.captured) + list(extra_args)
        if name == '<init>' or kind == 8:  # constructor reference Foo::new
            obj = Obj(owner, site=self.site('mref', owner, self.serial()))
            self._construct(obj, owner, desc, args[:len(parse_method_desc(desc)[0])], depth)
            return obj
        cf, m = self.find_method(owner, name, desc)
        if m is None or m.code is None or depth >= MAX_DEPTH:
            return Ret(owner, name, desc, None, args)
        nargs = len(parse_method_desc(desc)[0])
        if kind in (5, 7, 9) and not (m.access & ACC_STATIC):  # instance method ref: first arg is receiver
            this, margs = (args[0] if args else Unknown()), args[1:1 + nargs]
        else:
            this, margs = None, args[:nargs]
        while len(margs) < nargs:
            margs.append(Unknown(why='samarg'))
        fr = Frame(owner, m)
        return self.run(cf, m, margs, this, depth + 1, fr, ROOT_BUDGET // 4)

    def _construct(self, obj, owner, desc, args, depth):
        if obj.args is None:
            obj.args, obj.ctor_desc = args, desc
        else:
            obj.inits.append((owner, desc, args))
        if self.is_mod(owner) and depth < MAX_DEPTH:
            cf, m = self.find_method(owner, '<init>', desc)
            if m is not None and m.code is not None and cf.name == owner:
                fr = Frame(owner, m)
                self.run(cf, m, list(args), obj, depth + 1, fr, ROOT_BUDGET // 2)

    def run(self, cf, m, args, this, depth, fr, budget):
        """Interpret method m of class cf. Returns the first meaningful return value."""
        code = m.code
        insns = code.insns(cf.cp)
        pos = {ins.pc: k for k, ins in enumerate(insns)}
        locals_ = {}
        slot = 0
        if this is not None:
            locals_[0] = this
            slot = 1
        for d, a in zip(parse_method_desc(m.desc)[0], args):
            locals_[slot] = a
            slot += 2 if d in ('J', 'D') else 1
        stack = []
        pending = {}
        for (s, e, h, t) in code.handlers:
            pending.setdefault(h, [Unknown(f'L{t or "java/lang/Throwable"};', 'exc')])
        dead = False
        result = None
        where = (cf.name, m.name)
        steps = 0
        decided = {}     # conditional-branch pc -> (decision True/False/None, target) at its last evaluation
        iters = {}       # loop latch pc -> iterations unrolled
        loops = []       # active unrolled loops: [header pc, latch pc]
        worth = {}       # latch pc -> loop body worth unrolling
        base_ctx = self.ctx
        k, n = 0, len(insns)
        try:
            while k < n:
                ins = insns[k]
                k += 1
                steps += 1
                self.steps += 1
                if steps > budget or self.steps > TOTAL_BUDGET:
                    fr.truncated = True
                    break
                if loops and ins.pc > loops[-1][1]:   # left the innermost unrolled loop
                    loops.pop()
                    self.ctx = base_ctx + tuple(('it', L[1], iters.get(L[1], 0)) for L in loops)
                if ins.pc in pending and (dead or not stack):
                    stack = list(pending[ins.pc])
                dead = False
                op = ins.op
                try:
                    res = self._step(ins, op, stack, locals_, cf, fr, depth, where)
                except IndexError:  # stack underflow after a merge we could not model
                    stack = []
                    res = None
                if res is None:
                    continue
                tag, val = res
                if tag == 'branch':
                    tgt = ins.targets[0] if ins.targets else None
                    if op in _COND_OPS and fr.compares and fr.compares[-1][0] == ins.pc:
                        _pc, _op, a, b, _t = fr.compares[-1]
                        d = _decide(op, a, b)
                        decided[ins.pc] = (d, tgt)
                        if tgt is not None and tgt < ins.pc:
                            # backward conditional (do-while, ECJ-style loops): iterate while decidably taken
                            if d is True and iters.get(ins.pc, 0) < LOOP_MAX and tgt in pos and sum(iters.values()) < UNROLL_MAX and \
                                    self._loop_worth(fr, tgt, ins.pc, worth):
                                iters[ins.pc] = iters.get(ins.pc, 0) + 1
                                if not loops or loops[-1][1] != ins.pc:
                                    loops.append([tgt, ins.pc])
                                self.ctx = base_ctx + tuple(('it', L[1], iters.get(L[1], 0)) for L in loops)
                                k = pos[tgt]
                                continue
                        elif loops and tgt is not None and loops[-1][0] <= ins.pc < loops[-1][1] < tgt:
                            # exit test of an unrolled loop: decided -> follow it; undecidable -> stop unrolling
                            if d is not False and tgt in pos:
                                loops.pop()
                                self.ctx = base_ctx + tuple(('it', L[1], iters.get(L[1], 0)) for L in loops)
                                k = pos[tgt]
                                stack = []
                                continue
                    elif op in ('goto', 'goto_w') and tgt is not None and tgt < ins.pc and tgt in pos:
                        # loop latch: unroll again when every exit test seen in the loop body was decidable
                        tests = [dv for bpc, (dv, bt) in decided.items() if tgt <= bpc < ins.pc and bt is not None and bt > ins.pc]
                        if tests and all(dv is False for dv in tests) and iters.get(ins.pc, 0) < LOOP_MAX and \
                                sum(iters.values()) < UNROLL_MAX and self._loop_worth(fr, tgt, ins.pc, worth):
                            iters[ins.pc] = iters.get(ins.pc, 0) + 1
                            if not loops or loops[-1][1] != ins.pc:
                                loops.append([tgt, ins.pc])
                            self.ctx = base_ctx + tuple(('it', L[1], iters.get(L[1], 0)) for L in loops)
                            for bpc in [x for x in decided if tgt <= x < ins.pc]:
                                del decided[bpc]
                            k = pos[tgt]
                            stack = []
                            continue
                    for t in ins.targets:
                        if t > ins.pc and t not in pending:
                            pending[t] = list(stack)
                    if op in ('goto', 'goto_w', 'tableswitch', 'lookupswitch', 'athrow', 'jsr', 'jsr_w', 'ret'):
                        dead = True
                elif tag == 'return':
                    fr.returns.append((ins.pc, val))
                    if val is not None and result is None and not isinstance(val, Unknown):
                        result = val
                    dead = True
        finally:
            self.ctx = base_ctx
        return result

    def _loop_worth(self, fr, head, latch, cache):
        """Unroll only loops that build content: the body allocates non-JDK objects or makes registration-like calls,
        and draws no random numbers (world generation / chunk loops stay single-pass, as before)."""
        r = cache.get(latch)
        if r is None:
            r = False
            for pc, owner, name in fr.calls:
                if head <= pc < latch:
                    if name in _RANDOM_CALLS and (owner in ('java/util/Random', 'java/lang/Math') or 'Random' in owner):
                        r = None
                        break
                    if name.startswith(_REG_PREFIX) or (self.is_mod(owner) and name != '<init>') or name == '<init>' and not owner.startswith('java/'):
                        r = True
            if r is not None and not r:
                r = any(head <= pc < latch and not o.cls.startswith('java/') for pc, o in fr.news)
            cache[latch] = bool(r)
        return cache[latch]

    def _push_const(self, stack, v, t):
        stack.append(Const(v, t))

    def _step(self, ins, op, stack, locals_, cf, fr, depth, where):
        S = stack
        cat = OPCAT[op]
        if cat == 'load':
            idx = ins.arg if ins.arg is not None else int(op[-1])
            S.append(locals_.get(idx, Unknown(why='local')))
            return None
        if cat == 'invoke':
            return self._invoke(ins, op, S, cf, fr, depth, where)
        if cat == 'const':
            if op in ('bipush', 'sipush'):
                S.append(Const(ins.arg, 'I'))
            else:
                v, t = _CONSTS[op]
                S.append(Const(v, t))
            return None
        if cat == 'store':
            idx = ins.arg if ins.arg is not None else int(op[-1])
            locals_[idx] = S.pop()
            return None
        if cat == 'dup':
            S.append(S[-1])
            return None
        if cat == 'pop':
            S.pop()
            return None
        if cat == 'getstatic':
            owner, name, desc = ins.arg
            S.append(self.get_static(owner, name, desc))
            return None
        if cat == 'nop':
            return None
        if op == 'aconst_null':
            S.append(Const(None, 'N')); return None
        if op.startswith('iconst_'):
            S.append(Const(-1 if op == 'iconst_m1' else int(op[-1]), 'I')); return None
        if op.startswith('lconst_'):
            S.append(Const(int(op[-1]), 'J')); return None
        if op.startswith('fconst_'):
            S.append(Const(float(op[-1]), 'F')); return None
        if op.startswith('dconst_'):
            S.append(Const(float(op[-1]), 'D')); return None
        if op in ('bipush', 'sipush'):
            S.append(Const(ins.arg, 'I')); return None
        if op in ('ldc', 'ldc_w', 'ldc2_w'):
            k, v = ins.arg
            if k == 'int': S.append(Const(v, 'I'))
            elif k == 'float': S.append(Const(f32(v), 'F'))
            elif k == 'long': S.append(Const(v, 'J'))
            elif k == 'double': S.append(Const(v, 'D'))
            elif k == 'str': S.append(Const(v, 'S'))
            elif k == 'class': S.append(ClassRef(v))
            else: S.append(Unknown(why=k))
            return None
        if op[1:5] == 'load' and op[0] in 'ilfda':
            idx = ins.arg if ins.arg is not None else int(op[-1])
            S.append(locals_.get(idx, Unknown(why='local')))
            return None
        if op[1:6] == 'store' and op[0] in 'ilfda':
            idx = ins.arg if ins.arg is not None else int(op[-1])
            locals_[idx] = S.pop()
            return None
        if op == 'iinc':
            v = locals_.get(ins.arg)
            if isinstance(v, Const) and v.t == 'I':
                locals_[ins.arg] = Const(_fold('+', v.v, ins.arg2, 'I'), 'I')
            else:
                locals_[ins.arg] = Arith('+', v or Unknown(), Const(ins.arg2, 'I'))
            return None
        if op.endswith('aload') and len(op) == 6:   # xaload
            idx, arr = S.pop(), S.pop()
            if isinstance(arr, Arr) and isinstance(idx, Const) and idx.v in arr.elems:
                S.append(arr.elems[idx.v])
            else:
                S.append(Unknown(why='aelem'))
            return None
        if op.endswith('astore') and len(op) == 7:  # xastore
            val, idx, arr = S.pop(), S.pop(), S.pop()
            if isinstance(arr, Arr) and isinstance(idx, Const) and isinstance(idx.v, int) and len(arr.elems) < 4096:
                arr.elems[idx.v] = val
            return None
        if op == 'pop':
            S.pop(); return None
        if op == 'pop2':
            v = S.pop()
            if not (isinstance(v, Const) and v.t in 'JD'):
                if S:
                    S.pop()
            return None
        if op == 'dup':
            S.append(S[-1]); return None
        if op == 'dup_x1':
            a, b = S.pop(), S.pop(); S += [a, b, a]; return None
        if op == 'dup_x2':
            a, b, c = S.pop(), S.pop(), S.pop(); S += [a, c, b, a]; return None
        if op == 'dup2':
            if isinstance(S[-1], Const) and S[-1].t in 'JD':
                S.append(S[-1])
            else:
                S += [S[-2], S[-1]]
            return None
        if op in ('dup2_x1', 'dup2_x2'):
            a = S.pop()
            b = S.pop()
            S += [a, b, a]
            return None
        if op == 'swap':
            S[-1], S[-2] = S[-2], S[-1]; return None
        if op in INT_OPS:
            b, a = S.pop(), S.pop()
            t = op[0].upper()
            if isinstance(a, Const) and isinstance(b, Const) and a.v is not None and b.v is not None:
                r = _fold(INT_OPS[op], a.v, b.v, t)
                S.append(Const(r, t) if r is not None else Arith(INT_OPS[op], a, b))
            else:
                S.append(Arith(INT_OPS[op], a, b))
            return None
        if op in ('ineg', 'lneg', 'fneg', 'dneg'):
            a = S.pop()
            S.append(Const(-a.v, a.t) if isinstance(a, Const) and isinstance(a.v, (int, float)) else Arith('neg', a, None))
            return None
        if len(op) == 3 and op[1] == '2':  # conversions i2f etc.
            a = S.pop()
            t = op[2].upper()
            if isinstance(a, Const) and isinstance(a.v, (int, float)):
                v = a.v
                if t in 'FD':
                    v = float(v) if t == 'D' else f32(float(v))
                elif t in 'IJ':
                    v = int(v)
                elif t == 'B':
                    v = ((int(v) + 128) % 256) - 128; t = 'I'
                elif t == 'C':
                    v = int(v) % 65536; t = 'I'
                elif t == 'S':
                    v = ((int(v) + 32768) % 65536) - 32768; t = 'I'
                S.append(Const(v, t))
            else:
                S.append(a)
            return None
        if op in ('lcmp', 'fcmpl', 'fcmpg', 'dcmpl', 'dcmpg'):
            b, a = S.pop(), S.pop()
            S.append(Arith('cmp', a, b))
            return None
        if op in ('ifeq', 'ifne', 'iflt', 'ifge', 'ifgt', 'ifle', 'ifnull', 'ifnonnull'):
            a = S.pop()
            fr.compares.append((ins.pc, op, a, None, ins.targets[0]))
            return ('branch', None)
        if op.startswith('if_'):
            b, a = S.pop(), S.pop()
            fr.compares.append((ins.pc, op, a, b, ins.targets[0]))
            return ('branch', None)
        if op in ('goto', 'goto_w'):
            return ('branch', None)
        if op in ('jsr', 'jsr_w'):
            S.append(Unknown(why='retaddr'))
            return ('branch', None)
        if op == 'ret':
            return ('branch', None)
        if op in ('tableswitch', 'lookupswitch'):
            a = S.pop()
            fr.compares.append((ins.pc, op, a, ins.arg, ins.arg2))
            return ('branch', None)
        if op.endswith('return'):
            val = S.pop() if op != 'return' else None
            return ('return', val)
        if op == 'athrow':
            S.pop()
            return ('branch', None)
        if op == 'getstatic':
            owner, name, desc = ins.arg
            S.append(self.get_static(owner, name, desc))
            return None
        if op == 'putstatic':
            owner, name, desc = ins.arg
            v = S.pop()
            fr.stores.append((ins.pc, owner, name, v))
            if (owner, name) not in self.statics or not isinstance(v, (Unknown, Param)):
                self.statics[(owner, name)] = v
            return None
        if op == 'getfield':
            owner, name, desc = ins.arg
            o = S.pop()
            if isinstance(o, Obj) and name in o.fields:
                S.append(o.fields[name])
            else:
                S.append(FieldOf(o, owner, name, desc))
            return None
        if op == 'putfield':
            owner, name, desc = ins.arg
            v, o = S.pop(), S.pop()
            fr.stores.append((ins.pc, owner, name, v))
            if isinstance(o, Obj):
                o.fields[name] = v
            return None
        if op == 'new':
            o = Obj(ins.arg, site=self.site(cf.name, fr.method.name, ins.pc))
            if ins.arg in _COLLECTIONS:
                o.meta['coll'] = []
            fr.news.append((ins.pc, o))
            if self.on_new:
                self.on_new(o)
            S.append(o)
            return None
        if op in ('newarray', 'anewarray'):
            n = S.pop()
            S.append(Arr(ins.arg, n.v if isinstance(n, Const) else None))
            return None
        if op == 'multianewarray':
            for _ in range(ins.arg2):
                S.pop()
            S.append(Arr(ins.arg, None))
            return None
        if op == 'arraylength':
            a = S.pop()
            S.append(Const(a.n, 'I') if isinstance(a, Arr) and isinstance(a.n, int) else Unknown(why='len'))
            return None
        if op == 'checkcast':
            return None
        if op == 'instanceof':
            S.pop(); S.append(Unknown('Z', 'instanceof')); return None
        if op in ('monitorenter', 'monitorexit'):
            S.pop(); return None
        if op == 'invokedynamic':
            return self._indy(ins, S, cf, fr, depth, where)
        if op.startswith('invoke'):
            return self._invoke(ins, op, S, cf, fr, depth, where)
        return None

    def _indy(self, ins, S, cf, fr, depth, where):
        name, desc = ins.arg
        argd, retd = parse_method_desc(desc)
        args = [S.pop() for _ in argd][::-1]
        try:
            bsm, bargs = cf.bootstrap_info(ins.arg2)
        except ClassFormatError:
            S.append(Unknown(retd, 'indy'))
            return None
        _kind, bowner, bname, _bdesc = bsm
        if bowner == 'java/lang/invoke/LambdaMetafactory' and len(bargs) >= 2 and bargs[1][0] == 'mhandle':
            S.append(Lambda(bargs[1][1], args, name, bargs[0][1] if bargs[0][0] == 'mtype' else None))
        elif bowner == 'java/lang/invoke/StringConcatFactory':
            recipe = bargs[0][1] if bargs and bargs[0][0] == 'str' else None
            if bname == 'makeConcatWithConstants' and recipe is not None:
                parts, it, consts = [], iter(args), iter(bargs[1:])
                buf = ''
                for ch in recipe:
                    if ch in '\x01\x02':
                        if buf:
                            parts.append(Const(buf, 'S')); buf = ''
                        if ch == '\x01':
                            parts.append(next(it, Unknown()))
                        else:
                            c = next(consts, ('str', ''))
                            parts.append(Const(str(c[1]), 'S'))
                    else:
                        buf += ch
                if buf:
                    parts.append(Const(buf, 'S'))
            else:
                parts = list(args)
            S.append(_concat(parts))
        else:
            S.append(Unknown(retd, f'indy:{bname}'))
        return None

    def _invoke(self, ins, op, S, cf, fr, depth, where):
        owner, name, desc = ins.arg
        fr.calls.append((ins.pc, owner, name))
        argd, retd = parse_method_desc(desc)
        args = [S.pop() for _ in argd][::-1]
        recv = None if op == 'invokestatic' else S.pop()
        result = NOT_HANDLED
        if name == '<init>':
            if isinstance(recv, Obj):
                self._construct(recv, owner, desc, args, depth)
                if owner in ('java/lang/StringBuilder', 'java/lang/StringBuffer'):
                    recv.meta['sb'] = [a for a in args if not (isinstance(a, Const) and a.t == 'I')]
            elif isinstance(recv, SelfRef):
                pass
            self._event(fr, op, owner, name, desc, recv, args, None, where, depth)
            return None
        result = _java_semantics(owner, name, desc, recv, args)
        if result is NOT_HANDLED:
            result = self._collections(owner, name, desc, recv, args, depth, where, ins.pc)
        if result is NOT_HANDLED and isinstance(recv, Lambda) and name == recv.sam and depth < MAX_DEPTH:
            if recv.cache is None or args:
                v = self.invoke_lambda(recv, args, depth)
                if not args:
                    recv.cache = v
                result = v if v is not None else Unknown(retd, 'lambda')
            else:
                result = recv.cache
        if result is NOT_HANDLED and self.hook is not None:
            result = self.hook(self, op, owner, name, desc, recv, args, depth)
        if result is NOT_HANDLED and depth < MAX_DEPTH:
            target = None
            if op == 'invokestatic' or op == 'invokespecial':
                target = owner
            elif isinstance(recv, (Obj, SelfRef)) and self.is_mod(recv.cls):
                target = recv.cls
            elif self.is_mod(owner):
                target = owner
            if op == 'invokestatic' and not args and target is not None and self.is_mod(target):
                # zero-argument static helpers (ModItems.init()) run once as roots: no duplicate events
                tcf, tm = self.find_method(target, name, desc)
                if tm is not None and tm.code is not None and (tm.access & ACC_STATIC):
                    rf = self.run_root(tcf.name, tm.name, tm.desc)
                    if retd != 'V':
                        rv = next((v for _pc, v in (rf.returns if rf else []) if v is not None and not isinstance(v, Unknown)), None)
                        result = rv if rv is not None else Unknown(retd, 'root')
                    else:
                        result = None
                    target = None
            if target is not None and self.is_mod(target):
                tcf, tm = self.find_method(target, name, desc)
                if tm is not None and tm.code is not None and not (op == 'invokestatic' and not (tm.access & ACC_STATIC)):
                    sub = Frame(tcf.name, tm)
                    saved = self.ctx
                    self.ctx = saved + (('call', where[0], where[1], ins.pc),)   # objects built by the callee are per call site
                    try:
                        v = self.run(tcf, tm, args, recv if op != 'invokestatic' else None, depth + 1, sub, ROOT_BUDGET // 4)
                    finally:
                        self.ctx = saved
                    fr.events += sub.events
                    if retd != 'V':
                        result = v if v is not None else Unknown(retd, 'inlined')
                    else:
                        result = None
        if isinstance(recv, Obj):
            recv.calls.append((name, desc, args, owner))
        if result is NOT_HANDLED:
            if retd == 'V':
                result = None
            elif isinstance(recv, Obj) and self._chains(owner, retd, recv, name):
                result = recv
            elif desc_class(retd) or retd.startswith('['):
                result = Ret(owner, name, desc, recv, args, site=self.site(where[0], where[1], ins.pc))
            else:
                result = Call(owner, name, desc, recv, args)
        self._event(fr, op, owner, name, desc, recv, args, result, where, depth)
        if retd != 'V':
            S.append(result if result is not None else Unknown(retd))
        return None

    def _collections(self, owner, name, desc, recv, args, depth, where, pc):
        """java.util collections / iterators / arrays built in analysed code (bounded), plus enum values()."""
        if name == 'clone' and isinstance(recv, Arr):
            return recv
        if owner in _LIST_FACTORIES and name in _LIST_FACTORIES[owner]:
            o = Obj('java/util/ArrayList', site=self.site(where[0], where[1], pc))
            items = []
            for a in args:
                if isinstance(a, Arr):
                    items += a.values()
                elif isinstance(a, Obj) and 'coll' in a.meta:
                    items += a.meta['coll']
                elif not (isinstance(a, Const) and a.t == 'I' and name in ('withSize', 'create')):
                    items.append(a)
            if owner.endswith('NonNullList') and name == 'withSize':
                items = []
            o.meta['coll'] = items[:COLL_MAX]
            return o
        if owner == 'java/util/Collections' and name == 'addAll' and args and isinstance(args[0], Obj) and 'coll' in args[0].meta:
            for a in args[1:]:
                args[0].meta['coll'] += a.values() if isinstance(a, Arr) else [a]
            return Const(True, 'Z')
        if isinstance(recv, Obj) and 'coll' in recv.meta:
            c = recv.meta['coll']
            if name == '<init>':
                return NOT_HANDLED
            if name in ('add', 'push', 'offer', 'addLast') and len(args) == 1:
                if len(c) < COLL_MAX:
                    c.append(args[0])
                return Const(True, 'Z') if desc.endswith('Z') else None
            if name == 'add' and len(args) == 2 and isinstance(args[0], Const) and isinstance(args[0].v, int):
                if len(c) < COLL_MAX:
                    c.insert(args[0].v, args[1])
                return None
            if name == 'addAll' and args:
                src = args[-1]
                if isinstance(src, Obj) and 'coll' in src.meta:
                    c += src.meta['coll'][:COLL_MAX - len(c)]
                elif isinstance(src, Arr):
                    c += src.values()[:COLL_MAX - len(c)]
                return Const(True, 'Z')
            if name == 'put' and len(args) == 2:
                recv.meta.setdefault('keys', []).append(args[0])
                if len(c) < COLL_MAX:
                    c.append(args[1])
                return Unknown(why='put')
            if name == 'get' and len(args) == 1 and isinstance(args[0], Const) and 'keys' not in recv.meta and isinstance(args[0].v, int):
                return c[args[0].v] if 0 <= args[0].v < len(c) else Unknown(why='get')
            if name == 'get' and len(args) == 1 and 'keys' in recv.meta:
                for kk, vv in zip(reversed(recv.meta['keys']), reversed(c)):
                    if kk is args[0] or (isinstance(kk, Const) and isinstance(args[0], Const) and kk.v == args[0].v):
                        return vv
                return Unknown(why='get')
            if name == 'size':
                return Const(len(c), 'I')
            if name == 'isEmpty':
                return Const(not c, 'Z')
            if name in ('values', 'keySet', 'stream', 'build') or (name == 'toArray'):
                if name == 'keySet':
                    items = list(recv.meta.get('keys', []))
                else:
                    items = list(c)
                if name == 'toArray':
                    a = Arr('java/lang/Object', len(items))
                    a.elems = dict(enumerate(items))
                    return a
                o = Obj('java/util/ArrayList', site=self.site(where[0], where[1], pc))
                o.meta['coll'] = items
                return o
            if name == 'iterator':
                it = Obj('java/util/Iterator', site=self.site(where[0], where[1], pc))
                it.meta['iter'] = (c, [0])
                return it
            if name == 'forEach' and len(args) == 1 and isinstance(args[0], Lambda) and depth < MAX_DEPTH:
                saved = self.ctx
                try:
                    for i, x in enumerate(list(c)[:LOOP_MAX]):
                        self.ctx = saved + (('it', where[0], where[1], pc, i),)
                        self.invoke_lambda(args[0], [x], depth)
                finally:
                    self.ctx = saved
                return None
            return NOT_HANDLED
        if isinstance(recv, Obj) and not args and (recv.cls.endswith('/ItemStack') or 'stack_meta' in recv.meta):
            # ItemStack(item, count, meta) built in analysed code, or a synthetic stack used to evaluate per-meta overrides
            a = recv.args or []
            if name in ('getMetadata', 'getItemDamage', 'getDamage', 'getItemDamageForDisplay', 'getDamageValue'):
                if 'stack_meta' in recv.meta:
                    return Const(recv.meta['stack_meta'], 'I')
                if len(a) >= 3 and isinstance(a[2], Const):
                    return a[2]
                if len(a) <= 2 and a:
                    return Const(0, 'I')
            if name in ('getItem',) and a and isinstance(a[0], (Obj, SField)):
                return a[0]
            if name in ('getCount', 'getStackSize') and len(a) >= 2 and isinstance(a[1], Const):
                return a[1]
        if isinstance(recv, Obj) and 'iter' in recv.meta:
            c, i = recv.meta['iter']
            if name == 'hasNext':
                return Const(i[0] < len(c), 'Z')
            if name == 'next':
                if i[0] < len(c):
                    i[0] += 1
                    return c[i[0] - 1]
                return Unknown(why='next')
        if name == 'values' and not args and desc.startswith('()[') and owner not in self.classes:
            vals = VANILLA_ENUMS.get(owner.rsplit('/', 1)[-1])
            if vals:
                a = Arr(owner, len(vals))
                a.elems = {i: SField(owner, v, 'L' + owner + ';') for i, v in enumerate(vals)}
                return a
        if isinstance(recv, SField) and recv.owner.rsplit('/', 1)[-1] in VANILLA_ENUMS and not args:
            r = _vanilla_enum_value(recv, name)
            if r is not None:
                return r
        if isinstance(recv, Obj) and self.is_mod(recv.cls) and not args and name in ('name', 'ordinal', 'toString') and recv.args and \
                len(recv.args) >= 2 and isinstance(recv.args[0], Const) and recv.args[0].t == 'S':
            cf = self.classes.get(recv.cls)
            base = self.classes.get(cf.super_name) if cf is not None and cf.super_name != 'java/lang/Enum' else cf
            if base is not None and base.is_enum and not (name == 'toString' and self.find_method(recv.cls, 'toString', desc)[1]):
                return recv.args[0] if name in ('name', 'toString') else recv.args[1]
        return NOT_HANDLED

    def _chains(self, owner, retd, recv, name=''):
        rc = desc_class(retd)
        if rc is None or rc in ('java/lang/String', 'java/lang/Object'):
            return False
        if rc == owner:
            return True
        chain = self.chain(owner)
        if rc in chain or rc in self.chain(recv.cls):
            return True
        # builder-style setters on vanilla classes we cannot see (Item.setUnlocalizedName returns Item)
        if rc.startswith('java/'):
            return False
        n = name
        return (n.startswith(('set', 'with')) and len(n) > 3) or rc.split('/')[-1] in ('Item', 'Block')

    def _event(self, fr, op, owner, name, desc, recv, args, result, where, depth):
        ev = Event(op, owner, name, desc, recv, args, result, where, depth)
        fr.events.append(ev)
        self.events.append(ev)


def _concat(parts):
    flat = []
    for p in parts:
        if isinstance(p, StrCat):
            flat += p.parts
        else:
            flat.append(p)
    if all(isinstance(p, Const) for p in flat):
        return Const(''.join(_jstr(p.v) for p in flat), 'S')
    return StrCat(flat)


def _jstr(v):
    if v is None:
        return 'null'
    if isinstance(v, bool):
        return 'true' if v else 'false'
    if isinstance(v, float) and v.is_integer():
        return f'{v:.1f}'
    return str(v)


_RANDOM_CALLS = {'nextInt', 'nextFloat', 'nextDouble', 'nextBoolean', 'nextGaussian', 'nextLong', 'random', 'func_76136_a', 'func_151240_a'}
_REG_PREFIX = ('register', 'add', 'put', 'set', 'create', 'make', 'init', 'build', 'of')
_COND_OPS = {'ifeq', 'ifne', 'iflt', 'ifge', 'ifgt', 'ifle', 'ifnull', 'ifnonnull', 'if_icmpeq', 'if_icmpne', 'if_icmplt',
             'if_icmpge', 'if_icmpgt', 'if_icmple', 'if_acmpeq', 'if_acmpne'}


def _decide(op, a, b):
    """Statically decided outcome of a conditional branch (True = taken), or None."""
    def iv(x):
        if isinstance(x, Const) and isinstance(x.v, (bool, int)) and not isinstance(x.v, float):
            return int(x.v)
        if isinstance(x, Const) and x.t == 'C' and isinstance(x.v, str) and len(x.v) == 1:
            return ord(x.v)
        return None
    if op in ('ifnull', 'ifnonnull'):
        if isinstance(a, Const) and a.v is None:
            isnull = True
        elif isinstance(a, (Obj, Arr)) or (isinstance(a, Const) and a.t == 'S'):
            isnull = False
        else:
            return None
        return isnull if op == 'ifnull' else not isnull
    if op.startswith('if_icmp'):
        x, y = iv(a), iv(b)
        if x is None or y is None:
            return None
        return {'eq': x == y, 'ne': x != y, 'lt': x < y, 'ge': x >= y, 'gt': x > y, 'le': x <= y}[op[7:]]
    if op.startswith('if_acmp'):
        return None
    x = iv(a)
    if x is None:
        return None
    return {'ifeq': x == 0, 'ifne': x != 0, 'iflt': x < 0, 'ifge': x >= 0, 'ifgt': x > 0, 'ifle': x <= 0}[op]


_COLLECTIONS = {'java/util/ArrayList', 'java/util/LinkedList', 'java/util/HashSet', 'java/util/LinkedHashSet', 'java/util/TreeSet',
                'java/util/Vector', 'java/util/ArrayDeque', 'java/util/concurrent/CopyOnWriteArrayList', 'java/util/HashMap',
                'java/util/LinkedHashMap', 'java/util/TreeMap', 'java/util/EnumMap', 'java/util/IdentityHashMap',
                'java/util/concurrent/ConcurrentHashMap', 'java/util/Stack'}
_LIST_FACTORIES = {
    'java/util/Arrays': {'asList'}, 'java/util/List': {'of'}, 'java/util/Set': {'of'}, 'java/util/Collections': {'singletonList', 'singleton'},
    'com/google/common/collect/Lists': {'newArrayList', 'newLinkedList'}, 'com/google/common/collect/Sets': {'newHashSet', 'newLinkedHashSet'},
    'com/google/common/collect/ImmutableList': {'of', 'copyOf'}, 'com/google/common/collect/ImmutableSet': {'of', 'copyOf'},
    'com/google/common/collect/Maps': {'newHashMap', 'newLinkedHashMap', 'newEnumMap', 'newTreeMap'},
    'net/minecraft/util/NonNullList': {'create', 'withSize', 'from'}, 'net/minecraft/core/NonNullList': {'create', 'withSize', 'of'},
}

# Vanilla enums whose values() order and names are fixed (loops over them register one object per constant).
_DYES = ['WHITE', 'ORANGE', 'MAGENTA', 'LIGHT_BLUE', 'YELLOW', 'LIME', 'PINK', 'GRAY', 'SILVER', 'CYAN', 'PURPLE', 'BLUE', 'BROWN',
         'GREEN', 'RED', 'BLACK']
VANILLA_ENUMS = {
    'EnumDyeColor': _DYES, 'DyeColor': [d if d != 'SILVER' else 'LIGHT_GRAY' for d in _DYES],
    'BlockPlanks$EnumType': ['OAK', 'SPRUCE', 'BIRCH', 'JUNGLE', 'ACACIA', 'DARK_OAK'],
    'EnumFacing': ['DOWN', 'UP', 'NORTH', 'SOUTH', 'WEST', 'EAST'], 'Direction': ['DOWN', 'UP', 'NORTH', 'SOUTH', 'WEST', 'EAST'],
    'EntityEquipmentSlot': ['MAINHAND', 'OFFHAND', 'FEET', 'LEGS', 'CHEST', 'HEAD'],
    'EquipmentSlotType': ['MAINHAND', 'OFFHAND', 'FEET', 'LEGS', 'CHEST', 'HEAD'],
}


def _camel(s):
    p = s.lower().split('_')
    return p[0] + ''.join(x.title() for x in p[1:])


def _vanilla_enum_value(f, name):
    """name()/getName()/ordinal()/... of a vanilla enum constant (SField) -> Const or None."""
    owner = f.owner.rsplit('/', 1)[-1]
    vals = VANILLA_ENUMS[owner]
    if f.name not in vals:
        return None
    i = vals.index(f.name)
    if name in ('name', 'toString') or (name == 'getName' and owner in ('EnumFacing', 'Direction')):
        return Const(f.name if name == 'name' else f.name.lower(), 'S')
    if name in ('getName', 'getSerializedName', 'getString', 'getDyeColorName'):
        return Const(f.name.lower(), 'S')
    if name in ('ordinal', 'getMetadata', 'getMeta', 'getId', 'getIndex'):
        return Const(i, 'I')
    if name == 'getDyeDamage':
        return Const(15 - i, 'I')
    if name in ('getUnlocalizedName', 'getTranslationKey'):
        return Const(_camel(f.name) if owner == 'EnumDyeColor' else f.name.lower(), 'S')
    return None


BOX = {'java/lang/Integer': 'I', 'java/lang/Float': 'F', 'java/lang/Double': 'D', 'java/lang/Long': 'J',
       'java/lang/Character': 'C', 'java/lang/Boolean': 'Z', 'java/lang/Short': 'I', 'java/lang/Byte': 'I'}


def _java_semantics(owner, name, desc, recv, args):
    """Pure JDK methods we can evaluate on constants."""
    if owner in BOX and name == 'valueOf' and len(args) == 1:
        a = args[0]
        if isinstance(a, Const):
            if BOX[owner] == 'C' and isinstance(a.v, int):
                return Const(chr(a.v), 'C')
            if BOX[owner] == 'Z' and isinstance(a.v, int):
                return Const(bool(a.v), 'Z')
            return Const(a.v, BOX[owner])
        return a
    if owner in BOX and name.endswith('Value') and isinstance(recv, Const):
        return recv
    if owner in ('java/lang/StringBuilder', 'java/lang/StringBuffer') and isinstance(recv, Obj):
        sb = recv.meta.setdefault('sb', [])
        if name == 'append' and len(args) == 1:
            a = args[0]
            if isinstance(a, Const) and a.t == 'C':
                a = Const(a.v, 'S')
            elif isinstance(a, Const) and a.t == 'I' and desc.startswith('(C)'):
                a = Const(chr(a.v), 'S')
            sb.append(a)
            return recv
        if name == 'toString':
            return _concat(list(sb))
        return NOT_HANDLED
    if owner == 'java/lang/String':
        s = str_val(recv) if recv is not None else None
        if name == 'valueOf' and len(args) == 1 and isinstance(args[0], Const):
            return Const(_jstr(args[0].v) if args[0].t != 'C' else args[0].v, 'S')
        if s is None:
            return NOT_HANDLED
        a = [x.v if isinstance(x, Const) else None for x in args]
        try:
            if name == 'toLowerCase':
                return Const(s.lower(), 'S')
            if name == 'toUpperCase':
                return Const(s.upper(), 'S')
            if name == 'trim':
                return Const(s.strip(), 'S')
            if name == 'substring' and all(isinstance(x, int) for x in a):
                return Const(s[a[0]:a[1]] if len(a) == 2 else s[a[0]:], 'S')
            if name == 'concat' and isinstance(a[0], str):
                return Const(s + a[0], 'S')
            if name == 'replace' and len(a) == 2 and all(isinstance(x, str) for x in a):
                return Const(s.replace(a[0], a[1]), 'S')
            if name == 'length':
                return Const(len(s), 'I')
            if name == 'intern':
                return recv
        except (TypeError, IndexError):
            return NOT_HANDLED
    return NOT_HANDLED
