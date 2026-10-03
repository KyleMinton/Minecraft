#include "BlocksCommon.h"
#include "Fluids.h"

#include <algorithm>
#include <cmath>

namespace {

// ---- BlockFluid: what water and lava have in common.

class FluidBlock : public Block {
public:
    FluidBlock() {
        shape = Shape::Fluid;
        opaqueCube = false;
        normalCube = false;
    }
    bool IsTargetable(int meta) const override { return false; }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}
    int Texture(int side, int meta) const override {
        return (side == SIDE_UP || side == SIDE_DOWN) ? texture : texture + 1;
    }

    static int FlowDecay(const World& world, int x, int y, int z, BlockMaterial material) {
        return world.GetMaterial(x, y, z) == material ? world.GetMeta(x, y, z) : -1;
    }

    // Lava touching water hardens: a source into obsidian, shallow lava into cobblestone.
    void CheckForHarden(World& world, int x, int y, int z) const {
        if (world.GetBlock(x, y, z) != id || material != BlockMaterial::Lava) return;
        bool water = world.GetMaterial(x, y, z - 1) == BlockMaterial::Water || world.GetMaterial(x, y, z + 1) == BlockMaterial::Water ||
                     world.GetMaterial(x - 1, y, z) == BlockMaterial::Water || world.GetMaterial(x + 1, y, z) == BlockMaterial::Water ||
                     world.GetMaterial(x, y + 1, z) == BlockMaterial::Water;
        if (!water) return;
        int meta = world.GetMeta(x, y, z);
        if (meta == 0) world.SetBlock(x, y, z, BLOCK_OBSIDIAN);
        else if (meta <= 4) world.SetBlock(x, y, z, BLOCK_COBBLESTONE);
        else return;
        MixEffects(world, x, y, z);
    }
    static void MixEffects(World& world, int x, int y, int z) {
        JavaRandom& r = world.Random();
        world.PlaySound("random.fizz", x + 0.5, y + 0.5, z + 0.5, 0.5f, 2.6f + (r.NextFloat() - r.NextFloat()) * 0.8f);
        for (int i = 0; i < 8; i++) {
            world.SpawnParticle(ParticleType::LargeSmoke, x + r.NextDouble(), y + 1.2, z + r.NextDouble(), 0, 0, 0);
        }
    }
    void OnAdded(World& world, int x, int y, int z) const override { CheckForHarden(world, x, y, z); }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override { CheckForHarden(world, x, y, z); }

    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (material == BlockMaterial::Water && random.NextInt(64) == 0) {
            int meta = world.GetMeta(x, y, z);
            if (meta > 0 && meta < 8) {
                world.PlaySound("liquid.water", x + 0.5, y + 0.5, z + 0.5, random.NextFloat() * 0.25f + 0.75f, random.NextFloat() + 0.5f);
            }
        }
        if (material == BlockMaterial::Lava && world.GetMaterial(x, y + 1, z) == BlockMaterial::Air && !world.IsOpaqueCube(x, y + 1, z) &&
            random.NextInt(100) == 0) {
            world.SpawnParticle(ParticleType::Lava, x + random.NextFloat(), y + 1.0, z + random.NextFloat(), 0, 0, 0);
        }
    }
};

// ---- BlockFlowing: moving water (8) and lava (10).

class FlowingBlock final : public FluidBlock {
public:
    void OnAdded(World& world, int x, int y, int z) const override {
        FluidBlock::OnAdded(world, x, y, z);
        if (world.GetBlock(x, y, z) == id) world.ScheduleTick(x, y, z, id, tickRate);
    }

