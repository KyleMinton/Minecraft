#pragma once

#include "raylib.h"
#include "Models.h"
#include "Player.h"

// Draws the first-person hand: Steve's right arm when the hand is empty, or the
// held item otherwise (a little block, or the item's icon as a thin slab),
// placed and animated with Beta's first-person transforms
// (ItemRenderer.renderItemInFirstPerson), eating included. Drawn over the world
// with its own fixed 70-degree field of view.
// Construct after InitWindow() and destroy before CloseWindow().
class HandRenderer {
public:
    explicit HandRenderer(const ItemModels& models);
    ~HandRenderer();

    HandRenderer(const HandRenderer&) = delete;
    HandRenderer& operator=(const HandRenderer&) = delete;

    // Call after the 3D world is drawn. `brightness` is the light level at the player.
    // If the game is being drawn into a texture (for a menu's blur), pass it as `resume`.
    void Draw(const HandPose& pose, float brightness, const RenderTexture2D* resume = nullptr);

private:
    const ItemModels& models;
    Texture2D skin;
    Shader shader;
    int light0Loc, light1Loc, brightnessLoc;

    Mesh armMesh;
    Material armMaterial;
    Material blockMaterial;
    Material itemMaterial;
    RenderTexture2D target = {};
};
