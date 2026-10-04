#pragma once

// Shared by the Blocks*.cpp files that define every block.

#include "Block.h"
#include "Items.h"
#include "JavaRandom.h"
#include "Player.h"
#include "World.h"

#include <memory>
#include <string>
#include <vector>

// The registry each Blocks*.cpp file adds its blocks to.
struct BlockRegistry {
    std::unique_ptr<Block> blocks[MAX_BLOCK_ID];

    template <typename T, typename... Args>
    T& Add(int id, Args&&... args) {
        auto block = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *block;
        ref.id = (BlockId)id;
        blocks[id] = std::move(block);
        return ref;
    }
};

void RegisterBasicBlocks(BlockRegistry& r);
void RegisterPlantBlocks(BlockRegistry& r);
void RegisterFluidBlocks(BlockRegistry& r);
void RegisterRedstoneBlocks(BlockRegistry& r);
void RegisterBuildingBlocks(BlockRegistry& r);
void RegisterContainerBlocks(BlockRegistry& r);
void RegisterPistonBlocks(BlockRegistry& r);
void RegisterStairs(BlockRegistry& r); // last: copies other blocks

// Minecraft's Block setters, chainable: hardness also sets a matching explosion
// resistance (setHardness), and setResistance(r) is stored as r * 3 / 5.
template <typename T>
T& Props(T& block, const char* name, int texture, float hardness, BlockMaterial material, StepSound sound) {
    block.name = name;
    block.texture = texture;
    block.hardness = hardness;
    block.resistance = hardness; // Minecraft: blockResistance = hardness * 5, used as / 5
    block.material = material;
    block.sound = sound;
    return block;
}
inline void SetResistance(Block& block, float resistance) { block.resistance = resistance * 3.0f / 5.0f; }
// Not a full opaque cube: light and faces pass around it.
inline void SetNotCube(Block& block, Shape shape, Layer layer, int lightOpacity) {
    block.shape = shape;
    block.layer = layer;
    block.opaqueCube = false;
    block.normalCube = false;
    block.lightOpacity = lightOpacity;
}

// The direction a player faces, in Minecraft's 4-way yaw index:
// floor(yaw * 4 / 360 + 0.5) & 3 -> 0 south (+Z), 1 west, 2 north, 3 east.
int PlayerYawIndex(const Player& player);
// The same, but for `(yaw + 180) * 16 / 360` (signs) and door-style `- 0.5` rounding.
int PlayerYawIndex16(const Player& player);
int PlayerDoorIndex(const Player& player);
// Which of the six directions the player looks along most (pistons, dispensers).
int PlayerFacingSide(const Player& player, int x, int y, int z);

// A box's faces inset for drawing; used by several shapes.
inline BlockBox MakeBox(float x0, float y0, float z0, float x1, float y1, float z1) {
    BlockBox b;
    b.minX = x0; b.minY = y0; b.minZ = z0; b.maxX = x1; b.maxY = y1; b.maxZ = z1;
    return b;
}

// Pops `stack` out of the block like Block.dropBlockAsItem_do.
void DropStack(World& world, int x, int y, int z, const ItemStack& stack);

// Removes a block that can no longer stay (a torch whose wall is gone...), dropping it.
void BreakAndDrop(World& world, int x, int y, int z);

// Can something that needs support (a torch, a pressure plate) sit on top of this block?
bool CanSupportOnTop(const World& world, int x, int y, int z);
