#pragma once

#include "raylib.h"
#include "GuiFont.h"
#include "ItemStack.h"
#include "Models.h"

#include <map>
#include <set>

// Draws items in GUIs (the hotbar, inventory slots, the stack on the cursor):
// blocks as Beta's little 3D icons, rendered once per block at the current GUI
// scale, everything else as its 16x16 icon; with the stack size and a
// durability bar for worn tools.
// Construct after InitWindow() and destroy before CloseWindow().
class ItemIcons {
public:
    explicit ItemIcons(const ItemModels& models);
    ~ItemIcons();

    ItemIcons(const ItemIcons&) = delete;
    ItemIcons& operator=(const ItemIcons&) = delete;

    // Renders any 3D icons asked for since last time, and re-renders them all if
    // the GUI scale changed. Uses render textures, so call it before BeginDrawing().
    void Prepare();

    // The 16x16 icon stretched over `dest` (screen pixels).
    void DrawIcon(const ItemStack& stack, Rectangle dest) const;

    // The item with its top-left at screen pixel (x, y), with its count when more
    // than one and its wear (RenderItem.renderItemIntoGUI + renderItemOverlayIntoGUI).
    void DrawStack(const ItemStack& stack, int x, int y, int scale, const GuiFont& font) const;

    // Just the overlays, for when the icon was drawn separately.
    void DrawCount(const ItemStack& stack, int x, int y, int scale, const GuiFont& font) const;

private:
    const ItemModels& models;
    Shader shader;
    int iconScale = 0;
    mutable std::map<int, RenderTexture2D> icons; // id | meta << 8
    mutable std::set<int> pending;
};
