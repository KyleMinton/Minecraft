#pragma once

#include "Fluids.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Axis-aligned box movement against blocks' real collision boxes, shared by the
// player, dropped items and falling blocks: Beta's Entity.moveEntity, which
// clips the motion one axis at a time (Y, then X, then Z) and can step up
// onto something as high as `stepHeight`.

struct Box {
    double min[3];
    double max[3];

    Box Offset(double x, double y, double z) const {
        return { { min[0] + x, min[1] + y, min[2] + z }, { max[0] + x, max[1] + y, max[2] + z } };
    }
    // The box grown to cover a move (AxisAlignedBB.addCoord).
    Box Expand(double x, double y, double z) const {
        Box b = *this;
        (x < 0 ? b.min[0] : b.max[0]) += x;
        (y < 0 ? b.min[1] : b.max[1]) += y;
        (z < 0 ? b.min[2] : b.max[2]) += z;
        return b;
    }
    Box Contract(double x, double y, double z) const {
        return { { min[0] + x, min[1] + y, min[2] + z }, { max[0] - x, max[1] - y, max[2] - z } };
    }
};

inline Box ToBox(const BlockBox& b) { return { { b.minX, b.minY, b.minZ }, { b.maxX, b.maxY, b.maxZ } }; }

inline bool Overlaps(const Box& a, const Box& b) {
    for (int i = 0; i < 3; i++) {
        if (a.max[i] <= b.min[i] || a.min[i] >= b.max[i]) return false;
    }
    return true;
}

inline void CollidingBoxes(const World& world, const Box& region, std::vector<Box>& out) {
    std::vector<BlockBox> boxes;
    world.CollisionBoxes(region.min, region.max, boxes);
    out.clear();
    for (const BlockBox& b : boxes) out.push_back(ToBox(b));
}

inline bool AnyCollision(const World& world, const Box& box) {
    std::vector<Box> boxes;
    CollidingBoxes(world, box, boxes);
    return !boxes.empty();
}

// AxisAlignedBB.calculateXOffset and friends: how far `box` can move along
// `axis` before touching `other`.
inline double ClipOffset(const Box& box, const Box& other, int axis, double d) {
    int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    if (other.max[a1] <= box.min[a1] || other.min[a1] >= box.max[a1]) return d;
    if (other.max[a2] <= box.min[a2] || other.min[a2] >= box.max[a2]) return d;
    if (d > 0.0 && other.min[axis] >= box.max[axis]) d = std::min(d, other.min[axis] - box.max[axis]);
    if (d < 0.0 && other.max[axis] <= box.min[axis]) d = std::max(d, other.max[axis] - box.min[axis]);
    return d;
}

struct MoveResult {
    double dx = 0, dy = 0, dz = 0;   // how far it actually moved
    bool collidedX = false, collidedY = false, collidedZ = false;
    bool onGround = false;
    double stepped = 0;              // height climbed by stepping up
};

inline MoveResult MoveBox(const World& world, Box& box, double dx, double dy, double dz, double stepHeight, bool wasOnGround) {
    const double wantX = dx, wantY = dy, wantZ = dz;
    const Box start = box;
    std::vector<Box> boxes;
    CollidingBoxes(world, box.Expand(dx, dy, dz), boxes);

    for (const Box& b : boxes) dy = ClipOffset(box, b, 1, dy);
    box = box.Offset(0, dy, 0);
    bool groundish = wasOnGround || (wantY != dy && wantY < 0.0);
    for (const Box& b : boxes) dx = ClipOffset(box, b, 0, dx);
    box = box.Offset(dx, 0, 0);
    for (const Box& b : boxes) dz = ClipOffset(box, b, 2, dz);
    box = box.Offset(0, 0, dz);

    double stepped = 0.0;
    if (stepHeight > 0.0 && groundish && (wantX != dx || wantZ != dz)) {
        // Try again from the start, lifted by the step height, then settle back down.
        double firstX = dx, firstY = dy, firstZ = dz;
        Box firstBox = box;
        double sx = wantX, sy = stepHeight, sz = wantZ;
        Box b2 = start;
        CollidingBoxes(world, b2.Expand(wantX, stepHeight, wantZ), boxes);
        for (const Box& b : boxes) sy = ClipOffset(b2, b, 1, sy);
        b2 = b2.Offset(0, sy, 0);
        for (const Box& b : boxes) sx = ClipOffset(b2, b, 0, sx);
        b2 = b2.Offset(sx, 0, 0);
        for (const Box& b : boxes) sz = ClipOffset(b2, b, 2, sz);
        b2 = b2.Offset(0, 0, sz);
        double down = -sy;
        CollidingBoxes(world, b2.Expand(0, down, 0), boxes);
        for (const Box& b : boxes) down = ClipOffset(b2, b, 1, down);
        b2 = b2.Offset(0, down, 0);
        if (firstX * firstX + firstZ * firstZ >= sx * sx + sz * sz) {
            dx = firstX; dy = firstY; dz = firstZ;
            box = firstBox;
        } else {
            dx = sx; dz = sz; dy = sy + down;
            box = b2;
            stepped = b2.min[1] - start.min[1];
        }
    }

    MoveResult r;
    r.dx = dx; r.dy = dy; r.dz = dz;
    r.collidedX = wantX != dx;
    r.collidedY = wantY != dy;
    r.collidedZ = wantZ != dz;
    r.onGround = wantY != dy && wantY < 0.0;
    r.stepped = stepped;
    return r;
}

// World.handleMaterialAcceleration: is the box in this liquid (up to its
// surface), and which way does the liquid push it?
inline bool InLiquid(const World& world, const Box& box, BlockMaterial material, Vector3* push = nullptr) {
    int x0 = (int)std::floor(box.min[0]), x1 = (int)std::floor(box.max[0] + 1.0);
    int y0 = (int)std::floor(box.min[1]), y1 = (int)std::floor(box.max[1] + 1.0);
    int z0 = (int)std::floor(box.min[2]), z1 = (int)std::floor(box.max[2] + 1.0);
    bool found = false;
    Vector3 flow = { 0, 0, 0 };
    for (int x = x0; x < x1; x++) {
        for (int y = y0; y < y1; y++) {
            for (int z = z0; z < z1; z++) {
                if (world.GetMaterial(x, y, z) != material) continue;
                double surface = (y + 1) - FluidHeightPercent(world.GetMeta(x, y, z));
                if (box.max[1] < surface) continue;
                found = true;
                if (push != nullptr) {
                    Vector3 v = FluidFlowVector(world, x, y, z);
                    flow.x += v.x; flow.y += v.y; flow.z += v.z;
                }
            }
        }
    }
    if (push != nullptr) *push = flow;
    return found;
}

// Is any part of the box inside cells of this material (World.isMaterialInBB)?
inline bool TouchesMaterial(const World& world, const Box& box, BlockMaterial material) {
    int x0 = (int)std::floor(box.min[0]), x1 = (int)std::floor(box.max[0] + 1.0);
    int y0 = (int)std::floor(box.min[1]), y1 = (int)std::floor(box.max[1] + 1.0);
    int z0 = (int)std::floor(box.min[2]), z1 = (int)std::floor(box.max[2] + 1.0);
    for (int x = x0; x < x1; x++)
        for (int y = y0; y < y1; y++)
            for (int z = z0; z < z1; z++)
                if (world.GetMaterial(x, y, z) == material) return true;
    return false;
}
