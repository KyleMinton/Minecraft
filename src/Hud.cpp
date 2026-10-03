#include "Hud.h"
#include "Gui.h"
#include "JavaRandom.h"
#include "Items.h"

#include "rlgl.h"

#include <cmath>
#include <algorithm>
#include <string>

namespace {

// Minecraft's texture sizes and layout, in GUI pixels (multiplied by the GUI scale).
constexpr int HOTBAR_WIDTH = 182;
constexpr int HOTBAR_HEIGHT = 22;
constexpr int SLOT_SPACING = 20;
constexpr int ITEM_SIZE = 16;
constexpr int POP_TICKS = 5;       // matches the pickup animation length in Inventory.cpp
constexpr int NAME_Y_FROM_BOTTOM = 59;
constexpr int CROSSHAIR_CENTER_X = 8; // the cross's middle pixel in crosshair.png
constexpr int CROSSHAIR_CENTER_Y = 7;

constexpr int CREATIVE_NAME_DROP = 14;     // no health rows in creative, so the name sits lower

constexpr double NAME_SHOW_SECONDS = 2.0; // 40 ticks
constexpr double NAME_FADE_SECONDS = 0.5; // fades over the last 10 ticks

// Survival stats (GuiIngame.renderPlayerStats / renderExpBar), in GUI pixels.
constexpr int STATS_Y_FROM_BOTTOM = 39;  // hearts and hunger row
constexpr int XP_BAR_Y_FROM_BOTTOM = 29;
constexpr int ICON_SIZE = 9;
constexpr int ICON_SPACING = 8;          // icons overlap by a pixel
constexpr int LOW_HEALTH_SHAKE = 4;      // hearts jitter at 2 hearts or less

} // namespace

Hud::Hud(const ItemIcons& icons, const GuiFont& guiFont, Texture2D terrain) : items(icons), font(guiFont), atlas(terrain) {
    pumpkinBlur = LoadPixelTexture("resources/textures/misc/pumpkinblur.png");

    // Premultiplied so transparent pixels are black: with the inverting blend
    // below, black leaves the screen untouched and white inverts it.
    Image crosshair = LoadImage("resources/textures/gui/modern/sprites/hud/crosshair.png");
    ImageAlphaPremultiply(&crosshair);
    crosshairTexture = LoadTextureFromImage(crosshair);
    UnloadImage(crosshair);
    SetTextureFilter(crosshairTexture, TEXTURE_FILTER_POINT);
}

Hud::~Hud() {
    UnloadTexture(crosshairTexture);
    UnloadTexture(pumpkinBlur);
}

// A modern HUD sprite (gui/modern/sprites/hud/...) with its top-left at screen pixel (x, y).
static void HudSprite(const char* name, int x, int y, int s, int width = -1) {
    Texture2D t = GuiSprite(std::string("hud/") + name);
    if (t.id == 0) return;
    float w = width < 0 ? (float)t.width : (float)width;
    DrawTexturePro(t, Rectangle{ 0, 0, w, (float)t.height }, Rectangle{ (float)x, (float)y, w * s, (float)(t.height * s) }, Vector2{ 0, 0 }, 0.0f, WHITE);
}


