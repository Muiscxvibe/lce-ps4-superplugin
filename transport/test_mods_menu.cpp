/* Host test for mods_menu (Mods menu content flow logic). Build:
 *   g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -I. -I$T test_mods_menu.cpp mods_menu.cpp \
 *     content_catalog.c client_report.c $T/monocypher.c $T/monocypher-ed25519.c   (C files via gcc -c)
 * See run_test_mods_menu.sh.
 */
#include "mods_menu.hpp"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <pthread.h>

using namespace lce_mods;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void test_versions() {
    CHECK(version_compare("1", "1") == 0);
    CHECK(version_compare("1", "2") < 0);
    CHECK(version_compare("1.10", "1.9") > 0);
    CHECK(version_compare("1.0", "1") == 0);
    CHECK(version_compare("2", "1.99.99") > 0);
    CHECK(version_compare("", "0") == 0);
    CHECK(version_compare(nullptr, "1") < 0);
    CHECK(item_state(nullptr, "1") == ItemState::NotInstalled);
    CHECK(item_state("", "3") == ItemState::Installed);
    CHECK(item_state("1", "2") == ItemState::UpdateAvailable);
    CHECK(item_state("2", "2") == ItemState::Installed);
    CHECK(item_state("3", "2") == ItemState::InstalledNewer);
    CHECK(!strcmp(item_state_label(ItemState::UpdateAvailable), "Update available"));
}

static void test_base_url() {
    char b[64];
    CHECK(mirror_base_from_url("https://192.168.1.165:25567/v1/auth", b, sizeof b) && !strcmp(b, "https://192.168.1.165:25567"));
    CHECK(mirror_base_from_url("https://host", b, sizeof b) && !strcmp(b, "https://host"));
    CHECK(!mirror_base_from_url("http://host/x", b, sizeof b));
    CHECK(!mirror_base_from_url("https://", b, sizeof b));
    CHECK(!mirror_base_from_url("https://:80/x", b, sizeof b));
    CHECK(!mirror_base_from_url("https://u@host/x", b, sizeof b));
    CHECK(!mirror_base_from_url("https://ho st/x", b, sizeof b));
    CHECK(!mirror_base_from_url("https://averyveryverylonghostname.example/x", b, 10));
    CHECK(!mirror_base_from_url(nullptr, b, sizeof b));
}

static void test_size() {
    char b[32];
    format_size(0, b, sizeof b); CHECK(!strcmp(b, "0 B"));
    format_size(1023, b, sizeof b); CHECK(!strcmp(b, "1023 B"));
    format_size(1024, b, sizeof b); CHECK(!strcmp(b, "1.0 KB"));
    format_size(16341, b, sizeof b); CHECK(!strcmp(b, "16.0 KB"));
    format_size(8257576, b, sizeof b); CHECK(!strcmp(b, "7.9 MB"));
    format_size(113143272, b, sizeof b); CHECK(!strcmp(b, "108 MB"));
    format_size(5ull << 30, b, sizeof b); CHECK(!strcmp(b, "5.0 GB"));
    format_size(1500, b, 4); CHECK(strlen(b) <= 3);
}

static lce_catalog_item mk(const char *id, const char *ver, std::vector<const char *> paths, unsigned dlc = 0, unsigned skins = 0) {
    lce_catalog_item it{};
    snprintf(it.id, sizeof it.id, "%s", id);
    snprintf(it.version, sizeof it.version, "%s", ver);
    for (auto p : paths) {
        snprintf(it.files[it.file_count].path, sizeof it.files[0].path, "%s", p);
        it.files[it.file_count].size = 100 + it.file_count;
        ++it.file_count;
    }
    it.dlc_id = dlc; it.skin_count = skins;
    return it;
}

struct Plan { std::vector<std::pair<std::string, int>> v; };
static int collect(void *c, const char *p, int d) { static_cast<Plan *>(c)->v.push_back({p, d}); return 1; }
static int abort_second(void *c, const char *p, int d) { auto *pl = static_cast<Plan *>(c); pl->v.push_back({p, d}); return pl->v.size() < 2; }

