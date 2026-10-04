#include "BlocksCommon.h"

#include <cmath>

namespace {

// ---- Slabs (BlockStep): six kinds; two stacked of the same kind merge into a
// double slab.

int SlabTexture(int side, int meta) {
    switch (meta) {
        case 0: return side > 1 ? 5 : 6;
        case 1: return side == SIDE_DOWN ? 208 : (side == SIDE_UP ? 176 : 192);
        case 2: return 4;
        case 3: return 16;
        case 4: return 7;
        case 5: return 54;
        default: return 6;
    }
}

const char* SLAB_NAMES[6] = { "Stone Slab", "Sandstone Slab", "Wooden Slab", "Cobblestone Slab", "Bricks Slab", "Stone Bricks Slab" };

class SlabBlock final : public Block {
public:
    bool doubled = false;
    std::string DisplayName(int meta) const override { return SLAB_NAMES[std::min(meta, 5)]; }
    int Texture(int side, int meta) const override { return SlabTexture(side, meta); }
    int DamageDropped(int meta) const override { return meta; }
    BlockBox Bounds(const World&, int, int, int) const override { return doubled ? BlockBox() : MakeBox(0, 0, 0, 1, 0.5f, 1); }
    BlockBox ItemBounds(int) const override { return doubled ? BlockBox() : MakeBox(0, 0, 0, 1, 0.5f, 1); }
    bool ShouldSideBeRendered(const World& world, int nx, int ny, int nz, int side) const override {
        if (doubled) return Block::ShouldSideBeRendered(world, nx, ny, nz, side);
        if (side == SIDE_UP) return true;
        if (!Block::ShouldSideBeRendered(world, nx, ny, nz, side)) return false;
        if (side == SIDE_DOWN) return true;
        return world.GetBlock(nx, ny, nz) != id;
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (doubled) return;
        int meta = world.GetMeta(x, y, z);
        if (world.GetBlock(x, y - 1, z) == BLOCK_SLAB && world.GetMeta(x, y - 1, z) == meta) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            world.SetBlock(x, y - 1, z, BLOCK_DOUBLE_SLAB, meta);
        }
    }
    void Drops(World&, int, int, int, int meta, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(BLOCK_SLAB, doubled ? 2 : 1, meta));
    }
    ItemStack PickItem(const World& world, int x, int y, int z) const override { return ItemStack(BLOCK_SLAB, 1, world.GetMeta(x, y, z)); }
};

// ---- Stairs (BlockStairs): take their looks from a base block, and (in Beta)
// drop that block when broken. Metadata: bits 0-1 facing; bit 2 upside down
// (from 1.3: placed against the top half of a block, or its underside).

