#pragma once

#include "raylib.h"
#include "GuiFont.h"
#include "SoundSystem.h"

#include <string>
#include <vector>

enum class MenuAction { None, BackToGame, QuitToTitle, Singleplayer, QuitGame, Options };

// A Minecraft menu button (GuiButton). x is from the screen's center and y from a
// quarter of its height, both in GUI pixels, the way Minecraft's menus lay them out.
struct MenuButton {
    int x, y, width, height;
    std::string label;
    bool enabled;
    MenuAction action;
};

// A modern (1.21) button: the widget/button sprites at `gui` (GUI pixels),
// greyed out when disabled and outlined when hovered. (`widgets` is unused.)
void DrawGuiButton(const GuiFont& font, Texture2D widgets, Rectangle gui, const std::string& label,
                   bool enabled, bool hovered, int scale);

// A Minecraft-style menu screen with modern (1.21) looks: an optional title and
// buttons, over the blurred game (the Esc menu: main draws the blur), the
// blurred panorama, or, for the title screen, the panorama with the Minecraft
// logo and a splash. Buttons for things this game doesn't have are greyed out,
// as Minecraft does for unavailable options.
// Construct after InitWindow() and destroy before CloseWindow().
class MenuScreen {
public:
    enum class Background { DimmedWorld, Dirt, Title };

    MenuScreen(const GuiFont& font, std::string title, std::vector<MenuButton> buttons, Background background);
    ~MenuScreen();

    MenuScreen(const MenuScreen&) = delete;
    MenuScreen& operator=(const MenuScreen&) = delete;

    bool IsOpen() const { return open; }
    void Open() { open = true; }
    void Close() { open = false; }

    // Mouse clicks on the buttons; returns what the player picked.
    MenuAction Update(SoundSystem& sounds);

    void Draw() const;

    // A dirt screen with one line of text in the middle, for while the world loads
    // (Minecraft's "Building terrain").
    void DrawMessage(const char* text) const;

    // The blurred panorama behind menus away from the game (and loading screens).
    static void DrawDirtBackground(Texture2D unused);

private:
    const GuiFont& font;
    std::string title;
    std::vector<MenuButton> buttons;
    Background background;
    Texture2D widgets;
    Texture2D dirt;
    bool open = false;
    std::string splash;

    int Scale() const;
    void DrawTitleDecorations() const;
    Rectangle Bounds(const MenuButton& button) const; // in GUI pixels
    Vector2 MouseGui() const;
};
