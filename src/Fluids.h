#pragma once

#include "raylib.h"
#include "Block.h"

class World;

// Liquid geometry shared by the renderer and entity movement (BlockFluid).

// How full a liquid block with this metadata looks, 0..1 (getFluidHeightPercent).
float FluidHeightPercent(int meta);
// The surface height at the corner shared by the 4 cells around (x, z) (RenderBlocks.getFluidHeight).
float FluidCornerHeight(const World& world, int x, int y, int z, BlockMaterial material);
// Which way the liquid in this cell flows (BlockFluid.getFlowVector), normalized; zero for still.
Vector3 FluidFlowVector(const World& world, int x, int y, int z);
// The flow direction's angle for the texture, or -1000 for still liquid (BlockFluid.func_293_a).
double FluidFlowAngle(const World& world, int x, int y, int z, BlockMaterial material);
