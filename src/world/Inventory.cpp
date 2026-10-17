#include "world/Inventory.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace qc {
ItemStack makeStack(ItemId id, int count) {
    ItemStack s;
    s.id = id;
    s.count = count;
    return s;
}

int Inventory::add(ItemId id, int count) {
    if (!id || count <= 0) return 0;
    int maxS = maxStack(id);
    if (maxS > 1)
        for (auto& s : slots)
            if (s.id == id && s.count < maxS) {
                int n = std::min(count, maxS - s.count);
                s.count += n;
                count -= n;
                if (!count) return 0;
            }
    for (auto& s : slots)
        if (s.empty()) {
            int n = std::min(count, maxS);
            s.id = id;
            s.count = n;
            count -= n;
            if (!count) return 0;
        }
    return count;
}

int Inventory::countOf(ItemId id) const {
    int n = 0;
    for (auto& s : slots)
        if (s.id == id) n += s.count;
    return n;
}

bool Inventory::remove(ItemId id, int count) {
    if (countOf(id) < count) return false;
    for (int i = SIZE - 1; i >= 0 && count > 0; --i) {
        ItemStack& s = slots[i];
        if (s.id != id) continue;
        int n = std::min(count, s.count);
        s.count -= n;
        count -= n;
        if (s.count <= 0) s.clear();
    }
    return true;
}

std::string Inventory::serialize() const {
    std::ostringstream o;
    for (int i = 0; i < SIZE; ++i) {
        if (i) o << ',';
        o << slots[i].id << ':' << slots[i].count;
    }
    return o.str();
}

void Inventory::deserialize(const std::string& s) {
    clear();
    std::istringstream in(s);
    std::string item;
    int i = 0;
    while (std::getline(in, item, ',') && i < SIZE) {
        int id = 0, c = 0;
        // old saves have a 3rd number from when tools had durability, sscanf just skips it
        if (std::sscanf(item.c_str(), "%d:%d", &id, &c) == 2 && id > 0 && c > 0) {
            slots[i].id = ItemId(id);
            slots[i].count = c;
        }
        ++i;
    }
}

const std::vector<ItemId>& creativeItems() {
    static std::vector<ItemId> items;
    if (items.empty()) {
        const BlockId blocks[] = {B_GRASS,         B_DIRT,
                                  B_STONE,         B_COBBLE,
                                  B_MOSSY,         B_STONE_BRICKS,
                                  B_BRICKS,        B_SAND,
                                  B_SANDSTONE,     B_GRAVEL,
                                  B_CLAY,          B_PLANKS,
                                  B_LOG,           B_BIRCH_LOG,
                                  B_SPRUCE_LOG,    B_LEAVES,
                                  B_BIRCH_LEAVES,  B_SPRUCE_LEAVES,
                                  B_GLASS,         B_ICE,
                                  B_SNOW,          B_SNOWY_GRASS,
                                  B_CACTUS,        B_OBSIDIAN,
                                  B_BEDROCK,       B_COAL_ORE,
                                  B_IRON_ORE,      B_GOLD_ORE,
                                  B_DIAMOND_ORE,   B_GOLD_BLOCK,
                                  B_IRON_BLOCK,    B_DIAMOND_BLOCK,
                                  B_BOOKSHELF,     B_CRAFTING,
                                  B_GLOWSTONE,     B_TORCH,
                                  B_TNT,           B_WATER,
                                  B_LAVA,          B_TALL_GRASS,
                                  B_FLOWER_RED,    B_FLOWER_YELLOW,
                                  B_DEAD_BUSH,     B_WOOL_WHITE,
                                  B_WOOL_RED,      B_WOOL_ORANGE,
                                  B_WOOL_YELLOW,   B_WOOL_LIME,
                                  B_WOOL_CYAN,     B_WOOL_BLUE,
                                  B_WOOL_PURPLE,   B_WOOL_BLACK,
                                  B_GRANITE,       B_DIORITE,
                                  B_ANDESITE,      B_DEEPSLATE,
                                  B_TUFF,          B_CALCITE,
                                  B_PODZOL,        B_COARSE_DIRT,
                                  B_MUD,           B_MOSS,
                                  B_RED_SAND,      B_RED_SANDSTONE,
                                  B_TERRACOTTA,    B_TC_ORANGE,
                                  B_TC_YELLOW,     B_TC_BROWN,
                                  B_TC_RED,        B_TC_WHITE,
                                  B_TC_LIGHTGRAY,  B_PACKED_ICE,
                                  B_JUNGLE_LOG,    B_JUNGLE_LEAVES,
                                  B_ACACIA_LOG,    B_ACACIA_LEAVES,
                                  B_DARK_OAK_LOG,  B_DARK_OAK_LEAVES,
                                  B_FERN,          B_BLUE_FLOWER,
                                  B_SUGAR_CANE,    B_DRIPSTONE,
                                  B_GLOWSHROOM,    B_PROP,
                                  B_BEAM,          B_SCORIA,
                                  B_EMBER,         B_CRYSTAL,
                                  B_CRYSTAL_ROCK,  B_FOSSIL,
                                  B_SALT,          B_ASH,
                                  B_COPPER_ORE,    B_CONCRETE,
                                  B_REBAR,         B_RUST,
                                  B_GRATE,         B_SCAFFOLD,
                                  B_CABLE,         B_CELL,
                                  B_DYNAMO,        B_SWITCH,
                                  B_PLATE,         B_LAMP,
                                  B_RAM,           B_DETONATOR,
                                  B_SPARKGAP,      B_VINE,
                                  B_BRANCH,        B_ROOT,
                                  B_PEBBLES,       B_WILD_WHEAT,
                                  B_LONG_GRASS,    B_CATTAIL,
                                  B_LILY_PAD,      B_CACTUS_FLOWER,
                                  B_BARREL_CACTUS, B_TRAVERTINE,
                                  B_GEYSER,        B_FIREFLY_BUSH,
                                  B_BASALT,        B_POLISHED_BASALT,
                                  B_SULFUR,        B_NITRE_ORE,
                                  B_RUNE_STONE,    B_AMBER,
                                  B_THATCH,        B_STRAW_BALE,
                                  B_SLATE_TILE,    B_RUBBER,
                                  B_LUMEN_PANEL};
        for (BlockId b : blocks) items.push_back(b);
        for (ItemId i = I_FIRST; i < I_END; ++i)
            if (!retiredItem(i)) items.push_back(i);
    }
    return items;
}

}
