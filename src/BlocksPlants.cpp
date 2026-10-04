#include "BlocksCommon.h"
#include "Colors.h"
#include "TreeGen.h"

#include <algorithm>
#include <cmath>

namespace {

// Light for plants: sky light (ignoring night) or block light, whichever is
// brighter (World.getFullBlockLightValue).
int FullLight(const World& world, int x, int y, int z) {
    return std::max(world.GetSkyLight(x, y, z), world.GetBlockLight(x, y, z));
}

// ---- BlockFlower: small plants on dirt or grass that need light, pop off
// when their ground goes, and have no collision.

class PlantBlock : public Block {
public:
    float halfWidth = 0.2f;     // bounds (setBlockBounds(0.5 - f, 0, 0.5 - f, 0.5 + f, f * 3, 0.5 + f))
    float heightScale = 3.0f;

    PlantBlock() {
        SetNotCube(*this, Shape::Cross, Layer::Cutout, 0);
        ticksRandomly = true;
    }
    virtual bool CanGrowOn(int below) const { return below == BLOCK_GRASS || below == BLOCK_DIRT || below == BLOCK_FARMLAND; }
    BlockBox PlantBox() const {
        return MakeBox(0.5f - halfWidth, 0, 0.5f - halfWidth, 0.5f + halfWidth, std::min(1.0f, halfWidth * heightScale), 0.5f + halfWidth);
    }
    BlockBox Bounds(const World&, int, int, int) const override { return PlantBox(); }
    BlockBox ItemBounds(int) const override { return PlantBox(); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && CanGrowOn(world.GetBlock(x, y - 1, z));
    }
    bool CanStay(const World& world, int x, int y, int z) const override {
        return (FullLight(world, x, y, z) >= 8 || world.GetSkyLight(x, y, z) >= 15) && CanGrowOn(world.GetBlock(x, y - 1, z));
    }
    void CheckStay(World& world, int x, int y, int z) const {
        if (!CanStay(world, x, y, z)) BreakAndDrop(world, x, y, z);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override { CheckStay(world, x, y, z); }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override { CheckStay(world, x, y, z); }
};

// ---- BlockSapling: grows into an oak (sometimes a big one), a spruce or a birch.

class SaplingBlock final : public PlantBlock {
public:
    SaplingBlock() { halfWidth = 0.4f; heightScale = 2.0f; }
    int Texture(int side, int meta) const override {
        meta &= 3;
        if (meta == 1) return 63;
        if (meta == 2) return 79;
        return 15;
    }
    int DamageDropped(int meta) const override { return meta & 3; }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        PlantBlock::OnTick(world, x, y, z, random);
        if (world.GetBlock(x, y, z) != id) return;
        if (world.GetLight(x, y + 1, z) >= 9 && random.NextInt(30) == 0) {
            int meta = world.GetMeta(x, y, z);
            if ((meta & 8) == 0) world.SetMeta(x, y, z, meta | 8, false);
            else GrowTree(world, x, y, z, random);
        }
    }
    void GrowTree(World& world, int x, int y, int z, JavaRandom& random) const {
        int type = world.GetMeta(x, y, z) & 3;
        world.SetBlock(x, y, z, BLOCK_AIR, 0, false);
        WorldTreeAccess access(world);
        bool grew;
        if (type == 1) grew = GrowSpruceTree(access, random, x, y, z);
        else if (type == 2) grew = GrowBirchTree(access, random, x, y, z);
        else if (random.NextInt(10) == 0) grew = GrowBigOakTree(access, random, x, y, z);
        else grew = GrowOakTree(access, random, x, y, z);
        if (!grew) world.SetBlock(x, y, z, id, type, false);
    }
    bool Fertilize(World& world, int x, int y, int z) const override {
        GrowTree(world, x, y, z, world.Random());
        return true;
    }
};

// ---- BlockTallGrass: dead shrub, grass and fern; drops seeds now and then.

class TallGrassBlock final : public PlantBlock {
public:
    TallGrassBlock() { halfWidth = 0.4f; heightScale = 2.0f; }
    std::string DisplayName(int meta) const override {
        if (meta == 0) return "Shrub";
        if (meta == 2) return "Fern";
        return "Grass";
    }
    int Texture(int side, int meta) const override {
        if (meta == 1) return 39;
        if (meta == 2) return 56;
        return 55;
    }
    Color ItemTint(int meta) const override { return meta == 0 ? WHITE : GRASS_ITEM_COLOR; }
    Color WorldTint(const World& world, int x, int y, int z, int) const override {
        return world.GetMeta(x, y, z) == 0 ? WHITE : GRASS_COLOR;
    }
    int DamageDropped(int meta) const override { return meta; }
    void Drops(World&, int, int, int, int, JavaRandom& random, std::vector<ItemStack>& out) const override {
        if (random.NextInt(8) == 0) out.push_back(ItemStack(ITEM_SEEDS, 1));
    }
};

class DeadBushBlock final : public PlantBlock {
public:
    DeadBushBlock() { halfWidth = 0.4f; heightScale = 2.0f; }
    bool CanGrowOn(int below) const override { return below == BLOCK_SAND; }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}
};

