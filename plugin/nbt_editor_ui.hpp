#pragma once
// Controller-only NBT editor: screens, key handling, local edits/undo/clipboard and
// the requests it needs from the server. Hardware independent (host tested by
// nbt_editor_test.cpp); nbt_editor.cpp feeds it keys/replies and draws its View
// through the tab menu's existing NBT renderer (6 rows x 32 columns).
//
// Controls (also shown in the footer):
//   Home     NBT sub-tabs Player / Item / Entity / Block (the category bar): Left/Right switch,
//            Up/Down choose, Cross open, Triangle refresh; L1 = previous menu tab, R1 = Commands
//            overlay (cmd_overlay.cpp), Circle = close - exactly as on every other tab
//   List     (ender chest) Up/Down, Left/Right page, Cross open, R1/Triangle refresh, Circle back
//   Tree     Up/Down move, Left/Right page, Cross enter/edit/toggle, Circle up/leave,
//            R1/Triangle actions, L1 undo, Square apply
//            The footer names what Cross does on the focused row ("X open", "X toggle", "X type"...),
//            the pending change count and how to apply it; the first local edit says it is not applied yet.
//   Number   Left/Right -/+ step, L1/R1 finer/coarser step, Up/Down rows, Cross OK/run, Square keep + apply,
//            Triangle list, Circle cancel
//   Text     D-pad keys, Cross type, L1 clear, R1 symbols, Square backspace, Triangle presets, Circle cancel
//            (a part of a long text: Circle returns to the part list)
//   Lists    Up/Down, Left/Right page, L1/R1 +-50, Cross choose, Circle cancel
// Scrolling (every list): the selection always stays inside the 5 visible rows and the window
// only moves when the selection would leave it; a fresh Up on the first row / Down on the last
// wraps, a held (repeating) key stops at the end; Left/Right move a whole page and stop at the ends.
#include "nbt_schema.hpp"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/menu-feedback/keyboard_keys.hpp"

namespace lce_nbt_ed {

enum class Key : uint8_t { Up, Down, Left, Right, L1, R1, Cross, Circle, Triangle, Square, L2, R2 };
enum class KeyResult : uint8_t { Consumed, PassThrough };
enum class Expect : uint8_t { None, Nbt, Near };

constexpr unsigned kViewRows = 6, kViewText = 32;
enum RowRole : uint8_t { R_PLAIN, R_HEADING, R_STATUS, R_TARGET, R_NODE, R_CONTROL };  // = overlay_probe::RowRole
struct ViewRow { char label[kViewText + 1]; char value[kViewText + 1]; uint8_t type, role, control; bool enabled; };
struct View {
    ViewRow rows[kViewRows];
    uint8_t count, selected, category;
    bool no_focus, more_above, more_below, edit;
    uint64_t scroll_first, scroll_total;
    char status[64];
    bool keyboard, kb_shift, kb_symbols;
    uint8_t kb_sel, kb_len;
    uint16_t kb_text[128];
};

// Memory owned by the caller (static on the console).
struct Storage {
    Doc orig, work;
    uint8_t scratch[kMaxDocBytes];
    uint8_t undo[384 * 1024];
    uint8_t clip[64 * 1024];
    char script[64 * 1024];
    uint16_t text_units[32768];   // long strings: the whole value while one part is on the keyboard
    uint8_t text_bytes[65536];    // its modified UTF-8 encoding on commit
};

struct NearRow { char token[40]; char label[72]; };

// Scroll model shared by every list (5 visible rows under the heading).
constexpr unsigned kVisible = 5;
struct Scroll {
    unsigned sel, first;
    void clamp(unsigned n, unsigned vis = kVisible)
    {
        if (!n) { sel = first = 0; return; }
        if (sel >= n) sel = n - 1;
        if (first > sel) first = sel;
        if (sel >= first + vis) first = sel - vis + 1;
        if (first + vis > n) first = n > vis ? n - vis : 0;   // keep the window full at the end
    }
    // delta +-1: step (wraps only on a fresh press); +-kVisible: page; larger: jump. All clamp.
    void move(int delta, unsigned n, bool repeat, unsigned vis = kVisible)
    {
        if (!n) { sel = first = 0; return; }
        if (delta == 1 || delta == -1) {
            if (delta < 0) { if (sel > 0) --sel; else if (!repeat) sel = n - 1; }
            else { if (sel + 1 < n) ++sel; else if (!repeat) sel = 0; }
        } else if (delta == (int)vis || delta == -(int)vis) {
            if (delta < 0) { sel = sel >= vis ? sel - vis : 0; first = first >= vis ? first - vis : 0; }
            else { sel = sel + vis < n ? sel + vis : n - 1; first += vis; }
        } else {
            const long v = (long)sel + delta;
            sel = v < 0 ? 0 : v >= (long)n ? n - 1 : (unsigned)v;
        }
        clamp(n, vis);
    }
};

class Editor {
public:
    enum class Screen : uint8_t { Home, Picker, Tree, Actions, Number, Text, Choice, Templates, Confirm };

    explicit Editor(Storage *s) : st_(s) { reset(); }
    void reset()
    {
        screen_ = Screen::Home; sub_ = 0; memset(home_, 0, sizeof home_); radius_e_ = 16; radius_b_ = 16; waiting_ = Expect::None;
        memset(cache_, 0, sizeof cache_); list_ready_ = false; inflight_ = Inflight::None; want_ender_ = false;
        st_->orig.clear(); st_->work.clear(); undo_n_ = 0; undo_used_ = 0; edits_ = 0; clip_len_ = 0;
        live_ = true; status_[0] = 0; status_until_ = 0; outbox_ready_ = false; serial_ = 0; now_ = 0; last_get_ = 0;
        target_[0] = 0; kind_ = K_PLAYER; tree_ = Scroll{0, 0};
    }

    // ---------------------------------------------------------------- transport side
    // A request is a block of "/lce-nbt ..." lines ('\n' separated). The reply that ends it
    // is an MC|LceNbt (expect Nbt) or MC|LceNear (expect Near) payload.
    // Document requests (get/apply/undo) block the editor; list refreshes run in the background.
    bool take_outbox(const char **lines, size_t *len, Expect *expect)
    {
        if (outbox_ready_) { outbox_ready_ = false; *lines = st_->script; *len = outbox_len_; *expect = waiting_; inflight_ = Inflight::Doc; return true; }
        if (list_ready_) { list_ready_ = false; *lines = list_line_; *len = strlen(list_line_); *expect = Expect::Near; inflight_ = Inflight::List; return true; }
        return false;
    }
    bool waiting() const { return waiting_ != Expect::None; }
    void on_timeout() { lost("Server did not answer (timeout)"); }
    void on_send_failed() { lost("Could not send to the server"); }
    // MC|LceNbt reply (already split): op, target, ok, message, NBT bytes.
    void on_nbt(const char *op, const char *target, bool ok, const char *msg, const uint8_t *nbt, size_t n)
    {
        if (inflight_ == Inflight::Doc) inflight_ = Inflight::None;
        if (waiting_ != Expect::Nbt) return;
        waiting_ = Expect::None;
        const bool commit = !strcmp(op, "commit"), undo = !strcmp(op, "undo");
        if (!ok) { close_after_apply_ = false; char m[64]; if (!strcmp(op, "err")) snprintf(m, sizeof m, "Server: %s", msg); else snprintf(m, sizeof m, "%s failed: %s", commit ? "Apply" : undo ? "Undo" : "Read", msg); message(m); if (!commit && !undo && screen_ == Screen::Tree && !st_->work.valid()) screen_ = return_screen_; return; }
        // relative targets resolve on the server ("h" -> "i3", "wb" -> "w1,63,2", "kh" -> "k0,-1") and an apply that
        // moved or replaced the object (block entity x/y/z, World block x/y/z, entity id) answers with the new token:
        // keep editing that object from now on
        if (target && target[0] && strcmp(target, target_) && strlen(target) < sizeof target_ && target[0] == (target_[0] == 'h' ? 'i' : target_[0])) {
            snprintf(target_, sizeof target_, "%s", target);
        }
        if (!load(nbt, n)) { message("Could not read the NBT the server sent"); return; }
        if (commit) { message(msg && *msg ? msg : "Applied"); if (close_after_apply_) { close_after_apply_ = false; close_doc(); } }
        else if (undo) message(local_ ? "Local world restored the previous state" : "Server restored the previous state");
        else if (refreshing_) { /* quiet */ }
        else message("Loaded");
        refreshing_ = false;
    }
    // MC|LceNear reply: rows go to the cache of their kind (i / x / e / b), whatever is shown.
    void on_near(const uint8_t *p, size_t n)
    {
        if (inflight_ == Inflight::List) inflight_ = Inflight::None;
        size_t pos = 0;
        char kind[4];
        if (!rd_utf(p, n, &pos, kind, sizeof kind) || pos + 4 > n) { message("Bad list reply"); return; }
        NearCache *c = cache_of(kind[0]);
        if (!c) return;
        // keep the selected row (by token) where the list changes under it
        char keep[40] = {}; const bool home_list = kind[0] != 'x';
        Scroll &sc = home_list ? home_[sub_of_kind(kind[0])] : c->scroll;
        const unsigned extra = home_list ? home_fixed(sub_of_kind(kind[0])) : 0;
        if (sc.sel >= extra && sc.sel - extra < c->n) snprintf(keep, sizeof keep, "%s", c->rows[sc.sel - extra].token);
        const uint32_t cnt = (uint32_t)p[pos] << 24 | (uint32_t)p[pos + 1] << 16 | (uint32_t)p[pos + 2] << 8 | p[pos + 3]; pos += 4;
        c->n = 0;
        for (uint32_t i = 0; i < cnt && c->n < kMaxPicker; ++i) {
            NearRow &r = c->rows[c->n];
            if (!rd_utf(p, n, &pos, r.token, sizeof r.token) || !rd_utf(p, n, &pos, r.label, sizeof r.label)) break;
            ++c->n;
        }
        c->loaded = true; c->pending = false; c->at = now_;
        if (keep[0]) for (unsigned i = 0; i < c->n; ++i) if (!strcmp(c->rows[i].token, keep)) { sc.sel = i + extra; break; }
        sc.clamp(c->n + extra);
        if (kind[0] == 'x' && want_ender_ && waiting_ == Expect::None && screen_ == Screen::Home) { want_ender_ = false; screen_ = Screen::Picker; }
    }
    // Called every frame/tick with the current time (live refresh, message expiry).
    void tick(uint64_t now_us)
    {
        now_ = now_us;
        if (status_[0] && status_until_ && now_ > status_until_) status_[0] = 0;
        if (screen_ == Screen::Home && sub_ > 0) { NearCache *c = cache_of(sub_list_kind()); if (c && (!c->loaded || now_ - c->at > kListIntervalUs)) request_list(sub_list_kind()); }
        if (live_ && in_doc() && screen_ == Screen::Tree && !edits_ && waiting_ == Expect::None && !outbox_ready_ &&
            now_ - last_get_ > kLiveIntervalUs && st_->work.valid()) {
            refreshing_ = true; request_get();
        }
    }

    // ---------------------------------------------------------------- input
    KeyResult key(Key k, bool repeat)
    {
        if (waiting_ != Expect::None) {
            if (k == Key::Circle && !repeat) { waiting_ = Expect::None; refreshing_ = false; message("Cancelled"); if (!st_->work.valid() && screen_ == Screen::Tree) close_doc(); return KeyResult::Consumed; }
            return screen_ == Screen::Home && (k == Key::L1 || k == Key::R1) ? KeyResult::PassThrough : KeyResult::Consumed;
        }
        switch (screen_) {
        case Screen::Home: return home_key(k, repeat);
        case Screen::Picker: picker_key(k, repeat); break;
        case Screen::Tree: tree_key(k, repeat); break;
        case Screen::Actions: actions_key(k, repeat); break;
        case Screen::Number: number_key(k, repeat); break;
        case Screen::Text: text_key(k, repeat); break;
        case Screen::Choice: choice_key(k, repeat); break;
        case Screen::Templates: templates_key(k, repeat); break;
        case Screen::Confirm: confirm_key(k, repeat); break;
        }
        return KeyResult::Consumed;
    }
    // True when the editor owns L1/R1/Circle (anything but the Home screen).
    bool captures_menu_keys() const { return screen_ != Screen::Home || waiting_ != Expect::None; }
    // Home screen: L1 moves to another menu tab (the caller stops routing keys here until redrawn).
    bool leaves_tab(Key k) const { return screen_ == Screen::Home && waiting_ == Expect::None && k == Key::L1; }

    // ---------------------------------------------------------------- view
    void view(View *v)
    {
        memset(v, 0, sizeof *v);
        v->category = category();
        switch (screen_) {
        case Screen::Home: view_home(v); break;
        case Screen::Picker: view_picker(v); break;
        case Screen::Tree: view_tree(v); break;
        case Screen::Actions: view_list(v, action_title_, action_n_, action_s_, &Editor::action_row); break;
        case Screen::Number: view_number(v); break;
        case Screen::Text: view_text(v); break;
        case Screen::Choice: view_choice(v); break;
        case Screen::Templates: view_list(v, "Add: choose a type", tmpl_n_, tmpl_s_, &Editor::template_row); break;
        case Screen::Confirm: view_list(v, confirm_title_, confirm_n_, confirm_s_, &Editor::confirm_row); break;
        }
        if (waiting_ != Expect::None) snprintf(v->status, sizeof v->status, "%s", refreshing_ ? "Refreshing..." : "Waiting for the server...  Circle: cancel");
        else if (status_[0]) snprintf(v->status, sizeof v->status, "%s", status_);
        else hint(v->status, sizeof v->status);
    }

    // Local world (this console hosts the world, no LCE server): only the player document is
    // served (local_nbt.cpp); the other sub-tabs explain that instead of listing.
    void set_local(bool on)
    {
        if (on == local_) return;
        local_ = on;
        for (NearCache &c : cache_) { c.n = 0; c.loaded = false; c.pending = false; }
        list_ready_ = false;
        if (screen_ == Screen::Home || screen_ == Screen::Picker) { screen_ = Screen::Home; for (Scroll &h : home_) h = Scroll{0, 0}; }
    }
    bool local() const { return local_; }
    // Local mode, Player tab row 2: the caller hands the NBT tab back to the stock viewer until the menu closes.
    bool take_stock_request() { const bool r = want_stock_; want_stock_ = false; return r; }

