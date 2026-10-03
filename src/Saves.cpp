#include "Saves.h"
#include "World.h"
#include "DroppedItems.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Worlds live in %APPDATA%\RaylibApp\saves (like .minecraft), so the game can
// be installed somewhere read-only. Worlds from the old saves/ folder next to
// the game are copied there the first time.
fs::path SavesDir() {
    static fs::path dir;
    if (!dir.empty()) return dir;
    const char* appData = std::getenv("APPDATA");
    if (appData == nullptr) {
        dir = "saves";
        return dir;
    }
    dir = fs::path(appData) / "RaylibApp" / "saves";
    std::error_code error;
    if (!fs::exists(dir, error) && fs::is_directory("saves", error)) {
        fs::create_directories(dir.parent_path(), error);
        fs::copy("saves", dir, fs::copy_options::recursive, error);
    }
    fs::create_directories(dir, error);
    return dir;
}

fs::path FolderPath(const WorldInfo& info) {
    return SavesDir() / info.folder;
}

// Writes to a temporary file and then swaps it in, so a crash mid-save can't
// leave a half-written file behind.
bool WriteFileSafely(const fs::path& path, const std::string& contents) {
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << contents;
        if (!out) return false;
    }
    std::error_code error;
    fs::rename(temp, path, error);
    return !error;
}

std::string LevelText(const WorldInfo& info) {
    std::ostringstream out;
    out << "name " << info.name << "\n";
    out << "seed " << info.seed << "\n";
    out << "gamemode " << (info.gameMode == GameMode::Creative ? 1 : 0) << "\n";
    out << "time " << info.time << "\n";
    out << "lastplayed " << info.lastPlayed << "\n";
    return out.str();
}

bool ReadLevel(const fs::path& folder, WorldInfo& info) {
    std::ifstream in(folder / "level.dat");
    if (!in) return false;
    info.folder = folder.filename().string();
    info.name = info.folder;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string key;
        fields >> key;
        if (key == "name") {
            std::getline(fields >> std::ws, info.name);
        } else if (key == "seed") {
            fields >> info.seed;
        } else if (key == "gamemode") {
            int mode = 0;
            fields >> mode;
            info.gameMode = mode == 1 ? GameMode::Creative : GameMode::Survival;
        } else if (key == "time") {
            fields >> info.time;
        } else if (key == "lastplayed") {
            fields >> info.lastPlayed;
        }
    }
    return true;
}

} // namespace

namespace Saves {

std::vector<WorldInfo> ListWorlds() {
    std::vector<WorldInfo> worlds;
    std::error_code error;
    if (!fs::is_directory(SavesDir(), error)) return worlds;
    for (const fs::directory_entry& entry : fs::directory_iterator(SavesDir(), error)) {
        WorldInfo info;
        if (entry.is_directory() && ReadLevel(entry.path(), info)) worlds.push_back(info);
    }
    std::sort(worlds.begin(), worlds.end(), [](const WorldInfo& a, const WorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return worlds;
}

std::string FolderFor(const std::string& name) {
    std::string folder;
    for (char c : name) {
        folder += std::string("/\n\r\t\f`?*\\<>|\":").find(c) != std::string::npos || c == '\0' ? '_' : c;
    }
    while (!folder.empty() && (folder.back() == ' ' || folder.back() == '.')) folder.pop_back();
    if (folder.empty()) folder = "World";

    // Names Windows reserves for devices.
    static const char* RESERVED[] = { "CON", "COM", "PRN", "AUX", "CLOCK$", "NUL", "COM1", "COM2", "COM3", "COM4",
                                      "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                      "LPT6", "LPT7", "LPT8", "LPT9" };
    std::string upper = folder;
    for (char& c : upper) c = (char)toupper((unsigned char)c);
    for (const char* reserved : RESERVED) {
        if (upper == reserved) folder = "_" + folder + "_";
    }

    std::error_code error;
    while (fs::exists(SavesDir() / folder, error)) folder += "-";
    return folder;
}

int64_t SeedFromText(const std::string& text) {
    std::string trimmed = text;
    trimmed.erase(0, trimmed.find_first_not_of(' '));
    trimmed.erase(trimmed.find_last_not_of(' ') + 1);
    if (trimmed.empty()) {
        std::random_device device;
        std::mt19937_64 random(((uint64_t)device() << 32) ^ device() ^ (uint64_t)time(nullptr));
        return (int64_t)random();
    }
    try {
        size_t used = 0;
        long long number = std::stoll(trimmed, &used);
        if (used == trimmed.size()) return number != 0 ? number : SeedFromText(""); // 0 means random, like Minecraft
    } catch (...) {
    }
    int32_t hash = 0;
    for (unsigned char c : trimmed) hash = (int32_t)((uint32_t)hash * 31u + c);
    return hash;
}

bool CreateWorld(const WorldInfo& info) {
    std::error_code error;
    fs::create_directories(FolderPath(info), error);
    if (error) return false;
    return WriteFileSafely(FolderPath(info) / "level.dat", LevelText(info));
}

bool SaveWorld(WorldInfo& info, const World& world, const Player& player, const DroppedItems& drops, long long time) {
    std::error_code error;
    fs::create_directories(FolderPath(info), error);
    info.lastPlayed = (long long)std::time(nullptr);
    info.time = time;
    info.gameMode = player.GetGameMode();

    std::ostringstream playerText, itemsText, tilesText;
    player.Save(playerText);
    drops.Save(itemsText);
    world.SaveTileEntities(tilesText);
    bool ok = WriteFileSafely(FolderPath(info) / "player.dat", playerText.str());
    ok = WriteFileSafely(FolderPath(info) / "items.dat", itemsText.str()) && ok;
    ok = WriteFileSafely(FolderPath(info) / "tiles.dat", tilesText.str()) && ok;

    fs::path edits = FolderPath(info) / "edits.dat";
    fs::path editsTemp = edits;
    editsTemp += ".tmp";
    if (world.SaveEdits(editsTemp.string())) {
        fs::rename(editsTemp, edits, error);
        ok = ok && !error;
    } else {
        ok = false;
    }
    // level.dat last: it's what the world list reads.
    return WriteFileSafely(FolderPath(info) / "level.dat", LevelText(info)) && ok;
}

bool HasPlayer(const WorldInfo& info) {
    std::error_code error;
    return fs::exists(FolderPath(info) / "player.dat", error);
}

bool LoadPlayer(const WorldInfo& info, Player& player) {
    std::ifstream in(FolderPath(info) / "player.dat");
    if (!in) return false;
    player.Load(in);
    return true;
}

void LoadItems(const WorldInfo& info, DroppedItems& drops) {
    std::ifstream in(FolderPath(info) / "items.dat");
    if (in) drops.Load(in);
    else drops.Clear();
}

void LoadTileEntities(const WorldInfo& info, World& world) {
    std::ifstream in(FolderPath(info) / "tiles.dat");
    if (in) world.LoadTileEntities(in);
}

std::string EditsPath(const WorldInfo& info) {
    return (FolderPath(info) / "edits.dat").string();
}

bool DeleteWorld(const WorldInfo& info) {
    if (info.folder.empty()) return false;
    std::error_code error;
    fs::remove_all(FolderPath(info), error);
    return !error;
}

std::string FormatDate(long long unixSeconds) {
    std::time_t t = (std::time_t)unixSeconds;
    std::tm local{};
    localtime_s(&local, &t);
    char text[32];
    std::strftime(text, sizeof(text), "%Y/%m/%d %H:%M", &local);
    return text;
}

} // namespace Saves
