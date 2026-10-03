#pragma once

#include "ItemStack.h"

#include <iosfwd>
#include <memory>
#include <string>

class World;

// Extra data a block keeps beyond its ID and metadata (Minecraft's TileEntity):
// container contents, sign text, a piston's moving block, and so on.
enum class TileKind { Chest, Furnace, Dispenser, Sign, NoteBlock, Jukebox, MobSpawner, Piston };

class TileEntity {
public:
    explicit TileEntity(TileKind k) : kind(k) {}
    virtual ~TileEntity() = default;

    TileKind kind;

    // Once per tick while its chunk is loaded.
    virtual void Tick(World& world, int x, int y, int z) {}
    // "key value..." lines after the header World writes.
    virtual void Save(std::ostream& out) const = 0;
    virtual void Load(std::istream& in) = 0;

    // Containers (chest, furnace, dispenser) expose their slots.
    virtual int SlotCount() const { return 0; }
    virtual ItemStack* Slot(int index) { return nullptr; }
};

struct ChestTile : TileEntity {
    ItemStack items[27];
    int viewers = 0;                 // players with it open (just us)
    float lidAngle = 0.0f, prevLidAngle = 0.0f;

    ChestTile() : TileEntity(TileKind::Chest) {}
    void Tick(World& world, int x, int y, int z) override;
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
    int SlotCount() const override { return 27; }
    ItemStack* Slot(int index) override { return index >= 0 && index < 27 ? &items[index] : nullptr; }
};

struct FurnaceTile : TileEntity {
    ItemStack items[3];              // 0 input, 1 fuel, 2 output
    int burnTime = 0;                // ticks of fuel left
    int currentItemBurnTime = 0;     // how long the current fuel lasts in total
    int cookTime = 0;                // 0..200

    FurnaceTile() : TileEntity(TileKind::Furnace) {}
    void Tick(World& world, int x, int y, int z) override;
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
    int SlotCount() const override { return 3; }
    ItemStack* Slot(int index) override { return index >= 0 && index < 3 ? &items[index] : nullptr; }
    bool CanSmelt() const;
    void SmeltItem();
};

struct DispenserTile : TileEntity {
    ItemStack items[9];

    DispenserTile() : TileEntity(TileKind::Dispenser) {}
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
    int SlotCount() const override { return 9; }
    ItemStack* Slot(int index) override { return index >= 0 && index < 9 ? &items[index] : nullptr; }
};

struct SignTile : TileEntity {
    std::string lines[4];

    SignTile() : TileEntity(TileKind::Sign) {}
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
};

struct NoteBlockTile : TileEntity {
    int note = 0;                    // 0-24
    bool previouslyPowered = false;

    NoteBlockTile() : TileEntity(TileKind::NoteBlock) {}
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
};

struct JukeboxTile : TileEntity {
    int record = 0;                  // item ID of the disc inside, 0 = empty

    JukeboxTile() : TileEntity(TileKind::Jukebox) {}
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
};

struct MobSpawnerTile : TileEntity {
    int delay = 20;
    double rotation = 0.0, prevRotation = 0.0;

    MobSpawnerTile() : TileEntity(TileKind::MobSpawner) {}
    void Tick(World& world, int x, int y, int z) override;
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
};

// A block being pushed or pulled by a piston (TileEntityPiston): drawn sliding
// between its old and new spot over two ticks, then put down for real.
struct PistonTile : TileEntity {
    int storedId = 0, storedMeta = 0;
    int facing = 0;                  // the direction it's moving
    bool extending = true;
    bool isSource = false;           // the piston's own head, not a pushed block
    float progress = 0.0f, lastProgress = 0.0f;

    PistonTile() : TileEntity(TileKind::Piston) {}
    void Tick(World& world, int x, int y, int z) override;
    void Save(std::ostream& out) const override;
    void Load(std::istream& in) override;
    // Where the block is drawn, as an offset from this cell, at `partial` into the tick.
    float Offset(float partial) const;
    void Finish(World& world, int x, int y, int z);
};

std::unique_ptr<TileEntity> CreateTileEntity(TileKind kind);