    // Doors, signs, ladders and sugar cane hold liquids back, as do solid blocks.
    static bool BlocksFlow(const World& world, int x, int y, int z) {
        int there = world.GetBlock(x, y, z);
        if (there == BLOCK_WOODEN_DOOR || there == BLOCK_IRON_DOOR || there == BLOCK_SIGN_POST || there == BLOCK_LADDER || there == BLOCK_REEDS) return true;
        if (there == BLOCK_AIR) return false;
        return GetBlockDef((uint8_t)there).BlocksMovement();
    }
    bool CanDisplace(const World& world, int x, int y, int z) const {
        BlockMaterial there = world.GetMaterial(x, y, z);
        if (there == material || there == BlockMaterial::Lava) return false;
        return !BlocksFlow(world, x, y, z);
    }
    int SmallestDecay(const World& world, int x, int y, int z, int current, int& sources) const {
        int decay = FlowDecay(world, x, y, z, material);
        if (decay < 0) return current;
        if (decay == 0) sources++;
        if (decay >= 8) decay = 0;
        return (current >= 0 && decay >= current) ? current : decay;
    }
    int FlowCost(const World& world, int x, int y, int z, int distance, int from) const {
        int best = 1000;
        for (int dir = 0; dir < 4; dir++) {
            if ((dir == 0 && from == 1) || (dir == 1 && from == 0) || (dir == 2 && from == 3) || (dir == 3 && from == 2)) continue;
            int nx = x, nz = z;
            if (dir == 0) nx--;
            if (dir == 1) nx++;
            if (dir == 2) nz--;
            if (dir == 3) nz++;
            if (BlocksFlow(world, nx, y, nz) || (world.GetMaterial(nx, y, nz) == material && world.GetMeta(nx, y, nz) == 0)) continue;
            if (!BlocksFlow(world, nx, y - 1, nz)) return distance;
            if (distance >= 4) continue;
            int cost = FlowCost(world, nx, y, nz, distance + 1, dir);
            if (cost < best) best = cost;
        }
        return best;
    }
    // BlockFlowing.getOptimalFlowDirections: toward the nearest drop within 4 blocks.
    void OptimalDirections(const World& world, int x, int y, int z, bool out[4]) const {
        int cost[4];
        for (int dir = 0; dir < 4; dir++) {
            cost[dir] = 1000;
            int nx = x, nz = z;
            if (dir == 0) nx--;
            if (dir == 1) nx++;
            if (dir == 2) nz--;
            if (dir == 3) nz++;
            if (BlocksFlow(world, nx, y, nz) || (world.GetMaterial(nx, y, nz) == material && world.GetMeta(nx, y, nz) == 0)) continue;
            cost[dir] = !BlocksFlow(world, nx, y - 1, nz) ? 0 : FlowCost(world, nx, y, nz, 1, dir);
        }
        int least = *std::min_element(cost, cost + 4);
        for (int dir = 0; dir < 4; dir++) out[dir] = cost[dir] == least;
    }
    void FlowInto(World& world, int x, int y, int z, int level) const {
        if (!CanDisplace(world, x, y, z)) return;
        int there = world.GetBlock(x, y, z);
        if (there > 0) {
            if (material == BlockMaterial::Lava) MixEffects(world, x, y, z);
            else world.DropBlockAsItem(x, y, z, there, world.GetMeta(x, y, z));
        }
        world.SetBlock(x, y, z, id, level);
    }
    void BecomeStill(World& world, int x, int y, int z) const {
        int meta = world.GetMeta(x, y, z);
        world.SetBlock(x, y, z, id + 1, meta, false);
        world.MarkForRender(x, y, z);
    }

    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        int level = FlowDecay(world, x, y, z, material);
        int step = material == BlockMaterial::Lava ? 2 : 1;
        bool settle = true;
        if (level > 0) {
            int sources = 0;
            int smallest = -100;
            smallest = SmallestDecay(world, x - 1, y, z, smallest, sources);
            smallest = SmallestDecay(world, x + 1, y, z, smallest, sources);
            smallest = SmallestDecay(world, x, y, z - 1, smallest, sources);
            smallest = SmallestDecay(world, x, y, z + 1, smallest, sources);
            int next = smallest + step;
            if (next >= 8 || smallest < 0) next = -1;
            int above = FlowDecay(world, x, y + 1, z, material);
            if (above >= 0) next = above >= 8 ? above : above + 8;
            // Two sources side by side over something solid (or more source) make a new source.
            if (sources >= 2 && material == BlockMaterial::Water) {
                if (GetMaterialInfo(world.GetMaterial(x, y - 1, z)).solid) next = 0;
                else if (world.GetMaterial(x, y - 1, z) == material && world.GetMeta(x, y - 1, z) == 0) next = 0;
            }
            if (material == BlockMaterial::Lava && level < 8 && next < 8 && next > level && random.NextInt(4) != 0) {
                next = level;
                settle = false;
            }
            if (next != level) {
                level = next;
                if (level < 0) {
                    world.SetBlock(x, y, z, BLOCK_AIR);
                } else {
                    world.SetMeta(x, y, z, level);
                    world.ScheduleTick(x, y, z, id, tickRate);
                    world.NotifyNeighbors(x, y, z, id);
                }
            } else if (settle) {
                BecomeStill(world, x, y, z);
            }
        } else {
            BecomeStill(world, x, y, z);
        }