class StairsBlock final : public Block {
public:
    BlockId model = BLOCK_PLANKS;
    int modelMeta = 0;
    bool dropsModel = true; // Beta's stairs drop their base block; later ones drop themselves
    StairsBlock() {
        shape = Shape::Stairs;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 255;
    }
    int Texture(int side, int meta) const override { return GetBlockDef(model).Texture(side, modelMeta); }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        size_t first = out.size();
        switch (world.GetMeta(x, y, z) & 3) {
            case 0: out.push_back(MakeBox(0, 0, 0, 0.5f, 0.5f, 1)); out.push_back(MakeBox(0.5f, 0, 0, 1, 1, 1)); break;
            case 1: out.push_back(MakeBox(0, 0, 0, 0.5f, 1, 1)); out.push_back(MakeBox(0.5f, 0, 0, 1, 0.5f, 1)); break;
            case 2: out.push_back(MakeBox(0, 0, 0, 1, 0.5f, 0.5f)); out.push_back(MakeBox(0, 0, 0.5f, 1, 1, 1)); break;
            case 3: out.push_back(MakeBox(0, 0, 0, 1, 1, 0.5f)); out.push_back(MakeBox(0, 0, 0.5f, 1, 0.5f, 1)); break;
        }
        if (world.GetMeta(x, y, z) & 4) {
            for (size_t i = first; i < out.size(); i++) {
                BlockBox& b = out[i];
                float lo = 1.0f - b.maxY, hi = 1.0f - b.minY;
                b.minY = lo;
                b.maxY = hi;
            }
        }
    }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player& player) const override {
        static const int metas[4] = { 2, 1, 3, 0 };
        world.SetMeta(x, y, z, metas[PlayerYawIndex(player)] | (PlacedInUpperHalf(player, side) ? 4 : 0));
    }
    void Drops(World& world, int x, int y, int z, int, JavaRandom& random, std::vector<ItemStack>& out) const override {
        if (dropsModel) GetBlockDef(model).Drops(world, x, y, z, modelMeta, random, out);
        else out.push_back(ItemStack(id, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(id, 1); }
};

// ---- BlockFence: posts that join up with neighboring fences.

class FenceBlock final : public Block {
public:
    FenceBlock() {
        shape = Shape::Fence;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    static bool Connects(const World& world, int x, int y, int z) { return FenceJoins(world.GetBlock(x, y, z)); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>& out) const override {
        out.push_back(MakeBox(0, 0, 0, 1, 1.5f, 1));
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (IsTallPost(world.GetBlock(x, y - 1, z))) return Block::CanPlaceAt(world, x, y, z);
        if (!GetMaterialInfo(world.GetMaterial(x, y - 1, z)).solid) return false;
        return Block::CanPlaceAt(world, x, y, z);
    }
};

// ---- BlockFenceGate: swings open toward where you stand.

class FenceGateBlock final : public Block {
public:
    FenceGateBlock() {
        shape = Shape::FenceGate;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    static bool Open(int meta) { return (meta & 4) != 0; }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int dir = world.GetMeta(x, y, z) & 3;
        if (dir == 2 || dir == 0) return MakeBox(0, 0, 0.375f, 1, 1, 0.625f);
        return MakeBox(0.375f, 0, 0, 0.625f, 1, 1);
    }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        int meta = world.GetMeta(x, y, z);
        if (Open(meta)) return;
        int dir = meta & 3;
        if (dir == 2 || dir == 0) out.push_back(MakeBox(0, 0, 0.375f, 1, 1.5f, 0.625f));
        else out.push_back(MakeBox(0.375f, 0, 0, 0.625f, 1.5f, 1));
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (!GetMaterialInfo(world.GetMaterial(x, y - 1, z)).solid) return false;
        return Block::CanPlaceAt(world, x, y, z);
    }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        world.SetMeta(x, y, z, PlayerYawIndex(player) % 4);
    }
    bool OnActivated(World& world, int x, int y, int z, Player& player) const override {
        int meta = world.GetMeta(x, y, z);
        if (Open(meta)) {
            world.SetMeta(x, y, z, meta & ~4);
        } else {
            int facing = PlayerYawIndex(player) % 4;
            int dir = meta & 3;
            if (dir == (facing + 2) % 4) meta = facing;
            world.SetMeta(x, y, z, meta | 4);
        }
        world.PlaySound(world.Random().NextFloat() < 0.5f ? "random.door_open" : "random.door_close", x + 0.5, y + 0.5, z + 0.5, 1.0f,
                        world.Random().NextFloat() * 0.1f + 0.9f);
        return true;
    }
};

// ---- BlockPane: glass panes and iron bars, joining neighbors into walls.

