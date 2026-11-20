// Generic multi-entry smelting publisher for 1.95, generalised from
// steel_furnace_195/native.cpp (all of its RVAs re-verified against the image).
//
// FurnaceRecipes (object 0xf8, global 0x1416a80, built once by wrapper 0x325fa0
// from the static chain 0x477cd1, right after Items::init 0x477ccc):
//   +0x00 unordered_map<II*, II*>   input -> result   operator[] 0x329640
//   +0x50 unordered_map<II*, float> output -> xp      operator[] 0x329820
//   +0xa8 unordered_map<II*, u32>   input -> mask     operator[] 0x329a00
// Keys are ItemInstance POINTERS (FNV over the pointer bytes, 0x32965b);
// node: next +0, prev +8, key +0x10, value +0x18; list head +8, size +0x10.
// Lookups are linear scans comparing Item* and aux (-1 wildcard):
// getResult 0x3226f0, mask test 0x329ca0, XP 0x329e50. operator[] inserts at
// the list front (0x329759..0x3297b8), so a new entry shadows a stock one.
#include "items195.h"

namespace {
bool held(lce195x_env *e) { return e->m.held(e->m.ctx) == 1; }
bool rd(lce195x_env *e, uintptr_t a, void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.read(e->m.ctx, a, v, n) == 1; }
bool wr(lce195x_env *e, uintptr_t a, const void *v, size_t n) { return a && a <= UINTPTR_MAX - n && held(e) && e->m.write(e->m.ctx, a, v, n) == 1; }
const uintptr_t kOff[3] = {0, 0x50, 0xa8};

bool scan(lce195x_furnace_publisher *p, unsigned m) {
    lce195x_env *e = p->env;
    uintptr_t map = p->owner + kOff[m], head = 0, node = 0, count = 0;
    if (!rd(e, map + 8, &head, 8) || !head || !rd(e, map + 16, &count, 8) || count > 1024 || !rd(e, head, &node, 8)) return false;
    for (uintptr_t i = 0; i < count; i++) {
        uintptr_t next = 0, key = 0, value = 0, item = 0; uint8_t empty = 1;
        if (!node || node == head || !rd(e, node, &next, 8) || !rd(e, node + 0x10, &key, 8) || !key || !rd(e, node + 0x18, &value, 8) ||
            !rd(e, key + 0x38, &empty, 1) || empty || !rd(e, key + 0x28, &item, 8) || !item) return false;
        p->stock[m][i].node = node; p->stock[m][i].key = key; p->stock[m][i].value = value;
        node = next;
    }
    if (node != head) return false;
    p->stock_count[m] = count;
    return true;
}
uint64_t value_of(const lce195x_furnace_publisher *p, unsigned m, unsigned i, const lce195x_smelt &d) {
    if (m == 0) return p->outputs[i];
    if (m == 1) { uint32_t bits; __builtin_memcpy(&bits, &d.xp, 4); return bits; }
    return d.mask ? d.mask : LCE195X_COOK_FURNACE;
}
// every stock node unchanged, exactly `added` new nodes whose key/value are ours
bool intact(lce195x_furnace_publisher *p, unsigned m, unsigned added, const lce195x_smelt *defs) {
    lce195x_env *e = p->env;
    uintptr_t head = 0, count = 0, node = 0;
    if (!rd(e, p->owner + kOff[m] + 8, &head, 8) || !head || !rd(e, p->owner + kOff[m] + 16, &count, 8) ||
        count != p->stock_count[m] + added || !rd(e, head, &node, 8)) return false;
    unsigned old = 0, fresh = 0;
    for (uintptr_t i = 0; i < count; i++) {
        uintptr_t next = 0, key = 0, value = 0;
        if (!node || node == head || !rd(e, node, &next, 8) || !rd(e, node + 0x10, &key, 8) || !rd(e, node + 0x18, &value, 8)) return false;
        uint64_t j = 0;
        while (j < p->stock_count[m] && p->stock[m][j].node != node) j++;
        if (j < p->stock_count[m]) {
            if (p->stock[m][j].key != key || p->stock[m][j].value != value) return false;
            old++;
        } else {
            unsigned k = 0;
            while (k < added && key != (m == 1 ? p->outputs[k] : p->inputs[k])) k++;
            if (k == added) return false;
            uint64_t want = value_of(p, m, k, defs[k]);
            if (m == 0 ? value != want : (uint32_t)value != (uint32_t)want) return false;
            fresh++;
        }
        node = next;
    }
    return node == head && old == p->stock_count[m] && fresh == added;
}
}  // namespace

