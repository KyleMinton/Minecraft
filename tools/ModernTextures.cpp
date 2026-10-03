// ModernTextures: rebuilds the game's textures from Minecraft 1.21's assets.
//
//   ModernTextures <.minecraft/versions/1.21/1.21/assets/minecraft> <resources>
//
// The game draws from Beta-style atlases (terrain.png and items.png: 16x16
// grids of 16px tiles; particles.png: 8px cells) at Beta's tile numbers. 1.21
// ships every block, item and particle as its own file, so this lays each one
// into its Beta tile. Tiles with no 1.21 counterpart keep whatever the atlas had.
// It also converts 1.21's chest textures to the pre-1.15 layout the chest
// renderer uses, copies the animated block strips TextureFX plays, and copies the
// GUI, font, sky, sign, skin and break textures across as they are.

#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string g_assets;    // .../assets/minecraft
std::string g_resources; // the game's resources folder
int g_missing = 0;

// ---- Images

Image Load(const std::string& path) {
    if (!FileExists(path.c_str())) {
        printf("  missing: %s\n", path.c_str());
        g_missing++;
        return Image{};
    }
    Image image = LoadImage(path.c_str());
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return image;
}

Image Asset(const std::string& rel) { return Load(g_assets + "/textures/" + rel + ".png"); }

Color* Pixels(const Image& image) { return (Color*)image.data; }

// Raw copy (no blending) of a w x h block of src at (sx, sy) to dst at (dx, dy).
void Copy(const Image& src, int sx, int sy, int w, int h, Image& dst, int dx, int dy) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int fx = sx + x, fy = sy + y, tx = dx + x, ty = dy + y;
            if (fx < 0 || fy < 0 || fx >= src.width || fy >= src.height) continue;
            if (tx < 0 || ty < 0 || tx >= dst.width || ty >= dst.height) continue;
            Pixels(dst)[ty * dst.width + tx] = Pixels(src)[fy * src.width + fx];
        }
}

void CopyAll(const Image& src, Image& dst, int dx, int dy) { Copy(src, 0, 0, src.width, src.height, dst, dx, dy); }

Image Crop(const Image& src, int x, int y, int w, int h) {
    Image out = GenImageColor(w, h, BLANK);
    Copy(src, x, y, w, h, out, 0, 0);
    return out;
}

// The first frame of an animation strip (frames stacked vertically, each width x width).
Image FirstFrame(const Image& strip) { return Crop(strip, 0, 0, strip.width, strip.width); }

// Grayscale textures colored the way 1.21 colors them in the world.
void Multiply(Image& image, Color c) {
    for (int i = 0; i < image.width * image.height; i++) {
        Color& p = Pixels(image)[i];
        p.r = (unsigned char)(p.r * c.r / 255);
        p.g = (unsigned char)(p.g * c.g / 255);
        p.b = (unsigned char)(p.b * c.b / 255);
    }
}

// Source-over: `top` drawn over `image`.
void Over(Image& image, const Image& top) {
    for (int i = 0; i < image.width * image.height && i < top.width * top.height; i++) {
        Color s = Pixels(top)[i];
        Color& d = Pixels(image)[i];
        float a = s.a / 255.0f, da = d.a / 255.0f, oa = a + da * (1 - a);
        if (oa <= 0.0f) continue;
        d.r = (unsigned char)((s.r * a + d.r * da * (1 - a)) / oa);
        d.g = (unsigned char)((s.g * a + d.g * da * (1 - a)) / oa);
        d.b = (unsigned char)((s.b * a + d.b * da * (1 - a)) / oa);
        d.a = (unsigned char)(oa * 255.0f);
    }
}

bool Save(const Image& image, const std::string& rel) {
    std::string path = g_resources + "/" + rel;
    fs::create_directories(fs::path(path).parent_path());
    bool ok = ExportImage(image, path.c_str());
    printf("%s %s\n", ok ? "wrote" : "FAILED", rel.c_str());
    return ok;
}

void CopyFile(const std::string& fromRel, const std::string& toRel) {
    std::string from = g_assets + "/" + fromRel, to = g_resources + "/" + toRel;
    if (!fs::exists(from)) {
        printf("  missing: %s\n", from.c_str());
        g_missing++;
        return;
    }
    fs::create_directories(fs::path(to).parent_path());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing);
    printf("copied %s\n", toRel.c_str());
}

// ---- Atlases

