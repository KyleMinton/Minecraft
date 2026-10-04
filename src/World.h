#pragma once

#include "raylib.h"
#include "Chunk.h"
#include "JavaRandom.h"
#include "TileEntity.h"

#include <climits>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class SoundSystem;
class DroppedItems;
class BlockParticles;
class Entities;
class Player;

struct RaycastHit {
    bool hit = false;
    int x = 0, y = 0, z = 0;             // the targeted block
    int prevX = 0, prevY = 0, prevZ = 0; // the cell just in front of the hit face (where a placed block goes)
    int side = SIDE_UP;                  // which face of it was hit
    Vector3 point = { 0, 0, 0 };         // exactly where the ray hit
};

// The particles blocks and explosions make (Minecraft's spawnParticle names).
enum class ParticleType { Smoke, LargeSmoke, Flame, Lava, Reddust, Portal, Splash, Bubble, Explode, Note };

// Owns every loaded Chunk and streams them in/out around the player, giving an
// infinite world while only holding a bounded area in memory. Also runs the
// world's 20 Hz tick: scheduled block updates (liquids, redstone, pistons...),
// random block ticks (growing, spreading, melting...), tile entities and entities.
//
// Terrain is generated on background threads. A chunk's mesh is only built
// once all 8 surrounding chunks exist (so its borders and AO are always right),
// nearest first. Block changes remesh their chunks within a frame.
class World {
public:
    // Chunks with meshes, as a radius (the options menu's Render Distance). 8 =
    // Minecraft's "Normal" (128 blocks). Chunks with generated blocks reach one
    // further, and both unload a little past that so edges don't thrash.
    static int RenderDistance() { return renderDistance; }
    static float ViewDistance() { return renderDistance * 16.0f; } // blocks; fog ends here
    static int DataDistance() { return renderDistance + 1; }
    static int ModelUnloadDistance() { return renderDistance + 1; }
    static int DataUnloadDistance() { return renderDistance + 2; }
    static void SetRenderDistance(int chunks);
    static constexpr int TICK_DISTANCE = 7;                    // chunks around the player that tick

    explicit World(int64_t seed);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    int64_t GetSeed() const { return seed; }

    // The systems block logic reaches into (sounds, dropped items, particles,
    // falling blocks and TNT, the player). Set before playing.
    void SetServices(SoundSystem* sounds, DroppedItems* drops, BlockParticles* particles, Entities* entities, Player* player);

    // ---- Saving. Terrain comes from the seed, so a save only needs the blocks
    // that changed, plus tile entities. Load before LoadAround().
    bool SaveEdits(const std::string& path) const;
    bool LoadEdits(const std::string& path);
    // Pre-Beta-blocks saves (block IDs 0-17 of this game's own numbering).
    bool LoadLegacyEdits(const std::string& path);
    void SaveTileEntities(std::ostream& out) const;
    void LoadTileEntities(std::istream& in);

    // ---- Streaming
    void SetTexture(Texture2D texture, bool enabled);
    // Blocks until everything within render distance of `position` is generated
    // and meshed. For startup, before there's anything to show.
    void LoadAround(Vector3 position);
    // Every frame: streams chunks, runs the ticks due (20 per second), and
    // rebuilds meshes that changed.
    void Update(Vector3 playerPosition, float dt);
    long long GetTickCount() const { return tickCount; }
    // How far this frame is between world ticks, for drawing moving things.
    float GetTickAlpha() const { return (float)(tickAccumulator / 0.05); }

    // ---- Blocks
    BlockId GetBlock(int x, int y, int z) const;
    int GetMeta(int x, int y, int z) const;
    BlockMaterial GetMaterial(int x, int y, int z) const;
    bool IsLoaded(int x, int z) const;
    bool IsAir(int x, int y, int z) const { return GetBlock(x, y, z) == BLOCK_AIR; }
    bool IsOpaqueCube(int x, int y, int z) const { return BlockIsOpaqueCube(GetBlock(x, y, z)); }
    bool IsNormalCube(int x, int y, int z) const { return BlockIsNormalCube(GetBlock(x, y, z)); }
    // A solid top to stand things on (Minecraft's isBlockSolidOnSide/isBlockNormalCube).
    bool IsSolidMaterial(int x, int y, int z) const;

    // Changes a block (and its metadata). Calls the old block's OnRemoved and the
    // new one's OnAdded, relights, remeshes, and remembers the change for saving.
    // With `notify`, the six neighbors hear about it (OnNeighborChanged).
    bool SetBlock(int x, int y, int z, int id, int meta = 0, bool notify = true);
    bool SetMeta(int x, int y, int z, int meta, bool notify = true);
    void NotifyNeighbors(int x, int y, int z, int id);
    void NotifyBlock(int x, int y, int z, int neighborId);
    void MarkForRender(int x, int y, int z);

