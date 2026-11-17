// Server NBT / count bridge (plugin side). While the session is Active and this account has
// debug access, the debug menu's NBT features talk to the server instead of native memory:
//
//  * inbound: lce_remote_inbound_frame() (called from native_bridge.c for every server frame)
//    consumes exact MC|LceNbt / MC|LceNear packet-250 frames into a mailbox. They never reach
//    the retail client. The server sends them as whole frames (see LceDebugEngine.h).
//  * self queue (menu actions): lce_remote_pump() takes pending self_player requests and either
//    forwards them as "/lce-action" (plain actions) or runs the count/damage transactions
//    (30 observe, 31 set count, 32 undo, 87 set damage, 88 undo) against "/lce-nbt get|set i<slot>",
//    finishing with a CountReply read back from the server. Actions the server does not serve
//    are rejected instead of reporting success.
//  * NBT viewer queue (mc195_nbt_queue, held item READ): the process pointer is captured by
//    wrapping mc195_f3_bind; HELD_ITEM READ requests are answered with "/lce-nbt get h" bytes.
//    The tree editor itself is read-only in this build (the native transport has no APPLY
//    producer), so edits happen through the scalar transactions above.
#include "remote_nbt_core.hpp"
#include "remote_nbt_abi.h"
#include <orbis/libkernel.h>
#include "/home/codey/minecraft-lce-work/r72-session-candidate/loader/nbt_natural_process_195/process.h"

namespace {
lce_remote::Mailbox mailbox;
uint32_t last_seq;
lce_remote::Ctx ctx{1000, {}};
uint8_t reply_buffer[lce_remote::kMaxPayload + 16];
mc195_natural_process *natural_process;
constexpr uint64_t kTimeoutUs = 5000000;

struct SelfJob { bool active; lce_remote_req req; lce_remote::Job job; uint64_t deadline; } self_job;
struct DocJob { bool active; const mc195_nbt_request *req; uint64_t deadline; } doc_job;

uint64_t now_us() { return sceKernelGetProcessTime(); }

void finish_self(unsigned result, const lce_remote::CountOut *out) {
 if (!out || result != 1) { mc195_finish_remote_request(self_job.req.ticket, result, nullptr); return; }
 lce_remote_count_reply r{};
 r.observation = out->observation; r.slot = out->slot; r.count = out->count; r.damage = out->damage;
 r.undo_available = out->undo_available; r.damage_valid = out->damage_valid; r.ench_valid = out->ench_valid;
 r.ench_n = out->ench_n;
 for (unsigned i = 0; i < out->ench_n && i < 64; ++i) { r.ench_id[i] = out->ench[i].id; r.ench_level[i] = out->ench[i].level; }
 mc195_finish_remote_request(self_job.req.ticket, 1, &r);
}

void drop_all() {
 if (self_job.active) { finish_self(2, nullptr); self_job.active = false; }
 if (doc_job.active && natural_process) {
  mc195_nbt_complete(&natural_process->queue, doc_job.req, MC195_NBT_REJECTED, nullptr, 0);
  doc_job.active = false;
 }
 lce_remote::discard_pending(mailbox, &last_seq);
}

void step_self(const lce_remote::Reply *reply, uint64_t now) {
 char cmd[96]; lce_remote::CountOut out{};
 if (!reply && now > self_job.deadline) { finish_self(2, nullptr); self_job.active = false; return; }
 const lce_remote::Step s = lce_remote::job_step(self_job.job, ctx, reply, cmd, sizeof cmd, &out);
 switch (s) {
 case lce_remote::Step::Wait: return;
 case lce_remote::Step::Send:
  lce_remote::discard_pending(mailbox, &last_seq); // a late reply to an earlier request must not answer this one
  if (!mc195_remote_send_text(cmd)) { finish_self(2, nullptr); self_job.active = false; return; }
  self_job.deadline = now + kTimeoutUs;
  return;
 case lce_remote::Step::Ok: finish_self(1, &out); self_job.active = false; return;
 case lce_remote::Step::Reject: finish_self(2, nullptr); self_job.active = false; return;
 }
}

void begin_self(uint64_t now) {
 lce_remote_req req{};
 if (!mc195_take_remote_request(&req)) return;
 self_job = SelfJob{}; self_job.req = req;
 if (lce_remote::job_supported(req.action)) {
  // For 30/87-style observes only the slot matters; 31/87 carry the desired value.
  lce_remote::job_begin(&self_job.job, req.action, req.count_slot, req.count_expected, req.count_desired);
  self_job.active = true; self_job.deadline = now + kTimeoutUs;
  step_self(nullptr, now);
  return;
 }
 if (mc195_remote_action_served(req.action)) {
  mc195_finish_remote_action(req.ticket, mc195_remote_send_action(req.action, req.item_id) ? 1u : 2u);
  return;
 }
 mc195_finish_remote_action(req.ticket, 2u); // not available on this server: never claim success
}

void step_doc(const lce_remote::Reply *reply, uint64_t now) {
 auto *q = &natural_process->queue;
 if (reply) {
  doc_job.active = false;
  if (reply->ok && reply->nbt && reply->nbt_size > 0 && reply->nbt_size <= MC195_NBT_MAX_BYTES)
   mc195_nbt_complete(q, doc_job.req, MC195_NBT_OK, reply->nbt, reply->nbt_size);
  else mc195_nbt_complete(q, doc_job.req, MC195_NBT_REJECTED, nullptr, 0);
  return;
 }
 if (now > doc_job.deadline || now > doc_job.req->ticket.deadline_us) {
  doc_job.active = false;
  mc195_nbt_complete(q, doc_job.req, MC195_NBT_REJECTED, nullptr, 0);
 }
}

void begin_doc(uint64_t now) {
 if (!natural_process) return;
 auto *q = &natural_process->queue;
 for (unsigned i = 0; i < 4; ++i) {
  const mc195_nbt_request *r = mc195_nbt_claim(q, i);
  if (!r) continue;
  if (r->operation != MC195_NBT_READ || r->target != MC195_NBT_HELD_ITEM) { mc195_nbt_defer(q, r); continue; }
  lce_remote::discard_pending(mailbox, &last_seq);
  if (!mc195_remote_send_text("/lce-nbt get h")) { mc195_nbt_complete(q, r, MC195_NBT_REJECTED, nullptr, 0); continue; }
  doc_job.active = true; doc_job.req = r; doc_job.deadline = now + kTimeoutUs;
  return;
 }
}
}  // namespace

