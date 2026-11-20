// NBT tab -> full server NBT editor (console glue).
//
// While the session is Active and this account has debug access on the server, the
// tab menu's NBT tab shows lce_nbt_ed::Editor (nbt_editor_ui.hpp) instead of the
// native read-only viewer:
//  * draw : lce_nbt_editor_frame(), called from cmd_overlay.cpp's wrap of
//           tab_modal::Navigation::begin (one wrap per symbol). The original builds the frame
//           (viewport, glyph metrics, tab bar); for the NBT tab (active_tab 3) the rows,
//           footer, category bar, scroll rail and keyboard fields are replaced by the
//           editor's View, so the existing NuklearPresenter renders it unchanged.
//  * keys : remote_input.cpp (wrap of Navigation::input) offers every key to
//           lce_nbt_editor_input() unless the Commands overlay is shown; on the editor's Home
//           screen L1/R1/Circle pass through (R1 there opens the Commands overlay). Triangle/Square/L2/R2 are not delivered by the
//           menu's pad queue, so their edges are read from the raw pad sample
//           (ld --wrap on mc195_mods_pad_sample_data, called by native_pad before any masking).
//           Everything also works with the eight menu keys alone (actions menu).
//  * net  : remote_nbt.cpp drives the request/reply exchange (lce_nbt_editor_take/...).
// Off a server every call falls straight through to the original.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <orbis/libkernel.h>
#include "../../r72-session-candidate/tab-menu/modal-binding/navigation.hpp"
#include "nbt_editor_ui.hpp"

extern "C" bool mc195_remote_actions_active();
extern "C" bool mc195_nbt_offline_ready();
// local_nbt.cpp: this console hosts the world (no LCE server session)
extern "C" bool lce_nbt_local_available(void);
extern "C" bool lce_nbt_local_busy(void);
extern "C" int lce_nbt_local_bind(const void *);
extern "C" bool lce_nbt_local_current(const void *);
extern "C" bool lce_nbt_local_retire(void);
#include "request_gate.hpp"
#include "nbt_frame_owner.hpp"
extern "C" int mc195_nbt_navigation_owner(const void *,unsigned *,mc195_debug_pad_ticket *,void **);
extern "C" bool lce_nbt_local_post(const char *lines, size_t n);
extern "C" bool lce_nbt_local_take(char *op, size_t opc, char *target, size_t tc, bool *ok, char *msg, size_t mc, const uint8_t **nbt, size_t *n);
extern "C" void mc195_remote_log(const char *);

inline void *operator new(size_t, void *p) noexcept { return p; }

namespace {
using namespace lce_nbt_ed;
constexpr unsigned kNbtTab = 3;
constexpr uint64_t kRawEdgeMaxAgeUs = 250000;    // raw button edges older than this are ignored

Storage storage;
alignas(Editor) unsigned char editor_storage[sizeof(Editor)];
Editor *editor;
unsigned lock_word;
uint64_t editor_generation=1;
// The tab menu redraws only when an input or Navigation::poll() reports a change, so "the NBT tab
// is shown" is tracked as state: set by an NBT-tab frame, cleared by any other tab's frame, by
// Navigation::invalidate (menu closed) and when the editor hands L1 (tab switch) to the menu.
bool shown;
const void *local_navigation; int local_pad=-1;
const void *local_owner_navigation(){return __atomic_load_n(&local_navigation,__ATOMIC_ACQUIRE);}
void set_local_owner_navigation(const void *p){__atomic_store_n(&local_navigation,p,__ATOMIC_RELEASE);}
uint32_t drawn_hash;   // hash of the View last put into a frame
View view_cache, poll_view;

struct Guard {
    Guard() { while (__atomic_exchange_n(&lock_word, 1u, __ATOMIC_ACQUIRE)) {} }
    ~Guard() { __atomic_store_n(&lock_word, 0u, __ATOMIC_RELEASE); }
};
uint64_t now_us() { return sceKernelGetProcessTime(); }
Editor &ed() { if (!editor) editor = new (editor_storage) Editor(&storage); return *editor; }
void reset_editor_locked(){if(editor)editor->reset();uint64_t n=__atomic_load_n(&editor_generation,__ATOMIC_RELAXED);if(n!=UINT64_MAX)__atomic_store_n(&editor_generation,n+1,__ATOMIC_RELEASE);}

uint32_t hash_view(const View &v)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&v); uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof v; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// ---- raw pad edges (pad thread -> owner) ----
constexpr uint32_t kTriangle = 0x1000, kSquare = 0x8000, kL2 = 0x100, kR2 = 0x200;
uint32_t pad_previous[4];
uint64_t edge_time[4][4];          // per button: time of the latest press edge
uint64_t edge_taken[4][4];
uint64_t remote_edge_time[4],remote_edge_taken[4];
const uint32_t kRawBits[4] = {kTriangle, kSquare, kL2, kR2};
const Key kRawKeys[4] = {Key::Triangle, Key::Square, Key::L2, Key::R2};

