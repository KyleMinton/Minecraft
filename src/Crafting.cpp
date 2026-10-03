#include "Crafting.h"
#include "Block.h"
#include "Items.h"

#include <string>
#include <vector>

namespace {

constexpr int ANY = -1; // matches any damage value (a Block given to CraftingManager)

struct Ingredient {
    int id = 0;
    int damage = ANY;
    bool Matches(const ItemStack& stack) const {
        if (id == 0) return stack.IsEmpty();
        return !stack.IsEmpty() && stack.id == id && (damage == ANY || damage == stack.damage);
    }
};

// Blocks match any variant; items match damage 0 unless given one (as in Beta).
Ingredient B(int block) { return Ingredient{ block, ANY }; }
Ingredient I(int item, int damage = 0) { return Ingredient{ item, damage }; }

struct Key {
    char symbol;
    Ingredient ingredient;
};

struct Recipe {
    int width = 0, height = 0;
    std::vector<Ingredient> cells; // shaped: width * height, id 0 for empty
    std::vector<Ingredient> parts; // shapeless
    bool shapeless = false;
    ItemStack result;
};

class RecipeBook {
public:
    std::vector<Recipe> recipes;

    void Shaped(ItemStack result, std::vector<std::string> rows, std::vector<Key> keys) {
        Recipe r;
        r.result = result;
        r.height = (int)rows.size();
        r.width = (int)rows[0].size();
        for (const std::string& row : rows) {
            for (char c : row) {
                Ingredient ing;
                for (const Key& k : keys) {
                    if (k.symbol == c) ing = k.ingredient;
                }
                r.cells.push_back(ing);
            }
        }
        recipes.push_back(r);
    }

    void Shapeless(ItemStack result, std::vector<Ingredient> parts) {
        Recipe r;
        r.result = result;
        r.parts = parts;
        r.shapeless = true;
        recipes.push_back(r);
    }

