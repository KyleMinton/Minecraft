#pragma once

#include "ItemStack.h"
#include "Block.h"

#include <string>

class World;
class Player;

// Every item in Minecraft Beta 1.8, with Beta's item IDs. Blocks are items too:
// a block's item ID is its block ID.
enum ItemId : uint16_t {
    ITEM_IRON_SHOVEL = 256, ITEM_IRON_PICKAXE, ITEM_IRON_AXE, ITEM_FLINT_AND_STEEL, ITEM_APPLE, ITEM_BOW,
    ITEM_ARROW, ITEM_COAL, ITEM_DIAMOND, ITEM_IRON_INGOT, ITEM_GOLD_INGOT, ITEM_IRON_SWORD, ITEM_WOODEN_SWORD,
    ITEM_WOODEN_SHOVEL, ITEM_WOODEN_PICKAXE, ITEM_WOODEN_AXE, ITEM_STONE_SWORD, ITEM_STONE_SHOVEL,
    ITEM_STONE_PICKAXE, ITEM_STONE_AXE, ITEM_DIAMOND_SWORD, ITEM_DIAMOND_SHOVEL, ITEM_DIAMOND_PICKAXE,
    ITEM_DIAMOND_AXE, ITEM_STICK, ITEM_BOWL, ITEM_MUSHROOM_STEW, ITEM_GOLDEN_SWORD, ITEM_GOLDEN_SHOVEL,
    ITEM_GOLDEN_PICKAXE, ITEM_GOLDEN_AXE, ITEM_STRING, ITEM_FEATHER, ITEM_GUNPOWDER, ITEM_WOODEN_HOE,
    ITEM_STONE_HOE, ITEM_IRON_HOE, ITEM_DIAMOND_HOE, ITEM_GOLDEN_HOE, ITEM_SEEDS, ITEM_WHEAT, ITEM_BREAD,
    ITEM_LEATHER_HELMET, ITEM_LEATHER_CHESTPLATE, ITEM_LEATHER_LEGGINGS, ITEM_LEATHER_BOOTS,
    ITEM_CHAIN_HELMET, ITEM_CHAIN_CHESTPLATE, ITEM_CHAIN_LEGGINGS, ITEM_CHAIN_BOOTS,
    ITEM_IRON_HELMET, ITEM_IRON_CHESTPLATE, ITEM_IRON_LEGGINGS, ITEM_IRON_BOOTS,
    ITEM_DIAMOND_HELMET, ITEM_DIAMOND_CHESTPLATE, ITEM_DIAMOND_LEGGINGS, ITEM_DIAMOND_BOOTS,
    ITEM_GOLDEN_HELMET, ITEM_GOLDEN_CHESTPLATE, ITEM_GOLDEN_LEGGINGS, ITEM_GOLDEN_BOOTS,
    ITEM_FLINT, ITEM_RAW_PORKCHOP, ITEM_COOKED_PORKCHOP, ITEM_PAINTING, ITEM_GOLDEN_APPLE, ITEM_SIGN,
    ITEM_WOODEN_DOOR, ITEM_BUCKET, ITEM_WATER_BUCKET, ITEM_LAVA_BUCKET, ITEM_MINECART, ITEM_SADDLE,
    ITEM_IRON_DOOR, ITEM_REDSTONE, ITEM_SNOWBALL, ITEM_BOAT, ITEM_LEATHER, ITEM_MILK_BUCKET, ITEM_BRICK,
    ITEM_CLAY_BALL, ITEM_REEDS, ITEM_PAPER, ITEM_BOOK, ITEM_SLIMEBALL, ITEM_CHEST_MINECART,
    ITEM_FURNACE_MINECART, ITEM_EGG, ITEM_COMPASS, ITEM_FISHING_ROD, ITEM_CLOCK, ITEM_GLOWSTONE_DUST,
    ITEM_RAW_FISH, ITEM_COOKED_FISH, ITEM_DYE, ITEM_BONE, ITEM_SUGAR, ITEM_CAKE, ITEM_BED, ITEM_REPEATER,
    ITEM_COOKIE, ITEM_MAP, ITEM_SHEARS, ITEM_MELON, ITEM_PUMPKIN_SEEDS, ITEM_MELON_SEEDS, ITEM_RAW_BEEF,
    ITEM_STEAK, ITEM_RAW_CHICKEN, ITEM_COOKED_CHICKEN, ITEM_ROTTEN_FLESH, ITEM_ENDER_PEARL,
    ITEM_LAST_REGULAR = ITEM_ENDER_PEARL,
    ITEM_RECORD_13 = 2256, ITEM_RECORD_CAT = 2257,
};

