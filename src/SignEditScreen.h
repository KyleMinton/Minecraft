#pragma once

#include "raylib.h"
#include "GuiFont.h"

class World;
class SoundSystem;

// GuiEditSign: typing the four lines of a sign just placed. Up/Down/Enter move
// between lines; "Done" or Esc finishes.
// Construct after InitWindow() and destroy before CloseWindow().
class SignEditScreen {
public:
    explicit SignEditScreen(const GuiFont& font);
    ~SignEditScreen();

    SignEditScreen(const SignEditScreen&) = delete;
    SignEditScreen& operator=(const SignEditScreen&) = delete;

    bool IsOpen() const { return open; }
    void Open(World& world, int x, int y, int z);
    void Close();

    // Keyboard and mouse; call every frame while open. Returns true once it closes.
    bool Update(SoundSystem& sounds);
    void Draw() const;

private:
    const GuiFont& font;
    Texture2D signTexture;
    Texture2D widgets;
    bool open = false;
    World* world = nullptr;
    int signX = 0, signY = 0, signZ = 0;
    int line = 0;
    int ticks = 0;
    double tickTimer = 0.0;

    bool SignValid() const;
    Rectangle DoneButton(int scale) const;
};
