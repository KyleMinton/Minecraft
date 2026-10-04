#include "BlockRender.h"
#include "Chunk.h"
#include "Colors.h"
#include "Fluids.h"
#include "Light.h"
#include "TileEntity.h"
#include "World.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

bool IsFlammable(const World& world, int x, int y, int z); // BlocksFluids.cpp

namespace {

BlockBox MakeBoxLocal(float x0, float y0, float z0, float x1, float y1, float z1) {
    BlockBox b;
    b.minX = x0; b.minY = y0; b.minZ = z0; b.maxX = x1; b.maxY = y1; b.maxZ = z1;
    return b;
}

constexpr float AO_CURVE[4] = { 0.45f, 0.65f, 0.8f, 1.0f };
constexpr float PX = 1.0f / 256.0f; // one texel of terrain.png

// ---- Directions (Minecraft sides used as directions: 0 -Y, 1 +Y, 2 -Z, 3 +Z, 4 -X, 5 +X)

int DirAxis(int dir) {
    static const int axis[6] = { 1, 1, 2, 2, 0, 0 };
    return axis[dir];
}
bool DirPositive(int dir) { return (dir & 1) != 0; }
Vector3 DirVec(int dir) { return Vector3{ (float)SIDE_OFFSET[dir][0], (float)SIDE_OFFSET[dir][1], (float)SIDE_OFFSET[dir][2] }; }
float AxisCoord(const float p[3], int dir) {
    float c = p[DirAxis(dir)];
    return DirPositive(dir) ? c : 1.0f - c;
}

// Which way a face's texture runs: its right and up directions.
constexpr int DEFAULT_RIGHT[6] = { SIDE_EAST, SIDE_EAST, SIDE_WEST, SIDE_EAST, SIDE_SOUTH, SIDE_NORTH };
constexpr int DEFAULT_UP[6] = { SIDE_NORTH, SIDE_NORTH, SIDE_UP, SIDE_UP, SIDE_UP, SIDE_UP };

float SideShade(int side) {
    if (side == SIDE_UP) return 1.0f;
    if (side == SIDE_DOWN) return 0.5f;
    return (side == SIDE_NORTH || side == SIDE_SOUTH) ? 0.8f : 0.6f;
}

Color Scale(Color c, float f) {
    return Color{ (unsigned char)std::clamp(c.r * f, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * f, 0.0f, 255.0f),
                  (unsigned char)std::clamp(c.b * f, 0.0f, 255.0f), c.a };
}

struct Light2 {
    float sky = 15, glow = 0;
};

// While a chunk is being meshed, the light and opacity of every cell it looks
// at (the chunk plus a block all round), worked out once each.
struct MeshCache {
    int x0 = 0, z0 = 0;
    static constexpr int W = Chunk::SIZE + 2, H = Chunk::HEIGHT + 2;
    float sky[W][H][W], glow[W][H][W];
    int8_t lit[W][H][W];    // light cached?
    int8_t opaque[W][H][W]; // -1 unknown
    uint8_t ids[W][H][W];   // every block, from y -1 to HEIGHT
    bool opaqueId[256], greedyId[256];
    bool Index(int x, int y, int z, int& i, int& j, int& k) const {
        i = x - x0 + 1; j = y + 1; k = z - z0 + 1;
        return i >= 0 && j >= 0 && k >= 0 && i < W && j < H && k < W;
    }
};
MeshCache* g_meshCache = nullptr;

Light2 RenderLightUncached(const World& world, int x, int y, int z);

Light2 RenderLight(const World& world, int x, int y, int z) {
    int i, j, k;
    if (g_meshCache != nullptr && g_meshCache->Index(x, y, z, i, j, k)) {
        if (!g_meshCache->lit[i][j][k]) {
            Light2 l = RenderLightUncached(world, x, y, z);
            g_meshCache->sky[i][j][k] = l.sky;
            g_meshCache->glow[i][j][k] = l.glow;
            g_meshCache->lit[i][j][k] = 1;
        }
        return Light2{ g_meshCache->sky[i][j][k], g_meshCache->glow[i][j][k] };
    }
    return RenderLightUncached(world, x, y, z);
}

bool CachedOpaque(const World& world, int x, int y, int z) {
    int i, j, k;
    if (g_meshCache != nullptr && g_meshCache->Index(x, y, z, i, j, k)) {
        if (g_meshCache->opaque[i][j][k] < 0) g_meshCache->opaque[i][j][k] = world.IsOpaqueCube(x, y, z) ? 1 : 0;
        return g_meshCache->opaque[i][j][k] == 1;
    }
    return world.IsOpaqueCube(x, y, z);
}

// World.getLightBrightness as rendering sees it: slabs, stairs and farmland
// block light but aren't full cubes, so they borrow their brightest neighbor's.
Light2 RenderLightUncached(const World& world, int x, int y, int z) {
    uint8_t id = world.GetBlock(x, y, z);
    if (id != BLOCK_AIR && !BlockIsOpaqueCube(id) && BlockLightOpacity(id) >= MAX_LIGHT) {
        Light2 best{ 0, (float)world.GetBlockLight(x, y, z) };
        for (const auto& o : SIDE_OFFSET) {
            best.sky = std::max(best.sky, (float)world.GetSkyLight(x + o[0], y + o[1], z + o[2]));
            best.glow = std::max(best.glow, (float)world.GetBlockLight(x + o[0], y + o[1], z + o[2]));
        }
        return best;
    }
    return Light2{ (float)world.GetSkyLight(x, y, z), (float)world.GetBlockLight(x, y, z) };
}

// Smooth lighting and ambient occlusion for a face corner: the cell in front of
// the face, the two beside it toward the corner, and the diagonal one.
struct CornerShade {
    int ao;
    float sky, glow;
};

CornerShade SmoothCorner(const World& world, const int n[3], int a1, int s1, int a2, int s2) {
    int c1[3] = { n[0], n[1], n[2] }, c2[3] = { n[0], n[1], n[2] }, cd[3] = { n[0], n[1], n[2] };
    c1[a1] += s1;
    c2[a2] += s2;
    cd[a1] += s1;
    cd[a2] += s2;
    bool o1 = CachedOpaque(world, c1[0], c1[1], c1[2]);
    bool o2 = CachedOpaque(world, c2[0], c2[1], c2[2]);
    bool od = CachedOpaque(world, cd[0], cd[1], cd[2]);
    CornerShade result;
    result.ao = (o1 && o2) ? 0 : 3 - ((o1 ? 1 : 0) + (o2 ? 1 : 0) + (od ? 1 : 0));

    Light2 lc = RenderLight(world, n[0], n[1], n[2]);
    Light2 l1 = RenderLight(world, c1[0], c1[1], c1[2]);
    Light2 l2 = RenderLight(world, c2[0], c2[1], c2[2]);
    Light2 ld = (o1 && o2) ? l1 : RenderLight(world, cd[0], cd[1], cd[2]);
    auto avg = [](float c, float a, float b, float d) {
        if (a == 0) a = c;
        if (b == 0) b = c;
        if (d == 0) d = c;
        return (c + a + b + d) / 4.0f;
    };
    result.sky = avg(lc.sky, l1.sky, l2.sky, ld.sky);
    result.glow = avg(lc.glow, l1.glow, l2.glow, ld.glow);
    return result;
}

// ---- Rendering context for one block

struct Ctx {
    QuadSink& sink;
    const World* world; // null when drawing an item or entity
    int x, y, z;
    Vector3 off;        // where the block's local (0,0,0) goes
    int id, meta;
    const Block& def;
    Light2 flat;        // light for items/entities

    uint8_t Get(int dx, int dy, int dz) const { return world ? world->GetBlock(x + dx, y + dy, z + dz) : (uint8_t)BLOCK_AIR; }
    int Meta(int dx, int dy, int dz) const { return world ? world->GetMeta(x + dx, y + dy, z + dz) : 0; }
    bool Opaque(int dx, int dy, int dz) const { return world && world->IsOpaqueCube(x + dx, y + dy, z + dz); }
    bool Normal(int dx, int dy, int dz) const { return world && world->IsNormalCube(x + dx, y + dy, z + dz); }
    int Tex(int side) const { return world ? def.WorldTexture(*world, x, y, z, side) : def.Texture(side, meta); }
    Color Tint(int side) const { return world ? def.WorldTint(*world, x, y, z, side) : def.ItemTint(meta); }
    BlockBox Bounds() const { return world ? def.Bounds(*world, x, y, z) : def.ItemBounds(meta); }
    // The light for parts of the block not on a face of its cell (plants, torches...).
    Light2 Own() const { return world ? RenderLight(*world, x, y, z) : flat; }
};

// ---- Faces of boxes

struct FaceStyle {
    int tex;
    Color tint = WHITE;
    int right = -1, up = -1; // -1: the default for the side
    bool mirror = false;
};

// One face of a box within the cell, textured with the part of the tile it covers.
void EmitBoxFace(Ctx& c, const BlockBox& b, int side, const FaceStyle& style, Layer layer, float inset = 0.0f) {
    float mn[3] = { b.minX, b.minY, b.minZ }, mx[3] = { b.maxX, b.maxY, b.maxZ };
    int n = DirAxis(side);
    bool np = DirPositive(side);
    int right = style.right >= 0 ? style.right : DEFAULT_RIGHT[side];
    int up = style.up >= 0 ? style.up : DEFAULT_UP[side];
    int ra = DirAxis(right), ua = DirAxis(up);
    Vector2 tile = TileUV(style.tex);
    bool onBoundary = np ? mx[n] >= 1.0f : mn[n] <= 0.0f;

    static const int cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 };
    float pos[4][3];
    BlockVertex v[4];
    for (int k = 0; k < 4; k++) {
        float* p = pos[k];
        p[n] = np ? mx[n] - inset : mn[n] + inset;
        p[ra] = ((cu[k] == 0) == DirPositive(right)) ? mn[ra] : mx[ra];
        p[ua] = ((cv[k] == 0) == DirPositive(up)) ? mn[ua] : mx[ua];
        float u = AxisCoord(p, right), vv = 1.0f - AxisCoord(p, up);
        if (style.mirror) u = 1.0f - u;
        v[k].x = c.off.x + p[0];
        v[k].y = c.off.y + p[1];
        v[k].z = c.off.z + p[2];
        v[k].u = tile.x + u / 16.0f;
        v[k].v = tile.y + vv / 16.0f;
    }

