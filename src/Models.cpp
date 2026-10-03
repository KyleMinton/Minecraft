#include "Models.h"
#include "BlockRender.h"
#include "Gui.h"
#include "Items.h"

#include <cstring>

void MeshData::AddQuad(const Vector3 corners[4], const Vector2 uvs[4], Vector3 normal, Color color) {
    unsigned short base = (unsigned short)(positions.size() / 3);
    for (int i = 0; i < 4; i++) {
        positions.insert(positions.end(), { corners[i].x, corners[i].y, corners[i].z });
        texcoords.insert(texcoords.end(), { uvs[i].x, uvs[i].y });
        normals.insert(normals.end(), { normal.x, normal.y, normal.z });
        colors.insert(colors.end(), { color.r, color.g, color.b, color.a });
    }
    for (unsigned short i : { 0, 1, 2, 0, 2, 3 }) indices.push_back(base + i);
}

Mesh MeshData::Upload() const {
    Mesh mesh = { 0 };
    mesh.vertexCount = (int)(positions.size() / 3);
    mesh.triangleCount = (int)(indices.size() / 3);
    if (mesh.vertexCount == 0) return mesh;
    auto copy = [](const auto& source) {
        size_t bytes = source.size() * sizeof(source[0]);
        void* dest = MemAlloc((unsigned int)bytes);
        memcpy(dest, source.data(), bytes);
        return dest;
    };
    mesh.vertices = (float*)copy(positions);
    mesh.texcoords = (float*)copy(texcoords);
    mesh.normals = (float*)copy(normals);
    mesh.colors = (unsigned char*)copy(colors);
    mesh.indices = (unsigned short*)copy(indices);
    UploadMesh(&mesh, false);
    return mesh;
}

// The box's 8 corners and the standard skin layout for its 6 faces (TexturedQuad
// assigns each face's corners the UVs u2v1, u1v1, u1v2, u2v2).
void AddModelBox(MeshData& mesh, float x1, float y1, float z1, int dx, int dy, int dz, float inflate,
                 int u, int v, float texWidth, float texHeight) {
    float x2 = x1 + dx + inflate, y2 = y1 + dy + inflate, z2 = z1 + dz + inflate;
    x1 -= inflate; y1 -= inflate; z1 -= inflate;

    Vector3 c[8] = {
        { x1, y1, z1 }, { x2, y1, z1 }, { x2, y2, z1 }, { x1, y2, z1 },
        { x1, y1, z2 }, { x2, y1, z2 }, { x2, y2, z2 }, { x1, y2, z2 },
    };
    struct Face { int corners[4]; float u1, v1, u2, v2; Vector3 normal; };
    const Face faces[6] = {
        { { 5, 1, 2, 6 }, (float)(u + dz + dx), (float)(v + dz), (float)(u + dz + dx + dz), (float)(v + dz + dy), { 1, 0, 0 } },
        { { 0, 4, 7, 3 }, (float)u, (float)(v + dz), (float)(u + dz), (float)(v + dz + dy), { -1, 0, 0 } },
        { { 5, 4, 0, 1 }, (float)(u + dz), (float)v, (float)(u + dz + dx), (float)(v + dz), { 0, -1, 0 } },
        { { 2, 3, 7, 6 }, (float)(u + dz + dx), (float)(v + dz), (float)(u + dz + dx + dx), (float)v, { 0, 1, 0 } },
        { { 1, 0, 3, 2 }, (float)(u + dz), (float)(v + dz), (float)(u + dz + dx), (float)(v + dz + dy), { 0, 0, -1 } },
        { { 4, 5, 6, 7 }, (float)(u + dz + dx + dz), (float)(v + dz), (float)(u + dz + dx + dz + dx), (float)(v + dz + dy), { 0, 0, 1 } },
    };
    for (const Face& f : faces) {
        Vector3 corners[4] = { c[f.corners[0]], c[f.corners[1]], c[f.corners[2]], c[f.corners[3]] };
        Vector2 uvs[4] = {
            { f.u2 / texWidth, f.v1 / texHeight }, { f.u1 / texWidth, f.v1 / texHeight },
            { f.u1 / texWidth, f.v2 / texHeight }, { f.u2 / texWidth, f.v2 / texHeight },
        };
        mesh.AddQuad(corners, uvs, f.normal, WHITE);
    }
}

namespace {

// Collects a block's quads for item lighting: face normals, tint without Beta's face shading.
class ModelSink final : public QuadSink {
public:
    MeshData data;
    void Quad(const BlockVertex v[4], Vector3 normal, Layer, float) override {
        Vector3 corners[4];
        Vector2 uvs[4];
        for (int i = 0; i < 4; i++) {
            corners[i] = Vector3{ v[i].x, v[i].y, v[i].z };
            uvs[i] = Vector2{ v[i].u, v[i].v };
        }
        data.AddQuad(corners, uvs, normal, v[0].color);
    }
};

} // namespace

Mesh BuildBlockMesh(int id, int meta) {
    ModelSink sink;
    RenderBlockModel(sink, id, meta, Vector3{ -0.5f, -0.5f, -0.5f });
    return sink.data.Upload();
}

