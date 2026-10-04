#pragma once

#include "raylib.h"
#include "ItemStack.h"

#include <cstdint>
#include <string>
#include <vector>

class World;
class Player;
class JavaRandom;

// A block's ID as chunks and saves store it. Beta's blocks keep Beta's IDs
// (0-255); blocks from later versions start at FIRST_MODERN_BLOCK, clear of the
// item IDs (256-2267) that share ItemStack's numbering with blocks.
using BlockId = uint16_t;
constexpr int FIRST_MODERN_BLOCK = 4096;
constexpr int MAX_BLOCK_ID = 8192;

// Every block in Minecraft Beta 1.8, with Beta's block IDs (chunks store these).
// Each block can also carry 4 bits of metadata: its variant (wool color, wood
// type, slab kind), facing, growth stage, liquid level, power, and so on.
enum BlockType : BlockId {
    BLOCK_AIR = 0,
    BLOCK_STONE = 1,
    BLOCK_GRASS = 2,
    BLOCK_DIRT = 3,
    BLOCK_COBBLESTONE = 4,
    BLOCK_PLANKS = 5,
    BLOCK_SAPLING = 6,
    BLOCK_BEDROCK = 7,
    BLOCK_FLOWING_WATER = 8,
    BLOCK_WATER = 9,
    BLOCK_FLOWING_LAVA = 10,
    BLOCK_LAVA = 11,
    BLOCK_SAND = 12,
    BLOCK_GRAVEL = 13,
    BLOCK_GOLD_ORE = 14,
    BLOCK_IRON_ORE = 15,
    BLOCK_COAL_ORE = 16,
    BLOCK_LOG = 17,
    BLOCK_LEAVES = 18,
    BLOCK_SPONGE = 19,
    BLOCK_GLASS = 20,
    BLOCK_LAPIS_ORE = 21,
    BLOCK_LAPIS_BLOCK = 22,
    BLOCK_DISPENSER = 23,
    BLOCK_SANDSTONE = 24,
    BLOCK_NOTE_BLOCK = 25,
    BLOCK_BED = 26,
    BLOCK_POWERED_RAIL = 27,
    BLOCK_DETECTOR_RAIL = 28,
    BLOCK_STICKY_PISTON = 29,
    BLOCK_WEB = 30,
    BLOCK_TALL_GRASS = 31,
    BLOCK_DEAD_BUSH = 32,
    BLOCK_PISTON = 33,
    BLOCK_PISTON_HEAD = 34,
    BLOCK_WOOL = 35,
    BLOCK_PISTON_MOVING = 36,
    BLOCK_DANDELION = 37,
    BLOCK_ROSE = 38,
    BLOCK_BROWN_MUSHROOM = 39,
    BLOCK_RED_MUSHROOM = 40,
    BLOCK_GOLD_BLOCK = 41,
    BLOCK_IRON_BLOCK = 42,
    BLOCK_DOUBLE_SLAB = 43,
    BLOCK_SLAB = 44,
    BLOCK_BRICKS = 45,
    BLOCK_TNT = 46,
    BLOCK_BOOKSHELF = 47,
    BLOCK_MOSSY_COBBLESTONE = 48,
    BLOCK_OBSIDIAN = 49,
    BLOCK_TORCH = 50,
    BLOCK_FIRE = 51,
    BLOCK_MOB_SPAWNER = 52,
    BLOCK_OAK_STAIRS = 53,
    BLOCK_CHEST = 54,
    BLOCK_REDSTONE_WIRE = 55,
    BLOCK_DIAMOND_ORE = 56,
    BLOCK_DIAMOND_BLOCK = 57,
    BLOCK_CRAFTING_TABLE = 58,
    BLOCK_WHEAT = 59,
    BLOCK_FARMLAND = 60,
    BLOCK_FURNACE = 61,
    BLOCK_LIT_FURNACE = 62,
    BLOCK_SIGN_POST = 63,
    BLOCK_WOODEN_DOOR = 64,
    BLOCK_LADDER = 65,
    BLOCK_RAIL = 66,
    BLOCK_COBBLESTONE_STAIRS = 67,
    BLOCK_WALL_SIGN = 68,
    BLOCK_LEVER = 69,
    BLOCK_STONE_PRESSURE_PLATE = 70,
    BLOCK_IRON_DOOR = 71,
    BLOCK_WOODEN_PRESSURE_PLATE = 72,
    BLOCK_REDSTONE_ORE = 73,
    BLOCK_LIT_REDSTONE_ORE = 74,
    BLOCK_UNLIT_REDSTONE_TORCH = 75,
    BLOCK_REDSTONE_TORCH = 76,
    BLOCK_STONE_BUTTON = 77,
    BLOCK_SNOW_LAYER = 78,
    BLOCK_ICE = 79,
    BLOCK_SNOW = 80,
    BLOCK_CACTUS = 81,
    BLOCK_CLAY = 82,
    BLOCK_REEDS = 83,
    BLOCK_JUKEBOX = 84,
    BLOCK_FENCE = 85,
    BLOCK_PUMPKIN = 86,
    BLOCK_NETHERRACK = 87,
    BLOCK_SOUL_SAND = 88,
    BLOCK_GLOWSTONE = 89,
    BLOCK_PORTAL = 90,
    BLOCK_JACK_O_LANTERN = 91,
    BLOCK_CAKE = 92,
    BLOCK_UNPOWERED_REPEATER = 93,
    BLOCK_POWERED_REPEATER = 94,
    BLOCK_LOCKED_CHEST = 95,
    BLOCK_TRAPDOOR = 96,
    BLOCK_MONSTER_EGG = 97,
    BLOCK_STONE_BRICKS = 98,
    BLOCK_BROWN_MUSHROOM_BLOCK = 99,
    BLOCK_RED_MUSHROOM_BLOCK = 100,
    BLOCK_IRON_BARS = 101,
    BLOCK_GLASS_PANE = 102,
    BLOCK_MELON = 103,
    BLOCK_PUMPKIN_STEM = 104,
    BLOCK_MELON_STEM = 105,
    BLOCK_VINE = 106,
    BLOCK_FENCE_GATE = 107,
    BLOCK_BRICK_STAIRS = 108,
    BLOCK_STONE_BRICK_STAIRS = 109,
    BLOCK_COUNT = 110
};

