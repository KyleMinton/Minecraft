#include "BlocksCommon.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <set>
#include <tuple>

namespace {

// ---- BlockTorch: on a wall or on the ground; pops off without support.

class TorchBlock : public Block {
public:
    TorchBlock() { SetNotCube(*this, Shape::Torch, Layer::Cutout, 0); }

    static bool CanHoldTorch(const World& world, int x, int y, int z) {
        return world.IsNormalCube(x, y, z) || world.GetBlock(x, y, z) == BLOCK_FENCE;
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int meta = world.GetMeta(x, y, z) & 7;
        const float f = 0.15f;
        if (meta == 1) return MakeBox(0, 0.2f, 0.5f - f, f * 2, 0.8f, 0.5f + f);
        if (meta == 2) return MakeBox(1 - f * 2, 0.2f, 0.5f - f, 1, 0.8f, 0.5f + f);
        if (meta == 3) return MakeBox(0.5f - f, 0.2f, 0, 0.5f + f, 0.8f, f * 2);
        if (meta == 4) return MakeBox(0.5f - f, 0.2f, 1 - f * 2, 0.5f + f, 0.8f, 1);
        return MakeBox(0.4f, 0, 0.4f, 0.6f, 0.6f, 0.6f);
    }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        return world.IsNormalCube(x - 1, y, z) || world.IsNormalCube(x + 1, y, z) || world.IsNormalCube(x, y, z - 1) ||
               world.IsNormalCube(x, y, z + 1) || CanHoldTorch(world, x, y - 1, z);
    }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player&) const override {
        int meta = world.GetMeta(x, y, z);
        if (side == SIDE_UP && CanHoldTorch(world, x, y - 1, z)) meta = 5;
        if (side == SIDE_NORTH && world.IsNormalCube(x, y, z + 1)) meta = 4;
        if (side == SIDE_SOUTH && world.IsNormalCube(x, y, z - 1)) meta = 3;
        if (side == SIDE_WEST && world.IsNormalCube(x + 1, y, z)) meta = 2;
        if (side == SIDE_EAST && world.IsNormalCube(x - 1, y, z)) meta = 1;
        world.SetMeta(x, y, z, meta);
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetMeta(x, y, z) != 0) return;
        if (world.IsNormalCube(x - 1, y, z)) world.SetMeta(x, y, z, 1);
        else if (world.IsNormalCube(x + 1, y, z)) world.SetMeta(x, y, z, 2);
        else if (world.IsNormalCube(x, y, z - 1)) world.SetMeta(x, y, z, 3);
        else if (world.IsNormalCube(x, y, z + 1)) world.SetMeta(x, y, z, 4);
        else if (CanHoldTorch(world, x, y - 1, z)) world.SetMeta(x, y, z, 5);
        DropIfUnsupported(world, x, y, z);
    }
    bool DropIfUnsupported(World& world, int x, int y, int z) const {
        if (CanPlaceAtIgnoringSelf(world, x, y, z)) return true;
        BreakAndDrop(world, x, y, z);
        return false;
    }
    bool CanPlaceAtIgnoringSelf(const World& world, int x, int y, int z) const {
        return world.IsNormalCube(x - 1, y, z) || world.IsNormalCube(x + 1, y, z) || world.IsNormalCube(x, y, z - 1) ||
               world.IsNormalCube(x, y, z + 1) || CanHoldTorch(world, x, y - 1, z);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!DropIfUnsupported(world, x, y, z)) return;
        int meta = world.GetMeta(x, y, z);
        bool unsupported = (meta == 1 && !world.IsNormalCube(x - 1, y, z)) || (meta == 2 && !world.IsNormalCube(x + 1, y, z)) ||
                           (meta == 3 && !world.IsNormalCube(x, y, z - 1)) || (meta == 4 && !world.IsNormalCube(x, y, z + 1)) ||
                           (meta == 5 && !CanHoldTorch(world, x, y - 1, z));
        if (unsupported) BreakAndDrop(world, x, y, z);
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (world.GetMeta(x, y, z) == 0) OnAdded(world, x, y, z);
    }
    // Where the flame is (BlockTorch.randomDisplayTick).
    static Vector3 TipPosition(int x, int y, int z, int meta) {
        double cx = x + 0.5, cy = y + 0.7, cz = z + 0.5;
        const double up = 0.22, out = 0.27;
        if (meta == 1) return Vector3{ (float)(cx - out), (float)(cy + up), (float)cz };
        if (meta == 2) return Vector3{ (float)(cx + out), (float)(cy + up), (float)cz };
        if (meta == 3) return Vector3{ (float)cx, (float)(cy + up), (float)(cz - out) };
        if (meta == 4) return Vector3{ (float)cx, (float)(cy + up), (float)(cz + out) };
        return Vector3{ (float)cx, (float)cy, (float)cz };
    }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom&) const override {
        Vector3 tip = TipPosition(x, y, z, world.GetMeta(x, y, z));
        world.SpawnParticle(ParticleType::Smoke, tip.x, tip.y, tip.z, 0, 0, 0);
        world.SpawnParticle(ParticleType::Flame, tip.x, tip.y, tip.z, 0, 0, 0);
    }
};

// ---- BlockRedstoneTorch: on unless the block it's attached to is powered;
// burns out if toggled more than 8 times in 5 seconds.

class RedstoneTorchBlock final : public TorchBlock {
public:
    bool active = false;