        if (CanDisplace(world, x, y - 1, z)) {
            world.SetBlock(x, y - 1, z, id, level >= 8 ? level : level + 8);
        } else if (level >= 0 && (level == 0 || BlocksFlow(world, x, y - 1, z))) {
            bool dirs[4];
            OptimalDirections(world, x, y, z, dirs);
            int spread = level + step;
            if (level >= 8) spread = 1;
            if (spread >= 8) return;
            if (dirs[0]) FlowInto(world, x - 1, y, z, spread);
            if (dirs[1]) FlowInto(world, x + 1, y, z, spread);
            if (dirs[2]) FlowInto(world, x, y, z - 1, spread);
            if (dirs[3]) FlowInto(world, x, y, z + 1, spread);
        }
    }
};

bool Flammable(const World& world, int x, int y, int z);

// ---- BlockStationary: still water (9) and lava (11). Any change nearby turns
// it back into flowing liquid; still lava sets things around it on fire.

class StillBlock final : public FluidBlock {
public:
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        FluidBlock::OnNeighborChanged(world, x, y, z, neighbor);
        if (world.GetBlock(x, y, z) == id) {
            int meta = world.GetMeta(x, y, z);
            world.SetBlock(x, y, z, id - 1, meta, false);
            world.MarkForRender(x, y, z);
            world.ScheduleTick(x, y, z, id - 1, tickRate);
        }
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (material != BlockMaterial::Lava) return;
        int tries = random.NextInt(3);
        for (int i = 0; i < tries; i++) {
            x += random.NextInt(3) - 1;
            y++;
            z += random.NextInt(3) - 1;
            int there = world.GetBlock(x, y, z);
            if (there == BLOCK_AIR) {
                if (Flammable(world, x - 1, y, z) || Flammable(world, x + 1, y, z) || Flammable(world, x, y, z - 1) ||
                    Flammable(world, x, y, z + 1) || Flammable(world, x, y - 1, z) || Flammable(world, x, y + 1, z)) {
                    world.SetBlock(x, y, z, BLOCK_FIRE);
                    return;
                }
            } else if (GetBlockDef((uint8_t)there).BlocksMovement()) {
                return;
            }
        }
    }
};

// ---- BlockFire: burns out, spreads to flammable blocks, and lights TNT.

// How readily fire spreads to each block (chanceToEncourageFire) and burns it
// away (abilityToCatchFire), from BlockFire.initializeBlock.
struct Flammability { int encourage = 0; int burn = 0; };
Flammability GetFlammability(int id) {
    switch (id) {
        case BLOCK_PLANKS: case BLOCK_FENCE: case BLOCK_OAK_STAIRS: case BLOCK_FENCE_GATE: return { 5, 20 };
        case BLOCK_LOG: return { 5, 5 };
        case BLOCK_LEAVES: return { 30, 60 };
        case BLOCK_BOOKSHELF: return { 30, 20 };
        case BLOCK_TNT: return { 15, 100 };
        case BLOCK_TALL_GRASS: return { 60, 100 };
        case BLOCK_WOOL: return { 30, 60 };
        case BLOCK_VINE: return { 15, 100 };
        default: return {};
    }
}

bool Flammable(const World& world, int x, int y, int z) {
    return GetMaterialInfo(world.GetMaterial(x, y, z)).burns;
}

