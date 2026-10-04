#include "BlocksCommon.h"

#include <cmath>

namespace {

// Pistons keep changing blocks around themselves while moving; this stops them
// from reacting to their own changes (BlockPistonBase.ignoreUpdates).
bool g_ignorePistonUpdates = false;

bool IsExtended(int meta) { return (meta & 8) != 0; }
int Orientation(int meta) { return meta & 7; }

class PistonBaseBlock final : public Block {
public:
    bool sticky = false;

    PistonBaseBlock() {
        shape = Shape::PistonBase;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    int Texture(int side, int meta) const override {
        int facing = Orientation(meta);
        if (facing > 5) return texture;
        if (side == facing) return IsExtended(meta) ? 110 : texture;
        return side == OppositeSide(facing) ? 109 : 108;
    }
    int ItemIconTexture(int) const override { return texture; }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int meta = world.GetMeta(x, y, z);
        if (!IsExtended(meta)) return BlockBox();
        switch (Orientation(meta)) {
            case 0: return MakeBox(0, 0.25f, 0, 1, 1, 1);
            case 1: return MakeBox(0, 0, 0, 1, 0.75f, 1);
            case 2: return MakeBox(0, 0, 0.25f, 1, 1, 1);
            case 3: return MakeBox(0, 0, 0, 1, 1, 0.75f);
            case 4: return MakeBox(0.25f, 0, 0, 1, 1, 1);
            case 5: return MakeBox(0, 0, 0, 0.75f, 1, 1);
        }
        return BlockBox();
    }
    int PistonMobility(int meta) const override { return IsExtended(meta) ? 2 : 0; }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        world.SetMeta(x, y, z, PlayerFacingSide(player, x, y, z));
        if (!g_ignorePistonUpdates) UpdateState(world, x, y, z);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!g_ignorePistonUpdates) UpdateState(world, x, y, z);
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr && !g_ignorePistonUpdates) UpdateState(world, x, y, z);
    }

    // Beta pistons also count power one block above them ("quasi-connectivity").
    static bool Powered(const World& world, int x, int y, int z, int facing) {
        if (facing != 0 && world.IsBlockIndirectlyProvidingPowerTo(x, y - 1, z, 0)) return true;
        if (facing != 1 && world.IsBlockIndirectlyProvidingPowerTo(x, y + 1, z, 1)) return true;
        if (facing != 2 && world.IsBlockIndirectlyProvidingPowerTo(x, y, z - 1, 2)) return true;
        if (facing != 3 && world.IsBlockIndirectlyProvidingPowerTo(x, y, z + 1, 3)) return true;
        if (facing != 5 && world.IsBlockIndirectlyProvidingPowerTo(x + 1, y, z, 5)) return true;
        if (facing != 4 && world.IsBlockIndirectlyProvidingPowerTo(x - 1, y, z, 4)) return true;
        if (world.IsBlockIndirectlyProvidingPowerTo(x, y, z, 0)) return true;
        if (world.IsBlockIndirectlyProvidingPowerTo(x, y + 2, z, 1)) return true;
        if (world.IsBlockIndirectlyProvidingPowerTo(x, y + 1, z - 1, 2)) return true;
        if (world.IsBlockIndirectlyProvidingPowerTo(x, y + 1, z + 1, 3)) return true;
        if (world.IsBlockIndirectlyProvidingPowerTo(x - 1, y + 1, z, 4)) return true;
        return world.IsBlockIndirectlyProvidingPowerTo(x + 1, y + 1, z, 5);
    }

    void UpdateState(World& world, int x, int y, int z) const {
        int meta = world.GetMeta(x, y, z);
        int facing = Orientation(meta);
        bool powered = Powered(world, x, y, z, facing);
        if (meta == 7) return;
        if (powered && !IsExtended(meta)) {
            if (CanExtend(world, x, y, z, facing)) {
                world.SetMeta(x, y, z, facing | 8, false);
                world.BlockEvent(x, y, z, 0, facing);
            }
        } else if (!powered && IsExtended(meta)) {
            world.SetMeta(x, y, z, facing, false);
            world.BlockEvent(x, y, z, 1, facing);
        }
    }

    static bool CanPush(int id, const World& world, int x, int y, int z, bool allowBreaking) {
        if (id == BLOCK_OBSIDIAN) return false;
        const Block& block = GetBlockDef((BlockId)id);
        if (id == BLOCK_PISTON || id == BLOCK_STICKY_PISTON) {
            if (IsExtended(world.GetMeta(x, y, z))) return false;
        } else {
            if (block.hardness == -1.0f) return false;
            int mobility = block.PistonMobility(world.GetMeta(x, y, z));
            if (mobility == 2) return false;
            if (!allowBreaking && mobility == 1) return false;
        }
        return world.GetTileEntity(x, y, z) == nullptr;
    }

    static bool CanExtend(const World& world, int x, int y, int z, int facing) {
        int nx = x + SIDE_OFFSET[facing][0], ny = y + SIDE_OFFSET[facing][1], nz = z + SIDE_OFFSET[facing][2];
        for (int count = 0; count < 13; count++) {
            if (ny <= 0 || ny >= Chunk::HEIGHT - 1) return false;
            int id = world.GetBlock(nx, ny, nz);
            if (id == BLOCK_AIR) break;
            if (!CanPush(id, world, nx, ny, nz, true)) return false;
            if (GetBlockDef((BlockId)id).PistonMobility(world.GetMeta(nx, ny, nz)) == 1) break;
            if (count == 12) return false;
            nx += SIDE_OFFSET[facing][0];
            ny += SIDE_OFFSET[facing][1];
            nz += SIDE_OFFSET[facing][2];
        }
        return true;
    }

    static void StartMoving(World& world, int x, int y, int z, int id, int meta, int facing, bool extending, bool isSource) {
        world.SetBlock(x, y, z, BLOCK_PISTON_MOVING, meta, false);
        auto tile = std::make_unique<PistonTile>();
        tile->storedId = id;
        tile->storedMeta = meta;
        tile->facing = facing;
        tile->extending = extending;
        tile->isSource = isSource;
        world.SetTileEntity(x, y, z, std::move(tile));
    }

    bool TryExtend(World& world, int x, int y, int z, int facing) const {
        int ox = SIDE_OFFSET[facing][0], oy = SIDE_OFFSET[facing][1], oz = SIDE_OFFSET[facing][2];
        int nx = x + ox, ny = y + oy, nz = z + oz;
        for (int count = 0; count < 13; count++) {
            if (ny <= 0 || ny >= Chunk::HEIGHT - 1) return false;
            int id = world.GetBlock(nx, ny, nz);
            if (id == BLOCK_AIR) break;
            if (!CanPush(id, world, nx, ny, nz, true)) return false;
            if (GetBlockDef((BlockId)id).PistonMobility(world.GetMeta(nx, ny, nz)) == 1) {
                world.DropBlockAsItem(nx, ny, nz, id, world.GetMeta(nx, ny, nz));
                world.SetBlock(nx, ny, nz, BLOCK_AIR);
                break;
            }
            if (count == 12) return false;
            nx += ox;
            ny += oy;
            nz += oz;
        }
        // Move everything one step, from the far end back to the piston.
        while (nx != x || ny != y || nz != z) {
            int bx = nx - ox, by = ny - oy, bz = nz - oz;
            int id = world.GetBlock(bx, by, bz);
            int meta = world.GetMeta(bx, by, bz);
            if (id == this->id && bx == x && by == y && bz == z) {
                StartMoving(world, nx, ny, nz, BLOCK_PISTON_HEAD, facing | (sticky ? 8 : 0), facing, true, false);
            } else {
                StartMoving(world, nx, ny, nz, id, meta, facing, true, false);
            }
            nx = bx;
            ny = by;
            nz = bz;
        }
        return true;
    }

    void OnBlockEvent(World& world, int x, int y, int z, int action, int facing) const override {
        g_ignorePistonUpdates = true;
        int ox = SIDE_OFFSET[facing][0], oy = SIDE_OFFSET[facing][1], oz = SIDE_OFFSET[facing][2];
        if (action == 0) {
            if (TryExtend(world, x, y, z, facing)) {
                world.SetMeta(x, y, z, facing | 8);
                world.PlaySound("tile.piston.out", x + 0.5, y + 0.5, z + 0.5, 0.5f, world.Random().NextFloat() * 0.25f + 0.6f);
            }
        } else if (action == 1) {
            // A head still sliding out finishes first.
            TileEntity* front = world.GetTileEntity(x + ox, y + oy, z + oz);
            if (front != nullptr && front->kind == TileKind::Piston) static_cast<PistonTile*>(front)->Finish(world, x + ox, y + oy, z + oz);
            StartMoving(world, x, y, z, id, facing, facing, false, true);
            if (sticky) {
                int px = x + ox * 2, py = y + oy * 2, pz = z + oz * 2;
                int pulled = world.GetBlock(px, py, pz);
                int pulledMeta = world.GetMeta(px, py, pz);
                bool wasMoving = false;
                if (pulled == BLOCK_PISTON_MOVING) {
                    TileEntity* te = world.GetTileEntity(px, py, pz);
                    if (te != nullptr && te->kind == TileKind::Piston) {
                        PistonTile* moving = static_cast<PistonTile*>(te);
                        if (moving->facing == facing && moving->extending) {
                            moving->Finish(world, px, py, pz);
                            pulled = moving->storedId;
                            pulledMeta = moving->storedMeta;
                            wasMoving = true;
                        }
                    }
                }
                const Block& pulledBlock = GetBlockDef((BlockId)pulled);
                if (!wasMoving && pulled > 0 && CanPush(pulled, world, px, py, pz, false) &&
                    (pulledBlock.PistonMobility(pulledMeta) == 0 || pulled == BLOCK_PISTON || pulled == BLOCK_STICKY_PISTON)) {
                    g_ignorePistonUpdates = false;
                    world.SetBlock(px, py, pz, BLOCK_AIR);
                    g_ignorePistonUpdates = true;
                    StartMoving(world, x + ox, y + oy, z + oz, pulled, pulledMeta, facing, false, false);
                } else if (!wasMoving) {
                    g_ignorePistonUpdates = false;
                    world.SetBlock(x + ox, y + oy, z + oz, BLOCK_AIR);
                    g_ignorePistonUpdates = true;
                }
            } else {
                g_ignorePistonUpdates = false;
                world.SetBlock(x + ox, y + oy, z + oz, BLOCK_AIR);
                g_ignorePistonUpdates = true;
            }
            world.PlaySound("tile.piston.in", x + 0.5, y + 0.5, z + 0.5, 0.5f, world.Random().NextFloat() * 0.15f + 0.6f);
        }
        g_ignorePistonUpdates = false;
    }
    int DamageDropped(int) const override { return 0; }
};

