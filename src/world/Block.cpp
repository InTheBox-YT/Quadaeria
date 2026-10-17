#include "world/Block.h"
#include <initializer_list>
#include <string>

namespace qc {
BlockDef gBlocks[256];

namespace {
struct B {
    BlockDef d;
    B(const char* n, uint8_t tex) {
        d.name = n;
        d.texTop = d.texSide = d.texBottom = tex;
        d.render = Render::Opaque;
        d.light = LightKind::Opaque;
        d.solid = true;
        d.structural = true;
        d.replaceable = false;
        d.hardness = 1.5f;
        d.resistance = 3.0f;
    }
    B& tex(uint8_t top, uint8_t side, uint8_t bottom) {
        d.texTop = top;
        d.texSide = side;
        d.texBottom = bottom;
        return *this;
    }
    B& hard(float h, float res) {
        d.hardness = h;
        d.resistance = res;
        return *this;
    }
    B& snd(Sound s) {
        d.sound = s;
        return *this;
    }
    B& dens(float v) {
        d.density = v;
        return *this;
    }
    B& color(uint32_t c) {
        d.mapColor = c;
        return *this;
    }
    B& drop(ItemId i, float chance = 1.0f) {
        d.drop = i;
        d.dropChance = chance;
        return *this;
    }
    B& grav() {
        d.gravity = true;
        d.structural = false;
        return *this;
    }
    B& flam() {
        d.flammable = true;
        return *this;
    }
    B& emit(uint8_t e) {
        d.emit = e;
        return *this;
    }
    B& render(Render r) {
        d.render = r;
        return *this;
    }
    B& light(LightKind l) {
        d.light = l;
        return *this;
    }
    B& topAligned() {
        d.sideTopAligned = true;
        return *this;
    }
    B& tinted(uint8_t t) {
        d.tint = t;
        return *this;
    }
};
constexpr uint32_t C(uint8_t r, uint8_t g, uint8_t b) { return 0xff000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r; }

void set(BlockId id, const B& b) { gBlocks[id] = b.d; }

void plant(BlockId id, const char* name, uint8_t tex, uint32_t color) {
    BlockDef d;
    d.name = name;
    d.texTop = d.texSide = d.texBottom = tex;
    d.render = Render::Cross;
    d.light = LightKind::Transparent;
    d.solid = false;
    d.replaceable = true;
    d.hardness = 0;
    d.resistance = 0;
    d.sound = Sound::Grass;
    d.flammable = true;
    d.mapColor = color;
    d.density = 0.5f;
    gBlocks[id] = d;
}
}

void initBlocks() {
    BlockDef air;
    air.name = "Air";
    air.hardness = 0;
    air.resistance = 0;
    for (auto& g : gBlocks) g = air;

    set(B_STONE, B("Stone", T_STONE).hard(1.5f, 3).drop(B_COBBLE).dens(2.6f).color(C(125, 125, 125)));
    set(B_GRASS, B("Grass", T_GRASS_SIDE)
                     .tex(T_GRASS_TOP, T_GRASS_SIDE, T_DIRT)
                     .topAligned()
                     .hard(0.6f, 0.6f)
                     .drop(B_DIRT)
                     .snd(Sound::Grass)
                     .dens(1.5f)
                     .color(C(95, 150, 60))
                     .tinted(TINT_GRASS));
    set(B_DIRT, B("Dirt", T_DIRT).hard(0.5f, 0.5f).snd(Sound::Gravel).dens(1.5f).color(C(134, 96, 67)));
    set(B_COBBLE, B("Cobblestone", T_COBBLE).hard(2.0f, 3).dens(2.4f).color(C(115, 115, 115)));
    set(B_PLANKS, B("Planks", T_PLANKS).hard(2.0f, 1.5f).snd(Sound::Wood).dens(0.6f).flam().color(C(170, 135, 80)));
    set(B_BEDROCK, B("Bedrock", T_BEDROCK).hard(-1, 1e6f).dens(3.0f).color(C(60, 60, 60)));
    set(B_SAND, B("Sand", T_SAND).grav().hard(0.5f, 0.5f).snd(Sound::Sand).dens(1.6f).color(C(219, 207, 160)));
    set(B_GRAVEL, B("Gravel", T_GRAVEL).grav().hard(0.6f, 0.6f).snd(Sound::Gravel).dens(1.8f).color(C(130, 124, 122)));
    set(B_LOG, B("Oak Log", T_LOG_SIDE)
                   .tex(T_LOG_TOP, T_LOG_SIDE, T_LOG_TOP)
                   .hard(2.0f, 2)
                   .snd(Sound::Wood)
                   .dens(0.7f)
                   .flam()
                   .color(C(102, 81, 50)));
    set(B_LEAVES, B("Leaves", T_LEAVES)
                      .render(Render::Leaves)
                      .light(LightKind::Filter)
                      .hard(0.2f, 0.2f)
                      .snd(Sound::Grass)
                      .drop(I_APPLE, 0.04f)
                      .dens(0.12f)
                      .flam()
                      .color(C(60, 110, 40))
                      .tinted(TINT_FOLIAGE));
    set(B_GLASS, B("Glass", T_GLASS)
                     .render(Render::Glass)
                     .light(LightKind::Transparent)
                     .hard(0.3f, 0.3f)
                     .snd(Sound::Glass)
                     .drop(0, 0)
                     .dens(2.5f)
                     .color(C(200, 230, 240)));
    set(B_COAL_ORE, B("Coal Ore", T_COAL_ORE).hard(3, 3).drop(I_COAL).dens(2.6f).color(C(90, 90, 90)));
    set(B_IRON_ORE, B("Iron Ore", T_IRON_ORE).hard(3, 3).dens(3.0f).color(C(150, 130, 115)));
    set(B_GOLD_ORE, B("Gold Ore", T_GOLD_ORE).hard(3, 3).dens(3.5f).color(C(170, 160, 100)));
    set(B_DIAMOND_ORE, B("Diamond Ore", T_DIAMOND_ORE).hard(3, 3).drop(I_DIAMOND).dens(3.0f).color(C(120, 170, 170)));

    BlockDef water;
    water.name = "Water";
    water.texTop = water.texSide = water.texBottom = T_WATER;
    water.render = Render::Fluid;
    water.light = LightKind::Filter;
    water.fluid = true;
    water.replaceable = true;
    water.hardness = -1;
    water.resistance = 100;
    water.density = 1.0f;
    water.mapColor = C(50, 90, 200);
    water.tint = TINT_WATER;
    gBlocks[B_WATER] = water;
    BlockDef lava = water;
    lava.tint = TINT_NONE;
    lava.name = "Lava";
    lava.texTop = lava.texSide = lava.texBottom = T_LAVA;
    lava.light = LightKind::Transparent;
    lava.emit = 15;
    lava.density = 3.0f;
    lava.mapColor = C(230, 100, 20);
    gBlocks[B_LAVA] = lava;

    set(B_SNOW, B("Snow", T_SNOW).hard(0.3f, 0.2f).snd(Sound::Snow).dens(0.5f).color(C(240, 250, 250)));
    set(B_SNOWY_GRASS, B("Snowy Grass", T_SNOW_SIDE)
                           .tex(T_SNOW, T_SNOW_SIDE, T_DIRT)
                           .topAligned()
                           .hard(0.6f, 0.6f)
                           .drop(B_DIRT)
                           .snd(Sound::Snow)
                           .dens(1.5f)
                           .color(C(230, 240, 240)));
    set(B_ICE, B("Ice", T_ICE)
                   .render(Render::Glass)
                   .light(LightKind::Filter)
                   .hard(0.5f, 0.5f)
                   .snd(Sound::Glass)
                   .drop(0, 0)
                   .dens(0.9f)
                   .color(C(160, 190, 250)));
    set(B_CACTUS, B("Cactus", T_CACTUS_SIDE)
                      .tex(T_CACTUS_TOP, T_CACTUS_SIDE, T_CACTUS_TOP)
                      .hard(0.4f, 0.4f)
                      .snd(Sound::Wool)
                      .dens(0.8f)
                      .color(C(20, 120, 30)));
    set(B_CLAY, B("Clay", T_CLAY).hard(0.6f, 0.6f).snd(Sound::Gravel).dens(1.8f).color(C(160, 166, 180)));
    set(B_SANDSTONE, B("Sandstone", T_SANDSTONE_SIDE)
                         .tex(T_SANDSTONE_TOP, T_SANDSTONE_SIDE, T_SANDSTONE_TOP)
                         .hard(0.8f, 0.8f)
                         .dens(2.2f)
                         .color(C(215, 200, 150)));
    set(B_BRICKS, B("Bricks", T_BRICKS).hard(2, 6).dens(2.0f).color(C(150, 80, 65)));
    set(B_STONE_BRICKS, B("Stone Bricks", T_STONE_BRICKS).hard(1.5f, 6).dens(2.5f).color(C(120, 120, 120)));
    set(B_TNT, B("TNT", T_TNT_SIDE)
                   .tex(T_TNT_TOP, T_TNT_SIDE, T_TNT_BOTTOM)
                   .hard(0.1f, 0)
                   .snd(Sound::Grass)
                   .dens(1.2f)
                   .flam()
                   .color(C(200, 50, 40)));

    BlockDef torch;
    torch.name = "Torch";
    torch.texTop = torch.texSide = torch.texBottom = T_TORCH;
    torch.render = Render::Cross;
    torch.emit = 14;
    torch.hardness = 0;
    torch.resistance = 0;
    torch.replaceable = false;
    torch.sound = Sound::Wood;
    torch.mapColor = C(255, 210, 90);
    gBlocks[B_TORCH] = torch;

    set(B_GLOWSTONE, B("Glowstone", T_GLOWSTONE)
                         .light(LightKind::Opaque)
                         .emit(15)
                         .hard(0.3f, 0.3f)
                         .snd(Sound::Glass)
                         .dens(1.0f)
                         .color(C(250, 210, 120)));
    set(B_OBSIDIAN, B("Obsidian", T_OBSIDIAN).hard(25, 1200).dens(2.8f).color(C(30, 20, 45)));
    plant(B_TALL_GRASS, "Tall Grass", T_TALL_GRASS, C(90, 150, 60));
    gBlocks[B_TALL_GRASS].drop = 0;
    gBlocks[B_TALL_GRASS].dropChance = 0;
    gBlocks[B_TALL_GRASS].tint = TINT_GRASS;
    plant(B_FLOWER_RED, "Rose", T_FLOWER_RED, C(200, 40, 40));
    plant(B_FLOWER_YELLOW, "Dandelion", T_FLOWER_YELLOW, C(240, 220, 40));
    plant(B_DEAD_BUSH, "Dead Bush", T_DEAD_BUSH, C(140, 100, 50));
    gBlocks[B_DEAD_BUSH].dropChance = 0;
    gBlocks[B_DEAD_BUSH].drop = I_STICK;

    set(B_BIRCH_LOG, B("Birch Log", T_BIRCH_SIDE)
                         .tex(T_BIRCH_TOP, T_BIRCH_SIDE, T_BIRCH_TOP)
                         .hard(2, 2)
                         .snd(Sound::Wood)
                         .dens(0.65f)
                         .flam()
                         .color(C(215, 215, 205)));
    set(B_BIRCH_LEAVES, B("Birch Leaves", T_BIRCH_LEAVES)
                            .render(Render::Leaves)
                            .light(LightKind::Filter)
                            .hard(0.2f, 0.2f)
                            .snd(Sound::Grass)
                            .drop(I_APPLE, 0.02f)
                            .dens(0.12f)
                            .flam()
                            .color(C(100, 140, 60)));
    set(B_SPRUCE_LOG, B("Spruce Log", T_SPRUCE_SIDE)
                          .tex(T_SPRUCE_TOP, T_SPRUCE_SIDE, T_SPRUCE_TOP)
                          .hard(2, 2)
                          .snd(Sound::Wood)
                          .dens(0.6f)
                          .flam()
                          .color(C(80, 58, 35)));
    set(B_SPRUCE_LEAVES, B("Spruce Leaves", T_SPRUCE_LEAVES)
                             .render(Render::Leaves)
                             .light(LightKind::Filter)
                             .hard(0.2f, 0.2f)
                             .snd(Sound::Grass)
                             .drop(0, 0)
                             .dens(0.12f)
                             .flam()
                             .color(C(50, 80, 50)));
    const char* woolNames[] = {"White Wool", "Red Wool",  "Orange Wool", "Yellow Wool", "Lime Wool",
                               "Cyan Wool",  "Blue Wool", "Purple Wool", "Black Wool"};
    const uint32_t woolCols[] = {C(235, 235, 235), C(180, 50, 45), C(230, 125, 40), C(225, 205, 50), C(110, 190, 40),
                                 C(40, 140, 150),  C(50, 70, 170), C(130, 60, 180), C(30, 30, 35)};
    for (int i = 0; i < 9; ++i)
        set(BlockId(B_WOOL_WHITE + i),
            B(woolNames[i], uint8_t(T_WOOL_WHITE + i)).hard(0.8f, 0.8f).snd(Sound::Wool).dens(0.3f).flam().color(woolCols[i]));

    BlockDef fire;
    fire.name = "Fire";
    fire.texTop = fire.texSide = fire.texBottom = T_FIRE;
    fire.render = Render::Cross;
    fire.emit = 15;
    fire.hardness = 0;
    fire.resistance = 0;
    fire.drop = 0;
    fire.dropChance = 0;
    fire.mapColor = C(250, 150, 30);
    gBlocks[B_FIRE] = fire;

    set(B_MOSSY, B("Mossy Cobblestone", T_MOSSY).hard(2, 3).dens(2.4f).color(C(100, 120, 100)));
    set(B_GOLD_BLOCK, B("Gold Block", T_GOLD_BLOCK).hard(3, 6).dens(9.0f).color(C(250, 220, 70)));
    set(B_IRON_BLOCK, B("Iron Block", T_IRON_BLOCK).hard(5, 6).dens(7.0f).color(C(220, 220, 220)));
    set(B_DIAMOND_BLOCK, B("Diamond Block", T_DIAMOND_BLOCK).hard(5, 6).dens(3.5f).color(C(100, 220, 210)));
    set(B_BOOKSHELF, B("Bookshelf", T_BOOKSHELF)
                         .tex(T_PLANKS, T_BOOKSHELF, T_PLANKS)
                         .hard(1.5f, 1.5f)
                         .snd(Sound::Wood)
                         .dens(0.7f)
                         .flam()
                         .color(C(140, 100, 60)));
    set(B_CRAFTING, B("Crafting Table", T_CRAFTING_SIDE)
                        .tex(T_CRAFTING_TOP, T_CRAFTING_SIDE, T_PLANKS)
                        .hard(2.5f, 2.5f)
                        .snd(Sound::Wood)
                        .dens(0.7f)
                        .flam()
                        .color(C(150, 110, 70)));

    set(B_RED_SAND, B("Red Sand", T_RED_SAND).grav().hard(0.5f, 0.5f).snd(Sound::Sand).dens(1.6f).color(C(190, 102, 33)));
    const char* tcNames[] = {"Terracotta",     "Orange Terracotta", "Yellow Terracotta",    "Brown Terracotta",
                             "Red Terracotta", "White Terracotta",  "Light Gray Terracotta"};
    const uint32_t tcCols[] = {C(152, 94, 67), C(161, 83, 37),   C(186, 133, 35), C(77, 51, 35),
                               C(143, 61, 46), C(209, 178, 161), C(135, 107, 98)};
    for (int i = 0; i < 7; ++i)
        set(BlockId(B_TERRACOTTA + i), B(tcNames[i], uint8_t(T_TERRACOTTA + i)).hard(1.25f, 4.2f).dens(2.2f).color(tcCols[i]));
    set(B_GRANITE, B("Granite", T_GRANITE).hard(1.5f, 3).dens(2.7f).color(C(149, 103, 85)));
    set(B_DIORITE, B("Diorite", T_DIORITE).hard(1.5f, 3).dens(2.7f).color(C(188, 188, 188)));
    set(B_ANDESITE, B("Andesite", T_ANDESITE).hard(1.5f, 3).dens(2.6f).color(C(136, 136, 137)));
    set(B_DEEPSLATE,
        B("Deepslate", T_DEEPSLATE).tex(T_DEEPSLATE_TOP, T_DEEPSLATE, T_DEEPSLATE_TOP).hard(3, 6).dens(3.0f).color(C(80, 80, 82)));
    set(B_TUFF, B("Tuff", T_TUFF).hard(1.5f, 6).dens(2.4f).color(C(108, 109, 102)));
    set(B_PODZOL, B("Podzol", T_PODZOL_SIDE)
                      .tex(T_PODZOL_TOP, T_PODZOL_SIDE, T_DIRT)
                      .topAligned()
                      .hard(0.5f, 0.5f)
                      .drop(B_DIRT)
                      .snd(Sound::Gravel)
                      .dens(1.5f)
                      .color(C(91, 63, 24)));
    set(B_COARSE_DIRT, B("Coarse Dirt", T_COARSE_DIRT).hard(0.5f, 0.5f).snd(Sound::Gravel).dens(1.6f).color(C(119, 85, 59)));
    set(B_PACKED_ICE, B("Packed Ice", T_PACKED_ICE).hard(0.5f, 0.5f).snd(Sound::Glass).drop(0, 0).dens(0.9f).color(C(141, 180, 250)));
    set(B_MUD, B("Mud", T_MUD).hard(0.5f, 0.5f).snd(Sound::Gravel).dens(1.7f).color(C(60, 57, 61)));
    set(B_JUNGLE_LOG, B("Jungle Log", T_JUNGLE_SIDE)
                          .tex(T_JUNGLE_TOP, T_JUNGLE_SIDE, T_JUNGLE_TOP)
                          .hard(2, 2)
                          .snd(Sound::Wood)
                          .dens(0.7f)
                          .flam()
                          .color(C(85, 67, 25)));
    set(B_JUNGLE_LEAVES, B("Jungle Leaves", T_JUNGLE_LEAVES)
                             .render(Render::Leaves)
                             .light(LightKind::Filter)
                             .hard(0.2f, 0.2f)
                             .snd(Sound::Grass)
                             .drop(0, 0)
                             .dens(0.12f)
                             .flam()
                             .color(C(48, 120, 30))
                             .tinted(TINT_FOLIAGE));
    set(B_ACACIA_LOG, B("Acacia Log", T_ACACIA_SIDE)
                          .tex(T_ACACIA_TOP, T_ACACIA_SIDE, T_ACACIA_TOP)
                          .hard(2, 2)
                          .snd(Sound::Wood)
                          .dens(0.7f)
                          .flam()
                          .color(C(103, 96, 86)));
    set(B_ACACIA_LEAVES, B("Acacia Leaves", T_ACACIA_LEAVES)
                             .render(Render::Leaves)
                             .light(LightKind::Filter)
                             .hard(0.2f, 0.2f)
                             .snd(Sound::Grass)
                             .drop(0, 0)
                             .dens(0.12f)
                             .flam()
                             .color(C(90, 120, 40))
                             .tinted(TINT_FOLIAGE));
    set(B_DARK_OAK_LOG, B("Dark Oak Log", T_DARK_OAK_SIDE)
                            .tex(T_DARK_OAK_TOP, T_DARK_OAK_SIDE, T_DARK_OAK_TOP)
                            .hard(2, 2)
                            .snd(Sound::Wood)
                            .dens(0.75f)
                            .flam()
                            .color(C(60, 46, 26)));
    set(B_DARK_OAK_LEAVES, B("Dark Oak Leaves", T_DARK_OAK_LEAVES)
                               .render(Render::Leaves)
                               .light(LightKind::Filter)
                               .hard(0.2f, 0.2f)
                               .snd(Sound::Grass)
                               .drop(I_APPLE, 0.03f)
                               .dens(0.12f)
                               .flam()
                               .color(C(40, 80, 25))
                               .tinted(TINT_FOLIAGE));
    plant(B_FERN, "Fern", T_FERN, C(70, 120, 50));
    gBlocks[B_FERN].drop = 0;
    gBlocks[B_FERN].dropChance = 0;
    gBlocks[B_FERN].tint = TINT_GRASS;
    plant(B_BLUE_FLOWER, "Cornflower", T_BLUE_FLOWER, C(70, 100, 220));
    plant(B_SUGAR_CANE, "Sugar Cane", T_SUGAR_CANE, C(150, 200, 100));
    set(B_MOSS, B("Moss Block", T_MOSS).hard(0.1f, 0.1f).snd(Sound::Grass).dens(1.0f).color(C(89, 109, 45)));
    set(B_RED_SANDSTONE, B("Red Sandstone", T_RED_SANDSTONE_SIDE)
                             .tex(T_RED_SANDSTONE_TOP, T_RED_SANDSTONE_SIDE, T_RED_SANDSTONE_TOP)
                             .hard(0.8f, 0.8f)
                             .dens(2.2f)
                             .color(C(186, 99, 29)));
    set(B_CALCITE, B("Calcite", T_CALCITE).hard(0.75f, 0.75f).dens(2.7f).color(C(223, 224, 220)));
    set(B_DRIPSTONE, B("Dripstone", T_DRIPSTONE).hard(1.0f, 1.5f).dens(2.4f).color(C(134, 107, 92)));
    plant(B_GLOWSHROOM, "Glow Mushroom", T_GLOWSHROOM, C(90, 200, 190));
    gBlocks[B_GLOWSHROOM].emit = 10;
    gBlocks[B_GLOWSHROOM].flammable = false;
    set(B_PROP, B("Timber Prop", T_PROP_SIDE)
                    .tex(T_PROP_TOP, T_PROP_SIDE, T_PROP_TOP)
                    .hard(1.5f, 2.5f)
                    .snd(Sound::Wood)
                    .dens(0.75f)
                    .flam()
                    .color(C(110, 90, 60)));
    set(B_BEAM, B("Timber Beam", T_BEAM_SIDE)
                    .tex(T_PROP_TOP, T_BEAM_SIDE, T_PROP_TOP)
                    .hard(1.5f, 2.5f)
                    .snd(Sound::Wood)
                    .dens(0.75f)
                    .flam()
                    .color(C(110, 90, 60)));
    set(B_SCORIA, B("Scoria", T_SCORIA).hard(1.2f, 4).dens(1.6f).color(C(70, 60, 58)));
    set(B_EMBER, B("Ember Rock", T_EMBER).hard(1.5f, 4).dens(2.2f).emit(7).color(C(200, 90, 40)));
    plant(B_CRYSTAL, "Glow Crystal", T_CRYSTAL, C(170, 225, 235));
    gBlocks[B_CRYSTAL].emit = 9;
    gBlocks[B_CRYSTAL].flammable = false;
    gBlocks[B_CRYSTAL].sound = Sound::Glass;
    gBlocks[B_CRYSTAL].hardness = 0.3f;
    set(B_CRYSTAL_ROCK, B("Crystal Geode", T_CRYSTAL_ROCK).hard(2, 5).dens(2.6f).emit(4).snd(Sound::Glass).color(C(160, 200, 210)));
    set(B_FOSSIL, B("Fossil Stone", T_FOSSIL).hard(2, 4).dens(2.4f).color(C(200, 195, 180)));
    set(B_SALT, B("Salt Crust", T_SALT).hard(0.6f, 1).snd(Sound::Sand).dens(2.1f).color(C(230, 228, 220)));
    set(B_ASH, B("Ash", T_ASH).grav().hard(0.4f, 0.4f).snd(Sound::Sand).dens(0.7f).color(C(80, 80, 82)));
    set(B_COPPER_ORE, B("Copper Ore", T_COPPER_ORE).hard(3, 3).dens(2.9f).color(C(160, 110, 80)));
    set(B_CONCRETE, B("Concrete", T_CONCRETE).hard(2.5f, 6).dens(2.4f).color(C(150, 150, 148)));
    set(B_REBAR, B("Reinforced Concrete", T_REBAR).hard(4, 9).dens(2.6f).color(C(140, 138, 135)));
    set(B_RUST, B("Rusted Plate", T_RUST).hard(3, 6).dens(7.0f).color(C(120, 70, 45)));
    set(B_GRATE, B("Iron Grating", T_GRATE).render(Render::Glass).light(LightKind::Transparent).hard(2, 5).dens(3.0f).color(C(60, 60, 64)));
    set(B_SCAFFOLD, B("Scaffolding", T_SCAFFOLD)
                        .render(Render::Glass)
                        .light(LightKind::Transparent)
                        .hard(0.5f, 0.5f)
                        .snd(Sound::Wood)
                        .dens(0.3f)
                        .flam()
                        .color(C(140, 110, 70)));
    set(B_CABLE, B("Copper Cable", T_CABLE).hard(0.6f, 1).dens(4.0f).color(C(170, 100, 60)));
    set(B_CABLE_LIVE, B("Copper Cable", T_CABLE_LIVE).hard(0.6f, 1).dens(4.0f).emit(3).drop(B_CABLE).color(C(220, 150, 90)));
    set(B_CELL, B("Voltaic Cell", T_CELL_SIDE).tex(T_CELL_TOP, T_CELL_SIDE, T_CELL_TOP).hard(1, 3).dens(3.0f).color(C(70, 75, 80)));
    set(B_DYNAMO, B("Water Dynamo", T_DYNAMO).hard(2, 3).snd(Sound::Wood).dens(1.2f).color(C(110, 90, 60)));
    set(B_SWITCH, B("Knife Switch", T_SWITCH_OFF).hard(0.5f, 1).dens(2.0f).color(C(80, 80, 85)));
    set(B_SWITCH_ON, B("Knife Switch", T_SWITCH_ON).hard(0.5f, 1).dens(2.0f).drop(B_SWITCH).color(C(80, 80, 85)));
    set(B_PLATE, B("Load Plate", T_PLATE).hard(1, 3).dens(6.0f).color(C(110, 110, 115)));
    set(B_LAMP, B("Arc Lamp", T_LAMP_OFF).hard(0.4f, 0.4f).snd(Sound::Glass).dens(1.5f).color(C(150, 150, 140)));
    set(B_LAMP_ON, B("Arc Lamp", T_LAMP_ON).hard(0.4f, 0.4f).snd(Sound::Glass).dens(1.5f).emit(14).drop(B_LAMP).color(C(240, 230, 200)));
    set(B_RAM, B("Ram", T_RAM).hard(2, 4).dens(5.0f).color(C(100, 100, 100)));
    set(B_DETONATOR, B("Detonator", T_DETONATOR).hard(1, 1).dens(1.5f).color(C(150, 60, 50)));
    set(B_SPARKGAP, B("Spark Gap", T_SPARKGAP).hard(1, 2).dens(3.0f).color(C(90, 90, 100)));
    set(B_LANTERN, B("Storm Lantern", T_LANTERN).render(Render::Model).light(LightKind::Transparent).hard(0.3f, 0.4f).snd(Sound::Glass));
    gBlocks[B_LANTERN].solid = false;
    gBlocks[B_LANTERN].structural = false;
    gBlocks[B_LANTERN].replaceable = false;
    gBlocks[B_LANTERN].emit = 14;
    gBlocks[B_LANTERN].drop = I_LANTERN;
    for (int id = B_CABLE; id <= B_SPARKGAP; ++id) {
        auto& d = gBlocks[id];
        d.render = Render::Model;
        d.solid = false;
        d.structural = false;
        d.light = LightKind::Transparent;
        d.replaceable = false;
    }
    plant(B_VINE, "Trailing Vines", T_VINE, C(74, 129, 57));
    gBlocks[B_VINE].render = Render::Vine;
    gBlocks[B_VINE].tint = TINT_FOLIAGE;
    plant(B_BRANCH, "Fallen Branch", T_BRANCH, C(116, 85, 51));
    plant(B_ROOT, "Exposed Roots", T_ROOT, C(110, 82, 50));
    plant(B_PEBBLES, "Pebbles", T_PEBBLES, C(136, 135, 120));
    gBlocks[B_PEBBLES].flammable = false;
    gBlocks[B_PEBBLES].sound = Sound::Gravel;
    plant(B_WILD_WHEAT, "Wild Wheat", T_WHEAT, C(189, 167, 76));
    plant(B_LONG_GRASS, "Meadow Grass", T_LONG_GRASS, C(96, 145, 55));
    gBlocks[B_LONG_GRASS].tint = TINT_GRASS;
    plant(B_LONG_GRASS_TOP, "Meadow Grass Tips", T_LONG_GRASS_TOP, C(109, 157, 64));
    gBlocks[B_LONG_GRASS_TOP].tint = TINT_GRASS;
    gBlocks[B_LONG_GRASS_TOP].dropChance = 0;
    plant(B_FIREFLY_BUSH, "Firefly Bush", T_FIREFLY_PLANT, C(116, 156, 74));
    plant(B_CATTAIL, "Cattail", T_CATTAIL, C(103, 135, 54));
    plant(B_LILY_PAD, "Lily Pad", T_LILY_PAD, C(69, 132, 66));
    gBlocks[B_LILY_PAD].tint = TINT_FOLIAGE;
    plant(B_CACTUS_FLOWER, "Cactus Blossom", T_CACTUS_FLOWER, C(227, 115, 137));
    plant(B_BARREL_CACTUS, "Barrel Cactus", T_BARREL_CACTUS, C(104, 148, 63));
    for (BlockId b : {B_BRANCH, B_ROOT, B_PEBBLES, B_LILY_PAD, B_BARREL_CACTUS}) gBlocks[b].render = Render::Detail;
    set(B_TRAVERTINE, B("Travertine", T_TRAVERTINE).hard(1.3f, 2.5f).dens(2.3f).color(C(206, 188, 141)));
    set(B_GEYSER, B("Geyser Vent", T_GEYSER).tex(T_GEYSER, T_TRAVERTINE, T_TRAVERTINE).hard(2.0f, 4).dens(2.6f).color(C(191, 170, 125)));
    set(B_AUTUMN_LEAVES, B("Amber Leaves", T_AUTUMN_LEAVES)
                             .render(Render::Leaves)
                             .light(LightKind::Filter)
                             .hard(.2f, .2f)
                             .snd(Sound::Grass)
                             .dens(.12f)
                             .flam()
                             .color(C(188, 112, 43)));
    plant(B_HEATHER, "Heather", T_HEATHER, C(168, 105, 164));
    set(B_MUSHROOM_STEM, B("Cave Mushroom Stem", T_MUSHROOM_STEM).hard(.5f, .4f).dens(.35f).snd(Sound::Wood).color(C(134, 171, 154)));
    set(B_MUSHROOM_CAP,
        B("Luminous Mushroom Cap", T_MUSHROOM_CAP).hard(.3f, .3f).dens(.25f).emit(6).snd(Sound::Grass).color(C(61, 157, 146)));
    set(B_BASALT, B("Basalt", T_BASALT).hard(2.2f, 5).dens(3.0f).color(C(64, 66, 74)));
    set(B_POLISHED_BASALT, B("Polished Basalt", T_POLISHED_BASALT).hard(2.4f, 6).dens(3.0f).color(C(58, 60, 70)));
    set(B_SULFUR, B("Sulfur Deposit", T_SULFUR).hard(1.0f, 1.5f).drop(I_SULFUR).dens(2.0f).color(C(222, 196, 52)));
    set(B_NITRE_ORE, B("Nitre Ore", T_NITRE_ORE).hard(2.5f, 3).drop(I_NITRE).dens(2.6f).color(C(190, 200, 210)));
    set(B_RUNE_STONE, B("Rune Stone", T_RUNE).hard(3.0f, 8).emit(9).dens(2.8f).color(C(70, 190, 170)));
    set(B_AMBER, B("Amber", T_AMBER)
                     .render(Render::Glass)
                     .light(LightKind::Transparent)
                     .emit(5)
                     .hard(0.8f, 0.8f)
                     .snd(Sound::Glass)
                     .drop(I_AMBER)
                     .dens(1.1f)
                     .color(C(226, 150, 40)));
    set(B_THATCH, B("Thatch", T_THATCH).hard(0.4f, 0.4f).snd(Sound::Grass).dens(0.2f).flam().color(C(196, 170, 84)));
    set(B_STRAW_BALE, B("Straw Bale", T_STRAW_SIDE)
                          .tex(T_THATCH, T_STRAW_SIDE, T_THATCH)
                          .hard(0.5f, 0.5f)
                          .snd(Sound::Grass)
                          .dens(0.3f)
                          .flam()
                          .color(C(210, 180, 90)));
    set(B_SLATE_TILE, B("Slate Roofing", T_SLATE_TILE).hard(1.2f, 3).dens(2.6f).color(C(84, 94, 110)));
    set(B_RUBBER, B("Rubber Block", T_RUBBER).hard(0.6f, 1).snd(Sound::Wool).dens(1.1f).color(C(190, 60, 64)));
    set(B_LUMEN_PANEL,
        B("Lumen Panel", T_LUMEN).light(LightKind::Opaque).emit(14).hard(0.4f, 0.4f).snd(Sound::Glass).dens(1.6f).color(C(190, 240, 250)));
    for (auto& g : gBlocks) {
        g.collides = g.solid && g.render != Render::Leaves;
        if (g.render == Render::Leaves) {
            g.structural = false;
            g.replaceable = true;
        }
    }
}

namespace {
ItemDef makeItems(int i) {
    ItemDef d;
    switch (i + I_FIRST) {
    case I_STICK: d = {"Stick", T_STICK}; break;
    case I_COAL: d = {"Coal", T_COAL}; break;
    case I_IRON_INGOT: d = {"Iron Ingot", T_IRON_INGOT}; break;
    case I_GOLD_INGOT: d = {"Gold Ingot", T_GOLD_INGOT}; break;
    case I_DIAMOND: d = {"Diamond", T_DIAMOND}; break;
    case I_PORK:
        d = {"Raw Porkchop", T_PORK};
        d.food = true;
        break;
    case I_COOKED_PORK:
        d = {"Cooked Porkchop", T_COOKED_PORK};
        d.food = true;
        break;
    case I_APPLE:
        d = {"Apple", T_APPLE};
        d.food = true;
        break;
    case I_ROTTEN_FLESH:
        d = {"Rotten Flesh", T_ROTTEN_FLESH};
        d.food = true;
        break;
    case I_FLINT_STEEL:
        d = {"Flint and Steel", T_FLINT_STEEL};
        d.maxStack = 1;
        break;
    case I_COPPER: d = {"Copper Ingot", T_COPPER}; break;
    case I_SKIFF:
        d = {"River Skiff", T_SKIFF};
        d.maxStack = 1;
        break;
    case I_GRAPPLE:
        d = {"Grappling Hook", T_GRAPPLE};
        d.maxStack = 1;
        break;
    case I_ROPE:
        d = {"Zipline Rope", T_ROPE};
        d.maxStack = 16;
        break;
    case I_LANTERN:
        d = {"Storm Lantern", T_LANTERN};
        d.maxStack = 1;
        break;
    case I_GLIDER:
        d = {"Canvas Glider", T_GLIDER};
        d.maxStack = 1;
        break;
    case I_AMBER: d = {"Amber Shard", T_AMBER_ITEM}; break;
    case I_SULFUR: d = {"Sulfur", T_SULFUR_ITEM}; break;
    case I_NITRE: d = {"Nitre", T_NITRE_ITEM}; break;
    case I_BONE: d = {"Bone", T_BONE}; break;
    case I_HIDE: d = {"Hide", T_HIDE}; break;
    case I_RAW_GAME:
        d = {"Raw Game", T_RAW_GAME};
        d.food = true;
        break;
    case I_ROAST_GAME:
        d = {"Roast Game", T_ROAST_GAME};
        d.food = true;
        break;
    case I_RAW_FISH:
        d = {"Raw Fish", T_RAW_FISH};
        d.food = true;
        break;
    case I_SMOKED_FISH:
        d = {"Smoked Fish", T_SMOKED_FISH};
        d.food = true;
        break;
    case I_DYNAMITE:
        d = {"Dynamite", T_DYNAMITE};
        d.maxStack = 16;
        break;
    case I_POULTICE:
        d = {"Poultice", T_POULTICE};
        d.food = true;
        d.maxStack = 16;
        break;
    case I_HAMMER:
        d = {"Sledgehammer", T_HAMMER};
        d.maxStack = 1;
        d.tool = ToolKind::Pick;
        break;
    case I_POWDER: d = {"Blasting Powder", T_POWDER}; break;
    default: {
        int t = i + I_FIRST - I_PICK_WOOD;
        int kind = t / 4, tier = t % 4;
        static const char* names[4][4] = {{"Wooden Pickaxe", "Stone Pickaxe", "Iron Pickaxe", "Diamond Pickaxe"},
                                          {"Wooden Axe", "Stone Axe", "Iron Axe", "Diamond Axe"},
                                          {"Wooden Shovel", "Stone Shovel", "Iron Shovel", "Diamond Shovel"},
                                          {"Wooden Sword", "Stone Sword", "Iron Sword", "Diamond Sword"}};
        static const ToolKind kinds[4] = {ToolKind::Pick, ToolKind::Axe, ToolKind::Shovel, ToolKind::Sword};
        d.name = names[kind][tier];
        d.tex = uint8_t(T_PICK_WOOD + t);
        d.maxStack = 1;
        d.tool = kinds[kind];
        break;
    }
    }
    return d;
}
}

const ItemDef& itemDef(ItemId id) {
    static ItemDef table[I_END - I_FIRST];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < I_END - I_FIRST; ++i) table[i] = makeItems(i);
        init = true;
    }
    static ItemDef none;
    if (id < I_FIRST || id >= I_END) return none;
    return table[id - I_FIRST];
}

