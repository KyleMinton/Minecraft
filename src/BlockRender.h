#pragma once

#include "raylib.h"
#include "Block.h"

class World;
class Chunk;
struct ChunkMeshes;

// One corner of a block face. Positions are relative to wherever the caller's
// origin is; UVs are absolute terrain.png coordinates (0..1).
struct BlockVertex {
    float x, y, z;
    float u, v;
    Color color;     // tint x ambient occlusion
    float sky = 15;  // sky light level at this corner, 0-15
    float glow = 0;  // block light level at this corner, 0-15
};

// Receives the quads a block is made of, corners counter-clockwise seen from the front.
class QuadSink {
public:
    virtual ~QuadSink() = default;
    // `shade` is Beta's fixed per-direction face shading (1 top, 0.5 bottom, 0.8 / 0.6 sides),
    // which the world applies and item rendering replaces with its own lights.
    virtual void Quad(const BlockVertex v[4], Vector3 normal, Layer layer, float shade) = 0;
};

// Beta's RenderBlocks: the block at (x, y, z) as it looks in the world, lit by
// the world's light, with positions relative to `origin`.
void RenderWorldBlock(QuadSink& sink, const World& world, int x, int y, int z, Vector3 origin);

// The block (id, meta) as an item or entity (falling sand, TNT, a block being
// pushed): the unit cell at `offset`, lit evenly at the given light levels.
void RenderBlockModel(QuadSink& sink, int id, int meta, Vector3 offset, float sky = 15, float glow = 0);

// The block (id, meta) as a terrain-shader mesh in the unit cell at the origin,
// fully lit (inventory icons).
Mesh BuildBlockTerrainMesh(int id, int meta);

// A chunk's meshes: full opaque cubes greedy-merged, everything else block by block.
ChunkMeshes BuildChunkMeshes(const World& world, const Chunk& chunk);

// A tile's top-left in terrain.png UV space.
inline Vector2 TileUV(int tile) { return Vector2{ (tile & 15) / 16.0f, (tile >> 4) / 16.0f }; }
