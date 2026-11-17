#include "join_list.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
using namespace lce_join_list;
static int live;
static bool fail_reserve;
static void *allocate(size_t n) { void *p = std::malloc(n); if (p) ++live; return p; }
static void release(void *p) { if (p) { --live; std::free(p); } }
static void init(void *) {}
static bool reserve(Vector *v, size_t extra) {
    if (fail_reserve) return false;
    size_t used = v->begin ? static_cast<size_t>(v->end - v->begin) : 0;
    void **next = static_cast<void **>(std::realloc(v->begin, (used + extra) * sizeof(void *)));
    if (!next) return false;
    v->begin = next; v->end = next + used; v->capacity = v->end + extra;
    return true;
}
int main() {
    unsigned char temporary[0x208];uint32_t temporary_index=99;
    assert(!make_join_record(temporary,sizeof temporary-1,0));
    assert(!make_join_record(temporary,sizeof temporary,8));
    assert(make_join_record(temporary,sizeof temporary,2));
    assert(endpoint_index(temporary,&temporary_index) && temporary_index==2);
    Ops ops{allocate, allocate, release, init, reserve}; Vector v{};
    assert(append(&v, "Codeys LCE Server", 0, 0, false, ops) == BackendUnavailable);
    assert(!v.begin && live == 0);
    fail_reserve = true;
    assert(append(&v, "Server", 0, 0, true, ops) == OutOfMemory && live == 0);
    fail_reserve = false;
    assert(append(&v, "Codeys LCE Server", 3, 2, true, ops) == Added);
    assert(v.end - v.begin == 1 && live == 2);
    auto *record = static_cast<unsigned char *>(*v.begin);
    uint32_t index; assert(endpoint_index(record, &index) && index == 3);
    assert(record[0xbc] == 0x6f && record[0xbd] == 0x09);
    assert(record[0x1f9] == 1 && record[0x1fa] == 2 && record[0x1fb] == 1);
    assert(record[0xb8] == 16);
    uint16_t *label; std::memcpy(&label, record + 0xb0, sizeof label);
    assert(label[0] == 'C' && label[16] == 0);
    assert(append(&v, "Bad\nName", 0, 0, true, ops) == InvalidInput);
    record[0] ^= 1; assert(!endpoint_index(record, &index));
    release(label); release(record); std::free(v.begin);
    assert(live == 0);
    std::puts("join-list entry checks passed");
}