std::string itemName(ItemId id) {
    if (isBlockItem(id)) return gBlocks[id].name;
    return itemDef(id).name;
}

int maxStack(ItemId id) {
    if (isBlockItem(id)) return 512;
    return itemDef(id).maxStack;
}

namespace {
const char* kTextureFiles[] = {
    "blocks/stone",
    "blocks/dirt",
    "blocks/grass_top",
    "blocks/grass_side",
    "blocks/cobble",
    "blocks/planks",
    "blocks/bedrock",
    "blocks/sand",
    "blocks/gravel",
    "blocks/log_side",
    "blocks/log_top",
    "blocks/leaves",
    "blocks/glass",
    "blocks/coal_ore",
    "blocks/iron_ore",
    "blocks/gold_ore",
    "blocks/diamond_ore",
    "blocks/water",
    "blocks/lava",
    "blocks/snow",
    "blocks/snow_side",
    "blocks/ice",
    "blocks/cactus_side",
    "blocks/cactus_top",
    "blocks/clay",
    "blocks/sandstone_side",
    "blocks/sandstone_top",
    "blocks/bricks",
    "blocks/stone_bricks",
    "blocks/tnt_side",
    "blocks/tnt_top",
    "blocks/tnt_bottom",
    "blocks/torch",
    "blocks/glowstone",
    "blocks/obsidian",
    "blocks/tall_grass",
    "blocks/flower_red",
    "blocks/flower_yellow",
    "blocks/birch_side",
    "blocks/birch_top",
    "blocks/birch_leaves",
    "blocks/spruce_side",
    "blocks/spruce_top",
    "blocks/spruce_leaves",
    "blocks/wool_white",
    "blocks/wool_red",
    "blocks/wool_orange",
    "blocks/wool_yellow",
    "blocks/wool_lime",
    "blocks/wool_cyan",
    "blocks/wool_blue",
    "blocks/wool_purple",
    "blocks/wool_black",
    "blocks/fire",
    "blocks/mossy",
    "blocks/gold_block",
    "blocks/iron_block",
    "blocks/diamond_block",
    "blocks/bookshelf",
    "blocks/dead_bush",
    "blocks/crafting_top",
    "blocks/crafting_side",
    "blocks/red_sand",
    "blocks/terracotta",
    "blocks/tc_orange",
    "blocks/tc_yellow",
    "blocks/tc_brown",
    "blocks/tc_red",
    "blocks/tc_white",
    "blocks/tc_lightgray",
    "blocks/granite",
    "blocks/diorite",
    "blocks/andesite",
    "blocks/deepslate",
    "blocks/deepslate_top",
    "blocks/tuff",
    "blocks/podzol_top",
    "blocks/podzol_side",
    "blocks/coarse_dirt",
    "blocks/packed_ice",
    "blocks/mud",
    "blocks/jungle_side",
    "blocks/jungle_top",
    "blocks/jungle_leaves",
    "blocks/acacia_side",
    "blocks/acacia_top",
    "blocks/acacia_leaves",
    "blocks/dark_oak_side",
    "blocks/dark_oak_top",
    "blocks/dark_oak_leaves",
    "blocks/fern",
    "blocks/blue_flower",
    "blocks/sugar_cane",
    "blocks/moss",
    "blocks/red_sandstone_side",
    "blocks/red_sandstone_top",
    "blocks/calcite",
    "blocks/dripstone",
    "blocks/glowshroom",
    "blocks/prop_side",
    "blocks/prop_top",
    "blocks/beam_side",
    "blocks/scoria",
    "blocks/ember",
    "blocks/crystal",
    "blocks/crystal_rock",
    "blocks/fossil",
    "blocks/salt",
    "blocks/ash",
    "blocks/copper_ore",
    "blocks/concrete",
    "blocks/rebar",
    "blocks/rust",
    "blocks/grate",
    "blocks/scaffold",
    "blocks/cable",
    "blocks/cable_live",
    "blocks/cell_side",
    "blocks/cell_top",
    "blocks/dynamo",
    "blocks/switch_off",
    "blocks/switch_on",
    "blocks/plate",
    "blocks/lamp_off",
    "blocks/lamp_on",
    "blocks/ram",
    "blocks/detonator",
    "blocks/sparkgap",
    "items/stick",
    "items/coal",
    "items/iron_ingot",
    "items/gold_ingot",
    "items/diamond",
    "items/pork",
    "items/cooked_pork",
    "items/apple",
    "items/pick_wood",
    "items/pick_stone",
    "items/pick_iron",
    "items/pick_diamond",
    "items/axe_wood",
    "items/axe_stone",
    "items/axe_iron",
    "items/axe_diamond",
    "items/shovel_wood",
    "items/shovel_stone",
    "items/shovel_iron",
    "items/shovel_diamond",
    "items/sword_wood",
    "items/sword_stone",
    "items/sword_iron",
    "items/sword_diamond",
    "items/flint_steel",
    "items/rotten_flesh",
    "items/copper",
    "hand/player_skin",
    "hand/player_shirt",
    "hand/player_boot",
    "hand/player_pack",
    "items/skiff",
    "items/grapple",
    "items/rope",
    "items/lantern",
    "items/glider",
    "breaking/crack0",
    "breaking/crack1",
    "breaking/crack2",
    "breaking/crack3",
    "breaking/crack4",
    "breaking/crack5",
    "breaking/crack6",
    "breaking/crack7",
    "blocks/vine",
    "blocks/branch",
    "blocks/root",
    "blocks/pebbles",
    "blocks/wheat",
    "blocks/long_grass",
    "blocks/long_grass_top",
    "blocks/cattail",
    "blocks/lily_pad",
    "blocks/cactus_flower",
    "blocks/barrel_cactus",
    "blocks/travertine",
    "blocks/geyser",
    "blocks/plant_green",
    "blocks/plant_grain",
    "blocks/mushroom_cap",
    "blocks/firefly_plant",
    "blocks/firefly_bud",
    "blocks/autumn_leaves",
    "blocks/heather",
    "blocks/mushroom_stem",
    "particles/pfx_smoke0",
    "particles/pfx_smoke1",
    "particles/pfx_smoke2",
    "particles/pfx_smoke3",
    "particles/pfx_fire0",
    "particles/pfx_fire1",
    "particles/pfx_fire2",
    "particles/pfx_fire3",
    "particles/pfx_splash0",
    "particles/pfx_splash1",
    "particles/pfx_splash2",
    "particles/pfx_splash3",
    "ui/ui_memory",
    "ui/ui_camera",
    "ui/ui_particles",
    "ui/ui_display",
    "particles/pfx_spark",
    "particles/pfx_mote",
    "particles/pfx_drop",
    "blocks/basalt",
    "blocks/polished_basalt",
    "blocks/sulfur",
    "blocks/nitre_ore",
    "blocks/rune",
    "blocks/amber",
    "blocks/thatch",
    "blocks/straw_side",
    "blocks/slate_tile",
    "blocks/rubber",
    "blocks/lumen",
    "items/amber_item",
    "items/sulfur_item",
    "items/nitre_item",
    "items/bone",
    "items/hide",
    "items/raw_game",
    "items/roast_game",
    "items/raw_fish",
    "items/smoked_fish",
    "items/dynamite",
    "items/poultice",
    "items/hammer",
    "items/powder",
};
static_assert(sizeof(kTextureFiles) / sizeof(kTextureFiles[0]) == T_COUNT, "one file per texture layer");

}

const char* textureFile(int t) { return t >= 0 && t < T_COUNT ? kTextureFiles[t] : ""; }

int textureByName(const std::string& name) {
    for (int t = 0; t < T_COUNT; ++t)
        if (name == kTextureFiles[t]) return t;
    return -1;
}

}
