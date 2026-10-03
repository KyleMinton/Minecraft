#include "MenuScreen.h"
#include "Gui.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>

#include <utility>

namespace {

constexpr int TEXTURE_WIDTH = 200;
constexpr int BUTTON_V = 46;       // disabled row in widgets.png; normal is +20, hovered +40
constexpr int BUTTON_ROW = 20;
constexpr int TITLE_Y = 40;
constexpr int DIRT_TILE = 32;      // GUI pixels per dirt tile
const Color DIRT_TINT = { 64, 64, 64, 255 };

} // namespace

void DrawGuiButton(const GuiFont& font, Texture2D widgets, Rectangle r, const std::string& label,
                   bool enabled, bool hovered, int s) {
    (void)widgets;
    const char* sprite = !enabled ? "widget/button_disabled" : (hovered ? "widget/button_highlighted" : "widget/button");
    DrawGuiNineSlice(sprite, r.x, r.y, r.width, r.height, 3, 3, 3, 3, s);
    Color text = enabled ? WHITE : Argb(0xFFA0A0A0);
    int width = (int)r.width, height = (int)r.height;
    int textX = (int)r.x + width / 2 - font.Width(label.c_str()) / 2;
    int textY = (int)r.y + (height - 8) / 2;
    font.Draw(label.c_str(), textX * s, textY * s, s, text, true);
}

MenuScreen::MenuScreen(const GuiFont& guiFont, std::string menuTitle, std::vector<MenuButton> menuButtons, Background bg)
    : font(guiFont), title(std::move(menuTitle)), buttons(std::move(menuButtons)), background(bg) {
    widgets = LoadPixelTexture("resources/textures/gui/widgets.png");
    dirt = LoadPixelTexture("resources/textures/gui/options_background.png");
    SetTextureWrap(dirt, TEXTURE_WRAP_REPEAT);
    // A random splash for the title screen (texts/splashes.txt).
    char* text = LoadFileText("resources/textures/gui/modern/splashes.txt");
    if (text != nullptr) {
        std::vector<std::string> lines;
        std::string all = text, line;
        UnloadFileText(text);
        for (char c : all) {
            if (c == '\n') {
                if (!line.empty()) lines.push_back(line);
                line.clear();
            } else if (c != '\r') {
                line += c;
            }
        }
        if (!line.empty()) lines.push_back(line);
        // The font only has ASCII; skip splashes that need more.
        lines.erase(std::remove_if(lines.begin(), lines.end(), [](const std::string& l) {
            return std::any_of(l.begin(), l.end(), [](char c) { return (unsigned char)c >= 128; });
        }), lines.end());
        if (!lines.empty()) splash = lines[GetRandomValue(0, (int)lines.size() - 1)];
    }
}

MenuScreen::~MenuScreen() {
    UnloadTexture(widgets);
    UnloadTexture(dirt);
}

int MenuScreen::Scale() const {
    return GuiScale(GetScreenWidth(), GetScreenHeight());
}

Rectangle MenuScreen::Bounds(const MenuButton& button) const {
    int s = Scale();
    int guiWidth = (GetScreenWidth() + s - 1) / s, guiHeight = (GetScreenHeight() + s - 1) / s;
    return Rectangle{ (float)(guiWidth / 2 + button.x), (float)(guiHeight / 4 + button.y), (float)button.width, (float)button.height };
}

Vector2 MenuScreen::MouseGui() const {
    int s = Scale();
    Vector2 mouse = GetMousePosition();
    return Vector2{ (float)(int)(mouse.x / s), (float)(int)(mouse.y / s) };
}

MenuAction MenuScreen::Update(SoundSystem& sounds) {
    if (!open || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return MenuAction::None;
    Vector2 mouse = MouseGui();
    for (const MenuButton& button : buttons) {
        if (!button.enabled || !CheckCollisionPointRec(mouse, Bounds(button))) continue;
        sounds.PlayClick();
        return button.action;
    }
    return MenuAction::None;
}

void MenuScreen::DrawDirtBackground(Texture2D) {
    if (GuiBackdrop::Get() != nullptr) GuiBackdrop::Get()->DrawPanorama(true);
}

// TitleScreen: the logo with "Java Edition" under it, and the splash, tilted and
// pulsing, off its bottom-right corner.
void MenuScreen::DrawTitleDecorations() const {
    int s = Scale();
    int guiWidth = (GetScreenWidth() + s - 1) / s;
    float x = (float)(guiWidth / 2 - 128), y = 30.0f;
    Texture2D logo = GuiModernTexture("title/minecraft");
    Texture2D edition = GuiModernTexture("title/edition");
    DrawGuiTexture(logo, Rectangle{ 0, 0, (float)logo.width, logo.height * 44.0f / 64.0f }, x, y, 256, 44, s);
    DrawGuiTexture(edition, Rectangle{ 0, 0, edition.width * 98.0f / 128.0f, edition.height * 14.0f / 16.0f }, x + 79, y + 37, 98, 14, s);
    if (splash.empty()) return;
    float pulse = 1.8f - fabsf(sinf((float)fmod(GetTime(), 1.0) * PI * 2.0f) * 0.1f);
    float scale = pulse * 100.0f / (font.Width(splash.c_str()) + 32);
    rlDrawRenderBatchActive();
    rlPushMatrix();
    rlTranslatef((float)((guiWidth / 2 + 123) * s), (float)(69 * s), 0.0f);
    rlRotatef(-20.0f, 0, 0, 1);
    rlScalef(scale, scale, 1.0f);
    font.Draw(splash.c_str(), -font.Width(splash.c_str()) / 2 * s, -8 * s, s, Argb(0xFFFFFF00), true);
    rlDrawRenderBatchActive();
    rlPopMatrix();
}

void MenuScreen::Draw() const {
    if (!open) return;
    int s = Scale();
    int width = GetScreenWidth(), height = GetScreenHeight();

    (void)width;
    (void)height;
    if (background == Background::Dirt) DrawDirtBackground(dirt);
    if (background == Background::Title) {
        if (GuiBackdrop::Get() != nullptr) GuiBackdrop::Get()->DrawPanorama(false);
        DrawTitleDecorations();
        font.Draw("Minecraft Beta 1.8", 2 * s, (GetScreenHeight() / s - 10) * s, s, WHITE, true);
    }

    int guiWidth = (width + s - 1) / s;
    if (!title.empty()) {
        font.Draw(title.c_str(), (guiWidth / 2 - font.Width(title.c_str()) / 2) * s, TITLE_Y * s, s, WHITE, true);
    }

    Vector2 mouse = MouseGui();
    for (const MenuButton& button : buttons) {
        Rectangle r = Bounds(button);
        bool hovered = CheckCollisionPointRec(mouse, r);
        DrawGuiButton(font, widgets, r, button.label, button.enabled, hovered, s);
    }
}

void MenuScreen::DrawMessage(const char* text) const {
    int s = Scale();
    DrawDirtBackground(dirt);
    int guiWidth = (GetScreenWidth() + s - 1) / s, guiHeight = (GetScreenHeight() + s - 1) / s;
    font.Draw(text, (guiWidth / 2 - font.Width(text) / 2) * s, (guiHeight / 2 - 50) * s, s, WHITE, true);
}
