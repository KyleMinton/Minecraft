#pragma once

#include "raylib.h"
#include "Block.h"
#include "TileEntity.h"
#include <cstdint>
#include <utility>
#include <vector>

class World;
class JavaRandom;

// A chunk's two meshes: solid and alpha-tested faces, and blended ones (water,
// ice, portals), which are drawn afterwards.
struct ChunkMeshes {
    Mesh opaque = { 0 };
    Mesh translucent = { 0 };
    bool hasOpaque = false;
    bool hasTranslucent = false;
};

// A tile entity world generation wants (a dungeon's chests and spawner). Chunks
// are generated off the main thread, so World creates these when the chunk arrives.
struct GeneratedTile {
    int x, y, z; // chunk-local
    TileKind kind;
    std::vector<std::pair<int, ItemStack>> items; // slot, stack
};

class Chunk {
public:
    static constexpr int SIZE = 16;    // horizontal extent (X and Z)
    static constexpr int HEIGHT = 64;  // vertical extent (Y)
    static constexpr int SEA_LEVEL = 32;
    // Saves only keep edited blocks and regenerate the rest from the seed, so any
    // change to what the generator makes changes existing worlds. Bump this with
    // any such change; level.dat records the version each world was made with.
    static constexpr int GENERATOR_VERSION = 1;

    // Generates the chunk's terrain for the given world seed.
    Chunk(int chunkX, int chunkZ, int64_t seed);

    BlockId GetBlock(int x, int y, int z) const;
    int GetMeta(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockId type, int meta = 0);
    void SetMeta(int x, int y, int z, int meta);

    // Sky light, 0-15 (Minecraft's skylight): 15 open to the sky, one less per
    // block it has to spread sideways or down past the first shade.
    uint8_t GetSkyLight(int x, int y, int z) const { return skyLight[x][y][z]; }
    void SetSkyLight(int x, int y, int z, uint8_t level) { skyLight[x][y][z] = level; }
    // Block light, 0-15: from torches, lava, glowstone and other glowing blocks.
    uint8_t GetBlockLight(int x, int y, int z) const { return blockLight[x][y][z]; }
    void SetBlockLight(int x, int y, int z, uint8_t level) { blockLight[x][y][z] = level; }
    uint8_t GetLight(int channel, int x, int y, int z) const { return channel == 0 ? skyLight[x][y][z] : blockLight[x][y][z]; }
    void SetLight(int channel, int x, int y, int z, uint8_t level) { (channel == 0 ? skyLight : blockLight)[x][y][z] = level; }

    // Lights this chunk on its own: sunlight straight down from the sky, glowing
    // blocks' light, both spread within the chunk. Light crossing into
    // neighbors is World's job.
    void ComputeLight();

    int GetChunkX() const { return chunkX; }
    int GetChunkZ() const { return chunkZ; }

    // Builds the chunk's meshes: full opaque cubes greedy-merged into as few
    // quads as possible, everything else (plants, torches, stairs, glass,
    // liquids...) drawn block by block. Needs `world` to see one block into
    // neighboring chunks so borders cull, shade and light correctly.
    ChunkMeshes GenerateMesh(const World& world) const;

    std::vector<GeneratedTile> generatedTiles;

private:
    void GenerateTerrain();
    void GenerateFeatures();
    void GenerateTrees();
    BlockId StoneOrDeposit(int worldX, int y, int worldZ) const;
    void GenerateDungeons(JavaRandom& random);
    int SurfaceHeight(int worldX, int worldZ) const; // terrain only, for trees reaching in from neighbors
    void ColumnSolids(int worldX, int worldZ, bool solid[]) const;
    int Climate(int worldX, int worldZ) const;

    int chunkX;
    int chunkZ;
    int64_t seed;
    // The seed moves every noise field to a different spot and reshuffles the
    // feature hashes, so each seed is a different world.
    float noiseOffsetX, noiseOffsetZ;
    uint32_t seedHash;
    BlockId blocks[SIZE][HEIGHT][SIZE];
    uint8_t meta[SIZE][HEIGHT][SIZE];
    uint8_t skyLight[SIZE][HEIGHT][SIZE];
    uint8_t blockLight[SIZE][HEIGHT][SIZE];
};