    RedstoneTorchBlock() { tickRate = 2; ticksRandomly = false; }
    bool CanProvidePower() const override { return true; }
    void NotifyAround(World& world, int x, int y, int z) const {
        for (const auto& o : SIDE_OFFSET) world.NotifyNeighbors(x + o[0], y + o[1], z + o[2], id);
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetMeta(x, y, z) == 0) TorchBlock::OnAdded(world, x, y, z);
        if (active) NotifyAround(world, x, y, z);
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        if (active) NotifyAround(world, x, y, z);
    }
    bool IsPoweringTo(const World& world, int x, int y, int z, int side) const override {
        if (!active) return false;
        int meta = world.GetMeta(x, y, z);
        if (meta == 5 && side == 1) return false;
        if (meta == 3 && side == 3) return false;
        if (meta == 4 && side == 2) return false;
        if (meta == 1 && side == 5) return false;
        return meta != 2 || side != 4;
    }
    bool IsIndirectlyPoweringTo(const World& world, int x, int y, int z, int side) const override {
        return side == 0 && IsPoweringTo(world, x, y, z, side);
    }
    bool AttachedPowered(const World& world, int x, int y, int z) const {
        int meta = world.GetMeta(x, y, z);
        if (meta == 5 && world.IsBlockIndirectlyProvidingPowerTo(x, y - 1, z, 0)) return true;
        if (meta == 3 && world.IsBlockIndirectlyProvidingPowerTo(x, y, z - 1, 2)) return true;
        if (meta == 4 && world.IsBlockIndirectlyProvidingPowerTo(x, y, z + 1, 3)) return true;
        if (meta == 1 && world.IsBlockIndirectlyProvidingPowerTo(x - 1, y, z, 4)) return true;
        return meta == 2 && world.IsBlockIndirectlyProvidingPowerTo(x + 1, y, z, 5);
    }
    bool BurnedOut(World& world, int x, int y, int z, bool record) const {
        auto& toggles = world.TorchToggles();
        if (record) toggles.push_back({ x, y, z, world.GetTickCount() });
        int count = 0;
        for (const World::TorchToggle& t : toggles) {
            if (t.x == x && t.y == y && t.z == z && ++count >= 8) return true;
        }
        return false;
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        bool powered = AttachedPowered(world, x, y, z);
        auto& toggles = world.TorchToggles();
        while (!toggles.empty() && world.GetTickCount() - toggles.front().time > 100) toggles.pop_front();
        int meta = world.GetMeta(x, y, z);
        if (active) {
            if (powered) {
                world.SetBlock(x, y, z, BLOCK_UNLIT_REDSTONE_TORCH, meta);
                if (BurnedOut(world, x, y, z, true)) {
                    world.PlaySound("random.fizz", x + 0.5, y + 0.5, z + 0.5, 0.5f, 2.6f + (random.NextFloat() - random.NextFloat()) * 0.8f);
                    for (int i = 0; i < 5; i++) {
                        world.SpawnParticle(ParticleType::Smoke, x + random.NextDouble() * 0.6 + 0.2, y + random.NextDouble() * 0.6 + 0.2,
                                            z + random.NextDouble() * 0.6 + 0.2, 0, 0, 0);
                    }
                }
            }
        } else if (!powered && !BurnedOut(world, x, y, z, false)) {
            world.SetBlock(x, y, z, BLOCK_REDSTONE_TORCH, meta);
        }
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        TorchBlock::OnNeighborChanged(world, x, y, z, neighbor);
        if (world.GetBlock(x, y, z) == id) world.ScheduleTick(x, y, z, id, tickRate);
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(BLOCK_REDSTONE_TORCH, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(BLOCK_REDSTONE_TORCH, 1); }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (!active) return;
        Vector3 tip = TipPosition(x, y, z, world.GetMeta(x, y, z));
        double jitter = 0.2;
        world.SpawnParticle(ParticleType::Reddust, tip.x + (random.NextFloat() - 0.5) * jitter, tip.y + (random.NextFloat() - 0.5) * jitter,
                            tip.z + (random.NextFloat() - 0.5) * jitter, 0, 0, 0);
    }
};

// ---- BlockRedstoneWire: carries power 15 down to 0, one less per block.

class RedstoneWireBlock final : public Block {
public:
    mutable bool wiresProvidePower = true;
    mutable std::set<std::tuple<int, int, int>> needsUpdate;

