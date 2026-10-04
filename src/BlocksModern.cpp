// The blocks from after Beta 1.8: 1.21's building and colored blocks, made from
// the list in ModernBlocks.h. Most reuse Beta's classes with other textures
// (stairs, fences, doors...); the shapes Beta didn't have are here.

#include "BlocksCommon.h"
#include "ModernBlocks.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace {

constexpr int MODERN_COUNT = (int)(sizeof(MODERN_BLOCKS) / sizeof(MODERN_BLOCKS[0]));
constexpr int TEXTURE_COUNT = (int)(sizeof(MODERN_TEXTURES) / sizeof(MODERN_TEXTURES[0]));

// terrain.png's tile for a 1.21 texture ("block/granite" or just "granite"),
// as ModernTextures packed it: tile LEGACY_TILES + its place in MODERN_TEXTURES.
int Tile(const char* name, const char* folder = "block/") {
    if (name == nullptr) return -1;
    static const std::unordered_map<std::string, int> tiles = [] {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < TEXTURE_COUNT; i++) m[MODERN_TEXTURES[i]] = LEGACY_TILES + i;
        return m;
    }();
    auto it = tiles.find(std::string(folder) + name);
    if (it == tiles.end()) {
        TraceLog(LOG_WARNING, "No terrain tile for texture '%s%s'", folder, name);
        return 1; // stone, rather than garbage
    }
    return it->second;
}

struct Preset {
    BlockMaterial material;
    float hardness;
    float resistance; // 1.21's blast resistance
    StepSound sound;
};

Preset PresetFor(ModernPreset p) {
    switch (p) {
        case ModernPreset::Log: return { BlockMaterial::Wood, 2.0f, 2.0f, StepSound::Wood };
        case ModernPreset::Planks: return { BlockMaterial::Wood, 2.0f, 3.0f, StepSound::Wood };
        case ModernPreset::NetherWood: return { BlockMaterial::Wood, 2.0f, 3.0f, StepSound::Wood };
        case ModernPreset::Stone: return { BlockMaterial::Rock, 1.5f, 6.0f, StepSound::Stone };
        case ModernPreset::Cobble: return { BlockMaterial::Rock, 2.0f, 6.0f, StepSound::Stone };
        case ModernPreset::Deepslate: return { BlockMaterial::Rock, 3.5f, 6.0f, StepSound::Stone };
        case ModernPreset::Reinforced: return { BlockMaterial::Rock, 55.0f, 1200.0f, StepSound::Stone };
        case ModernPreset::Brick: return { BlockMaterial::Rock, 2.0f, 6.0f, StepSound::Stone };
        case ModernPreset::Mud: return { BlockMaterial::Rock, 1.5f, 3.0f, StepSound::Stone };
        case ModernPreset::Sandstone: return { BlockMaterial::Rock, 0.8f, 0.8f, StepSound::Stone };
        case ModernPreset::Lantern: return { BlockMaterial::Glass, 0.3f, 0.3f, StepSound::Glass };
        case ModernPreset::NetherBrick: return { BlockMaterial::Rock, 2.0f, 6.0f, StepSound::Stone };
        case ModernPreset::Basalt: return { BlockMaterial::Rock, 1.25f, 4.2f, StepSound::Stone };
        case ModernPreset::EndStone: return { BlockMaterial::Rock, 3.0f, 9.0f, StepSound::Stone };
        case ModernPreset::Metal: return { BlockMaterial::Iron, 5.0f, 6.0f, StepSound::Metal };
        case ModernPreset::Netherite: return { BlockMaterial::Iron, 50.0f, 1200.0f, StepSound::Metal };
        case ModernPreset::Quartz: return { BlockMaterial::Rock, 0.8f, 0.8f, StepSound::Stone };
        case ModernPreset::Amethyst: return { BlockMaterial::Rock, 1.5f, 1.5f, StepSound::Glass };
        case ModernPreset::Copper: return { BlockMaterial::Iron, 3.0f, 6.0f, StepSound::Metal };
        case ModernPreset::Wool: return { BlockMaterial::Cloth, 0.8f, 0.8f, StepSound::Cloth };
        case ModernPreset::Terracotta: return { BlockMaterial::Rock, 1.25f, 4.2f, StepSound::Stone };
        case ModernPreset::Concrete: return { BlockMaterial::Rock, 1.8f, 1.8f, StepSound::Stone };
        case ModernPreset::Powder: return { BlockMaterial::Sand, 0.5f, 0.5f, StepSound::Sand };
        case ModernPreset::Glass: return { BlockMaterial::Glass, 0.3f, 0.3f, StepSound::Glass };
    }
    return { BlockMaterial::Rock, 1.5f, 6.0f, StepSound::Stone };
}