class PaneBlock final : public Block {
public:
    int edgeTexture = 148;
    bool dropsSelf = true;
    PaneBlock() { SetNotCube(*this, Shape::Pane, Layer::Cutout, 0); }
    // The top and bottom are the thin edge strip (the renderer reads it from here).
    int Texture(int side, int meta) const override { return side <= SIDE_UP ? edgeTexture : texture; }
    static bool Connects(const World& world, int x, int y, int z, int self) { return PaneJoins(world.GetBlock(x, y, z)); }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        bool n = Connects(world, x, y, z - 1, id), s = Connects(world, x, y, z + 1, id);
        bool w = Connects(world, x - 1, y, z, id), e = Connects(world, x + 1, y, z, id);
        bool none = !n && !s && !w && !e;
        if ((w && e) || none) out.push_back(MakeBox(0, 0, 0.4375f, 1, 1, 0.5625f));
        else if (w && !e) out.push_back(MakeBox(0, 0, 0.4375f, 0.5f, 1, 0.5625f));
        else if (!w && e) out.push_back(MakeBox(0.5f, 0, 0.4375f, 1, 1, 0.5625f));
        if ((n && s) || none) out.push_back(MakeBox(0.4375f, 0, 0, 0.5625f, 1, 1));
        else if (n && !s) out.push_back(MakeBox(0.4375f, 0, 0, 0.5625f, 1, 0.5f));
        else if (!n && s) out.push_back(MakeBox(0.4375f, 0, 0.5f, 0.5625f, 1, 1));
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        if (dropsSelf) out.push_back(ItemStack(id, 1));
    }
};

// ---- Doors (BlockDoor): two blocks tall. Metadata: bits 0-1 facing, bit 2
// open, bit 3 top half. Wooden doors open by hand, iron ones only with redstone.

