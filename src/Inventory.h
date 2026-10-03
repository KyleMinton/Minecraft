#pragma once

#include "Items.h"
#include "ItemStack.h"

// The player's items, laid out like Minecraft's InventoryPlayer.
class Inventory {
public:
    static constexpr int HOTBAR_SIZE = 9;
    static constexpr int MAIN_SIZE = 36;  // 0-8 the hotbar, 9-35 the three rows above it
    static constexpr int ARMOR_SIZE = 4;  // helmet, chestplate, leggings, boots

    ItemStack main[MAIN_SIZE];
    ItemStack armor[ARMOR_SIZE];
    ItemStack offhand;
    ItemStack cursor;                     // what's carried on the mouse while an inventory screen is open

    // Stores as many as fit (InventoryPlayer.addItemStackToInventory): topping up
    // the held stack first, then any matching stack, then the first empty slot.
    // Items that don't stack (tools) only go into empty slots. Returns how many didn't fit.
    int Add(const ItemStack& stack, int selectedSlot);

    // Counts down the pickup animations (InventoryPlayer.decrementAnimations); once per tick.
    void TickAnimations();

    // Armor (InventoryPlayer.getTotalArmorValue / damageArmor).
    int TotalArmor() const;
    void DamageArmor(int damage);
};
