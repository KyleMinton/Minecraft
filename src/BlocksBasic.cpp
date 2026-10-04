#include "BlocksCommon.h"
#include "Colors.h"

#include <cmath>

namespace {

// A plain cube, optionally with its own top and bottom textures, dropping
// itself (or something else) like most of Minecraft's simple blocks.
class CubeBlock : public Block {
public:
    int top = -1, bottom = -1;
    int dropId = -1;                 // -1 = itself, 0 = nothing
    int dropMin = 1, dropMax = 1;
    int dropDamage = 0;
    bool keepMeta = false;

    int Texture(int side, int meta) const override {
        if (side == SIDE_UP && top >= 0) return top;
        if (side == SIDE_DOWN && bottom >= 0) return bottom;
        return texture;
    }
    int DamageDropped(int meta) const override { return keepMeta ? meta : dropDamage; }
    void Drops(World& world, int x, int y, int z, int meta, JavaRandom& random, std::vector<ItemStack>& out) const override {
        int item = dropId < 0 ? id : dropId;
        if (item == 0) return;
        int count = dropMin + (dropMax > dropMin ? random.NextInt(dropMax - dropMin + 1) : 0);
        if (count > 0) out.push_back(ItemStack(item, count, DamageDropped(meta)));
    }
};

// ---- Grass (BlockGrass): spreads onto dirt in the light, dies in the dark.

class GrassBlock final : public CubeBlock {
public:
    GrassBlock() { ticksRandomly = true; }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP) return 0;
        if (side == SIDE_DOWN) return 2;
        return 3;
    }
    int WorldTexture(const World& world, int x, int y, int z, int side) const override {
        if (side == SIDE_UP) return 0;
        if (side == SIDE_DOWN) return 2;
        BlockMaterial above = world.GetMaterial(x, y + 1, z);
        return (above == BlockMaterial::Snow || above == BlockMaterial::BuiltSnow) ? 68 : 3;
    }
    Color ItemTint(int meta) const override { return GRASS_ITEM_COLOR; }
    // The sides are dirt; their green fringe is the overlay RenderCube adds.
    Color ItemFaceTint(int meta, int side) const override { return side == SIDE_UP ? GRASS_ITEM_COLOR : WHITE; }
    Color WorldTint(const World& world, int x, int y, int z, int side) const override {
        return side == SIDE_UP ? GRASS_COLOR : WHITE;
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(BLOCK_DIRT, 1));
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (world.GetLight(x, y + 1, z) < 4 && BlockLightOpacity(world.GetBlock(x, y + 1, z)) > 2) {
            if (random.NextInt(4) != 0) return;
            world.SetBlock(x, y, z, BLOCK_DIRT);
        } else if (world.GetLight(x, y + 1, z) >= 9) {
            int tx = x + random.NextInt(3) - 1;
            int ty = y + random.NextInt(5) - 3;
            int tz = z + random.NextInt(3) - 1;
            int above = world.GetBlock(tx, ty + 1, tz);
            if (world.GetBlock(tx, ty, tz) == BLOCK_DIRT && world.GetLight(tx, ty + 1, tz) >= 4 && BlockLightOpacity((uint8_t)above) <= 2) {
                world.SetBlock(tx, ty, tz, BLOCK_GRASS);
            }
        }
    }
};

// ---- Sand and gravel (BlockSand): fall when there's nothing under them.

class FallingBlock : public CubeBlock {
public:
    FallingBlock() { tickRate = 3; }
    static bool CanFallInto(const World& world, int x, int y, int z) {
        int id = world.GetBlock(x, y, z);
        if (id == BLOCK_AIR || id == BLOCK_FIRE) return true;
        BlockMaterial material = GetBlockDef((BlockId)id).material;
        return material == BlockMaterial::Water || material == BlockMaterial::Lava;
    }
    void OnAdded(World& world, int x, int y, int z) const override { world.ScheduleTick(x, y, z, id, tickRate); }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override { world.ScheduleTick(x, y, z, id, tickRate); }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (y > 0 && CanFallInto(world, x, y - 1, z)) {
            int meta = world.GetMeta(x, y, z);
            world.SetBlock(x, y, z, BLOCK_AIR);
            world.SpawnFallingBlock(x, y, z, id, meta);
        }
    }
};

class GravelBlock final : public FallingBlock {
public:
    void Drops(World&, int, int, int, int, JavaRandom& random, std::vector<ItemStack>& out) const override {
        out.push_back(random.NextInt(10) == 0 ? ItemStack(ITEM_FLINT, 1) : ItemStack(BLOCK_GRAVEL, 1));
    }
};