class DoorBlock final : public Block {
public:
    int item = ITEM_WOODEN_DOOR;
    int topTexture = -1; // -1: the tile above `texture`, as in Beta's terrain.png
    int itemIcon = -1;   // the item's terrain.png tile, for doors that are their own item
    bool byHand = false; // opens by hand even though it's metal (copper doors)
    DoorBlock() {
        shape = Shape::Door;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
        layer = Layer::Cutout;
    }
    static int State(int meta) { return (meta & 4) == 0 ? ((meta - 1) & 3) : (meta & 3); }
    int Texture(int side, int meta) const override {
        if ((meta & 8) == 0) return texture;
        return topTexture >= 0 ? topTexture : texture - 16;
    }
    int ItemIconTexture(int) const override { return itemIcon >= 0 ? itemIcon : texture; }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        const float t = 0.1875f;
        switch (State(world.GetMeta(x, y, z))) {
            case 0: return MakeBox(0, 0, 0, 1, 1, t);
            case 1: return MakeBox(1 - t, 0, 0, 1, 1, 1);
            case 2: return MakeBox(0, 0, 1 - t, 1, 1, 1);
            default: return MakeBox(0, 0, 0, t, 1, 1);
        }
    }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        out.push_back(Bounds(world, x, y, z));
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (y >= Chunk::HEIGHT - 1) return false;
        return world.IsNormalCube(x, y - 1, z) && Block::CanPlaceAt(world, x, y, z) && Block::CanPlaceAt(world, x, y + 1, z);
    }
    static void Toggle(World& world, int x, int y, int z, int id, int meta) {
        if (world.GetBlock(x, y + 1, z) == id) world.SetMeta(x, y + 1, z, (meta ^ 4) + 8);
        world.SetMeta(x, y, z, meta ^ 4);
        world.MarkForRender(x, y - 1, z);
        world.MarkForRender(x, y, z);
        world.PlaySound(world.Random().NextDouble() < 0.5 ? "random.door_open" : "random.door_close", x + 0.5, y + 0.5, z + 0.5, 1.0f,
                        world.Random().NextFloat() * 0.1f + 0.9f);
    }
    bool OnActivated(World& world, int x, int y, int z, Player& player) const override {
        if (material == BlockMaterial::Iron && !byHand) return true;
        int meta = world.GetMeta(x, y, z);
        if (meta & 8) {
            if (world.GetBlock(x, y - 1, z) == id) OnActivated(world, x, y - 1, z, player);
            return true;
        }
        Toggle(world, x, y, z, id, meta);
        return true;
    }
    void OnClicked(World& world, int x, int y, int z, Player& player) const override {
        if (material != BlockMaterial::Iron || byHand) OnActivated(world, x, y, z, player);
    }
    void SetPowered(World& world, int x, int y, int z, bool powered) const {
        int meta = world.GetMeta(x, y, z);
        bool open = (meta & 4) != 0;
        if (open == powered) return;
        if (meta & 8) {
            if (world.GetBlock(x, y - 1, z) == id) SetPowered(world, x, y - 1, z, powered);
            return;
        }
        Toggle(world, x, y, z, id, meta);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        int meta = world.GetMeta(x, y, z);
        if (meta & 8) {
            if (world.GetBlock(x, y - 1, z) != id) world.SetBlock(x, y, z, BLOCK_AIR);
            else if (neighbor > 0 && GetBlockDef((BlockId)neighbor).CanProvidePower()) OnNeighborChanged(world, x, y - 1, z, neighbor);
            return;
        }
        bool broken = false;
        if (world.GetBlock(x, y + 1, z) != id) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            broken = true;
        }
        if (!world.IsNormalCube(x, y - 1, z)) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            broken = true;
            if (world.GetBlock(x, y + 1, z) == id) world.SetBlock(x, y + 1, z, BLOCK_AIR);
        }
        if (broken) {
            world.SpawnItem(x, y, z, ItemStack(item, 1));
        } else if (neighbor > 0 && GetBlockDef((BlockId)neighbor).CanProvidePower()) {
            bool powered = world.IsBlockIndirectlyGettingPowered(x, y, z) || world.IsBlockIndirectlyGettingPowered(x, y + 1, z);
            SetPowered(world, x, y, z, powered);
        }
    }
    void Drops(World&, int, int, int, int meta, JavaRandom&, std::vector<ItemStack>& out) const override {
        if ((meta & 8) == 0) out.push_back(ItemStack(item, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(item, 1); }
    int PistonMobility(int) const override { return 1; }
};

// ---- BlockTrapDoor: a hatch on the side of a block that swings up.

class TrapdoorBlock final : public Block {
public:
    bool iron = false; // opened only by redstone
    TrapdoorBlock() {
        SetNotCube(*this, Shape::Cube, Layer::Cutout, 0);
    }
    BlockBox Box(int meta) const {
        const float t = 0.1875f;
        if ((meta & 4) == 0) return MakeBox(0, 0, 0, 1, t, 1);
        switch (meta & 3) {
            case 0: return MakeBox(0, 0, 1 - t, 1, 1, 1);
            case 1: return MakeBox(0, 0, 0, 1, 1, t);
            case 2: return MakeBox(1 - t, 0, 0, 1, 1, 1);
            default: return MakeBox(0, 0, 0, t, 1, 1);
        }
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override { return Box(world.GetMeta(x, y, z)); }
    BlockBox ItemBounds(int) const override { return MakeBox(0, 0.40625f, 0, 1, 0.59375f, 1); }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        out.push_back(Bounds(world, x, y, z));
    }
    static void Behind(int meta, int& dx, int& dz) {
        dx = 0;
        dz = 0;
        switch (meta & 3) {
            case 0: dz = 1; break;
            case 1: dz = -1; break;
            case 2: dx = 1; break;
            default: dx = -1; break;
        }
    }
    bool CanPlaceOnSide(const World& world, int x, int y, int z, int side) const override {
        if (side == SIDE_DOWN || side == SIDE_UP) return false;
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        if (side == SIDE_NORTH) z++;
        if (side == SIDE_SOUTH) z--;
        if (side == SIDE_WEST) x++;
        if (side == SIDE_EAST) x--;
        return world.IsNormalCube(x, y, z);
    }
    bool CanPlaceAt(const World&, int, int, int) const override { return false; }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player&) const override {
        int meta = 0;
        if (side == SIDE_SOUTH) meta = 1;
        if (side == SIDE_WEST) meta = 2;
        if (side == SIDE_EAST) meta = 3;
        world.SetMeta(x, y, z, meta);
    }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        if (iron) return true;
        world.SetMeta(x, y, z, world.GetMeta(x, y, z) ^ 4);
        world.PlaySound(world.Random().NextDouble() < 0.5 ? "random.door_open" : "random.door_close", x + 0.5, y + 0.5, z + 0.5, 1.0f,
                        world.Random().NextFloat() * 0.1f + 0.9f);
        return true;
    }
    void OnClicked(World& world, int x, int y, int z, Player& player) const override {
        if (!iron) OnActivated(world, x, y, z, player);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        int meta = world.GetMeta(x, y, z);
        int dx, dz;
        Behind(meta, dx, dz);
        if (!world.IsNormalCube(x + dx, y, z + dz)) {
            BreakAndDrop(world, x, y, z);
            return;
        }
        if (neighbor > 0 && GetBlockDef((BlockId)neighbor).CanProvidePower()) {
            bool powered = world.IsBlockIndirectlyGettingPowered(x, y, z);
            bool open = (meta & 4) != 0;
            if (open != powered) {
                world.SetMeta(x, y, z, meta ^ 4);
                world.PlaySound(world.Random().NextDouble() < 0.5 ? "random.door_open" : "random.door_close", x + 0.5, y + 0.5, z + 0.5,
                                1.0f, world.Random().NextFloat() * 0.1f + 0.9f);
            }
        }
    }
};

