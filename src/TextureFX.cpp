#include "TextureFX.h"
#include "Block.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

constexpr double TICK_SECONDS = 1.0 / 20.0;
constexpr int TILE_SIZE = 16; // terrain.png's tiles

// The few parts of an .mcmeta "animation" block the game's textures use:
// "frametime" (ticks per frame, default 1) and "frames", a list of frame
// numbers or {"index": n, "time": t} objects (default: every frame in order).
void ParseMcmeta(const std::string& text, int frameCount, std::vector<int>& frames, std::vector<int>& times) {
    auto numberAfter = [&](size_t from, size_t to, const char* key, int fallback) {
        size_t at = text.find(key, from);
        if (at == std::string::npos || at >= to) return fallback;
        at = text.find(':', at);
        return at == std::string::npos ? fallback : std::atoi(text.c_str() + at + 1);
    };
    int frameTime = std::max(1, numberAfter(0, text.size(), "\"frametime\"", 1));

    size_t list = text.find("\"frames\"");
    if (list != std::string::npos) list = text.find('[', list);
    size_t end = list == std::string::npos ? std::string::npos : text.find(']', list);
    if (end != std::string::npos) {
        for (size_t i = list + 1; i < end; i++) {
            char c = text[i];
            if (c == '{') {
                size_t close = std::min(text.find('}', i), end);
                frames.push_back(numberAfter(i, close, "\"index\"", 0));
                times.push_back(std::max(1, numberAfter(i, close, "\"time\"", frameTime)));
                i = close;
            } else if (std::isdigit((unsigned char)c)) {
                frames.push_back(std::atoi(text.c_str() + i));
                times.push_back(frameTime);
                while (i + 1 < end && std::isdigit((unsigned char)text[i + 1])) i++;
            }
        }
    }
    if (frames.empty()) {
        for (int f = 0; f < frameCount; f++) {
            frames.push_back(f);
            times.push_back(frameTime);
        }
    }
    for (int& f : frames) f = std::clamp(f, 0, frameCount - 1);
}

} // namespace

TextureFX::TextureFX(Texture2D terrain) : atlas(terrain) {
    Load("water_still", 205);
    Load("water_flow", 206); // 32px frames: tiles 206-207 and 222-223
    Load("lava_still", 237);
    Load("lava_flow", 238);
    Load("fire_0", 31);
    Load("fire_1", 47);
    Load("nether_portal", 14);
}

TextureFX::~TextureFX() {
    for (Animation& a : animations) UnloadImage(a.strip);
}

// Without its strip (or with a terrain.png that isn't 16px tiles) a tile just
// keeps the still image it has in the atlas.
void TextureFX::Load(const std::string& name, int tile) {
    if (atlas.id == 0 || atlas.width != TILE_SIZE * ATLAS_TILES) return;
    std::string path = "resources/textures/animated/" + name + ".png";
    if (!FileExists(path.c_str())) return;
    Animation a;
    a.strip = LoadImage(path.c_str());
    if (a.strip.data == nullptr || a.strip.width <= 0 || a.strip.height < a.strip.width || a.strip.width % TILE_SIZE != 0) {
        UnloadImage(a.strip);
        return;
    }
    ImageFormat(&a.strip, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    a.size = a.strip.width;
    a.tile = tile;

    std::string mcmeta;
    std::string metaPath = path + ".mcmeta";
    if (FileExists(metaPath.c_str())) {
        char* text = LoadFileText(metaPath.c_str());
        if (text != nullptr) {
            mcmeta = text;
            UnloadFileText(text);
        }
    }
    ParseMcmeta(mcmeta, a.strip.height / a.size, a.frames, a.times);
    a.ticksLeft = a.times[0];
    Upload(a);
    animations.push_back(std::move(a));
}

void TextureFX::Update(float dt) {
    accumulator += dt;
    int ticks = 0;
    while (accumulator >= TICK_SECONDS && ticks < 5) {
        Tick();
        accumulator -= TICK_SECONDS;
        ticks++;
    }
    if (accumulator >= TICK_SECONDS) accumulator = 0.0;
}

void TextureFX::Tick() {
    for (Animation& a : animations) {
        if (--a.ticksLeft > 0) continue;
        a.step = (a.step + 1) % (int)a.frames.size();
        a.ticksLeft = a.times[a.step];
        Upload(a);
    }
}

// The current frame into its tile(s): a 32px frame fills 2x2 tiles.
void TextureFX::Upload(const Animation& a) const {
    const unsigned char* frame = (const unsigned char*)a.strip.data + (size_t)a.frames[a.step] * a.size * a.size * 4;
    Rectangle rect = { (float)(TileColumn(a.tile) * TILE_SIZE), (float)(TileRow(a.tile) * TILE_SIZE), (float)a.size, (float)a.size };
    UpdateTextureRec(atlas, rect, frame);
}
