#pragma once

#include "raylib.h"
#include "raymath.h"
#include "Block.h"
#include "ItemStack.h"

#include <map>
#include <vector>

// Mesh-building helpers shared by everything that draws Minecraft-style models:
// the first-person hand, the player in the inventory, dropped items, falling
// blocks, chests and signs.

struct MeshData {
    std::vector<float> positions, texcoords, normals;
    std::vector<unsigned char> colors;
    std::vector<unsigned short> indices;

    void AddQuad(const Vector3 corners[4], const Vector2 uvs[4], Vector3 normal, Color color);
    Mesh Upload() const;
};

// Minecraft's ModelBox: a box of dx*dy*dz model pixels at (x1, y1, z1), grown by
// `inflate` on every side, with the standard skin layout starting at texel (u, v).
// Coordinates are in model pixels with Y pointing down, as in Minecraft.
void AddModelBox(MeshData& mesh, float x1, float y1, float z1, int dx, int dy, int dz, float inflate,
                 int u, int v, float texWidth, float texHeight);

// A block as a model centered on the origin, textured from terrain.png and
// tinted like its item, with face normals for item lighting.
Mesh BuildBlockMesh(int id, int meta);

// ItemRenderer.renderItemIn2D: an item's 16x16 icon as a thin slab, a pixel
// deep, from (0,0,0) to (1,1,-1/16). `uv` is the icon's rectangle in its sheet.
Mesh BuildFlatItemMesh(Rectangle uv, Color tint);

// Builds a transform the way OpenGL's matrix stack does: each call applies to the
// geometry before everything added earlier, so they read in Minecraft's code order.
struct GlMatrixStack {
    Matrix m = MatrixIdentity();
    void Translate(float x, float y, float z) { m = MatrixMultiply(MatrixTranslate(x, y, z), m); }
    void Rotate(float degrees, float x, float y, float z) { m = MatrixMultiply(MatrixRotate(Vector3{ x, y, z }, degrees * DEG2RAD), m); }
    void Scale(float s) { m = MatrixMultiply(MatrixScale(s, s, s), m); }
    void Scale(float x, float y, float z) { m = MatrixMultiply(MatrixScale(x, y, z), m); }
};

// A material using `shader` and `texture`. Free with MemFree(material.maps) only:
// the shader and texture belong to the caller.
Material MakeMaterial(Shader shader, Texture2D texture);

// Minecraft's item lighting (RenderHelper.enableStandardItemLighting): two
// directional lights, in whatever space the caller transforms them into.
constexpr Vector3 ITEM_LIGHT0 = { 0.2f, 1.0f, -0.7f };
constexpr Vector3 ITEM_LIGHT1 = { -0.2f, 1.0f, 0.7f };

// The block metadata an item is drawn with: its damage, except pistons, which
// face up as items.
inline int ItemRenderMeta(const ItemStack& stack) {
    return (stack.id == BLOCK_PISTON || stack.id == BLOCK_STICKY_PISTON) ? 1 : stack.damage;
}

// Every item's look, built on first use: blocks that render in 3D as little
// block models, everything else as its icon from items.png or terrain.png.
// Construct after InitWindow() and destroy before CloseWindow().
class ItemModels {
public:
    explicit ItemModels(Texture2D atlas);
    ~ItemModels();

    ItemModels(const ItemModels&) = delete;
    ItemModels& operator=(const ItemModels&) = delete;

    // Drawn as a little block (RenderBlocks.renderItemIn3d), rather than a flat icon.
    static bool Is3D(const ItemStack& stack);
    const Mesh& BlockModel(int id, int meta) const;     // centered on the origin, terrain.png
    const Mesh& FlatModel(const ItemStack& stack) const; // a pixel-thick slab, in its sheet
    Texture2D Sheet(const ItemStack& stack) const;      // terrain.png or items.png
    Rectangle IconUV(const ItemStack& stack) const;     // the icon's rectangle in its sheet, 0..1
    Color Tint(const ItemStack& stack) const;
    Texture2D Atlas() const { return atlas; }
    Texture2D ItemSheet() const { return items; }

private:
    Texture2D atlas;
    Texture2D items;
    mutable std::map<int, Mesh> blockMeshes; // id | meta << 16
    mutable std::map<int, Mesh> flatMeshes;  // id | damage << 16
};