#if !defined(LCE_RELEASE)
void log_line(const char *what, const char *detail)
{
    char l[200]; snprintf(l, sizeof l, "NBT editor: %s %s\n", what, detail ? detail : ""); mc195_remote_log(l);
}

#endif

// Copies ASCII into a frame text field; returns the length.
// The editor serves the NBT tab on an LCE server session (remote_nbt.cpp transport) or, when
// this console hosts the world, through the local backend; the server session wins.
bool stock_viewer;   // local world: the user chose the stock viewer; cleared when the menu closes
bool editor_active() { return mc195_nbt_offline_ready() && lce_nbt_local_available() && !stock_viewer; }
bool local_mode() { return mc195_nbt_offline_ready() && lce_nbt_local_available() && !stock_viewer; }
// Local transport, owner thread under the Guard: hand the next request to the backend and
// deliver its reply (the same calls remote_nbt.cpp makes for a server).
void pump_local(Editor &e)
{
    if (!local_mode()) return;
    char op[8], target[8], msg[96]; bool ok; const uint8_t *nbt; size_t n;
    if (lce_nbt_local_take(op, sizeof op, target, sizeof target, &ok, msg, sizeof msg, &nbt, &n)) {
#if !defined(LCE_RELEASE)
        char detail[160]; snprintf(detail, sizeof detail, "%s %s %s %s (%zu bytes)", op, target, ok ? "ok" : "failed", msg, n); log_line("local reply", detail);
#endif
        e.on_nbt(op, target, ok, msg, nbt, n);
    }
    if (lce_nbt_local_busy()) return;
    const char *lines; size_t len; Expect ex;
    if (!e.take_outbox(&lines, &len, &ex)) return;
    if (ex != Expect::Nbt || !lce_nbt_local_post(lines, len)) { e.on_send_failed(); return; }
#if !defined(LCE_RELEASE)
    char first[96]; size_t k = 0; while (k < len && k + 1 < sizeof first && lines[k] != '\n') { first[k] = lines[k]; ++k; } first[k] = 0;
    log_line("local send", first);
#endif
}

uint8_t put16(char16_t *dst, const char *src, unsigned cap)
{
    unsigned n = 0;
    for (; src[n] && n < cap; ++n) { const char c = src[n]; dst[n] = (char16_t)(c >= 0x20 && c <= 0x7e ? c : '?'); }
    dst[n] = 0; return (uint8_t)n;
}