// An atlas of 16x16 cells; the existing file's tiles are the starting point.
struct Atlas {
    Image image;
    int cell;
    Atlas(const std::string& rel, int size, int cellSize) : cell(cellSize) {
        std::string path = g_resources + "/" + rel;
        image = FileExists(path.c_str()) ? Load(path) : GenImageColor(size, size, BLANK);
        if (image.width != size || image.height != size) ImageResizeNN(&image, size, size);
    }
    // Replaces cell `index` with `tile` (scaled to the cell size if it isn't).
    void Put(int index, Image tile) {
        if (tile.data == nullptr) return;
        if (tile.width != cell || tile.height != cell) ImageResizeNN(&tile, cell, cell);
        int x = (index % 16) * cell, y = (index / 16) * cell;
        for (int j = 0; j < cell; j++)
            for (int i = 0; i < cell; i++) Pixels(image)[(y + j) * image.width + x + i] = BLANK;
        CopyAll(tile, image, x, y);
        UnloadImage(tile);
    }
    // A bigger image across several cells from `index` (flowing liquids: 2x2).
    void PutSpan(int index, Image tile, int cells) {
        if (tile.data == nullptr) return;
        if (tile.width != cell * cells) ImageResizeNN(&tile, cell * cells, cell * cells);
        CopyAll(tile, image, (index % 16) * cell, (index / 16) * cell);
        UnloadImage(tile);
    }
};

Image Block(const std::string& name) {
    Image image = Asset("block/" + name);
    if (image.data != nullptr && image.height > image.width) {
        Image frame = FirstFrame(image);
        UnloadImage(image);
        return frame;
    }
    return image;
}

constexpr Color WATER_COLOR = { 0x3F, 0x76, 0xE4, 255 };   // 1.21's default water
constexpr Color LEATHER_COLOR = { 0xA0, 0x65, 0x40, 255 }; // undyed leather armor

struct Named { int index; const char* name; };