// ---- Cubes: plain blocks, glass, concrete powder, the block of redstone...

class ModernCube : public Block {
public:
    int top = -1, bottom = -1;
    unsigned flags = 0;
    BlockId hardensInto = 0; // concrete powder: concrete

    int Texture(int side, int meta) const override {
        if (side == SIDE_UP && top >= 0) return top;
        if (side == SIDE_DOWN) return bottom >= 0 ? bottom : (top >= 0 ? top : texture);
        return texture;
    }
    bool ShouldSideBeRendered(const World& world, int nx, int ny, int nz, int side) const override {
        if ((flags & MB_HIDE_SAME) && world.GetBlock(nx, ny, nz) == id) return false;
        return Block::ShouldSideBeRendered(world, nx, ny, nz, side);
    }
    bool CanProvidePower() const override { return (flags & MB_POWER) != 0; }
    bool IsPoweringTo(const World&, int, int, int, int) const override { return (flags & MB_POWER) != 0; }

    // Concrete powder: falls like sand, and sets into concrete touching water.
    bool TouchesWater(const World& world, int x, int y, int z) const {
        for (int side = 1; side < 6; side++) { // not from below, as in 1.21
            if (world.GetMaterial(x + SIDE_OFFSET[side][0], y + SIDE_OFFSET[side][1], z + SIDE_OFFSET[side][2]) == BlockMaterial::Water) {
                return true;
            }
        }
        return false;
    }
    bool Settle(World& world, int x, int y, int z) const {
        if (hardensInto != 0 && TouchesWater(world, x, y, z)) {
            world.SetBlock(x, y, z, hardensInto);
            return true;
        }
        if (flags & MB_FALLING) world.ScheduleTick(x, y, z, id, 3);
        return false;
    }
    void OnAdded(World& world, int x, int y, int z) const override {
        if (flags & MB_FALLING) Settle(world, x, y, z);
    }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (flags & MB_FALLING) Settle(world, x, y, z);
    }
    void OnTick(World& world, int x, int y, int z, JavaRandom&) const override {
        if ((flags & MB_FALLING) == 0 || y <= 0) return;
        BlockId below = world.GetBlock(x, y - 1, z);
        BlockMaterial m = GetBlockDef(below).material;
        if (below == BLOCK_AIR || below == BLOCK_FIRE || m == BlockMaterial::Water || m == BlockMaterial::Lava) {
            world.SetBlock(x, y, z, BLOCK_AIR);
            world.SpawnFallingBlock(x, y, z, id, 0);
        }
    }
};

// ---- Pillars: logs, wood, stems, basalt, quartz and purpur pillars. Metadata
// is the axis they lie along: 0 upright, 1 along X, 2 along Z.

class PillarBlock final : public Block {
public:
    int end = -1;
    static int Axis(int meta) { return meta & 3; }
    static bool EndFace(int side, int meta) {
        if (Axis(meta) == 1) return side == SIDE_WEST || side == SIDE_EAST;
        if (Axis(meta) == 2) return side == SIDE_NORTH || side == SIDE_SOUTH;
        return side == SIDE_UP || side == SIDE_DOWN;
    }
    int Texture(int side, int meta) const override { return EndFace(side, meta) && end >= 0 ? end : texture; }
    int FaceTextureRotation(int side, int meta) const override {
        if (Axis(meta) == 1 && side != SIDE_WEST && side != SIDE_EAST) return 1;
        if (Axis(meta) == 2 && (side == SIDE_WEST || side == SIDE_EAST)) return 1;
        return 0;
    }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player&) const override {
        int axis = (side == SIDE_WEST || side == SIDE_EAST) ? 1 : (side == SIDE_NORTH || side == SIDE_SOUTH) ? 2 : 0;
        world.SetMeta(x, y, z, axis, false);
    }
    int MetaForPlacement(int) const override { return 0; }
};