    // ---------------------------------------------------------------- inspection (tests)
    Screen screen() const { return screen_; }
    const Doc &work() const { return st_->work; }
    Doc &work_mut() { return st_->work; }
    const char *target() const { return target_; }
    unsigned edits() const { return edits_; }
    const char *status() const { return status_; }
    uint32_t selected_node() const { return sel_node(); }
    bool live() const { return live_; }
    unsigned undo_depth() const { return undo_n_; }
    unsigned sub_tab() const { return sub_; }
    Scroll home_scroll(unsigned sub) const { return home_[sub & 3]; }
    Scroll tree_scroll() const { return tree_; }
    unsigned home_rows() const { return home_count(sub_); }

private:
    static constexpr unsigned kMaxPicker = 64, kMaxActions = 32, kMaxTemplates = 40, kUndoSlots = 48;
    static constexpr uint64_t kLiveIntervalUs = 2000000, kListIntervalUs = 3000000;
    struct NearCache { NearRow rows[64]; unsigned n; bool loaded, pending; uint64_t at; Scroll scroll; };
    enum class Inflight : uint8_t { None, Doc, List };
    NearCache cache_[4];                 // i, x, e, b
    char list_line_[48]; bool list_ready_ = false; Inflight inflight_ = Inflight::None; bool want_ender_ = false;
    unsigned sub_ = 0; Scroll home_[4];  // NBT sub-tabs: 0 Player, 1 Item, 2 Entity, 3 Block
    bool local_ = false, want_stock_ = false;
    Storage *st_;
    Screen screen_, return_screen_ = Screen::Home;
    Expect waiting_;
    bool outbox_ready_, refreshing_ = false, live_;
    size_t outbox_len_ = 0;
    uint32_t serial_;
    uint64_t now_, last_get_;
    char status_[64]; uint64_t status_until_;
    // home
    unsigned radius_e_, radius_b_;
    // document
    char target_[40], title_[40]; char kind_;
    uint32_t crc_ = 0;
    Path dir_{}; Scroll tree_{0, 0};
    unsigned &sel_ = tree_.sel, &first_ = tree_.first;
    bool array_view_ = false;            // dir_ is an array; rows are its elements
    unsigned edits_;
    // undo ring
    uint32_t undo_off_[kUndoSlots], undo_len_[kUndoSlots]; Path undo_dir_[kUndoSlots]; unsigned undo_sel_[kUndoSlots]; unsigned undo_n_; size_t undo_used_;
    // clipboard: one named tag
    size_t clip_len_; char clip_label_[40];
    // actions
    enum Act : uint8_t { A_EDIT, A_OPEN, A_PICK, A_TOGGLE, A_ADD_HERE, A_ADD_INSIDE, A_DUP, A_UP, A_DOWN, A_RENAME, A_COPY, A_PASTE_HERE,
                         A_PASTE_INSIDE, A_PASTE_OVER, A_DELETE, A_LIST_TYPE, A_ARR_INSERT, A_ARR_APPEND, A_ARR_DELETE, A_APPLY, A_REVERT,
                         A_UNDO, A_REFRESH, A_LIVE, A_SERVER_UNDO, A_CLOSE, A_PRESETS_DONE,
                         A_SEARCH, A_FIND_NEXT, A_FIND_PREV, A_PASTE_VALUE, A_ARR_UP, A_ARR_DOWN };
    Act actions_[kMaxActions]; unsigned action_n_ = 0; Scroll action_s_{0, 0}; char action_title_[kViewText + 1] = "Actions";
    // number editor
    Path num_path_{}; int64_t num_elem_ = -1; uint8_t num_type_ = T_INT; int64_t num_i_ = 0, num_orig_i_ = 0; double num_d_ = 0, num_orig_d_ = 0; double num_step_ = 1;
    Scroll num_s_{0, 0}; unsigned &num_row_ = num_s_.sel; Hint num_hint_{}; char num_name_[40];
    enum NumRow : uint8_t { N_VALUE, N_STEP, N_DONE, N_RESET, N_ZERO, N_MIN, N_MAX, N_NEGATE, N_TOGGLE, N_PICK, N_DIGITS };
    NumRow num_rows_[12]; unsigned num_rows_n_ = 0;
    // digit editor: the value as a fixed-width digit string ("+0000000064", "+1.234567890E+012"); Up/Down change the
    // digit under the cursor (no carry), Left/Right move, Cross keeps, Circle leaves without changes
    bool num_digits_ = false; char dig_[40]; unsigned dig_len_ = 0, dig_cur_ = 0;
    // text editor
    enum class TextMode : uint8_t { Value, Rename, NewName, Search };
    // long strings (> 127 units): one part is on the keyboard, the rest waits in Storage::text_units
    unsigned text_total_ = 0, text_pre_ = 0, text_win_ = 0, text_parts_ = 0, text_part_ = 0;
    // search: case-insensitive text matched against key names, string values and value previews (pick names too)
    char needle_[64] = {};
    TextMode text_mode_ = TextMode::Value; Path text_path_{}; uint16_t kb_[128]; unsigned kb_len_ = 0, kb_sel_ = 0; bool kb_shift_ = false, kb_symbols_ = false; unsigned kb_max_ = 127;
    // choice list
    enum class ChoiceMode : uint8_t { Value, ListType, Preset, TextPart };
    ChoiceMode choice_mode_ = ChoiceMode::Value; Pick choice_pick_ = Pick::None; Scroll choice_s_{0, 0}; unsigned &choice_sel_ = choice_s_.sel; unsigned choice_n_ = 0; Path choice_path_{}; Screen choice_return_ = Screen::Tree;
    // templates
    const Template *tmpl_[kMaxTemplates]; unsigned tmpl_n_ = 0; Scroll tmpl_s_{0, 0}; unsigned &tmpl_sel_ = tmpl_s_.sel; Path tmpl_parent_{}; bool tmpl_after_sel_ = false;
    // confirm
    enum Conf : uint8_t { C_APPLY_LEAVE, C_DISCARD_LEAVE, C_STAY, C_DISCARD_REFRESH, C_DELETE_YES, C_DELETE_NO };
    Conf confirm_[4]; unsigned confirm_n_ = 0; Scroll confirm_s_{0, 0}; unsigned &confirm_sel_ = confirm_s_.sel; const char *confirm_title_ = "";

    // ================================================================ helpers
    static bool rd_utf(const uint8_t *p, size_t n, size_t *pos, char *out, size_t cap)
    {
        if (*pos + 2 > n) return false;
        const size_t len = (size_t)p[*pos] << 8 | p[*pos + 1]; *pos += 2;
        if (*pos + len > n) return false;
        ascii_preview(p + *pos, len, out, cap);
        *pos += len; return true;
    }
    void message(const char *m) { snprintf(status_, sizeof status_, "%s", m); status_until_ = now_ + 4000000; }
    bool in_doc() const { return screen_ != Screen::Home && screen_ != Screen::Picker; }
    static uint8_t category_of(char kind) { switch (kind) { case K_ITEM: case K_ENDER: case 'h': return 1; case K_ENTITY: return 2; case K_BLOCK: case K_WORLD: return 3; default: return 0; } }
    uint8_t category() const { return screen_ == Screen::Home ? (uint8_t)sub_ : screen_ == Screen::Picker ? 1 : category_of(kind_); }
    NearCache *cache_of(char k) { switch (k) { case 'i': return &cache_[0]; case 'x': return &cache_[1]; case 'e': return &cache_[2]; case 'b': return &cache_[3]; default: return nullptr; } }
    const NearCache *cache_of(char k) const { return const_cast<Editor *>(this)->cache_of(k); }
    static unsigned sub_of_kind(char k) { return k == 'e' ? 2 : k == 'b' ? 3 : 1; }
    char sub_list_kind() const { return sub_ == 1 ? 'i' : sub_ == 2 ? 'e' : sub_ == 3 ? 'b' : 0; }
    // background list refresh (never blocks the editor; one queued at a time)
    void request_list(char k)
    {
        if (local_) return;
        NearCache *c = cache_of(k);
        if (!c || c->pending) return;
        if (k == 'e') snprintf(list_line_, sizeof list_line_, "/lce-nbt list e %u\n", radius_e_);
        else if (k == 'b') snprintf(list_line_, sizeof list_line_, "/lce-nbt list b %u\n", radius_b_);
        else snprintf(list_line_, sizeof list_line_, "/lce-nbt list %c\n", k);
        for (NearCache &o : cache_) o.pending = false;   // only the newest queued list matters
        c->pending = true; list_ready_ = true;
    }
    void lost(const char *why)
    {
        const Inflight f = inflight_; inflight_ = Inflight::None;
        if (f == Inflight::List || (f == Inflight::None && waiting_ == Expect::None)) { for (NearCache &c : cache_) c.pending = false; return; }
        if (waiting_ == Expect::None) return;
        waiting_ = Expect::None; refreshing_ = false; message(why);
        if (!st_->work.valid() && screen_ == Screen::Tree) close_doc();
    }
    void begin_request(Expect e) { waiting_ = e; outbox_ready_ = true; ++serial_; }
    void request_text(const char *line, Expect e)
    {
        const int n = snprintf(st_->script, sizeof st_->script, "%s\n", line);
        outbox_len_ = n > 0 ? (size_t)n : 0;
        begin_request(e);
    }
    void request_get() { char l[64]; snprintf(l, sizeof l, "/lce-nbt get %s", target_); last_get_ = now_; request_text(l, Expect::Nbt); }
    void open_target(const char *token, const char *title)
    {
        snprintf(target_, sizeof target_, "%s", token); snprintf(title_, sizeof title_, "%s", title);
        kind_ = token[0] == 'h' ? (char)K_ITEM : token[0];
        st_->orig.clear(); st_->work.clear(); dir_.n = 0; sel_ = first_ = 0; array_view_ = false; edits_ = 0; undo_n_ = 0; undo_used_ = 0;
        return_screen_ = screen_;
        screen_ = Screen::Tree; refreshing_ = false;
        request_get();
    }
    bool load(const uint8_t *nbt, size_t n)
    {
        // remember the cursor by names (the server's key order is not stable)
        static KeyPath kdir, ksel; bool have_sel = false;
        const bool had = st_->work.valid();
        if (had) {
            key_path_of(st_->work, dir_node(), &kdir);
            const uint32_t s = sel_node();
            if (s != kNil) { key_path_of(st_->work, s, &ksel); have_sel = true; }
        } else kdir.n = 0;
        const unsigned ksel_index = sel_;
        if (!st_->orig.parse(nbt, n) || !st_->work.parse(nbt, n)) { st_->orig.clear(); st_->work.clear(); return false; }
        crc_ = crc32(nbt, n);
        edits_ = 0;
        if (!refreshing_) { undo_n_ = 0; undo_used_ = 0; }
        unsigned reached = 0;
        uint32_t d = node_at_key(st_->work, kdir, &reached);
        if (d != kNil && !is_container(st_->work.at(d).type) && !is_array(st_->work.at(d).type)) d = st_->work.at(d).parent;
        path_of(st_->work, d, &dir_);
        array_view_ = is_array(st_->work.at(d).type);
        sel_ = ksel_index;
        if (have_sel && reached == kdir.n) {
            unsigned r2 = 0; const uint32_t s = node_at_key(st_->work, ksel, &r2);
            if (r2 == ksel.n && st_->work.at(s).parent == d) sel_ = st_->work.index_of(s);
        }
        clamp_sel(); clamp_sel();
        last_get_ = now_;
        if (screen_ != Screen::Tree && screen_ != Screen::Actions && screen_ != Screen::Confirm) screen_ = Screen::Tree;
        return true;
    }
    uint32_t dir_node() const { return st_->work.valid() ? node_at(st_->work, dir_) : kNil; }
    unsigned row_count() const
    {
        const uint32_t d = dir_node();
        if (d == kNil) return 0;
        return array_view_ ? st_->work.at(d).v.b.len : st_->work.at(d).count;
    }
    uint32_t sel_node() const
    {
        const uint32_t d = dir_node();
        if (d == kNil || array_view_) return kNil;
        return st_->work.child_at(d, sel_);
    }
    void clamp_sel() { tree_.clamp(row_count()); }
    char kind_letter() const { return kind_; }
    Hint hint_of(uint32_t n) const { return hint_for(st_->work, n, kind_); }
    bool node_readonly(uint32_t n) const
    {
        for (uint32_t x = n; x != kNil && x != st_->work.root; x = st_->work.at(x).parent) if (hint_of(x).readonly) return true;
        return false;
    }

    // ---- undo -----------------------------------------------------------------------------
    // Snapshot the work document before an edit.
    bool push_undo()
    {
        const size_t cap = sizeof st_->undo;
        const size_t n = st_->work.serialize(st_->scratch, kMaxDocBytes);
        if (!n || n > cap / 2) return false;
        while (undo_n_ && (undo_n_ >= kUndoSlots || undo_used_ + n > cap)) drop_oldest_undo();
        memcpy(st_->undo + undo_used_, st_->scratch, n);
        undo_off_[undo_n_] = (uint32_t)undo_used_; undo_len_[undo_n_] = (uint32_t)n; undo_dir_[undo_n_] = dir_; undo_sel_[undo_n_] = sel_;
        ++undo_n_; undo_used_ += n;
        return true;
    }
    void drop_oldest_undo()
    {
        const size_t first = undo_len_[0];
        memmove(st_->undo, st_->undo + first, undo_used_ - first);
        for (unsigned i = 1; i < undo_n_; ++i) { undo_off_[i - 1] = undo_off_[i] - (uint32_t)first; undo_len_[i - 1] = undo_len_[i]; undo_dir_[i - 1] = undo_dir_[i]; undo_sel_[i - 1] = undo_sel_[i]; }
        --undo_n_; undo_used_ -= first;
    }
    void undo()
    {
        if (!undo_n_) { message(edits_ ? "Undo history is full; use Revert" : "Nothing to undo"); return; }
        --undo_n_;
        if (!st_->work.parse(st_->undo + undo_off_[undo_n_], undo_len_[undo_n_])) { message("Undo failed"); return; }
        undo_used_ = undo_off_[undo_n_];
        dir_ = undo_dir_[undo_n_]; sel_ = undo_sel_[undo_n_];
        const uint32_t d = dir_node(); array_view_ = d != kNil && is_array(st_->work.at(d).type);
        if (edits_) --edits_;
        clamp_sel(); message("Undone");
    }
    // Prepares an edit: undo snapshot + room in the pools. Node ids must be re-resolved after.
    bool begin_edit(unsigned nodes = 64, size_t bytes = 4096)
    {
        if (!st_->work.valid()) return false;
        bool compacted = false;
        if (!st_->work.reserve(st_->scratch, nodes, bytes, &compacted)) { message("Document is full"); return false; }
        if (!push_undo()) message("Document too large for undo history");
        return true;
    }
    void end_edit()
    {
        ++edits_; clamp_sel();
        if (edits_ == 1) message("Not applied yet: Square applies it");   // edits stay local until applied
    }

