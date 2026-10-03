#pragma once

#include "raylib.h"
#include "Block.h"

// Draws the crack texture over a block being mined: Beta's terrain.png tiles
// 240-249 (generated placeholders if it's missing), over the block's outline.
// Construct after InitWindow() and destroy before CloseWindow().
class BreakOverlay {
public:
    static constexpr int STAGE_COUNT = 10;

    BreakOverlay();
    ~BreakOverlay();

    BreakOverlay(const BreakOverlay&) = delete;
    BreakOverlay& operator=(const BreakOverlay&) = delete;

    // `viewForward` is the direction the player looks; the cracks line up with it.
    void Draw(int x, int y, int z, int stage, Vector3 viewForward, BlockBox bounds);

private:
    Texture2D stages[STAGE_COUNT];
};
