#include "Items.h"
#include "Player.h"
#include "SoundSystem.h"
#include "World.h"

#include <array>
#include <cmath>
#include <memory>

namespace {

// ---- Tool materials (EnumToolMaterial): harvest level, durability, speed.
struct ToolMaterial { int harvest; int uses; float efficiency; };
constexpr ToolMaterial TIER_WOOD{ 0, 59, 2.0f }, TIER_STONE{ 1, 131, 4.0f }, TIER_IRON{ 2, 250, 6.0f }, TIER_DIAMOND{ 3, 1561, 8.0f }, TIER_GOLD{ 0, 32, 12.0f };

// A block as an item (ItemBlock).
class BlockItemDef final : public ItemDef {
public:
    explicit BlockItemDef(int blockId) {
        id = (uint16_t)blockId;
        name = GetBlockDef((BlockId)blockId).name;
    }
    std::string DisplayName(int damage) const override { return GetBlockDef((BlockId)id).DisplayName(damage); }
    int Icon(int damage) const override { return GetBlockDef((BlockId)id).ItemIconTexture(damage); }
    IconSheet Sheet() const override { return IconSheet::Terrain; }
    Color Tint(int damage) const override { return GetBlockDef((BlockId)id).ItemTint(damage); }
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        // A slab onto the same slab's open side makes a double slab.
        if (world.GetBlock(x, y, z) == id && GetBlockDef((BlockId)id).CombineFromItem(world, x, y, z, side)) {
            stack.count--;
            return true;
        }
        if (world.GetBlock(x, y, z) == BLOCK_SNOW_LAYER) {
            side = SIDE_DOWN;
        } else {
            x += SIDE_OFFSET[side][0];
            y += SIDE_OFFSET[side][1];
            z += SIDE_OFFSET[side][2];
        }
        return PlaceBlockFromItem(stack, player, world, x, y, z, side, id, GetBlockDef((BlockId)id).MetaForPlacement(stack.damage));
    }
};

// Items that put a block in the world (ItemReed: sugar cane, cake, repeaters).
class PlacerItemDef final : public ItemDef {
public:
    int block = 0;
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        if (world.GetBlock(x, y, z) == BLOCK_SNOW_LAYER) {
            side = SIDE_DOWN;
        } else {
            x += SIDE_OFFSET[side][0];
            y += SIDE_OFFSET[side][1];
            z += SIDE_OFFSET[side][2];
        }
        return PlaceBlockFromItem(stack, player, world, x, y, z, side, block, 0);
    }
};

class RedstoneItemDef final : public ItemDef {
public:
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        if (world.GetBlock(x, y, z) != BLOCK_SNOW_LAYER) {
            x += SIDE_OFFSET[side][0];
            y += SIDE_OFFSET[side][1];
            z += SIDE_OFFSET[side][2];
            if (!world.IsAir(x, y, z)) return false;
        }
        if (!GetBlockDef(BLOCK_REDSTONE_WIRE).CanPlaceAt(world, x, y, z)) return true;
        world.SetBlock(x, y, z, BLOCK_REDSTONE_WIRE);
        if (!player.IsCreative()) stack.Split(1);
        return true;
    }
};

// Wheat, pumpkin and melon seeds go on top of farmland.
class SeedsItemDef final : public ItemDef {
public:
    int crop = BLOCK_WHEAT;
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        if (side != SIDE_UP) return false;
        if (world.GetBlock(x, y, z) != BLOCK_FARMLAND || !world.IsAir(x, y + 1, z)) return false;
        world.SetBlock(x, y + 1, z, crop);
        if (!player.IsCreative()) stack.Split(1);
        return true;
    }
};

