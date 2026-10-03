#pragma once

#include "raylib.h"
#include "GuiFont.h"
#include "Options.h"
#include "SoundSystem.h"

#include <functional>
#include <string>
#include <vector>

// Minecraft's Options screens (GuiOptions and its sub-screens, 1.11's layout to
// match the other menus): FOV and difficulty, then Music & Sounds, Video
// Settings and Controls, with sliders you drag and buttons you click to cycle.
// Changes apply as you make them and are saved when you leave.
// Construct after InitWindow() and destroy before CloseWindow().
class OptionsScreen {
public:
    explicit OptionsScreen(const GuiFont& font);
    ~OptionsScreen();

    OptionsScreen(const OptionsScreen&) = delete;
    OptionsScreen& operator=(const OptionsScreen&) = delete;

    bool IsOpen() const { return open; }
    // Over the darkened world (from the game menu) or the dirt (from the title screen).
    void Open(bool overWorld);

    // Mouse and Esc; `options` is edited in place. Returns true when something
    // changed (apply it), and closes itself (saving the options) on Done or Esc.
    bool Update(SoundSystem& sounds, GameOptions& options);
    void Draw(const GameOptions& options) const;

private:
    enum class Page { Main, Sounds, Video, Controls };
    enum class Kind { Button, Slider };
    struct Widget {
        Kind kind;
        int x, y, width;       // x from the center, y from a sixth of the height, GUI pixels
        bool enabled;
        std::function<std::string(const GameOptions&)> label;
        std::function<float(const GameOptions&)> value;          // sliders: 0..1
        std::function<void(GameOptions&, float)> set;            // sliders: from 0..1; buttons: clicked
        int goTo = -1;                                           // buttons that open another page
        bool done = false;                                       // the "Done" button
    };

    const GuiFont& font;
    Texture2D widgets, dirt;
    bool open = false;
    bool overWorld = false;
    Page page = Page::Main;
    int dragging = -1;

    std::vector<Widget> Widgets() const;
    Rectangle Bounds(const Widget& w) const;
    Vector2 MouseGui() const;
    int Scale() const;
    std::string Title() const;
    void Back(GameOptions& options);
};
