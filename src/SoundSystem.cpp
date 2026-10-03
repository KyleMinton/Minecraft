#include "SoundSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

constexpr int VOICES = 4;          // how many copies of one sound can play at once
constexpr int MAX_VARIANTS = 16;
constexpr float RECORD_RANGE = 64.0f; // jukeboxes play at volume 4: 4 x 16 blocks

// "tile.piston.out" -> "resources/sounds/tile/piston/out"
std::string BasePath(const std::string& name) {
    std::string path = "resources/sounds/";
    for (char c : name) path += c == '.' ? '/' : c;
    return path;
}

} // namespace

SoundSystem::SoundSystem() {
    FilePathList files = LoadDirectoryFilesEx("resources/sounds/music", ".ogg", false);
    for (unsigned int i = 0; i < files.count; i++) musicTracks.push_back(files.paths[i]);
    UnloadDirectoryFiles(files);
    ticksBeforeMusic = GetRandomValue(0, 11999);
}

SoundSystem::~SoundSystem() {
    StopAll();
    if (musicPlaying) {
        StopMusicStream(music);
        UnloadMusicStream(music);
    }
    for (auto& [name, bank] : banks) {
        for (auto& voices : bank.aliases)
            for (size_t v = 1; v < voices.size(); v++) UnloadSoundAlias(voices[v]); // voice 0 is the sound itself
        for (Sound& s : bank.sounds) UnloadSound(s);
    }
}

// Loads a sound's files the first time it's played: the name itself, or
// numbered variants 1, 2, 3...
SoundSystem::Variants& SoundSystem::Bank(const std::string& name) {
    auto it = banks.find(name);
    if (it != banks.end()) return it->second;
    Variants& bank = banks[name];
    std::string base = BasePath(name);
    if (FileExists((base + ".ogg").c_str())) {
        bank.sounds.push_back(LoadSound((base + ".ogg").c_str()));
    } else {
        for (int i = 1; i <= MAX_VARIANTS; i++) {
            std::string path = base + std::to_string(i) + ".ogg";
            if (!FileExists(path.c_str())) break;
            bank.sounds.push_back(LoadSound(path.c_str()));
        }
    }
    for (Sound& s : bank.sounds) {
        std::vector<Sound> voices;
        voices.push_back(s);
        for (int v = 1; v < VOICES; v++) voices.push_back(LoadSoundAlias(s));
        bank.aliases.push_back(voices);
        bank.next.push_back(0);
    }
    return bank;
}

void SoundSystem::Start(const std::string& name, float volume, float pitch) {
    Variants& bank = Bank(name);
    if (bank.sounds.empty() || volume <= 0.0f) return;
    int variant = GetRandomValue(0, (int)bank.sounds.size() - 1);
    std::vector<Sound>& voices = bank.aliases[variant];
    Sound& voice = voices[bank.next[variant]];
    bank.next[variant] = (bank.next[variant] + 1) % (int)voices.size();
    SetSoundVolume(voice, std::min(volume, 1.0f) * soundVolume);
    SetSoundPitch(voice, std::clamp(pitch, 0.5f, 2.0f));
    PlaySound(voice);
}

void SoundSystem::SetListener(Vector3 position) {
    listener = position;
}

// Beta's sound engine: linear falloff to silence at 16 blocks, further for
// sounds louder than 1 (which themselves play at full volume).
void SoundSystem::Play(const std::string& name, float x, float y, float z, float volume, float pitch) {
    float range = 16.0f * std::max(volume, 1.0f);
    float dx = x - listener.x, dy = y - listener.y, dz = z - listener.z;
    float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance >= range) return;
    float gain = std::min(volume, 1.0f) * (1.0f - distance / range);
    Start(name, gain, pitch);
}

void SoundSystem::PlayUI(const std::string& name, float volume, float pitch) {
    Start(name, volume, pitch);
}

// Beta's pickup: random.pop at volume 0.2, pitch ((r1 - r2) * 0.7 + 1) * 2.
void SoundSystem::PlayPickup() {
    float r1 = GetRandomValue(0, 1000) / 1000.0f, r2 = GetRandomValue(0, 1000) / 1000.0f;
    PlayUI("random.pop", 0.2f, ((r1 - r2) * 0.7f + 1.0f) * 2.0f);
}

// GuiButton's click: random.click at full volume and pitch.
void SoundSystem::PlayClick() {
    PlayUI("random.click", 1.0f, 1.0f);
}

void SoundSystem::PlayRecord(const std::string& name, int x, int y, int z) {
    StopAll();
    std::string path = "resources/sounds/records/" + name + ".ogg";
    if (!FileExists(path.c_str())) return;
    record = LoadMusicStream(path.c_str());
    record.looping = false;
    recordX = x;
    recordY = y;
    recordZ = z;
    recordPlaying = true;
    PlayMusicStream(record);
    Update(0.0f);
}

void SoundSystem::StopRecord(int x, int y, int z) {
    if (recordPlaying && x == recordX && y == recordY && z == recordZ) StopAll();
}

void SoundSystem::StopAll() {
    if (!recordPlaying) return;
    StopMusicStream(record);
    UnloadMusicStream(record);
    recordPlaying = false;
}

void SoundSystem::SetVolumes(float musicLevel, float soundLevel) {
    musicVolume = musicLevel;
    soundVolume = soundLevel;
    if (musicPlaying) {
        SetMusicVolume(music, musicVolume);
        if (musicVolume <= 0.0f) {
            StopMusicStream(music);
            UnloadMusicStream(music);
            musicPlaying = false;
        }
    }
}

void SoundSystem::Update(float dt) {
    // Music: one track at a time, never over a record, with a long quiet gap between.
    if (musicPlaying) {
        UpdateMusicStream(music);
        if (!IsMusicStreamPlaying(music)) {
            UnloadMusicStream(music);
            musicPlaying = false;
        }
    } else if (musicVolume > 0.0f && !recordPlaying && !musicTracks.empty()) {
        ticksBeforeMusic -= dt * 20.0;
        if (ticksBeforeMusic <= 0.0) {
            const std::string& track = musicTracks[GetRandomValue(0, (int)musicTracks.size() - 1)];
            music = LoadMusicStream(track.c_str());
            music.looping = false;
            SetMusicVolume(music, musicVolume);
            PlayMusicStream(music);
            musicPlaying = true;
            ticksBeforeMusic = GetRandomValue(12000, 23999);
        }
    }

    if (!recordPlaying) return;
    UpdateMusicStream(record);
    float dx = recordX + 0.5f - listener.x, dy = recordY + 0.5f - listener.y, dz = recordZ + 0.5f - listener.z;
    float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    SetMusicVolume(record, std::max(0.0f, 1.0f - distance / RECORD_RANGE) * soundVolume);
    if (!IsMusicStreamPlaying(record)) StopAll();
}