// ---- BlockMushroom: likes the dark, grows on any opaque block, spreads slowly.

class MushroomBlock final : public PlantBlock {
public:
    int type = 0; // 0 brown, 1 red
    MushroomBlock() { heightScale = 2.0f; }
    bool CanGrowOn(int below) const override { return BlockIsOpaqueCube((BlockId)below); }
    bool CanStay(const World& world, int x, int y, int z) const override {
        if (y < 0 || y >= Chunk::HEIGHT) return false;
        return FullLight(world, x, y, z) < 13 && CanGrowOn(world.GetBlock(x, y - 1, z));
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (random.NextInt(100) == 0) {
            int tx = x + random.NextInt(3) - 1;
            int ty = y + random.NextInt(2) - random.NextInt(2);
            int tz = z + random.NextInt(3) - 1;
            if (world.IsAir(tx, ty, tz) && CanStay(world, tx, ty, tz)) world.SetBlock(tx, ty, tz, id);
        }
    }
    bool Fertilize(World& world, int x, int y, int z) const override {
        int meta = world.GetMeta(x, y, z);
        world.SetBlock(x, y, z, BLOCK_AIR, 0, false);
        WorldTreeAccess access(world);
        if (!GrowHugeMushroom(access, world.Random(), x, y, z, type)) {
            world.SetBlock(x, y, z, id, meta, false);
            return false;
        }
        return true;
    }
};

// Wheat and stems grow faster on wet farmland, and slower crowded in rows
// (BlockCrops.getGrowthRate / BlockStem.getGrowthModifier).
float GrowthRate(const World& world, int x, int y, int z, int id) {
    float rate = 1.0f;
    int n = world.GetBlock(x, y, z - 1), s = world.GetBlock(x, y, z + 1);
    int w = world.GetBlock(x - 1, y, z), e = world.GetBlock(x + 1, y, z);
    int nw = world.GetBlock(x - 1, y, z - 1), ne = world.GetBlock(x + 1, y, z - 1);
    int se = world.GetBlock(x + 1, y, z + 1), sw = world.GetBlock(x - 1, y, z + 1);
    bool alongX = w == id || e == id;
    bool alongZ = n == id || s == id;
    bool diagonal = nw == id || ne == id || se == id || sw == id;
    for (int xx = x - 1; xx <= x + 1; xx++)
        for (int zz = z - 1; zz <= z + 1; zz++) {
            float soil = 0.0f;
            if (world.GetBlock(xx, y - 1, zz) == BLOCK_FARMLAND) {
                soil = 1.0f;
                if (world.GetMeta(xx, y - 1, zz) > 0) soil = 3.0f;
            }
            if (xx != x || zz != z) soil /= 4.0f;
            rate += soil;
        }
    if (diagonal || (alongX && alongZ)) rate /= 2.0f;
    return rate;
}