static void test_uninstall_plan() {
    Plan p;
    auto skin = mk("touhou-2021", "1", {"SkinPack.pck"}, 2021, 8);
    int n = plan_uninstall("/data/lce-mods/packs", "skin-packs", skin, collect, &p);
    CHECK(n == 4 && (int)p.v.size() == 4);
    CHECK(p.v[0].first == "/data/lce-mods/packs/touhou-2021/lce.desc" && !p.v[0].second);   // descriptor first
    CHECK(p.v[1].first == "/data/lce-mods/packs/touhou-2021/SkinPack.pck" && !p.v[1].second);
    CHECK(p.v[2].first == "/data/lce-mods/packs/touhou-2021/lce.version");
    CHECK(p.v[3].first == "/data/lce-mods/packs/touhou-2021" && p.v[3].second == 1);

    Plan q;
    auto tex = mk("pp", "2", {"Data/media.arc", "Data/x16Data.pck", "TexturePack.pck", "a/b/c.txt"});
    n = plan_uninstall("/r", "texture-packs", tex, collect, &q);
    CHECK(n > 0);
    // Texture packs: lce.tex descriptor first (never half-registered); maps/mods have none.
    // Then every file before any directory; dirs deepest first, id dir last.
    CHECK(q.v[0].first == "/r/pp/lce.tex" && !q.v[0].second);
    CHECK(q.v[1].first == "/r/pp/Data/media.arc");
    Plan mp;
    CHECK(plan_uninstall("/r", "maps", mk("mm", "1", {"a.zip"}), collect, &mp) > 0 && mp.v[0].first == "/r/mm/a.zip");
    size_t first_dir = 0;
    for (size_t i = 0; i < q.v.size(); ++i) if (q.v[i].second) { first_dir = i; break; }
    CHECK(first_dir > 0);
    for (size_t i = first_dir; i < q.v.size(); ++i) CHECK(q.v[i].second == 1);
    CHECK(q.v.back().first == "/r/pp");
    CHECK(q.v[first_dir].first == "/r/pp/a/b");
    int data = 0; for (auto &e : q.v) if (e.first == "/r/pp/Data") ++data;
    CHECK(data == 1);                                   // de-duplicated
    for (auto &e : q.v) CHECK(e.first.rfind("/r/pp", 0) == 0);   // never leaves the item folder

    Plan r;
    CHECK(plan_uninstall("/r", "bogus", tex, collect, &r) == -1);       // unknown repo
    auto bad = mk("x", "1", {"../evil"});
    CHECK(plan_uninstall("/r", "maps", bad, collect, &r) == -1);
    auto badid = mk("Bad_ID", "1", {"f"});
    CHECK(plan_uninstall("/r", "maps", badid, collect, &r) == -1);
    lce_catalog_item empty{}; snprintf(empty.id, sizeof empty.id, "e");
    CHECK(plan_uninstall("/r", "maps", empty, collect, &r) == -1);
    Plan s;
    CHECK(plan_uninstall("/r", "maps", mk("m", "1", {"a.zip"}), abort_second, &s) == -1);
}

static void emit_line(void *c, const char *p, size_t n) { static_cast<std::vector<std::string> *>(c)->push_back(std::string(p, n)); }
static void test_wrap() {
    std::vector<std::string> v;
    CHECK(wrap_text("hello brave new world", 11, 10, emit_line, &v) == 2 && v[0] == "hello brave" && v[1] == "new world");
    v.clear();
    CHECK(wrap_text("abcdefghijkl", 5, 10, emit_line, &v) == 3 && v[0] == "abcde" && v[1] == "fghij" && v[2] == "kl");
    v.clear();
    CHECK(wrap_text("a\n\nb", 10, 10, emit_line, &v) == 3 && v[0] == "a" && v[1] == "" && v[2] == "b");
    v.clear();
    CHECK(wrap_text("one two three four", 7, 2, emit_line, &v) == 2 && v[1] == "three");
    v.clear();
    CHECK(wrap_text("", 7, 2, emit_line, &v) == 0 && wrap_text(nullptr, 7, 2, emit_line, &v) == 0 && wrap_text("x", 0, 2, emit_line, &v) == 0);
    v.clear();
    CHECK(wrap_text("exact fit", 9, 5, emit_line, &v) == 1 && v[0] == "exact fit");
    for (auto &l : v) CHECK(l.size() <= 9);
    CHECK(!strcmp(category_info(Category::Maps).catalog_type, "map") && !strcmp(category_info(Category::Mods).catalog_type, "mod") &&
          !strcmp(category_info(Category::SkinPacks).catalog_type, "skin-pack") && !strcmp(category_info(Category::TexturePacks).catalog_type, "texture-pack"));
}

static Cmd press(Menu &m, Key k, bool repeat = false) { return m.key(k, repeat); }

