#include "app/Game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace qc {
namespace {
constexpr int kCats = 7;

bool isTech(ItemId id) { return (id >= B_CABLE && id <= B_SPARKGAP) || id == B_LANTERN || id == B_LUMEN_PANEL || id == B_TNT; }
bool isBuild(ItemId id) {
    switch (id) {
    case B_PLANKS:
    case B_COBBLE:
    case B_BRICKS:
    case B_STONE_BRICKS:
    case B_GLASS:
    case B_BOOKSHELF:
    case B_CRAFTING:
    case B_GOLD_BLOCK:
    case B_IRON_BLOCK:
    case B_DIAMOND_BLOCK:
    case B_CONCRETE:
    case B_REBAR:
    case B_RUST:
    case B_GRATE:
    case B_SCAFFOLD:
    case B_POLISHED_BASALT:
    case B_THATCH:
    case B_STRAW_BALE:
    case B_SLATE_TILE:
    case B_RUBBER:
    case B_PROP:
    case B_BEAM:
    case B_TORCH:
    case B_GLOWSTONE:
    case B_SANDSTONE:
    case B_RED_SANDSTONE:
    case B_MOSSY:
    case B_RUNE_STONE:
    case B_STONE: return true;
    default: return id >= B_WOOL_WHITE && id <= B_WOOL_BLACK;
    }
}
}

int Game::itemCategory(ItemId id) const {
    if (isBlockItem(id)) {
        if (isTech(id)) return 5;
        if (isBuild(id)) return 1;
        return 2;
    }
    const ItemDef& d = itemDef(id);
    if (d.food) return 4;
    if (d.tool != ToolKind::None || d.maxStack == 1 || id == I_DYNAMITE || id == I_ROPE || id == I_FLINT_STEEL) return 3;
    return 6;
}