// ItemDoor: puts both halves down, hinged away from walls and toward other doors.
class DoorItemDef final : public ItemDef {
public:
    int block = BLOCK_WOODEN_DOOR;
    IconSheet sheet = IconSheet::Items; // later doors' icons are in terrain.png
    IconSheet Sheet() const override { return sheet; }
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        if (side != SIDE_UP) return false;
        y++;
        const Block& door = GetBlockDef((BlockId)block);
        if (!door.CanPlaceAt(world, x, y, z)) return false;
        int facing = PlayerDoorFacing(player);
        int dx = 0, dz = 0;
        if (facing == 0) dz = 1;
        if (facing == 1) dx = -1;
        if (facing == 2) dz = -1;
        if (facing == 3) dx = 1;
        int wallsLeft = (world.IsNormalCube(x - dx, y, z - dz) ? 1 : 0) + (world.IsNormalCube(x - dx, y + 1, z - dz) ? 1 : 0);
        int wallsRight = (world.IsNormalCube(x + dx, y, z + dz) ? 1 : 0) + (world.IsNormalCube(x + dx, y + 1, z + dz) ? 1 : 0);
        bool doorLeft = world.GetBlock(x - dx, y, z - dz) == block || world.GetBlock(x - dx, y + 1, z - dz) == block;
        bool doorRight = world.GetBlock(x + dx, y, z + dz) == block || world.GetBlock(x + dx, y + 1, z + dz) == block;
        bool mirrored = (doorLeft && !doorRight) || wallsRight > wallsLeft;
        if (mirrored) facing = ((facing - 1) & 3) + 4;
        world.SetBlock(x, y, z, block, facing, false);
        world.SetBlock(x, y + 1, z, block, facing + 8, false);
        world.NotifyNeighbors(x, y, z, block);
        world.NotifyNeighbors(x, y + 1, z, block);
        if (!player.IsCreative()) stack.Split(1);
        return true;
    }
    static int PlayerDoorFacing(const Player& player) {
        return (int)std::floor((player.YawDegrees() + 180.0) * 4.0 / 360.0 - 0.5) & 3;
    }
};

// ItemBed: two blocks, the head in the direction you face.
class BedItemDef final : public ItemDef {
public:
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        if (side != SIDE_UP) return false;
        y++;
        int facing = (int)std::floor(player.YawDegrees() * 4.0 / 360.0 + 0.5) & 3;
        static const int head[4][2] = { { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 } };
        int hx = x + head[facing][0], hz = z + head[facing][1];
        if (world.IsAir(x, y, z) && world.IsAir(hx, y, hz) && world.IsNormalCube(x, y - 1, z) && world.IsNormalCube(hx, y - 1, hz)) {
            world.SetBlock(x, y, z, BLOCK_BED, facing);
            world.SetBlock(hx, y, hz, BLOCK_BED, facing + 8);
            if (!player.IsCreative()) stack.Split(1);
            return true;
        }
        return false;
    }
};

// ItemSign: a post on top of a block, or a sign on its side; then you write on it.
class SignItemDef final : public ItemDef {
public:
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        if (side == SIDE_DOWN) return false;
        if (!GetMaterialInfo(world.GetMaterial(x, y, z)).solid) return false;
        x += SIDE_OFFSET[side][0];
        y += SIDE_OFFSET[side][1];
        z += SIDE_OFFSET[side][2];
        if (!GetBlockDef(BLOCK_SIGN_POST).CanPlaceAt(world, x, y, z)) return false;
        if (side == SIDE_UP) world.SetBlock(x, y, z, BLOCK_SIGN_POST, (int)std::floor((player.YawDegrees() + 180.0) * 16.0 / 360.0 + 0.5) & 15);
        else world.SetBlock(x, y, z, BLOCK_WALL_SIGN, side);
        if (!player.IsCreative()) stack.Split(1);
        player.OpenContainer(ScreenKind::Sign, x, y, z);
        return true;
    }
};

class FlintAndSteelDef final : public ItemDef {
public:
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        x += SIDE_OFFSET[side][0];
        y += SIDE_OFFSET[side][1];
        z += SIDE_OFFSET[side][2];
        if (world.IsAir(x, y, z)) {
            world.PlaySound("fire.ignite", x + 0.5, y + 0.5, z + 0.5, 1.0f, world.Random().NextFloat() * 0.4f + 0.8f);
            world.SetBlock(x, y, z, BLOCK_FIRE);
        }
        if (!player.IsCreative()) DamageItem(stack, 1);
        return true;
    }
};

// ItemHoe: tills grass or dirt into farmland.
class HoeDef final : public ItemDef {
public:
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        int here = world.GetBlock(x, y, z);
        int above = world.GetBlock(x, y + 1, z);
        if ((side != SIDE_DOWN && above == BLOCK_AIR && here == BLOCK_GRASS) || here == BLOCK_DIRT) {
            const Block& farmland = GetBlockDef(BLOCK_FARMLAND);
            world.PlaySound(StepSoundEvent(farmland.sound), x + 0.5, y + 0.5, z + 0.5, (StepSoundVolume(farmland.sound) + 1.0f) / 2.0f,
                            StepSoundPitch(farmland.sound) * 0.8f);
            world.SetBlock(x, y, z, BLOCK_FARMLAND);
            if (!player.IsCreative()) DamageItem(stack, 1);
            return true;
        }
        return false;
    }
};