// ---- BlockCrops (wheat): 8 stages on farmland.

class CropsBlock final : public PlantBlock {
public:
    CropsBlock() { SetNotCube(*this, Shape::Crops, Layer::Cutout, 0); halfWidth = 0.5f; heightScale = 0.5f; }
    bool CanGrowOn(int below) const override { return below == BLOCK_FARMLAND; }
    int Texture(int side, int meta) const override { return 88 + std::clamp(meta, 0, 7); }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        PlantBlock::OnTick(world, x, y, z, random);
        if (world.GetBlock(x, y, z) != id) return;
        if (world.GetLight(x, y + 1, z) >= 9) {
            int stage = world.GetMeta(x, y, z);
            if (stage < 7) {
                float rate = GrowthRate(world, x, y, z, id);
                if (random.NextInt((int)(100.0f / rate)) == 0) world.SetMeta(x, y, z, stage + 1);
            }
        }
    }
    bool Fertilize(World& world, int x, int y, int z) const override {
        world.SetMeta(x, y, z, 7);
        return true;
    }
    void Drops(World& world, int x, int y, int z, int meta, JavaRandom& random, std::vector<ItemStack>& out) const override {
        if (meta == 7) out.push_back(ItemStack(ITEM_WHEAT, 1));
        for (int i = 0; i < 3; i++) {
            if (random.NextInt(15) <= meta) out.push_back(ItemStack(ITEM_SEEDS, 1));
        }
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(ITEM_SEEDS, 1); }
};

// ---- BlockFarmland: wet (darker) near water, dries out and turns back to dirt,
// and gets trampled if you land on it.

class FarmlandBlock final : public Block {
public:
    FarmlandBlock() {
        ticksRandomly = true;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 255;
    }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP) return meta > 0 ? 86 : 87;
        return 2;
    }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0, 0, 0, 1, 0.9375f, 1); }
    BlockBox ItemBounds(int) const override { return MakeBox(0, 0, 0, 1, 0.9375f, 1); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>& out) const override { out.push_back(BlockBox()); }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(BLOCK_DIRT, 1));
    }
    static bool WaterNearby(const World& world, int x, int y, int z) {
        for (int xx = x - 4; xx <= x + 4; xx++)
            for (int yy = y; yy <= y + 1; yy++)
                for (int zz = z - 4; zz <= z + 4; zz++)
                    if (world.GetMaterial(xx, yy, zz) == BlockMaterial::Water) return true;
        return false;
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (random.NextInt(5) != 0) return;
        if (WaterNearby(world, x, y, z)) {
            world.SetMeta(x, y, z, 7);
        } else {
            int meta = world.GetMeta(x, y, z);
            if (meta > 0) world.SetMeta(x, y, z, meta - 1);
            else if (world.GetBlock(x, y + 1, z) != BLOCK_WHEAT) world.SetBlock(x, y, z, BLOCK_DIRT);
        }
    }
    void OnFallenUpon(World& world, int x, int y, int z, Player&, float distance) const override {
        if (world.Random().NextFloat() < distance - 0.5f) world.SetBlock(x, y, z, BLOCK_DIRT);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (GetMaterialInfo(world.GetMaterial(x, y + 1, z)).solid) world.SetBlock(x, y, z, BLOCK_DIRT);
    }
};

// ---- BlockCactus: grows 3 tall on sand, can't touch anything solid, hurts.

