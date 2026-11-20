#include <string.h>
#include "../loader/ledger.h"
#include "../loader/loader.h"
#include "../loader/sha256.h"
#include "../loader/retail195/id_space_195.h"
#include "check.h"
#include "mock_backend.h"

static MockGame game;

int main() {
    mock_game_init(&game, 0);
    lce_id_space s;
    memset(&s, 0, sizeof s);
    lce195_default_id_space(&s);
    s.item_in_use = mock_item_in_use; s.block_in_use = mock_block_in_use; s.user = &game;

    static lce_ledger l;
    CHECK_EQ(lce_ledger_load(&l, "", 0), LCE_OK);
    uint32_t a, b, c, blk, blk2;
    CHECK_EQ(lce_ledger_claim(&l, &s, LCE_KIND_ITEM, "mm:a", &a), LCE_OK);
    CHECK_EQ(a, 2283);  // first ID after the bundled providers' reserved block
    CHECK_EQ(lce_ledger_claim(&l, &s, LCE_KIND_BLOCK, "mm:blk", &blk), LCE_OK);
    CHECK_EQ(blk, 254);  // first allowlisted live-empty block slot (253 is Lucky)
    CHECK_EQ(lce_ledger_claim(&l, &s, LCE_KIND_BLOCK, "mm:blk2", &blk2), LCE_OK);
    CHECK_EQ(blk2, 262);
    CHECK_EQ(lce_block_item_id(blk2), 1030);
    CHECK_EQ(lce_ledger_claim(&l, &s, LCE_KIND_ITEM, "mm:b", &b), LCE_OK);
    CHECK_EQ(b, 2284);
    CHECK_EQ(lce_ledger_claim(&l, &s, LCE_KIND_ITEM, "mm:a", &c), LCE_E_COLLISION);  // twice in one session
    CHECK(l.dirty);

    // serialize -> load round trip keeps every mapping
    static char text[65536];
    size_t len = 0;
    CHECK_EQ(lce_ledger_serialize(&l, text, sizeof text, &len), LCE_OK);
    static lce_ledger r;
    CHECK_EQ(lce_ledger_load(&r, text, len), LCE_OK);
    CHECK_EQ(r.count, 4);
    CHECK_EQ(lce_ledger_find(&r, LCE_KIND_ITEM, "mm:b"), 2284);
    CHECK_EQ(lce_ledger_find(&r, LCE_KIND_BLOCK, "mm:blk2"), 262);
    // next session: claims are stable even if requested in a different order
    CHECK_EQ(lce_ledger_claim(&r, &s, LCE_KIND_ITEM, "mm:b", &c), LCE_OK); CHECK_EQ(c, 2284);
    CHECK_EQ(lce_ledger_claim(&r, &s, LCE_KIND_ITEM, "mm:new", &c), LCE_OK); CHECK_EQ(c, 2285);
    CHECK_EQ(lce_ledger_claim(&r, &s, LCE_KIND_ITEM, "mm:a", &c), LCE_OK); CHECK_EQ(c, 2283);

    // tamper: any byte change is detected; structural lies are rejected
    for (size_t i = 0; i + 72 < len; i += 5) {
        static char t2[65536];
        memcpy(t2, text, len);
        t2[i] ^= 1;
        static lce_ledger x;
        CHECK(lce_ledger_load(&x, t2, len) != LCE_OK);
    }
    {
        static lce_ledger x;
        const char *dup = "LCEIDS 1\nitem mm:a 2283\nitem mm:b 2283\n";
        static char t3[512]; size_t n = strlen(dup); memcpy(t3, dup, n);
        // append a valid hash so only the semantic duplicate check can reject it
        static lce_ledger tmp; lce_ledger_init(&tmp);
        uint8_t d[32];
        lce_sha256(t3, n, d);
        static const char hx[] = "0123456789abcdef";
        memcpy(t3 + n, "sha256 ", 7);
        for (int i = 0; i < 32; ++i) { t3[n + 7 + 2 * i] = hx[d[i] >> 4]; t3[n + 8 + 2 * i] = hx[d[i] & 15]; }
        t3[n + 71] = '\n';
        CHECK_EQ(lce_ledger_load(&x, t3, n + 72), LCE_E_LEDGER);
    }

    // a block's item id may not alias a recorded item, and vice versa
    {
        static lce_ledger x; lce_ledger_init(&x);
        lce_id_space t = s;
        t.item_min = 2283; t.item_max = 2397;
        uint32_t id;
        CHECK_EQ(lce_ledger_claim(&x, &t, LCE_KIND_BLOCK, "mm:low", &id), LCE_OK);  // 254 -> item 254
        CHECK_EQ(id, 254);
        // a plain item range that includes 254 must skip it
        t.item_min = 250; t.item_max = 260;
        game.item_slots[250] = game.item_slots[251] = game.item_slots[252] = nullptr;  // pretend free (synthetic)
        CHECK_EQ(lce_ledger_claim(&x, &t, LCE_KIND_ITEM, "mm:i1", &id), LCE_OK); CHECK_EQ(id, 250);
        CHECK_EQ(lce_ledger_claim(&x, &t, LCE_KIND_ITEM, "mm:i2", &id), LCE_OK); CHECK_EQ(id, 251);
        CHECK_EQ(lce_ledger_claim(&x, &t, LCE_KIND_ITEM, "mm:i3", &id), LCE_OK); CHECK_EQ(id, 252);
        CHECK_EQ(lce_ledger_claim(&x, &t, LCE_KIND_ITEM, "mm:i4", &id), LCE_OK);
        CHECK(id != 254 && id != 253);  // 253 reserved (Lucky), 254 owned by m:low's block item
        mock_game_init(&game, 0);
    }

    // exhaustion -> FULL, never wraps into vanilla/reserved space
    {
        static lce_ledger x; lce_ledger_init(&x);
        uint32_t id = 0, n = 0;
        char key[32];
        lce_status st = LCE_OK;
        while (st == LCE_OK) { snprintf(key, sizeof key, "mm:i%u", n++); st = lce_ledger_claim(&x, &s, LCE_KIND_ITEM, key, &id); CHECK(st != LCE_OK || (id >= 2283 && id <= 2397)); }
        CHECK_EQ(st, LCE_E_FULL);
        CHECK_EQ(n - 1, 2397 - 2283 + 1);
        n = 0; st = LCE_OK;
        while (st == LCE_OK) { snprintf(key, sizeof key, "mm:b%u", n++); st = lce_ledger_claim(&x, &s, LCE_KIND_BLOCK, key, &id); }
        CHECK_EQ(st, LCE_E_FULL);
        CHECK_EQ(n - 1, 19);  // size of the live-probed allowlist
    }

    // recorded ID later taken by the game (update / other plugin): fail closed
    {
        static lce_ledger x; lce_ledger_init(&x);
        uint32_t id;
        CHECK_EQ(lce_ledger_claim(&x, &s, LCE_KIND_ITEM, "mm:z", &id), LCE_OK);
        lce_ledger_reset_live(&x);
        game.item_slots[id] = &game;  // occupied now
        CHECK_EQ(lce_ledger_claim(&x, &s, LCE_KIND_ITEM, "mm:z", &id), LCE_E_COLLISION);
        mock_game_init(&game, 0);
    }

    // API 1.1 ext kinds: effect / enchant / biome / potion, each its own id space with up to 2 ranges
    {
        static lce_ledger x; lce_ledger_init(&x);
        lce_id_space e = s;
        memset(e.ext_range, 0, sizeof e.ext_range);
        e.ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][0][0] = 168; e.ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][0][1] = 169;
        e.ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][1][0] = 50;  e.ext_range[LCE_KIND_BIOME - LCE_KIND_EXT_FIRST][1][1] = 50;
        e.ext_range[LCE_KIND_EFFECT - LCE_KIND_EXT_FIRST][0][0] = 168; e.ext_range[LCE_KIND_EFFECT - LCE_KIND_EXT_FIRST][0][1] = 200;
        static int busy_effect = 0;
        e.ext_in_use = [](void *, uint32_t kind, uint32_t id) { return kind == LCE_KIND_EFFECT && (int)id == busy_effect ? 1 : 0; };
        uint32_t id;
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_BIOME, "mm:b1", &id), LCE_OK); CHECK_EQ(id, 168u);
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_BIOME, "mm:b2", &id), LCE_OK); CHECK_EQ(id, 169u);
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_BIOME, "mm:b3", &id), LCE_OK); CHECK_EQ(id, 50u);   // second range
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_BIOME, "mm:b4", &id), LCE_E_FULL);
        busy_effect = 168;
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_EFFECT, "mm:b1", &id), LCE_OK); CHECK_EQ(id, 169u);  // same key, other kind; 168 live in game
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_ENCHANT, "mm:e", &id), LCE_E_FULL);                  // no range configured
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_ITEM, "mm:b1", &id), LCE_OK);                        // items unaffected
        CHECK_EQ(lce_ledger_claim(&x, &e, LCE_KIND_BIOME, "mm:b1", &id), LCE_E_COLLISION);              // twice per session
        static char text[8192]; size_t n = 0;
        CHECK_EQ(lce_ledger_serialize(&x, text, sizeof text, &n), LCE_OK);
        CHECK(strstr(text, "biome mm:b1 168\n") && strstr(text, "effect mm:b1 169\n") && strstr(text, "biome mm:b3 50\n"));
        static lce_ledger y;
        CHECK_EQ(lce_ledger_load(&y, text, n), LCE_OK);
        CHECK_EQ(lce_ledger_find(&y, LCE_KIND_BIOME, "mm:b2"), 169);
        CHECK_EQ(lce_ledger_find(&y, LCE_KIND_EFFECT, "mm:b1"), 169);
        CHECK_EQ(lce_ledger_claim(&y, &e, LCE_KIND_BIOME, "mm:b2", &id), LCE_OK); CHECK_EQ(id, 169u);   // stable across sessions
        busy_effect = 169;                                                                               // the game now owns it: fail closed
        CHECK_EQ(lce_ledger_claim(&y, &e, LCE_KIND_EFFECT, "mm:b1", &id), LCE_E_COLLISION);
        // duplicate id within one kind is a corrupt ledger; the same number across kinds is fine
        static char bad[256]; size_t bl;
        static lce_ledger t; lce_ledger_init(&t);
        t.count = 2; t.entries[0].kind = LCE_KIND_BIOME; t.entries[0].id = 170; strcpy(t.entries[0].key, "mm:p");
        t.entries[1].kind = LCE_KIND_BIOME; t.entries[1].id = 170; strcpy(t.entries[1].key, "mm:q");
        CHECK_EQ(lce_ledger_serialize(&t, bad, sizeof bad, &bl), LCE_OK);
        static lce_ledger u;
        CHECK_EQ(lce_ledger_load(&u, bad, bl), LCE_E_LEDGER);
        t.entries[1].kind = LCE_KIND_EFFECT;
        CHECK_EQ(lce_ledger_serialize(&t, bad, sizeof bad, &bl), LCE_OK);
        CHECK_EQ(lce_ledger_load(&u, bad, bl), LCE_OK);
        // an unknown kind word stays fatal
        const char junk[] = "LCEIDS 1\nwidget mm:w 3\n";
        CHECK_EQ(lce_ledger_load(&u, junk, sizeof junk - 1), LCE_E_LEDGER);
    }
    TEST_MAIN_END("test_ledger");
}
