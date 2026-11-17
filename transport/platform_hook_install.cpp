#include "platform_hook_install.hpp"

namespace lce_platform_hook {
namespace {
template <typename T> T fn(uintptr_t base,uintptr_t offset) {
    return reinterpret_cast<T>(base+offset);
}
}
lce_join_adapter::Ops Install::make_ops(uintptr_t b,const lce_join_list::Ops &rows,
        bool (*ready)(),int (*join)(const lce_servers::Server &,int,int),
        void (*info)(const lce_servers::Server &,void *,lce_join_adapter::Complete,void *),
        bool (*may_append_rows)(void *), void *row_gate_context) {
    lce_join_adapter::Ops result{};
    result.rows=rows;
    result.native_list=fn<decltype(result.native_list)>(b,0xde0c70);
    result.native_join=fn<decltype(result.native_join)>(b,0xde0450);
    result.native_full_info=fn<decltype(result.native_full_info)>(b,0xde0f20);
    result.native_quick_match=fn<decltype(result.native_quick_match)>(b,0xde04e0);
    result.backend_ready=ready; result.backend_join=join; result.backend_full_info=info;
    result.may_append_rows=may_append_rows; result.row_gate_context=row_gate_context;
    return result;
}
lce_session_queries::Native Install::native_queries(uintptr_t b) {
    lce_session_queries::Native n{};
    n.player_count=fn<decltype(n.player_count)>(b,0xddfca0);
    n.local_player=fn<decltype(n.local_player)>(b,0xde0fd0);
    n.player_by_small_id=fn<decltype(n.player_by_small_id)>(b,0xde1080);
    n.host_player=fn<decltype(n.host_player)>(b,0xde10b0);
    n.is_host=fn<decltype(n.is_host)>(b,0xde10e0);
    n.in_session=fn<decltype(n.in_session)>(b,0xde1190);
    n.in_gameplay=fn<decltype(n.in_gameplay)>(b,0xde11a0);
    n.ready_to_play_or_idle=fn<decltype(n.ready_to_play_or_idle)>(b,0xde11c0);
    return n;
}
Install::Install(uintptr_t image,void *manager,const lce_servers::List &servers,
        const lce_join_list::Ops &rows,bool (*ready)(),
        int (*join)(const lce_servers::Server &,int,int),
        void (*info)(const lce_servers::Server &,void *,lce_join_adapter::Complete,void *),
        bool (*may_append_rows)(void *), void *row_gate_context,
        const lce_session_queries::Custom &custom)
    : image_(image),manager_(manager),binding_(manager,servers,make_ops(image,rows,ready,join,info,may_append_rows,row_gate_context)),
      queries_(native_queries(image),custom) {}
bool Install::publish() {
    if (published_ || !image_ || !manager_ || image_>UINTPTR_MAX-0x143b148 ||
        *reinterpret_cast<void **>(image_+0x143b148)!=manager_) return false;
    auto **vptr=reinterpret_cast<const uintptr_t **>(manager_);
    auto *original=reinterpret_cast<const uintptr_t *>(image_+0x1349db8);
    if (__atomic_load_n(vptr,__ATOMIC_ACQUIRE)!=original) return false;
    // Validate the full stock vtable before claiming global dispatchers.
    lce_join_hook::Vtable check;
    if (!check.prepare(image_,original-2,{1,2,3}) || !binding_.activate()) return false;
    if (!queries_.bind()) { binding_.deactivate(); return false; }
    auto hooks=binding_.replacements();
    if (!queries_.replacements(&hooks) || !table_.prepare(image_,original-2,hooks)) {
        queries_.unbind(); binding_.deactivate(); return false;
    }
    // The replacement and its callback owners are initialized before the
    // switch and retained for process lifetime. A concurrent dispatch that
    // already loaded the original table may finish using that still-live
    // table. Do not overwrite another component's intervening vptr change.
    auto *expected=original;
    if (!__atomic_compare_exchange_n(vptr,&expected,table_.address_point(),false,
            __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        queries_.unbind(); binding_.deactivate(); return false;
    }
    published_=true;
    return true;
}
}