// Called by native_bridge.c on the session worker thread for every server frame.
extern "C" int lce_remote_inbound_frame(const uint8_t *data, size_t n) {
 const uint8_t *payload; size_t size;
 const int kind = lce_remote::match_frame(data, n, &payload, &size);
 if (!kind) return 0;
 lce_remote::post(mailbox, (uint8_t)kind, payload, size);
 return 1;
}

extern "C" int __real__Z13mc195_f3_bindP21mc195_natural_process(void *) __asm__("__real__Z13mc195_f3_bindP21mc195_natural_process");
extern "C" int __wrap__Z13mc195_f3_bindP21mc195_natural_process(void *process) __asm__("__wrap__Z13mc195_f3_bindP21mc195_natural_process");
extern "C" int __wrap__Z13mc195_f3_bindP21mc195_natural_process(void *process) {
 // The process object is permanently resident; remember it for the server consumer.
 natural_process = static_cast<mc195_natural_process *>(process);
 return __real__Z13mc195_f3_bindP21mc195_natural_process(process);
}

extern "C" void lce_remote_pump(void) {
 if (!mc195_remote_actions_active()) {
  if (self_job.active || doc_job.active) drop_all();
  else lce_remote::discard_pending(mailbox, &last_seq);
  return;
 }
 const uint64_t now = now_us();
 lce_remote::Reply reply{}; bool have = false;
 uint8_t kind = 0; size_t size = 0;
 if (lce_remote::take(mailbox, &last_seq, &kind, reply_buffer, sizeof reply_buffer, &size) && kind == lce_remote::kNbt)
  have = lce_remote::parse_reply(reply_buffer, size, &reply);
 if (self_job.active) { step_self(have ? &reply : nullptr, now); return; }
 if (doc_job.active) { step_doc(have ? &reply : nullptr, now); return; }
 begin_self(now);
 if (!self_job.active) begin_doc(now);
}
