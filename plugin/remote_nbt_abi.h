#pragma once
// C ABI between remote_actions.cpp (owns the r72 self_player queue headers, which cannot be
// included next to debug_main) and remote_nbt.cpp (server NBT / count bridge).
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct lce_remote_req {unsigned action;uint32_t item_id;uint64_t ticket;uint64_t count_observation;int32_t count_slot,count_expected,count_desired;};
struct lce_remote_count_reply {uint64_t observation;int32_t slot,count,damage;bool undo_available,damage_valid,ench_valid;unsigned ench_n;int16_t ench_id[64],ench_level[64];};
bool mc195_take_remote_request(struct lce_remote_req *);
void mc195_finish_remote_request(uint64_t ticket,unsigned result,const struct lce_remote_count_reply *);
void mc195_finish_remote_action(uint64_t ticket,unsigned result);
bool mc195_remote_action_served(unsigned action);
bool mc195_remote_actions_active(void);
bool mc195_remote_send_text(const char *);
bool mc195_remote_send_action(unsigned action,uint32_t item_id);
/* Called every owner tick from debug_main; drives the server NBT / count bridge. */
void lce_remote_pump(void);
#ifdef __cplusplus
}
#endif
