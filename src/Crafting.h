#pragma once

#include "ItemStack.h"

// Beta 1.8's crafting recipes (CraftingManager and the Recipes* classes).
// `grid` is `size` x `size` (2 for the inventory, 3 for a crafting table), row-major.
// Returns the result, or an empty stack if nothing matches.
ItemStack CraftingResult(const ItemStack* grid, int size);

// What's left in a crafting slot after an item there is used up
// (Item.getContainerItem): buckets of milk, water or lava leave the bucket.
int CraftingContainerItem(int id);
