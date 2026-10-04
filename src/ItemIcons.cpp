#include "ItemIcons.h"
#include "BlockRender.h"
#include "Gui.h"
#include "Items.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

constexpr int ITEM_SIZE = 16;
// Minecraft's icon view: tilted 30 degrees down, looking at a corner (45 degrees).
constexpr float ICON_PITCH = 30.0f * DEG2RAD, ICON_YAW = 45.0f * DEG2RAD;

int IconKey(const ItemStack& stack) {
    return stack.id | (stack.damage & 15) << 16;
}

// 1.21's lighting for blocks in GUIs (Lighting.setupFor3DItems, and
// minecraft_mix_light in the item shaders): two fixed lights, each adding 60% of
// its diffuse to a 40% ambient, capped at full. The lights are DIFFUSE_LIGHT_0
// (0.2, 1, -0.7) and DIFFUSE_LIGHT_1 (-0.2, 1, 0.7) turned into the GUI's view
// (RenderSystem.setupGui3DDiffuseLighting's rotations, in a view with Y up),
// here expressed in the icon camera's frame. The top, left (south) and right
// (east) faces come out as in 1.21, 100%, 65% and 40%; every other face goes by
// its own direction.
float GuiShade(Vector3 normal) {
    struct Lights { Vector3 a, b; };
    static const Lights lights = [] {
        auto rotX = [](Vector3 v, float a) { return Vector3{ v.x, v.y * cosf(a) - v.z * sinf(a), v.y * sinf(a) + v.z * cosf(a) }; };
        auto rotY = [](Vector3 v, float a) { return Vector3{ v.x * cosf(a) + v.z * sinf(a), v.y, -v.x * sinf(a) + v.z * cosf(a) }; };
        auto toView = [&](Vector3 l) {
            l = rotX(Vector3Normalize(l), 2.3561945f);
            l = rotY(l, -0.3926991f);
            l = rotX(l, 3.2375858f);
            return rotY(l, 1.0821041f);
        };
        // The view's axes in block space: right, up, and back toward the viewer.
        Vector3 back = { cosf(ICON_PITCH) * sinf(ICON_YAW), sinf(ICON_PITCH), cosf(ICON_PITCH) * cosf(ICON_YAW) };
        Vector3 right = { cosf(ICON_YAW), 0.0f, -sinf(ICON_YAW) };
        Vector3 up = Vector3CrossProduct(back, right);
        auto toBlock = [&](Vector3 v) {
            return Vector3Add(Vector3Add(Vector3Scale(right, v.x), Vector3Scale(up, v.y)), Vector3Scale(back, v.z));
        };
        return Lights{ toBlock(toView(Vector3{ 0.2f, 1.0f, -0.7f })), toBlock(toView(Vector3{ -0.2f, 1.0f, 0.7f })) };
    }();
    float diffuse = std::max(0.0f, Vector3DotProduct(lights.a, normal)) + std::max(0.0f, Vector3DotProduct(lights.b, normal));
    return std::min(1.0f, diffuse * 0.6f + 0.4f);
}

} // namespace

ItemIcons::ItemIcons(const ItemModels& itemModels) : models(itemModels) {
    shader = LoadShader("resources/shaders/chunk.vs", "resources/shaders/chunk.fs");
    float tileSize = 1.0f / ATLAS_TILES;
    SetShaderValue(shader, GetShaderLocation(shader, "tileSize"), &tileSize, SHADER_UNIFORM_FLOAT);
    // Icons share the terrain shader but aren't fogged or darkened at night.
    float zero = 0.0f, farAway = 1.0e6f, fartherAway = 2.0e6f;
    SetShaderValue(shader, GetShaderLocation(shader, "fogDensity"), &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "fogStart"), &farAway, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "fogEnd"), &fartherAway, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "skyDarken"), &zero, SHADER_UNIFORM_FLOAT);
    // Drawn as the translucent pass, blended: stained glass keeps its see-through tint
    // (the opaque pass would cut its half-clear pixels away); clear pixels still drop.
    float one = 1.0f;
    SetShaderValue(shader, GetShaderLocation(shader, "translucentPass"), &one, SHADER_UNIFORM_FLOAT);
}

ItemIcons::~ItemIcons() {
    for (auto& [key, texture] : icons) UnloadRenderTexture(texture);
    UnloadShader(shader);
}