    RedstoneWireBlock() { SetNotCube(*this, Shape::RedstoneWire, Layer::Cutout, 0); }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0, 0, 0, 1, 0.0625f, 1); }
    BlockBox ItemBounds(int) const override { return MakeBox(0, 0, 0, 1, 0.0625f, 1); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    int Texture(int side, int meta) const override { return 164; }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && world.IsNormalCube(x, y - 1, z);
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(ITEM_REDSTONE, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(ITEM_REDSTONE, 1); }
    bool CanProvidePower() const override { return wiresProvidePower; }

    static int MaxStrength(const World& world, int x, int y, int z, int current) {
        if (world.GetBlock(x, y, z) != BLOCK_REDSTONE_WIRE) return current;
        return std::max(world.GetMeta(x, y, z), current);
    }

    void CalculateChanges(World& world, int x, int y, int z, int fromX, int fromY, int fromZ) const {
        int old = world.GetMeta(x, y, z);
        int strength = 0;
        wiresProvidePower = false;
        bool powered = world.IsBlockIndirectlyGettingPowered(x, y, z);
        wiresProvidePower = true;
        if (powered) {
            strength = 15;
        } else {
            for (int dir = 0; dir < 4; dir++) {
                int nx = x, nz = z;
                if (dir == 0) nx--;
                if (dir == 1) nx++;
                if (dir == 2) nz--;
                if (dir == 3) nz++;
                if (nx != fromX || y != fromY || nz != fromZ) strength = MaxStrength(world, nx, y, nz, strength);
                if (world.IsNormalCube(nx, y, nz) && !world.IsNormalCube(x, y + 1, z)) {
                    if (nx != fromX || y + 1 != fromY || nz != fromZ) strength = MaxStrength(world, nx, y + 1, nz, strength);
                } else if (!world.IsNormalCube(nx, y, nz) && (nx != fromX || y - 1 != fromY || nz != fromZ)) {
                    strength = MaxStrength(world, nx, y - 1, nz, strength);
                }
            }
            strength = strength > 0 ? strength - 1 : 0;
        }
        if (old == strength) return;

        world.SetMeta(x, y, z, strength, false);
        world.MarkForRender(x, y, z);
        for (int dir = 0; dir < 4; dir++) {
            int nx = x, nz = z, ny = y - 1;
            if (dir == 0) nx--;
            if (dir == 1) nx++;
            if (dir == 2) nz--;
            if (dir == 3) nz++;
            if (world.IsNormalCube(nx, y, nz)) ny += 2;
            int neighbor = MaxStrength(world, nx, y, nz, -1);
            int mine = world.GetMeta(x, y, z);
            if (mine > 0) mine--;
            if (neighbor >= 0 && neighbor != mine) CalculateChanges(world, nx, y, nz, x, y, z);
            neighbor = MaxStrength(world, nx, ny, nz, -1);
            mine = world.GetMeta(x, y, z);
            if (mine > 0) mine--;
            if (neighbor >= 0 && neighbor != mine) CalculateChanges(world, nx, ny, nz, x, y, z);
        }
        if (old < strength || strength == 0) {
            needsUpdate.insert({ x, y, z });
            for (const auto& o : SIDE_OFFSET) needsUpdate.insert({ x + o[0], y + o[1], z + o[2] });
        }
    }
    void UpdateAndPropagate(World& world, int x, int y, int z) const {
        CalculateChanges(world, x, y, z, x, y, z);
        std::set<std::tuple<int, int, int>> todo;
        todo.swap(needsUpdate);
        for (const auto& [px, py, pz] : todo) world.NotifyNeighbors(px, py, pz, id);
    }
    void NotifyWireNeighbors(World& world, int x, int y, int z) const {
        if (world.GetBlock(x, y, z) != id) return;
        world.NotifyNeighbors(x, y, z, id);
        for (const auto& o : SIDE_OFFSET) world.NotifyNeighbors(x + o[0], y + o[1], z + o[2], id);
    }
    void NotifyAll(World& world, int x, int y, int z) const {
        world.NotifyNeighbors(x, y + 1, z, id);
        world.NotifyNeighbors(x, y - 1, z, id);
        NotifyWireNeighbors(world, x - 1, y, z);
        NotifyWireNeighbors(world, x + 1, y, z);
        NotifyWireNeighbors(world, x, y, z - 1);
        NotifyWireNeighbors(world, x, y, z + 1);
        NotifyWireNeighbors(world, x - 1, world.IsNormalCube(x - 1, y, z) ? y + 1 : y - 1, z);
        NotifyWireNeighbors(world, x + 1, world.IsNormalCube(x + 1, y, z) ? y + 1 : y - 1, z);
        NotifyWireNeighbors(world, x, world.IsNormalCube(x, y, z - 1) ? y + 1 : y - 1, z - 1);
        NotifyWireNeighbors(world, x, world.IsNormalCube(x, y, z + 1) ? y + 1 : y - 1, z + 1);
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        UpdateAndPropagate(world, x, y, z);
        NotifyAll(world, x, y, z);
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        world.NotifyNeighbors(x, y + 1, z, id);
        world.NotifyNeighbors(x, y - 1, z, id);
        UpdateAndPropagate(world, x, y, z);
        NotifyAll(world, x, y, z);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!world.IsNormalCube(x, y - 1, z)) {
            BreakAndDrop(world, x, y, z);
            return;
        }
        UpdateAndPropagate(world, x, y, z);
    }
    bool IsIndirectlyPoweringTo(const World& world, int x, int y, int z, int side) const override {
        return wiresProvidePower && IsPoweringTo(world, x, y, z, side);
    }
    // Does the block at (x, y, z) connect to wire coming from direction `dir`
    // (Minecraft's Direction: 0 south, 1 west, 2 north, 3 east; -1 = any)?
    static bool ConnectsTo(const World& world, int x, int y, int z, int dir) {
        int id = world.GetBlock(x, y, z);
        if (id == BLOCK_REDSTONE_WIRE) return true;
        if (id == BLOCK_AIR) return false;
        if (GetBlockDef((BlockId)id).CanProvidePower() && dir != -1) {
            if (id != BLOCK_UNPOWERED_REPEATER && id != BLOCK_POWERED_REPEATER) return true;
        }
        if (id == BLOCK_UNPOWERED_REPEATER || id == BLOCK_POWERED_REPEATER) {
            static const int opposite[4] = { 2, 3, 0, 1 };
            int facing = world.GetMeta(x, y, z) & 3;
            return dir == facing || dir == opposite[facing];
        }
        return false;
    }
    bool IsPoweringTo(const World& world, int x, int y, int z, int side) const override {
        if (!wiresProvidePower) return false;
        if (world.GetMeta(x, y, z) == 0) return false;
        if (side == 1) return true;
        bool west = ConnectsTo(world, x - 1, y, z, 1) || (!world.IsNormalCube(x - 1, y, z) && ConnectsTo(world, x - 1, y - 1, z, -1));
        bool east = ConnectsTo(world, x + 1, y, z, 3) || (!world.IsNormalCube(x + 1, y, z) && ConnectsTo(world, x + 1, y - 1, z, -1));
        bool north = ConnectsTo(world, x, y, z - 1, 2) || (!world.IsNormalCube(x, y, z - 1) && ConnectsTo(world, x, y - 1, z - 1, -1));
        bool south = ConnectsTo(world, x, y, z + 1, 0) || (!world.IsNormalCube(x, y, z + 1) && ConnectsTo(world, x, y - 1, z + 1, -1));
        if (!world.IsNormalCube(x, y + 1, z)) {
            if (world.IsNormalCube(x - 1, y, z) && ConnectsTo(world, x - 1, y + 1, z, -1)) west = true;
            if (world.IsNormalCube(x + 1, y, z) && ConnectsTo(world, x + 1, y + 1, z, -1)) east = true;
            if (world.IsNormalCube(x, y, z - 1) && ConnectsTo(world, x, y + 1, z - 1, -1)) north = true;
            if (world.IsNormalCube(x, y, z + 1) && ConnectsTo(world, x, y + 1, z + 1, -1)) south = true;
        }
        if (!north && !east && !west && !south && side >= 2 && side <= 5) return true;
        if (side == 2 && north && !west && !east) return true;
        if (side == 3 && south && !west && !east) return true;
        if (side == 4 && west && !north && !south) return true;
        return side == 5 && east && !north && !south;
    }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        int power = world.GetMeta(x, y, z);
        if (power <= 0) return;
        float f = power / 15.0f;
        float r = f * 0.6f + 0.4f;
        float g = std::max(0.0f, f * f * 0.7f - 0.5f);
        float b = std::max(0.0f, f * f * 0.6f - 0.7f);
        world.SpawnParticle(ParticleType::Reddust, x + 0.5 + (random.NextFloat() - 0.5) * 0.2, y + 0.0625,
                            z + 0.5 + (random.NextFloat() - 0.5) * 0.2, r, g, b);
    }
};

// ---- BlockRedstoneRepeater: passes power along one way after 1-4 ticks' delay.

class RepeaterBlock final : public Block {
public:
    bool powered = false;
    static constexpr int DELAYS[4] = { 1, 2, 3, 4 };