// Beta's terrain.png tile numbers, and the 1.21 block texture for each.
const Named TERRAIN[] = {
    { 0, "grass_block_top" }, { 1, "stone" }, { 2, "dirt" }, { 3, "grass_block_side" }, { 4, "oak_planks" },
    { 5, "smooth_stone_slab_side" }, { 6, "smooth_stone" }, { 7, "bricks" }, { 8, "tnt_side" }, { 9, "tnt_top" },
    { 10, "tnt_bottom" }, { 11, "cobweb" }, { 12, "poppy" }, { 13, "dandelion" }, { 15, "oak_sapling" },
    { 16, "cobblestone" }, { 17, "bedrock" }, { 18, "sand" }, { 19, "gravel" }, { 20, "oak_log" }, { 21, "oak_log_top" },
    { 22, "iron_block" }, { 23, "gold_block" }, { 24, "diamond_block" }, { 28, "red_mushroom" }, { 29, "brown_mushroom" },
    { 32, "gold_ore" }, { 33, "iron_ore" }, { 34, "coal_ore" }, { 35, "bookshelf" }, { 36, "mossy_cobblestone" },
    { 37, "obsidian" }, { 38, "grass_block_side_overlay" }, { 39, "short_grass" }, { 43, "crafting_table_top" },
    { 44, "furnace_front" }, { 45, "furnace_side" }, { 46, "dispenser_front" }, { 48, "sponge" }, { 49, "glass" },
    { 50, "diamond_ore" }, { 51, "redstone_ore" }, { 52, "oak_leaves" }, { 54, "stone_bricks" }, { 55, "dead_bush" },
    { 56, "fern" }, { 59, "crafting_table_side" }, { 60, "crafting_table_front" }, { 61, "furnace_front_on" },
    { 62, "furnace_top" }, { 63, "spruce_sapling" }, { 64, "white_wool" }, { 65, "spawner" }, { 66, "snow" }, { 67, "ice" },
    { 68, "grass_block_snow" }, { 69, "cactus_top" }, { 70, "cactus_side" }, { 71, "cactus_bottom" }, { 72, "clay" },
    { 73, "sugar_cane" }, { 74, "jukebox_side" }, { 75, "jukebox_top" }, { 79, "birch_sapling" }, { 80, "torch" },
    { 81, "oak_door_top" }, { 82, "iron_door_top" }, { 83, "ladder" }, { 84, "oak_trapdoor" }, { 85, "iron_bars" },
    { 86, "farmland_moist" }, { 87, "farmland" }, { 88, "wheat_stage0" }, { 89, "wheat_stage1" }, { 90, "wheat_stage2" },
    { 91, "wheat_stage3" }, { 92, "wheat_stage4" }, { 93, "wheat_stage5" }, { 94, "wheat_stage6" }, { 95, "wheat_stage7" },
    { 96, "lever" }, { 97, "oak_door_bottom" }, { 98, "iron_door_bottom" }, { 99, "redstone_torch" },
    { 100, "mossy_stone_bricks" }, { 101, "cracked_stone_bricks" }, { 102, "pumpkin_top" }, { 103, "netherrack" },
    { 104, "soul_sand" }, { 105, "glowstone" }, { 106, "piston_top_sticky" }, { 107, "piston_top" }, { 108, "piston_side" },
    { 109, "piston_bottom" }, { 110, "piston_inner" }, { 111, "melon_stem" }, { 112, "rail_corner" },
    { 113, "black_wool" }, { 114, "gray_wool" }, { 115, "redstone_torch_off" }, { 116, "spruce_log" }, { 117, "birch_log" },
    { 118, "pumpkin_side" }, { 119, "carved_pumpkin" }, { 120, "jack_o_lantern" }, { 121, "cake_top" }, { 122, "cake_side" },
    { 123, "cake_inner" }, { 124, "cake_bottom" }, { 125, "red_mushroom_block" }, { 126, "brown_mushroom_block" },
    { 127, "attached_melon_stem" }, { 128, "rail" }, { 129, "red_wool" }, { 130, "pink_wool" }, { 131, "repeater" },
    { 132, "spruce_leaves" }, { 136, "melon_side" }, { 137, "melon_top" }, { 141, "mushroom_stem" },
    { 142, "mushroom_block_inside" }, { 143, "vine" }, { 144, "lapis_block" }, { 145, "green_wool" }, { 146, "lime_wool" },
    { 147, "repeater_on" }, { 148, "glass_pane_top" }, { 160, "lapis_ore" }, { 161, "brown_wool" }, { 162, "yellow_wool" },
    { 163, "powered_rail_on" }, { 176, "sandstone_top" }, { 177, "blue_wool" }, { 178, "light_blue_wool" },
    { 179, "powered_rail" }, { 192, "sandstone" }, { 193, "purple_wool" }, { 194, "magenta_wool" }, { 195, "detector_rail" },
    { 208, "sandstone_bottom" }, { 209, "cyan_wool" }, { 210, "orange_wool" }, { 225, "light_gray_wool" },
    // Textures 1.21 has that Beta shared with something else, in tiles Beta left empty.
    { 211, "note_block" }, { 212, "birch_leaves" }, { 213, "spruce_log_top" }, { 214, "birch_log_top" },
};

// 1.21's chest textures, rebuilt in the layout of the old chest model (the
// renderer's): 1.15 turned the model upright, so every face moved. Per box:
// top and bottom swap (each flipped), front and back swap, and every side turns 180.
struct Box { int u, v, w, h, d; };
struct Faces { Image top, bottom, right, front, left, back; };

Faces Extract(const Image& img, Box b) {
    return Faces{ Crop(img, b.u + b.d, b.v, b.w, b.d), Crop(img, b.u + b.d + b.w, b.v, b.w, b.d),
                  Crop(img, b.u, b.v + b.d, b.d, b.h), Crop(img, b.u + b.d, b.v + b.d, b.w, b.h),
                  Crop(img, b.u + b.d + b.w, b.v + b.d, b.d, b.h), Crop(img, b.u + 2 * b.d + b.w, b.v + b.d, b.w, b.h) };
}

void Place(Image& img, Box b, const Faces& f) {
    CopyAll(f.top, img, b.u + b.d, b.v);
    CopyAll(f.bottom, img, b.u + b.d + b.w, b.v);
    CopyAll(f.right, img, b.u, b.v + b.d);
    CopyAll(f.front, img, b.u + b.d, b.v + b.d);
    CopyAll(f.left, img, b.u + b.d + b.w, b.v + b.d);
    CopyAll(f.back, img, b.u + 2 * b.d + b.w, b.v + b.d);
}

