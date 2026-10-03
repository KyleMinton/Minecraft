#include "Chunk.h"
#include "Items.h"
#include "JavaRandom.h"
#include "Light.h"
#include "TreeGen.h"
#include "World.h"

#include "stb_perlin.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <unordered_map>

// Implemented in BlockRender.cpp.
ChunkMeshes BuildChunkMeshes(const World& world, const Chunk& chunk);

namespace {

// Arbitrary large offsets so different noise "channels" (terrain shape, caves,
// regional hilliness) don't sample the same underlying gradient field at
// correlated coordinates.
constexpr float CAVE_NOISE_OFFSET = 91731.0f;

constexpr float TERRAIN_XZ_SCALE = 1.0f / 80.0f;
constexpr float TERRAIN_Y_SCALE = 1.0f / 48.0f;
constexpr int TERRAIN_OCTAVES = 4;
constexpr float TERRAIN_LACUNARITY = 2.0f;
constexpr float TERRAIN_GAIN = 0.5f;

constexpr float REGION_SCALE = 1.0f / 300.0f; // low-frequency: controls hilly vs. flat regions

constexpr float CAVE_SCALE = 1.0f / 40.0f;
constexpr int CAVE_OCTAVES = 3;
constexpr float CAVE_THRESHOLD = 0.62f; // higher = fewer/smaller caves

// Climate: two slow noise fields pick each area's biome.
constexpr float CLIMATE_SCALE = 1.0f / 260.0f;
constexpr float TEMPERATURE_OFFSET = 51237.0f;
constexpr float RAINFALL_OFFSET = -27113.0f;

constexpr int LAVA_LEVEL = 5; // cave air at or below this is lava, as in Beta's caves

enum Biome { BIOME_FOREST, BIOME_PLAINS, BIOME_TAIGA, BIOME_DESERT };

uint32_t HashCoords(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return h;
}

constexpr uint32_t BEDROCK_SEED = 4242u;

// Blobs of ore/gravel/dirt embedded in stone. Each is its own 3D noise field;
// wherever it exceeds `threshold` (and y <= maxY), stone becomes that block.
// Checked in order, so rarer deep ores win over common ones where they overlap.
struct Deposit {
    uint8_t type;
    int maxY;
    float threshold; // higher = rarer
    float scale;     // higher = smaller blobs
    float offset;    // decorrelates each deposit's noise field
};

constexpr Deposit DEPOSITS[] = {
    { BLOCK_DIAMOND_ORE,  14, 0.73f, 0.25f, 1000.0f },
    { BLOCK_LAPIS_ORE,    24, 0.74f, 0.25f, 7000.0f },
    { BLOCK_REDSTONE_ORE, 14, 0.66f, 0.22f, 8000.0f },
    { BLOCK_GOLD_ORE,     24, 0.70f, 0.25f, 2000.0f },
    { BLOCK_IRON_ORE,     44, 0.61f, 0.22f, 3000.0f },
    { BLOCK_COAL_ORE,     60, 0.56f, 0.20f, 4000.0f },
    { BLOCK_GRAVEL,       60, 0.55f, 0.09f, 5000.0f },
    { BLOCK_DIRT,         60, 0.55f, 0.09f, 6000.0f },
};

// Blocks laid under a surface block, top to bottom, before stone takes over.
constexpr uint8_t GRASS_LAYERS[] = { BLOCK_DIRT, BLOCK_DIRT, BLOCK_DIRT };
constexpr uint8_t BEACH_LAYERS[] = { BLOCK_SAND, BLOCK_SAND, BLOCK_SANDSTONE, BLOCK_SANDSTONE };
constexpr uint8_t SEABED_LAYERS[] = { BLOCK_GRAVEL, BLOCK_GRAVEL };

// The top block of a column whose highest solid block is at `y`.
uint8_t SurfaceBlock(int y, int biome) {
    if (y > Chunk::SEA_LEVEL + 1) return biome == BIOME_DESERT ? BLOCK_SAND : BLOCK_GRASS;
    if (y >= Chunk::SEA_LEVEL - 3) return BLOCK_SAND;
    return BLOCK_GRAVEL;
}

// splitmix64: spreads a seed's bits so nearby seeds give unrelated worlds.
uint64_t MixSeed(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Beta's per-chunk populate seed (ChunkProviderGenerate.populate).
int64_t ChunkSeed(int64_t worldSeed, int cx, int cz, int64_t salt) {
    JavaRandom r(worldSeed ^ salt);
    int64_t a = r.NextLong() / 2 * 2 + 1;
    int64_t b = r.NextLong() / 2 * 2 + 1;
    return ((int64_t)cx * a + (int64_t)cz * b) ^ worldSeed ^ salt;
}

bool IsSolid(uint8_t id) { return GetMaterialInfo(GetBlockDef(id).material).solid; }

// Beta's pickCheckLootItem (WorldGenDungeons).
ItemStack DungeonLoot(JavaRandom& r) {
    switch (r.NextInt(11)) {
        case 0: return ItemStack(ITEM_SADDLE, 1);
        case 1: return ItemStack(ITEM_IRON_INGOT, r.NextInt(4) + 1);
        case 2: return ItemStack(ITEM_BREAD, 1);
        case 3: return ItemStack(ITEM_WHEAT, r.NextInt(4) + 1);
        case 4: return ItemStack(ITEM_GUNPOWDER, r.NextInt(4) + 1);
        case 5: return ItemStack(ITEM_STRING, r.NextInt(4) + 1);
        case 6: return ItemStack(ITEM_BUCKET, 1);
        case 7: return r.NextInt(100) == 0 ? ItemStack(ITEM_GOLDEN_APPLE, 1) : ItemStack();
        case 8: return r.NextInt(2) == 0 ? ItemStack(ITEM_REDSTONE, r.NextInt(4) + 1) : ItemStack();
        case 9:
            if (r.NextInt(10) == 0) return ItemStack(r.NextInt(2) == 0 ? ITEM_RECORD_13 : ITEM_RECORD_CAT, 1);
            return ItemStack();
        default: return ItemStack(ITEM_DYE, 1, DYE_BROWN);
    }
}

} // namespace

uint8_t Chunk::StoneOrDeposit(int worldX, int y, int worldZ) const {
    for (const Deposit& d : DEPOSITS) {
        if (y > d.maxY) continue;
        float n = stb_perlin_noise3((worldX + noiseOffsetX + d.offset) * d.scale, (y + d.offset) * d.scale,
                                    (worldZ + noiseOffsetZ + d.offset) * d.scale, 0, 0, 0);
        if (n > d.threshold) return d.type;
    }
    return BLOCK_STONE;
}

Chunk::Chunk(int cx, int cz, int64_t worldSeed) : chunkX(cx), chunkZ(cz), seed(worldSeed) {
    // Offsets up to +-100,000 blocks keep the noise inputs small enough for float precision.
    uint64_t mixed = MixSeed((uint64_t)worldSeed);
    noiseOffsetX = (float)((int64_t)(mixed % 200000) - 100000) + (float)((mixed >> 40) & 0xFFFF) / 65536.0f;
    noiseOffsetZ = (float)((int64_t)((mixed >> 20) % 200000) - 100000) + (float)((mixed >> 24) & 0xFFFF) / 65536.0f;
    seedHash = (uint32_t)(mixed >> 32) ^ (uint32_t)mixed;
    memset(blocks, 0, sizeof(blocks));
    memset(meta, 0, sizeof(meta));
    GenerateTerrain();
    GenerateTrees();
    GenerateFeatures();
    ComputeLight();
}

// ---- Terrain

int Chunk::Climate(int worldX, int worldZ) const {
    float nx = worldX + noiseOffsetX, nz = worldZ + noiseOffsetZ;
    float temperature = stb_perlin_fbm_noise3((nx + TEMPERATURE_OFFSET) * CLIMATE_SCALE, 7.0f, (nz + TEMPERATURE_OFFSET) * CLIMATE_SCALE, 2.0f, 0.5f, 2);
    float rainfall = stb_perlin_fbm_noise3((nx + RAINFALL_OFFSET) * CLIMATE_SCALE, 3.0f, (nz + RAINFALL_OFFSET) * CLIMATE_SCALE, 2.0f, 0.5f, 2);
    if (temperature < -0.28f) return BIOME_TAIGA;
    if (temperature > 0.3f && rainfall < 0.0f) return BIOME_DESERT;
    if (rainfall < -0.15f) return BIOME_PLAINS;
    return BIOME_FOREST;
}

// Beta-style density-field terrain: a 3D fractal noise field biased by height
// (so it's reliably solid deep down and reliably open high up, with a rough,
// sometimes-overhanging transition band in between), carved by a second noise
// field for caves, with a low-frequency 2D field controlling how hilly each region is.
void Chunk::ColumnSolids(int worldX, int worldZ, bool solid[]) const {
    float noiseX = worldX + noiseOffsetX, noiseZ = worldZ + noiseOffsetZ;
    float regional = stb_perlin_fbm_noise3(noiseX * REGION_SCALE, 0.0f, noiseZ * REGION_SCALE, 2.0f, 0.5f, 3);
    float hillAmplitude = 14.0f + regional * 10.0f;
    for (int y = HEIGHT - 1; y >= 0; y--) {
        float base = stb_perlin_fbm_noise3(noiseX * TERRAIN_XZ_SCALE, y * TERRAIN_Y_SCALE, noiseZ * TERRAIN_XZ_SCALE,
                                            TERRAIN_LACUNARITY, TERRAIN_GAIN, TERRAIN_OCTAVES);
        float heightBias = (float)(y - SEA_LEVEL) / hillAmplitude;
        bool s = (base - heightBias) > 0.0f;
        if (s) {
            float cave = stb_perlin_fbm_noise3((noiseX + CAVE_NOISE_OFFSET) * CAVE_SCALE, y * CAVE_SCALE * 1.5f,
                                                 (noiseZ + CAVE_NOISE_OFFSET) * CAVE_SCALE, 2.0f, 0.5f, CAVE_OCTAVES);
            if (fabsf(cave) > CAVE_THRESHOLD) s = false;
        }
        if (y == 0) s = true;
        solid[y] = s;
    }
}

int Chunk::SurfaceHeight(int worldX, int worldZ) const {
    bool solid[HEIGHT];
    ColumnSolids(worldX, worldZ, solid);
    for (int y = HEIGHT - 1; y >= 0; y--) {
        if (solid[y]) return y;
    }
    return 0;
}

void Chunk::GenerateTerrain() {
    bool solid[HEIGHT];
    for (int x = 0; x < SIZE; x++) {
        for (int z = 0; z < SIZE; z++) {
            int worldX = chunkX * SIZE + x;
            int worldZ = chunkZ * SIZE + z;
            ColumnSolids(worldX, worldZ, solid);
            int biome = Climate(worldX, worldZ);

            bool foundSurface = false;
            const uint8_t* layers = nullptr;
            int layerCount = 0;
            int layerIndex = 0;
            bool prevWasOpen = true; // above the top of the chunk is open sky

            for (int y = HEIGHT - 1; y >= 0; y--) {
                bool s = solid[y];
                // Bedrock: a solid floor at y=0, thinning out randomly up to y=3.
                bool bedrock = y == 0 ||
                    (s && y <= 3 && HashCoords(worldX * 31 + y, worldZ, BEDROCK_SEED ^ seedHash) % 4 < (uint32_t)(4 - y));

                uint8_t type;
                if (bedrock) {
                    type = BLOCK_BEDROCK;
                } else if (s) {
                    if (prevWasOpen) {
                        if (!foundSurface) {
                            foundSurface = true;
                            layerIndex = 0;
                            type = SurfaceBlock(y, biome);
                            if (type == BLOCK_GRASS) {
                                layers = GRASS_LAYERS; layerCount = (int)sizeof(GRASS_LAYERS);
                            } else if (type == BLOCK_SAND) {
                                layers = BEACH_LAYERS; layerCount = (int)sizeof(BEACH_LAYERS);
                            } else {
                                layers = SEABED_LAYERS; layerCount = (int)sizeof(SEABED_LAYERS);
                            }
                        } else {
                            // A cave ceiling deeper down, not the true surface: plain stone,
                            // and no more surface layers below it.
                            type = StoneOrDeposit(worldX, y, worldZ);
                            layerIndex = layerCount;
                        }
                    } else if (layerIndex < layerCount) {
                        type = layers[layerIndex++];
                    } else {
                        type = StoneOrDeposit(worldX, y, worldZ);
                    }
                } else if (!foundSurface) {
                    // Open to the sky: the sea fills it up to sea level, frozen over in the cold.
                    if (y <= SEA_LEVEL) type = (y == SEA_LEVEL && biome == BIOME_TAIGA) ? BLOCK_ICE : BLOCK_WATER;
                    else type = BLOCK_AIR;
                } else {
                    type = y <= LAVA_LEVEL ? BLOCK_LAVA : BLOCK_AIR; // caves
                }

                blocks[x][y][z] = type;
                prevWasOpen = !s;
            }
        }
    }
}

// ---- Trees
//
// Beta's tree generators, run so that trees can cross chunk borders: every
// chunk's trees come from that chunk's own seed, and each is grown against an
// imagined world made from the terrain function alone (not the neighbor's real
// blocks, which may not exist yet), so whichever chunk draws a tree gets the
// same one. Each chunk keeps just the part of each nearby tree inside it.

namespace {

struct ColumnInfo {
    int surface;
    uint8_t top;
    int biome;
};

using ColumnFn = void (*)(const Chunk&, int, int, ColumnInfo&);
using BlockGrid = uint8_t (*)[Chunk::HEIGHT][Chunk::SIZE];

class GenTreeWorld final : public TreeWorld {
public:
    GenTreeWorld(const Chunk& c, std::unordered_map<int64_t, ColumnInfo>& cache, BlockGrid b, BlockGrid m, ColumnFn fn)
        : chunk(c), columns(cache), blocks(b), meta(m), column(fn) {}