    RepeaterBlock() { SetNotCube(*this, Shape::Repeater, Layer::Cutout, 0); }
    int Texture(int side, int meta) const override {
        if (side == SIDE_DOWN) return powered ? 99 : 115;
        if (side == SIDE_UP) return powered ? 147 : 131;
        return 5;
    }
    int ItemIconTexture(int) const override { return 131; }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0, 0, 0, 1, 0.125f, 1); }
    BlockBox ItemBounds(int) const override { return MakeBox(0, 0, 0, 1, 0.125f, 1); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanProvidePower() const override { return false; }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && world.IsNormalCube(x, y - 1, z);
    }
    bool CanStay(const World& world, int x, int y, int z) const override { return world.IsNormalCube(x, y - 1, z); }
    bool IsIndirectlyPoweringTo(const World& world, int x, int y, int z, int side) const override {
        if (!powered) return false;
        int facing = world.GetMeta(x, y, z) & 3;
        if (facing == 0 && side == 3) return true;
        if (facing == 1 && side == 4) return true;
        if (facing == 2 && side == 2) return true;
        return facing == 3 && side == 5;
    }
    bool IsPoweringTo(const World& world, int x, int y, int z, int side) const override {
        return IsIndirectlyPoweringTo(world, x, y, z, side);
    }
    static bool InputPowered(const World& world, int x, int y, int z, int meta) {
        switch (meta & 3) {
            case 0: return world.IsBlockIndirectlyProvidingPowerTo(x, y, z + 1, 3) ||
                           (world.GetBlock(x, y, z + 1) == BLOCK_REDSTONE_WIRE && world.GetMeta(x, y, z + 1) > 0);
            case 1: return world.IsBlockIndirectlyProvidingPowerTo(x - 1, y, z, 4) ||
                           (world.GetBlock(x - 1, y, z) == BLOCK_REDSTONE_WIRE && world.GetMeta(x - 1, y, z) > 0);
            case 2: return world.IsBlockIndirectlyProvidingPowerTo(x, y, z - 1, 2) ||
                           (world.GetBlock(x, y, z - 1) == BLOCK_REDSTONE_WIRE && world.GetMeta(x, y, z - 1) > 0);
            default: return world.IsBlockIndirectlyProvidingPowerTo(x + 1, y, z, 5) ||
                            (world.GetBlock(x + 1, y, z) == BLOCK_REDSTONE_WIRE && world.GetMeta(x + 1, y, z) > 0);
        }
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        int meta = world.GetMeta(x, y, z);
        bool input = InputPowered(world, x, y, z, meta);
        if (powered && !input) {
            world.SetBlock(x, y, z, BLOCK_UNPOWERED_REPEATER, meta);
        } else if (!powered) {
            world.SetBlock(x, y, z, BLOCK_POWERED_REPEATER, meta);
            if (!input) world.ScheduleTick(x, y, z, BLOCK_POWERED_REPEATER, DELAYS[(meta & 0xC) >> 2] * 2);
        }
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!CanStay(world, x, y, z)) {
            BreakAndDrop(world, x, y, z);
            return;
        }
        int meta = world.GetMeta(x, y, z);
        bool input = InputPowered(world, x, y, z, meta);
        int delay = DELAYS[(meta & 0xC) >> 2] * 2;
        if ((powered && !input) || (!powered && input)) world.ScheduleTick(x, y, z, id, delay);
    }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        int meta = world.GetMeta(x, y, z);
        int delay = (((meta & 0xC) >> 2) + 1) << 2 & 0xC;
        world.SetMeta(x, y, z, delay | (meta & 3));
        return true;
    }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        int facing = (PlayerYawIndex(player) + 2) % 4;
        world.SetMeta(x, y, z, facing);
        if (InputPowered(world, x, y, z, facing)) world.ScheduleTick(x, y, z, id, 1);
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        for (const auto& o : SIDE_OFFSET) world.NotifyNeighbors(x + o[0], y + o[1], z + o[2], id);
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(ITEM_REPEATER, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(ITEM_REPEATER, 1); }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (!powered) return;
        // A spark at one of the two torches (BlockRedstoneRepeater.randomDisplayTick).
        int meta = world.GetMeta(x, y, z);
        double px = x + 0.5 + (random.NextFloat() - 0.5) * 0.2;
        double py = y + 0.4 + (random.NextFloat() - 0.5) * 0.2;
        double pz = z + 0.5 + (random.NextFloat() - 0.5) * 0.2;
        double dx = 0.0, dz = 0.0;
        if (random.NextInt(2) == 0) {
            switch (meta & 3) {
                case 0: dz = -0.3125; break;
                case 2: dz = 0.3125; break;
                case 3: dx = -0.3125; break;
                case 1: dx = 0.3125; break;
            }
        } else {
            static const double torchOffsets[4] = { -0.0625, 0.0625, 0.1875, 0.3125 };
            double o = torchOffsets[(meta & 0xC) >> 2];
            switch (meta & 3) {
                case 0: dz = o; break;
                case 2: dz = -o; break;
                case 3: dx = o; break;
                case 1: dx = -o; break;
            }
        }
        world.SpawnParticle(ParticleType::Reddust, px + dx, py, pz + dz, 0, 0, 0);
    }
};

// Levers, buttons and plates power the block they're on strongly, and tell its
// neighbors when they change.
void NotifyAttached(World& world, int x, int y, int z, int id, int attachment) {
    world.NotifyNeighbors(x, y, z, id);
    if (attachment == 1) world.NotifyNeighbors(x - 1, y, z, id);
    else if (attachment == 2) world.NotifyNeighbors(x + 1, y, z, id);
    else if (attachment == 3) world.NotifyNeighbors(x, y, z - 1, id);
    else if (attachment == 4) world.NotifyNeighbors(x, y, z + 1, id);
    else world.NotifyNeighbors(x, y - 1, z, id);
}

// ---- BlockLever: flips on and off, on a wall or the ground.

class LeverBlock final : public Block {
public:
    LeverBlock() { SetNotCube(*this, Shape::Lever, Layer::Cutout, 0); }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int meta = world.GetMeta(x, y, z) & 7;
        const float f = 0.1875f;
        if (meta == 1) return MakeBox(0, 0.2f, 0.5f - f, f * 2, 0.8f, 0.5f + f);
        if (meta == 2) return MakeBox(1 - f * 2, 0.2f, 0.5f - f, 1, 0.8f, 0.5f + f);
        if (meta == 3) return MakeBox(0.5f - f, 0.2f, 0, 0.5f + f, 0.8f, f * 2);
        if (meta == 4) return MakeBox(0.5f - f, 0.2f, 1 - f * 2, 0.5f + f, 0.8f, 1);
        return MakeBox(0.25f, 0, 0.25f, 0.75f, 0.6f, 0.75f);
    }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanProvidePower() const override { return true; }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        return world.IsNormalCube(x - 1, y, z) || world.IsNormalCube(x + 1, y, z) || world.IsNormalCube(x, y, z - 1) ||
               world.IsNormalCube(x, y, z + 1) || world.IsNormalCube(x, y - 1, z);
    }
    bool CanPlaceOnSide(const World& world, int x, int y, int z, int side) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        if (side == SIDE_UP) return world.IsNormalCube(x, y - 1, z);
        if (side == SIDE_NORTH) return world.IsNormalCube(x, y, z + 1);
        if (side == SIDE_SOUTH) return world.IsNormalCube(x, y, z - 1);
        if (side == SIDE_WEST) return world.IsNormalCube(x + 1, y, z);
        if (side == SIDE_EAST) return world.IsNormalCube(x - 1, y, z);
        return false;
    }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player&) const override {
        int meta = -1;
        if (side == SIDE_UP && world.IsNormalCube(x, y - 1, z)) meta = 5 + world.Random().NextInt(2);
        if (side == SIDE_NORTH && world.IsNormalCube(x, y, z + 1)) meta = 4;
        if (side == SIDE_SOUTH && world.IsNormalCube(x, y, z - 1)) meta = 3;
        if (side == SIDE_WEST && world.IsNormalCube(x + 1, y, z)) meta = 2;
        if (side == SIDE_EAST && world.IsNormalCube(x - 1, y, z)) meta = 1;
        if (meta < 0) meta = 5;
        world.SetMeta(x, y, z, meta);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        int meta = world.GetMeta(x, y, z) & 7;
        bool unsupported = (meta == 1 && !world.IsNormalCube(x - 1, y, z)) || (meta == 2 && !world.IsNormalCube(x + 1, y, z)) ||
                           (meta == 3 && !world.IsNormalCube(x, y, z - 1)) || (meta == 4 && !world.IsNormalCube(x, y, z + 1)) ||
                           ((meta == 5 || meta == 6) && !world.IsNormalCube(x, y - 1, z));
        if (unsupported) BreakAndDrop(world, x, y, z);
    }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        int meta = world.GetMeta(x, y, z);
        int attachment = meta & 7;
        int on = 8 - (meta & 8);
        world.SetMeta(x, y, z, attachment + on);
        world.MarkForRender(x, y, z);
        world.PlaySound("random.click", x + 0.5, y + 0.5, z + 0.5, 0.3f, on > 0 ? 0.6f : 0.5f);
        NotifyAttached(world, x, y, z, id, attachment);
        return true;
    }
    void OnClicked(World& world, int x, int y, int z, Player& player) const override { OnActivated(world, x, y, z, player); }
    void OnRemoved(World& world, int x, int y, int z, int oldMeta) const override {
        if (oldMeta & 8) NotifyAttached(world, x, y, z, id, oldMeta & 7);
    }
    bool IsPoweringTo(const World& world, int x, int y, int z, int) const override { return (world.GetMeta(x, y, z) & 8) != 0; }
    bool IsIndirectlyPoweringTo(const World& world, int x, int y, int z, int side) const override {
        int meta = world.GetMeta(x, y, z);
        if ((meta & 8) == 0) return false;
        int a = meta & 7;
        if ((a == 6 || a == 5) && side == 1) return true;
        if (a == 4 && side == 2) return true;
        if (a == 3 && side == 3) return true;
        if (a == 2 && side == 4) return true;
        return a == 1 && side == 5;
    }
};