// ---- BlockLadder: climbable, on the side of a block.

class LadderBlock final : public Block {
public:
    LadderBlock() { SetNotCube(*this, Shape::Ladder, Layer::Cutout, 0); }
    bool IsLadder(int) const override { return true; }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        const float t = 0.125f;
        switch (world.GetMeta(x, y, z)) {
            case 2: return MakeBox(0, 0, 1 - t, 1, 1, 1);
            case 3: return MakeBox(0, 0, 0, 1, 1, t);
            case 4: return MakeBox(1 - t, 0, 0, 1, 1, 1);
            case 5: return MakeBox(0, 0, 0, t, 1, 1);
        }
        return BlockBox();
    }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        out.push_back(Bounds(world, x, y, z));
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        return world.IsNormalCube(x - 1, y, z) || world.IsNormalCube(x + 1, y, z) || world.IsNormalCube(x, y, z - 1) ||
               world.IsNormalCube(x, y, z + 1);
    }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player&) const override {
        int meta = world.GetMeta(x, y, z);
        if ((meta == 0 || side == SIDE_NORTH) && world.IsNormalCube(x, y, z + 1)) meta = 2;
        if ((meta == 0 || side == SIDE_SOUTH) && world.IsNormalCube(x, y, z - 1)) meta = 3;
        if ((meta == 0 || side == SIDE_WEST) && world.IsNormalCube(x + 1, y, z)) meta = 4;
        if ((meta == 0 || side == SIDE_EAST) && world.IsNormalCube(x - 1, y, z)) meta = 5;
        world.SetMeta(x, y, z, meta);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        int meta = world.GetMeta(x, y, z);
        bool supported = (meta == 2 && world.IsNormalCube(x, y, z + 1)) || (meta == 3 && world.IsNormalCube(x, y, z - 1)) ||
                         (meta == 4 && world.IsNormalCube(x + 1, y, z)) || (meta == 5 && world.IsNormalCube(x - 1, y, z));
        if (!supported) BreakAndDrop(world, x, y, z);
    }
    int DamageDropped(int) const override { return 0; }
};

// ---- BlockBed: two blocks, foot and head (bit 3), facing 0-3; sleeping in it
// at night skips to morning and sets your spawn.

constexpr int BED_HEAD_OFFSET[4][2] = { { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 0 } };