    // Lighting: smooth with AO where the face lies on the cell's boundary, else flat.
    int order[4] = { 0, 1, 2, 3 };
    Vector3 normal = DirVec(side);
    if (Vector3DotProduct(Vector3CrossProduct(DirVec(right), DirVec(up)), normal) < 0) std::swap(order[1], order[3]);
    BlockVertex out[4];
    for (int k = 0; k < 4; k++) {
        BlockVertex vert = v[order[k]];
        const float* p = pos[order[k]];
        Light2 light = c.flat;
        int ao = 3;
        if (c.world != nullptr) {
            int cell[3] = { c.x + SIDE_OFFSET[side][0], c.y + SIDE_OFFSET[side][1], c.z + SIDE_OFFSET[side][2] };
            if (onBoundary) {
                int t1 = (n + 1) % 3, t2 = (n + 2) % 3;
                CornerShade s = SmoothCorner(*c.world, cell, t1, p[t1] > 0.5f ? 1 : -1, t2, p[t2] > 0.5f ? 1 : -1);
                light = Light2{ s.sky, s.glow };
                ao = s.ao;
            } else {
                light = c.Own();
            }
        }
        vert.sky = light.sky;
        vert.glow = light.glow;
        vert.color = Scale(style.tint, AO_CURVE[ao]);
        out[k] = vert;
    }
    c.sink.Quad(out, normal, layer, SideShade(side));
}

enum class Cull { None, Neighbor, Block };

bool FaceVisible(const Ctx& c, const BlockBox& b, int side, Cull cull) {
    if (c.world == nullptr || cull == Cull::None) return true;
    float mn[3] = { b.minX, b.minY, b.minZ }, mx[3] = { b.maxX, b.maxY, b.maxZ };
    int n = DirAxis(side);
    bool onBoundary = DirPositive(side) ? mx[n] >= 1.0f : mn[n] <= 0.0f;
    if (!onBoundary) return true;
    int nx = c.x + SIDE_OFFSET[side][0], ny = c.y + SIDE_OFFSET[side][1], nz = c.z + SIDE_OFFSET[side][2];
    if (cull == Cull::Block) return c.def.ShouldSideBeRendered(*c.world, nx, ny, nz, side);
    return !c.world->IsOpaqueCube(nx, ny, nz);
}

// A box with the block's own textures on each side.
void RenderBox(Ctx& c, const BlockBox& b, Cull cull, Layer layer, int texOverride = -1) {
    for (int side = 0; side < 6; side++) {
        if (!FaceVisible(c, b, side, cull)) continue;
        FaceStyle style;
        style.tex = texOverride >= 0 ? texOverride : c.Tex(side);
        style.tint = c.Tint(side);
        EmitBoxFace(c, b, side, style, layer);
    }
}

// ---- Free-form quads (plants, torches, rails...)

// Four corners (block-local) with their UVs, lit flat with the block's own light.
void FreeQuad(Ctx& c, const Vector3 p[4], const Vector2 uv[4], Color tint, Layer layer, bool doubleSided, Light2 light) {
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p[1], p[0]), Vector3Subtract(p[2], p[0])));
    BlockVertex v[4];
    for (int k = 0; k < 4; k++) {
        v[k].x = c.off.x + p[k].x;
        v[k].y = c.off.y + p[k].y;
        v[k].z = c.off.z + p[k].z;
        v[k].u = uv[k].x;
        v[k].v = uv[k].y;
        v[k].color = tint;
        v[k].sky = light.sky;
        v[k].glow = light.glow;
    }
    c.sink.Quad(v, normal, layer, 1.0f);
    if (doubleSided) {
        BlockVertex r[4] = { v[3], v[2], v[1], v[0] };
        c.sink.Quad(r, Vector3Negate(normal), layer, 1.0f);
    }
}

// The whole tile over a quad whose corners run top-left, bottom-left, bottom-right, top-right.
void TileCorners(int tex, Vector2 uv[4], float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    Vector2 t = TileUV(tex);
    uv[0] = { t.x + u0 / 16.0f, t.y + v0 / 16.0f };
    uv[1] = { t.x + u0 / 16.0f, t.y + v1 / 16.0f };
    uv[2] = { t.x + u1 / 16.0f, t.y + v1 / 16.0f };
    uv[3] = { t.x + u1 / 16.0f, t.y + v0 / 16.0f };
}

// RenderBlocks.renderCrossedSquares
void RenderCross(Ctx& c, int tex, Color tint, float yOffset = 0.0f, float height = 1.0f, float v0 = 0.0f) {
    const float lo = 0.5f - 0.45f, hi = 0.5f + 0.45f;
    Vector2 uv[4];
    TileCorners(tex, uv, 0, v0, 1, v0 + height);
    float top = yOffset + height, bot = yOffset;
    Light2 light = c.Own();
    Vector3 a[4] = { { lo, top, lo }, { lo, bot, lo }, { hi, bot, hi }, { hi, top, hi } };
    Vector3 b[4] = { { lo, top, hi }, { lo, bot, hi }, { hi, bot, lo }, { hi, top, lo } };
    FreeQuad(c, a, uv, tint, Layer::Cutout, true, light);
    FreeQuad(c, b, uv, tint, Layer::Cutout, true, light);
}

// RenderBlocks.renderBlockCrops: a # of four planes.
void RenderCrops(Ctx& c, int tex, Color tint) {
    Vector2 uv[4];
    TileCorners(tex, uv);
    Light2 light = c.Own();
    const float bot = -0.0625f, top = 1.0f - 0.0625f;
    for (float d : { 0.25f, 0.75f }) {
        Vector3 a[4] = { { d, top, 0 }, { d, bot, 0 }, { d, bot, 1 }, { d, top, 1 } };
        Vector3 b[4] = { { 0, top, d }, { 0, bot, d }, { 1, bot, d }, { 1, top, d } };
        FreeQuad(c, a, uv, tint, Layer::Cutout, true, light);
        FreeQuad(c, b, uv, tint, Layer::Cutout, true, light);
    }
}

// RenderBlocks.renderTorchAtAngle: a torch at (px, py, pz) (block-local), leaning so
// its foot is (dx, dz) from its top.
void RenderTorchAtAngle(Ctx& c, int tex, float px, float py, float pz, float dx, float dz) {
    Vector2 t = TileUV(tex);
    float f = t.x, f1 = t.x + 15.99f * PX, f2 = t.y, f3 = t.y + 15.99f * PX;
    float d5 = f + 7 * PX, d6 = f2 + 6 * PX, d7 = f + 9 * PX, d8 = f2 + 8 * PX;
    float x = px + 0.5f, z = pz + 0.5f, y = py;
    float d9 = x - 0.5f, d10 = x + 0.5f, d11 = z - 0.5f, d12 = z + 0.5f;
    const float w = 0.0625f, h = 0.625f;
    Light2 light = c.Own();
    Color white = WHITE;

    float tx = x + dx * (1 - h), tz = z + dz * (1 - h);
    Vector3 top[4] = { { tx - w, y + h, tz - w }, { tx - w, y + h, tz + w }, { tx + w, y + h, tz + w }, { tx + w, y + h, tz - w } };
    Vector2 topUV[4] = { { d5, d6 }, { d5, d8 }, { d7, d8 }, { d7, d6 } };
    FreeQuad(c, top, topUV, white, Layer::Cutout, false, light);

    Vector2 uv[4] = { { f, f2 }, { f, f3 }, { f1, f3 }, { f1, f2 } };
    Vector3 s1[4] = { { x - w, y + 1, d11 }, { x - w + dx, y, d11 + dz }, { x - w + dx, y, d12 + dz }, { x - w, y + 1, d12 } };
    Vector3 s2[4] = { { x + w, y + 1, d12 }, { x + w + dx, y, d12 + dz }, { x + w + dx, y, d11 + dz }, { x + w, y + 1, d11 } };
    Vector3 s3[4] = { { d9, y + 1, z + w }, { d9 + dx, y, z + w + dz }, { d10 + dx, y, z + w + dz }, { d10, y + 1, z + w } };
    Vector3 s4[4] = { { d10, y + 1, z - w }, { d10 + dx, y, z - w + dz }, { d9 + dx, y, z - w + dz }, { d9, y + 1, z - w } };
    FreeQuad(c, s1, uv, white, Layer::Cutout, false, light);
    FreeQuad(c, s2, uv, white, Layer::Cutout, false, light);
    FreeQuad(c, s3, uv, white, Layer::Cutout, false, light);
    FreeQuad(c, s4, uv, white, Layer::Cutout, false, light);
}

void RenderTorch(Ctx& c) {
    int tex = c.Tex(SIDE_UP);
    int meta = c.world ? c.meta : 5;
    if (meta == 1) RenderTorchAtAngle(c, tex, -0.1f, 0.2f, 0, -0.4f, 0);
    else if (meta == 2) RenderTorchAtAngle(c, tex, 0.1f, 0.2f, 0, 0.4f, 0);
    else if (meta == 3) RenderTorchAtAngle(c, tex, 0, 0.2f, -0.1f, 0, -0.4f);
    else if (meta == 4) RenderTorchAtAngle(c, tex, 0, 0.2f, 0.1f, 0, 0.4f);
    else RenderTorchAtAngle(c, tex, 0, 0, 0, 0, 0);
}

// ---- Fire (RenderBlocks.renderBlockFire)