static void test_menu_flow() {
    Menu m;
    m.reset();
    CHECK(m.screen() == Screen::Categories && m.cursor() == 0);
    CHECK(press(m, Key::Up).kind == CmdKind::None);
    CHECK(press(m, Key::Down).kind == CmdKind::Changed && m.cursor() == 1);
    for (int i = 0; i < 6; ++i) press(m, Key::Down);
    CHECK(m.cursor() == 3);
    CHECK(press(m, Key::PrevTab).kind == CmdKind::LeavePrev);
    CHECK(press(m, Key::NextTab).kind == CmdKind::LeaveNext);
    CHECK(press(m, Key::Back).kind == CmdKind::Close);
    CHECK(press(m, Key::Accept, true).kind == CmdKind::None);   // no auto-repeat accept
    press(m, Key::Up); press(m, Key::Up);                       // Texture Packs
    Cmd c = press(m, Key::Accept);
    CHECK(c.kind == CmdKind::LoadCatalog && c.category == Category::TexturePacks && m.screen() == Screen::Loading);
    // stale result for another category is ignored
    m.loaded(Category::Maps, true, 5, nullptr);
    CHECK(m.screen() == Screen::Loading);
    // back out while loading, worker result arrives late and is ignored
    CHECK(press(m, Key::Back).kind == CmdKind::Changed && m.screen() == Screen::Categories);
    m.loaded(Category::TexturePacks, true, 5, nullptr);
    CHECK(m.screen() == Screen::Categories);
    // load again, failure -> message -> back to categories
    press(m, Key::Accept);
    m.loaded(Category::TexturePacks, false, 0, "Content server unreachable.");
    CHECK(m.screen() == Screen::Message && !m.message_ok() && !strcmp(m.message(), "Content server unreachable."));
    press(m, Key::Accept);
    CHECK(m.screen() == Screen::Categories);
    // empty catalog
    press(m, Key::Accept);
    m.loaded(Category::TexturePacks, true, 0, nullptr);
    CHECK(m.screen() == Screen::Message && m.message_ok());
    press(m, Key::Back);
    // real list of 10
    press(m, Key::Accept);
    m.loaded(Category::TexturePacks, true, 10, nullptr);
    CHECK(m.screen() == Screen::Items && m.item_count() == 10);
    m.set_state(1, ItemState::Installed);
    m.set_state(2, ItemState::UpdateAvailable);
    for (int i = 0; i < 9; ++i) press(m, Key::Down, true);       // repeat allowed for navigation
    CHECK(m.cursor() == 9 && m.scroll() == 3);
    CHECK(press(m, Key::Down).kind == CmdKind::None);
    for (int i = 0; i < 9; ++i) press(m, Key::Up);
    CHECK(m.cursor() == 0 && m.scroll() == 0);
    // install item 0 (not installed)
    c = press(m, Key::Accept);
    CHECK(c.kind == CmdKind::Install && c.item == 0 && m.screen() == Screen::Working);
    CHECK(press(m, Key::Down).kind == CmdKind::None);            // inert while working
    m.set_progress(50, 100, 1, 4);
    CHECK(m.done() == 50 && m.total() == 100 && m.file() == 1 && m.files() == 4);
    c = press(m, Key::Back);
    CHECK(c.kind == CmdKind::Cancel && m.cancelling());
    CHECK(press(m, Key::Back).kind == CmdKind::None);            // cancel only once
    m.finished(false, "Cancelled.");
    CHECK(m.screen() == Screen::Message && !m.cancelling());
    press(m, Key::Back);
    CHECK(m.screen() == Screen::Items);
    // update item 2
    press(m, Key::Down); press(m, Key::Down);
    c = press(m, Key::Accept);
    CHECK(c.kind == CmdKind::Install && c.item == 2);
    m.finished(true, "done");
    CHECK(m.message_ok());
    press(m, Key::Accept);
    m.set_state(2, ItemState::Installed);
    // installed item 1 -> confirm uninstall
    press(m, Key::Up);
    c = press(m, Key::Accept);
    CHECK(c.kind == CmdKind::Changed && m.screen() == Screen::Confirm);
    CHECK(press(m, Key::Back).kind == CmdKind::Changed && m.screen() == Screen::Items);
    press(m, Key::Accept);
    c = press(m, Key::Accept);
    CHECK(c.kind == CmdKind::Uninstall && c.item == 1 && m.uninstalling());
    CHECK(press(m, Key::Back).kind == CmdKind::None);            // uninstall is not cancellable
    m.finished(true, "Removed.");
    press(m, Key::Accept);
    CHECK(m.screen() == Screen::Items);
    // tabs leave overlay only from list screens
    CHECK(press(m, Key::NextTab).kind == CmdKind::LeaveNext);
    CHECK(press(m, Key::Back).kind == CmdKind::Changed && m.screen() == Screen::Categories);
    // revision moves on visible changes
    uint32_t r = m.revision(); press(m, Key::Down); CHECK(m.revision() != r);
    r = m.revision(); press(m, Key::Up); press(m, Key::Up); CHECK(m.revision() != r);
    m.reset(); CHECK(m.screen() == Screen::Categories);
    // results arriving in the wrong screen are ignored
    m.finished(true, "x"); CHECK(m.screen() == Screen::Categories);
    m.set_progress(1, 2, 3, 4); CHECK(m.done() == 0);
}

