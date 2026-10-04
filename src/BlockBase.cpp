#include "BlocksCommon.h"

#include <array>
#include <cmath>

// ---- Materials

const MaterialInfo& GetMaterialInfo(BlockMaterial material) {
    //                              solid  liquid replace burns  harvest mobility
    static const MaterialInfo AIR     { false, false, true,  false, true,  0 };
    static const MaterialInfo GRASS   { true,  false, false, false, true,  0 };
    static const MaterialInfo GROUND  { true,  false, false, false, true,  0 };
    static const MaterialInfo WOOD    { true,  false, false, true,  true,  0 };
    static const MaterialInfo ROCK    { true,  false, false, false, false, 0 };
    static const MaterialInfo IRON    { true,  false, false, false, false, 0 };
    static const MaterialInfo WATER   { false, true,  true,  false, true,  1 };
    static const MaterialInfo LAVA    { false, true,  true,  false, true,  1 };
    static const MaterialInfo LEAVES  { true,  false, false, true,  true,  1 };
    static const MaterialInfo PLANTS  { false, false, false, false, true,  1 };
    static const MaterialInfo VINE    { false, false, true,  true,  true,  1 };
    static const MaterialInfo SPONGE  { true,  false, false, false, true,  0 };
    static const MaterialInfo CLOTH   { true,  false, false, true,  true,  0 };
    static const MaterialInfo FIRE    { false, false, true,  false, true,  1 };
    static const MaterialInfo SAND    { true,  false, false, false, true,  0 };
    static const MaterialInfo CIRCUIT { false, false, false, false, true,  1 };
    static const MaterialInfo GLASS   { true,  false, false, false, true,  0 };
    static const MaterialInfo TNT     { true,  false, false, true,  true,  0 };
    static const MaterialInfo ICE     { true,  false, false, false, true,  0 };
    static const MaterialInfo SNOW    { false, false, true,  false, false, 1 };
    static const MaterialInfo BSNOW   { true,  false, false, false, false, 0 };
    static const MaterialInfo CACTUS  { true,  false, false, false, true,  1 };
    static const MaterialInfo CLAY    { true,  false, false, false, true,  0 };
    static const MaterialInfo PUMPKIN { true,  false, false, false, true,  1 };
    static const MaterialInfo PORTAL  { false, false, false, false, true,  2 };
    static const MaterialInfo CAKE    { true,  false, false, false, true,  1 };
    static const MaterialInfo WEB     { false, false, false, false, false, 1 };
    static const MaterialInfo PISTON  { true,  false, false, false, true,  2 };
    switch (material) {
        case BlockMaterial::Air: return AIR;
        case BlockMaterial::Grass: return GRASS;
        case BlockMaterial::Ground: return GROUND;
        case BlockMaterial::Wood: return WOOD;
        case BlockMaterial::Rock: return ROCK;
        case BlockMaterial::Iron: return IRON;
        case BlockMaterial::Water: return WATER;
        case BlockMaterial::Lava: return LAVA;
        case BlockMaterial::Leaves: return LEAVES;
        case BlockMaterial::Plants: return PLANTS;
        case BlockMaterial::Vine: return VINE;
        case BlockMaterial::Sponge: return SPONGE;
        case BlockMaterial::Cloth: return CLOTH;
        case BlockMaterial::Fire: return FIRE;
        case BlockMaterial::Sand: return SAND;
        case BlockMaterial::Circuits: return CIRCUIT;
        case BlockMaterial::Glass: return GLASS;
        case BlockMaterial::Tnt: return TNT;
        case BlockMaterial::Ice: return ICE;
        case BlockMaterial::Snow: return SNOW;
        case BlockMaterial::BuiltSnow: return BSNOW;
        case BlockMaterial::Cactus: return CACTUS;
        case BlockMaterial::Clay: return CLAY;
        case BlockMaterial::Pumpkin: return PUMPKIN;
        case BlockMaterial::Portal: return PORTAL;
        case BlockMaterial::Cake: return CAKE;
        case BlockMaterial::Web: return WEB;
        case BlockMaterial::Piston: return PISTON;
    }
    return AIR;
}

// ---- Default block behavior (Minecraft's Block)

int Block::WorldTexture(const World& world, int x, int y, int z, int side) const {
    return Texture(side, world.GetMeta(x, y, z));
}

Color Block::WorldTint(const World& world, int x, int y, int z, int side) const {
    return ItemTint(world.GetMeta(x, y, z));
}