extern "C" int lce195x_furnace_publish(lce195x_furnace_publisher *p, const lce195x_smelt *defs, unsigned count) {
    if (!p || p->state || !p->env || !p->owner || !defs || !count || count > LCE195X_SMELT_MAX) return 0;
    lce195x_env *e = p->env;
    const lce195x_natives &n = e->n;
    if (!e->m.held || !e->m.read || !e->m.write || !n.alloc || !n.stack_ctor || !n.furnace_map[0] || !n.furnace_map[1] || !n.furnace_map[2]) return 0;
    p->state = 3;
    if (!held(e)) return 0;
    for (unsigned i = 0; i < count; i++) {
        const lce195x_smelt &d = defs[i];
        float xp = d.xp;
        if (!d.input_item || !d.output_item || d.input_item == d.output_item || !d.output_count || d.output_count > 64 ||
            !(d.input_aux == -1 || (d.input_aux >= 0 && d.input_aux <= 32767)) || d.output_aux < 0 || d.output_aux > 32767 ||
            !(xp >= 0.0f && xp <= 100.0f) || d.mask > 0xf) return 0;
        // one entry per input Item (linear scans would make a second one dead)
        for (unsigned j = 0; j < i; j++) if (defs[j].input_item == d.input_item) return 0;
    }
    for (unsigned m = 0; m < 3; m++) if (!scan(p, m)) return 0;
    p->count = count; p->state = 1;
    for (unsigned i = 0; i < count; i++) {
        const lce195x_smelt &d = defs[i];
        p->inputs[i] = (uintptr_t)n.alloc(0xa8);
        p->outputs[i] = (uintptr_t)n.alloc(0xa8);
        if (!p->inputs[i] || !p->outputs[i]) goto uncertain;
        n.stack_ctor((void *)p->inputs[i], (void *)d.input_item, 1, d.input_aux);
        n.stack_ctor((void *)p->outputs[i], (void *)d.output_item, (int)d.output_count, d.output_aux);
        for (unsigned k = 0; k < 2; k++) {
            uintptr_t s = k ? p->outputs[i] : p->inputs[i], item = 0; uint32_t c = 0, a = 1; uint8_t empty = 1;
            if (!rd(e, s + 0x28, &item, 8) || item != (k ? d.output_item : d.input_item) || !rd(e, s + 0x10, &c, 4) || c != (k ? d.output_count : 1u) ||
                !rd(e, s + 0x3c, &a, 4) || a != (uint32_t)(k ? d.output_aux : d.input_aux) || !rd(e, s + 0x38, &empty, 1) || empty) goto uncertain;
        }
        for (unsigned m = 0; m < 3; m++) {
            uintptr_t key = m == 1 ? p->outputs[i] : p->inputs[i];
            uintptr_t slot = n.furnace_map[m](p->owner + kOff[m], &key);
            uint64_t zero = 1, want = value_of(p, m, i, d);
            if (!slot || !rd(e, slot, &zero, m == 0 ? 8 : 4) || (m == 0 ? zero : (uint32_t)zero)) goto uncertain;  // fresh node, zero-initialised
            if (!wr(e, slot, &want, m == 0 ? 8 : 4)) goto uncertain;
        }
        p->published = i + 1;
    }
    for (unsigned m = 0; m < 3; m++) if (!intact(p, m, count, defs)) goto uncertain;
    p->state = 2;
    return 1;
uncertain:
    p->state = 4;
    return -1;
}