    int Get(int x, int y, int z) const override {
        if (y < 0) return BLOCK_BEDROCK;
        if (y >= Chunk::HEIGHT) return BLOCK_AIR;
        auto placed = overlay.find(Key(x, y, z));
        if (placed != overlay.end()) return placed->second;
        const ColumnInfo& col = Column(x, z);
        if (y > col.surface) return y <= Chunk::SEA_LEVEL ? BLOCK_WATER : BLOCK_AIR;
        if (y == col.surface) return col.top;
        return BLOCK_DIRT;
    }

    void Set(int x, int y, int z, int id, int m) override {
        if (y < 0 || y >= Chunk::HEIGHT) return;
        overlay[Key(x, y, z)] = id;
        int lx = x - chunk.GetChunkX() * Chunk::SIZE, lz = z - chunk.GetChunkZ() * Chunk::SIZE;
        if (lx < 0 || lz < 0 || lx >= Chunk::SIZE || lz >= Chunk::SIZE) return;
        blocks[lx][y][lz] = (uint8_t)id;
        meta[lx][y][lz] = (uint8_t)m;
    }

    int Height() const override { return Chunk::HEIGHT; }

    const ColumnInfo& Column(int x, int z) const {
        int64_t key = ((int64_t)x << 32) ^ (uint32_t)z;
        auto it = columns.find(key);
        if (it != columns.end()) return it->second;
        ColumnInfo info;
        column(chunk, x, z, info);
        return columns.emplace(key, info).first->second;
    }

private:
    static int64_t Key(int x, int y, int z) {
        return ((int64_t)(x & 0x3FFFFFF) << 38) | ((int64_t)(z & 0x3FFFFFF) << 12) | (y & 0xFFF);
    }