void RenderFire(Ctx& c) {
    Light2 light = c.Own();
    const float h = 1.4f;
    Color white = WHITE;
    auto quad = [&](int tex, Vector3 a, Vector3 b, Vector3 d, Vector3 e, bool flip) {
        Vector2 t = TileUV(tex);
        float u1 = t.x, u2 = t.x + 15.99f * PX, v1 = t.y, v2 = t.y + 15.99f * PX;
        if (flip) std::swap(u1, u2);
        Vector3 p[4] = { a, b, d, e };
        Vector2 uv[4] = { { u2, v1 }, { u2, v2 }, { u1, v2 }, { u1, v1 } };
        FreeQuad(c, p, uv, white, Layer::Cutout, true, light);
    };
    const int tex = 31, tex2 = 47;
    bool floor = c.world == nullptr || c.Normal(0, -1, 0) || IsFlammable(*c.world, c.x, c.y - 1, c.z);
    if (floor) {
        float d1 = 0.5f + 0.2f, d3 = 0.5f - 0.2f, d5 = 0.5f + 0.2f, d7 = 0.5f - 0.2f;
        float d9 = 0.5f - 0.3f, d11 = 0.5f + 0.3f, d13 = 0.5f - 0.3f, d15 = 0.5f + 0.3f;
        quad(tex, { d9, h, 1 }, { d1, 0, 1 }, { d1, 0, 0 }, { d9, h, 0 }, false);
        quad(tex, { d11, h, 0 }, { d3, 0, 0 }, { d3, 0, 1 }, { d11, h, 1 }, false);
        quad(tex2, { 1, h, d15 }, { 1, 0, d7 }, { 0, 0, d7 }, { 0, h, d15 }, false);
        quad(tex2, { 0, h, d13 }, { 0, 0, d5 }, { 1, 0, d5 }, { 1, h, d13 }, false);
        d1 = 0.0f; d3 = 1.0f; d5 = 0.0f; d7 = 1.0f;
        d9 = 0.5f - 0.4f; d11 = 0.5f + 0.4f; d13 = 0.5f - 0.4f; d15 = 0.5f + 0.4f;
        quad(tex2, { d9, h, 0 }, { d1, 0, 0 }, { d1, 0, 1 }, { d9, h, 1 }, true);
        quad(tex2, { d11, h, 1 }, { d3, 0, 1 }, { d3, 0, 0 }, { d11, h, 0 }, true);
        quad(tex, { 0, h, d15 }, { 0, 0, d7 }, { 1, 0, d7 }, { 1, h, d15 }, true);
        quad(tex, { 1, h, d13 }, { 1, 0, d5 }, { 0, 0, d5 }, { 0, h, d13 }, true);
        return;
    }
    // Climbing whatever burns beside it.
    const float lean = 0.2f, lift = 0.0625f;
    if (IsFlammable(*c.world, c.x - 1, c.y, c.z)) quad(tex, { lean, h + lift, 1 }, { 0, lift, 1 }, { 0, lift, 0 }, { lean, h + lift, 0 }, false);
    if (IsFlammable(*c.world, c.x + 1, c.y, c.z)) quad(tex, { 1 - lean, h + lift, 0 }, { 1, lift, 0 }, { 1, lift, 1 }, { 1 - lean, h + lift, 1 }, false);
    if (IsFlammable(*c.world, c.x, c.y, c.z - 1)) quad(tex2, { 0, h + lift, lean }, { 0, lift, 0 }, { 1, lift, 0 }, { 1, h + lift, lean }, false);
    if (IsFlammable(*c.world, c.x, c.y, c.z + 1)) quad(tex2, { 1, h + lift, 1 - lean }, { 1, lift, 1 }, { 0, lift, 1 }, { 0, h + lift, 1 - lean }, false);
    if (IsFlammable(*c.world, c.x, c.y + 1, c.z)) {
        quad(tex, { 1, 1, 0 }, { 0.5f, 1 - 0.5f, 0 }, { 0.5f, 1 - 0.5f, 1 }, { 1, 1, 1 }, false);
        quad(tex2, { 0, 1, 1 }, { 0.5f, 1 - 0.5f, 1 }, { 0.5f, 1 - 0.5f, 0 }, { 0, 1, 0 }, false);
    }
}

// ---- Liquids (RenderBlocks.renderBlockFluids)

void RenderFluid(Ctx& c) {
    const World& world = *c.world;
    BlockMaterial material = c.def.material;
    Layer layer = c.def.layer;
    bool renderTop = c.Get(0, 1, 0) == BLOCK_AIR || GetBlockDef(c.Get(0, 1, 0)).material != material;
    if (renderTop && c.Opaque(0, 1, 0)) renderTop = false;
    float h00 = FluidCornerHeight(world, c.x, c.y, c.z, material);
    float h01 = FluidCornerHeight(world, c.x, c.y, c.z + 1, material);
    float h11 = FluidCornerHeight(world, c.x + 1, c.y, c.z + 1, material);
    float h10 = FluidCornerHeight(world, c.x + 1, c.y, c.z, material);
    Light2 own = c.Own();
    Color white = WHITE;

    if (renderTop) {
        int tex = c.def.Texture(SIDE_UP, c.meta);
        double angle = FluidFlowAngle(world, c.x, c.y, c.z, material);
        if (angle > -999.0) tex = c.def.Texture(SIDE_NORTH, c.meta);
        Vector2 t = TileUV(tex);
        float cu = t.x + 8 * PX, cv = t.y + 8 * PX;
        if (angle < -999.0) angle = 0.0;
        else { cu = t.x + 16 * PX; cv = t.y + 16 * PX; }
        float s = (float)std::sin(angle) * 8 * PX, co = (float)std::cos(angle) * 8 * PX;
        Vector3 p[4] = { { 0, h00, 0 }, { 0, h01, 1 }, { 1, h11, 1 }, { 1, h10, 0 } };
        Vector2 uv[4] = { { cu - co - s, cv - co + s }, { cu - co + s, cv + co + s }, { cu + co + s, cv + co - s }, { cu + co - s, cv - co - s } };
        Light2 light = own;
        if (world.GetBlock(c.x, c.y + 1, c.z) == BLOCK_AIR) {
            Light2 above = RenderLight(world, c.x, c.y + 1, c.z);
            light.sky = std::max(light.sky, above.sky);
            light.glow = std::max(light.glow, above.glow);
        }
        BlockVertex v[4];
        for (int k = 0; k < 4; k++) {
            v[k] = BlockVertex{ c.off.x + p[k].x, c.off.y + p[k].y, c.off.z + p[k].z, uv[k].x, uv[k].y, white, light.sky, light.glow };
        }
        c.sink.Quad(v, Vector3{ 0, 1, 0 }, layer, 1.0f);
        if (material == BlockMaterial::Water) {
            BlockVertex r[4] = { v[3], v[2], v[1], v[0] };
            c.sink.Quad(r, Vector3{ 0, -1, 0 }, layer, 0.5f);
        }
    }

    bool below = c.Get(0, -1, 0) != BLOCK_AIR && GetBlockDef(c.Get(0, -1, 0)).material == material;
    if (!below && !c.Opaque(0, -1, 0)) {
        int tex = c.def.Texture(SIDE_DOWN, c.meta);
        Vector2 uv[4];
        TileCorners(tex, uv);
        Vector3 p[4] = { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } };
        Light2 light = RenderLight(world, c.x, c.y - 1, c.z);
        BlockVertex v[4];
        for (int k = 0; k < 4; k++) v[k] = BlockVertex{ c.off.x + p[k].x, c.off.y + p[k].y, c.off.z + p[k].z, uv[k].x, uv[k].y, white, light.sky, light.glow };
        c.sink.Quad(v, Vector3{ 0, -1, 0 }, layer, 0.5f);
    }

    // Sides: from each corner's surface height down to the bottom, flowing texture.
    struct SideInfo { int side; int dx, dz; float ax, az, bx, bz; float ha, hb; };
    const SideInfo sides[4] = {
        { SIDE_NORTH, 0, -1, 1, 0, 0, 0, h10, h00 },
        { SIDE_SOUTH, 0, 1, 0, 1, 1, 1, h01, h11 },
        { SIDE_WEST, -1, 0, 0, 0, 0, 1, h00, h01 },
        { SIDE_EAST, 1, 0, 1, 1, 1, 0, h11, h10 },
    };
    int sideTex = c.def.Texture(SIDE_NORTH, c.meta);
    Vector2 t = TileUV(sideTex);
    for (const SideInfo& s : sides) {
        uint8_t n = c.Get(s.dx, 0, s.dz);
        if (n != BLOCK_AIR && GetBlockDef(n).material == material) continue;
        if (c.Opaque(s.dx, 0, s.dz)) continue;
        float u1 = t.x, u2 = t.x + 16 * PX;
        float va = t.y + (1.0f - s.ha) * 16 * 0.5f * PX, vb = t.y + (1.0f - s.hb) * 16 * 0.5f * PX, vBottom = t.y + 8 * PX;
        Light2 light = RenderLight(world, c.x + s.dx, c.y, c.z + s.dz);
        Vector3 p[4] = { { s.ax, s.ha, s.az }, { s.ax, 0, s.az }, { s.bx, 0, s.bz }, { s.bx, s.hb, s.bz } };
        Vector2 uv[4] = { { u1, va }, { u1, vBottom }, { u2, vBottom }, { u2, vb } };
        BlockVertex v[4];
        for (int k = 0; k < 4; k++) v[k] = BlockVertex{ c.off.x + p[k].x, c.off.y + p[k].y, c.off.z + p[k].z, uv[k].x, uv[k].y, white, light.sky, light.glow };
        c.sink.Quad(v, DirVec(s.side), layer, SideShade(s.side));
        if (material == BlockMaterial::Water) {
            BlockVertex r[4] = { v[3], v[2], v[1], v[0] };
            c.sink.Quad(r, Vector3Negate(DirVec(s.side)), layer, SideShade(s.side));
        }
    }
}

// ---- Redstone dust (RenderBlocks.renderBlockRedstoneWire)

bool WireConnectsTo(const World& world, int x, int y, int z, int dir) {
    int id = world.GetBlock(x, y, z);
    if (id == BLOCK_REDSTONE_WIRE) return true;
    if (id == BLOCK_AIR) return false;
    if (id == BLOCK_UNPOWERED_REPEATER || id == BLOCK_POWERED_REPEATER) {
        static const int opposite[4] = { 2, 3, 0, 1 };
        int facing = world.GetMeta(x, y, z) & 3;
        return dir == facing || dir == opposite[facing];
    }
    return GetBlockDef((uint8_t)id).CanProvidePower() && dir != -1;
}

