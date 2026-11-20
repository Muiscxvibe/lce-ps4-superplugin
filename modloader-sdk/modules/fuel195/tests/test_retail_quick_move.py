"""Execute stock Quick Move and the compiled fuel195 vtable hook on the pinned image.

The inventory fixture supplies slot callbacks and unrelated UI layout state.
The image executes recipe and cooker-mask lookup, the stock fuel classifier,
stack copying and reference cleanup, and the native inventory transfer.
"""
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'strings195' / 'tests'))
from uc195 import IMAGE, IMAGE_SHA256, Machine
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_RCX, UC_X86_REG_RDI, UC_X86_REG_RDX, UC_X86_REG_RSI

QUICK_MOVE = 0x325100
FURNACE_MENU_VTABLE, QUICK_MOVE_VSLOT = 0x125B708, 0x68
CLASSIFIER = 0x320F20
TRANSFER, RECIPE_RESULT, ITEM_COPY = 0xE030, 0x3226F0, 0x3642B0
EMPTY_STACK, AIR_ITEM = 0x1416D98, 0x1417850
STOCK_COAL_ITEM, BLOCK_FALLBACK = 0x1417910, 0x1409F50
ITEM_HASH_TABLE, RECIPES = 0x1417788, 0x1416A80
SLOTS, STUBS = 39, 0x5200000
CORE_PAGE = 0x5300000


