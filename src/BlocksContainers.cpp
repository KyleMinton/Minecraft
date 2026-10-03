#include "BlocksCommon.h"

#include <algorithm>
#include "SoundSystem.h"

#include <cmath>

namespace {

// Throws a container's contents out when it's broken (BlockChest.onBlockRemoval).
void SpillContents(World& world, int x, int y, int z) {
    TileEntity* tile = world.GetTileEntity(x, y, z);
    if (tile == nullptr) return;
    JavaRandom& random = world.Random();
    for (int i = 0; i < tile->SlotCount(); i++) {
        ItemStack* stack = tile->Slot(i);
        if (stack == nullptr || stack->IsEmpty()) continue;
        float fx = random.NextFloat() * 0.8f + 0.1f;
        float fy = random.NextFloat() * 0.8f + 0.1f;
        float fz = random.NextFloat() * 0.8f + 0.1f;
        while (stack->count > 0) {
            int amount = random.NextInt(21) + 10;
            ItemStack part = stack->Split(amount);
            const float spread = 0.05f;
            world.SpawnItemAt(x + fx, y + fy, z + fz, part, random.NextGaussian() * spread, random.NextGaussian() * spread + 0.2,
                              random.NextGaussian() * spread, 10);
        }
    }
}

// Furnaces and dispensers face away from the solid blocks around them when
// placed by something other than a player (setDefaultDirection).
int DefaultFacing(const World& world, int x, int y, int z) {
    bool north = world.IsOpaqueCube(x, y, z - 1), south = world.IsOpaqueCube(x, y, z + 1);
    bool west = world.IsOpaqueCube(x - 1, y, z), east = world.IsOpaqueCube(x + 1, y, z);
    int facing = 3;
    if (north && !south) facing = 3;
    if (south && !north) facing = 2;
    if (west && !east) facing = 5;
    if (east && !west) facing = 4;
    return facing;
}

int FacingFromPlayer(const Player& player) {
    static const int facings[4] = { 2, 5, 3, 4 };
    return facings[PlayerYawIndex(player)];
}

// ---- BlockChest: 27 slots; two side by side make a large chest.

class ChestBlock final : public Block {
public:
    ChestBlock() {
        shape = Shape::Chest;
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 0;
    }
    bool HasTileEntity() const override { return true; }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP || side == SIDE_DOWN) return 25;
        return side == SIDE_SOUTH ? 27 : 26;
    }
    static bool HasChestNeighbor(const World& world, int x, int y, int z) {
        if (world.GetBlock(x, y, z) != BLOCK_CHEST) return false;
        return world.GetBlock(x - 1, y, z) == BLOCK_CHEST || world.GetBlock(x + 1, y, z) == BLOCK_CHEST ||
               world.GetBlock(x, y, z - 1) == BLOCK_CHEST || world.GetBlock(x, y, z + 1) == BLOCK_CHEST;
    }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        int neighbors = 0;
        if (world.GetBlock(x - 1, y, z) == id) neighbors++;
        if (world.GetBlock(x + 1, y, z) == id) neighbors++;
        if (world.GetBlock(x, y, z - 1) == id) neighbors++;
        if (world.GetBlock(x, y, z + 1) == id) neighbors++;
        if (neighbors > 1) return false;
        if (HasChestNeighbor(world, x - 1, y, z) || HasChestNeighbor(world, x + 1, y, z) ||
            HasChestNeighbor(world, x, y, z - 1) || HasChestNeighbor(world, x, y, z + 1)) return false;
        return Block::CanPlaceAt(world, x, y, z);
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) world.SetTileEntity(x, y, z, CreateTileEntity(TileKind::Chest));
    }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        int facing = FacingFromPlayer(player);
        // Two chests side by side face the same way.
        int nx = x, nz = z;
        bool paired = false;
        if (world.GetBlock(x - 1, y, z) == id) { nx = x - 1; paired = true; }
        else if (world.GetBlock(x + 1, y, z) == id) { nx = x + 1; paired = true; }
        else if (world.GetBlock(x, y, z - 1) == id) { nz = z - 1; paired = true; }
        else if (world.GetBlock(x, y, z + 1) == id) { nz = z + 1; paired = true; }
        if (paired) {
            bool alongX = nx != x;
            if (alongX && (facing == 4 || facing == 5)) facing = 2;
            if (!alongX && (facing == 2 || facing == 3)) facing = 5;
            world.SetMeta(nx, y, nz, facing);
        }
        world.SetMeta(x, y, z, facing);
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        SpillContents(world, x, y, z);
        world.RemoveTileEntity(x, y, z);
    }
    bool OnActivated(World& world, int x, int y, int z, Player& player) const override {
        if (world.IsNormalCube(x, y + 1, z)) return true;
        if (world.GetBlock(x - 1, y, z) == id && world.IsNormalCube(x - 1, y + 1, z)) return true;
        if (world.GetBlock(x + 1, y, z) == id && world.IsNormalCube(x + 1, y + 1, z)) return true;
        if (world.GetBlock(x, y, z - 1) == id && world.IsNormalCube(x, y + 1, z - 1)) return true;
        if (world.GetBlock(x, y, z + 1) == id && world.IsNormalCube(x, y + 1, z + 1)) return true;
        player.OpenContainer(ScreenKind::Chest, x, y, z);
        return true;
    }
};

