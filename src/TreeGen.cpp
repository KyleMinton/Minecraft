#include "TreeGen.h"
#include "Block.h"
#include "World.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

bool OpaqueAt(const TreeWorld& world, int x, int y, int z) {
    return BlockIsOpaqueCube((uint8_t)world.Get(x, y, z));
}

// The trunk and small round canopy shared by oak and birch (WorldGenTrees / WorldGenForest).
bool GrowSmallTree(TreeWorld& world, JavaRandom& random, int x, int y, int z, int height, int woodMeta) {
    int top = world.Height();
    if (y < 1 || y + height + 1 > top) return false;
    for (int yy = y; yy <= y + 1 + height; yy++) {
        int r = 1;
        if (yy == y) r = 0;
        if (yy >= y + 1 + height - 2) r = 2;
        for (int xx = x - r; xx <= x + r; xx++)
            for (int zz = z - r; zz <= z + r; zz++) {
                if (yy < 0 || yy >= top) return false;
                int id = world.Get(xx, yy, zz);
                if (id != BLOCK_AIR && id != BLOCK_LEAVES) return false;
            }
    }
    int ground = world.Get(x, y - 1, z);
    if ((ground != BLOCK_GRASS && ground != BLOCK_DIRT) || y >= top - height - 1) return false;
    world.Set(x, y - 1, z, BLOCK_DIRT, 0);

    for (int yy = y - 3 + height; yy <= y + height; yy++) {
        int fromTop = yy - (y + height);
        int r = 1 - fromTop / 2;
        for (int xx = x - r; xx <= x + r; xx++) {
            int dx = xx - x;
            for (int zz = z - r; zz <= z + r; zz++) {
                int dz = zz - z;
                if ((std::abs(dx) != r || std::abs(dz) != r || (random.NextInt(2) != 0 && fromTop != 0)) && !OpaqueAt(world, xx, yy, zz)) {
                    world.Set(xx, yy, zz, BLOCK_LEAVES, woodMeta);
                }
            }
        }
    }
    for (int i = 0; i < height; i++) {
        int id = world.Get(x, y + i, z);
        if (id == BLOCK_AIR || id == BLOCK_LEAVES) world.Set(x, y + i, z, BLOCK_LOG, woodMeta);
    }
    return true;
}

// ---- WorldGenBigTree

class BigTree {
public:
    BigTree(TreeWorld& w) : world(w) {}

    bool Generate(JavaRandom& parentRandom, int x, int y, int z) {
        rand.SetSeed(parentRandom.NextLong());
        base[0] = x; base[1] = y; base[2] = z;
        if (heightLimit == 0) heightLimit = 5 + rand.NextInt(heightLimitLimit);
        if (!ValidLocation()) return false;
        GenerateLeafNodeList();
        GenerateLeaves();
        GenerateTrunk();
        GenerateLeafNodeBases();
        return true;
    }

private:
    static constexpr int OTHER[6] = { 2, 0, 0, 1, 2, 1 };
    TreeWorld& world;
    JavaRandom rand{ 0 };
    int base[3] = { 0, 0, 0 };
    int heightLimit = 0;
    int height = 0;
    double heightAttenuation = 0.618;
    double branchSlope = 0.381;
    double scaleWidth = 1.0;
    double leafDensity = 1.0;
    int heightLimitLimit = 12;
    int leafDistanceLimit = 4;
    std::vector<std::array<int, 4>> leafNodes;

    void SetBlock(int x, int y, int z, int id) { world.Set(x, y, z, id, 0); }

    float LayerSize(int i) const {
        if ((double)i < (double)(float)heightLimit * 0.3) return -1.618f;
        float half = heightLimit / 2.0f;
        float offset = heightLimit / 2.0f - i;
        float size;
        if (offset == 0.0f) size = half;
        else if (std::fabs(offset) >= half) size = 0.0f;
        else size = (float)std::sqrt(std::pow(std::fabs(half), 2.0) - std::pow(std::fabs(offset), 2.0));
        return size * 0.5f;
    }