class FurnaceMachine(Machine):
    def __init__(self, item, source, count, fuel_count=0, fuel_capacity=64,
                 reject=False, recipe_mask=0, different_fuel=False, wrapper=None):
        super().__init__()
        self.wrapper = wrapper
        self.registered = {}
        self.routes, self.events, self.classifications = [], [], []
        self.recipe_count, self.copies = 0, 0
        self.u.mem_map(STUBS, 0x1000)
        self.menu = self.alloc(0x1A8)
        self.wq(self.menu, FURNACE_MENU_VTABLE)
        self.slot_array = self.alloc(SLOTS * 8)
        self.wq(self.menu + 0x30, self.slot_array)
        self.wq(self.menu + 0x38, self.slot_array + SLOTS * 8)
        self.player_stack = self.alloc(16)
        self.player = self.alloc(8)
        self.wq(self.player_stack, self.player)
        self.furnace = self.alloc(8)
        furnace_vtable = self.alloc(0xA0)
        self.wq(self.furnace, furnace_vtable)
        self.wq(self.menu + 0x178, self.furnace)
        self.wq(furnace_vtable + 0x90, self.stub(0x80, self.cooker_kind))
        self.slots = [self.alloc(0x20) for _ in range(SLOTS)]
        self.slot_indices = {slot: i for i, slot in enumerate(self.slots)}
        self.slot_vtable = self.alloc(0x68)
        for offset, callback in ((0x28, self.take), (0x30, self.can_place),
                                 (0x38, self.get_slot), (0x40, self.has_item),
                                 (0x48, self.set_slot), (0x50, self.changed),
                                 (0x58, self.limit),
                                 (0x60, self.limit)):
            self.wq(self.slot_vtable + offset, self.stub(offset, callback))
        self.reject, self.fuel_capacity = reject, fuel_capacity
        self.stock, self.custom, self.other = (self.new_item() for _ in range(3))
        self.air = self.new_item()
        self.wq(AIR_ITEM, self.air)
        self.wq(STOCK_COAL_ITEM, self.stock)
        self.registered[self.custom] = 1234
        self.empty = self.alloc(0xA8)
        self.u.mem_write(self.empty + 0x38, b'\1')
        self.wq(EMPTY_STACK, self.empty)
        self.wq(EMPTY_STACK + 8, 0)
        self.make_stock_classifier_fixture()
        for i, slot in enumerate(self.slots):
            self.wq(self.slot_array + 8 * i, slot)
            self.wq(slot, self.slot_vtable)
            self.wq(slot + 8, self.empty)
        self.item = {'stock': self.stock, 'custom': self.custom, 'other': self.other}[item]
        self.assign_new(self.slots[source] + 8, self.item, count)
        if fuel_count:
            self.assign_new(self.slots[1] + 8, self.other if different_fuel else self.item, fuel_count)
        self.make_recipes(self.item, recipe_mask)
        # The stock layout helper queries UI state absent in a headless fixture.
        self.replace[0xF430] = lambda _: 0
        if wrapper:
            code, entry, runtime = wrapper
            self.u.mem_map(CORE_PAGE, 0x4000)
            self.u.mem_write(CORE_PAGE, code)
            self.u.mem_write(runtime, struct.pack('<IIQIIQ', 1, 0, self.custom, 1234, 0, 0))
            address = FURNACE_MENU_VTABLE + QUICK_MOVE_VSLOT
            assert self.q(address) == QUICK_MOVE
            self.wq(address, entry)
        for address, callback in ((CLASSIFIER, self.on_classifier), (TRANSFER, self.on_transfer),
                                  (RECIPE_RESULT, self.on_recipe), (ITEM_COPY, self.on_copy)):
            self.u.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)

    def registers(self, *names):
        return tuple(self.u.reg_read(name) for name in names)

    def stub(self, offset, fn):
        address = STUBS + offset
        self.u.mem_write(address, b'\xc3')
        self.replace[address] = fn
        return address

    def cooker_kind(self, _):
        assert self.registers(UC_X86_REG_RDI, UC_X86_REG_RSI) == (self.furnace, 5)
        return 0

    def new_item(self):
        item = self.alloc(0xA0)
        self.wq(item, 0x126E638)
        self.wd(item + 0x18, 64)
        return item

    def assign_new(self, destination, item, count, aux=0):
        """Create a native ItemInstance and native shared_ptr in its destination."""
        model = self.alloc(0xA8)
        self.wq(model + 0x28, item)
        self.wd(model + 0x10, count)
        self.wd(model + 0x3C, aux)
        self.call(ITEM_COPY, (destination, model))

    def make_stock_classifier_fixture(self):
        """An empty block-item hash and a neutral default block material."""
        head, buckets = self.alloc(0x18), self.alloc(0x30)
        self.wq(ITEM_HASH_TABLE + 8, head)
        self.wq(ITEM_HASH_TABLE + 0x20, buckets)
        self.wq(ITEM_HASH_TABLE + 0x38, 0)
        self.wq(ITEM_HASH_TABLE + 0x40, 1)
        self.wq(buckets + 0x10, head)
        material, material_vtable = self.alloc(8), self.alloc(8)
        fallback_block = self.alloc(0x198)
        self.wq(material_vtable, self.stub(0x88, lambda _: 1))
        self.wq(material, material_vtable)
        self.wq(fallback_block + 0x190, material)
        self.wq(BLOCK_FALLBACK, fallback_block)

    def make_recipes(self, item, mask):
        recipes = self.alloc(0xF8)
        self.wq(RECIPES, recipes)
        for offset in (0, 0xA8):
            head = self.alloc(0x20)
            self.wq(recipes + offset + 8, head)
            self.wq(head, head)
            if mask:
                pair, result, node = self.alloc(16), self.alloc(16), self.alloc(0x20)
                self.assign_new(pair, item, 1, -1)
                self.assign_new(result, self.other, 1)
                self.wq(head, node)
                self.wq(node, head)
                self.wq(node + 0x10, self.q(pair))
                self.wq(node + 0x18, mask if offset else self.q(result))

    def on_classifier(self, *_):
        pair, = self.registers(UC_X86_REG_RDI)
        self.classifications.append(self.q(self.q(pair) + 0x28))

    def on_transfer(self, *_):
        self.routes.append(self.registers(UC_X86_REG_RDX, UC_X86_REG_RCX))

    def on_recipe(self, *_):
        self.recipe_count += 1

    def on_copy(self, *_):
        self.copies += 1

    def get_slot(self, _):
        out, slot = self.registers(UC_X86_REG_RDI, UC_X86_REG_RSI)
        self.wq(out, self.q(slot + 8))
        control = self.q(slot + 16)
        self.wq(out + 8, control)
        if control:
            self.wd(control + 8, self.d(control + 8) + 1)
        return out

    def has_item(self, _):
        slot, = self.registers(UC_X86_REG_RDI)
        return self.d(self.q(slot + 8) + 0x10) > 0

    def can_place(self, _):
        slot, pair = self.registers(UC_X86_REG_RDI, UC_X86_REG_RSI)
        index = self.slot_indices[slot]
        item = self.q(self.q(pair) + 0x28)
        self.events.append(('may_place', index))
        return index != 1 or (not self.reject and (item == self.stock or item in self.registered))

    def set_slot(self, _):
        slot, pair = self.registers(UC_X86_REG_RDI, UC_X86_REG_RSI)
        new_item, new_control = self.q(pair), self.q(pair + 8)
        old_control = self.q(slot + 16)
        self.events.append(('set', self.slot_indices[slot]))
        if new_control:
            self.wd(new_control + 8, self.d(new_control + 8) + 1)
        if old_control:
            # The native move owns a temporary source reference during set.
            assert self.d(old_control + 8) > 1
            self.wd(old_control + 8, self.d(old_control + 8) - 1)
        self.wq(slot + 8, new_item)
        self.wq(slot + 16, new_control)

    def changed(self, _):
        slot, = self.registers(UC_X86_REG_RDI)
        self.events.append(('changed', self.slot_indices[slot]))

    def limit(self, _):
        slot, = self.registers(UC_X86_REG_RDI)
        return self.fuel_capacity if self.slot_indices[slot] == 1 else 64

    def take(self, _):
        out, slot, player, stack = self.registers(
            UC_X86_REG_RDI, UC_X86_REG_RSI, UC_X86_REG_RDX, UC_X86_REG_RCX)
        self.events.append(('take', self.slot_indices[slot]))
        assert self.q(player) == self.player
        assert self.q(self.q(stack) + 0x28) == self.item
        self.wq(out, self.empty)
        self.wq(out + 8, 0)
        return out

    def stack(self, slot):
        return self.q(self.slots[slot] + 8)

    def release(self, pair):
        self.call(0x66B00, (pair, 0, 0))

    def assert_owned(self, pair):
        control = self.q(pair + 8)
        assert control in self.live and self.q(pair) in self.live
        assert self.d(control + 8) == 1

    def run(self, source):
        origin = self.slots[source] + 8
        original_stack, original_control = self.q(origin), self.q(origin + 8)
        out = self.alloc(16)
        method = self.q(self.q(self.menu) + QUICK_MOVE_VSLOT)
        assert method == (self.wrapper[1] if self.wrapper else QUICK_MOVE)
        assert self.call(method, (out, self.menu, self.player_stack, source), stack=(0,)) == out
        if self.q(origin) == self.empty:
            assert original_stack not in self.live and original_control not in self.live
        else:
            self.assert_owned(origin)
        for slot in self.slots:
            if self.q(slot + 8) != self.empty:
                self.assert_owned(slot + 8)
        if self.q(out) == self.empty:
            assert self.q(out + 8) == 0
            return 0
        self.assert_owned(out)
        assert self.q(out) != original_stack
        assert self.q(self.q(out) + 0x28) == self.item
        original_count = self.d(self.q(out) + 0x10)
        stack, control = self.q(out), self.q(out + 8)
        self.release(out)
        assert stack not in self.live and control not in self.live
        return original_count