// ---- BlockFurnace: lit (62) while burning fuel; swapping keeps its contents.

bool g_keepFurnaceContents = false;

class FurnaceBlock final : public Block {
public:
    bool lit = false;
    bool HasTileEntity() const override { return true; }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP || side == SIDE_DOWN) return 62;
        int front = meta >= 2 ? meta : SIDE_SOUTH; // items face south
        if (side == front) return lit ? 61 : 44;
        return 45;
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) world.SetTileEntity(x, y, z, CreateTileEntity(TileKind::Furnace));
        if (world.GetMeta(x, y, z) < 2) world.SetMeta(x, y, z, DefaultFacing(world, x, y, z), false);
    }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        world.SetMeta(x, y, z, FacingFromPlayer(player));
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        if (g_keepFurnaceContents) return;
        SpillContents(world, x, y, z);
        world.RemoveTileEntity(x, y, z);
    }
    bool OnActivated(World&, int x, int y, int z, Player& player) const override {
        player.OpenContainer(ScreenKind::Furnace, x, y, z);
        return true;
    }
    void Drops(World&, int, int, int, int, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(BLOCK_FURNACE, 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(BLOCK_FURNACE, 1); }
    int DamageDropped(int) const override { return 0; }
    void RandomDisplayTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (!lit) return;
        int meta = world.GetMeta(x, y, z);
        float cx = x + 0.5f, cy = y + random.NextFloat() * 6.0f / 16.0f, cz = z + 0.5f;
        float out = 0.52f, along = random.NextFloat() * 0.6f - 0.3f;
        float px = cx, pz = cz;
        if (meta == 4) { px = cx - out; pz = cz + along; }
        else if (meta == 5) { px = cx + out; pz = cz + along; }
        else if (meta == 2) { px = cx + along; pz = cz - out; }
        else if (meta == 3) { px = cx + along; pz = cz + out; }
        world.SpawnParticle(ParticleType::Smoke, px, cy, pz, 0, 0, 0);
        world.SpawnParticle(ParticleType::Flame, px, cy, pz, 0, 0, 0);
    }
};

// ---- BlockDispenser: shoots out one item from inside when powered.