void fill(overlay_probe::TabFrame &f, const View &v)
{
    using overlay_probe::kRows; using overlay_probe::kTextUnits;
    f.row_count = v.count > kRows ? kRows : v.count;
    for (unsigned i = 0; i < f.row_count; ++i) {
        const ViewRow &s = v.rows[i]; overlay_probe::Row &r = f.rows[i];
        char text[80];
        if (s.value[0]) snprintf(text, sizeof text, "%s: %s", s.label, s.value); else snprintf(text, sizeof text, "%s", s.label[0] ? s.label : " ");
        r.length = put16(r.text, text, kTextUnits);
        if (!r.length) { r.text[0] = ' '; r.text[1] = 0; r.length = 1; }
        r.enabled = s.enabled || s.role == R_HEADING || s.role == R_STATUS;
        r.role = (overlay_probe::RowRole)s.role;
        r.type = s.type <= 11 ? s.type : 0; r.control = s.control;
        r.label_length = put16(r.label, s.label, kTextUnits);
        r.value_length = put16(r.value, s.value, kTextUnits);
    }
    f.no_focused_row = v.no_focus || v.selected >= f.row_count;
    f.selected_row = f.no_focused_row ? 0 : v.selected;
    f.more_above = v.more_above; f.more_below = v.more_below;
    f.nbt = true; f.nbt_edit = v.edit; f.nbt_category = v.category < 4 ? v.category : 0;
    f.scroll_total = v.scroll_total; f.scroll_first = v.scroll_first > v.scroll_total ? v.scroll_total : v.scroll_first;
    f.registry_detail = false; f.registry_item_action = false;
    f.notice = overlay_probe::FrameNotice::none;
    unsigned n = 0; for (; v.status[n] && n < 63; ++n) f.action_status[n] = (v.status[n] >= 0x20 && v.status[n] <= 0x7e) ? v.status[n] : '?';
    f.action_status[n] = 0; f.action_status_length = (uint8_t)n;
    f.keyboard = v.keyboard;
    if (v.keyboard) {
        f.keyboard_shift = v.kb_shift; f.keyboard_symbols = v.kb_symbols;
        f.keyboard_selection = v.kb_sel < 45 ? v.kb_sel : 0;
        f.keyboard_length = v.kb_len > 127 ? 127 : v.kb_len;
        for (unsigned i = 0; i < f.keyboard_length; ++i) f.keyboard_text[i] = v.kb_text[i];
    }
}
}  // namespace

