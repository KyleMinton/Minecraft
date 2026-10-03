#pragma once

#include "raylib.h"

#include <string>
#include <unordered_map>
#include <vector>

// Minecraft Beta 1.8's sounds, by the names the game uses ("step.stone",
// "random.click", "tile.piston.out"...). A name maps to files under
// resources/sounds/ with the dots as folders: "step.stone" plays one of
// step/stone1.ogg, stone2.ogg... at random, "random.click" plays random/click.ogg.
// Sounds in the world fade with distance from the listener like Beta's
// (16 blocks, or further for louder sounds). Records stream from records/,
// and Beta's music (music/calm, hal, nuance, piano) plays every so often.
// Construct after InitAudioDevice() and destroy before CloseAudioDevice().
class SoundSystem {
public:
    SoundSystem();
    ~SoundSystem();

    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    // Where the player's ears are, for positional sounds.
    void SetListener(Vector3 position);

    // A sound at a place in the world.
    void Play(const std::string& name, float x, float y, float z, float volume, float pitch);
    // A sound with no place (GUI clicks, the player's own sounds).
    void PlayUI(const std::string& name, float volume, float pitch);

    void PlayPickup(); // an item going into your inventory
    void PlayClick();  // a menu button

    // Jukeboxes: a record plays from a block until it's taken out.
    void PlayRecord(const std::string& record, int x, int y, int z);
    void StopRecord(int x, int y, int z);
    void StopAll();

    // Once a frame: keeps records and music streaming and at the right volume, and
    // now and then starts a music track (Beta's SoundManager.playRandomMusicIfReady).
    void Update(float dt);

    // Which music fits now (1.21's MusicManager situations): the menu's starts within
    // 1-30 seconds and cuts off game music; in a world, a menu track plays out, then
    // the game's long gaps apply. Menu tracks come from music/menu/ if it exists.
    void SetMenuMusic(bool menu);

    // The options menu's sliders, 0..1.
    void SetVolumes(float music, float sound);

private:
    struct Variants {
        std::vector<Sound> sounds;
        std::vector<std::vector<Sound>> aliases; // extra voices so a sound can overlap itself
        std::vector<int> next;
    };
    std::unordered_map<std::string, Variants> banks;
    Vector3 listener = { 0, 0, 0 };

    float musicVolume = 1.0f, soundVolume = 1.0f;
    Music music = {};
    bool musicPlaying = false;
    double ticksBeforeMusic = 0.0;   // Beta waits 0-10 minutes for the first track, 10-20 between tracks
    std::vector<std::string> musicTracks;
    std::vector<std::string> menuTracks; // empty: the menu uses musicTracks
    bool inMenu = false;
    bool musicIsMenu = false;            // the playing track was started for the menu

    Music record = {};
    bool recordPlaying = false;
    int recordX = 0, recordY = 0, recordZ = 0;

    Variants& Bank(const std::string& name);
    void Start(const std::string& name, float volume, float pitch);
};
