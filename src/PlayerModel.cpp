#include "PlayerModel.h"
#include "Models.h"

#include "rlgl.h"

#include <cmath>

namespace {

constexpr float MODEL_SCALE = 1.0f / 16.0f; // model pixels to blocks
constexpr float PLAYER_SCALE = 0.9375f;     // RenderPlayer shrinks the model slightly
constexpr float MODEL_Y_OFFSET = -1.501f;   // RenderLivingBase stands the model on its feet
constexpr float GUI_DEPTH = 50.0f;

} // namespace

PlayerModel::PlayerModel() {
    skin = LoadTexture("resources/textures/entity/steve.png");
    SetTextureFilter(skin, TEXTURE_FILTER_POINT);
    shader = LoadShader("resources/shaders/hand.vs", "resources/shaders/hand.fs");
    light0Loc = GetShaderLocation(shader, "light0");
    light1Loc = GetShaderLocation(shader, "light1");
    brightnessLoc = GetShaderLocation(shader, "brightness");

    // ModelBiped / ModelPlayer: each part's box, texture origin and pivot, then the
    // outer layer of 64x64 skins (older 64x32 skins reuse the right limbs for the left).
    float w = (float)skin.width, h = (float)skin.height;
    bool modern = skin.height >= 64;
    struct PartDef { float x, y, z; int dx, dy, dz; int u, v; int overU, overV; float overInflate; Vector3 pivot; };
    const PartDef defs[PART_COUNT] = {
        { -4, -8, -4, 8, 8, 8, 0, 0, 32, 0, 0.5f, { 0, 0, 0 } },                               // head + hat
        { -4, 0, -2, 8, 12, 4, 16, 16, 16, 32, 0.25f, { 0, 0, 0 } },                           // body + jacket
        { -3, -2, -2, 4, 12, 4, 40, 16, 40, 32, 0.25f, { -5, 2, 0 } },                         // right arm + sleeve
        { -1, -2, -2, 4, 12, 4, modern ? 32 : 40, modern ? 48 : 16, 48, 48, 0.25f, { 5, 2, 0 } }, // left arm + sleeve
        { -2, 0, -2, 4, 12, 4, 0, 16, 0, 32, 0.25f, { -1.9f, 12, 0 } },                         // right leg + pants
        { -2, 0, -2, 4, 12, 4, modern ? 16 : 0, modern ? 48 : 16, 0, 48, 0.25f, { 1.9f, 12, 0 } }, // left leg + pants
    };
    for (int i = 0; i < PART_COUNT; i++) {
        const PartDef& d = defs[i];
        MeshData mesh;
        AddModelBox(mesh, d.x, d.y, d.z, d.dx, d.dy, d.dz, 0.0f, d.u, d.v, w, h);
        if (modern || i == HEAD) AddModelBox(mesh, d.x, d.y, d.z, d.dx, d.dy, d.dz, d.overInflate, d.overU, d.overV, w, h);
        parts[i] = mesh.Upload();
        pivots[i] = d.pivot;
    }
    material = MakeMaterial(shader, skin);
}

PlayerModel::~PlayerModel() {
    for (Mesh& mesh : parts) UnloadMesh(mesh);
    MemFree(material.maps);
    if (target.id != 0) UnloadRenderTexture(target);
    UnloadShader(shader);
    UnloadTexture(skin);
}