class BedBlock final : public Block {
public:
    BedBlock() {
        shape = Shape::Bed;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    int Texture(int side, int meta) const override {
        if (side == SIDE_DOWN) return 4;
        static const int bedDirection[4][6] = { { 1, 0, 3, 2, 5, 4 }, { 1, 0, 5, 4, 2, 3 }, { 1, 0, 2, 3, 4, 5 }, { 1, 0, 4, 5, 3, 2 } };
        int d = bedDirection[meta & 3][side];
        if (meta & 8) {
            if (d == 2) return texture + 2 + 16;
            return (d == 5 || d == 4) ? texture + 1 + 16 : texture + 1;
        }
        if (d == 3) return texture - 1 + 16;
        return (d == 5 || d == 4) ? texture + 16 : texture;
    }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0, 0, 0, 1, 0.5625f, 1); }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        int meta = world.GetMeta(x, y, z);
        int dir = meta & 3;
        if ((meta & 8) == 0) {
            if (world.GetBlock(x + BED_HEAD_OFFSET[dir][0], y, z + BED_HEAD_OFFSET[dir][1]) != id) world.SetBlock(x, y, z, BLOCK_AIR);
        } else if (world.GetBlock(x - BED_HEAD_OFFSET[dir][0], y, z - BED_HEAD_OFFSET[dir][1]) != id) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            world.SpawnItem(x, y, z, ItemStack(ITEM_BED, 1));
        }
    }
    bool OnActivated(World& world, int x, int y, int z, Player& player) const override {
        int meta = world.GetMeta(x, y, z);
        if ((meta & 8) == 0) {
            int dir = meta & 3;
            x += BED_HEAD_OFFSET[dir][0];
            z += BED_HEAD_OFFSET[dir][1];
            if (world.GetBlock(x, y, z) != id) return true;
            meta = world.GetMeta(x, y, z);
        }
        if (world.IsDaytime()) {
            player.ShowMessage("You can only sleep at night");
            return true;
        }
        player.TrySleep(x, y, z, meta & 3);
        return true;
    }
    void Drops(World&, int, int, int, int meta, JavaRandom&, std::vector<ItemStack>& out) const override {
        if (meta & 8) out.push_back(ItemStack(ITEM_BED, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(ITEM_BED, 1); }
    int PistonMobility(int) const override { return 1; }
};

// ---- Signs (BlockSign): on a post (16 rotations) or on a wall; the text lives
// in a tile entity.

class SignBlock final : public Block {
public:
    bool standing = true;
    SignBlock() {
        shape = Shape::Sign;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    bool HasTileEntity() const override { return true; }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        if (standing) return MakeBox(0.25f, 0, 0.25f, 0.75f, 1, 0.75f);
        const float lo = 0.28125f, hi = 0.78125f, t = 0.125f;
        switch (world.GetMeta(x, y, z)) {
            case 2: return MakeBox(0, lo, 1 - t, 1, hi, 1);
            case 3: return MakeBox(0, lo, 0, 1, hi, t);
            case 4: return MakeBox(1 - t, lo, 0, 1, hi, 1);
            default: return MakeBox(0, lo, 0, t, hi, 1);
        }
    }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) world.SetTileEntity(x, y, z, CreateTileEntity(TileKind::Sign));
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override { world.RemoveTileEntity(x, y, z); }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        bool keep;
        if (standing) {
            keep = GetMaterialInfo(world.GetMaterial(x, y - 1, z)).solid;
        } else {
            int meta = world.GetMeta(x, y, z);
            keep = (meta == 2 && GetMaterialInfo(world.GetMaterial(x, y, z + 1)).solid) ||
                   (meta == 3 && GetMaterialInfo(world.GetMaterial(x, y, z - 1)).solid) ||
                   (meta == 4 && GetMaterialInfo(world.GetMaterial(x + 1, y, z)).solid) ||
                   (meta == 5 && GetMaterialInfo(world.GetMaterial(x - 1, y, z)).solid);
        }
        if (!keep) BreakAndDrop(world, x, y, z);
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(ITEM_SIGN, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(ITEM_SIGN, 1); }
    bool IsTargetable(int) const override { return true; }
};

