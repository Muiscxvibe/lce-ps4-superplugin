#include "player_channel.hpp"

namespace lce_player_channel {
bool Channel::start(uintptr_t image,lce_external_players::Players &players,
                    lce_session_transport &session,const lce_native_bridge_ops &ops,
                    lce_send_failure_fn failure,void *failure_context) {
    if (attempted_ || !image || image>UINTPTR_MAX-0x134aba0 || !failure ||
        !players.local() || !players.host() || !players.socket() ||
        session.state!=LCE_SESSION_ACTIVE || !session.connection.reader.has_id ||
        session.connection.reader.assigned_id==0 || session.connection.reader.assigned_id==0xff)
        return false;
    attempted_=true;
    auto *original=reinterpret_cast<const uintptr_t *>(image+0x134aaa8);
    auto **vptr=reinterpret_cast<const uintptr_t **>(players.local());
    if (*vptr!=original || !table_.prepare(image,original-2,
            reinterpret_cast<uintptr_t>(&lce_player_send_hook))) return false;
    if (!lce_native_bridge_attach(&bridge_,&session,players.socket(),&ops)) return false;
    binding_={players.local(),players.host(),&bridge_,
              reinterpret_cast<lce_player_send_fn>(image+0xdfce70),failure,failure_context};
    if (!lce_player_send_install_binding(&binding_)) {
        lce_native_bridge_detach(&bridge_); return false;
    }
    local_=players.local(); original_=original;
    __atomic_store_n(vptr,table_.address_point(),__ATOMIC_RELEASE);
    active_=true;
    return true;
}
bool Channel::stop() {
    if (!active_ || !local_ || !original_) return false;
    auto **vptr=reinterpret_cast<const uintptr_t **>(local_);
    if (__atomic_load_n(vptr,__ATOMIC_ACQUIRE)!=table_.address_point()) return false;
    __atomic_store_n(vptr,original_,__ATOMIC_RELEASE);
    lce_player_send_remove_binding(&binding_);
    lce_native_bridge_detach(&bridge_);
    active_=false;
    return true;
}
}
