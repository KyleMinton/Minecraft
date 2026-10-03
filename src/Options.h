#pragma once

#include <string>

// The player's settings (Minecraft's GameSettings), kept in options.txt as
// Beta's "key:value" lines, next to the saves in %APPDATA%\RaylibApp.
struct GameOptions {
    float music = 1.0f;        // volume, 0..1
    float sound = 1.0f;
    float sensitivity = 0.5f;  // 0.5 = 100%
    bool invertMouse = false;
    float fov = 110.0f;        // degrees, 30..110
    int difficulty = 2;        // 0 Peaceful, 1 Easy, 2 Normal, 3 Hard
    int renderDistance = 8;    // chunks, 2..16
    int maxFramerate = 0;      // 0 = unlimited, else 10..250
    bool viewBobbing = true;
    int guiScale = 0;          // 0 Auto, 1 Small, 2 Normal, 3 Large

    void Load();
    void Save() const;
};

std::string OptionsPath();