// A face is drawn if the block doesn't fill that side of its cell, or the
// neighbor there isn't an opaque cube (Block.shouldSideBeRendered).
bool Block::ShouldSideBeRendered(const World& world, int nx, int ny, int nz, int side) const {
    int x = nx - SIDE_OFFSET[side][0], y = ny - SIDE_OFFSET[side][1], z = nz - SIDE_OFFSET[side][2];
    BlockBox b = Bounds(world, x, y, z);
    if (side == SIDE_DOWN && b.minY > 0.0f) return true;
    if (side == SIDE_UP && b.maxY < 1.0f) return true;
    if (side == SIDE_NORTH && b.minZ > 0.0f) return true;
    if (side == SIDE_SOUTH && b.maxZ < 1.0f) return true;
    if (side == SIDE_WEST && b.minX > 0.0f) return true;
    if (side == SIDE_EAST && b.maxX < 1.0f) return true;
    return !world.IsOpaqueCube(nx, ny, nz);
}

// RenderBlocks.renderItemIn3d: blocks drawn as little 3D models in the
// inventory; everything else is a flat sprite.
bool Block::RendersAs3DItem() const {
    switch (shape) {
        case Shape::Cube: case Shape::Cactus: case Shape::Stairs: case Shape::Fence:
        case Shape::PistonBase: case Shape::Chest: case Shape::Wall:
            return true;
        default:
            return false;
    }
}

void Block::CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const {
    if (BlocksMovement()) out.push_back(Bounds(world, x, y, z));
}

bool Block::CanPlaceAt(const World& world, int x, int y, int z) const {
    BlockId there = world.GetBlock(x, y, z);
    return there == BLOCK_AIR || GetMaterialInfo(GetBlockDef(there).material).replaceable;
}

void Block::Drops(World& world, int x, int y, int z, int meta, JavaRandom& random, std::vector<ItemStack>& out) const {
    out.push_back(ItemStack(id, 1, DamageDropped(meta)));
}

ItemStack Block::PickItem(const World& world, int x, int y, int z) const {
    return ItemStack(id, 1, DamageDropped(world.GetMeta(x, y, z)));
}

// ---- Registry

namespace {

class AirBlock final : public Block {
public:
    AirBlock() {
        name = "Air";
        material = BlockMaterial::Air;
        sound = StepSound::None;
        lightOpacity = 0;
        shape = Shape::None;
        opaqueCube = false;
        normalCube = false;
    }
    bool IsTargetable(int) const override { return false; }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}
};

struct Tables {
    BlockRegistry registry;
    std::array<int, MAX_BLOCK_ID> opacity{};
    std::array<int, MAX_BLOCK_ID> light{};
    std::array<bool, MAX_BLOCK_ID> opaque{};
    std::array<bool, MAX_BLOCK_ID> normal{};

    Tables() {
        registry.Add<AirBlock>(BLOCK_AIR);
        RegisterBasicBlocks(registry);
        RegisterPlantBlocks(registry);
        RegisterFluidBlocks(registry);
        RegisterRedstoneBlocks(registry);
        RegisterBuildingBlocks(registry);
        RegisterContainerBlocks(registry);
        RegisterPistonBlocks(registry);
        RegisterStairs(registry);
        RegisterModernBlocks(registry);
        for (int id = 0; id < MAX_BLOCK_ID; id++) {
            const Block* block = registry.blocks[id].get();
            if (block == nullptr) block = registry.blocks[BLOCK_AIR].get();
            opacity[id] = block->lightOpacity;
            light[id] = block->lightValue;
            opaque[id] = block->opaqueCube;
            normal[id] = block->normalCube;
        }
    }
};

const Tables& GetTables() {
    static const Tables tables;
    return tables;
}

} // namespace

// IDs past the table (a damaged save) read as air.
const Block& GetBlockDef(BlockId id) {
    const Tables& t = GetTables();
    const Block* block = id < MAX_BLOCK_ID ? t.registry.blocks[id].get() : nullptr;
    return block != nullptr ? *block : *t.registry.blocks[BLOCK_AIR];
}

bool FenceJoins(BlockId id) {
    Shape s = GetBlockDef(id).shape;
    return s == Shape::Fence || s == Shape::FenceGate || BlockIsOpaqueCube(id);
}

bool WallJoins(BlockId id) {
    Shape s = GetBlockDef(id).shape;
    return s == Shape::Wall || s == Shape::FenceGate || s == Shape::Pane || BlockIsOpaqueCube(id);
}

