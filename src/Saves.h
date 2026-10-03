#pragma once

#include "Player.h"

#include <cstdint>
#include <string>
#include <vector>

class World;
class DroppedItems;

// One saved world: a folder under saves/ holding
//   level.dat  - name, seed, game mode, time of day, when it was last played
//   player.dat - the player (Player::Save)
//   edits.dat  - every block the player changed (World::SaveEdits)
//   items.dat  - items lying on the ground (DroppedItems::Save)
//   tiles.dat  - tile entities and pending block updates (World::SaveTileEntities)
struct WorldInfo {
    std::string folder;
    std::string name;
    int64_t seed = 0;
    GameMode gameMode = GameMode::Survival;
    long long lastPlayed = 0; // Unix time, seconds
    long long time = 0;       // world ticks
    int generatorVersion = Chunk::GENERATOR_VERSION; // terrain generator it was created with
};

namespace Saves {

// Every world in saves/, most recently played first.
std::vector<WorldInfo> ListWorlds();

// A folder name for a new world called `name` (GuiCreateWorld's rules): characters
// Windows can't use become '_', and "-" is added until it doesn't clash.
std::string FolderFor(const std::string& name);

// Minecraft's seed box: a number is used as-is, other text is hashed like Java's
// String.hashCode(), and a blank box picks a random seed.
int64_t SeedFromText(const std::string& text);

// Makes the folder and level.dat for a brand-new world.
bool CreateWorld(const WorldInfo& info);

// Writes everything. Updates info.lastPlayed, time and game mode first.
bool SaveWorld(WorldInfo& info, const World& world, const Player& player, const DroppedItems& drops, long long time);

bool HasPlayer(const WorldInfo& info);
bool LoadPlayer(const WorldInfo& info, Player& player);
void LoadItems(const WorldInfo& info, DroppedItems& drops);
std::string EditsPath(const WorldInfo& info);
// tiles.dat: chests' contents, signs' text, furnaces... (World::SaveTileEntities).
void LoadTileEntities(const WorldInfo& info, World& world);

bool DeleteWorld(const WorldInfo& info);

// "2026/09/30 14:05", as the world list shows it.
std::string FormatDate(long long unixSeconds);

} // namespace Saves
