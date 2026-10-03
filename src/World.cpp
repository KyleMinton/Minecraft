#include "World.h"
#include "BlockParticles.h"
#include "BlockRender.h"
#include "DroppedItems.h"
#include "Entities.h"
#include "Light.h"
#include "Player.h"
#include "SoundSystem.h"

#include "raymath.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>
#include <sstream>
#include <tuple>

namespace {

constexpr double MESH_BUDGET_SECONDS = 0.004; // per frame; at least one mesh is always built
constexpr double TICK_SECONDS = 0.05;
constexpr int MAX_TICKS_PER_FRAME = 5;
constexpr int RANDOM_TICKS_PER_CHUNK = 10;    // Beta's 20 per 128-tall chunk, for our 64-tall ones
constexpr char EDITS_MAGIC[4] = { 'B', 'E', 'D', '1' };

int EditIndex(int x, int y, int z) {
    return x + Chunk::SIZE * (z + Chunk::SIZE * y);
}

int ChunkDistance(int cx, int cz, int pcx, int pcz) {
    return std::max(abs(cx - pcx), abs(cz - pcz));
}

int LocalCoord(int world) {
    return world - World::ChunkCoord(world) * Chunk::SIZE;
}

int SignExtend26(int64_t v) {
    return (int32_t)((uint32_t)(v & 0x3FFFFFF) << 6) >> 6;
}

// Block IDs of this game before it had every Beta block.
uint8_t LegacyBlock(uint8_t old) {
    static const uint8_t map[18] = {
        BLOCK_AIR, BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_SAND, BLOCK_WATER, BLOCK_LOG, BLOCK_LEAVES, BLOCK_COBBLESTONE,
        BLOCK_GRAVEL, BLOCK_BEDROCK, BLOCK_COAL_ORE, BLOCK_IRON_ORE, BLOCK_GOLD_ORE, BLOCK_DIAMOND_ORE, BLOCK_SANDSTONE,
        BLOCK_PLANKS, BLOCK_BRICKS,
    };
    return old < 18 ? map[old] : BLOCK_AIR;
}

} // namespace

World::World(int64_t worldSeed)
    : seed(worldSeed), random(worldSeed), randomTickLcg((int)(worldSeed * 3 + 1013904223)), atlasTexture{ 0 }, hasTexture(false),
      hasStreamed(false), lastPlayerChunkX(0), lastPlayerChunkZ(0) {
    chunkShader = LoadShader("resources/shaders/chunk.vs", "resources/shaders/chunk.fs");
    float tileSize = 1.0f / ATLAS_TILES;
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "tileSize"), &tileSize, SHADER_UNIFORM_FLOAT);
    ambientCountdown = random.NextInt(12000);

    // Leave a core free for the main thread.
    int workerCount = std::clamp((int)std::thread::hardware_concurrency() - 1, 1, 4);
    for (int i = 0; i < workerCount; i++) workers.emplace_back(&World::WorkerLoop, this);
}

World::~World() {
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        stopping = true;
    }
    jobReady.notify_all();
    for (std::thread& worker : workers) worker.join();
    for (auto& [key, loaded] : chunks) UnloadChunkModels(loaded);
    UnloadShader(chunkShader);
}

void World::SetServices(SoundSystem* s, DroppedItems* d, BlockParticles* p, Entities* e, Player* pl) {
    sounds = s;
    drops = d;
    particles = p;
    entities = e;
    player = pl;
}

void World::SetTexture(Texture2D texture, bool enabled) {
    atlasTexture = texture;
    hasTexture = enabled;
}

int64_t World::Key(int cx, int cz) {
    return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;
}

int64_t World::TileKey(int x, int y, int z) {
    return ((int64_t)(x & 0x3FFFFFF) << 38) | ((int64_t)(z & 0x3FFFFFF) << 12) | (int64_t)(y & 0xFFF);
}
int World::TileX(int64_t key) { return SignExtend26(key >> 38); }
int World::TileZ(int64_t key) { return SignExtend26(key >> 12); }
int World::TileY(int64_t key) { return (int)(key & 0xFFF); }

uint64_t World::TickKey(int x, int y, int z, int id) {
    return ((uint64_t)TileKey(x, y, z) * 31u) ^ ((uint64_t)id << 56);
}

// Floor division, so negative world coordinates map to the right chunk.
int World::ChunkCoord(int worldCoord) {
    return (worldCoord >= 0) ? (worldCoord / Chunk::SIZE) : ((worldCoord - Chunk::SIZE + 1) / Chunk::SIZE);
}

// ---- Saving

// edits.dat: "BED1", then for each edited chunk its X and Z (int32), how many
// edits (int32), and each edit's block index (uint16), block ID and metadata (uint8 each).
bool World::SaveEdits(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(EDITS_MAGIC, 4);
    auto write32 = [&](int32_t v) { out.write((const char*)&v, 4); };
    for (const auto& [key, chunkEdits] : edits) {
        write32((int32_t)(key >> 32));
        write32((int32_t)(uint32_t)key);
        write32((int32_t)chunkEdits.size());
        for (const auto& [index, value] : chunkEdits) {
            uint16_t i = (uint16_t)index;
            uint8_t id = (uint8_t)(value & 0xFF), meta = (uint8_t)(value >> 8);
            out.write((const char*)&i, 2);
            out.write((const char*)&id, 1);
            out.write((const char*)&meta, 1);
        }
    }
    return (bool)out;
}

bool World::LoadEdits(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[4] = {};
    if (!in.read(magic, 4) || memcmp(magic, EDITS_MAGIC, 4) != 0) {
        in.close();
        return LoadLegacyEdits(path);
    }
    int32_t header[3];
    while (in.read((char*)header, sizeof(header))) {
        auto& chunkEdits = edits[Key(header[0], header[1])];
        for (int32_t n = 0; n < header[2]; n++) {
            uint16_t index;
            uint8_t id, meta;
            if (!in.read((char*)&index, 2) || !in.read((char*)&id, 1) || !in.read((char*)&meta, 1)) return false;
            if (index < Chunk::SIZE * Chunk::SIZE * Chunk::HEIGHT && id < BLOCK_COUNT) chunkEdits[index] = (uint16_t)(id | (meta & 15) << 8);
        }
    }
    return true;
}

// The old format: index (uint16) and this game's old block numbering (uint8).
bool World::LoadLegacyEdits(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    int32_t header[3];
    while (in.read((char*)header, sizeof(header))) {
        auto& chunkEdits = edits[Key(header[0], header[1])];
        for (int32_t n = 0; n < header[2]; n++) {
            uint16_t index;
            uint8_t type;
            if (!in.read((char*)&index, 2) || !in.read((char*)&type, 1)) return false;
            if (index < Chunk::SIZE * Chunk::SIZE * Chunk::HEIGHT) chunkEdits[index] = LegacyBlock(type);
        }
    }
    return true;
}