void RenderWire(Ctx& c) {
    Light2 light = c.Own();
    int power = c.world ? c.meta : 0;
    float f = power / 15.0f;
    float r = power == 0 ? 0.3f : f * 0.6f + 0.4f;
    float g = std::max(0.0f, f * f * 0.7f - 0.5f);
    float b = std::max(0.0f, f * f * 0.6f - 0.7f);
    Color tint = { (unsigned char)(r * 255), (unsigned char)(g * 255), (unsigned char)(b * 255), 255 };

    bool w = false, e = false, n = false, s = false, upW = false, upE = false, upN = false, upS = false;
    if (c.world) {
        const World& world = *c.world;
        int x = c.x, y = c.y, z = c.z;
        w = WireConnectsTo(world, x - 1, y, z, 1) || (!world.IsNormalCube(x - 1, y, z) && WireConnectsTo(world, x - 1, y - 1, z, -1));
        e = WireConnectsTo(world, x + 1, y, z, 3) || (!world.IsNormalCube(x + 1, y, z) && WireConnectsTo(world, x + 1, y - 1, z, -1));
        n = WireConnectsTo(world, x, y, z - 1, 2) || (!world.IsNormalCube(x, y, z - 1) && WireConnectsTo(world, x, y - 1, z - 1, -1));
        s = WireConnectsTo(world, x, y, z + 1, 0) || (!world.IsNormalCube(x, y, z + 1) && WireConnectsTo(world, x, y - 1, z + 1, -1));
        if (!world.IsNormalCube(x, y + 1, z)) {
            if (world.IsNormalCube(x - 1, y, z) && WireConnectsTo(world, x - 1, y + 1, z, -1)) w = upW = true;
            if (world.IsNormalCube(x + 1, y, z) && WireConnectsTo(world, x + 1, y + 1, z, -1)) e = upE = true;
            if (world.IsNormalCube(x, y, z - 1) && WireConnectsTo(world, x, y + 1, z - 1, -1)) n = upN = true;
            if (world.IsNormalCube(x, y, z + 1) && WireConnectsTo(world, x, y + 1, z + 1, -1)) s = upS = true;
        }
    }

    const float h = 0.015625f;
    int type = 0;
    if ((w || e) && !n && !s) type = 1;
    if ((n || s) && !e && !w) type = 2;
    if (type == 0) {
        float x0 = 0, x1 = 1, z0 = 0, z1 = 1;
        if (w || e || n || s) {
            const float px = 5.0f / 16.0f;
            if (!w) x0 += px;
            if (!e) x1 -= px;
            if (!n) z0 += px;
            if (!s) z1 -= px;
        }
        Vector2 t = TileUV(164);
        Vector3 p[4] = { { x0, h, z0 }, { x0, h, z1 }, { x1, h, z1 }, { x1, h, z0 } };
        Vector2 uv[4] = { { t.x + x0 / 16, t.y + z0 / 16 }, { t.x + x0 / 16, t.y + z1 / 16 }, { t.x + x1 / 16, t.y + z1 / 16 }, { t.x + x1 / 16, t.y + z0 / 16 } };
        FreeQuad(c, p, uv, tint, Layer::Cutout, false, light);
    } else {
        Vector2 t = TileUV(165);
        Vector3 p[4] = { { 0, h, 0 }, { 0, h, 1 }, { 1, h, 1 }, { 1, h, 0 } };
        // The line runs along the texture's U; turn it to run north-south if needed.
        Vector2 uv[4];
        if (type == 1) {
            uv[0] = { t.x, t.y }; uv[1] = { t.x, t.y + 1 / 16.0f }; uv[2] = { t.x + 1 / 16.0f, t.y + 1 / 16.0f }; uv[3] = { t.x + 1 / 16.0f, t.y };
        } else {
            uv[0] = { t.x, t.y + 1 / 16.0f }; uv[1] = { t.x + 1 / 16.0f, t.y + 1 / 16.0f }; uv[2] = { t.x + 1 / 16.0f, t.y }; uv[3] = { t.x, t.y };
        }
        FreeQuad(c, p, uv, tint, Layer::Cutout, false, light);
    }

    // Up the side of a block to wire on top of it.
    Vector2 t = TileUV(165);
    const float in = 0.015625f;
    Vector2 vuv[4] = { { t.x + 1 / 16.0f, t.y }, { t.x, t.y }, { t.x, t.y + 1 / 16.0f }, { t.x + 1 / 16.0f, t.y + 1 / 16.0f } };
    if (upW) { Vector3 p[4] = { { in, 1, 1 }, { in, 0, 1 }, { in, 0, 0 }, { in, 1, 0 } }; FreeQuad(c, p, vuv, tint, Layer::Cutout, true, light); }
    if (upE) { Vector3 p[4] = { { 1 - in, 1, 0 }, { 1 - in, 0, 0 }, { 1 - in, 0, 1 }, { 1 - in, 1, 1 } }; FreeQuad(c, p, vuv, tint, Layer::Cutout, true, light); }
    if (upN) { Vector3 p[4] = { { 0, 1, in }, { 0, 0, in }, { 1, 0, in }, { 1, 1, in } }; FreeQuad(c, p, vuv, tint, Layer::Cutout, true, light); }
    if (upS) { Vector3 p[4] = { { 1, 1, 1 - in }, { 1, 0, 1 - in }, { 0, 0, 1 - in }, { 0, 1, 1 - in } }; FreeQuad(c, p, vuv, tint, Layer::Cutout, true, light); }
}

// ---- Rails (RenderBlocks.renderBlockMinecartTrack)

void RenderRail(Ctx& c) {
    int meta = c.world ? c.meta : 0;
    int tex = c.Tex(SIDE_UP);
    if (c.id != BLOCK_RAIL) meta &= 7;
    Vector2 t = TileUV(tex);
    float u1 = t.x, u2 = t.x + 15.99f * PX, v1 = t.y, v2 = t.y + 15.99f * PX;
    float x0 = 1, x1 = 1, x2 = 0, x3 = 0;
    float z0 = 0, z1 = 1, z2 = 1, z3 = 0;
    if (meta == 1 || meta == 2 || meta == 3 || meta == 7) {
        x0 = x3 = 1; x1 = x2 = 0; z0 = z1 = 1; z2 = z3 = 0;
    } else if (meta == 8) {
        x0 = x1 = 0; x2 = x3 = 1; z0 = z3 = 1; z1 = z2 = 0;
    } else if (meta == 9) {
        x0 = x3 = 0; x1 = x2 = 1; z0 = z1 = 0; z2 = z3 = 1;
    }
    const float base = 0.0625f;
    float y0 = base, y1 = base, y2 = base, y3 = base;
    if (meta == 2 || meta == 4) { y0 += 1; y3 += 1; }
    else if (meta == 3 || meta == 5) { y1 += 1; y2 += 1; }
    Vector3 p[4] = { { x0, y0, z0 }, { x1, y1, z1 }, { x2, y2, z2 }, { x3, y3, z3 } };
    Vector2 uv[4] = { { u2, v1 }, { u2, v2 }, { u1, v2 }, { u1, v1 } };
    // Beta's order faces down; flip it to face up, and draw the underside too.
    Vector3 q[4] = { p[3], p[2], p[1], p[0] };
    Vector2 quv[4] = { uv[3], uv[2], uv[1], uv[0] };
    FreeQuad(c, q, quv, WHITE, Layer::Cutout, true, c.Own());
}

// ---- Ladders and vines: flat against a wall.

void WallPlane(Ctx& c, int wallSide, int tex, Color tint) {
    const float d = 0.05f;
    Vector2 uv[4];
    TileCorners(tex, uv);
    Vector3 p[4];
    switch (wallSide) {
        case SIDE_SOUTH: { Vector3 q[4] = { { 1, 1, 1 - d }, { 1, 0, 1 - d }, { 0, 0, 1 - d }, { 0, 1, 1 - d } }; std::copy(q, q + 4, p); break; }
        case SIDE_NORTH: { Vector3 q[4] = { { 0, 1, d }, { 0, 0, d }, { 1, 0, d }, { 1, 1, d } }; std::copy(q, q + 4, p); break; }
        case SIDE_EAST: { Vector3 q[4] = { { 1 - d, 1, 0 }, { 1 - d, 0, 0 }, { 1 - d, 0, 1 }, { 1 - d, 1, 1 } }; std::copy(q, q + 4, p); break; }
        case SIDE_WEST: { Vector3 q[4] = { { d, 1, 1 }, { d, 0, 1 }, { d, 0, 0 }, { d, 1, 0 } }; std::copy(q, q + 4, p); break; }
        default: { Vector3 q[4] = { { 0, 1 - d, 0 }, { 0, 1 - d, 1 }, { 1, 1 - d, 1 }, { 1, 1 - d, 0 } }; std::copy(q, q + 4, p); break; }
    }
    FreeQuad(c, p, uv, tint, Layer::Cutout, true, c.Own());
}

void RenderLadder(Ctx& c) {
    int meta = c.world ? c.meta : 2;
    int wall = meta == 2 ? SIDE_SOUTH : meta == 3 ? SIDE_NORTH : meta == 4 ? SIDE_EAST : SIDE_WEST;
    WallPlane(c, wall, c.Tex(SIDE_NORTH), WHITE);
}

void RenderVine(Ctx& c) {
    int meta = c.world ? c.meta : 1;
    int tex = c.Tex(SIDE_NORTH);
    Color tint = c.Tint(SIDE_NORTH);
    if (meta & 1) WallPlane(c, SIDE_SOUTH, tex, tint);
    if (meta & 2) WallPlane(c, SIDE_WEST, tex, tint);
    if (meta & 4) WallPlane(c, SIDE_NORTH, tex, tint);
    if (meta & 8) WallPlane(c, SIDE_EAST, tex, tint);
    if (c.world && c.Normal(0, 1, 0)) WallPlane(c, SIDE_UP, tex, tint);
}

// ---- Stairs, fences, gates: several boxes with the block's textures.