    float LeafSize(int i) const {
        if (i < 0 || i >= leafDistanceLimit) return -1.0f;
        return i != 0 && i != leafDistanceLimit - 1 ? 3.0f : 2.0f;
    }

    void LeafLayer(int x, int y, int z, float size, int axis, int id) {
        int r = (int)((double)size + 0.618);
        int a1 = OTHER[axis], a2 = OTHER[axis + 3];
        int center[3] = { x, y, z };
        int pos[3] = { 0, 0, 0 };
        pos[axis] = center[axis];
        for (int i = -r; i <= r; i++) {
            pos[a1] = center[a1] + i;
            for (int j = -r; j <= r; j++) {
                double d = std::sqrt(std::pow(std::abs(i) + 0.5, 2.0) + std::pow(std::abs(j) + 0.5, 2.0));
                if (d > size) continue;
                pos[a2] = center[a2] + j;
                int there = world.Get(pos[0], pos[1], pos[2]);
                if (there == BLOCK_AIR || there == BLOCK_LEAVES) SetBlock(pos[0], pos[1], pos[2], id);
            }
        }
    }

    void LeafNode(int x, int y, int z) {
        for (int yy = y; yy < y + leafDistanceLimit; yy++) LeafLayer(x, yy, z, LeafSize(yy - y), 1, BLOCK_LEAVES);
    }

    void PlaceLine(const int from[3], const int to[3], int id) {
        int delta[3], major = 0;
        for (int a = 0; a < 3; a++) {
            delta[a] = to[a] - from[a];
            if (std::abs(delta[a]) > std::abs(delta[major])) major = a;
        }
        if (delta[major] == 0) return;
        int a1 = OTHER[major], a2 = OTHER[major + 3];
        int step = delta[major] > 0 ? 1 : -1;
        double d1 = (double)delta[a1] / delta[major], d2 = (double)delta[a2] / delta[major];
        int pos[3];
        for (int k = 0, end = delta[major] + step; k != end; k += step) {
            pos[major] = (int)std::floor(from[major] + k + 0.5);
            pos[a1] = (int)std::floor(from[a1] + k * d1 + 0.5);
            pos[a2] = (int)std::floor(from[a2] + k * d2 + 0.5);
            SetBlock(pos[0], pos[1], pos[2], id);
        }
    }

    int CheckLine(const int from[3], const int to[3]) const {
        int delta[3], major = 0;
        for (int a = 0; a < 3; a++) {
            delta[a] = to[a] - from[a];
            if (std::abs(delta[a]) > std::abs(delta[major])) major = a;
        }
        if (delta[major] == 0) return -1;
        int a1 = OTHER[major], a2 = OTHER[major + 3];
        int step = delta[major] > 0 ? 1 : -1;
        double d1 = (double)delta[a1] / delta[major], d2 = (double)delta[a2] / delta[major];
        int pos[3];
        int j = 0, end = delta[major] + step;
        for (; j != end; j += step) {
            pos[major] = from[major] + j;
            pos[a1] = (int)std::floor(from[a1] + j * d1);
            pos[a2] = (int)std::floor(from[a2] + j * d2);
            int id = world.Get(pos[0], pos[1], pos[2]);
            if (id != BLOCK_AIR && id != BLOCK_LEAVES) break;
        }
        return j == end ? -1 : std::abs(j);
    }

    bool ValidLocation() {
        int from[3] = { base[0], base[1], base[2] };
        int to[3] = { base[0], base[1] + heightLimit - 1, base[2] };
        int ground = world.Get(base[0], base[1] - 1, base[2]);
        if (ground != BLOCK_GRASS && ground != BLOCK_DIRT) return false;
        if (base[1] + heightLimit >= world.Height()) heightLimit = world.Height() - base[1] - 1;
        if (heightLimit < 6) return false;
        int blocked = CheckLine(from, to);
        if (blocked == -1) return true;
        if (blocked < 6) return false;
        heightLimit = blocked;
        return true;
    }