static void test_categories() {
    CHECK(!strcmp(category_info(Category::SkinPacks).repo, "skin-packs"));
    CHECK(category_info(Category::SkinPacks).activation == Activation::OnRestart);
    CHECK(!strcmp(category_info(Category::SkinPacks).root, "/data/lce-mods/packs"));
    CHECK(!strcmp(category_info(Category::TexturePacks).repo, "texture-packs"));
    CHECK(!strcmp(category_info(Category::Maps).repo, "maps"));
    CHECK(!strcmp(category_info(Category::Mods).repo, "mods"));
    // Texture packs are registered at the next start from <downloads>/texture-packs/<id>/lce.tex.
    const auto &tp = category_info(Category::TexturePacks);
    CHECK(tp.activation == Activation::OnRestart);
    CHECK(!strcmp(tp.root, "/data/lce-mods/downloads/texture-packs"));
    CHECK(strstr(tp.after_install, "Restart Minecraft") != nullptr);
    CHECK(tp.after_install_plain && strstr(tp.after_install_plain, "cannot load") != nullptr);
    CHECK(strstr(tp.intro, "texture pack list") != nullptr);
    CHECK(strncmp(tp.root, "/data/lce-mods/packs", 20) != 0);
    CHECK(category_info(Category::SkinPacks).after_install_plain == nullptr);
    for (unsigned i = 0; i < kCategoryCount; ++i) CHECK(*category_info((Category)i).intro);
    for (unsigned i = 2; i < kCategoryCount; ++i) {
        const auto &ci = category_info((Category)i);
        CHECK(ci.activation == Activation::DownloadOnly);
        CHECK(strstr(ci.after_install, "cannot") != nullptr);
        CHECK(strncmp(ci.root, "/data/lce-mods/packs", 20) != 0);   // never inside the built-in pack folder
        char path[256];
        lce_catalog_item it = mk("xx", "1", {"f"});
        CHECK(lce_catalog_install_path(path, sizeof path, ci.root, ci.repo, &it, &it.files[0]) == 1);
    }
    CHECK(strstr(category_info(Category::SkinPacks).after_install, "Restart") != nullptr);
    char p[128]; CHECK(version_path(p, sizeof p, "/a", "b") && !strcmp(p, "/a/b/lce.version"));
    CHECK(!version_path(p, 5, "/a", "b"));
}

static Board g_board;
static void *worker(void *) {
    for (unsigned i = 1; i <= 100 && !g_board.cancelled(); ++i) g_board.report(i, 100, 1, 1);
    g_board.finish(true, 7, "fin");
    return nullptr;
}

static void test_board() {
    Board b;
    Board::Result r;
    uint64_t d, t; unsigned f, fs;
    CHECK(!b.take(&r) && !b.running());
    lce_catalog_item it = mk("m", "1", {"a.zip"});
    CHECK(b.begin(JobKind::Install, Category::Maps, 3, &it));
    CHECK(b.running());
    CHECK(!b.begin(JobKind::Load, Category::Maps, 0, nullptr));   // one job at a time
    CHECK(!b.take(&r));
    CHECK(!b.poll_progress(&d, &t, &f, &fs));
    b.report(10, 20, 1, 2);
    CHECK(b.poll_progress(&d, &t, &f, &fs) && d == 10 && t == 20 && f == 1 && fs == 2);
    CHECK(!b.poll_progress(&d, &t, &f, &fs));
    CHECK(!b.cancelled()); b.request_cancel(); CHECK(b.cancelled());
    b.finish(false, 0, "Cancelled.");
    CHECK(!b.running());
    CHECK(!b.begin(JobKind::Load, Category::Maps, 0, nullptr));   // unread result blocks a new job
    CHECK(b.take(&r) && !r.ok && r.kind == JobKind::Install && r.category == Category::Maps && r.item_index == 3 && !strcmp(r.message, "Cancelled."));
    CHECK(!b.take(&r));
    CHECK(b.begin(JobKind::Load, Category::Mods, 0, nullptr));
    CHECK(!b.cancelled());                                         // cancel flag cleared per job
    CHECK(b.item.file_count == 0);
    b.finish(true, 5, nullptr);
    CHECK(b.take(&r) && r.ok && r.count == 5 && r.message[0] == 0);
    // threaded hand-off
    CHECK(g_board.begin(JobKind::Load, Category::SkinPacks, 0, nullptr));
    pthread_t th; pthread_create(&th, nullptr, worker, nullptr);
    pthread_join(th, nullptr);
    CHECK(g_board.take(&r) && r.ok && r.count == 7 && !strcmp(r.message, "fin"));
}

int main() {
    test_versions(); test_base_url(); test_size(); test_uninstall_plan(); test_menu_flow(); test_categories(); test_board(); test_wrap();
    printf("%d failures\n", failures);
    return failures != 0;
}