    // ---- home: NBT sub-tabs ------------------------------------------------------------------
    // 0 Player: Me, Level data, Chunk here, Scoreboard   1 Item: Held item, Ender chest slots, inventory rows
    // 2 Entity: radius row + nearby entities           3 Block: radius row, block below feet / at feet / at head, block entities
    // Every sub-tab keeps its own selection/scroll; lists are cached per kind and refreshed in the
    // background when shown (older than 3 s), so a switch is immediate and never shows another tab's rows.
    static constexpr unsigned kPlayerRows = 4, kItemFixed = 2, kBlockFixed = 4;
    unsigned home_fixed(unsigned sub) const { return sub == 0 ? kPlayerRows : sub == 1 ? kItemFixed : sub == 3 ? kBlockFixed : 1; }
    unsigned home_count(unsigned sub) const
    {
        if (local_) return sub == 0 ? 2 : 0;
        if (sub == 0) return kPlayerRows;
        const NearCache *c = cache_of(sub == 1 ? 'i' : sub == 2 ? 'e' : 'b');
        return home_fixed(sub) + (c ? c->n : 0);
    }
    const NearRow *home_list_row(unsigned sub, unsigned i) const
    {
        if (sub == 0 || i < home_fixed(sub)) return nullptr;
        const NearCache *c = cache_of(sub == 1 ? 'i' : sub == 2 ? 'e' : 'b');
        return c && i - home_fixed(sub) < c->n ? &c->rows[i - home_fixed(sub)] : nullptr;
    }
    void home_row(unsigned i, ViewRow *r) const
    {
        r->role = R_TARGET; r->enabled = true;
        if (const NearRow *nr = home_list_row(sub_, i)) { picker_row_text(*nr, r->label, sizeof r->label, r->value, sizeof r->value); return; }
        if (local_) { snprintf(r->label, sizeof r->label, "%s", i == 0 ? "Me (player)" : "Stock viewer"); snprintf(r->value, sizeof r->value, "%s", i == 0 ? "edit (local world)" : "entities, blocks"); return; }
        switch (sub_) {
        case 0: { static const char *const t[] = {"Me (player)", "Level data", "Chunk here", "Scoreboard"}; static const char *const v[] = {"", "time weather rules seed", "biomes", ""};
                  snprintf(r->label, sizeof r->label, "%s", t[i]); snprintf(r->value, sizeof r->value, "%s", v[i]); break; }
        case 1: snprintf(r->label, sizeof r->label, "%s", i == 0 ? "Held item" : "Ender chest slots..."); break;
        case 3: if (i > 0) { static const char *const t[] = {"Block below feet", "Block at feet", "Block at head"}; snprintf(r->label, sizeof r->label, "%s", t[i - 1]); snprintf(r->value, sizeof r->value, "id data biome"); break; }
            [[fallthrough]];   // row 0: radius
        default:
            snprintf(r->label, sizeof r->label, "Search radius");
            snprintf(r->value, sizeof r->value, "%u blocks (X: change)", sub_ == 2 ? radius_e_ : radius_b_);
            r->role = R_CONTROL; break;
        }
    }
    void switch_sub(int dir)
    {
        sub_ = (sub_ + 4 + (dir > 0 ? 1 : 3)) % 4;
        home_[sub_].clamp(home_count(sub_));
        if (sub_ > 0) { NearCache *c = cache_of(sub_list_kind()); if (c && (!c->loaded || now_ - c->at > kListIntervalUs)) request_list(sub_list_kind()); }
    }
    KeyResult home_key(Key k, bool repeat)
    {
        Scroll &sc = home_[sub_];
        const unsigned n = home_count(sub_);
        switch (k) {
        case Key::Up: sc.move(-1, n, repeat); return KeyResult::Consumed;
        case Key::Down: sc.move(1, n, repeat); return KeyResult::Consumed;
        case Key::Left: case Key::Right: if (!repeat) switch_sub(k == Key::Right ? 1 : -1); return KeyResult::Consumed;
        case Key::Triangle: if (!repeat && sub_ > 0) { request_list(sub_list_kind()); message("Refreshing list"); } return KeyResult::Consumed;
        case Key::Cross: {
            if (repeat || !n) return KeyResult::Consumed;
            if (const NearRow *nr = home_list_row(sub_, sc.sel)) {
                char label[40], value[40]; picker_row_text(*nr, label, sizeof label, value, sizeof value);
                open_target(nr->token, label); return KeyResult::Consumed;
            }
            if (local_) { if (sub_ == 0) { if (sc.sel == 0) open_target("p", "Me"); else want_stock_ = true; } return KeyResult::Consumed; }
            switch (sub_) {
            case 0: if (sc.sel == 0) open_target("p", "Me"); else if (sc.sel == 1) open_target("l", "Level"); else if (sc.sel == 2) open_target("kh", "Chunk"); else open_target("s", "Scoreboard"); break;
            case 1:
                if (sc.sel == 0) open_target("h", "Held item");
                else { if (cache_[1].loaded) screen_ = Screen::Picker; else want_ender_ = true; request_list('x'); }
                break;
            default: {
                if (sub_ == 3 && sc.sel > 0) { static const char *const tok[] = {"wb", "wf", "wh"}; static const char *const tt[] = {"Block below", "Block at feet", "Block at head"}; open_target(tok[sc.sel - 1], tt[sc.sel - 1]); break; }
                unsigned *r = sub_ == 2 ? &radius_e_ : &radius_b_;
                static const unsigned steps[] = {4, 8, 16, 32, 64};
                unsigned i = 0; while (i < 4 && steps[i] < *r) ++i;
                *r = steps[(i + 1) % 5]; cache_of(sub_list_kind())->pending = false; request_list(sub_list_kind());
                break;
            }
            }
            return KeyResult::Consumed;
        }
        case Key::Square: case Key::L2: case Key::R2: return KeyResult::Consumed;
        default: return KeyResult::PassThrough;   // L1 previous tab, R1 Commands overlay, Circle close
        }
    }
    void view_home(View *v) const
    {
        static const char *const names[] = {"Player", "Item", "Entity", "Block"};
        const unsigned n = home_count(sub_);
        const NearCache *c = sub_ ? cache_of(sub_list_kind()) : nullptr;
        if (c) snprintf(v->rows[0].label, sizeof v->rows[0].label, "NBT %s (%u)%s", names[sub_], c->n, c->pending ? " ..." : "");
        else snprintf(v->rows[0].label, sizeof v->rows[0].label, "NBT %s", names[sub_]);
        v->rows[0].role = R_HEADING;
        Scroll sc = home_[sub_]; sc.clamp(n);
        v->count = 1;
        for (unsigned i = sc.first; i < n && v->count < kViewRows; ++i) home_row(i, &v->rows[v->count++]);
        if (local_ && sub_ && v->count < kViewRows) {
            ViewRow &r = v->rows[v->count++]; snprintf(r.label, sizeof r.label, "%s", sub_ == 1 ? "local world: Me > Inventory" : "local world: needs an LCE server"); r.role = R_STATUS;
            v->no_focus = true; v->selected = 0; v->scroll_first = 0; v->scroll_total = 0; v->more_above = v->more_below = false; return;
        }
        if (c && !c->loaded && v->count < kViewRows) { ViewRow &r = v->rows[v->count++]; snprintf(r.label, sizeof r.label, "Loading..."); r.role = R_STATUS; }
        else if (c && c->loaded && !c->n && v->count < kViewRows) { ViewRow &r = v->rows[v->count++]; snprintf(r.label, sizeof r.label, "Nothing nearby"); r.role = R_STATUS; }
        v->selected = (uint8_t)(1 + sc.sel - sc.first);
        v->more_above = sc.first > 0; v->more_below = sc.first + kVisible < n;
        v->scroll_first = sc.first; v->scroll_total = n;
    }

    // ---- ender chest list ----------------------------------------------------------------------
    void picker_row_text(const NearRow &r, char *label, size_t lc, char *value, size_t vc) const
    {
        // fields: e: type|name|dist|health   b: type|dist   i/x: id|count|damage|flags
        char f[4][40] = {}; unsigned k = 0; size_t j = 0;
        for (const char *p = r.label; *p && k < 4; ++p) { if (*p == '|') { f[k][j] = 0; ++k; j = 0; continue; } if (j + 1 < sizeof f[0]) f[k][j++] = *p; }
        if (k < 4) f[k][j] = 0;
        if (r.token[0] == 'e') {
            if (f[1][0]) snprintf(label, lc, "%s \"%s\"", f[0], f[1]); else snprintf(label, lc, "%s", f[0]);
            if (f[3][0]) snprintf(value, vc, "%sm hp %s", f[2], f[3]); else snprintf(value, vc, "%sm", f[2]);
        } else if (r.token[0] == 'b') {
            snprintf(label, lc, "%s", f[0]); snprintf(value, vc, "%s %sm", r.token + 1, f[1]);
        } else {
            const long id = strtol_(f[0]);
            const char *nm = item_name(id);
            const unsigned slot = (unsigned)strtol_(r.token + 1);
            if (nm) snprintf(label, lc, "%u %s", slot, nm); else snprintf(label, lc, "%u item %ld", slot, id);
            snprintf(value, vc, "x%s%s%s%s%s", f[1], strcmp(f[2], "0") ? " d" : "", strcmp(f[2], "0") ? f[2] : "", strchr(f[3], 'E') ? " ench" : "", strchr(f[3], 'N') ? " named" : "");
        }
    }
    static long strtol_(const char *s) { char *e; return (long)Snbt::strtoll_(s, &e); }
    void picker_key(Key k, bool repeat)
    {
        NearCache &c = cache_[1]; Scroll &sc = c.scroll;
        switch (k) {
        case Key::Up: sc.move(-1, c.n, repeat); break;
        case Key::Down: sc.move(1, c.n, repeat); break;
        case Key::Left: sc.move(-(int)kVisible, c.n, repeat); break;
        case Key::Right: sc.move((int)kVisible, c.n, repeat); break;
        case Key::Circle: if (!repeat) screen_ = Screen::Home; break;
        case Key::R1: case Key::Triangle: if (!repeat) request_list('x'); break;
        case Key::Cross:
            if (repeat || !c.n) break;
            { char label[40], value[40]; picker_row_text(c.rows[sc.sel], label, sizeof label, value, sizeof value); open_target(c.rows[sc.sel].token, label); }
            break;
        default: break;
        }
    }
    void view_picker(View *v) const
    {
        const NearCache &c = cache_[1];
        snprintf(v->rows[0].label, sizeof v->rows[0].label, "Ender chest (%u)%s", c.n, c.pending ? " ..." : ""); v->rows[0].role = R_HEADING;
        v->count = 1;
        if (!c.n) { snprintf(v->rows[1].label, sizeof v->rows[1].label, c.loaded ? "Empty" : "Loading..."); v->rows[1].role = R_STATUS; v->count = 2; v->no_focus = true; return; }
        Scroll sc = c.scroll; sc.clamp(c.n);
        for (unsigned i = sc.first; i < c.n && v->count < kViewRows; ++i) {
            ViewRow &r = v->rows[v->count++]; r.role = R_TARGET; r.enabled = true;
            picker_row_text(c.rows[i], r.label, sizeof r.label, r.value, sizeof r.value);
        }
        v->selected = (uint8_t)(1 + sc.sel - sc.first);
        v->more_above = sc.first > 0; v->more_below = sc.first + kVisible < c.n;
        v->scroll_first = sc.first; v->scroll_total = c.n;
    }