class CactusBlock final : public Block {
public:
    CactusBlock() {
        ticksRandomly = true;
        SetNotCube(*this, Shape::Cactus, Layer::Cutout, 0);
    }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP) return 69;
        if (side == SIDE_DOWN) return 71;
        return 70;
    }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0.0625f, 0, 0.0625f, 0.9375f, 1, 0.9375f); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>& out) const override {
        out.push_back(MakeBox(0.0625f, 0, 0.0625f, 0.9375f, 0.9375f, 0.9375f));
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && CanStay(world, x, y, z);
    }
    bool CanStay(const World& world, int x, int y, int z) const override {
        if (GetMaterialInfo(world.GetMaterial(x - 1, y, z)).solid) return false;
        if (GetMaterialInfo(world.GetMaterial(x + 1, y, z)).solid) return false;
        if (GetMaterialInfo(world.GetMaterial(x, y, z - 1)).solid) return false;
        if (GetMaterialInfo(world.GetMaterial(x, y, z + 1)).solid) return false;
        int below = world.GetBlock(x, y - 1, z);
        return below == BLOCK_CACTUS || below == BLOCK_SAND;
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!CanStay(world, x, y, z)) BreakAndDrop(world, x, y, z);
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (!world.IsAir(x, y + 1, z)) return;
        int height = 1;
        while (world.GetBlock(x, y - height, z) == id) height++;
        if (height >= 3) return;
        int age = world.GetMeta(x, y, z);
        if (age == 15) {
            world.SetBlock(x, y + 1, z, id);
            world.SetMeta(x, y, z, 0);
        } else {
            world.SetMeta(x, y, z, age + 1);
        }
    }
    void OnPlayerCollided(World&, int, int, int, Player& player) const override { player.Hurt(1.0f); }
    int DamageDropped(int) const override { return 0; }
};

// ---- BlockReed (sugar cane): grows 3 tall next to water.

class ReedsBlock final : public Block {
public:
    ReedsBlock() {
        ticksRandomly = true;
        SetNotCube(*this, Shape::Cross, Layer::Cutout, 0);
    }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0.125f, 0, 0.125f, 0.875f, 1, 0.875f); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        return CanStay(world, x, y, z);
    }
    bool CanStay(const World& world, int x, int y, int z) const override {
        int below = world.GetBlock(x, y - 1, z);
        if (below == id) return true;
        if (below != BLOCK_GRASS && below != BLOCK_DIRT && below != BLOCK_SAND) return false;
        return world.GetMaterial(x - 1, y - 1, z) == BlockMaterial::Water || world.GetMaterial(x + 1, y - 1, z) == BlockMaterial::Water ||
               world.GetMaterial(x, y - 1, z - 1) == BlockMaterial::Water || world.GetMaterial(x, y - 1, z + 1) == BlockMaterial::Water;
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!CanStay(world, x, y, z)) BreakAndDrop(world, x, y, z);
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (!world.IsAir(x, y + 1, z)) return;
        int height = 1;
        while (world.GetBlock(x, y - height, z) == id) height++;
        if (height >= 3) return;
        int age = world.GetMeta(x, y, z);
        if (age == 15) {
            world.SetBlock(x, y + 1, z, id);
            world.SetMeta(x, y, z, 0);
        } else {
            world.SetMeta(x, y, z, age + 1);
        }
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(ITEM_REEDS, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(ITEM_REEDS, 1); }
};

// ---- BlockStem: pumpkin and melon stems grow, then grow fruit beside them.

class StemBlock final : public PlantBlock {
public:
    int fruit = BLOCK_PUMPKIN;
    int seeds = ITEM_PUMPKIN_SEEDS;

