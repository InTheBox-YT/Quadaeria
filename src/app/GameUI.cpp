#define GLFW_INCLUDE_NONE
#include "app/Game.h"
#include "world/Save.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <ctime>

namespace qc {
namespace {
std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}
}

void Game::drawBackground(bool) { ui_.rect(0, 0, ui_.w, ui_.h, col::Paper); }

float Game::card(float w, float h) {
    float x = std::floor((ui_.w - w) * 0.5f), y = std::floor((ui_.h - h) * 0.5f);
    if (worldLoaded_) ui_.panel(x, y, w, h);
    return x;
}

void Game::drawTitle() {
    const float cx = ui_.w * 0.5f;
    const std::string logo = "QUADAERIA";
    float size = std::clamp(std::floor(ui_.w * 0.5f / ui_.textWidth(logo)), 2.0f, 5.0f);
    float ly = std::max(16.0f, ui_.h * 0.16f);
    ui_.textCentered(cx, ly, logo, col::Ink, size);
    const float bw = 160, bh = 20;
    float y = std::max(ly + 9 * size + 24, ui_.h * 0.40f);
    const char* entries[4] = {"Play", "Options", "Controls", "Quit"};
    for (int k = 0; k < 4; ++k) {
        if (ui_.button(entries[k], cx - bw * 0.5f, y, bw, bh)) {
            sounds_.ui();
            if (k == 0) {
                refreshWorldList();
                setScreen(Screen::Worlds);
            } else if (k == 1) {
                optionsReturn_ = Screen::Title;
                optionsTab_ = 0;
                setScreen(Screen::Options);
            } else if (k == 2) {
                optionsReturn_ = Screen::Title;
                optionsTab_ = 3;
                setScreen(Screen::Options);
            } else
                quit = true;
            return;
        }
        y += bh + 6;
    }
}

