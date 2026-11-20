// Debug menu: "Commands" and "WorldEdit" virtual tabs (controller palette for the server's
// in-game commands, see server LceCommands.cpp). Pure logic: ps4-transport/cmd_menu.{hpp,cpp}.
//
// Attachment (all through ld --wrap, the prebuilt menu objects are unchanged):
//  * tab_modal::Navigation::begin       records the stock frame's active tab / keyboard state.
//  * tab_modal::Navigation::input       (already wrapped in remote_input.cpp, which calls
//    lce_cmd_overlay_input first): R1 on the NBT tab opens the overlay; while it is shown it
//    receives every key except "close", and returns InputResult::changed so DirectRuntime
//    presents a new frame.
//  * menu_render::NuklearPresenter::start  hands the presenter a proxy DirectLease: the stock
//    frame is drawn as usual, and right before the lease ends the overlay is painted on top
//    with the same DirectSink (same target, same frame, same lease).
//  * tab_modal::Navigation::invalidate  (menu closed) hides the overlay.
//  * lce_remote_inbound_frame           consumes MC|LceCmd reply frames (worker thread) into a
//    mailbox; everything else goes to the NBT bridge (remote_nbt.cpp) as before.
//  * lce_remote_pump                    (owner tick) drains the mailbox: palette state + toast.
// On a server with debug access (mc195_remote_actions_active) lines go to the server. In a local
// world the Commands tab maps what it can onto the menu's own native actions (local_cmd.hpp,
// submitted through the Navigation's tab_actions::Dispatcher); WorldEdit and the rest say that
// they need an LCE server.
// Tab strip: the stock presenter draws four tabs (Misc/World/Tools/NBT). The proxy lease below
// also repaints that strip with all six tabs, so Commands and WorldEdit are always visible.
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../../r72-session-candidate/tab-menu/modal-binding/navigation.hpp"
#include "../../r72-session-candidate/menu-render-bridge/nuklear_presenter.hpp"
#include "../../r72-session-candidate/tab-menu/modal-binding/vanilla_catalog_data.hpp"
#include "../../r72-session-candidate/debug-entity-actions/mob-catalog-next/mob_catalog.hpp"
#include "cmd_menu.hpp"
#include "local_cmd.hpp"

extern "C" bool mc195_remote_actions_active();
extern "C" bool mc195_remote_send_text(const char *line);
extern "C" void mc195_remote_log(const char *);
extern "C" void lce_mods_notify(const char *message);
// chat_overlay.cpp (in-game chat + server F3)
extern "C" bool lce_chat_on_reply(const char *op, bool ok, const char *msg, const char *data);
extern "C" void lce_chat_open_keyboard(int pad, const char *prefill);
extern "C" void lce_chat_toggle(int pad, const char *what);
extern "C" unsigned lce_chat_history(const char **out, unsigned max);
extern "C" bool lce_chat_in_world(void);
// remote_actions.cpp (Dispatcher hooks; off a server they run the native body unchanged)
extern "C" unsigned char lce_hook_submit(void *self, unsigned action, uint32_t epoch);
extern "C" unsigned char lce_hook_submit_item_quantity(void *self, uint32_t item, uint32_t quantity, uint32_t epoch);
uint32_t lce_nav_current_epoch(const void *nav) __asm__("_ZNK9tab_modal10Navigation13current_epochEv");