// terrain.png: a 64x64 grid of 16px tiles. Beta's tile numbers (0-255) keep
// Beta's 16x16 layout in its top-left corner (tile row * 16 + col); numbers from
// 256 fill the rows below that, 64 to a row.
constexpr int ATLAS_TILES = 64;
constexpr int LEGACY_TILES = 256;
constexpr int MAX_TILE = LEGACY_TILES + (ATLAS_TILES - 16) * ATLAS_TILES;
inline int TileColumn(int tile) { return tile < LEGACY_TILES ? (tile & 15) : (tile - LEGACY_TILES) % ATLAS_TILES; }
inline int TileRow(int tile) { return tile < LEGACY_TILES ? (tile >> 4) : 16 + (tile - LEGACY_TILES) / ATLAS_TILES; }

// Minecraft's side numbering: 0 down (-Y), 1 up (+Y), 2 north (-Z), 3 south (+Z),
// 4 west (-X), 5 east (+X).
enum Side : int { SIDE_DOWN = 0, SIDE_UP = 1, SIDE_NORTH = 2, SIDE_SOUTH = 3, SIDE_WEST = 4, SIDE_EAST = 5 };
constexpr int SIDE_OFFSET[6][3] = { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };
inline int OppositeSide(int side) { return side ^ 1; }

// What a block is made of (Minecraft's BlockMaterial): decides whether it blocks
// movement, burns, is a liquid, needs a tool to drop anything, and how pistons treat it.
enum class BlockMaterial : uint8_t {
    Air, Grass, Ground, Wood, Rock, Iron, Water, Lava, Leaves, Plants, Vine, Sponge, Cloth, Fire, Sand,
    Circuits, Glass, Tnt, Ice, Snow, BuiltSnow, Cactus, Clay, Pumpkin, Portal, Cake, Web, Piston
};

