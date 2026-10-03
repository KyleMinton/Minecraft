#include "TileEntityRenderer.h"
#include "Gui.h"
#include "Sky.h"
#include "TileEntity.h"
#include "World.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr float PIXEL = 1.0f / 16.0f;

Mesh BoxMesh(float x, float y, float z, int dx, int dy, int dz, int u, int v, float texWidth, float texHeight) {
    MeshData data;
    AddModelBox(data, x, y, z, dx, dy, dz, 0.0f, u, v, texWidth, texHeight);
    return data.Upload();
}

// A chest (single or double) next to this one along X or Z.
bool IsChest(const World& world, int x, int y, int z) {
    return world.GetBlock(x, y, z) == BLOCK_CHEST;
}

float LidAngle(const World& world, int x, int y, int z, float partial) {
    TileEntity* te = world.GetTileEntity(x, y, z);
    if (te == nullptr || te->kind != TileKind::Chest) return 0.0f;
    const ChestTile* chest = static_cast<const ChestTile*>(te);
    return chest->prevLidAngle + (chest->lidAngle - chest->prevLidAngle) * partial;
}

} // namespace

TileEntityRenderer::TileEntityRenderer(const ItemModels& itemModels, const GuiFont& guiFont) : models(itemModels), font(guiFont) {
    chestTexture = LoadPixelTexture("resources/textures/item/chest.png");
    largeChestTexture = LoadPixelTexture("resources/textures/item/largechest.png");
    signTexture = LoadPixelTexture("resources/textures/item/sign.png");

    shader = LoadShader("resources/shaders/entity.vs", "resources/shaders/entity.fs");
    light0Loc = GetShaderLocation(shader, "light0");
    light1Loc = GetShaderLocation(shader, "light1");
    brightnessLoc = GetShaderLocation(shader, "brightness");
    fogColorLoc = GetShaderLocation(shader, "fogColor");
    fogStartLoc = GetShaderLocation(shader, "fogStart");
    fogEndLoc = GetShaderLocation(shader, "fogEnd");
    fogDensityLoc = GetShaderLocation(shader, "fogDensity");
    cameraPosLoc = GetShaderLocation(shader, "cameraPos");
    Vector3 light0 = Vector3Normalize(ITEM_LIGHT0), light1 = Vector3Normalize(ITEM_LIGHT1);
    SetShaderValue(shader, light0Loc, &light0, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, light1Loc, &light1, SHADER_UNIFORM_VEC3);

    // ModelChest and ModelLargeChest.
    chestLid = BoxMesh(0, -5, -14, 14, 5, 14, 0, 0, 64, 64);
    chestKnob = BoxMesh(-1, -2, -15, 2, 4, 1, 0, 0, 64, 64);
    chestBottom = BoxMesh(0, 0, 0, 14, 10, 14, 0, 19, 64, 64);
    largeLid = BoxMesh(0, -5, -14, 30, 5, 14, 0, 0, 128, 64);
    largeKnob = BoxMesh(-1, -2, -15, 2, 4, 1, 0, 0, 128, 64);
    largeBottom = BoxMesh(0, 0, 0, 30, 10, 14, 0, 19, 128, 64);
    // ModelSign.
    signBoard = BoxMesh(-12, -14, -1, 24, 12, 2, 0, 0, 64, 32);
    signStick = BoxMesh(-1, -2, -1, 2, 14, 2, 0, 14, 64, 32);

    chestMaterial = MakeMaterial(shader, chestTexture);
    largeChestMaterial = MakeMaterial(shader, largeChestTexture);
    signMaterial = MakeMaterial(shader, signTexture);
    blockMaterial = MakeMaterial(shader, models.Atlas());
}

TileEntityRenderer::~TileEntityRenderer() {
    for (Mesh* mesh : { &chestLid, &chestKnob, &chestBottom, &largeLid, &largeKnob, &largeBottom, &signBoard, &signStick }) {
        UnloadMesh(*mesh);
    }
    for (Material* material : { &chestMaterial, &largeChestMaterial, &signMaterial, &blockMaterial }) MemFree(material->maps);
    UnloadShader(shader);
    UnloadTexture(chestTexture);
    UnloadTexture(largeChestTexture);
    UnloadTexture(signTexture);
}

