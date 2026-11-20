#pragma once
namespace nbt_frame_owner {
enum class Decision { Ignore, Leave, Bind };
inline Decision decide(bool has_owner,bool same_owner,bool owner_current,bool nbt_tab) {
 if(has_owner&&!same_owner&&owner_current)return Decision::Ignore;
 if(!nbt_tab)return same_owner?Decision::Leave:Decision::Ignore;
 return Decision::Bind;
}
}