    // ---- Ticks
    void ScheduleTick(int x, int y, int z, int id, int delay);
    // A block event (pistons moving, note blocks playing), delivered at once.
    void BlockEvent(int x, int y, int z, int a, int b);
    JavaRandom& Random() { return random; }
    // Recent redstone torch toggles, for burning out (BlockRedstoneTorch.torchUpdates).
    // Kept per world: world tick counts restart at 0 each time one is opened.
    struct TorchToggle { int x, y, z; long long time; };
    std::deque<TorchToggle>& TorchToggles() { return torchToggles; }

    // ---- Light
    int GetSkyLight(int x, int y, int z) const;
    int GetBlockLight(int x, int y, int z) const;
    // The level that lights this spot right now: sky light less night, or block light.
    int GetLight(int x, int y, int z) const;
    int GetSkyDarken() const { return skyDarken; }
    void SetSkyDarken(int levels) { skyDarken = levels; }
    // Is it daytime enough for things that need the sky (sleeping, mobs)?
    bool IsDaytime() const { return skyDarken < 4; }

    // ---- Redstone (World.isBlockGettingPowered & co.)
    bool IsBlockProvidingPowerTo(int x, int y, int z, int side) const;
    bool IsBlockGettingPowered(int x, int y, int z) const;
    bool IsBlockIndirectlyProvidingPowerTo(int x, int y, int z, int side) const;
    bool IsBlockIndirectlyGettingPowered(int x, int y, int z) const;

    // ---- Tile entities
    TileEntity* GetTileEntity(int x, int y, int z) const;
    void SetTileEntity(int x, int y, int z, std::unique_ptr<TileEntity> tile);
    void RemoveTileEntity(int x, int y, int z);
    // Every tile entity near `center` (for rendering chests, signs, moving blocks).
    template <typename Fn> void ForEachTileEntity(Fn fn) const {
        for (const auto& [key, tile] : tileEntities) fn(TileX(key), TileY(key), TileZ(key), *tile);
    }

    // ---- Effects
    void PlaySound(const char* name, double x, double y, double z, float volume, float pitch);
    void SpawnParticle(ParticleType type, double x, double y, double z, double mx, double my, double mz);
    // Breaking a block: its break sound and the burst of block particles.
    void BreakEffects(int x, int y, int z, int id, int meta);
    // Block.dropBlockAsItemWithChance: each drop has `chance` to appear, popped out of the block.
    void DropBlockAsItem(int x, int y, int z, int id, int meta, float chance = 1.0f);
    void SpawnItem(int x, int y, int z, const ItemStack& stack);
    void SpawnItemAt(double x, double y, double z, const ItemStack& stack, double mx, double my, double mz, int pickupDelay);
    void SpawnFallingBlock(int x, int y, int z, int id, int meta);
    void SpawnPrimedTnt(double x, double y, double z, int fuse);
    // An explosion (Explosion.doExplosionA/B): breaks blocks, drops some, hurts and pushes the player.
    void Explode(double x, double y, double z, float power, bool fire);
    Player* GetPlayer() const { return player; }
    SoundSystem* GetSounds() const { return sounds; }
    Entities* GetEntities() const { return entities; }

    // ---- Queries
    // Aims along a ray and returns the first targetable block it enters, tested
    // against the block's real outline (slabs, torches...). Liquids only count with `hitLiquids`.
    RaycastHit Raycast(Vector3 origin, Vector3 direction, float maxDistance, bool hitLiquids = false) const;
    // Every block collision box overlapping [min, max], in world coordinates.
    void CollisionBoxes(const double min[3], const double max[3], std::vector<BlockBox>& out, std::vector<Vector3>* origins = nullptr) const;
    // Is anything (the player; with !livingOnly also items, falling blocks, TNT) inside the box?
    bool EntitiesInBox(const double min[3], const double max[3], bool livingOnly) const;

    // ---- Drawing
    void SetFog(Vector3 color, float start, float end, float density, float skyDarken, Vector3 cameraPos);
    void Draw() const;            // opaque and alpha-tested blocks
    void DrawTranslucent(Vector3 cameraPos) const; // water, ice, portals, far to near
    Shader GetChunkShader() const { return chunkShader; }
    Texture2D GetAtlas() const { return atlasTexture; }

