#pragma once
// Pure logic for the "Download content" screen of the Mods menu: categories,
// version/installed-state rules, uninstall planning, a menu state machine and
// the worker<->UI handoff board. No console or game dependencies, so it is
// unit-tested on the host (test_mods_menu.cpp). The engine that actually
// downloads and verifies content is content_catalog/content_download.
#include <stddef.h>
#include <stdint.h>
#include "content_catalog.h"

namespace lce_mods {

enum class Category : uint8_t { SkinPacks = 0, TexturePacks = 1, Maps = 2, Mods = 3 };
constexpr unsigned kCategoryCount = 4;

enum class Activation : uint8_t {
    OnRestart,      // takes effect at next game start (skin and texture packs)
    DownloadOnly    // stored on the console but the game cannot activate it yet
};

struct CategoryInfo {
    const char *label;      // menu text
    const char *repo;       // content repo / mirror path: mods, texture-packs, maps, skin-packs
    const char *root;       // install root; content lands in <root>/<id>/
    Activation activation;
    const char *after_install;  // message shown after a successful install
    const char *catalog_type;   // "type" a verified catalog of this category must carry
    const char *intro;          // description on the category screen
    const char *after_install_plain;  // shown instead of after_install when the item got no registration descriptor (nullptr: n/a)
};
const CategoryInfo &category_info(Category c);

// Dotted numeric versions ("1", "1.2.10"). <0, 0, >0 like strcmp; missing parts are 0.
int version_compare(const char *a, const char *b);

enum class ItemState : uint8_t { NotInstalled, Installed, UpdateAvailable, InstalledNewer };
// installed == nullptr: not installed. Empty string: installed, version unknown.
ItemState item_state(const char *installed, const char *catalog_version);
const char *item_state_label(ItemState s);

// "https://host:port/anything" -> "https://host:port". False if not https or
// unsafe characters are present.
bool mirror_base_from_url(const char *url, char *out, size_t cap);

// 1234567 -> "1.2 MB" (powers of 1024).
void format_size(uint64_t bytes, char *out, size_t cap);
uint64_t item_total_size(const lce_catalog_item &item);

// Everything that uninstalling a catalog item removes, in a safe order: the
// registration descriptor first (so a pack is never half-registered), then the
// files, the version marker, then directories deepest first, id directory last.
// emit(ctx, path, is_dir) returns 0 to abort. Returns number of entries
// emitted, or -1 on an unsafe path / overflow.
constexpr const char *kDescFile = "lce.desc";         // skin packs
constexpr const char *kTexDescFile = "lce.tex";       // texture packs (tex_registry.h)
constexpr const char *kVersionFile = "lce.version";
int plan_uninstall(const char *root, const char *repo, const lce_catalog_item &item,
                   int (*emit)(void *, const char *, int), void *ctx);

// Greedy word wrap for the detail pane: emit(ctx, line, len) for each line of at
// most `width` characters (long words are split). Returns the line count, stops
// after max_lines. Newlines in the text force a break.
unsigned wrap_text(const char *text, size_t width, unsigned max_lines,
                   void (*emit)(void *, const char *, size_t), void *ctx);

// Installed marker path "<root>/<id>/lce.version".
bool version_path(char *out, size_t cap, const char *root, const char *id);

// ---------------------------------------------------------------- state machine
enum class Key : uint8_t { Up, Down, Left, Right, Accept, Back, PrevTab, NextTab };
enum class Screen : uint8_t { Categories, Loading, Items, Confirm, Working, Message };
enum class CmdKind : uint8_t { None, Changed, LoadCatalog, Install, Uninstall, Cancel, Close, LeavePrev, LeaveNext };
struct Cmd {
    CmdKind kind = CmdKind::None;
    Category category = Category::SkinPacks;
    unsigned item = 0;
};
constexpr unsigned kMaxItems = LCE_CATALOG_MAX_ITEMS;
constexpr unsigned kVisibleRows = 7;

class Menu {
public:
    // Reset to the category list (menu opened). Worker progress is untouched.
    void reset();
    Cmd key(Key k, bool repeat);

    // Worker results (delivered by the UI thread after polling the Board).
    // loaded() is ignored unless the menu is waiting for that category.
    void loaded(Category c, bool ok, unsigned count, const char *error);
    void set_state(unsigned item, ItemState s);
    void set_progress(uint64_t done, uint64_t total, unsigned file, unsigned files);
    void finished(bool ok, const char *message);   // install/uninstall result (also cancel)

    Screen screen() const { return screen_; }
    Category category() const { return cat_; }
    unsigned cursor() const { return screen_ == Screen::Categories ? cat_cursor_ : cursor_; }
    unsigned scroll() const { return scroll_; }
    unsigned item_count() const { return count_; }
    ItemState state(unsigned i) const { return i < kMaxItems ? states_[i] : ItemState::NotInstalled; }
    const char *message() const { return message_; }
    bool message_ok() const { return message_ok_; }
    bool cancelling() const { return cancelling_; }
    uint64_t done() const { return done_; }
    uint64_t total() const { return total_; }
    unsigned file() const { return file_; }
    unsigned files() const { return files_; }
    bool uninstalling() const { return uninstalling_; }
    // Bumped on every change a redraw must reflect.
    uint32_t revision() const { return rev_; }
    void touch() { ++rev_; }

private:
    void set_message(bool ok, const char *m);
    void fix_scroll();
    Screen screen_ = Screen::Categories;
    Category cat_ = Category::SkinPacks;
    unsigned cat_cursor_ = 0, cursor_ = 0, scroll_ = 0, count_ = 0;
    ItemState states_[kMaxItems] = {};
    char message_[160] = {};
    bool message_ok_ = false, cancelling_ = false, uninstalling_ = false;
    uint64_t done_ = 0, total_ = 0;
    unsigned file_ = 0, files_ = 0;
    uint32_t rev_ = 1;
};

// ---------------------------------------------------------------- worker board
// One job at a time. The UI thread begins a job and polls for completion; the
// worker thread publishes progress and the result. Hand-off uses
// acquire/release on `phase`; everything else is plain data owned by whoever
// holds the phase.
enum class JobKind : uint8_t { Load, Install, Uninstall };
struct Board {
    unsigned phase = 0;        // 0 idle, 1 running, 2 done (result waiting)
    unsigned cancel = 0;
    JobKind kind = JobKind::Load;
    Category category = Category::SkinPacks;
    unsigned item_index = 0;
    lce_catalog_item item{};    // copy used by Install/Uninstall jobs
    // progress (relaxed atomics)
    uint64_t done = 0, total = 0;
    unsigned file = 0, files = 0, progress_rev = 0;
    // result
    bool ok = false;
    unsigned loaded_count = 0;
    char message[160] = {};

    // UI thread. False if a job is already running or a result is unread.
    bool begin(JobKind k, Category c, unsigned item_index, const lce_catalog_item *item);
    // Worker thread.
    void report(uint64_t done_bytes, uint64_t total_bytes, unsigned file_no, unsigned file_count);
    bool cancelled() const;
    void finish(bool ok_, unsigned count, const char *msg);
    // UI thread.
    void request_cancel();
    bool running() const;
    // Copies out the result and returns the board to idle. False if not done.
    struct Result { JobKind kind; Category category; unsigned item_index; bool ok; unsigned count; char message[160]; };
    bool take(Result *out);
    // Latest progress; true if it changed since the last call.
    bool poll_progress(uint64_t *done_bytes, uint64_t *total_bytes, unsigned *file_no, unsigned *file_count);
private:
    unsigned seen_progress_ = 0;
};

}  // namespace lce_mods
