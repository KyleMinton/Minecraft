#pragma once

#include "raylib.h"
#include "Inventory.h"
#include "Models.h"
#include "World.h"
#include "SoundSystem.h"

#include <iosfwd>
#include <map>
#include <vector>

class Player;

// Items lying in the world (Minecraft's EntityItem): little spinning blocks or
// flat item sprites that fall, slide and bounce, merge with nearby items of the
// same kind, burn up in lava, get picked up when you walk over them (with the
// "pop" and the fly-into-you animation), and vanish after 5 minutes.
// Construct after InitWindow() and destroy before CloseWindow().
class DroppedItems {
public:
    explicit DroppedItems(const ItemModels& models);
    ~DroppedItems();

    DroppedItems(const DroppedItems&) = delete;
    DroppedItems& operator=(const DroppedItems&) = delete;

    // An item at (x, y, z) (the bottom-center of its 0.25-block box) with the given
    // motion, which can't be picked up for `pickupDelay` ticks.
    void Spawn(double x, double y, double z, double motionX, double motionY, double motionZ,
               const ItemStack& stack, int pickupDelay);

    // Block.dropBlockAsItem: somewhere inside the block, popping up in a random direction.
    void SpawnFromBlock(int x, int y, int z, const ItemStack& stack);

    // Runs the 20 Hz item ticks due this frame and hands items to the player.
    void Update(float dt, const World& world, Player& player, SoundSystem& sounds);

    // Same fog and time of day as the terrain.
    void SetFog(Vector3 color, float start, float end, float density, float skyDarken, Vector3 cameraPos);

    // Call between BeginMode3D() and EndMode3D().
    void Draw(const Player& player, const World& world, const Camera3D& camera) const;

    bool AnyInBox(const double min[3], const double max[3]) const;
    size_t Count() const { return items.size(); }

    // items.dat: one "item2 id count damage x y z motionX motionY motionZ age pickupDelay" line each.
    void Save(std::ostream& out) const;
    void Load(std::istream& in);
    void Clear();

private:
    struct Item {
        double x, y, z;
        double prevX, prevY, prevZ;
        double motionX, motionY, motionZ;
        ItemStack stack;
        int age = 0;
        int pickupDelay = 0;
        float hoverStart = 0.0f; // random phase for the bob and spin
        bool onGround = false;
        bool dead = false;
    };
    struct Pickup {
        Item item;  // frozen where it was picked up
        int age = 0;
    };

    const ItemModels& models;
    std::vector<Item> items;
    std::vector<Pickup> pickups;
    double tickAccumulator = 0.0;
    float skyDarken = 0.0f;
    float alpha = 0.0f;

    Shader shader;
    int light0Loc, light1Loc, brightnessLoc, fogColorLoc, fogStartLoc, fogEndLoc, fogDensityLoc, cameraPosLoc;
    Material blockMaterial, itemMaterial;
    mutable std::map<int, Mesh> sprites; // flat items, as camera-facing quads

    void Tick(const World& world, Player& player, SoundSystem& sounds);
    void Move(Item& item, const World& world);
    void PushOutOfBlocks(Item& item, const World& world);
    void MergeNearby(Item& item);
    const Mesh& Sprite(const ItemStack& stack) const;
    void DrawItem(const Item& item, double x, double y, double z, float partial, const World& world, const Camera3D& camera) const;
};
