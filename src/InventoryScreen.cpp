#include "InventoryScreen.h"
#include "Crafting.h"
#include "Gui.h"
#include "Items.h"
#include "ModernBlocks.h"
#include "TileEntity.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr int SURVIVAL_WIDTH = 176, SURVIVAL_HEIGHT = 166;   // container/inventory.png
constexpr int CREATIVE_WIDTH = 195, CREATIVE_HEIGHT = 136;   // container/creative_inventory/tab_*.png
constexpr int CONTAINER_WIDTH = 176, CONTAINER_HEIGHT = 166; // crafting_table.png, furnace.png, dispenser.png
constexpr int ITEM_SIZE = 16;
constexpr int SLOT_PITCH = 18;

// Survival (ContainerPlayer), container slot numbers used by shift-clicking.
constexpr int RESULT_SLOT = 0;
constexpr int MAIN_SLOTS_START = 9, MAIN_SLOTS_END = 36;
constexpr int HOTBAR_SLOTS_START = 36, HOTBAR_SLOTS_END = 45;
constexpr int MODEL_X = 51, MODEL_Y = 75, MODEL_SIZE = 30;

// Creative (CreativeModeInventoryScreen): a 9x5 grid with a scrollbar, tabs above and below.
constexpr int CREATIVE_COLUMNS = 9, CREATIVE_VISIBLE_ROWS = 5;
constexpr int SCROLL_X = 175, SCROLL_TOP = 18, SCROLL_WIDTH = 12, SCROLL_HEIGHT = 112;
constexpr int SCROLL_THUMB_HEIGHT = 15;
constexpr int TAB_WIDTH = 26, TAB_HEIGHT = 32;

constexpr double DOUBLE_CLICK_SECONDS = 0.25;
const Color TITLE_COLOR = Argb(0xFF404040);

bool ShiftDown() { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }
bool CtrlDown() { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }

// Beta 1.8's creative list: its blocks, in ContainerCreative's order, then every item.
std::vector<ItemStack> CreativeList() {
    std::vector<ItemStack> list;
    auto add = [&](int id, int meta = 0) { list.emplace_back(id, 1, meta); };
    const int blocks[] = {
        BLOCK_COBBLESTONE, BLOCK_STONE, BLOCK_DIAMOND_ORE, BLOCK_GOLD_ORE, BLOCK_IRON_ORE, BLOCK_COAL_ORE, BLOCK_LAPIS_ORE,
        BLOCK_REDSTONE_ORE, BLOCK_CLAY, BLOCK_DIAMOND_BLOCK, BLOCK_GOLD_BLOCK, BLOCK_IRON_BLOCK, BLOCK_BEDROCK, BLOCK_LAPIS_BLOCK,
        BLOCK_BRICKS, BLOCK_MOSSY_COBBLESTONE,
    };
    for (int id : blocks) add(id);
    for (int m = 0; m < 3; m++) add(BLOCK_STONE_BRICKS, m);
    for (int m = 0; m < 6; m++) add(BLOCK_SLAB, m);
    for (int id : { BLOCK_OBSIDIAN, BLOCK_NETHERRACK, BLOCK_SOUL_SAND, BLOCK_GLOWSTONE }) add(id);
    for (int m = 0; m < 3; m++) add(BLOCK_LOG, m);
    for (int m = 0; m < 3; m++) add(BLOCK_LEAVES, m);
    for (int id : { BLOCK_DIRT, BLOCK_GRASS, BLOCK_SAND, BLOCK_SANDSTONE, BLOCK_GRAVEL, BLOCK_WEB, BLOCK_PLANKS }) add(id);
    for (int m = 0; m < 3; m++) add(BLOCK_SAPLING, m);
    add(BLOCK_TALL_GRASS, 1);
    add(BLOCK_TALL_GRASS, 2);
    for (int id : { BLOCK_DEAD_BUSH, BLOCK_SPONGE, BLOCK_ICE, BLOCK_SNOW, BLOCK_DANDELION, BLOCK_ROSE, BLOCK_BROWN_MUSHROOM,
                    BLOCK_RED_MUSHROOM, BLOCK_BROWN_MUSHROOM_BLOCK, BLOCK_RED_MUSHROOM_BLOCK, BLOCK_CACTUS, BLOCK_MELON, BLOCK_PUMPKIN,
                    BLOCK_JACK_O_LANTERN, BLOCK_VINE, BLOCK_IRON_BARS, BLOCK_GLASS_PANE, BLOCK_FENCE, BLOCK_FENCE_GATE, BLOCK_LADDER,
                    BLOCK_TORCH, BLOCK_OAK_STAIRS, BLOCK_COBBLESTONE_STAIRS, BLOCK_BRICK_STAIRS, BLOCK_STONE_BRICK_STAIRS, BLOCK_TRAPDOOR,
                    BLOCK_RAIL, BLOCK_POWERED_RAIL, BLOCK_DETECTOR_RAIL, BLOCK_REDSTONE_TORCH, BLOCK_STONE_BUTTON, BLOCK_LEVER,
                    BLOCK_STONE_PRESSURE_PLATE, BLOCK_WOODEN_PRESSURE_PLATE, BLOCK_PISTON, BLOCK_STICKY_PISTON, BLOCK_CRAFTING_TABLE,
                    BLOCK_FURNACE, BLOCK_CHEST, BLOCK_DISPENSER, BLOCK_JUKEBOX, BLOCK_NOTE_BLOCK, BLOCK_BOOKSHELF, BLOCK_TNT,
                    BLOCK_GLASS, BLOCK_SNOW_LAYER, BLOCK_MOB_SPAWNER, BLOCK_MONSTER_EGG }) {
        add(id);
    }
    for (int m = 0; m < 16; m++) add(BLOCK_WOOL, m);
    for (int id = 256; id <= ITEM_LAST_REGULAR; id++) {
        if (id == ITEM_DYE) {
            for (int m = 0; m < 16; m++) add(id, m);
        } else if (id == ITEM_COAL) {
            add(id, 0);
            add(id, 1);
        } else {
            add(id);
        }
    }
    add(ITEM_RECORD_13);
    add(ITEM_RECORD_CAT);
    return list;
}