// ---- BlockCake: six slices, each worth a little food.

class CakeBlock final : public Block {
public:
    CakeBlock() {
        ticksRandomly = false;
        SetNotCube(*this, Shape::Cube, Layer::Opaque, 0);
    }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP) return 121;
        if (side == SIDE_DOWN) return 124;
        if (meta > 0 && side == SIDE_WEST) return 123;
        return 122;
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        const float f = 0.0625f;
        float start = (1 + world.GetMeta(x, y, z) * 2) / 16.0f;
        return MakeBox(start, 0, f, 1 - f, 0.5f, 1 - f);
    }
    BlockBox ItemBounds(int) const override { return MakeBox(0.0625f, 0, 0.0625f, 0.9375f, 0.5f, 0.9375f); }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        const float f = 0.0625f;
        float start = (1 + world.GetMeta(x, y, z) * 2) / 16.0f;
        out.push_back(MakeBox(start, 0, f, 1 - f, 0.5f - f, 1 - f));
    }
    void Eat(World& world, int x, int y, int z, Player& player) const {
        if (!player.CanEat(false)) return;
        player.AddFood(2, 0.1f);
        int slices = world.GetMeta(x, y, z) + 1;
        if (slices >= 6) world.SetBlock(x, y, z, BLOCK_AIR);
        else world.SetMeta(x, y, z, slices);
    }
    bool OnActivated(World& world, int x, int y, int z, Player& player) const override {
        Eat(world, x, y, z, player);
        return true;
    }
    void OnClicked(World& world, int x, int y, int z, Player& player) const override { Eat(world, x, y, z, player); }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && GetMaterialInfo(world.GetMaterial(x, y - 1, z)).solid;
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!GetMaterialInfo(world.GetMaterial(x, y - 1, z)).solid) world.SetBlock(x, y, z, BLOCK_AIR);
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(ITEM_CAKE, 1); }
};

} // namespace

void RegisterBuildingBlocks(BlockRegistry& r) {
    { auto& b = Props(r.Add<SlabBlock>(BLOCK_DOUBLE_SLAB), "Stone Slab", 6, 2.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 10); b.doubled = true; }
    { auto& b = Props(r.Add<SlabBlock>(BLOCK_SLAB), "Stone Slab", 6, 2.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 10);
      b.doubled = false; b.opaqueCube = false; b.normalCube = false; b.lightOpacity = 255; }

    { auto& b = Props(r.Add<FenceBlock>(BLOCK_FENCE), "Fence", 4, 2.0f, BlockMaterial::Wood, StepSound::Wood); SetResistance(b, 5); }
    { auto& b = Props(r.Add<FenceGateBlock>(BLOCK_FENCE_GATE), "Fence Gate", 4, 2.0f, BlockMaterial::Wood, StepSound::Wood); SetResistance(b, 5); }
    { auto& b = Props(r.Add<PaneBlock>(BLOCK_GLASS_PANE), "Glass Pane", 49, 0.3f, BlockMaterial::Glass, StepSound::Glass); b.edgeTexture = 148; b.dropsSelf = false; }
    { auto& b = Props(r.Add<PaneBlock>(BLOCK_IRON_BARS), "Iron Bars", 85, 5.0f, BlockMaterial::Iron, StepSound::Metal); SetResistance(b, 10); b.edgeTexture = 85; b.dropsSelf = true; }
    { auto& b = Props(r.Add<DoorBlock>(BLOCK_WOODEN_DOOR), "Wooden Door", 97, 3.0f, BlockMaterial::Wood, StepSound::Wood); b.item = ITEM_WOODEN_DOOR; }
    { auto& b = Props(r.Add<DoorBlock>(BLOCK_IRON_DOOR), "Iron Door", 98, 5.0f, BlockMaterial::Iron, StepSound::Metal); b.item = ITEM_IRON_DOOR; }
    Props(r.Add<TrapdoorBlock>(BLOCK_TRAPDOOR), "Trapdoor", 84, 3.0f, BlockMaterial::Wood, StepSound::Wood);
    Props(r.Add<LadderBlock>(BLOCK_LADDER), "Ladder", 83, 0.4f, BlockMaterial::Circuits, StepSound::Wood);
    Props(r.Add<BedBlock>(BLOCK_BED), "Bed", 134, 0.2f, BlockMaterial::Cloth, StepSound::Wood);
    { auto& b = Props(r.Add<SignBlock>(BLOCK_SIGN_POST), "Sign", 4, 1.0f, BlockMaterial::Wood, StepSound::Wood); b.standing = true; }
    { auto& b = Props(r.Add<SignBlock>(BLOCK_WALL_SIGN), "Sign", 4, 1.0f, BlockMaterial::Wood, StepSound::Wood); b.standing = false; }
    Props(r.Add<CakeBlock>(BLOCK_CAKE), "Cake", 121, 0.5f, BlockMaterial::Cake, StepSound::Cloth);
}