void Game::drawWorlds() {
    float cx = ui_.w * 0.5f;
    card(340, ui_.h - 12);
    ui_.heading(cx, 14, "Worlds");

    const float lx = cx - 150, ly = 36.0f, lw = 300, rowH = 30.0f;
    const float listH = std::max(rowH, ui_.h - ly - 64);
    ui_.inset(lx - 4, ly - 4, lw + 8, listH + 8);

    static float scroll = 0, scrollTarget = 0;
    static double lastClick = 0;
    if (clock_ - screenTime_ < 0.05) { scroll = scrollTarget = 0; }
    const float maxScroll = std::max(0.0f, float(worlds_.size()) * rowH - listH);
    if (ui_.hovered(lx - 8, ly, lw + 16, listH)) scrollTarget -= ui_.in.scroll * rowH * 0.8f;
    const int count = int(worlds_.size());
    if (count > 0) {
        if (ui_.in.keyDown) {
            selectedWorld_ = std::min(count - 1, selectedWorld_ + 1);
            sounds_.ui();
            float top = selectedWorld_ * rowH;
            if (top + rowH > scrollTarget + listH) scrollTarget = top + rowH - listH;
        }
        if (ui_.in.keyUp) {
            selectedWorld_ = std::max(0, selectedWorld_ - 1);
            sounds_.ui();
            float top = selectedWorld_ * rowH;
            if (top < scrollTarget) scrollTarget = top;
        }
        if (ui_.in.enter && selectedWorld_ >= 0 && selectedWorld_ < count) {
            sounds_.ui();
            openWorld(worlds_[size_t(selectedWorld_)].folder);
            return;
        }
    }
    scrollTarget = std::clamp(scrollTarget, 0.0f, maxScroll);
    scroll += (scrollTarget - scroll) * (1.0f - std::exp(-ui_.dt() * 16.0f));
    if (std::fabs(scrollTarget - scroll) < 0.05f) scroll = scrollTarget;

    for (size_t i = 0; i < worlds_.size(); ++i) {
        float y = ly + float(i) * rowH - scroll;
        if (y < ly - 2 || y + rowH - 2 > ly + listH + 2) continue;
        const WorldInfo& w = worlds_[i];
        bool sel = int(i) == selectedWorld_;
        bool inView = y >= ly - 1 && y + rowH - 2 <= ly + listH + 1;
        bool hov = inView && ui_.hovered(lx, y, lw, rowH - 2) && ui_.mouse.y > ly && ui_.mouse.y < ly + listH;
        if (sel)
            ui_.rect(lx, y, lw, rowH - 2, col::Ink);
        else if (hov)
            ui_.rect(lx, y, lw, rowH - 2, col::Paper);
        ui_.text(lx + 6, y + 4, w.name, sel ? col::Paper : col::Ink, false);
        char buf[64] = "";
        std::time_t t = std::time_t(w.lastPlayed);
        if (w.lastPlayed > 0) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
        ui_.text(lx + 6, y + 15, buf, sel ? col::PaperDeep : col::InkSoft, false);
        if (hov && ui_.in.pressed) {
            if (selectedWorld_ == int(i) && clock_ - lastClick < 0.35) {
                sounds_.ui();
                openWorld(w.folder);
                return;
            }
            if (selectedWorld_ != int(i)) sounds_.ui();
            selectedWorld_ = int(i);
            lastClick = clock_;
        }
    }
    if (maxScroll > 0) {
        float bh = std::max(14.0f, listH * listH / (listH + maxScroll)), by0 = ly + (listH - bh) * (scroll / maxScroll);
        ui_.rect(lx + lw + 5, ly, 3, listH, col::PaperDeep);
        ui_.rect(lx + lw + 5, by0, 3, bh, col::InkSoft);
    }
    if (worlds_.empty()) ui_.textCentered(cx, ly + 20, "No worlds yet", col::InkSoft);
    float by = ui_.h - 52;
    bool has = selectedWorld_ >= 0 && selectedWorld_ < int(worlds_.size());
    if (ui_.button("Play", cx - 154, by, 150, 20, has)) {
        sounds_.ui();
        openWorld(worlds_[size_t(selectedWorld_)].folder);
        return;
    }
    if (ui_.button("New world", cx + 4, by, 150)) {
        sounds_.ui();
        newName_ = "New World";
        newSeed_.clear();
        newGen_ = GenSettings{};
        setScreen(Screen::Create);
    }
    if (ui_.button("Delete", cx - 154, by + 24, 150, 20, has)) {
        sounds_.ui();
        setScreen(Screen::Confirm);
    }
    if (ui_.button("Cancel", cx + 4, by + 24, 150) || escPressed()) {
        sounds_.ui();
        setScreen(Screen::Title);
    }
    if (!message_.empty()) ui_.textCentered(cx, by - 12, message_, col::Red);
}

void Game::drawCreate() {
    float cx = ui_.w * 0.5f, y = std::max(20.0f, ui_.h * 0.15f);
    card(340, ui_.h - 12);
    ui_.heading(cx, y, "New world");
    ui_.text(cx - 100, y + 26, "Name", col::InkSoft, false);
    ui_.textField(newName_, focus_, 1, cx - 100, y + 36, 200);
    ui_.text(cx - 100, y + 66, "Seed", col::InkSoft, false);
    ui_.textField(newSeed_, focus_, 2, cx - 100, y + 76, 200);
    if (ui_.button("World Generation", cx - 100, y + 106, 200)) {
        sounds_.ui();
        setScreen(Screen::CreateWorldGen);
        return;
    }
    if (focus_ < 0) focus_ = 1;
    if (ui_.button("Create", cx - 154, ui_.h - 34, 150) || ui_.in.enter) {
        sounds_.ui();
        createWorld(newName_, newSeed_, newGen_);
        return;
    }
    if (ui_.button("Cancel", cx + 4, ui_.h - 34, 150) || escPressed()) {
        sounds_.ui();
        setScreen(Screen::Worlds);
    }
}