class DispenserBlock final : public Block {
public:
    DispenserBlock() { tickRate = 4; }
    bool HasTileEntity() const override { return true; }
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP || side == SIDE_DOWN) return 62;
        int front = meta >= 2 ? meta : SIDE_SOUTH;
        return side == front ? 46 : 45;
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) world.SetTileEntity(x, y, z, CreateTileEntity(TileKind::Dispenser));
        if (world.GetMeta(x, y, z) < 2) world.SetMeta(x, y, z, DefaultFacing(world, x, y, z), false);
    }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        world.SetMeta(x, y, z, FacingFromPlayer(player));
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        SpillContents(world, x, y, z);
        world.RemoveTileEntity(x, y, z);
    }
    bool OnActivated(World&, int x, int y, int z, Player& player) const override {
        player.OpenContainer(ScreenKind::Dispenser, x, y, z);
        return true;
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        if (neighbor > 0 && GetBlockDef((uint8_t)neighbor).CanProvidePower()) {
            if (world.IsBlockIndirectlyGettingPowered(x, y, z) || world.IsBlockIndirectlyGettingPowered(x, y + 1, z)) {
                world.ScheduleTick(x, y, z, id, tickRate);
            }
        }
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom& random) const override {
        if (world.IsBlockIndirectlyGettingPowered(x, y, z) || world.IsBlockIndirectlyGettingPowered(x, y + 1, z)) Dispense(world, x, y, z, random);
    }
    void Dispense(World& world, int x, int y, int z, JavaRandom& random) const {
        int meta = world.GetMeta(x, y, z);
        int dx = 0, dz = 0;
        if (meta == 3) dz = 1;
        else if (meta == 2) dz = -1;
        else if (meta == 5) dx = 1;
        else dx = -1;
        TileEntity* tile = world.GetTileEntity(x, y, z);
        if (tile == nullptr) return;
        // A random non-empty slot.
        int chosen = -1, seen = 0;
        for (int i = 0; i < tile->SlotCount(); i++) {
            if (!tile->Slot(i)->IsEmpty() && random.NextInt(++seen) == 0) chosen = i;
        }
        double px = x + dx * 0.6 + 0.5, py = y + 0.5, pz = z + dz * 0.6 + 0.5;
        if (chosen < 0) {
            world.PlaySound("random.click", x, y, z, 1.0f, 1.2f);
            return;
        }
        ItemStack shot = tile->Slot(chosen)->Split(1);
        double speed = random.NextDouble() * 0.1 + 0.2;
        double mx = dx * speed + random.NextGaussian() * 0.045;
        double my = 0.2 + random.NextGaussian() * 0.045;
        double mz = dz * speed + random.NextGaussian() * 0.045;
        world.SpawnItemAt(px, py - 0.3, pz, shot, mx, my, mz, 10);
        bool projectile = shot.id == ITEM_ARROW || shot.id == ITEM_SNOWBALL || shot.id == ITEM_EGG;
        world.PlaySound(projectile ? "random.bow" : "random.click", x, y, z, 1.0f, projectile ? 1.2f : 1.0f);
        for (int i = 0; i < 10; i++) {
            double r = random.NextDouble() * 0.2 + 0.01;
            double sx = px + dx * 0.01 + (random.NextDouble() - 0.5) * dz * 0.5;
            double sy = py + (random.NextDouble() - 0.5) * 0.5;
            double sz = pz + dz * 0.01 + (random.NextDouble() - 0.5) * dx * 0.5;
            world.SpawnParticle(ParticleType::Smoke, sx, sy, sz, dx * r + random.NextGaussian() * 0.01,
                                -0.03 + random.NextGaussian() * 0.01, dz * r + random.NextGaussian() * 0.01);
        }
    }
};

// ---- Crafting table: 3x3 crafting.

class CraftingTableBlock final : public Block {
public:
    int Texture(int side, int meta) const override {
        if (side == SIDE_UP) return 43;
        if (side == SIDE_DOWN) return 4;
        return (side == SIDE_NORTH || side == SIDE_WEST) ? 60 : 59;
    }
    bool OnActivated(World&, int x, int y, int z, Player& player) const override {
        player.OpenContainer(ScreenKind::Crafting, x, y, z);
        return true;
    }
};

// ---- BlockJukebox: plays the record put in it; right click takes it out.

class JukeboxBlock final : public Block {
public:
    bool HasTileEntity() const override { return true; }
    int Texture(int side, int meta) const override { return side == SIDE_UP ? 75 : 74; }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) world.SetTileEntity(x, y, z, CreateTileEntity(TileKind::Jukebox));
    }
    static void Eject(World& world, int x, int y, int z) {
        TileEntity* te = world.GetTileEntity(x, y, z);
        if (te == nullptr || te->kind != TileKind::Jukebox) return;
        JukeboxTile* box = static_cast<JukeboxTile*>(te);
        if (box->record == 0) return;
        if (world.GetSounds() != nullptr) world.GetSounds()->StopRecord(x, y, z);
        int record = box->record;
        box->record = 0;
        world.SetMeta(x, y, z, 0);
        JavaRandom& random = world.Random();
        const float f = 0.7f;
        world.SpawnItemAt(x + random.NextFloat() * f + (1 - f) * 0.5, y + random.NextFloat() * f + (1 - f) * 0.2 + 0.6,
                          z + random.NextFloat() * f + (1 - f) * 0.5, ItemStack(record, 1), 0, 0.2, 0, 10);
    }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        if (world.GetMeta(x, y, z) == 0) return false;
        Eject(world, x, y, z);
        return true;
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override {
        Eject(world, x, y, z);
        world.RemoveTileEntity(x, y, z);
    }
};

// ---- BlockNote: plays a note (pitch 0-24, right click to change) when powered
// or hit; the block under it picks the instrument.