// ---- Slabs (1.3+): a bottom or top half, or both. Metadata: 0 bottom, 1 top,
// 2 double. They look like the block they're made of.

class ModernSlab final : public Block {
public:
    BlockId model = BLOCK_STONE;
    int modelMeta = 0;
    int sideTexture = -1; // smooth stone slabs have their own sides
    ModernSlab() {
        opaqueCube = false;
        normalCube = false;
        lightOpacity = 255;
    }
    static BlockBox Box(int meta) {
        if (meta == 1) return MakeBox(0, 0.5f, 0, 1, 1, 1);
        if (meta == 2) return BlockBox();
        return MakeBox(0, 0, 0, 1, 0.5f, 1);
    }
    int Texture(int side, int meta) const override {
        if (side > SIDE_UP && sideTexture >= 0) return sideTexture;
        return GetBlockDef(model).Texture(side, modelMeta);
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override { return Box(world.GetMeta(x, y, z)); }
    BlockBox ItemBounds(int) const override { return Box(0); }
    int MetaForPlacement(int) const override { return 0; }
    void OnPlaced(World& world, int x, int y, int z, int side, const Player& player) const override {
        if (PlacedInUpperHalf(player, side)) world.SetMeta(x, y, z, 1, false);
    }
    bool CombineFromItem(World& world, int x, int y, int z, int side) const override {
        int meta = world.GetMeta(x, y, z);
        if ((meta == 0 && side == SIDE_UP) || (meta == 1 && side == SIDE_DOWN)) {
            double mn[3] = { (double)x, (double)y, (double)z }, mx[3] = { x + 1.0, y + 1.0, z + 1.0 };
            if (world.EntitiesInBox(mn, mx, true)) return false; // no room for the other half
            world.SetMeta(x, y, z, 2);
            const char* step = StepSoundEvent(sound);
            if (step != nullptr) world.PlaySound(step, x + 0.5, y + 0.5, z + 0.5, 1.0f, 0.8f);
            return true;
        }
        return false;
    }
    void Drops(World&, int, int, int, int meta, JavaRandom&, std::vector<ItemStack>& out) const override {
        out.push_back(ItemStack(id, meta == 2 ? 2 : 1));
    }
    ItemStack PickItem(const World&, int, int, int) const override { return ItemStack(id, 1); }
};

// ---- Walls (1.4+): posts with arms toward walls, panes, gates and solid blocks,
// 1.5 blocks tall to anything walking into them.

class WallBlock final : public Block {
public:
    BlockId model = BLOCK_COBBLESTONE;
    int modelMeta = 0;
    WallBlock() { SetNotCube(*this, Shape::Wall, Layer::Opaque, 0); }
    int Texture(int side, int meta) const override { return GetBlockDef(model).Texture(side, modelMeta); }
    void Joins(const World& world, int x, int y, int z, bool& n, bool& s, bool& w, bool& e) const {
        n = WallJoins(world.GetBlock(x, y, z - 1));
        s = WallJoins(world.GetBlock(x, y, z + 1));
        w = WallJoins(world.GetBlock(x - 1, y, z));
        e = WallJoins(world.GetBlock(x + 1, y, z));
    }
    BlockBox Bounds(const World& world, int x, int y, int z) const override {
        bool n, s, w, e;
        Joins(world, x, y, z, n, s, w, e);
        return MakeBox(w ? 0 : 0.25f, 0, n ? 0 : 0.25f, e ? 1 : 0.75f, 1, s ? 1 : 0.75f);
    }
    void CollisionBoxes(const World& world, int x, int y, int z, std::vector<BlockBox>& out) const override {
        bool n, s, w, e;
        Joins(world, x, y, z, n, s, w, e);
        out.push_back(MakeBox(0.25f, 0, 0.25f, 0.75f, 1.5f, 0.75f));
        const float a = 0.3125f, b = 0.6875f;
        if (n) out.push_back(MakeBox(a, 0, 0, b, 1.5f, 0.5f));
        if (s) out.push_back(MakeBox(a, 0, 0.5f, b, 1.5f, 1));
        if (w) out.push_back(MakeBox(0, 0, a, 0.5f, 1.5f, b));
        if (e) out.push_back(MakeBox(0.5f, 0, a, 1, 1.5f, b));
    }
};

// ---- Carpet (1.6+): a one-pixel layer that needs something under it.

class CarpetBlock final : public Block {
public:
    CarpetBlock() { SetNotCube(*this, Shape::Cube, Layer::Opaque, 0); }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0, 0, 0, 1, 0.0625f, 1); }
    BlockBox ItemBounds(int) const override { return MakeBox(0, 0, 0, 1, 0.0625f, 1); }
    bool CanPlaceAt(const World& world, int x, int y, int z) const override {
        return Block::CanPlaceAt(world, x, y, z) && world.GetBlock(x, y - 1, z) != BLOCK_AIR;
    }
    bool CanStay(const World& world, int x, int y, int z) const override { return world.GetBlock(x, y - 1, z) != BLOCK_AIR; }
    void OnNeighborChanged(World& world, int x, int y, int z, int) const override {
        if (!CanStay(world, x, y, z)) BreakAndDrop(world, x, y, z);
    }
};

