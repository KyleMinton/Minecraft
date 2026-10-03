#include "TileEntity.h"
#include "Items.h"
#include "Player.h"
#include "World.h"

#include <istream>
#include <ostream>
#include <sstream>

void SetFurnaceLit(World& world, int x, int y, int z, bool lit); // BlocksContainers.cpp

namespace {

// Stacks as "count (slot id amount damage)...".
void WriteSlots(std::ostream& out, const ItemStack* items, int n) {
    int used = 0;
    for (int i = 0; i < n; i++) used += items[i].IsEmpty() ? 0 : 1;
    out << used;
    for (int i = 0; i < n; i++) {
        if (items[i].IsEmpty()) continue;
        out << ' ' << i << ' ' << items[i].id << ' ' << items[i].count << ' ' << items[i].damage;
    }
}

void ReadSlots(std::istream& in, ItemStack* items, int n) {
    int used = 0;
    in >> used;
    for (int k = 0; k < used; k++) {
        int slot = -1, id = 0, count = 0, damage = 0;
        if (!(in >> slot >> id >> count >> damage)) return;
        if (slot >= 0 && slot < n && GetItemDef(id) != nullptr && count > 0) items[slot] = ItemStack(id, count, damage);
    }
}

// Sign text can hold spaces; they're stored percent-escaped.
std::string Escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == ' ') out += "%20";
        else if (c == '%') out += "%25";
        else out += c;
    }
    return out.empty() ? "%" : out;
}

std::string Unescape(const std::string& text) {
    if (text == "%") return "";
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '%' && i + 2 < text.size()) {
            std::string code = text.substr(i + 1, 2);
            if (code == "20") { out += ' '; i += 2; continue; }
            if (code == "25") { out += '%'; i += 2; continue; }
        }
        out += text[i];
    }
    return out;
}

} // namespace

// ---- Chest: the lid swings open while someone has it open.

void ChestTile::Tick(World&, int, int, int) {
    prevLidAngle = lidAngle;
    const float speed = 0.1f;
    if ((viewers == 0 && lidAngle > 0.0f) || (viewers > 0 && lidAngle < 1.0f)) {
        lidAngle += viewers > 0 ? speed : -speed;
        if (lidAngle > 1.0f) lidAngle = 1.0f;
        if (lidAngle < 0.0f) lidAngle = 0.0f;
    }
}

void ChestTile::Save(std::ostream& out) const { WriteSlots(out, items, 27); }
void ChestTile::Load(std::istream& in) { ReadSlots(in, items, 27); }

// ---- Furnace (TileEntityFurnace)

bool FurnaceTile::CanSmelt() const {
    if (items[0].IsEmpty()) return false;
    ItemStack result = SmeltingResult(items[0]);
    if (result.IsEmpty()) return false;
    if (items[2].IsEmpty()) return true;
    if (!SameItem(items[2], result)) return false;
    return items[2].count + result.count <= MaxStackSize(items[2]);
}

void FurnaceTile::SmeltItem() {
    if (!CanSmelt()) return;
    ItemStack result = SmeltingResult(items[0]);
    if (items[2].IsEmpty()) items[2] = result;
    else items[2].count += result.count;
    items[0].Split(1);
}

void FurnaceTile::Tick(World& world, int x, int y, int z) {
    bool wasBurning = burnTime > 0;
    if (burnTime > 0) burnTime--;
    if (burnTime == 0 && CanSmelt()) {
        currentItemBurnTime = burnTime = FuelTicks(items[1]);
        if (burnTime > 0 && !items[1].IsEmpty()) items[1].Split(1);
    }
    if (burnTime > 0 && CanSmelt()) {
        if (++cookTime == 200) {
            cookTime = 0;
            SmeltItem();
        }
    } else {
        cookTime = 0;
    }
    if (wasBurning != (burnTime > 0)) SetFurnaceLit(world, x, y, z, burnTime > 0); // replaces this tile; nothing after
}

void FurnaceTile::Save(std::ostream& out) const {
    out << burnTime << ' ' << currentItemBurnTime << ' ' << cookTime << ' ';
    WriteSlots(out, items, 3);
}

void FurnaceTile::Load(std::istream& in) {
    in >> burnTime >> currentItemBurnTime >> cookTime;
    ReadSlots(in, items, 3);
}

// ---- Dispenser

void DispenserTile::Save(std::ostream& out) const { WriteSlots(out, items, 9); }
void DispenserTile::Load(std::istream& in) { ReadSlots(in, items, 9); }

// ---- Sign

void SignTile::Save(std::ostream& out) const {
    for (int i = 0; i < 4; i++) out << (i ? " " : "") << Escape(lines[i]);
}

void SignTile::Load(std::istream& in) {
    for (int i = 0; i < 4; i++) {
        std::string word;
        if (!(in >> word)) return;
        lines[i] = Unescape(word);
    }
}

// ---- Note block, jukebox

