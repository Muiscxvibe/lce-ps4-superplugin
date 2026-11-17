#include "account_join_route.hpp"
#include "auth_client.h"

namespace lce_account_join {
Selection Route::select(const void *record,int pad) {
    uint32_t index=0;
    if (!lce_join_list::endpoint_index(record,&index)) return Selection::Native;
    // Native authenticated join is currently verified only for primary pad 0.
    if (pending_ || index>=servers_.count || index>=lce_servers::kMaxServers || pad!=0 ||
        !ops_.ready || !ops_.join_authenticated || !ops_.report || !ops_.ready(ops_.context) ||
        !flow_.start(pad)) return Selection::Rejected;
    selected_=servers_.entries[index];pad_=pad;pending_=true;
    return Selection::Started;
}
Selection Route::select_credentials(const void *record,int pad,const char *username,const char *password,bool remember) {
    uint32_t index=0;
    if (!lce_join_list::endpoint_index(record,&index)) return Selection::Native;
    if (pending_ || index>=servers_.count || index>=lce_servers::kMaxServers || pad!=0 ||
        !ops_.ready || !ops_.join_authenticated || !ops_.report || !ops_.ready(ops_.context) ||
        !flow_.start_with_credentials(pad,username,password,remember)) return Selection::Rejected;
    selected_=servers_.entries[index];pad_=pad;pending_=true;
    return Selection::Started;
}
Selection Route::select_token(const void *record,int pad,const char *token) {
    uint32_t index=0;
    if(!lce_join_list::endpoint_index(record,&index))return Selection::Native;
    if(pending_ || index>=servers_.count || index>=lce_servers::kMaxServers || pad!=0 ||
       !ops_.ready || !ops_.join_authenticated || !ops_.report || !ops_.ready(ops_.context) ||
       !flow_.start_with_token(pad,token))return Selection::Rejected;
    selected_=servers_.entries[index];pad_=pad;pending_=true;return Selection::Started;
}
void Route::tick() {
    if (!pending_) return;
    flow_.tick();
    const auto state=flow_.state();
    if (state==lce_account_flow::State::Accepted) {
        lce_auth_result result{};
        bool ok=flow_.take_result(&result) && result.accepted && result.ticket_id[0] && result.ticket_key[0] &&
                ops_.join_authenticated(ops_.context,selected_,pad_,result);
        lce_auth_wipe(&result,sizeof result); // owned result includes bearer material
        pending_=false;pad_=-1;ops_.report(ops_.context,ok);
    } else if (state==lce_account_flow::State::Failed || state==lce_account_flow::State::Cancelled) {
        flow_.reset();pending_=false;pad_=-1;ops_.report(ops_.context,false);
    }
}
}