void TileEntityRenderer::SetFog(Vector3 color, float start, float end, float density, float darken, Vector3 cameraPos) {
    skyDarken = darken;
    SetShaderValue(shader, fogColorLoc, &color, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, fogStartLoc, &start, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogEndLoc, &end, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, fogDensityLoc, &density, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, cameraPosLoc, &cameraPos, SHADER_UNIFORM_VEC3);
}

float TileEntityRenderer::Brightness(const World& world, int x, int y, int z) const {
    int level = std::max(world.GetSkyLight(x, y, z) - (int)skyDarken, world.GetBlockLight(x, y, z));
    // A tile entity's own cell is dark when its block is opaque to light; look above instead.
    if (level == 0) level = std::max(world.GetSkyLight(x, y + 1, z) - (int)skyDarken, world.GetBlockLight(x, y + 1, z));
    return LightLevelBrightness(level);
}

void TileEntityRenderer::Draw(const World& world, Vector3 cameraPos, float partial) const {
    const float maxDistance = 64.0f; // TileEntityRenderer only draws within 64 blocks
    world.ForEachTileEntity([&](int x, int y, int z, const TileEntity& tile) {
        float dx = x + 0.5f - cameraPos.x, dy = y + 0.5f - cameraPos.y, dz = z + 0.5f - cameraPos.z;
        if (dx * dx + dy * dy + dz * dz > maxDistance * maxDistance) return;
        if (!world.IsLoaded(x, z)) return;
        switch (tile.kind) {
            case TileKind::Chest: DrawChest(world, x, y, z, partial); break;
            case TileKind::Sign: DrawSign(world, x, y, z); break;
            case TileKind::Piston: DrawMovingBlock(world, x, y, z, partial); break;
            default: break;
        }
    });
}

// TileEntityChestRenderer: the chest at the -X/-Z end of a pair draws both halves.
void TileEntityRenderer::DrawChest(const World& world, int x, int y, int z, float partial) const {
    if (world.GetBlock(x, y, z) != BLOCK_CHEST) return;
    if (IsChest(world, x - 1, y, z) || IsChest(world, x, y, z - 1)) return;
    bool pairX = IsChest(world, x + 1, y, z), pairZ = IsChest(world, x, y, z + 1);
    bool large = pairX || pairZ;
    int meta = world.GetMeta(x, y, z);

    float brightness = Brightness(world, x, y, z);
    SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);

    float rotation = 0.0f;
    if (meta == 2) rotation = 180.0f;
    if (meta == 4) rotation = 90.0f;
    if (meta == 5) rotation = -90.0f;

    GlMatrixStack t;
    t.Translate((float)x, y + 1.0f, z + 1.0f);
    t.Scale(1.0f, -1.0f, -1.0f);
    t.Translate(0.5f, 0.5f, 0.5f);
    if (large) {
        // The large model spans +X in its own space; turn and shift it to cover both halves.
        if (pairX && (meta == 2 || meta == 4 || meta == 5)) t.Translate(meta == 2 ? 1.0f : 0.0f, 0.0f, 0.0f);
        if (pairZ && meta == 5) t.Translate(0.0f, 0.0f, -1.0f);
        if (pairZ && meta == 4) t.Translate(0.0f, 0.0f, 0.0f);
    }
    t.Rotate(rotation, 0, 1, 0);
    t.Translate(-0.5f, -0.5f, -0.5f);

    float lid = LidAngle(world, x, y, z, partial);
    if (pairX) lid = std::max(lid, LidAngle(world, x + 1, y, z, partial));
    if (pairZ) lid = std::max(lid, LidAngle(world, x, y, z + 1, partial));
    lid = 1.0f - lid;
    lid = 1.0f - lid * lid * lid;
    float lidDegrees = -(lid * 90.0f);

    auto part = [&](const Mesh& mesh, const Material& material, float px, float py, float pz, float angle) {
        GlMatrixStack p = t;
        p.Translate(px * PIXEL, py * PIXEL, pz * PIXEL);
        if (angle != 0.0f) p.Rotate(angle, 1, 0, 0);
        p.Scale(PIXEL);
        DrawMesh(mesh, material, p.m);
    };
    if (large) {
        part(largeBottom, largeChestMaterial, 1, 6, 1, 0);
        part(largeLid, largeChestMaterial, 1, 7, 15, lidDegrees);
        part(largeKnob, largeChestMaterial, 16, 7, 15, lidDegrees);
    } else {
        part(chestBottom, chestMaterial, 1, 6, 1, 0);
        part(chestLid, chestMaterial, 1, 7, 15, lidDegrees);
        part(chestKnob, chestMaterial, 8, 7, 15, lidDegrees);
    }
}