    // ---- tree ------------------------------------------------------------------------------
    void breadcrumb(char *out, size_t cap) const
    {
        const Doc &d = st_->work;
        char path[200]; size_t len = 0; path[0] = 0;
        uint32_t chain[kMaxDepth]; unsigned k = 0;
        for (uint32_t n = dir_node(); n != kNil && n != d.root && k < kMaxDepth; n = d.at(n).parent) chain[k++] = n;
        for (unsigned i = k; i-- > 0;) {
            const uint32_t n = chain[i]; char part[48];
            if (d.at(d.at(n).parent).type == T_LIST) snprintf(part, sizeof part, "[%u]", d.index_of(n));
            else { char nm[40]; ascii_preview(d.bytes(d.at(n).name_off), d.at(n).name_len, nm, sizeof nm); snprintf(part, sizeof part, "%s%s", len ? ">" : "", nm); }
            const size_t pl = strlen(part); if (len + pl + 1 < sizeof path) { memcpy(path + len, part, pl + 1); len += pl; }
        }
        char head[80];
        if (len) snprintf(head, sizeof head, "%s>%s", title_, path); else snprintf(head, sizeof head, "%s", title_);
        if (edits_) { const size_t hl = strlen(head); snprintf(head + hl, sizeof head - hl, " *%u", edits_); }
        // keep the end visible
        const size_t hl = strlen(head);
        if (hl > kViewText) snprintf(out, cap, "..%s", head + hl - (kViewText - 2)); else snprintf(out, cap, "%s", head);
    }
    void node_row(uint32_t n, ViewRow *r) const
    {
        const Doc &d = st_->work;
        const Node &x = d.at(n);
        r->type = x.type; r->role = R_NODE; r->enabled = !node_readonly(n);
        if (d.at(x.parent).type == T_LIST) snprintf(r->label, sizeof r->label, "[%u]", d.index_of(n));
        else ascii_preview(d.bytes(x.name_off), x.name_len, r->label, sizeof r->label);
        describe_value(d, n, kind_, r->value, sizeof r->value);
        if (!is_container(x.type) && !is_array(x.type) && r->enabled) { r->role = R_CONTROL; r->control = hint_of(n).boolean ? 1 : 0; }
    }
    void view_tree(View *v) const
    {
        breadcrumb(v->rows[0].label, sizeof v->rows[0].label); v->rows[0].role = R_HEADING;
        v->count = 1;
        if (!st_->work.valid()) { snprintf(v->rows[1].label, sizeof v->rows[1].label, "%s", waiting_ != Expect::None ? "Loading..." : "No data (Circle: back)"); v->rows[1].role = R_STATUS; v->count = 2; v->no_focus = true; return; }
        const uint32_t d = dir_node(); const unsigned n = row_count();
        if (!n) { snprintf(v->rows[1].label, sizeof v->rows[1].label, "(empty)  R1: add a tag"); v->rows[1].role = R_STATUS; v->count = 2; v->no_focus = true; return; }
        Scroll sc = tree_; sc.clamp(n);
        for (unsigned i = sc.first; i < n && v->count < kViewRows; ++i) {
            ViewRow &r = v->rows[v->count++];
            if (array_view_) {
                r.role = R_CONTROL; r.enabled = !node_readonly(d); r.type = st_->work.at(d).type == T_BYTE_ARRAY ? T_BYTE : T_INT;
                const Hint eh = array_elem_hint(st_->work, d, kind_); const int64_t ev = hint_value(eh, st_->work.array_get(d, i));
                const char *en = eh.pick != Pick::None ? choice_name(eh.pick, ev) : nullptr;
                snprintf(r.label, sizeof r.label, "[%u]", i);
                if (en) snprintf(r.value, sizeof r.value, "%lld %s", (long long)ev, en); else snprintf(r.value, sizeof r.value, "%lld", (long long)ev);
            } else node_row(st_->work.child_at(d, i), &r);
        }
        v->selected = (uint8_t)(1 + sc.sel - sc.first);
        v->more_above = sc.first > 0; v->more_below = sc.first + kVisible < n;
        v->scroll_first = sc.first; v->scroll_total = n;
    }
    void tree_key(Key k, bool repeat)
    {
        if (!st_->work.valid()) { if (k == Key::Circle && !repeat) screen_ = return_screen_; return; }
        const unsigned n = row_count();
        switch (k) {
        case Key::Up: tree_.move(-1, n, repeat); break;
        case Key::Down: tree_.move(1, n, repeat); break;
        case Key::Left: tree_.move(-(int)kVisible, n, repeat); break;
        case Key::Right: tree_.move((int)kVisible, n, repeat); break;
        case Key::Cross: if (!repeat && n) activate(); break;
        case Key::Circle: if (!repeat) go_up(); break;
        case Key::R1: case Key::Triangle: if (!repeat) open_actions(); break;
        case Key::L1: if (!repeat) undo(); break;
        case Key::Square: if (!repeat) apply(); break;
        case Key::R2: case Key::L2: if (!repeat) { if (needle_[0]) find(k == Key::R2 ? 1 : -1); else open_search(); } break;
        default: break;
        }
        clamp_sel();
    }
    void enter(uint32_t node)
    {
        path_of(st_->work, node, &dir_);
        array_view_ = is_array(st_->work.at(node).type);
        sel_ = first_ = 0;
    }
    void go_up()
    {
        if (dir_.n) {
            const unsigned back = dir_.idx[dir_.n - 1];
            --dir_.n; array_view_ = false; sel_ = back; first_ = sel_ > 2 ? sel_ - 2 : 0; clamp_sel();   // parent row centred
            return;
        }
        if (edits_) { open_confirm_leave(); return; }
        close_doc();
    }
    void close_doc()
    {
        st_->orig.clear(); st_->work.clear(); edits_ = 0; undo_n_ = 0; undo_used_ = 0;
        screen_ = return_screen_ == Screen::Picker ? Screen::Picker : Screen::Home;
        // lists may have changed while the document was open
        if (screen_ == Screen::Picker) request_list('x'); else if (sub_list_kind()) request_list(sub_list_kind());
    }
    void activate()
    {
        if (array_view_) { open_number_array(sel_); return; }
        const uint32_t s = sel_node(); if (s == kNil) return;
        const Node &x = st_->work.at(s);
        if (is_container(x.type) || is_array(x.type)) { enter(s); return; }
        if (node_readonly(s)) { message(protected_reason(kind_)); return; }
        const Hint h = hint_of(s);
        if (h.boolean) { toggle(s); return; }
        if (x.type == T_STRING) { if (h.pick != Pick::None) open_choice_value(s, h.pick); else open_text(s, TextMode::Value); return; }
        open_number(s);
    }
    void toggle(uint32_t s)
    {
        Path p; path_of(st_->work, s, &p);
        if (!begin_edit()) return;
        const uint32_t n = node_at(st_->work, p);
        st_->work.set_integer(n, st_->work.at(n).v.i ? 0 : 1); end_edit();
    }

    // ---- actions ---------------------------------------------------------------------------
    void add_action(Act a) { if (action_n_ < kMaxActions) actions_[action_n_++] = a; }
    void open_actions()
    {
        action_n_ = 0;
        const Doc &d = st_->work;
        const uint32_t dn = dir_node(); const uint32_t s = sel_node();
        const bool ro_dir = dn != kNil && node_readonly(dn);
        // title names the row the actions apply to
        if (array_view_ && row_count()) snprintf(action_title_, sizeof action_title_, "Actions: [%u]", sel_);
        else if (s != kNil && d.at(d.at(s).parent).type == T_LIST) snprintf(action_title_, sizeof action_title_, "Actions: [%u]", d.index_of(s));
        else if (s != kNil) { char nm[24]; ascii_preview(d.bytes(d.at(s).name_off), d.at(s).name_len, nm, sizeof nm); snprintf(action_title_, sizeof action_title_, "Actions: %s", nm); }
        else snprintf(action_title_, sizeof action_title_, "Actions");
        // pending changes first: applying them is the most common next step
        if (edits_) add_action(A_APPLY);
        if (undo_n_) add_action(A_UNDO);
        if (array_view_) {
            if (row_count()) add_action(A_EDIT);
            if (!ro_dir) {
                add_action(A_ARR_INSERT); add_action(A_ARR_APPEND);
                if (row_count()) { if (sel_ > 0) add_action(A_ARR_UP); if (sel_ + 1 < row_count()) add_action(A_ARR_DOWN); add_action(A_ARR_DELETE); }
            }
            if (row_count()) add_action(A_COPY);
            if (row_count() && !ro_dir && clip_scalar()) add_action(A_PASTE_VALUE);
        } else if (s != kNil) {
            const Node &x = d.at(s); const Hint h = hint_of(s); const bool ro = node_readonly(s);
            const bool in_list = d.at(x.parent).type == T_LIST;
            if (is_container(x.type) || is_array(x.type)) add_action(A_OPEN);
            else if (!ro) { if (h.boolean) add_action(A_TOGGLE); add_action(A_EDIT); if (h.pick != Pick::None) add_action(A_PICK); }
            if (!ro_dir) add_action(A_ADD_HERE);
            if (is_container(x.type) && !ro) add_action(A_ADD_INSIDE);
            if (!ro) add_action(A_DUP);
            if (in_list && !ro) { if (x.prev != kNil) add_action(A_UP); if (x.next != kNil) add_action(A_DOWN); }
            if (!in_list && !ro) add_action(A_RENAME);
            add_action(A_COPY);
            if (clip_len_ && !ro_dir) add_action(A_PASTE_HERE);
            if (clip_len_ && is_container(x.type) && !ro) add_action(A_PASTE_INSIDE);
            if (clip_len_ && !ro) add_action(A_PASTE_OVER);
            if (!ro && clip_scalar() && (is_integer(x.type) || is_real(x.type) || x.type == T_STRING)) add_action(A_PASTE_VALUE);
            if (x.type == T_LIST && !x.count && !ro) add_action(A_LIST_TYPE);
            if (!ro) add_action(A_DELETE);
        } else if (dn != kNil && !ro_dir) {
            add_action(A_ADD_HERE);
            if (clip_len_) add_action(A_PASTE_HERE);
            if (d.at(dn).type == T_LIST && !d.at(dn).count) add_action(A_LIST_TYPE);
        }
        add_action(A_SEARCH);
        if (needle_[0]) { add_action(A_FIND_NEXT); add_action(A_FIND_PREV); }
        if (edits_) add_action(A_REVERT);
        add_action(A_REFRESH); add_action(A_LIVE); add_action(A_SERVER_UNDO); add_action(A_CLOSE);
        action_s_ = Scroll{0, 0};
        screen_ = Screen::Actions;
    }
    void action_row(unsigned i, ViewRow *r) const
    {
        r->role = R_TARGET; r->enabled = true;
        const char *t = "?"; char buf[40];
        switch (actions_[i]) {
        case A_EDIT: t = "Edit value"; break;
        case A_OPEN: t = "Open"; break;
        case A_PICK: t = "Pick from list"; break;
        case A_TOGGLE: t = "Toggle true/false"; break;
        case A_ADD_HERE: t = "Add new tag here..."; break;
        case A_ADD_INSIDE: t = "Add tag inside..."; break;
        case A_DUP: t = "Duplicate"; break;
        case A_UP: t = "Move up"; break;
        case A_DOWN: t = "Move down"; break;
        case A_RENAME: t = "Rename"; break;
        case A_COPY: t = "Copy"; break;
        case A_PASTE_HERE: t = "Paste here"; snprintf(r->value, sizeof r->value, "%s", clip_label_); break;
        case A_PASTE_INSIDE: t = "Paste inside"; snprintf(r->value, sizeof r->value, "%s", clip_label_); break;
        case A_PASTE_OVER: t = "Paste over (replace)"; break;
        case A_DELETE: t = "Delete"; break;
        case A_LIST_TYPE: t = "Set list element type"; break;
        case A_ARR_INSERT: t = "Insert element"; break;
        case A_ARR_APPEND: t = "Append element"; break;
        case A_ARR_DELETE: t = "Delete element"; break;
        case A_APPLY: snprintf(buf, sizeof buf, "Apply %u change%s to game (Sq)", edits_, edits_ == 1 ? "" : "s"); t = buf; break;
        case A_REVERT: t = "Discard all changes"; break;
        case A_UNDO: t = "Undo last edit (L1)"; break;
        case A_REFRESH: t = edits_ ? "Reload (discard changes)" : "Reload from server"; break;
        case A_LIVE: t = "Auto refresh"; snprintf(r->value, sizeof r->value, "%s", live_ ? "on" : "off"); r->role = R_CONTROL; break;
        case A_SERVER_UNDO: t = local_ ? "Undo last apply (local)" : "Undo last apply (server)"; break;
        case A_CLOSE: t = "Close (back to the list)"; break;
        case A_PRESETS_DONE: t = "Done"; break;
        case A_SEARCH: t = "Search..."; if (needle_[0]) snprintf(r->value, sizeof r->value, "%s", needle_); break;
        case A_FIND_NEXT: t = "Next match (R2)"; break;
        case A_FIND_PREV: t = "Previous match (L2)"; break;
        case A_PASTE_VALUE: t = "Paste value"; snprintf(r->value, sizeof r->value, "%s", clip_label_); break;
        case A_ARR_UP: t = "Move element up"; break;
        case A_ARR_DOWN: t = "Move element down"; break;
        }
        snprintf(r->label, sizeof r->label, "%s", t);
    }
    void actions_key(Key k, bool repeat)
    {
        switch (k) {
        case Key::Up: action_s_.move(-1, action_n_, repeat); break;
        case Key::Down: action_s_.move(1, action_n_, repeat); break;
        case Key::Left: action_s_.move(-(int)kVisible, action_n_, repeat); break;
        case Key::Right: action_s_.move((int)kVisible, action_n_, repeat); break;
        case Key::Circle: case Key::Triangle: case Key::R1: if (!repeat) screen_ = Screen::Tree; break;
        case Key::Cross: if (!repeat && action_n_) { screen_ = Screen::Tree; run(actions_[action_s_.sel]); } break;
        default: break;
        }
    }
    void run(Act a)
    {
        const uint32_t s = sel_node();
        switch (a) {
        case A_EDIT: if (array_view_) open_number_array(sel_); else if (s != kNil) { if (st_->work.at(s).type == T_STRING) open_text(s, TextMode::Value); else open_number(s); } break;
        case A_OPEN: if (s != kNil) enter(s); break;
        case A_PICK: if (s != kNil) open_choice_value(s, hint_of(s).pick); break;
        case A_TOGGLE: if (s != kNil) toggle(s); break;
        case A_ADD_HERE: open_templates(dir_node(), true); break;
        case A_ADD_INSIDE: if (s != kNil) open_templates(s, false); break;
        case A_DUP: duplicate(); break;
        case A_UP: case A_DOWN: move_node(a == A_UP ? -1 : 1); break;
        case A_RENAME: if (s != kNil) open_text(s, TextMode::Rename); break;
        case A_COPY: copy(); break;
        case A_PASTE_HERE: paste(dir_node(), true); break;
        case A_PASTE_INSIDE: if (s != kNil) paste(s, false); break;
        case A_PASTE_OVER: paste_over(); break;
        case A_DELETE: delete_selected(); break;
        case A_LIST_TYPE: open_list_type(s != kNil && st_->work.at(s).type == T_LIST ? s : dir_node()); break;
        case A_ARR_INSERT: case A_ARR_APPEND: array_insert(a == A_ARR_APPEND); break;
        case A_ARR_DELETE: array_delete(); break;
        case A_APPLY: apply(); break;
        case A_REVERT: revert(); break;
        case A_UNDO: undo(); break;
        case A_REFRESH: refreshing_ = false; request_get(); break;
        case A_LIVE: live_ = !live_; message(live_ ? "Live refresh on (when there are no changes)" : "Live refresh off"); break;
        case A_SERVER_UNDO: { char l[64]; snprintf(l, sizeof l, "/lce-nbt undo %s", target_); request_text(l, Expect::Nbt); break; }
        case A_CLOSE: if (edits_) open_confirm_leave(); else close_doc(); break;
        case A_PRESETS_DONE: break;
        case A_SEARCH: open_search(); break;
        case A_FIND_NEXT: find(1); break;
        case A_FIND_PREV: find(-1); break;
        case A_PASTE_VALUE: paste_value(); break;
        case A_ARR_UP: case A_ARR_DOWN: array_move(a == A_ARR_UP ? -1 : 1); break;
        }
    }
    // generic list view with a title and N rows
    void view_list(View *v, const char *title, unsigned n, Scroll scroll, void (Editor::*row)(unsigned, ViewRow *) const) const
    {
        snprintf(v->rows[0].label, sizeof v->rows[0].label, "%s", title); v->rows[0].role = R_HEADING;
        scroll.clamp(n); const unsigned sel = scroll.sel, first = scroll.first;
        v->count = 1;
        for (unsigned i = first; i < n && v->count < kViewRows; ++i) (this->*row)(i, &v->rows[v->count++]);
        v->selected = (uint8_t)(1 + sel - first);
        v->more_above = first > 0; v->more_below = first + 5 < n; v->scroll_first = first; v->scroll_total = n;
        if (!n) { snprintf(v->rows[1].label, sizeof v->rows[1].label, "(nothing)"); v->rows[1].role = R_STATUS; v->count = 2; v->no_focus = true; }
    }