def check(name, item, route, destination, source=3, count=5, wrapper=None,
          expected_source=0, expected_destination=5, initial_fuel=0, **flags):
    machine = FurnaceMachine(item, source, count, initial_fuel, wrapper=wrapper, **flags)
    result = machine.run(source)
    active_recipe = flags.get('recipe_mask', 0) & 1
    recipe_count = 0 if source < 3 else 2 if wrapper and item == 'custom' and active_recipe else 1
    assert machine.recipe_count == recipe_count, (name, machine.recipe_count)
    assert machine.routes == [route], (name, machine.routes)
    assert machine.d(machine.stack(source) + 0x10) == expected_source, name
    assert machine.d(machine.stack(destination) + 0x10) == expected_destination, name
    if result:
        assert result == count and ('take', source) in machine.events, (name, machine.events)
    else:
        assert expected_source == count and ('take', source) not in machine.events, (name, machine.events)
    assert result == (0 if expected_source == count else count), name
    classifier_used = source >= 3 and not active_recipe and (not wrapper or item != 'custom')
    assert machine.classifications == ([machine.item] if classifier_used else []), (
        name, machine.classifications)
    assert machine.copies >= 1, (name, machine.copies)
    assert all(machine.stack(i) == machine.empty for i in range(SLOTS)
               if i not in (source, destination) and not (i == 1 and initial_fuel)), name
    print(f'PASS {name}')