void Game::drawCreateWorldGen() {
    float cx = ui_.w * .5f;
    float y = std::max(18.0f, ui_.h * .5f - 150.0f);
    card(340, ui_.h - 12);
    ui_.heading(cx, y, "World generation");
    const float lx = cx - 155, rx = cx + 5, bw = 150, step = 30;
    float row = y + 38;
    auto pct = [](float v) { return std::to_string(int(std::lround(v * 100.0f))) + "%"; };
    auto snap = [](float& v) { v = std::round(v * 20.0f) / 20.0f; };
    float v = newGen_.terrainScale;
    if (ui_.slider("Terrain Scale: " + pct(v), v, .50f, 2.00f, lx, row, bw)) {
        snap(v);
        newGen_.terrainScale = v;
    }
    v = newGen_.relief;
    if (ui_.slider("Relief: " + pct(v), v, .50f, 1.60f, rx, row, bw)) {
        snap(v);
        newGen_.relief = v;
    }
    row += step;
    v = newGen_.biomeScale;
    if (ui_.slider("Biome Size: " + pct(v), v, .50f, 2.50f, lx, row, bw)) {
        snap(v);
        newGen_.biomeScale = v;
    }
    v = newGen_.rivers;
    if (ui_.slider("Rivers: " + pct(v), v, 0.0f, 2.0f, rx, row, bw)) {
        snap(v);
        newGen_.rivers = v;
    }
    row += step;
    v = newGen_.caves;
    if (ui_.slider("Caves & Ravines: " + pct(v), v, 0.0f, 2.0f, lx, row, bw)) {
        snap(v);
        newGen_.caves = v;
    }
    v = newGen_.vegetation;
    if (ui_.slider("Vegetation: " + pct(v), v, 0.0f, 2.0f, rx, row, bw)) {
        snap(v);
        newGen_.vegetation = v;
    }
    row += step;
    v = newGen_.structures;
    if (ui_.slider("Structures: " + pct(v), v, 0.0f, 2.0f, lx, row, bw)) {
        snap(v);
        newGen_.structures = v;
    }
    if (ui_.button("Reset Defaults", rx, row, bw)) {
        sounds_.ui();
        newGen_ = GenSettings{};
    }
    row += step + 4;
    if (ui_.button("Done", cx - 154, ui_.h - 34, 150) || escPressed()) {
        sounds_.ui();
        setScreen(Screen::Create);
        return;
    }
    if (ui_.button("Reset", cx + 4, ui_.h - 34, 150)) {
        sounds_.ui();
        newGen_ = GenSettings{};
    }
}

void Game::drawConfirm() {
    float cx = ui_.w * 0.5f, y = ui_.h * 0.35f;
    if (selectedWorld_ < 0 || selectedWorld_ >= int(worlds_.size())) {
        setScreen(Screen::Worlds);
        return;
    }
    card(340, 100);
    ui_.textCentered(cx, y, "Delete " + worlds_[size_t(selectedWorld_)].name + "?", col::Ink);
    if (ui_.button("Delete", cx - 154, y + 40, 150)) {
        sounds_.ui();
        removeTree(dataDir() + "/worlds/" + worlds_[size_t(selectedWorld_)].folder);
        refreshWorldList();
        setScreen(Screen::Worlds);
    }
    if (ui_.button("Cancel", cx + 4, y + 40, 150) || escPressed()) {
        sounds_.ui();
        setScreen(Screen::Worlds);
    }
}

void Game::drawLoading() {
    float cx = ui_.w * 0.5f, cy = ui_.h * 0.5f;
    card(200, 60);
    ui_.heading(cx, cy - 18, "Loading");
    int ready = 0, total = 0;
    int pcx = floorDiv(cellOf(player_.pos.x), CS), pcz = floorDiv(cellOf(player_.pos.z), CS);
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            ++total;
            Column* c = world_.column(pcx + dx, pcz + dz);
            if (c && c->state == ColState::Lit) ++ready;
        }
    static float shown = 0;
    float p = float(ready) / float(total);
    if (clock_ - screenTime_ < 0.05) shown = 0;
    shown += (p - shown) * (1.0f - std::exp(-ui_.dt() * 6.0f));
    float bwid = 140, bx = cx - bwid * 0.5f, byy = cy + 4;
    ui_.rect(bx, byy, bwid, 4, col::PaperDeep);
    ui_.rect(bx, byy, std::floor(bwid * shown), 4, col::Ink);
}