Faces ToOldModel(const Faces& m) {
    Faces o{ ImageCopy(m.bottom), ImageCopy(m.top), ImageCopy(m.right), ImageCopy(m.back), ImageCopy(m.left), ImageCopy(m.front) };
    ImageFlipVertical(&o.top);
    ImageFlipVertical(&o.bottom);
    for (Image* side : { &o.right, &o.front, &o.left, &o.back }) {
        ImageFlipVertical(side);
        ImageFlipHorizontal(side);
    }
    return o;
}

void Unload(Faces& f) {
    for (Image* i : { &f.top, &f.bottom, &f.right, &f.front, &f.left, &f.back }) UnloadImage(*i);
}

Image SideBySide(const Image& a, const Image& b) {
    Image out = GenImageColor(a.width + b.width, std::max(a.height, b.height), BLANK);
    CopyAll(a, out, 0, 0);
    CopyAll(b, out, a.width, 0);
    return out;
}

void BuildChests() {
    const Box lid = { 0, 0, 14, 5, 14 }, bottom = { 0, 19, 14, 10, 14 }, knob = { 0, 0, 2, 4, 1 };
    Image normal = Asset("entity/chest/normal");
    if (normal.data != nullptr) {
        Image old = GenImageColor(64, 64, BLANK);
        for (Box b : { lid, bottom, knob }) {
            Faces m = Extract(normal, b), o = ToOldModel(m);
            Place(old, b, o);
            Unload(m);
            Unload(o);
        }
        Save(old, "textures/item/chest.png");
        UnloadImage(old);
        UnloadImage(normal);
    }

    // The double chest: 1.21 has a texture per half (15 wide, a 1-wide lock half);
    // the old model is one 30-wide box (2-wide knob). Each half is converted, then
    // their faces are laid side by side.
    Image left = Asset("entity/chest/normal_left"), right = Asset("entity/chest/normal_right");
    if (left.data != nullptr && right.data != nullptr) {
        Image old = GenImageColor(128, 64, BLANK);
        const Box halves[3] = { { 0, 0, 15, 5, 14 }, { 0, 19, 15, 10, 14 }, { 0, 0, 1, 4, 1 } };
        const Box wholes[3] = { { 0, 0, 30, 5, 14 }, { 0, 19, 30, 10, 14 }, { 0, 0, 2, 4, 1 } };
        for (int i = 0; i < 3; i++) {
            Faces l = ToOldModel(Extract(left, halves[i])), r = ToOldModel(Extract(right, halves[i]));
            // Seen from the front, the left half's faces are the right-hand half of the
            // old box's front, and so on round the box.
            Faces w{ SideBySide(r.top, l.top), SideBySide(r.bottom, l.bottom), ImageCopy(r.right),
                     SideBySide(r.front, l.front), ImageCopy(l.left), SideBySide(l.back, r.back) };
            Place(old, wholes[i], w);
            Unload(l);
            Unload(r);
            Unload(w);
        }
        Save(old, "textures/item/largechest.png");
        UnloadImage(old);
    }
    UnloadImage(left);
    UnloadImage(right);
}