class FireBlock final : public Block {
public:
    FireBlock() {
        SetNotCube(*this, Shape::Fire, Layer::Cutout, 0);
        tickRate = 40;
    }
    bool IsTargetable(int) const override { return false; }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}

    static bool CanCatch(const World& world, int x, int y, int z) {
        return GetFlammability(world.GetBlock(x, y, z)).encourage > 0;
    }
    static bool NeighborCanBurn(const World& world, int x, int y, int z) {
        return CanCatch(world, x + 1, y, z) || CanCatch(world, x - 1, y, z) || CanCatch(world, x, y - 1, z) ||
               CanCatch(world, x, y + 1, z) || CanCatch(world, x, y, z - 1) || CanCatch(world, x, y, z + 1);
    }
    static int NeighborEncouragement(const World& world, int x, int y, int z) {
        if (!world.IsAir(x, y, z)) return 0;
        int best = 0;
        for (const auto& o : SIDE_OFFSET) best = std::max(best, GetFlammability(world.GetBlock(x + o[0], y + o[1], z + o[2])).encourage);
        return best;
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return world.IsNormalCube(x, y - 1, z) || NeighborCanBurn(world, x, y, z);
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetBlock(x, y - 1, z) == BLOCK_OBSIDIAN && TryCreatePortal(world, x, y, z)) return;
        if (!world.IsNormalCube(x, y - 1, z) && !NeighborCanBurn(world, x, y, z)) world.SetBlock(x, y, z, BLOCK_AIR);
        else world.ScheduleTick(x, y, z, id, tickRate);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!world.IsNormalCube(x, y - 1, z) && !NeighborCanBurn(world, x, y, z)) world.SetBlock(x, y, z, BLOCK_AIR);
    }
    void TryBurn(World& world, int x, int y, int z, int chance, JavaRandom& random, int age) const {
        int burn = GetFlammability(world.GetBlock(x, y, z)).burn;
        if (random.NextInt(chance) >= burn) return;
        bool tnt = world.GetBlock(x, y, z) == BLOCK_TNT;
        if (random.NextInt(age + 10) < 5) {
            int newAge = std::min(15, age + random.NextInt(5) / 4);
            world.SetBlock(x, y, z, id, newAge);
        } else {
            world.SetBlock(x, y, z, BLOCK_AIR);
        }
        if (tnt) GetBlockDef(BLOCK_TNT).OnDestroyedByPlayer(world, x, y, z, 1, *world.GetPlayer());
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        bool eternal = world.GetBlock(x, y - 1, z) == BLOCK_NETHERRACK;
        if (!CanPlaceAt(world, x, y, z)) world.SetBlock(x, y, z, BLOCK_AIR);
        int age = world.GetMeta(x, y, z);
        if (age < 15) world.SetMeta(x, y, z, age + random.NextInt(3) / 2, false);
        world.ScheduleTick(x, y, z, id, tickRate);
        if (!eternal && !NeighborCanBurn(world, x, y, z)) {
            if (!world.IsNormalCube(x, y - 1, z) || age > 3) world.SetBlock(x, y, z, BLOCK_AIR);
            return;
        }
        if (!eternal && !CanCatch(world, x, y - 1, z) && age == 15 && random.NextInt(4) == 0) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            return;
        }
        TryBurn(world, x + 1, y, z, 300, random, age);
        TryBurn(world, x - 1, y, z, 300, random, age);
        TryBurn(world, x, y - 1, z, 250, random, age);
        TryBurn(world, x, y + 1, z, 250, random, age);
        TryBurn(world, x, y, z - 1, 300, random, age);
        TryBurn(world, x, y, z + 1, 300, random, age);
        for (int xx = x - 1; xx <= x + 1; xx++)
            for (int zz = z - 1; zz <= z + 1; zz++)
                for (int yy = y - 1; yy <= y + 4; yy++) {
                    if (xx == x && yy == y && zz == z) continue;
                    int chance = 100;
                    if (yy > y + 1) chance += (yy - (y + 1)) * 100;
                    int encourage = NeighborEncouragement(world, xx, yy, zz);
                    if (encourage <= 0) continue;
                    int odds = (encourage + 40) / (age + 30);
                    if (odds > 0 && random.NextInt(chance) <= odds) {
                        world.SetBlock(xx, yy, zz, id, std::min(15, age + random.NextInt(5) / 4));
                    }
                }
    }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (random.NextInt(24) == 0) {
            world.PlaySound("fire.fire", x + 0.5, y + 0.5, z + 0.5, 1.0f + random.NextFloat(), random.NextFloat() * 0.7f + 0.3f);
        }
        if (!world.IsNormalCube(x, y - 1, z) && !CanCatch(world, x, y - 1, z)) {
            // Fire on the side of something: smoke from the burning faces.
            for (int i = 0; i < 2; i++) {
                if (CanCatch(world, x - 1, y, z)) world.SpawnParticle(ParticleType::LargeSmoke, x + random.NextFloat() * 0.1f, y + random.NextFloat(), z + random.NextFloat(), 0, 0, 0);
                if (CanCatch(world, x + 1, y, z)) world.SpawnParticle(ParticleType::LargeSmoke, x + 1 - random.NextFloat() * 0.1f, y + random.NextFloat(), z + random.NextFloat(), 0, 0, 0);
                if (CanCatch(world, x, y, z - 1)) world.SpawnParticle(ParticleType::LargeSmoke, x + random.NextFloat(), y + random.NextFloat(), z + random.NextFloat() * 0.1f, 0, 0, 0);
                if (CanCatch(world, x, y, z + 1)) world.SpawnParticle(ParticleType::LargeSmoke, x + random.NextFloat(), y + random.NextFloat(), z + 1 - random.NextFloat() * 0.1f, 0, 0, 0);
                if (CanCatch(world, x, y + 1, z)) world.SpawnParticle(ParticleType::LargeSmoke, x + random.NextFloat(), y + 1 - random.NextFloat() * 0.1f, z + random.NextFloat(), 0, 0, 0);
            }
        } else {
            for (int i = 0; i < 3; i++) {
                world.SpawnParticle(ParticleType::LargeSmoke, x + random.NextFloat(), y + random.NextFloat() * 0.5f + 0.5f, z + random.NextFloat(), 0, 0, 0);
            }
        }
    }
    void OnPlayerCollided(World&, int, int, int, Player& player) const override {
        player.Hurt(1.0f);
        player.SetFire(300);
    }

    // BlockPortal.tryToCreatePortal: fire inside a 4x5 obsidian frame fills it with portal.
    static bool TryCreatePortal(World& world, int x, int y, int z) {
        int dx = 0, dz = 0;
        if (world.GetBlock(x - 1, y, z) == BLOCK_OBSIDIAN || world.GetBlock(x + 1, y, z) == BLOCK_OBSIDIAN) dx = 1;
        if (world.GetBlock(x, y, z - 1) == BLOCK_OBSIDIAN || world.GetBlock(x, y, z + 1) == BLOCK_OBSIDIAN) dz = 1;
        if (dx == dz) return false;
        if (world.GetBlock(x - dx, y, z - dz) == BLOCK_AIR) {
            x -= dx;
            z -= dz;
        }
        for (int a = -1; a <= 2; a++)
            for (int b = -1; b <= 3; b++) {
                bool frame = a == -1 || a == 2 || b == -1 || b == 3;
                if ((a == -1 || a == 2) && (b == -1 || b == 3)) continue;
                int there = world.GetBlock(x + dx * a, y + b, z + dz * a);
                if (frame) {
                    if (there != BLOCK_OBSIDIAN) return false;
                } else if (there != BLOCK_AIR && there != BLOCK_FIRE) {
                    return false;
                }
            }
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 3; b++) world.SetBlock(x + dx * a, y + b, z + dz * a, BLOCK_PORTAL);
        return true;
    }
};