    StemBlock() { SetNotCube(*this, Shape::Stem, Layer::Cutout, 0); }
    bool CanGrowOn(int below) const override { return below == BLOCK_FARMLAND; }
    int Texture(int side, int meta) const override { return 111; }
    Color ItemTint(int meta) const override {
        return Color{ (unsigned char)(meta * 32), (unsigned char)(255 - meta * 8), (unsigned char)(meta * 4), 255 };
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int meta = world.GetMeta(x, y, z);
        return MakeBox(0.375f, 0, 0.375f, 0.625f, (meta * 2 + 2) / 16.0f, 0.625f);
    }
    // Which side the grown fruit is on, or -1 (BlockStem.func_35296_f).
    int FruitSide(const World& world, int x, int y, int z) const {
        if (world.GetMeta(x, y, z) < 7) return -1;
        if (world.GetBlock(x - 1, y, z) == fruit) return SIDE_WEST;
        if (world.GetBlock(x + 1, y, z) == fruit) return SIDE_EAST;
        if (world.GetBlock(x, y, z - 1) == fruit) return SIDE_NORTH;
        if (world.GetBlock(x, y, z + 1) == fruit) return SIDE_SOUTH;
        return -1;
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        PlantBlock::OnTick(world, x, y, z, random);
        if (world.GetBlock(x, y, z) != id) return;
        if (world.GetLight(x, y + 1, z) < 9) return;
        float rate = GrowthRate(world, x, y, z, id);
        if (random.NextInt((int)(25.0f / rate) + 1) != 0) return;
        int age = world.GetMeta(x, y, z);
        if (age < 7) {
            world.SetMeta(x, y, z, age + 1);
            return;
        }
        if (FruitSide(world, x, y, z) >= 0) return;
        int dir = random.NextInt(4);
        int fx = x, fz = z;
        if (dir == 0) fx--;
        if (dir == 1) fx++;
        if (dir == 2) fz--;
        if (dir == 3) fz++;
        int ground = world.GetBlock(fx, y - 1, fz);
        if (world.IsAir(fx, y, fz) && (ground == BLOCK_FARMLAND || ground == BLOCK_DIRT || ground == BLOCK_GRASS)) {
            world.SetBlock(fx, y, fz, fruit);
        }
    }
    bool Fertilize(World& world, int x, int y, int z) const override {
        world.SetMeta(x, y, z, 7);
        return true;
    }
    void Drops(World&, int, int, int, int meta, JavaRandom& random, std::vector<ItemStack>& out) const override {
        for (int i = 0; i < 3; i++) {
            if (random.NextInt(15) <= meta) out.push_back(ItemStack(seeds, 1));
        }
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(seeds, 1); }
};

// ---- BlockVine: hangs on the sides of blocks, climbable, spreads slowly.
// Metadata bits say which sides it's on: 1 south, 2 west, 4 north, 8 east.