namespace {

const char* TileKindName(TileKind kind) {
    switch (kind) {
        case TileKind::Chest: return "chest";
        case TileKind::Furnace: return "furnace";
        case TileKind::Dispenser: return "dispenser";
        case TileKind::Sign: return "sign";
        case TileKind::NoteBlock: return "note";
        case TileKind::Jukebox: return "jukebox";
        case TileKind::MobSpawner: return "spawner";
        case TileKind::Piston: return "piston";
    }
    return "?";
}

bool TileKindFromName(const std::string& name, TileKind& kind) {
    const TileKind all[] = { TileKind::Chest, TileKind::Furnace, TileKind::Dispenser, TileKind::Sign,
                             TileKind::NoteBlock, TileKind::Jukebox, TileKind::MobSpawner, TileKind::Piston };
    for (TileKind k : all) {
        if (name == TileKindName(k)) { kind = k; return true; }
    }
    return false;
}

} // namespace

// tiles.dat: "populated <chunkX> <chunkZ>" lines for chunks whose dungeon
// chests have been made, "tile <x> <y> <z> <kind> <data...>" for each tile
// entity, and "tick <x> <y> <z> <id> <delay>" for pending block updates.
void World::SaveTileEntities(std::ostream& out) const {
    for (int64_t key : tilesPopulated) out << "populated " << (int32_t)(key >> 32) << ' ' << (int32_t)(uint32_t)key << '\n';
    for (const auto& [key, tile] : tileEntities) {
        out << "tile " << TileX(key) << ' ' << TileY(key) << ' ' << TileZ(key) << ' ' << TileKindName(tile->kind) << ' ';
        tile->Save(out);
        out << '\n';
    }
    for (const ScheduledTick& t : scheduled) {
        out << "tick " << t.x << ' ' << t.y << ' ' << t.z << ' ' << t.id << ' ' << std::max(0LL, t.time - tickCount) << '\n';
    }
}

void World::LoadTileEntities(std::istream& in) {
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string what;
        row >> what;
        if (what == "populated") {
            int cx, cz;
            if (row >> cx >> cz) tilesPopulated.insert(Key(cx, cz));
        } else if (what == "tile") {
            int x, y, z;
            std::string kindName;
            TileKind kind;
            if (!(row >> x >> y >> z >> kindName) || !TileKindFromName(kindName, kind)) continue;
            std::unique_ptr<TileEntity> tile = CreateTileEntity(kind);
            tile->Load(row);
            tileEntities[TileKey(x, y, z)] = std::move(tile);
        } else if (what == "tick") {
            int x, y, z, id;
            long long delay;
            if (row >> x >> y >> z >> id >> delay) ScheduleTick(x, y, z, id, (int)delay);
        }
    }
}

// ---- Streaming

// Worker thread: builds Chunk terrain, which only touches the chunk itself.
void World::WorkerLoop() {
    while (true) {
        std::pair<int, int> job;
        {
            std::unique_lock<std::mutex> lock(jobMutex);
            jobReady.wait(lock, [this] { return stopping || !jobs.empty(); });
            if (stopping) return;
            job = jobs.front();
            jobs.pop_front();
        }
        auto chunk = std::make_unique<Chunk>(job.first, job.second, seed);
        std::lock_guard<std::mutex> lock(jobMutex);
        finished.push_back(std::move(chunk));
    }
}

// Queues every missing chunk in data range, nearest first. Jobs still waiting
// from an older position are dropped and re-queued in the new order.
void World::RequestChunks(int pcx, int pcz) {
    std::vector<std::pair<int, int>> wanted;
    for (int dx = -DataDistance(); dx <= DataDistance(); dx++) {
        for (int dz = -DataDistance(); dz <= DataDistance(); dz++) wanted.emplace_back(pcx + dx, pcz + dz);
    }
    std::sort(wanted.begin(), wanted.end(), [&](const auto& a, const auto& b) {
        int da = (a.first - pcx) * (a.first - pcx) + (a.second - pcz) * (a.second - pcz);
        int db = (b.first - pcx) * (b.first - pcx) + (b.second - pcz) * (b.second - pcz);
        return da < db;
    });
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        for (const auto& [cx, cz] : jobs) requested.erase(Key(cx, cz));
        jobs.clear();
        for (const auto& [cx, cz] : wanted) {
            int64_t key = Key(cx, cz);
            if (chunks.count(key) || requested.count(key)) continue;
            jobs.emplace_back(cx, cz);
            requested.insert(key);
        }
    }
    jobReady.notify_all();
}

void World::UnloadChunkModels(LoadedChunk& loaded) {
    if (loaded.hasOpaque) UnloadModel(loaded.opaque);
    if (loaded.hasTranslucent) UnloadModel(loaded.translucent);
    loaded.hasOpaque = loaded.hasTranslucent = loaded.hasModel = false;
}

void World::UnloadFarChunks(int pcx, int pcz) {
    for (auto it = chunks.begin(); it != chunks.end();) {
        int dist = ChunkDistance(it->second.chunk->GetChunkX(), it->second.chunk->GetChunkZ(), pcx, pcz);
        if (it->second.hasModel && dist > ModelUnloadDistance()) UnloadChunkModels(it->second);
        if (dist > DataUnloadDistance()) {
            UnloadChunkModels(it->second);
            dirtyChunks.erase(it->first);
            it = chunks.erase(it);
            cachedKey = INT64_MIN;
            cachedChunk = nullptr;
        } else {
            ++it;
        }
    }
}

// Moves finished terrain from the workers into the world.
void World::IntegrateFinished(int pcx, int pcz) {
    std::vector<std::unique_ptr<Chunk>> ready;
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        ready.swap(finished);
    }

    for (std::unique_ptr<Chunk>& chunk : ready) {
        int cx = chunk->GetChunkX();
        int cz = chunk->GetChunkZ();
        int64_t key = Key(cx, cz);
        requested.erase(key);

        // The player may have moved on while this was being generated.
        if (ChunkDistance(cx, cz, pcx, pcz) > DataUnloadDistance() || chunks.count(key)) continue;

        // Re-apply anything changed here before this chunk was last unloaded.
        auto chunkEdits = edits.find(key);
        if (chunkEdits != edits.end()) {
            for (const auto& [index, value] : chunkEdits->second) {
                int x = index % Chunk::SIZE;
                int z = (index / Chunk::SIZE) % Chunk::SIZE;
                int y = index / (Chunk::SIZE * Chunk::SIZE);
                chunk->SetBlock(x, y, z, (uint8_t)(value & 0xFF), value >> 8);
            }
            chunk->ComputeLight(); // the edits change its shadows
        }

        // Dungeon chests and spawners, the first time this chunk is seen.
        if (!tilesPopulated.count(key)) {
            tilesPopulated.insert(key);
            for (const GeneratedTile& g : chunk->generatedTiles) {
                if (chunk->GetBlock(g.x, g.y, g.z) == BLOCK_AIR) continue;
                int wx = cx * Chunk::SIZE + g.x, wz = cz * Chunk::SIZE + g.z;
                if (tileEntities.count(TileKey(wx, g.y, wz))) continue;
                std::unique_ptr<TileEntity> tile = CreateTileEntity(g.kind);
                for (const auto& [slot, stack] : g.items) {
                    if (slot >= 0 && slot < tile->SlotCount()) *tile->Slot(slot) = stack;
                }
                tileEntities[TileKey(wx, g.y, wz)] = std::move(tile);
            }
        }
        chunk->generatedTiles.clear();

        chunks[key].chunk = std::move(chunk);
        cachedKey = INT64_MIN;
        cachedChunk = nullptr;
        LightAcrossBorders(cx, cz);
    }
}

