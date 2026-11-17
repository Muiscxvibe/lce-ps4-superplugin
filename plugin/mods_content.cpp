// Mods menu: "Download" section (content mirror browser/installer).
//
// The retail Mods screen is the owner-thread frontend in tabs_modmenu_*.o (four
// sections: Mods, Skin Packs, Texture Packs, Maps). This file adds a fifth,
// virtual section that sits between Maps and Mods in the L1/R1 cycle. It is
// attached with ld --wrap on mc195_mods::Model::{open,refresh,input,draw}, so
// the prebuilt frontend and its pad/frame lifecycle are unchanged; while the
// section is not shown every call falls straight through to the original.
//
//  * Categories -> list (installed / update available) -> install, update or
//    uninstall, on the plugin's worker scheduler. Nothing here blocks the
//    owner thread; it only polls a lock-free Board.
//  * Catalog signature, per-file size + SHA-256 and atomic install are done by
//    the existing engine (content_catalog / content_download).
//  * All state-machine logic lives in ps4-transport/mods_menu.{hpp,cpp}
//    (host-tested); this file is the console glue and the drawing code.
#include <fcntl.h>
#include <orbis/libkernel.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "/home/codey/minecraft-lce-work/r72-session-candidate/tab-menu/modal-binding/navigation.hpp"
#include "/home/codey/minecraft-lce-work/r72-session-candidate/menu-render-bridge/nuklear_presenter.hpp"
#include "content_catalog.h"
#include "content_download.h"
#include "mods_menu.hpp"

extern "C" void mc195_remote_log(const char *);
// Provided by debug_main: returns the account service URL (and the HTTPS
// context) once the account bootstrap has finished, else nullptr.
extern "C" const char *lce_mods_net(const lce_https_api **api, const lce_https_trust **trust);
extern "C" bool lce_mods_schedule(void (*task)(void *, uint64_t), void *ctx);
extern "C" void lce_mods_notify(const char *message);
// dyn_textures: 1 while the downloaded texture pack with this folder name is registered
// in the running game (its files must not be replaced or removed until a restart).
extern "C" int mc195_dyn_textures_in_use(const char *slug);

namespace mc195_mods { class Model; enum class Section : unsigned { mods, skins, textures, maps }; }
// The originals, reached through ld --wrap.
tab_modal::Response real_input(mc195_mods::Model *, tab_modal::Key, bool) __asm__("__real__ZN10mc195_mods5Model5inputEN9tab_modal3KeyEb");
bool real_open(mc195_mods::Model *) __asm__("__real__ZN10mc195_mods5Model4openEv");
bool real_refresh(mc195_mods::Model *) __asm__("__real__ZN10mc195_mods5Model7refreshEv");
bool real_draw(mc195_mods::Model *, const menu_render::DirectSink &, int, int, int, int, uint8_t, uint8_t)
    __asm__("__real__ZN10mc195_mods5Model4drawERKN11menu_render10DirectSinkEiiiihh");

// The Model class is not included: the prebuilt tabs_modmenu_model.o was built
// against a service.hpp whose Snapshot is 1560 bytes larger than the one in the
// current sources, so the header's layout does not match the object. The few
// fields needed are addressed by the offsets in the prebuilt object (verified
// from its disassembly by build-crashlog.sh, which fails the build on a change):
//   +0x68 view_.section (unsigned), +0x1327c open_ (bool), +0x13288 generation_.
namespace {
constexpr unsigned kSectionOffset = 0x68, kActiveOffset = 0x1327c, kGenerationOffset = 0x13288;
using ModelPtr = mc195_mods::Model *;
inline unsigned model_section(ModelPtr m) { return *reinterpret_cast<unsigned *>(reinterpret_cast<char *>(m) + kSectionOffset); }
inline bool model_active(ModelPtr m) { return *(reinterpret_cast<char *>(m) + kActiveOffset) != 0; }
inline void bump_generation(ModelPtr m) { ++*reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(m) + kGenerationOffset); }
}