// ---- BlockPistonExtension: the head and arm of an extended piston.

class PistonHeadBlock final : public Block {
public:
    PistonHeadBlock() {
        shape = Shape::PistonHead;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    int Texture(int side, int meta) const override {
        int facing = Orientation(meta);
        if (side == facing) return (meta & 8) ? 106 : 107;
        return side < 6 && side == OppositeSide(facing) ? 107 : 108;
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        switch (Orientation(world.GetMeta(x, y, z))) {
            case 0: return MakeBox(0, 0, 0, 1, 0.25f, 1);
            case 1: return MakeBox(0, 0.75f, 0, 1, 1, 1);
            case 2: return MakeBox(0, 0, 0, 1, 1, 0.25f);
            case 3: return MakeBox(0, 0, 0.75f, 1, 1, 1);
            case 4: return MakeBox(0, 0, 0, 0.25f, 1, 1);
            case 5: return MakeBox(0.75f, 0, 0, 1, 1, 1);
        }
        return BlockBox();
    }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        switch (Orientation(world.GetMeta(x, y, z))) {
            case 0: out.push_back(MakeBox(0, 0, 0, 1, 0.25f, 1)); out.push_back(MakeBox(0.375f, 0.25f, 0.375f, 0.625f, 1, 0.625f)); break;
            case 1: out.push_back(MakeBox(0, 0.75f, 0, 1, 1, 1)); out.push_back(MakeBox(0.375f, 0, 0.375f, 0.625f, 0.75f, 0.625f)); break;
            case 2: out.push_back(MakeBox(0, 0, 0, 1, 1, 0.25f)); out.push_back(MakeBox(0.25f, 0.375f, 0.25f, 0.75f, 0.625f, 1)); break;
            case 3: out.push_back(MakeBox(0, 0, 0.75f, 1, 1, 1)); out.push_back(MakeBox(0.25f, 0.375f, 0, 0.75f, 0.625f, 0.75f)); break;
            case 4: out.push_back(MakeBox(0, 0, 0, 0.25f, 1, 1)); out.push_back(MakeBox(0.375f, 0.25f, 0.25f, 0.625f, 0.75f, 1)); break;
            case 5: out.push_back(MakeBox(0.75f, 0, 0, 1, 1, 1)); out.push_back(MakeBox(0, 0.375f, 0.25f, 0.75f, 0.625f, 0.75f)); break;
        }
    }
    bool CanPlaceAt(const World&, int, int, int) const override { return false; }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}
    int PistonMobility(int) const override { return 2; }
    ItemStack PickItem(const World& world, int x, int y, int z) const override {
        return ItemStack((world.GetMeta(x, y, z) & 8) ? BLOCK_STICKY_PISTON : BLOCK_PISTON, 1);
    }
    void OnRemoved(World& world, int x, int y, int z, int oldMeta) const override {
        int back = OppositeSide(Orientation(oldMeta));
        int bx = x + SIDE_OFFSET[back][0], by = y + SIDE_OFFSET[back][1], bz = z + SIDE_OFFSET[back][2];
        int base = world.GetBlock(bx, by, bz);
        if ((base == BLOCK_PISTON || base == BLOCK_STICKY_PISTON) && IsExtended(world.GetMeta(bx, by, bz))) {
            world.DropBlockAsItem(bx, by, bz, base, world.GetMeta(bx, by, bz));
            world.SetBlock(bx, by, bz, BLOCK_AIR);
        }
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        int back = OppositeSide(Orientation(world.GetMeta(x, y, z)));
        int bx = x + SIDE_OFFSET[back][0], by = y + SIDE_OFFSET[back][1], bz = z + SIDE_OFFSET[back][2];
        int base = world.GetBlock(bx, by, bz);
        if (base != BLOCK_PISTON && base != BLOCK_STICKY_PISTON) world.SetBlock(x, y, z, BLOCK_AIR);
        else GetBlockDef((BlockId)base).OnNeighborChanged(world, bx, by, bz, neighbor);
    }
};