void BuildTerrain() {
    Atlas terrain("textures/terrain.png", 256, 16);
    for (const Named& t : TERRAIN) terrain.Put(t.index, Block(t.name));

    // Redstone dust: Beta has a cross (164) and a line along U (165); 1.21 draws
    // them from a dot and line pieces.
    Image dot = Block("redstone_dust_dot"), line = Block("redstone_dust_line0");
    if (dot.data != nullptr && line.data != nullptr) {
        Image across = ImageCopy(line);
        ImageRotateCW(&across);
        Image cross = ImageCopy(dot);
        Over(cross, line);
        Over(cross, across);
        // line0 runs along V; Beta's line tile runs along U.
        terrain.Put(165, across);
        terrain.Put(164, cross);
    }
    UnloadImage(dot);
    UnloadImage(line);

    // The chest's block tiles (top 25, side 26, front 27; its break particles use
    // them), cut from the converted chest texture: the lid's face over the base's.
    Image chest = Load(g_resources + "/textures/item/chest.png");
    if (chest.data != nullptr) {
        auto stack = [&](int lidX, int baseX) {
            Image face = GenImageColor(14, 15, BLANK);
            Copy(chest, lidX, 14, 14, 5, face, 0, 0);
            Copy(chest, baseX, 33, 14, 10, face, 0, 5);
            return face;
        };
        Image front = stack(14, 14);
        Copy(chest, 1, 1, 2, 4, front, 6, 3); // the latch
        terrain.Put(25, Crop(chest, 14, 0, 14, 14));
        terrain.Put(26, stack(0, 0));
        terrain.Put(27, front);
        UnloadImage(chest);
    }

    // Liquids, fire and portals: their first frames (TextureFX animates them).
    Image water = Block("water_still"), waterFlow = FirstFrame(Asset("block/water_flow"));
    Multiply(water, WATER_COLOR);
    Multiply(waterFlow, WATER_COLOR);
    terrain.Put(205, water);
    terrain.PutSpan(206, waterFlow, 2);
    terrain.Put(237, Block("lava_still"));
    terrain.PutSpan(238, FirstFrame(Asset("block/lava_flow")), 2);
    terrain.Put(31, Block("fire_0"));
    terrain.Put(47, Block("fire_1"));
    terrain.Put(14, Block("nether_portal"));

    // Bed tops from the 1.21 bed's mattress (entity/bed/red.png): the head
    // half's top is at (6, 6), the foot half's at (6, 28), turned to Beta's tiles.
    Image bed = Asset("entity/bed/red");
    if (bed.data != nullptr) {
        Image head = Crop(bed, 6, 6, 16, 16), foot = Crop(bed, 6, 28, 16, 16);
        ImageRotateCW(&head);
        ImageRotateCW(&foot);
        terrain.Put(135, head);
        terrain.Put(134, foot);
        UnloadImage(bed);
    }

    // The block-breaking cracks (BreakOverlay reads tiles 240-249).
    for (int i = 0; i < 10; i++) terrain.Put(240 + i, Block(TextFormat("destroy_stage_%d", i)));

    Save(terrain.image, "textures/terrain.png");
    UnloadImage(terrain.image);
}

// Beta's items.png icon numbers, and the 1.21 item texture for each.
const Named ITEMS[] = {
    { 1, "chainmail_helmet" }, { 2, "iron_helmet" }, { 3, "diamond_helmet" }, { 4, "golden_helmet" },
    { 5, "flint_and_steel" }, { 6, "flint" }, { 7, "coal" }, { 8, "string" }, { 9, "wheat_seeds" }, { 10, "apple" },
    { 11, "golden_apple" }, { 12, "egg" }, { 13, "sugar" }, { 14, "snowball" },
    { 17, "chainmail_chestplate" }, { 18, "iron_chestplate" }, { 19, "diamond_chestplate" }, { 20, "golden_chestplate" },
    { 21, "bow" }, { 22, "brick" }, { 23, "iron_ingot" }, { 24, "feather" }, { 25, "wheat" }, { 26, "painting" },
    { 27, "sugar_cane" }, { 28, "bone" }, { 29, "cake" }, { 30, "slime_ball" },
    { 33, "chainmail_leggings" }, { 34, "iron_leggings" }, { 35, "diamond_leggings" }, { 36, "golden_leggings" },
    { 37, "arrow" }, { 39, "gold_ingot" }, { 40, "gunpowder" }, { 41, "bread" }, { 42, "oak_sign" }, { 43, "oak_door" },
    { 44, "iron_door" },
    { 49, "chainmail_boots" }, { 50, "iron_boots" }, { 51, "diamond_boots" }, { 52, "golden_boots" }, { 53, "stick" },
    { 54, "compass_16" }, { 55, "diamond" }, { 56, "redstone" }, { 57, "clay_ball" }, { 58, "paper" }, { 59, "book" },
    { 60, "filled_map" }, { 61, "pumpkin_seeds" }, { 62, "melon_seeds" },
    { 64, "wooden_sword" }, { 65, "stone_sword" }, { 66, "iron_sword" }, { 67, "diamond_sword" }, { 68, "golden_sword" },
    { 69, "fishing_rod" }, { 70, "clock_00" }, { 71, "bowl" }, { 72, "mushroom_stew" }, { 73, "glowstone_dust" },
    { 74, "bucket" }, { 75, "water_bucket" }, { 76, "lava_bucket" }, { 77, "milk_bucket" },
    { 80, "wooden_shovel" }, { 81, "stone_shovel" }, { 82, "iron_shovel" }, { 83, "diamond_shovel" }, { 84, "golden_shovel" },
    { 86, "repeater" }, { 87, "porkchop" }, { 88, "cooked_porkchop" }, { 89, "cod" }, { 90, "cooked_cod" },
    { 91, "rotten_flesh" }, { 92, "cookie" }, { 93, "shears" },
    { 96, "wooden_pickaxe" }, { 97, "stone_pickaxe" }, { 98, "iron_pickaxe" }, { 99, "diamond_pickaxe" },
    { 100, "golden_pickaxe" }, { 103, "leather" }, { 104, "saddle" }, { 105, "beef" }, { 106, "cooked_beef" },
    { 107, "ender_pearl" }, { 109, "melon_slice" },
    { 112, "wooden_axe" }, { 113, "stone_axe" }, { 114, "iron_axe" }, { 115, "diamond_axe" }, { 116, "golden_axe" },
    { 121, "chicken" }, { 122, "cooked_chicken" },
    { 128, "wooden_hoe" }, { 129, "stone_hoe" }, { 130, "iron_hoe" }, { 131, "diamond_hoe" }, { 132, "golden_hoe" },
    { 135, "minecart" }, { 136, "oak_boat" }, { 151, "chest_minecart" }, { 167, "furnace_minecart" },
    { 240, "music_disc_13" }, { 241, "music_disc_cat" },
};