void PlayerModel::Render(int guiScale, float x, float y, float size, float mouseX, float mouseY,
                         float ageInTicks, float brightness) {
    int width = GetScreenWidth(), height = GetScreenHeight();
    if (target.id == 0 || target.texture.width != width || target.texture.height != height) {
        if (target.id != 0) UnloadRenderTexture(target);
        target = LoadRenderTexture(width, height);
        SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
    }

    // How far the mouse is from the player decides where it looks (GuiInventory).
    float lookX = x - mouseX, lookY = (y - 50.0f) - mouseY;
    float bodyYaw = atanf(lookX / 40.0f) * 20.0f;
    float headYaw = atanf(lookX / 40.0f) * 40.0f;
    float headPitch = -atanf(lookY / 40.0f) * 20.0f;

    // drawEntityOnScreen, then RenderLivingBase (degrees, OpenGL call order).
    GlMatrixStack t;
    t.Translate(x, y, GUI_DEPTH);
    t.Scale(-size, size, size);
    t.Rotate(180.0f, 0, 0, 1);

    // The item lights are placed while the model is turned 135 degrees, so they
    // light it from the front-left; directions ignore the translation and size.
    GlMatrixStack lightFrame;
    lightFrame.Scale(-1.0f, 1.0f, 1.0f);
    lightFrame.Rotate(180.0f, 0, 0, 1);
    lightFrame.Rotate(135.0f, 0, 1, 0);
    Vector3 light0 = Vector3Normalize(Vector3Transform(Vector3Normalize(ITEM_LIGHT0), lightFrame.m));
    Vector3 light1 = Vector3Normalize(Vector3Transform(Vector3Normalize(ITEM_LIGHT1), lightFrame.m));
    SetShaderValue(shader, light0Loc, &light0, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, light1Loc, &light1, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);

    t.Rotate(-atanf(lookY / 40.0f) * 20.0f, 1, 0, 0);
    t.Rotate(180.0f - bodyYaw, 0, 1, 0);
    t.Scale(-1.0f, -1.0f, 1.0f);
    t.Scale(PLAYER_SCALE);
    t.Translate(0.0f, MODEL_Y_OFFSET, 0.0f);

    // ModelBiped.setRotationAngles (radians): the head follows the look, and the
    // arms sway slightly with time.
    float sway = cosf(ageInTicks * 0.09f) * 0.05f + 0.05f;
    float swing = sinf(ageInTicks * 0.067f) * 0.05f;
    Vector3 angles[PART_COUNT] = {};
    angles[HEAD] = Vector3{ headPitch * DEG2RAD, (headYaw - bodyYaw) * DEG2RAD, 0.0f };
    angles[RIGHT_ARM] = Vector3{ swing, 0.0f, sway };
    angles[LEFT_ARM] = Vector3{ -swing, 0.0f, -sway };

    BeginTextureMode(target);
        ClearBackground(BLANK);
        rlMatrixMode(RL_PROJECTION);
        rlLoadIdentity();
        rlOrtho(0.0, (double)width / guiScale, (double)height / guiScale, 0.0, -1000.0, 1000.0);
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        rlEnableDepthTest();
        rlDisableBackfaceCulling(); // Minecraft draws living entities with culling off

        for (int i = 0; i < PART_COUNT; i++) {
            // ModelRenderer.render: move to the pivot, rotate Z, Y, X, then pixels to blocks.
            GlMatrixStack p = t;
            p.Translate(pivots[i].x * MODEL_SCALE, pivots[i].y * MODEL_SCALE, pivots[i].z * MODEL_SCALE);
            if (angles[i].z != 0.0f) p.Rotate(angles[i].z * RAD2DEG, 0, 0, 1);
            if (angles[i].y != 0.0f) p.Rotate(angles[i].y * RAD2DEG, 0, 1, 0);
            if (angles[i].x != 0.0f) p.Rotate(angles[i].x * RAD2DEG, 1, 0, 0);
            p.Scale(MODEL_SCALE);
            DrawMesh(parts[i], material, p.m);
        }

        rlDrawRenderBatchActive();
        rlEnableBackfaceCulling();
        rlDisableDepthTest();
    EndTextureMode();
}

void PlayerModel::Draw() const {
    if (target.id == 0) return;
    Rectangle flipped = { 0.0f, 0.0f, (float)target.texture.width, -(float)target.texture.height };
    DrawTextureRec(target.texture, flipped, Vector2{ 0.0f, 0.0f }, WHITE);
}
