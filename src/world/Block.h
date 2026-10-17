#pragma once
#include <cstdint>
#include "core/Math.h"
#include <string>

namespace qc {
using BlockId = uint8_t;
using ItemId = uint16_t;

enum Tex : uint8_t {
    T_STONE,
    T_DIRT,
    T_GRASS_TOP,
    T_GRASS_SIDE,
    T_COBBLE,
    T_PLANKS,
    T_BEDROCK,
    T_SAND,
    T_GRAVEL,
    T_LOG_SIDE,
    T_LOG_TOP,
    T_LEAVES,
    T_GLASS,
    T_COAL_ORE,
    T_IRON_ORE,
    T_GOLD_ORE,
    T_DIAMOND_ORE,
    T_WATER,
    T_LAVA,
    T_SNOW,
    T_SNOW_SIDE,
    T_ICE,
    T_CACTUS_SIDE,
    T_CACTUS_TOP,
    T_CLAY,
    T_SANDSTONE_SIDE,
    T_SANDSTONE_TOP,
    T_BRICKS,
    T_STONE_BRICKS,
    T_TNT_SIDE,
    T_TNT_TOP,
    T_TNT_BOTTOM,
    T_TORCH,
    T_GLOWSTONE,
    T_OBSIDIAN,
    T_TALL_GRASS,
    T_FLOWER_RED,
    T_FLOWER_YELLOW,
    T_BIRCH_SIDE,
    T_BIRCH_TOP,
    T_BIRCH_LEAVES,
    T_SPRUCE_SIDE,
    T_SPRUCE_TOP,
    T_SPRUCE_LEAVES,
    T_WOOL_WHITE,
    T_WOOL_RED,
    T_WOOL_ORANGE,
    T_WOOL_YELLOW,
    T_WOOL_LIME,
    T_WOOL_CYAN,
    T_WOOL_BLUE,
    T_WOOL_PURPLE,
    T_WOOL_BLACK,
    T_FIRE,
    T_MOSSY,
    T_GOLD_BLOCK,
    T_IRON_BLOCK,
    T_DIAMOND_BLOCK,
    T_BOOKSHELF,
    T_DEAD_BUSH,
    T_CRAFTING_TOP,
    T_CRAFTING_SIDE,
    T_RED_SAND,
    T_TERRACOTTA,
    T_TC_ORANGE,
    T_TC_YELLOW,
    T_TC_BROWN,
    T_TC_RED,
    T_TC_WHITE,
    T_TC_LIGHTGRAY,
    T_GRANITE,
    T_DIORITE,
    T_ANDESITE,
    T_DEEPSLATE,
    T_DEEPSLATE_TOP,
    T_TUFF,
    T_PODZOL_TOP,
    T_PODZOL_SIDE,
    T_COARSE_DIRT,
    T_PACKED_ICE,
    T_MUD,
    T_JUNGLE_SIDE,
    T_JUNGLE_TOP,
    T_JUNGLE_LEAVES,
    T_ACACIA_SIDE,
    T_ACACIA_TOP,
    T_ACACIA_LEAVES,
    T_DARK_OAK_SIDE,
    T_DARK_OAK_TOP,
    T_DARK_OAK_LEAVES,
    T_FERN,
    T_BLUE_FLOWER,
    T_SUGAR_CANE,
    T_MOSS,
    T_RED_SANDSTONE_SIDE,
    T_RED_SANDSTONE_TOP,
    T_CALCITE,
    T_DRIPSTONE,
    T_GLOWSHROOM,
    T_PROP_SIDE,
    T_PROP_TOP,
    T_BEAM_SIDE,
    T_SCORIA,
    T_EMBER,
    T_CRYSTAL,
    T_CRYSTAL_ROCK,
    T_FOSSIL,
    T_SALT,
    T_ASH,
    T_COPPER_ORE,
    T_CONCRETE,
    T_REBAR,
    T_RUST,
    T_GRATE,
    T_SCAFFOLD,
    T_CABLE,
    T_CABLE_LIVE,
    T_CELL_SIDE,
    T_CELL_TOP,
    T_DYNAMO,
    T_SWITCH_OFF,
    T_SWITCH_ON,
    T_PLATE,
    T_LAMP_OFF,
    T_LAMP_ON,
    T_RAM,
    T_DETONATOR,
    T_SPARKGAP,
    T_STICK,
    T_COAL,
    T_IRON_INGOT,
    T_GOLD_INGOT,
    T_DIAMOND,
    T_PORK,
    T_COOKED_PORK,
    T_APPLE,
    T_PICK_WOOD,
    T_PICK_STONE,
    T_PICK_IRON,
    T_PICK_DIAMOND,
    T_AXE_WOOD,
    T_AXE_STONE,
    T_AXE_IRON,
    T_AXE_DIAMOND,
    T_SHOVEL_WOOD,
    T_SHOVEL_STONE,
    T_SHOVEL_IRON,
    T_SHOVEL_DIAMOND,
    T_SWORD_WOOD,
    T_SWORD_STONE,
    T_SWORD_IRON,
    T_SWORD_DIAMOND,
    T_FLINT_STEEL,
    T_ROTTEN_FLESH,
    T_COPPER,
    T_PLAYER_SKIN,
    T_PLAYER_SHIRT,
    T_PLAYER_BOOT,
    T_PLAYER_PACK,
    T_SKIFF,
    T_GRAPPLE,
    T_ROPE,
    T_LANTERN,
    T_GLIDER,
    T_CRACK0,
    T_CRACK1,
    T_CRACK2,
    T_CRACK3,
    T_CRACK4,
    T_CRACK5,
    T_CRACK6,
    T_CRACK7,
    T_VINE,
    T_BRANCH,
    T_ROOT,
    T_PEBBLES,
    T_WHEAT,
    T_LONG_GRASS,
    T_LONG_GRASS_TOP,
    T_CATTAIL,
    T_LILY_PAD,
    T_CACTUS_FLOWER,
    T_BARREL_CACTUS,
    T_TRAVERTINE,
    T_GEYSER,
    T_PLANT_GREEN,
    T_PLANT_GRAIN,
    T_MUSHROOM_CAP,
    T_FIREFLY_PLANT,
    T_FIREFLY_BUD,
    T_AUTUMN_LEAVES,
    T_HEATHER,
    T_MUSHROOM_STEM,
    T_PFX_SMOKE0,
    T_PFX_SMOKE1,
    T_PFX_SMOKE2,
    T_PFX_SMOKE3,
    T_PFX_FIRE0,
    T_PFX_FIRE1,
    T_PFX_FIRE2,
    T_PFX_FIRE3,
    T_PFX_SPLASH0,
    T_PFX_SPLASH1,
    T_PFX_SPLASH2,
    T_PFX_SPLASH3,
    T_UI_MEMORY,
    T_UI_CAMERA,
    T_UI_PARTICLES,
    T_UI_DISPLAY,
    T_PFX_SPARK,
    T_PFX_MOTE,
    T_PFX_DROP,
    T_BASALT,
    T_POLISHED_BASALT,
    T_SULFUR,
    T_NITRE_ORE,
    T_RUNE,
    T_AMBER,
    T_THATCH,
    T_STRAW_SIDE,
    T_SLATE_TILE,
    T_RUBBER,
    T_LUMEN,
    T_AMBER_ITEM,
    T_SULFUR_ITEM,
    T_NITRE_ITEM,
    T_BONE,
    T_HIDE,
    T_RAW_GAME,
    T_ROAST_GAME,
    T_RAW_FISH,
    T_SMOKED_FISH,
    T_DYNAMITE,
    T_POULTICE,
    T_HAMMER,
    T_POWDER,
    T_COUNT
};

enum Block : BlockId {
    B_AIR = 0,
    B_STONE,
    B_GRASS,
    B_DIRT,
    B_COBBLE,
    B_PLANKS,
    B_BEDROCK,
    B_SAND,
    B_GRAVEL,
    B_LOG,
    B_LEAVES,
    B_GLASS,
    B_COAL_ORE,
    B_IRON_ORE,
    B_GOLD_ORE,
    B_DIAMOND_ORE,
    B_WATER,
    B_LAVA,
    B_SNOW,
    B_SNOWY_GRASS,
    B_ICE,
    B_CACTUS,
    B_CLAY,
    B_SANDSTONE,
    B_BRICKS,
    B_STONE_BRICKS,
    B_TNT,
    B_TORCH,
    B_GLOWSTONE,
    B_OBSIDIAN,
    B_TALL_GRASS,
    B_FLOWER_RED,
    B_FLOWER_YELLOW,
    B_BIRCH_LOG,
    B_BIRCH_LEAVES,
    B_SPRUCE_LOG,
    B_SPRUCE_LEAVES,
    B_WOOL_WHITE,
    B_WOOL_RED,
    B_WOOL_ORANGE,
    B_WOOL_YELLOW,
    B_WOOL_LIME,
    B_WOOL_CYAN,
    B_WOOL_BLUE,
    B_WOOL_PURPLE,
    B_WOOL_BLACK,
    B_FIRE,
    B_MOSSY,
    B_GOLD_BLOCK,
    B_IRON_BLOCK,
    B_DIAMOND_BLOCK,
    B_BOOKSHELF,
    B_DEAD_BUSH,
    B_CRAFTING,
    B_RED_SAND,
    B_TERRACOTTA,
    B_TC_ORANGE,
    B_TC_YELLOW,
    B_TC_BROWN,
    B_TC_RED,
    B_TC_WHITE,
    B_TC_LIGHTGRAY,
    B_GRANITE,
    B_DIORITE,
    B_ANDESITE,
    B_DEEPSLATE,
    B_TUFF,
    B_PODZOL,
    B_COARSE_DIRT,
    B_PACKED_ICE,
    B_MUD,
    B_JUNGLE_LOG,
    B_JUNGLE_LEAVES,
    B_ACACIA_LOG,
    B_ACACIA_LEAVES,
    B_DARK_OAK_LOG,
    B_DARK_OAK_LEAVES,
    B_FERN,
    B_BLUE_FLOWER,
    B_SUGAR_CANE,
    B_MOSS,
    B_RED_SANDSTONE,
    B_CALCITE,
    B_DRIPSTONE,
    B_GLOWSHROOM,
    B_PROP,
    B_BEAM,
    B_SCORIA,
    B_EMBER,
    B_CRYSTAL,
    B_CRYSTAL_ROCK,
    B_FOSSIL,
    B_SALT,
    B_ASH,
    B_COPPER_ORE,
    B_CONCRETE,
    B_REBAR,
    B_RUST,
    B_GRATE,
    B_SCAFFOLD,
    B_CABLE,
    B_CABLE_LIVE,
    B_CELL,
    B_DYNAMO,
    B_SWITCH,
    B_SWITCH_ON,
    B_PLATE,
    B_LAMP,
    B_LAMP_ON,
    B_RAM,
    B_DETONATOR,
    B_SPARKGAP,
    B_LANTERN,
    B_VINE,
    B_BRANCH,
    B_ROOT,
    B_PEBBLES,
    B_WILD_WHEAT,
    B_LONG_GRASS,
    B_LONG_GRASS_TOP,
    B_CATTAIL,
    B_LILY_PAD,
    B_CACTUS_FLOWER,
    B_BARREL_CACTUS,
    B_TRAVERTINE,
    B_GEYSER,
    B_FIREFLY_BUSH,
    B_AUTUMN_LEAVES,
    B_HEATHER,
    B_MUSHROOM_STEM,
    B_MUSHROOM_CAP,
    B_BASALT,
    B_POLISHED_BASALT,
    B_SULFUR,
    B_NITRE_ORE,
    B_RUNE_STONE,
    B_AMBER,
    B_THATCH,
    B_STRAW_BALE,
    B_SLATE_TILE,
    B_RUBBER,
    B_LUMEN_PANEL,
    B_COUNT
};

enum class Render : uint8_t { None, Opaque, Cutout, Leaves, Glass, Fluid, Cross, Model, Vine, Detail };
enum class LightKind : uint8_t { Transparent, Filter, Opaque };
enum class Sound : uint8_t { Stone, Wood, Grass, Sand, Gravel, Glass, Wool, Snow };
enum class ToolKind : uint8_t { None, Pick, Axe, Shovel, Sword };

struct BlockDef {
    const char* name = "air";
    uint8_t texTop = 0, texSide = 0, texBottom = 0;
    Render render = Render::None;
    LightKind light = LightKind::Transparent;
    uint8_t emit = 0;
    bool solid = false;
    bool collides = false;
    bool gravity = false;
    bool structural = false;
    bool replaceable = true;
    bool fluid = false;
    bool flammable = false;
    bool sideTopAligned = false;
    float hardness = 0;
    float density = 1.0f;
    float resistance = 0.5f;
    ItemId drop = 0;
    float dropChance = 1.0f;
    Sound sound = Sound::Stone;
    uint32_t mapColor = 0xff808080;
    uint8_t tint = 0;
};
enum TintClass : uint8_t { TINT_NONE = 0, TINT_GRASS = 1, TINT_FOLIAGE = 2, TINT_WATER = 3 };

extern BlockDef gBlocks[256];
void initBlocks();
const char* textureFile(int texture);
int textureByName(const std::string& name);
inline const BlockDef& def(BlockId b) { return gBlocks[b]; }
inline bool isSmallBlock(BlockId b) {
    auto r = def(b).render;
    return r == Render::Cross || r == Render::Model || r == Render::Vine || r == Render::Detail;
}
inline bool isPlantDetail(BlockId b) {
    auto r = def(b).render;
    return r == Render::Cross || r == Render::Vine || r == Render::Detail;
}
inline float geyserJet(int x, int z, float time) {
    uint32_t h = uint32_t(x) * 1664525u + uint32_t(z) * 1013904223u;
    float phase = std::fmod(time + float(h % 1800) / 100.0f, 18.0f);
    return phase < 3.5f ? std::min(1.0f, std::min(phase * 2.0f, (3.5f - phase) * 2.0f)) : 0.0f;
}

enum ItemCode : ItemId {
    I_FIRST = 256,
    I_STICK = 256,
    I_COAL,
    I_IRON_INGOT,
    I_GOLD_INGOT,
    I_DIAMOND,
    I_PORK,
    I_COOKED_PORK,
    I_APPLE,
    I_PICK_WOOD,
    I_PICK_STONE,
    I_PICK_IRON,
    I_PICK_DIAMOND,
    I_AXE_WOOD,
    I_AXE_STONE,
    I_AXE_IRON,
    I_AXE_DIAMOND,
    I_SHOVEL_WOOD,
    I_SHOVEL_STONE,
    I_SHOVEL_IRON,
    I_SHOVEL_DIAMOND,
    I_SWORD_WOOD,
    I_SWORD_STONE,
    I_SWORD_IRON,
    I_SWORD_DIAMOND,
    I_FLINT_STEEL,
    I_ROTTEN_FLESH,
    I_COPPER,
    I_SKIFF,
    I_GRAPPLE,
    I_ROPE,
    I_LANTERN,
    I_GLIDER,
    I_AMBER,
    I_SULFUR,
    I_NITRE,
    I_BONE,
    I_HIDE,
    I_RAW_GAME,
    I_ROAST_GAME,
    I_RAW_FISH,
    I_SMOKED_FISH,
    I_DYNAMITE,
    I_POULTICE,
    I_HAMMER,
    I_POWDER,
    I_END
};

struct ItemDef {
    const char* name = "";
    uint8_t tex = 0;
    int maxStack = 64;
    ToolKind tool = ToolKind::None;
    bool food = false;
};
const ItemDef& itemDef(ItemId id);
inline bool isBlockItem(ItemId id) { return id > 0 && id < 256; }
inline bool isPillar(BlockId b) {
    return b == B_LOG || b == B_BIRCH_LOG || b == B_SPRUCE_LOG || b == B_JUNGLE_LOG || b == B_ACACIA_LOG || b == B_DARK_OAK_LOG ||
           b == B_BEAM;
}
inline vec3 emissionColor(BlockId b) {
    switch (b) {
    case B_MUSHROOM_CAP:
    case B_GLOWSHROOM: return {0.24f, 1.45f, 1.32f};
    case B_CRYSTAL_ROCK:
    case B_CRYSTAL: return {0.55f, 0.88f, 1.70f};
    case B_LAVA:
    case B_EMBER: return {1.80f, 0.55f, 0.13f};
    case B_GLOWSTONE: return {1.55f, 1.36f, 0.72f};
    case B_LAMP:
    case B_LAMP_ON: return {1.55f, 1.40f, 1.08f};
    case B_LUMEN_PANEL: return {1.05f, 1.40f, 1.60f};
    case B_RUNE_STONE: return {0.30f, 1.30f, 1.15f};
    case B_AMBER: return {1.60f, 1.00f, 0.32f};
    default: return {1.70f, 1.02f, 0.38f};
    }
}
inline int emissionChannelLevel(float strength, int emission) {
    return std::clamp(int(std::lround(float(emission) + std::log(strength / 1.8f) / std::log(1.25f))), 0, 15);
}
inline float lightLevelBrightness(float level) {
    const float floor = std::pow(0.8f, 15.0f);
    return std::max(0.0f, (std::pow(0.8f, 15.0f - level) - floor) / (1.0f - floor)) * 1.80f;
}
inline vec3 emissionPeak(vec3 color, int emission) {
    return {lightLevelBrightness(float(emissionChannelLevel(color.x, emission))),
            lightLevelBrightness(float(emissionChannelLevel(color.y, emission))),
            lightLevelBrightness(float(emissionChannelLevel(color.z, emission)))};
}
std::string itemName(ItemId id);
inline bool retiredItem(ItemId id) {
    switch (id) {
    case I_PORK:
    case I_COOKED_PORK:
    case I_ROTTEN_FLESH:
    case I_RAW_GAME:
    case I_ROAST_GAME:
    case I_RAW_FISH:
    case I_SMOKED_FISH:
    case I_HIDE:
    case I_BONE: return true;
    default: return false;
    }
}
int maxStack(ItemId id);

}
