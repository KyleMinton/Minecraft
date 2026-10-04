"""Generates src/ModernBlocksData.h: the blocks from after Beta 1.8 (1.21's
building and colored blocks), the textures they use, and the order of the
Building Blocks and Colored Blocks creative tabs.

    python tools/modern_blocks.py

Block IDs and texture tiles are positions in their lists, so they're kept
append-only: a regenerated file keeps every name it had in the same place and
adds new ones at the end. Saved worlds store those IDs, so never reorder them.
"""

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "ModernBlocksData.h")

COLORS = ["white", "light_gray", "gray", "black", "brown", "red", "orange", "yellow",
          "lime", "green", "cyan", "light_blue", "blue", "purple", "magenta", "pink"]
WOOL_META = {"white": 0, "orange": 1, "magenta": 2, "light_blue": 3, "yellow": 4, "lime": 5, "pink": 6, "gray": 7,
             "light_gray": 8, "cyan": 9, "purple": 10, "blue": 11, "brown": 12, "green": 13, "red": 14, "black": 15}

# 1.21 names that are Beta's own blocks or items here: (C++ ID, metadata).
BETA = {
    "oak_log": ("BLOCK_LOG", 0), "spruce_log": ("BLOCK_LOG", 1), "birch_log": ("BLOCK_LOG", 2),
    "oak_planks": ("BLOCK_PLANKS", 0), "oak_stairs": ("BLOCK_OAK_STAIRS", 0), "oak_fence": ("BLOCK_FENCE", 0),
    "oak_fence_gate": ("BLOCK_FENCE_GATE", 0), "oak_door": ("ITEM_WOODEN_DOOR", 0), "oak_trapdoor": ("BLOCK_TRAPDOOR", 0),
    "oak_pressure_plate": ("BLOCK_WOODEN_PRESSURE_PLATE", 0),
    "stone": ("BLOCK_STONE", 0), "stone_pressure_plate": ("BLOCK_STONE_PRESSURE_PLATE", 0), "stone_button": ("BLOCK_STONE_BUTTON", 0),
    "cobblestone": ("BLOCK_COBBLESTONE", 0), "cobblestone_stairs": ("BLOCK_COBBLESTONE_STAIRS", 0),
    "mossy_cobblestone": ("BLOCK_MOSSY_COBBLESTONE", 0),
    "stone_bricks": ("BLOCK_STONE_BRICKS", 0), "mossy_stone_bricks": ("BLOCK_STONE_BRICKS", 1),
    "cracked_stone_bricks": ("BLOCK_STONE_BRICKS", 2), "stone_brick_stairs": ("BLOCK_STONE_BRICK_STAIRS", 0),
    "bricks": ("BLOCK_BRICKS", 0), "brick_stairs": ("BLOCK_BRICK_STAIRS", 0), "sandstone": ("BLOCK_SANDSTONE", 0),
    "netherrack": ("BLOCK_NETHERRACK", 0), "iron_block": ("BLOCK_IRON_BLOCK", 0), "iron_bars": ("BLOCK_IRON_BARS", 0),
    "iron_door": ("ITEM_IRON_DOOR", 0), "gold_block": ("BLOCK_GOLD_BLOCK", 0), "lapis_block": ("BLOCK_LAPIS_BLOCK", 0),
    "diamond_block": ("BLOCK_DIAMOND_BLOCK", 0), "glass": ("BLOCK_GLASS", 0), "glass_pane": ("BLOCK_GLASS_PANE", 0),
}
for c in COLORS:
    BETA[c + "_wool"] = ("BLOCK_WOOL", WOOL_META[c])

blocks = {}   # name -> spec dict
building = []  # tab order
colored = []


def add(tab, name, kind=None, preset=None, tex=None, top=None, bottom=None, base=None, item=None, flags=(), display=None):
    tab.append(name)
    if name in BETA:
        return
    assert kind, name
    blocks[name] = dict(kind=kind, preset=preset, tex=tex, top=top, bottom=bottom, base=base, item=item,
                        flags=list(flags), display=display or Display(name))