    RecipeBook() {
        // ---- RecipesTools
        const Ingredient toolMaterials[5] = { B(BLOCK_PLANKS), B(BLOCK_COBBLESTONE), I(ITEM_IRON_INGOT), I(ITEM_DIAMOND), I(ITEM_GOLD_INGOT) };
        const int pickaxes[5] = { ITEM_WOODEN_PICKAXE, ITEM_STONE_PICKAXE, ITEM_IRON_PICKAXE, ITEM_DIAMOND_PICKAXE, ITEM_GOLDEN_PICKAXE };
        const int shovels[5] = { ITEM_WOODEN_SHOVEL, ITEM_STONE_SHOVEL, ITEM_IRON_SHOVEL, ITEM_DIAMOND_SHOVEL, ITEM_GOLDEN_SHOVEL };
        const int axes[5] = { ITEM_WOODEN_AXE, ITEM_STONE_AXE, ITEM_IRON_AXE, ITEM_DIAMOND_AXE, ITEM_GOLDEN_AXE };
        const int hoes[5] = { ITEM_WOODEN_HOE, ITEM_STONE_HOE, ITEM_IRON_HOE, ITEM_DIAMOND_HOE, ITEM_GOLDEN_HOE };
        const int swords[5] = { ITEM_WOODEN_SWORD, ITEM_STONE_SWORD, ITEM_IRON_SWORD, ITEM_DIAMOND_SWORD, ITEM_GOLDEN_SWORD };
        const Ingredient stick = I(ITEM_STICK);
        for (int m = 0; m < 5; m++) {
            const Ingredient x = toolMaterials[m];
            Shaped(ItemStack(pickaxes[m], 1), { "XXX", " # ", " # " }, { { 'X', x }, { '#', stick } });
            Shaped(ItemStack(shovels[m], 1), { "X", "#", "#" }, { { 'X', x }, { '#', stick } });
            Shaped(ItemStack(axes[m], 1), { "XX", "X#", " #" }, { { 'X', x }, { '#', stick } });
            Shaped(ItemStack(hoes[m], 1), { "XX", " #", " #" }, { { 'X', x }, { '#', stick } });
            // ---- RecipesWeapons
            Shaped(ItemStack(swords[m], 1), { "X", "X", "#" }, { { 'X', x }, { '#', stick } });
        }
        Shaped(ItemStack(ITEM_SHEARS, 1), { " #", "# " }, { { '#', I(ITEM_IRON_INGOT) } });
        Shaped(ItemStack(ITEM_BOW, 1), { " #X", "# X", " #X" }, { { 'X', I(ITEM_STRING) }, { '#', stick } });
        Shaped(ItemStack(ITEM_ARROW, 4), { "X", "#", "Y" }, { { 'Y', I(ITEM_FEATHER) }, { 'X', I(ITEM_FLINT) }, { '#', stick } });

        // ---- RecipesIngots
        const std::pair<int, ItemStack> storage[4] = {
            { BLOCK_GOLD_BLOCK, ItemStack(ITEM_GOLD_INGOT, 9) },
            { BLOCK_IRON_BLOCK, ItemStack(ITEM_IRON_INGOT, 9) },
            { BLOCK_DIAMOND_BLOCK, ItemStack(ITEM_DIAMOND, 9) },
            { BLOCK_LAPIS_BLOCK, ItemStack(ITEM_DYE, 9, DYE_BLUE) },
        };
        for (const auto& [block, ingots] : storage) {
            Shaped(ItemStack(block, 1), { "###", "###", "###" }, { { '#', I(ingots.id, ingots.damage) } });
            Shaped(ingots, { "#" }, { { '#', B(block) } });
        }

        // ---- RecipesFood
        Shaped(ItemStack(ITEM_MUSHROOM_STEW, 1), { "Y", "X", "#" },
               { { 'X', B(BLOCK_BROWN_MUSHROOM) }, { 'Y', B(BLOCK_RED_MUSHROOM) }, { '#', I(ITEM_BOWL) } });
        Shaped(ItemStack(ITEM_MUSHROOM_STEW, 1), { "Y", "X", "#" },
               { { 'X', B(BLOCK_RED_MUSHROOM) }, { 'Y', B(BLOCK_BROWN_MUSHROOM) }, { '#', I(ITEM_BOWL) } });
        Shaped(ItemStack(ITEM_COOKIE, 8), { "#X#" }, { { 'X', I(ITEM_DYE, DYE_BROWN) }, { '#', I(ITEM_WHEAT) } });
        Shaped(ItemStack(BLOCK_MELON, 1), { "MMM", "MMM", "MMM" }, { { 'M', I(ITEM_MELON) } });
        Shaped(ItemStack(ITEM_MELON_SEEDS, 1), { "M" }, { { 'M', I(ITEM_MELON) } });
        Shaped(ItemStack(ITEM_PUMPKIN_SEEDS, 4), { "M" }, { { 'M', B(BLOCK_PUMPKIN) } });

        // ---- RecipesCrafting
        Shaped(ItemStack(BLOCK_CHEST, 1), { "###", "# #", "###" }, { { '#', B(BLOCK_PLANKS) } });
        Shaped(ItemStack(BLOCK_FURNACE, 1), { "###", "# #", "###" }, { { '#', B(BLOCK_COBBLESTONE) } });
        Shaped(ItemStack(BLOCK_CRAFTING_TABLE, 1), { "##", "##" }, { { '#', B(BLOCK_PLANKS) } });
        Shaped(ItemStack(BLOCK_SANDSTONE, 1), { "##", "##" }, { { '#', B(BLOCK_SAND) } });
        Shaped(ItemStack(BLOCK_STONE_BRICKS, 4), { "##", "##" }, { { '#', B(BLOCK_STONE) } });
        Shaped(ItemStack(BLOCK_IRON_BARS, 16), { "###", "###" }, { { '#', I(ITEM_IRON_INGOT) } });
        Shaped(ItemStack(BLOCK_GLASS_PANE, 16), { "###", "###" }, { { '#', B(BLOCK_GLASS) } });

        // ---- RecipesArmor (chain armor is made of fire, as in Beta: uncraftable)
        const Ingredient armorMaterials[5] = { I(ITEM_LEATHER), B(BLOCK_FIRE), I(ITEM_IRON_INGOT), I(ITEM_DIAMOND), I(ITEM_GOLD_INGOT) };
        const std::vector<std::string> armorShapes[4] = {
            { "XXX", "X X" }, { "X X", "XXX", "XXX" }, { "XXX", "X X", "X X" }, { "X X", "X X" },
        };
        for (int m = 0; m < 5; m++) {
            for (int slot = 0; slot < 4; slot++) {
                Shaped(ItemStack(ITEM_LEATHER_HELMET + m * 4 + slot, 1), armorShapes[slot], { { 'X', armorMaterials[m] } });
            }
        }

        // ---- RecipesDyes
        for (int dye = 0; dye < 16; dye++) {
            Shapeless(ItemStack(BLOCK_WOOL, 1, ~dye & 15), { I(ITEM_DYE, dye), I(BLOCK_WOOL, 0) });
        }
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_YELLOW), { B(BLOCK_DANDELION) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_RED), { B(BLOCK_ROSE) });
        Shapeless(ItemStack(ITEM_DYE, 3, DYE_WHITE), { I(ITEM_BONE) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_PINK), { I(ITEM_DYE, DYE_RED), I(ITEM_DYE, DYE_WHITE) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_ORANGE), { I(ITEM_DYE, DYE_RED), I(ITEM_DYE, DYE_YELLOW) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_LIME), { I(ITEM_DYE, DYE_GREEN), I(ITEM_DYE, DYE_WHITE) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_GRAY), { I(ITEM_DYE, DYE_BLACK), I(ITEM_DYE, DYE_WHITE) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_LIGHT_GRAY), { I(ITEM_DYE, DYE_GRAY), I(ITEM_DYE, DYE_WHITE) });
        Shapeless(ItemStack(ITEM_DYE, 3, DYE_LIGHT_GRAY), { I(ITEM_DYE, DYE_BLACK), I(ITEM_DYE, DYE_WHITE), I(ITEM_DYE, DYE_WHITE) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_LIGHT_BLUE), { I(ITEM_DYE, DYE_BLUE), I(ITEM_DYE, DYE_WHITE) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_CYAN), { I(ITEM_DYE, DYE_BLUE), I(ITEM_DYE, DYE_GREEN) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_PURPLE), { I(ITEM_DYE, DYE_BLUE), I(ITEM_DYE, DYE_RED) });
        Shapeless(ItemStack(ITEM_DYE, 2, DYE_MAGENTA), { I(ITEM_DYE, DYE_PURPLE), I(ITEM_DYE, DYE_PINK) });
        Shapeless(ItemStack(ITEM_DYE, 3, DYE_MAGENTA), { I(ITEM_DYE, DYE_BLUE), I(ITEM_DYE, DYE_RED), I(ITEM_DYE, DYE_PINK) });
        Shapeless(ItemStack(ITEM_DYE, 4, DYE_MAGENTA),
                  { I(ITEM_DYE, DYE_BLUE), I(ITEM_DYE, DYE_RED), I(ITEM_DYE, DYE_RED), I(ITEM_DYE, DYE_WHITE) });