// Dye colors, as the dye item's damage value (ItemDye).
enum DyeColor { DYE_BLACK = 0, DYE_RED, DYE_GREEN, DYE_BROWN, DYE_BLUE, DYE_PURPLE, DYE_CYAN, DYE_LIGHT_GRAY,
                DYE_GRAY, DYE_PINK, DYE_LIME, DYE_YELLOW, DYE_LIGHT_BLUE, DYE_MAGENTA, DYE_ORANGE, DYE_WHITE };

enum class ToolType : uint8_t { None, Shovel, Pickaxe, Axe, Sword, Hoe, Shears };

// Where an item's icon comes from: items.png, or (for block items) terrain.png.
enum class IconSheet : uint8_t { Items, Terrain };

// One kind of item (Minecraft's Item class).
class ItemDef {
public:
    uint16_t id = 0;
    std::string name;
    int icon = 0;              // tile in items.png
    int maxStack = 64;
    int maxDamage = 0;         // durability; 0 = doesn't wear out
    ToolType tool = ToolType::None;
    int toolTier = 0;          // harvest level: 0 wood/gold, 1 stone, 2 iron, 3 diamond
    float efficiency = 1.0f;   // mining speed on blocks it's good for
    int foodHunger = 0;        // > 0 for food
    float foodSaturation = 0.0f;
    bool alwaysEdible = false;
    int armorSlot = -1;        // 0 helmet, 1 chestplate, 2 leggings, 3 boots
    int armorPoints = 0;

    virtual ~ItemDef() = default;
    virtual std::string DisplayName(int damage) const { return name; }
    virtual int Icon(int damage) const { return icon; }
    virtual IconSheet Sheet() const { return IconSheet::Items; }
    virtual Color Tint(int damage) const { return WHITE; }
    // Right click on a block's face; returns true if it did something.
    virtual bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const { return false; }
    // Right click anywhere (buckets); returns true if it did something.
    virtual bool UseInAir(ItemStack& stack, Player& player, World& world) const { return false; }
    bool IsFood() const { return foodHunger > 0; }
    // A tool drawn upright in the hand, like Minecraft's isFull3D (swords, tools, sticks, bows).
    virtual bool HeldLikeTool() const { return tool != ToolType::None && tool != ToolType::Shears; }
};

// The item definition for an ID (blocks included), or nullptr for an unknown ID.
const ItemDef* GetItemDef(int id);
int MaxStackSize(const ItemStack& stack);
std::string ItemName(const ItemStack& stack);
bool IsBlockItem(int id);

// Mining: how fast `held` breaks `block` (ItemTool.getStrVsBlock), and whether it
// gets the drops (canHarvestBlock).
float ToolSpeed(const ItemStack& held, uint8_t block, int meta);
bool CanHarvest(const ItemStack& held, uint8_t block);
// Uses up durability; returns false if the tool broke.
bool DamageItem(ItemStack& stack, int amount);

// Furnaces (TileEntityFurnace.getItemBurnTime / FurnaceRecipes).
int FuelTicks(const ItemStack& stack);
ItemStack SmeltingResult(const ItemStack& input);

// Places a block the way ItemBlock does (the face clicked, replaceable blocks,
// the block's own placement rules); used by block items and seeds, doors, beds...
bool PlaceBlockFromItem(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side, int blockId, int meta);