// Beta's dye icons by damage (ItemDye: two columns from icon 78).
const char* DYES[16] = { "ink_sac", "red_dye", "green_dye", "cocoa_beans", "lapis_lazuli", "purple_dye", "cyan_dye",
                         "light_gray_dye", "gray_dye", "pink_dye", "lime_dye", "yellow_dye", "light_blue_dye",
                         "magenta_dye", "orange_dye", "bone_meal" };

void BuildItems() {
    Atlas items("textures/gui/items.png", 256, 16);
    for (const Named& t : ITEMS) items.Put(t.index, Asset("item/" + std::string(t.name)));
    for (int d = 0; d < 16; d++) items.Put(78 + (d % 8) * 16 + d / 8, Asset("item/" + std::string(DYES[d])));

    // Leather armor is grey in 1.21, dyed brown unless dyed otherwise, with an undyed overlay.
    const char* leather[4] = { "leather_helmet", "leather_chestplate", "leather_leggings", "leather_boots" };
    for (int slot = 0; slot < 4; slot++) {
        Image base = Asset("item/" + std::string(leather[slot]));
        Image overlay = Asset("item/" + std::string(leather[slot]) + "_overlay");
        if (base.data == nullptr) continue;
        Multiply(base, LEATHER_COLOR);
        if (overlay.data != nullptr) {
            Over(base, overlay);
            UnloadImage(overlay);
        }
        items.Put(slot * 16, base);
    }

    Save(items.image, "textures/gui/items.png");
    UnloadImage(items.image);
}

void BuildParticles() {
    Atlas particles("textures/particles.png", 128, 8);
    for (int i = 0; i < 8; i++) particles.Put(i, Asset(TextFormat("particle/generic_%d", i)));
    for (int i = 0; i < 4; i++) particles.Put(19 + i, Asset(TextFormat("particle/splash_%d", i)));
    particles.Put(32, Asset("particle/bubble"));
    particles.Put(48, Asset("particle/flame"));
    particles.Put(49, Asset("particle/lava"));
    particles.Put(64, Asset("particle/note"));
    Save(particles.image, "textures/particles.png");
    UnloadImage(particles.image);
}

// The animated strips TextureFX plays, with their .mcmeta timing. Water is
// colored here, since the game doesn't tint it.
void CopyAnimations() {
    for (const char* name : { "water_still", "water_flow", "lava_still", "lava_flow", "fire_0", "fire_1", "nether_portal" }) {
        std::string rel = std::string("textures/block/") + name;
        Image strip = Asset("block/" + std::string(name));
        if (strip.data == nullptr) continue;
        if (std::string(name).rfind("water", 0) == 0) Multiply(strip, WATER_COLOR);
        Save(strip, std::string("textures/animated/") + name + ".png");
        UnloadImage(strip);
        CopyFile(rel + ".png.mcmeta", std::string("textures/animated/") + name + ".png.mcmeta");
    }
}

void CopyGui() {
    std::string from = g_assets + "/textures/gui", to = g_resources + "/textures/gui/modern";
    std::error_code error;
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    printf("%s textures/gui -> textures/gui/modern%s\n", error ? "FAILED copying" : "copied", error ? (": " + error.message()).c_str() : "");
    CopyFile("texts/splashes.txt", "textures/gui/modern/splashes.txt");
}