// ---------------------------------------------------------------- draw (from cmd_overlay.cpp's Navigation::begin wrap)
// Called with the stock frame right after the original Navigation::begin. While the Commands
// overlay is painted on top, the editor keeps rendering underneath but ignores raw buttons.
extern "C" __attribute__((visibility("hidden"))) void lce_nbt_editor_frame(void *navigation, tab_modal::Frame *frame, bool overlay_shown)
{
    if (!frame || !navigation) return;
    if (!mc195_nbt_offline_ready()) {
        shown=false;
        if(local_owner_navigation() && lce_nbt_local_retire()){Guard g;reset_editor_locked();set_local_owner_navigation(nullptr);local_pad=-1;}
        return;
    }
    if (mc195_nbt_offline_ready() && lce_nbt_local_available()) {
        const void *owner=local_owner_navigation();
        const auto action=nbt_frame_owner::decide(owner!=nullptr,owner==navigation,owner && owner!=navigation && lce_nbt_local_current(owner),frame->view.active_tab==kNbtTab);
        if(action==nbt_frame_owner::Decision::Ignore)return;
        if(action==nbt_frame_owner::Decision::Leave){
            shown=false;if(lce_nbt_local_retire()){Guard g;reset_editor_locked();set_local_owner_navigation(nullptr);local_pad=-1;}
            return;
        }
        int lease=lce_nbt_local_bind(navigation); unsigned slot=0;void *resolved_owner=nullptr;mc195_debug_pad_ticket ticket{};
        if(!lease || mc195_nbt_navigation_owner(navigation,&slot,&ticket,&resolved_owner)!=1){if(!owner || owner==navigation)shown=false;return;}
        {Guard g; if(lease==2){reset_editor_locked(); for(unsigned i=0;i<4;++i)edge_taken[slot][i]=__atomic_load_n(&edge_time[slot][i],__ATOMIC_ACQUIRE);} local_pad=(int)slot;set_local_owner_navigation(navigation);}
    }
    const uint64_t now = now_us();
    if (frame->view.active_tab != kNbtTab || !editor_active()) { shown = false; return; }
    {
        Guard g;
        Editor &e = ed();
        const bool fresh = !shown;
        shown = true;
        // raw-only buttons pressed while this tab is shown (not ones from before it appeared)
        for (unsigned i = 0; i < 4; ++i) {
            const unsigned slot=local_mode() && local_pad>=0 ? (unsigned)local_pad : 0;
            const uint64_t t = local_mode()?__atomic_load_n(&edge_time[slot][i], __ATOMIC_ACQUIRE):__atomic_load_n(&remote_edge_time[i], __ATOMIC_ACQUIRE);
            uint64_t &taken=local_mode()?edge_taken[slot][i]:remote_edge_taken[i];
            if (t && t != taken) { taken = t; if (!fresh && !overlay_shown && now - t < kRawEdgeMaxAgeUs) e.key(kRawKeys[i], false); }
        }
        e.set_local(local_mode());
        e.tick(now);
        pump_local(e);
        if (e.take_stock_request()) { stock_viewer = true; shown = false; return; }   // the next frame is the stock viewer's
        e.view(&view_cache);
        drawn_hash = hash_view(view_cache);
    }
    fill(frame->view, view_cache);
    frame->preview_identity = {};
}
// cmd_overlay.cpp's Navigation::invalidate wrap: the menu closed.
extern "C" __attribute__((visibility("hidden"))) void lce_nbt_editor_invalidate(void *navigation) { if(local_owner_navigation() && navigation!=local_owner_navigation() && mc195_nbt_offline_ready()) return; shown = false; stock_viewer = false; if(local_owner_navigation()==navigation && lce_nbt_local_retire()){Guard g;reset_editor_locked();set_local_owner_navigation(nullptr);local_pad=-1;} }

// Navigation::poll is the owner tick of the open menu; a non-zero result only marks the frame
// dirty (SceneRuntime / RawPadOwner / PauseRuntimeManager). Report a change when the editor's
// view moved on by itself (server reply, live refresh, raw Triangle/Square, message expiry).
unsigned lce_real_nav_poll(tab_modal::Navigation *) __asm__("__real__ZN9tab_modal10Navigation4pollEv");
unsigned lce_wrap_nav_poll(tab_modal::Navigation *) __asm__("__wrap__ZN9tab_modal10Navigation4pollEv") __attribute__((visibility("hidden")));
unsigned lce_wrap_nav_poll(tab_modal::Navigation *self)
{
    const unsigned r = lce_real_nav_poll(self);
    if (!shown || !editor_active() || (local_mode() && (self!=local_owner_navigation() || !lce_nbt_local_current(self)))) return r;
    const uint64_t now = now_us();
    bool changed;
    {
        Guard g;
        Editor &e = ed();
        bool raw = false;
        for (unsigned i = 0; i < 4; ++i) { const unsigned slot=local_mode() && local_pad>=0 ? (unsigned)local_pad : 0; const uint64_t t = local_mode()?__atomic_load_n(&edge_time[slot][i], __ATOMIC_ACQUIRE):__atomic_load_n(&remote_edge_time[i], __ATOMIC_ACQUIRE); if (t && t != (local_mode()?edge_taken[slot][i]:remote_edge_taken[i]) && now - t < kRawEdgeMaxAgeUs) raw = true; }
        e.set_local(local_mode());
        e.tick(now);
        pump_local(e);
        e.view(&poll_view);
        changed = raw || hash_view(poll_view) != drawn_hash;
    }
    return r ? r : changed ? 1u : 0u;
}