class NoteBlockBlock final : public Block {
public:
    bool HasTileEntity() const override { return true; }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (world.GetTileEntity(x, y, z) == nullptr) world.SetTileEntity(x, y, z, CreateTileEntity(TileKind::NoteBlock));
    }
    void OnRemoved(World& world, int x, int y, int z, int) const override { world.RemoveTileEntity(x, y, z); }
    static NoteBlockTile* Tile(World& world, int x, int y, int z) {
        TileEntity* te = world.GetTileEntity(x, y, z);
        return te != nullptr && te->kind == TileKind::NoteBlock ? static_cast<NoteBlockTile*>(te) : nullptr;
    }
    static void Trigger(World& world, int x, int y, int z) {
        NoteBlockTile* tile = Tile(world, x, y, z);
        if (tile == nullptr || world.GetMaterial(x, y + 1, z) != BlockMaterial::Air) return;
        BlockMaterial below = world.GetMaterial(x, y - 1, z);
        int instrument = 0;
        if (below == BlockMaterial::Rock) instrument = 1;
        if (below == BlockMaterial::Sand) instrument = 2;
        if (below == BlockMaterial::Glass) instrument = 3;
        if (below == BlockMaterial::Wood) instrument = 4;
        world.BlockEvent(x, y, z, instrument, tile->note);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int neighbor) const override {
        if (neighbor <= 0 || !GetBlockDef((uint8_t)neighbor).CanProvidePower()) return;
        bool powered = world.IsBlockGettingPowered(x, y, z);
        NoteBlockTile* tile = Tile(world, x, y, z);
        if (tile == nullptr || tile->previouslyPowered == powered) return;
        if (powered) Trigger(world, x, y, z);
        tile->previouslyPowered = powered;
    }
    bool OnActivated(World& world, int x, int y, int z, Player&) const override {
        NoteBlockTile* tile = Tile(world, x, y, z);
        if (tile == nullptr) return true;
        tile->note = (tile->note + 1) % 25;
        Trigger(world, x, y, z);
        return true;
    }
    void OnClicked(World& world, int x, int y, int z, Player&) const override { Trigger(world, x, y, z); }
    void OnBlockEvent(World& world, int x, int y, int z, int instrument, int note) const override {
        static const char* names[5] = { "note.harp", "note.bd", "note.snare", "note.hat", "note.bassattack" };
        float pitch = (float)std::pow(2.0, (note - 12) / 12.0);
        world.PlaySound(names[std::clamp(instrument, 0, 4)], x + 0.5, y + 0.5, z + 0.5, 3.0f, pitch);
        world.SpawnParticle(ParticleType::Note, x + 0.5, y + 1.2, z + 0.5, note / 24.0, 0, 0);
    }
};

} // namespace

// Swaps a furnace between lit and unlit, keeping what's inside (BlockFurnace.updateFurnaceBlockState).
void SetFurnaceLit(World& world, int x, int y, int z, bool lit) {
    int meta = world.GetMeta(x, y, z);
    std::unique_ptr<TileEntity> keep;
    TileEntity* tile = world.GetTileEntity(x, y, z);
    if (tile != nullptr) {
        auto copy = std::make_unique<FurnaceTile>(*static_cast<FurnaceTile*>(tile));
        keep = std::move(copy);
    }
    g_keepFurnaceContents = true;
    world.SetBlock(x, y, z, lit ? BLOCK_LIT_FURNACE : BLOCK_FURNACE, meta);
    g_keepFurnaceContents = false;
    if (keep) world.SetTileEntity(x, y, z, std::move(keep));
}

void RegisterContainerBlocks(BlockRegistry& r) {
    Props(r.Add<ChestBlock>(BLOCK_CHEST), "Chest", 26, 2.5f, BlockMaterial::Wood, StepSound::Wood);
    { auto& b = Props(r.Add<FurnaceBlock>(BLOCK_FURNACE), "Furnace", 45, 3.5f, BlockMaterial::Rock, StepSound::Stone); b.lit = false; }
    { auto& b = Props(r.Add<FurnaceBlock>(BLOCK_LIT_FURNACE), "Furnace", 45, 3.5f, BlockMaterial::Rock, StepSound::Stone); b.lit = true; b.lightValue = 13; }
    Props(r.Add<DispenserBlock>(BLOCK_DISPENSER), "Dispenser", 45, 3.5f, BlockMaterial::Rock, StepSound::Stone);
    Props(r.Add<CraftingTableBlock>(BLOCK_CRAFTING_TABLE), "Crafting Table", 59, 2.5f, BlockMaterial::Wood, StepSound::Wood);
    { auto& b = Props(r.Add<JukeboxBlock>(BLOCK_JUKEBOX), "Jukebox", 74, 2.0f, BlockMaterial::Wood, StepSound::Stone); SetResistance(b, 10); }
    Props(r.Add<NoteBlockBlock>(BLOCK_NOTE_BLOCK), "Note Block", 211, 0.8f, BlockMaterial::Wood, StepSound::Wood);
}