        // ---- CraftingManager
        const Ingredient planks = B(BLOCK_PLANKS), cobble = B(BLOCK_COBBLESTONE), iron = I(ITEM_IRON_INGOT);
        const Ingredient redstone = I(ITEM_REDSTONE);
        Shaped(ItemStack(ITEM_PAPER, 3), { "###" }, { { '#', I(ITEM_REEDS) } });
        Shaped(ItemStack(ITEM_BOOK, 1), { "#", "#", "#" }, { { '#', I(ITEM_PAPER) } });
        Shaped(ItemStack(BLOCK_FENCE, 2), { "###", "###" }, { { '#', stick } });
        Shaped(ItemStack(BLOCK_FENCE_GATE, 1), { "#W#", "#W#" }, { { '#', stick }, { 'W', planks } });
        Shaped(ItemStack(BLOCK_JUKEBOX, 1), { "###", "#X#", "###" }, { { '#', planks }, { 'X', I(ITEM_DIAMOND) } });
        Shaped(ItemStack(BLOCK_NOTE_BLOCK, 1), { "###", "#X#", "###" }, { { '#', planks }, { 'X', redstone } });
        Shaped(ItemStack(BLOCK_BOOKSHELF, 1), { "###", "XXX", "###" }, { { '#', planks }, { 'X', I(ITEM_BOOK) } });
        Shaped(ItemStack(BLOCK_SNOW, 1), { "##", "##" }, { { '#', I(ITEM_SNOWBALL) } });
        Shaped(ItemStack(BLOCK_CLAY, 1), { "##", "##" }, { { '#', I(ITEM_CLAY_BALL) } });
        Shaped(ItemStack(BLOCK_BRICKS, 1), { "##", "##" }, { { '#', I(ITEM_BRICK) } });
        Shaped(ItemStack(BLOCK_GLOWSTONE, 1), { "##", "##" }, { { '#', I(ITEM_GLOWSTONE_DUST) } });
        Shaped(ItemStack(BLOCK_WOOL, 1), { "##", "##" }, { { '#', I(ITEM_STRING) } });
        Shaped(ItemStack(BLOCK_TNT, 1), { "X#X", "#X#", "X#X" }, { { 'X', I(ITEM_GUNPOWDER) }, { '#', B(BLOCK_SAND) } });
        Shaped(ItemStack(BLOCK_SLAB, 3, 3), { "###" }, { { '#', cobble } });
        Shaped(ItemStack(BLOCK_SLAB, 3, 0), { "###" }, { { '#', B(BLOCK_STONE) } });
        Shaped(ItemStack(BLOCK_SLAB, 3, 1), { "###" }, { { '#', B(BLOCK_SANDSTONE) } });
        Shaped(ItemStack(BLOCK_SLAB, 3, 2), { "###" }, { { '#', planks } });
        Shaped(ItemStack(BLOCK_SLAB, 3, 4), { "###" }, { { '#', B(BLOCK_BRICKS) } });
        Shaped(ItemStack(BLOCK_SLAB, 3, 5), { "###" }, { { '#', B(BLOCK_STONE_BRICKS) } });
        Shaped(ItemStack(BLOCK_LADDER, 2), { "# #", "###", "# #" }, { { '#', stick } });
        Shaped(ItemStack(ITEM_WOODEN_DOOR, 1), { "##", "##", "##" }, { { '#', planks } });
        Shaped(ItemStack(BLOCK_TRAPDOOR, 2), { "###", "###" }, { { '#', planks } });
        Shaped(ItemStack(ITEM_IRON_DOOR, 1), { "##", "##", "##" }, { { '#', iron } });
        Shaped(ItemStack(ITEM_SIGN, 1), { "###", "###", " X " }, { { '#', planks }, { 'X', stick } });
        Shaped(ItemStack(ITEM_CAKE, 1), { "AAA", "BEB", "CCC" },
               { { 'A', I(ITEM_MILK_BUCKET) }, { 'B', I(ITEM_SUGAR) }, { 'C', I(ITEM_WHEAT) }, { 'E', I(ITEM_EGG) } });
        Shaped(ItemStack(ITEM_SUGAR, 1), { "#" }, { { '#', I(ITEM_REEDS) } });
        Shaped(ItemStack(BLOCK_PLANKS, 4), { "#" }, { { '#', B(BLOCK_LOG) } });
        Shaped(ItemStack(ITEM_STICK, 4), { "#", "#" }, { { '#', planks } });
        Shaped(ItemStack(BLOCK_TORCH, 4), { "X", "#" }, { { 'X', I(ITEM_COAL, 0) }, { '#', stick } });
        Shaped(ItemStack(BLOCK_TORCH, 4), { "X", "#" }, { { 'X', I(ITEM_COAL, 1) }, { '#', stick } });
        Shaped(ItemStack(ITEM_BOWL, 4), { "# #", " # " }, { { '#', planks } });
        Shaped(ItemStack(BLOCK_RAIL, 16), { "X X", "X#X", "X X" }, { { 'X', iron }, { '#', stick } });
        Shaped(ItemStack(BLOCK_POWERED_RAIL, 6), { "X X", "X#X", "XRX" },
               { { 'X', I(ITEM_GOLD_INGOT) }, { 'R', redstone }, { '#', stick } });
        Shaped(ItemStack(BLOCK_DETECTOR_RAIL, 6), { "X X", "X#X", "XRX" },
               { { 'X', iron }, { 'R', redstone }, { '#', B(BLOCK_STONE_PRESSURE_PLATE) } });
        Shaped(ItemStack(ITEM_MINECART, 1), { "# #", "###" }, { { '#', iron } });
        Shaped(ItemStack(BLOCK_JACK_O_LANTERN, 1), { "A", "B" }, { { 'A', B(BLOCK_PUMPKIN) }, { 'B', B(BLOCK_TORCH) } });
        Shaped(ItemStack(ITEM_CHEST_MINECART, 1), { "A", "B" }, { { 'A', B(BLOCK_CHEST) }, { 'B', I(ITEM_MINECART) } });
        Shaped(ItemStack(ITEM_FURNACE_MINECART, 1), { "A", "B" }, { { 'A', B(BLOCK_FURNACE) }, { 'B', I(ITEM_MINECART) } });
        Shaped(ItemStack(ITEM_BOAT, 1), { "# #", "###" }, { { '#', planks } });
        Shaped(ItemStack(ITEM_BUCKET, 1), { "# #", " # " }, { { '#', iron } });
        Shaped(ItemStack(ITEM_FLINT_AND_STEEL, 1), { "A ", " B" }, { { 'A', iron }, { 'B', I(ITEM_FLINT) } });
        Shaped(ItemStack(ITEM_BREAD, 1), { "###" }, { { '#', I(ITEM_WHEAT) } });
        Shaped(ItemStack(BLOCK_OAK_STAIRS, 4), { "#  ", "## ", "###" }, { { '#', planks } });
        Shaped(ItemStack(BLOCK_COBBLESTONE_STAIRS, 4), { "#  ", "## ", "###" }, { { '#', cobble } });
        Shaped(ItemStack(BLOCK_BRICK_STAIRS, 4), { "#  ", "## ", "###" }, { { '#', B(BLOCK_BRICKS) } });
        Shaped(ItemStack(BLOCK_STONE_BRICK_STAIRS, 4), { "#  ", "## ", "###" }, { { '#', B(BLOCK_STONE_BRICKS) } });
        Shaped(ItemStack(ITEM_FISHING_ROD, 1), { "  #", " #X", "# X" }, { { '#', stick }, { 'X', I(ITEM_STRING) } });
        Shaped(ItemStack(ITEM_PAINTING, 1), { "###", "#X#", "###" }, { { '#', stick }, { 'X', B(BLOCK_WOOL) } });
        Shaped(ItemStack(ITEM_GOLDEN_APPLE, 1), { "###", "#X#", "###" }, { { '#', B(BLOCK_GOLD_BLOCK) }, { 'X', I(ITEM_APPLE) } });
        Shaped(ItemStack(BLOCK_LEVER, 1), { "X", "#" }, { { 'X', stick }, { '#', cobble } });
        Shaped(ItemStack(BLOCK_REDSTONE_TORCH, 1), { "X", "#" }, { { 'X', redstone }, { '#', stick } });
        Shaped(ItemStack(ITEM_REPEATER, 1), { "#X#", "III" },
               { { '#', B(BLOCK_REDSTONE_TORCH) }, { 'X', redstone }, { 'I', B(BLOCK_STONE) } });
        Shaped(ItemStack(ITEM_CLOCK, 1), { " # ", "#X#", " # " }, { { '#', I(ITEM_GOLD_INGOT) }, { 'X', redstone } });
        Shaped(ItemStack(ITEM_COMPASS, 1), { " # ", "#X#", " # " }, { { '#', iron }, { 'X', redstone } });
        Shaped(ItemStack(ITEM_MAP, 1), { "###", "#X#", "###" }, { { '#', I(ITEM_PAPER) }, { 'X', I(ITEM_COMPASS) } });
        Shaped(ItemStack(BLOCK_STONE_BUTTON, 1), { "#", "#" }, { { '#', B(BLOCK_STONE) } });
        Shaped(ItemStack(BLOCK_STONE_PRESSURE_PLATE, 1), { "##" }, { { '#', B(BLOCK_STONE) } });
        Shaped(ItemStack(BLOCK_WOODEN_PRESSURE_PLATE, 1), { "##" }, { { '#', planks } });
        Shaped(ItemStack(BLOCK_DISPENSER, 1), { "###", "#X#", "#R#" }, { { '#', cobble }, { 'X', I(ITEM_BOW) }, { 'R', redstone } });
        Shaped(ItemStack(BLOCK_PISTON, 1), { "TTT", "#X#", "#R#" },
               { { '#', cobble }, { 'X', iron }, { 'R', redstone }, { 'T', planks } });
        Shaped(ItemStack(BLOCK_STICKY_PISTON, 1), { "S", "P" }, { { 'S', I(ITEM_SLIMEBALL) }, { 'P', B(BLOCK_PISTON) } });
        Shaped(ItemStack(ITEM_BED, 1), { "###", "XXX" }, { { '#', B(BLOCK_WOOL) }, { 'X', planks } });
    }
};

