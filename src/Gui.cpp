#include "Gui.h"
#include "GuiFont.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

std::map<std::string, Texture2D>& Cache() {
    static std::map<std::string, Texture2D> cache;
    return cache;
}

Texture2D Load(const std::string& path) {
    auto& cache = Cache();
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    Texture2D texture = { 0 };
    if (FileExists(path.c_str())) texture = LoadPixelTexture(path.c_str());
    else TraceLog(LOG_WARNING, "GUI: missing %s", path.c_str());
    cache[path] = texture;
    return texture;
}

constexpr int MENU_TILE = 32; // GUI pixels per menu background tile

} // namespace

Texture2D GuiSprite(const std::string& name) {
    return Load("resources/textures/gui/modern/sprites/" + name + ".png");
}

Texture2D GuiModernTexture(const std::string& name) {
    return Load("resources/textures/gui/modern/" + name + ".png");
}

void UnloadGuiTextures() {
    for (auto& [path, texture] : Cache()) {
        if (texture.id != 0) UnloadTexture(texture);
    }
    Cache().clear();
}

void DrawGuiTexture(Texture2D texture, Rectangle source, float x, float y, float w, float h, int s, Color tint) {
    if (texture.id == 0 || w <= 0 || h <= 0) return;
    DrawTexturePro(texture, source, Rectangle{ x * s, y * s, w * s, h * s }, Vector2{ 0, 0 }, 0.0f, tint);
}

void DrawGuiSprite(const std::string& name, float x, float y, int s, Color tint) {
    Texture2D t = GuiSprite(name);
    DrawGuiTexture(t, Rectangle{ 0, 0, (float)t.width, (float)t.height }, x, y, (float)t.width, (float)t.height, s, tint);
}

// Fills a box with a texel rectangle repeated (cropped at the far edges).
static void DrawTiled(Texture2D t, Rectangle src, float x, float y, float w, float h, int s, Color tint) {
    if (src.width <= 0 || src.height <= 0) return;
    for (float ty = 0; ty < h; ty += src.height) {
        float ph = std::min(src.height, h - ty);
        for (float tx = 0; tx < w; tx += src.width) {
            float pw = std::min(src.width, w - tx);
            DrawGuiTexture(t, Rectangle{ src.x, src.y, pw, ph }, x + tx, y + ty, pw, ph, s, tint);
        }
    }
}

void DrawGuiNineSlice(const std::string& name, float x, float y, float w, float h, int l, int t, int r, int b, int s, Color tint) {
    Texture2D tex = GuiSprite(name);
    if (tex.id == 0) return;
    float tw = (float)tex.width, th = (float)tex.height;
    // Corners shrink if the box is smaller than them.
    float L = std::min((float)l, w / 2), R = std::min((float)r, w / 2), T = std::min((float)t, h / 2), B = std::min((float)b, h / 2);
    float midW = tw - l - r, midH = th - t - b;
    float innerW = w - L - R, innerH = h - T - B;
    DrawGuiTexture(tex, Rectangle{ 0, 0, L, T }, x, y, L, T, s, tint);
    DrawGuiTexture(tex, Rectangle{ tw - R, 0, R, T }, x + w - R, y, R, T, s, tint);
    DrawGuiTexture(tex, Rectangle{ 0, th - B, L, B }, x, y + h - B, L, B, s, tint);
    DrawGuiTexture(tex, Rectangle{ tw - R, th - B, R, B }, x + w - R, y + h - B, R, B, s, tint);
    DrawTiled(tex, Rectangle{ (float)l, 0, midW, T }, x + L, y, innerW, T, s, tint);
    DrawTiled(tex, Rectangle{ (float)l, th - B, midW, B }, x + L, y + h - B, innerW, B, s, tint);
    DrawTiled(tex, Rectangle{ 0, (float)t, L, midH }, x, y + T, L, innerH, s, tint);
    DrawTiled(tex, Rectangle{ tw - R, (float)t, R, midH }, x + w - R, y + T, R, innerH, s, tint);
    DrawTiled(tex, Rectangle{ (float)l, (float)t, midW, midH }, x + L, y + T, innerW, innerH, s, tint);
}