// belt goes down the left side
void Game::drawHUD() {
    float cx = ui_.w * 0.5f;
    if (screen_ == Screen::Playing) {
        float my = ui_.h * 0.5f, r = miniMode_ ? 2 : 4;
        uint32_t c = col::rgba(250, 250, 250, target_.hit ? 230 : 160);
        ui_.rect(cx - r, my - 0.5f, 2 * r + 1, 1, c);
        ui_.rect(cx - 0.5f, my - r, 1, 2 * r + 1, c);
    }
    if (screen_ == Screen::Playing && target_.hit && isPowerBlock(target_.block)) {
        std::string t = std::string(def(target_.block).name) + "  -  " + sim_.powerInfo({target_.x, target_.y, target_.z});
        float tw = ui_.textWidth(t) + 10, ty = ui_.h * 0.5f + 12;
        ui_.plate(cx - tw * 0.5f, ty - 3, tw, 13);
        ui_.textCentered(cx, ty, t, col::Ink);
    }
    if (player_.mode == GameMode::Spectator) return;
    const float S = 22, gapY = 2, beltH = 9 * S + 8 * gapY;
    const float beltX = 5.0f, beltY = std::floor(ui_.h * 0.5f - beltH * 0.5f);
    for (int i = 0; i < 9; ++i) {
        float x = beltX, y = beltY + i * (S + gapY);
        bool sel = i == player_.selected;
        ui_.rect(x, y, S, S, sel ? col::rgba(250, 247, 238, 230) : col::rgba(236, 230, 214, 110));
        if (slotPop_[i] > 0) ui_.rect(x, y, S, S, col::rgba(255, 255, 255, int(60 * slotPop_[i])));
        const ItemStack& st = player_.inv.slots[i];
        if (!st.empty()) ui_.item(x + 3, y + 3, S - 6, st.id, st.count);
    }
    const ItemStack& held = player_.held();
    if (!held.empty() && screen_ == Screen::Playing) {
        float a = 0.6f + 0.4f * std::min(1.0f, heldNameTimer_);
        std::string n = itemName(held.id);
        if (held.count > 1 && !isBlockItem(held.id)) n += "  x" + std::to_string(held.count);
        float tx = beltX + S + 6, ty = beltY + player_.selected * (S + gapY) + (S - 8) * 0.5f;
        ui_.text(tx, ty, n, col::rgba(250, 247, 238, int(255 * a)));
    }
    if (screen_ == Screen::Playing && !hideHud_) {
        float fy = ui_.h * 0.5f + 20.0f;
        for (int i = int(feed_.size()) - 1; i >= 0; --i) {
            const FeedLine& f = feed_[size_t(i)];
            float in = std::min(1.0f, f.age * 7.0f), out = std::clamp((3.2f - f.age) * 2.0f, 0.0f, 1.0f);
            float a = std::min(in, out);
            std::string t = "+" + std::to_string(isBlockItem(f.item) ? std::max(1, f.count / 8) : f.count) + " " + itemName(f.item);
            float tw = ui_.textWidth(t);
            float x = ui_.w - tw - 30 + (1.0f - in) * 24.0f, y = fy - (feed_.size() - 1 - i) * 13.0f - std::min(f.age, 1.0f) * 3.0f;
            ui_.item(x, y - 3, 12, f.item, 0);
            ui_.text(x + 15, y, t, col::rgba(250, 247, 238, int(255 * a)));
        }
    }
}