void RenderStairs(Ctx& c) {
    int meta = c.world ? c.meta & 3 : 0;
    BlockBox a, b;
    switch (meta) {
        case 0: a = MakeBoxLocal(0, 0, 0, 0.5f, 0.5f, 1); b = MakeBoxLocal(0.5f, 0, 0, 1, 1, 1); break;
        case 1: a = MakeBoxLocal(0, 0, 0, 0.5f, 1, 1); b = MakeBoxLocal(0.5f, 0, 0, 1, 0.5f, 1); break;
        case 2: a = MakeBoxLocal(0, 0, 0, 1, 0.5f, 0.5f); b = MakeBoxLocal(0, 0, 0.5f, 1, 1, 1); break;
        default: a = MakeBoxLocal(0, 0, 0, 1, 1, 0.5f); b = MakeBoxLocal(0, 0, 0.5f, 1, 0.5f, 1); break;
    }
    RenderBox(c, a, Cull::Neighbor, Layer::Opaque);
    RenderBox(c, b, Cull::Neighbor, Layer::Opaque);
}

bool FenceConnects(const Ctx& c, int dx, int dz) {
    int id = c.Get(dx, 0, dz);
    return id == BLOCK_FENCE || id == BLOCK_FENCE_GATE;
}

void RenderFence(Ctx& c) {
    if (c.world == nullptr) {
        // The item: two posts joined by two bars.
        RenderBox(c, MakeBoxLocal(0.375f, 0, 0, 0.625f, 1, 0.25f), Cull::None, Layer::Opaque);
        RenderBox(c, MakeBoxLocal(0.375f, 0, 0.75f, 0.625f, 1, 1), Cull::None, Layer::Opaque);
        RenderBox(c, MakeBoxLocal(0.4375f, 0.8125f, 0.25f, 0.5625f, 0.9375f, 0.75f), Cull::None, Layer::Opaque);
        RenderBox(c, MakeBoxLocal(0.4375f, 0.3125f, 0.25f, 0.5625f, 0.4375f, 0.75f), Cull::None, Layer::Opaque);
        return;
    }
    RenderBox(c, MakeBoxLocal(0.375f, 0, 0.375f, 0.625f, 1, 0.625f), Cull::Neighbor, Layer::Opaque);
    bool w = FenceConnects(c, -1, 0), e = FenceConnects(c, 1, 0), n = FenceConnects(c, 0, -1), s = FenceConnects(c, 0, 1);
    bool alongX = w || e, alongZ = n || s;
    const float f = 0.4375f, f1 = 0.5625f;
    float x0 = w ? 0 : f, x1 = e ? 1 : f1, z0 = n ? 0 : f, z1 = s ? 1 : f1;
    for (float y0 : { 0.75f, 0.375f }) {
        float y1 = y0 + 0.1875f;
        if (alongX) RenderBox(c, MakeBoxLocal(x0, y0, f, x1, y1, f1), Cull::None, Layer::Opaque);
        if (alongZ) RenderBox(c, MakeBoxLocal(f, y0, z0, f1, y1, z1), Cull::None, Layer::Opaque);
    }
}

void RenderFenceGate(Ctx& c) {
    int meta = c.world ? c.meta : 0;
    int dir = meta & 3;
    bool open = (meta & 4) != 0;
    bool alongX = dir == 0 || dir == 2; // the gate spans X
    // Build as if spanning X, then swap axes.
    auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        if (alongX) RenderBox(c, MakeBoxLocal(x0, y0, z0, x1, y1, z1), Cull::None, Layer::Opaque);
        else RenderBox(c, MakeBoxLocal(z0, y0, x0, z1, y1, x1), Cull::None, Layer::Opaque);
    };
    box(0, 0.3125f, 0.4375f, 0.125f, 1, 0.5625f);
    box(0.875f, 0.3125f, 0.4375f, 1, 1, 0.5625f);
    if (!open) {
        box(0.375f, 0.375f, 0.4375f, 0.5f, 0.9375f, 0.5625f);
        box(0.5f, 0.375f, 0.4375f, 0.625f, 0.9375f, 0.5625f);
        box(0.625f, 0.375f, 0.4375f, 0.875f, 0.5625f, 0.5625f);
        box(0.625f, 0.75f, 0.4375f, 0.875f, 0.9375f, 0.5625f);
        box(0.125f, 0.375f, 0.4375f, 0.375f, 0.5625f, 0.5625f);
        box(0.125f, 0.75f, 0.4375f, 0.375f, 0.9375f, 0.5625f);
    } else {
        // Swung open: each half folds back along its post.
        bool towardPositive = alongX ? (dir == 0) : (dir == 3);
        float z0 = towardPositive ? 0.5625f : 0.1875f, z1 = towardPositive ? 0.8125f : 0.4375f;
        float e0 = towardPositive ? 0.8125f : 0.0625f, e1 = towardPositive ? 0.9375f : 0.1875f;
        box(0, 0.375f, e0, 0.125f, 0.9375f, e1);
        box(0.875f, 0.375f, e0, 1, 0.9375f, e1);
        box(0, 0.375f, z0, 0.125f, 0.5625f, z1);
        box(0, 0.75f, z0, 0.125f, 0.9375f, z1);
        box(0.875f, 0.375f, z0, 1, 0.5625f, z1);
        box(0.875f, 0.75f, z0, 1, 0.9375f, z1);
    }
}

// ---- Cactus: sides pulled in a pixel, top and bottom full.

void RenderCactus(Ctx& c) {
    BlockBox full;
    for (int side = 0; side < 6; side++) {
        bool vertical = side == SIDE_UP || side == SIDE_DOWN;
        if (vertical && !FaceVisible(c, full, side, Cull::Neighbor)) continue;
        FaceStyle style;
        style.tex = c.Tex(side);
        style.tint = c.Tint(side);
        EmitBoxFace(c, full, side, style, Layer::Cutout, vertical ? 0.0f : 0.0625f);
    }
}

// ---- Doors: a thin box, the texture the same way round from both sides.

void RenderDoor(Ctx& c) {
    BlockBox b = c.Bounds();
    for (int side = 0; side < 6; side++) {
        if (!FaceVisible(c, b, side, Cull::Neighbor)) continue;
        FaceStyle style;
        style.tex = c.Tex(side);
        style.mirror = side == SIDE_SOUTH || side == SIDE_WEST;
        EmitBoxFace(c, b, side, style, Layer::Cutout);
    }
}

int PerpendicularRight(int side, int up) {
    for (int r = 0; r < 6; r++) {
        if (DirAxis(r) == DirAxis(side) || DirAxis(r) == DirAxis(up)) continue;
        if (Vector3DotProduct(Vector3CrossProduct(DirVec(r), DirVec(up)), DirVec(side)) > 0) return r;
    }
    return DEFAULT_RIGHT[side];
}

// The texture up direction for a face whose right is `right`.
int UpFor(int side, int right) {
    for (int u = 0; u < 6; u++) {
        if (DirAxis(u) == DirAxis(side) || DirAxis(u) == DirAxis(right)) continue;
        if (Vector3DotProduct(Vector3CrossProduct(DirVec(right), DirVec(u)), DirVec(side)) > 0) return u;
    }
    return DEFAULT_UP[side];
}

// ---- Beds (RenderBlocks.renderBlockBed)

constexpr int BED_DIR_SIDE[4] = { SIDE_SOUTH, SIDE_WEST, SIDE_NORTH, SIDE_EAST }; // head direction by facing

void RenderBed(Ctx& c) {
    int meta = c.world ? c.meta : 0;
    int headDir = BED_DIR_SIDE[meta & 3];
    bool head = (meta & 8) != 0;
    int joint = head ? OppositeSide(headDir) : headDir;
    BlockBox b = MakeBoxLocal(0, 0, 0, 1, 0.5625f, 1);
    for (int side = 0; side < 6; side++) {
        if (side == joint && c.world) continue;
        if (side != SIDE_UP && side != SIDE_DOWN && !FaceVisible(c, b, side, Cull::Neighbor)) continue;
        FaceStyle style;
        style.tex = c.def.Texture(side, meta);
        if (side == SIDE_DOWN) {
            BlockBox bottom = MakeBoxLocal(0, 0.1875f, 0, 1, 0.1875f, 1);
            EmitBoxFace(c, bottom, side, style, Layer::Opaque);
            continue;
        }
        if (side == SIDE_UP) {
            style.right = headDir;
            style.up = UpFor(SIDE_UP, headDir);
        } else if (DirAxis(side) != DirAxis(headDir)) {
            // Long sides: the pillow end of the texture toward the head.
            int defaultRight = DEFAULT_RIGHT[side];
            style.mirror = defaultRight != headDir;
        }
        EmitBoxFace(c, b, side, style, Layer::Opaque);
    }
}

// ---- Repeaters (RenderBlocks.renderBlockRepeater)

void RenderRepeater(Ctx& c) {
    int meta = c.world ? c.meta : 0;
    int dir = meta & 3, delay = (meta >> 2) & 3;
    static const int OUTPUT[4] = { SIDE_NORTH, SIDE_EAST, SIDE_SOUTH, SIDE_WEST };
    BlockBox slab = MakeBoxLocal(0, 0, 0, 1, 0.125f, 1);
    for (int side = 0; side < 6; side++) {
        if (!FaceVisible(c, slab, side, Cull::Neighbor)) continue;
        FaceStyle style;
        style.tex = side == SIDE_UP ? c.def.Texture(SIDE_UP, meta) : 6;
        if (side == SIDE_UP) {
            style.up = OUTPUT[dir];
            style.right = PerpendicularRight(SIDE_UP, style.up);
        }
        EmitBoxFace(c, slab, side, style, Layer::Cutout);
    }
    static const float TORCH_OFFSET[4] = { -0.0625f, 0.0625f, 0.1875f, 0.3125f };
    float d1 = 0, d2 = 0, d3 = 0, d4 = 0;
    switch (dir) {
        case 0: d4 = -0.3125f; d2 = TORCH_OFFSET[delay]; break;
        case 2: d4 = 0.3125f; d2 = -TORCH_OFFSET[delay]; break;
        case 3: d3 = -0.3125f; d1 = TORCH_OFFSET[delay]; break;
        default: d3 = 0.3125f; d1 = -TORCH_OFFSET[delay]; break;
    }
    int torch = c.id == BLOCK_POWERED_REPEATER ? 99 : 115;
    RenderTorchAtAngle(c, torch, d1, -0.1875f, d2, 0, 0);
    RenderTorchAtAngle(c, torch, d3, -0.1875f, d4, 0, 0);
}

