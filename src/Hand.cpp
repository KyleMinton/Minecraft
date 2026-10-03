#include "Hand.h"
#include "Models.h"

#include "raymath.h"
#include "rlgl.h"

#include <cmath>

namespace {

constexpr float HAND_FOV = 70.0f; // Minecraft draws the hand at 70 degrees whatever your FOV setting

// The right arm in Minecraft's player model: a 4x12x4 box (in skin pixels) at
// (-3, -2, -2), pivoting at (-5, 2, 0), textured from skin pixel (40, 16).
constexpr float ARM_PIVOT_X = -5.0f, ARM_PIVOT_Y = 2.0f;
constexpr float ARM_IDLE_ROLL = 0.1f;
constexpr float MODEL_SCALE = 1.0f / 16.0f;

} // namespace

HandRenderer::HandRenderer(const ItemModels& itemModels) : models(itemModels) {
    skin = LoadTexture("resources/textures/entity/steve.png");
    SetTextureFilter(skin, TEXTURE_FILTER_POINT);

    shader = LoadShader("resources/shaders/hand.vs", "resources/shaders/hand.fs");
    light0Loc = GetShaderLocation(shader, "light0");
    light1Loc = GetShaderLocation(shader, "light1");
    brightnessLoc = GetShaderLocation(shader, "brightness");

    MeshData arm;
    float w = (float)skin.width, h = (float)skin.height;
    AddModelBox(arm, -3.0f, -2.0f, -2.0f, 4, 12, 4, 0.0f, 40, 16, w, h);
    if (skin.height >= 64) AddModelBox(arm, -3.0f, -2.0f, -2.0f, 4, 12, 4, 0.25f, 40, 32, w, h);
    armMesh = arm.Upload();
    armMaterial = MakeMaterial(shader, skin);
    blockMaterial = MakeMaterial(shader, models.Atlas());
    itemMaterial = MakeMaterial(shader, models.ItemSheet());
}

HandRenderer::~HandRenderer() {
    if (target.id != 0) UnloadRenderTexture(target);
    UnloadMesh(armMesh);
    MemFree(armMaterial.maps);
    MemFree(blockMaterial.maps);
    MemFree(itemMaterial.maps);
    UnloadShader(shader);
    UnloadTexture(skin);
}

