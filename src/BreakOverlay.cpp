#include "BreakOverlay.h"

#include "raymath.h"
#include "rlgl.h"

#include <cmath>

#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr int PLACEHOLDER_SIZE = 16;

// A fixed sequence of crack pixels spreading out from the middle; stage N shows
// the first (N + 1) tenths of it, so each stage grows the previous one's cracks.
std::vector<std::pair<int, int>> CrackPixels() {
    uint32_t rng = 20240517u;
    auto next = [&rng](int range) {
        rng = rng * 1664525u + 1013904223u;
        return (int)((rng >> 16) % (uint32_t)range);
    };

    struct Walker { int x, y, dx, dy; };
    std::vector<Walker> walkers = {
        { 8, 7, 1, 0 }, { 7, 8, -1, 0 }, { 8, 8, 0, 1 }, { 7, 7, 0, -1 },
        { 8, 7, 1, -1 }, { 7, 8, -1, 1 },
    };

    std::vector<std::pair<int, int>> pixels;
    bool used[PLACEHOLDER_SIZE][PLACEHOLDER_SIZE] = {};
    for (int step = 0; step < 400 && pixels.size() < 90; step++) {
        Walker& w = walkers[step % walkers.size()];
        // Mostly keep heading, sometimes veer, so lines look jagged rather than straight.
        int turn = next(10);
        int mx = w.dx, my = w.dy;
        if (turn < 2) mx = (mx == 0) ? (next(2) ? 1 : -1) : 0;
        else if (turn < 4) my = (my == 0) ? (next(2) ? 1 : -1) : 0;
        int nx = w.x + mx, ny = w.y + my;
        if (nx < 0 || ny < 0 || nx >= PLACEHOLDER_SIZE || ny >= PLACEHOLDER_SIZE) continue;
        w.x = nx;
        w.y = ny;
        if (!used[nx][ny]) {
            used[nx][ny] = true;
            pixels.emplace_back(nx, ny);
        }
        if (next(25) == 0) walkers.push_back({ w.x, w.y, w.dy, w.dx }); // branch
    }
    return pixels;
}

Texture2D PlaceholderStage(int stage, const std::vector<std::pair<int, int>>& pixels) {
    Image image = GenImageColor(PLACEHOLDER_SIZE, PLACEHOLDER_SIZE, Color{ 128, 128, 128, 255 }); // mid-gray = no change when multiplied in
    size_t count = pixels.size() * (stage + 1) / BreakOverlay::STAGE_COUNT;
    for (size_t i = 0; i < count; i++) {
        ImageDrawPixel(&image, pixels[i].first, pixels[i].second, Color{ 40, 40, 40, 255 });
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

} // namespace

BreakOverlay::BreakOverlay() {
    std::vector<std::pair<int, int>> crackPixels = CrackPixels();
    // Beta's cracks are terrain.png tiles 240-249.
    Image terrain = LoadImage("resources/textures/terrain.png");
    bool haveTerrain = terrain.data != nullptr && terrain.width == 256;
    if (haveTerrain) ImageFormat(&terrain, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    for (int i = 0; i < STAGE_COUNT; i++) {
        if (haveTerrain) {
            // Minecraft multiplies the cracks into the block (blend DST_COLOR, SRC_COLOR:
            // result = 2 x crack x block), so mid-gray changes nothing. Make the
            // see-through pixels mid-gray so only the cracks show.
            int tile = 240 + i;
            Image image = ImageFromImage(terrain, Rectangle{ (float)((tile & 15) * 16), (float)((tile >> 4) * 16), 16.0f, 16.0f });
            Color* pixels = (Color*)image.data;
            for (int p = 0; p < image.width * image.height; p++) {
                float a = pixels[p].a / 255.0f;
                for (unsigned char* c : { &pixels[p].r, &pixels[p].g, &pixels[p].b }) {
                    *c = (unsigned char)(*c * a + 128.0f * (1.0f - a));
                }
                pixels[p].a = 255;
            }
            stages[i] = LoadTextureFromImage(image);
            UnloadImage(image);
        } else {
            stages[i] = PlaceholderStage(i, crackPixels);
        }
        SetTextureFilter(stages[i], TEXTURE_FILTER_POINT);
    }
    if (terrain.data != nullptr) UnloadImage(terrain);
}

BreakOverlay::~BreakOverlay() {
    for (Texture2D& texture : stages) UnloadTexture(texture);
}

// The crack is always the same way up for the player: upright on the sides, and
// on the top and bottom turned so its top points the way you face (snapped to
// the nearest of the four directions, like Minecraft's block textures).
void BreakOverlay::Draw(int x, int y, int z, int stage, Vector3 viewForward, BlockBox bounds) {
    if (stage < 0 || stage >= STAGE_COUNT) return;

    Vector3 facing = fabsf(viewForward.x) > fabsf(viewForward.z)
        ? Vector3{ viewForward.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f }
        : Vector3{ 0.0f, 0.0f, viewForward.z > 0.0f ? 1.0f : -1.0f };
    Vector3 up = { 0.0f, 1.0f, 0.0f };
    Vector3 playerRight = Vector3CrossProduct(facing, up);

    const Vector3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    Vector3 center = { x + (bounds.minX + bounds.maxX) / 2, y + (bounds.minY + bounds.maxY) / 2, z + (bounds.minZ + bounds.maxZ) / 2 };
    Vector3 half = { (bounds.maxX - bounds.minX) / 2 + 0.002f, (bounds.maxY - bounds.minY) / 2 + 0.002f, (bounds.maxZ - bounds.minZ) / 2 + 0.002f };

    rlDrawRenderBatchActive();
    rlSetBlendFactors(RL_DST_COLOR, RL_SRC_COLOR, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
    rlSetTexture(stages[stage].id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (const Vector3& n : normals) {
        // Which way the texture's top and right point on this face, as seen from outside.
        bool horizontal = n.y != 0.0f;
        Vector3 texUp = horizontal ? facing : up;
        Vector3 texRight = horizontal ? playerRight : Vector3CrossProduct(up, n);

        // Corners wound counter-clockwise seen from outside, for backface culling.
        Vector3 a = horizontal ? Vector3{ 1, 0, 0 } : up;
        Vector3 b = Vector3CrossProduct(a, n);
        Vector3 corners[4] = {
            Vector3Subtract(Vector3Negate(b), a), Vector3Subtract(b, a), Vector3Add(b, a), Vector3Add(Vector3Negate(b), a),
        };
        for (const Vector3& c : corners) {
            Vector3 offset = Vector3Multiply(Vector3Add(c, n), half);
            rlTexCoord2f(0.5f + Vector3DotProduct(c, texRight) * 0.5f, 0.5f - Vector3DotProduct(c, texUp) * 0.5f);
            rlVertex3f(center.x + offset.x, center.y + offset.y, center.z + offset.z);
        }
    }
    rlEnd();
    rlSetTexture(rlGetTextureIdDefault());
    EndBlendMode();
}