    void GenerateLeafNodeList() {
        height = (int)(heightLimit * heightAttenuation);
        if (height >= heightLimit) height = heightLimit - 1;
        int perLayer = (int)(1.382 + std::pow(leafDensity * heightLimit / 13.0, 2.0));
        if (perLayer < 1) perLayer = 1;
        std::vector<std::array<int, 4>> nodes;
        int y = base[1] + heightLimit - leafDistanceLimit;
        int trunkTop = base[1] + height;
        int layer = y - base[1];
        nodes.push_back({ base[0], y, base[2], trunkTop });
        y--;
        while (layer >= 0) {
            float size = LayerSize(layer);
            if (size < 0.0f) {
                y--;
                layer--;
                continue;
            }
            for (int n = 0; n < perLayer; n++) {
                double dist = scaleWidth * (size * (rand.NextFloat() + 0.328));
                double angle = rand.NextFloat() * 2.0 * 3.14159;
                int nx = (int)std::floor(dist * std::sin(angle) + base[0] + 0.5);
                int nz = (int)std::floor(dist * std::cos(angle) + base[2] + 0.5);
                int node[3] = { nx, y, nz };
                int nodeTop[3] = { nx, y + leafDistanceLimit, nz };
                if (CheckLine(node, nodeTop) != -1) continue;
                int branchBase[3] = { base[0], base[1], base[2] };
                double horizontal = std::sqrt(std::pow(std::abs(base[0] - node[0]), 2.0) + std::pow(std::abs(base[2] - node[2]), 2.0));
                double drop = horizontal * branchSlope;
                if (node[1] - drop > trunkTop) branchBase[1] = trunkTop;
                else branchBase[1] = (int)(node[1] - drop);
                if (CheckLine(branchBase, node) == -1) nodes.push_back({ nx, y, nz, branchBase[1] });
            }
            y--;
            layer--;
        }
        leafNodes = nodes;
    }

    void GenerateLeaves() {
        for (const auto& node : leafNodes) LeafNode(node[0], node[1], node[2]);
    }

    void GenerateTrunk() {
        int from[3] = { base[0], base[1], base[2] };
        int to[3] = { base[0], base[1] + height, base[2] };
        PlaceLine(from, to, BLOCK_LOG);
    }

    void GenerateLeafNodeBases() {
        int from[3] = { base[0], base[1], base[2] };
        for (const auto& node : leafNodes) {
            int to[3] = { node[0], node[1], node[2] };
            from[1] = node[3];
            if ((double)(from[1] - base[1]) >= heightLimit * 0.2) PlaceLine(from, to, BLOCK_LOG);
        }
    }
};

} // namespace

bool GrowOakTree(TreeWorld& world, JavaRandom& random, int x, int y, int z) {
    return GrowSmallTree(world, random, x, y, z, random.NextInt(3) + 4, 0);
}

bool GrowBirchTree(TreeWorld& world, JavaRandom& random, int x, int y, int z) {
    return GrowSmallTree(world, random, x, y, z, random.NextInt(3) + 5, 2);
}

bool GrowSpruceTree(TreeWorld& world, JavaRandom& random, int x, int y, int z) {
    int height = random.NextInt(4) + 6;
    int bare = 1 + random.NextInt(2);
    int leafy = height - bare;
    int maxRadius = 2 + random.NextInt(2);
    int top = world.Height();
    if (y < 1 || y + height + 1 > top) return false;
    for (int yy = y; yy <= y + 1 + height; yy++) {
        int r = (yy - y < bare) ? 0 : maxRadius;
        for (int xx = x - r; xx <= x + r; xx++)
            for (int zz = z - r; zz <= z + r; zz++) {
                if (yy < 0 || yy >= top) return false;
                int id = world.Get(xx, yy, zz);
                if (id != BLOCK_AIR && id != BLOCK_LEAVES) return false;
            }
    }
    int ground = world.Get(x, y - 1, z);
    if ((ground != BLOCK_GRASS && ground != BLOCK_DIRT) || y >= top - height - 1) return false;
    world.Set(x, y - 1, z, BLOCK_DIRT, 0);

    int r = random.NextInt(2);
    int limit = 1;
    bool started = false;
    for (int i = 0; i <= leafy; i++) {
        int yy = y + height - i;
        for (int xx = x - r; xx <= x + r; xx++) {
            int dx = xx - x;
            for (int zz = z - r; zz <= z + r; zz++) {
                int dz = zz - z;
                if ((std::abs(dx) != r || std::abs(dz) != r || r <= 0) && !OpaqueAt(world, xx, yy, zz)) {
                    world.Set(xx, yy, zz, BLOCK_LEAVES, 1);
                }
            }
        }
        if (r >= limit) {
            r = started ? 1 : 0;
            started = true;
            if (++limit > maxRadius) limit = maxRadius;
        } else {
            r++;
        }
    }
    int shorter = random.NextInt(3);
    for (int i = 0; i < height - shorter; i++) {
        int id = world.Get(x, y + i, z);
        if (id == BLOCK_AIR || id == BLOCK_LEAVES) world.Set(x, y + i, z, BLOCK_LOG, 1);
    }
    return true;
}