// Front and back faces of the whole icon, then a thin strip for every row and
// column of pixels on each edge direction, so the transparent parts cut the
// outline out of the slab.
Mesh BuildFlatItemMesh(Rectangle uv, Color tint) {
    MeshData mesh;
    const float depth = 1.0f / 16.0f;
    float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.width, v1 = uv.y + uv.height;
    {
        Vector3 front[4] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
        Vector2 frontUV[4] = { { u1, v1 }, { u0, v1 }, { u0, v0 }, { u1, v0 } };
        mesh.AddQuad(front, frontUV, Vector3{ 0, 0, 1 }, tint);
        Vector3 back[4] = { { 0, 1, -depth }, { 1, 1, -depth }, { 1, 0, -depth }, { 0, 0, -depth } };
        Vector2 backUV[4] = { { u1, v0 }, { u0, v0 }, { u0, v1 }, { u1, v1 } };
        mesh.AddQuad(back, backUV, Vector3{ 0, 0, -1 }, tint);
    }
    const int pixels = 16;
    float du = uv.width / pixels, dv = uv.height / pixels;
    for (int i = 0; i < pixels; i++) {
        float f = (float)i / pixels;
        float u = u1 - (u1 - u0) * f - du * 0.5f; // columns run right-to-left in U, as Beta's
        Vector3 a[4] = { { f, 0, -depth }, { f, 0, 0 }, { f, 1, 0 }, { f, 1, -depth } };
        Vector2 auv[4] = { { u, v1 }, { u, v1 }, { u, v0 }, { u, v0 } };
        mesh.AddQuad(a, auv, Vector3{ -1, 0, 0 }, tint);
        float g = f + 1.0f / pixels;
        Vector3 b[4] = { { g, 1, -depth }, { g, 1, 0 }, { g, 0, 0 }, { g, 0, -depth } };
        Vector2 buv[4] = { { u, v0 }, { u, v0 }, { u, v1 }, { u, v1 } };
        mesh.AddQuad(b, buv, Vector3{ 1, 0, 0 }, tint);

        float v = v1 - (v1 - v0) * f - dv * 0.5f;
        Vector3 c[4] = { { 0, g, 0 }, { 1, g, 0 }, { 1, g, -depth }, { 0, g, -depth } };
        Vector2 cuv[4] = { { u1, v }, { u0, v }, { u0, v }, { u1, v } };
        mesh.AddQuad(c, cuv, Vector3{ 0, 1, 0 }, tint);
        Vector3 d[4] = { { 1, f, 0 }, { 0, f, 0 }, { 0, f, -depth }, { 1, f, -depth } };
        Vector2 duv[4] = { { u0, v }, { u1, v }, { u1, v }, { u0, v } };
        mesh.AddQuad(d, duv, Vector3{ 0, -1, 0 }, tint);
    }
    return mesh.Upload();
}

Material MakeMaterial(Shader shader, Texture2D texture) {
    Material material = LoadMaterialDefault();
    material.shader = shader;
    material.maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    return material;
}

// ---- ItemModels

ItemModels::ItemModels(Texture2D terrain) : atlas(terrain) {
    items = LoadPixelTexture("resources/textures/gui/items.png");
}

ItemModels::~ItemModels() {
    for (auto& [key, mesh] : blockMeshes) if (mesh.vertexCount > 0) UnloadMesh(mesh);
    for (auto& [key, mesh] : flatMeshes) if (mesh.vertexCount > 0) UnloadMesh(mesh);
    UnloadTexture(items);
}

bool ItemModels::Is3D(const ItemStack& stack) {
    return IsBlockItem(stack.id) && GetBlockDef((uint8_t)stack.id).RendersAs3DItem();
}

const Mesh& ItemModels::BlockModel(int id, int meta) const {
    int key = id | (meta & 15) << 8;
    auto it = blockMeshes.find(key);
    if (it != blockMeshes.end()) return it->second;
    return blockMeshes.emplace(key, BuildBlockMesh(id, meta)).first->second;
}

const Mesh& ItemModels::FlatModel(const ItemStack& stack) const {
    int key = stack.id | (int)stack.damage << 12;
    const ItemDef* def = GetItemDef(stack.id);
    bool damageable = def != nullptr && def->maxDamage > 0;
    if (damageable) key = stack.id; // wear doesn't change the look
    auto it = flatMeshes.find(key);
    if (it != flatMeshes.end()) return it->second;
    return flatMeshes.emplace(key, BuildFlatItemMesh(IconUV(stack), Tint(stack))).first->second;
}

Texture2D ItemModels::Sheet(const ItemStack& stack) const {
    const ItemDef* def = GetItemDef(stack.id);
    return def != nullptr && def->Sheet() == IconSheet::Items ? items : atlas;
}

Rectangle ItemModels::IconUV(const ItemStack& stack) const {
    const ItemDef* def = GetItemDef(stack.id);
    int icon = def != nullptr ? def->Icon(stack.damage) : 0;
    return Rectangle{ (icon & 15) / 16.0f, (icon >> 4) / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f };
}

Color ItemModels::Tint(const ItemStack& stack) const {
    const ItemDef* def = GetItemDef(stack.id);
    return def != nullptr ? def->Tint(stack.damage) : WHITE;
}