// ---- Glazed terracotta (1.12+): faces the opposite way to the player placing
// it, its pattern turned on each face as 1.21's template_glazed_terracotta
// model has it. Metadata: 0 south, 1 west, 2 north, 3 east (the model's y turn / 90).

class GlazedBlock final : public Block {
public:
    int FaceTextureRotation(int side, int meta) const override {
        int turn = meta & 3; // quarter turns about Y, clockwise from above
        if (side == SIDE_UP) return turn;
        if (side == SIDE_DOWN) return (4 - turn) & 3;
        // The model's face now on `side`: undo the turn.
        static const int ring[4] = { SIDE_SOUTH, SIDE_WEST, SIDE_NORTH, SIDE_EAST }; // clockwise from above
        int at = 0;
        while (ring[at] != side) at++;
        int modelSide = ring[(at - turn + 4) & 3];
        switch (modelSide) {
            case SIDE_NORTH: return 1; // 90
            case SIDE_SOUTH: return 3; // 270
            case SIDE_EAST: return 2;  // 180
            default: return 0;         // west
        }
    }
    void OnPlaced(World& world, int x, int y, int z, int, const Player& player) const override {
        world.SetMeta(x, y, z, (PlayerYawIndex(player) + 2) & 3, false);
    }
    int MetaForPlacement(int) const override { return 0; }
    int DamageDropped(int) const override { return 0; }
};

// ---- Chains (1.16+): a thin hanging strip.

class ChainBlock final : public Block {
public:
    ChainBlock() { SetNotCube(*this, Shape::Chain, Layer::Cutout, 0); }
    BlockBox Bounds(const World&, int, int, int) const override { return MakeBox(0.40625f, 0, 0.40625f, 0.59375f, 1, 0.59375f); }
    BlockBox ItemBounds(int) const override { return MakeBox(0.40625f, 0, 0.40625f, 0.59375f, 1, 0.59375f); }
};

// The block a stairs/slab/wall is made of: a later block, or one of Beta's.
bool FindBlock(const char* name, int& id, int& meta);

void ApplyPreset(Block& b, const ModernBlockSpec& spec) {
    Preset p = PresetFor(spec.preset);
    b.name = spec.display;
    b.material = p.material;
    b.hardness = p.hardness;
    SetResistance(b, p.resistance * 5.0f / 3.0f); // 1.21's resistance in Beta's units
    b.sound = p.sound;
}

} // namespace

bool LookupBlockName(const char* name, int& id, int& meta) {
    static const std::unordered_map<std::string, std::pair<int, int>> names = [] {
        std::unordered_map<std::string, std::pair<int, int>> m;
        for (const BetaEquivalent& b : BETA_EQUIVALENTS) m[b.name] = { b.id, b.meta };
        for (int i = 0; i < MODERN_COUNT; i++) m[MODERN_BLOCKS[i].name] = { FIRST_MODERN_BLOCK + i, 0 };
        return m;
    }();
    auto it = names.find(name);
    if (it == names.end()) return false;
    id = it->second.first;
    meta = it->second.second;
    return true;
}

namespace {

bool FindBlock(const char* name, int& id, int& meta) {
    if (LookupBlockName(name, id, meta) && id < MAX_BLOCK_ID) return true;
    TraceLog(LOG_WARNING, "No block '%s'", name ? name : "(null)");
    id = BLOCK_STONE;
    meta = 0;
    return false;
}

} // namespace