// ItemDye: bone meal grows things.
class DyeDef final : public ItemDef {
public:
    std::string DisplayName(int damage) const override {
        static const char* names[16] = { "Ink Sac", "Rose Red", "Cactus Green", "Cocoa Beans", "Lapis Lazuli", "Purple Dye",
                                         "Cyan Dye", "Light Gray Dye", "Gray Dye", "Pink Dye", "Lime Dye", "Dandelion Yellow",
                                         "Light Blue Dye", "Magenta Dye", "Orange Dye", "Bone Meal" };
        return names[damage & 15];
    }
    int Icon(int damage) const override {
        damage &= 15;
        return icon + (damage % 8) * 16 + damage / 8;
    }
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side) const override {
        if (stack.damage != DYE_WHITE) return false;
        int id = world.GetBlock(x, y, z);
        bool used = false;
        if (id == BLOCK_SAPLING || id == BLOCK_BROWN_MUSHROOM || id == BLOCK_RED_MUSHROOM || id == BLOCK_WHEAT ||
            id == BLOCK_PUMPKIN_STEM || id == BLOCK_MELON_STEM) {
            used = GetBlockDef((BlockId)id).Fertilize(world, x, y, z) || id == BLOCK_BROWN_MUSHROOM || id == BLOCK_RED_MUSHROOM;
        } else if (id == BLOCK_GRASS) {
            JavaRandom& random = world.Random();
            for (int attempt = 0; attempt < 128; attempt++) {
                int px = x, py = y + 1, pz = z;
                bool ok = true;
                for (int step = 0; step < attempt / 16; step++) {
                    px += random.NextInt(3) - 1;
                    py += (random.NextInt(3) - 1) * random.NextInt(3) / 2;
                    pz += random.NextInt(3) - 1;
                    if (world.GetBlock(px, py - 1, pz) != BLOCK_GRASS || world.IsNormalCube(px, py, pz)) { ok = false; break; }
                }
                if (!ok || !world.IsAir(px, py, pz)) continue;
                if (random.NextInt(10) != 0) world.SetBlock(px, py, pz, BLOCK_TALL_GRASS, 1);
                else if (random.NextInt(3) != 0) world.SetBlock(px, py, pz, BLOCK_DANDELION);
                else world.SetBlock(px, py, pz, BLOCK_ROSE);
            }
            used = true;
        }
        if (used && !player.IsCreative()) stack.Split(1);
        return used;
    }
};

// ItemBucket: scoops up source blocks and pours them out.
class BucketDef final : public ItemDef {
public:
    int contents = 0; // 0 empty, else the flowing liquid block
    bool UseInAir(ItemStack& stack, Player& player, World& world) const override {
        RaycastHit hit = world.Raycast(player.GetEyePosition(), player.LookVector(), 5.0f, contents == 0);
        if (!hit.hit) return false;
        if (contents == 0) {
            BlockMaterial material = world.GetMaterial(hit.x, hit.y, hit.z);
            if (world.GetMeta(hit.x, hit.y, hit.z) != 0) return false;
            int filled = 0;
            if (material == BlockMaterial::Water) filled = ITEM_WATER_BUCKET;
            else if (material == BlockMaterial::Lava) filled = ITEM_LAVA_BUCKET;
            if (filled == 0) return false;
            world.SetBlock(hit.x, hit.y, hit.z, BLOCK_AIR);
            if (!player.IsCreative()) stack = ItemStack(filled, 1);
            return true;
        }
        int x = hit.x + SIDE_OFFSET[hit.side][0], y = hit.y + SIDE_OFFSET[hit.side][1], z = hit.z + SIDE_OFFSET[hit.side][2];
        if (world.IsAir(x, y, z) || !GetMaterialInfo(world.GetMaterial(x, y, z)).solid) {
            world.SetBlock(x, y, z, contents, 0);
            if (!player.IsCreative()) stack = ItemStack(ITEM_BUCKET, 1);
            return true;
        }
        return false;
    }
};

