/* Canonical vanilla group keys accepted by mods, shared by all backends.
 *
 * Crafting groups match the 7 categories of the console-proven Mods crafting
 * tab (loader/modded_crafting_tabs_195 registry.h: TOOLS, MATERIALS, FOOD,
 * ARMOUR, TECH, TRANSPORT, MISC). Creative groups name the stock Creative
 * categories; the retail backend never inserts into stock Creative tabs (by
 * project rule) and places such items in the private Mods Creative group,
 * keeping the requested category only as metadata. */
#pragma once
#include <string.h>

enum { LCE_CRAFT_TOOLS = 0, LCE_CRAFT_MATERIALS, LCE_CRAFT_FOOD, LCE_CRAFT_ARMOUR, LCE_CRAFT_TECH, LCE_CRAFT_TRANSPORT, LCE_CRAFT_MISC, LCE_CRAFT_COUNT };
enum { LCE_CREATIVE_BUILDING = 0, LCE_CREATIVE_DECORATION, LCE_CREATIVE_REDSTONE, LCE_CREATIVE_TRANSPORT, LCE_CREATIVE_MATERIALS,
       LCE_CREATIVE_FOOD, LCE_CREATIVE_TOOLS, LCE_CREATIVE_BREWING, LCE_CREATIVE_MISC, LCE_CREATIVE_COUNT };

static inline int lce_group_lookup(const char *key, int crafting) {
    struct Row { const char *name; int craft, creative; };
    static const struct Row rows[] = {
        {"minecraft:tools", LCE_CRAFT_TOOLS, LCE_CREATIVE_TOOLS},
        {"minecraft:combat", LCE_CRAFT_TOOLS, LCE_CREATIVE_TOOLS},
        {"minecraft:weapons", LCE_CRAFT_TOOLS, LCE_CREATIVE_TOOLS},
        {"minecraft:materials", LCE_CRAFT_MATERIALS, LCE_CREATIVE_MATERIALS},
        {"minecraft:food", LCE_CRAFT_FOOD, LCE_CREATIVE_FOOD},
        {"minecraft:armour", LCE_CRAFT_ARMOUR, LCE_CREATIVE_TOOLS},
        {"minecraft:armor", LCE_CRAFT_ARMOUR, LCE_CREATIVE_TOOLS},
        {"minecraft:redstone", LCE_CRAFT_TECH, LCE_CREATIVE_REDSTONE},
        {"minecraft:mechanisms", LCE_CRAFT_TECH, LCE_CREATIVE_REDSTONE},
        {"minecraft:transportation", LCE_CRAFT_TRANSPORT, LCE_CREATIVE_TRANSPORT},
        {"minecraft:transport", LCE_CRAFT_TRANSPORT, LCE_CREATIVE_TRANSPORT},
        {"minecraft:building_blocks", LCE_CRAFT_MISC, LCE_CREATIVE_BUILDING},
        {"minecraft:structures", LCE_CRAFT_MISC, LCE_CREATIVE_BUILDING},
        {"minecraft:decorations", LCE_CRAFT_MISC, LCE_CREATIVE_DECORATION},
        {"minecraft:brewing", LCE_CRAFT_MISC, LCE_CREATIVE_BREWING},
        {"minecraft:misc", LCE_CRAFT_MISC, LCE_CREATIVE_MISC},
    };
    if (!key) return -1;
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; ++i)
        if (!strcmp(rows[i].name, key)) return crafting ? rows[i].craft : rows[i].creative;
    return -1;
}
