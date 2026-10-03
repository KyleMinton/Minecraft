#include "Inventory.h"

#include <algorithm>

namespace {

constexpr int PICKUP_POP_TICKS = 5;

} // namespace

int Inventory::Add(const ItemStack& stack, int selectedSlot) {
    if (stack.IsEmpty()) return 0;
    int count = stack.count;
    int maxStack = MaxStackSize(stack);
    const ItemDef* def = GetItemDef(stack.id);
    bool damageable = def != nullptr && def->maxDamage > 0;

    auto canTopUp = [&](const ItemStack& s) { return maxStack > 1 && !damageable && SameItem(s, stack) && s.count < maxStack; };

    while (count > 0) {
        ItemStack* target = nullptr;
        if (canTopUp(main[selectedSlot])) target = &main[selectedSlot];
        for (int i = 0; target == nullptr && i < MAIN_SIZE; i++) {
            if (canTopUp(main[i])) target = &main[i];
        }
        for (int i = 0; target == nullptr && i < MAIN_SIZE; i++) {
            if (main[i].IsEmpty()) {
                main[i] = ItemStack(stack.id, 0, stack.damage);
                target = &main[i];
            }
        }
        if (target == nullptr) break; // full

        int moved = std::min(count, maxStack - target->count);
        target->count += moved;
        target->popTicks = PICKUP_POP_TICKS;
        count -= moved;
    }
    return count;
}

void Inventory::TickAnimations() {
    for (ItemStack& stack : main) {
        if (stack.popTicks > 0) stack.popTicks--;
    }
}

int Inventory::TotalArmor() const {
    int total = 0;
    for (const ItemStack& piece : armor) {
        const ItemDef* def = piece.IsEmpty() ? nullptr : GetItemDef(piece.id);
        if (def != nullptr) total += def->armorPoints;
    }
    return total;
}

void Inventory::DamageArmor(int damage) {
    for (ItemStack& piece : armor) {
        if (piece.IsEmpty()) continue;
        DamageItem(piece, std::max(1, damage / 4));
    }
}