class RecordDef final : public ItemDef {
public:
    bool UseOnBlock(ItemStack& stack, Player& player, World& world, int x, int y, int z, int) const override {
        if (world.GetBlock(x, y, z) != BLOCK_JUKEBOX || world.GetMeta(x, y, z) != 0) return false;
        TileEntity* te = world.GetTileEntity(x, y, z);
        if (te == nullptr || te->kind != TileKind::Jukebox) return false;
        static_cast<JukeboxTile*>(te)->record = id;
        world.SetMeta(x, y, z, 1);
        if (world.GetSounds() != nullptr) world.GetSounds()->PlayRecord(id == ITEM_RECORD_13 ? "13" : "cat", x, y, z);
        player.ShowMessage(id == ITEM_RECORD_13 ? "Now playing: C418 - 13" : "Now playing: C418 - cat");
        if (!player.IsCreative()) stack.Split(1);
        return true;
    }
};

class CoalDef final : public ItemDef {
public:
    std::string DisplayName(int damage) const override { return damage == 1 ? "Charcoal" : "Coal"; }
};

struct ItemTable {
    // 0-255 Beta's blocks, 256+ items (records mapped below), FIRST_MODERN_BLOCK+ later blocks.
    std::vector<std::unique_ptr<ItemDef>> items = std::vector<std::unique_ptr<ItemDef>>(MAX_BLOCK_ID);
    std::unique_ptr<ItemDef> record13, recordCat;

    template <typename T = ItemDef>
    T& Add(int itemId, const char* name, int icon, int maxStack = 64) {
        auto def = std::make_unique<T>();
        def->id = (uint16_t)itemId;
        def->name = name;
        def->icon = icon;
        def->maxStack = maxStack;
        T& ref = *def;
        items[itemId] = std::move(def);
        return ref;
    }
    ItemDef& Tool(int itemId, const char* name, int icon, ToolType type, const ToolMaterial& m) {
        ItemDef& def = Add(itemId, name, icon, 1);
        def.tool = type;
        def.toolTier = m.harvest;
        def.efficiency = m.efficiency;
        def.maxDamage = m.uses;
        return def;
    }
    ItemDef& Food(int itemId, const char* name, int icon, int hunger, float saturation, int maxStack = 64) {
        ItemDef& def = Add(itemId, name, icon, maxStack);
        def.foodHunger = hunger;
        def.foodSaturation = saturation;
        return def;
    }
    ItemDef& Armor(int itemId, const char* name, int icon, int slot, int material) {
        static const int points[4] = { 3, 8, 6, 3 };
        static const int baseDamage[4] = { 11, 16, 15, 13 };
        ItemDef& def = Add(itemId, name, icon, 1);
        def.armorSlot = slot;
        def.armorPoints = points[slot];
        def.maxDamage = (baseDamage[slot] * 3) << material;
        return def;
    }