// ---------------------------------------------------------------- keys (from remote_input.cpp)
// key: tab_modal::Key order (previous_tab, next_tab, up, down, left, right, accept, back, close).
// Returns 1 when the editor consumed the key (the original must not see it).
extern "C" __attribute__((visibility("hidden"))) int lce_nbt_editor_input(void *navigation, int32_t pad, int key, bool pressed, bool repeat)
{
    const uint64_t now = now_us();
    (void)now;
    // RawPadOwner routes every physical slot through DirectRuntime pad 0.
    // The current Navigation lease verifies the actual opening pad separately.
    // A stale/contended editor lease must consume keys for the already drawn
    // editor rather than activate the hidden stock page underneath it.
    if (!shown || navigation!=local_owner_navigation()) return 0;
    if (key < 0 || key >= 8) return 0;   // close: always the menu's
    if (pad!=0 || !editor_active() || !lce_nbt_local_current(navigation)) return 1;
    static const Key map[] = {Key::L1, Key::R1, Key::Up, Key::Down, Key::Left, Key::Right, Key::Cross, Key::Circle};
    Guard g;
    Editor &e = ed();
    const Key k = map[key];
    if (!pressed) {
        // releases follow their press: keep them away from the native page unless it is a tab/close key on Home
        return e.captures_menu_keys() || (k != Key::L1 && k != Key::R1 && k != Key::Circle) ? 1 : 0;
    }
    const KeyResult r = e.key(k, repeat);
    if (r == KeyResult::PassThrough && e.leaves_tab(k)) shown = false;   // the menu switches tabs now
    return r == KeyResult::Consumed ? 1 : 0;
}

// ---------------------------------------------------------------- raw pad sample (pad thread)
extern "C" uint32_t lce_chat_pad_filter(int slot, uint32_t buttons, uint64_t now, unsigned char *pad_data);
extern "C" void lce_perf_pad_sample(int slot, const uint8_t *p, unsigned n);
extern "C" void __real_mc195_mods_pad_sample_data(int slot, int handle, unsigned char *p, unsigned n, uint64_t now);
extern "C" __attribute__((visibility("hidden"))) void __wrap_mc195_mods_pad_sample_data(int slot, int handle, unsigned char *p, unsigned n, uint64_t now)
{
    lce_perf_pad_sample(slot, p, n);   // perf_boot.cpp: idle frame-rate reducer (raw sample, before any masking)
    if (p && n >= 0x4d && slot >= 0 && slot < 4) {
        uint32_t buttons; memcpy(&buttons, p, 4);
        if (!p[0x4c]) buttons = 0;   // disconnected
        if (p[0x4c]) {   // chat_overlay.cpp: touch pad long press opens the chat keyboard; the press is hidden from the game
            buttons = lce_chat_pad_filter(slot, buttons, now, p);   // also neutralises the sticks while the chat keyboard is open
            memcpy(p, &buttons, 4);
        }
        const uint32_t edges = buttons & ~pad_previous[slot];
        pad_previous[slot] = buttons;
        if (edges) {
            const uint64_t t = sceKernelGetProcessTime();
            for (unsigned i = 0; i < 4; ++i) if (edges & kRawBits[i]) { __atomic_store_n(&edge_time[slot][i], t, __ATOMIC_RELEASE); __atomic_store_n(&remote_edge_time[i],t,__ATOMIC_RELEASE); }
        }
    }
    __real_mc195_mods_pad_sample_data(slot, handle, p, n, now);
}

// Remote full editing is intentionally unavailable in this candidate. Keep the
// original transport ABI so unrelated remote actions remain unchanged.
extern "C" __attribute__((visibility("hidden"))) bool lce_nbt_editor_take(const char **,size_t *,int *){return false;}
extern "C" __attribute__((visibility("hidden"))) void lce_nbt_editor_nbt(const char *,const char *,bool,const char *,const uint8_t *,size_t){}
extern "C" __attribute__((visibility("hidden"))) void lce_nbt_editor_near(const uint8_t *,size_t){}
extern "C" __attribute__((visibility("hidden"))) void lce_nbt_editor_failed(bool){}