namespace {
// tab_modal::Navigation layout (prebuilt tabs_29.o, checked by build-crashlog.sh): actions_ at +0x3210.
constexpr unsigned kNavDispatcher = 0x3210;
using lce_cmdmenu::Menu;
using lce_cmdmenu::NamedId;

// ---- catalogues from the plugin's tables
NamedId g_items[sizeof(vanilla_lists::data::items) / sizeof(vanilla_lists::data::items[0])];
NamedId g_blocks[sizeof(vanilla_lists::data::items) / sizeof(vanilla_lists::data::items[0]) + 1];
NamedId g_mobs[sizeof(entity_actions_mobs::types) / sizeof(entity_actions_mobs::types[0])];
NamedId g_ench[sizeof(vanilla_lists::data::enchantments) / sizeof(vanilla_lists::data::enchantments[0])];
// PS4 mob ids the server can spawn (LceDebugCommand.h planMobSpawn).
bool server_mob(uint32_t id)
{
    switch (id) {
    case 4: case 5: case 27: case 28: case 29: case 31: case 32: case 50: case 51: case 52: case 53: case 54: case 55: case 56:
    case 57: case 58: case 59: case 60: case 61: case 62: case 65: case 66: case 67: case 68: case 90: case 91: case 92: case 93:
    case 94: case 95: case 96: case 97: case 98: case 99: case 100: case 101: case 120: return true;
    default: return false;
    }
}

Menu g_menu;
bool g_ready = false, g_active = false;
unsigned g_last_tab = 0;
bool g_last_keyboard = false;
unsigned g_lock;
void lock() { while (__atomic_exchange_n(&g_lock, 1u, __ATOMIC_ACQUIRE)) {} }
void unlock() { __atomic_store_n(&g_lock, 0u, __ATOMIC_RELEASE); }
struct Guard { Guard() { lock(); } ~Guard() { unlock(); } };

void ensure_ready()
{
    if (g_ready) return;
    lce_cmdmenu::Catalogs c;
    unsigned ni = 0, nb = 0, nm = 0, ne = 0;
    g_blocks[nb++] = NamedId{0, "air"};
    for (const auto &e : vanilla_lists::data::items) {
        g_items[ni++] = NamedId{e.id, e.name};
        if (e.id < 256) g_blocks[nb++] = NamedId{e.id, e.name};
    }
    for (const auto &t : entity_actions_mobs::types) if (server_mob(t.numeric_id)) g_mobs[nm++] = NamedId{t.numeric_id, t.name};
    for (const auto &e : vanilla_lists::data::enchantments) g_ench[ne++] = NamedId{e.id, e.name};
    c.items = g_items; c.item_count = ni; c.blocks = g_blocks; c.block_count = nb; c.mobs = g_mobs; c.mob_count = nm;
    c.effects = lce_cmdmenu::kEffects; c.effect_count = lce_cmdmenu::kEffectCount; c.enchants = g_ench; c.enchant_count = ne;
    g_menu.init(c);
    g_ready = true;
}

int g_pad = 0;
void *g_nav = nullptr;   // Navigation of the current input call (owner thread)
bool g_remote = false;   // last input: connected to a server with debug access
// Local world: run a palette line through the menu's native actions.
void local_send(const char *line)
{
    using lce_local_cmd::Kind;
    const lce_local_cmd::Mapped m = lce_local_cmd::map(line);
    if (m.kind == Kind::query) return;
    char msg[200];
    if (m.kind == Kind::none) {
        snprintf(msg, sizeof msg, "%s needs an LCE server (local worlds run Game mode S/C, Time, Weather, Difficulty, Game rules, Heal, Feed, "
                 "XP +/-1 and Give here).", !strncmp(line, "/lce-cmd //", 11) ? "WorldEdit" : "This command");
        g_menu.apply_reply("cmd", false, msg, "");
        return;
    }
    unsigned char *disp = g_nav ? static_cast<unsigned char *>(g_nav) + kNavDispatcher : nullptr;
    const uint32_t epoch = g_nav ? lce_nav_current_epoch(g_nav) : 0;
    unsigned char r = 1;
    if (disp && epoch) {
        r = m.kind == Kind::give ? lce_hook_submit_item_quantity(disp, m.a, m.b, epoch) : lce_hook_submit(disp, m.action, epoch);
    }
    if (r == 0) snprintf(msg, sizeof msg, "Local world: done%s%s.", m.note[0] ? " - " : "", m.note);
    else if (r == 2) snprintf(msg, sizeof msg, "Local world: the previous action is still running, try again.");
    else snprintf(msg, sizeof msg, "Local world: the game refused this action right now (no world, or not allowed here).");
    g_menu.apply_reply("cmd", r == 0, msg, "");
    mc195_remote_log(r == 0 ? "Commands overlay: local action submitted.\n" : "Commands overlay: local action refused.\n");
}
void do_output(const lce_cmdmenu::Output &o)
{
    if (o.kind == lce_cmdmenu::Output::Exit) { g_active = false; return; }
    // chat_overlay.cpp: on-screen keyboard and toggles (chat log pin, F3)
    if (o.kind == lce_cmdmenu::Output::Keyboard) { lce_chat_open_keyboard(g_pad, o.line); return; }
    if (o.kind == lce_cmdmenu::Output::Toggle) { lce_chat_toggle(g_pad, o.line); return; }
    if (o.kind != lce_cmdmenu::Output::Send) return;
    if (!g_remote) { local_send(o.line); return; }
    if (!lce_chat_in_world() || !mc195_remote_send_text(o.line))
        g_menu.apply_reply("cmd", false, "Not connected to a server with debug access.", "");
}

// ---- MC|LceCmd inbound mailbox (worker thread -> owner thread)
struct Msg { uint16_t n; uint8_t b[1400]; };
Msg g_ring[16];
unsigned g_head, g_tail, g_ring_lock;
void ring_lock() { while (__atomic_exchange_n(&g_ring_lock, 1u, __ATOMIC_ACQUIRE)) {} }
void ring_unlock() { __atomic_store_n(&g_ring_lock, 0u, __ATOMIC_RELEASE); }

unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
bool read_utf(const uint8_t *p, size_t n, size_t *pos, char *out, size_t cap)
{
    if (*pos + 2 > n) return false;
    const size_t len = be16(p + *pos); *pos += 2;
    if (*pos + len > n) return false;
    size_t k = 0;
    for (size_t i = 0; i < len; ++i) { const uint8_t ch = p[*pos + i]; if (k + 1 < cap) out[k++] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?'; }
    out[k] = 0; *pos += len;
    return true;
}

// ---- drawing
using DC = menu_render::DirectColor;
struct Painter {
    const menu_render::DirectSink &s;
    int gw, gh;
    bool rect(int a, int b, int c, int d, DC col) { return s.rect(s.context, a, b, c, d, col); }
    bool text(int a, int b, int c, const char *v, DC col)
    {
        if (c <= 0) return true;
        size_t n = strlen(v), max = (size_t)c / (unsigned)gw;
        if (n > max) n = max;
        return !n || s.text(s.context, a, b, c, gh, v, n, col);
    }
};
// Wraps text into at most `lines` lines of width w.
bool wrapped(Painter &p, int x, int y, int w, const char *t, DC col, int lines, int step = 18)
{
    const size_t per = (size_t)(w / p.gw);
    if (!per) return true;
    bool ok = true;
    while (*t && lines-- > 0 && ok) {
        size_t n = strlen(t);
        if (n > per) { n = per; while (n > per / 2 && t[n] != ' ') --n; if (t[n] != ' ') n = per; }
        char line[160];
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, t, n); line[n] = 0;
        ok = p.text(x, y, w, line, col);
        t += n; while (*t == ' ') ++t;
        y += step;
    }
    return ok;
}

bool paint(const menu_render::DirectSink &s, int x, int y, int w, int h, uint8_t gw, uint8_t gh)
{
    if (!gw || !gh || w < 480 || h < 300 || !s.clip || !s.rect || !s.text) return false;
    Painter p{s, gw, gh};
    const DC ink{35, 35, 35, 255}, white{245, 245, 245, 255}, muted{92, 92, 92, 255}, border{30, 30, 30, 255};
    const DC yellow{255, 255, 95, 255}, orange{143, 71, 15, 255}, sel{119, 129, 177, 255}, rowc{125, 125, 125, 255}, red{150, 30, 30, 255};
    const int pw = w - 40 < 680 ? w - 40 : 680, ph = h - 40 < 384 ? h - 40 : 384;
    const int px = x + (w - pw) / 2, py = y + (h - ph) / 2;
    if (!s.clip(s.context, x, y, w, h) || !p.rect(px + 5, py + 5, pw, ph, {0, 0, 0, 145}) || !p.rect(px, py, pw, ph, border) ||
        !p.rect(px + 3, py + 3, pw - 6, ph - 6, {204, 204, 204, 255}) ||
        !p.text(px + 16, py + 12, pw - 32, g_remote ? "Minecraft debug - server commands" : "Minecraft debug - commands (local world)", ink)) return false;
    // tab strip: the four stock tabs (L1 goes back to NBT) + ours
    static const char *const tabs[] = {"Misc", "World", "Tools", "NBT", "Commands", "WorldEdit"};
    const unsigned active = 4 + (unsigned)g_menu.page();
    const int tw = (pw - 28) / 6;
    for (unsigned i = 0; i < 6; ++i) {
        const int tx = px + 14 + (int)i * tw;
        if (!p.rect(tx, py + 32, tw - 3, 26, border) ||
            !p.rect(tx + 1, py + 33, tw - 5, 24, i == active ? DC{128, 137, 186, 255} : i < 4 ? DC{170, 170, 170, 255} : DC{157, 157, 157, 255}) ||
            !p.text(tx + 8, py + 40, tw - 16, tabs[i], i == active ? yellow : white)) return false;
    }
    const int top = py + 66, bodyh = ph - 66 - 52;
    const int listx = px + 14, listw = (pw - 40) * 55 / 100, detx = listx + listw + 12, detw = pw - 28 - listw - 12;
    if (!p.rect(listx, top, listw, bodyh, {69, 69, 69, 255}) || !p.rect(detx, top, detw, bodyh, {224, 224, 224, 255})) return false;
    const int rowh = 24, dx = detx + 10, dw = detw - 20;
    auto row = [&](unsigned r, bool selected, const char *a, const char *b, bool dim) {
        const int ry = top + 4 + (int)r * (rowh + 2);
        if (ry + rowh > top + bodyh) return true;
        const int bw = b && *b ? (int)strlen(b) * gw : 0;
        const int aw = listw - 24 - (bw ? bw + 10 : 0);
        return p.rect(listx + 4, ry, listw - 8, rowh, selected ? sel : rowc) &&
               p.text(listx + 10, ry + (rowh - gh) / 2, aw, a, selected ? yellow : dim ? DC{200, 200, 200, 255} : white) &&
               (!bw || p.text(listx + listw - 14 - bw, ry + (rowh - gh) / 2, bw, b, selected ? yellow : white));
    };
    const auto &st = g_menu.state();
    const lce_cmdmenu::CmdSpec &cur = g_menu.current();
    const char *hint = "";
    bool ok = true;
    char a[64], b[64];
    switch (g_menu.screen()) {
    case lce_cmdmenu::Screen::List: {
        unsigned n; const lce_cmdmenu::CmdSpec *cs = lce_cmdmenu::page_commands(g_menu.page(), &n);
        for (unsigned r = 0; ok && r < lce_cmdmenu::kListRows && g_menu.list_scroll() + r < n; ++r) {
            const unsigned i = g_menu.list_scroll() + r;
            ok = row(r, i == g_menu.list_cursor(), cs[i].label, cs[i].arg_count ? ">" : "", false);
        }
        if (ok && n > lce_cmdmenu::kListRows) {
            const int thumb = bodyh * (int)lce_cmdmenu::kListRows / (int)n;
            const int off = (bodyh - thumb) * (int)g_menu.list_scroll() / (int)(n - lce_cmdmenu::kListRows);
            ok = p.rect(listx + listw - 6, top + 2, 3, bodyh - 4, {47, 47, 47, 255}) && p.rect(listx + listw - 6, top + off, 3, thumb, {219, 219, 219, 255});
        }
        hint = g_menu.page() == lce_cmdmenu::Page::Commands ? "Cross: choose   Left/Right: page   L1: NBT tab   R1: WorldEdit   Circle: back"
                                                           : "Cross: choose   Left/Right: page   L1: Commands   Circle: back";
        break;
    }
    case lce_cmdmenu::Screen::Form: {
        const unsigned rows = g_menu.form_rows();
        const unsigned first = g_menu.form_cursor() >= lce_cmdmenu::kListRows ? g_menu.form_cursor() - lce_cmdmenu::kListRows + 1 : 0;
        for (unsigned r = 0; ok && r < lce_cmdmenu::kListRows && first + r < rows; ++r) {
            g_menu.form_row(first + r, a, sizeof a, b, sizeof b);
            ok = row(r, first + r == g_menu.form_cursor(), a, b, false);
        }
        hint = g_menu.form_hint();
        break;
    }
    case lce_cmdmenu::Screen::Picker: {
        const unsigned n = g_menu.picker_count();
        for (unsigned r = 0; ok && r < lce_cmdmenu::kPickerRows && g_menu.picker_scroll() + r < n; ++r) {
            g_menu.picker_label(g_menu.picker_scroll() + r, a, sizeof a);
            ok = row(r, g_menu.picker_scroll() + r == g_menu.picker_cursor(), a, nullptr, false);
        }
        snprintf(b, sizeof b, "%u / %u", g_menu.picker_cursor() + 1, n);
        ok = ok && p.text(dx, top + 150, dw, b, muted);
        hint = "Cross: pick   Up/Down: 1   Left/Right: 10   L1/R1: 100   Circle: back (no change)";
        break;
    }
    case lce_cmdmenu::Screen::Confirm: {
        char t[200];
        g_menu.confirm_text(t, sizeof t);
        ok = p.rect(listx + 4, top + 4, listw - 8, 120, border) && p.rect(listx + 6, top + 6, listw - 12, 116, {236, 236, 236, 255}) &&
             p.text(listx + 14, top + 14, listw - 28, "Confirm", orange) && wrapped(p, listx + 14, top + 38, listw - 28, t, ink, 4) &&
             p.text(listx + 14, top + 104, listw - 28, "Cross: yes, run it   Circle: no", ink);
        hint = "Cross: yes, run it   Circle: no, go back";
        break;
    }
    }
    // detail panel: command title + help, WorldEdit state, last result
    int ty = top + 10;
    ok = ok && p.text(dx, ty, dw, g_menu.screen() == lce_cmdmenu::Screen::Picker ? g_menu.picker_title() : cur.label, ink);
    ty += 22;
    if (g_menu.screen() != lce_cmdmenu::Screen::Picker) { ok = ok && wrapped(p, dx, ty, dw, cur.help ? cur.help : "", muted, 4); ty += 74; }
    if (g_menu.page() == lce_cmdmenu::Page::WorldEdit && g_menu.screen() != lce_cmdmenu::Screen::Picker) {
        char t[96];
        snprintf(t, sizeof t, "Pos1 %s", st.have_sel && st.p1[0] ? st.p1 : "-"); ok = ok && p.text(dx, ty, dw, t, ink); ty += 18;
        snprintf(t, sizeof t, "Pos2 %s", st.have_sel && st.p2[0] ? st.p2 : "-"); ok = ok && p.text(dx, ty, dw, t, ink); ty += 18;
        if (st.volume > 0) snprintf(t, sizeof t, "Size %s = %lld", st.size, st.volume); else snprintf(t, sizeof t, "Size -");
        ok = ok && p.text(dx, ty, dw, t, ink); ty += 18;
        snprintf(t, sizeof t, "Clipboard %s  Undo %u  Redo %u", st.clip[0] ? st.clip : "-", st.undo, st.redo);
        ok = ok && p.text(dx, ty, dw, t, muted); ty += 18;
        if (st.job[0] && strcmp(st.job, "-")) { snprintf(t, sizeof t, "Running edit: %s", st.job); ok = ok && p.text(dx, ty, dw, t, orange); }
        ty += 20;
    }
    if (st.last_message[0]) {
        const int my = top + bodyh - 64;
        ok = ok && p.rect(detx + 4, my - 4, detw - 8, 62, st.last_ok ? DC{200, 214, 200, 255} : DC{226, 196, 190, 255}) &&
             wrapped(p, dx, my, dw, st.last_message, st.last_ok ? ink : red, 3);
    }
    (void)ty;
    return ok && p.text(px + 16, py + ph - 44, pw - 32, "Results also appear as notifications. Info refreshes on the next button press.", muted) &&
           p.text(px + 16, py + ph - 24, pw - 32, hint, ink);
}

// ---- six-tab strip over the stock four-tab strip
// The stock NuklearPresenter (r72 menu-render-bridge/nuklear_presenter.cpp, prebuilt) lays the panel
// out from the frame; the same arithmetic gives its tab strip (top + 2, 22 high, full panel width).
struct Strip { bool valid; int x, y, w, h; uint8_t gw, gh, rows, active; bool nbt, keyboard; } g_strip;
bool paint_strip(const menu_render::DirectSink &s, const Strip &f)
{
    if (!f.valid || f.keyboard || !s.clip || !s.rect || !s.text || f.w < 320 || f.h < 200 || !f.gw || f.gw > 16 || !f.gh || f.gh > 12 || f.active >= 4) return true;
    const int panel_limit = f.nbt ? 680 : 520;
    const int width = f.w - 32 < panel_limit ? f.w - 32 : panel_limit;
    const int available = f.h - 32, fixed = f.nbt ? 80 : 56;
    const int row_height = f.rows && (available - fixed) / (int)f.rows < 20 ? (available - fixed) / (int)f.rows : 20;
    if (row_height < 14) return true;
    const int panel_height = fixed + (int)f.rows * row_height;
    if (panel_height > available) return true;
    const int left = f.x + (f.w - width) / 2, top = f.y + (f.h - panel_height) / 2;
    static const char *const names[] = {"Misc", "World", "Tools", "NBT", "Commands", "WorldEdit"};
    static const char *const mid[] = {"Misc", "World", "Tools", "NBT", "Cmds", "WEdit"};
    static const char *const tiny[] = {"Ms", "Wd", "Tl", "NB", "Cm", "WE"};
    Painter p{s, f.gw, f.gh};
    if (!s.clip(s.context, left, top, width, panel_height) || !p.rect(left + 2, top + 2, width - 4, 22, {205, 205, 205, 255})) return false;
    for (int i = 0; i < 6; ++i) {
        const int x = left + 2 + i * (width - 4) / 6, tw = left + 2 + (i + 1) * (width - 4) / 6 - x;
        const int cap = (tw - 10) / f.gw;
        const char *label = (int)strlen(names[i]) <= cap ? names[i] : (int)strlen(mid[i]) <= cap ? mid[i] : tiny[i];
        const bool on = i == (int)f.active;
        const DC face = on ? DC{205, 205, 205, 255} : i < 4 ? DC{145, 145, 145, 255} : DC{120, 128, 160, 255};
        if (!p.rect(x, top + 2, tw - 2, 22, {40, 40, 40, 255}) || !p.rect(x + 1, top + 3, tw - 4, 20, face) ||
            !p.text(x + 5, top + 7, tw - 10, label, on ? DC{30, 30, 30, 255} : DC{255, 255, 255, 255})) return false;
    }
    return true;
}

// ---- proxy lease for NuklearPresenter::start
struct Proxy {
    menu_render::DirectLease orig;
    menu_render::DirectSink sink;
    bool have;
    int x, y, w, h;
    uint8_t gw, gh;
} g_proxy;
bool proxy_begin(void *c, menu_render::DirectSink *s)
{
    auto *p = static_cast<Proxy *>(c);
    if (!p->orig.begin(p->orig.context, s)) return false;
    p->sink = *s; p->have = true;
    return true;
}
bool proxy_current(void *c) { auto *p = static_cast<Proxy *>(c); return p->orig.current(p->orig.context); }
bool proxy_end(void *c)
{
    auto *p = static_cast<Proxy *>(c);
    if (p->have && p->orig.current(p->orig.context)) {
        Guard g;
        if (g_active) {
            if (!paint(p->sink, p->x, p->y, p->w, p->h, p->gw, p->gh)) {
                // Do not leave an invisible modal intercepting game input.
                g_active = false;
                mc195_remote_log("Commands overlay: paint failed; closed.\n");
            }
        } else if (!paint_strip(p->sink, g_strip)) {
            mc195_remote_log("Commands overlay: tab strip paint failed.\n");
        }
    }
    p->have = false;
    return p->orig.end(p->orig.context);
}
}  // namespace