    ItemTable() {
        for (int b = 1; b < BLOCK_COUNT; b++) items[b] = std::make_unique<BlockItemDef>(b);
        for (int b = FIRST_MODERN_BLOCK; b < MAX_BLOCK_ID; b++) {
            if (!BlockExists(b)) continue;
            const Block& block = GetBlockDef((BlockId)b);
            if (block.shape == Shape::Door) {
                // A door is its own item since 1.13: it places both halves.
                auto door = std::make_unique<DoorItemDef>();
                door->id = (uint16_t)b;
                door->name = block.name;
                door->icon = block.ItemIconTexture(0);
                door->sheet = IconSheet::Terrain;
                door->block = b;
                items[b] = std::move(door);
            } else {
                items[b] = std::make_unique<BlockItemDef>(b);
            }
        }

        Tool(ITEM_IRON_SHOVEL, "Iron Shovel", 82, ToolType::Shovel, TIER_IRON);
        Tool(ITEM_IRON_PICKAXE, "Iron Pickaxe", 98, ToolType::Pickaxe, TIER_IRON);
        Tool(ITEM_IRON_AXE, "Iron Axe", 114, ToolType::Axe, TIER_IRON);
        { auto& d = Add<FlintAndSteelDef>(ITEM_FLINT_AND_STEEL, "Flint and Steel", 5, 1); d.maxDamage = 64; }
        Food(ITEM_APPLE, "Apple", 10, 4, 0.3f);
        { auto& d = Add(ITEM_BOW, "Bow", 21, 1); d.maxDamage = 384; }
        Add(ITEM_ARROW, "Arrow", 37);
        Add<CoalDef>(ITEM_COAL, "Coal", 7);
        Add(ITEM_DIAMOND, "Diamond", 55);
        Add(ITEM_IRON_INGOT, "Iron Ingot", 23);
        Add(ITEM_GOLD_INGOT, "Gold Ingot", 39);
        Tool(ITEM_IRON_SWORD, "Iron Sword", 66, ToolType::Sword, TIER_IRON);
        Tool(ITEM_WOODEN_SWORD, "Wooden Sword", 64, ToolType::Sword, TIER_WOOD);
        Tool(ITEM_WOODEN_SHOVEL, "Wooden Shovel", 80, ToolType::Shovel, TIER_WOOD);
        Tool(ITEM_WOODEN_PICKAXE, "Wooden Pickaxe", 96, ToolType::Pickaxe, TIER_WOOD);
        Tool(ITEM_WOODEN_AXE, "Wooden Axe", 112, ToolType::Axe, TIER_WOOD);
        Tool(ITEM_STONE_SWORD, "Stone Sword", 65, ToolType::Sword, TIER_STONE);
        Tool(ITEM_STONE_SHOVEL, "Stone Shovel", 81, ToolType::Shovel, TIER_STONE);
        Tool(ITEM_STONE_PICKAXE, "Stone Pickaxe", 97, ToolType::Pickaxe, TIER_STONE);
        Tool(ITEM_STONE_AXE, "Stone Axe", 113, ToolType::Axe, TIER_STONE);
        Tool(ITEM_DIAMOND_SWORD, "Diamond Sword", 67, ToolType::Sword, TIER_DIAMOND);
        Tool(ITEM_DIAMOND_SHOVEL, "Diamond Shovel", 83, ToolType::Shovel, TIER_DIAMOND);
        Tool(ITEM_DIAMOND_PICKAXE, "Diamond Pickaxe", 99, ToolType::Pickaxe, TIER_DIAMOND);
        Tool(ITEM_DIAMOND_AXE, "Diamond Axe", 115, ToolType::Axe, TIER_DIAMOND);
        { auto& d = Add(ITEM_STICK, "Stick", 53); (void)d; }
        Add(ITEM_BOWL, "Bowl", 71);
        Food(ITEM_MUSHROOM_STEW, "Mushroom Stew", 72, 6, 0.6f, 1);
        Tool(ITEM_GOLDEN_SWORD, "Golden Sword", 68, ToolType::Sword, TIER_GOLD);
        Tool(ITEM_GOLDEN_SHOVEL, "Golden Shovel", 84, ToolType::Shovel, TIER_GOLD);
        Tool(ITEM_GOLDEN_PICKAXE, "Golden Pickaxe", 100, ToolType::Pickaxe, TIER_GOLD);
        Tool(ITEM_GOLDEN_AXE, "Golden Axe", 116, ToolType::Axe, TIER_GOLD);
        Add(ITEM_STRING, "String", 8);
        Add(ITEM_FEATHER, "Feather", 24);
        Add(ITEM_GUNPOWDER, "Gunpowder", 40);
        auto hoe = [&](int itemId, const char* name, int icon, const ToolMaterial& m) {
            auto& d = Add<HoeDef>(itemId, name, icon, 1);
            d.tool = ToolType::Hoe;
            d.maxDamage = m.uses;
        };
        hoe(ITEM_WOODEN_HOE, "Wooden Hoe", 128, TIER_WOOD);
        hoe(ITEM_STONE_HOE, "Stone Hoe", 129, TIER_STONE);
        hoe(ITEM_IRON_HOE, "Iron Hoe", 130, TIER_IRON);
        hoe(ITEM_DIAMOND_HOE, "Diamond Hoe", 131, TIER_DIAMOND);
        hoe(ITEM_GOLDEN_HOE, "Golden Hoe", 132, TIER_GOLD);
        { auto& d = Add<SeedsItemDef>(ITEM_SEEDS, "Seeds", 9); d.crop = BLOCK_WHEAT; }
        Add(ITEM_WHEAT, "Wheat", 25);
        Food(ITEM_BREAD, "Bread", 41, 5, 0.6f);
        const char* armorNames[5][4] = {
            { "Leather Cap", "Leather Tunic", "Leather Pants", "Leather Boots" },
            { "Chain Helmet", "Chain Chestplate", "Chain Leggings", "Chain Boots" },
            { "Iron Helmet", "Iron Chestplate", "Iron Leggings", "Iron Boots" },
            { "Diamond Helmet", "Diamond Chestplate", "Diamond Leggings", "Diamond Boots" },
            { "Golden Helmet", "Golden Chestplate", "Golden Leggings", "Golden boots" },
        };
        for (int material = 0; material < 5; material++)
            for (int slot = 0; slot < 4; slot++)
                Armor(ITEM_LEATHER_HELMET + material * 4 + slot, armorNames[material][slot], material + slot * 16, slot, material);
        Add(ITEM_FLINT, "Flint", 6);
        Food(ITEM_RAW_PORKCHOP, "Raw Porkchop", 87, 3, 0.3f);
        Food(ITEM_COOKED_PORKCHOP, "Cooked Porkchop", 88, 8, 0.8f);
        Add(ITEM_PAINTING, "Painting", 26);
        { auto& d = Food(ITEM_GOLDEN_APPLE, "Golden Apple", 11, 10, 1.2f); d.alwaysEdible = true; }
        Add<SignItemDef>(ITEM_SIGN, "Sign", 42, 1);
        { auto& d = Add<DoorItemDef>(ITEM_WOODEN_DOOR, "Wooden Door", 43, 1); d.block = BLOCK_WOODEN_DOOR; }
        { auto& d = Add<BucketDef>(ITEM_BUCKET, "Bucket", 74, 1); d.contents = 0; }
        { auto& d = Add<BucketDef>(ITEM_WATER_BUCKET, "Water Bucket", 75, 1); d.contents = BLOCK_FLOWING_WATER; }
        { auto& d = Add<BucketDef>(ITEM_LAVA_BUCKET, "Lava bucket", 76, 1); d.contents = BLOCK_FLOWING_LAVA; }
        Add(ITEM_MINECART, "Minecart", 135, 1);
        Add(ITEM_SADDLE, "Saddle", 104, 1);
        { auto& d = Add<DoorItemDef>(ITEM_IRON_DOOR, "Iron Door", 44, 1); d.block = BLOCK_IRON_DOOR; }
        Add<RedstoneItemDef>(ITEM_REDSTONE, "Redstone", 56);
        Add(ITEM_SNOWBALL, "Snowball", 14, 16);
        Add(ITEM_BOAT, "Boat", 136, 1);
        Add(ITEM_LEATHER, "Leather", 103);
        Add(ITEM_MILK_BUCKET, "Milk", 77, 1);
        Add(ITEM_BRICK, "Brick", 22);
        Add(ITEM_CLAY_BALL, "Clay", 57);
        { auto& d = Add<PlacerItemDef>(ITEM_REEDS, "Sugar Canes", 27); d.block = BLOCK_REEDS; }
        Add(ITEM_PAPER, "Paper", 58);
        Add(ITEM_BOOK, "Book", 59);
        Add(ITEM_SLIMEBALL, "Slimeball", 30);
        Add(ITEM_CHEST_MINECART, "Minecart with Chest", 151, 1);
        Add(ITEM_FURNACE_MINECART, "Minecart with Furnace", 167, 1);
        Add(ITEM_EGG, "Egg", 12, 16);
        Add(ITEM_COMPASS, "Compass", 54);
        { auto& d = Add(ITEM_FISHING_ROD, "Fishing Rod", 69, 1); d.maxDamage = 64; }
        Add(ITEM_CLOCK, "Clock", 70);
        Add(ITEM_GLOWSTONE_DUST, "Glowstone Dust", 73);
        Food(ITEM_RAW_FISH, "Raw Fish", 89, 2, 0.3f);
        Food(ITEM_COOKED_FISH, "Cooked Fish", 90, 5, 0.6f);
        Add<DyeDef>(ITEM_DYE, "Dye", 78);
        Add(ITEM_BONE, "Bone", 28);
        Add(ITEM_SUGAR, "Sugar", 13);
        { auto& d = Add<PlacerItemDef>(ITEM_CAKE, "Cake", 29, 1); d.block = BLOCK_CAKE; }
        Add<BedItemDef>(ITEM_BED, "Bed", 45, 1);
        { auto& d = Add<PlacerItemDef>(ITEM_REPEATER, "Redstone Repeater", 86); d.block = BLOCK_UNPOWERED_REPEATER; }
        Food(ITEM_COOKIE, "Cookie", 92, 2, 0.1f);
        Add(ITEM_MAP, "Map", 60, 1);
        { auto& d = Add(ITEM_SHEARS, "Shears", 93, 1); d.tool = ToolType::Shears; d.maxDamage = 238; }
        Food(ITEM_MELON, "Melon", 109, 2, 0.3f);
        { auto& d = Add<SeedsItemDef>(ITEM_PUMPKIN_SEEDS, "Pumpkin Seeds", 61); d.crop = BLOCK_PUMPKIN_STEM; }
        { auto& d = Add<SeedsItemDef>(ITEM_MELON_SEEDS, "Melon Seeds", 62); d.crop = BLOCK_MELON_STEM; }
        Food(ITEM_RAW_BEEF, "Raw Beef", 105, 3, 0.3f);
        Food(ITEM_STEAK, "Steak", 106, 8, 0.8f);
        Food(ITEM_RAW_CHICKEN, "Raw Chicken", 121, 2, 0.3f);
        Food(ITEM_COOKED_CHICKEN, "Cooked Chicken", 122, 6, 0.6f);
        Food(ITEM_ROTTEN_FLESH, "Rotten Flesh", 91, 4, 0.1f);
        Add(ITEM_ENDER_PEARL, "Ender Pearl", 107, 16);

        auto record = [](int itemId, int icon) {
            auto def = std::make_unique<RecordDef>();
            def->id = (uint16_t)itemId;
            def->name = "Music Disc";
            def->icon = icon;
            def->maxStack = 1;
            return def;
        };
        record13 = record(ITEM_RECORD_13, 240);
        recordCat = record(ITEM_RECORD_CAT, 241);
    }
};