namespace {
using namespace lce_mods;

// ============================================================ worker side
Board g_board;
Menu g_menu;
lce_catalog g_cat;                                   // written by the worker during Load only
alignas(8) uint8_t g_index_buf[192 * 1024];          // index.json
ItemState g_state[kMaxItems];                        // written by the worker, read after take()
bool g_active = false;                               // virtual section shown
uint32_t g_drawn_rev = 0;

void log(const char *m) { mc195_remote_log(m); }

// ---- file operations for the engine
bool path_exists(const char *p) { OrbisKernelStat st; return sceKernelStat(p, &st) >= 0; }
int fs_mkdir_p(void *, const char *dir) {
    char tmp[400];
    size_t n = strlen(dir);
    if (!n || n >= sizeof tmp) return -1;
    memcpy(tmp, dir, n + 1);
    for (size_t i = 1; i < n; ++i)
        if (tmp[i] == '/') { tmp[i] = 0; if (!path_exists(tmp)) (void)sceKernelMkdir(tmp, 0777); tmp[i] = '/'; }
    if (!path_exists(tmp)) (void)sceKernelMkdir(tmp, 0777);
    return path_exists(tmp) ? 0 : -1;
}
int fs_open_write(void *, const char *p) { return sceKernelOpen(p, O_WRONLY | O_CREAT | O_TRUNC, 0666); }
int fs_write(void *, int fd, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    while (n) {
        size_t w = sceKernelWrite(fd, b, n);
        if (!w || w > n) return -1;
        b += w; n -= w;
    }
    return 0;
}
int fs_close(void *, int fd) { return sceKernelClose(fd) >= 0 ? 0 : -1; }
int fs_rename(void *, const char *a, const char *b) { return sceKernelRename(a, b) >= 0 ? 0 : -1; }
int fs_unlink(void *, const char *p) { return sceKernelUnlink(p) >= 0 ? 0 : -1; }
const lce_content_fs kFs = {fs_mkdir_p, fs_open_write, fs_write, fs_close, fs_rename, fs_unlink, nullptr};

// ---- installed state
// Marker "<root>/<id>/lce.version" (written after a complete install). Without
// it an item counts as installed if its registration descriptor (skin packs) or
// all of its files are present; the version is then unknown.
ItemState probe(const CategoryInfo &ci, const lce_catalog_item &it) {
    char p[400], buf[24];
    if (version_path(p, sizeof p, ci.root, it.id)) {
        int fd = sceKernelOpen(p, O_RDONLY, 0);
        if (fd >= 0) {
            size_t n = sceKernelRead(fd, buf, sizeof buf - 1);
            (void)sceKernelClose(fd);
            if (n > sizeof buf - 1) n = 0;
            size_t e = 0;
            while (e < n && ((buf[e] >= '0' && buf[e] <= '9') || buf[e] == '.')) ++e;
            buf[e] = 0;
            return item_state(buf, it.version);
        }
    }
    if (it.dlc_id && it.skin_count) {
        snprintf(p, sizeof p, "%s/%s/%s", ci.root, it.id, kDescFile);
        return path_exists(p) ? item_state("", it.version) : ItemState::NotInstalled;
    }
    for (unsigned i = 0; i < it.file_count; ++i) {
        if (!lce_catalog_install_path(p, sizeof p, ci.root, ci.repo, &it, &it.files[i]) || !path_exists(p)) return ItemState::NotInstalled;
    }
    return item_state("", it.version);
}

int plan_remove(void *, const char *path, int is_dir) {
    // Missing files are fine; directories that still hold foreign files stay.
    if (is_dir) (void)sceKernelRmdir(path); else (void)sceKernelUnlink(path);
    return 1;
}
bool remove_item(const CategoryInfo &ci, const lce_catalog_item &it) {
    if (plan_uninstall(ci.root, ci.repo, it, plan_remove, nullptr) < 0) return false;
    return probe(ci, it) == ItemState::NotInstalled;
}

bool write_version_marker(const CategoryInfo &ci, const lce_catalog_item &it) {
    char path[400], part[410], line[24];
    if (!version_path(path, sizeof path, ci.root, it.id) || snprintf(part, sizeof part, "%s.part", path) >= (int)sizeof part) return false;
    int n = snprintf(line, sizeof line, "%s\n", it.version);
    int fd = fs_open_write(nullptr, part);
    if (fd < 0) return false;
    bool ok = fs_write(nullptr, fd, line, (size_t)n) == 0;
    ok = fs_close(nullptr, fd) == 0 && ok;
    if (!ok || fs_rename(nullptr, part, path) != 0) { (void)sceKernelUnlink(part); return false; }
    return true;
}

// ---- download with progress / cancel
struct GetCtx {
    const lce_https_api *api;
    const lce_https_trust *trust;
    uint64_t total, finished_files, file_done, last_report;
    unsigned file, files;
    int (*sink)(void *, const uint8_t *, size_t);
    void *sink_ctx;
};
int progress_sink(void *c, const uint8_t *d, size_t n) {
    auto *g = static_cast<GetCtx *>(c);
    if (g_board.cancelled() || !g->sink(g->sink_ctx, d, n)) return 0;
    g->file_done += n;
    if (g->file_done - g->last_report >= 65536) {
        g->last_report = g->file_done;
        g_board.report(g->finished_files + g->file_done, g->total, g->file, g->files);
    }
    return 1;
}
int install_getter(void *ctx, const char *url, uint64_t max, int (*sink)(void *, const uint8_t *, size_t), void *sink_ctx) {
    auto *g = static_cast<GetCtx *>(ctx);
    g->finished_files += g->file_done;
    g->file_done = g->last_report = 0;
    ++g->file;
    g->sink = sink; g->sink_ctx = sink_ctx;
    g_board.report(g->finished_files, g->total, g->file, g->files);
    return lce_https_get(g->api, g->trust, url, max, progress_sink, g, nullptr);
}

bool texture_in_use(const CategoryInfo &ci, const lce_catalog_item &it) {
    return !strcmp(ci.repo, "texture-packs") && mc195_dyn_textures_in_use(it.id) != 0;
}

void finish_msg(bool ok, unsigned count, const char *fmt, const char *a = "", const char *b = "") {
    char m[160];
    snprintf(m, sizeof m, fmt, a, b);
    g_board.finish(ok, count, m);
}

void job_load(const CategoryInfo &ci, const char *url, const lce_https_api *api, const lce_https_trust *trust) {
    char base[160];
    if (!mirror_base_from_url(url, base, sizeof base)) { g_board.finish(false, 0, "Content server address is invalid."); return; }
    if (!lce_content_fetch_catalog(api, trust, base, ci.repo, g_index_buf, sizeof g_index_buf, &g_cat) ||
        strcmp(g_cat.type, ci.catalog_type)) {
        memset(&g_cat, 0, sizeof g_cat);
        g_board.finish(false, 0, "Could not load the list. Check the content server and that the signature is valid.");
        return;
    }
    for (unsigned i = 0; i < g_cat.count && i < kMaxItems; ++i) g_state[i] = probe(ci, g_cat.items[i]);
    g_board.finish(true, (unsigned)g_cat.count, nullptr);
}

void job_install(const CategoryInfo &ci, const char *url, const lce_https_api *api, const lce_https_trust *trust) {
    const lce_catalog_item &it = g_board.item;
    char base[160];
    if (!mirror_base_from_url(url, base, sizeof base)) { g_board.finish(false, 0, "Content server address is invalid."); return; }
    // Updating: take the pack out of service first; the descriptor is rewritten last.
    char p[400];
    if (texture_in_use(ci, it)) { g_board.finish(false, 0, "This texture pack is in use. Restart Minecraft, then update it."); return; }
    if (version_path(p, sizeof p, ci.root, it.id)) (void)sceKernelUnlink(p);
    if (it.dlc_id && it.skin_count) { snprintf(p, sizeof p, "%s/%s/%s", ci.root, it.id, kDescFile); (void)sceKernelUnlink(p); }
    if (!strcmp(ci.repo, "texture-packs")) { snprintf(p, sizeof p, "%s/%s/%s", ci.root, it.id, kTexDescFile); (void)sceKernelUnlink(p); }
    GetCtx g{api, trust, item_total_size(it), 0, 0, 0, 0, it.file_count, nullptr, nullptr};
    bool ok = lce_content_install_with(install_getter, &g, &kFs, base, ci.repo, ci.root, &it) != 0;
    if (ok && !write_version_marker(ci, it)) log("Mods menu: version marker not written.\n");
    ItemState s = ok ? probe(ci, it) : ItemState::NotInstalled;
    if (!ok) { if (probe(ci, it) == ItemState::NotInstalled) (void)remove_item(ci, it); s = probe(ci, it); }
    g_state[g_board.item_index] = s;
    if (ok) {
        // A texture pack without a descriptor (other file layout) is only stored.
        bool plain = ci.after_install_plain && !strcmp(ci.repo, "texture-packs") &&
                     snprintf(p, sizeof p, "%s/%s/%s", ci.root, it.id, kTexDescFile) > 0 && !path_exists(p);
        finish_msg(true, 0, "%s\n%s", it.name, plain ? ci.after_install_plain : ci.after_install);
        char note[120]; snprintf(note, sizeof note, "%s installed.", it.name); lce_mods_notify(note);
    } else if (g_board.cancelled()) {
        g_board.finish(false, 0, "Cancelled. Nothing was changed.");
    } else {
        g_board.finish(false, 0, "Download failed (network, signature or checksum error). The file was not installed.");
        lce_mods_notify("Content download failed.");
    }
}

void job_uninstall(const CategoryInfo &ci) {
    const lce_catalog_item &it = g_board.item;
    if (texture_in_use(ci, it)) { g_board.finish(false, 0, "This texture pack is in use. Restart Minecraft, then remove it."); return; }
    bool ok = remove_item(ci, it);
    g_state[g_board.item_index] = probe(ci, it);
    if (ok) finish_msg(true, 0, "%s removed.%s", it.name, ci.activation == Activation::OnRestart ? " Restart Minecraft to apply." : "");
    else g_board.finish(false, 0, "Could not remove all files.");
}

void job_main(void *, uint64_t) {
    const CategoryInfo &ci = category_info(g_board.category);
    const lce_https_api *api = nullptr;
    const lce_https_trust *trust = nullptr;
    const char *url = lce_mods_net(&api, &trust);
    switch (g_board.kind) {
    case JobKind::Load:
        if (!url) { g_board.finish(false, 0, "Server connection is not set up (needs /data/lce-account.conf and the CA file)."); return; }
        job_load(ci, url, api, trust); return;
    case JobKind::Install:
        if (!url) { g_board.finish(false, 0, "Server connection is not set up."); return; }
        job_install(ci, url, api, trust); return;
    case JobKind::Uninstall:
        job_uninstall(ci); return;
    }
}

bool start_job(JobKind k, Category c, unsigned index) {
    const lce_catalog_item *it = (k == JobKind::Load) ? nullptr : &g_cat.items[index];
    if (!g_board.begin(k, c, index, it)) return false;
    if (!lce_mods_schedule(job_main, nullptr)) {
        g_board.finish(false, 0, "The background worker is busy. Try again in a moment.");
        return false;
    }
    return true;
}

// ============================================================ UI side
// Applies finished worker results to the menu (owner thread only).
void poll() {
    uint64_t d, t; unsigned f, fs;
    if (g_board.poll_progress(&d, &t, &f, &fs)) g_menu.set_progress(d, t, f, fs);
    Board::Result r;
    if (!g_board.take(&r)) return;
    if (r.kind == JobKind::Load) {
        g_menu.loaded(r.category, r.ok, r.count, r.message);
        if (r.ok) for (unsigned i = 0; i < r.count && i < kMaxItems; ++i) g_menu.set_state(i, g_state[i]);
    } else {
        if (r.item_index < kMaxItems) g_menu.set_state(r.item_index, g_state[r.item_index]);
        g_menu.finished(r.ok, r.message);
    }
}

using Model = mc195_mods::Model;
using TKey = tab_modal::Key;
using Response = tab_modal::Response;
using Result = tab_modal::InputResult;

void leave(Model *self, bool next) {
    g_active = false;
    g_menu.touch();
    const unsigned want = next ? (unsigned)mc195_mods::Section::mods : (unsigned)mc195_mods::Section::maps;
    if (model_section(self) != want) (void)real_input(self, next ? TKey::next_tab : TKey::previous_tab, false);
}

bool map_key(TKey k, lce_mods::Key *out) {
    switch (k) {
    case TKey::up: *out = lce_mods::Key::Up; return true;
    case TKey::down: *out = lce_mods::Key::Down; return true;
    case TKey::left: *out = lce_mods::Key::Left; return true;
    case TKey::right: *out = lce_mods::Key::Right; return true;
    case TKey::accept: *out = lce_mods::Key::Accept; return true;
    case TKey::back: *out = lce_mods::Key::Back; return true;
    case TKey::previous_tab: *out = lce_mods::Key::PrevTab; return true;
    case TKey::next_tab: *out = lce_mods::Key::NextTab; return true;
    default: return false;
    }
}

Response handle_overlay_key(Model *self, TKey key, bool repeat) {
    lce_mods::Key k;
    poll();
    if (!map_key(key, &k)) {
        if (key != TKey::close) return Response{Result::consumed, {}};
        g_active = false;
        return real_input(self, key, false);                 // close the whole Mods menu
    }
    Cmd c = g_menu.key(k, repeat);
    switch (c.kind) {
    case CmdKind::Close:
        g_active = false;
        return real_input(self, TKey::back, false);          // closes the whole Mods menu
    case CmdKind::LeavePrev: leave(self, false); return Response{Result::changed, {}};
    case CmdKind::LeaveNext: leave(self, true); return Response{Result::changed, {}};
    case CmdKind::LoadCatalog:
        if (!start_job(JobKind::Load, c.category, 0)) {
            g_menu.loaded(c.category, false, 0, g_board.running() ? "Still finishing the previous request. Try again in a moment."
                                                                  : "The background worker is busy. Try again in a moment.");
        }
        break;
    case CmdKind::Install:
    case CmdKind::Uninstall:
        if (!start_job(c.kind == CmdKind::Install ? JobKind::Install : JobKind::Uninstall, c.category, c.item))
            g_menu.finished(false, "The background worker is busy. Try again in a moment.");
        break;
    case CmdKind::Cancel: g_board.request_cancel(); break;
    default: break;
    }
    return Response{c.kind == CmdKind::None ? Result::consumed : Result::changed, {}};
}

// ---- drawing (same panel geometry/colours as the retail Mods screen)
using DC = menu_render::DirectColor;
struct Painter {
    const menu_render::DirectSink &s;
    int gw, gh;
    bool rect(int a, int b, int c, int d, DC col) { return s.rect(s.context, a, b, c, d, col); }
    bool text(int a, int b, int c, const char *v, DC col) {
        size_t n = strlen(v), max = (size_t)c / (unsigned)gw;
        if (n > max) n = max;
        return s.text(s.context, a, b, c, (int)gh, v, n, col);
    }
};
struct WrapCtx { Painter *p; int x, y, w, step; DC col; bool ok; };
void wrap_emit(void *c, const char *s, size_t n) {
    auto *w = static_cast<WrapCtx *>(c);
    char line[96];
    if (n >= sizeof line) n = sizeof line - 1;
    memcpy(line, s, n); line[n] = 0;
    if (n && w->ok) w->ok = w->p->text(w->x, w->y, w->w, line, w->col);
    w->y += w->step;
}
bool draw_wrapped(Painter &p, int x, int y, int w, const char *text, DC col, unsigned max_lines, int *end_y = nullptr) {
    WrapCtx c{&p, x, y, w, 19, col, true};
    wrap_text(text, (size_t)w / (unsigned)p.gw, max_lines, wrap_emit, &c);
    if (end_y) *end_y = c.y;
    return c.ok;
}

bool draw_overlay(const menu_render::DirectSink &s, int x, int y, int w, int h, uint8_t gw, uint8_t gh) {
    if (!gw || !gh || w < 580 || h < 390 || !s.clip || !s.rect || !s.text) return false;
    poll();
    Painter p{s, gw, gh};
    const DC ink{35, 35, 35, 255}, white{245, 245, 245, 255}, muted{92, 92, 92, 255}, border{30, 30, 30, 255};
    const DC yellow{255, 255, 95, 255}, orange{143, 71, 15, 255};
    const int pw = w < 900 ? w - 40 : 860, ph = 390, px = x + (w - pw) / 2, py = y + (h - ph) / 2;
    if (!s.clip(s.context, x, y, w, h) || !p.rect(px + 5, py + 5, pw, ph, {0, 0, 0, 145}) || !p.rect(px, py, pw, ph, border) ||
        !p.rect(px + 3, py + 3, pw - 6, ph - 6, {204, 204, 204, 255}) || !p.text(px + 16, py + 14, pw - 32, "LCE Mods", ink)) return false;
    static const char *const tabs[] = {"Mods", "Skin Packs", "Texture Packs", "Maps", "Download"};
    const int tw = (pw - 28) / 5;
    for (unsigned i = 0; i < 5; ++i) {
        bool active = i == 4;
        int tx = px + 14 + (int)i * tw;
        if (!p.rect(tx, py + 36, tw - 3, 28, border) || !p.rect(tx + 1, py + 37, tw - 5, 26, active ? DC{128, 137, 186, 255} : DC{157, 157, 157, 255}) ||
            !p.text(tx + 9, py + 46, tw - 18, tabs[i], active ? yellow : white)) return false;
    }
    const int listx = px + 14, listy = py + 75, listw = (pw - 40) * 53 / 100, detailx = listx + listw + 14, detailw = pw - 28 - listw - 14;
    if (!p.rect(listx, listy, listw, 244, {69, 69, 69, 255}) || !p.rect(detailx, listy, detailw, 244, {224, 224, 224, 255})) return false;
    const int dx = detailx + 12, dw = detailw - 24;
    const Screen sc = g_menu.screen();
    const char *status = "";
    bool ok = true;
    auto row = [&](unsigned r, bool sel, const char *a, const char *b) {
        int ry = listy + 5 + (int)r * 33;
        return p.rect(listx + 4, ry, listw - 8, 31, sel ? DC{119, 129, 177, 255} : DC{125, 125, 125, 255}) &&
               p.text(listx + 11, ry + 4, listw - 28, a, sel ? yellow : white) &&
               (!b || p.text(listx + 11, ry + 17, listw - 28, b, white));
    };
    if (sc == Screen::Categories) {
        for (unsigned i = 0; ok && i < kCategoryCount; ++i) {
            const auto &ci = category_info((Category)i);
            ok = row(i, i == g_menu.cursor(), ci.label, ci.activation == Activation::OnRestart ? "Applied at next launch" : "Download only for now");
        }
        const auto &ci = category_info((Category)g_menu.cursor());
        ok = ok && p.text(dx, listy + 12, dw, ci.label, ink);
        ok = ok && draw_wrapped(p, dx, listy + 39, dw, ci.intro, muted, 8);
        status = "Press Cross to browse the server's list.";
    } else if (sc == Screen::Loading) {
        ok = p.text(listx + 10, listy + 16, listw - 20, "Contacting the content server...", white) &&
             p.text(dx, listy + 12, dw, category_info(g_menu.category()).label, ink) &&
             draw_wrapped(p, dx, listy + 39, dw, "Downloading and verifying the signed list.", muted, 4);
        status = "Circle: back";
    } else {
        // Items / Confirm / Working / Message show the list of the selected category.
        const unsigned n = g_menu.item_count();
        if (!n) ok = p.text(listx + 10, listy + 16, listw - 20, "Nothing to show", white);
        for (unsigned r = 0; ok && r < kVisibleRows && g_menu.scroll() + r < n; ++r) {
            unsigned i = g_menu.scroll() + r;
            const auto &it = g_cat.items[i];
            char a[80], b[64], sz[24];
            snprintf(a, sizeof a, "%s  v%s", it.name, it.version);
            format_size(item_total_size(it), sz, sizeof sz);
            snprintf(b, sizeof b, "%s  -  %s", item_state_label(g_menu.state(i)), sz);
            ok = row(r, i == g_menu.cursor(), a, b);
        }
        if (ok && n > kVisibleRows) {
            const int thumb = 244 * (int)kVisibleRows / (int)n < 12 ? 12 : 244 * (int)kVisibleRows / (int)n;
            const int offset = (244 - thumb) * (int)g_menu.scroll() / (int)(n - kVisibleRows);
            ok = p.rect(listx + listw - 7, listy + 2, 4, 240, {47, 47, 47, 255}) && p.rect(listx + listw - 7, listy + offset, 4, thumb, {219, 219, 219, 255});
        }
        const unsigned cur = g_menu.cursor() < kMaxItems ? g_menu.cursor() : 0;
        const auto &it = g_cat.items[cur];
        const auto &ci = category_info(g_menu.category());
        if (ok && n) {
            char t[100], sz[24];
            ok = p.text(dx, listy + 12, dw, it.name, ink);
            int y2 = listy + 36;
            format_size(item_total_size(it), sz, sizeof sz);
            snprintf(t, sizeof t, "Version %s, %s, %u file%s", it.version, sz, it.file_count, it.file_count == 1 ? "" : "s");
            ok = ok && p.text(dx, y2, dw, t, muted);
            ok = ok && draw_wrapped(p, dx, y2 + 22, dw, it.description, ink, 3);
            ok = ok && p.text(dx, listy + 110, dw, item_state_label(g_menu.state(cur)), g_menu.state(cur) == ItemState::UpdateAvailable ? orange : ink);
            if (sc == Screen::Items) {
                ItemState st = g_menu.state(cur);
                const bool install = st == ItemState::NotInstalled || st == ItemState::UpdateAvailable;
                ok = ok && p.rect(detailx + 10, listy + 136, detailw - 20, 30, {112, 127, 172, 255}) &&
                     p.text(detailx + 20, listy + 147, detailw - 40, install ? (st == ItemState::UpdateAvailable ? "Cross: Update" : "Cross: Download and install") : "Cross: Uninstall", white);
                ok = ok && draw_wrapped(p, dx, listy + 176, dw, ci.activation == Activation::OnRestart ? "Takes effect after you restart Minecraft."
                                                                                                          : "Download only: cannot be activated yet.", muted, 3);
            }
        }
        if (ok && sc == Screen::Confirm) {
            ok = p.rect(detailx + 6, listy + 118, detailw - 12, 120, border) && p.rect(detailx + 8, listy + 120, detailw - 16, 116, {236, 236, 236, 255}) &&
                 p.text(dx + 4, listy + 130, dw - 8, "Uninstall this item?", orange) &&
                 draw_wrapped(p, dx + 4, listy + 152, dw - 8, "Its files will be deleted from the console.", ink, 3) &&
                 p.text(dx + 4, listy + 206, dw - 8, "Cross: Yes    Circle: No", ink);
            status = "Confirm: Cross to uninstall, Circle to keep it.";
        } else if (ok && sc == Screen::Working) {
            const uint64_t total = g_menu.total(), done = g_menu.done();
            ok = p.rect(detailx + 6, listy + 118, detailw - 12, 120, border) && p.rect(detailx + 8, listy + 120, detailw - 16, 116, {236, 236, 236, 255});
            if (g_menu.uninstalling()) ok = ok && p.text(dx + 4, listy + 130, dw - 8, "Removing files...", ink);
            else {
                char t[80], a[24], b[24];
                format_size(done, a, sizeof a); format_size(total, b, sizeof b);
                snprintf(t, sizeof t, "%s  (file %u of %u)", g_menu.cancelling() ? "Cancelling..." : "Downloading...", g_menu.file(), g_menu.files());
                ok = ok && p.text(dx + 4, listy + 130, dw - 8, t, ink);
                const int bw = dw - 8, fill = total ? (int)((uint64_t)bw * (done > total ? total : done) / total) : 0;
                ok = ok && p.rect(dx + 4, listy + 160, bw, 18, border) && p.rect(dx + 5, listy + 161, bw - 2, 16, {160, 160, 160, 255}) &&
                     (fill <= 2 || p.rect(dx + 5, listy + 161, fill - 2, 16, {112, 160, 112, 255}));
                snprintf(t, sizeof t, "%s / %s", a, b);
                ok = ok && p.text(dx + 4, listy + 186, dw - 8, t, muted) && p.text(dx + 4, listy + 210, dw - 8, g_menu.cancelling() ? "Please wait" : "Circle: Cancel", ink);
            }
            status = g_menu.uninstalling() ? "Working..." : "Downloading. The game stays usable; Circle cancels.";
        } else if (ok && sc == Screen::Message) {
            ok = p.rect(detailx + 6, listy + 6, detailw - 12, 232, border) && p.rect(detailx + 8, listy + 8, detailw - 16, 228, {236, 236, 236, 255}) &&
                 p.text(dx + 4, listy + 16, dw - 8, g_menu.message_ok() ? "Done" : "Problem", g_menu.message_ok() ? ink : orange) &&
                 draw_wrapped(p, dx + 4, listy + 42, dw - 8, g_menu.message(), ink, 9) &&
                 p.text(dx + 4, listy + 214, dw - 8, "Cross / Circle: continue", muted);
            status = g_menu.message_ok() ? "Finished." : "Not completed.";
        } else if (sc == Screen::Items) status = "Cross: install / update / uninstall   Circle: categories";
    }
    if (!ok || !p.text(px + 16, py + 333, pw - 32, status, ink) ||
        !p.text(px + 16, py + 360, pw - 32, "L1 / R1: section   Up / Down: select   Cross: choose   Circle: back", ink)) return false;
    g_drawn_rev = g_menu.revision();
    return true;
}
}  // namespace

