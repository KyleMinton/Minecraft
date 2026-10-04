#pragma once

#include "raylib.h"
#include "World.h"

#include <deque>
#include <vector>

// Beta's particles: chips of a block's texture that fly off while you mine it
// and burst out when it breaks (EntityDiggingFX), and the effects blocks and
// explosions make (smoke, flames, lava, redstone dust, portal swirls, notes,
// splashes, bubbles, explosions) from particles.png. They move with Beta's
// physics and are lit by the light where they are (flames and lava glow).
// Construct after InitWindow() and destroy before CloseWindow().
class BlockParticles {
public:
    BlockParticles(Texture2D atlas, bool hasAtlas);
    ~BlockParticles();

    BlockParticles(const BlockParticles&) = delete;
    BlockParticles& operator=(const BlockParticles&) = delete;

    // The burst when a block breaks: a 4x4x4 grid of chips thrown outward.
    void AddDestroyEffects(const World& world, int x, int y, int z, BlockId block, int meta);
    // One small chip off the face being mined, once per tick.
    void AddHitEffect(const World& world, int x, int y, int z, int side, BlockId block, int meta);
    // World.spawnParticle. For reddust, (mx, my, mz) is the color (0 red = full red).
    void Spawn(ParticleType type, double x, double y, double z, double mx, double my, double mz);

    // Runs the 20 Hz particle ticks due this frame.
    void Update(float dt, const World& world);
    void Clear() { particles.clear(); }

    void SetFog(Vector3 color, float start, float end, float density, float skyDarken, Vector3 cameraPos);

    // Call between BeginMode3D() and EndMode3D().
    void Draw(const Camera3D& camera, const World& world) const;

private:
    enum class Kind { Digging, Smoke, Flame, Lava, Reddust, Portal, Splash, Bubble, Explode, Note };
    struct Particle {
        Kind kind = Kind::Digging;
        double x, y, z;
        double prevX, prevY, prevZ;
        double startX = 0, startY = 0, startZ = 0; // portal particles fly from here
        double motionX, motionY, motionZ;
        float scale = 1.0f;     // Beta's particleScale; the quad is 0.1 * scale across each way
        float baseScale = 1.0f;
        float gravity = 0.0f;
        int texture = 0;        // cell in particles.png (8x8 px)
        float u = 0, v = 0;     // digging: top-left of its 4x4-texel piece of terrain.png
        Color tint = WHITE;
        int age = 0, maxAge = 0;
        bool onGround = false;
        bool noClip = false;
    };

    std::deque<Particle> particles; // oldest first; the oldest goes when it's full
    double tickAccumulator = 0.0;
    float alpha = 0.0f;
    float skyDarken = 0.0f;

    Texture2D atlas;
    bool hasAtlas;
    Texture2D particleTexture;
    Shader shader;
    int fogColorLoc, fogStartLoc, fogEndLoc, fogDensityLoc, cameraPosLoc;

    Particle MakeDigging(const World& world, double x, double y, double z, double mx, double my, double mz, int bx, int by, int bz,
                         BlockId block, int meta) const;
    void Add(const Particle& particle);
    void Tick(const World& world);
    void DrawBatch(const Camera3D& camera, const World& world, bool digging) const;
};
