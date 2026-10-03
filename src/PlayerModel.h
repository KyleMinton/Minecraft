#pragma once

#include "raylib.h"

// Steve's whole body (Minecraft's ModelPlayer: head, body, arms and legs, plus
// the outer hat/jacket/sleeve/pants layers of 64x64 skins), as drawn in the
// survival inventory (GuiInventory.drawEntityOnScreen): the body and head turn
// to face the mouse and the arms sway gently.
// Construct after InitWindow() and destroy before CloseWindow().
class PlayerModel {
public:
    PlayerModel();
    ~PlayerModel();

    PlayerModel(const PlayerModel&) = delete;
    PlayerModel& operator=(const PlayerModel&) = delete;

    // Renders the player standing at GUI point (x, y) with `size` GUI pixels per
    // model unit, looking toward the mouse at GUI point (mouseX, mouseY). All GUI
    // points are multiplied by `guiScale` on screen. Uses a render texture, so call
    // it before BeginDrawing(), then Draw() it where it belongs in the GUI.
    void Render(int guiScale, float x, float y, float size, float mouseX, float mouseY,
                float ageInTicks, float brightness);

    void Draw() const;

private:
    enum Part { HEAD, BODY, RIGHT_ARM, LEFT_ARM, RIGHT_LEG, LEFT_LEG, PART_COUNT };

    Texture2D skin;
    Shader shader;
    int light0Loc, light1Loc, brightnessLoc;
    Mesh parts[PART_COUNT] = {};
    Vector3 pivots[PART_COUNT] = {};
    Material material;
    RenderTexture2D target = { 0 };
};
