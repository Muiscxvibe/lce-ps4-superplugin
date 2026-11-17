#include "join_adapter.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
using namespace lce_join_adapter;
using lce_join_list::Vector;
static bool available, owner_thread=true;
static int native_calls, backend_calls, complete_calls;
static Vector rows{};
static void *alloc(size_t n) { return std::malloc(n); }
static void release(void *p) { std::free(p); }
static void init(void *) {}
static bool reserve(Vector *v, size_t extra) {
    size_t n = v->begin ? static_cast<size_t>(v->end - v->begin) : 0;
    auto **p = static_cast<void **>(std::realloc(v->begin, (n + extra) * sizeof(void *)));
    if (!p) return false;
    v->begin = p; v->end = p + n; v->capacity = v->end + extra; return true;
}
static Vector *native_list(void *, int, int, bool) { ++native_calls; return &rows; }
static int native_join(void *, void *, int, int) { ++native_calls; return 42; }
static void native_info(void *, void *, Complete cb, void *ctx) { ++native_calls; cb(true, ctx); }
static bool ready() { return available; }
static bool may_append(void *) { return owner_thread; }
static int backend_join(const lce_servers::Server &s, int mask, int pad) {
    assert(!std::strcmp(s.host, "192.168.1.165") && s.port == 25565 && mask == 1 && pad == 0);
    ++backend_calls; return 0;
}
static void backend_info(const lce_servers::Server &, void *, Complete cb, void *ctx) {
    ++backend_calls; cb(true, ctx);
}
static void complete(bool ok, void *ctx) { ++complete_calls; *static_cast<bool *>(ctx) = ok; }
int main() {
    lce_servers::List servers{}; servers.count = 1;
    std::strcpy(servers.entries[0].label, "Codeys LCE");
    std::strcpy(servers.entries[0].host, "192.168.1.165"); servers.entries[0].port = 25565;
    Ops ops{{alloc, alloc, release, init, reserve}, native_list, native_join, native_info,
            ready, backend_join, backend_info, may_append, nullptr};
    Adapter adapter(servers, ops); unsigned char native_row[0x208]{}; bool result = false;
    assert(adapter.get_list(nullptr, 0, 1, false) == &rows && !rows.begin);
    assert(adapter.join(nullptr, native_row, 1, 0) == 42 && native_calls == 2);
    adapter.full_info(nullptr, native_row, complete, &result);
    assert(result && native_calls == 3 && complete_calls == 1);
    available = true;
    owner_thread=false;
    assert(adapter.get_list(nullptr, 0, 1, false) == &rows && !rows.begin);
    owner_thread=true;
    assert(adapter.get_list(nullptr, 0, 1, false) == &rows && rows.end - rows.begin == 1);
    void *own = rows.begin[0];
    assert(adapter.join(nullptr, own, 1, 0) == 0 && backend_calls == 1);
    result = false; adapter.full_info(nullptr, own, complete, &result);
    assert(result && backend_calls == 2 && complete_calls == 2);
    // Minecraft can discard a refreshed duplicate but keep the old row.
    assert(adapter.get_list(nullptr,0,1,false)==&rows && rows.end-rows.begin==2);
    void *duplicate=rows.begin[1], *duplicate_label;
    std::memcpy(&duplicate_label,static_cast<char *>(duplicate)+0xb0,sizeof duplicate_label);
    release(duplicate_label); release(duplicate); --rows.end;
    assert(adapter.join(nullptr,own,1,0)==0 && backend_calls==3);
    result=false; adapter.full_info(nullptr,own,complete,&result);
    assert(result && backend_calls==4 && complete_calls==3);
    available = false;
    assert(adapter.join(nullptr, own, 1, 0) == 1 && native_calls == 6);
    result = true; adapter.full_info(nullptr, own, complete, &result);
    assert(!result && complete_calls == 4);
    assert(adapter.get_list(nullptr, 0, 1, false) == &rows && native_calls == 7);
    assert(adapter.join(nullptr, own, 1, 0) == 1 && native_calls == 7);
    void *label; std::memcpy(&label, static_cast<char *>(own) + 0xb0, sizeof label);
    release(label); release(own); std::free(rows.begin);
    std::puts("join adapter routing checks passed");
}