// Which modern creative tab an item belongs in: 0 Building Blocks, 1 Colored
// Blocks, 2 Natural Blocks, 3 Functional Blocks, 4 Redstone Blocks, 6 Tools &
// Utilities, 7 Combat, 8 Food & Drinks, 9 Ingredients.
int CreativeCategory(const ItemStack& stack) {
    switch (stack.id) {
        case BLOCK_WOOL: return 1;
        case BLOCK_GRASS: case BLOCK_DIRT: case BLOCK_SAND: case BLOCK_GRAVEL: case BLOCK_CLAY: case BLOCK_COAL_ORE:
        case BLOCK_IRON_ORE: case BLOCK_GOLD_ORE: case BLOCK_DIAMOND_ORE: case BLOCK_LAPIS_ORE: case BLOCK_REDSTONE_ORE:
        case BLOCK_LOG: case BLOCK_LEAVES: case BLOCK_SAPLING: case BLOCK_TALL_GRASS: case BLOCK_DEAD_BUSH: case BLOCK_DANDELION:
        case BLOCK_ROSE: case BLOCK_BROWN_MUSHROOM: case BLOCK_RED_MUSHROOM: case BLOCK_BROWN_MUSHROOM_BLOCK:
        case BLOCK_RED_MUSHROOM_BLOCK: case BLOCK_CACTUS: case BLOCK_PUMPKIN: case BLOCK_MELON: case BLOCK_VINE: case BLOCK_ICE:
        case BLOCK_SNOW: case BLOCK_SNOW_LAYER: case BLOCK_NETHERRACK: case BLOCK_SOUL_SAND: case BLOCK_GLOWSTONE: case BLOCK_OBSIDIAN:
        case BLOCK_BEDROCK: case BLOCK_WEB: case BLOCK_MONSTER_EGG: case BLOCK_SPONGE: case ITEM_SEEDS: case ITEM_PUMPKIN_SEEDS:
        case ITEM_MELON_SEEDS: case ITEM_REEDS:
            return 2;
        case BLOCK_CRAFTING_TABLE: case BLOCK_FURNACE: case BLOCK_CHEST: case BLOCK_JUKEBOX: case BLOCK_BOOKSHELF: case BLOCK_LADDER:
        case BLOCK_TORCH: case BLOCK_JACK_O_LANTERN: case BLOCK_MOB_SPAWNER: case ITEM_BED: case ITEM_SIGN: case ITEM_PAINTING:
            return 3;
        case ITEM_REDSTONE: case BLOCK_REDSTONE_TORCH: case ITEM_REPEATER: case BLOCK_LEVER: case BLOCK_STONE_BUTTON:
        case BLOCK_STONE_PRESSURE_PLATE: case BLOCK_WOODEN_PRESSURE_PLATE: case BLOCK_PISTON: case BLOCK_STICKY_PISTON:
        case BLOCK_DISPENSER: case BLOCK_NOTE_BLOCK: case BLOCK_TNT: case BLOCK_RAIL: case BLOCK_POWERED_RAIL: case BLOCK_DETECTOR_RAIL:
        case ITEM_IRON_DOOR: case BLOCK_TRAPDOOR:
            return 4;
        case ITEM_FLINT_AND_STEEL: case ITEM_SHEARS: case ITEM_FISHING_ROD: case ITEM_BUCKET: case ITEM_WATER_BUCKET:
        case ITEM_LAVA_BUCKET: case ITEM_COMPASS: case ITEM_CLOCK: case ITEM_MAP: case ITEM_SADDLE: case ITEM_BOAT: case ITEM_MINECART:
        case ITEM_CHEST_MINECART: case ITEM_FURNACE_MINECART: case ITEM_RECORD_13: case ITEM_RECORD_CAT:
            return 6;
        case ITEM_BOW: case ITEM_ARROW: case ITEM_SNOWBALL: case ITEM_EGG:
            return 7;
        case ITEM_CAKE: case ITEM_MILK_BUCKET:
            return 8;
        default: break;
    }
    const ItemDef* def = GetItemDef(stack.id);
    if (def != nullptr && !IsBlockItem(stack.id)) {
        if (def->tool == ToolType::Sword || def->armorSlot >= 0) return 7;
        if (def->tool != ToolType::None) return 6;
        if (def->IsFood()) return 8;
        return 9;
    }
    return 0; // the rest are building blocks
}

} // namespace

InventoryScreen::InventoryScreen(const ItemIcons& icons, const GuiFont& guiFont) : items(icons), font(guiFont) {
    creativeItems = CreativeList();
    creativeTabs = {
        { "Building Blocks", 0, true, ItemStack(BLOCK_BRICKS, 1), {} },
        { "Colored Blocks", 1, true, ItemStack(BLOCK_WOOL, 1, 9), {} },
        { "Natural Blocks", 2, true, ItemStack(BLOCK_GRASS, 1), {} },
        { "Functional Blocks", 3, true, ItemStack(BLOCK_CRAFTING_TABLE, 1), {} },
        { "Redstone Blocks", 4, true, ItemStack(ITEM_REDSTONE, 1), {} },
        { "Search Items", 6, true, ItemStack(ITEM_COMPASS, 1), {} },
        { "Tools & Utilities", 0, false, ItemStack(ITEM_DIAMOND_PICKAXE, 1), {} },
        { "Combat", 1, false, ItemStack(ITEM_GOLDEN_SWORD, 1), {} },
        { "Food & Drinks", 2, false, ItemStack(ITEM_GOLDEN_APPLE, 1), {} },
        { "Ingredients", 3, false, ItemStack(ITEM_IRON_INGOT, 1), {} },
        { "Survival Inventory", 6, false, ItemStack(BLOCK_CHEST, 1), {} },
    };
    // Building Blocks and Colored Blocks: 1.21's lists, in its order. The other tabs
    // sort Beta's items; anything of Beta's the 1.21 lists leave out (its own
    // slab kinds...) is only in the search.
    auto fromNames = [](const char* const* names, size_t count) {
        std::vector<ItemStack> list;
        for (size_t i = 0; i < count; i++) {
            int id, meta;
            if (LookupBlockName(names[i], id, meta)) list.emplace_back(id, 1, meta);
        }
        return list;
    };
    creativeTabs[0].items = fromNames(BUILDING_TAB, sizeof(BUILDING_TAB) / sizeof(BUILDING_TAB[0]));
    creativeTabs[1].items = fromNames(COLORED_TAB, sizeof(COLORED_TAB) / sizeof(COLORED_TAB[0]));
    for (const ItemStack& stack : creativeItems) {
        int tab = CreativeCategory(stack);
        if (tab != 0 && tab != 1) creativeTabs[tab].items.push_back(stack);
    }
    // The search tab: every tab's items once each, then Beta's leftovers.
    std::vector<ItemStack> everything;
    auto addOnce = [&](const ItemStack& stack) {
        for (const ItemStack& s : everything) {
            if (s.id == stack.id && s.damage == stack.damage) return;
        }
        everything.push_back(stack);
    };
    for (size_t t = 0; t < creativeTabs.size(); t++) {
        if ((int)t == SEARCH_TAB) continue;
        for (const ItemStack& stack : creativeTabs[t].items) addOnce(stack);
    }
    for (const ItemStack& stack : creativeItems) addOnce(stack);
    creativeItems = everything;
    creativeTabs[SEARCH_TAB].items = creativeItems;
    shownItems = creativeTabs[0].items;
}