const ItemTable& Table() {
    static const ItemTable table;
    return table;
}

} // namespace

const ItemDef* GetItemDef(int id) {
    const ItemTable& t = Table();
    if (id == ITEM_RECORD_13) return t.record13.get();
    if (id == ITEM_RECORD_CAT) return t.recordCat.get();
    if (id <= 0 || id >= (int)t.items.size()) return nullptr;
    return t.items[id].get();
}

bool IsBlockItem(int id) { return (id > 0 && id < 256) || (id >= FIRST_MODERN_BLOCK && BlockExists(id)); }

int MaxStackSize(const ItemStack& stack) {
    const ItemDef* def = GetItemDef(stack.id);
    return def != nullptr ? def->maxStack : 64;
}

std::string ItemName(const ItemStack& stack) {
    const ItemDef* def = GetItemDef(stack.id);
    return def != nullptr ? def->DisplayName(stack.damage) : "";
}

// ---- Mining

float ToolSpeed(const ItemStack& held, BlockId block, int meta) {
    const ItemDef* def = held.IsEmpty() ? nullptr : GetItemDef(held.id);
    if (def == nullptr) return 1.0f;
    const Block& b = GetBlockDef(block);
    switch (def->tool) {
        case ToolType::Sword:
            return block == BLOCK_WEB ? 15.0f : 1.5f;
        case ToolType::Shears:
            if (block == BLOCK_WEB || block == BLOCK_LEAVES) return 15.0f;
            if (block == BLOCK_WOOL) return 5.0f;
            return 1.0f;
        case ToolType::Pickaxe:
            if (b.material == BlockMaterial::Rock || b.material == BlockMaterial::Iron || block == BLOCK_ICE) return def->efficiency;
            if (block == BLOCK_DOUBLE_SLAB || block == BLOCK_SLAB) return def->efficiency;
            return 1.0f;
        case ToolType::Shovel:
            switch (block) {
                case BLOCK_GRASS: case BLOCK_DIRT: case BLOCK_SAND: case BLOCK_GRAVEL: case BLOCK_SNOW_LAYER:
                case BLOCK_SNOW: case BLOCK_CLAY: case BLOCK_FARMLAND: case BLOCK_SOUL_SAND:
                    return def->efficiency;
                default:
                    return b.material == BlockMaterial::Sand ? def->efficiency : 1.0f; // concrete powder
            }
        case ToolType::Axe:
            if (b.material == BlockMaterial::Wood || block == BLOCK_PLANKS || block == BLOCK_BOOKSHELF || block == BLOCK_LOG || block == BLOCK_CHEST) {
                return def->efficiency;
            }
            return 1.0f;
        default:
            return 1.0f;
    }
}