// ---- BlockPistonMoving: a block partway through being pushed or pulled.

class PistonMovingBlock final : public Block {
public:
    PistonMovingBlock() {
        shape = Shape::MovingPiston;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    bool HasTileEntity() const override { return true; }
    bool CanPlaceAt(const World&, int, int, int) const override { return false; }
    int PistonMobility(int) const override { return 2; }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        TileEntity* te = world.GetTileEntity(x, y, z);
        if (te != nullptr && te->kind == TileKind::Piston) world.RemoveTileEntity(x, y, z);
    }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            return true;
        }
        return false;
    }
    void Drops(World& world, int x, int y, int z, int, JavaRandom& random, std::vector<ItemStack>& out) const override {
        TileEntity* te = world.GetTileEntity(x, y, z);
        if (te == nullptr || te->kind != TileKind::Piston) return;
        const PistonTile* moving = static_cast<const PistonTile*>(te);
        GetBlockDef((BlockId)moving->storedId).Drops(world, x, y, z, moving->storedMeta, random, out);
    }
    // Collision: the stored block, shifted to where it is now.
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        TileEntity* te = world.GetTileEntity(x, y, z);
        if (te == nullptr || te->kind != TileKind::Piston) return;
        const PistonTile* moving = static_cast<const PistonTile*>(te);
        if (moving->storedId == 0 || moving->storedId == BLOCK_PISTON_MOVING) return;
        float offset = moving->Offset(1.0f);
        float dx = offset * SIDE_OFFSET[moving->facing][0], dy = offset * SIDE_OFFSET[moving->facing][1], dz = offset * SIDE_OFFSET[moving->facing][2];
        BlockBox box;
        box.minX += dx; box.maxX += dx; box.minY += dy; box.maxY += dy; box.minZ += dz; box.maxZ += dz;
        out.push_back(box);
    }
    BlockBox Bounds(const World&, int, int, int) const override { return BlockBox(); }
    bool IsTargetable(int) const override { return false; }
};

} // namespace

void RegisterPistonBlocks(BlockRegistry& r) {
    { auto& b = Props(r.Add<PistonBaseBlock>(BLOCK_PISTON), "Piston", 107, 0.5f, BlockMaterial::Piston, StepSound::Stone); b.sticky = false; }
    { auto& b = Props(r.Add<PistonBaseBlock>(BLOCK_STICKY_PISTON), "Sticky Piston", 106, 0.5f, BlockMaterial::Piston, StepSound::Stone); b.sticky = true; }
    Props(r.Add<PistonHeadBlock>(BLOCK_PISTON_HEAD), "Piston", 107, 0.5f, BlockMaterial::Piston, StepSound::Stone);
    Props(r.Add<PistonMovingBlock>(BLOCK_PISTON_MOVING), "Piston", 107, -1.0f, BlockMaterial::Piston, StepSound::Stone);
}