// Hearts on the left, hunger on the right, XP bar underneath, as in Minecraft 1.8.
void Hud::DrawSurvivalStats(const Player& player, int left, int s) {
    int height = GetScreenHeight();
    PlayerStats stats = player.GetStats();
    int health = (int)ceilf(stats.health);
    long long ticks = stats.ticks;
    double now = GetTime();

    // After a hit (or heal) the hearts flash, outlining the old health for a second.
    if (health < displayedHealth && stats.hurtResistantTime > 0) {
        lastHealthChange = now;
        healthFlashUntil = ticks + 20;
    } else if (health > displayedHealth && stats.hurtResistantTime > 0) {
        lastHealthChange = now;
        healthFlashUntil = ticks + 10;
    }
    if (now - lastHealthChange > 1.0) {
        previousHealth = health;
        lastHealthChange = now;
    }
    displayedHealth = health;
    bool flash = healthFlashUntil > ticks && (healthFlashUntil - ticks) / 3 % 2 == 1;

    // Minecraft reseeds this every frame from the tick count, so the shake is stable within a tick.
    JavaRandom random(ticks * 312871LL);
    int rowY = height - STATS_Y_FROM_BOTTOM * s;

    for (int i = 9; i >= 0; i--) {
        int x = left + i * ICON_SPACING * s;
        int y = rowY;
        if (health <= LOW_HEALTH_SHAKE) y += random.NextInt(2) * s;

        HudSprite(flash ? "heart/container_blinking" : "heart/container", x, y, s);
        if (flash) {
            if (i * 2 + 1 < previousHealth) HudSprite("heart/full_blinking", x, y, s);
            if (i * 2 + 1 == previousHealth) HudSprite("heart/half_blinking", x, y, s);
        }
        if (i * 2 + 1 < health) HudSprite("heart/full", x, y, s);
        if (i * 2 + 1 == health) HudSprite("heart/half", x, y, s);
    }

    int right = left + HOTBAR_WIDTH * s;
    for (int i = 0; i < 10; i++) {
        int x = right - (i * ICON_SPACING + ICON_SIZE) * s;
        int y = rowY;
        // With no saturation left, the hunger bar twitches, faster the emptier it is.
        if (stats.saturation <= 0.0f && ticks % (stats.food * 3 + 1) == 0) y += (random.NextInt(3) - 1) * s;

        HudSprite("food_empty", x, y, s);
        if (i * 2 + 1 < stats.food) HudSprite("food_full", x, y, s);
        if (i * 2 + 1 == stats.food) HudSprite("food_half", x, y, s);
    }

    // Armor above the hearts, air bubbles above the hunger (Beta 1.8's GuiIngame).
    int upperY = rowY - 10 * s;
    if (stats.armor > 0) {
        for (int i = 0; i < 10; i++) {
            int x = left + i * ICON_SPACING * s;
            if (i * 2 + 1 < stats.armor) HudSprite("armor_full", x, upperY, s);
            else if (i * 2 + 1 == stats.armor) HudSprite("armor_half", x, upperY, s);
            else HudSprite("armor_empty", x, upperY, s);
        }
    }
    if (stats.headInWater) {
        int full = (int)ceil((stats.air - 2) * 10.0 / 300.0);
        int popping = (int)ceil(stats.air * 10.0 / 300.0) - full;
        for (int i = 0; i < full + popping; i++) {
            int x = right - (i * ICON_SPACING + ICON_SIZE) * s;
            HudSprite(i < full ? "air" : "air_bursting", x, upperY, s);
        }
    }

    int barY = height - XP_BAR_Y_FROM_BOTTOM * s;
    HudSprite("experience_bar_background", left, barY, s);
    int filled = std::min((int)(stats.experience * (HOTBAR_WIDTH + 1)), HOTBAR_WIDTH);
    if (filled > 0) HudSprite("experience_bar_progress", left, barY, s, filled);

    if (stats.level > 0) {
        // Level number in green with a black outline, centered just above the bar.
        std::string text = std::to_string(stats.level);
        int textX = GetScreenWidth() / 2 - font.Width(text.c_str()) * s / 2;
        int textY = height - (XP_BAR_Y_FROM_BOTTOM + 6) * s;
        for (int dx = -1; dx <= 1; dx += 2) font.Draw(text.c_str(), textX + dx * s, textY, s, BLACK);
        for (int dy = -1; dy <= 1; dy += 2) font.Draw(text.c_str(), textX, textY + dy * s, s, BLACK);
        font.Draw(text.c_str(), textX, textY, s, Argb(0xFF80FF20));
    }
}

void Hud::Draw(const Player& player) {
    int width = GetScreenWidth();
    int height = GetScreenHeight();
    int s = GuiScale(width, height);
    DrawOverlays(player, s);
    int slot = player.GetSelectedSlot();
    GameMode mode = player.GetGameMode();

    // The popup shows the selected item's name whenever it changes to a different
    // item (and hides it for an empty hand), or the new game mode after switching.
    ItemStack item = player.HeldItem();
    bool changed = item.IsEmpty() != lastItem.IsEmpty() || item.id != lastItem.id || item.damage != lastItem.damage;
    if (!itemKnown || changed) {
        if (itemKnown) {
            popupText = item.IsEmpty() ? "" : ItemName(item);
            popupShownAt = GetTime();
        }
        lastItem = item;
        itemKnown = true;
    }
    if (!modeKnown || mode != lastMode) {
        if (modeKnown) {
            popupText = (mode == GameMode::Creative) ? "Creative Mode" : "Survival Mode";
            popupShownAt = GetTime();
        }
        lastMode = mode;
        modeKnown = true;
    }

    // Hotbar, then the selection frame (1px outside the slot), then the icons.
    int left = width / 2 - (HOTBAR_WIDTH / 2) * s;
    int top = height - HOTBAR_HEIGHT * s;
    HudSprite("hotbar", left, top, s);
    HudSprite("hotbar_selection", left + (slot * SLOT_SPACING - 1) * s, top - s, s);

    const Inventory& inventory = player.GetInventory();
    for (int i = 0; i < Inventory::HOTBAR_SIZE; i++) {
        const ItemStack& stack = inventory.main[i];
        if (stack.IsEmpty()) continue;
        int x = left + (3 + i * SLOT_SPACING) * s;
        int y = top + 3 * s;

        // A freshly picked-up stack squashes and springs back (GuiIngame.renderHotbarItem).
        float pop = stack.popTicks > 0 ? (float)stack.popTicks - player.GetTickAlpha() : 0.0f;
        Rectangle dest = { (float)x, (float)y, (float)(ITEM_SIZE * s), (float)(ITEM_SIZE * s) };
        if (pop > 0.0f) {
            float squash = 1.0f + pop / POP_TICKS;
            float anchorX = x + 8.0f * s, anchorY = y + 12.0f * s;
            dest.width /= squash;
            dest.height *= (squash + 1.0f) / 2.0f;
            dest.x = anchorX - (anchorX - x) / squash;
            dest.y = anchorY - (anchorY - y) * (squash + 1.0f) / 2.0f;
        }
        items.DrawIcon(stack, dest);
        items.DrawCount(stack, x, y, s, font);
    }

    if (mode == GameMode::Survival) DrawSurvivalStats(player, left, s);

    // Popup text above the hotbar, fading out, with Minecraft's drop shadow.
    double remaining = NAME_SHOW_SECONDS - (GetTime() - popupShownAt);
    if (remaining > 0.0 && !popupText.empty()) {
        float alpha = (float)fmin(1.0, remaining / NAME_FADE_SECONDS);
        const char* text = popupText.c_str();
        int textX = width / 2 - font.Width(text) * s / 2;
        int textY = height - (NAME_Y_FROM_BOTTOM - (mode == GameMode::Creative ? CREATIVE_NAME_DROP : 0)) * s;
        font.Draw(text, textX, textY, s, Fade(WHITE, alpha), true);
    }

    DrawMessages(player, s);

    // Crosshair, inverting the screen behind it: result = (1 - dst) * src + dst * (1 - src).
    rlSetBlendFactors(RL_ONE_MINUS_DST_COLOR, RL_ONE_MINUS_SRC_COLOR, RL_FUNC_ADD);
    BeginBlendMode(BLEND_CUSTOM);
        DrawTextureEx(crosshairTexture,
                      Vector2{ (float)(width / 2 - CROSSHAIR_CENTER_X * s), (float)(height / 2 - CROSSHAIR_CENTER_Y * s) },
                      0.0f, (float)s, WHITE);
    EndBlendMode();
}