void Game::drawDebug() {
    std::vector<std::string> L;
    vec3 p = player_.pos;
    int bx = cellOf(p.x), by = cellOf(p.y), bz = cellOf(p.z);
    L.push_back("Quadaeria v16  (" + std::to_string(int(fps_ + 0.5f)) + " fps)" + fmt("  update %.2f ms", msUpdate_) +
                fmt("  render(cpu) %.2f ms", msRender_) + fmt("  light %.2f ms", msLightVol_));
    {
        static std::string gpuName;
        if (gpuName.empty()) {
            const char* r = (const char*)gl::GetString(gl::RENDERER);
            gpuName = r ? r : "unknown";
        }
        L.push_back("Graphics: " + gpuName);
    }
    L.push_back(fmt("XYZ: %.2f", p.x) + fmt(" / %.2f", p.y) + fmt(" / %.2f", p.z));
    L.push_back("Mini: " + std::to_string(bx) + " " + std::to_string(by) + " " + std::to_string(bz) + "   Block: " +
                std::to_string(floorDiv(bx, 2)) + " " + std::to_string(floorDiv(by, 2)) + " " + std::to_string(floorDiv(bz, 2)));
    const char* dirs[4] = {"north (-Z)", "east (+X)", "south (+Z)", "west (-X)"};
    float yaw = std::fmod(player_.yaw + PI * 0.25f, 2 * PI);
    if (yaw < 0) yaw += 2 * PI;
    L.push_back(std::string("Facing: ") + dirs[int(yaw / (PI * 0.5f)) % 4]);
    if (gen_) {
        auto s = gen_->surface(bx, bz);
        bool below = by < s.height - 12 && world_.skyAt(bx, by, bz) < 6;
        L.push_back(std::string("Biome: ") + (below ? caveThemeName(gen_->caveTheme(bx, bz, s.temp)) : biomeName(s.biome)));
    }
    uint8_t li = world_.lightAt(bx, cellOf(p.y + 1), bz);
    L.push_back("Light: sky " + std::to_string(li >> 4) + "  block " + std::to_string(li & 15) + fmt("   time %.3f", dayTime_));
    L.push_back("Columns: " + std::to_string(streamer_.loadedColumns()) + "  jobs: " + std::to_string(streamer_.pendingJobs()) +
                "  threads: " + std::to_string(pool_.workers()));
    const auto& rs = renderer_.stats;
    L.push_back("Sections: " + std::to_string(rs.drawn) + "/" + std::to_string(rs.sections) + "  quads: " + std::to_string(rs.quads) +
                fmt("  GPU %.1f MB", double(rs.gpuBytes) / 1048576.0) + "  draws: " + std::to_string(rs.drawCalls) +
                (renderer_.multiDraw() ? " (multi)" : ""));
    if (renderer_.gpu.valid) {
        const auto& g = renderer_.gpu;
        L.push_back(fmt("GPU %.1f ms:", g.total) + fmt(" shadow %.1f", g.shadow) + fmt(" terrain %.1f", g.terrain) +
                    fmt(" ents %.1f", g.entities) + fmt(" water %.1f", g.water) + fmt(" post %.1f", g.post) + fmt(" ui %.1f", g.ui));
    }
    const auto& ss = sim_.stats;
    L.push_back(std::string("CPU sim:") + fmt(" checks %.2f", ss.checksMs) + fmt(" fluid %.2f", ss.fluidMs) +
                fmt(" bodies %.2f", ss.bodyMs) + fmt("  stream %.2f", msStream_) + fmt(" light %.2f", msLightVol_));
    L.push_back("Flecs: " + std::to_string(ss.ecsSystems) + " stages" + fmt("  %.2f ms", ss.ecsMs) + fmt("  objects %.2f", ss.objectMs) +
                fmt("  particles %.2f", ss.particleMs) + "  rows " + std::to_string(ss.particleSlots));
    L.push_back("Entities: " + std::to_string(ss.entities) + "  falling structures: " + std::to_string(ss.bodies) +
                "  debris: " + std::to_string(ss.debris));
    L.push_back("Active fluid cells: " + std::to_string(ss.fluids) + "  particles: " + std::to_string(ss.particles));
    if (target_.hit)
        L.push_back(std::string("Target: ") + def(target_.block).name + " @ " + std::to_string(target_.x) + " " +
                    std::to_string(target_.y) + " " + std::to_string(target_.z));
    float y = 2;
    for (auto& s : L) {
        ui_.rect(1, y - 1, ui_.textWidth(s) + 3, 10, col::rgba(80, 80, 80, 140));
        ui_.text(2, y, s, col::rgba(224, 224, 224), false);
        y += 10;
    }
}

