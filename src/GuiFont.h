#pragma once

#include "raylib.h"

// Minecraft's pixel font (FontRenderer) from resources/textures/font/ascii.png:
// a 16x16 grid of 8x8 glyphs indexed by character code, each as wide as its
// rightmost filled column plus one pixel of spacing. Positions are screen pixels
// and sizes are in GUI pixels multiplied by the GUI scale, like the rest of the HUD.
// Falls back to raylib's font if the texture is missing.
// Construct after InitWindow() and destroy before CloseWindow().
class GuiFont {
public:
    GuiFont();
    ~GuiFont();

    GuiFont(const GuiFont&) = delete;
    GuiFont& operator=(const GuiFont&) = delete;

    // Width in GUI pixels (FontRenderer.getStringWidth).
    int Width(const char* text) const;

    // Draws with the top-left at screen pixel (x, y). With `shadow`, first draws a
    // copy one GUI pixel down-right at a quarter brightness, like drawStringWithShadow.
    void Draw(const char* text, int x, int y, int scale, Color color, bool shadow = false) const;

    // Draws in 3D (signs): glyphs in the plane of `transform`, 1 unit per font
    // pixel, top-left at the local origin, y down. Call inside BeginMode3D().
    void Draw3D(const char* text, float x, float y, const Matrix& transform, Color color) const;

    // Minecraft's shadow color for a text color.
    static Color ShadowOf(Color color);

private:
    Texture2D texture = { 0 };
    bool loaded = false;
    int charWidth[256] = {};

    void DrawRaw(const char* text, int x, int y, int scale, Color color) const;
};