// remote_input.cpp: while the overlay is shown it gets every key before the NBT editor.
extern "C" __attribute__((visibility("hidden"))) bool lce_cmd_overlay_shown(void) { return g_active; }
// nbt_editor.cpp: replaces the NBT tab's rows when the server editor is active (Navigation::begin
// can be --wrap'ed only once, so the editor hooks in here).
extern "C" void lce_nbt_editor_frame(void *navigation, tab_modal::Frame *frame, bool overlay_shown);

// Called first by remote_input.cpp's Navigation::input wrapper. true = consumed, *result set.
extern "C" __attribute__((visibility("hidden"))) bool lce_cmd_overlay_input(void *nav, int32_t pad, unsigned key, bool pressed, bool repeat, unsigned char *result)
{
    if (!pressed || key > (unsigned)lce_cmdmenu::Key::close) return false;
    const bool remote = mc195_remote_actions_active();
    if (g_active && remote != g_remote) g_active = false;   // joined / left a server under the overlay
    Guard g;
    g_nav = nav; g_remote = remote;
    ensure_ready();
    g_pad = pad >= 0 && pad < 4 ? pad : 0;
    { const char *h[16]; const unsigned n = lce_chat_history(h, 16); g_menu.set_history(h, n); }
    const auto k = static_cast<lce_cmdmenu::Key>(key);
    if (!g_active) {
        if (k != lce_cmdmenu::Key::next_tab || repeat || g_last_tab != 3 || g_last_keyboard) return false;
        g_active = true;
        g_menu.reset();
        do_output(g_menu.enter(lce_cmdmenu::Page::Commands));
        *result = (unsigned char)tab_modal::InputResult::changed;
        return true;
    }
    if (k == lce_cmdmenu::Key::close) { g_active = false; return false; }   // the stock menu closes
    do_output(g_menu.input(k, repeat));
    *result = (unsigned char)tab_modal::InputResult::changed;
    return true;
}