void HandRenderer::Draw(const HandPose& pose, float brightness, const RenderTexture2D* resume) {
    // Item lighting is set in the frame of the view angles (RenderHelper.enableStandardItemLighting).
    Matrix lightFrame = MatrixMultiply(MatrixRotateY(pose.yaw * DEG2RAD), MatrixRotateX(pose.pitch * DEG2RAD));
    Vector3 light0 = Vector3Normalize(Vector3Transform(Vector3Normalize(ITEM_LIGHT0), lightFrame));
    Vector3 light1 = Vector3Normalize(Vector3Transform(Vector3Normalize(ITEM_LIGHT1), lightFrame));
    SetShaderValue(shader, light0Loc, &light0, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, light1Loc, &light1, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);

    float s = pose.swingProgress;
    float swingRoot = sinf(sqrtf(s) * PI);
    float swingSquare = sinf(s * s * PI);
    float lowered = 1.0f - pose.equipProgress;

    GlMatrixStack t;
    // Damage tilt, then view bobbing, so the hand moves with the camera.
    t.Rotate(pose.hurtRoll, 0, 0, 1);
    t.Translate(sinf(pose.bobPhase) * pose.bobAmount * 0.5f, -fabsf(cosf(pose.bobPhase) * pose.bobAmount), 0.0f);
    t.Rotate(sinf(pose.bobPhase) * pose.bobAmount * 3.0f, 0, 0, 1);
    t.Rotate(fabsf(cosf(pose.bobPhase - 0.2f) * pose.bobAmount) * 5.0f, 1, 0, 0);
    t.Rotate(pose.tilt, 1, 0, 0);
    // The hand trails quick view turns.
    t.Rotate(pose.lagPitch * 0.1f, 1, 0, 0);
    t.Rotate(pose.lagYaw * 0.1f, 0, 1, 0);

    const Mesh* mesh = nullptr;
    const Material* material = nullptr;
    const ItemStack& item = pose.item;

    if (item.IsEmpty()) {
        // Bare arm (ItemRenderer.renderItemInFirstPerson's empty hand).
        t.Translate(-0.3f * swingRoot, 0.4f * sinf(sqrtf(s) * PI * 2.0f), -0.4f * sinf(s * PI));
        t.Translate(0.64f, -0.6f, -0.72f);
        t.Translate(0.0f, lowered * -0.6f, 0.0f);
        t.Rotate(45.0f, 0, 1, 0);
        t.Rotate(swingRoot * 70.0f, 0, 1, 0);
        t.Rotate(swingSquare * -20.0f, 0, 0, 1);
        t.Translate(-1.0f, 3.6f, 3.5f);
        t.Rotate(120.0f, 0, 0, 1);
        t.Rotate(200.0f, 1, 0, 0);
        t.Rotate(-135.0f, 0, 1, 0);
        t.Translate(5.6f, 0.0f, 0.0f);
        t.Translate(ARM_PIVOT_X * MODEL_SCALE, ARM_PIVOT_Y * MODEL_SCALE, 0.0f);
        t.Rotate(ARM_IDLE_ROLL * RAD2DEG, 0, 0, 1);
        t.Scale(MODEL_SCALE);
        mesh = &armMesh;
        material = &armMaterial;
    } else {
        const float f5 = 0.8f;
        if (pose.useTicks >= 0.0f) {
            // Eating: the food comes up to your mouth and bobs.
            float remaining = pose.useDuration - pose.useTicks + 1.0f;
            float progress = 1.0f - remaining / pose.useDuration;
            float f = 1.0f - progress;
            f = f * f * f;
            f = f * f * f;
            f = f * f * f;
            float lift = 1.0f - f;
            t.Translate(0.0f, fabsf(cosf(remaining / 4.0f * PI) * 0.1f) * (progress <= 0.2f ? 0.0f : 1.0f), 0.0f);
            t.Translate(lift * 0.6f, -lift * 0.5f, 0.0f);
            t.Rotate(lift * 90.0f, 0, 1, 0);
            t.Rotate(lift * 10.0f, 1, 0, 0);
            t.Rotate(lift * 30.0f, 0, 0, 1);
        } else {
            t.Translate(-0.4f * swingRoot, 0.2f * sinf(sqrtf(s) * PI * 2.0f), -0.2f * sinf(s * PI));
        }
        t.Translate(0.7f * f5, -0.65f * f5 - lowered * 0.6f, -0.9f * f5);
        t.Rotate(45.0f, 0, 1, 0);
        t.Rotate(swingSquare * -20.0f, 0, 1, 0);
        t.Rotate(swingRoot * -20.0f, 0, 0, 1);
        t.Rotate(swingRoot * -80.0f, 1, 0, 0);
        t.Scale(0.4f);
        if (ItemModels::Is3D(item)) {
            mesh = &models.BlockModel(item.id, ItemRenderMeta(item));
            material = &blockMaterial;
        } else {
            // RenderItem's 2D items: the icon as a slab, tipped toward you.
            t.Translate(0.0f, -0.3f, 0.0f);
            t.Scale(1.5f);
            t.Rotate(50.0f, 0, 1, 0);
            t.Rotate(335.0f, 0, 0, 1);
            t.Translate(-0.9375f, -0.0625f, 0.0f);
            mesh = &models.FlatModel(item);
            material = models.Sheet(item).id == models.ItemSheet().id ? &itemMaterial : &blockMaterial;
        }
    }
    if (mesh->vertexCount == 0) return;

    // The hand has its own camera at the origin, and always draws over the world.
    Camera3D handCamera = { 0 };
    handCamera.position = Vector3{ 0.0f, 0.0f, 0.0f };
    handCamera.target = Vector3{ 0.0f, 0.0f, -1.0f };
    handCamera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    handCamera.fovy = HAND_FOV;
    handCamera.projection = CAMERA_PERSPECTIVE;

    // Drawn into its own buffer (with its own depth), then over the screen, so
    // it never goes behind the world but still hides its own far sides.
    int width = GetScreenWidth(), height = GetScreenHeight();
    if (target.id == 0 || target.texture.width != width || target.texture.height != height) {
        if (target.id != 0) UnloadRenderTexture(target);
        target = LoadRenderTexture(width, height);
    }
    rlDrawRenderBatchActive();
    BeginTextureMode(target);
        ClearBackground(BLANK);
        BeginMode3D(handCamera);
            DrawMesh(*mesh, *material, t.m);
        EndMode3D();
    EndTextureMode();
    if (resume != nullptr) BeginTextureMode(*resume);
    DrawTextureRec(target.texture, Rectangle{ 0, 0, (float)width, -(float)height }, Vector2{ 0, 0 }, WHITE);
}