    // ---- structural edits ----------------------------------------------------------------
    void duplicate()
    {
        uint32_t s = sel_node(); if (s == kNil) return;
        Path p; path_of(st_->work, s, &p);
        if (!begin_edit(st_->work.free_nodes() > 4096 ? 4096 : 64, 64 * 1024)) return;
        s = node_at(st_->work, p);
        uint8_t *tmp = st_->scratch;
        const size_t len = st_->work.serialize_node(s, tmp, kMaxDocBytes, true);
        if (!len) { message("Too large to duplicate"); return; }
        const uint32_t parent = st_->work.at(s).parent;
        const uint32_t c = st_->work.insert_serialized(parent, s, tmp, len);
        if (c == kNil) { message("Cannot duplicate here"); return; }
        sel_ = st_->work.index_of(c); end_edit(); message("Duplicated");
    }
    void move_node(int dir)
    {
        const uint32_t s0 = sel_node(); if (s0 == kNil) return;
        Path p; path_of(st_->work, s0, &p);
        if (!begin_edit()) return;
        const uint32_t s = node_at(st_->work, p);
        if (!st_->work.move(s, dir)) return;
        sel_ = st_->work.index_of(s); end_edit();
    }
    // ---- value clipboard / array elements ----
    // The clipboard holds one named tag; a scalar one can also be pasted as a value (converted to the target's type).
    bool clip_scalar() const { return clip_len_ >= 3 && ((clip_[0] >= T_BYTE && clip_[0] <= T_DOUBLE) || clip_[0] == T_STRING); }
    const uint8_t *clip_ = st_->clip;
    bool clip_value(int64_t *iv, double *dv, const uint8_t **str, size_t *slen, bool *real, bool *text) const
    {
        if (!clip_scalar()) return false;
        const uint8_t t = clip_[0]; const size_t nl = (size_t)clip_[1] << 8 | clip_[2]; size_t o = 3 + nl;
        auto be = [&](unsigned k) { uint64_t v = 0; for (unsigned i = 0; i < k; ++i) v = v << 8 | clip_[o + i]; return v; };
        static const unsigned w[] = {0, 1, 2, 4, 8, 4, 8};
        *real = is_real(t); *text = t == T_STRING;
        if (t == T_STRING) { if (o + 2 > clip_len_) return false; *slen = (size_t)be(2); o += 2; if (o + *slen > clip_len_) return false; *str = clip_ + o; return true; }
        if (o + w[t] > clip_len_) return false;
        const uint64_t u = be(w[t]);
        switch (t) {
        case T_BYTE: *iv = (int8_t)u; break; case T_SHORT: *iv = (int16_t)u; break; case T_INT: *iv = (int32_t)u; break;
        case T_LONG: *iv = (int64_t)u; break; case T_FLOAT: *dv = bitsf((uint32_t)u); break; default: *dv = bitsd(u); break;
        }
        if (!*real) *dv = (double)*iv;
        return true;
    }
    // Reject non-finite and out-of-range values before a float-to-int cast.
    static bool clip_integer(double value, int64_t *out)
    {
        if (!(value >= -9223372036854775808.0 && value < 9223372036854775808.0)) return false;
        *out = (int64_t)value; return true;
    }
    static bool clip_decimal(const uint8_t *s, size_t n, int64_t *iv, double *dv, bool *real)
    {
        if (!n || n >= 64) return false;
        size_t i = 0; bool neg = false, dot = false, exp = false;
        if (s[i] == '-' || s[i] == '+') { neg = s[i] == '-'; if (++i == n) return false; }
        const size_t begin = i;
        uint64_t mag = 0; bool overflow = false;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            unsigned d = s[i++] - '0';
            if (mag > (UINT64_MAX - d) / 10) overflow = true; else mag = mag * 10 + d;
        }
        if (i == begin) return false;
        if (i < n && s[i] == '.') { dot = true; ++i; while (i < n && s[i] >= '0' && s[i] <= '9') ++i; }
        if (i < n && (s[i] == 'e' || s[i] == 'E')) {
            exp = true; ++i; if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
            size_t start = i; unsigned power = 0;
            while (i < n && s[i] >= '0' && s[i] <= '9') { if (power <= 308) power = power * 10 + (s[i] - '0'); ++i; }
            if (i == start || power > 308) return false;
        }
        if (i != n) return false;
        if (!dot && !exp) {
            const uint64_t limit = (uint64_t)INT64_MAX + (neg ? 1u : 0u);
            if (overflow || mag > limit) return false;
            *iv = neg ? (mag == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)mag) : (int64_t)mag;
            *dv = (double)*iv; *real = false; return true;
        }
        char b[64]; memcpy(b, s, n); b[n] = 0;
        const char *start = b[0] == '+' ? b + 1 : b; char *end;
        *dv = Snbt::strtod_(start, &end);
        if (*end || !__builtin_isfinite(*dv)) return false;
        *real = true; return true;
    }
    void paste_value()
    {
        int64_t iv = 0; double dv = 0; const uint8_t *str = nullptr; size_t sl = 0; bool real = false, text = false;
        if (!clip_value(&iv, &dv, &str, &sl, &real, &text)) { message("Clipboard holds no value"); return; }
        const uint32_t dest = array_view_ ? dir_node() : sel_node();
        if (dest == kNil) return;
        const uint8_t target_type = st_->work.at(dest).type;
        if (text && target_type != T_STRING && !clip_decimal(str, sl, &iv, &dv, &real)) { message("Text is not a valid number"); return; }
        if ((array_view_ || is_integer(target_type)) && real && !clip_integer(dv, &iv)) { message("Value out of range"); return; }
        if (array_view_) {
            const uint32_t a0 = dir_node(); if (a0 == kNil || !row_count()) return;
            if (text && !(str && sl)) { message("Empty text"); return; }
            int64_t lo = st_->work.at(a0).type == T_BYTE_ARRAY ? -128 : INT32_MIN, hi = st_->work.at(a0).type == T_BYTE_ARRAY ? 255 : INT32_MAX;
            if (iv < lo || iv > hi) { message("Value out of range for this array"); return; }
            Path p = dir_; const unsigned i = sel_;
            if (!begin_edit()) return;
            const uint32_t a = node_at(st_->work, p); st_->work.array_put(a, i, iv); ++st_->work.revision; end_edit(); message("Pasted value"); return;
        }
        const uint32_t s0 = sel_node(); if (s0 == kNil) return;
        Path p; path_of(st_->work, s0, &p);
        const uint8_t tt = st_->work.at(s0).type;
        if (!begin_edit(8, sl * 2 + 64)) return;
        const uint32_t n = node_at(st_->work, p);
        bool ok;
        if (tt == T_STRING) {
            if (text) ok = st_->work.set_string(n, str, sl);
            else { char b[64]; if (real) fmt_short_real(dv, b, sizeof b); else snprintf(b, sizeof b, "%lld", (long long)iv); ok = st_->work.set_string(n, (const uint8_t *)b, strlen(b)); }
        } else if (is_integer(tt)) ok = st_->work.set_integer(n, iv);
        else ok = st_->work.set_real(n, tt == T_FLOAT ? (double)(float)dv : dv);
        if (!ok) { undo_drop_last(); message("Value does not fit this tag"); return; }
        end_edit(); message("Pasted value");
    }
    void array_move(int dir)
    {
        const unsigned n = row_count(), i = sel_;
        if (!n || (dir < 0 && i == 0) || (dir > 0 && i + 1 >= n)) return;
        Path p = dir_;
        if (!begin_edit()) return;
        const uint32_t a = node_at(st_->work, p); const unsigned j = dir < 0 ? i - 1 : i + 1;
        const int64_t x = st_->work.array_get(a, i), y = st_->work.array_get(a, j);
        st_->work.array_put(a, i, y); st_->work.array_put(a, j, x); ++st_->work.revision;
        sel_ = j; end_edit();
    }
    void copy()
    {
        if (array_view_) {   // one element, as an unnamed int / byte tag
            const uint32_t a = dir_node(); if (a == kNil || !row_count()) return;
            const bool b = st_->work.at(a).type == T_BYTE_ARRAY; const int64_t v = st_->work.array_get(a, sel_);
            uint8_t *c = st_->clip; c[0] = b ? T_BYTE : T_INT; c[1] = 0; c[2] = 0;
            if (b) { c[3] = (uint8_t)v; clip_len_ = 4; } else { const uint32_t u = (uint32_t)v; c[3] = (uint8_t)(u >> 24); c[4] = (uint8_t)(u >> 16); c[5] = (uint8_t)(u >> 8); c[6] = (uint8_t)u; clip_len_ = 7; }
            snprintf(clip_label_, sizeof clip_label_, "%s %lld", type_name(c[0]), (long long)v);
            message("Copied element"); return;
        }
        const uint32_t s = sel_node(); if (s == kNil) return;
        const size_t n = st_->work.serialize_node(s, st_->clip, sizeof st_->clip, true);
        if (!n) { message("Too large for the clipboard (64 KB)"); return; }
        clip_len_ = n;
        char nm[24]; ascii_preview(st_->work.bytes(st_->work.at(s).name_off), st_->work.at(s).name_len, nm, sizeof nm);
        snprintf(clip_label_, sizeof clip_label_, "%s %s", type_name(st_->work.at(s).type), nm[0] ? nm : "entry");
        message("Copied");
    }
    void paste(uint32_t parent0, bool after_sel)
    {
        if (!clip_len_ || parent0 == kNil) return;
        Path pp; path_of(st_->work, parent0, &pp);
        const unsigned sel = sel_;
        if (!begin_edit(4096, clip_len_ * 2 + 1024)) return;
        const uint32_t parent = node_at(st_->work, pp);
        const uint32_t after = after_sel ? st_->work.child_at(parent, sel) : kNil;
        const uint32_t c = st_->work.insert_serialized(parent, after, st_->clip, clip_len_);
        if (c == kNil) { message(st_->work.at(parent).type == T_LIST ? "List holds a different type" : "Cannot paste here"); undo_drop_last(); return; }
        if (after_sel) sel_ = st_->work.index_of(c); else { enter(parent); sel_ = st_->work.index_of(c); }
        end_edit(); message("Pasted");
    }
    void paste_over()
    {
        const uint32_t s0 = sel_node(); if (s0 == kNil || !clip_len_) return;
        Path p; path_of(st_->work, s0, &p);
        if (!begin_edit(4096, clip_len_ * 2 + 1024)) return;
        const uint32_t s = node_at(st_->work, p);
        const uint32_t c = st_->work.replace_serialized(s, st_->clip, clip_len_);
        if (c == kNil) { undo_restore_last(); message("Cannot replace with that type here"); return; }
        sel_ = st_->work.index_of(c); end_edit(); message("Replaced");
    }
    void undo_drop_last() { if (undo_n_) { --undo_n_; undo_used_ = undo_off_[undo_n_]; } }
    void undo_restore_last()
    {
        if (!undo_n_) return;
        --undo_n_; st_->work.parse(st_->undo + undo_off_[undo_n_], undo_len_[undo_n_]); undo_used_ = undo_off_[undo_n_];
    }
    void delete_selected()
    {
        const uint32_t s0 = sel_node(); if (s0 == kNil) return;
        Path p; path_of(st_->work, s0, &p);
        if (!begin_edit()) return;
        st_->work.remove(node_at(st_->work, p)); end_edit(); message("Deleted (L1 undoes)");
    }
    void array_insert(bool append)
    {
        Path p = dir_; const unsigned at = append ? row_count() : sel_;
        if (!begin_edit(8, 300 * 1024)) return;
        const uint32_t a = node_at(st_->work, p);
        if (!st_->work.array_splice(a, at, 0, 1, 0)) { message("Array is full"); return; }
        sel_ = at; end_edit();
    }
    void array_delete()
    {
        Path p = dir_;
        if (!row_count() || !begin_edit(8, 300 * 1024)) return;
        const uint32_t a = node_at(st_->work, p);
        st_->work.array_splice(a, sel_, 1, 0, 0); end_edit();
    }

    // ---- templates -------------------------------------------------------------------------
    void open_templates(uint32_t parent, bool after_sel)
    {
        if (parent == kNil) return;
        tmpl_n_ = (unsigned)templates_for(st_->work, parent, kind_, tmpl_, kMaxTemplates);
        tmpl_s_ = Scroll{0, 0}; path_of(st_->work, parent, &tmpl_parent_); tmpl_after_sel_ = after_sel;
        if (!tmpl_n_) { message(st_->work.at(parent).type == T_LIST ? "Set the list type first" : "Nothing can be added here"); return; }
        screen_ = Screen::Templates;
    }
    void template_row(unsigned i, ViewRow *r) const
    {
        r->role = R_TARGET; r->enabled = true;
        snprintf(r->label, sizeof r->label, "%s", tmpl_[i]->label);
        snprintf(r->value, sizeof r->value, "%s", tmpl_[i]->snbt);
    }
    void templates_key(Key k, bool repeat)
    {
        switch (k) {
        case Key::Up: tmpl_s_.move(-1, tmpl_n_, repeat); break;
        case Key::Down: tmpl_s_.move(1, tmpl_n_, repeat); break;
        case Key::Left: tmpl_s_.move(-(int)kVisible, tmpl_n_, repeat); break;
        case Key::Right: tmpl_s_.move((int)kVisible, tmpl_n_, repeat); break;
        case Key::Circle: if (!repeat) screen_ = Screen::Tree; break;
        case Key::Cross: if (!repeat && tmpl_n_) add_template(tmpl_[tmpl_sel_]); break;
        default: break;
        }
    }
    void add_template(const Template *t)
    {
        screen_ = Screen::Tree;
        const unsigned sel = sel_;
        if (!begin_edit()) return;
        const uint32_t parent = node_at(st_->work, tmpl_parent_);
        const bool list = st_->work.at(parent).type == T_LIST;
        char name[64] = {};
        if (!list) {
            const char *base = t->name[0] ? t->name : "new_tag";
            if (st_->work.find(parent, base) != kNil && strncmp(base, "new_", 4)) { undo_drop_last(); message("That tag already exists"); return; }
            st_->work.unique_name(parent, base, name, sizeof name);
        }
        const uint32_t c = snbt_build(st_->work, parent, list ? nullptr : name, t->snbt);
        if (c == kNil) { undo_restore_last(); message(list ? "List holds a different type" : "Could not add"); return; }
        // place after the selected row when adding next to it
        const bool here = tmpl_after_sel_ && dir_node() == parent;
        if (here) { const uint32_t after = st_->work.child_at(parent, sel); if (after != kNil && after != c) { st_->work.unlink(c); st_->work.link_after(parent, after, c); } }
        // items: first free slot
        if (list && (st_->work.name_eq(parent, "Inventory") || st_->work.name_eq(parent, "Items") || st_->work.name_eq(parent, "EnderItems"))) {
            const uint32_t slot = st_->work.find(c, "Slot");
            if (slot != kNil) for (int sv = 0; sv < 128; ++sv) {
                bool used = false;
                for (uint32_t o = st_->work.at(parent).first; o != kNil; o = st_->work.at(o).next) { const uint32_t os = o == c ? kNil : st_->work.find(o, "Slot"); if (os != kNil && st_->work.at(os).v.i == sv) used = true; }
                if (!used) { st_->work.set_integer(slot, sv); break; }
            }
        }
        if (!here) enter(parent);
        sel_ = st_->work.index_of(c); end_edit(); clamp_sel();
        if (!list && !strncmp(name, "new_", 4)) open_text(c, TextMode::NewName);
        else message("Added");
    }
    void open_list_type(uint32_t list)
    {
        if (list == kNil) return;
        path_of(st_->work, list, &choice_path_); choice_mode_ = ChoiceMode::ListType; choice_n_ = 11; choice_sel_ = 9; choice_return_ = Screen::Tree;
        choice_s_.first = choice_s_.sel > 2 ? choice_s_.sel - 2 : 0; choice_s_.clamp(choice_n_); screen_ = Screen::Choice;
    }

    // ---- number editor ---------------------------------------------------------------------
    void build_num_rows()
    {
        num_rows_n_ = 0;
        auto add = [&](NumRow r) { num_rows_[num_rows_n_++] = r; };
        add(N_VALUE); add(N_DONE); add(N_STEP); add(N_DIGITS); add(N_RESET);
        if (num_hint_.pick != Pick::None) add(N_PICK);
        add(N_ZERO);
        if (is_integer(num_type_)) { add(N_MIN); add(N_MAX); }
        add(N_NEGATE);
        if (num_type_ == T_BYTE) add(N_TOGGLE);
    }
    void open_number(uint32_t s)
    {
        const Node &x = st_->work.at(s);
        path_of(st_->work, s, &num_path_); num_elem_ = -1; num_type_ = x.type;
        num_hint_ = hint_of(s);
        num_i_ = num_orig_i_ = hint_value(num_hint_, x.v.i); num_d_ = num_orig_d_ = x.v.d;
        num_step_ = num_hint_.step > 0 ? num_hint_.step : 1;
        ascii_preview(st_->work.bytes(x.name_off), x.name_len, num_name_, sizeof num_name_);
        if (!num_name_[0]) snprintf(num_name_, sizeof num_name_, "[%u]", st_->work.index_of(s));
        num_s_ = Scroll{0, 0}; num_digits_ = false; build_num_rows(); screen_ = Screen::Number;
    }
    void open_number_array(unsigned idx)
    {
        const uint32_t a = dir_node(); if (a == kNil || idx >= st_->work.at(a).v.b.len) return;
        if (node_readonly(a)) { message("Read-only"); return; }
        num_path_ = dir_; num_elem_ = idx; num_type_ = st_->work.at(a).type == T_BYTE_ARRAY ? T_BYTE : T_INT;
        num_hint_ = array_elem_hint(st_->work, a, kind_); num_step_ = 1;
        num_i_ = num_orig_i_ = hint_value(num_hint_, st_->work.array_get(a, idx));
        snprintf(num_name_, sizeof num_name_, "[%u]", idx);
        num_s_ = Scroll{0, 0}; num_digits_ = false; build_num_rows(); screen_ = Screen::Number;
    }
    void num_limits(int64_t *lo, int64_t *hi) const
    {
        int_range(num_type_, lo, hi);
        if (num_type_ == T_BYTE && (num_elem_ >= 0 || num_hint_.u8)) { *lo = -128; *hi = 255; }   // the server takes 0-255 for bytes too
        if (num_hint_.u8) *lo = 0;
        if (num_hint_.ranged) { if (num_hint_.lo > *lo) *lo = num_hint_.lo; if (num_hint_.hi < *hi) *hi = num_hint_.hi; }
    }
    // One step down/up. Integers use exact 64-bit arithmetic (saturating), so longs keep every digit.
    void num_adjust(bool plus)
    {
        if (is_integer(num_type_)) {
            int64_t lo, hi; num_limits(&lo, &hi);
            const int64_t st = num_step_ >= 9.2e18 ? INT64_MAX : (int64_t)num_step_;
            int64_t v;
            if (__builtin_add_overflow(num_i_, plus ? st : -st, &v)) v = plus ? INT64_MAX : INT64_MIN;
            num_i_ = v < lo ? lo : v > hi ? hi : v;
        } else {
            num_d_ += plus ? num_step_ : -num_step_;
            num_clamp_real();
            // kill accumulated binary noise at the current step size
            if (num_step_ < 1) { const double q = num_d_ / num_step_; const double r = (double)(int64_t)(q + (q >= 0 ? 0.5 : -0.5)); num_d_ = r * num_step_; }
        }
    }
    void num_clamp_real()
    {
        if (num_type_ == T_FLOAT) { if (num_d_ > 3.4e38) num_d_ = 3.4e38; if (num_d_ < -3.4e38) num_d_ = -3.4e38; num_d_ = (double)(float)num_d_; }
        if (num_d_ != num_d_) num_d_ = 0;   // NaN
    }
    // ---- digit editor ----
    unsigned int_width() const { return num_type_ == T_BYTE ? 3 : num_type_ == T_SHORT ? 5 : num_type_ == T_INT ? 10 : 19; }
    void digits_open()
    {
        if (is_integer(num_type_)) {
            const uint64_t mag = num_i_ < 0 ? (uint64_t)0 - (uint64_t)num_i_ : (uint64_t)num_i_;
            const unsigned w = int_width();
            dig_[0] = num_i_ < 0 ? '-' : '+';
            uint64_t m = mag; for (unsigned i = w; i > 0; --i) { dig_[i] = (char)('0' + m % 10); m /= 10; }
            dig_len_ = w + 1; dig_[dig_len_] = 0;
            dig_cur_ = dig_len_ - 1;   // ones
        } else {
            // sign, mantissa d.ddd..., E, exponent sign, exponent digits
            const bool f = num_type_ == T_FLOAT;
            char b[40]; snprintf(b, sizeof b, f ? "%+.6E" : "%+.9E", num_d_);
            // normalise the exponent to 2 (float) / 3 (double) digits
            const char *e = strchr(b, 'E'); const int ex = e ? atoi_(e + 1) : 0;
            const size_t ml = e ? (size_t)(e - b) : strlen(b);
            const int ew = f ? 2 : 3; const int ax = ex < 0 ? -ex : ex;
            snprintf(dig_, sizeof dig_, "%.*sE%c%0*d", (int)ml, b, ex < 0 ? '-' : '+', ew, ax);
            dig_len_ = (unsigned)strlen(dig_);
            dig_cur_ = 1;
        }
        num_digits_ = true;
    }
    static int atoi_(const char *p) { int sg = 1; if (*p == '-' || *p == '+') { if (*p == '-') sg = -1; ++p; } int v = 0; while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0'); return sg * v; }
    bool dig_editable(unsigned i) const { return i < dig_len_ && (dig_[i] == '+' || dig_[i] == '-' || (dig_[i] >= '0' && dig_[i] <= '9')); }
    void digits_move(int dir)
    {
        unsigned i = dig_cur_;
        for (unsigned k = 0; k < dig_len_; ++k) { i = (i + dig_len_ + (dir > 0 ? 1 : dig_len_ - 1)) % dig_len_; if (dig_editable(i)) { dig_cur_ = i; return; } }
    }
    void digits_change(int dir)
    {
        char &c = dig_[dig_cur_];
        if (c == '+' || c == '-') { c = c == '+' ? '-' : '+'; return; }
        if (c >= '0' && c <= '9') c = (char)('0' + (c - '0' + (dir > 0 ? 1 : 9)) % 10);
    }
    // Parses the digit string back into the value (clamped to the tag's range). False if it does not fit.
    bool digits_commit()
    {
        if (is_integer(num_type_)) {
            uint64_t mag = 0; bool over = false;
            for (unsigned i = 1; i < dig_len_; ++i) { if (mag > (UINT64_MAX - 9) / 10) over = true; mag = mag * 10 + (uint64_t)(dig_[i] - '0'); }
            int64_t lo, hi; num_limits(&lo, &hi);
            const bool neg = dig_[0] == '-';
            if (!over && !neg && mag <= (uint64_t)INT64_MAX) { const int64_t v = (int64_t)mag; if (v >= lo && v <= hi) { num_i_ = v; return true; } }
            if (!over && neg && mag <= (uint64_t)INT64_MAX + 1) { const int64_t v = mag == (uint64_t)INT64_MAX + 1 ? INT64_MIN : -(int64_t)mag; if (v >= lo && v <= hi) { num_i_ = v; return true; } }
            char m[64]; snprintf(m, sizeof m, "Out of range %lld..%lld", (long long)lo, (long long)hi); message(m);
            return false;
        }
        // "+d.ddddE+ee": integer mantissa M with k fraction digits -> M * 10^(e-k), scaled by an exact power of ten
        uint64_t m = 0; int k = 0, ex = 0; bool frac = false, exneg = false; unsigned i = 1;
        for (; i < dig_len_ && dig_[i] != 'E'; ++i) { if (dig_[i] == '.') { frac = true; continue; } m = m * 10 + (uint64_t)(dig_[i] - '0'); if (frac) ++k; }
        if (i < dig_len_) { ++i; exneg = dig_[i] == '-'; for (++i; i < dig_len_; ++i) ex = ex * 10 + (dig_[i] - '0'); }
        const int p = (exneg ? -ex : ex) - k;
        double v = (double)m, scale = 1; const int ap = p < 0 ? -p : p;
        for (int j = 0; j < ap && j < 400; ++j) scale *= 10;
        v = p < 0 ? v / scale : v * scale;
        if (dig_[0] == '-') v = -v;
        if (num_type_ == T_FLOAT && (v > 3.4e38 || v < -3.4e38)) { message("Too large for a float"); return false; }
        if (v != v || v > 1.7e308 || v < -1.7e308) { message("Not a finite number"); return false; }
        num_d_ = v; num_clamp_real(); return true;
    }
    void digits_key(Key k, bool repeat)
    {
        switch (k) {
        case Key::Up: digits_change(1); break;
        case Key::Down: digits_change(-1); break;
        case Key::Left: digits_move(-1); break;
        case Key::Right: digits_move(1); break;
        case Key::L1: if (!repeat) { dig_cur_ = 0; if (!dig_editable(0)) digits_move(1); } break;
        case Key::R1: if (!repeat) { dig_cur_ = dig_len_ - 1; } break;
        case Key::Cross: if (!repeat && digits_commit()) { num_digits_ = false; num_row_ = 0; } break;
        case Key::Circle: if (!repeat) num_digits_ = false; break;
        default: break;
        }
    }
    void digits_text(char *out, size_t cap) const
    {
        size_t o = 0;
        for (unsigned i = 0; i < dig_len_ && o + 3 < cap; ++i) {
            if (i == dig_cur_) { out[o++] = '['; out[o++] = dig_[i]; out[o++] = ']'; } else out[o++] = dig_[i];
        }
        out[o] = 0;
    }
    void num_step(bool coarser)
    {
        const double maxs = num_type_ == T_LONG ? 1e18 : is_integer(num_type_) ? 1e9 : 1e6, mins = is_integer(num_type_) ? 1 : 0.001;
        num_step_ = coarser ? num_step_ * 10 : num_step_ / 10;
        if (num_step_ > maxs) num_step_ = maxs;
        if (num_step_ < mins) num_step_ = mins;
        char m[40]; fmt_short_real(num_step_, m, sizeof m); char t[48]; snprintf(t, sizeof t, "Step %s", m); message(t);
    }
    void number_key(Key k, bool repeat)
    {
        if (num_digits_) { digits_key(k, repeat); return; }
        const NumRow row = num_rows_[num_row_];
        switch (k) {
        case Key::Up: num_s_.move(-1, num_rows_n_, repeat); break;
        case Key::Down: num_s_.move(1, num_rows_n_, repeat); break;
        case Key::Left: case Key::Right: {
            const bool plus = k == Key::Right;
            if (row == N_STEP) { if (!repeat) num_step(plus); break; }
            num_row_ = 0;
            num_adjust(plus);
            break;
        }
        case Key::L1: if (!repeat) num_step(false); break;
        case Key::R1: if (!repeat) num_step(true); break;
        case Key::Triangle: if (!repeat && num_hint_.pick != Pick::None) open_choice_number(); break;
        case Key::Circle: if (!repeat) screen_ = Screen::Tree; break;
        case Key::Square: if (!repeat) { commit_number(); if (screen_ == Screen::Tree && edits_) apply(); } break;   // keep + apply in one press
        case Key::Cross:
            if (repeat) break;
            switch (row) {
            case N_VALUE: case N_DONE: commit_number(); break;
            case N_STEP: num_step(true); break;
            case N_RESET: num_i_ = num_orig_i_; num_d_ = num_orig_d_; break;
            case N_ZERO: { int64_t lo, hi; num_limits(&lo, &hi); num_i_ = 0 < lo ? lo : 0 > hi ? hi : 0; num_d_ = 0; break; }
            case N_DIGITS: digits_open(); break;
            case N_MIN: { int64_t lo, hi; num_limits(&lo, &hi); num_i_ = lo; break; }
            case N_MAX: { int64_t lo, hi; num_limits(&lo, &hi); num_i_ = hi; break; }
            case N_NEGATE: if (is_integer(num_type_)) { int64_t lo, hi; num_limits(&lo, &hi); if (num_i_ != INT64_MIN && -num_i_ >= lo && -num_i_ <= hi) num_i_ = -num_i_; } else num_d_ = -num_d_; break;
            case N_TOGGLE: num_i_ = num_i_ ? 0 : 1; break;
            case N_PICK: open_choice_number(); break;
            }
            break;
        default: break;
        }
    }
    void commit_number()
    {
        if (!begin_edit()) return;
        const uint32_t n = node_at(st_->work, num_path_);
        bool ok;
        // bytes edited as 0..255 are stored signed
        const int64_t stored = num_type_ == T_BYTE && num_i_ > 127 && num_i_ <= 255 ? num_i_ - 256 : num_i_;
        if (num_elem_ >= 0) { int64_t lo, hi; num_limits(&lo, &hi); ok = num_i_ >= lo && num_i_ <= hi; if (ok) { st_->work.array_put(n, (uint32_t)num_elem_, stored); ++st_->work.revision; } }
        else ok = is_integer(num_type_) ? st_->work.set_integer(n, stored) : st_->work.set_real(n, num_d_);
        screen_ = Screen::Tree;
        const bool same = num_elem_ < 0 && (is_integer(num_type_) ? num_i_ == num_orig_i_ : dbits(num_d_) == dbits(num_orig_d_));
        if (!ok) { undo_drop_last(); message("Value out of range"); return; }
        if (same) { undo_drop_last(); return; }
        end_edit();
    }
    void num_value_text(char *out, size_t cap) const
    {
        if (is_integer(num_type_)) {
            const char *nm = num_hint_.pick != Pick::None ? choice_name(num_hint_.pick, num_i_) : nullptr;
            if (num_hint_.boolean) snprintf(out, cap, "%lld %s", (long long)num_i_, num_i_ ? "true" : "false");
            else if (nm) snprintf(out, cap, "%lld %s", (long long)num_i_, nm); else snprintf(out, cap, "%lld", (long long)num_i_);
        } else { char b[40]; snprintf(b, sizeof b, "%.6g", num_d_); fmt_short_real(num_d_, out, cap); if (strlen(out) > 24) snprintf(out, cap, "%s", b); }
    }
    void num_row(unsigned i, ViewRow *r) const
    {
        r->role = R_TARGET; r->enabled = true;
        char b[40];
        switch (num_rows_[i]) {
        case N_VALUE: snprintf(r->label, sizeof r->label, "Value"); num_value_text(r->value, sizeof r->value); r->role = R_CONTROL; r->control = 2; r->type = num_type_; break;
        case N_STEP: snprintf(r->label, sizeof r->label, "Step size (L1/R1)"); fmt_short_real(num_step_, r->value, sizeof r->value); r->role = R_CONTROL; r->control = 2; break;
        case N_DONE: snprintf(r->label, sizeof r->label, "Keep this value (X)"); break;
        case N_RESET: snprintf(r->label, sizeof r->label, "Back to original"); if (is_integer(num_type_)) snprintf(r->value, sizeof r->value, "%lld", (long long)num_orig_i_); else { fmt_short_real(num_orig_d_, b, sizeof b); snprintf(r->value, sizeof r->value, "%s", b); } break;
        case N_ZERO: snprintf(r->label, sizeof r->label, "Set to 0"); break;
        case N_MIN: { int64_t lo, hi; num_limits(&lo, &hi); snprintf(r->label, sizeof r->label, "Minimum"); snprintf(r->value, sizeof r->value, "%lld", (long long)lo); break; }
        case N_MAX: { int64_t lo, hi; num_limits(&lo, &hi); snprintf(r->label, sizeof r->label, "Maximum"); snprintf(r->value, sizeof r->value, "%lld", (long long)hi); break; }
        case N_NEGATE: snprintf(r->label, sizeof r->label, "Negate"); break;
        case N_TOGGLE: snprintf(r->label, sizeof r->label, "Toggle 0 / 1"); break;
        case N_PICK: snprintf(r->label, sizeof r->label, "Pick from list (Triangle)"); break;
        case N_DIGITS: snprintf(r->label, sizeof r->label, "Edit digit by digit"); break;
        }
    }
    void view_number(View *v) const
    {
        char title[48]; snprintf(title, sizeof title, "Edit %s (%s)", num_name_, type_name(num_type_));
        if (num_digits_) {
            snprintf(v->rows[0].label, sizeof v->rows[0].label, "%s", title); v->rows[0].role = R_HEADING;
            ViewRow &r = v->rows[1]; r.role = R_CONTROL; r.control = 2; r.enabled = true; r.type = num_type_;
            snprintf(r.label, sizeof r.label, "Digits"); digits_text(r.value, sizeof r.value);
            ViewRow &h = v->rows[2]; h.role = R_STATUS; snprintf(h.label, sizeof h.label, "Up/Down digit  Left/Right move");
            ViewRow &h2 = v->rows[3]; h2.role = R_STATUS; snprintf(h2.label, sizeof h2.label, "Cross keep  Circle back");
            v->count = 4; v->selected = 1; v->edit = true; return;
        }
        view_list(v, title, num_rows_n_, num_s_, &Editor::num_row);
        v->edit = true;
    }

    // ---- choice lists ----------------------------------------------------------------------
    void open_choice_value(uint32_t s, Pick p)
    {
        path_of(st_->work, s, &choice_path_); choice_pick_ = p; choice_mode_ = ChoiceMode::Value; choice_return_ = Screen::Tree;
        choice_n_ = (unsigned)choice_count(p);
        const Node &x = st_->work.at(s);
        choice_sel_ = (unsigned)(x.type == T_STRING ? choice_index(p, 0, (const char *)st_->work.bytes(x.v.b.off), x.v.b.len) : choice_index(p, hint_value(hint_of(s), x.v.i), nullptr, 0));
        choice_s_.first = choice_s_.sel > 2 ? choice_s_.sel - 2 : 0; choice_s_.clamp(choice_n_); screen_ = Screen::Choice;
    }
    void open_choice_number()
    {
        choice_pick_ = num_hint_.pick; choice_mode_ = ChoiceMode::Value; choice_return_ = Screen::Number; choice_path_ = num_path_;
        choice_n_ = (unsigned)choice_count(choice_pick_); choice_sel_ = (unsigned)choice_index(choice_pick_, num_i_, nullptr, 0);
        choice_s_.first = choice_s_.sel > 2 ? choice_s_.sel - 2 : 0; choice_s_.clamp(choice_n_); screen_ = Screen::Choice;
    }
    void choice_row(unsigned i, ViewRow *r) const
    {
        r->role = R_TARGET; r->enabled = true;
        if (choice_mode_ == ChoiceMode::ListType) { static const uint8_t types[] = {T_BYTE, T_SHORT, T_INT, T_LONG, T_FLOAT, T_DOUBLE, T_STRING, T_LIST, T_COMPOUND, T_BYTE_ARRAY, T_INT_ARRAY}; snprintf(r->label, sizeof r->label, "%s", type_name(types[i])); return; }
        if (choice_mode_ == ChoiceMode::Preset) { snprintf(r->label, sizeof r->label, "%s", kTextPresets[i].label); return; }
        if (choice_mode_ == ChoiceMode::TextPart) { text_part_row(i, r); return; }
        const Choice c = choice_at(choice_pick_, i);
        snprintf(r->label, sizeof r->label, "%s", c.name);
        if (!pick_is_string(choice_pick_)) snprintf(r->value, sizeof r->value, "%lld", (long long)c.id);
    }
    void view_choice(View *v) const
    {
        char tp[40]; snprintf(tp, sizeof tp, "Long text: %u chars", text_total_);
        const char *t = choice_mode_ == ChoiceMode::ListType ? "List element type" : choice_mode_ == ChoiceMode::Preset ? "Text presets" : choice_mode_ == ChoiceMode::TextPart ? tp : "Choose a value";
        view_list(v, t, choice_n_, choice_s_, &Editor::choice_row);
    }
    void choice_key(Key k, bool repeat)
    {
        switch (k) {
        case Key::Up: choice_s_.move(-1, choice_n_, repeat); break;
        case Key::Down: choice_s_.move(1, choice_n_, repeat); break;
        case Key::Left: choice_s_.move(-(int)kVisible, choice_n_, repeat); break;
        case Key::Right: choice_s_.move((int)kVisible, choice_n_, repeat); break;
        case Key::L1: choice_s_.move(-50, choice_n_, repeat); break;
        case Key::R1: choice_s_.move(50, choice_n_, repeat); break;
        case Key::Circle: if (!repeat) screen_ = choice_return_; break;
        case Key::Cross: if (!repeat && choice_n_) choose(); break;
        default: break;
        }
    }
    void choose()
    {
        if (choice_mode_ == ChoiceMode::TextPart) { open_text_part(choice_sel_); return; }
        if (choice_mode_ == ChoiceMode::Preset) {
            const TextPreset &p = kTextPresets[choice_sel_];
            uint16_t u[64]; const size_t n = mutf8_to_utf16((const uint8_t *)p.text, strlen(p.text), u, 64);
            if (!p.append) kb_len_ = 0;
            for (size_t i = 0; i < n && kb_len_ < kb_max_; ++i) kb_[kb_len_++] = u[i];
            screen_ = Screen::Text; return;
        }
        if (choice_mode_ == ChoiceMode::ListType) {
            static const uint8_t types[] = {T_BYTE, T_SHORT, T_INT, T_LONG, T_FLOAT, T_DOUBLE, T_STRING, T_LIST, T_COMPOUND, T_BYTE_ARRAY, T_INT_ARRAY};
            screen_ = Screen::Tree;
            if (!begin_edit()) return;
            if (!st_->work.set_list_type(node_at(st_->work, choice_path_), types[choice_sel_])) { undo_drop_last(); message("Only empty lists can change type"); return; }
            end_edit(); return;
        }
        const Choice c = choice_at(choice_pick_, choice_sel_);
        if (choice_return_ == Screen::Number) { num_i_ = c.id; int64_t lo, hi; num_limits(&lo, &hi); if (num_i_ < lo) num_i_ = lo; if (num_i_ > hi) num_i_ = hi; screen_ = Screen::Number; num_row_ = 0; return; }
        screen_ = Screen::Tree;
        if (!begin_edit()) return;
        const uint32_t n = node_at(st_->work, choice_path_);
        const int64_t iv = st_->work.at(n).type == T_BYTE && c.id > 127 && c.id <= 255 ? c.id - 256 : c.id;   // 0-255 ids in a byte
        const bool ok = st_->work.at(n).type == T_STRING ? st_->work.set_string(n, (const uint8_t *)c.name, strlen(c.name)) : st_->work.set_integer(n, iv);
        if (!ok) { undo_drop_last(); message("Value does not fit this tag"); return; }
        end_edit();
    }

    // ---- search -----------------------------------------------------------------------------
    static char lower_(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }
    static bool contains_ci(const char *hay, size_t hl, const char *needle)
    {
        const size_t nl = strlen(needle);
        if (!nl || nl > hl) return false;
        for (size_t i = 0; i + nl <= hl; ++i) { size_t k = 0; while (k < nl && lower_(hay[i + k]) == needle[k]) ++k; if (k == nl) return true; }
        return false;
    }
    bool node_matches(uint32_t n) const
    {
        const Doc &d = st_->work; const Node &x = d.at(n);
        if (contains_ci((const char *)d.bytes(x.name_off), x.name_len, needle_)) return true;
        if (x.type == T_STRING && contains_ci((const char *)d.bytes(x.v.b.off), x.v.b.len, needle_)) return true;
        if (x.type == T_COMPOUND || x.type == T_LIST) return false;
        char v[64]; describe_value(d, n, kind_, v, sizeof v);
        return contains_ci(v, strlen(v), needle_);
    }
    // pre-order successor (children first, then siblings, then the parents' siblings)
    uint32_t next_preorder(uint32_t n) const
    {
        const Doc &d = st_->work;
        if ((d.at(n).type == T_COMPOUND || d.at(n).type == T_LIST) && d.at(n).first != kNil) return d.at(n).first;
        for (uint32_t x = n; x != kNil && x != d.root; x = d.at(x).parent) if (d.at(x).next != kNil) return d.at(x).next;
        return kNil;
    }
public:
    // Jumps to the next (dir > 0) / previous match after the selection, wrapping. Returns the number of matches.
    unsigned find(int dir)
    {
        if (!needle_[0] || !st_->work.valid()) return 0;
        const Doc &d = st_->work;
        const uint32_t cur = array_view_ ? dir_node() : sel_node();
        unsigned total = 0; bool passed = cur == kNil;
        uint32_t first = kNil, last_before = kNil, first_after = kNil, last = kNil;
        for (uint32_t n = d.at(d.root).first; n != kNil; n = next_preorder(n)) {
            if (n == cur) passed = true;
            if (!node_matches(n)) continue;
            ++total; if (first == kNil) first = n; last = n;
            if (!passed) last_before = n;
            else if (n != cur && first_after == kNil) first_after = n;
        }
        if (!total) { char m[64]; snprintf(m, sizeof m, "No match for \"%s\"", needle_); message(m); return 0; }
        uint32_t to = dir > 0 ? (first_after != kNil ? first_after : first) : (last_before != kNil ? last_before : last);
        // index of the target among the matches
        unsigned idx = 0; for (uint32_t n = d.at(d.root).first; n != kNil; n = next_preorder(n)) if (node_matches(n)) { ++idx; if (n == to) break; }
        const uint32_t parent = d.at(to).parent;
        path_of(d, parent, &dir_); array_view_ = false; sel_ = d.index_of(to); first_ = sel_ > 2 ? sel_ - 2 : 0; clamp_sel();
        char nm[24]; ascii_preview(d.bytes(d.at(to).name_off), d.at(to).name_len, nm, sizeof nm);
        char m[64]; snprintf(m, sizeof m, "Match %u/%u %s  (R2 next, L2 prev)", idx, total, nm); message(m);
        return total;
    }
    const char *search_text() const { return needle_; }
private:
    void open_search()
    {
        text_mode_ = TextMode::Search; text_pre_ = text_total_ = 0;
        kb_len_ = 0; for (const char *c = needle_; *c && kb_len_ < 60; ++c) kb_[kb_len_++] = (uint8_t)*c;
        kb_max_ = 60; kb_sel_ = 0; kb_shift_ = false; kb_symbols_ = false; screen_ = Screen::Text;
    }

    // ---- text editor (on-screen keyboard of the tab menu renderer) -------------------------
    static constexpr unsigned kPartUnits = 100;
    void open_text(uint32_t s, TextMode mode)
    {
        const Node &x = st_->work.at(s);
        path_of(st_->work, s, &text_path_); text_mode_ = mode; text_pre_ = 0; text_total_ = 0; text_parts_ = 0;
        if (mode == TextMode::Value) {
            const size_t n = mutf8_to_utf16(st_->work.bytes(x.v.b.off), x.v.b.len, st_->text_units, sizeof st_->text_units / 2);
            text_total_ = (unsigned)n;
            if (n > 127) {   // long text: choose the part to edit (the rest is kept)
                text_parts_ = (unsigned)((n + kPartUnits - 1) / kPartUnits);
                choice_mode_ = ChoiceMode::TextPart; choice_n_ = text_parts_ + 1; choice_s_ = Scroll{0, 0}; choice_return_ = Screen::Tree; screen_ = Screen::Choice;
                return;
            }
            text_win_ = (unsigned)n; for (unsigned i = 0; i < n; ++i) kb_[i] = st_->text_units[i]; kb_len_ = (unsigned)n; kb_max_ = 127;
        }
        else { kb_len_ = (unsigned)mutf8_to_utf16(st_->work.bytes(x.name_off), x.name_len, kb_, 64); kb_max_ = 64; }
        kb_sel_ = 0; kb_shift_ = false; kb_symbols_ = false; screen_ = Screen::Text;
    }
    // part k of a long text (k == parts: append at the end)
    void open_text_part(unsigned k)
    {
        text_part_ = k;
        text_pre_ = k >= text_parts_ ? text_total_ : k * kPartUnits;
        text_win_ = text_total_ - text_pre_ < kPartUnits ? text_total_ - text_pre_ : kPartUnits;
        for (unsigned i = 0; i < text_win_; ++i) kb_[i] = st_->text_units[text_pre_ + i];
        kb_len_ = text_win_; kb_max_ = 127; kb_sel_ = 0; kb_shift_ = false; kb_symbols_ = false; screen_ = Screen::Text;
    }
    void text_part_row(unsigned i, ViewRow *r) const
    {
        r->role = R_TARGET; r->enabled = true;
        if (i >= text_parts_) { snprintf(r->label, sizeof r->label, "Append at the end"); snprintf(r->value, sizeof r->value, "%u chars", text_total_); return; }
        const unsigned a = i * kPartUnits, b = a + kPartUnits < text_total_ ? a + kPartUnits : text_total_;
        snprintf(r->label, sizeof r->label, "Part %u (%u-%u)", i + 1, a + 1, b);
        char t[24]; unsigned k = 0; for (unsigned j = a; j < b && k < 20; ++j) t[k++] = st_->text_units[j] >= 0x20 && st_->text_units[j] < 0x7f ? (char)st_->text_units[j] : '?';
        t[k] = 0; snprintf(r->value, sizeof r->value, "%s", t);
    }
    void text_key(Key k, bool repeat)
    {
        unsigned row = kb_sel_ < 40 ? kb_sel_ / 10 : 4, col = kb_sel_ < 40 ? kb_sel_ % 10 : (kb_sel_ - 40) * 2;
        switch (k) {
        case Key::Up: if (row == 0) row = 4; else --row; break;
        case Key::Down: if (row == 4) row = 0; else ++row; break;
        case Key::Left: if (row == 4) col = col >= 2 ? col - 2 : 8; else col = col ? col - 1 : 9; break;
        case Key::Right: if (row == 4) col = col + 2 <= 8 ? col + 2 : 0; else col = col < 9 ? col + 1 : 0; break;
        case Key::L1: if (!repeat) kb_len_ = 0; return;
        case Key::R1: if (!repeat) kb_symbols_ = !kb_symbols_; return;
        case Key::Square: if (kb_len_) --kb_len_; return;
        case Key::Triangle: if (!repeat && text_mode_ == TextMode::Value) { choice_mode_ = ChoiceMode::Preset; choice_n_ = sizeof kTextPresets / sizeof kTextPresets[0]; choice_sel_ = 0; choice_return_ = Screen::Text; screen_ = Screen::Choice; } return;
        case Key::Circle: if (!repeat) text_cancel(); return;
        case Key::Cross: text_press(); return;
        default: return;
        }
        kb_sel_ = row == 4 ? 40 + col / 2 : row * 10 + col;
    }
    void text_press()
    {
        if (kb_sel_ < 40) {
            const char c = menu_feedback::keyboard_key(kb_sel_, kb_shift_, kb_symbols_);
            if (kb_len_ < kb_max_) kb_[kb_len_++] = (uint8_t)c; else message("Text limit reached");
            if (kb_shift_ && !kb_symbols_) kb_shift_ = false;
            return;
        }
        switch (kb_sel_ - 40) {
        case 0: if (kb_symbols_) kb_symbols_ = false; else kb_shift_ = !kb_shift_; break;
        case 1: if (kb_len_ < kb_max_) kb_[kb_len_++] = ' '; break;
        case 2: if (kb_len_) --kb_len_; break;
        case 3: commit_text(); break;
        case 4: text_cancel(); break;
        }
    }
    // Circle / Cancel key: one level back (a part of a long text returns to the part list)
    void text_cancel()
    {
        if (text_mode_ == TextMode::NewName) message("Added (default name kept)");
        if (text_mode_ == TextMode::Value && text_parts_) {
            choice_mode_ = ChoiceMode::TextPart; choice_n_ = text_parts_ + 1; choice_sel_ = text_part_; choice_return_ = Screen::Tree;
            choice_s_.clamp(choice_n_); screen_ = Screen::Choice; return;
        }
        screen_ = Screen::Tree;
    }
    void commit_text()
    {
        if (text_mode_ == TextMode::Search) {
            size_t k = 0; for (unsigned i = 0; i < kb_len_ && k + 1 < sizeof needle_; ++i) needle_[k++] = kb_[i] < 0x80 ? lower_((char)kb_[i]) : '?';
            needle_[k] = 0; screen_ = Screen::Tree;
            if (needle_[0]) find(1); else message("Search cleared");
            return;
        }
        if (text_mode_ == TextMode::Value && text_parts_) { commit_text_part(); return; }
        uint8_t enc[512];
        const size_t n = kb_len_ ? utf16_to_mutf8(kb_, kb_len_, enc, sizeof enc) : 0;
        if (kb_len_ && !n) { message("Text too long"); return; }
        if (text_mode_ != TextMode::Value) {
            if (!n) { message("A name cannot be empty"); return; }
            for (size_t i = 0; i < n; ++i) if (enc[i] < 0x20) { message("Invalid character in name"); return; }
        }
        screen_ = Screen::Tree;
        if (!begin_edit()) return;
        const uint32_t s = node_at(st_->work, text_path_);
        bool ok;
        if (text_mode_ == TextMode::Value) {
            const Node &x = st_->work.at(s);
            if (x.v.b.len == n && !memcmp(st_->work.bytes(x.v.b.off), enc, n)) { undo_drop_last(); return; }
            ok = st_->work.set_string(s, enc, n);
        } else {
            if (st_->work.name_is(s, (const char *)enc, n)) { undo_drop_last(); return; }
            ok = st_->work.set_name(s, (const char *)enc, n);
        }
        if (!ok) { undo_drop_last(); message(text_mode_ == TextMode::Value ? "Could not set text" : "A tag with that name exists"); return; }
        if (text_mode_ == TextMode::NewName) { undo_drop_last(); message("Added"); clamp_sel(); return; }  // part of the add
        end_edit();
    }
    // writes part text_part_ back into the whole value: before + keyboard + after
    void commit_text_part()
    {
        const unsigned post = text_total_ - text_pre_ - text_win_;
        const unsigned total = text_pre_ + kb_len_ + post;
        if (total > sizeof st_->text_units / 2) { message("Text too long"); return; }
        uint16_t *u = st_->text_units;
        memmove(u + text_pre_ + kb_len_, u + text_pre_ + text_win_, post * sizeof(uint16_t));
        for (unsigned i = 0; i < kb_len_; ++i) u[text_pre_ + i] = kb_[i];
        const size_t n = total ? utf16_to_mutf8(u, total, st_->text_bytes, sizeof st_->text_bytes) : 0;
        // keep the buffer consistent for another part even if the write fails
        text_total_ = total; text_win_ = kb_len_;
        if (total && (!n || n > 65535)) { message("Text too long (64 KB)"); return; }
        screen_ = Screen::Tree;
        if (!begin_edit(8, n + 64)) return;
        const uint32_t s = node_at(st_->work, text_path_);
        const Node &x = st_->work.at(s);
        if (x.v.b.len == n && !memcmp(st_->work.bytes(x.v.b.off), st_->text_bytes, n)) { undo_drop_last(); return; }
        if (!st_->work.set_string(s, st_->text_bytes, n)) { undo_drop_last(); message("Could not set text"); return; }
        end_edit();
    }
    void view_text(View *v) const
    {
        v->keyboard = true; v->kb_sel = (uint8_t)kb_sel_; v->kb_shift = kb_shift_; v->kb_symbols = kb_symbols_; v->kb_len = (uint8_t)kb_len_;
        memcpy(v->kb_text, kb_, sizeof(uint16_t) * kb_len_);
        if (text_mode_ == TextMode::Search) snprintf(v->rows[0].label, sizeof v->rows[0].label, "Search keys and values");
        else if (text_mode_ == TextMode::Value && text_parts_) {
            if (text_part_ >= text_parts_) snprintf(v->rows[0].label, sizeof v->rows[0].label, "Append text (after %u)", text_total_);
            else snprintf(v->rows[0].label, sizeof v->rows[0].label, "Edit text part %u/%u", text_part_ + 1, text_parts_);
        }
        else snprintf(v->rows[0].label, sizeof v->rows[0].label, "%s", text_mode_ == TextMode::Value ? "Edit text" : "Tag name");
        v->rows[0].role = R_HEADING; v->count = 1;
    }

    // ---- confirm ---------------------------------------------------------------------------
    void open_confirm_leave()
    {
        confirm_n_ = 0; confirm_[confirm_n_++] = C_APPLY_LEAVE; confirm_[confirm_n_++] = C_DISCARD_LEAVE; confirm_[confirm_n_++] = C_STAY;
        confirm_s_ = Scroll{0, 0}; confirm_title_ = "Changes not applied yet"; screen_ = Screen::Confirm;
    }
    void confirm_row(unsigned i, ViewRow *r) const
    {
        r->role = R_TARGET; r->enabled = true;
        static const char *const t[] = {"Apply, then close", "Discard changes and close", "Keep editing", "Discard and reload", "Yes", "No"};
        snprintf(r->label, sizeof r->label, "%s", t[confirm_[i]]);
        if (confirm_[i] == C_APPLY_LEAVE) snprintf(r->value, sizeof r->value, "%u changes", edits_);
    }
    void confirm_key(Key k, bool repeat)
    {
        switch (k) {
        case Key::Up: confirm_s_.move(-1, confirm_n_, repeat); break;
        case Key::Down: confirm_s_.move(1, confirm_n_, repeat); break;
        case Key::Circle: if (!repeat) screen_ = Screen::Tree; break;
        case Key::Cross:
            if (repeat) break;
            switch (confirm_[confirm_sel_]) {
            case C_APPLY_LEAVE: screen_ = Screen::Tree; close_after_apply_ = true; apply(); break;
            case C_DISCARD_LEAVE: close_doc(); break;
            case C_DISCARD_REFRESH: screen_ = Screen::Tree; request_get(); break;
            default: screen_ = Screen::Tree; break;
            }
            break;
        default: break;
        }
    }
    bool close_after_apply_ = false;

    // ---- apply / revert --------------------------------------------------------------------
public:
    void apply()
    {
        if (!edits_) { message("No changes to apply"); return; }
        Script s{st_->script, sizeof st_->script, 0, 0, true, {0}};
        st_->script[0] = 0;
        if (kind_ == K_ITEM || kind_ == K_ENDER) s.line("/lce-nbt tx %s %08x", target_, crc_);
        else s.line("/lce-nbt tx %s", target_);
        const unsigned before = s.lines;
        if (!diff_script(st_->orig, st_->work, target_, s)) { message(s.error); return; }
        if (s.lines == before) { message("No differences from the server copy"); edits_ = 0; return; }
        s.line("/lce-nbt commit");
        if (!s.ok) { message(s.error); return; }
        outbox_len_ = s.len; refreshing_ = false;
        begin_request(Expect::Nbt);
        char m[48]; snprintf(m, sizeof m, "Applying %u edits...", s.lines - 2); message(m);
    }
private:
    void revert()
    {
        if (!edits_) return;
        push_undo();
        const size_t n = st_->orig.serialize(st_->scratch, kMaxDocBytes);
        if (!n || !st_->work.parse(st_->scratch, n)) { message("Revert failed"); return; }
        edits_ = 0; clamp_sel(); message("Reverted to the server copy");
    }

    // ---- footer hints -----------------------------------------------------------------------
    // One line under the rows (the stock second line always reads "Cross Select  Circle Back").
    // Every screen names Cross for the focused row, the other buttons that work there and what
    // Circle does (one level back). X = Cross, O = Circle, Sq = Square, Tri = Triangle, </> = Left/Right.
    const char *tree_cross() const
    {
        if (!row_count()) return "R1 add";
        if (array_view_) return node_readonly(dir_node()) ? "read-only" : "X change";
        const uint32_t s = sel_node(); if (s == kNil) return "X open";
        const Node &x = st_->work.at(s);
        if (is_container(x.type) || is_array(x.type)) return "X open";
        if (node_readonly(s)) return "read-only";
        const Hint h = hint_of(s);
        if (h.boolean) return "X toggle";
        if (x.type == T_STRING) return h.pick != Pick::None ? "X choose" : "X type";
        return "X change";
    }
    void hint(char *out, size_t cap) const
    {
        switch (screen_) {
        case Screen::Home: {
            const bool radius = !local_ && (sub_ == 2 || sub_ == 3) && home_[sub_].sel == 0;
            if (local_) snprintf(out, cap, "X open  </> Player/Item/Entity/Block  O close");
            else if (!sub_) snprintf(out, cap, "X open  </> Player/Item/Entity/Block  R1 Commands  O close");
            else snprintf(out, cap, "%s  Tri reload  </> NBT tab  R1 Commands  O close", radius ? "X radius" : "X open");
            break;
        }
        case Screen::Picker: snprintf(out, cap, "X open slot  </> page  Tri reload  O back"); break;
        case Screen::Tree: {
            const char *back = !st_->work.valid() ? "O back" : dir_.n ? "O up" : edits_ ? "O close (asks)" : "O close";
            if (!st_->work.valid()) snprintf(out, cap, "%s", back);
            else if (edits_) snprintf(out, cap, "%s  Sq apply %u  L1 undo  R1 menu  %s", tree_cross(), edits_, back);
            else snprintf(out, cap, "%s  R1 menu  R2 find  %s%s", tree_cross(), back, live_ ? "  (live)" : "");
            break;
        }
        case Screen::Number:
            if (num_digits_) { snprintf(out, cap, "Up/Down digit  </> move  X keep  O back"); break; }
            if (num_rows_[num_row_] == N_VALUE || num_rows_[num_row_] == N_DONE) snprintf(out, cap, "</> -/+  L1/R1 step  X keep  Sq keep+apply  O cancel");
            else snprintf(out, cap, "X select  </> -/+  L1/R1 step  Sq keep+apply  O cancel");
            break;
        case Screen::Choice: snprintf(out, cap, "%u/%u  X choose  O back  </> page  L1/R1 50", choice_sel_ + 1, choice_n_); break;
        case Screen::Templates: snprintf(out, cap, "X add this  </> page  O back"); break;
        case Screen::Actions: snprintf(out, cap, "X do it  </> page  O back"); break;
        case Screen::Confirm: snprintf(out, cap, "X choose  O keep editing"); break;
        case Screen::Text: snprintf(out, cap, "Sq delete  Tri presets  L1 clear  R1 symbols  O cancel"); break;
        }
    }
};

}  // namespace lce_nbt_ed
