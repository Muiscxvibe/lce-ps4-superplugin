#pragma once
#include "account_flow.hpp"
#include "join_list.hpp"
#include "../ps4-toolchain/loader/server_config/servers.hpp"

namespace lce_account_join {
enum class Selection { Native, Started, Rejected };
struct Ops {
    void *context;
    bool (*ready)(void *);
    bool (*join_authenticated)(void *,const lce_servers::Server &,int,const lce_auth_result &);
    void (*report)(void *,bool);
};
// UI-thread only. Copies the configured endpoint; never retains a scene or a
// game-owned session record across the asynchronous keyboard/network flow.
class Route {
public:
    Route(lce_account_flow::Flow &flow,const lce_servers::List &servers,const Ops &ops)
        : flow_(flow),servers_(servers),ops_(ops) {}
    Selection select(const void *record,int pad);
    Selection select_credentials(const void *record,int pad,const char *username,const char *password,bool remember=false);
    Selection select_token(const void *record,int pad,const char *token);
    void tick();
private:
    lce_account_flow::Flow &flow_;
    lce_servers::List servers_{};
    Ops ops_{};
    lce_servers::Server selected_{};
    int pad_=-1;
    bool pending_=false;
};
}
