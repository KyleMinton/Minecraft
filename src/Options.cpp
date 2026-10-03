#include "Options.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

std::string OptionsPath() {
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr) return "options.txt";
    fs::path dir = fs::path(appData) / "RaylibApp";
    std::error_code error;
    fs::create_directories(dir, error);
    return (dir / "options.txt").string();
}

void GameOptions::Load() {
    std::ifstream in(OptionsPath());
    std::string line;
    while (std::getline(in, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon), value = line.substr(colon + 1);
        try {
            if (key == "music") music = std::clamp(std::stof(value), 0.0f, 1.0f);
            else if (key == "sound") sound = std::clamp(std::stof(value), 0.0f, 1.0f);
            else if (key == "mouseSensitivity") sensitivity = std::clamp(std::stof(value), 0.0f, 1.0f);
            else if (key == "invertYMouse") invertMouse = value == "true";
            else if (key == "fov") fov = std::clamp(std::stof(value), 30.0f, 110.0f);
            else if (key == "difficulty") difficulty = std::clamp(std::stoi(value), 0, 3);
            else if (key == "renderDistance") renderDistance = std::clamp(std::stoi(value), 2, 16);
            else if (key == "maxFps") maxFramerate = std::clamp(std::stoi(value), 0, 250);
            else if (key == "bobView") viewBobbing = value == "true";
            else if (key == "guiScale") guiScale = std::clamp(std::stoi(value), 0, 3);
        } catch (...) {
            // A bad value keeps the default.
        }
    }
}

void GameOptions::Save() const {
    std::ofstream out(OptionsPath(), std::ios::trunc);
    out << "music:" << music << "\n";
    out << "sound:" << sound << "\n";
    out << "mouseSensitivity:" << sensitivity << "\n";
    out << "invertYMouse:" << (invertMouse ? "true" : "false") << "\n";
    out << "fov:" << fov << "\n";
    out << "difficulty:" << difficulty << "\n";
    out << "renderDistance:" << renderDistance << "\n";
    out << "maxFps:" << maxFramerate << "\n";
    out << "bobView:" << (viewBobbing ? "true" : "false") << "\n";
    out << "guiScale:" << guiScale << "\n";
}