// ---- BlockPortal: a sheet of swirling purple in an obsidian frame.

class PortalBlock final : public Block {
public:
    PortalBlock() { SetNotCube(*this, Shape::Cube, Layer::Translucent, 0); }
    bool AlongX(const World& world, int x, int y, int z) const {
        return world.GetBlock(x - 1, y, z) == id || world.GetBlock(x + 1, y, z) == id;
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        if (AlongX(world, x, y, z)) return MakeBox(0, 0, 0.375f, 1, 1, 0.625f);
        return MakeBox(0.375f, 0, 0, 0.625f, 1, 1);
    }
    bool IsTargetable(int) const override { return false; }
    void CollisionBoxes(const World&, int, int, int, std::vector<BlockBox>&) const override {}
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>&) const override {}
    bool ShouldSideBeRendered(const World& world, int nx, int ny, int nz, int side) const override {
        if (world.GetBlock(nx, ny, nz) == id) return false;
        return Block::ShouldSideBeRendered(world, nx, ny, nz, side);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        int dx = 0, dz = 1;
        if (AlongX(world, x, y, z)) {
            dx = 1;
            dz = 0;
        }
        int bottom = y;
        while (world.GetBlock(x, bottom - 1, z) == id) bottom--;
        if (world.GetBlock(x, bottom - 1, z) != BLOCK_OBSIDIAN) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            return;
        }
        int height = 1;
        while (height < 4 && world.GetBlock(x, bottom + height, z) == id) height++;
        if (height != 3 || world.GetBlock(x, bottom + height, z) != BLOCK_OBSIDIAN) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            return;
        }
        bool alongX = world.GetBlock(x - 1, y, z) == id || world.GetBlock(x + 1, y, z) == id;
        bool alongZ = world.GetBlock(x, y, z - 1) == id || world.GetBlock(x, y, z + 1) == id;
        if (alongX && alongZ) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            return;
        }
        bool framed = (world.GetBlock(x + dx, y, z + dz) == BLOCK_OBSIDIAN && world.GetBlock(x - dx, y, z - dz) == id) ||
                      (world.GetBlock(x - dx, y, z - dz) == BLOCK_OBSIDIAN && world.GetBlock(x + dx, y, z + dz) == id);
        if (!framed) world.SetBlock(x, y, z, BLOCK_AIR);
    }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (random.NextInt(100) == 0) {
            world.PlaySound("portal.portal", x + 0.5, y + 0.5, z + 0.5, 0.5f, random.NextFloat() * 0.4f + 0.8f);
        }
        for (int i = 0; i < 4; i++) {
            double px = x + random.NextFloat(), py = y + random.NextFloat(), pz = z + random.NextFloat();
            double mx = (random.NextFloat() - 0.5) * 0.5, my = (random.NextFloat() - 0.5) * 0.5, mz = (random.NextFloat() - 0.5) * 0.5;
            int dir = random.NextInt(2) * 2 - 1;
            if (world.GetBlock(x - 1, y, z) == id || world.GetBlock(x + 1, y, z) == id) {
                pz = z + 0.5 + 0.25 * dir;
                mz = random.NextFloat() * 2.0f * dir;
            } else {
                px = x + 0.5 + 0.25 * dir;
                mx = random.NextFloat() * 2.0f * dir;
            }
            world.SpawnParticle(ParticleType::Portal, px, py, pz, mx, my, mz);
        }
    }
    void OnPlayerCollided(World&, int, int, int, Player& player) const override { player.SetInPortal(); }
    int PistonMobility(int) const override { return 2; }
};

} // namespace