// ---- BlockButton: powers for a second (20 ticks) when pressed.

class ButtonBlock final : public Block {
public:
    ButtonBlock() { SetNotCube(*this, Shape::Cube, Layer::Opaque, 0); tickRate = 20; }
    int Texture(int, int) const override { return 1; }
    BlockBox Box(int meta) const {
        int a = meta & 7;
        bool pressed = (meta & 8) != 0;
        const float lo = 0.375f, hi = 0.625f, half = 0.1875f;
        float depth = pressed ? 0.0625f : 0.125f;
        if (a == 1) return MakeBox(0, lo, 0.5f - half, depth, hi, 0.5f + half);
        if (a == 2) return MakeBox(1 - depth, lo, 0.5f - half, 1, hi, 0.5f + half);
        if (a == 3) return MakeBox(0.5f - half, lo, 0, 0.5f + half, hi, depth);
        return MakeBox(0.5f - half, lo, 1 - depth, 0.5f + half, hi, 1);
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override { return Box(world.GetMeta(x, y, z)); }
    BlockBox ItemBounds(int) const override { return MakeBox(0.3125f, 0.375f, 0.4375f, 0.6875f, 0.625f, 0.5625f); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanProvidePower() const override { return true; }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        return world.IsNormalCube(x - 1, y, z) || world.IsNormalCube(x + 1, y, z) || world.IsNormalCube(x, y, z - 1) ||
               world.IsNormalCube(x, y, z + 1);
    }
    bool CanPlaceOnSide(const World& world, int x, int y, int z, int side) const override {
        if (!Block::CanPlaceAt(world, x, y, z)) return false;
        if (side == SIDE_NORTH) return world.IsNormalCube(x, y, z + 1);
        if (side == SIDE_SOUTH) return world.IsNormalCube(x, y, z - 1);
        if (side == SIDE_WEST) return world.IsNormalCube(x + 1, y, z);
        if (side == SIDE_EAST) return world.IsNormalCube(x - 1, y, z);
        return false;
    }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player&) const override {
        int meta = 0;
        if (side == SIDE_NORTH && world.IsNormalCube(x, y, z + 1)) meta = 4;
        else if (side == SIDE_SOUTH && world.IsNormalCube(x, y, z - 1)) meta = 3;
        else if (side == SIDE_WEST && world.IsNormalCube(x + 1, y, z)) meta = 2;
        else if (side == SIDE_EAST && world.IsNormalCube(x - 1, y, z)) meta = 1;
        else if (world.IsNormalCube(x - 1, y, z)) meta = 1;
        else if (world.IsNormalCube(x + 1, y, z)) meta = 2;
        else if (world.IsNormalCube(x, y, z - 1)) meta = 3;
        else meta = 4;
        world.SetMeta(x, y, z, meta);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        int a = world.GetMeta(x, y, z) & 7;
        bool unsupported = (a == 1 && !world.IsNormalCube(x - 1, y, z)) || (a == 2 && !world.IsNormalCube(x + 1, y, z)) ||
                           (a == 3 && !world.IsNormalCube(x, y, z - 1)) || (a == 4 && !world.IsNormalCube(x, y, z + 1));
        if (unsupported) BreakAndDrop(world, x, y, z);
    }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        int meta = world.GetMeta(x, y, z);
        if (meta & 8) return true;
        world.SetMeta(x, y, z, (meta & 7) | 8);
        world.MarkForRender(x, y, z);
        world.PlaySound("random.click", x + 0.5, y + 0.5, z + 0.5, 0.3f, 0.6f);
        NotifyAttached(world, x, y, z, id, meta & 7);
        world.ScheduleTick(x, y, z, id, tickRate);
        return true;
    }
    void OnClicked(World& world, int x, int y, int z, Player& player) const override { OnActivated(world, x, y, z, player); }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        int meta = world.GetMeta(x, y, z);
        if ((meta & 8) == 0) return;
        world.SetMeta(x, y, z, meta & 7);
        NotifyAttached(world, x, y, z, id, meta & 7);
        world.PlaySound("random.click", x + 0.5, y + 0.5, z + 0.5, 0.3f, 0.5f);
        world.MarkForRender(x, y, z);
    }
    void OnRemoved(World& world, int x, int y, int z, int oldMeta) const override {
        if (oldMeta & 8) NotifyAttached(world, x, y, z, id, oldMeta & 7);
    }
    bool IsPoweringTo(const World& world, int x, int y, int z, int) const override { return (world.GetMeta(x, y, z) & 8) != 0; }
    bool IsIndirectlyPoweringTo(const World& world, int x, int y, int z, int side) const override {
        int meta = world.GetMeta(x, y, z);
        if ((meta & 8) == 0) return false;
        int a = meta & 7;
        if (a == 5 && side == 1) return true;
        if (a == 4 && side == 2) return true;
        if (a == 3 && side == 3) return true;
        if (a == 2 && side == 4) return true;
        return a == 1 && side == 5;
    }
};