Chunk* World::ChunkAt(int worldX, int worldZ) const {
    int64_t key = Key(ChunkCoord(worldX), ChunkCoord(worldZ));
    if (key == cachedKey) return cachedChunk;
    auto it = chunks.find(key);
    Chunk* chunk = it == chunks.end() ? nullptr : it->second.chunk.get();
    cachedKey = key;
    cachedChunk = chunk;
    return chunk;
}

// A chunk's faces and AO look one block into all 8 surrounding chunks.
bool World::HasAllNeighbors(int cx, int cz) const {
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            if (!chunks.count(Key(cx + dx, cz + dz))) return false;
        }
    }
    return true;
}

// Builds meshes for in-range chunks that are ready, nearest first, until the
// time budget runs out.
void World::BuildModels(int pcx, int pcz, double budgetSeconds) {
    std::vector<std::pair<int, LoadedChunk*>> candidates;
    for (auto& [key, loaded] : chunks) {
        if (loaded.hasModel) continue;
        int cx = loaded.chunk->GetChunkX();
        int cz = loaded.chunk->GetChunkZ();
        if (ChunkDistance(cx, cz, pcx, pcz) > RenderDistance() || !HasAllNeighbors(cx, cz)) continue;
        int dx = cx - pcx, dz = cz - pcz;
        candidates.emplace_back(dx * dx + dz * dz, &loaded);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    double start = GetTime();
    for (auto& [dist, loaded] : candidates) {
        BuildModel(*loaded);
        if (GetTime() - start >= budgetSeconds) break;
    }
}

void World::BuildModel(LoadedChunk& loaded) {
    UnloadChunkModels(loaded);
    ChunkMeshes meshes = loaded.chunk->GenerateMesh(*this);
    auto make = [&](Mesh mesh) {
        Model model = LoadModelFromMesh(mesh);
        model.materials[0].shader = chunkShader;
        if (hasTexture) model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = atlasTexture;
        return model;
    };
    if (meshes.hasOpaque) {
        loaded.opaque = make(meshes.opaque);
        loaded.hasOpaque = true;
    }
    if (meshes.hasTranslucent) {
        loaded.translucent = make(meshes.translucent);
        loaded.hasTranslucent = true;
    }
    loaded.hasModel = true;
}

void World::RebuildDirty() {
    for (int64_t key : dirtyChunks) {
        auto it = chunks.find(key);
        if (it == chunks.end() || !it->second.hasModel) continue;
        BuildModel(it->second);
    }
    dirtyChunks.clear();
}

void World::LoadAround(Vector3 position) {
    int pcx = (int)floorf(position.x / Chunk::SIZE);
    int pcz = (int)floorf(position.z / Chunk::SIZE);
    hasStreamed = true;
    lastPlayerChunkX = pcx;
    lastPlayerChunkZ = pcz;
    RequestChunks(pcx, pcz);

    while (true) {
        IntegrateFinished(pcx, pcz);
        bool allLoaded = true;
        for (int dx = -DataDistance(); dx <= DataDistance() && allLoaded; dx++) {
            for (int dz = -DataDistance(); dz <= DataDistance() && allLoaded; dz++) {
                if (!chunks.count(Key(pcx + dx, pcz + dz))) allLoaded = false;
            }
        }
        if (allLoaded) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    dirtyChunks.clear();
    BuildModels(pcx, pcz, INFINITY);
}

void World::SetRenderDistance(int chunks) {
    renderDistance = std::clamp(chunks, 2, 16);
}

void World::Update(Vector3 playerPosition, float dt) {
    int pcx = (int)floorf(playerPosition.x / Chunk::SIZE);
    int pcz = (int)floorf(playerPosition.z / Chunk::SIZE);

    if (!hasStreamed || pcx != lastPlayerChunkX || pcz != lastPlayerChunkZ || streamedDistance != renderDistance) {
        hasStreamed = true;
        streamedDistance = renderDistance;
        lastPlayerChunkX = pcx;
        lastPlayerChunkZ = pcz;
        UnloadFarChunks(pcx, pcz);
        RequestChunks(pcx, pcz);
    }
    IntegrateFinished(pcx, pcz);

    tickAccumulator += dt;
    int ticks = 0;
    while (tickAccumulator >= TICK_SECONDS && ticks < MAX_TICKS_PER_FRAME) {
        tickAccumulator -= TICK_SECONDS;
        Tick();
        ticks++;
    }
    if (tickAccumulator > TICK_SECONDS) tickAccumulator = 0.0; // too far behind: skip ahead

    RebuildDirty();
    BuildModels(pcx, pcz, MESH_BUDGET_SECONDS);
}

// ---- Blocks

uint8_t World::GetBlock(int x, int y, int z) const {
    if (y < 0 || y >= Chunk::HEIGHT) return BLOCK_AIR;
    Chunk* chunk = ChunkAt(x, z);
    if (chunk == nullptr) return BLOCK_AIR;
    return chunk->GetBlock(LocalCoord(x), y, LocalCoord(z));
}

int World::GetMeta(int x, int y, int z) const {
    if (y < 0 || y >= Chunk::HEIGHT) return 0;
    Chunk* chunk = ChunkAt(x, z);
    if (chunk == nullptr) return 0;
    return chunk->GetMeta(LocalCoord(x), y, LocalCoord(z));
}

BlockMaterial World::GetMaterial(int x, int y, int z) const {
    return GetBlockDef(GetBlock(x, y, z)).material;
}

bool World::IsLoaded(int x, int z) const {
    return ChunkAt(x, z) != nullptr;
}

bool World::IsSolidMaterial(int x, int y, int z) const {
    return GetMaterialInfo(GetMaterial(x, y, z)).solid;
}

bool World::SetBlock(int x, int y, int z, int id, int meta, bool notify) {
    if (y < 0 || y >= Chunk::HEIGHT) return false;
    Chunk* chunk = ChunkAt(x, z);
    if (chunk == nullptr) return false;
    int lx = LocalCoord(x), lz = LocalCoord(z);
    uint8_t old = chunk->GetBlock(lx, y, lz);
    int oldMeta = chunk->GetMeta(lx, y, lz);
    meta &= 15;
    if (old == id && oldMeta == meta) return false;

    chunk->SetBlock(lx, y, lz, (uint8_t)id, meta);
    edits[Key(ChunkCoord(x), ChunkCoord(z))][EditIndex(lx, y, lz)] = (uint16_t)(id | meta << 8);

    if (old != id) GetBlockDef(old).OnRemoved(*this, x, y, z, oldMeta);
    if (BlockLightOpacity(old) != BlockLightOpacity((uint8_t)id) || BlockLightValue(old) != BlockLightValue((uint8_t)id)) {
        Relight(x, y, z);
    }
    if (old != id && GetBlock(x, y, z) == id) GetBlockDef((uint8_t)id).OnAdded(*this, x, y, z);
    MarkForRender(x, y, z);
    if (notify) NotifyNeighbors(x, y, z, id);
    return true;
}

bool World::SetMeta(int x, int y, int z, int meta, bool notify) {
    if (y < 0 || y >= Chunk::HEIGHT) return false;
    Chunk* chunk = ChunkAt(x, z);
    if (chunk == nullptr) return false;
    int lx = LocalCoord(x), lz = LocalCoord(z);
    meta &= 15;
    uint8_t id = chunk->GetBlock(lx, y, lz);
    chunk->SetMeta(lx, y, lz, meta);
    edits[Key(ChunkCoord(x), ChunkCoord(z))][EditIndex(lx, y, lz)] = (uint16_t)(id | meta << 8);
    MarkForRender(x, y, z);
    if (notify) NotifyNeighbors(x, y, z, id);
    return true;
}

void World::NotifyNeighbors(int x, int y, int z, int id) {
    NotifyBlock(x - 1, y, z, id);
    NotifyBlock(x + 1, y, z, id);
    NotifyBlock(x, y - 1, z, id);
    NotifyBlock(x, y + 1, z, id);
    NotifyBlock(x, y, z - 1, id);
    NotifyBlock(x, y, z + 1, id);
}

void World::NotifyBlock(int x, int y, int z, int neighborId) {
    uint8_t id = GetBlock(x, y, z);
    if (id == BLOCK_AIR) return;
    GetBlockDef(id).OnNeighborChanged(*this, x, y, z, neighborId);
}

// A block's looks reach one block into its neighbors (culling, AO, light), so
// its chunk gets rebuilt, and the chunks next to it if it's on their border.
void World::MarkForRender(int x, int y, int z) {
    (void)y;
    int cx = ChunkCoord(x), cz = ChunkCoord(z);
    int lx = x - cx * Chunk::SIZE, lz = z - cz * Chunk::SIZE;
    int x0 = lx == 0 ? -1 : 0, x1 = lx == Chunk::SIZE - 1 ? 1 : 0;
    int z0 = lz == 0 ? -1 : 0, z1 = lz == Chunk::SIZE - 1 ? 1 : 0;
    for (int dx = x0; dx <= x1; dx++)
        for (int dz = z0; dz <= z1; dz++) dirtyChunks.insert(Key(cx + dx, cz + dz));
}

void World::MarkLightDirty(int x, int z) {
    MarkForRender(x, 0, z);
}

// ---- Ticks

void World::ScheduleTick(int x, int y, int z, int id, int delay) {
    uint64_t key = TickKey(x, y, z, id);
    if (scheduledKeys.count(key)) return;
    scheduledKeys.insert(key);
    scheduled.insert(ScheduledTick{ tickCount + delay, tickOrder++, x, y, z, id });
}

void World::BlockEvent(int x, int y, int z, int a, int b) {
    uint8_t id = GetBlock(x, y, z);
    if (id != BLOCK_AIR) GetBlockDef(id).OnBlockEvent(*this, x, y, z, a, b);
}

void World::Tick() {
    tickCount++;
    TickScheduled();
    TickRandomly();
    TickTileEntities();
    if (entities != nullptr) entities->Tick(*this);
    RandomDisplayTicks();
    PlayCaveAmbience();
}

void World::TickScheduled() {
    for (int n = 0; n < 1000 && !scheduled.empty(); n++) {
        ScheduledTick t = *scheduled.begin();
        if (t.time > tickCount) break;
        scheduled.erase(scheduled.begin());
        scheduledKeys.erase(TickKey(t.x, t.y, t.z, t.id));
        if (!IsLoaded(t.x, t.z)) continue;
        uint8_t id = GetBlock(t.x, t.y, t.z);
        if (id == t.id && id != BLOCK_AIR) GetBlockDef(id).OnTick(*this, t.x, t.y, t.z, random);
    }
}

// World.updateBlocks: a few random blocks in every chunk near the player get a
// chance to do something (grow, spread, melt, burn out...).
void World::TickRandomly() {
    if (player == nullptr) return;
    Vector3 p = player->GetPosition();
    int pcx = ChunkCoord((int)floorf(p.x)), pcz = ChunkCoord((int)floorf(p.z));
    std::vector<std::pair<int, int>> nearby;
    for (int dx = -TICK_DISTANCE; dx <= TICK_DISTANCE; dx++) {
        for (int dz = -TICK_DISTANCE; dz <= TICK_DISTANCE; dz++) {
            if (chunks.count(Key(pcx + dx, pcz + dz))) nearby.emplace_back(pcx + dx, pcz + dz);
        }
    }
    for (auto [cx, cz] : nearby) {
        for (int i = 0; i < RANDOM_TICKS_PER_CHUNK; i++) {
            randomTickLcg = randomTickLcg * 3 + 1013904223;
            int r = randomTickLcg >> 2;
            int x = cx * Chunk::SIZE + (r & 15), z = cz * Chunk::SIZE + ((r >> 8) & 15), y = (r >> 16) & (Chunk::HEIGHT - 1);
            uint8_t id = GetBlock(x, y, z);
            const Block& def = GetBlockDef(id);
            if (def.ticksRandomly) def.OnTick(*this, x, y, z, random);
        }
    }
}

void World::TickTileEntities() {
    std::vector<int64_t> keys;
    keys.reserve(tileEntities.size());
    for (const auto& [key, tile] : tileEntities) keys.push_back(key);
    for (int64_t key : keys) {
        auto it = tileEntities.find(key);
        if (it == tileEntities.end()) continue;
        int x = TileX(key), y = TileY(key), z = TileZ(key);
        if (!IsLoaded(x, z)) continue;
        it->second->Tick(*this, x, y, z); // may remove itself
    }
}

// World.randomDisplayUpdates: torches smoke, lava spits, portals swirl.
void World::RandomDisplayTicks() {
    if (player == nullptr) return;
    Vector3 p = player->GetPosition();
    int px = (int)floorf(p.x), py = (int)floorf(p.y), pz = (int)floorf(p.z);
    const int range = 16;
    for (int i = 0; i < 1000; i++) {
        int x = px + random.NextInt(range) - random.NextInt(range);
        int y = py + random.NextInt(range) - random.NextInt(range);
        int z = pz + random.NextInt(range) - random.NextInt(range);
        uint8_t id = GetBlock(x, y, z);
        if (id != BLOCK_AIR) GetBlockDef(id).RandomDisplayTick(*this, x, y, z, random);
    }
}

// World.updateBlocks' cave sounds: now and then, somewhere dark near the player.
void World::PlayCaveAmbience() {
    if (player == nullptr) return;
    if (ambientCountdown > 0) {
        ambientCountdown--;
        return;
    }
    Vector3 p = player->GetPosition();
    int x = (int)floorf(p.x) + random.NextInt(16) - 8;
    int y = (int)floorf(p.y) + random.NextInt(16) - 8;
    int z = (int)floorf(p.z) + random.NextInt(16) - 8;
    if (GetBlock(x, y, z) == BLOCK_AIR && std::max(GetSkyLight(x, y, z), GetBlockLight(x, y, z)) <= random.NextInt(8) &&
        GetSkyLight(x, y, z) <= 0) {
        double dx = x + 0.5 - p.x, dy = y + 0.5 - p.y, dz = z + 0.5 - p.z;
        if (dx * dx + dy * dy + dz * dz > 4.0) {
            PlaySound("ambient.cave.cave", x + 0.5, y + 0.5, z + 0.5, 0.7f, 0.8f + random.NextFloat() * 0.2f);
            ambientCountdown = random.NextInt(12000) + 6000;
        }
    }
}

// ---- Light

int World::GetSkyLight(int x, int y, int z) const {
    if (y >= Chunk::HEIGHT) return MAX_LIGHT;
    if (y < 0) return 0;
    Chunk* chunk = ChunkAt(x, z);
    if (chunk == nullptr) return MAX_LIGHT;
    return chunk->GetSkyLight(LocalCoord(x), y, LocalCoord(z));
}

int World::GetBlockLight(int x, int y, int z) const {
    if (y < 0 || y >= Chunk::HEIGHT) return 0;
    Chunk* chunk = ChunkAt(x, z);
    if (chunk == nullptr) return 0;
    return chunk->GetBlockLight(LocalCoord(x), y, LocalCoord(z));
}

int World::GetLight(int x, int y, int z) const {
    return std::max(GetSkyLight(x, y, z) - skyDarken, GetBlockLight(x, y, z));
}

void World::SpreadLight(int channel, std::deque<LightCell>& seeds) {
    while (!seeds.empty()) {
        LightCell cell = seeds.front();
        seeds.pop_front();
        Chunk* chunk = ChunkAt(cell.x, cell.z);
        if (chunk == nullptr) continue;
        int level = chunk->GetLight(channel, LocalCoord(cell.x), cell.y, LocalCoord(cell.z));
        if (level <= 1) continue;

        for (int dir = 0; dir < 6; dir++) {
            int nx = cell.x + LIGHT_DIRS[dir][0], ny = cell.y + LIGHT_DIRS[dir][1], nz = cell.z + LIGHT_DIRS[dir][2];
            if (ny < 0 || ny >= Chunk::HEIGHT) continue;
            Chunk* neighbor = ChunkAt(nx, nz);
            if (neighbor == nullptr) continue;
            int lx = LocalCoord(nx), lz = LocalCoord(nz);
            int opacity = BlockLightOpacity(neighbor->GetBlock(lx, ny, lz));
            if (opacity >= MAX_LIGHT) continue;
            int lit = LightInto(level, opacity, channel == 0 && dir == LIGHT_DOWN);
            if (lit <= neighbor->GetLight(channel, lx, ny, lz)) continue;
            neighbor->SetLight(channel, lx, ny, lz, (uint8_t)lit);
            MarkLightDirty(nx, nz);
            seeds.push_back({ nx, ny, nz, lit });
        }
    }
}

void World::RemoveLight(int channel, std::deque<LightCell>& removed, std::deque<LightCell>& refill) {
    while (!removed.empty()) {
        LightCell cell = removed.front();
        removed.pop_front();
        for (int dir = 0; dir < 6; dir++) {
            int nx = cell.x + LIGHT_DIRS[dir][0], ny = cell.y + LIGHT_DIRS[dir][1], nz = cell.z + LIGHT_DIRS[dir][2];
            if (ny < 0 || ny >= Chunk::HEIGHT) continue;
            Chunk* neighbor = ChunkAt(nx, nz);
            if (neighbor == nullptr) continue;
            int lx = LocalCoord(nx), lz = LocalCoord(nz);
            int level = neighbor->GetLight(channel, lx, ny, lz);
            if (level == 0) continue;

            // Lit through the removed cell (dimmer, or sunlight straight below it):
            // clear it too. Anything at least as bright has its own source; relight from it.
            bool litThroughCell = level < cell.level ||
                                  (channel == 0 && dir == LIGHT_DOWN && cell.level == MAX_LIGHT && level == MAX_LIGHT);
            if (litThroughCell) {
                int glow = channel == 1 ? BlockLightValue(neighbor->GetBlock(lx, ny, lz)) : 0;
                neighbor->SetLight(channel, lx, ny, lz, (uint8_t)glow);
                MarkLightDirty(nx, nz);
                removed.push_back({ nx, ny, nz, level });
                if (glow > 0) refill.push_back({ nx, ny, nz, glow });
            } else {
                refill.push_back({ nx, ny, nz, level });
            }
        }
    }
}

// After a block changes: take away whatever light passed through it, then let
// the light around it (and its own glow) flow back in under its new opacity.
void World::Relight(int x, int y, int z) {
    Chunk* chunk = ChunkAt(x, z);
    if (chunk == nullptr) return;
    int lx = LocalCoord(x), lz = LocalCoord(z);
    uint8_t id = chunk->GetBlock(lx, y, lz);
    int opacity = BlockLightOpacity(id);
    for (int channel = 0; channel < 2; channel++) {
        std::deque<LightCell> removed, refill;
        int old = chunk->GetLight(channel, lx, y, lz);
        chunk->SetLight(channel, lx, y, lz, 0);
        removed.push_back({ x, y, z, old });
        RemoveLight(channel, removed, refill);
        if (channel == 1) {
            int glow = BlockLightValue(id);
            if (glow > 0) {
                chunk->SetLight(1, lx, y, lz, (uint8_t)glow);
                refill.push_back({ x, y, z, glow });
            }
        } else if (y == Chunk::HEIGHT - 1 && opacity < MAX_LIGHT) {
            // Open sky above the top of the world.
            chunk->SetLight(0, lx, y, lz, (uint8_t)std::max(0, LightInto(MAX_LIGHT, opacity, true)));
            refill.push_back({ x, y, z, 0 });
        }
        for (const auto& dir : LIGHT_DIRS) {
            int ny = y + dir[1];
            if (ny >= 0 && ny < Chunk::HEIGHT) refill.push_back({ x + dir[0], ny, z + dir[2], 0 });
        }
        SpreadLight(channel, refill);
    }
    MarkLightDirty(x, z);
}

// A newly loaded chunk and each loaded neighbor light each other across their
// shared face. Only unmeshed chunks can change: light fades out within 15
// blocks, and every chunk that close to a new one was still waiting for it.
void World::LightAcrossBorders(int cx, int cz) {
    const int sides[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int channel = 0; channel < 2; channel++) {
        std::deque<LightCell> seeds;
        for (const auto& side : sides) {
            if (!chunks.count(Key(cx + side[0], cz + side[1]))) continue;
            for (int i = 0; i < Chunk::SIZE; i++) {
                int ax, az, bx, bz;
                if (side[0] != 0) {
                    ax = cx * Chunk::SIZE + (side[0] > 0 ? Chunk::SIZE - 1 : 0);
                    bx = ax + side[0];
                    az = bz = cz * Chunk::SIZE + i;
                } else {
                    az = cz * Chunk::SIZE + (side[1] > 0 ? Chunk::SIZE - 1 : 0);
                    bz = az + side[1];
                    ax = bx = cx * Chunk::SIZE + i;
                }
                Chunk* a = ChunkAt(ax, az);
                Chunk* b = ChunkAt(bx, bz);
                for (int y = 0; y < Chunk::HEIGHT; y++) {
                    if (a->GetLight(channel, LocalCoord(ax), y, LocalCoord(az)) > 1) seeds.push_back({ ax, y, az, 0 });
                    if (b->GetLight(channel, LocalCoord(bx), y, LocalCoord(bz)) > 1) seeds.push_back({ bx, y, bz, 0 });
                }
            }
        }
        SpreadLight(channel, seeds);
    }
}

// ---- Redstone (World.isBlockGettingPowered & co.)

bool World::IsBlockProvidingPowerTo(int x, int y, int z, int side) const {
    uint8_t id = GetBlock(x, y, z);
    return id != BLOCK_AIR && GetBlockDef(id).IsIndirectlyPoweringTo(*this, x, y, z, side);
}

bool World::IsBlockGettingPowered(int x, int y, int z) const {
    return IsBlockProvidingPowerTo(x, y - 1, z, 0) || IsBlockProvidingPowerTo(x, y + 1, z, 1) ||
           IsBlockProvidingPowerTo(x, y, z - 1, 2) || IsBlockProvidingPowerTo(x, y, z + 1, 3) ||
           IsBlockProvidingPowerTo(x - 1, y, z, 4) || IsBlockProvidingPowerTo(x + 1, y, z, 5);
}

bool World::IsBlockIndirectlyProvidingPowerTo(int x, int y, int z, int side) const {
    if (IsNormalCube(x, y, z)) return IsBlockGettingPowered(x, y, z);
    uint8_t id = GetBlock(x, y, z);
    return id != BLOCK_AIR && GetBlockDef(id).IsPoweringTo(*this, x, y, z, side);
}

bool World::IsBlockIndirectlyGettingPowered(int x, int y, int z) const {
    return IsBlockIndirectlyProvidingPowerTo(x, y - 1, z, 0) || IsBlockIndirectlyProvidingPowerTo(x, y + 1, z, 1) ||
           IsBlockIndirectlyProvidingPowerTo(x, y, z - 1, 2) || IsBlockIndirectlyProvidingPowerTo(x, y, z + 1, 3) ||
           IsBlockIndirectlyProvidingPowerTo(x - 1, y, z, 4) || IsBlockIndirectlyProvidingPowerTo(x + 1, y, z, 5);
}

// ---- Tile entities

TileEntity* World::GetTileEntity(int x, int y, int z) const {
    auto it = tileEntities.find(TileKey(x, y, z));
    return it == tileEntities.end() ? nullptr : it->second.get();
}

void World::SetTileEntity(int x, int y, int z, std::unique_ptr<TileEntity> tile) {
    tileEntities[TileKey(x, y, z)] = std::move(tile);
}

void World::RemoveTileEntity(int x, int y, int z) {
    tileEntities.erase(TileKey(x, y, z));
}

// ---- Effects

void World::PlaySound(const char* name, double x, double y, double z, float volume, float pitch) {
    if (sounds != nullptr && name != nullptr) sounds->Play(name, (float)x, (float)y, (float)z, volume, pitch);
}

void World::SpawnParticle(ParticleType type, double x, double y, double z, double mx, double my, double mz) {
    if (particles != nullptr) particles->Spawn(type, x, y, z, mx, my, mz);
}

void World::BreakEffects(int x, int y, int z, int id, int meta) {
    const Block& def = GetBlockDef((uint8_t)id);
    PlaySound(BreakSoundEvent(def.sound), x + 0.5, y + 0.5, z + 0.5, (StepSoundVolume(def.sound) + 1.0f) / 2.0f,
              StepSoundPitch(def.sound) * 0.8f);
    if (particles != nullptr) particles->AddDestroyEffects(*this, x, y, z, (uint8_t)id, meta);
}

void World::DropBlockAsItem(int x, int y, int z, int id, int meta, float chance) {
    if (id <= 0) return;
    std::vector<ItemStack> out;
    GetBlockDef((uint8_t)id).Drops(*this, x, y, z, meta, random, out);
    for (const ItemStack& stack : out) {
        if (stack.IsEmpty()) continue;
        if (random.NextFloat() > chance) continue;
        SpawnItem(x, y, z, stack);
    }
}

void World::SpawnItem(int x, int y, int z, const ItemStack& stack) {
    if (drops != nullptr && !stack.IsEmpty()) drops->SpawnFromBlock(x, y, z, stack);
}

void World::SpawnItemAt(double x, double y, double z, const ItemStack& stack, double mx, double my, double mz, int pickupDelay) {
    if (drops != nullptr && !stack.IsEmpty()) drops->Spawn(x, y, z, mx, my, mz, stack, pickupDelay);
}

void World::SpawnFallingBlock(int x, int y, int z, int id, int meta) {
    if (entities != nullptr) entities->SpawnFallingBlock(x + 0.5, y, z + 0.5, id, meta);
}

void World::SpawnPrimedTnt(double x, double y, double z, int fuse) {
    if (entities != nullptr) entities->SpawnTnt(x, y, z, fuse);
}

// How much of a box can see the point (Explosion's getBlockDensity): the share
// of sample points in the box with a clear line to it.
float World::BlockDensity(Vector3 from, const double min[3], const double max[3]) const {
    int clear = 0, total = 0;
    for (int i = 0; i <= 2; i++) {
        for (int j = 0; j <= 2; j++) {
            for (int k = 0; k <= 2; k++) {
                Vector3 p = { (float)(min[0] + (max[0] - min[0]) * i / 2.0), (float)(min[1] + (max[1] - min[1]) * j / 2.0),
                              (float)(min[2] + (max[2] - min[2]) * k / 2.0) };
                Vector3 d = Vector3Subtract(from, p);
                float len = Vector3Length(d);
                total++;
                if (len < 1e-3f) { clear++; continue; }
                RaycastHit hit = Raycast(p, d, len, false);
                if (!hit.hit || !BlockIsOpaqueCube(GetBlock(hit.x, hit.y, hit.z))) clear++;
            }
        }
    }
    return total > 0 ? (float)clear / total : 1.0f;
}

// Explosion.doExplosionA and doExplosionB.
void World::Explode(double x, double y, double z, float power, bool fire) {
    std::vector<std::tuple<int, int, int>> destroyed;
    std::unordered_set<int64_t> seen;
    const int n = 16;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            for (int k = 0; k < n; k++) {
                if (i != 0 && i != n - 1 && j != 0 && j != n - 1 && k != 0 && k != n - 1) continue;
                double dx = i / (n - 1.0) * 2.0 - 1.0, dy = j / (n - 1.0) * 2.0 - 1.0, dz = k / (n - 1.0) * 2.0 - 1.0;
                double len = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= len; dy /= len; dz /= len;
                float strength = power * (0.7f + random.NextFloat() * 0.6f);
                double px = x, py = y, pz = z;
                const float step = 0.3f;
                for (; strength > 0.0f; strength -= step * 0.75f) {
                    int bx = (int)floor(px), by = (int)floor(py), bz = (int)floor(pz);
                    uint8_t id = GetBlock(bx, by, bz);
                    if (id != BLOCK_AIR) strength -= (GetBlockDef(id).resistance + 0.3f) * step;
                    if (strength > 0.0f && by >= 0 && by < Chunk::HEIGHT) {
                        if (seen.insert(TileKey(bx, by, bz)).second) destroyed.emplace_back(bx, by, bz);
                    }
                    px += dx * step; py += dy * step; pz += dz * step;
                }
            }
        }
    }

    // Hurt and fling the player.
    if (player != nullptr) {
        double radius = power * 2.0;
        Vector3 p = player->GetPosition();
        double ex = p.x - x, ey = p.y - y, ez = p.z - z;
        double len = std::sqrt(ex * ex + ey * ey + ez * ez);
        double dist = len / radius;
        if (dist <= 1.0) {
            if (len > 1e-4) { ex /= len; ey /= len; ez /= len; }
            double min[3] = { p.x - 0.3, p.y, p.z - 0.3 }, max[3] = { p.x + 0.3, p.y + 1.8, p.z + 0.3 };
            double density = BlockDensity(Vector3{ (float)x, (float)y, (float)z }, min, max);
            double impact = (1.0 - dist) * density;
            player->Hurt((float)(int)((impact * impact + impact) / 2.0 * 8.0 * radius + 1.0));
            player->AddVelocity(ex * impact, ey * impact, ez * impact);
        }
    }
    if (entities != nullptr) entities->ExplosionPush(*this, x, y, z, power);

    PlaySound("random.explode", x, y, z, 4.0f, (1.0f + (random.NextFloat() - random.NextFloat()) * 0.2f) * 0.7f);
    for (auto it = destroyed.rbegin(); it != destroyed.rend(); ++it) {
        auto [bx, by, bz] = *it;
        uint8_t id = GetBlock(bx, by, bz);
        double fx = bx + random.NextFloat(), fy = by + random.NextFloat(), fz = bz + random.NextFloat();
        double mx = fx - x, my = fy - y, mz = fz - z;
        double len = std::sqrt(mx * mx + my * my + mz * mz);
        if (len > 1e-4) {
            mx /= len; my /= len; mz /= len;
            double speed = 0.5 / (len / power + 0.1) * (random.NextFloat() * random.NextFloat() + 0.3f);
            mx *= speed; my *= speed; mz *= speed;
            SpawnParticle(ParticleType::Explode, (fx + x) / 2.0, (fy + y) / 2.0, (fz + z) / 2.0, mx, my, mz);
            SpawnParticle(ParticleType::Smoke, fx, fy, fz, mx, my, mz);
        }
        if (id == BLOCK_AIR) continue;
        int meta = GetMeta(bx, by, bz);
        DropBlockAsItem(bx, by, bz, id, meta, 0.3f);
        SetBlock(bx, by, bz, BLOCK_AIR);
        GetBlockDef(id).OnExploded(*this, bx, by, bz);
    }
    if (fire) {
        for (auto [bx, by, bz] : destroyed) {
            if (GetBlock(bx, by, bz) == BLOCK_AIR && IsOpaqueCube(bx, by - 1, bz) && random.NextInt(3) == 0) SetBlock(bx, by, bz, BLOCK_FIRE);
        }
    }
}