// Stairs copy their base block's properties, so they're added once everything
// else exists (called last by the registry).
void RegisterStairs(BlockRegistry& r) {
    auto stairs = [&](int id, const char* name, BlockId model) {
        const Block& m = *r.blocks[model];
        auto& b = Props(r.Add<StairsBlock>(id), name, m.texture, m.hardness, m.material, m.sound);
        b.resistance = m.resistance;
        b.model = model;
    };
    stairs(BLOCK_OAK_STAIRS, "Wooden Stairs", BLOCK_PLANKS);
    stairs(BLOCK_COBBLESTONE_STAIRS, "Stone Stairs", BLOCK_COBBLESTONE);
    stairs(BLOCK_BRICK_STAIRS, "Brick Stairs", BLOCK_BRICKS);
    stairs(BLOCK_STONE_BRICK_STAIRS, "Stone Brick Stairs", BLOCK_STONE_BRICKS);
}

// ---- Beta's classes for the blocks after Beta (RegisterModernBlocks sets their
// names, hardness and sounds).

Block& AddStairs(BlockRegistry& r, int id, int model, int modelMeta) {
    auto& b = r.Add<StairsBlock>(id);
    b.model = (BlockId)model;
    b.modelMeta = modelMeta;
    b.dropsModel = false;
    return b;
}

Block& AddFence(BlockRegistry& r, int id, int texture) {
    auto& b = r.Add<FenceBlock>(id);
    b.texture = texture;
    return b;
}

Block& AddFenceGate(BlockRegistry& r, int id, int texture) {
    auto& b = r.Add<FenceGateBlock>(id);
    b.texture = texture;
    return b;
}

Block& AddPane(BlockRegistry& r, int id, int texture, int edgeTexture) {
    auto& b = r.Add<PaneBlock>(id);
    b.texture = texture;
    b.edgeTexture = edgeTexture;
    return b;
}

// A door that is its own item (doors after Beta), drawn from two textures.
Block& AddDoor(BlockRegistry& r, int id, int bottomTexture, int topTexture, int itemIcon, bool iron) {
    auto& b = r.Add<DoorBlock>(id);
    b.texture = bottomTexture;
    b.topTexture = topTexture;
    b.itemIcon = itemIcon;
    b.item = id;
    b.byHand = !iron;
    return b;
}

Block& AddTrapdoor(BlockRegistry& r, int id, int texture, bool iron) {
    auto& b = r.Add<TrapdoorBlock>(id);
    b.texture = texture;
    b.iron = iron;
    return b;
}
