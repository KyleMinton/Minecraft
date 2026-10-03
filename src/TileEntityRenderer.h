#pragma once

#include "raylib.h"
#include "GuiFont.h"
#include "Models.h"

class World;

// Draws the blocks Beta draws with models instead of in the chunk mesh
// (TileEntitySpecialRenderer): chests with their opening lids (item/chest.png,
// largechest.png), signs with their text (item/sign.png), and blocks being
// moved by pistons.
// Construct after InitWindow() and destroy before CloseWindow().
class TileEntityRenderer {
public:
    TileEntityRenderer(const ItemModels& models, const GuiFont& font);
    ~TileEntityRenderer();

    TileEntityRenderer(const TileEntityRenderer&) = delete;
    TileEntityRenderer& operator=(const TileEntityRenderer&) = delete;

    void SetFog(Vector3 color, float start, float end, float density, float skyDarken, Vector3 cameraPos);
    // Call between BeginMode3D() and EndMode3D(). `partial` is how far this frame is between world ticks.
    void Draw(const World& world, Vector3 cameraPos, float partial) const;

private:
    const ItemModels& models;
    const GuiFont& font;
    Texture2D chestTexture, largeChestTexture, signTexture;
    Shader shader;
    int light0Loc, light1Loc, brightnessLoc, fogColorLoc, fogStartLoc, fogEndLoc, fogDensityLoc, cameraPosLoc;
    float skyDarken = 0.0f;

    // ModelChest / ModelLargeChest parts, in model pixels.
    Mesh chestBottom, chestLid, chestKnob;
    Mesh largeBottom, largeLid, largeKnob;
    Mesh signBoard, signStick;
    Material chestMaterial, largeChestMaterial, signMaterial, blockMaterial;

    float Brightness(const World& world, int x, int y, int z) const;
    void DrawChest(const World& world, int x, int y, int z, float partial) const;
    void DrawSign(const World& world, int x, int y, int z) const;
    void DrawMovingBlock(const World& world, int x, int y, int z, float partial) const;
};
