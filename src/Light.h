#pragma once

// Minecraft's sky light rules, shared by the chunk (initial lighting) and the
// world (lighting across chunks and after edits).

constexpr int MAX_LIGHT = 15;

// The light a block gets from a neighbor lit at `level`: one less per block
// travelled, or the block's opacity if that's more. Full sunlight going
// straight down through clear blocks doesn't fade at all.
inline int LightInto(int level, int opacity, bool downward) {
    if (downward && level == MAX_LIGHT && opacity == 0) return MAX_LIGHT;
    return level - (opacity > 1 ? opacity : 1);
}

// The six neighbor directions; index 3 is straight down.
constexpr int LIGHT_DIRS[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
constexpr int LIGHT_DOWN = 3;
