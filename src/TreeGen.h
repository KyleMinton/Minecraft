#pragma once

#include "JavaRandom.h"

// Where a tree grows: the live world (a sapling growing) or a chunk being
// generated. Beta's generators only need to read and write blocks.
class TreeWorld {
public:
    virtual ~TreeWorld() = default;
    virtual int Get(int x, int y, int z) const = 0;
    virtual void Set(int x, int y, int z, int id, int meta) = 0;
    virtual int Height() const = 0;
};

// Beta 1.8's tree generators, ported as they are. Each checks for room and
// good ground first and returns false (placing nothing) if there isn't any.
bool GrowOakTree(TreeWorld& world, JavaRandom& random, int x, int y, int z);      // WorldGenTrees
bool GrowBirchTree(TreeWorld& world, JavaRandom& random, int x, int y, int z);    // WorldGenForest
bool GrowSpruceTree(TreeWorld& world, JavaRandom& random, int x, int y, int z);   // WorldGenTaiga2
bool GrowBigOakTree(TreeWorld& world, JavaRandom& random, int x, int y, int z);   // WorldGenBigTree
bool GrowHugeMushroom(TreeWorld& world, JavaRandom& random, int x, int y, int z, int type); // WorldGenBigMushroom (0 brown, 1 red)

class World;
// The live world, for saplings and bone meal.
class WorldTreeAccess : public TreeWorld {
public:
    explicit WorldTreeAccess(World& w) : world(w) {}
    int Get(int x, int y, int z) const override;
    void Set(int x, int y, int z, int id, int meta) override;
    int Height() const override;
private:
    World& world;
};