// ---- Logs (BlockLog): three kinds of wood; breaking one tells nearby leaves to
// check whether they're still attached to a tree.

class LogBlock final : public CubeBlock {
public:
    std::string DisplayName(int meta) const override { return "Wood"; }
    int Texture(int side, int meta) const override {
        // Spruce and birch end grain: 1.21's own, in tiles Beta left empty (213, 214).
        if (side == SIDE_UP || side == SIDE_DOWN) return meta == 1 ? 213 : meta == 2 ? 214 : 21;
        if (meta == 1) return 116;
        if (meta == 2) return 117;
        return 20;
    }
    int DamageDropped(int meta) const override { return meta & 3; }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        const int reach = 4;
        for (int dx = -reach; dx <= reach; dx++)
            for (int dy = -reach; dy <= reach; dy++)
                for (int dz = -reach; dz <= reach; dz++) {
                    if (world.GetBlock(x + dx, y + dy, z + dz) != BLOCK_LEAVES) continue;
                    int m = world.GetMeta(x + dx, y + dy, z + dz);
                    if ((m & 8) == 0) world.SetMeta(x + dx, y + dy, z + dz, m | 8, false);
                }
    }
};

// ---- Leaves (BlockLeaves): fancy (see-through) leaves that decay once no log
// is within 4 blocks, dropping a sapling now and then.

class LeavesBlock final : public Block {
public:
    LeavesBlock() {
        ticksRandomly = true;
        SetNotCube(*this, Shape::Cube, Layer::Cutout, 1);
        normalCube = true; // leaves are solid for suffocation and redstone
    }
    // Birch has its own leaves texture in 1.21 (tile 212); Beta tinted oak's.
    int Texture(int side, int meta) const override { return (meta & 3) == 1 ? 132 : (meta & 3) == 2 ? 212 : 52; }
    Color ItemTint(int meta) const override {
        if ((meta & 3) == 1) return SPRUCE_LEAVES_COLOR;
        if ((meta & 3) == 2) return BIRCH_LEAVES_COLOR;
        return OAK_LEAVES_ITEM_COLOR;
    }
    Color WorldTint(const World& world, int x, int y, int z, int side) const override {
        int meta = world.GetMeta(x, y, z) & 3;
        if (meta == 1) return SPRUCE_LEAVES_COLOR;
        if (meta == 2) return BIRCH_LEAVES_COLOR;
        return FOLIAGE_COLOR;
    }
    int DamageDropped(int meta) const override { return meta & 3; }
    void Drops(World&, int, int, int, int meta, JavaRandom& random, std::vector<ItemStack>& out) const override {
        if (random.NextInt(20) == 0) out.push_back(ItemStack(BLOCK_SAPLING, 1, meta & 3));
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                for (int dz = -1; dz <= 1; dz++) {
                    if (world.GetBlock(x + dx, y + dy, z + dz) != BLOCK_LEAVES) continue;
                    int m = world.GetMeta(x + dx, y + dy, z + dz);
                    world.SetMeta(x + dx, y + dy, z + dz, m | 8, false);
                }
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        int meta = world.GetMeta(x, y, z);
        if ((meta & 8) == 0) return;

        // Distance (through leaves) to the nearest log, up to 4.
        const int reach = 4, size = 32, half = size / 2;
        static thread_local int grid[size * size * size];
        auto at = [&](int dx, int dy, int dz) -> int& { return grid[(dx + half) * size * size + (dy + half) * size + (dz + half)]; };
        for (int dx = -reach; dx <= reach; dx++)
            for (int dy = -reach; dy <= reach; dy++)
                for (int dz = -reach; dz <= reach; dz++) {
                    int id = world.GetBlock(x + dx, y + dy, z + dz);
                    at(dx, dy, dz) = id == BLOCK_LOG ? 0 : (id == BLOCK_LEAVES ? -2 : -1);
                }
        for (int step = 1; step <= 4; step++)
            for (int dx = -reach; dx <= reach; dx++)
                for (int dy = -reach; dy <= reach; dy++)
                    for (int dz = -reach; dz <= reach; dz++) {
                        if (at(dx, dy, dz) != step - 1) continue;
                        for (const auto& o : SIDE_OFFSET) {
                            int nx = dx + o[0], ny = dy + o[1], nz = dz + o[2];
                            if (nx < -half || ny < -half || nz < -half || nx >= half || ny >= half || nz >= half) continue;
                            if (at(nx, ny, nz) == -2) at(nx, ny, nz) = step;
                        }
                    }
        if (at(0, 0, 0) >= 0) {
            world.SetMeta(x, y, z, meta & ~8, false);
        } else {
            world.DropBlockAsItem(x, y, z, id, meta);
            world.SetBlock(x, y, z, BLOCK_AIR);
        }
    }
};