void Game::drawPause() {
    ui_.rect(0, 0, ui_.w, ui_.h, col::rgba(0, 0, 0, 90));
    const float cx = ui_.w * 0.5f, bw = 160, bh = 20, ch = 4 * (bh + 6) + 60;
    card(200, ch);
    float y = std::floor((ui_.h - ch) * 0.5f) + 12;
    ui_.heading(cx, y, "Paused");
    ui_.textCentered(cx, y + 20, worldName_, col::InkSoft);
    y += 40;
    const float bx = cx - bw * 0.5f;
    if (ui_.button("Resume", bx, y, bw, bh) || escPressed()) {
        sounds_.ui();
        setScreen(Screen::Playing);
        return;
    }
    y += bh + 6;
    if (ui_.button("Options", bx, y, bw, bh)) {
        sounds_.ui();
        optionsReturn_ = Screen::Pause;
        optionsTab_ = 0;
        setScreen(Screen::Options);
        return;
    }
    y += bh + 6;
    if (ui_.button("Controls", bx, y, bw, bh)) {
        sounds_.ui();
        optionsReturn_ = Screen::Pause;
        optionsTab_ = 3;
        setScreen(Screen::Options);
        return;
    }
    y += bh + 6;
    if (ui_.button("Save and quit", bx, y, bw, bh)) {
        sounds_.ui();
        closeWorld(true);
        setScreen(Screen::Title);
        return;
    }
}

