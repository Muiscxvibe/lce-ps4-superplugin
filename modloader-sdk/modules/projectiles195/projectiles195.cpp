// projectiles195: see projectiles195.h for the mechanism and its limits.
#include "projectiles195.h"
#include <stdio.h>
#include <string.h>

namespace {
p195_module *g;   // one module per process (thunks)
// The kind pending on the throwing thread between p195_item_use and the stock shoot call.
uint32_t g_pending;      // kind + 1, 0 = none
uint64_t g_pending_tid;

void fail(p195_module *m, const char *s) { snprintf(m->last_error, sizeof m->last_error, "%s", s); }
bool rd(p195_module *m, uintptr_t a, void *v, size_t n) { return a && m->mem.read(m->mem.ctx, a, v, n) == 1; }
bool q(p195_module *m, uintptr_t a, uintptr_t *v) { return rd(m, a, v, 8); }
uint64_t tid(p195_module *m) { return m->mem.thread_id ? m->mem.thread_id(m->mem.ctx) : 0; }
int kind_of_item(const p195_module *m, uintptr_t self) {
    for (uint32_t k = 0; k < m->count; ++k) if (m->items[k] == self) return (int)k;
    return -1;
}
int kind_of_projectile(const p195_module *m, uintptr_t vptr) {
    for (uint32_t k = 0; k < m->count; ++k) if (vptr == (uintptr_t)&m->proj_vt[k][2]) return (int)k;
    return -1;
}
}  // namespace

extern "C" int p195_configure(p195_module *m, uintptr_t base, const p195_natives *n, const p195_memory *mem,
                              const p195_kind *kinds, uint32_t count, p195_hit_fn hit, void *hit_ctx) {
    if (!m) return 0;
    memset(m, 0, sizeof *m);
    if (g) { fail(m, "projectiles195: already configured"); return 0; }
    if (!base || !n || !n->item_use || !n->shoot || !n->on_hit || !mem || !mem->read || !mem->cas8 || !mem->patch_ro8 || !mem->thread_id) {
        fail(m, "projectiles195: natives/memory not bound"); return 0;
    }
    if (!count || count > P195_MAX_KINDS || !kinds) { fail(m, "projectiles195: 1..16 throwable kinds"); return 0; }
    for (uint32_t k = 0; k < count; ++k) {
        p195_kind x = kinds[k];
        if (!x.key[0] || memchr(x.key, 0, sizeof x.key) == nullptr) { fail(m, "projectiles195: bad key"); return 0; }
        if (x.speed == 0) x.speed = P195_STOCK_SPEED;
        if (x.inaccuracy == 0) x.inaccuracy = P195_STOCK_INACCURACY;
        if (!(x.speed >= 0.1f && x.speed <= 4.0f) || !(x.inaccuracy >= 0.0f && x.inaccuracy <= 20.0f)) {
            fail(m, "projectiles195: speed 0.1..4, inaccuracy 0..20"); return 0;
        }
        m->kinds[k] = x;
    }
    m->base = base; m->n = *n; m->mem = *mem; m->hit = hit; m->hit_ctx = hit_ctx; m->count = count;
    m->state = P195_READY;
    g = m;
    return 1;
}

extern "C" int p195_adopt_item(p195_module *m, uint32_t k, uintptr_t item) {
    if (!m || m->state == P195_IDLE || m->state == P195_UNCERTAIN || k >= m->count || !item || m->items[k]) return 0;
    uintptr_t vt = 0, cur[2 + P195_ITEM_SLOTS], stock[2 + P195_ITEM_SLOTS], snow[2 + P195_ITEM_SLOTS];
    if (!q(m, item, &vt) || vt < 16 || !rd(m, vt - 16, cur, sizeof cur) ||
        !rd(m, m->base + P195_RVA_ITEM_VT - 16, stock, sizeof stock) || !rd(m, m->base + P195_RVA_SNOWBALLITEM_VT - 16, snow, sizeof snow)) {
        fail(m, "throwable item: vtables unreadable"); return 0;
    }
    // the SDK item: a private copy of Item's vtable; only the name slot may differ (the use slot must be stock)
    for (unsigned i = 0; i < 2 + P195_ITEM_SLOTS; ++i)
        if (i != 2 + P195_SLOT_NAME / 8 && cur[i] != stock[i]) { fail(m, "throwable item: not a generic SDK item (vtable differs)"); return 0; }
    if (vt == m->base + P195_RVA_ITEM_VT) { fail(m, "throwable item: stock Item vtable (no private copy)"); return 0; }
    // pins: SnowballItem keeps Item's slots except class id/destructors/use
    if (snow[2 + P195_SLOT_USE / 8] != m->base + P195_RVA_SNOWBALLITEM_USE || stock[2 + P195_SLOT_USE / 8] != m->base + P195_RVA_ITEM_USE) {
        fail(m, "throwable item: SnowballItem/Item use pins differ"); return 0;
    }
    for (unsigned i = 2; i < 2 + P195_ITEM_SLOTS; ++i) {
        const unsigned off = (i - 2) * 8;
        if (off != 0x10 && off != 0x18 && off != 0x20 && off != P195_SLOT_USE && snow[i] != stock[i]) { fail(m, "throwable item: SnowballItem vtable pin"); return 0; }
    }
    uintptr_t *mine = m->item_vt[k];
    memcpy(mine, snow, sizeof snow);
    mine[2 + P195_SLOT_NAME / 8] = cur[2 + P195_SLOT_NAME / 8];
    mine[2 + P195_SLOT_USE / 8] = (uintptr_t)&p195_item_use;
    m->items[k] = item;
    if (m->mem.cas8(m->mem.ctx, item, vt, (uintptr_t)&mine[2]) != 1) { m->items[k] = 0; fail(m, "throwable item: vptr changed"); return 0; }
    return 1;
}