// ---- Glass (BlockGlass/BlockBreakable): see-through; neighbors of the same
// kind hide the faces between them.

class GlassBlock : public CubeBlock {
public:
    GlassBlock() { SetNotCube(*this, Shape::Cube, Layer::Cutout, 0); dropId = 0; }
    bool ShouldSideBeRendered(const World& world, int nx, int ny, int nz, int side) const override {
        if (world.GetBlock(nx, ny, nz) == id) return false;
        return Block::ShouldSideBeRendered(world, nx, ny, nz, side);
    }
};

// ---- Ice (BlockIce): slippery, translucent, melts near bright block light,
// and leaves water behind when broken over something.

class IceBlock final : public GlassBlock {
public:
    IceBlock() {
        layer = Layer::Translucent;
        lightOpacity = 3;
        slipperiness = 0.98f;
        ticksRandomly = true;
    }
    void OnDestroyedByPlayer(World& world, int x, int y, int z, int meta, Player& player) const override {
        BlockMaterial below = world.GetMaterial(x, y - 1, z);
        if (GetMaterialInfo(below).solid || GetMaterialInfo(below).liquid) world.SetBlock(x, y, z, BLOCK_FLOWING_WATER);
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (world.GetBlockLight(x, y, z) > 11 - lightOpacity) {
            world.SetBlock(x, y, z, BLOCK_WATER);
        }
    }
};

// ---- Snow block (BlockSnowBlock): melts in bright block light.

class SnowBlock final : public CubeBlock {
public:
    SnowBlock() { ticksRandomly = true; dropId = ITEM_SNOWBALL; dropMin = dropMax = 4; }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (world.GetBlockLight(x, y, z) > 11) BreakAndDrop(world, x, y, z);
    }
};

// ---- Wool (BlockCloth): 16 colors from the metadata.

class WoolBlock final : public CubeBlock {
public:
    WoolBlock() { keepMeta = true; }
    int Texture(int side, int meta) const override {
        if (meta == 0) return 64;
        int m = ~meta & 15;
        return 113 + ((m & 8) >> 3) + (m & 7) * 16;
    }
    std::string DisplayName(int meta) const override {
        static const char* names[16] = { "Wool", "Orange Wool", "Magenta Wool", "Light Blue Wool", "Yellow Wool",
                                         "Lime Wool", "Pink Wool", "Gray Wool", "Light Gray Wool", "Cyan Wool",
                                         "Purple Wool", "Blue Wool", "Brown Wool", "Green Wool", "Red Wool", "Black Wool" };
        return names[meta & 15];
    }
};

// ---- Pumpkins and jack o'lanterns (BlockPumpkin): the face looks toward the
// player who placed it; they need solid ground.

class PumpkinBlock final : public CubeBlock {
public:
    bool lit = false;
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP || side == SIDE_DOWN) return 102;
        int face = lit ? 120 : 119;
        if ((meta == 2 && side == SIDE_NORTH) || (meta == 3 && side == SIDE_EAST) ||
            (meta == 0 && side == SIDE_SOUTH) || (meta == 1 && side == SIDE_WEST)) return face;
        return 118;
    }
    int ItemIconTexture(int) const override { return lit ? 120 : 119; }
    int DamageDropped(int) const override { return 0; }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && world.IsNormalCube(x, y - 1, z);
    }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        world.SetMeta(x, y, z, (int)std::floor(player.YawDegrees() * 4.0 / 360.0 + 2.5) & 3);
    }
};

// ---- Stone bricks (BlockStoneBrick): plain, mossy, cracked.

class StoneBrickBlock final : public CubeBlock {
public:
    StoneBrickBlock() { keepMeta = true; }
    int Texture(int side, int meta) const override {
        if (meta == 1) return 100;
        if (meta == 2) return 101;
        return 54;
    }
};

// ---- Silverfish stone (BlockSilverfish): looks like stone, cobblestone or stone bricks.

class MonsterEggBlock final : public CubeBlock {
public:
    MonsterEggBlock() { dropId = 0; }
    int Texture(int side, int meta) const override {
        if (meta == 1) return 16;
        if (meta == 2) return 54;
        return 1;
    }
};