void ItemIcons::Prepare() {
    int scale = GuiScale(GetScreenWidth(), GetScreenHeight());
    if (scale != iconScale) {
        iconScale = scale;
        for (auto& [key, texture] : icons) {
            pending.insert(key);
            UnloadRenderTexture(texture);
        }
        icons.clear();
    }
    if (pending.empty()) return;

    Camera3D camera = { 0 };
    camera.target = Vector3{ 0.5f, 0.5f, 0.5f };
    camera.position = Vector3{ 0.5f + cosf(ICON_PITCH) * sinf(ICON_YAW) * 4.0f, 0.5f + sinf(ICON_PITCH) * 4.0f,
                               0.5f + cosf(ICON_PITCH) * cosf(ICON_YAW) * 4.0f };
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = 1.62f; // view height in blocks; the cube's outline is ~1.57 tall and ~1.41 wide
    camera.projection = CAMERA_ORTHOGRAPHIC;

    int size = ITEM_SIZE * iconScale;
    Material material = MakeMaterial(shader, models.Atlas());
    for (int key : pending) {
        int id = key & 0xFFFF, meta = key >> 16;
        Mesh mesh = BuildBlockTerrainMesh(id, ItemRenderMeta(ItemStack(id, 1, meta)), GuiShade);
        RenderTexture2D target = LoadRenderTexture(size, size);
        SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
        BeginTextureMode(target);
            ClearBackground(BLANK);
            if (mesh.vertexCount > 0) {
                BeginMode3D(camera);
                    BeginBlendMode(BLEND_ALPHA);
                    DrawMesh(mesh, material, MatrixIdentity());
                    EndBlendMode();
                EndMode3D();
            }
        EndTextureMode();
        if (mesh.vertexCount > 0) UnloadMesh(mesh);
        icons[key] = target;
    }
    MemFree(material.maps);
    pending.clear();
}

void ItemIcons::DrawIcon(const ItemStack& stack, Rectangle dest) const {
    if (stack.IsEmpty() || GetItemDef(stack.id) == nullptr) return;
    if (ItemModels::Is3D(stack)) {
        auto it = icons.find(IconKey(stack));
        if (it == icons.end()) {
            pending.insert(IconKey(stack)); // drawn from the next frame
            return;
        }
        Texture2D icon = it->second.texture;
        Rectangle flipped = { 0.0f, 0.0f, (float)icon.width, -(float)icon.height }; // render textures are upside down
        DrawTexturePro(icon, flipped, dest, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);
        return;
    }
    Texture2D sheet = models.Sheet(stack);
    Rectangle uv = models.IconUV(stack);
    Rectangle source = { uv.x * sheet.width, uv.y * sheet.height, uv.width * sheet.width, uv.height * sheet.height };
    DrawTexturePro(sheet, source, dest, Vector2{ 0.0f, 0.0f }, 0.0f, models.Tint(stack));
}

void ItemIcons::DrawStack(const ItemStack& stack, int x, int y, int scale, const GuiFont& font) const {
    if (stack.IsEmpty()) return;
    DrawIcon(stack, Rectangle{ (float)x, (float)y, (float)(ITEM_SIZE * scale), (float)(ITEM_SIZE * scale) });
    DrawCount(stack, x, y, scale, font);
}

void ItemIcons::DrawCount(const ItemStack& stack, int x, int y, int scale, const GuiFont& font) const {
    if (stack.IsEmpty()) return;
    if (stack.count > 1) {
        // Right-aligned in the slot's bottom-right corner, white with a shadow.
        std::string text = std::to_string(stack.count);
        int textX = x + (19 - 2 - font.Width(text.c_str())) * scale;
        int textY = y + (6 + 3) * scale;
        font.Draw(text.c_str(), textX, textY, scale, WHITE, true);
    }
    // Wear: a green-to-red bar under worn tools (RenderItem.renderItemOverlayIntoGUI).
    const ItemDef* def = GetItemDef(stack.id);
    if (def != nullptr && def->maxDamage > 0 && stack.damage > 0) {
        int length = (int)lround(13.0 - stack.damage * 13.0 / def->maxDamage);
        int green = (int)lround(255.0 - stack.damage * 255.0 / def->maxDamage);
        DrawRectangle(x + 2 * scale, y + 13 * scale, 13 * scale, 2 * scale, BLACK);
        DrawRectangle(x + 2 * scale, y + 13 * scale, 12 * scale, 1 * scale, Color{ (unsigned char)((255 - green) / 4), 64, 0, 255 });
        DrawRectangle(x + 2 * scale, y + 13 * scale, length * scale, 1 * scale, Color{ (unsigned char)(255 - green), (unsigned char)green, 0, 255 });
    }
}
