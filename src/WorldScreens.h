#pragma once

#include "raylib.h"
#include "GuiFont.h"
#include "SoundSystem.h"
#include "Saves.h"

#include <string>
#include <vector>

// A one-line text box (GuiTextField): click to focus, type, Backspace/Delete,
// arrow keys, Home/End. Positions are GUI pixels.
struct TextField {
    int x = 0, y = 0, width = 200, height = 20;
    std::string text;
    size_t cursor = 0;
    size_t maxLength = 32;
    bool focused = false;

    void SetText(const std::string& value);
    // Returns true if the text changed.
    bool HandleKeys();
    void Click(Vector2 mouse, const GuiFont& font);
    void Draw(const GuiFont& font, int scale) const;
};

// Minecraft 1.11's world screens, all on the dirt background:
//   Select World     - the saved worlds, newest first, with Play / Create / Delete
//   Create New World - name, game mode, and (under More World Options) the seed
//   Delete confirm   - "Are you sure you want to delete this world?"
// Construct after InitWindow() and destroy before CloseWindow().
class WorldScreens {
public:
    enum class Page { Closed, Select, Create, ConfirmDelete };

    // What the player asked for this frame.
    struct Result {
        enum class Kind { None, Play, Create, Back } kind = Kind::None;
        WorldInfo world; // for Play (an existing world) and Create (a new one, already on disk)
    };

    explicit WorldScreens(const GuiFont& font);
    ~WorldScreens();

    WorldScreens(const WorldScreens&) = delete;
    WorldScreens& operator=(const WorldScreens&) = delete;

    bool IsOpen() const { return page != Page::Closed; }
    void OpenSelect();
    void Close() { page = Page::Closed; }

    Result Update(SoundSystem& sounds);
    void Draw() const;

private:
    struct Button {
        Rectangle rect; // GUI pixels
        std::string label;
        bool enabled;
    };

    const GuiFont& font;
    Texture2D widgets, dirt, unknownIcon, selectionIcons;
    Page page = Page::Closed;

    // Select World
    std::vector<WorldInfo> worlds;
    int selected = -1;
    float scroll = 0.0f;
    int lastClickIndex = -1;
    double lastClickTime = -1.0;

    // Create New World
    TextField nameField, seedField;
    GameMode newMode = GameMode::Survival;
    bool moreOptions = false;
    std::string folderName;

    int Scale() const;
    int GuiWidth() const;
    int GuiHeight() const;
    Vector2 MouseGui() const;
    bool Hovered(const Button& button) const;
    bool Clicked(const Button& button, SoundSystem& sounds) const;
    void DrawButton(const Button& button) const;
    void DrawCentered(const char* text, int y, Color color) const;
    void DrawDirt(int top, int bottom, float offset, unsigned char shade) const;

    std::vector<Button> SelectButtons() const;
    std::vector<Button> CreateButtons() const;
    std::vector<Button> DeleteButtons() const;
    int ListTop() const { return 32; }
    int ListBottom() const { return GuiHeight() - 64; }
    float MaxScroll() const;
    void OpenCreate();

    Result UpdateSelect(SoundSystem& sounds);
    Result UpdateCreate(SoundSystem& sounds);
    Result UpdateDelete(SoundSystem& sounds);
    void DrawSelect() const;
    void DrawCreate() const;
    void DrawDelete() const;
};