def build_wrapper(directory):
    """Link and extract the same C++ transfer routine installed by fuel195."""
    source = Path(__file__).resolve().parents[1] / 'fuel195.cpp'
    linked, raw = directory / 'quick_move.elf', directory / 'quick_move.bin'
    entry = 'fuel195_quick_move_wrapper'
    subprocess.run(['c++', '-std=c++17', '-O2', '-fno-pic', '-fno-pie', '-fno-exceptions',
                    '-fno-stack-protector', '-ffunction-sections', '-nostdlib', '-no-pie',
                    f'-Wl,--gc-sections,--build-id=none,-e,{entry},-Ttext={CORE_PAGE:#x}',
                    str(source), '-o', str(linked)], check=True)
    subprocess.run(['objcopy', '-O', 'binary', '--only-section=.text', linked, raw], check=True)
    symbols = subprocess.check_output(['nm', linked], text=True).splitlines()
    address = next(int(line.split()[0], 16) for line in symbols if line.endswith(' ' + entry))
    runtime = next(int(line.split()[0], 16) for line in symbols if line.endswith('12_GLOBAL__N_14g_rtE'))
    return raw.read_bytes(), address, runtime


def check_fuel_routes(wrapper):
    check('registered fuel from inventory', 'custom', (1, 2), 1, wrapper=wrapper)
    check('registered fuel from hotbar', 'custom', (1, 2), 1, source=31, wrapper=wrapper)
    check('vanilla coal still enters the fuel slot', 'stock', (1, 2), 1, wrapper=wrapper)
    check('unregistered item still enters the hotbar', 'other', (30, 39), 30, wrapper=wrapper)
    check('unregistered hotbar item returns to the inventory', 'other', (3, 30), 3, source=31, wrapper=wrapper)
    check('matching smelting recipe takes priority', 'custom', (0, 1), 0, recipe_mask=1, wrapper=wrapper)
    check('recipe for another cooker leaves fuel eligible', 'custom', (1, 2), 1, recipe_mask=2, wrapper=wrapper)
    check('registered furnace input returns to inventory', 'custom', (3, 39), 3, source=0, wrapper=wrapper)
    check('fuel slot rejects placement', 'custom', (1, 2), 1,
          expected_source=5, expected_destination=0, reject=True, wrapper=wrapper)
    check('fuel slot with different item remains untouched', 'custom', (1, 2), 1,
          expected_source=5, expected_destination=2, initial_fuel=2, different_fuel=True, wrapper=wrapper)
    check('full matching stack leaves source untouched', 'custom', (1, 2), 1,
          expected_source=5, expected_destination=64, initial_fuel=64, wrapper=wrapper)
    check('partially merges into a matching stack', 'custom', (1, 2), 1,
          expected_source=3, expected_destination=64, initial_fuel=62, wrapper=wrapper)
    check('slot capacity splits moves into an empty fuel slot', 'custom', (1, 2), 1,
          expected_source=3, expected_destination=2, fuel_capacity=2, wrapper=wrapper)


def main():
    assert hashlib.sha256(IMAGE.read_bytes()).hexdigest() == IMAGE_SHA256, 'unexpected image'
    check('stock behavior sends custom fuel to hotbar', 'custom', (30, 39), 30)
    with tempfile.TemporaryDirectory(prefix='fuel195-quick-move-') as directory:
        check_fuel_routes(build_wrapper(Path(directory)))
    print('fuel195 retail Quick Move: 14 scenarios passed')


if __name__ == '__main__':
    main()