// ---- Queries

namespace {

// Ray against a box; returns the distance and which side was entered.
bool RayBox(const float o[3], const float d[3], const float mn[3], const float mx[3], float maxT, float& tHit, int& side) {
    float tmin = 0.0f, tmax = maxT;
    int enter = -1;
    static const int NEG_SIDE[3] = { SIDE_WEST, SIDE_DOWN, SIDE_NORTH };
    static const int POS_SIDE[3] = { SIDE_EAST, SIDE_UP, SIDE_SOUTH };
    for (int a = 0; a < 3; a++) {
        if (std::fabs(d[a]) < 1e-9f) {
            if (o[a] < mn[a] || o[a] > mx[a]) return false;
            continue;
        }
        float t1 = (mn[a] - o[a]) / d[a], t2 = (mx[a] - o[a]) / d[a];
        int s1 = NEG_SIDE[a], s2 = POS_SIDE[a];
        if (t1 > t2) { std::swap(t1, t2); std::swap(s1, s2); }
        if (t1 > tmin) { tmin = t1; enter = s1; }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (enter < 0) enter = SIDE_UP; // started inside the box
    tHit = tmin;
    side = enter;
    return true;
}

} // namespace

// Voxel traversal (Amanatides & Woo), testing each block's real outline.
RaycastHit World::Raycast(Vector3 origin, Vector3 direction, float maxDistance, bool hitLiquids) const {
    RaycastHit result;
    float length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (length <= 0.0f) return result;
    float dir[3] = { direction.x / length, direction.y / length, direction.z / length };
    float pos[3] = { origin.x, origin.y, origin.z };

    int cell[3], step[3];
    float tMax[3], tDelta[3];
    for (int a = 0; a < 3; a++) {
        cell[a] = (int)floorf(pos[a]);
        if (dir[a] > 0.0f) {
            step[a] = 1;
            tDelta[a] = 1.0f / dir[a];
            tMax[a] = (cell[a] + 1 - pos[a]) * tDelta[a];
        } else if (dir[a] < 0.0f) {
            step[a] = -1;
            tDelta[a] = -1.0f / dir[a];
            tMax[a] = (pos[a] - cell[a]) * tDelta[a];
        } else {
            step[a] = 0;
            tDelta[a] = INFINITY;
            tMax[a] = INFINITY;
        }
    }

    float t = 0.0f;
    for (int guard = 0; guard < 1024 && t <= maxDistance; guard++) {
        uint8_t id = GetBlock(cell[0], cell[1], cell[2]);
        if (id != BLOCK_AIR) {
            const Block& def = GetBlockDef(id);
            int meta = GetMeta(cell[0], cell[1], cell[2]);
            bool targetable = def.IsLiquid() ? (hitLiquids && meta == 0) : def.IsTargetable(meta);
            if (targetable) {
                BlockBox b = def.IsLiquid() ? BlockBox() : def.Bounds(*this, cell[0], cell[1], cell[2]);
                float mn[3] = { cell[0] + b.minX, cell[1] + b.minY, cell[2] + b.minZ };
                float mx[3] = { cell[0] + b.maxX, cell[1] + b.maxY, cell[2] + b.maxZ };
                float tHit;
                int side;
                if (RayBox(pos, dir, mn, mx, maxDistance, tHit, side)) {
                    result.hit = true;
                    result.x = cell[0]; result.y = cell[1]; result.z = cell[2];
                    result.side = side;
                    result.prevX = cell[0] + SIDE_OFFSET[side][0];
                    result.prevY = cell[1] + SIDE_OFFSET[side][1];
                    result.prevZ = cell[2] + SIDE_OFFSET[side][2];
                    result.point = { pos[0] + dir[0] * tHit, pos[1] + dir[1] * tHit, pos[2] + dir[2] * tHit };
                    return result;
                }
            }
        }
        int axis = (tMax[0] < tMax[1]) ? ((tMax[0] < tMax[2]) ? 0 : 2) : ((tMax[1] < tMax[2]) ? 1 : 2);
        cell[axis] += step[axis];
        t = tMax[axis];
        tMax[axis] += tDelta[axis];
    }
    return result;
}

void World::CollisionBoxes(const double min[3], const double max[3], std::vector<BlockBox>& out, std::vector<Vector3>* origins) const {
    int x0 = (int)floor(min[0]), x1 = (int)floor(max[0]);
    int y0 = (int)floor(min[1]) - 1, y1 = (int)floor(max[1]); // fences reach up into the block above
    int z0 = (int)floor(min[2]), z1 = (int)floor(max[2]);
    std::vector<BlockBox> local;
    for (int x = x0; x <= x1; x++) {
        for (int z = z0; z <= z1; z++) {
            bool loaded = IsLoaded(x, z);
            for (int y = y0; y <= y1; y++) {
                if (!loaded) {
                    // Unloaded ground is solid, so nothing falls out of the world.
                    if (y < Chunk::SEA_LEVEL) {
                        out.push_back(BlockBox{ (float)x, (float)y, (float)z, x + 1.0f, y + 1.0f, z + 1.0f });
                        if (origins != nullptr) origins->push_back(Vector3{ (float)x, (float)y, (float)z });
                    }
                    continue;
                }
                uint8_t id = GetBlock(x, y, z);
                if (id == BLOCK_AIR) continue;
                local.clear();
                GetBlockDef(id).CollisionBoxes(*this, x, y, z, local);
                for (const BlockBox& b : local) {
                    BlockBox w{ x + b.minX, y + b.minY, z + b.minZ, x + b.maxX, y + b.maxY, z + b.maxZ };
                    if (w.maxX <= min[0] || w.minX >= max[0] || w.maxY <= min[1] || w.minY >= max[1] || w.maxZ <= min[2] ||
                        w.minZ >= max[2]) {
                        continue;
                    }
                    out.push_back(w);
                    if (origins != nullptr) origins->push_back(Vector3{ (float)x, (float)y, (float)z });
                }
            }
        }
    }
}

bool World::EntitiesInBox(const double min[3], const double max[3], bool livingOnly) const {
    if (player != nullptr) {
        Vector3 p = player->GetPosition();
        double pmin[3] = { p.x - 0.3, p.y, p.z - 0.3 }, pmax[3] = { p.x + 0.3, p.y + 1.8, p.z + 0.3 };
        if (pmin[0] < max[0] && pmax[0] > min[0] && pmin[1] < max[1] && pmax[1] > min[1] && pmin[2] < max[2] && pmax[2] > min[2]) return true;
    }
    if (livingOnly) return false;
    if (drops != nullptr && drops->AnyInBox(min, max)) return true;
    return entities != nullptr && entities->AnyInBox(min, max);
}

// ---- Drawing

void World::SetFog(Vector3 color, float start, float end, float density, float darken, Vector3 cameraPos) {
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "fogColor"), &color, SHADER_UNIFORM_VEC3);
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "fogStart"), &start, SHADER_UNIFORM_FLOAT);
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "fogEnd"), &end, SHADER_UNIFORM_FLOAT);
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "fogDensity"), &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "skyDarken"), &darken, SHADER_UNIFORM_FLOAT);
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "cameraPos"), &cameraPos, SHADER_UNIFORM_VEC3);
}