InventoryScreen::~InventoryScreen() {
}

int InventoryScreen::Width() const {
    if (mode == Mode::Player) return SURVIVAL_WIDTH;
    if (mode == Mode::Creative) return CREATIVE_WIDTH;
    return CONTAINER_WIDTH;
}

int InventoryScreen::Height() const {
    if (mode == Mode::Player) return SURVIVAL_HEIGHT;
    if (mode == Mode::Creative) return CREATIVE_HEIGHT;
    if (mode == Mode::Chest) return 114 + chestRows * SLOT_PITCH;
    return CONTAINER_HEIGHT;
}

// The three rows of the inventory, then the hotbar.
void InventoryScreen::AddPlayerSlots(int top, int hotbarY) {
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 9; col++) slots.push_back({ SlotKind::Main, 9 + row * 9 + col, 8 + col * SLOT_PITCH, top + row * SLOT_PITCH });
    for (int col = 0; col < 9; col++) slots.push_back({ SlotKind::Main, col, 8 + col * SLOT_PITCH, hotbarY });
}

void InventoryScreen::BuildSlots() {
    slots.clear();
    switch (mode) {
        case Mode::Player:
            slots.push_back({ SlotKind::CraftResult, 0, 154, 28 });
            for (int i = 0; i < 4; i++) slots.push_back({ SlotKind::Craft, i, 98 + (i % 2) * SLOT_PITCH, 18 + (i / 2) * SLOT_PITCH });
            for (int i = 0; i < 4; i++) slots.push_back({ SlotKind::Armor, i, 8, 8 + i * SLOT_PITCH });
            AddPlayerSlots(84, 142);
            slots.push_back({ SlotKind::Offhand, 0, 77, 62 });
            break;
        case Mode::Creative:
            if (creativeTab == INVENTORY_TAB) {
                // The survival inventory, rearranged to fit (CreativeModeInventoryScreen.selectTab).
                for (int i = 0; i < 4; i++) slots.push_back({ SlotKind::Armor, i, 54 + (i / 2) * 54, 6 + (i % 2) * 27 });
                slots.push_back({ SlotKind::Offhand, 0, 35, 20 });
                for (int row = 0; row < 3; row++)
                    for (int col = 0; col < 9; col++) slots.push_back({ SlotKind::Main, 9 + row * 9 + col, 9 + col * SLOT_PITCH, 54 + row * SLOT_PITCH });
                slots.push_back({ SlotKind::Destroy, 0, 173, 112 });
            } else {
                for (int row = 0; row < CREATIVE_VISIBLE_ROWS; row++)
                    for (int col = 0; col < CREATIVE_COLUMNS; col++)
                        slots.push_back({ SlotKind::Creative, row * CREATIVE_COLUMNS + col, 9 + col * SLOT_PITCH, 18 + row * SLOT_PITCH });
            }
            for (int col = 0; col < 9; col++) slots.push_back({ SlotKind::Main, col, 9 + col * SLOT_PITCH, 112 });
            break;
        case Mode::Crafting:
            slots.push_back({ SlotKind::CraftResult, 0, 124, 35 });
            for (int i = 0; i < 9; i++) slots.push_back({ SlotKind::Craft, i, 30 + (i % 3) * SLOT_PITCH, 17 + (i / 3) * SLOT_PITCH });
            AddPlayerSlots(84, 142);
            break;
        case Mode::Chest: {
            for (int row = 0; row < chestRows; row++)
                for (int col = 0; col < 9; col++) slots.push_back({ SlotKind::Container, row * 9 + col, 8 + col * SLOT_PITCH, 18 + row * SLOT_PITCH });
            int extra = (chestRows - 4) * SLOT_PITCH;
            AddPlayerSlots(103 + extra, 161 + extra);
            break;
        }
        case Mode::Furnace:
            slots.push_back({ SlotKind::Container, 0, 56, 17 });
            slots.push_back({ SlotKind::Container, 1, 56, 53 });
            slots.push_back({ SlotKind::FurnaceResult, 2, 116, 35 });
            AddPlayerSlots(84, 142);
            break;
        case Mode::Dispenser:
            for (int i = 0; i < 9; i++) slots.push_back({ SlotKind::Container, i, 62 + (i % 3) * SLOT_PITCH, 17 + (i / 3) * SLOT_PITCH });
            AddPlayerSlots(84, 142);
            break;
    }
}

void InventoryScreen::Open(const Player& player) {
    open = true;
    mode = player.GetGameMode() == GameMode::Creative ? Mode::Creative : Mode::Player;
    gridSize = 2;
    if (mode == Mode::Creative && creativeTab == SEARCH_TAB) {
        while (GetCharPressed() != 0) {} // the E that opened it isn't a search
    }
    world = nullptr;
    tiles.clear();
    BuildSlots();
    hoveredSlot = -1;
    lastClickSlot = -1;
    scrolling = false;
    wasClicking = true; // a click still held from before doesn't grab the scrollbar
    UpdateLayout();
}