struct MaterialInfo {
    bool solid;         // blocks movement (and liquids)
    bool liquid;
    bool replaceable;   // placing a block here overwrites it
    bool burns;         // fire can spread to it
    bool harvestable;   // drops something without the right tool
    int pistonMobility; // 0 pushable, 1 breaks when pushed, 2 immovable
};
const MaterialInfo& GetMaterialInfo(BlockMaterial material);

// Which set of step sounds a block uses (StepSound). Beta breaks blocks with the
// step sound too (glass with random.glass, sand with step.gravel).
enum class StepSound : uint8_t { None, Stone, Wood, Gravel, Grass, Cloth, Sand, Glass, Metal, Powder };

// How a block is drawn (RenderBlocks' render types).
enum class Shape : uint8_t {
    None, Cube, Cross, Torch, Fire, Fluid, RedstoneWire, Crops, Door, Ladder, Rail, Stairs, Fence, Lever,
    Cactus, Bed, Repeater, PistonBase, PistonHead, Pane, Stem, Vine, FenceGate, Chest, Sign, MovingPiston
};

// Which pass a block's faces go in: solid, alpha-tested (leaves, glass, plants),
// or blended (water, ice, portals).
enum class Layer : uint8_t { Opaque, Cutout, Translucent };

// A box within a block, 0..1 on each axis (can reach past 1 for fences).
struct BlockBox {
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float maxX = 1.0f, maxY = 1.0f, maxZ = 1.0f;
};

// One kind of block: its properties, and hooks the world calls, mirroring
// Minecraft's Block class. Look one up with GetBlockDef(id).
class Block {
public:
    BlockId id = 0;
    std::string name;          // Beta's English name
    int texture = 0;           // default texture tile
    float hardness = 0.0f;     // negative = unbreakable
    float resistance = 0.0f;   // explosion resistance (Minecraft's blockResistance / 5)
    BlockMaterial material = BlockMaterial::Rock;
    StepSound sound = StepSound::Stone;
    int lightOpacity = 255;    // light lost passing through (255 = none gets through)
    int lightValue = 0;        // light the block gives off, 0-15
    float slipperiness = 0.6f;
    bool ticksRandomly = false;
    int tickRate = 10;         // ticks between scheduled updates
    Shape shape = Shape::Cube;
    Layer layer = Layer::Opaque;
    bool opaqueCube = true;    // a full, opaque cube: hides neighbors' faces, casts AO
    bool normalCube = true;    // a full solid cube for redstone and suffocation (isBlockNormalCube)

    virtual ~Block() = default;

    // ---- Appearance
    virtual std::string DisplayName(int meta) const { return name; }
    virtual int Texture(int side, int meta) const { return texture; }
    // The texture for a face in the world, which can depend on neighbors
    // (grass with snow on it, double chests, piston state).
    virtual int WorldTexture(const World& world, int x, int y, int z, int side) const;
    virtual Color ItemTint(int meta) const { return WHITE; }
    // The tint of one face of the block drawn as an item (grass colors only its top).
    virtual Color ItemFaceTint(int meta, int side) const { return ItemTint(meta); }
    virtual Color WorldTint(const World& world, int x, int y, int z, int side) const;
    // Faces of this block next to (nx, ny, nz), which is on `side`, are drawn unless hidden.
    virtual bool ShouldSideBeRendered(const World& world, int nx, int ny, int nz, int side) const;
    virtual int ItemIconTexture(int meta) const { return Texture(SIDE_NORTH, meta); }
    virtual bool RendersAs3DItem() const;

    // ---- Shape
    // The block's outline and render bounds (setBlockBoundsBasedOnState).
    virtual BlockBox Bounds(const World& world, int x, int y, int z) const { return BlockBox(); }
    virtual BlockBox ItemBounds(int meta) const { return BlockBox(); }
    // Collision boxes, in block-local coordinates.
    virtual void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const;
    virtual bool IsTargetable(int meta) const { return true; } // can be aimed at (liquids can't)