bool real_nav_begin(void *, int, int, int, int, uint8_t, uint8_t, tab_modal::Frame *) __asm__("__real__ZN9tab_modal10Navigation5beginEiiiihhPNS_5FrameE");
bool wrap_nav_begin(void *, int, int, int, int, uint8_t, uint8_t, tab_modal::Frame *) __asm__("__wrap__ZN9tab_modal10Navigation5beginEiiiihhPNS_5FrameE") __attribute__((visibility("hidden")));
bool wrap_nav_begin(void *self, int x, int y, int w, int h, uint8_t gw, uint8_t gh, tab_modal::Frame *out)
{
    const bool r = real_nav_begin(self, x, y, w, h, gw, gh, out);
    if (r && out) lce_nbt_editor_frame(self, out, g_active);   // NBT editor first: its keyboard state counts below
    if (r && out) {
        g_last_tab = out->view.active_tab; g_last_keyboard = out->view.keyboard;
        const auto &v = out->view;
        g_strip = Strip{true, v.x, v.y, v.width, v.height, v.max_glyph_width, v.max_glyph_height, v.row_count, v.active_tab, v.nbt, v.keyboard};
    } else {
        g_strip.valid = false;
    }
    return r;
}

void real_nav_invalidate(void *) __asm__("__real__ZN9tab_modal10Navigation10invalidateEv");
void wrap_nav_invalidate(void *) __asm__("__wrap__ZN9tab_modal10Navigation10invalidateEv") __attribute__((visibility("hidden")));
extern "C" void lce_nbt_editor_invalidate(void *navigation);
void wrap_nav_invalidate(void *self)
{
    g_active = false;
    lce_nbt_editor_invalidate(self);
    real_nav_invalidate(self);
}