// ---------------------------------------------------------------- wraps
extern "C" __attribute__((visibility("hidden"))) bool lce_mods_overlay_active() { return g_active; }

bool wrap_open(Model *self) __asm__("__wrap__ZN10mc195_mods5Model4openEv");
bool wrap_open(Model *self) {
    g_active = false;
    poll();
    if (g_menu.screen() != Screen::Working) g_menu.reset();   // keep showing a download in progress
    return real_open(self);
}

bool wrap_refresh(Model *self) __asm__("__wrap__ZN10mc195_mods5Model7refreshEv");
bool wrap_refresh(Model *self) {
    const bool r = real_refresh(self);
    poll();
    if (g_active && g_menu.revision() != g_drawn_rev) { bump_generation(self); g_drawn_rev = g_menu.revision(); }
    return r;
}

Response wrap_input(Model *self, TKey key, bool repeat) __asm__("__wrap__ZN10mc195_mods5Model5inputEN9tab_modal3KeyEb");
Response wrap_input(Model *self, TKey key, bool repeat) {
    if (g_active) return handle_overlay_key(self, key, repeat);
    if (!repeat && model_active(self)) {
        const unsigned sec = model_section(self);
        if ((key == TKey::next_tab && sec == (unsigned)mc195_mods::Section::maps) || (key == TKey::previous_tab && sec == (unsigned)mc195_mods::Section::mods)) {
            g_active = true;
            poll();
            g_menu.touch();
            bump_generation(self);
            return Response{Result::changed, {}};
        }
    }
    return real_input(self, key, repeat);
}

bool wrap_draw(Model *self, const menu_render::DirectSink &s, int x, int y, int w, int h, uint8_t gw, uint8_t gh)
    __asm__("__wrap__ZN10mc195_mods5Model4drawERKN11menu_render10DirectSinkEiiiihh");
bool wrap_draw(Model *self, const menu_render::DirectSink &s, int x, int y, int w, int h, uint8_t gw, uint8_t gh) {
    if (g_active) return draw_overlay(s, x, y, w, h, gw, gh);
    if (!real_draw(self, s, x, y, w, h, gw, gh)) return false;
    // Discoverability: one line in the title bar. Painted after the original so a failure here never hides the menu.
    Painter p{s, gw, gh};
    const int pw = w < 900 ? w - 40 : 860, px = x + (w - pw) / 2, py = y + (h - 390) / 2;
    const char *hint = "R1 on Maps / L1 on Mods: Download";
    const int hw = (int)strlen(hint) * gw;
    (void)p.text(px + pw - 16 - hw, py + 14, hw, hint, {92, 92, 92, 255});
    return true;
}