    const Chunk& chunk;
    std::unordered_map<int64_t, ColumnInfo>& columns;
    BlockGrid blocks;
    BlockGrid meta;
    ColumnFn column;
    std::unordered_map<int64_t, int> overlay;
};

} // namespace

void Chunk::GenerateTrees() {
    constexpr int REACH = 7; // how far a tree can spread from its trunk (big oaks)
    std::unordered_map<int64_t, ColumnInfo> columns;
    ColumnFn columnFn = [](const Chunk& c, int x, int z, ColumnInfo& info) {
        info.surface = c.SurfaceHeight(x, z);
        info.biome = c.Climate(x, z);
        info.top = SurfaceBlock(info.surface, info.biome);
    };

    for (int ncx = chunkX - 1; ncx <= chunkX + 1; ncx++) {
        for (int ncz = chunkZ - 1; ncz <= chunkZ + 1; ncz++) {
            JavaRandom r(ChunkSeed(seed, ncx, ncz, 0x7EE5));
            int biome = Climate(ncx * SIZE + 8, ncz * SIZE + 8);
            int count = 0;
            if (biome == BIOME_FOREST) count = 4 + r.NextInt(4);
            else if (biome == BIOME_TAIGA) count = 3 + r.NextInt(3);
            else if (biome == BIOME_PLAINS) count = r.NextInt(8) == 0 ? 1 : 0;
            if (r.NextInt(10) == 0) count++;
            if (biome == BIOME_DESERT) count = 0;

            for (int i = 0; i < count; i++) {
                int x = ncx * SIZE + r.NextInt(SIZE);
                int z = ncz * SIZE + r.NextInt(SIZE);
                int64_t treeSeed = r.NextLong();
                int kind = r.NextInt(10);
                if (x < chunkX * SIZE - REACH || x >= (chunkX + 1) * SIZE + REACH) continue;
                if (z < chunkZ * SIZE - REACH || z >= (chunkZ + 1) * SIZE + REACH) continue;

                GenTreeWorld view(*this, columns, blocks, meta, columnFn);
                const ColumnInfo& col = view.Column(x, z);
                if (col.top != BLOCK_GRASS) continue;
                int y = col.surface + 1;
                JavaRandom treeRandom(treeSeed);
                if (biome == BIOME_TAIGA) GrowSpruceTree(view, treeRandom, x, y, z);
                else if (kind < 2) GrowBirchTree(view, treeRandom, x, y, z);
                else if (kind == 2) GrowBigOakTree(view, treeRandom, x, y, z);
                else GrowOakTree(view, treeRandom, x, y, z);
            }
        }
    }
}

// ---- Decoration (Beta's BiomeDecorator and WorldGen* features, kept inside the chunk)

namespace {

int TopY(BlockGrid blocks, int x, int z) {
    for (int y = Chunk::HEIGHT - 1; y >= 0; y--) {
        if (blocks[x][y][z] != BLOCK_AIR) return y;
    }
    return -1;
}

} // namespace

// WorldGenDungeons: a mossy cobblestone room with a spawner and up to two chests.
void Chunk::GenerateDungeons(JavaRandom& r) {
    for (int attempt = 0; attempt < 8; attempt++) {
        int cx = r.NextInt(8) + 4, cy = r.NextInt(HEIGHT - 8) + 2, cz = r.NextInt(8) + 4;
        const int height = 3;
        int rx = r.NextInt(2) + 2, rz = r.NextInt(2) + 2;
        auto at = [&](int x, int y, int z) -> uint8_t {
            if (x < 0 || z < 0 || x >= SIZE || z >= SIZE || y < 0 || y >= HEIGHT) return BLOCK_AIR;
            return blocks[x][y][z];
        };
        if (cx - rx - 1 < 0 || cx + rx + 1 >= SIZE || cz - rz - 1 < 0 || cz + rz + 1 >= SIZE) continue;
        int openings = 0;
        bool ok = true;
        for (int x = cx - rx - 1; x <= cx + rx + 1 && ok; x++) {
            for (int y = cy - 1; y <= cy + height + 1 && ok; y++) {
                for (int z = cz - rz - 1; z <= cz + rz + 1 && ok; z++) {
                    bool solid = IsSolid(at(x, y, z));
                    if (y == cy - 1 && !solid) ok = false;
                    if (y == cy + height + 1 && !solid) ok = false;
                    if ((x == cx - rx - 1 || x == cx + rx + 1 || z == cz - rz - 1 || z == cz + rz + 1) && y == cy &&
                        at(x, y, z) == BLOCK_AIR && at(x, y + 1, z) == BLOCK_AIR) {
                        openings++;
                    }
                }
            }
        }
        if (!ok || openings < 1 || openings > 5) continue;

        for (int x = cx - rx - 1; x <= cx + rx + 1; x++) {
            for (int y = cy + height; y >= cy - 1; y--) {
                for (int z = cz - rz - 1; z <= cz + rz + 1; z++) {
                    bool wall = x == cx - rx - 1 || y == cy - 1 || z == cz - rz - 1 || x == cx + rx + 1 || z == cz + rz + 1;
                    if (!wall) {
                        blocks[x][y][z] = BLOCK_AIR;
                    } else if (y >= 1 && !IsSolid(at(x, y - 1, z))) {
                        blocks[x][y][z] = BLOCK_AIR;
                    } else if (IsSolid(at(x, y, z))) {
                        blocks[x][y][z] = (y == cy - 1 && r.NextInt(4) != 0) ? BLOCK_MOSSY_COBBLESTONE : BLOCK_COBBLESTONE;
                    }
                    meta[x][y][z] = 0;
                }
            }
        }

        for (int chest = 0; chest < 2; chest++) {
            for (int tries = 0; tries < 3; tries++) {
                int x = cx + r.NextInt(rx * 2 + 1) - rx;
                int z = cz + r.NextInt(rz * 2 + 1) - rz;
                if (at(x, cy, z) != BLOCK_AIR) continue;
                int walls = (IsSolid(at(x - 1, cy, z)) ? 1 : 0) + (IsSolid(at(x + 1, cy, z)) ? 1 : 0) +
                            (IsSolid(at(x, cy, z - 1)) ? 1 : 0) + (IsSolid(at(x, cy, z + 1)) ? 1 : 0);
                if (walls != 1) continue;
                blocks[x][cy][z] = BLOCK_CHEST;
                meta[x][cy][z] = 0;
                GeneratedTile tile{ x, cy, z, TileKind::Chest, {} };
                for (int k = 0; k < 8; k++) {
                    ItemStack loot = DungeonLoot(r);
                    int slot = r.NextInt(27);
                    if (!loot.IsEmpty()) tile.items.emplace_back(slot, loot);
                }
                generatedTiles.push_back(tile);
                break;
            }
        }

        blocks[cx][cy][cz] = BLOCK_MOB_SPAWNER;
        meta[cx][cy][cz] = 0;
        generatedTiles.push_back(GeneratedTile{ cx, cy, cz, TileKind::MobSpawner, {} });
    }
}

void Chunk::GenerateFeatures() {
    JavaRandom r(ChunkSeed(seed, chunkX, chunkZ, 0xDEC0));
    int biome = Climate(chunkX * SIZE + 8, chunkZ * SIZE + 8);

    GenerateDungeons(r);

    auto inside = [](int x, int y, int z) { return x >= 0 && z >= 0 && x < SIZE && z < SIZE && y >= 1 && y < HEIGHT - 1; };

    // Clay: patches in the sand and gravel under shallow water (WorldGenClay).
    if (r.NextInt(2) == 0) {
        int x = r.NextInt(SIZE), z = r.NextInt(SIZE);
        int y = TopY(blocks, x, z);
        if (y > 0 && blocks[x][y][z] == BLOCK_WATER) {
            int floor = y;
            while (floor > 0 && blocks[x][floor][z] == BLOCK_WATER) floor--;
            int radius = r.NextInt(2) + 2;
            for (int dx = -radius; dx <= radius; dx++) {
                for (int dz = -radius; dz <= radius; dz++) {
                    if (dx * dx + dz * dz > radius * radius) continue;
                    for (int dy = -1; dy <= 0; dy++) {
                        int px = x + dx, py = floor + dy, pz = z + dz;
                        if (!inside(px, py, pz)) continue;
                        uint8_t b = blocks[px][py][pz];
                        if (b == BLOCK_SAND || b == BLOCK_GRAVEL || b == BLOCK_DIRT) blocks[px][py][pz] = BLOCK_CLAY;
                    }
                }
            }
        }
    }

    // A cluster of plants around a random spot (WorldGenFlowers, WorldGenTallGrass...).
    auto scatter = [&](int tries, int spread, auto&& place) {
        int x = r.NextInt(SIZE), z = r.NextInt(SIZE);
        int y = TopY(blocks, x, z) + 1;
        for (int i = 0; i < tries; i++) {
            int px = x + r.NextInt(spread) - r.NextInt(spread);
            int py = y + r.NextInt(4) - r.NextInt(4);
            int pz = z + r.NextInt(spread) - r.NextInt(spread);
            if (!inside(px, py, pz) || blocks[px][py][pz] != BLOCK_AIR) continue;
            place(px, py, pz);
        }
    };
    auto onGrass = [&](int x, int y, int z) { return blocks[x][y - 1][z] == BLOCK_GRASS || blocks[x][y - 1][z] == BLOCK_DIRT; };

    int grassPatches = biome == BIOME_PLAINS ? 10 : biome == BIOME_FOREST ? 2 : biome == BIOME_TAIGA ? 1 : 0;
    for (int i = 0; i < grassPatches; i++) {
        int kind = (biome == BIOME_TAIGA && r.NextInt(5) != 0) ? 2 : 1; // ferns in the taiga
        scatter(64, 8, [&](int x, int y, int z) {
            if (onGrass(x, y, z)) { blocks[x][y][z] = BLOCK_TALL_GRASS; meta[x][y][z] = (uint8_t)kind; }
        });
    }
    int flowerPatches = biome == BIOME_PLAINS ? 4 : biome == BIOME_FOREST ? 2 : 0;
    for (int i = 0; i < flowerPatches; i++) {
        scatter(64, 8, [&](int x, int y, int z) { if (onGrass(x, y, z)) blocks[x][y][z] = BLOCK_DANDELION; });
    }
    if (flowerPatches > 0 && r.NextInt(2) == 0) {
        scatter(64, 8, [&](int x, int y, int z) { if (onGrass(x, y, z)) blocks[x][y][z] = BLOCK_ROSE; });
    }

    // Mushrooms: only where it's dark (something solid overhead).
    auto shaded = [&](int x, int y, int z) {
        for (int yy = y + 1; yy < std::min(HEIGHT, y + 8); yy++) {
            if (BlockIsOpaqueCube(blocks[x][yy][z]) || blocks[x][yy][z] == BLOCK_LEAVES) return true;
        }
        return false;
    };
    for (int kind = 0; kind < 2; kind++) {
        if (r.NextInt(kind == 0 ? 4 : 8) != 0) continue;
        uint8_t mushroom = kind == 0 ? BLOCK_BROWN_MUSHROOM : BLOCK_RED_MUSHROOM;
        scatter(64, 8, [&](int x, int y, int z) {
            if (BlockIsOpaqueCube(blocks[x][y - 1][z]) && shaded(x, y, z)) blocks[x][y][z] = mushroom;
        });
    }

    // Sugar cane beside water (WorldGenReed).
    for (int i = 0; i < 10; i++) {
        scatter(20, 4, [&](int x, int y, int z) {
            uint8_t ground = blocks[x][y - 1][z];
            if (ground != BLOCK_GRASS && ground != BLOCK_DIRT && ground != BLOCK_SAND) return;
            bool water = false;
            const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto& o : d) {
                int nx = x + o[0], nz = z + o[1];
                if (nx < 0 || nz < 0 || nx >= SIZE || nz >= SIZE) continue;
                uint8_t n = blocks[nx][y - 1][nz];
                if (n == BLOCK_WATER || n == BLOCK_FLOWING_WATER) water = true;
            }
            if (!water) return;
            int h = 2 + r.NextInt(r.NextInt(3) + 1);
            for (int k = 0; k < h && y + k < HEIGHT - 1 && blocks[x][y + k][z] == BLOCK_AIR; k++) blocks[x][y + k][z] = BLOCK_REEDS;
        });
    }

