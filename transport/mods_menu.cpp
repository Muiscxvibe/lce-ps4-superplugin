#include "mods_menu.hpp"
#include <string.h>
#include <stdio.h>

namespace lce_mods {

namespace {
// Skin and texture packs are the categories the game can pick up on its own at
// the next start: dyn_skins registers <packs>/<id>/SkinPack.pck from lce.desc and
// dyn_textures registers <downloads>/texture-packs/<id>/ from lce.tex. Texture
// packs stay apart from the built-in pack folders (/data/lce-mods/packs, which
// carry baked-in hashes) so a download can never overwrite a built-in pack of the
// same id. Maps and mods are kept in /data/lce-mods/downloads, download only.
const CategoryInfo kInfo[kCategoryCount] = {
    {"Skin Packs", "skin-packs", "/data/lce-mods/packs", Activation::OnRestart,
     "Installed. Restart Minecraft to use the new skins.", "skin-pack",
     "Download signed skin packs from the server. They are verified, installed, and appear after you restart Minecraft.",
     nullptr},
    {"Texture Packs", "texture-packs", "/data/lce-mods/downloads/texture-packs", Activation::OnRestart,
     "Installed. Restart Minecraft, then pick it in the texture pack list.", "texture-pack",
     "Download signed texture packs from the server. They are verified, installed, and appear in the game's texture pack list after you restart Minecraft.",
     "Downloaded. This pack has a layout the game cannot load yet; the files are stored on the console."},
    {"Maps", "maps", "/data/lce-mods/downloads/maps", Activation::DownloadOnly,
     "Downloaded. Maps cannot be imported from here yet; the files are stored for a later update.", "map",
     "Download signed files from the server. The game cannot activate this type yet; files are stored on the console for a later update.",
     nullptr},
    {"Mods", "mods", "/data/lce-mods/downloads/mods", Activation::DownloadOnly,
     "Downloaded. Mods cannot be activated yet; the files are stored for a later update.", "mod",
     "Download signed files from the server. The game cannot activate this type yet; files are stored on the console for a later update.",
     nullptr},
};

void copy(char *out, size_t cap, const char *in) {
    size_t n = 0;
    if (cap) {
        while (in && in[n] && n + 1 < cap) { out[n] = in[n]; ++n; }
        out[n] = 0;
    }
}

struct Emit {
    int (*fn)(void *, const char *, int);
    void *ctx;
    int count;
    bool bad;
    bool put(const char *p, int dir) {
        if (bad) return false;
        if (!fn(ctx, p, dir)) { bad = true; return false; }
        ++count;
        return true;
    }
};
}  // namespace

const CategoryInfo &category_info(Category c) {
    unsigned i = (unsigned)c;
    return kInfo[i < kCategoryCount ? i : 0];
}

int version_compare(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    for (;;) {
        unsigned long long x = 0, y = 0;
        while (*a >= '0' && *a <= '9') { if (x < (1ull << 56)) x = x * 10 + (unsigned)(*a - '0'); ++a; }
        while (*b >= '0' && *b <= '9') { if (y < (1ull << 56)) y = y * 10 + (unsigned)(*b - '0'); ++b; }
        if (x != y) return x < y ? -1 : 1;
        if (!*a && !*b) return 0;
        if (*a == '.') ++a;
        else if (*a) return 1;   // junk: treated as different, newer
        if (*b == '.') ++b;
        else if (*b) return -1;
    }
}

ItemState item_state(const char *installed, const char *catalog_version) {
    if (!installed) return ItemState::NotInstalled;
    if (!installed[0]) return ItemState::Installed;
    int c = version_compare(installed, catalog_version);
    return c < 0 ? ItemState::UpdateAvailable : c > 0 ? ItemState::InstalledNewer : ItemState::Installed;
}

const char *item_state_label(ItemState s) {
    switch (s) {
    case ItemState::Installed: return "Installed";
    case ItemState::UpdateAvailable: return "Update available";
    case ItemState::InstalledNewer: return "Installed (newer)";
    default: return "Not installed";
    }
}

bool mirror_base_from_url(const char *url, char *out, size_t cap) {
    if (!url || !out || !cap || strncmp(url, "https://", 8)) return false;
    size_t n = 8;
    while (url[n] && url[n] != '/' && url[n] != '?') {
        unsigned char c = (unsigned char)url[n];
        if (c <= 0x20 || c >= 0x7f || c == '@' || c == '#' || c == '\\') return false;
        ++n;
    }
    if (n == 8 || url[8] == ':' || n + 1 > cap) return false;
    memcpy(out, url, n);
    out[n] = 0;
    return true;
}

void format_size(uint64_t bytes, char *out, size_t cap) {
    if (!cap) return;
    if (bytes < 1024) { snprintf(out, cap, "%u B", (unsigned)bytes); return; }
    static const char *const units[] = {"KB", "MB", "GB"};
    unsigned u = 0;
    uint64_t scaled = bytes;                 // value * 1024^-(u+1), in tenths below
    unsigned shift = 10;
    while (u < 2 && (bytes >> (shift + 10))) { shift += 10; ++u; }
    scaled = (bytes * 10 + ((1ull << shift) >> 1)) >> shift;   // tenths, rounded
    if (scaled >= 1000) snprintf(out, cap, "%u %s", (unsigned)((scaled + 5) / 10), units[u]);
    else snprintf(out, cap, "%u.%u %s", (unsigned)(scaled / 10), (unsigned)(scaled % 10), units[u]);
}

uint64_t item_total_size(const lce_catalog_item &item) {
    uint64_t t = 0;
    for (unsigned i = 0; i < item.file_count && i < LCE_CATALOG_MAX_FILES; ++i) t += item.files[i].size;
    return t;
}

unsigned wrap_text(const char *text, size_t width, unsigned max_lines,
                   void (*emit)(void *, const char *, size_t), void *ctx) {
    unsigned lines = 0;
    if (!text || !width || !emit) return 0;
    const char *p = text;
    while (*p && lines < max_lines) {
        while (*p == ' ') ++p;
        if (*p == '\n') { ++p; emit(ctx, p, 0); ++lines; continue; }
        if (!*p) break;
        size_t n = 0, brk = 0;
        while (p[n] && p[n] != '\n' && n < width) { if (p[n] == ' ') brk = n; ++n; }
        if (p[n] && p[n] != '\n' && p[n] != ' ' && brk) n = brk;   // break at the last space
        size_t end = n;
        while (end && p[end - 1] == ' ') --end;
        emit(ctx, p, end);
        ++lines;
        p += n;
        if (*p == '\n') ++p;
    }
    return lines;
}

bool version_path(char *out, size_t cap, const char *root, const char *id) {
    if (!out || !root || !id) return false;
    int n = snprintf(out, cap, "%s/%s/%s", root, id, kVersionFile);
    return n > 0 && (size_t)n < cap;
}

int plan_uninstall(const char *root, const char *repo, const lce_catalog_item &item,
                   int (*emit)(void *, const char *, int), void *ctx) {
    char path[400];
    Emit e{emit, ctx, 0, false};
    if (!root || !repo || !emit || !item.file_count || item.file_count > LCE_CATALOG_MAX_FILES) return -1;
    // Descriptor first.
    int n = snprintf(path, sizeof path, "%s/%s/%s", root, item.id, kDescFile);
    if (n <= 0 || (size_t)n >= sizeof path) return -1;
    if (item.dlc_id && item.skin_count && !e.put(path, 0)) return -1;
    if (!strcmp(repo, "texture-packs")) {
        n = snprintf(path, sizeof path, "%s/%s/%s", root, item.id, kTexDescFile);
        if (n <= 0 || (size_t)n >= sizeof path || !e.put(path, 0)) return -1;
    }
    for (unsigned i = 0; i < item.file_count; ++i) {
        if (!lce_catalog_install_path(path, sizeof path, root, repo, &item, &item.files[i])) return -1;
        if (!e.put(path, 0)) return -1;
    }
    if (!version_path(path, sizeof path, root, item.id) || !e.put(path, 0)) return -1;
    // Directories: for every file, its parent chain below <root>/<id>, deepest
    // first, de-duplicated; the id directory is last.
    char dirs[LCE_CATALOG_MAX_FILES * 4][400];
    unsigned dir_count = 0;
    char base[400];
    n = snprintf(base, sizeof base, "%s/%s", root, item.id);
    if (n <= 0 || (size_t)n >= sizeof base) return -1;
    size_t base_len = (size_t)n;
    for (unsigned i = 0; i < item.file_count; ++i) {
        if (!lce_catalog_install_path(path, sizeof path, root, repo, &item, &item.files[i])) return -1;
        for (;;) {
            char *slash = strrchr(path, '/');
            if (!slash || (size_t)(slash - path) <= base_len) break;
            *slash = 0;
            bool seen = false;
            for (unsigned k = 0; k < dir_count; ++k) if (!strcmp(dirs[k], path)) seen = true;
            if (!seen) {
                if (dir_count >= sizeof dirs / sizeof dirs[0]) return -1;
                copy(dirs[dir_count++], sizeof dirs[0], path);
            }
        }
    }
    // Deepest first: sort by number of '/' descending (stable enough for the tiny set).
    for (unsigned a = 0; a < dir_count; ++a)
        for (unsigned b = a + 1; b < dir_count; ++b) {
            unsigned da = 0, db = 0;
            for (const char *p = dirs[a]; *p; ++p) da += *p == '/';
            for (const char *p = dirs[b]; *p; ++p) db += *p == '/';
            if (db > da) { char t[400]; copy(t, sizeof t, dirs[a]); copy(dirs[a], sizeof dirs[a], dirs[b]); copy(dirs[b], sizeof dirs[b], t); }
        }
    for (unsigned k = 0; k < dir_count; ++k) if (!e.put(dirs[k], 1)) return -1;
    if (!e.put(base, 1)) return -1;
    return e.count;
}

// ------------------------------------------------------------------ Menu
void Menu::set_message(bool ok, const char *m) {
    copy(message_, sizeof message_, m ? m : "");
    message_ok_ = ok;
}

void Menu::fix_scroll() {
    if (cursor_ >= count_) cursor_ = count_ ? count_ - 1 : 0;
    if (scroll_ > cursor_) scroll_ = cursor_;
    if (cursor_ >= scroll_ + kVisibleRows) scroll_ = cursor_ - (kVisibleRows - 1);
}

void Menu::reset() {
    screen_ = Screen::Categories;
    cursor_ = scroll_ = 0;
    count_ = 0;
    message_[0] = 0;
    cancelling_ = uninstalling_ = false;
    ++rev_;
}

Cmd Menu::key(Key k, bool repeat) {
    Cmd none{}, changed{CmdKind::Changed, cat_, cursor_};
    auto bump = [&]() { ++rev_; Cmd c = changed; c.category = cat_; c.item = cursor_; return c; };
    switch (screen_) {
    case Screen::Categories:
        if (k == Key::Up) { if (cat_cursor_) { --cat_cursor_; return bump(); } return none; }
        if (k == Key::Down) { if (cat_cursor_ + 1 < kCategoryCount) { ++cat_cursor_; return bump(); } return none; }
        if (repeat) return none;
        if (k == Key::Accept) {
            cat_ = (Category)cat_cursor_;
            screen_ = Screen::Loading; count_ = 0; cursor_ = scroll_ = 0;
            set_message(false, "");
            ++rev_;
            return Cmd{CmdKind::LoadCatalog, cat_, 0};
        }
        if (k == Key::Back) return Cmd{CmdKind::Close, cat_, 0};
        if (k == Key::PrevTab) return Cmd{CmdKind::LeavePrev, cat_, 0};
        if (k == Key::NextTab) return Cmd{CmdKind::LeaveNext, cat_, 0};
        return none;
    case Screen::Loading:
        if (repeat) return none;
        if (k == Key::Back) { screen_ = Screen::Categories; return bump(); }
        return none;
    case Screen::Items:
        if (k == Key::Up) { if (cursor_) { --cursor_; fix_scroll(); return bump(); } return none; }
        if (k == Key::Down) { if (cursor_ + 1 < count_) { ++cursor_; fix_scroll(); return bump(); } return none; }
        if (repeat) return none;
        if (k == Key::Back) { screen_ = Screen::Categories; return bump(); }
        if (k == Key::PrevTab) return Cmd{CmdKind::LeavePrev, cat_, 0};
        if (k == Key::NextTab) return Cmd{CmdKind::LeaveNext, cat_, 0};
        if (k == Key::Accept && count_) {
            ItemState s = states_[cursor_];
            if (s == ItemState::NotInstalled || s == ItemState::UpdateAvailable) {
                screen_ = Screen::Working; uninstalling_ = false; cancelling_ = false;
                done_ = total_ = 0; file_ = files_ = 0;
                ++rev_;
                return Cmd{CmdKind::Install, cat_, cursor_};
            }
            screen_ = Screen::Confirm; ++rev_;
            return Cmd{CmdKind::Changed, cat_, cursor_};
        }
        return none;
    case Screen::Confirm:
        if (repeat) return none;
        if (k == Key::Back || k == Key::Left) { screen_ = Screen::Items; return bump(); }
        if (k == Key::Accept) {
            screen_ = Screen::Working; uninstalling_ = true; cancelling_ = false;
            done_ = total_ = 0; file_ = files_ = 0;
            ++rev_;
            return Cmd{CmdKind::Uninstall, cat_, cursor_};
        }
        return none;
    case Screen::Working:
        if (repeat) return none;
        if (k == Key::Back && !cancelling_ && !uninstalling_) { cancelling_ = true; ++rev_; return Cmd{CmdKind::Cancel, cat_, cursor_}; }
        return none;
    case Screen::Message:
        if (repeat) return none;
        if (k == Key::Accept || k == Key::Back) { screen_ = count_ ? Screen::Items : Screen::Categories; return bump(); }
        return none;
    }
    return none;
}

void Menu::loaded(Category c, bool ok, unsigned count, const char *error) {
    if (screen_ != Screen::Loading || c != cat_) return;
    if (!ok) {
        count_ = 0;
        set_message(false, error && error[0] ? error : "Could not load the content list.");
        screen_ = Screen::Message;
    } else if (!count) {
        count_ = 0;
        set_message(true, "Nothing is published in this category yet.");
        screen_ = Screen::Message;
    } else {
        count_ = count > kMaxItems ? kMaxItems : count;
        cursor_ = scroll_ = 0;
        for (unsigned i = 0; i < kMaxItems; ++i) states_[i] = ItemState::NotInstalled;
        screen_ = Screen::Items;
    }
    ++rev_;
}

void Menu::set_state(unsigned item, ItemState s) {
    if (item < kMaxItems && states_[item] != s) { states_[item] = s; ++rev_; }
}

void Menu::set_progress(uint64_t d, uint64_t t, unsigned f, unsigned fs) {
    if (screen_ != Screen::Working) return;
    if (d != done_ || t != total_ || f != file_ || fs != files_) { done_ = d; total_ = t; file_ = f; files_ = fs; ++rev_; }
}

void Menu::finished(bool ok, const char *message) {
    if (screen_ != Screen::Working) return;
    set_message(ok, message);
    screen_ = Screen::Message;
    cancelling_ = false;
    ++rev_;
}

// ------------------------------------------------------------------ Board
bool Board::begin(JobKind k, Category c, unsigned index, const lce_catalog_item *it) {
    unsigned expected = 0;
    if (!__atomic_compare_exchange_n(&phase, &expected, 3u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return false;
    kind = k; category = c; item_index = index;
    if (it) item = *it; else memset(&item, 0, sizeof item);
    ok = false; loaded_count = 0; message[0] = 0;
    __atomic_store_n(&done, 0, __ATOMIC_RELAXED); __atomic_store_n(&total, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&file, 0, __ATOMIC_RELAXED); __atomic_store_n(&files, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&cancel, 0, __ATOMIC_RELAXED);
    seen_progress_ = __atomic_load_n(&progress_rev, __ATOMIC_RELAXED);
    __atomic_store_n(&phase, 1u, __ATOMIC_RELEASE);
    return true;
}

void Board::report(uint64_t d, uint64_t t, unsigned f, unsigned fs) {
    __atomic_store_n(&done, d, __ATOMIC_RELAXED);
    __atomic_store_n(&total, t, __ATOMIC_RELAXED);
    __atomic_store_n(&file, f, __ATOMIC_RELAXED);
    __atomic_store_n(&files, fs, __ATOMIC_RELAXED);
    __atomic_fetch_add(&progress_rev, 1u, __ATOMIC_RELEASE);
}

bool Board::cancelled() const { return __atomic_load_n(&cancel, __ATOMIC_RELAXED) != 0; }
void Board::request_cancel() { __atomic_store_n(&cancel, 1u, __ATOMIC_RELAXED); }
bool Board::running() const { return __atomic_load_n(&phase, __ATOMIC_ACQUIRE) == 1; }

void Board::finish(bool ok_, unsigned count, const char *msg) {
    ok = ok_; loaded_count = count;
    copy(message, sizeof message, msg ? msg : "");
    __atomic_store_n(&phase, 2u, __ATOMIC_RELEASE);
}

bool Board::take(Result *out) {
    if (__atomic_load_n(&phase, __ATOMIC_ACQUIRE) != 2) return false;
    if (out) {
        out->kind = kind; out->category = category; out->item_index = item_index;
        out->ok = ok; out->count = loaded_count;
        copy(out->message, sizeof out->message, message);
    }
    __atomic_store_n(&phase, 0u, __ATOMIC_RELEASE);
    return true;
}

bool Board::poll_progress(uint64_t *d, uint64_t *t, unsigned *f, unsigned *fs) {
    unsigned rev = __atomic_load_n(&progress_rev, __ATOMIC_ACQUIRE);
    if (rev == seen_progress_) return false;
    seen_progress_ = rev;
    *d = __atomic_load_n(&done, __ATOMIC_RELAXED); *t = __atomic_load_n(&total, __ATOMIC_RELAXED);
    *f = __atomic_load_n(&file, __ATOMIC_RELAXED); *fs = __atomic_load_n(&files, __ATOMIC_RELAXED);
    return true;
}

}  // namespace lce_mods
