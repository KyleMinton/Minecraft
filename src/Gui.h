#pragma once

#include "raylib.h"

#include <cstdint>

// The options menu's GUI Scale: 0 Auto, else the most it may be (1 Small, 2 Normal, 3 Large).
inline int g_guiScaleSetting = 0;

// Minecraft picks the largest scale that still leaves a 320x240 GUI, up to the setting.
inline int GuiScale(int width, int height) {
    int limit = g_guiScaleSetting == 0 ? 1000 : g_guiScaleSetting;
    int scale = 1;
    while (scale < limit && width / (scale + 1) >= 320 && height / (scale + 1) >= 240) scale++;
    return scale;
}

// A color written the way Minecraft's GUI code writes them: 0xAARRGGBB.
inline Color Argb(uint32_t argb) {
    return Color{ (unsigned char)(argb >> 16), (unsigned char)(argb >> 8), (unsigned char)argb, (unsigned char)(argb >> 24) };
}

// Gui.drawGradientRect: a rectangle from (left, top) to (right, bottom) in GUI
// pixels, fading from `topColor` to `bottomColor`, drawn at `scale` from a screen origin.
inline void DrawGuiGradient(int originX, int originY, int scale, int left, int top, int right, int bottom,
                            Color topColor, Color bottomColor) {
    DrawRectangleGradientV(originX + left * scale, originY + top * scale, (right - left) * scale, (bottom - top) * scale,
                           topColor, bottomColor);
}

inline Texture2D LoadPixelTexture(const char* path) {
    Texture2D texture = LoadTexture(path);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}

// ---- Modern (1.21) GUI pieces, from resources/textures/gui/modern.

#include <string>

// A sprite from gui/modern/sprites ("widget/button", "hud/heart/full"...), or a
// texture from gui/modern ("container/furnace", "menu_background"), loaded on first use.
Texture2D GuiSprite(const std::string& name);
Texture2D GuiModernTexture(const std::string& name);
void UnloadGuiTextures();

// Draws part of a texture (texel rectangle `source`) at a GUI-pixel rectangle.
void DrawGuiTexture(Texture2D texture, Rectangle source, float x, float y, float w, float h, int scale, Color tint = WHITE);
// A whole sprite at its own size, or a part of it.
void DrawGuiSprite(const std::string& name, float x, float y, int scale, Color tint = WHITE);
// GuiGraphics.blitSprite for nine-slice sprites: corners as they are, edges and
// middle tiled to fill a w x h GUI-pixel box.
void DrawGuiNineSlice(const std::string& name, float x, float y, float w, float h, int left, int top, int right, int bottom,
                      int scale, Color tint = WHITE);
// The modern tooltip (tooltip/background + tooltip/frame) around a w x h box of text at (x, y).
void DrawGuiTooltipBox(float x, float y, float w, float h, int scale);
// The modern tooltip for one line of text near the mouse (all in GUI pixels).
class GuiFont;
void DrawGuiTooltip(const GuiFont& font, const std::string& text, int mouseX, int mouseY, int scale);

// What modern menus draw behind themselves: the title screen's spinning
// panorama (blurred on every screen but the title itself), or the game
// blurred behind in-game menus, with the menu background's darkening.
// One per game, made after InitWindow().
class GuiBackdrop {
public:
    GuiBackdrop();
    ~GuiBackdrop();
    GuiBackdrop(const GuiBackdrop&) = delete;
    GuiBackdrop& operator=(const GuiBackdrop&) = delete;

    static GuiBackdrop* Get() { return instance; }

    // Advances the panorama's spin.
    void Update(float dt);
    // The panorama, sharp (title screen) or blurred and darkened (other menus).
    void DrawPanorama(bool blurred);
    // For in-game menus: render the game into SceneTarget(), then draw it blurred and darkened.
    RenderTexture2D& SceneTarget();
    void DrawBlurredScene();
    // A modern header or footer line across the screen at GUI y (separator textures).
    void DrawSeparator(int y, bool header, bool inWorld, int scale);
    // The menu list's darker background between two GUI y's.
    void DrawListBackground(int top, int bottom, bool inWorld, int scale);

private:
    static inline GuiBackdrop* instance = nullptr;
    Texture2D faces[6] = {};
    Texture2D overlay = {}; // panorama_overlay.png: the shading laid over the spinning cube
    Shader blur = {};
    int blurDirectionLoc = -1, blurTexelLoc = -1, blurRadiusLoc = -1;
    RenderTexture2D scene = {}, pingA = {}, pingB = {};
    float spin = 0.0f;

    void EnsureTargets();
    void BlurInto(Texture2D source, RenderTexture2D& target, Vector2 direction, float radius);
    void DrawBlurred(Texture2D source, bool inWorld);
    void DrawMenuBackground(bool inWorld);
};
