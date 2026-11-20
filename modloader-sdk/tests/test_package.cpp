// Parser tests: the converted example package must parse; any corruption must
// be rejected without crashing (run under ASan/UBSan by the Makefile).
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "../loader/package.h"
#include "../loader/sha256.h"
#include "check.h"

static std::vector<uint8_t> load(const char *path) {
    std::vector<uint8_t> v;
    FILE *f = fopen(path, "rb");
    if (!f) return v;
    int c;
    while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c);
    fclose(f);
    return v;
}
static void rehash(std::vector<uint8_t> &v) {
    if (v.size() >= 64) lce_sha256(v.data() + 64, v.size() - 64, v.data() + 24);
}
static lce_status parse(const std::vector<uint8_t> &v) {
    lce_package_view view;
    char err[96];
    return lce_package_parse(v.data(), v.size(), &view, err, sizeof err);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "build/pkg/rubymod/mod.lcm";
    std::vector<uint8_t> pkg = load(path);
    CHECK(pkg.size() > 64);
    lce_package_view v;
    char err[96] = {0};
    lce_status st = lce_package_parse(pkg.data(), pkg.size(), &v, err, sizeof err);
    if (st) fprintf(stderr, "parse error: %s\n", err);
    CHECK_EQ(st, LCE_OK);
    CHECK(!strcmp(lce_package_str(&v, v.meta->modid), "rubymod"));
    CHECK(!strcmp(lce_package_str(&v, v.meta->version), "1.2.0"));
    CHECK_EQ(v.item_count, 3);
    CHECK_EQ(v.block_count, 2);
    CHECK_EQ(v.tex_count, 5);
    CHECK_EQ(v.recipe_count, 5);
    CHECK_EQ(v.smelt_count, 1);
    CHECK_EQ(v.tab_count, 1);
    CHECK(v.lang_count >= 5);

    // key/modid validators
    CHECK(lce_valid_key("rubymod:ruby"));
    CHECK(lce_valid_key("minecraft:#265"));
    CHECK(!lce_valid_key("rubymod:"));
    CHECK(!lce_valid_key("Rubymod:ruby"));
    CHECK(!lce_valid_key("rubymod:../x"));
    CHECK(!lce_valid_key("r:ruby"));
    CHECK(!lce_valid_modid("packs"));
    CHECK(!lce_valid_modid("reservations"));
    CHECK(!lce_valid_modid("worlds")); CHECK(!lce_valid_modid("downloads")); CHECK(!lce_valid_modid("selection-content-v9"));
    CHECK(!lce_valid_modid("minecraft"));
    CHECK(lce_valid_modid("rubymod"));

    // integrity: any unhashed byte flip is a hash failure
    for (size_t i = 64; i < pkg.size(); i += 37) {
        std::vector<uint8_t> c = pkg;
        c[i] ^= 0x5a;
        CHECK_EQ(parse(c), LCE_E_HASH);
    }
    // truncation and size field
    { std::vector<uint8_t> c(pkg.begin(), pkg.end() - 4); CHECK(parse(c) != LCE_OK); }
    { std::vector<uint8_t> c = pkg; c[0] = 'X'; CHECK_EQ(parse(c), LCE_E_INVALID); }
    { std::vector<uint8_t> c = pkg; c[10] = 2; CHECK_EQ(parse(c), LCE_E_VERSION); }      // api major 2
    {
        std::vector<uint8_t> c = pkg;
        constexpr uint16_t newer = LCE_MOD_API_MINOR + 1;
        c[12] = (uint8_t)newer;
        c[13] = (uint8_t)(newer >> 8);
        CHECK_EQ(parse(c), LCE_E_VERSION);
    }
    { std::vector<uint8_t> c = pkg; c[56] = 1; CHECK_EQ(parse(c), LCE_E_INVALID); }      // reserved header byte
    // structural mutations with a correct hash must still be rejected or parse safely
    {   // overlapping sections: point section 1 at section 0
        std::vector<uint8_t> c = pkg;
        memcpy(&c[64 + 16 + 4], &c[64 + 4], 4);
        rehash(c);
        CHECK(parse(c) != LCE_OK);
    }
    {   // string ref out of range in META
        std::vector<uint8_t> c = pkg;
        lce_package_view vv; lce_package_parse(c.data(), c.size(), &vv, nullptr, 0);
        size_t meta_off = (const uint8_t *)vv.meta - c.data();
        uint32_t bad = 0x7fffffff; memcpy(&c[meta_off], &bad, 4);
        rehash(c);
        CHECK_EQ(parse(c), LCE_E_INVALID);
    }
    {   // wrong target game
        std::vector<uint8_t> c = pkg;
        lce_package_view vv; lce_package_parse(c.data(), c.size(), &vv, nullptr, 0);
        size_t off = (const uint8_t *)lce_package_str(&vv, vv.meta->target_game) - c.data();
        c[off + 4] = 'X';
        rehash(c);
        CHECK_EQ(parse(c), LCE_E_VERSION);
    }
    {   // item key in another namespace
        std::vector<uint8_t> c = pkg;
        lce_package_view vv; lce_package_parse(c.data(), c.size(), &vv, nullptr, 0);
        size_t off = (const uint8_t *)lce_package_str(&vv, vv.items[0].key) - c.data();
        c[off] = 'x';  // "xubymod:..."
        rehash(c);
        CHECK_EQ(parse(c), LCE_E_INVALID);
    }
    // fuzz: random byte writes + rehash: must never crash, OK only if still self-consistent
    srand(1234);
    int accepted = 0;
    for (int iter = 0; iter < 20000; ++iter) {
        std::vector<uint8_t> c = pkg;
        int n = 1 + rand() % 4;
        for (int k = 0; k < n; ++k) c[64 + (size_t)rand() % (c.size() - 64)] = (uint8_t)rand();
        rehash(c);
        lce_package_view vv;
        if (lce_package_parse(c.data(), c.size(), &vv, nullptr, 0) == LCE_OK) {
            ++accepted;
            // touch every string that the parser vouched for
            for (uint32_t i = 0; i < vv.item_count; ++i) CHECK(strlen(lce_package_str(&vv, vv.items[i].key)) < LCE_MAX_KEY);
        }
    }
    printf("fuzz: %d/20000 mutated packages still valid (pixel/number changes)\n", accepted);
    // API 1.9 ITEM3 (throwables fixture, optional second argument): records, then corrupt records with a correct hash
    if (argc > 2) {
        std::vector<uint8_t> t = load(argv[2]);
        lce_package_view tv;
        CHECK_EQ(lce_package_parse(t.data(), t.size(), &tv, err, sizeof err), LCE_OK);
        CHECK(tv.items3 && tv.item3_count == tv.item_count && tv.item_count == 3);
        if (tv.items3 && tv.item3_count == 3) {
            CHECK(tv.items3[0].speed_milli == 2000 && tv.items3[0].damage_milli == 4000 && tv.items3[0].effects_count == 1);
            CHECK(!strcmp(lce_package_str(&tv, tv.arrs[tv.items3[0].effects_index]), "minecraft:slowness"));
            CHECK(tv.items3[1].explode_milli == 1500 && tv.items3[1].fire_ticks == 100 && !tv.items3[2].speed_milli);
            const size_t rec = (const uint8_t *)tv.items3 - t.data();
            struct { size_t off; uint32_t value; } bad[] = {{rec + 28, 6}, {rec + 24, 9}, {rec + 20, 0x7fffffff}, {rec + 20, tv.arr_words}};
            for (auto &b : bad) {
                std::vector<uint8_t> c = t;
                memcpy(&c[b.off], &b.value, 4);
                rehash(c);
                CHECK_EQ(parse(c), LCE_E_INVALID);
            }
            std::vector<uint8_t> c = t;   // an effect ref that is not a key
            uint32_t zero = 0; memcpy(&c[(const uint8_t *)&tv.arrs[tv.items3[0].effects_index] - t.data()], &zero, 4);
            rehash(c);
            CHECK_EQ(parse(c), LCE_E_INVALID);
        }
    }
#ifdef LCE_HAVE_BOATS_1_10
    // API 1.10 boat items (boats fixture, optional third argument): ITEM3 boat_type 0..5, 6+ refused
    if (argc > 3) {
        std::vector<uint8_t> t = load(argv[3]);
        lce_package_view tv;
        CHECK_EQ(lce_package_parse(t.data(), t.size(), &tv, err, sizeof err), LCE_OK);
        CHECK(tv.items3 && tv.item3_count == 3);
        if (tv.items3 && tv.item3_count == 3) {
            CHECK(tv.items3[0].boat_type == 1 && tv.items3[1].boat_type == 2 && tv.items3[2].boat_type == 3 && !tv.items3[0].speed_milli);
            CHECK((tv.items[0].flags & 16u) && (tv.items[2].flags & 16u) && !(tv.items[0].flags & 8u));
            std::vector<uint8_t> c = t;
            uint32_t six = 6;
            memcpy(&c[(const uint8_t *)&tv.items3[1].boat_type - t.data()], &six, 4);
            rehash(c);
            CHECK_EQ(parse(c), LCE_E_INVALID);
        }
    }
#endif
    TEST_MAIN_END("test_package");
}
