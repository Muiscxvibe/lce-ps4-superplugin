#include "package.h"
#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "ext.h"
#include <stdlib.h>

namespace {

void set_err(char *err, size_t cap, const char *msg) {
    if (!err || !cap) return;
    size_t n = strlen(msg);
    if (n >= cap) n = cap - 1;
    memcpy(err, msg, n);
    err[n] = 0;
}

struct SecSpec { uint32_t type; uint32_t rec_size; uint32_t max_count; };
// rec_size 0 = raw area (STRS, DATA). max_count bounds host and console memory.
const SecSpec kSpecs[] = {
    {LCE_SEC_STRS, 0, 0},
    {LCE_SEC_META, sizeof(lce_pkg_meta), 1},
    {LCE_SEC_TEX, sizeof(lce_pkg_tex), 1024},
    {LCE_SEC_DATA, 0, 0},
    {LCE_SEC_ITEM, sizeof(lce_pkg_item), 512},
    {LCE_SEC_BLCK, sizeof(lce_pkg_block), 256},
    {LCE_SEC_RCPE, sizeof(lce_pkg_recipe), 1024},
    {LCE_SEC_SMLT, sizeof(lce_pkg_smelt), 512},
    {LCE_SEC_TABS, sizeof(lce_pkg_tab), 8},
    {LCE_SEC_LANG, sizeof(lce_pkg_lang), 8192},
    {LCE_SEC_ENTY, sizeof(lce_pkg_entity), 64},
    {LCE_SEC_ORES, sizeof(lce_pkg_ore), 64},
    {LCE_SEC_DEPS, sizeof(lce_pkg_dep), 16},
    {LCE_SEC_SCRIPT, sizeof(lce_pkg_script), 16},
    {LCE_SEC_EXT, 0, 0},
    {LCE_SEC_ARRS, 0, 0},
    {LCE_SEC_ITEM2, sizeof(lce_pkg_item2), 512},
    {LCE_SEC_BLCK2, sizeof(lce_pkg_block2), 256},
    {LCE_SEC_BLCK3, sizeof(lce_pkg_block3), 256},
    {LCE_SEC_BLCK4, sizeof(lce_pkg_block4), 256},
    {LCE_SEC_ITEM3, sizeof(lce_pkg_item3), 512},
    {LCE_SEC_WAYP, sizeof(lce_pkg_waypoint), LCE_PKG_WAYPOINT_LIMIT},
};

static_assert(sizeof(lce_pkg_waypoint) == 56, "WAYP wire record size");

bool valid_waypoint(const lce_pkg_waypoint &pin) {
    auto coordinate = [](int32_t value) {
        return value >= -LCE_PKG_WAYPOINT_COORD_LIMIT && value <= LCE_PKG_WAYPOINT_COORD_LIMIT;
    };
    if (pin.layer >= LCE_PKG_WAYPOINT_LAYERS || pin.reserved || pin.rgb > 0xffffffu ||
        !coordinate(pin.x) || !coordinate(pin.y) || !coordinate(pin.z) ||
        !coordinate(pin.end_x) || !coordinate(pin.end_z)) return false;
    for (uint8_t byte : pin.reserved_tail) if (byte) return false;
    if (pin.kind == LCE_PKG_WAYPOINT_POINT) {
        if (pin.x != pin.end_x || pin.z != pin.end_z) return false;
    } else if (pin.kind != LCE_PKG_WAYPOINT_AREA || pin.x == pin.end_x || pin.z == pin.end_z) {
        return false;
    }
    if (!pin.label[0]) return false;
    bool ended = false;
    for (unsigned char byte : pin.label) {
        if (!byte) ended = true;
        else if (ended || byte < 32 || byte > 126) return false;
    }
    return ended;
}

const SecSpec *spec_for(uint32_t type) {
    for (const SecSpec &s : kSpecs) if (s.type == type) return &s;
    return nullptr;
}

// Strict UTF-8 (no overlongs/surrogates), no C0 controls except none at all.
bool valid_utf8_text(const char *s, uint32_t len) {
    const unsigned char *p = (const unsigned char *)s;
    for (uint32_t i = 0; i < len;) {
        unsigned c = p[i];
        if (c < 0x20 || c == 0x7f) return false;
        if (c < 0x80) { ++i; continue; }
        unsigned n, min;
        if ((c & 0xe0) == 0xc0) { n = 1; min = 0x80; c &= 0x1f; }
        else if ((c & 0xf0) == 0xe0) { n = 2; min = 0x800; c &= 0x0f; }
        else if ((c & 0xf8) == 0xf0) { n = 3; min = 0x10000; c &= 0x07; }
        else return false;
        if (i + n >= len) return false;  // continuation bytes must exist
        for (unsigned k = 1; k <= n; ++k) {
            if ((p[i + k] & 0xc0) != 0x80) return false;
            c = (c << 6) | (p[i + k] & 0x3f);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
        i += n + 1;
    }
    return true;
}

bool str_ok(const lce_package_view *v, lce_pkg_str ref, uint32_t max_len) {
    if (ref >= v->strs_size) return false;
    const char *s = v->strs + ref;
    uint32_t avail = v->strs_size - ref;
    uint32_t len = 0;
    while (len < avail && s[len]) ++len;
    if (len == avail || len > max_len) return false;  // no NUL inside pool / too long
    return valid_utf8_text(s, len);
}

bool key_or_empty(const lce_package_view *v, lce_pkg_str ref) {
    if (!str_ok(v, ref, LCE_MAX_KEY - 1)) return false;
    const char *s = v->strs + ref;
    return !*s || lce_valid_key(s);
}
bool key_required(const lce_package_view *v, lce_pkg_str ref) {
    return str_ok(v, ref, LCE_MAX_KEY - 1) && lce_valid_key(v->strs + ref);
}

}  // namespace

extern "C" int lce_valid_modid(const char *id) {
    if (!id) return 0;
    size_t n = strlen(id);
    if (n < 2 || n >= LCE_MAX_MODID) return 0;
    for (size_t i = 0; i < n; ++i) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    // Folders of /data/lce-mods owned by other plugin parts (skin/texture packs, the Mods-menu downloader,
    // the map importer, the ID ledger) are never mod ids, so discovery skips them silently.
    static const char *const kReserved[] = {"minecraft", "packs", "lce", "reservations", "worlds", "downloads", "maps", "updates", "cache", "staged"};
    for (const char *r : kReserved) if (!strcmp(id, r)) return 0;
    return 1;
}

extern "C" int lce_valid_key(const char *key) {
    if (!key) return 0;
    const char *colon = strchr(key, ':');
    if (!colon) return 0;
    size_t ns = (size_t)(colon - key), n = strlen(key);
    if (ns < 2 || ns >= LCE_MAX_MODID || n >= LCE_MAX_KEY || n == ns + 1) return 0;
    for (size_t i = 0; i < ns; ++i) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    for (size_t i = ns + 1; i < n; ++i) {
        char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/' || c == '-' || c == '#'))
            return 0;
    }
    if (strstr(key, "..")) return 0;
    return 1;
}

extern "C" const char *lce_package_str(const lce_package_view *v, lce_pkg_str ref) {
    return (ref < v->strs_size) ? v->strs + ref : "";
}

extern "C" lce_status lce_package_parse(const uint8_t *data, size_t size, lce_package_view *v, char *err, size_t cap) {
    memset(v, 0, sizeof *v);
    if (!data || size < sizeof(lce_pkg_header) || size > LCE_PKG_MAX_FILE) { set_err(err, cap, "bad size"); return LCE_E_INVALID; }
    const lce_pkg_header *h = (const lce_pkg_header *)data;
    if (memcmp(h->magic, LCE_PKG_MAGIC, 8)) { set_err(err, cap, "bad magic"); return LCE_E_INVALID; }
    if (h->format != LCE_PKG_FORMAT) { set_err(err, cap, "unsupported package format"); return LCE_E_VERSION; }
    if (h->api_major != LCE_MOD_API_MAJOR || h->api_minor > LCE_MOD_API_MINOR) { set_err(err, cap, "api version mismatch"); return LCE_E_VERSION; }
    if (h->file_size != size) { set_err(err, cap, "file_size mismatch (truncated?)"); return LCE_E_INVALID; }
    if (h->section_count == 0 || h->section_count > LCE_PKG_MAX_SECTIONS) { set_err(err, cap, "bad section count"); return LCE_E_INVALID; }
    for (unsigned i = 0; i < sizeof h->reserved; ++i) if (h->reserved[i]) { set_err(err, cap, "reserved header bytes set"); return LCE_E_INVALID; }
    uint8_t digest[32];
    lce_sha256(data + sizeof *h, size - sizeof *h, digest);
    if (memcmp(digest, h->payload_sha256, 32)) { set_err(err, cap, "payload sha256 mismatch"); return LCE_E_HASH; }

    uint32_t table_end = (uint32_t)sizeof *h + (uint32_t)h->section_count * (uint32_t)sizeof(lce_pkg_section);
    if (table_end > size) { set_err(err, cap, "section table out of bounds"); return LCE_E_INVALID; }
    const lce_pkg_section *secs = (const lce_pkg_section *)(data + sizeof *h);
    v->data = data; v->size = (uint32_t)size; v->header = h;
    uint32_t seen = 0;
    for (unsigned i = 0; i < h->section_count; ++i) {
        const lce_pkg_section &s = secs[i];
        const SecSpec *sp = spec_for(s.type);
        if (!sp) { set_err(err, cap, "unknown section type"); return LCE_E_INVALID; }
        if (seen & (1u << s.type)) { set_err(err, cap, "duplicate section"); return LCE_E_INVALID; }
        seen |= 1u << s.type;
        if (s.offset < table_end || (s.offset & 3) || s.offset > size || s.size > size - s.offset) { set_err(err, cap, "section out of bounds"); return LCE_E_INVALID; }
        for (unsigned j = 0; j < i; ++j) {  // no overlap
            const lce_pkg_section &o = secs[j];
            if (s.size && o.size && s.offset < o.offset + o.size && o.offset < s.offset + s.size) { set_err(err, cap, "overlapping sections"); return LCE_E_INVALID; }
        }
        if (sp->rec_size) {
            if (s.count > sp->max_count || (uint64_t)s.count * sp->rec_size != s.size) { set_err(err, cap, "bad record count/size"); return LCE_E_INVALID; }
        } else if (s.count) { set_err(err, cap, "raw section with count"); return LCE_E_INVALID; }
        const uint8_t *body = data + s.offset;
        switch (s.type) {
        case LCE_SEC_STRS: v->strs = (const char *)body; v->strs_size = s.size; break;
        case LCE_SEC_DATA: v->blob = body; v->blob_size = s.size; break;
        case LCE_SEC_META: v->meta = s.count ? (const lce_pkg_meta *)body : nullptr; break;
        case LCE_SEC_TEX: v->tex = (const lce_pkg_tex *)body; v->tex_count = s.count; break;
        case LCE_SEC_ITEM: v->items = (const lce_pkg_item *)body; v->item_count = s.count; break;
        case LCE_SEC_BLCK: v->blocks = (const lce_pkg_block *)body; v->block_count = s.count; break;
        case LCE_SEC_RCPE: v->recipes = (const lce_pkg_recipe *)body; v->recipe_count = s.count; break;
        case LCE_SEC_SMLT: v->smelts = (const lce_pkg_smelt *)body; v->smelt_count = s.count; break;
        case LCE_SEC_TABS: v->tabs = (const lce_pkg_tab *)body; v->tab_count = s.count; break;
        case LCE_SEC_LANG: v->lang = (const lce_pkg_lang *)body; v->lang_count = s.count; break;
        case LCE_SEC_ENTY: v->entities = (const lce_pkg_entity *)body; v->entity_count = s.count; break;
        case LCE_SEC_ORES: v->ores = (const lce_pkg_ore *)body; v->ore_count = s.count; break;
        case LCE_SEC_DEPS: v->deps = (const lce_pkg_dep *)body; v->dep_count = s.count; break;
        case LCE_SEC_WAYP: v->waypoints = (const lce_pkg_waypoint *)body; v->waypoint_count = s.count; break;
        case LCE_SEC_SCRIPT: v->scripts = (const lce_pkg_script *)body; v->script_count = s.count; break;
        case LCE_SEC_EXT: if (s.size & 3) { set_err(err, cap, "EXT size"); return LCE_E_INVALID; } v->ext = (const uint32_t *)body; v->ext_words = s.size / 4; break;
        case LCE_SEC_ITEM2: v->items2 = (const lce_pkg_item2 *)body; v->item2_count = s.count; break;
        case LCE_SEC_BLCK2: v->blocks2 = (const lce_pkg_block2 *)body; v->block2_count = s.count; break;
        case LCE_SEC_BLCK3: v->blocks3 = (const lce_pkg_block3 *)body; v->block3_count = s.count; break;
        case LCE_SEC_BLCK4: v->blocks4 = (const lce_pkg_block4 *)body; v->block4_count = s.count; break;
        case LCE_SEC_ITEM3: v->items3 = (const lce_pkg_item3 *)body; v->item3_count = s.count; break;
        case LCE_SEC_ARRS: if (s.size & 3) { set_err(err, cap, "ARRS size"); return LCE_E_INVALID; } v->arrs = (const uint32_t *)body; v->arr_words = s.size / 4; break;
        }
    }
    if (!v->strs || !v->strs_size || v->strs[0] != 0) { set_err(err, cap, "missing/invalid string pool"); return LCE_E_INVALID; }
    if (!v->meta) { set_err(err, cap, "missing META"); return LCE_E_INVALID; }

    // META
    const lce_pkg_meta *m = v->meta;
    if (!str_ok(v, m->modid, LCE_MAX_MODID - 1) || !lce_valid_modid(v->strs + m->modid)) { set_err(err, cap, "invalid modid"); return LCE_E_INVALID; }
    if (!str_ok(v, m->name, LCE_MAX_TEXT - 1) || !str_ok(v, m->version, 15) || !str_ok(v, m->author, LCE_MAX_TEXT - 1) ||
        !str_ok(v, m->description, 511) || !str_ok(v, m->target_game, 31)) { set_err(err, cap, "invalid META string"); return LCE_E_INVALID; }
    if (strcmp(v->strs + m->target_game, LCE_MOD_TARGET_GAME)) { set_err(err, cap, "package targets a different game build"); return LCE_E_VERSION; }
    const char *modid = v->strs + m->modid;
    size_t ml = strlen(modid);
    auto owned = [&](lce_pkg_str ref) {  // key must live in this mod's namespace
        const char *k = v->strs + ref;
        return !strncmp(k, modid, ml) && k[ml] == ':';
    };

    for (uint32_t i = 0; i < v->tex_count; ++i) {
        const lce_pkg_tex &t = v->tex[i];
        if (!key_required(v, t.key) || !owned(t.key)) { set_err(err, cap, "texture key invalid/not owned"); return LCE_E_INVALID; }
        bool dims = (t.atlas <= 1 || t.atlas == 4) ? (t.width == 16 && t.height == 16) : t.atlas == 2 ? (t.width == 64 && t.height == 32)
                  : t.atlas == 3 ? (t.width == 64 && (t.height == 32 || t.height == 64)) : false;
        if (!dims || t.reserved[0] || t.reserved[1] || t.reserved[2]) { set_err(err, cap, "texture size does not match its atlas (0/1/4: 16x16, 2: 64x32, 3: 64x32|64x64)"); return LCE_E_UNSUPPORTED; }
        if (t.data_size != (uint32_t)t.width * t.height * 4u || t.data_offset > v->blob_size || t.data_size > v->blob_size - t.data_offset) { set_err(err, cap, "texture data out of bounds"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->item_count; ++i) {
        const lce_pkg_item &it = v->items[i];
        if (!key_required(v, it.key) || !owned(it.key) || !str_ok(v, it.name, LCE_MAX_TEXT - 1) || !key_required(v, it.texture) || !key_or_empty(v, it.creative_tab)) { set_err(err, cap, "invalid item record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->block_count; ++i) {
        const lce_pkg_block &b = v->blocks[i];
        bool ok = key_required(v, b.key) && owned(b.key) && str_ok(v, b.name, LCE_MAX_TEXT - 1) && key_required(v, b.texture[0]) && key_or_empty(v, b.drop) && key_or_empty(v, b.creative_tab);
        for (int f = 1; f < 6; ++f) ok = ok && key_or_empty(v, b.texture[f]);
        if (!ok) { set_err(err, cap, "invalid block record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->recipe_count; ++i) {
        const lce_pkg_recipe &r = v->recipes[i];
        bool ok = key_required(v, r.key) && owned(r.key) && key_required(v, r.result) && key_or_empty(v, r.craft_tab) && !r.reserved;
        for (int c = 0; c < 9; ++c) ok = ok && key_or_empty(v, r.cells[c].item) && !r.cells[c].reserved;
        if (!ok) { set_err(err, cap, "invalid recipe record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->smelt_count; ++i) {
        const lce_pkg_smelt &s = v->smelts[i];
        if (!key_required(v, s.key) || !owned(s.key) || !key_required(v, s.input.item) || !key_required(v, s.result) || s.input.reserved) { set_err(err, cap, "invalid smelting record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->tab_count; ++i) {
        const lce_pkg_tab &t = v->tabs[i];
        if (!key_required(v, t.key) || !owned(t.key) || !str_ok(v, t.name, LCE_MAX_TEXT - 1) || !key_required(v, t.icon_item) || t.reserved[0] || t.reserved[1]) { set_err(err, cap, "invalid tab record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->lang_count; ++i) {
        const lce_pkg_lang &l = v->lang[i];
        if (!str_ok(v, l.locale, 15) || !str_ok(v, l.key, LCE_MAX_KEY - 1) || !str_ok(v, l.value, 511)) { set_err(err, cap, "invalid lang record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->entity_count; ++i) {
        const lce_pkg_entity &e = v->entities[i];
        if (!key_required(v, e.key) || !owned(e.key) || !key_required(v, e.base_mob) || !key_or_empty(v, e.texture)) { set_err(err, cap, "invalid entity record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->ore_count; ++i) {
        const lce_pkg_ore &o = v->ores[i];
        if (!key_required(v, o.key) || !owned(o.key) || !key_required(v, o.block) || !key_or_empty(v, o.replace)) { set_err(err, cap, "invalid ore record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->dep_count; ++i) {
        const lce_pkg_dep &d = v->deps[i];
        if (!str_ok(v, d.modid, LCE_MAX_MODID - 1) || !lce_valid_modid(v->strs + d.modid) || !str_ok(v, d.min_version, 15)) { set_err(err, cap, "invalid dependency"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->script_count; ++i) {
        const lce_pkg_script &sc = v->scripts[i];
        if (!str_ok(v, sc.name, 63) || !*(v->strs + sc.name) || sc.data_offset > v->blob_size || sc.data_size > v->blob_size - sc.data_offset || sc.data_size > (1u << 20)) { set_err(err, cap, "invalid script record"); return LCE_E_INVALID; }
    }
    if ((v->items2 && v->item2_count != v->item_count) || (v->blocks2 && v->block2_count != v->block_count)) { set_err(err, cap, "ITEM2/BLCK2 count mismatch"); return LCE_E_INVALID; }
    for (uint32_t i = 0; i < v->item2_count; ++i) {
        const lce_pkg_item2 &x = v->items2[i];
        if (!key_or_empty(v, x.armor_texture) || !key_or_empty(v, x.repair_item) || x.effects_count > 8 ||
            x.effects_index > v->arr_words || (uint64_t)x.effects_count * 4 > v->arr_words - x.effects_index) { set_err(err, cap, "invalid ITEM2 record"); return LCE_E_INVALID; }
        for (uint32_t k = 0; k < x.effects_count; ++k) if (!key_required(v, v->arrs[x.effects_index + 4 * k])) { set_err(err, cap, "invalid food effect"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->block2_count; ++i)
        if (!str_ok(v, v->blocks2[i].harvest_tool, 15)) { set_err(err, cap, "invalid BLCK2 record"); return LCE_E_INVALID; }
    if (v->items3 && v->item3_count != v->item_count) { set_err(err, cap, "ITEM3 count mismatch"); return LCE_E_INVALID; }
    for (uint32_t i = 0; i < v->item3_count; ++i) {
        const lce_pkg_item3 &x = v->items3[i];
        if (x.boat_type > 5 || x.effects_count > 8 || x.effects_index > v->arr_words || (uint64_t)x.effects_count * 4 > v->arr_words - x.effects_index) {
            set_err(err, cap, "invalid ITEM3 record"); return LCE_E_INVALID;
        }
        for (uint32_t k = 0; k < x.effects_count; ++k) if (!key_required(v, v->arrs[x.effects_index + 4 * k])) { set_err(err, cap, "invalid projectile effect"); return LCE_E_INVALID; }
    }
    if (v->blocks3 && v->block3_count != v->block_count) { set_err(err, cap, "BLCK3 count mismatch"); return LCE_E_INVALID; }
    for (uint32_t i = 0; i < v->block3_count; ++i) {
        const lce_pkg_block3 &x = v->blocks3[i];
        bool ok = x.variant_count <= 16;
        if (ok && x.names_index != ~0u) {
            ok = x.names_index <= v->arr_words && x.variant_count <= v->arr_words - x.names_index;
            for (uint32_t k = 0; ok && k < x.variant_count; ++k) ok = str_ok(v, v->arrs[x.names_index + k], LCE_MAX_TEXT - 1);
        }
        if (ok && x.textures_index != ~0u) {
            ok = x.textures_index <= v->arr_words && 6ull * x.variant_count <= v->arr_words - x.textures_index;
            for (uint32_t k = 0; ok && k < 6u * x.variant_count; ++k) ok = !v->arrs[x.textures_index + k] || key_or_empty(v, v->arrs[x.textures_index + k]);
        }
        if (!ok) { set_err(err, cap, "invalid BLCK3 record"); return LCE_E_INVALID; }
    }
    if (v->blocks4 && (v->block4_count != v->block_count || !v->blocks3)) { set_err(err, cap, "BLCK4 count mismatch"); return LCE_E_INVALID; }
    for (uint32_t i = 0; i < v->block4_count; ++i) {   // API 1.11 multi-box shapes
        const lce_pkg_block4 &x = v->blocks4[i];
        const uint32_t n = (uint32_t)x.box_count * x.box_sets;
        bool ok = !x.reserved && x.box_count <= 8 && x.box_sets <= 16 && (!x.box_count == !x.box_sets);
        if (ok && n) ok = x.boxes_index <= v->arr_words && n <= v->arr_words - x.boxes_index;
        for (uint32_t k = 0; ok && k < n; ++k) ok = !(v->arrs[x.boxes_index + k] >> 30);
        if (!ok) { set_err(err, cap, "invalid BLCK4 record"); return LCE_E_INVALID; }
    }
    for (uint32_t i = 0; i < v->waypoint_count; ++i) {
        if (!valid_waypoint(v->waypoints[i])) { set_err(err, cap, "invalid WAYP record"); return LCE_E_INVALID; }
    }
    uint32_t n = 0;
    if (lce_package_ext_walk(v, nullptr, nullptr, &n) != LCE_OK) { set_err(err, cap, "invalid EXT record"); return LCE_E_INVALID; }
    v->ext_count = n;
    return LCE_OK;
}

namespace {
bool slot_str(const lce_package_view *v, uint32_t ref) { return ref == 0 || str_ok(v, ref, 511); }
uint32_t elem_words(uint16_t kind) {
    switch (kind) { case LCE_F_ARR_EFFECT: return 4; case LCE_F_ARR_GOAL: return 4; case LCE_F_ARR_DROP: return 5; case LCE_F_ARR_REF: return 1; default: return 0; }
}
}  // namespace

/* Walks (and bounds-checks) every EXT record. With build != NULL, constructs
 * each descriptor (pointers into the package + malloc'd arrays, freed after
 * the callback) and calls build(user, type, desc). */
extern "C" lce_status lce_package_ext_walk(const lce_package_view *v, lce_status (*build)(void *, uint32_t, const void *), void *user, uint32_t *count) {
    uint32_t pos = 0, records = 0;
    while (pos < v->ext_words) {
        if (v->ext_words - pos < 2) return LCE_E_INVALID;
        uint32_t type = v->ext[pos], slots = v->ext[pos + 1];
        const lce_ext_schema *sc = lce_ext_schema_of(type);
        if (!sc || !sc->packable || slots > 64 || v->ext_words - pos - 2 < slots || ++records > 4096) return LCE_E_INVALID;
        const uint32_t *slot = v->ext + pos + 2;
        uint32_t need = 0;
        for (uint32_t f = 0; f < sc->field_count; ++f) {
            uint16_t k = sc->fields[f].kind;
            need += (k == LCE_F_ARR_EFFECT || k == LCE_F_ARR_GOAL || k == LCE_F_ARR_DROP || k == LCE_F_ARR_REF || k == LCE_F_BLOB) ? 2 : 1;
        }
        if (need != slots) return LCE_E_INVALID;
        alignas(8) uint8_t desc[320];
        memset(desc, 0, sizeof desc);
        *(uint32_t *)desc = sc->desc_size;
        void *temps[16]; uint32_t ntemp = 0;
        lce_status st = LCE_OK;
        uint32_t si = 0;
        uint16_t sx = 0, sy = 0, sz = 0;
        for (uint32_t f = 0; f < sc->field_count && st == LCE_OK; ++f) {
            const lce_ext_field &fd = sc->fields[f];
            uint8_t *dst = desc + fd.offset;
            switch (fd.kind) {
            case LCE_F_KEY: case LCE_F_TEXT: case LCE_F_REF: case LCE_F_REF_REQ: case LCE_F_NAME:
                if (!slot_str(v, slot[si])) { st = LCE_E_INVALID; break; }
                *(const char **)dst = slot[si] ? v->strs + slot[si] : nullptr; ++si; break;
            case LCE_F_U8: if (slot[si] > 0xff) st = LCE_E_INVALID; else *dst = (uint8_t)slot[si]; ++si; break;
            case LCE_F_U16: if (slot[si] > 0xffff) st = LCE_E_INVALID; else *(uint16_t *)dst = (uint16_t)slot[si]; ++si; break;
            case LCE_F_U32: *(uint32_t *)dst = slot[si]; ++si; break;
            case LCE_F_BLOB: {
                uint32_t off = slot[si], bytes = slot[si + 1]; si += 2;
                if (off > v->blob_size || bytes > v->blob_size - off) { st = LCE_E_INVALID; break; }
                *(const uint8_t **)dst = bytes ? v->blob + off : nullptr;
                *(uint32_t *)(desc + fd.count_offset) = bytes;
                break;
            }
            case LCE_F_ARR_EFFECT: case LCE_F_ARR_GOAL: case LCE_F_ARR_DROP: case LCE_F_ARR_REF: {
                uint32_t idx = slot[si], n = slot[si + 1], w = elem_words(fd.kind); si += 2;
                if (n > 256 || idx > v->arr_words || (uint64_t)n * w > v->arr_words - idx) { st = LCE_E_INVALID; break; }
                const uint32_t *a = v->arrs + idx;
                *(uint32_t *)(desc + fd.count_offset) = n;
                if (!n) break;
                for (uint32_t i = 0; i < n * w; ++i) {  // string positions must be valid
                    uint32_t col = i % w;
                    bool is_str = fd.kind == LCE_F_ARR_REF || (fd.kind == LCE_F_ARR_EFFECT && col == 0) || (fd.kind == LCE_F_ARR_GOAL && col == 3) || (fd.kind == LCE_F_ARR_DROP && col == 0);
                    if (is_str && !slot_str(v, a[i])) { st = LCE_E_INVALID; break; }
                }
                if (st != LCE_OK || !build) break;
                if (ntemp >= 16) { st = LCE_E_FULL; break; }
                void *arr = nullptr;
                if (fd.kind == LCE_F_ARR_EFFECT) {
                    lce_effect_ref *e = (lce_effect_ref *)calloc(n, sizeof *e);
                    for (uint32_t i = 0; e && i < n; ++i) { e[i].effect = a[4 * i] ? v->strs + a[4 * i] : nullptr; e[i].duration_ticks = a[4 * i + 1]; e[i].amplifier = (uint8_t)a[4 * i + 2]; e[i].chance_percent = (uint8_t)a[4 * i + 3]; }
                    arr = e;
                } else if (fd.kind == LCE_F_ARR_GOAL) {
                    lce_ai_goal *g = (lce_ai_goal *)calloc(n, sizeof *g);
                    for (uint32_t i = 0; g && i < n; ++i) { g[i].kind = (uint16_t)a[4 * i]; g[i].priority = (uint16_t)a[4 * i + 1]; g[i].speed_milli = a[4 * i + 2]; g[i].param = a[4 * i + 3] ? v->strs + a[4 * i + 3] : nullptr; }
                    arr = g;
                } else if (fd.kind == LCE_F_ARR_DROP) {
                    lce_drop *d = (lce_drop *)calloc(n, sizeof *d);
                    for (uint32_t i = 0; d && i < n; ++i) { d[i].item = a[5 * i] ? v->strs + a[5 * i] : nullptr; d[i].min = (uint8_t)a[5 * i + 1]; d[i].max = (uint8_t)a[5 * i + 2]; d[i].chance_percent = (uint8_t)a[5 * i + 3]; d[i].looting_bonus = (uint8_t)a[5 * i + 4]; }
                    arr = d;
                } else {
                    const char **r = (const char **)calloc(n, sizeof *r);
                    for (uint32_t i = 0; r && i < n; ++i) r[i] = a[i] ? v->strs + a[i] : nullptr;
                    arr = r;
                }
                if (!arr) { st = LCE_E_FULL; break; }
                temps[ntemp++] = arr;
                *(void **)dst = arr;
                break;
            }
            case LCE_F_ARR_U16_XYZ: {
                const lce_structure_desc *sd = (const lce_structure_desc *)desc;  // size fields precede blocks in the schema
                sx = sd->size_x; sy = sd->size_y; sz = sd->size_z;
                uint64_t n = (uint64_t)sx * sy * sz;
                uint32_t idx = slot[si]; ++si;
                if (!n || n > 64u * 64u * 64u || idx > v->arr_words || n > v->arr_words - idx) { st = LCE_E_INVALID; break; }
                for (uint64_t i = 0; i < n; ++i) if (v->arrs[idx + i] > 0xffff) { st = LCE_E_INVALID; break; }
                if (st != LCE_OK || !build) break;
                if (ntemp >= 16) { st = LCE_E_FULL; break; }
                uint16_t *b = (uint16_t *)malloc((size_t)n * 2);
                if (!b) { st = LCE_E_FULL; break; }
                for (uint64_t i = 0; i < n; ++i) b[i] = (uint16_t)v->arrs[idx + i];
                temps[ntemp++] = b;
                *(uint16_t **)dst = b;
                break;
            }
            default: st = LCE_E_INVALID; break;
            }
        }
        if (st == LCE_OK && build) st = build(user, type, desc);
        for (uint32_t i = 0; i < ntemp; ++i) free(temps[i]);
        if (st != LCE_OK) return st;
        pos += 2 + slots;
    }
    if (count) *count = records;
    return LCE_OK;
}

namespace {
const char *opt(const lce_package_view *v, lce_pkg_str r) { const char *s = lce_package_str(v, r); return *s ? s : nullptr; }

lce_status fail(char *err, size_t cap, const char *what, uint32_t idx, lce_status st) {
    if (err && cap) snprintf(err, cap, "%s #%u rejected (%d)", what, (unsigned)idx, (int)st);
    return st;
}
}  // namespace

extern "C" lce_status lce_package_register(const lce_package_view *v, const lce_api *api, lce_mod_ctx *ctx, char *err, size_t cap) {
    lce_status st;
    for (uint32_t i = 0; i < v->tex_count; ++i) {
        const lce_pkg_tex &t = v->tex[i];
        lce_texture_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, t.key); d.width = t.width; d.height = t.height;
        d.rgba = v->blob + t.data_offset; d.atlas = t.atlas;
        if ((st = api->register_texture(ctx, &d)) != LCE_OK) return fail(err, cap, "texture", i, st);
    }
    for (uint32_t i = 0; i < v->tab_count; ++i) {
        const lce_pkg_tab &t = v->tabs[i];
        lce_tab_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, t.key); d.name = lce_package_str(v, t.name); d.icon_item = lce_package_str(v, t.icon_item);
        d.creative = t.creative; d.crafting = t.crafting;
        if ((st = api->register_tab(ctx, &d)) != LCE_OK) return fail(err, cap, "tab", i, st);
    }
    for (uint32_t i = 0; i < v->block_count; ++i) {
        const lce_pkg_block &b = v->blocks[i];
        lce_block_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, b.key); d.name = lce_package_str(v, b.name);
        for (int f = 0; f < 6; ++f) d.texture[f] = opt(v, b.texture[f]);
        d.drop = opt(v, b.drop); d.creative_tab = opt(v, b.creative_tab);
        d.hardness_milli = b.hardness_milli; d.resistance_milli = b.resistance_milli;
        d.material = b.material; d.sound = b.sound; d.light_emission = b.light_emission;
        d.harvest_tier = b.harvest_tier; d.drop_min = b.drop_min; d.drop_max = b.drop_max; d.flags = b.flags;
        if (v->blocks2) {
            const lce_pkg_block2 &x = v->blocks2[i];
            d.block_entity_slots = x.storage_slots; d.placement = x.placement; d.flammability = x.flammability;
            d.slipperiness_milli = x.slipperiness_milli; d.harvest_tool = opt(v, x.harvest_tool);
        }
        const char *vn[16] = {nullptr}, *vt[96] = {nullptr};   // the loader deep-copies them in register_block
        uint8_t bx[16 * 8 * 6];
        if (v->blocks3) {
            const lce_pkg_block3 &x = v->blocks3[i];
            d.variant_count = x.variant_count; d.shape = x.shape; memcpy(d.box, x.box, 6);
            if (x.names_index != ~0u) { for (uint32_t k = 0; k < x.variant_count; ++k) vn[k] = opt(v, v->arrs[x.names_index + k]); d.variant_names = vn; }
            if (x.textures_index != ~0u) { for (uint32_t k = 0; k < 6u * x.variant_count; ++k) vt[k] = opt(v, v->arrs[x.textures_index + k]); d.variant_textures = vt; }
        }
        if (v->blocks4 && v->blocks4[i].box_count) {   // API 1.11: unpack 6 x 5 bits per box (the loader validates the ranges)
            const lce_pkg_block4 &x = v->blocks4[i];
            const uint32_t n = (uint32_t)x.box_count * x.box_sets;
            for (uint32_t k = 0; k < n; ++k)
                for (int a = 0; a < 6; ++a) bx[k * 6 + a] = (uint8_t)(v->arrs[x.boxes_index + k] >> (5 * a) & 31);
            d.box_count = x.box_count; d.box_sets = x.box_sets; d.boxes = bx;
        }
        if ((st = api->register_block(ctx, &d)) != LCE_OK) return fail(err, cap, "block", i, st);
    }
    for (uint32_t i = 0; i < v->item_count; ++i) {
        const lce_pkg_item &it = v->items[i];
        lce_item_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, it.key); d.name = lce_package_str(v, it.name); d.texture = lce_package_str(v, it.texture);
        d.creative_tab = opt(v, it.creative_tab); d.kind = it.kind; d.max_stack = it.max_stack; d.max_damage = it.max_damage;
        d.tier = it.tier; d.food_hunger = it.food_hunger; d.food_saturation_milli = it.food_saturation_milli;
        d.flags = it.flags; d.attack_bonus_milli = it.attack_bonus_milli;
        lce_effect_ref fx[8];
        if (v->items2) {
            const lce_pkg_item2 &x = v->items2[i];
            d.armor_points = x.armor_points; d.enchantability = x.enchantability;
            d.armor_texture = opt(v, x.armor_texture); d.repair_item = opt(v, x.repair_item);
            d.fuel_ticks = x.fuel_ticks; d.use_ticks = x.use_ticks; d.mining_speed_milli = x.mining_speed_milli;
            for (uint32_t k = 0; k < x.effects_count; ++k) {
                const uint32_t *a = v->arrs + x.effects_index + 4 * k;
                fx[k].effect = lce_package_str(v, a[0]); fx[k].duration_ticks = a[1]; fx[k].amplifier = (uint8_t)a[2]; fx[k].chance_percent = (uint8_t)a[3];
                fx[k].reserved[0] = fx[k].reserved[1] = 0;
            }
            d.food_effects = x.effects_count ? fx : nullptr; d.food_effect_count = x.effects_count;
        }
        lce_effect_ref px[8];
        if (v->items3) {   // API 1.9 throwables (the loader validates ranges and the THROWABLE flag)
            const lce_pkg_item3 &x = v->items3[i];
            d.projectile_speed_milli = x.speed_milli; d.projectile_inaccuracy_milli = x.inaccuracy_milli;
            d.projectile_damage_milli = x.damage_milli; d.projectile_fire_ticks = x.fire_ticks; d.projectile_explode_milli = x.explode_milli;
            for (uint32_t k = 0; k < x.effects_count; ++k) {
                const uint32_t *a = v->arrs + x.effects_index + 4 * k;
                px[k].effect = lce_package_str(v, a[0]); px[k].duration_ticks = a[1]; px[k].amplifier = (uint8_t)a[2]; px[k].chance_percent = (uint8_t)a[3];
                px[k].reserved[0] = px[k].reserved[1] = 0;
            }
            d.projectile_effects = x.effects_count ? px : nullptr; d.projectile_effect_count = x.effects_count;
            d.boat_type = x.boat_type;   // API 1.10 (the loader checks it against LCE_ITEM_FLAG_BOAT)
        }
        if ((st = api->register_item(ctx, &d)) != LCE_OK) return fail(err, cap, "item", i, st);
    }
    for (uint32_t i = 0; i < v->recipe_count; ++i) {
        const lce_pkg_recipe &r = v->recipes[i];
        lce_recipe_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, r.key); d.width = r.width; d.height = r.height; d.shapeless = r.shapeless;
        for (int c = 0; c < 9; ++c) { d.cells[c].item = opt(v, r.cells[c].item); d.cells[c].aux = r.cells[c].aux; }
        d.result = lce_package_str(v, r.result); d.result_count = r.result_count; d.result_aux = r.result_aux;
        d.craft_tab = opt(v, r.craft_tab);
        if ((st = api->register_recipe(ctx, &d)) != LCE_OK) return fail(err, cap, "recipe", i, st);
    }
    for (uint32_t i = 0; i < v->smelt_count; ++i) {
        const lce_pkg_smelt &s = v->smelts[i];
        lce_smelt_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, s.key); d.input.item = lce_package_str(v, s.input.item); d.input.aux = s.input.aux;
        d.result = lce_package_str(v, s.result); d.result_count = s.result_count; d.result_aux = s.result_aux; d.xp_milli = s.xp_milli;
        if ((st = api->register_smelting(ctx, &d)) != LCE_OK) return fail(err, cap, "smelting", i, st);
    }
    for (uint32_t i = 0; i < v->lang_count; ++i) {
        const lce_pkg_lang &l = v->lang[i];
        lce_string_desc d{}; d.struct_size = sizeof d;
        d.locale = lce_package_str(v, l.locale); d.key = lce_package_str(v, l.key); d.value = lce_package_str(v, l.value);
        if ((st = api->register_string(ctx, &d)) != LCE_OK) return fail(err, cap, "string", i, st);
    }
    for (uint32_t i = 0; i < v->entity_count; ++i) {
        const lce_pkg_entity &e = v->entities[i];
        lce_entity_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, e.key); d.base_mob = lce_package_str(v, e.base_mob); d.texture = opt(v, e.texture);
        d.max_health_milli = e.max_health_milli; d.spawn_weight = e.spawn_weight;
        if ((st = api->register_entity(ctx, &d)) != LCE_OK) return fail(err, cap, "entity", i, st);
    }
    for (uint32_t i = 0; i < v->ore_count; ++i) {
        const lce_pkg_ore &o = v->ores[i];
        lce_ore_desc d{}; d.struct_size = sizeof d;
        d.key = lce_package_str(v, o.key); d.block = lce_package_str(v, o.block); d.replace = opt(v, o.replace);
        d.vein_size = o.vein_size; d.veins_per_chunk = o.veins_per_chunk; d.min_y = o.min_y; d.max_y = o.max_y; d.dimension = o.dimension;
        if ((st = api->register_ore(ctx, &d)) != LCE_OK) return fail(err, cap, "ore", i, st);
    }
    if (v->ext_count) {
        struct Ctx { const lce_api *api; lce_mod_ctx *ctx; uint32_t index; } c{api, ctx, 0};
        auto build = [](void *u, uint32_t type, const void *desc) -> lce_status {
            Ctx *c = (Ctx *)u;
            const lce_api *a = c->api;
            ++c->index;
            switch (type) {
            case LCE_EXT_EFFECT: return a->register_effect(c->ctx, (const lce_effect_desc *)desc);
            case LCE_EXT_POTION: return a->register_potion(c->ctx, (const lce_potion_desc *)desc);
            case LCE_EXT_BREW: return a->register_brewing(c->ctx, (const lce_brew_desc *)desc);
            case LCE_EXT_ENCHANT: return a->register_enchantment(c->ctx, (const lce_enchant_desc *)desc);
            case LCE_EXT_FUEL: return a->register_fuel(c->ctx, (const lce_fuel_desc *)desc);
            case LCE_EXT_ENTITY: return a->register_entity2(c->ctx, (const lce_entity_desc2 *)desc);
            case LCE_EXT_FEATURE: return a->register_feature(c->ctx, (const lce_feature_desc *)desc);
            case LCE_EXT_BIOME: return a->register_biome(c->ctx, (const lce_biome_desc *)desc);
            case LCE_EXT_STRUCTURE: return a->register_structure(c->ctx, (const lce_structure_desc *)desc);
            case LCE_EXT_SOUND: return a->register_sound(c->ctx, (const lce_sound_desc *)desc);
            case LCE_EXT_PARTICLE: return a->register_particle(c->ctx, (const lce_particle_desc *)desc);
            default: return LCE_E_INVALID;
            }
        };
        if ((st = lce_package_ext_walk(v, build, &c, nullptr)) != LCE_OK) return fail(err, cap, "ext record", c.index, st);
    }
    return LCE_OK;
}