bool GrowBigOakTree(TreeWorld& world, JavaRandom& random, int x, int y, int z) {
    BigTree tree(world);
    return tree.Generate(random, x, y, z);
}

bool GrowHugeMushroom(TreeWorld& world, JavaRandom& random, int x, int y, int z, int type) {
    int stem = random.NextInt(3) + 4;
    int top = world.Height();
    if (y < 1 || y + stem + 1 > top) return false;
    for (int yy = y; yy <= y + 1 + stem; yy++) {
        int r = (yy == y) ? 0 : 3;
        for (int xx = x - r; xx <= x + r; xx++)
            for (int zz = z - r; zz <= z + r; zz++) {
                if (yy < 0 || yy >= top) return false;
                int id = world.Get(xx, yy, zz);
                if (id != BLOCK_AIR && id != BLOCK_LEAVES) return false;
            }
    }
    int ground = world.Get(x, y - 1, z);
    if (ground != BLOCK_DIRT && ground != BLOCK_GRASS) return false;
    world.Set(x, y - 1, z, BLOCK_DIRT, 0);

    int capId = BLOCK_BROWN_MUSHROOM_BLOCK + type;
    int capBottom = y + stem;
    if (type == 1) capBottom = y + stem - 3;
    for (int yy = capBottom; yy <= y + stem; yy++) {
        int r = 1;
        if (yy < y + stem) r++;
        if (type == 0) r = 3;
        for (int xx = x - r; xx <= x + r; xx++)
            for (int zz = z - r; zz <= z + r; zz++) {
                int m = 5;
                if (xx == x - r) m--;
                if (xx == x + r) m++;
                if (zz == z - r) m -= 3;
                if (zz == z + r) m += 3;
                if (type == 0 || yy < y + stem) {
                    if ((xx == x - r || xx == x + r) && (zz == z - r || zz == z + r)) continue;
                    if (xx == x - (r - 1) && zz == z - r) m = 1;
                    if (xx == x - r && zz == z - (r - 1)) m = 1;
                    if (xx == x + (r - 1) && zz == z - r) m = 3;
                    if (xx == x + r && zz == z - (r - 1)) m = 3;
                    if (xx == x - (r - 1) && zz == z + r) m = 7;
                    if (xx == x - r && zz == z + (r - 1)) m = 7;
                    if (xx == x + (r - 1) && zz == z + r) m = 9;
                    if (xx == x + r && zz == z + (r - 1)) m = 9;
                }
                if (m == 5 && yy < y + stem) m = 0;
                if ((m != 0 || y >= y + stem - 1) && !OpaqueAt(world, xx, yy, zz)) world.Set(xx, yy, zz, capId, m);
            }
    }
    for (int i = 0; i < stem; i++) {
        if (!OpaqueAt(world, x, y + i, z)) world.Set(x, y + i, z, capId, 10);
    }
    return true;
}

int WorldTreeAccess::Get(int x, int y, int z) const { return world.GetBlock(x, y, z); }
void WorldTreeAccess::Set(int x, int y, int z, int id, int meta) { world.SetBlock(x, y, z, id, meta, false); }
int WorldTreeAccess::Height() const { return Chunk::HEIGHT; }