extern "C" int p195_bind_item(p195_module *m, uint32_t k, uintptr_t item) {
    if (!m || m->state != P195_READY || k >= m->count || !item || m->items[k]) return 0;
    for (uint32_t j = 0; j < m->count; ++j) if (m->items[j] == item) { fail(m, "throwable item: bound twice"); return 0; }
    uintptr_t vt = 0, cur[2 + P195_ITEM_SLOTS], snow[2 + P195_ITEM_SLOTS];
    if (!q(m, item, &vt) || vt < 16 || !rd(m, vt - 16, cur, sizeof cur) || !rd(m, m->base + P195_RVA_SNOWBALLITEM_VT - 16, snow, sizeof snow)) {
        fail(m, "throwable item: vtables unreadable"); return 0;
    }
    if (vt == m->base + P195_RVA_SNOWBALLITEM_VT || snow[2 + P195_SLOT_USE / 8] != m->base + P195_RVA_SNOWBALLITEM_USE) {
        fail(m, "throwable item: stock SnowballItem vtable or use pin differs"); return 0;
    }
    // a private SnowballItem copy: offset-to-top, RTTI, class id (+0x10) and destructors (+0x18/+0x20) are SnowballItem's.
    // Other slots are the SDK's own overrides (name, enchantability, repair, the events195 inventoryTick/use routes).
    static const unsigned kIdentity[] = {0, 1, 2 + 0x10 / 8, 2 + 0x18 / 8, 2 + 0x20 / 8};
    for (unsigned i : kIdentity)
        if (cur[i] != snow[i]) { fail(m, "throwable item: not a SnowballItem copy (RTTI/class id/destructors differ)"); return 0; }
    const uintptr_t use = cur[2 + P195_SLOT_USE / 8];
    if (use == m->base + P195_RVA_SNOWBALLITEM_USE || use == m->base + P195_RVA_ITEM_USE || !use) {
        fail(m, "throwable item: +0x40 does not reach p195_item_use"); return 0;
    }
    m->items[k] = item;
    return 1;
}

extern "C" int p195_install(p195_module *m) {
    if (!m || m->state != P195_READY) return 0;
    for (uint32_t k = 0; k < m->count; ++k) if (!m->items[k]) { fail(m, "install before every throwable item was adopted"); return 0; }
    uintptr_t vt[2 + P195_SNOWBALL_SLOTS + 1];
    const uintptr_t at = m->base + P195_RVA_SNOWBALL_VT;
    if (!rd(m, at - 16, vt, sizeof vt)) { fail(m, "Snowball vtable unreadable"); return 0; }
    if (vt[0] != 0 || !vt[1] || vt[2 + P195_SLOT_SHOOT / 8] != m->base + P195_RVA_SHOOT ||
        vt[2 + P195_SLOT_GETOWNER / 8] != m->base + P195_RVA_GETOWNER || vt[2 + P195_SLOT_ONHIT / 8] != m->base + P195_RVA_ONHIT ||
        (intptr_t)vt[2 + P195_SNOWBALL_SLOTS] != P195_SECONDARY_OTT) {
        fail(m, "Snowball vtable pins differ (shoot/getOwner/onHit/secondary)"); return 0;
    }
    for (uint32_t k = 0; k < m->count; ++k) {
        memcpy(m->proj_vt[k], vt, sizeof m->proj_vt[k]);
        m->proj_vt[k][2 + P195_SLOT_ONHIT / 8] = (uintptr_t)&p195_on_hit;
    }
    const uintptr_t slot = at + P195_SLOT_SHOOT;
    const int rc = m->mem.patch_ro8(m->mem.ctx, slot, m->base + P195_RVA_SHOOT, (uintptr_t)&p195_shoot_hook);
    if (rc == 1) { __atomic_store_n(&m->state, (uint32_t)P195_ARMED, __ATOMIC_RELEASE); return 1; }
    if (rc == 0) { fail(m, "Snowball shoot slot changed during install"); return 0; }
    __atomic_store_n(&m->state, (uint32_t)P195_UNCERTAIN, __ATOMIC_RELEASE);
    fail(m, "RELRO protection not restored after the shoot slot install");
    return -1;
}