// ---- Levers (RenderBlocks.renderBlockLever): a cobblestone base and a stick.

void AddStick(Ctx& c, Vector3 pivot, Vector3 axis, Vector3 side, int tex, Light2 light) {
    // A 2x2-pixel stick 10 pixels long from `pivot` along `axis`.
    Vector3 other = Vector3CrossProduct(axis, side);
    const float half = 1.0f / 16.0f, len = 10.0f / 16.0f;
    Vector2 t = TileUV(tex);
    float u0 = t.x + 7 * PX, u1 = t.x + 9 * PX, vTop = t.y + 6 * PX, vBottom = t.y + 16 * PX;
    auto P = [&](float a, float s, float o) {
        return Vector3Add(pivot, Vector3Add(Vector3Scale(axis, a), Vector3Add(Vector3Scale(side, s), Vector3Scale(other, o))));
    };
    struct Face { float s0, o0, s1, o1; };
    // Four long faces around the stick.
    const float corners[4][2] = { { -half, -half }, { half, -half }, { half, half }, { -half, half } };
    for (int k = 0; k < 4; k++) {
        const float* a = corners[k];
        const float* b = corners[(k + 1) % 4];
        Vector3 p[4] = { P(len, a[0], a[1]), P(0, a[0], a[1]), P(0, b[0], b[1]), P(len, b[0], b[1]) };
        Vector2 uv[4] = { { u0, vTop }, { u0, vBottom }, { u1, vBottom }, { u1, vTop } };
        FreeQuad(c, p, uv, WHITE, Layer::Cutout, true, light);
    }
    Vector3 cap[4] = { P(len, -half, -half), P(len, half, -half), P(len, half, half), P(len, -half, half) };
    Vector2 capUV[4] = { { u0, vTop }, { u1, vTop }, { u1, vTop + 2 * PX }, { u0, vTop + 2 * PX } };
    FreeQuad(c, cap, capUV, WHITE, Layer::Cutout, true, light);
}

void RenderLever(Ctx& c) {
    int meta = c.world ? c.meta : 5;
    int a = meta & 7;
    bool on = (meta & 8) != 0;
    const float f = 0.25f, f1 = 0.1875f, f2 = 0.1875f;
    BlockBox base;
    Vector3 normal, tangent;
    if (a == 5) { base = MakeBoxLocal(0.5f - f1, 0, 0.5f - f, 0.5f + f1, f2, 0.5f + f); normal = { 0, 1, 0 }; tangent = { 0, 0, 1 }; }
    else if (a == 6) { base = MakeBoxLocal(0.5f - f, 0, 0.5f - f1, 0.5f + f, f2, 0.5f + f1); normal = { 0, 1, 0 }; tangent = { 1, 0, 0 }; }
    else if (a == 4) { base = MakeBoxLocal(0.5f - f1, 0.5f - f, 1 - f2, 0.5f + f1, 0.5f + f, 1); normal = { 0, 0, -1 }; tangent = { 0, 1, 0 }; }
    else if (a == 3) { base = MakeBoxLocal(0.5f - f1, 0.5f - f, 0, 0.5f + f1, 0.5f + f, f2); normal = { 0, 0, 1 }; tangent = { 0, 1, 0 }; }
    else if (a == 2) { base = MakeBoxLocal(1 - f2, 0.5f - f, 0.5f - f1, 1, 0.5f + f, 0.5f + f1); normal = { -1, 0, 0 }; tangent = { 0, 1, 0 }; }
    else { base = MakeBoxLocal(0, 0.5f - f, 0.5f - f1, f2, 0.5f + f, 0.5f + f1); normal = { 1, 0, 0 }; tangent = { 0, 1, 0 }; }
    RenderBox(c, base, Cull::Neighbor, Layer::Cutout, 16);

    Vector3 center = { (base.minX + base.maxX) / 2, (base.minY + base.maxY) / 2, (base.minZ + base.maxZ) / 2 };
    const float angle = 40.0f * DEG2RAD;
    float sign = on ? -1.0f : 1.0f;
    if (a == 5 || a == 6) sign = on ? 1.0f : -1.0f;
    Vector3 axis = Vector3Add(Vector3Scale(normal, std::cos(angle)), Vector3Scale(tangent, sign * std::sin(angle)));
    Vector3 side = Vector3Normalize(Vector3CrossProduct(normal, tangent));
    AddStick(c, center, axis, side, c.Tex(SIDE_UP), c.Own());
}

// ---- Pistons: side textures point the way the piston faces.

void RenderPistonBase(Ctx& c) {
    int meta = c.meta;
    int facing = meta & 7;
    if (facing > 5) facing = 1;
    BlockBox b = c.world ? c.Bounds() : BlockBox();
    for (int side = 0; side < 6; side++) {
        if (!FaceVisible(c, b, side, Cull::Neighbor)) continue;
        FaceStyle style;
        style.tex = c.def.Texture(side, meta);
        if (DirAxis(side) != DirAxis(facing)) {
            style.up = facing;
            style.right = PerpendicularRight(side, facing);
        }
        EmitBoxFace(c, b, side, style, Layer::Opaque);
    }
}