// TooltipRenderUtil.renderTooltipBackground: 9-pixel borders around the text plus 3 of padding.
void DrawGuiTooltipBox(float x, float y, float w, float h, int s) {
    float bx = x - 3 - 9, by = y - 3 - 9, bw = w + 3 + 3 + 18, bh = h + 3 + 3 + 18;
    DrawGuiNineSlice("tooltip/background", bx, by, bw, bh, 9, 9, 9, 9, s);
    DrawGuiNineSlice("tooltip/frame", bx, by, bw, bh, 10, 10, 10, 10, s);
}

void DrawGuiTooltip(const GuiFont& font, const std::string& text, int mouseX, int mouseY, int s) {
    int w = font.Width(text.c_str()), h = 8;
    int guiWidth = (GetScreenWidth() + s - 1) / s, guiHeight = (GetScreenHeight() + s - 1) / s;
    int x = mouseX + 12, y = mouseY - 12;
    if (x + w + 4 > guiWidth) x = std::max(4, mouseX - 16 - w);
    if (y + h + 4 > guiHeight) y = guiHeight - h - 4;
    y = std::max(4, y);
    DrawGuiTooltipBox((float)x, (float)y, (float)w, (float)h, s);
    font.Draw(text.c_str(), x * s, y * s, s, WHITE, true);
}

// ---- GuiBackdrop