// The title panorama: the jar only has 1x1 stand-ins; the launcher downloads the
// real images into .minecraft/assets/objects, named by hash in the version's
// asset index. Expects the assets folder at .minecraft/versions/<v>/<v>/assets/minecraft,
// next to versions/<v>/<v>.json.
void CopyPanorama() {
    fs::path versionDir = fs::path(g_assets).parent_path().parent_path(); // versions/<v>/<v>
    fs::path versionJson = versionDir.parent_path() / (versionDir.filename().string() + ".json");
    fs::path root = versionDir.parent_path().parent_path().parent_path();  // .minecraft
    auto readAll = [](const fs::path& path) {
        char* text = LoadFileText(path.string().c_str());
        std::string s = text != nullptr ? text : "";
        if (text != nullptr) UnloadFileText(text);
        return s;
    };
    // The text of the first "quoted" value after `key`, starting at `from`.
    auto valueAfter = [](const std::string& text, size_t from, const std::string& key) {
        size_t at = text.find(key, from);
        if (at == std::string::npos) return std::string();
        at = text.find('"', text.find(':', at + key.size()) + 1);
        size_t end = text.find('"', at + 1);
        return at == std::string::npos || end == std::string::npos ? std::string() : text.substr(at + 1, end - at - 1);
    };

    std::string version = readAll(versionJson);
    size_t indexBlock = version.find("\"assetIndex\"");
    std::string indexId = indexBlock == std::string::npos ? "" : valueAfter(version, indexBlock, "\"id\"");
    std::string index = indexId.empty() ? "" : readAll(root / "assets" / "indexes" / (indexId + ".json"));
    if (index.empty()) {
        printf("  no asset index found (%s): the panorama keeps its stand-in images\n", versionJson.string().c_str());
        return;
    }
    for (int i = 0; i <= 6; i++) {
        std::string name = i < 6 ? TextFormat("panorama_%d.png", i) : "panorama_overlay.png";
        size_t entry = index.find("\"minecraft/textures/gui/title/background/" + name + "\"");
        std::string hash = entry == std::string::npos ? "" : valueAfter(index, entry, "\"hash\"");
        fs::path object = root / "assets" / "objects" / hash.substr(0, 2) / hash;
        std::string to = g_resources + "/textures/gui/modern/title/background/" + name;
        if (hash.size() < 2 || !fs::exists(object)) {
            printf("  missing: %s (run 1.21 once in the launcher to download it)\n", name.c_str());
            g_missing++;
            continue;
        }
        fs::copy_file(object, to, fs::copy_options::overwrite_existing);
        printf("copied textures/gui/modern/title/background/%s\n", name.c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: ModernTextures <.minecraft/versions/1.21/1.21/assets/minecraft> <resources folder>\n");
        return 1;
    }
    g_assets = argv[1];
    g_resources = argv[2];
    SetTraceLogLevel(LOG_WARNING);
    if (!DirectoryExists((g_assets + "/textures/block").c_str())) {
        printf("%s doesn't look like an assets/minecraft folder (no textures/block)\n", g_assets.c_str());
        return 1;
    }

    BuildChests(); // first: the terrain's chest tiles are cut from it
    BuildTerrain();
    BuildItems();
    BuildParticles();
    CopyAnimations();
    CopyGui();
    CopyPanorama(); // after CopyGui, which brings the jar's stand-ins

    CopyFile("textures/environment/sun.png", "textures/environment/sun.png");
    CopyFile("textures/environment/moon_phases.png", "textures/environment/moon_phases.png");
    CopyFile("textures/environment/clouds.png", "textures/environment/clouds.png");
    CopyFile("textures/font/ascii.png", "textures/font/ascii.png");
    CopyFile("textures/misc/pumpkinblur.png", "textures/misc/pumpkinblur.png");
    CopyFile("textures/misc/unknown_server.png", "textures/gui/unknown_world.png");
    CopyFile("textures/entity/signs/oak.png", "textures/item/sign.png");
    CopyFile("textures/entity/player/wide/steve.png", "textures/entity/steve.png");
    for (int i = 0; i < 10; i++) {
        CopyFile(TextFormat("textures/block/destroy_stage_%d.png", i), TextFormat("textures/destroy/destroy_stage_%d.png", i));
    }

    printf(g_missing ? "done, %d files missing\n" : "done\n", g_missing);
    return g_missing ? 2 : 0;
}