void World::Draw() const {
    float pass = 0.0f;
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "translucentPass"), &pass, SHADER_UNIFORM_FLOAT);
    for (const auto& [key, loaded] : chunks) {
        if (!loaded.hasOpaque) continue;
        Vector3 origin = { (float)(loaded.chunk->GetChunkX() * Chunk::SIZE), 0.0f, (float)(loaded.chunk->GetChunkZ() * Chunk::SIZE) };
        DrawModel(loaded.opaque, origin, 1.0f, WHITE);
    }
}

void World::DrawTranslucent(Vector3 cameraPos) const {
    std::vector<std::pair<float, const LoadedChunk*>> order;
    for (const auto& [key, loaded] : chunks) {
        if (!loaded.hasTranslucent) continue;
        float cx = loaded.chunk->GetChunkX() * Chunk::SIZE + 8.0f - cameraPos.x;
        float cz = loaded.chunk->GetChunkZ() * Chunk::SIZE + 8.0f - cameraPos.z;
        order.emplace_back(cx * cx + cz * cz, &loaded);
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    float pass = 1.0f;
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "translucentPass"), &pass, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ALPHA);
    for (const auto& [dist, loaded] : order) {
        Vector3 origin = { (float)(loaded->chunk->GetChunkX() * Chunk::SIZE), 0.0f, (float)(loaded->chunk->GetChunkZ() * Chunk::SIZE) };
        DrawModel(loaded->translucent, origin, 1.0f, WHITE);
    }
    EndBlendMode();
    pass = 0.0f;
    SetShaderValue(chunkShader, GetShaderLocation(chunkShader, "translucentPass"), &pass, SHADER_UNIFORM_FLOAT);
}