void InventoryScreen::OpenContainer(ScreenKind kind, int x, int y, int z, World& w, const Player& player) {
    (void)player;
    open = true;
    world = &w;
    tiles.clear();
    gridSize = 3;
    chestRows = 3;
    switch (kind) {
        case ScreenKind::Crafting:
            mode = Mode::Crafting;
            title = "Crafting";
            break;
        case ScreenKind::Chest: {
            mode = Mode::Chest;
            slotsPerTile = 27;
            // InventoryLargeChest: the half at lower X/Z comes first.
            if (w.GetBlock(x - 1, y, z) == BLOCK_CHEST) tiles = { Vector3{ (float)x - 1, (float)y, (float)z }, Vector3{ (float)x, (float)y, (float)z } };
            else if (w.GetBlock(x + 1, y, z) == BLOCK_CHEST) tiles = { Vector3{ (float)x, (float)y, (float)z }, Vector3{ (float)x + 1, (float)y, (float)z } };
            else if (w.GetBlock(x, y, z - 1) == BLOCK_CHEST) tiles = { Vector3{ (float)x, (float)y, (float)z - 1 }, Vector3{ (float)x, (float)y, (float)z } };
            else if (w.GetBlock(x, y, z + 1) == BLOCK_CHEST) tiles = { Vector3{ (float)x, (float)y, (float)z }, Vector3{ (float)x, (float)y, (float)z + 1 } };
            else tiles = { Vector3{ (float)x, (float)y, (float)z } };
            chestRows = (int)tiles.size() * 3;
            title = tiles.size() > 1 ? "Large Chest" : "Chest";
            for (int i = 0; i < (int)tiles.size(); i++) {
                if (TileEntity* te = Tile(i); te != nullptr && te->kind == TileKind::Chest) static_cast<ChestTile*>(te)->viewers++;
            }
            break;
        }
        case ScreenKind::Furnace:
            mode = Mode::Furnace;
            slotsPerTile = 3;
            tiles = { Vector3{ (float)x, (float)y, (float)z } };
            title = "Furnace";
            break;
        case ScreenKind::Dispenser:
            mode = Mode::Dispenser;
            slotsPerTile = 9;
            tiles = { Vector3{ (float)x, (float)y, (float)z } };
            title = "Trap";
            break;
        default:
            open = false;
            return;
    }
    BuildSlots();
    hoveredSlot = -1;
    lastClickSlot = -1;
    UpdateLayout();
}

TileEntity* InventoryScreen::Tile(int index) const {
    if (world == nullptr || index < 0 || index >= (int)tiles.size()) return nullptr;
    const Vector3& p = tiles[index];
    return world->GetTileEntity((int)p.x, (int)p.y, (int)p.z);
}

bool InventoryScreen::ContainerValid() const {
    for (int i = 0; i < (int)tiles.size(); i++) {
        TileEntity* te = Tile(i);
        if (te == nullptr || te->SlotCount() < slotsPerTile) return false;
    }
    return true;
}

void InventoryScreen::Close(Player& player, DroppedItems& drops) {
    Inventory& inventory = player.GetInventory();
    for (ItemStack& stack : craftGrid) {
        player.DropItem(stack, drops);
        stack.Clear();
    }
    craftResult.Clear();
    player.DropItem(inventory.cursor, drops);
    inventory.cursor.Clear();
    if (mode == Mode::Chest) {
        for (int i = 0; i < (int)tiles.size(); i++) {
            if (TileEntity* te = Tile(i); te != nullptr && te->kind == TileKind::Chest) {
                ChestTile* chest = static_cast<ChestTile*>(te);
                chest->viewers = std::max(0, chest->viewers - 1);
            }
        }
    }
    tiles.clear();
    world = nullptr;
    open = false;
}

void InventoryScreen::UpdateLayout() {
    int width = GetScreenWidth(), height = GetScreenHeight();
    guiScale = GuiScale(width, height);
    int guiWidth = (width + guiScale - 1) / guiScale, guiHeight = (height + guiScale - 1) / guiScale;
    guiLeft = (guiWidth - Width()) / 2;
    guiTop = (guiHeight - Height()) / 2;
    Vector2 mouse = GetMousePosition();
    mouseX = (int)(mouse.x * guiWidth / width);
    mouseY = (int)(mouse.y * guiHeight / height);
}

int InventoryScreen::SlotAt(int x, int y) const {
    for (int i = 0; i < (int)slots.size(); i++) {
        int sx = guiLeft + slots[i].x, sy = guiTop + slots[i].y;
        if (x >= sx - 1 && x < sx + ITEM_SIZE + 1 && y >= sy - 1 && y < sy + ITEM_SIZE + 1) return i;
    }
    return -1;
}

ItemStack* InventoryScreen::StackIn(const Slot& slot, Player& player) {
    Inventory& inventory = player.GetInventory();
    switch (slot.kind) {
        case SlotKind::Main:        return &inventory.main[slot.index];
        case SlotKind::Armor:       return &inventory.armor[slot.index];
        case SlotKind::Offhand:     return &inventory.offhand;
        case SlotKind::Craft:       return &craftGrid[slot.index];
        case SlotKind::CraftResult: return &craftResult;
        case SlotKind::Container:
        case SlotKind::FurnaceResult: {
            TileEntity* te = Tile(slot.index / std::max(1, slotsPerTile));
            return te != nullptr ? te->Slot(slot.index % std::max(1, slotsPerTile)) : nullptr;
        }
        default: return nullptr; // creative slots aren't real storage
    }
}

ItemStack InventoryScreen::StackShown(const Slot& slot, const Player& player) const {
    if (slot.kind == SlotKind::Creative) {
        int index = CreativeFirstRow() * CREATIVE_COLUMNS + slot.index;
        if (index < (int)shownItems.size()) return shownItems[index];
        return ItemStack();
    }
    ItemStack* stack = const_cast<InventoryScreen*>(this)->StackIn(slot, const_cast<Player&>(player));
    return stack != nullptr ? *stack : ItemStack();
}

// Armor slots take only armor for that slot, one at a time; results are take-only.
bool InventoryScreen::CanPlaceIn(const Slot& slot, const ItemStack& stack) const {
    switch (slot.kind) {
        case SlotKind::CraftResult: case SlotKind::FurnaceResult: case SlotKind::Creative: case SlotKind::Destroy: return false;
        case SlotKind::Armor: {
            const ItemDef* def = stack.IsEmpty() ? nullptr : GetItemDef(stack.id);
            if (stack.IsEmpty()) return true;
            if (slot.index == 0 && (stack.id == BLOCK_PUMPKIN)) return true; // Beta lets you wear a pumpkin
            return def != nullptr && def->armorSlot == slot.index;
        }
        default: return true;
    }
}

int InventoryScreen::SlotLimit(const Slot& slot, const ItemStack& stack) const {
    if (slot.kind == SlotKind::Armor) return 1;
    return MaxStackSize(stack);
}

int InventoryScreen::CreativeRows() const {
    return ((int)shownItems.size() + CREATIVE_COLUMNS - 1) / CREATIVE_COLUMNS;
}

