#pragma once

#include <cstdint>

// A pile of one kind of item. IDs follow Beta 1.8: below 256 is a block (the
// block's ID), 256 and up are items (see Items.h). `damage` is the block's
// metadata (wool color, wood type...) or a dye's color, or a tool's wear.
struct ItemStack {
    uint16_t id = 0;     // 0 = empty
    uint16_t damage = 0;
    int count = 0;
    int popTicks = 0;    // hotbar "squash" animation after picking this up (ItemStack.animationsToGo)

    ItemStack() = default;
    ItemStack(int itemId, int amount, int itemDamage = 0)
        : id((uint16_t)itemId), damage((uint16_t)itemDamage), count(amount) {}

    bool IsEmpty() const { return id == 0 || count <= 0; }
    void Clear() { *this = ItemStack(); }

    // Removes up to `amount` items and returns them as a new stack.
    ItemStack Split(int amount) {
        int taken = amount < count ? amount : count;
        ItemStack part(id, taken, damage);
        count -= taken;
        if (count <= 0) Clear();
        return part;
    }
};

// Same kind of item (and same variant), so the two can share a stack.
inline bool SameItem(const ItemStack& a, const ItemStack& b) {
    return !a.IsEmpty() && !b.IsEmpty() && a.id == b.id && a.damage == b.damage;
}