class VineBlock final : public Block {
public:
    VineBlock() {
        ticksRandomly = true;
        SetNotCube(*this, Shape::Vine, Layer::Cutout, 0);
    }
    static int BitForSide(int side) {
        if (side == SIDE_SOUTH) return 1;
        if (side == SIDE_WEST) return 2;
        if (side == SIDE_NORTH) return 4;
        if (side == SIDE_EAST) return 8;
        return 0;
    }
    static bool Supports(const World& world, int x, int y, int z) {
        int id = world.GetBlock(x, y, z);
        return id != BLOCK_AIR && BlockIsOpaqueCube((BlockId)id) && GetBlockDef((BlockId)id).BlocksMovement();
    }
    Color ItemTint(int) const override { return FOLIAGE_COLOR; }
    Color WorldTint(const World&, int, int, int, int) const override { return FOLIAGE_COLOR; }
    bool IsLadder(int) const override { return true; }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int meta = world.GetMeta(x, y, z);
        const float t = 0.0625f;
        float x0 = 1, y0 = 1, z0 = 1, x1 = 0, y1 = 0, z1 = 0;
        bool any = meta > 0;
        if (meta & 2) { x1 = std::max(x1, t); x0 = 0; y0 = 0; y1 = 1; z0 = 0; z1 = 1; }
        if (meta & 8) { x0 = std::min(x0, 1 - t); x1 = 1; y0 = 0; y1 = 1; z0 = 0; z1 = 1; }
        if (meta & 4) { z1 = std::max(z1, t); z0 = 0; x0 = 0; x1 = 1; y0 = 0; y1 = 1; }
        if (meta & 1) { z0 = std::min(z0, 1 - t); z1 = 1; x0 = 0; x1 = 1; y0 = 0; y1 = 1; }
        if (!any && Supports(world, x, y + 1, z)) { y0 = std::min(y0, 1 - t); y1 = 1; x0 = 0; x1 = 1; z0 = 0; z1 = 1; }
        if (x0 > x1) return MakeBox(0, 0, 0, 1, 1, 1);
        return MakeBox(x0, y0, z0, x1, y1, z1);
    }
    bool CanPlaceOnSide(const World& world, int x, int y, int z, int side) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        switch (side) {
            case SIDE_UP: return Supports(world, x, y + 1, z);
            case SIDE_NORTH: return Supports(world, x, y, z + 1);
            case SIDE_SOUTH: return Supports(world, x, y, z - 1);
            case SIDE_WEST: return Supports(world, x + 1, y, z);
            case SIDE_EAST: return Supports(world, x - 1, y, z);
            default: return false;
        }
    }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player&) const override {
        // Clicking a block's north face hangs the vine on that block, to our south.
        int bit = 0;
        if (side == SIDE_NORTH) bit = 1;
        if (side == SIDE_SOUTH) bit = 4;
        if (side == SIDE_WEST) bit = 8;
        if (side == SIDE_EAST) bit = 2;
        world.SetMeta(x, y, z, bit);
    }
    // Keeps only the sides still backed by a block (or by the vine above).
    int SupportedSides(const World& world, int x, int y, int z, int meta) const {
        int above = world.GetBlock(x, y + 1, z) == id ? world.GetMeta(x, y + 1, z) : 0;
        int result = 0;
        if ((meta & 1) && (Supports(world, x, y, z + 1) || (above & 1))) result |= 1;
        if ((meta & 2) && (Supports(world, x - 1, y, z) || (above & 2))) result |= 2;
        if ((meta & 4) && (Supports(world, x, y, z - 1) || (above & 4))) result |= 4;
        if ((meta & 8) && (Supports(world, x + 1, y, z) || (above & 8))) result |= 8;
        return result;
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        int meta = world.GetMeta(x, y, z);
        int kept = SupportedSides(world, x, y, z, meta);
        if (kept == 0 && !Supports(world, x, y + 1, z)) {
            world.SetBlock(x, y, z, BLOCK_AIR);
        } else if (kept != meta) {
            world.SetMeta(x, y, z, kept);
        }
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (random.NextInt(4) != 0) return;
        // Too many vines around already?
        int nearby = 5;
        for (int xx = x - 4; xx <= x + 4 && nearby > 0; xx++)
            for (int zz = z - 4; zz <= z + 4 && nearby > 0; zz++)
                for (int yy = y - 1; yy <= y + 1 && nearby > 0; yy++)
                    if (world.GetBlock(xx, yy, zz) == id) nearby--;
        bool crowded = nearby <= 0;
        int meta = world.GetMeta(x, y, z);
        int choice = random.NextInt(6);
        if (choice == 0 && y > 1) {
            // Hang down.
            int below = world.GetBlock(x, y - 1, z);
            if (below == BLOCK_AIR) {
                int bits = random.NextInt(16) & meta;
                if (bits > 0) world.SetBlock(x, y - 1, z, id, bits);
            } else if (below == id) {
                int bits = random.NextInt(16) & meta;
                int belowMeta = world.GetMeta(x, y - 1, z);
                if ((belowMeta | bits) != belowMeta) world.SetMeta(x, y - 1, z, belowMeta | bits);
            }
        } else if (choice >= 2 && !crowded) {
            // Creep onto a new wall beside us.
            static const int sides[4] = { SIDE_SOUTH, SIDE_WEST, SIDE_NORTH, SIDE_EAST };
            int side = sides[choice - 2];
            int bit = BitForSide(side);
            if (meta & bit) return;
            int nx = x + SIDE_OFFSET[side][0], nz = z + SIDE_OFFSET[side][2];
            if (Supports(world, nx, y, nz)) world.SetMeta(x, y, z, meta | bit);
        }
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(BLOCK_VINE, 1); }
};

// ---- BlockSnow (snow layer): a thin blanket that melts in bright block light.