    // Pumpkins (WorldGenPumpkin).
    if (r.NextInt(32) == 0) {
        scatter(64, 8, [&](int x, int y, int z) {
            if (blocks[x][y - 1][z] == BLOCK_GRASS) { blocks[x][y][z] = BLOCK_PUMPKIN; meta[x][y][z] = (uint8_t)r.NextInt(4); }
        });
    }

    if (biome == BIOME_DESERT) {
        // Cacti (WorldGenCactus): sand below, nothing solid beside.
        for (int i = 0; i < 10; i++) {
            scatter(10, 8, [&](int x, int y, int z) {
                int h = 1 + r.NextInt(r.NextInt(3) + 1);
                for (int k = 0; k < h; k++) {
                    int yy = y + k;
                    if (yy >= HEIGHT - 1 || blocks[x][yy][z] != BLOCK_AIR) break;
                    uint8_t below = blocks[x][yy - 1][z];
                    if (below != BLOCK_SAND && below != BLOCK_CACTUS) break;
                    bool clear = true;
                    const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                    for (const auto& o : d) {
                        int nx = x + o[0], nz = z + o[1];
                        if (nx >= 0 && nz >= 0 && nx < SIZE && nz < SIZE && IsSolid(blocks[nx][yy][nz])) clear = false;
                    }
                    if (!clear) break;
                    blocks[x][yy][z] = BLOCK_CACTUS;
                }
            });
        }
        for (int i = 0; i < 2; i++) {
            scatter(4, 8, [&](int x, int y, int z) { if (blocks[x][y - 1][z] == BLOCK_SAND) blocks[x][y][z] = BLOCK_DEAD_BUSH; });
        }
    }