extern "C" int p195_restore(p195_module *m) {
    if (!m || m->state != P195_ARMED) return 0;
    const int rc = m->mem.patch_ro8(m->mem.ctx, m->base + P195_RVA_SNOWBALL_VT + P195_SLOT_SHOOT, (uintptr_t)&p195_shoot_hook, m->base + P195_RVA_SHOOT);
    if (rc == 1) { __atomic_store_n(&m->state, (uint32_t)P195_READY, __ATOMIC_RELEASE); return 1; }
    fail(m, "restore: shoot slot not ours or protection not restored");
    return -1;
}

// ---- game-ABI thunks ----------------------------------------------------------------
extern "C" void *p195_item_use(void *sret, void *self, void *level, void *player_sp, int hand) {
    p195_module *m = g;
    const int k = m ? kind_of_item(m, (uintptr_t)self) : -1;
    if (k < 0) return m ? m->n.item_use(sret, self, level, player_sp, hand) : sret;   // not reached: only adopted items use it
    const uint64_t me = tid(m);
    uint32_t none = 0;
    const bool mine = me && __atomic_compare_exchange_n(&g_pending, &none, (uint32_t)k + 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (mine) __atomic_store_n(&g_pending_tid, me, __ATOMIC_RELEASE);
    void *r = m->n.item_use(sret, self, level, player_sp, hand);   // stock SnowballItem::use: client side throws nothing
    if (mine) { __atomic_store_n(&g_pending_tid, (uint64_t)0, __ATOMIC_RELEASE); __atomic_store_n(&g_pending, 0u, __ATOMIC_RELEASE); }
    __atomic_fetch_add(&m->throws, 1, __ATOMIC_RELAXED);
    return r;
}

extern "C" void p195_shoot_hook(void *self, void *sp, float x_rot, float y_rot, float z, float speed, float inaccuracy) {
    p195_module *m = g;
    const uint32_t pending = __atomic_load_n(&g_pending, __ATOMIC_ACQUIRE);
    if (m && pending && __atomic_load_n(&g_pending_tid, __ATOMIC_ACQUIRE) == tid(m)) {
        const uint32_t k = pending - 1;
        uintptr_t vptr = 0;
        // one projectile per use: the first stock Snowball shot on the throwing thread
        if (k < m->count && q(m, (uintptr_t)self, &vptr) && vptr == m->base + P195_RVA_SNOWBALL_VT &&
            m->mem.cas8(m->mem.ctx, (uintptr_t)self, vptr, (uintptr_t)&m->proj_vt[k][2]) == 1) {
            __atomic_store_n(&g_pending, 0u, __ATOMIC_RELEASE);
            __atomic_fetch_add(&m->swaps, 1, __ATOMIC_RELAXED);
            speed = m->kinds[k].speed; inaccuracy = m->kinds[k].inaccuracy;
        } else {
            __atomic_fetch_add(&m->foreign_shoots, 1, __ATOMIC_RELAXED);
        }
    }
    if (m) m->n.shoot(self, sp, x_rot, y_rot, z, speed, inaccuracy);
}

extern "C" uint8_t p195_on_hit(void *self, void *hit) {
    p195_module *m = g;
    if (!m) return 0;
    uintptr_t vptr = 0, level = 0, entity = 0;
    uint8_t client = 1;
    double pos[3] = {0, 0, 0};
    const int k = q(m, (uintptr_t)self, &vptr) ? kind_of_projectile(m, vptr) : -1;
    if (k >= 0 && m->hit && q(m, (uintptr_t)self + P195_F_LEVEL, &level) && level && rd(m, level + P195_F_LEVEL_CLIENT, &client, 1) && !client &&
        hit && q(m, (uintptr_t)hit + P195_F_HIT_ENTITY, &entity) && rd(m, (uintptr_t)self + P195_F_POS, pos, sizeof pos)) {
        __atomic_fetch_add(&m->hits, 1, __ATOMIC_RELAXED);
        m->hit(m->hit_ctx, (uint32_t)k, self, (void *)entity, pos[0], pos[1], pos[2]);
    }
    return m->n.on_hit(self, hit);
}

#ifndef P195_TEST
extern "C" void p195_natives_bind(p195_natives *n, uintptr_t base) {
    n->item_use = (void *(*)(void *, void *, void *, void *, int))(base + P195_RVA_SNOWBALLITEM_USE);
    n->shoot = (void (*)(void *, void *, float, float, float, float, float))(base + P195_RVA_SHOOT);
    n->on_hit = (uint8_t (*)(void *, void *))(base + P195_RVA_ONHIT);
}
#endif