def Display(name):
    special = {
        "coal_block": "Block of Coal", "redstone_block": "Block of Redstone", "emerald_block": "Block of Emerald",
        "netherite_block": "Block of Netherite", "bamboo_block": "Block of Bamboo",
        "stripped_bamboo_block": "Block of Stripped Bamboo", "amethyst_block": "Block of Amethyst",
        "copper_block": "Block of Copper", "waxed_copper_block": "Waxed Block of Copper", "quartz_block": "Block of Quartz",
        "smooth_quartz": "Smooth Quartz Block", "chiseled_quartz_block": "Chiseled Quartz Block",
        "heavy_weighted_pressure_plate": "Heavy Weighted Pressure Plate",
        "light_weighted_pressure_plate": "Light Weighted Pressure Plate",
    }
    if name in special:
        return special[name]
    return " ".join(w.capitalize() for w in name.split("_"))


def family(tab, base, preset, stairs=True, slab=True, wall=False, prefix=None):
    """A material's stairs, slab and wall, named the way 1.21 names them."""
    p = prefix or base
    if stairs:
        add(tab, p + "_stairs", "Stairs", preset, base=base)
    if slab:
        add(tab, p + "_slab", "Slab", preset, base=base)
    if wall:
        add(tab, p + "_wall", "Wall", preset, base=base)


def cube(tab, name, preset, tex=None, top=None, bottom=None, flags=()):
    add(tab, name, "Cube", preset, tex=tex or name, top=top, bottom=bottom, flags=flags)


def pillar(tab, name, preset, side, end):
    add(tab, name, "Pillar", preset, tex=side, top=end)


# ---- Building Blocks (CreativeModeTabs.BUILDING_BLOCKS, 1.21)

B = building
for w in ["oak", "spruce", "birch", "jungle", "acacia", "dark_oak", "mangrove", "cherry"]:
    pillar(B, w + "_log", "Log", w + "_log", w + "_log_top")
    pillar(B, w + "_wood", "Log", w + "_log", w + "_log")
    pillar(B, "stripped_" + w + "_log", "Log", "stripped_" + w + "_log", "stripped_" + w + "_log_top")
    pillar(B, "stripped_" + w + "_wood", "Log", "stripped_" + w + "_log", "stripped_" + w + "_log")
    cube(B, w + "_planks", "Planks")
    family(B, w + "_planks", "Planks", prefix=w)
    add(B, w + "_fence", "Fence", "Planks", tex=w + "_planks")
    add(B, w + "_fence_gate", "FenceGate", "Planks", tex=w + "_planks")
    add(B, w + "_door", "Door", "Planks", tex=w + "_door_bottom", base=w + "_door_top", item=w + "_door")
    add(B, w + "_trapdoor", "Trapdoor", "Planks", tex=w + "_trapdoor")
    add(B, w + "_pressure_plate", "PressurePlate", "Planks", tex=w + "_planks", flags=["ANYTHING"])
    add(B, w + "_button", "Button", "Planks", tex=w + "_planks", flags=["WOODEN"])

pillar(B, "bamboo_block", "Log", "bamboo_block", "bamboo_block_top")
pillar(B, "stripped_bamboo_block", "Log", "stripped_bamboo_block", "stripped_bamboo_block_top")
cube(B, "bamboo_planks", "Planks")
cube(B, "bamboo_mosaic", "Planks")
add(B, "bamboo_stairs", "Stairs", "Planks", base="bamboo_planks")
add(B, "bamboo_mosaic_stairs", "Stairs", "Planks", base="bamboo_mosaic")
add(B, "bamboo_slab", "Slab", "Planks", base="bamboo_planks")
add(B, "bamboo_mosaic_slab", "Slab", "Planks", base="bamboo_mosaic")
add(B, "bamboo_fence", "Fence", "Planks", tex="bamboo_planks")
add(B, "bamboo_fence_gate", "FenceGate", "Planks", tex="bamboo_planks")
add(B, "bamboo_door", "Door", "Planks", tex="bamboo_door_bottom", base="bamboo_door_top", item="bamboo_door")
add(B, "bamboo_trapdoor", "Trapdoor", "Planks", tex="bamboo_trapdoor")
add(B, "bamboo_pressure_plate", "PressurePlate", "Planks", tex="bamboo_planks", flags=["ANYTHING"])
add(B, "bamboo_button", "Button", "Planks", tex="bamboo_planks", flags=["WOODEN"])