// ---- BlockPressurePlate: pressed while something stands on it (wood: anything,
// stone: only the player).

class PressurePlateBlock final : public Block {
public:
    bool anything = false;
    PressurePlateBlock() { SetNotCube(*this, Shape::Cube, Layer::Opaque, 0); tickRate = 20; }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        const float f = 0.0625f;
        return MakeBox(f, 0, f, 1 - f, world.GetMeta(x, y, z) == 1 ? 0.03125f : 0.0625f, 1 - f);
    }
    BlockBox ItemBounds(int) const override { return MakeBox(0, 0.375f, 0, 1, 0.625f, 1); }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanProvidePower() const override { return true; }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && CanSupportOnTop(world, x, y - 1, z);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!CanSupportOnTop(world, x, y - 1, z)) BreakAndDrop(world, x, y, z);
    }
    void Check(World& world, int x, int y, int z) const {
        bool wasOn = world.GetMeta(x, y, z) == 1;
        const double f = 0.125;
        double min[3] = { x + f, (double)y, z + f }, max[3] = { x + 1 - f, y + 0.25, z + 1 - f };
        bool on = world.EntitiesInBox(min, max, !anything);
        if (on && !wasOn) {
            world.SetMeta(x, y, z, 1);
            world.NotifyNeighbors(x, y, z, id);
            world.NotifyNeighbors(x, y - 1, z, id);
            world.MarkForRender(x, y, z);
            world.PlaySound("random.click", x + 0.5, y + 0.1, z + 0.5, 0.3f, 0.6f);
        }
        if (!on && wasOn) {
            world.SetMeta(x, y, z, 0);
            world.NotifyNeighbors(x, y, z, id);
            world.NotifyNeighbors(x, y - 1, z, id);
            world.MarkForRender(x, y, z);
            world.PlaySound("random.click", x + 0.5, y + 0.1, z + 0.5, 0.3f, 0.5f);
        }
        if (on) world.ScheduleTick(x, y, z, id, tickRate);
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (world.GetMeta(x, y, z) != 0) Check(world, x, y, z);
    }
    void OnPlayerCollided(World& world, int x, int y, int z, Player&) const override {
        if (world.GetMeta(x, y, z) != 1) Check(world, x, y, z);
    }
    void OnRemoved(World& world, int x, int y, int z, int oldMeta) const override {
        if (oldMeta > 0) {
            world.NotifyNeighbors(x, y, z, id);
            world.NotifyNeighbors(x, y - 1, z, id);
        }
    }
    bool IsPoweringTo(const World& world, int x, int y, int z, int) const override { return world.GetMeta(x, y, z) > 0; }
    bool IsIndirectlyPoweringTo(const World& world, int x, int y, int z, int side) const override {
        return world.GetMeta(x, y, z) != 0 && side == 1;
    }
};

// ---- BlockRedstoneOre: glows when touched, then fades.

class RedstoneOreBlock final : public Block {
public:
    bool glowing = false;
    RedstoneOreBlock() { tickRate = 30; }
    void Glow(World& world, int x, int y, int z) const {
        Sparkle(world, x, y, z);
        if (!glowing) world.SetBlock(x, y, z, BLOCK_LIT_REDSTONE_ORE);
    }
    void OnClicked(World& world, int x, int y, int z, Player&) const override { Glow(world, x, y, z); }
    void OnPlayerWalking(World& world, int x, int y, int z, Player&) const override { Glow(world, x, y, z); }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        Glow(world, x, y, z);
        return false;
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (glowing) world.SetBlock(x, y, z, BLOCK_REDSTONE_ORE);
    }
    void Drops(World&, int, int, int, int, JavaRandom& random, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(ITEM_REDSTONE, 4 + random.NextInt(2)));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(BLOCK_REDSTONE_ORE, 1); }
    void Sparkle(World& world, int x, int y, int z) const {
        JavaRandom& random = world.Random();
        const double off = 0.0625;
        for (int side = 0; side < 6; side++) {
            double px = x + random.NextFloat(), py = y + random.NextFloat(), pz = z + random.NextFloat();
            if (side == 0 && !world.IsOpaqueCube(x, y + 1, z)) py = y + 1 + off;
            else if (side == 1 && !world.IsOpaqueCube(x, y - 1, z)) py = y - off;
            else if (side == 2 && !world.IsOpaqueCube(x, y, z + 1)) pz = z + 1 + off;
            else if (side == 3 && !world.IsOpaqueCube(x, y, z - 1)) pz = z - off;
            else if (side == 4 && !world.IsOpaqueCube(x + 1, y, z)) px = x + 1 + off;
            else if (side == 5 && !world.IsOpaqueCube(x - 1, y, z)) px = x - off;
            else continue;
            world.SpawnParticle(ParticleType::Reddust, px, py, pz, 0, 0, 0);
        }
    }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if (glowing) Sparkle(world, x, y, z);
    }
};

// ---- Rails (BlockRail + RailLogic): connect themselves to neighboring rails,
// sloping up onto blocks; powered rails pass power along up to 8 rails.

bool IsRailId(int id) { return id == BLOCK_RAIL || id == BLOCK_POWERED_RAIL || id == BLOCK_DETECTOR_RAIL; }
bool IsRailAt(const World& world, int x, int y, int z) { return IsRailId(world.GetBlock(x, y, z)); }

class RailLogic {
public:
    struct Pos { int x, y, z; };
    World& world;
    int x, y, z;
    bool straight;
    std::vector<Pos> connected;

