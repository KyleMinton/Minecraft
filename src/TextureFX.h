#pragma once

#include "raylib.h"

#include <string>
#include <vector>

// The animated terrain textures, played the way 1.21 plays them: each is a strip
// of frames (resources/textures/animated/<name>.png, from ModernTextures) timed by
// its .mcmeta, and the frame showing is copied into its terrain.png tile 20 times
// a second. Still and flowing water and lava, the two fire textures, the portal.
class TextureFX {
public:
    explicit TextureFX(Texture2D atlas);
    ~TextureFX();

    TextureFX(const TextureFX&) = delete;
    TextureFX& operator=(const TextureFX&) = delete;

    // Runs the 20 Hz texture ticks due this frame.
    void Update(float dt);

private:
    struct Animation {
        Image strip = {};            // frames stacked top to bottom, each size x size
        int size = 16;
        int tile = 0;                // the top-left terrain.png tile it fills
        std::vector<int> frames;     // strip frame shown at each step
        std::vector<int> times;      // ticks each step lasts
        int step = 0;
        int ticksLeft = 0;
    };

    Texture2D atlas;
    std::vector<Animation> animations;
    double accumulator = 0.0;

    void Load(const std::string& name, int tile);
    void Tick();
    void Upload(const Animation& animation) const;
};