bool real_presenter_start(void *, tab_modal::Navigation &, const menu_render::DirectLease &, int, int, int, int, uint8_t, uint8_t, menu_render::DirectFrameId *)
    __asm__("__real__ZN11menu_render16NuklearPresenter5startERN9tab_modal10NavigationERKNS_11DirectLeaseEiiiihhPNS_13DirectFrameIdE");
bool wrap_presenter_start(void *, tab_modal::Navigation &, const menu_render::DirectLease &, int, int, int, int, uint8_t, uint8_t, menu_render::DirectFrameId *)
    __asm__("__wrap__ZN11menu_render16NuklearPresenter5startERN9tab_modal10NavigationERKNS_11DirectLeaseEiiiihhPNS_13DirectFrameIdE") __attribute__((visibility("hidden")));
bool wrap_presenter_start(void *self, tab_modal::Navigation &nav, const menu_render::DirectLease &lease, int x, int y, int w, int h, uint8_t gw, uint8_t gh,
                          menu_render::DirectFrameId *out)
{
    if (!lease.begin || !lease.current || !lease.end) return real_presenter_start(self, nav, lease, x, y, w, h, gw, gh, out);
    g_proxy.orig = lease; g_proxy.have = false;
    g_proxy.x = x; g_proxy.y = y; g_proxy.w = w; g_proxy.h = h; g_proxy.gw = gw; g_proxy.gh = gh;
    const menu_render::DirectLease proxy{&g_proxy, proxy_begin, proxy_current, proxy_end};
    return real_presenter_start(self, nav, proxy, x, y, w, h, gw, gh, out);
}