    // ---- Placing
    virtual bool CanPlaceAt(const World& world, int x, int y, int z) const;
    virtual bool CanPlaceOnSide(const World& world, int x, int y, int z, int side) const { return CanPlaceAt(world, x, y, z); }
    virtual bool CanStay(const World& world, int x, int y, int z) const { return true; }
    // After placing (from an item), aimed at `side` of the block clicked; sets facing.
    virtual void OnPlaced(World& world, int x, int y, int z, int side, const Player& player) const {}
    virtual int MetaForPlacement(int itemDamage) const { return itemDamage; }

    // ---- World events
    virtual void OnAdded(World& world, int x, int y, int z) const {}
    virtual void OnRemoved(World& world, int x, int y, int z, int oldMeta) const {}
    virtual void OnNeighborChanged(World& world, int x, int y, int z, int neighborId) const {}
    virtual void OnTick(World& world, int x, int y, int z, JavaRandom& random) const {}
    virtual void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const {}
    virtual void OnBlockEvent(World& world, int x, int y, int z, int a, int b) const {}
    // Right click; returns true if it did something (so nothing gets placed).
    virtual bool OnActivated(World& world, int x, int y, int z, Player& player) const { return false; }
    // Left click (start of mining).
    virtual void OnClicked(World& world, int x, int y, int z, Player& player) const {}
    virtual void OnPlayerCollided(World& world, int x, int y, int z, Player& player) const {}
    virtual void OnPlayerWalking(World& world, int x, int y, int z, Player& player) const {}
    virtual void OnDestroyedByPlayer(World& world, int x, int y, int z, int meta, Player& player) const {}
    virtual void OnExploded(World& world, int x, int y, int z) const {}
    // Landing on top of it after falling `distance` blocks (farmland gets trampled).
    virtual void OnFallenUpon(World& world, int x, int y, int z, Player& player, float distance) const {}
    // Bone meal (crops ripen, saplings and mushrooms grow big); true if it worked.
    virtual bool Fertilize(World& world, int x, int y, int z) const { return false; }

    // ---- Drops
    // What breaking it drops (without the tool check -- see World::HarvestBlock).
    virtual void Drops(World& world, int x, int y, int z, int meta, JavaRandom& random, std::vector<ItemStack>& out) const;
    virtual int DamageDropped(int meta) const { return 0; }
    // The item a creative middle click picks.
    virtual ItemStack PickItem(const World& world, int x, int y, int z) const;

    // ---- Redstone
    virtual bool CanProvidePower() const { return false; }
    // Weak power out toward the block asking (Minecraft's isPoweringTo); `side` is
    // the direction from the asking block to this one.
    virtual bool IsPoweringTo(const World& world, int x, int y, int z, int side) const { return false; }
    // Strong power into the block on `side` (isIndirectlyPoweringTo).
    virtual bool IsIndirectlyPoweringTo(const World& world, int x, int y, int z, int side) const { return false; }

    // ---- Misc
    virtual bool IsLadder(int meta) const { return false; }
    virtual int PistonMobility(int meta) const { return GetMaterialInfo(material).pistonMobility; }
    virtual bool HasTileEntity() const { return false; }
    bool IsLiquid() const { return GetMaterialInfo(material).liquid; }
    bool BlocksMovement() const { return GetMaterialInfo(material).solid; }
};

const Block& GetBlockDef(BlockId id);
// Is there a block with this ID (anything registered, air included)?
bool BlockExists(int id);

// Quick lookups the light engine and mesher use a lot.
int BlockLightOpacity(BlockId id);
int BlockLightValue(BlockId id);
bool BlockIsOpaqueCube(BlockId id);
bool BlockIsNormalCube(BlockId id);

// Beta's step sounds.
const char* StepSoundEvent(StepSound sound);  // walking and placing: "step.stone", "step.wood", ...
const char* BreakSoundEvent(StepSound sound); // breaking: the same, except glass ("random.glass") and sand ("step.gravel")
float StepSoundVolume(StepSound sound);
float StepSoundPitch(StepSound sound);