bool CanHarvest(const ItemStack& held, BlockId block) {
    const Block& b = GetBlockDef(block);
    if (GetMaterialInfo(b.material).harvestable) return true;
    const ItemDef* def = held.IsEmpty() ? nullptr : GetItemDef(held.id);
    if (def == nullptr) return false;
    switch (def->tool) {
        case ToolType::Pickaxe:
            if (block == BLOCK_OBSIDIAN) return def->toolTier == 3;
            if (block == BLOCK_DIAMOND_ORE || block == BLOCK_DIAMOND_BLOCK) return def->toolTier >= 2;
            if (block == BLOCK_GOLD_BLOCK || block == BLOCK_GOLD_ORE) return def->toolTier >= 2;
            if (block == BLOCK_IRON_BLOCK || block == BLOCK_IRON_ORE) return def->toolTier >= 1;
            if (block == BLOCK_LAPIS_BLOCK || block == BLOCK_LAPIS_ORE) return def->toolTier >= 1;
            if (block == BLOCK_REDSTONE_ORE || block == BLOCK_LIT_REDSTONE_ORE) return def->toolTier >= 2;
            return b.material == BlockMaterial::Rock || b.material == BlockMaterial::Iron;
        case ToolType::Shovel:
            return block == BLOCK_SNOW_LAYER || block == BLOCK_SNOW;
        case ToolType::Sword:
        case ToolType::Shears:
            return block == BLOCK_WEB;
        default:
            return false;
    }
}