extern "C" int __real_lce_remote_inbound_frame(const uint8_t *, size_t);
extern "C" void lce_chat_inbound_frame(const uint8_t *f, size_t n);
extern "C" bool lce_chat_in_world(void);
extern "C" __attribute__((visibility("hidden"))) int __wrap_lce_remote_inbound_frame(const uint8_t *f, size_t n)
{
    lce_chat_inbound_frame(f, n);   // chat_overlay.cpp: in-world detection (nothing is sent before Login)
    static const char kName[] = "MC|LceCmd";
    const size_t chars = sizeof kName - 1;
    if (f && n >= 3 + chars * 2 + 2 && f[0] == 250 && be16(f + 1) == chars) {
        bool match = true;
        for (size_t i = 0; i < chars && match; ++i) match = f[3 + 2 * i] == 0 && f[4 + 2 * i] == (uint8_t)kName[i];
        const size_t at = 3 + chars * 2;
        if (match && n == at + 2 + be16(f + at)) {
            const size_t len = be16(f + at);
            ring_lock();
            if (len <= sizeof g_ring[0].b && g_head - g_tail < 16) {
                Msg &m = g_ring[g_head % 16];
                memcpy(m.b, f + at + 2, len); m.n = (uint16_t)len; ++g_head;
            }
            ring_unlock();
            return 1;   // never reaches the retail client
        }
    }
    return __real_lce_remote_inbound_frame(f, n);
}