const RecipeBook& Book() {
    static const RecipeBook book;
    return book;
}

// ShapedRecipes.checkMatch: the pattern at (ox, oy), optionally mirrored.
bool ShapedMatchAt(const Recipe& r, const ItemStack* grid, int size, int ox, int oy, bool mirror) {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int px = x - ox, py = y - oy;
            Ingredient want;
            if (px >= 0 && py >= 0 && px < r.width && py < r.height) {
                want = r.cells[py * r.width + (mirror ? r.width - 1 - px : px)];
            }
            if (!want.Matches(grid[y * size + x])) return false;
        }
    }
    return true;
}

bool Matches(const Recipe& r, const ItemStack* grid, int size) {
    if (r.shapeless) {
        std::vector<Ingredient> left = r.parts;
        for (int i = 0; i < size * size; i++) {
            if (grid[i].IsEmpty()) continue;
            bool found = false;
            for (size_t k = 0; k < left.size(); k++) {
                if (left[k].Matches(grid[i])) {
                    left.erase(left.begin() + (long)k);
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return left.empty();
    }
    if (r.width > size || r.height > size) return false;
    for (int oy = 0; oy <= size - r.height; oy++) {
        for (int ox = 0; ox <= size - r.width; ox++) {
            if (ShapedMatchAt(r, grid, size, ox, oy, true) || ShapedMatchAt(r, grid, size, ox, oy, false)) return true;
        }
    }
    return false;
}

} // namespace

ItemStack CraftingResult(const ItemStack* grid, int size) {
    for (const Recipe& r : Book().recipes) {
        if (Matches(r, grid, size)) return r.result;
    }
    return ItemStack();
}

int CraftingContainerItem(int id) {
    if (id == ITEM_MILK_BUCKET || id == ITEM_WATER_BUCKET || id == ITEM_LAVA_BUCKET) return ITEM_BUCKET;
    return 0;
}