    // Snow on everything in the cold.
    for (int x = 0; x < SIZE; x++) {
        for (int z = 0; z < SIZE; z++) {
            if (Climate(chunkX * SIZE + x, chunkZ * SIZE + z) != BIOME_TAIGA) continue;
            int y = TopY(blocks, x, z);
            if (y < 0 || y >= HEIGHT - 1) continue;
            uint8_t top = blocks[x][y][z];
            if (top == BLOCK_WATER) { blocks[x][y][z] = BLOCK_ICE; continue; }
            if (BlockIsOpaqueCube(top) || top == BLOCK_LEAVES) blocks[x][y + 1][z] = BLOCK_SNOW_LAYER;
        }
    }
}

// ---- Access

uint8_t Chunk::GetBlock(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= SIZE || y >= HEIGHT || z >= SIZE) return 0;
    return blocks[x][y][z];
}

int Chunk::GetMeta(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= SIZE || y >= HEIGHT || z >= SIZE) return 0;
    return meta[x][y][z];
}

void Chunk::SetBlock(int x, int y, int z, uint8_t type, int m) {
    if (x < 0 || y < 0 || z < 0 || x >= SIZE || y >= HEIGHT || z >= SIZE) return;
    blocks[x][y][z] = type;
    meta[x][y][z] = (uint8_t)(m & 15);
}

