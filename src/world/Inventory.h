#pragma once
#include "world/Block.h"
#include <string>
#include <vector>

namespace qc {
struct ItemStack {
    ItemId id = 0;
    int count = 0;
    bool empty() const { return id == 0 || count <= 0; }
    void clear() {
        id = 0;
        count = 0;
    }
};
ItemStack makeStack(ItemId id, int count);

struct Inventory {
    static constexpr int SIZE = 36;
    ItemStack slots[SIZE];
    int add(ItemId id, int count);
    int countOf(ItemId id) const;
    bool remove(ItemId id, int count);
    void clear() {
        for (auto& s : slots) s.clear();
    }
    std::string serialize() const;
    void deserialize(const std::string& s);
};

const std::vector<ItemId>& creativeItems();

}