// TileEntitySignRenderer: the board (and post), then the four lines of text.
void TileEntityRenderer::DrawSign(const World& world, int x, int y, int z) const {
    uint8_t id = world.GetBlock(x, y, z);
    if (id != BLOCK_SIGN_POST && id != BLOCK_WALL_SIGN) return;
    const TileEntity* te = world.GetTileEntity(x, y, z);
    if (te == nullptr || te->kind != TileKind::Sign) return;
    const SignTile* sign = static_cast<const SignTile*>(te);
    int meta = world.GetMeta(x, y, z);
    const float f1 = 0.6666667f;

    float brightness = Brightness(world, x, y, z);
    SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);

    GlMatrixStack t;
    bool post = id == BLOCK_SIGN_POST;
    t.Translate(x + 0.5f, y + 0.75f * f1, z + 0.5f);
    if (post) {
        t.Rotate(-(meta * 360.0f) / 16.0f, 0, 1, 0);
    } else {
        float rotation = 0.0f;
        if (meta == 2) rotation = 180.0f;
        if (meta == 4) rotation = 90.0f;
        if (meta == 5) rotation = -90.0f;
        t.Rotate(-rotation, 0, 1, 0);
        t.Translate(0.0f, -0.3125f, -0.4375f);
    }

    GlMatrixStack model = t;
    model.Scale(f1, -f1, -f1);
    model.Scale(PIXEL);
    DrawMesh(signBoard, signMaterial, model.m);
    if (post) DrawMesh(signStick, signMaterial, model.m);

    // Black text on the front of the board.
    GlMatrixStack text = t;
    float f3 = 0.01666667f * f1;
    text.Translate(0.0f, 0.5f * f1, 0.07f * f1);
    text.Scale(f3, -f3, f3);
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    for (int line = 0; line < 4; line++) {
        const std::string& s = sign->lines[line];
        if (s.empty()) continue;
        font.Draw3D(s.c_str(), (float)(-font.Width(s.c_str()) / 2), (float)(line * 10 - 4 * 5), text.m, Color{ 0, 0, 0, 255 });
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

// TileEntityRendererPiston: the block partway between where it was and where it's going.
void TileEntityRenderer::DrawMovingBlock(const World& world, int x, int y, int z, float partial) const {
    const TileEntity* te = world.GetTileEntity(x, y, z);
    if (te == nullptr || te->kind != TileKind::Piston) return;
    const PistonTile* moving = static_cast<const PistonTile*>(te);
    if (moving->storedId <= 0 || moving->storedId >= BLOCK_COUNT) return;
    float offset = moving->Offset(partial);
    float ox = offset * SIDE_OFFSET[moving->facing][0], oy = offset * SIDE_OFFSET[moving->facing][1], oz = offset * SIDE_OFFSET[moving->facing][2];
    float brightness = Brightness(world, x, y, z);
    SetShaderValue(shader, brightnessLoc, &brightness, SHADER_UNIFORM_FLOAT);
    GlMatrixStack t;
    t.Translate(x + 0.5f + ox, y + 0.5f + oy, z + 0.5f + oz);
    const Mesh& mesh = models.BlockModel(moving->storedId, moving->storedMeta);
    if (mesh.vertexCount > 0) DrawMesh(mesh, blockMaterial, t.m);
}
