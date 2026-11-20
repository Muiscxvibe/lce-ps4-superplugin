// Per-mod config store (loader/config_store.h): parser, typed lookups, fallbacks.
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "check.h"
#include "../loader/config_store.h"

static lce_cfg_store g;
static void parse(const char *t) { lce_cfg_parse(&g, t, strlen(t)); }

int main(int argc, char **argv) {
    parse("# generated\n"
          "top = 3\n"
          "[general]\n"
          "ore_rate = 8   # veins per chunk\n"
          "speed = 1.25\n"
          "big = 1e3\n"
          "hex = 0x1F\n"
          "neg = -12\n"
          "enabled = true\n"
          "disabled = false\n"
          "greeting = \"Hi \\\"there\\\" # not a comment\"\n"
          "lit = 'C:\\path'\n"
          "ores = [\"minecraft:iron_ore\", \"a:b\"]\r\n"
          "\n"
          "[client.render]\n"
          "  fancy = true\n"
          "nested.key = 5\n");
    CHECK_EQ(g.bad_lines, 0); CHECK_EQ(g.duplicates, 0);
    CHECK_EQ(g.count, 13);
    CHECK_EQ(lce_cfg_int(&g, "top", 0, 1, 0), 3);
    CHECK_EQ(lce_cfg_int(&g, "general.ore_rate", 1, 1, 64), 8);
    CHECK_EQ(lce_cfg_int(&g, "general.ore_rate", 1, 1, 4), 1);           // out of range -> default
    CHECK_EQ(lce_cfg_int(&g, "general.ore_rate", 7, 1, 0), 8);           // min > max: no range
    CHECK(lce_cfg_double(&g, "general.speed", 0, 0, 10) == 1.25);
    CHECK_EQ(lce_cfg_int(&g, "general.speed", 4, 1, 0), 4);              // fractional double is not an int
    CHECK_EQ(lce_cfg_int(&g, "general.big", 0, 1, 0), 1000);             // integral double is
    CHECK(lce_cfg_double(&g, "general.ore_rate", 0, 1, 0) == 8.0);       // int widens
    CHECK_EQ(lce_cfg_int(&g, "general.hex", 0, 1, 0), 31);
    CHECK_EQ(lce_cfg_int(&g, "general.neg", 0, -100, 100), -12);
    CHECK_EQ(lce_cfg_int(&g, "general.enabled", 0, 0, 1), 1);
    CHECK_EQ(lce_cfg_int(&g, "general.disabled", 1, 0, 1), 0);
    CHECK(!strcmp(lce_cfg_string(&g, "general.greeting", "x"), "Hi \"there\" # not a comment"));
    CHECK(!strcmp(lce_cfg_string(&g, "general.lit", "x"), "C:\\path"));
    CHECK(!strcmp(lce_cfg_string(&g, "general.ores", "x"), "[\"minecraft:iron_ore\", \"a:b\"]"));
    CHECK_EQ(lce_cfg_int(&g, "client.render.fancy", 0, 0, 1), 1);
    CHECK_EQ(lce_cfg_int(&g, "client.render.nested.key", 0, 1, 0), 5);
    CHECK(!strcmp(lce_cfg_string(&g, "general.ore_rate", "dflt"), "dflt")); // wrong type -> default
    CHECK_EQ(lce_cfg_int(&g, "general.greeting", 9, 1, 0), 9);
    CHECK_EQ(lce_cfg_int(&g, "missing", 42, 1, 0), 42);
    CHECK(lce_cfg_string(&g, "missing", nullptr) == nullptr);
    CHECK(lce_cfg_find(&g, nullptr) == nullptr && lce_cfg_find(nullptr, "top") == nullptr);

    // bad lines are skipped and counted; the rest applies; duplicates keep the first value
    parse("a = 1\n"
          "b = \n"                 // 2: no value
          "c = hello\n"            // 3: bare word
          "= 4\n"                  // 4: no key
          "d = \"unterminated\n"   // 5
          "e = 1_000\n"            // 6: underscores unsupported
          "[bad section\n"         // 7
          "f = 2\n"               // 8: keys under a broken header are skipped
          "a = 9\n"               // 9
          "[[arr]]\n"              // 10: arrays of tables unsupported...
          "g = 3\n"                // ...and their keys are skipped (11)
          "[ok]\n"
          "h = 4\n"
          "s = \"x\" trailing\n"   // 14
          "k$ = 1\n");             // 15
    CHECK_EQ(g.first_bad_line, 2);
    CHECK_EQ(g.bad_lines, 12);
    CHECK_EQ(g.duplicates, 0);
    CHECK_EQ(lce_cfg_int(&g, "a", 0, 1, 0), 1);
    CHECK(!lce_cfg_find(&g, "f"));
    CHECK_EQ(lce_cfg_int(&g, "ok.h", 0, 1, 0), 4);
    CHECK(!lce_cfg_find(&g, "g") && !lce_cfg_find(&g, "arr.g"));

    // limits: key length, string length, entry count
    {
        std::string t = std::string(70, 'k') + " = 1\n";
        t += "s = \"" + std::string(255, 'x') + "\"\n";
        t += "t = \"" + std::string(256, 'x') + "\"\n";
        for (int i = 0; i < 140; ++i) t += "n" + std::to_string(i) + " = " + std::to_string(i) + "\n";
        lce_cfg_parse(&g, t.data(), t.size());
        CHECK(lce_cfg_find(&g, "s") != nullptr);
        CHECK_EQ(strlen(lce_cfg_string(&g, "s", "")), 255);
        CHECK(!lce_cfg_find(&g, "t"));
        CHECK_EQ(g.count, LCE_CFG_MAX_ENTRIES);
        CHECK_EQ(g.bad_lines, 2 + (140 - (LCE_CFG_MAX_ENTRIES - 1)));
        CHECK_EQ(lce_cfg_int(&g, "n126", -1, 1, 0), 126);
        CHECK(!lce_cfg_find(&g, "n127"));
    }
    // BOM, CRLF, empty and comment-only input, no trailing newline
    parse("a = 9223372036854775807\nb = -9223372036854775808\nc = 9223372036854775808\nd = 0xFFFFFFFFFFFFFFFF\ne = 0x7fffffffffffffff\nf = 0x\ng = -0x10\nh = 1x\n");
    CHECK_EQ(lce_cfg_int(&g, "a", 0, 1, 0), INT64_MAX); CHECK(lce_cfg_int(&g, "b", 0, 1, 0) == INT64_MIN);
    CHECK(!lce_cfg_find(&g, "c") && !lce_cfg_find(&g, "d") && !lce_cfg_find(&g, "f") && !lce_cfg_find(&g, "h"));
    CHECK_EQ(lce_cfg_int(&g, "e", 0, 1, 0), INT64_MAX); CHECK_EQ(lce_cfg_int(&g, "g", 0, 1, 0), -16); CHECK_EQ(g.bad_lines, 4);
    parse("[s]\nk = 1\nk = 2\n[s]\nk = 3\n"); CHECK_EQ(g.duplicates, 2); CHECK_EQ(lce_cfg_int(&g, "s.k", 0, 1, 0), 1);
    parse("\xef\xbb\xbfx = 1\r\ny = 2");
    CHECK_EQ(g.bad_lines, 0); CHECK_EQ(lce_cfg_int(&g, "x", 0, 1, 0), 1); CHECK_EQ(lce_cfg_int(&g, "y", 0, 1, 0), 2);
    parse(""); CHECK_EQ(g.count, 0);
    parse("# only\n   \n"); CHECK_EQ(g.count, 0); CHECK_EQ(g.bad_lines, 0);
    // garbage never crashes (ASan) and never exceeds the table
    {
        uint32_t seed = 12345;
        char buf[512];
        const char alphabet[] = "ab=.[]\"'#\\ \n01x-e_";
        for (int iter = 0; iter < 20000; ++iter) {
            size_t n = (seed = seed * 1103515245u + 12345u) % sizeof buf;
            for (size_t i = 0; i < n; ++i) { seed = seed * 1103515245u + 12345u; buf[i] = alphabet[(seed >> 16) % (sizeof alphabet - 1)]; }
            lce_cfg_parse(&g, buf, n);
            if (g.count > LCE_CFG_MAX_ENTRIES) { CHECK(false); break; }
            for (uint32_t i = 0; i < g.count; ++i) if (strlen(g.e[i].key) >= LCE_CFG_MAX_KEY) { CHECK(false); break; }
        }
    }
    // the default file tools/lce_modpack.py writes for tests/fixtures/allcontent reads back exactly
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        CHECK(f != nullptr);
        if (f) {
            static char buf[LCE_CFG_MAX_FILE + 1];
            size_t n = fread(buf, 1, sizeof buf, f);
            fclose(f);
            CHECK(n <= LCE_CFG_MAX_FILE);
            lce_cfg_parse(&g, buf, n);
            CHECK_EQ(g.bad_lines, 0); CHECK_EQ(g.duplicates, 0); CHECK_EQ(g.count, 7);
            CHECK_EQ(lce_cfg_int(&g, "general.ore_rate", 0, 1, 64), 8);
            CHECK(lce_cfg_double(&g, "general.speed", 0, 0.1, 10) == 1.25);
            CHECK_EQ(lce_cfg_int(&g, "general.enable_wand", 0, 0, 1), 1);
            CHECK(!strcmp(lce_cfg_string(&g, "general.greeting", ""), "Hi \"miner\"\tC:\\ok"));
            CHECK(!strcmp(lce_cfg_string(&g, "general.ores", ""), "[\"minecraft:iron_ore\", 3, false]"));
            CHECK_EQ(lce_cfg_int(&g, "client.render.fancy", 1, 0, 1), 0);
            CHECK_EQ(lce_cfg_int(&g, "top_level", 0, 1, 0), -7);
        }
    }
    TEST_MAIN_END("test_config");
}
