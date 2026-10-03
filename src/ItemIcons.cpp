#include "ItemIcons.h"
#include "BlockRender.h"
#include "Gui.h"
#include "Items.h"

#include <cmath>
#include <string>

namespace {

constexpr int ITEM_SIZE = 16;

int IconKey(const ItemStack& stack) {
    return stack.id | (stack.damage & 15) << 8;
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
    SetShaderValue(shader, GetShaderLocation(shader, "translucentPass"), &zero, SHADER_UNIFORM_FLOAT);
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

    // Minecraft's icon view: tilted 30 degrees down, looking at a corner (45 degrees).
    Camera3D camera = { 0 };
    camera.target = Vector3{ 0.5f, 0.5f, 0.5f };
    float pitch = 30.0f * DEG2RAD, yaw = 45.0f * DEG2RAD;
    camera.position = Vector3{ 0.5f + cosf(pitch) * sinf(yaw) * 4.0f, 0.5f + sinf(pitch) * 4.0f, 0.5f + cosf(pitch) * cosf(yaw) * 4.0f };
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = 1.62f; // view height in blocks; the cube's outline is ~1.57 tall and ~1.41 wide
    camera.projection = CAMERA_ORTHOGRAPHIC;

    int size = ITEM_SIZE * iconScale;
    Material material = MakeMaterial(shader, models.Atlas());
    for (int key : pending) {
        int id = key & 0xFF, meta = key >> 8;
        Mesh mesh = BuildBlockTerrainMesh(id, ItemRenderMeta(ItemStack(id, 1, meta)));
        RenderTexture2D target = LoadRenderTexture(size, size);
        SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
        BeginTextureMode(target);
            ClearBackground(BLANK);
            if (mesh.vertexCount > 0) {
                BeginMode3D(camera);
                    DrawMesh(mesh, material, MatrixIdentity());
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