int InventoryScreen::CreativeFirstRow() const {
    int hiddenRows = CreativeRows() - CREATIVE_VISIBLE_ROWS;
    if (hiddenRows <= 0) return 0;
    return (int)(scroll * hiddenRows + 0.5f);
}

InventoryScreen::Range InventoryScreen::PlayerRange() const {
    switch (mode) {
        case Mode::Player: return { MAIN_SLOTS_START, HOTBAR_SLOTS_END };
        case Mode::Crafting: return { 10, 46 };
        case Mode::Chest: return { chestRows * 9, chestRows * 9 + 36 };
        case Mode::Furnace: return { 3, 39 };
        case Mode::Dispenser: return { 9, 45 };
        default: return { 0, 0 };
    }
}

void InventoryScreen::Update(Player& player, DroppedItems& drops) {
    if (!tiles.empty() && !ContainerValid()) {
        Close(player, drops); // the block was broken
        return;
    }
    UpdateLayout();
    hoveredSlot = SlotAt(mouseX, mouseY);
    Inventory& inventory = player.GetInventory();

    if (mode == Mode::Creative) {
        // Tabs: click to switch.
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int t = 0; t < (int)creativeTabs.size(); t++) {
                if (CheckCollisionPointRec(Vector2{ (float)mouseX, (float)mouseY }, TabBounds(t))) {
                    SelectTab(t);
                    return;
                }
            }
        }
        // The search box.
        if (creativeTab == SEARCH_TAB) {
            bool changed = false;
            for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
                if (c >= 32 && c < 127 && search.size() < 50) {
                    search += (char)c;
                    changed = true;
                }
            }
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !search.empty()) {
                search.pop_back();
                changed = true;
            }
            if (changed) UpdateSearch();
        }
        int hiddenRows = CreativeRows() - CREATIVE_VISIBLE_ROWS;
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && hiddenRows > 0) scroll -= (wheel > 0.0f ? 1.0f : -1.0f) / hiddenRows;
        bool mouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        int trackLeft = guiLeft + SCROLL_X, trackTop = guiTop + SCROLL_TOP;
        if (!wasClicking && mouseDown && hiddenRows > 0 && creativeTab != INVENTORY_TAB && mouseX >= trackLeft && mouseY >= trackTop &&
            mouseX < trackLeft + SCROLL_WIDTH + 2 && mouseY < trackTop + SCROLL_HEIGHT) {
            scrolling = true;
        }
        if (!mouseDown) scrolling = false;
        wasClicking = mouseDown;
        if (scrolling) scroll = (mouseY - trackTop - 7.5f) / (SCROLL_HEIGHT - SCROLL_THUMB_HEIGHT);
        scroll = std::clamp(scroll, 0.0f, 1.0f);
    }

    if (hoveredSlot >= 0 && inventory.cursor.IsEmpty()) {
        if (mode != Mode::Creative) {
            for (int i = 0; i < Inventory::HOTBAR_SIZE; i++) {
                if (IsKeyPressed(KEY_ONE + i)) SwapWithHotbar(hoveredSlot, i, player);
            }
        }
        if (IsKeyPressed(KEY_Q) && !CapturesKeyboard()) Throw(hoveredSlot, CtrlDown(), player, drops);
    }

    for (int button : { 0, 1 }) {
        if (!IsMouseButtonPressed(button == 0 ? MOUSE_BUTTON_LEFT : MOUSE_BUTTON_RIGHT)) continue;
        if (scrolling) continue;
        bool outside = mouseX < guiLeft || mouseY < guiTop || mouseX >= guiLeft + Width() || mouseY >= guiTop + Height();
        if (hoveredSlot < 0) {
            if (outside && !inventory.cursor.IsEmpty()) {
                player.DropItem(inventory.cursor.Split(button == 0 ? inventory.cursor.count : 1), drops);
            }
            continue;
        }
        double now = GetTime();
        bool doubleClick = hoveredSlot == lastClickSlot && button == lastClickButton && button == 0 && now - lastClickTime < DOUBLE_CLICK_SECONDS;
        lastClickSlot = hoveredSlot;
        lastClickButton = button;
        lastClickTime = doubleClick ? -1.0 : now;

        const Slot& slot = slots[hoveredSlot];
        if (slot.kind == SlotKind::Destroy) {
            inventory.cursor.Clear(); // the creative inventory's bin
            continue;
        }
        if (slot.kind == SlotKind::Creative) {
            CreativeClick(hoveredSlot, button, ShiftDown(), player);
        } else if (ShiftDown()) {
            if (mode != Mode::Creative) ShiftClick(hoveredSlot, player, drops);
        } else if (doubleClick && mode != Mode::Creative) {
            if (inventory.cursor.IsEmpty()) Click(hoveredSlot, button, player);
            CollectAll(player);
        } else {
            Click(hoveredSlot, button, player);
        }
    }
}

// Container.slotClick, PICKUP.
void InventoryScreen::Click(int slotIndex, int button, Player& player) {
    const Slot& slot = slots[slotIndex];
    if (slot.kind == SlotKind::CraftResult || slot.kind == SlotKind::FurnaceResult) {
        TakeResult(slot, player);
        return;
    }
    ItemStack* stack = StackIn(slot, player);
    if (stack == nullptr) return;
    ItemStack& cursor = player.GetInventory().cursor;

    if (stack->IsEmpty()) {
        if (!cursor.IsEmpty() && CanPlaceIn(slot, cursor)) {
            int amount = std::min(button == 0 ? cursor.count : 1, SlotLimit(slot, cursor));
            *stack = cursor.Split(amount);
        }
    } else if (cursor.IsEmpty()) {
        cursor = stack->Split(button == 0 ? stack->count : (stack->count + 1) / 2);
    } else if (CanPlaceIn(slot, cursor)) {
        if (SameItem(*stack, cursor)) {
            int moved = std::min(button == 0 ? cursor.count : 1, SlotLimit(slot, cursor) - stack->count);
            if (moved > 0) {
                stack->count += moved;
                cursor.Split(moved);
            }
        } else if (cursor.count <= SlotLimit(slot, cursor)) {
            std::swap(*stack, cursor);
        }
    } else if (SameItem(*stack, cursor) && cursor.count + stack->count <= MaxStackSize(cursor)) {
        cursor.count += stack->Split(stack->count).count;
    }
    if (slot.kind == SlotKind::Craft) UpdateCraftResult();
}

