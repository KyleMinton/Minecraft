#pragma once

#include "raylib.h"
#include "Player.h"
#include "DroppedItems.h"
#include "ItemIcons.h"
#include "GuiFont.h"
#include "PlayerModel.h"

#include <vector>

// Every item screen, with modern (1.21) looks: the E-key inventory (the 2x2
// crafting grid and you in the middle in survival; the tabbed creative
// inventory, with search and a survival-inventory tab, in creative), and the
// screens blocks open: the crafting table's 3x3 grid, chests (27 slots, or 54
// for a large chest), furnaces with their flame and progress arrow, and
// dispensers. Clicking still works the way it did (Beta's creative clicks too).
//
// Clicking works like Minecraft (Container.slotClick): left click picks up or
// places a whole stack, right click half a stack or a single item, shift-click
// moves a stack across, double-click gathers matching items, 1-9 swaps with a
// hotbar slot, Q throws, and clicking outside the window drops what you carry.
// Construct after InitWindow() and destroy before CloseWindow().
class InventoryScreen {
public:
    InventoryScreen(const ItemIcons& icons, const GuiFont& font);
    ~InventoryScreen();

    InventoryScreen(const InventoryScreen&) = delete;
    InventoryScreen& operator=(const InventoryScreen&) = delete;

    bool IsOpen() const { return open; }
    // Typing goes to the creative search box (so E doesn't close the screen).
    bool CapturesKeyboard() const { return open && mode == Mode::Creative && creativeTab == SEARCH_TAB; }

    // The E-key screen for the player's game mode.
    void Open(const Player& player);
    // A block's screen (crafting table, chest, furnace, dispenser).
    void OpenContainer(ScreenKind kind, int x, int y, int z, World& world, const Player& player);

    // Anything left in a crafting grid or on the cursor is thrown out, as in Minecraft.
    void Close(Player& player, DroppedItems& drops);

    // Mouse and keyboard; call every frame while open. Closes itself if its block goes away.
    void Update(Player& player, DroppedItems& drops);

    // Renders the player model; uses a render texture, so call before BeginDrawing().
    void Prepare(const Player& player, float brightness);

    void Draw(const Player& player) const;

private:
    enum class Mode { Player, Creative, Crafting, Chest, Furnace, Dispenser };
    enum class SlotKind { Main, Armor, Offhand, Craft, CraftResult, Creative, Container, FurnaceResult, Destroy };
    struct Slot {
        SlotKind kind;
        int index; // into the array it belongs to (or the visible creative grid)
        int x, y;  // item position inside the window, in GUI pixels
    };
    struct Range { int start, end; };

    const ItemIcons& items;
    const GuiFont& font;
    PlayerModel playerModel;

    bool open = false;
    Mode mode = Mode::Player;
    std::vector<Slot> slots;         // in the container's own order, which shift-click ranges rely on
    std::vector<ItemStack> creativeItems;

    // CreativeModeInventoryScreen's tabs.
    struct CreativeTab {
        const char* name;
        int column;       // 0-5 from the left, 6 at the right edge
        bool top;
        ItemStack icon;
        std::vector<ItemStack> items;
    };
    static constexpr int SEARCH_TAB = 5, INVENTORY_TAB = 10;
    std::vector<CreativeTab> creativeTabs;
    int creativeTab = 0;
    std::vector<ItemStack> shownItems; // the open tab's items (search results on the search tab)
    std::string search;

    int gridSize = 2;
    ItemStack craftGrid[9];
    ItemStack craftResult;

    // A block's own storage.
    World* world = nullptr;
    std::vector<Vector3> tiles;      // tile entity positions: one, or two for a large chest
    int slotsPerTile = 0;
    int chestRows = 3;
    std::string title;

    // Frame state (GUI pixels), shared by Update and Draw.
    int guiScale = 1;
    int guiLeft = 0, guiTop = 0;
    int mouseX = 0, mouseY = 0;
    int hoveredSlot = -1;

    int lastClickSlot = -1;
    int lastClickButton = -1;
    double lastClickTime = -1.0;

    float scroll = 0.0f;
    bool scrolling = false;
    bool wasClicking = false;

    int Width() const;
    int Height() const;
    void BuildSlots();
    void AddPlayerSlots(int top, int hotbarY);
    void UpdateLayout();
    int SlotAt(int x, int y) const;
    TileEntity* Tile(int index) const;
    ItemStack* StackIn(const Slot& slot, Player& player);
    ItemStack StackShown(const Slot& slot, const Player& player) const;
    bool CanPlaceIn(const Slot& slot, const ItemStack& stack) const;
    int SlotLimit(const Slot& slot, const ItemStack& stack) const;
    int CreativeRows() const;
    int CreativeFirstRow() const;
    Range PlayerRange() const;       // where the player's own slots start and end
    bool ContainerValid() const;

    void Click(int slotIndex, int button, Player& player);
    void ShiftClick(int slotIndex, Player& player, DroppedItems& drops);
    void CollectAll(Player& player);
    void SwapWithHotbar(int slotIndex, int hotbarSlot, Player& player);
    void Throw(int slotIndex, bool wholeStack, Player& player, DroppedItems& drops);
    void CreativeClick(int slotIndex, int button, bool shift, Player& player);
    bool MergeInto(ItemStack& stack, int start, int end, bool reverse, Player& player);
    bool TakeResult(const Slot& slot, Player& player);
    void ConsumeIngredients();
    void UpdateCraftResult();

    void DrawBackground() const;
    Rectangle TabBounds(int tab) const; // GUI pixels
    void SelectTab(int tab);
    void UpdateSearch();
    void DrawTabs(bool selected) const;
    void DrawSlots(const Player& player) const;
    void DrawTooltip(const std::string& text) const;
};