for w in ["crimson", "warped"]:
    pillar(B, w + "_stem", "NetherWood", w + "_stem", w + "_stem_top")
    pillar(B, w + "_hyphae", "NetherWood", w + "_stem", w + "_stem")
    pillar(B, "stripped_" + w + "_stem", "NetherWood", "stripped_" + w + "_stem", "stripped_" + w + "_stem_top")
    pillar(B, "stripped_" + w + "_hyphae", "NetherWood", "stripped_" + w + "_stem", "stripped_" + w + "_stem")
    cube(B, w + "_planks", "NetherWood")
    family(B, w + "_planks", "NetherWood", prefix=w)
    add(B, w + "_fence", "Fence", "NetherWood", tex=w + "_planks")
    add(B, w + "_fence_gate", "FenceGate", "NetherWood", tex=w + "_planks")
    add(B, w + "_door", "Door", "NetherWood", tex=w + "_door_bottom", base=w + "_door_top", item=w + "_door")
    add(B, w + "_trapdoor", "Trapdoor", "NetherWood", tex=w + "_trapdoor")
    add(B, w + "_pressure_plate", "PressurePlate", "NetherWood", tex=w + "_planks", flags=["ANYTHING"])
    add(B, w + "_button", "Button", "NetherWood", tex=w + "_planks", flags=["WOODEN"])

add(B, "stone")
family(B, "stone", "Stone")
add(B, "stone_pressure_plate")
add(B, "stone_button")
add(B, "cobblestone")
add(B, "cobblestone_stairs")
add(B, "cobblestone_slab", "Slab", "Cobble", base="cobblestone")
add(B, "cobblestone_wall", "Wall", "Cobble", base="cobblestone")
add(B, "mossy_cobblestone")
family(B, "mossy_cobblestone", "Cobble", wall=True)
cube(B, "smooth_stone", "Stone")
add(B, "smooth_stone_slab", "Slab", "Stone", base="smooth_stone", tex="smooth_stone_slab_side")
add(B, "stone_bricks")
add(B, "cracked_stone_bricks")
add(B, "stone_brick_stairs")
add(B, "stone_brick_slab", "Slab", "Stone", base="stone_bricks")
add(B, "stone_brick_wall", "Wall", "Stone", base="stone_bricks")
cube(B, "chiseled_stone_bricks", "Stone")
add(B, "mossy_stone_bricks")
family(B, "mossy_stone_bricks", "Stone", wall=True, prefix="mossy_stone_brick")
for s in ["granite", "diorite", "andesite"]:
    cube(B, s, "Stone")
    family(B, s, "Stone", wall=True)
    cube(B, "polished_" + s, "Stone")
    family(B, "polished_" + s, "Stone")
pillar(B, "deepslate", "Deepslate", "deepslate", "deepslate_top")
cube(B, "cobbled_deepslate", "Deepslate")
family(B, "cobbled_deepslate", "Deepslate", wall=True)
cube(B, "chiseled_deepslate", "Deepslate")
cube(B, "polished_deepslate", "Deepslate")
family(B, "polished_deepslate", "Deepslate", wall=True)
cube(B, "deepslate_bricks", "Deepslate")
cube(B, "cracked_deepslate_bricks", "Deepslate")
family(B, "deepslate_bricks", "Deepslate", wall=True, prefix="deepslate_brick")
cube(B, "deepslate_tiles", "Deepslate")
cube(B, "cracked_deepslate_tiles", "Deepslate")
family(B, "deepslate_tiles", "Deepslate", wall=True, prefix="deepslate_tile")
cube(B, "reinforced_deepslate", "Reinforced", tex="reinforced_deepslate_side", top="reinforced_deepslate_top",
     bottom="reinforced_deepslate_bottom")
