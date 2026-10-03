#include "GuiFont.h"

#include "raymath.h"
#include "rlgl.h"

namespace {

constexpr int GLYPH_SIZE = 8;
constexpr int SPACE_WIDTH = 4;

} // namespace

GuiFont::GuiFont() {
    const char* path = "resources/textures/font/ascii.png";
    if (!FileExists(path)) return;

    Image image = LoadImage(path);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const Color* pixels = (const Color*)image.data;
    int cell = image.width / 16; // 8 for the standard texture

    // FontRenderer.readFontTexture: each glyph ends at its rightmost column with
    // any visible pixel; the advance is that width (scaled to 8px cells) plus 1.
    for (int c = 0; c < 256; c++) {
        int cellX = (c % 16) * cell, cellY = (c / 16) * cell;
        int width = cell - 1;
        for (; width >= 0; width--) {
            bool empty = true;
            for (int y = 0; y < cell && empty; y++) {
                if (pixels[(cellY + y) * image.width + cellX + width].a != 0) empty = false;
            }
            if (!empty) break;
        }
        width++;
        charWidth[c] = (int)(0.5 + width * (8.0 / cell)) + 1;
    }
    charWidth[' '] = SPACE_WIDTH;

    texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    UnloadImage(image);
    loaded = true;
}

GuiFont::~GuiFont() {
    if (loaded) UnloadTexture(texture);
}

int GuiFont::Width(const char* text) const {
    if (!loaded) return MeasureText(text, GLYPH_SIZE);
    int width = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) width += charWidth[*p];
    return width;
}

Color GuiFont::ShadowOf(Color color) {
    // (rgb & 0xFCFCFC) >> 2, alpha kept.
    return Color{ (unsigned char)((color.r & 0xFC) >> 2), (unsigned char)((color.g & 0xFC) >> 2),
                  (unsigned char)((color.b & 0xFC) >> 2), color.a };
}

void GuiFont::Draw(const char* text, int x, int y, int scale, Color color, bool shadow) const {
    if (shadow) DrawRaw(text, x + scale, y + scale, scale, ShadowOf(color));
    DrawRaw(text, x, y, scale, color);
}

void GuiFont::DrawRaw(const char* text, int x, int y, int scale, Color color) const {
    if (!loaded) {
        DrawText(text, x, y, GLYPH_SIZE * scale, color);
        return;
    }
    float cell = texture.width / 16.0f;
    float texelsPerPixel = cell / GLYPH_SIZE;
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        int c = *p;
        int glyphWidth = charWidth[c] - 1; // the last column of the advance is spacing
        if (c != ' ' && glyphWidth > 0) {
            Rectangle source = { (c % 16) * cell, (c / 16) * cell, glyphWidth * texelsPerPixel, cell };
            Rectangle dest = { (float)x, (float)y, (float)(glyphWidth * scale), (float)(GLYPH_SIZE * scale) };
            DrawTexturePro(texture, source, dest, Vector2{ 0.0f, 0.0f }, 0.0f, color);
        }
        x += charWidth[c] * scale;
    }
}

void GuiFont::Draw3D(const char* text, float x, float y, const Matrix& transform, Color color) const {
    if (!loaded) return;
    float cell = texture.width / 16.0f;
    float texelsPerPixel = cell / GLYPH_SIZE;
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
        int c = *p;
        int glyphWidth = charWidth[c] - 1;
        if (c != ' ' && glyphWidth > 0) {
            float u0 = (c % 16) * cell / texture.width, v0 = (c / 16) * cell / texture.height;
            float u1 = u0 + glyphWidth * texelsPerPixel / texture.width, v1 = v0 + cell / texture.height;
            Vector3 corners[4] = { { x, y, 0 }, { x, y + GLYPH_SIZE, 0 }, { x + glyphWidth, y + GLYPH_SIZE, 0 }, { x + glyphWidth, y, 0 } };
            Vector2 uvs[4] = { { u0, v0 }, { u0, v1 }, { u1, v1 }, { u1, v0 } };
            for (int i = 0; i < 4; i++) {
                Vector3 w = Vector3Transform(corners[i], transform);
                rlTexCoord2f(uvs[i].x, uvs[i].y);
                rlVertex3f(w.x, w.y, w.z);
            }
        }
        x += charWidth[c];
    }
    rlEnd();
    rlSetTexture(0);
}