void NoteBlockTile::Save(std::ostream& out) const { out << note << ' ' << (previouslyPowered ? 1 : 0); }
void NoteBlockTile::Load(std::istream& in) {
    int powered = 0;
    in >> note >> powered;
    previouslyPowered = powered != 0;
}

void JukeboxTile::Save(std::ostream& out) const { out << record; }
void JukeboxTile::Load(std::istream& in) { in >> record; }

// ---- Mob spawner: no mobs here, but its little flame still spins and smokes.

void MobSpawnerTile::Tick(World& world, int x, int y, int z) {
    Player* player = world.GetPlayer();
    if (player == nullptr) return;
    Vector3 p = player->GetPosition();
    double dx = p.x - (x + 0.5), dy = p.y - (y + 0.5), dz = p.z - (z + 0.5);
    if (dx * dx + dy * dy + dz * dz > 16.0 * 16.0) return;
    JavaRandom& random = world.Random();
    world.SpawnParticle(ParticleType::Smoke, x + random.NextFloat(), y + random.NextFloat(), z + random.NextFloat(), 0, 0, 0);
    world.SpawnParticle(ParticleType::Flame, x + random.NextFloat(), y + random.NextFloat(), z + random.NextFloat(), 0, 0, 0);
    prevRotation = rotation;
    rotation += 1000.0 / (delay + 200.0);
    while (rotation > 360.0) {
        rotation -= 360.0;
        prevRotation -= 360.0;
    }
    if (delay > 0) delay--;
    else delay = 200 + random.NextInt(600);
}

void MobSpawnerTile::Save(std::ostream& out) const { out << delay; }
void MobSpawnerTile::Load(std::istream& in) { in >> delay; }

// ---- Moving piston block (TileEntityPiston): slides over two ticks, then lands.

float PistonTile::Offset(float partial) const {
    if (partial > 1.0f) partial = 1.0f;
    float p = lastProgress + (progress - lastProgress) * partial;
    return extending ? p - 1.0f : 1.0f - p;
}

void PistonTile::Finish(World& world, int x, int y, int z) {
    if (lastProgress >= 1.0f) return;
    int id = storedId, meta = storedMeta;
    lastProgress = progress = 1.0f;
    world.RemoveTileEntity(x, y, z); // destroys this
    if (world.GetBlock(x, y, z) == BLOCK_PISTON_MOVING) world.SetBlock(x, y, z, id, meta);
}

void PistonTile::Tick(World& world, int x, int y, int z) {
    lastProgress = progress;
    if (lastProgress >= 1.0f) {
        int id = storedId, meta = storedMeta;
        world.RemoveTileEntity(x, y, z); // destroys this
        if (world.GetBlock(x, y, z) == BLOCK_PISTON_MOVING) world.SetBlock(x, y, z, id, meta);
        return;
    }
    progress += 0.5f;
    if (progress >= 1.0f) progress = 1.0f;
    if (extending && world.GetPlayer() != nullptr && storedId != BLOCK_AIR) {
        // Push the player out of the way of the block moving into them.
        float move = progress - lastProgress + 0.0625f;
        float offset = Offset(1.0f);
        double min[3] = { x + offset * SIDE_OFFSET[facing][0], y + offset * SIDE_OFFSET[facing][1], z + offset * SIDE_OFFSET[facing][2] };
        double max[3] = { min[0] + 1.0, min[1] + 1.0, min[2] + 1.0 };
        Player& player = *world.GetPlayer();
        if (player.Overlaps(min, max)) {
            player.Push(world, SIDE_OFFSET[facing][0] * move, SIDE_OFFSET[facing][1] * move, SIDE_OFFSET[facing][2] * move);
        }
    }
}

void PistonTile::Save(std::ostream& out) const {
    out << storedId << ' ' << storedMeta << ' ' << facing << ' ' << (extending ? 1 : 0) << ' ' << (isSource ? 1 : 0) << ' ' << progress;
}

void PistonTile::Load(std::istream& in) {
    int ext = 1, src = 0;
    in >> storedId >> storedMeta >> facing >> ext >> src >> progress;
    extending = ext != 0;
    isSource = src != 0;
    lastProgress = progress;
}

std::unique_ptr<TileEntity> CreateTileEntity(TileKind kind) {
    switch (kind) {
        case TileKind::Chest: return std::make_unique<ChestTile>();
        case TileKind::Furnace: return std::make_unique<FurnaceTile>();
        case TileKind::Dispenser: return std::make_unique<DispenserTile>();
        case TileKind::Sign: return std::make_unique<SignTile>();
        case TileKind::NoteBlock: return std::make_unique<NoteBlockTile>();
        case TileKind::Jukebox: return std::make_unique<JukeboxTile>();
        case TileKind::MobSpawner: return std::make_unique<MobSpawnerTile>();
        case TileKind::Piston: return std::make_unique<PistonTile>();
    }
    return nullptr;
}
