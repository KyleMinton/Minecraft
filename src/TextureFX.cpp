#include "TextureFX.h"
#include "JavaRandom.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double TICK_SECONDS = 1.0 / 20.0;
constexpr int WATER_TILE = 205, WATER_FLOW_TILE = 206, LAVA_TILE = 237, LAVA_FLOW_TILE = 238;
constexpr int FIRE_TILES[2] = { 31, 47 };
constexpr int PORTAL_TILE = 14;
constexpr float PI_F = 3.14159265f;

uint8_t Byte(float v) {
    return (uint8_t)std::clamp((int)v, 0, 255);
}

} // namespace

TextureFX::TextureFX(Texture2D terrain) : atlas(terrain) {
    // TexturePortalFX: 32 frames of a two-armed swirl, worked out once.
    JavaRandom random(100);
    portalFrames.assign(32, std::vector<uint8_t>(1024));
    for (int frame = 0; frame < 32; frame++) {
        for (int j = 0; j < 16; j++) {
            for (int k = 0; k < 16; k++) {
                float f = 0.0f;
                for (int l = 0; l < 2; l++) {
                    float f1 = l * 8.0f, f2 = l * 8.0f;
                    float f3 = (j - f1) / 16.0f * 2.0f;
                    float f4 = (k - f2) / 16.0f * 2.0f;
                    if (f3 < -1.0f) f3 += 2.0f;
                    if (f3 >= 1.0f) f3 -= 2.0f;
                    if (f4 < -1.0f) f4 += 2.0f;
                    if (f4 >= 1.0f) f4 -= 2.0f;
                    float f5 = f3 * f3 + f4 * f4;
                    float f6 = (float)std::atan2(f4, f3) + ((frame / 32.0f) * PI_F * 2.0f - f5 * 10.0f + l * 2) * (l * 2 - 1);
                    f6 = (std::sin(f6) + 1.0f) / 2.0f;
                    f6 /= f5 + 1.0f;
                    f += f6 * 0.5f;
                }
                f += random.NextFloat() * 0.1f;
                int i = (k * 16 + j) * 4;
                portalFrames[frame][i + 0] = Byte(f * f * 200.0f + 55.0f);
                portalFrames[frame][i + 1] = Byte(f * f * f * f * 255.0f);
                portalFrames[frame][i + 2] = Byte(f * 100.0f + 155.0f);
                portalFrames[frame][i + 3] = Byte(f * 100.0f + 155.0f);
            }
        }
    }
    Tick();
}

double TextureFX::Random() {
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / (double)(1u << 24);
}

void TextureFX::Update(float dt) {
    accumulator += dt;
    int ticks = 0;
    while (accumulator >= TICK_SECONDS && ticks < 5) {
        Tick();
        accumulator -= TICK_SECONDS;
        ticks++;
    }
    if (accumulator >= TICK_SECONDS) accumulator = 0.0;
}

void TextureFX::Tick() {
    tickCounter++;
    TickWater(water, false);
    TickWater(waterFlow, true);
    TickLava(lava, false);
    TickLava(lavaFlow, true);
    TickFire(0);
    TickFire(1);
    Upload(PORTAL_TILE, portalFrames[tickCounter & 31].data(), 1);
}

// TextureWaterFX and TextureWaterFlowFX: a blurred field of random droplets;
// the flowing one smears it downward every tick.
void TextureFX::TickWater(Heat& h, bool flowing) {
    for (int i = 0; i < 16; i++) {
        for (int k = 0; k < 16; k++) {
            float f = 0.0f;
            if (!flowing) {
                for (int j1 = i - 1; j1 <= i + 1; j1++) f += h.red[(j1 & 15) + (k & 15) * 16];
                h.green[i + k * 16] = f / 3.3f + h.blue[i + k * 16] * 0.8f;
            } else {
                for (int j1 = k - 2; j1 <= k; j1++) f += h.red[(i & 15) + (j1 & 15) * 16];
                h.green[i + k * 16] = f / 3.2f + h.blue[i + k * 16] * 0.8f;
            }
        }
    }
    for (int i = 0; i < 256; i++) {
        h.blue[i] += h.alpha[i] * 0.05f;
        if (h.blue[i] < 0.0f) h.blue[i] = 0.0f;
        h.alpha[i] -= flowing ? 0.3f : 0.1f;
        if (Random() < (flowing ? 0.2 : 0.05)) h.alpha[i] = 0.5f;
    }
    std::swap(h.red, h.green);
    uint8_t rgba[1024];
    for (int i = 0; i < 256; i++) {
        float f1 = std::clamp(h.red[i], 0.0f, 1.0f);
        float f2 = f1 * f1;
        rgba[i * 4 + 0] = Byte(32.0f + f2 * 32.0f);
        rgba[i * 4 + 1] = Byte(50.0f + f2 * 64.0f);
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = Byte(146.0f + f2 * 50.0f);
    }
    Upload(flowing ? WATER_FLOW_TILE : WATER_TILE, rgba, flowing ? 2 : 1);
}