// Can fire climb onto this block? (For drawing fire on walls.)
bool IsFlammable(const World& world, int x, int y, int z) { return Flammable(world, x, y, z); }

// ---- Liquid geometry

float FluidHeightPercent(int meta) {
    if (meta >= 8) meta = 0;
    return (meta + 1) / 9.0f;
}

float FluidCornerHeight(const World& world, int x, int y, int z, BlockMaterial material) {
    int weight = 0;
    float total = 0.0f;
    for (int i = 0; i < 4; i++) {
        int cx = x - (i & 1), cz = z - (i >> 1 & 1);
        if (world.GetMaterial(cx, y + 1, cz) == material) return 1.0f;
        BlockMaterial there = world.GetMaterial(cx, y, cz);
        if (there == material) {
            int meta = world.GetMeta(cx, y, cz);
            if (meta >= 8 || meta == 0) {
                total += FluidHeightPercent(meta) * 10.0f;
                weight += 10;
            }
            total += FluidHeightPercent(meta);
            weight++;
        } else if (!GetMaterialInfo(there).solid) {
            total += 1.0f;
            weight++;
        }
    }
    return 1.0f - total / weight;
}

static int EffectiveDecay(const World& world, int x, int y, int z, BlockMaterial material) {
    if (world.GetMaterial(x, y, z) != material) return -1;
    int meta = world.GetMeta(x, y, z);
    return meta >= 8 ? 0 : meta;
}