void RegisterModernBlocks(BlockRegistry& r) {
    for (int i = 0; i < MODERN_COUNT; i++) {
        const ModernBlockSpec& spec = MODERN_BLOCKS[i];
        int id = FIRST_MODERN_BLOCK + i;
        int tex = Tile(spec.tex);
        Block* block = nullptr;
        switch (spec.kind) {
            case ModernKind::Cube: {
                auto& b = r.Add<ModernCube>(id);
                b.texture = tex;
                b.top = Tile(spec.top);
                b.bottom = Tile(spec.bottom);
                b.flags = spec.flags;
                if (spec.flags & (MB_CUTOUT | MB_TRANSLUCENT)) {
                    SetNotCube(b, Shape::Cube, (spec.flags & MB_TRANSLUCENT) ? Layer::Translucent : Layer::Cutout, 0);
                }
                if (spec.flags & MB_DARK) b.lightOpacity = 255;
                if (spec.flags & MB_LIGHT) b.lightValue = 15;
                if (spec.flags & MB_FALLING) {
                    int into, meta;
                    if (spec.base != nullptr && FindBlock(spec.base, into, meta)) b.hardensInto = (BlockId)into;
                }
                block = &b;
                break;
            }
            case ModernKind::Pillar: {
                auto& b = r.Add<PillarBlock>(id);
                b.texture = tex;
                b.end = Tile(spec.top);
                block = &b;
                break;
            }
            case ModernKind::Stairs: case ModernKind::Slab: case ModernKind::Wall: {
                int model, modelMeta;
                FindBlock(spec.base, model, modelMeta);
                if (spec.kind == ModernKind::Stairs) {
                    block = &AddStairs(r, id, model, modelMeta);
                } else if (spec.kind == ModernKind::Slab) {
                    auto& b = r.Add<ModernSlab>(id);
                    b.model = (BlockId)model;
                    b.modelMeta = modelMeta;
                    b.sideTexture = Tile(spec.tex);
                    block = &b;
                } else {
                    auto& b = r.Add<WallBlock>(id);
                    b.model = (BlockId)model;
                    b.modelMeta = modelMeta;
                    block = &b;
                }
                // Their looks come from the model at draw time; this is the item icon/particles fallback.
                block->texture = r.blocks[model] ? r.blocks[model]->Texture(SIDE_NORTH, modelMeta) : 1;
                break;
            }
            case ModernKind::Fence: block = &AddFence(r, id, tex); break;
            case ModernKind::FenceGate: block = &AddFenceGate(r, id, tex); break;
            case ModernKind::Door: block = &AddDoor(r, id, tex, Tile(spec.base), Tile(spec.item, "item/"), false); break; // copper too: by hand
            case ModernKind::Trapdoor: block = &AddTrapdoor(r, id, tex, (spec.flags & MB_IRON) != 0); break;
            case ModernKind::PressurePlate: block = &AddPressurePlate(r, id, tex, (spec.flags & MB_ANYTHING) != 0); break;
            case ModernKind::Button: block = &AddButton(r, id, tex, (spec.flags & MB_WOODEN) != 0); break;
            case ModernKind::Pane: {
                block = &AddPane(r, id, tex, Tile(spec.base));
                if (spec.flags & MB_TRANSLUCENT) block->layer = Layer::Translucent;
                break;
            }
            case ModernKind::Carpet: {
                auto& b = r.Add<CarpetBlock>(id);
                b.texture = tex;
                block = &b;
                break;
            }
            case ModernKind::Glazed: {
                auto& b = r.Add<GlazedBlock>(id);
                b.texture = tex;
                block = &b;
                break;
            }
            case ModernKind::Chain: {
                auto& b = r.Add<ChainBlock>(id);
                b.texture = tex;
                block = &b;
                break;
            }
        }
        // AddStairs etc. set the texture; the rest is the preset's.
        int keepTexture = block->texture;
        ApplyPreset(*block, spec);
        block->texture = keepTexture;
        switch (spec.kind) {
            case ModernKind::PressurePlate: case ModernKind::Button: block->hardness = 0.5f; block->resistance = 0.5f; break;
            case ModernKind::Door: case ModernKind::Trapdoor:
                if (block->material == BlockMaterial::Wood) block->hardness = 3.0f;
                break;
            case ModernKind::Carpet: block->hardness = 0.1f; block->resistance = 0.1f; break;
            default: break;
        }
    }
}