void Game::drawInventory() {
    const float open = std::min(1.0f, float(clock_ - screenTime_) / 0.18f);
    const float ease = 1.0f - (1.0f - open) * (1.0f - open);
    ui_.gradient(0, 0, ui_.w, ui_.h, col::rgba(0, 0, 0, int(150 * ease)), col::rgba(0, 0, 0, int(185 * ease)));
    const float S = 22;
    const float PW = std::min(ui_.w - 16.0f, 420.0f), PH = std::min(ui_.h - 24.0f, 304.0f);
    const float packW = 9 * S, gap = 12;
    const float craftW = PW - 16 - packW - gap;
    float x0 = std::floor((ui_.w - PW) * 0.5f), y0 = std::floor((ui_.h - PH) * 0.5f + (1.0f - ease) * 16.0f);
    bool shift = input_->down(Key::LeftShift);

    ui_.panel(x0, y0, PW, PH);
    ui_.text(x0 + 8, y0 + 8, "Inventory", col::Ink, false);

    ItemStack tipStack;
    bool clickedSomething = false;
    const float top = y0 + 28;
    const float pxl = x0 + 8, rxl = pxl + packW + gap;

    auto drawSlot = [&](float x, float y, bool hov, bool selected) { ui_.inset(x, y, S - 2, S - 2, selected || hov); };
    auto invSlot = [&](int idx, float x, float y) {
        ItemStack& s = player_.inv.slots[idx];
        bool hov = ui_.hovered(x, y, S - 2, S - 2);
        drawSlot(x, y, hov, idx == player_.selected);
        if (!s.empty()) ui_.item(x + 2, y + 2, S - 6, s.id, s.count);
        if (!hov) return;
        if (!s.empty()) tipStack = s;
        for (int k = 0; k < 9; ++k)
            if (input_->pressed(Key::Num1 + k) && k != idx) {
                std::swap(player_.inv.slots[k], s);
                sounds_.uiPop(1.4f);
            }
        if (ui_.in.pressed || ui_.in.rightPressed) {
            bool dbl = ui_.in.pressed && clock_ - lastSlotClick_ < 0.3 && lastSlotIdx_ == idx;
            lastSlotClick_ = clock_;
            lastSlotIdx_ = idx;
            if (dbl && !cursor_.empty() && maxStack(cursor_.id) > 1) {
                for (int i = 0; i < Inventory::SIZE && cursor_.count < maxStack(cursor_.id); ++i) {
                    ItemStack& t = player_.inv.slots[i];
                    if (t.id != cursor_.id) continue;
                    int n = std::min(t.count, maxStack(cursor_.id) - cursor_.count);
                    cursor_.count += n;
                    t.count -= n;
                    if (t.count <= 0) t.clear();
                }
            } else
                slotClick(s, ui_.in.rightPressed, shift, idx);
            clickedSomething = true;
            sounds_.uiPop(ui_.in.rightPressed ? 1.6f : 1.2f);
        }
    };
    auto label = [&](float x, float y, const char* t, float w) {
        (void)w;
        ui_.text(x, y, t, col::InkSoft, false);
    };

    float my = top;
    label(pxl, my, "Items", packW);
    my += 12;
    ui_.textField(creativeSearch_, searchFocus_, 7, pxl, my, packW - 2, 14, 24);
    my += 18;
    std::vector<ItemId> list;
    std::string q = creativeSearch_;
    for (auto& ch : q) ch = char(std::tolower(ch));
    for (ItemId id : creativeItems()) {
        if (creativeCat_ && itemCategory(id) != creativeCat_) continue;
        if (!q.empty()) {
            std::string n = itemName(id);
            for (auto& ch : n) ch = char(std::tolower(ch));
            if (n.find(q) == std::string::npos) continue;
        }
        list.push_back(id);
    }
    const int cols = 9, rows = std::max(3, int((y0 + PH - 100 - my) / S));
    int totalRows = int((list.size() + size_t(cols) - 1) / size_t(cols));
    if (ui_.hovered(pxl, my, packW, rows * S)) creativeScroll_ -= int(ui_.in.scroll);
    creativeScroll_ = std::clamp(creativeScroll_, 0, std::max(0, totalRows - rows));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            size_t i = size_t((r + creativeScroll_) * cols + c);
            float x = pxl + c * S, y = my + r * S;
            bool hov = ui_.hovered(x, y, S - 2, S - 2);
            drawSlot(x, y, hov, false);
            if (i >= list.size()) continue;
            ui_.item(x + 2, y + 2, S - 6, list[i]);
            if (hov) {
                tipStack = makeStack(list[i], 0);
                if (ui_.in.pressed || ui_.in.rightPressed) {
                    clickedSomething = true;
                    sounds_.uiPop(1.3f);
                    if (!cursor_.empty())
                        cursor_.clear();
                    else if (shift)
                        player_.inv.slots[player_.selected] = makeStack(list[i], maxStack(list[i]));
                    else
                        cursor_ = makeStack(list[i], ui_.in.rightPressed && isBlockItem(list[i]) ? 8 : maxStack(list[i]));
                }
            }
        }
    if (totalRows > rows) {
        float sbH = rows * S - 2, knob = float(creativeScroll_) / float(totalRows - rows);
        ui_.rect(pxl + packW, my, 3, sbH, col::PaperDim);
        ui_.rect(pxl + packW, my + knob * (sbH - 16), 3, 16, col::InkSoft);
    }
    my += rows * S + 10;
    label(pxl, my, "Belt", packW);
    my += 12;
    for (int i = 0; i < 9; ++i) invSlot(i, pxl + i * S, my);
    my += S + 6;
    float inspectY = my;

    float ry = top + 12;
    static const ItemId icons[kCats] = {ItemId(B_CRAFTING), ItemId(B_BRICKS), ItemId(B_GRASS), I_PICK_IRON, I_APPLE,
                                        ItemId(B_CELL),     I_IRON_INGOT};
    static const char* full[kCats] = {"Everything", "Building", "Nature", "Tools", "Food", "Devices", "Materials"};
    for (int k = 0; k < kCats; ++k) {
        float y = ry + k * 20;
        bool on = creativeCat_ == k, hov = ui_.hovered(rxl, y, craftW, 18);
        ui_.rect(rxl, y, craftW, 18, on ? col::Ink : hov ? col::PaperDim : col::Paper);
        ui_.item(rxl + 5, y + 2, 14, icons[k]);
        ui_.text(rxl + 24, y + 5, full[k], on ? col::Paper : col::Ink, false);
        if (hov && ui_.in.pressed) {
            creativeCat_ = k;
            creativeScroll_ = 0;
            sounds_.uiTick();
            clickedSomething = true;
        }
    }

    if ((ui_.in.pressed || ui_.in.rightPressed) && !clickedSomething && !ui_.hovered(x0, y0, PW, PH) && !cursor_.empty()) {
        sim_.spawnItem(player_.eye() + player_.look() * 0.5f, cursor_.id, cursor_.count, player_.look() * 5.0f);
        cursor_.clear();
        sounds_.uiPress();
    }
    ui_.layer();
    if (tipStack.id && cursor_.empty()) ui_.text(pxl, inspectY, itemName(tipStack.id), col::Ink, false);
    ui_.layer();
    if (!cursor_.empty()) ui_.item(ui_.mouse.x - 8, ui_.mouse.y - 8, 16, cursor_.id, cursor_.count);
}

}