class SnowLayerBlock final : public Block {
public:
    SnowLayerBlock() {
        ticksRandomly = true;
        SetNotCube(*this, Shape::Cube, Layer::Opaque, 0);
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int layers = (world.GetMeta(x, y, z) & 7) + 1;
        return MakeBox(0, 0, 0, 1, layers * 2 / 16.0f, 1);
    }
    BlockBox ItemBounds(int) const override { return MakeBox(0, 0, 0, 1, 0.125f, 1); }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        if ((world.GetMeta(x, y, z) & 7) >= 3) out.push_back(MakeBox(0, 0, 0, 1, 0.5f, 1));
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        int below = world.GetBlock(x, y - 1, z);
        if (below == BLOCK_AIR || !BlockIsOpaqueCube((BlockId)below)) return false;
        return GetMaterialInfo(world.GetMaterial(x, y - 1, z)).solid;
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!CanPlaceAt(world, x, y, z)) world.SetBlock(x, y, z, BLOCK_AIR);
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (world.GetBlockLight(x, y, z) > 11) BreakAndDrop(world, x, y, z);
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(ITEM_SNOWBALL, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(BLOCK_SNOW_LAYER, 1); }
};

} // namespace

void RegisterPlantBlocks(BlockRegistry& r) {
    Props(r.Add<SaplingBlock>(BLOCK_SAPLING), "Sapling", 15, 0.0f, BlockMaterial::Plants, StepSound::Grass);
    Props(r.Add<TallGrassBlock>(BLOCK_TALL_GRASS), "Grass", 39, 0.0f, BlockMaterial::Vine, StepSound::Grass);
    Props(r.Add<DeadBushBlock>(BLOCK_DEAD_BUSH), "Dead Bush", 55, 0.0f, BlockMaterial::Vine, StepSound::Grass);
    Props(r.Add<PlantBlock>(BLOCK_DANDELION), "Flower", 13, 0.0f, BlockMaterial::Plants, StepSound::Grass);
    Props(r.Add<PlantBlock>(BLOCK_ROSE), "Rose", 12, 0.0f, BlockMaterial::Plants, StepSound::Grass);
    { auto& b = Props(r.Add<MushroomBlock>(BLOCK_BROWN_MUSHROOM), "Mushroom", 29, 0.0f, BlockMaterial::Plants, StepSound::Grass);
      b.type = 0; b.lightValue = 1; }
    { auto& b = Props(r.Add<MushroomBlock>(BLOCK_RED_MUSHROOM), "Mushroom", 28, 0.0f, BlockMaterial::Plants, StepSound::Grass); b.type = 1; }
    Props(r.Add<CropsBlock>(BLOCK_WHEAT), "Crops", 88, 0.0f, BlockMaterial::Plants, StepSound::Grass);
    Props(r.Add<FarmlandBlock>(BLOCK_FARMLAND), "Farmland", 87, 0.6f, BlockMaterial::Ground, StepSound::Gravel);
    Props(r.Add<CactusBlock>(BLOCK_CACTUS), "Cactus", 70, 0.4f, BlockMaterial::Cactus, StepSound::Cloth);
    Props(r.Add<ReedsBlock>(BLOCK_REEDS), "Sugar cane", 73, 0.0f, BlockMaterial::Plants, StepSound::Grass);
    { auto& b = Props(r.Add<StemBlock>(BLOCK_PUMPKIN_STEM), "Pumpkin Stem", 111, 0.0f, BlockMaterial::Plants, StepSound::Wood);
      b.fruit = BLOCK_PUMPKIN; b.seeds = ITEM_PUMPKIN_SEEDS; }
    { auto& b = Props(r.Add<StemBlock>(BLOCK_MELON_STEM), "Melon Stem", 111, 0.0f, BlockMaterial::Plants, StepSound::Wood);
      b.fruit = BLOCK_MELON; b.seeds = ITEM_MELON_SEEDS; }
    Props(r.Add<VineBlock>(BLOCK_VINE), "Vines", 143, 0.2f, BlockMaterial::Vine, StepSound::Grass);
    Props(r.Add<SnowLayerBlock>(BLOCK_SNOW_LAYER), "Snow", 66, 0.1f, BlockMaterial::Snow, StepSound::Cloth);
}