bool DamageItem(ItemStack& stack, int amount) {
    const ItemDef* def = GetItemDef(stack.id);
    if (def == nullptr || def->maxDamage <= 0) return true;
    stack.damage = (uint16_t)(stack.damage + amount);
    if (stack.damage > def->maxDamage) {
        stack.Clear();
        return false;
    }
    return true;
}

// ---- Furnaces

int FuelTicks(const ItemStack& stack) {
    if (stack.IsEmpty()) return 0;
    if (IsBlockItem(stack.id) && GetBlockDef((BlockId)stack.id).material == BlockMaterial::Wood) return 300;
    if (stack.id == ITEM_STICK) return 100;
    if (stack.id == ITEM_COAL) return 1600;
    if (stack.id == ITEM_LAVA_BUCKET) return 20000;
    if (stack.id == BLOCK_SAPLING) return 100;
    return 0;
}

ItemStack SmeltingResult(const ItemStack& input) {
    if (input.IsEmpty()) return ItemStack();
    switch (input.id) {
        case BLOCK_IRON_ORE: return ItemStack(ITEM_IRON_INGOT, 1);
        case BLOCK_GOLD_ORE: return ItemStack(ITEM_GOLD_INGOT, 1);
        case BLOCK_DIAMOND_ORE: return ItemStack(ITEM_DIAMOND, 1);
        case BLOCK_SAND: return ItemStack(BLOCK_GLASS, 1);
        case ITEM_RAW_PORKCHOP: return ItemStack(ITEM_COOKED_PORKCHOP, 1);
        case ITEM_RAW_BEEF: return ItemStack(ITEM_STEAK, 1);
        case ITEM_RAW_CHICKEN: return ItemStack(ITEM_COOKED_CHICKEN, 1);
        case ITEM_RAW_FISH: return ItemStack(ITEM_COOKED_FISH, 1);
        case BLOCK_COBBLESTONE: return ItemStack(BLOCK_STONE, 1);
        case ITEM_CLAY_BALL: return ItemStack(ITEM_BRICK, 1);
        case BLOCK_CACTUS: return ItemStack(ITEM_DYE, 1, DYE_GREEN);
        case BLOCK_LOG: return ItemStack(ITEM_COAL, 1, 1);
        default: return ItemStack();
    }
}

// ---- Placing (ItemBlock.onItemUse / World.canBlockBePlacedAt)

bool PlaceBlockFromItem(ItemStack& stack, Player& player, World& world, int x, int y, int z, int side, int blockId, int meta) {
    if (stack.IsEmpty()) return false;
    const Block& block = GetBlockDef((BlockId)blockId);
    if (y >= Chunk::HEIGHT - 1 && block.BlocksMovement()) return false;
    if (y < 0 || y >= Chunk::HEIGHT) return false;
    int there = world.GetBlock(x, y, z);
    if (there != BLOCK_AIR && !GetMaterialInfo(GetBlockDef((BlockId)there).material).replaceable) return false;
    if (!block.CanPlaceOnSide(world, x, y, z, side)) return false;
    if (block.BlocksMovement()) {
        double min[3] = { (double)x, (double)y, (double)z }, max[3] = { x + 1.0, y + 1.0, z + 1.0 };
        if (player.Overlaps(min, max)) return false;
    }
    if (!world.SetBlock(x, y, z, blockId, meta)) return false;
    block.OnPlaced(world, x, y, z, side, player);
    world.PlaySound(StepSoundEvent(block.sound), x + 0.5, y + 0.5, z + 0.5, (StepSoundVolume(block.sound) + 1.0f) / 2.0f,
                    StepSoundPitch(block.sound) * 0.8f);
    if (!player.IsCreative()) stack.Split(1);
    return true;
}