void Chunk::SetMeta(int x, int y, int z, int m) {
    if (x < 0 || y < 0 || z < 0 || x >= SIZE || y >= HEIGHT || z >= SIZE) return;
    meta[x][y][z] = (uint8_t)(m & 15);
}

// ---- Light
//
// Chunk.generateSkylightMap, then spreading: each column is lit from the top
// down until something opaque; below the first shade it fades a level per
// block. Glowing blocks light their surroundings on the second channel.
void Chunk::ComputeLight() {
    memset(skyLight, 0, sizeof(skyLight));
    memset(blockLight, 0, sizeof(blockLight));
    for (int x = 0; x < SIZE; x++) {
        for (int z = 0; z < SIZE; z++) {
            int level = MAX_LIGHT;
            for (int y = HEIGHT - 1; y >= 0; y--) {
                level = LightInto(level, BlockLightOpacity(blocks[x][y][z]), true);
                if (level <= 0) break;
                skyLight[x][y][z] = (uint8_t)level;
            }
        }
    }

    struct Cell { int x, y, z; };
    for (int channel = 0; channel < 2; channel++) {
        auto* light = channel == 0 ? skyLight : blockLight;
        std::deque<Cell> queue;
        for (int x = 0; x < SIZE; x++)
            for (int y = 0; y < HEIGHT; y++)
                for (int z = 0; z < SIZE; z++) {
                    if (channel == 1) {
                        int glow = BlockLightValue(blocks[x][y][z]);
                        if (glow > 0) {
                            light[x][y][z] = (uint8_t)glow;
                            queue.push_back({ x, y, z });
                        }
                    } else if (light[x][y][z] > 1) {
                        // Only cells brighter than a neighbor can light anything.
                        for (int dir = 0; dir < 6; dir++) {
                            int nx = x + LIGHT_DIRS[dir][0], ny = y + LIGHT_DIRS[dir][1], nz = z + LIGHT_DIRS[dir][2];
                            if (nx < 0 || ny < 0 || nz < 0 || nx >= SIZE || ny >= HEIGHT || nz >= SIZE) continue;
                            if (light[nx][ny][nz] + 1 < light[x][y][z]) { queue.push_back({ x, y, z }); break; }
                        }
                    }
                }
        while (!queue.empty()) {
            Cell c = queue.front();
            queue.pop_front();
            int level = light[c.x][c.y][c.z];
            if (level <= 1) continue;
            for (int dir = 0; dir < 6; dir++) {
                int nx = c.x + LIGHT_DIRS[dir][0], ny = c.y + LIGHT_DIRS[dir][1], nz = c.z + LIGHT_DIRS[dir][2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= SIZE || ny >= HEIGHT || nz >= SIZE) continue;
                int opacity = BlockLightOpacity(blocks[nx][ny][nz]);
                if (opacity >= MAX_LIGHT) continue;
                int lit = LightInto(level, opacity, channel == 0 && dir == LIGHT_DOWN);
                if (lit <= light[nx][ny][nz]) continue;
                light[nx][ny][nz] = (uint8_t)lit;
                queue.push_back({ nx, ny, nz });
            }
        }
    }
}

ChunkMeshes Chunk::GenerateMesh(const World& world) const {
    return BuildChunkMeshes(world, *this);
}