cube(B, "tuff", "Stone")
family(B, "tuff", "Stone", wall=True)
cube(B, "chiseled_tuff", "Stone", top="chiseled_tuff_top", bottom="chiseled_tuff_top")
cube(B, "polished_tuff", "Stone")
family(B, "polished_tuff", "Stone", wall=True)
cube(B, "tuff_bricks", "Stone")
family(B, "tuff_bricks", "Stone", wall=True, prefix="tuff_brick")
cube(B, "chiseled_tuff_bricks", "Stone", top="chiseled_tuff_bricks_top", bottom="chiseled_tuff_bricks_top")
add(B, "bricks")
add(B, "brick_stairs")
add(B, "brick_slab", "Slab", "Brick", base="bricks")
add(B, "brick_wall", "Wall", "Brick", base="bricks")
cube(B, "packed_mud", "Mud")
cube(B, "mud_bricks", "Mud")
family(B, "mud_bricks", "Mud", wall=True, prefix="mud_brick")
add(B, "sandstone")
family(B, "sandstone", "Sandstone", wall=True)
cube(B, "chiseled_sandstone", "Sandstone", top="sandstone_top", bottom="sandstone_top")
cube(B, "smooth_sandstone", "Sandstone", tex="sandstone_top")
family(B, "smooth_sandstone", "Sandstone")
cube(B, "cut_sandstone", "Sandstone", top="sandstone_top", bottom="sandstone_top")
add(B, "cut_sandstone_slab", "Slab", "Sandstone", base="cut_sandstone")
cube(B, "red_sandstone", "Sandstone", top="red_sandstone_top", bottom="red_sandstone_bottom")
family(B, "red_sandstone", "Sandstone", wall=True)
cube(B, "chiseled_red_sandstone", "Sandstone", top="red_sandstone_top", bottom="red_sandstone_top")
cube(B, "smooth_red_sandstone", "Sandstone", tex="red_sandstone_top")
family(B, "smooth_red_sandstone", "Sandstone")
cube(B, "cut_red_sandstone", "Sandstone", top="red_sandstone_top", bottom="red_sandstone_top")
add(B, "cut_red_sandstone_slab", "Slab", "Sandstone", base="cut_red_sandstone")
cube(B, "sea_lantern", "Lantern", flags=["LIGHT"])
cube(B, "prismarine", "Stone")
family(B, "prismarine", "Stone", wall=True)
cube(B, "prismarine_bricks", "Stone")
family(B, "prismarine_bricks", "Stone", prefix="prismarine_brick")
cube(B, "dark_prismarine", "Stone")
family(B, "dark_prismarine", "Stone")
add(B, "netherrack")
cube(B, "nether_bricks", "NetherBrick")
cube(B, "cracked_nether_bricks", "NetherBrick")
family(B, "nether_bricks", "NetherBrick", wall=True, prefix="nether_brick")
add(B, "nether_brick_fence", "Fence", "NetherBrick", tex="nether_bricks")
cube(B, "chiseled_nether_bricks", "NetherBrick")
cube(B, "red_nether_bricks", "NetherBrick")
family(B, "red_nether_bricks", "NetherBrick", wall=True, prefix="red_nether_brick")
pillar(B, "basalt", "Basalt", "basalt_side", "basalt_top")
cube(B, "smooth_basalt", "Basalt")
pillar(B, "polished_basalt", "Basalt", "polished_basalt_side", "polished_basalt_top")
cube(B, "blackstone", "Stone", top="blackstone_top", bottom="blackstone_top")
cube(B, "gilded_blackstone", "Stone")
family(B, "blackstone", "Stone", wall=True)
cube(B, "chiseled_polished_blackstone", "Stone")
cube(B, "polished_blackstone", "Stone")
family(B, "polished_blackstone", "Stone", wall=True)
add(B, "polished_blackstone_pressure_plate", "PressurePlate", "Stone", tex="polished_blackstone")
add(B, "polished_blackstone_button", "Button", "Stone", tex="polished_blackstone")
cube(B, "polished_blackstone_bricks", "Stone")
cube(B, "cracked_polished_blackstone_bricks", "Stone")
family(B, "polished_blackstone_bricks", "Stone", wall=True, prefix="polished_blackstone_brick")
cube(B, "end_stone", "EndStone")
cube(B, "end_stone_bricks", "EndStone")
family(B, "end_stone_bricks", "EndStone", wall=True, prefix="end_stone_brick")
cube(B, "purpur_block", "Stone")
pillar(B, "purpur_pillar", "Stone", "purpur_pillar", "purpur_pillar_top")
family(B, "purpur_block", "Stone", prefix="purpur")
cube(B, "coal_block", "Metal")
add(B, "iron_block")
add(B, "iron_bars")
add(B, "iron_door")
add(B, "iron_trapdoor", "Trapdoor", "Metal", tex="iron_trapdoor", flags=["IRON"])
add(B, "heavy_weighted_pressure_plate", "PressurePlate", "Metal", tex="iron_block", flags=["ANYTHING"])
add(B, "chain", "Chain", "Metal", tex="chain")
add(B, "gold_block")
add(B, "light_weighted_pressure_plate", "PressurePlate", "Metal", tex="gold_block", flags=["ANYTHING"])
cube(B, "redstone_block", "Metal", flags=["POWER"])
cube(B, "emerald_block", "Metal")
add(B, "lapis_block")
add(B, "diamond_block")
cube(B, "netherite_block", "Netherite")
cube(B, "quartz_block", "Quartz", tex="quartz_block_side", top="quartz_block_top", bottom="quartz_block_bottom")
cube(B, "chiseled_quartz_block", "Quartz", top="chiseled_quartz_block_top", bottom="chiseled_quartz_block_top")
pillar(B, "quartz_pillar", "Quartz", "quartz_pillar", "quartz_pillar_top")
family(B, "quartz_block", "Quartz", prefix="quartz")
cube(B, "smooth_quartz", "Quartz", tex="quartz_block_bottom")
family(B, "smooth_quartz", "Quartz")
cube(B, "quartz_bricks", "Quartz")
cube(B, "amethyst_block", "Amethyst")