bool PaneJoins(BlockId id) {
    const Block& def = GetBlockDef(id);
    bool glassCube = def.shape == Shape::Cube && def.material == BlockMaterial::Glass; // glass, stained glass
    return def.shape == Shape::Pane || def.shape == Shape::Wall || glassCube || BlockIsOpaqueCube(id);
}

bool IsTallPost(BlockId id) {
    Shape s = GetBlockDef(id).shape;
    return s == Shape::Fence || s == Shape::Wall;
}

bool BlockExists(int id) { return id >= 0 && id < MAX_BLOCK_ID && GetTables().registry.blocks[id] != nullptr; }

int BlockLightOpacity(BlockId id) { return id < MAX_BLOCK_ID ? GetTables().opacity[id] : 0; }
int BlockLightValue(BlockId id) { return id < MAX_BLOCK_ID ? GetTables().light[id] : 0; }
bool BlockIsOpaqueCube(BlockId id) { return id < MAX_BLOCK_ID && GetTables().opaque[id]; }
bool BlockIsNormalCube(BlockId id) { return id < MAX_BLOCK_ID && GetTables().normal[id]; }

// ---- Step sounds (Beta's StepSound, StepSoundStone for glass, StepSoundSand)

const char* StepSoundEvent(StepSound sound) {
    switch (sound) {
        case StepSound::Stone: case StepSound::Glass: case StepSound::Metal: case StepSound::Powder: return "step.stone";
        case StepSound::Wood: return "step.wood";
        case StepSound::Gravel: return "step.gravel";
        case StepSound::Grass: return "step.grass";
        case StepSound::Cloth: return "step.cloth";
        case StepSound::Sand: return "step.sand";
        default: return nullptr;
    }
}

const char* BreakSoundEvent(StepSound sound) {
    if (sound == StepSound::Glass) return "random.glass";
    if (sound == StepSound::Sand) return "step.gravel";
    return StepSoundEvent(sound);
}

float StepSoundVolume(StepSound sound) { return 1.0f; }
float StepSoundPitch(StepSound sound) { return sound == StepSound::Metal ? 1.5f : 1.0f; }

// ---- Helpers shared by the block definitions

int PlayerYawIndex(const Player& player) {
    return (int)std::floor(player.YawDegrees() * 4.0 / 360.0 + 0.5) & 3;
}

int PlayerYawIndex16(const Player& player) {
    return (int)std::floor((player.YawDegrees() + 180.0) * 16.0 / 360.0 + 0.5) & 15;
}

int PlayerDoorIndex(const Player& player) {
    return (int)std::floor((player.YawDegrees() + 180.0) * 4.0 / 360.0 - 0.5) & 3;
}

// BlockPistonBase.determineOrientation: looking steeply up or down from close by
// faces the piston vertically, otherwise it faces the player horizontally.
int PlayerFacingSide(const Player& player, int x, int y, int z) {
    Vector3 pos = player.GetPosition();
    if (std::fabs(pos.x - x) < 2.0f && std::fabs(pos.z - z) < 2.0f) {
        double eye = pos.y + 1.82; // Minecraft: posY + 1.82 - yOffset, i.e. feet + 1.82
        if (eye - y > 2.0) return SIDE_UP;
        if (y - eye > 0.0) return SIDE_DOWN;
    }
    int l = PlayerYawIndex(player);
    if (l == 0) return SIDE_NORTH;
    if (l == 1) return SIDE_EAST;
    if (l == 2) return SIDE_SOUTH;
    return SIDE_WEST;
}

void DropStack(World& world, int x, int y, int z, const ItemStack& stack) {
    world.SpawnItem(x, y, z, stack);
}

void BreakAndDrop(World& world, int x, int y, int z) {
    int id = world.GetBlock(x, y, z);
    int meta = world.GetMeta(x, y, z);
    world.DropBlockAsItem(x, y, z, id, meta);
    world.SetBlock(x, y, z, BLOCK_AIR);
}

bool PlacedInUpperHalf(const Player& player, int side) {
    if (side == SIDE_DOWN) return true;
    if (side == SIDE_UP) return false;
    float y = player.GetTarget().point.y;
    return y - std::floor(y) > 0.5f;
}

bool CanSupportOnTop(const World& world, int x, int y, int z) {
    return world.IsNormalCube(x, y, z) || IsTallPost(world.GetBlock(x, y, z));
}