extern "C" void __real_lce_remote_pump(void);
// Drains MC|LceCmd replies on the owner thread (also called from chat_overlay.cpp's owner tick,
// so players without debug access get their chat log): the chat module takes chat / f3 /
// mirror and copies results into the chat log; the palette gets the rest (and toasts results).
extern "C" __attribute__((visibility("hidden"))) void lce_cmd_drain(void)
{
    for (int guard = 0; guard < 16; ++guard) {
        Msg m;
        ring_lock();
        const bool have = g_tail != g_head;
        if (have) { m = g_ring[g_tail % 16]; ++g_tail; }
        ring_unlock();
        if (!have) break;
        char op[16], msg[201], data[1001];
        size_t pos = 0;
        if (!read_utf(m.b, m.n, &pos, op, sizeof op) || pos >= m.n) continue;
        const bool ok = m.b[pos++] == 1;
        if (!read_utf(m.b, m.n, &pos, msg, sizeof msg) || !read_utf(m.b, m.n, &pos, data, sizeof data)) continue;
        if (lce_chat_on_reply(op, ok, msg, data)) continue;
        bool toast;
        {
            Guard g;
            ensure_ready();
            toast = g_menu.apply_reply(op, ok, msg, data);
        }
        if (toast) lce_mods_notify(msg);
    }
}

extern "C" __attribute__((visibility("hidden"))) void __wrap_lce_remote_pump(void)
{
    __real_lce_remote_pump();
    lce_cmd_drain();
}