def copper(waxed):
    for stage in ["", "exposed_", "weathered_", "oxidized_"]:
        w = "waxed_" if waxed else ""
        block = (w + stage + "copper") if stage else (w + "copper_block")
        cube(B, block, "Copper", tex=(stage + "copper") if stage else "copper_block")
        cube(B, w + stage + "chiseled_copper", "Copper", tex=stage + "chiseled_copper")
        cube(B, w + stage + "copper_grate", "Copper", tex=stage + "copper_grate", flags=["CUTOUT"])
        cube(B, w + stage + "cut_copper", "Copper", tex=stage + "cut_copper")
        family(B, w + stage + "cut_copper", "Copper")
        add(B, w + stage + "copper_door", "Door", "Copper", tex=stage + "copper_door_bottom", base=stage + "copper_door_top",
            item=stage + "copper_door")
        add(B, w + stage + "copper_trapdoor", "Trapdoor", "Copper", tex=stage + "copper_trapdoor")
        cube(B, w + stage + "copper_bulb", "Copper", tex=stage + "copper_bulb")


copper(False)
copper(True)

# ---- Colored Blocks (CreativeModeTabs.COLORED_BLOCKS, 1.21; shulker boxes, beds,
# candles and banners need models this game doesn't have yet).

C = colored
for c in COLORS:
    add(C, c + "_wool")
for c in COLORS:
    add(C, c + "_carpet", "Carpet", "Wool", tex=c + "_wool")
cube(C, "terracotta", "Terracotta")
for c in COLORS:
    cube(C, c + "_terracotta", "Terracotta")
for c in COLORS:
    cube(C, c + "_concrete", "Concrete")
for c in COLORS:
    add(C, c + "_concrete_powder", "Cube", "Powder", tex=c + "_concrete_powder", base=c + "_concrete", flags=["FALLING"])
for c in COLORS:
    add(C, c + "_glazed_terracotta", "Glazed", "Terracotta", tex=c + "_glazed_terracotta")