// ---- Huge mushrooms (BlockMushroomCap): the metadata picks which faces are cap.

class MushroomCapBlock final : public CubeBlock {
public:
    int type = 0; // 0 brown, 1 red
    int Texture(int side, int meta) const override {
        int cap = 126 - type;
        if (meta == 10 && side > 1) return 141;
        if (meta >= 1 && meta <= 9 && side == 1) return cap;
        if (meta >= 1 && meta <= 3 && side == 2) return cap;
        if (meta >= 7 && meta <= 9 && side == 3) return cap;
        if ((meta == 1 || meta == 4 || meta == 7) && side == 4) return cap;
        if ((meta == 3 || meta == 6 || meta == 9) && side == 5) return cap;
        if (meta == 14) return cap;
        if (meta == 15) return 141;
        return 142;
    }
    int ItemIconTexture(int) const override { return 126 - type; }
    void Drops(World&, int, int, int, int, JavaRandom& random, std::vector<ItemStack>& out) const override {
        int count = random.NextInt(10) - 7;
        if (count > 0) out.push_back(ItemStack(type == 0 ? BLOCK_BROWN_MUSHROOM : BLOCK_RED_MUSHROOM, count));
    }
};

// ---- Soul sand (BlockSoulSand): a little lower than a full block, and slows you.

class SoulSandBlock final : public CubeBlock {
public:
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>& out) const override {
        out.push_back(MakeBox(0, 0, 0, 1, 0.875f, 1));
    }
    void OnPlayerCollided(World&, int, int, int, Player& player) const override { player.MultiplyMotion(0.4, 1.0); }
};

// ---- TNT (BlockTNT): redstone lights it, as does hitting it with flint and
// steel before breaking it; explosions set off more TNT with a shorter fuse.

class TntBlock final : public CubeBlock {
public:
    TntBlock() { dropId = 0; }
    int Texture(int side, int meta) const override {
        if (side == SIDE_DOWN) return 10;
        if (side == SIDE_UP) return 9;
        return 8;
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.IsBlockIndirectlyGettingPowered(x, y, z)) {
            OnDestroyedByPlayer(world, x, y, z, 1, *world.GetPlayer());
            world.SetBlock(x, y, z, BLOCK_AIR);
        }
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        if (neighbor > 0 && GetBlockDef((BlockId)neighbor).CanProvidePower() && world.IsBlockIndirectlyGettingPowered(x, y, z)) {
            OnDestroyedByPlayer(world, x, y, z, 1, *world.GetPlayer());
            world.SetBlock(x, y, z, BLOCK_AIR);
        }
    }
    void OnDestroyedByPlayer(World& world, int x, int y, int z, int meta, Player&) const override {
        if ((meta & 1) == 0) {
            DropStack(world, x, y, z, ItemStack(BLOCK_TNT, 1));
        } else {
            world.SpawnPrimedTnt(x + 0.5, y + 0.5, z + 0.5, 80);
            world.PlaySound("random.fuse", x + 0.5, y + 0.5, z + 0.5, 1.0f, 1.0f);
        }
    }
    void OnExploded(World& world, int x, int y, int z) const override {
        int fuse = world.Random().NextInt(80 / 4) + 80 / 8;
        world.SpawnPrimedTnt(x + 0.5, y + 0.5, z + 0.5, fuse);
    }
    void OnClicked(World& world, int x, int y, int z, Player& player) const override {
        const ItemStack& held = player.HeldItem();
        if (!held.IsEmpty() && held.id == ITEM_FLINT_AND_STEEL) world.SetMeta(x, y, z, 1, false);
    }
};

// ---- Bookshelves drop nothing in Beta; glowstone drops its dust.

class CobwebBlock final : public Block {
public:
    CobwebBlock() { SetNotCube(*this, Shape::Cross, Layer::Cutout, 1); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    void OnPlayerCollided(World&, int, int, int, Player& player) const override { player.SetInWeb(); }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(ITEM_STRING, 1));
    }
};

// ---- Locked chest (BlockLockedChest): the April Fools' chest; glows, and vanishes by itself.

class LockedChestBlock final : public CubeBlock {
public:
    LockedChestBlock() { ticksRandomly = true; dropId = 0; }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP || side == SIDE_DOWN) return 25;
        return side == SIDE_SOUTH ? 27 : 26;
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override { world.SetBlock(x, y, z, BLOCK_AIR); }
};

