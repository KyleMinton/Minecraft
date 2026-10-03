#pragma once

#include "raylib.h"

#include <cstdint>
#include <vector>

// Beta's procedurally animated terrain textures (the TextureFX classes), redrawn
// into terrain.png 20 times a second: still and flowing water, still and flowing
// lava, the two fire frames, and the portal swirl.
class TextureFX {
public:
    explicit TextureFX(Texture2D atlas);

    // Runs the 20 Hz texture ticks due this frame.
    void Update(float dt);

private:
    Texture2D atlas;
    double accumulator = 0.0;
    int tickCounter = 0;
    uint32_t rng = 12345;

    // TextureWaterFX / TextureWaterFlowFX / TextureLavaFX / TextureLavaFlowFX state.
    struct Heat {
        float red[256] = {}, green[256] = {}, blue[256] = {}, alpha[256] = {};
    };
    Heat water, waterFlow, lava, lavaFlow;
    float fire[2][16 * 20] = {}, fireNext[2][16 * 20] = {};
    std::vector<std::vector<uint8_t>> portalFrames;

    double Random();
    void Tick();
    void TickWater(Heat& h, bool flowing);
    void TickLava(Heat& h, bool flowing);
    void TickFire(int which);
    // Copies a 16x16 RGBA image into the tile, or the 2x2 tiles from it for flowing liquids.
    void Upload(int tile, const uint8_t* rgba, int tiles);
};