    RailLogic(World& w, int px, int py, int pz) : world(w), x(px), y(py), z(pz) {
        int id = world.GetBlock(px, py, pz);
        int meta = world.GetMeta(px, py, pz);
        straight = id != BLOCK_RAIL;
        if (straight) meta &= ~8;
        SetConnections(meta);
    }
    void SetConnections(int meta) {
        connected.clear();
        switch (meta) {
            case 0: connected = { { x, y, z - 1 }, { x, y, z + 1 } }; break;
            case 1: connected = { { x - 1, y, z }, { x + 1, y, z } }; break;
            case 2: connected = { { x - 1, y, z }, { x + 1, y + 1, z } }; break;
            case 3: connected = { { x - 1, y + 1, z }, { x + 1, y, z } }; break;
            case 4: connected = { { x, y + 1, z - 1 }, { x, y, z + 1 } }; break;
            case 5: connected = { { x, y, z - 1 }, { x, y + 1, z + 1 } }; break;
            case 6: connected = { { x + 1, y, z }, { x, y, z + 1 } }; break;
            case 7: connected = { { x - 1, y, z }, { x, y, z + 1 } }; break;
            case 8: connected = { { x - 1, y, z }, { x, y, z - 1 } }; break;
            case 9: connected = { { x + 1, y, z }, { x, y, z - 1 } }; break;
        }
    }
    bool IsMinecartTrack(int px, int py, int pz) const {
        return IsRailAt(world, px, py, pz) || IsRailAt(world, px, py + 1, pz) || IsRailAt(world, px, py - 1, pz);
    }
    bool LogicAt(int px, int py, int pz, int& ry) const {
        if (IsRailAt(world, px, py, pz)) { ry = py; return true; }
        if (IsRailAt(world, px, py + 1, pz)) { ry = py + 1; return true; }
        if (IsRailAt(world, px, py - 1, pz)) { ry = py - 1; return true; }
        return false;
    }
    bool IsConnectedTo(int px, int pz) const {
        for (const Pos& p : connected)
            if (p.x == px && p.z == pz) return true;
        return false;
    }
    bool IsConnectedTo(const RailLogic& other) const {
        for (const Pos& p : connected)
            if (p.x == other.x && p.z == other.z) return true;
        return false;
    }
    void RefreshConnected() {
        std::vector<Pos> kept;
        for (const Pos& p : connected) {
            int ry;
            if (!LogicAt(p.x, p.y, p.z, ry)) continue;
            RailLogic other(world, p.x, ry, p.z);
            if (other.IsConnectedTo(*this)) kept.push_back({ p.x, ry, p.z });
        }
        connected = kept;
    }
    bool CanConnectTo(const RailLogic& other) const {
        if (IsConnectedTo(other)) return true;
        return connected.size() < 2;
    }
    int AdjacentTracks() const {
        int count = 0;
        if (IsMinecartTrack(x, y, z - 1)) count++;
        if (IsMinecartTrack(x, y, z + 1)) count++;
        if (IsMinecartTrack(x - 1, y, z)) count++;
        if (IsMinecartTrack(x + 1, y, z)) count++;
        return count;
    }
    void ConnectTo(const RailLogic& other) {
        connected.push_back({ other.x, other.y, other.z });
        bool n = IsConnectedTo(x, z - 1), s = IsConnectedTo(x, z + 1), w = IsConnectedTo(x - 1, z), e = IsConnectedTo(x + 1, z);
        int meta = -1;
        if (n || s) meta = 0;
        if (w || e) meta = 1;
        if (!straight) {
            if (s && e && !n && !w) meta = 6;
            if (s && w && !n && !e) meta = 7;
            if (n && w && !s && !e) meta = 8;
            if (n && e && !s && !w) meta = 9;
        }
        if (meta == 0) {
            if (IsRailAt(world, x, y + 1, z - 1)) meta = 4;
            if (IsRailAt(world, x, y + 1, z + 1)) meta = 5;
        }
        if (meta == 1) {
            if (IsRailAt(world, x + 1, y + 1, z)) meta = 2;
            if (IsRailAt(world, x - 1, y + 1, z)) meta = 3;
        }
        if (meta < 0) meta = 0;
        int full = straight ? (world.GetMeta(x, y, z) & 8) | meta : meta;
        world.SetMeta(x, y, z, full);
    }
    bool CanConnectFrom(int px, int py, int pz) {
        int ry;
        if (!LogicAt(px, py, pz, ry)) return false;
        RailLogic other(world, px, ry, pz);
        other.RefreshConnected();
        return other.CanConnectTo(*this);
    }
    void RefreshShape(bool powered) {
        bool n = CanConnectFrom(x, y, z - 1), s = CanConnectFrom(x, y, z + 1);
        bool w = CanConnectFrom(x - 1, y, z), e = CanConnectFrom(x + 1, y, z);
        int meta = -1;
        if ((n || s) && !w && !e) meta = 0;
        if ((w || e) && !n && !s) meta = 1;
        if (!straight) {
            if (s && e && !n && !w) meta = 6;
            if (s && w && !n && !e) meta = 7;
            if (n && w && !s && !e) meta = 8;
            if (n && e && !s && !w) meta = 9;
        }
        if (meta == -1) {
            if (n || s) meta = 0;
            if (w || e) meta = 1;
            if (!straight) {
                if (powered) {
                    if (s && e) meta = 6;
                    if (w && s) meta = 7;
                    if (e && n) meta = 9;
                    if (n && w) meta = 8;
                } else {
                    if (n && w) meta = 8;
                    if (e && n) meta = 9;
                    if (w && s) meta = 7;
                    if (s && e) meta = 6;
                }
            }
        }
        if (meta == 0) {
            if (IsRailAt(world, x, y + 1, z - 1)) meta = 4;
            if (IsRailAt(world, x, y + 1, z + 1)) meta = 5;
        }
        if (meta == 1) {
            if (IsRailAt(world, x + 1, y + 1, z)) meta = 2;
            if (IsRailAt(world, x - 1, y + 1, z)) meta = 3;
        }
        if (meta < 0) meta = 0;
        SetConnections(meta);
        int full = straight ? (world.GetMeta(x, y, z) & 8) | meta : meta;
        if (powered || world.GetMeta(x, y, z) != full) {
            world.SetMeta(x, y, z, full);
            for (const Pos& p : connected) {
                int ry;
                if (!LogicAt(p.x, p.y, p.z, ry)) continue;
                RailLogic other(world, p.x, ry, p.z);
                other.RefreshConnected();
                if (other.CanConnectTo(*this)) other.ConnectTo(*this);
            }
        }
    }
};