void Game::drawOptions() {
    if (worldLoaded_) ui_.rect(0, 0, ui_.w, ui_.h, col::rgba(0, 0, 0, 90));
    const float m = 8, sw = std::clamp(std::floor(ui_.w * 0.24f), 110.0f, 150.0f);
    if (worldLoaded_)
        ui_.panel(m, m, ui_.w - 2 * m, ui_.h - 2 * m);
    else
        ui_.rect(0, 0, ui_.w, ui_.h, col::Paper);
    ui_.rect(m + sw, m + 10, 1, ui_.h - 2 * m - 20, col::PaperDeep);
    float ty = m + 10;
    const char* tabs[4] = {"Video", "Play", "System", "Controls"};
    optionsTab_ = std::clamp(optionsTab_, 0, 3);
    for (int k = 0; k < 4; ++k) {
        if (ui_.menuEntry(tabs[k], m + 6, ty, sw - 12, 1, optionsTab_ == k) && optionsTab_ != k) {
            sounds_.uiTick();
            optionsTab_ = k;
        }
        ty += UI::menuEntryHeight(1) + 2;
    }
    bool back = ui_.button("Back", m + 6, ui_.h - m - 28, sw - 12, 20) || escPressed();

    const float x0 = m + sw + 14, cw = std::min(ui_.w - x0 - m - 10, 420.0f);
    float y = m + 10;
    ui_.text(x0, y, tabs[optionsTab_], col::Ink, false, 2);
    y += 28;
    const float labW = std::min(100.0f, cw * 0.36f), segW = std::min(cw - labW, 240.0f);
    bool remesh = false, apply = false;
    auto slider = [&](float& yy, const char* label, std::string value, float& v, float lo, float hi) {
        bool rowHov = ui_.hovered(x0, yy, labW + segW, 14);
        ui_.text(x0, yy + 3, label, rowHov ? col::Ink : col::InkSoft, false);
        bool ch = ui_.slider(value, v, lo, hi, x0 + labW, yy, segW, 14);
        yy += 18;
        return ch;
    };
    switch (optionsTab_) {
    case 0: {
        float rd = float(settings_.renderDist);
        if (slider(y, "Chunks", std::to_string(settings_.renderDist), rd, 2, 24)) settings_.renderDist = int(std::round(rd));
        int g = segRow(x0, y, labW, segW, "Graphics", settings_.graphics, {"Fancy", "Balanced", "Fast"});
        if (g != settings_.graphics) {
            settings_.graphics = g;
            apply = remesh = true;
        }
        float rs = settings_.renderScale;
        if (slider(y, "Render scale", std::to_string(int(std::round(settings_.renderScale * 100))) + "%", rs, 0.4f, 1.0f))
            settings_.renderScale = std::round(rs * 20) / 20;
        int at = segRow(x0, y, labW, segW, "Atmosphere", settings_.atmosphere, {"Clear", "Soft", "Hazy"});
        if (at != settings_.atmosphere) {
            settings_.atmosphere = at;
            apply = true;
        }
        y += 8;
        if (ui_.button("Potato preset", x0 + labW, y, segW, 16)) {
            sounds_.ui();
            settings_.potato();
            applySettings();
            remesh = true;
        }
        break;
    }
    case 1: {
        slider(y, "Field of view", std::to_string(int(settings_.fov)), settings_.fov, 50, 110);
        slider(y, "Sensitivity", std::to_string(int(settings_.sensitivity * 100)) + "%", settings_.sensitivity, 0.2f, 3.0f);
        {
            int b = segRow(x0, y, labW, segW, "View bob", settings_.bobbing ? 1 : 0, {"Off", "On"});
            settings_.bobbing = b == 1;
        }
        int ph = segRow(x0, y, labW, segW, "Physics", settings_.physics, {"Light", "Normal", "High"});
        if (ph != settings_.physics) {
            settings_.physics = ph;
            apply = true;
        }
        Settings& S = settings_;
        S.turnSway = segRow(x0, y, labW, segW, "Turn sway", S.turnSway ? 1 : 0, {"Off", "On"}) == 1;
        S.sprintPulse = segRow(x0, y, labW, segW, "Sprint pulse", S.sprintPulse ? 1 : 0, {"Off", "On"}) == 1;
        S.handDrift = segRow(x0, y, labW, segW, "Hand drift", S.handDrift ? 1 : 0, {"Off", "On"}) == 1;
        S.stepShake = segRow(x0, y, labW, segW, "Shake", S.stepShake ? 1 : 0, {"Smooth", "Stepped"}) == 1;
        break;
    }
    case 2: {
        int vs = segRow(x0, y, labW, segW, "VSync", settings_.vsync ? 1 : 0, {"Off", "On"});
        if (vs != (settings_.vsync ? 1 : 0)) {
            settings_.vsync = vs == 1;
            apply = true;
        }
        int fs = segRow(x0, y, labW, segW, "Window", settings_.fullscreen ? 1 : 0, {"Windowed", "Full"});
        if (fs != (settings_.fullscreen ? 1 : 0)) {
            settings_.fullscreen = fs == 1;
            GLFWmonitor* mon = glfwGetPrimaryMonitor();
            const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
            if (settings_.fullscreen && vm)
                glfwSetWindowMonitor(win_, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
            else
                glfwSetWindowMonitor(win_, nullptr, 80, 80, 1152, 864, 0);
        }
        int gsv = segRow(x0, y, labW, segW, "Menu size", std::clamp(settings_.guiScale, 0, 3), {"Auto", "1x", "2x", "3x"});
        if (gsv != settings_.guiScale) settings_.guiScale = gsv;
        settings_.aspect43 = segRow(x0, y, labW, segW, "Frame", settings_.aspect43 ? 0 : 1, {"4:3", "Wide"}) == 0;
        if (slider(y, "Volume", std::to_string(int(settings_.volume * 100)) + "%", settings_.volume, 0, 1) && audio_ && audio_->enabled())
            audio_->setMasterVolume(settings_.volume);
        break;
    }
    case 3: {
        const char* rows[][2] = {
            {"WASD / mouse", "move, look"},
            {"Space", "jump, fly"},
            {"Shift", "sneak, down"},
            {"Ctrl", "sprint"},
            {"Left click", "break"},
            {"Right click", "place, use"},
            {"Middle click", "pick block"},
            {"R", "block / mini"},
            {"1-9, wheel", "belt"},
            {"X", "last slot"},
            {"E", "inventory"},
            {"Q", "drop"},
            {"C", "zoom"},
            {"F1", "hide HUD"},
            {"F2", "screenshot"},
            {"F3", "debug"},
            {"F4", "spectator"},
            {"F11", "fullscreen"},
            {"G", "day / night"},
        };
        const int n = int(sizeof(rows) / sizeof(rows[0])), half = (n + 1) / 2;
        const float colW = (cw - 10) * 0.5f;
        for (int k = 0; k < n; ++k) {
            float cx = x0 + (k < half ? 0 : colW + 10), ry = y + (k % half) * 15;
            ui_.text(cx, ry, rows[k][0], col::Ink, false);
            ui_.text(cx + 78, ry, rows[k][1], col::InkSoft, false);
        }
        break;
    }
    }
    if (apply) applySettings();
    if (remesh && worldLoaded_) streamer_.remeshAll();
    if (back) {
        sounds_.ui();
        applySettings();
        setScreen(optionsReturn_);
    }
}

int Game::segRow(float x, float& yy, float labW, float segW, const char* label, int value, std::initializer_list<const char*> opts) {
    const float rh = 14;
    bool rowHov = ui_.hovered(x, yy, labW + segW, rh);
    ui_.text(x, yy + 3, label, rowHov ? col::Ink : col::InkSoft, false);
    int n = int(opts.size()), picked = value, k = 0;
    const float sx = x + labW, sw = segW / float(n);
    ui_.inset(sx, yy, segW, rh, false);
    if (value >= 0 && value < n) ui_.rect(std::floor(sx + value * sw), yy, std::floor(sw), rh, col::Ink);
    for (const char* o : opts) {
        float bx = sx + k * sw;
        bool on = value == k, hov = ui_.hovered(bx, yy, sw, rh);
        ui_.textCentered(bx + sw * 0.5f, yy + 3, o, on ? col::Paper : hov ? col::Ink : col::InkSoft);
        if (hov && ui_.in.pressed && !on) {
            picked = k;
            sounds_.uiTick();
        }
        ++k;
    }
    yy += rh + 4;
    return picked;
}

void Game::drawControls() {
    optionsTab_ = 3;
    setScreen(Screen::Options);
}

bool Game::slotClick(ItemStack& slot, bool right, bool shift, int idx) {
    if (shift && !right && !slot.empty() && idx >= 0) {
        int from = idx < 9 ? 9 : 0, to = idx < 9 ? 36 : 9;
        ItemStack s = slot;
        slot.clear();
        int mx = maxStack(s.id);
        for (int i = from; i < to && s.count > 0; ++i) {
            ItemStack& t = player_.inv.slots[i];
            if (t.id == s.id && mx > 1 && t.count < mx) {
                int n = std::min(s.count, mx - t.count);
                t.count += n;
                s.count -= n;
            }
        }
        for (int i = from; i < to && s.count > 0; ++i)
            if (player_.inv.slots[i].empty()) {
                player_.inv.slots[i] = s;
                s.count = 0;
            }
        if (s.count > 0) slot = s;
        return true;
    }
    if (!right) {
        if (cursor_.empty()) {
            cursor_ = slot;
            slot.clear();
        } else if (slot.empty()) {
            slot = cursor_;
            cursor_.clear();
        } else if (slot.id == cursor_.id && maxStack(slot.id) > 1) {
            int n = std::min(cursor_.count, maxStack(slot.id) - slot.count);
            slot.count += n;
            cursor_.count -= n;
            if (cursor_.count <= 0) cursor_.clear();
        } else
            std::swap(slot, cursor_);
    } else {
        if (cursor_.empty() && !slot.empty()) {
            int half = (slot.count + 1) / 2;
            cursor_ = slot;
            cursor_.count = half;
            slot.count -= half;
            if (slot.count <= 0) slot.clear();
        } else if (!cursor_.empty()) {
            int unit = isBlockItem(cursor_.id) ? std::min(8, cursor_.count) : 1;
            if (slot.empty()) {
                slot = cursor_;
                slot.count = unit;
                cursor_.count -= unit;
            } else if (slot.id == cursor_.id && slot.count + unit <= maxStack(slot.id)) {
                slot.count += unit;
                cursor_.count -= unit;
            }
            if (cursor_.count <= 0) cursor_.clear();
        }
    }
    return true;
}

void Game::closeInventory() {
    if (!cursor_.empty()) {
        int left = player_.inv.add(cursor_.id, cursor_.count);
        if (left > 0) sim_.spawnItem(player_.eye() + player_.look() * 0.5f, cursor_.id, left, player_.look() * 4.0f);
        cursor_.clear();
    }
    setScreen(Screen::Playing);
}

}