// ---- Mob spawner: a cage with nothing in it (no mobs yet), spinning its fire.

class MobSpawnerBlock final : public CubeBlock {
public:
    MobSpawnerBlock() { SetNotCube(*this, Shape::Cube, Layer::Cutout, 0); dropId = 0; normalCube = true; }
    bool HasTileEntity() const override { return true; }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) world.SetTileEntity(x, y, z, CreateTileEntity(TileKind::MobSpawner));
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override { world.RemoveTileEntity(x, y, z); }
    int PistonMobility(int) const override { return 2; }
};

class BedrockBlock final : public CubeBlock {
public:
    int PistonMobility(int) const override { return 2; }
};

class ObsidianBlock final : public CubeBlock {
public:
    int PistonMobility(int) const override { return 2; }
};

} // namespace

void RegisterBasicBlocks(BlockRegistry& r) {
    auto cube = [&](int id, const char* name, int tex, float hardness, BlockMaterial m, StepSound s) -> CubeBlock& {
        return Props(r.Add<CubeBlock>(id), name, tex, hardness, m, s);
    };

    { auto& b = cube(BLOCK_STONE, "Stone", 1, 1.5f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 10); b.dropId = BLOCK_COBBLESTONE; }
    Props(r.Add<GrassBlock>(BLOCK_GRASS), "Grass Block", 3, 0.6f, BlockMaterial::Grass, StepSound::Grass);
    cube(BLOCK_DIRT, "Dirt", 2, 0.5f, BlockMaterial::Ground, StepSound::Gravel);
    { auto& b = cube(BLOCK_COBBLESTONE, "Cobblestone", 16, 2.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 10); }
    { auto& b = cube(BLOCK_PLANKS, "Wooden Planks", 4, 2.0f, BlockMaterial::Wood, StepSound::Wood); SetResistance(b, 5); }
    { auto& b = Props(r.Add<BedrockBlock>(BLOCK_BEDROCK), "Bedrock", 17, -1.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 6000000.0f); }
    Props(r.Add<FallingBlock>(BLOCK_SAND), "Sand", 18, 0.5f, BlockMaterial::Sand, StepSound::Sand);
    Props(r.Add<GravelBlock>(BLOCK_GRAVEL), "Gravel", 19, 0.6f, BlockMaterial::Sand, StepSound::Gravel);
    { auto& b = cube(BLOCK_GOLD_ORE, "Gold Ore", 32, 3.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 5); }
    { auto& b = cube(BLOCK_IRON_ORE, "Iron Ore", 33, 3.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 5); }
    { auto& b = cube(BLOCK_COAL_ORE, "Coal Ore", 34, 3.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 5); b.dropId = ITEM_COAL; }
    Props(r.Add<LogBlock>(BLOCK_LOG), "Wood", 20, 2.0f, BlockMaterial::Wood, StepSound::Wood);
    Props(r.Add<LeavesBlock>(BLOCK_LEAVES), "Leaves", 52, 0.2f, BlockMaterial::Leaves, StepSound::Grass);
    cube(BLOCK_SPONGE, "Sponge", 48, 0.6f, BlockMaterial::Sponge, StepSound::Grass);
    Props(r.Add<GlassBlock>(BLOCK_GLASS), "Glass", 49, 0.3f, BlockMaterial::Glass, StepSound::Glass);
    { auto& b = cube(BLOCK_LAPIS_ORE, "Lapis Lazuli Ore", 160, 3.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 5);
      b.dropId = ITEM_DYE; b.dropDamage = DYE_BLUE; b.dropMin = 4; b.dropMax = 8; }
    { auto& b = cube(BLOCK_LAPIS_BLOCK, "Lapis Lazuli Block", 144, 3.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 5); }
    { auto& b = cube(BLOCK_SANDSTONE, "Sandstone", 192, 0.8f, BlockMaterial::Rock, StepSound::Stone); b.top = 176; b.bottom = 208; }
    Props(r.Add<CobwebBlock>(BLOCK_WEB), "Cobweb", 11, 4.0f, BlockMaterial::Web, StepSound::Cloth);
    Props(r.Add<WoolBlock>(BLOCK_WOOL), "Wool", 64, 0.8f, BlockMaterial::Cloth, StepSound::Cloth);
    { auto& b = cube(BLOCK_GOLD_BLOCK, "Block of Gold", 23, 3.0f, BlockMaterial::Iron, StepSound::Metal); SetResistance(b, 10); }
    { auto& b = cube(BLOCK_IRON_BLOCK, "Block of Iron", 22, 5.0f, BlockMaterial::Iron, StepSound::Metal); SetResistance(b, 10); }
    { auto& b = cube(BLOCK_BRICKS, "Bricks", 7, 2.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 10); }
    Props(r.Add<TntBlock>(BLOCK_TNT), "TNT", 8, 0.0f, BlockMaterial::Tnt, StepSound::Grass);
    { auto& b = cube(BLOCK_BOOKSHELF, "Bookshelf", 35, 1.5f, BlockMaterial::Wood, StepSound::Wood); b.top = 4; b.bottom = 4; b.dropId = 0; }
    { auto& b = cube(BLOCK_MOSSY_COBBLESTONE, "Moss Stone", 36, 2.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 10); }
    { auto& b = Props(r.Add<ObsidianBlock>(BLOCK_OBSIDIAN), "Obsidian", 37, 10.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 2000); }
    Props(r.Add<MobSpawnerBlock>(BLOCK_MOB_SPAWNER), "Monster Spawner", 65, 5.0f, BlockMaterial::Rock, StepSound::Metal);
    { auto& b = cube(BLOCK_DIAMOND_ORE, "Diamond Ore", 50, 3.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 5); b.dropId = ITEM_DIAMOND; }
    { auto& b = cube(BLOCK_DIAMOND_BLOCK, "Block of Diamond", 24, 5.0f, BlockMaterial::Iron, StepSound::Metal); SetResistance(b, 10); }
    Props(r.Add<IceBlock>(BLOCK_ICE), "Ice", 67, 0.5f, BlockMaterial::Ice, StepSound::Glass);
    Props(r.Add<SnowBlock>(BLOCK_SNOW), "Snow", 66, 0.2f, BlockMaterial::BuiltSnow, StepSound::Cloth);
    { auto& b = cube(BLOCK_CLAY, "Clay", 72, 0.6f, BlockMaterial::Clay, StepSound::Gravel); b.dropId = ITEM_CLAY_BALL; b.dropMin = b.dropMax = 4; }
    Props(r.Add<PumpkinBlock>(BLOCK_PUMPKIN), "Pumpkin", 118, 1.0f, BlockMaterial::Pumpkin, StepSound::Wood);
    cube(BLOCK_NETHERRACK, "Netherrack", 103, 0.4f, BlockMaterial::Rock, StepSound::Stone);
    Props(r.Add<SoulSandBlock>(BLOCK_SOUL_SAND), "Soul Sand", 104, 0.5f, BlockMaterial::Sand, StepSound::Sand);
    { auto& b = cube(BLOCK_GLOWSTONE, "Glowstone", 105, 0.3f, BlockMaterial::Glass, StepSound::Glass); b.lightValue = 15;
      b.dropId = ITEM_GLOWSTONE_DUST; b.dropMin = 2; b.dropMax = 4; }
    { auto& b = Props(r.Add<PumpkinBlock>(BLOCK_JACK_O_LANTERN), "Jack 'o' Lantern", 118, 1.0f, BlockMaterial::Pumpkin, StepSound::Wood);
      b.lit = true; b.lightValue = 15; }
    { auto& b = Props(r.Add<LockedChestBlock>(BLOCK_LOCKED_CHEST), "Locked chest", 26, 0.0f, BlockMaterial::Wood, StepSound::Wood); b.lightValue = 15; }
    Props(r.Add<MonsterEggBlock>(BLOCK_MONSTER_EGG), "Stone", 1, 0.75f, BlockMaterial::Clay, StepSound::Stone);
    { auto& b = Props(r.Add<StoneBrickBlock>(BLOCK_STONE_BRICKS), "Stone Bricks", 54, 1.5f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 10); }
    { auto& b = Props(r.Add<MushroomCapBlock>(BLOCK_BROWN_MUSHROOM_BLOCK), "Mushroom", 142, 0.2f, BlockMaterial::Wood, StepSound::Wood); b.type = 0; }
    { auto& b = Props(r.Add<MushroomCapBlock>(BLOCK_RED_MUSHROOM_BLOCK), "Mushroom", 142, 0.2f, BlockMaterial::Wood, StepSound::Wood); b.type = 1; }
    { auto& b = cube(BLOCK_MELON, "Melon", 136, 1.0f, BlockMaterial::Pumpkin, StepSound::Wood); b.top = 137; b.bottom = 137;
      b.dropId = ITEM_MELON; b.dropMin = 3; b.dropMax = 7; }
}