add(C, "glass")
cube(C, "tinted_glass", "Glass", flags=["TRANSLUCENT", "HIDE_SAME", "DARK"])
for c in COLORS:
    cube(C, c + "_stained_glass", "Glass", flags=["TRANSLUCENT", "HIDE_SAME"])
add(C, "glass_pane")
for c in COLORS:
    add(C, c + "_stained_glass_pane", "Pane", "Glass", tex=c + "_stained_glass", base=c + "_stained_glass_pane_top",
        flags=["TRANSLUCENT"])


# ---- Output

def textures_of(name, spec):
    out = []
    for key in ("tex", "top", "bottom"):
        if spec[key]:
            out.append("block/" + spec[key])
    if spec["kind"] == "Door":
        out.append("block/" + spec["base"])
        out.append("item/" + spec["item"])
    if spec["kind"] == "Pane":
        out.append("block/" + spec["base"])
    return out


def previous(section):
    """Names from the existing file's list, so their positions don't move."""
    if not os.path.exists(OUT):
        return []
    text = open(OUT).read()
    m = re.search(r"// BEGIN " + section + r"\n(.*?)// END " + section, text, re.S)
    return re.findall(r'^\s*\{?\s*"([^"]+)"', m.group(1), re.M) if m else []


def stable(old, new):
    keep = [n for n in old if n in new]
    gone = [n for n in old if n not in new]
    assert not gone, "names can't be removed (saves use their positions): %s" % gone
    return keep + [n for n in new if n not in keep]


block_order = stable(previous("BLOCKS"), list(blocks))
texture_order = stable(previous("TEXTURES"), [t for n in blocks for t in textures_of(n, blocks[n])])
texture_order = list(dict.fromkeys(texture_order))


def cstr(s):
    return '"%s"' % s if s else "nullptr"


lines = [
    "// Generated by tools/modern_blocks.py from 1.21's block list; edit that, not this.",
    "// Included by ModernBlocks.h.",
    "",
    "// Blocks after Beta, in ID order: ID = FIRST_MODERN_BLOCK + row. Append-only.",
    "inline constexpr ModernBlockSpec MODERN_BLOCKS[] = {",
    "// BEGIN BLOCKS",
]
for n in block_order:
    s = blocks[n]
    flags = " | ".join("MB_" + f for f in s["flags"]) or "0"
    lines.append('    { "%s", "%s", ModernKind::%s, ModernPreset::%s, %s, %s, %s, %s, %s, %s },' % (
        n, s["display"], s["kind"], s["preset"], cstr(s["tex"]), cstr(s["top"]), cstr(s["bottom"]), cstr(s["base"]),
        cstr(s["item"]), flags))
lines += ["// END BLOCKS", "};", "",
          "// Textures those blocks use (block/ or item/ in 1.21's textures), in tile order:",
          "// tile = LEGACY_TILES + row. Append-only.",
          "inline constexpr const char* MODERN_TEXTURES[] = {",
          "// BEGIN TEXTURES"]
lines += ['    "%s",' % t for t in texture_order]
lines += ["// END TEXTURES", "};", "",
          "// 1.21's Beta-era blocks and items as this game has them: (ID, metadata).",
          "inline constexpr BetaEquivalent BETA_EQUIVALENTS[] = {"]
lines += ['    { "%s", %s, %d },' % (n, i, m) for n, (i, m) in BETA.items()]
lines += ["};", "", "// The Building Blocks tab, in 1.21's order.", "inline constexpr const char* BUILDING_TAB[] = {"]
lines += ['    "%s",' % n for n in building]
lines += ["};", "", "// The Colored Blocks tab, in 1.21's order.", "inline constexpr const char* COLORED_TAB[] = {"]
lines += ['    "%s",' % n for n in colored]
lines += ["};", ""]
open(OUT, "w", newline="\n").write("\n".join(lines))
print("%d blocks, %d textures, %d building, %d colored -> %s" % (len(block_order), len(texture_order), len(building),
                                                                len(colored), OUT))