// TextureLavaFX and TextureLavaFlowFX: a slowly churning heat field; the
// flowing one scrolls down a pixel every three ticks.
void TextureFX::TickLava(Heat& h, bool flowing) {
    int scroll = flowing ? tickCounter / 3 : 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            float f = 0.0f;
            int l = (int)(std::sin(j * PI_F * 2.0f / 16.0f) * 1.2f);
            int i1 = (int)(std::sin(i * PI_F * 2.0f / 16.0f) * 1.2f);
            for (int k1 = i - 1; k1 <= i + 1; k1++) {
                for (int i2 = j - 1; i2 <= j + 1; i2++) {
                    int k2 = (k1 + l) & 15;
                    int i3 = (i2 + i1) & 15;
                    f += h.red[k2 + i3 * 16];
                }
            }
            h.green[i + j * 16] = f / 10.0f +
                ((h.blue[(i & 15) + (j & 15) * 16] + h.blue[((i + 1) & 15) + (j & 15) * 16] + h.blue[((i + 1) & 15) + ((j + 1) & 15) * 16] +
                  h.blue[(i & 15) + ((j + 1) & 15) * 16]) / 4.0f) * 0.8f;
            h.blue[i + j * 16] += h.alpha[i + j * 16] * 0.01f;
            if (h.blue[i + j * 16] < 0.0f) h.blue[i + j * 16] = 0.0f;
            h.alpha[i + j * 16] -= 0.06f;
            if (Random() < 0.005) h.alpha[i + j * 16] = 1.5f;
        }
    }
    std::swap(h.red, h.green);
    uint8_t rgba[1024];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int src = x + ((y - scroll) & 15) * 16;
            float f1 = std::clamp(h.red[src] * 2.0f, 0.0f, 1.0f);
            int i = (x + y * 16) * 4;
            rgba[i + 0] = Byte(f1 * 100.0f + 155.0f);
            rgba[i + 1] = Byte(f1 * f1 * 255.0f);
            rgba[i + 2] = Byte(f1 * f1 * f1 * f1 * 128.0f);
            rgba[i + 3] = 255;
        }
    }
    Upload(flowing ? LAVA_FLOW_TILE : LAVA_TILE, rgba, flowing ? 2 : 1);
}

// TextureFlamesFX: heat rising from a randomly flickering bottom row.
void TextureFX::TickFire(int which) {
    float* f = fire[which];
    float* g = fireNext[which];
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 20; j++) {
            int k = 18;
            float f1 = f[i + ((j + 1) % 20) * 16] * k;
            for (int i1 = i - 1; i1 <= i + 1; i1++) {
                for (int k1 = j; k1 <= j + 1; k1++) {
                    if (i1 >= 0 && k1 >= 0 && i1 < 16 && k1 < 20) f1 += f[i1 + k1 * 16];
                    k++;
                }
            }
            g[i + j * 16] = f1 / (k * 1.06f);
            if (j >= 19) g[i + j * 16] = (float)(Random() * Random() * Random() * 4.0 + Random() * 0.1 + 0.2);
        }
    }
    std::swap_ranges(f, f + 16 * 20, g);
    uint8_t rgba[1024];
    for (int i = 0; i < 256; i++) {
        float f3 = std::clamp(f[i] * 1.8f, 0.0f, 1.0f);
        rgba[i * 4 + 0] = Byte(f3 * 155.0f + 100.0f);
        rgba[i * 4 + 1] = Byte(f3 * f3 * 255.0f);
        rgba[i * 4 + 2] = Byte(std::pow(f3, 10.0f) * 255.0f);
        rgba[i * 4 + 3] = f3 < 0.5f ? 0 : 255;
    }
    Upload(FIRE_TILES[which], rgba, 1);
}

void TextureFX::Upload(int tile, const uint8_t* rgba, int tiles) {
    if (atlas.id == 0) return;
    int size = atlas.width / 16;
    if (size != 16) return; // only Beta's 16px tiles
    for (int ty = 0; ty < tiles; ty++) {
        for (int tx = 0; tx < tiles; tx++) {
            int t = tile + tx + ty * 16;
            Rectangle rect = { (float)((t & 15) * 16), (float)((t >> 4) * 16), 16.0f, 16.0f };
            UpdateTextureRec(atlas, rect, rgba);
        }
    }
}
