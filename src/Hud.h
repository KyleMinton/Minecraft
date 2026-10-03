#pragma once

#include "raylib.h"
#include "Player.h"
#include "ItemIcons.h"
#include "GuiFont.h"

#include <string>

// Minecraft-style HUD: the hotbar with the items in it, the selection frame,
// the selected item's name (shown briefly when it changes), a crosshair that
// inverts whatever is behind it, and in survival the hearts, hunger and XP bar
// (GuiIngame), armor and air, chat messages, and the fire, portal, pumpkin
// and sleep overlays. Everything is sized with Minecraft's automatic GUI scale.
// Construct after InitWindow() and destroy before CloseWindow().
class Hud {
public:
    Hud(const ItemIcons& icons, const GuiFont& font, Texture2D atlas);
    ~Hud();

    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    void Draw(const Player& player);

private:
    const ItemIcons& items;
    const GuiFont& font;

    Texture2D crosshairTexture;
    Texture2D atlas;        // terrain.png, for the fire and portal overlays
    Texture2D pumpkinBlur;

    // The fading text above the hotbar: the selected item's name, or the game mode.
    ItemStack lastItem;
    bool itemKnown = false;
    GameMode lastMode = GameMode::Survival;
    bool modeKnown = false;
    std::string popupText;
    double popupShownAt = -100.0;

    // Heart flashing after health changes (GuiIngame.playerHealth / lastPlayerHealth).
    int displayedHealth = 0;
    int previousHealth = 0;
    long long healthFlashUntil = 0;
    double lastHealthChange = 0.0;

    void DrawSurvivalStats(const Player& player, int left, int scale);
    void DrawOverlays(const Player& player, int scale) const;
    void DrawMessages(const Player& player, int scale) const;
};
