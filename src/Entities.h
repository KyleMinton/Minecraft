#pragma once

#include "raylib.h"
#include "Models.h"

#include <vector>

class World;

// The world's non-item entities in this game: falling sand and gravel
// (EntityFallingSand) and lit TNT (EntityTNTPrimed). They tick with the world
// and draw as the blocks they are.
// Construct after InitWindow() and destroy before CloseWindow().
class Entities {
public:
    explicit Entities(const ItemModels& models);
    ~Entities();

    Entities(const Entities&) = delete;
    Entities& operator=(const Entities&) = delete;

    void SpawnFallingBlock(double x, double y, double z, int id, int meta);
    void SpawnTnt(double x, double y, double z, int fuse);

    void Tick(World& world);                                   // once per world tick
    void ExplosionPush(World& world, double x, double y, double z, float power);
    bool AnyInBox(const double min[3], const double max[3]) const;
    void Clear() { entities.clear(); }

    void SetFog(Vector3 color, float start, float end, float density, float skyDarken, Vector3 cameraPos);
    // Call between BeginMode3D() and EndMode3D(). `partial` is how far this frame is between world ticks.
    void Draw(const World& world, float partial) const;

private:
    struct Entity {
        bool tnt = false;
        int id = 0, meta = 0;
        double x, y, z;
        double prevX, prevY, prevZ;
        double motionX = 0, motionY = 0, motionZ = 0;
        int age = 0;
        int fuse = 0;
        bool onGround = false;
        bool dead = false;
    };

    const ItemModels& models;
    std::vector<Entity> entities;
    float skyDarken = 0.0f;
    Shader shader;
    int light0Loc, light1Loc, brightnessLoc, fogColorLoc, fogStartLoc, fogEndLoc, fogDensityLoc, cameraPosLoc;
    Material material;
};