void RenderPistonHead(Ctx& c) {
    int meta = c.meta;
    int facing = meta & 7;
    if (facing > 5) facing = 1;
    BlockBox plate = c.world ? c.Bounds() : BlockBox();
    if (c.world == nullptr) {
        float mn[3] = { 0, 0, 0 }, mx[3] = { 1, 1, 1 };
        int ax = DirAxis(facing);
        if (DirPositive(facing)) mn[ax] = 0.75f; else mx[ax] = 0.25f;
        plate = MakeBoxLocal(mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
    }
    for (int side = 0; side < 6; side++) {
        if (!FaceVisible(c, plate, side, Cull::Neighbor)) continue;
        FaceStyle style;
        style.tex = c.def.Texture(side, meta);
        if (DirAxis(side) != DirAxis(facing)) {
            style.up = facing;
            style.right = PerpendicularRight(side, facing);
        }
        EmitBoxFace(c, plate, side, style, Layer::Opaque);
    }
    // The arm, reaching back into the base.
    int ax = DirAxis(facing);
    float mn[3] = { 0.375f, 0.375f, 0.375f }, mx[3] = { 0.625f, 0.625f, 0.625f };
    if (DirPositive(facing)) { mn[ax] = -0.25f; mx[ax] = 0.75f; }
    else { mn[ax] = 0.25f; mx[ax] = 1.25f; }
    Light2 light = c.Own();
    Vector2 t = TileUV(108);
    for (int side = 0; side < 6; side++) {
        if (DirAxis(side) == ax) continue;
        float fmn[3] = { mn[0], mn[1], mn[2] }, fmx[3] = { mx[0], mx[1], mx[2] };
        int n = DirAxis(side);
        float plane = DirPositive(side) ? fmx[n] : fmn[n];
        int wAxis = 3 - n - ax;
        // Corners: along the arm (u) and across it (v).
        Vector3 p[4];
        float along[4] = { fmn[ax], fmn[ax], fmx[ax], fmx[ax] };
        float across[4] = { fmn[wAxis], fmx[wAxis], fmx[wAxis], fmn[wAxis] };
        for (int k = 0; k < 4; k++) {
            float q[3];
            q[n] = plane;
            q[ax] = along[k];
            q[wAxis] = across[k];
            p[k] = { q[0], q[1], q[2] };
        }
        Vector2 uv[4] = { { t.x, t.y }, { t.x, t.y + 4 * PX }, { t.x + 16 * PX, t.y + 4 * PX }, { t.x + 16 * PX, t.y } };
        Vector3 nrm = Vector3CrossProduct(Vector3Subtract(p[1], p[0]), Vector3Subtract(p[2], p[0]));
        if (Vector3DotProduct(nrm, DirVec(side)) < 0) {
            std::swap(p[1], p[3]);
            std::swap(uv[1], uv[3]);
        }
        FreeQuad(c, p, uv, Scale(WHITE, SideShade(side)), Layer::Opaque, false, light);
    }
}

// ---- Glass panes and iron bars

void RenderPane(Ctx& c) {
    int tex = c.Tex(SIDE_NORTH);
    const Block& def = c.def;
    int edge = c.id == BLOCK_GLASS_PANE ? 148 : tex;
    auto connects = [&](int dx, int dz) {
        int id = c.Get(dx, 0, dz);
        return BlockIsOpaqueCube((uint8_t)id) || id == c.id || id == BLOCK_GLASS;
    };
    bool n = false, s = false, w = false, e = false;
    if (c.world) { n = connects(0, -1); s = connects(0, 1); w = connects(-1, 0); e = connects(1, 0); }
    bool none = !n && !s && !w && !e;
    Light2 light = c.Own();
    Vector2 t = TileUV(tex), te = TileUV(edge);
    (void)def;

    auto planeX = [&](float x0, float x1) { // a plane along X at z = 0.5
        Vector3 p[4] = { { x0, 1, 0.5f }, { x0, 0, 0.5f }, { x1, 0, 0.5f }, { x1, 1, 0.5f } };
        Vector2 uv[4] = { { t.x + x0 / 16, t.y }, { t.x + x0 / 16, t.y + 1 / 16.0f }, { t.x + x1 / 16, t.y + 1 / 16.0f }, { t.x + x1 / 16, t.y } };
        FreeQuad(c, p, uv, WHITE, Layer::Cutout, true, light);
        for (float y : { 0.0f, 1.0f }) {
            Vector3 q[4] = { { x0, y, 0.4375f }, { x0, y, 0.5625f }, { x1, y, 0.5625f }, { x1, y, 0.4375f } };
            Vector2 quv[4] = { { te.x + 7 * PX, te.y + x0 / 16 }, { te.x + 9 * PX, te.y + x0 / 16 }, { te.x + 9 * PX, te.y + x1 / 16 }, { te.x + 7 * PX, te.y + x1 / 16 } };
            FreeQuad(c, q, quv, WHITE, Layer::Cutout, true, light);
        }
    };
    auto planeZ = [&](float z0, float z1) { // a plane along Z at x = 0.5
        Vector3 p[4] = { { 0.5f, 1, z1 }, { 0.5f, 0, z1 }, { 0.5f, 0, z0 }, { 0.5f, 1, z0 } };
        Vector2 uv[4] = { { t.x + (1 - z1) / 16, t.y }, { t.x + (1 - z1) / 16, t.y + 1 / 16.0f }, { t.x + (1 - z0) / 16, t.y + 1 / 16.0f }, { t.x + (1 - z0) / 16, t.y } };
        FreeQuad(c, p, uv, WHITE, Layer::Cutout, true, light);
        for (float y : { 0.0f, 1.0f }) {
            Vector3 q[4] = { { 0.4375f, y, z0 }, { 0.4375f, y, z1 }, { 0.5625f, y, z1 }, { 0.5625f, y, z0 } };
            Vector2 quv[4] = { { te.x + 7 * PX, te.y + z0 / 16 }, { te.x + 7 * PX, te.y + z1 / 16 }, { te.x + 9 * PX, te.y + z1 / 16 }, { te.x + 9 * PX, te.y + z0 / 16 } };
            FreeQuad(c, q, quv, WHITE, Layer::Cutout, true, light);
        }
    };
    if ((w && e) || none || c.world == nullptr) planeX(0, 1);
    else if (w) planeX(0, 0.5f);
    else if (e) planeX(0.5f, 1);
    if ((n && s) || none) { if (c.world) planeZ(0, 1); }
    else if (n) planeZ(0, 0.5f);
    else if (s) planeZ(0.5f, 1);
}

// ---- Pumpkin and melon stems (RenderBlocks.renderBlockStem)

void RenderStem(Ctx& c) {
    int meta = c.world ? c.meta : 7;
    Color tint = c.Tint(SIDE_NORTH);
    float height = (meta * 2 + 2) / 16.0f;
    int fruitSide = -1;
    if (c.world && meta >= 7) {
        int fruit = c.id == BLOCK_PUMPKIN_STEM ? BLOCK_PUMPKIN : BLOCK_MELON;
        if (c.Get(-1, 0, 0) == fruit) fruitSide = SIDE_WEST;
        else if (c.Get(1, 0, 0) == fruit) fruitSide = SIDE_EAST;
        else if (c.Get(0, 0, -1) == fruit) fruitSide = SIDE_NORTH;
        else if (c.Get(0, 0, 1) == fruit) fruitSide = SIDE_SOUTH;
    }
    if (fruitSide < 0) {
        RenderCross(c, 111, tint, -0.0625f, std::min(height, 1.0f), 0.0f);
        return;
    }
    // Fully grown and bent over toward its fruit.
    Light2 light = c.Own();
    const float top = 0.5f, bot = -0.0625f;
    Vector2 t = TileUV(127);
    float u0 = t.x, u1 = t.x + 16 * PX, v0 = t.y + (1 - (top - bot)) * 16 * PX, v1 = t.y + 16 * PX;
    Vector3 p[4];
    bool flip = fruitSide == SIDE_WEST || fruitSide == SIDE_NORTH;
    if (fruitSide == SIDE_WEST || fruitSide == SIDE_EAST) {
        Vector3 q[4] = { { 0, top, 0.5f }, { 0, bot, 0.5f }, { 1, bot, 0.5f }, { 1, top, 0.5f } };
        std::copy(q, q + 4, p);
    } else {
        Vector3 q[4] = { { 0.5f, top, 0 }, { 0.5f, bot, 0 }, { 0.5f, bot, 1 }, { 0.5f, top, 1 } };
        std::copy(q, q + 4, p);
    }
    if (flip) std::swap(u0, u1);
    Vector2 uv[4] = { { u0, v0 }, { u0, v1 }, { u1, v1 }, { u1, v0 } };
    FreeQuad(c, p, uv, tint, Layer::Cutout, true, light);
}

// ---- Cubes (the standard renderer), with Beta's grass side overlay.

void RenderCube(Ctx& c) {
    BlockBox b = c.Bounds();
    Layer layer = c.def.layer;
    RenderBox(c, b, Cull::Block, layer);
    if (c.id == BLOCK_GRASS) {
        for (int side = 2; side < 6; side++) {
            if (c.Tex(side) != 3 || !FaceVisible(c, b, side, Cull::Block)) continue;
            FaceStyle style;
            style.tex = 38;
            style.tint = c.world ? GRASS_COLOR : GRASS_ITEM_COLOR;
            EmitBoxFace(c, b, side, style, Layer::Cutout);
        }
    }
}

void Render(Ctx& c) {
    switch (c.def.shape) {
        case Shape::None: case Shape::Chest: case Shape::Sign: case Shape::MovingPiston: return;
        case Shape::Cube: RenderCube(c); return;
        case Shape::Cross: RenderCross(c, c.Tex(SIDE_NORTH), c.Tint(SIDE_NORTH)); return;
        case Shape::Torch: RenderTorch(c); return;
        case Shape::Fire: RenderFire(c); return;
        case Shape::Fluid: if (c.world) RenderFluid(c); return;
        case Shape::RedstoneWire: RenderWire(c); return;
        case Shape::Crops: RenderCrops(c, c.Tex(SIDE_NORTH), c.Tint(SIDE_NORTH)); return;
        case Shape::Door: RenderDoor(c); return;
        case Shape::Ladder: RenderLadder(c); return;
        case Shape::Rail: RenderRail(c); return;
        case Shape::Stairs: RenderStairs(c); return;
        case Shape::Fence: RenderFence(c); return;
        case Shape::Lever: RenderLever(c); return;
        case Shape::Cactus: RenderCactus(c); return;
        case Shape::Bed: RenderBed(c); return;
        case Shape::Repeater: RenderRepeater(c); return;
        case Shape::PistonBase: RenderPistonBase(c); return;
        case Shape::PistonHead: RenderPistonHead(c); return;
        case Shape::Pane: RenderPane(c); return;
        case Shape::Stem: RenderStem(c); return;
        case Shape::Vine: RenderVine(c); return;
        case Shape::FenceGate: RenderFenceGate(c); return;
    }
}

// ---- Chunk meshes

struct MeshBuffer {
    std::vector<float> positions, texcoords, texcoords2, normals;
    std::vector<unsigned char> colors;

    // Two triangles, split along the diagonal that keeps the brighter corners together.
    void Add(const BlockVertex v[4], float shade, bool tiled, Vector2 tileOrigin) {
        float lit[4];
        for (int k = 0; k < 4; k++) lit[k] = (v[k].color.r + v[k].color.g + v[k].color.b) * (std::max(v[k].sky, v[k].glow) + 2);
        static const int A[6] = { 0, 1, 2, 0, 2, 3 }, B[6] = { 1, 2, 3, 1, 3, 0 };
        const int* order = (lit[0] + lit[2] < lit[1] + lit[3]) ? B : A;
        for (int i = 0; i < 6; i++) {
            const BlockVertex& p = v[order[i]];
            positions.insert(positions.end(), { p.x, p.y, p.z });
            texcoords.insert(texcoords.end(), { p.u, p.v });
            if (tiled) texcoords2.insert(texcoords2.end(), { tileOrigin.x, tileOrigin.y });
            else texcoords2.insert(texcoords2.end(), { -1.0f, -1.0f });
            normals.insert(normals.end(), { p.sky, p.glow, 0.0f });
            Color col = Scale(p.color, shade);
            colors.insert(colors.end(), { col.r, col.g, col.b, 255 });
        }
    }

    bool Empty() const { return positions.empty(); }

    Mesh Upload() const {
        Mesh mesh = { 0 };
        mesh.vertexCount = (int)(positions.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        auto copy = [](const auto& src) {
            using T = typename std::decay_t<decltype(src)>::value_type;
            T* dst = (T*)MemAlloc((unsigned int)(src.size() * sizeof(T)));
            memcpy(dst, src.data(), src.size() * sizeof(T));
            return dst;
        };
        mesh.vertices = copy(positions);
        mesh.texcoords = copy(texcoords);
        mesh.texcoords2 = copy(texcoords2);
        mesh.normals = copy(normals);
        mesh.colors = copy(colors);
        UploadMesh(&mesh, false);
        return mesh;
    }
};

class ChunkSink final : public QuadSink {
public:
    MeshBuffer opaque, translucent;
    float (*shadeByNormal)(Vector3) = nullptr; // replaces the per-side shade when set
    void Quad(const BlockVertex v[4], Vector3 normal, Layer layer, float shade) override {
        if (shadeByNormal != nullptr) shade = shadeByNormal(normal);
        (layer == Layer::Translucent ? translucent : opaque).Add(v, shade, false, Vector2{ 0, 0 });
    }
};

// Greedy meshing of full opaque cubes: faces that look identical (same
// texture, tint, AO and light at every corner) merge into one big quad whose
// texture repeats per block in the shader.
struct FaceKey {
    int tex = -1;
    Color tint = WHITE;
    bool overlay = false;
    uint8_t ao[4] = {};
    uint8_t sky[4] = {}, glow[4] = {}; // corner light x 4
    bool Empty() const { return tex < 0; }
    bool operator==(const FaceKey& o) const {
        return tex == o.tex && tint.r == o.tint.r && tint.g == o.tint.g && tint.b == o.tint.b && overlay == o.overlay &&
               memcmp(ao, o.ao, 4) == 0 && memcmp(sky, o.sky, 4) == 0 && memcmp(glow, o.glow, 4) == 0;
    }
};

void GreedyFaces(const World& world, const Chunk& chunk, MeshBuffer& out) {
    const int dims[3] = { Chunk::SIZE, Chunk::HEIGHT, Chunk::SIZE };
    const int baseX = chunk.GetChunkX() * Chunk::SIZE, baseZ = chunk.GetChunkZ() * Chunk::SIZE;
    const MeshCache& cache = *g_meshCache;
    auto blockAt = [&](const int l[3]) -> uint8_t { return cache.ids[l[0] + 1][l[1] + 1][l[2] + 1]; };

    std::vector<FaceKey> mask;
    for (int side = 0; side < 6; side++) {
        int n = DirAxis(side);
        int a1 = (n + 1) % 3, a2 = (n + 2) % 3;
        mask.assign((size_t)dims[a1] * dims[a2], FaceKey());
        FaceKey* maskData = mask.data();
        int right = DEFAULT_RIGHT[side], up = DEFAULT_UP[side];
        int ra = DirAxis(right), ua = DirAxis(up);
        Vector3 normal = DirVec(side);
        bool swapOrder = Vector3DotProduct(Vector3CrossProduct(DirVec(right), DirVec(up)), normal) < 0;

        for (int slice = 0; slice < dims[n]; slice++) {
            // Build the mask of visible faces in this slice.
            for (int j = 0; j < dims[a2]; j++) {
                for (int i = 0; i < dims[a1]; i++) {
                    FaceKey& key = maskData[(size_t)j * dims[a1] + i];
                    key.tex = -1;
                    int l[3];
                    l[n] = slice; l[a1] = i; l[a2] = j;
                    uint8_t id = blockAt(l);
                    if (!cache.greedyId[id]) continue;
                    int nl[3] = { l[0] + SIDE_OFFSET[side][0], l[1] + SIDE_OFFSET[side][1], l[2] + SIDE_OFFSET[side][2] };
                    if (nl[1] < 0) continue; // bottom of the world
                    if (cache.opaqueId[blockAt(nl)]) continue;
                    const Block& def = GetBlockDef(id);
                    int wx = baseX + l[0], wy = l[1], wz = baseZ + l[2];
                    key.tex = def.WorldTexture(world, wx, wy, wz, side);
                    key.tint = def.WorldTint(world, wx, wy, wz, side);
                    key.overlay = id == BLOCK_GRASS && key.tex == 3;
                    int cell[3] = { baseX + nl[0], nl[1], baseZ + nl[2] };
                    static const int S1[4] = { -1, 1, 1, -1 }, S2[4] = { -1, -1, 1, 1 };
                    for (int k = 0; k < 4; k++) {
                        CornerShade s = SmoothCorner(world, cell, a1, S1[k], a2, S2[k]);
                        key.ao[k] = (uint8_t)s.ao;
                        key.sky[k] = (uint8_t)std::lround(s.sky * 4);
                        key.glow[k] = (uint8_t)std::lround(s.glow * 4);
                    }
                }
            }
            // Merge rectangles.
            for (int j = 0; j < dims[a2]; j++) {
                for (int i = 0; i < dims[a1];) {
                    const FaceKey key = maskData[(size_t)j * dims[a1] + i];
                    if (key.Empty()) { i++; continue; }
                    int w = 1;
                    while (i + w < dims[a1] && maskData[(size_t)j * dims[a1] + i + w] == key) w++;
                    int h = 1;
                    for (bool done = false; j + h < dims[a2] && !done; ) {
                        for (int k = 0; k < w; k++) {
                            if (!(maskData[(size_t)(j + h) * dims[a1] + i + k] == key)) { done = true; break; }
                        }
                        if (!done) h++;
                    }
                    for (int jj = 0; jj < h; jj++)
                        for (int ii = 0; ii < w; ii++) maskData[(size_t)(j + jj) * dims[a1] + i + ii].tex = -1;

                    // Corners of the rectangle, ordered by the face's right/up.
                    float mn[3], mx[3];
                    mn[n] = mx[n] = (float)(slice + (DirPositive(side) ? 1 : 0));
                    mn[a1] = (float)i; mx[a1] = (float)(i + w);
                    mn[a2] = (float)j; mx[a2] = (float)(j + h);
                    static const int cu[4] = { 0, 1, 1, 0 }, cv[4] = { 0, 0, 1, 1 };
                    BlockVertex v[4];
                    for (int k = 0; k < 4; k++) {
                        float p[3];
                        p[n] = mn[n];
                        p[ra] = ((cu[k] == 0) == DirPositive(right)) ? mn[ra] : mx[ra];
                        p[ua] = ((cv[k] == 0) == DirPositive(up)) ? mn[ua] : mx[ua];
                        int s1 = p[a1] > mn[a1] ? 1 : 0, s2 = p[a2] > mn[a2] ? 1 : 0;
                        int corner = s2 == 0 ? (s1 == 0 ? 0 : 1) : (s1 == 0 ? 3 : 2);
                        BlockVertex& vert = v[k];
                        vert.x = p[0]; vert.y = p[1]; vert.z = p[2];
                        vert.u = DirPositive(right) ? p[ra] : -p[ra];
                        vert.v = -(DirPositive(up) ? p[ua] : -p[ua]);
                        vert.color = Scale(key.tint, AO_CURVE[key.ao[corner]]);
                        vert.sky = key.sky[corner] / 4.0f;
                        vert.glow = key.glow[corner] / 4.0f;
                    }
                    if (swapOrder) std::swap(v[1], v[3]);
                    out.Add(v, SideShade(side), true, TileUV(key.tex));
                    if (key.overlay) {
                        BlockVertex o[4];
                        for (int k = 0; k < 4; k++) {
                            o[k] = v[k];
                            Color base = o[k].color; // AO only: the base face is untinted
                            o[k].color = Color{ (unsigned char)(base.r * GRASS_COLOR.r / 255), (unsigned char)(base.g * GRASS_COLOR.g / 255),
                                                (unsigned char)(base.b * GRASS_COLOR.b / 255), 255 };
                        }
                        out.Add(o, SideShade(side), true, TileUV(38));
                    }
                    i += w;
                }
            }
        }
    }
}

// A QuadSink for item/entity models is in Models.cpp; this file only builds chunks.

} // namespace

void RenderWorldBlock(QuadSink& sink, const World& world, int x, int y, int z, Vector3 origin) {
    uint8_t id = world.GetBlock(x, y, z);
    if (id == BLOCK_AIR) return;
    Ctx c{ sink, &world, x, y, z, Vector3{ x - origin.x, y - origin.y, z - origin.z }, id, world.GetMeta(x, y, z), GetBlockDef(id), Light2{} };
    Render(c);
}

void RenderBlockModel(QuadSink& sink, int id, int meta, Vector3 offset, float sky, float glow) {
    if (id <= 0 || id >= 256) return;
    Ctx c{ sink, nullptr, 0, 0, 0, offset, id, meta, GetBlockDef((uint8_t)id), Light2{ sky, glow } };
    Render(c);
}

Mesh BuildBlockTerrainMesh(int id, int meta, float (*shade)(Vector3 normal)) {
    ChunkSink sink;
    sink.shadeByNormal = shade;
    RenderBlockModel(sink, id, meta, Vector3{ 0, 0, 0 });
    MeshBuffer& all = sink.opaque;
    const MeshBuffer& t = sink.translucent;
    all.positions.insert(all.positions.end(), t.positions.begin(), t.positions.end());
    all.texcoords.insert(all.texcoords.end(), t.texcoords.begin(), t.texcoords.end());
    all.texcoords2.insert(all.texcoords2.end(), t.texcoords2.begin(), t.texcoords2.end());
    all.normals.insert(all.normals.end(), t.normals.begin(), t.normals.end());
    all.colors.insert(all.colors.end(), t.colors.begin(), t.colors.end());
    if (all.Empty()) return Mesh{ 0 };
    return all.Upload();
}

ChunkMeshes BuildChunkMeshes(const World& world, const Chunk& chunk) {
    static MeshCache cache; // big; meshing is main-thread only
    cache.x0 = chunk.GetChunkX() * Chunk::SIZE;
    cache.z0 = chunk.GetChunkZ() * Chunk::SIZE;
    memset(cache.lit, 0, sizeof(cache.lit));
    for (int id = 0; id < 256; id++) {
        cache.opaqueId[id] = BlockIsOpaqueCube((uint8_t)id);
        cache.greedyId[id] = cache.opaqueId[id] && GetBlockDef((uint8_t)id).shape == Shape::Cube;
    }
    for (int i = 0; i < MeshCache::W; i++)
        for (int k = 0; k < MeshCache::W; k++) {
            int lx = i - 1, lz = k - 1;
            bool inside = lx >= 0 && lz >= 0 && lx < Chunk::SIZE && lz < Chunk::SIZE;
            for (int j = 0; j < MeshCache::H; j++) {
                int y = j - 1;
                uint8_t id = BLOCK_AIR;
                if (y >= 0 && y < Chunk::HEIGHT) id = inside ? chunk.GetBlock(lx, y, lz) : world.GetBlock(cache.x0 + lx, y, cache.z0 + lz);
                cache.ids[i][j][k] = id;
                cache.opaque[i][j][k] = cache.opaqueId[id] ? 1 : 0;
            }
        }
    g_meshCache = &cache;
    ChunkSink sink;
    GreedyFaces(world, chunk, sink.opaque);
    const int baseX = chunk.GetChunkX() * Chunk::SIZE, baseZ = chunk.GetChunkZ() * Chunk::SIZE;
    Vector3 origin = { (float)baseX, 0.0f, (float)baseZ };
    for (int x = 0; x < Chunk::SIZE; x++) {
        for (int y = 0; y < Chunk::HEIGHT; y++) {
            for (int z = 0; z < Chunk::SIZE; z++) {
                uint8_t id = cache.ids[x + 1][y + 1][z + 1];
                if (id == BLOCK_AIR || cache.greedyId[id]) continue; // greedy
                RenderWorldBlock(sink, world, baseX + x, y, baseZ + z, origin);
            }
        }
    }
    g_meshCache = nullptr;
    ChunkMeshes meshes;
    if (!sink.opaque.Empty()) {
        meshes.opaque = sink.opaque.Upload();
        meshes.hasOpaque = true;
    }
    if (!sink.translucent.Empty()) {
        meshes.translucent = sink.translucent.Upload();
        meshes.hasTranslucent = true;
    }
    return meshes;
}