// Each container's transferStackInSlot.
void InventoryScreen::ShiftClick(int slotIndex, Player& player, DroppedItems& drops) {
    const Slot& slot = slots[slotIndex];
    Range p = PlayerRange();
    int rowsEnd = p.start + 27;

    if (slot.kind == SlotKind::CraftResult) {
        while (!craftResult.IsEmpty()) {
            ItemStack made = craftResult;
            if (!MergeInto(made, p.start, p.end, true, player)) break;
            ConsumeIngredients();
            if (!made.IsEmpty()) {
                player.DropItem(made, drops); // crafted, but only part of it fit
                break;
            }
        }
        return;
    }
    ItemStack* stack = StackIn(slot, player);
    if (stack == nullptr || stack->IsEmpty()) return;

    if (slotIndex >= p.start && slotIndex < p.end) {
        bool moved = false;
        if (mode == Mode::Chest) moved = MergeInto(*stack, 0, chestRows * 9, false, player);
        else if (mode == Mode::Dispenser) moved = MergeInto(*stack, 0, 9, false, player);
        else if (mode == Mode::Player) {
            const ItemDef* def = GetItemDef(stack->id);
            if (def != nullptr && def->armorSlot >= 0) {
                ItemStack* armor = &player.GetInventory().armor[def->armorSlot];
                if (armor->IsEmpty()) { *armor = stack->Split(1); moved = true; }
            }
        }
        if (!moved) {
            if (slotIndex < rowsEnd) MergeInto(*stack, rowsEnd, p.end, false, player);
            else MergeInto(*stack, p.start, rowsEnd, false, player);
        }
    } else if (mode == Mode::Chest || mode == Mode::Furnace || mode == Mode::Dispenser) {
        MergeInto(*stack, p.start, p.end, mode == Mode::Chest || slot.kind == SlotKind::FurnaceResult, player);
    } else {
        MergeInto(*stack, p.start, p.end, false, player); // crafting grid, armor, off hand
    }
    if (stack->count <= 0) stack->Clear();
    if (slot.kind == SlotKind::Craft) UpdateCraftResult();
}

// Container.mergeItemStack: top up matching stacks first, then the first empty slot.
bool InventoryScreen::MergeInto(ItemStack& stack, int start, int end, bool reverse, Player& player) {
    bool moved = false;
    for (int pass = 0; pass < 2 && !stack.IsEmpty(); pass++) {
        for (int n = 0; n < end - start && !stack.IsEmpty(); n++) {
            int i = reverse ? end - 1 - n : start + n;
            const Slot& slot = slots[i];
            if (!CanPlaceIn(slot, stack)) continue;
            ItemStack* target = StackIn(slot, player);
            if (target == nullptr) continue;
            int limit = SlotLimit(slot, stack);
            if (pass == 0) {
                if (!SameItem(*target, stack) || target->count >= limit) continue;
                int amount = std::min(stack.count, limit - target->count);
                target->count += amount;
                stack.Split(amount);
                moved = true;
            } else if (target->IsEmpty()) {
                *target = stack.Split(std::min(stack.count, limit));
                moved = true;
                if (stack.IsEmpty()) break;
            }
        }
    }
    return moved;
}

void InventoryScreen::CollectAll(Player& player) {
    ItemStack& cursor = player.GetInventory().cursor;
    if (cursor.IsEmpty()) return;
    int limit = MaxStackSize(cursor);
    for (int pass = 0; pass < 2; pass++) {
        for (const Slot& slot : slots) {
            if (cursor.count >= limit) break;
            if (slot.kind == SlotKind::CraftResult || slot.kind == SlotKind::FurnaceResult || slot.kind == SlotKind::Creative) continue;
            ItemStack* stack = StackIn(slot, player);
            if (stack == nullptr || !SameItem(*stack, cursor)) continue;
            if (pass == 0 && stack->count >= limit) continue;
            cursor.count += stack->Split(std::min(limit - cursor.count, stack->count)).count;
        }
    }
    UpdateCraftResult();
}

void InventoryScreen::SwapWithHotbar(int slotIndex, int hotbarSlot, Player& player) {
    const Slot& slot = slots[slotIndex];
    ItemStack& hotbar = player.GetInventory().main[hotbarSlot];
    if (slot.kind == SlotKind::CraftResult || slot.kind == SlotKind::FurnaceResult) {
        ItemStack* result = StackIn(slot, player);
        if (hotbar.IsEmpty() && result != nullptr && !result->IsEmpty()) {
            hotbar = *result;
            if (slot.kind == SlotKind::CraftResult) ConsumeIngredients();
            else result->Clear();
        }
        return;
    }
    ItemStack* stack = StackIn(slot, player);
    if (stack == nullptr || stack == &hotbar) return;
    if (hotbar.IsEmpty()) {
        hotbar = *stack;
        stack->Clear();
    } else if (CanPlaceIn(slot, hotbar) && hotbar.count <= SlotLimit(slot, hotbar)) {
        std::swap(*stack, hotbar);
    }
    if (slot.kind == SlotKind::Craft) UpdateCraftResult();
}

void InventoryScreen::Throw(int slotIndex, bool wholeStack, Player& player, DroppedItems& drops) {
    const Slot& slot = slots[slotIndex];
    if (slot.kind == SlotKind::CraftResult || slot.kind == SlotKind::Creative) return;
    ItemStack* stack = StackIn(slot, player);
    if (stack == nullptr || stack->IsEmpty()) return;
    player.DropItem(stack->Split(wholeStack ? stack->count : 1), drops);
    if (slot.kind == SlotKind::Craft) UpdateCraftResult();
}

// Beta's creative grid: clicking gives you one of the item (shift: a full stack);
// clicking it again adds one (right click takes one away); clicking with anything
// else in hand deletes what you carry.
void InventoryScreen::CreativeClick(int slotIndex, int button, bool shift, Player& player) {
    ItemStack& held = player.GetInventory().cursor;
    ItemStack offered = StackShown(slots[slotIndex], player);
    int limit = offered.IsEmpty() ? 64 : MaxStackSize(offered);
    if (!held.IsEmpty() && !offered.IsEmpty() && SameItem(held, offered)) {
        if (button == 0) {
            if (shift) held.count = limit;
            else if (held.count < limit) held.count++;
        } else {
            held.Split(1);
        }
    } else if (!held.IsEmpty() || offered.IsEmpty()) {
        held.Clear();
    } else {
        held = offered;
        if (shift) held.count = limit;
    }
}