Vector3 FluidFlowVector(const World& world, int x, int y, int z) {
    BlockMaterial material = world.GetMaterial(x, y, z);
    Vector3 flow = { 0, 0, 0 };
    int decay = EffectiveDecay(world, x, y, z, material);
    for (int dir = 0; dir < 4; dir++) {
        int nx = x, nz = z;
        if (dir == 0) nx--;
        if (dir == 1) nz--;
        if (dir == 2) nx++;
        if (dir == 3) nz++;
        int n = EffectiveDecay(world, nx, y, nz, material);
        if (n < 0) {
            if (!GetMaterialInfo(world.GetMaterial(nx, y, nz)).solid) {
                n = EffectiveDecay(world, nx, y - 1, nz, material);
                if (n >= 0) {
                    int d = n - (decay - 8);
                    flow.x += (nx - x) * d;
                    flow.z += (nz - z) * d;
                }
            }
        } else {
            int d = n - decay;
            flow.x += (nx - x) * d;
            flow.z += (nz - z) * d;
        }
    }
    if (world.GetMeta(x, y, z) >= 8) {
        bool walled = world.IsSolidMaterial(x, y, z - 1) || world.IsSolidMaterial(x, y, z + 1) || world.IsSolidMaterial(x - 1, y, z) ||
                      world.IsSolidMaterial(x + 1, y, z) || world.IsSolidMaterial(x, y + 1, z - 1) || world.IsSolidMaterial(x, y + 1, z + 1) ||
                      world.IsSolidMaterial(x - 1, y + 1, z) || world.IsSolidMaterial(x + 1, y + 1, z);
        if (walled) {
            float len = std::sqrt(flow.x * flow.x + flow.y * flow.y + flow.z * flow.z);
            if (len > 1e-4f) { flow.x /= len; flow.y /= len; flow.z /= len; }
            flow.y -= 6.0f;
        }
    }
    float len = std::sqrt(flow.x * flow.x + flow.y * flow.y + flow.z * flow.z);
    if (len < 1e-4f) return Vector3{ 0, 0, 0 };
    return Vector3{ flow.x / len, flow.y / len, flow.z / len };
}

double FluidFlowAngle(const World& world, int x, int y, int z, BlockMaterial material) {
    Vector3 flow = FluidFlowVector(world, x, y, z);
    if (flow.x == 0.0f && flow.z == 0.0f) return -1000.0;
    return std::atan2(flow.z, flow.x) - 3.14159265358979 / 2.0;
}

bool TryCreatePortal(World& world, int x, int y, int z) { return FireBlock::TryCreatePortal(world, x, y, z); }

void RegisterFluidBlocks(BlockRegistry& r) {
    auto fluid = [&](Block& b, const char* name, int tex, float hardness, BlockMaterial m, int opacity, int light, int rate) {
        Props(b, name, tex, hardness, m, StepSound::None);
        b.lightOpacity = opacity;
        b.lightValue = light;
        b.tickRate = rate;
        b.layer = m == BlockMaterial::Water ? Layer::Translucent : Layer::Opaque;
    };
    fluid(r.Add<FlowingBlock>(BLOCK_FLOWING_WATER), "Water", 205, 100.0f, BlockMaterial::Water, 3, 0, 5);
    fluid(r.Add<StillBlock>(BLOCK_WATER), "Water", 205, 100.0f, BlockMaterial::Water, 3, 0, 5);
    fluid(r.Add<FlowingBlock>(BLOCK_FLOWING_LAVA), "Lava", 237, 0.0f, BlockMaterial::Lava, 255, 15, 30);
    { auto& b = r.Add<StillBlock>(BLOCK_LAVA); fluid(b, "Lava", 237, 100.0f, BlockMaterial::Lava, 255, 15, 30); b.ticksRandomly = true; }
    { auto& b = Props(r.Add<FireBlock>(BLOCK_FIRE), "Fire", 31, 0.0f, BlockMaterial::Fire, StepSound::Wood); b.lightValue = 15; }
    { auto& b = Props(r.Add<PortalBlock>(BLOCK_PORTAL), "Portal", 14, -1.0f, BlockMaterial::Portal, StepSound::Glass); b.lightValue = 11; }
}
