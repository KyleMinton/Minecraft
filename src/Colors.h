#pragma once

#include "raylib.h"

// Beta colors its greyscale grass and leaves from climate color maps
// (misc/grasscolor.png, foliagecolor.png). There are no biomes here, so blocks in
// the world take the colors of a temperate forest (temperature 0.7, rainfall 0.8),
// and items the colors Beta uses for inventory icons.
constexpr Color GRASS_COLOR = { 121, 192, 90, 255 };
constexpr Color FOLIAGE_COLOR = { 89, 174, 48, 255 };
constexpr Color GRASS_ITEM_COLOR = { 124, 189, 107, 255 };      // grasscolor(0.5, 1.0)
constexpr Color OAK_LEAVES_ITEM_COLOR = { 72, 181, 24, 255 };   // ColorizerFoliage.getFoliageColorBasic
constexpr Color SPRUCE_LEAVES_COLOR = { 97, 153, 97, 255 };     // getFoliageColorPine
constexpr Color BIRCH_LEAVES_COLOR = { 128, 167, 85, 255 };     // getFoliageColorBirch