// Taking a crafting or furnace result onto the cursor, if it fits there.
bool InventoryScreen::TakeResult(const Slot& slot, Player& player) {
    ItemStack& cursor = player.GetInventory().cursor;
    ItemStack* result = StackIn(slot, player);
    if (result == nullptr || result->IsEmpty()) return false;
    if (cursor.IsEmpty()) {
        cursor = *result;
    } else if (SameItem(cursor, *result) && cursor.count + result->count <= MaxStackSize(cursor)) {
        cursor.count += result->count;
    } else {
        return false;
    }
    if (slot.kind == SlotKind::CraftResult) ConsumeIngredients();
    else result->Clear();
    return true;
}

// SlotCrafting.onPickupFromSlot: one of each ingredient, leaving buckets behind.
void InventoryScreen::ConsumeIngredients() {
    for (int i = 0; i < gridSize * gridSize; i++) {
        ItemStack& stack = craftGrid[i];
        if (stack.IsEmpty()) continue;
        int container = CraftingContainerItem(stack.id);
        stack.Split(1);
        if (container != 0) stack = ItemStack(container, 1);
    }
    UpdateCraftResult();
}

void InventoryScreen::UpdateCraftResult() {
    craftResult = CraftingResult(craftGrid, gridSize);
}

// Where a creative tab sits: along the top or bottom edge, 27 pixels apart, the
// last column pushed to the right edge.
Rectangle InventoryScreen::TabBounds(int tab) const {
    const CreativeTab& t = creativeTabs[tab];
    int x = guiLeft + 27 * t.column;
    if (t.column == 6) x = guiLeft + CREATIVE_WIDTH - 27 + 1;
    int y = t.top ? guiTop - 28 : guiTop + CREATIVE_HEIGHT - 4;
    return Rectangle{ (float)x, (float)y, (float)TAB_WIDTH, (float)TAB_HEIGHT };
}

void InventoryScreen::SelectTab(int tab) {
    if (tab == creativeTab) return;
    creativeTab = tab;
    scroll = 0.0f;
    if (tab == SEARCH_TAB) {
        while (GetCharPressed() != 0) {}
        UpdateSearch();
    } else {
        shownItems = creativeTabs[tab].items;
    }
    BuildSlots();
    hoveredSlot = -1;
}

// Items whose names contain the search text, ignoring case.
void InventoryScreen::UpdateSearch() {
    auto lower = [](std::string text) {
        for (char& c : text) c = (char)tolower((unsigned char)c);
        return text;
    };
    std::string needle = lower(search);
    shownItems.clear();
    for (const ItemStack& stack : creativeItems) {
        if (needle.empty() || lower(ItemName(stack)).find(needle) != std::string::npos) shownItems.push_back(stack);
    }
    scroll = 0.0f;
}

void InventoryScreen::Prepare(const Player& player, float brightness) {
    if (!open) return;
    UpdateLayout();
    PlayerStats stats = player.GetStats();
    if (mode == Mode::Player) {
        playerModel.Render(guiScale, (float)(guiLeft + MODEL_X), (float)(guiTop + MODEL_Y), (float)MODEL_SIZE, (float)mouseX, (float)mouseY,
                           (float)stats.ticks + player.GetTickAlpha(), brightness);
    } else if (mode == Mode::Creative && creativeTab == INVENTORY_TAB) {
        playerModel.Render(guiScale, (float)(guiLeft + 88), (float)(guiTop + 45), 20.0f, (float)mouseX, (float)mouseY,
                           (float)stats.ticks + player.GetTickAlpha(), brightness);
    }
}

void InventoryScreen::DrawTabs(bool selected) const {
    int s = guiScale;
    for (int t = 0; t < (int)creativeTabs.size(); t++) {
        if ((t == creativeTab) != selected) continue;
        const CreativeTab& tab = creativeTabs[t];
        Rectangle r = TabBounds(t);
        std::string sprite = std::string("container/creative_inventory/tab_") + (tab.top ? "top_" : "bottom_") +
                             (selected ? "selected_" : "unselected_") + std::to_string(tab.column + 1);
        DrawGuiSprite(sprite, r.x, r.y, s);
        int iconX = (int)r.x + 5, iconY = (int)r.y + 8 + (tab.top ? 1 : -1);
        items.DrawIcon(tab.icon, Rectangle{ (float)(iconX * s), (float)(iconY * s), (float)(ITEM_SIZE * s), (float)(ITEM_SIZE * s) });
    }
}

void InventoryScreen::DrawBackground() const {
    int s = guiScale;
    auto blit = [&](const char* name, int u, int v, int x, int y, int w, int h) {
        Texture2D texture = GuiModernTexture(name);
        DrawGuiTexture(texture, Rectangle{ (float)u, (float)v, (float)w, (float)h }, (float)(guiLeft + x), (float)(guiTop + y), (float)w, (float)h, s);
    };
    switch (mode) {
        case Mode::Player:
            blit("container/inventory", 0, 0, 0, 0, SURVIVAL_WIDTH, SURVIVAL_HEIGHT);
            playerModel.Draw();
            break;
        case Mode::Creative: {
            DrawTabs(false);
            const char* background = creativeTab == SEARCH_TAB ? "container/creative_inventory/tab_item_search"
                                   : creativeTab == INVENTORY_TAB ? "container/creative_inventory/tab_inventory"
                                   : "container/creative_inventory/tab_items";
            blit(background, 0, 0, 0, 0, CREATIVE_WIDTH, CREATIVE_HEIGHT);
            if (creativeTab == INVENTORY_TAB) {
                playerModel.Draw();
            } else {
                bool canScroll = CreativeRows() > CREATIVE_VISIBLE_ROWS;
                int thumbY = SCROLL_TOP + (int)((SCROLL_HEIGHT - SCROLL_THUMB_HEIGHT - 2) * scroll);
                DrawGuiSprite(canScroll ? "container/creative_inventory/scroller" : "container/creative_inventory/scroller_disabled",
                              (float)(guiLeft + SCROLL_X), (float)(guiTop + thumbY), s);
            }
            DrawTabs(true);
            break;
        }
        case Mode::Crafting:
            blit("container/crafting_table", 0, 0, 0, 0, CONTAINER_WIDTH, CONTAINER_HEIGHT);
            break;
        case Mode::Chest:
            // The top of generic_54.png cut to the chest's rows, then the inventory part.
            blit("container/generic_54", 0, 0, 0, 0, CONTAINER_WIDTH, chestRows * SLOT_PITCH + 17);
            blit("container/generic_54", 0, 126, 0, chestRows * SLOT_PITCH + 17, CONTAINER_WIDTH, 96);
            break;
        case Mode::Furnace: {
            blit("container/furnace", 0, 0, 0, 0, CONTAINER_WIDTH, CONTAINER_HEIGHT);
            TileEntity* te = Tile(0);
            if (te != nullptr && te->kind == TileKind::Furnace) {
                const FurnaceTile* furnace = static_cast<const FurnaceTile*>(te);
                if (furnace->burnTime > 0) {
                    int total = furnace->currentItemBurnTime > 0 ? furnace->currentItemBurnTime : 200;
                    int flame = (int)ceilf(furnace->burnTime / (float)total * 13.0f) + 1;
                    Texture2D lit = GuiSprite("container/furnace/lit_progress");
                    DrawGuiTexture(lit, Rectangle{ 0, (float)(14 - flame), 14, (float)flame }, (float)(guiLeft + 56), (float)(guiTop + 36 + 14 - flame),
                                   14, (float)flame, s);
                }
                int arrow = (int)ceilf(furnace->cookTime / 200.0f * 24.0f);
                if (arrow > 0) {
                    Texture2D burn = GuiSprite("container/furnace/burn_progress");
                    DrawGuiTexture(burn, Rectangle{ 0, 0, (float)arrow, 16 }, (float)(guiLeft + 79), (float)(guiTop + 34), (float)arrow, 16, s);
                }
            }
            break;
        }
        case Mode::Dispenser:
            blit("container/dispenser", 0, 0, 0, 0, CONTAINER_WIDTH, CONTAINER_HEIGHT);
            break;
    }
}