GuiBackdrop::GuiBackdrop() {
    instance = this;
    for (int i = 0; i < 6; i++) {
        faces[i] = LoadTexture(TextFormat("resources/textures/gui/modern/title/background/panorama_%d.png", i));
        SetTextureFilter(faces[i], TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(faces[i], TEXTURE_WRAP_CLAMP);
    }
    blur = LoadShader(nullptr, "resources/shaders/blur.fs");
    blurDirectionLoc = GetShaderLocation(blur, "direction");
    blurTexelLoc = GetShaderLocation(blur, "texel");
    blurRadiusLoc = GetShaderLocation(blur, "radius");
}

GuiBackdrop::~GuiBackdrop() {
    for (Texture2D& t : faces) UnloadTexture(t);
    UnloadShader(blur);
    for (RenderTexture2D* t : { &scene, &pingA, &pingB }) {
        if (t->id != 0) UnloadRenderTexture(*t);
    }
    if (instance == this) instance = nullptr;
}

void GuiBackdrop::Update(float dt) {
    // PanoramaRenderer: a tenth of a degree a tick.
    spin = fmodf(spin + dt * 20.0f * 0.1f, 360.0f);
}

void GuiBackdrop::EnsureTargets() {
    int w = GetScreenWidth(), h = GetScreenHeight();
    for (RenderTexture2D* t : { &scene, &pingA, &pingB }) {
        if (t->id != 0 && t->texture.width == w && t->texture.height == h) continue;
        if (t->id != 0) UnloadRenderTexture(*t);
        *t = LoadRenderTexture(w, h);
        SetTextureFilter(t->texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(t->texture, TEXTURE_WRAP_CLAMP);
    }
}

RenderTexture2D& GuiBackdrop::SceneTarget() {
    EnsureTargets();
    return scene;
}

// One direction of a Gaussian blur from `source` into `target`.
void GuiBackdrop::BlurInto(Texture2D source, RenderTexture2D& target, Vector2 direction, float radius) {
    Vector2 texel = { 1.0f / source.width, 1.0f / source.height };
    SetShaderValue(blur, blurDirectionLoc, &direction, SHADER_UNIFORM_VEC2);
    SetShaderValue(blur, blurTexelLoc, &texel, SHADER_UNIFORM_VEC2);
    SetShaderValue(blur, blurRadiusLoc, &radius, SHADER_UNIFORM_FLOAT);
    BeginTextureMode(target);
        ClearBackground(BLACK);
        BeginShaderMode(blur);
            DrawTextureRec(source, Rectangle{ 0, 0, (float)source.width, -(float)source.height }, Vector2{ 0, 0 }, WHITE);
        EndShaderMode();
    EndTextureMode();
}

// The menu background's blur (5 by default), then its darkening tiles.
void GuiBackdrop::DrawBlurred(Texture2D source, bool inWorld) {
    EnsureTargets();
    int s = GuiScale(GetScreenWidth(), GetScreenHeight());
    float radius = 5.0f * s;
    BlurInto(source, pingA, Vector2{ 1, 0 }, radius);
    BlurInto(pingA.texture, pingB, Vector2{ 0, 1 }, radius);
    DrawTextureRec(pingB.texture, Rectangle{ 0, 0, (float)pingB.texture.width, -(float)pingB.texture.height }, Vector2{ 0, 0 }, WHITE);
    DrawMenuBackground(inWorld);
}

void GuiBackdrop::DrawMenuBackground(bool inWorld) {
    int s = GuiScale(GetScreenWidth(), GetScreenHeight());
    Texture2D tile = GuiModernTexture(inWorld ? "inworld_menu_background" : "menu_background");
    if (tile.id == 0) return;
    SetTextureWrap(tile, TEXTURE_WRAP_REPEAT);
    float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
    float tiles = (float)(MENU_TILE * s);
    DrawTexturePro(tile, Rectangle{ 0, 0, w / tiles * tile.width, h / tiles * tile.height }, Rectangle{ 0, 0, w, h }, Vector2{ 0, 0 }, 0.0f, WHITE);
}

void GuiBackdrop::DrawPanorama(bool blurred) {
    auto drawCube = [&]() {
        Camera3D camera = { 0 };
        camera.position = Vector3{ 0, 0, 0 };
        camera.target = Vector3{ 0, 0, -1 };
        camera.up = Vector3{ 0, 1, 0 };
        camera.fovy = 85.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        ClearBackground(BLACK);
        BeginMode3D(camera);
        rlDisableDepthTest();
        rlDisableBackfaceCulling();
        rlPushMatrix();
        // CubeMap.render: flipped, tipped down 10 degrees, turning.
        rlRotatef(180.0f, 1, 0, 0);
        rlRotatef(10.0f, 1, 0, 0);
        rlRotatef(-spin, 0, 1, 0);
        static const float v[6][4][3] = {
            { { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 }, { 1, -1, 1 } },
            { { 1, -1, 1 }, { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, -1 } },
            { { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, -1 } },
            { { -1, -1, -1 }, { -1, 1, -1 }, { -1, 1, 1 }, { -1, -1, 1 } },
            { { -1, -1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, -1, -1 } },
            { { -1, 1, 1 }, { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 } },
        };
        static const float uv[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
        for (int f = 0; f < 6; f++) {
            rlSetTexture(faces[f].id);
            rlBegin(RL_QUADS);
            rlColor4ub(255, 255, 255, 255);
            for (int k = 0; k < 4; k++) {
                rlTexCoord2f(uv[k][0], uv[k][1]);
                rlVertex3f(v[f][k][0], v[f][k][1], v[f][k][2]);
            }
            rlEnd();
        }
        rlSetTexture(0);
        rlPopMatrix();
        rlEnableBackfaceCulling();
        rlEnableDepthTest();
        EndMode3D();
    };
    if (!blurred) {
        drawCube();
        return;
    }
    EnsureTargets();
    rlDrawRenderBatchActive();
    BeginTextureMode(scene);
        drawCube();
    EndTextureMode();
    DrawBlurred(scene.texture, false);
}

void GuiBackdrop::DrawBlurredScene() {
    DrawBlurred(scene.texture, true);
}

void GuiBackdrop::DrawSeparator(int y, bool header, bool inWorld, int s) {
    std::string name = std::string(inWorld ? "inworld_" : "") + (header ? "header_separator" : "footer_separator");
    Texture2D t = GuiModernTexture(name);
    if (t.id == 0) return;
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    float w = (float)GetScreenWidth();
    DrawTexturePro(t, Rectangle{ 0, 0, w / s, (float)t.height }, Rectangle{ 0, (float)(y * s), w, (float)(t.height * s) }, Vector2{ 0, 0 }, 0.0f, WHITE);
}

void GuiBackdrop::DrawListBackground(int top, int bottom, bool inWorld, int s) {
    Texture2D t = GuiModernTexture(inWorld ? "inworld_menu_list_background" : "menu_list_background");
    if (t.id == 0) return;
    SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
    float w = (float)GetScreenWidth(), h = (float)((bottom - top) * s);
    float tiles = (float)(MENU_TILE * s);
    DrawTexturePro(t, Rectangle{ 0, 0, w / tiles * t.width, h / tiles * t.height }, Rectangle{ 0, (float)(top * s), w, h }, Vector2{ 0, 0 }, 0.0f, WHITE);
}