// What's in front of your eyes: a pumpkin on your head, portal swirl, flames,
// and the fade to black while you sleep.
void Hud::DrawOverlays(const Player& player, int s) const {
    (void)s;
    int width = GetScreenWidth(), height = GetScreenHeight();
    const ItemStack& helmet = player.GetInventory().armor[0];
    if (!helmet.IsEmpty() && helmet.id == BLOCK_PUMPKIN) {
        DrawTexturePro(pumpkinBlur, Rectangle{ 0, 0, (float)pumpkinBlur.width, (float)pumpkinBlur.height },
                       Rectangle{ 0, 0, (float)width, (float)height }, Vector2{ 0, 0 }, 0.0f, WHITE);
    }
    float portal = player.PortalTime();
    if (portal > 0.0f) {
        // GuiIngame.renderPortalOverlay: the portal texture over everything, fading in.
        float alpha = portal < 1.0f ? portal * portal * portal * portal * 0.8f + 0.2f : 1.0f;
        Rectangle tile = { (14 % 16) * 16.0f, (14 / 16) * 16.0f, 16.0f, 16.0f };
        DrawTexturePro(atlas, tile, Rectangle{ 0, 0, (float)width, (float)height }, Vector2{ 0, 0 }, 0.0f, Fade(WHITE, alpha));
    }
    if (player.IsBurning()) {
        // ItemRenderer.renderFireInFirstPerson: flames licking up both lower corners.
        Rectangle tile = { (31 % 16) * 16.0f, (31 / 16) * 16.0f, 16.0f, 16.0f };
        float size = height * 0.9f;
        DrawTexturePro(atlas, tile, Rectangle{ width * 0.25f, height - size * 0.35f, size, size }, Vector2{ size / 2, size / 2 }, -10.0f,
                       Fade(WHITE, 0.9f));
        Rectangle mirrored = tile;
        mirrored.width = -16.0f;
        DrawTexturePro(atlas, mirrored, Rectangle{ width * 0.75f, height - size * 0.35f, size, size }, Vector2{ size / 2, size / 2 }, 10.0f,
                       Fade(WHITE, 0.9f));
    }
    if (player.IsSleeping()) {
        float f = player.SleepTimer() / 100.0f;
        if (f > 1.0f) f = 1.0f - (player.SleepTimer() - 100) / 10.0f;
        DrawRectangle(0, 0, width, height, Color{ 16, 16, 32, (unsigned char)(std::clamp(f * 0.8f, 0.0f, 1.0f) * 255) });
    }
}

// GuiIngame's chat: recent messages at the bottom left, fading after 10 seconds.
void Hud::DrawMessages(const Player& player, int s) const {
    const auto& messages = player.Messages();
    int height = GetScreenHeight();
    double now = GetTime();
    int line = 0;
    for (int i = (int)messages.size() - 1; i >= 0 && line < 10; i--) {
        double age = now - messages[i].time;
        if (age > 10.0) continue;
        float opacity = (float)std::clamp((10.0 - age) / 1.0, 0.0, 1.0);
        opacity *= opacity;
        int y = height - (48 + line * 9) * s;
        const char* text = messages[i].text.c_str();
        DrawRectangle(2 * s, y - s, 320 * s, 9 * s, Color{ 0, 0, 0, (unsigned char)(128 * opacity) });
        font.Draw(text, 2 * s, y, s, Fade(WHITE, opacity), true);
        line++;
    }
}