void InventoryScreen::Draw(const Player& player) const {
    if (!open) return;
    int s = guiScale;
    int width = GetScreenWidth(), height = GetScreenHeight();
    int left = guiLeft * s, top = guiTop * s;

    // The world darkens behind any screen (GuiScreen.drawDefaultBackground).
    DrawRectangleGradientV(0, 0, width, height, Argb(0xC0101010), Argb(0xD0101010));
    DrawBackground();
    DrawSlots(player);

    switch (mode) {
        case Mode::Creative:
            if (creativeTab == SEARCH_TAB) {
                // The search box's text (an EditBox without a border), with a blinking cursor.
                std::string text = search;
                if ((int)(GetTime() * 20.0) / 6 % 2 == 0) text += "_";
                font.Draw(text.c_str(), left + 82 * s, top + 6 * s, s, WHITE, true);
            } else if (creativeTab != INVENTORY_TAB) {
                font.Draw(creativeTabs[creativeTab].name, left + 8 * s, top + 6 * s, s, TITLE_COLOR);
            }
            break;
        case Mode::Player: font.Draw("Crafting", left + 97 * s, top + 8 * s, s, TITLE_COLOR); break;
        case Mode::Crafting:
            font.Draw("Crafting", left + 29 * s, top + 6 * s, s, TITLE_COLOR);
            font.Draw("Inventory", left + 8 * s, top + (Height() - 96 + 2) * s, s, TITLE_COLOR);
            break;
        case Mode::Furnace:
            font.Draw("Furnace", left + (CONTAINER_WIDTH - font.Width("Furnace")) / 2 * s, top + 6 * s, s, TITLE_COLOR);
            font.Draw("Inventory", left + 8 * s, top + (Height() - 96 + 2) * s, s, TITLE_COLOR);
            break;
        case Mode::Dispenser:
            font.Draw("Dispenser", left + 8 * s + (CONTAINER_WIDTH - 16 - font.Width("Dispenser")) / 2 * s, top + 6 * s, s, TITLE_COLOR);
            font.Draw("Inventory", left + 8 * s, top + (Height() - 96 + 2) * s, s, TITLE_COLOR);
            break;
        case Mode::Chest:
            font.Draw(title.c_str(), left + 8 * s, top + 6 * s, s, TITLE_COLOR);
            font.Draw("Inventory", left + 8 * s, top + (Height() - 96 + 2) * s, s, TITLE_COLOR);
            break;
    }

    const ItemStack& cursor = player.GetInventory().cursor;
    items.DrawStack(cursor, (mouseX - 8) * s, (mouseY - 8) * s, s, font);

    if (cursor.IsEmpty() && hoveredSlot >= 0) {
        ItemStack hovered = StackShown(slots[hoveredSlot], player);
        if (!hovered.IsEmpty()) DrawTooltip(ItemName(hovered));
    }
    if (hoveredSlot >= 0 && slots[hoveredSlot].kind == SlotKind::Destroy) DrawTooltip("Destroy Item");
    if (mode == Mode::Creative) {
        for (int t = 0; t < (int)creativeTabs.size(); t++) {
            if (CheckCollisionPointRec(Vector2{ (float)mouseX, (float)mouseY }, TabBounds(t))) DrawTooltip(creativeTabs[t].name);
        }
    }
}

void InventoryScreen::DrawSlots(const Player& player) const {
    int s = guiScale;
    static const char* EMPTY_SLOTS[5] = { "container/slot/helmet", "container/slot/chestplate", "container/slot/leggings",
                                          "container/slot/boots", "container/slot/shield" };
    for (int i = 0; i < (int)slots.size(); i++) {
        const Slot& slot = slots[i];
        int gx = guiLeft + slot.x, gy = guiTop + slot.y;
        int x = gx * s, y = gy * s;
        ItemStack stack = StackShown(slot, player);
        // The modern slot highlight: a glow behind the item and a frame in front of it.
        if (i == hoveredSlot) DrawGuiSprite("container/slot_highlight_back", (float)(gx - 4), (float)(gy - 4), s);
        if (!stack.IsEmpty()) {
            items.DrawStack(stack, x, y, s, font);
        } else if (slot.kind == SlotKind::Armor || slot.kind == SlotKind::Offhand) {
            DrawGuiSprite(EMPTY_SLOTS[slot.kind == SlotKind::Armor ? slot.index : 4], (float)gx, (float)gy, s);
        }
        if (i == hoveredSlot) DrawGuiSprite("container/slot_highlight_front", (float)(gx - 4), (float)(gy - 4), s);
    }
}

void InventoryScreen::DrawTooltip(const std::string& label) const {
    DrawGuiTooltip(font, label, mouseX, mouseY, guiScale);
}
