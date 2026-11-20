"""Retail tagged Creative stack round trips for SDK-published Potion objects."""
from pathlib import Path
import sys

from unicorn import UC_HOOK_INSN_INVALID
from unicorn.x86_const import UC_X86_REG_RBX, UC_X86_REG_RDI

from uc_common import alloc, b, call, d, q, run_range, wd, wq, wstr

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'strings195' / 'tests'))
from uc195 import Machine


def check_creative_potions(u, check, potions):
    # Reuse the existing exact Unicorn models for Jaguar AVX stores and PLT
    # imports that the ItemInstance and Item wrapper need.
    avx = Machine()
    avx.u = u
    u.hook_add(UC_HOOK_INSN_INVALID, avx._invalid)
    stubs = {0xe6ce10: avx._atomic(1), 0xe6ce20: avx._atomic(-1), 0xe6d090: avx._memcpy}

    def native(fn, *args, extra=None):
        return call(u, fn, args, stubs={**stubs, **(extra or {})}, limit=5_000_000)[0]

    def zalloc(size):
        addr = alloc(u, size)
        u.mem_write(addr, bytes(size))
        return addr

    # This fixture only boots effects and potion registries. Supply the two
    # startup globals the stack paths use from the rest of the title's init.
    empty = alloc(u, 0xa8)
    native(0x3b7720, empty, 0, 0, 0)
    wq(u, 0x1416d98, empty)
    wq(u, 0x1416da0, 0)
    potion_key = 0x142eba8
    u.mem_write(potion_key, bytes(u.mem_read(wstr(u, 'Potion'), 0x28)))

    # Use native Item subclasses in stock Items::init (0x386070). Its
    # Splash/Lingering/TippedArrow constructors are inlined in the retail image.
    drinks = alloc(u, 0xb0)
    native(0x565270, drinks)
    items = [(373, drinks, 0x12a62d8)]
    for item_id, size, begin, end, vtable in (
        (438, 0xb0, 0x3a17a4, 0x3a180c, 0x12da138),
        (441, 0xb0, 0x3a1b9b, 0x3a1c03, 0x127e198),
        (440, 0xa8, 0x3a1a32, 0x3a1aa1, 0x12e76a8),
    ):
        item = alloc(u, size)
        u.reg_write(UC_X86_REG_RBX, item)
        run_range(u, begin, end, stubs=stubs)
        items.append((item_id, item, vtable))

    def make_stack(item):
        obj = alloc(u, 0xa8)
        native(0x3b7720, obj, item, 1, 0)
        control = zalloc(24)
        wq(u, control, 0x1214808)
        wd(u, control + 8, 1)
        wd(u, control + 12, 1)
        wq(u, control + 16, obj)
        pair = zalloc(16)
        native(0x68d30, pair, obj, control)
        return pair

    def release(pair):
        owner = q(u, pair + 8)
        if owner and native(0xe6ce20, owner + 8, 1, 5) == 1:
            vt = q(u, owner)
            native(q(u, vt), owner)
            if native(0xe6ce20, owner + 12, 1, 5) == 1:
                native(q(u, vt + 8), owner)
        u.mem_write(pair, bytes(16))

    def tagged_key(tag):
        if not tag or q(u, tag) != 0x122ede0:
            return None
        value = native(0x1b7400, tag, potion_key)
        if not value or q(u, value) != 0x12d83a8:
            return None
        size = q(u, value + 0x20)
        chars = q(u, value + 0x10) if q(u, value + 0x28) >= 8 else value + 0x10
        return bytes(u.mem_read(chars, 2 * size)).decode('utf-16le')

    group, group_ptr = zalloc(32), zalloc(8)
    wq(u, group_ptr, group)
    for kind, (item_id, item, vtable) in enumerate(items):
        check(q(u, item) == vtable, f'Creative potion uses native stock class for {item_id}')
        for potion, key, sids in potions:
            pair = make_stack(item)
            tagged = zalloc(16)
            # PotionUtils takes the shared_ptr by move into the result pair.
            check(native(0x562cd0, tagged, pair, potion) == tagged and not q(u, pair) and not q(u, pair + 8),
                  f'{item_id} {key}: setPotion consumes its input pair')
            source = q(u, tagged)
            tag = q(u, source + 0x30)
            check(native(0x562670, tagged) == potion and tagged_key(tag) == key,
                  f'{item_id} {key}: setPotion stores the exact namespaced StringTag')

            native(0xa34430, group_ptr, tagged)
            wrapper = q(u, q(u, group + 16) - 8)
            clone = q(u, wrapper + 0x38)
            check(q(u, group_ptr) == group and q(u, wrapper) == 0x1328e18 and q(u, wrapper + 0x30) == item
                  and d(u, wrapper + 0x18) == 1 and d(u, wrapper + 0x44) == 0
                  and clone != tag and tagged_key(clone) == key,
                  f'{item_id} {key}: retail ITEM wrapper owns an independent Potion tag')
            release(tagged)

            selected = zalloc(16)
            native(q(u, q(u, wrapper)), selected, wrapper)
            obj = q(u, selected)
            selected_tag = q(u, obj + 0x30)
            check(q(u, obj + 0x28) == item and d(u, obj + 0x10) == 1 and d(u, obj + 0x3c) == 0
                  and not b(u, obj + 0x38) and selected_tag != clone and tagged_key(selected_tag) == key
                  and native(0x562670, selected) == potion,
                  f'{item_id} {key}: selecting the row delivers the matching potion after the source is released')

            seen = []
            label = alloc(u, 4)
            u.mem_write(label, b'P\0\0\0')

            def localize(game):
                seen.append(game.reg_read(UC_X86_REG_RDI) & 0xffffffff)
                return label

            native(q(u, vtable + 0x108), zalloc(0x28), item, selected, extra={0xaac9b0: localize})
            check(seen == [sids[kind]], f'{item_id} {key}: native name reads selected potion and stock form')
            release(selected)
    check(q(u, group + 16) - q(u, group + 8) == 8 * len(items) * len(potions),
          'all stock item forms and distinct potion identities coexist in the same private tab')