    static int ChunkCoord(int worldCoord);

private:
    struct LoadedChunk {
        std::unique_ptr<Chunk> chunk;
        Model opaque{}, translucent{};
        bool hasModel = false;      // meshed (the opaque model exists, maybe empty)
        bool hasOpaque = false;
        bool hasTranslucent = false;
    };
    struct ScheduledTick {
        long long time;
        long long order;
        int x, y, z, id;
        bool operator<(const ScheduledTick& other) const {
            return time != other.time ? time < other.time : order < other.order;
        }
    };

    static inline int renderDistance = 8;
    int streamedDistance = 0; // the render distance the loaded area was streamed for
    int64_t seed;
    std::unordered_map<int64_t, LoadedChunk> chunks;
    // Changed blocks per chunk key: local block index -> id | meta << 16.
    std::unordered_map<int64_t, std::unordered_map<int, uint32_t>> edits;
    std::unordered_map<int64_t, std::unique_ptr<TileEntity>> tileEntities; // by block position key
    std::unordered_set<int64_t> tilesPopulated; // chunks whose generated chests/spawners have been made
    std::unordered_set<int64_t> dirtyChunks;    // meshed chunks to rebuild this frame
    mutable int64_t cachedKey = INT64_MIN;       // the last chunk GetBlock found, for speed
    mutable Chunk* cachedChunk = nullptr;
    int ambientCountdown = 0;
    std::set<ScheduledTick> scheduled;
    // Every pending (block position, block ID), scheduled or deferred, so a block isn't scheduled twice.
    std::set<std::pair<int64_t, int>> scheduledKeys;
    // Ticks that came due while their chunk was unloaded, by chunk key; they run once it's back.
    std::unordered_map<int64_t, std::vector<ScheduledTick>> deferredTicks;
    std::deque<TorchToggle> torchToggles;
    long long tickOrder = 0;
    long long tickCount = 0;
    double tickAccumulator = 0.0;
    int skyDarken = 0;
    JavaRandom random;
    int randomTickLcg;

    Texture2D atlasTexture;
    bool hasTexture;
    Shader chunkShader;
    int fogColorLoc, fogStartLoc, fogEndLoc, fogDensityLoc, skyDarkenLoc, cameraPosLoc, translucentPassLoc;
    bool hasStreamed;
    int lastPlayerChunkX;
    int lastPlayerChunkZ;

    SoundSystem* sounds = nullptr;
    DroppedItems* drops = nullptr;
    BlockParticles* particles = nullptr;
    Entities* entities = nullptr;
    Player* player = nullptr;

    // Background terrain generation. `jobs` and `finished` are shared with the
    // workers under `jobMutex`; everything else is main-thread only.
    std::vector<std::thread> workers;
    std::mutex jobMutex;
    std::condition_variable jobReady;
    std::deque<std::pair<int, int>> jobs;
    std::vector<std::unique_ptr<Chunk>> finished;
    bool stopping = false;
    std::unordered_set<int64_t> requested; // queued or being generated

    static int64_t Key(int cx, int cz);
    static std::pair<int64_t, int> TickKey(int x, int y, int z, int id);
    void MarkLightDirty(int x, int z);
    void UnloadChunkModels(LoadedChunk& loaded);
    void PlayCaveAmbience();
    static int64_t TileKey(int x, int y, int z);
    static int TileX(int64_t key);
    static int TileY(int64_t key);
    static int TileZ(int64_t key);

    void WorkerLoop();
    void RequestChunks(int pcx, int pcz);
    void UnloadFarChunks(int pcx, int pcz);
    void IntegrateFinished(int pcx, int pcz);
    void BuildModels(int pcx, int pcz, double budgetSeconds);
    bool HasAllNeighbors(int cx, int cz) const;
    void BuildModel(LoadedChunk& loaded);
    void RebuildDirty();
    void Tick();
    void TickScheduled();
    void TickRandomly();
    void TickTileEntities();
    void RandomDisplayTicks();

    // Light spreading between chunks (Minecraft's World.checkLightFor): brightening
    // floods outward from `seeds`; darkening first clears everything that was lit
    // through a removed cell, then refills from whatever still shines in.
    // Channel 0 is sky light, 1 is block light.
    struct LightCell { int x, y, z; int level; };
    Chunk* ChunkAt(int worldX, int worldZ) const;
    void SpreadLight(int channel, std::deque<LightCell>& seeds);
    void RemoveLight(int channel, std::deque<LightCell>& removed, std::deque<LightCell>& refill);
    void Relight(int x, int y, int z);
    void LightAcrossBorders(int cx, int cz);
    float BlockDensity(Vector3 from, const double min[3], const double max[3]) const;
    // Which loaded chunks the current 3D camera can see (call inside BeginMode3D).
    std::vector<const LoadedChunk*> VisibleChunks(bool translucent) const;
};
