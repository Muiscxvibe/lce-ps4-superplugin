// Host the r72 private group with an instrumented native append boundary.
#include "creative.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
uintptr_t base;
unsigned append_calls, checks;
bool alias_clone;
uintptr_t entries[LCE195_CREATIVE_CAPACITY];
alignas(8) uint8_t wrappers[LCE195_CREATIVE_CAPACITY][0x50];
alignas(8) uint64_t clones[LCE195_CREATIVE_CAPACITY][2];

void check(bool value) { ++checks; assert(value); }
int held(void *) { return 1; }
int read(void *, uintptr_t p, void *out, size_t n) {
    if (!p) return 0;
    memcpy(out, (const void *)p, n);
    return 1;
}
template<class T> T get(uintptr_t p) {
    T result{};
    memcpy(&result, (const void *)p, sizeof result);
    return result;
}
template<class T> void put(uintptr_t p, T value) {
    memcpy((void *)p, &value, sizeof value);
}

struct Source {
    alignas(8) uint8_t stack[0xa8]{};
    uintptr_t control[3]{}, tag[2]{};
    lce195_creative_tagged_row row{};
    Source(uintptr_t item, uintptr_t identity) {
        row = {{(uintptr_t)stack, (uintptr_t)control}, item, 0};
        control[0] = base + 0x1214808u;
        control[1] = 1;
        control[2] = row.pair.object;
        tag[0] = base + 0x122ede0u;
        tag[1] = identity;
        put((uintptr_t)stack + 0x10, 1u);
        put((uintptr_t)stack + 0x28, item);
        put((uintptr_t)stack + 0x30, (uintptr_t)tag);
    }
};

void append(uintptr_t *p, uintptr_t item, uint32_t aux, uintptr_t tag) {
    auto *group = (uintptr_t *)*p;
    if (!group[1]) {
        group[1] = group[2] = (uintptr_t)entries;
        group[3] = (uintptr_t)(entries + LCE195_CREATIVE_CAPACITY);
    }
    const uintptr_t wrapper = (uintptr_t)wrappers[append_calls];
    put(wrapper, base + 0x1328e18u);
    put(wrapper + 0x18, 1u);
    put(wrapper + 0x30, item);
    put(wrapper + 0x44, aux);
    if (tag) {
        memcpy(clones[append_calls], (void *)tag, sizeof clones[0]);
        put(wrapper + 0x38, alias_clone ? tag : (uintptr_t)clones[append_calls]);
    }
    put(group[2], wrapper);
    group[2] += 8;
    ++append_calls;
}
}

extern "C" void lce195_test_creative_append(uintptr_t *p, uintptr_t item, int aux) {
    append(p, item, (uint32_t)aux, 0);
}
extern "C" void lce195_test_creative_append_stack(uintptr_t *p, const lce195_creative_stack_pair *pair) {
    append(p, get<uintptr_t>(pair->object + 0x28), get<uint32_t>(pair->object + 0x3c),
           get<uintptr_t>(pair->object + 0x30));
}

int main() {
    auto *image = calloc(1, 0x143d200u);
    base = (uintptr_t)image;
    lce195_additive_creative c{};
    c.base = base;
    c.held = held;
    c.read = read;

    alignas(8) uint8_t stock[0x50]{};
    uintptr_t stock_slot = (uintptr_t)stock;
    const uintptr_t stock_item = 0x3000u;
    put((uintptr_t)stock + 0x30, stock_item);
    put(base + 0x143d028u, (uintptr_t)&stock_slot);
    put(base + 0x143d030u, (uintptr_t)(&stock_slot + 1));
    put(base + 0x143d038u, (uintptr_t)(&stock_slot + 1));

    check(lce195_creative_build_provided(&c) == 1);
    Source first(stock_item, 1), second(stock_item, 2);
    lce195_creative_tagged_row rows[] = {first.row, second.row};
    check(lce195_creative_append_tagged_batch(&c, rows, LCE195_CREATIVE_CAPACITY + 1) == 0);
    rows[1].pair.control = 0;
    check(lce195_creative_append_tagged_batch(&c, rows, 2) == 0);
    check(c.count == 0 && append_calls == 0);
    rows[1] = second.row;
    check(lce195_creative_append_tagged_batch(&c, rows, 2) == 1);
    check(c.count == 2 && append_calls == 2);
    for (unsigned i = 0; i < 2; ++i) {
        uintptr_t wrapper = lce195_creative_wrapper(&c, i);
        check(wrapper && get<uintptr_t>(wrapper + 0x30) == stock_item);
        check(get<uintptr_t>(get<uintptr_t>(wrapper + 0x38) + 8) == i + 1);
    }

    const lce195_creative_plain_row blocks[] = {{0x5000u, 0}, {0x5000u, 1}};
    check(lce195_creative_append_plain_batch(&c, blocks, 2) == 1);
    check(c.count == 4 && !c.row_tagged[2] && !c.row_tagged[3]);
    check(lce195_creative_append_stack(&c, &first.row.pair, stock_item, 0) == 1);
    check(c.count == 5 && c.row_tagged[4]);
    alias_clone = true;
    check(lce195_creative_append_tagged_batch(&c, rows, 2) == -1);
    check(c.state == 4 && !lce195_creative_wrapper(&c, 0));
    free(image);
    printf("Creative tagged batches: %u checks passed\n", checks);
}
