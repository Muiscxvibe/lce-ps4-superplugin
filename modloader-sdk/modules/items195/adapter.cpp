// Core plan entries -> items195 publisher descriptors.
#include "items195.h"
#include <string.h>

namespace {
bool rd(lce195x_env *e, uintptr_t a, void *v, size_t n) { return a && e->m.held(e->m.ctx) == 1 && e->m.read(e->m.ctx, a, v, n) == 1; }
bool ingredient(lce195x_env *e, const lce_plan_ingredient &g, lce195x_ingredient *out) {
    if (g.id <= 0 || g.id > 65535) return false;
    uintptr_t item = e->n.item_by_id((unsigned)g.id);
    if (!item) return false;
    out->id = (uint32_t)g.id; out->aux = g.aux; out->item = item;
    return true;
}
}  // namespace

extern "C" int lce195x_recipe_from_plan(lce195x_env *e, const lce_plan_recipe *r, const char *provider, lce195x_recipe *out) {
    if (!e || !r || !r->desc || !out || !provider || r->result_id <= 0 || r->result_id > 65535) return 0;
    const lce_recipe_desc *d = r->desc;
    memset(out, 0, sizeof *out);
    out->abi = LCE195X_RECIPE_ABI; out->size = sizeof *out; out->provider = provider; out->key = d->key;
    out->output_id = (uint32_t)r->result_id; out->output = e->n.item_by_id(out->output_id);
    out->output_count = d->result_count ? d->result_count : 1; out->output_aux = d->result_aux < 0 ? 0 : d->result_aux;
    uint32_t cls[2] = {0, 0};
    if (!out->output || !rd(e, out->output + 0x28, cls, 8) || cls[0] > 255) return 0;
    out->classification = cls[0];
    out->group = 0;   // list 0 / group field as the r72 publisher (console postcheck)
    out->category = r->craft_group >= 0 && r->craft_group < 7 ? (uint32_t)r->craft_group : 6u;
    out->shared_output = 1;   // native model: buckets hold several recipes (STATUS.md)
    if (d->width < 1 || d->width > 3 || d->height < 1 || d->height > 3) return 0;
    if (d->shapeless) {
        out->shapeless = 1;
        unsigned n = 0;
        for (unsigned c = 0; c < 9; c++) {
            if (r->cells[c].id < 0) continue;
            if (n >= 9 || !ingredient(e, r->cells[c], &out->cells[n])) return 0;
            ++n;
        }
        if (!n) return 0;
        out->ingredient_count = n;
    } else {
        out->width = d->width; out->height = d->height;
        for (unsigned y = 0; y < d->height; y++)
            for (unsigned x = 0; x < d->width; x++) {
                const lce_plan_ingredient &g = r->cells[y * 3 + x];
                if (g.id < 0) continue;
                if (!ingredient(e, g, &out->cells[y * d->width + x])) return 0;
            }
    }
    return 1;
}

extern "C" int lce195x_smelt_from_plan(lce195x_env *e, const lce_plan_smelt *s, lce195x_smelt *out) {
    if (!e || !s || !s->desc || !out || s->input.id <= 0 || s->result_id <= 0) return 0;
    memset(out, 0, sizeof *out);
    out->input_item = e->n.item_by_id((unsigned)s->input.id); out->input_aux = s->input.aux;
    out->output_item = e->n.item_by_id((unsigned)s->result_id);
    out->output_count = s->desc->result_count ? s->desc->result_count : 1; out->output_aux = s->desc->result_aux < 0 ? 0 : s->desc->result_aux;
    out->xp = (float)s->desc->xp_milli / 1000.0f;
    out->mask = LCE195X_COOK_FURNACE;
    return out->input_item && out->output_item ? 1 : 0;
}

extern "C" int lce195x_brew_from_plan(lce195x_env *e, const lce_plan *plan, const lce_plan_ext *x, lce195x_brew *out) {
    if (!e || !plan || !plan->resolve || !x || x->type != LCE_EXT_BREW || !x->desc || !out) return 0;
    const lce_brew_desc *d = (const lce_brew_desc *)x->desc;
    if (lce195x_brew_check(d, nullptr, 0) != LCE_OK) return 0;
    int32_t id = plan->resolve(plan->resolve_ctx, LCE_REF_ITEM, d->reagent);
    if (id <= 0) return 0;
    out->from_potion = lce195x_brew_potion_id(d->input_potion);   // stock, or a published mod potion (effects195)
    out->to_potion = lce195x_brew_potion_id(d->output_potion);
    if (out->from_potion <= 0 || out->to_potion <= 0) return 0;
    out->reagent = e->n.item_by_id((unsigned)id);
    out->reagent_aux = -1;
    return out->reagent ? 1 : 0;
}
