// Glue between the composite retail195 backend and modules/events195 +
// services195. Kept in its own translation unit because events195's
// lce195_natives (natives195.h) and backend195's lce195_natives are different
// structs that must never meet in one TU.
#include <stdio.h>
#include <string.h>
#include "../loader.h"
#include "../../modules/events195/ev195.h"
#include "../../modules/events195/services195.h"

extern "C" lce_status lce195g_check_sound(const lce_sound_desc *d, char *err, uint32_t cap);
extern "C" int lce195g_register_plan(const lce_plan *plan, char *err, uint32_t cap);
extern "C" int lce195g_services_install(lce_loader *l, uintptr_t base, const void *world_ops);

namespace {
void set_err(char *err, uint32_t cap, const char *m) { if (err && cap) snprintf(err, cap, "%s", m); }
// "modid:name" -> modid (static buffer per call site)
bool modid_of(const char *key, char *out, size_t cap) {
    const char *c = key ? strchr(key, ':') : nullptr;
    if (!c || (size_t)(c - key) >= cap || c == key) return false;
    memcpy(out, key, (size_t)(c - key));
    out[c - key] = 0;
    return true;
}
lce195_l10n_entry g_strings[LCE_LOADER_MAX_STRINGS];
lce_loader *g_loader;
int32_t res_item(void *, const char *k) { return lce_loader_item_id(g_loader, k); }
int32_t res_block(void *, const char *k) { return lce_loader_block_id(g_loader, k); }
}  // namespace

extern "C" lce_status lce195g_check_sound(const lce_sound_desc *d, char *err, uint32_t cap) {
    if (!d) { set_err(err, cap, "events195: null sound"); return LCE_E_INVALID; }
    if (d->pcm || d->pcm_bytes) { set_err(err, cap, "events195: custom PCM audio is not possible on 1.95 (sound bank format not implemented, gap G11); use clone_of a vanilla sound event"); return LCE_E_UNSUPPORTED; }
    if (!d->clone_of || lce195_sound_id(d->clone_of) < 0) { set_err(err, cap, "events195: clone_of is not a 1.95 sound event (sound_ids_195.inc)"); return LCE_E_UNSUPPORTED; }
    float vol = d->volume_milli ? d->volume_milli / 1000.0f : 1.0f, pitch = d->pitch_milli ? d->pitch_milli / 1000.0f : 1.0f;
    if (vol <= 0 || vol > 4 || pitch < 0.5f || pitch > 2.0f) { set_err(err, cap, "events195: sound volume 0..4, pitch 0.5..2"); return LCE_E_INVALID; }
    return LCE_OK;
}

extern "C" int lce195g_register_plan(const lce_plan *p, char *err, uint32_t cap) {
    if (!p) return 0;
    char mod[LCE_MAX_MODID];
    for (uint32_t i = 0; i < p->ext_count; ++i) {
        const lce_plan_ext &x = p->ext[i];
        if (x.type == LCE_EXT_SOUND) {
            const lce_sound_desc *d = (const lce_sound_desc *)x.desc;
            if (d->pcm || d->pcm_bytes) continue;   // custom audio: audio195 (late phase) registers the event and its key
            char e[128] = {0};
            if (!modid_of(d->key, mod, sizeof mod) || lce195_sound_register(mod, d, e, sizeof e) != 1) { snprintf(err, cap, "sound %s: %s", d->key ? d->key : "?", e); return 0; }
        } else if (x.type == LCE_EXT_COMMAND) {
            const lce_command_desc *d = (const lce_command_desc *)x.desc;
            if (!modid_of(x.key, mod, sizeof mod) || lce195_cmd_register(mod, d) != 1) { snprintf(err, cap, "command %s refused (name taken or invalid)", x.key ? x.key : "?"); return 0; }
        } else if (x.type == LCE_EXT_KEYBIND) {
            const lce_keybind_desc *d = (const lce_keybind_desc *)x.desc;
            if (lce195_keys_register(d) != 1) { snprintf(err, cap, "keybind %s refused (chord taken or invalid)", x.key ? x.key : "?"); return 0; }
        }
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < p->string_count && n < LCE_LOADER_MAX_STRINGS; ++i) g_strings[n++] = lce195_l10n_entry{p->strings[i].locale, p->strings[i].key, p->strings[i].value};
    if (n && lce195_l10n_load(g_strings, n) != 1) { set_err(err, cap, "localisation table full"); return 0; }
    return 1;
}

extern "C" int lce195g_services_install(lce_loader *l, uintptr_t base, const void *world_ops) {
    if (!l) return 0;
    g_loader = l;
    static lce195_natives n;
#ifdef LCE195_HOST_TEST
    // host tests inject natives through lce195g_services_install_test
    (void)base;
    return 0;
#else
    lce195_natives_bind_rvas(&n, base);
#endif
    static lce195_world_ops none;
    lce195_resolver r{nullptr, res_item, res_block};
    if (!lce195_services_init(&n, world_ops ? (const lce195_world_ops *)world_ops : &none, &r)) return 0;
    lce_api api;
    memset(&api, 0, sizeof api);
    api.struct_size = sizeof api;   // fill() reads only the entries that fit: without this nothing was installed
    lce195_services_fill(&api);
    api.capabilities = LCE_CAP_SERVICES;
    api.capabilities2 |= lce195_ev_capabilities2();
    lce_loader_set_services(l, &api);
    return 1;
}
