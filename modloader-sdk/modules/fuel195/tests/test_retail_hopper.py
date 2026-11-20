"""Exercise the exact retail Hopper placement gate against the fuel runtime."""
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_retail_quick_move import CORE_PAGE, FurnaceMachine, IMAGE, IMAGE_SHA256

FURNACE_BLOCK_ENTITY = 0x125B190
HOPPER_CAN_PLACE = 0x365BA0
HOPPER_MOVE = 0x3655D0
CONTAINER_OFFSET = 0x50
CONTAINER_TABLE = 0x125B4E0
DIRECT_CAN_PLACE = 0x323650
FACED_CAN_PLACE = 0x3237D0


def build_probe(directory):
    elf, binary = directory / 'probe.elf', directory / 'probe.bin'
    entry = 'fuel195_canplace_wrapper'
    bind = 'fuel195_bind_natives'
    subprocess.run(['c++', '-std=c++17', '-O2', '-fno-pic', '-fno-pie', '-fno-exceptions',
                    '-fno-stack-protector', '-ffunction-sections', '-nostdlib', '-no-pie',
                    f'-Wl,--gc-sections,--build-id=none,-e,{entry},--undefined={bind},-Ttext={CORE_PAGE:#x}',
                    ROOT / 'fuel195.cpp', '-o', elf], check=True)
    subprocess.run(['objcopy', '-O', 'binary', '--only-section=.text', '--only-section=.rodata', elf, binary], check=True)
    symbols = {parts[-1]: int(parts[0], 16) for line in subprocess.check_output(['nm', elf], text=True).splitlines()
               if len(parts := line.split()) == 3}
    return binary.read_bytes(), symbols[entry], symbols[bind], symbols['_ZN12_GLOBAL__N_14g_rtE']


def evaluate(probe):
    machine = FurnaceMachine('custom', 3, 5)
    code, entry, bind, runtime = probe
    machine.u.mem_map(CORE_PAGE, 0x5000)
    machine.u.mem_write(CORE_PAGE, code)
    machine.u.mem_write(runtime, struct.pack('<IIQIIQ', 1, 0, machine.custom, 1234, 0, 0))
    machine.call(bind, (runtime, 0), stack=(0,))
    be = machine.alloc(0x450)
    container = be + CONTAINER_OFFSET
    storage = machine.alloc(0x48)
    machine.wq(be, FURNACE_BLOCK_ENTITY)
    machine.wq(container, CONTAINER_TABLE)
    machine.wq(be + 0x58, storage)
    machine.wq(storage + 0x18, machine.empty)
    # Existing publication clones the primary vtable and switches the object vptr.
    primary = machine.alloc(0x438)
    machine.u.mem_write(primary, bytes(machine.u.mem_read(FURNACE_BLOCK_ENTITY - 0x20, 0x438)))
    primary += 0x20
    machine.wq(primary + 0x188, entry)
    machine.wq(be, primary)
    assert machine.q(CONTAINER_TABLE + 0x88) == DIRECT_CAN_PLACE
    assert machine.q(CONTAINER_TABLE + 0xB8) == FACED_CAN_PLACE
    custom = machine.slots[3] + 8
    side = machine.alloc(8)
    held = machine.q(custom + 8)

    def accepted(pair, slot):
        return bool(machine.call(HOPPER_CAN_PLACE, (container, pair, slot, side), stack=(0,)) & 0xff)

    assert not accepted(custom, 1), 'existing primary hook should miss this Hopper path'
    assert machine.d(held + 8) == 1
    # Exact data slot. Both retail functions ignore the unused fourth argument.
    machine.wq(CONTAINER_TABLE + 0x88, FACED_CAN_PLACE)
    assert accepted(custom, 1), 'sided hopper should accept custom fuel'
    assert accepted(custom, 0), 'hopper should still accept input'
    assert not accepted(custom, 2), 'hopper must reject insertion into output'
    stock, unrelated = machine.alloc(16), machine.alloc(16)
    machine.assign_new(stock, machine.stock, 1)
    machine.assign_new(unrelated, machine.other, 1)
    assert accepted(stock, 1), 'vanilla coal should still be allowed'
    assert not accepted(unrelated, 1), 'unrelated items must still be rejected'
    assert machine.d(held + 8) == 1
    machine.assert_owned(stock)
    machine.assert_owned(unrelated)
    out = machine.alloc(16)
    result = machine.call(HOPPER_MOVE, (out, 0, container, custom, 1, side), stack=(0,))
    assert result == out and machine.q(out) == machine.empty
    assert machine.q(custom) == 0 and machine.q(custom + 8) == 0
    assert machine.q(machine.q(storage + 0x18) + 0x28) == machine.custom
    assert machine.d(machine.q(storage + 0x18) + 0x10) == 5
    machine.assert_owned(storage + 0x18)
    print('fuel195 retail hopper placement probe: 14 assertions passed')


if __name__ == '__main__':
    assert hashlib.sha256(IMAGE.read_bytes()).hexdigest() == IMAGE_SHA256, 'unexpected image'
    with tempfile.TemporaryDirectory(prefix='fuel195-hopper-probe-') as directory:
        evaluate(build_probe(Path(directory)))