class RailBlock final : public Block {
public:
    RailBlock() { SetNotCube(*this, Shape::Rail, Layer::Cutout, 0); }
    bool Straight() const { return id != BLOCK_RAIL; }
    int Texture(int side, int meta) const override {
        if (id == BLOCK_POWERED_RAIL) return (meta & 8) ? 163 : 179;
        if (id == BLOCK_DETECTOR_RAIL) return 195;
        return meta >= 6 ? 112 : 128;
    }
    int ItemIconTexture(int) const override { return id == BLOCK_POWERED_RAIL ? 179 : texture; }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        int shape = world.GetMeta(x, y, z);
        if (Straight()) shape &= 7;
        return MakeBox(0, 0, 0, 1, (shape >= 2 && shape <= 5) ? 0.625f : 0.125f, 1);
    }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && world.IsNormalCube(x, y - 1, z);
    }
    bool CanProvidePower() const override { return id == BLOCK_DETECTOR_RAIL; }
    void OnAdded(World& world, int x, int y, int z) const override {
        RailLogic(world, x, y, z).RefreshShape(true);
    }
    // Powered rails: carried power through up to 8 connected powered rails.
    bool PoweredThrough(World& world, int x, int y, int z, int meta, bool forward, int distance) const {
        if (distance >= 8) return false;
        int shape = meta & 7;
        bool checkBelow = true;
        switch (shape) {
            case 0: if (forward) z++; else z--; break;
            case 1: if (forward) x--; else x++; break;
            case 2: if (forward) x--; else { x++; y++; checkBelow = false; } shape = 1; break;
            case 3: if (forward) { x--; y++; checkBelow = false; } else x++; shape = 1; break;
            case 4: if (forward) z++; else { z--; y++; checkBelow = false; } shape = 0; break;
            case 5: if (forward) { z++; y++; checkBelow = false; } else z--; shape = 0; break;
        }
        if (PoweredRailAt(world, x, y, z, forward, distance, shape)) return true;
        return checkBelow && PoweredRailAt(world, x, y - 1, z, forward, distance, shape);
    }
    bool PoweredRailAt(World& world, int x, int y, int z, bool forward, int distance, int shape) const {
        if (world.GetBlock(x, y, z) != BLOCK_POWERED_RAIL) return false;
        int meta = world.GetMeta(x, y, z);
        int other = meta & 7;
        if (shape == 1 && (other == 0 || other == 4 || other == 5)) return false;
        if (shape == 0 && (other == 1 || other == 2 || other == 3)) return false;
        if ((meta & 8) == 0) return false;
        if (world.IsBlockIndirectlyGettingPowered(x, y, z) || world.IsBlockIndirectlyGettingPowered(x, y + 1, z)) return true;
        return PoweredThrough(world, x, y, z, meta, forward, distance + 1);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        int meta = world.GetMeta(x, y, z);
        int shape = Straight() ? meta & 7 : meta;
        bool drop = !world.IsNormalCube(x, y - 1, z);
        if (shape == 2 && !world.IsNormalCube(x + 1, y, z)) drop = true;
        if (shape == 3 && !world.IsNormalCube(x - 1, y, z)) drop = true;
        if (shape == 4 && !world.IsNormalCube(x, y, z - 1)) drop = true;
        if (shape == 5 && !world.IsNormalCube(x, y, z + 1)) drop = true;
        if (drop) {
            BreakAndDrop(world, x, y, z);
            return;
        }
        if (id == BLOCK_POWERED_RAIL) {
            bool powered = world.IsBlockIndirectlyGettingPowered(x, y, z) || world.IsBlockIndirectlyGettingPowered(x, y + 1, z);
            powered = powered || PoweredThrough(world, x, y, z, meta, true, 0) || PoweredThrough(world, x, y, z, meta, false, 0);
            bool changed = false;
            if (powered && (meta & 8) == 0) {
                world.SetMeta(x, y, z, shape | 8);
                changed = true;
            } else if (!powered && (meta & 8) != 0) {
                world.SetMeta(x, y, z, shape);
                changed = true;
            }
            if (changed) {
                world.NotifyNeighbors(x, y - 1, z, id);
                if (shape >= 2 && shape <= 5) world.NotifyNeighbors(x, y + 1, z, id);
            }
        } else if (neighbor > 0 && GetBlockDef((BlockId)neighbor).CanProvidePower() && !Straight() &&
                   RailLogic(world, x, y, z).AdjacentTracks() == 3) {
            RailLogic(world, x, y, z).RefreshShape(false);
        }
    }
    int DamageDropped(int) const override { return 0; }
};

} // namespace

void RegisterRedstoneBlocks(BlockRegistry& r) {
    { auto& b = Props(r.Add<TorchBlock>(BLOCK_TORCH), "Torch", 80, 0.0f, BlockMaterial::Circuits, StepSound::Wood);
      b.lightValue = 14; b.ticksRandomly = true; }
    { auto& b = Props(r.Add<RedstoneWireBlock>(BLOCK_REDSTONE_WIRE), "Redstone Dust", 164, 0.0f, BlockMaterial::Circuits, StepSound::Powder); (void)b; }
    { auto& b = Props(r.Add<RedstoneTorchBlock>(BLOCK_UNLIT_REDSTONE_TORCH), "Redstone Torch", 115, 0.0f, BlockMaterial::Circuits, StepSound::Wood);
      b.active = false; }
    { auto& b = Props(r.Add<RedstoneTorchBlock>(BLOCK_REDSTONE_TORCH), "Redstone Torch", 99, 0.0f, BlockMaterial::Circuits, StepSound::Wood);
      b.active = true; b.lightValue = 7; }
    { auto& b = Props(r.Add<RepeaterBlock>(BLOCK_UNPOWERED_REPEATER), "Redstone Repeater", 131, 0.0f, BlockMaterial::Circuits, StepSound::Wood); b.powered = false; }
    { auto& b = Props(r.Add<RepeaterBlock>(BLOCK_POWERED_REPEATER), "Redstone Repeater", 147, 0.0f, BlockMaterial::Circuits, StepSound::Wood);
      b.powered = true; b.lightValue = 9; }
    Props(r.Add<LeverBlock>(BLOCK_LEVER), "Lever", 96, 0.5f, BlockMaterial::Circuits, StepSound::Wood);
    Props(r.Add<ButtonBlock>(BLOCK_STONE_BUTTON), "Button", 1, 0.5f, BlockMaterial::Circuits, StepSound::Stone);
    { auto& b = Props(r.Add<PressurePlateBlock>(BLOCK_STONE_PRESSURE_PLATE), "Pressure Plate", 1, 0.5f, BlockMaterial::Rock, StepSound::Stone); b.anything = false; }
    { auto& b = Props(r.Add<PressurePlateBlock>(BLOCK_WOODEN_PRESSURE_PLATE), "Pressure Plate", 4, 0.5f, BlockMaterial::Wood, StepSound::Wood); b.anything = true; }
    { auto& b = Props(r.Add<RedstoneOreBlock>(BLOCK_REDSTONE_ORE), "Redstone Ore", 51, 3.0f, BlockMaterial::Rock, StepSound::Stone); SetResistance(b, 5); }
    { auto& b = Props(r.Add<RedstoneOreBlock>(BLOCK_LIT_REDSTONE_ORE), "Redstone Ore", 51, 3.0f, BlockMaterial::Rock, StepSound::Stone);
      SetResistance(b, 5); b.glowing = true; b.ticksRandomly = true; b.lightValue = 9; }
    Props(r.Add<RailBlock>(BLOCK_RAIL), "Rail", 128, 0.7f, BlockMaterial::Circuits, StepSound::Metal);
    Props(r.Add<RailBlock>(BLOCK_POWERED_RAIL), "Powered Rail", 179, 0.7f, BlockMaterial::Circuits, StepSound::Metal);
    Props(r.Add<RailBlock>(BLOCK_DETECTOR_RAIL), "Detector Rail", 195, 0.7f, BlockMaterial::Circuits, StepSound::Metal);
}
