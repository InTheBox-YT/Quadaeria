#define GLFW_INCLUDE_NONE
#include "app/Game.h"
#include <cstring>
#include "world/Save.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace qc {
namespace {
vec3 cellsCenter(const std::vector<ivec3>& cells) {
    vec3 c;
    for (auto& p : cells) c += vec3((p.x + 0.5f) * VOX, (p.y + 0.5f) * VOX, (p.z + 0.5f) * VOX);
    return cells.empty() ? c : c / float(cells.size());
}
}

void Game::doBreak() {
    if (targetCells_.empty()) return;
    uint8_t main = world_.get(target_.x, target_.y, target_.z);
    vec3 c = cellsCenter(targetCells_);
    for (const ivec3& p : targetCells_) {
        uint8_t b = world_.get(p.x, p.y, p.z);
        sim_.blockParticles(vec3((p.x + 0.5f) * VOX, (p.y + 0.5f) * VOX, (p.z + 0.5f) * VOX), b, miniMode_ ? 6 : 3, 0.22f, 2.0f);
    }
    sim_.removeCells(targetCells_, false, &player_, c);
    if (def(main).structural) sim_.structuralEdit(targetCells_, true);
    sim_.sounds.push_back({Snd::Dig, c, 0.9f, 0.85f + float(std::fmod(clock_ * 13.1, 0.3)), def(main).sound});
    mineKey_ = 0;
}

bool Game::doPlace() {
    ItemStack& held = player_.held();
    if (held.empty() || !isBlockItem(held.id) || previewCells_.empty()) return false;
    BlockId id = BlockId(held.id);
    int avail = 1 << 30;
    ivec3 origin = previewCells_.front();
    for (auto p : previewCells_) origin = {std::min(origin.x, p.x), std::min(origin.y, p.y), std::min(origin.z, p.z)};
    int used = 0;
    static uint8_t tntGroup = 0;
    tntGroup = uint8_t(tntGroup % 31 + 1);
    for (const ivec3& p : previewCells_) {
        if (avail <= 0 && id != B_LONG_GRASS) break;
        uint8_t meta = 0;
        if (id == B_WATER || id == B_LAVA) {
            if (miniMode_ || p.y > origin.y)
                meta = FLUID_SOURCE_META;
            else {
                uint8_t under = world_.get(p.x, p.y - 1, p.z);
                const BlockDef& ud = def(under);
                bool openUnder = under == B_AIR || under == B_FIRE || (ud.replaceable && !ud.solid && !ud.fluid);
                meta = fluidFlowMeta(uint8_t(id), 0, openUnder);
            }
        } else
            meta = def(id).fluid ? FLUID_FULL_META : id == B_TNT ? tntGroup : 0;
        int phase = ((p.x - origin.x) & 1) | (((p.y - origin.y) & 1) << 1) | (((p.z - origin.z) & 1) << 2);
        if (orientable(id)) meta = makeOrientMeta(0, phase, p.x, p.y, p.z);
        if (id == B_TNT) meta = uint8_t(tntGroup | (makeOrientMeta(0, phase, p.x, p.y, p.z) & 224));
        if (isPillar(id)) {
            int o = target_.nx ? 1 : target_.nz ? 2 : 0;
            uint8_t local = axisMeta(o, p.x - origin.x, p.y - origin.y, p.z - origin.z);
            meta = makeOrientMeta(o, metaPhase(local, p.x - origin.x, p.y - origin.y, p.z - origin.z), p.x, p.y, p.z);
        }
        if (id == B_CELL) meta = 255;
        if (def(id).render == Render::Leaves) meta = LEAF_PLACED;
        if (id == B_RAM || id == B_SPARKGAP) {
            vec3 d = player_.look();
            vec3 a(std::fabs(d.x), std::fabs(d.y), std::fabs(d.z));
            meta = a.y > a.x && a.y > a.z ? (d.y > 0 ? 2 : 3) : a.x > a.z ? (d.x > 0 ? 0 : 1) : (d.z > 0 ? 4 : 5);
        }
        uint8_t placed = id == B_LONG_GRASS && p.y > origin.y ? uint8_t(B_LONG_GRASS_TOP) : id;
        if (!def(placed).fluid && def(world_.get(p.x, p.y, p.z)).fluid) sim_.displaceFluid(p.x, p.y, p.z);
        if (world_.setBlock(p.x, p.y, p.z, placed, meta) && placed != B_LONG_GRASS_TOP) {
            --avail;
            ++used;
        }
    }
    if (!used) return false;
    sim_.sounds.push_back({Snd::Place, cellsCenter(previewCells_), 0.8f, 0.9f, def(id).sound});
    if (def(id).structural) sim_.structuralEdit(previewCells_, false);
    swing_ = 1;
    return true;
}

void Game::interact(float dt) {
    vec3 eye = player_.viewEye(), dir = player_.look();
    float reach = 6.0f;
    target_ = raycast(world_, eye, dir, reach, player_.held().id == B_LILY_PAD);
    targetEnt_ = -1;
    if (targetEnt_ >= 0) target_.hit = false;
    targetCells_ = breakCells(target_, miniMode_);
    ItemStack& held = player_.held();
    previewCells_ = placeCells(target_, miniMode_, held.empty() ? ItemId(0) : held.id);
    useCooldown_ -= dt;
    attackCooldown_ -= dt;
    breakCooldown_ -= dt;

    if (useAdventure()) return;

    if (input_->mousePressed(Mouse::Left) && !target_.hit && attackCooldown_ <= 0) {
        swing_ = 1;
        attackCooldown_ = 0.3f;
    }
    if (input_->mouseDown(Mouse::Left) && target_.hit && !targetCells_.empty() && targetEnt_ < 0) {
        int64_t key = packPos(target_.x, target_.y, target_.z) * 2 + (miniMode_ ? 1 : 0);
        if (key != mineKey_) mineKey_ = key;
        if (swing_ < 0.05f) swing_ = 1.0f;
        if (breakCooldown_ <= 0) {
            doBreak();
            // instant break, just a small delay so holding click doesnt eat everything
            breakCooldown_ = 0.2f;
        }
    } else {
        mineKey_ = 0;
        breakCooldown_ = std::min(breakCooldown_, 0.0f);
    }
    if (input_->mousePressed(Mouse::Middle) && target_.hit) {
        ItemId id = target_.block == B_LONG_GRASS_TOP ? ItemId(B_LONG_GRASS) : ItemId(target_.block);
        int slot = -1;
        for (int i = 0; i < 9; ++i)
            if (player_.inv.slots[i].id == id) slot = i;
        if (slot >= 0)
            player_.selected = slot;
        else
            player_.inv.slots[player_.selected] = makeStack(id, maxStack(id));
    }
    bool rightDown = input_->mouseDown(Mouse::Right), rightPressed = input_->mousePressed(Mouse::Right);
    if (rightPressed) useCooldown_ = 0;
    if (rightDown && useCooldown_ <= 0) {
        useCooldown_ = 0.16f;
        if (held.id == I_DYNAMITE && rightPressed) {
            vec3 look = player_.look();
            sim_.throwDynamite(player_.eye() + look * 0.5f, look * 11.0f + vec3(0, 3.0f, 0) + player_.vel * 0.5f);
            sim_.sounds.push_back({Snd::Hiss, player_.eye(), 0.5f, 1.6f});
            swing_ = 1;
            useCooldown_ = 0.45f;
        } else if (target_.hit) {
            if (held.id == I_FLINT_STEEL) {
                if (target_.block == B_TNT)
                    sim_.igniteTNT(target_.x, target_.y, target_.z, 4.0f);
                else {
                    int x = target_.x + target_.nx, y = target_.y + target_.ny, z = target_.z + target_.nz;
                    if (world_.get(x, y, z) == B_AIR) world_.setBlock(x, y, z, B_FIRE);
                }
                sim_.sounds.push_back({Snd::Burn, player_.eye(), 0.6f, 1.5f});
                swing_ = 1;
                useCooldown_ = 0.35f;
            } else if (target_.block == B_DYNAMO && !player_.sneaking) {
                sim_.crankDynamo({target_.x, target_.y, target_.z});
                swing_ = 1;
                useCooldown_ = 0.3f;
            } else if ((target_.block == B_SWITCH || target_.block == B_SWITCH_ON) && rightPressed && !player_.sneaking) {
                const uint8_t to = target_.block == B_SWITCH ? B_SWITCH_ON : B_SWITCH;
                std::vector<ivec3> q{{target_.x, target_.y, target_.z}};
                for (size_t i = 0; i < q.size() && q.size() < 8; ++i) {
                    const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (auto& d : D) {
                        ivec3 n{q[i].x + d[0], q[i].y + d[1], q[i].z + d[2]};
                        if (world_.get(n.x, n.y, n.z) != target_.block) continue;
                        bool seen = false;
                        for (auto& c : q)
                            if (c == n) seen = true;
                        if (!seen && q.size() < 8) q.push_back(n);
                    }
                }
                for (auto& c : q) world_.setBlock(c.x, c.y, c.z, to);
                sim_.sounds.push_back({Snd::Click, vec3((target_.x + 0.5f) * VOX, (target_.y + 0.5f) * VOX, (target_.z + 0.5f) * VOX), 0.9f,
                                       to == B_SWITCH_ON ? 0.8f : 0.6f});
                swing_ = 1;
            } else {
                doPlace();
            }
        }
    }
}

// the --do stuff from the command line, handy for taking screenshots
void Game::runScript() {
    if (scriptWait_ > 0) {
        --scriptWait_;
        return;
    }
    if (script.empty()) return;
    std::string a = script.front();
    script.erase(script.begin());
    auto arg = [&](const std::string& prefix) { return a.substr(prefix.size()); };
    auto nums = [](const std::string& s) {
        std::vector<float> v;
        std::stringstream ss(s);
        std::string t;
        while (std::getline(ss, t, ',')) v.push_back(float(std::atof(t.c_str())));
        return v;
    };
    if (a != "waitload") std::printf("[quadaeria] script: %s\n", a.c_str());
    if (a.rfind("wait:", 0) == 0)
        scriptWait_ = std::atoi(arg("wait:").c_str());
    else if (a.rfind("create:", 0) == 0) {
        std::vector<std::string> parts;
        std::stringstream ss(arg("create:"));
        std::string t;
        while (std::getline(ss, t, ':')) parts.push_back(t);
        if (worldLoaded_) closeWorld(false);
        createWorld(parts.size() > 0 ? parts[0] : "Test", parts.size() > 1 ? parts[1] : "", GenSettings{});
    } else if (a.rfind("open:", 0) == 0)
        openWorld(arg("open:"));
    else if (a == "waitload") {
        if (screen_ == Screen::Loading) { script.insert(script.begin(), a); }
    } else if (a.rfind("pos:", 0) == 0) {
        auto v = nums(arg("pos:"));
        if (v.size() >= 3) {
            player_.pos = {v[0], v[1], v[2]};
            player_.fallStart = v[1];
            player_.vel = {};
        }
    } else if (a.rfind("ground:", 0) == 0) {
        auto v = nums(arg("ground:"));
        if (v.size() >= 2) {
            int top = world_.heightAtTop(cellOf(v[0]), cellOf(v[1]));
            player_.pos = {v[0], top * VOX + 0.01f, v[1]};
            player_.fallStart = player_.pos.y;
        }
    } else if (a.rfind("look:", 0) == 0) {
        auto v = nums(arg("look:"));
        if (v.size() >= 2) {
            player_.yaw = v[0] * DEG2RAD;
            player_.pitch = v[1] * DEG2RAD;
        }
    } else if (a.rfind("time:", 0) == 0)
        dayTime_ = float(std::atof(arg("time:").c_str()));
    else if (a == "fly")
        player_.flying = !player_.flying;
    else if (a == "pfxtest") {
        vec3 f = player_.look();
        vec3 c = player_.eye() + f * 3.0f;
        vec3 r(std::cos(player_.yaw), 0, std::sin(player_.yaw));
        for (int i = 0; i < 8; ++i)
            sim_.particle(PKind::Smoke, c - r * 1.2f + vec3(0, 0.2f * i, 0), vec3(0, 0.5f, 0), 2.5f, 0.45f, vec4(0.8f, 0.8f, 0.82f, 0.8f));
        sim_.dustPuff(c + vec3(0, -0.8f, 0), B_DIRT, 10, 1.0f);
        sim_.blockParticles(c + r * 0.3f, B_STONE, 14, 0.3f, 2.0f);
        for (int i = 0; i < 14; ++i)
            sim_.particle(PKind::Spark, c + r * 1.2f, vec3(sim_.rngRange(-1, 1), sim_.rngRange(0.5f, 2.5f), sim_.rngRange(-1, 1)), 0.5f,
                          0.05f, vec4(1, 0.8f, 0.4f, 1));
        for (int i = 0; i < 6; ++i)
            sim_.particle(PKind::Flame, c + r * 0.8f + vec3(0, -0.5f + 0.15f * i, 0), vec3(0, 0.6f, 0), 0.8f, 0.3f, vec4(1, 0.7f, 0.2f, 1));
        for (int i = 0; i < 12; ++i)
            sim_.particle(PKind::Splash, c - r * 0.4f,
                          vec3(sim_.rngRange(-1.5f, 1.5f), sim_.rngRange(1.5f, 3.5f), sim_.rngRange(-1.5f, 1.5f)), 0.8f, 0.06f,
                          vec4(0.6f, 0.75f, 1.0f, 0.8f));
    } else if (a.rfind("uiclick:", 0) == 0) {
        auto v = nums(arg("uiclick:"));
        if (v.size() >= 2) {
            fakeMouse_ = vec2(v[0], v[1]);
            fakeMouseFrames_ = 3;
        }
    } else if (a.rfind("uihover:", 0) == 0) {
        auto v = nums(arg("uihover:"));
        if (v.size() >= 2) {
            fakeMouse_ = vec2(v[0], v[1]);
            fakeHover_ = true;
        }
    } else if (a.rfind("mode:", 0) == 0)
        player_.mode = std::atoi(arg("mode:").c_str()) == 2 ? GameMode::Spectator : GameMode::Creative;
    else if (a.rfind("swing:", 0) == 0)
        swing_ = float(std::atof(arg("swing:").c_str()));
    else if (a.rfind("hold:", 0) == 0) {
        player_.inv.slots[player_.selected] = makeStack(ItemId(std::atoi(arg("hold:").c_str())), 1);
    } else if (a.rfind("walk:", 0) == 0) {
        auto v = nums(arg("walk:"));
        if (v.size() >= 3) {
            scriptMove_ = vec3(v[1], v.size() > 3 ? v[3] : 0.0f, v[0]);
            scriptMoveT_ = v[2];
            scriptYawRate_ = v.size() > 4 ? v[4] : 0.0f;
        }
    } else if (a == "creative")
        player_.mode = GameMode::Creative;
    else if (a == "mini")
        miniMode_ = true;
    else if (a == "block")
        miniMode_ = false;
    else if (a.rfind("select:", 0) == 0)
        player_.selected = std::clamp(std::atoi(arg("select:").c_str()), 0, 8);
    else if (a.rfind("give:", 0) == 0) {
        auto v = nums(arg("give:"));
        if (v.size() >= 2) player_.inv.slots[player_.selected] = makeStack(ItemId(v[0]), int(v[1]));
    } else if (a == "place") {
        interact(0);
        doPlace();
    } else if (a == "break") {
        interact(0);
        std::printf("[quadaeria] break: hit=%d block=%s at %d %d %d cells=%zu\n", int(target_.hit), def(target_.block).name, target_.x,
                    target_.y, target_.z, targetCells_.size());
        doBreak();
    } else if (a.rfind("tnt:", 0) == 0) {
        auto v = nums(arg("tnt:"));
        if (v.size() >= 3) {
            int bx = cellOf(player_.pos.x) + int(v[0]), by = cellOf(player_.pos.y) + int(v[1]), bz = cellOf(player_.pos.z) + int(v[2]);
            bx -= floorMod(bx, 2);
            bz -= floorMod(bz, 2);
            for (int d = 0; d < 8; ++d) world_.setBlock(bx + (d & 1), by + ((d >> 1) & 1), bz + (d >> 2), B_TNT);
            sim_.igniteTNT(bx, by, bz, v.size() > 3 ? v[3] : 2.0f);
        }
    } else if (a.rfind("explode:", 0) == 0) {
        interact(0);
        if (target_.hit)
            sim_.explode(vec3((target_.x + 0.5f) * VOX, (target_.y + 0.5f) * VOX, (target_.z + 0.5f) * VOX),
                         float(std::atof(arg("explode:").c_str())), player_);
    } else if (a.rfind("box:", 0) == 0) {
        auto v = nums(arg("box:"));
        if (v.size() >= 7)
            for (int y = int(v[1]); y <= int(v[4]); ++y)
                for (int z = int(v[2]); z <= int(v[5]); ++z)
                    for (int x = int(v[0]); x <= int(v[3]); ++x)
                        sim_.setBlockBatch(x, y, z, uint8_t(v[6]),
                                           (uint8_t(v[6]) == B_WATER || uint8_t(v[6]) == B_LAVA)
                                               ? FLUID_SOURCE_META
                                               : (def(uint8_t(v[6])).fluid ? FLUID_FULL_META : 0));
    } else if (a.rfind("rboxm:", 0) == 0) {
        auto v = nums(arg("rboxm:"));
        int px = cellOf(player_.pos.x), py = cellOf(player_.pos.y + 0.01f), pz = cellOf(player_.pos.z);
        if (v.size() >= 8)
            for (int y = py + int(v[1]); y <= py + int(v[4]); ++y)
                for (int z = pz + int(v[2]); z <= pz + int(v[5]); ++z)
                    for (int x = px + int(v[0]); x <= px + int(v[3]); ++x) world_.setBlock(x, y, z, uint8_t(v[6]), uint8_t(v[7]));
    } else if (a.rfind("toggle:", 0) == 0) {
        auto v = nums(arg("toggle:"));
        int x = cellOf(player_.pos.x) + int(v[0]), y = cellOf(player_.pos.y + 0.01f) + int(v[1]), z = cellOf(player_.pos.z) + int(v[2]);
        uint8_t b = world_.get(x, y, z);
        if (b == B_SWITCH || b == B_SWITCH_ON) world_.setBlock(x, y, z, b == B_SWITCH ? B_SWITCH_ON : B_SWITCH);
    } else if (a.rfind("probe:", 0) == 0) {
        auto v = nums(arg("probe:"));
        int x = cellOf(player_.pos.x) + int(v[0]), y = cellOf(player_.pos.y + 0.01f) + int(v[1]), z = cellOf(player_.pos.z) + int(v[2]);
        uint8_t b = world_.get(x, y, z);
        std::printf("[quadaeria] probe %d %d %d: %s meta %d  %s\n", x, y, z, def(b).name, world_.metaAt(x, y, z),
                    isPowerBlock(b) ? sim_.powerInfo({x, y, z}).c_str() : "");
    } else if (a.rfind("countbox:", 0) == 0) {
        auto v = nums(arg("countbox:"));
        int px = cellOf(player_.pos.x), py = cellOf(player_.pos.y + 0.01f), pz = cellOf(player_.pos.z), n = 0;
        if (v.size() >= 7)
            for (int y = py + int(v[1]); y <= py + int(v[4]); ++y)
                for (int z = pz + int(v[2]); z <= pz + int(v[5]); ++z)
                    for (int x = px + int(v[0]); x <= px + int(v[3]); ++x) n += world_.get(x, y, z) == uint8_t(v[6]);
        std::printf("[quadaeria] countbox: %d\n", n);
    } else if (a.rfind("rbox:", 0) == 0) {
        auto v = nums(arg("rbox:"));
        int px = cellOf(player_.pos.x), py = cellOf(player_.pos.y + 0.01f), pz = cellOf(player_.pos.z);
        if (v.size() >= 7)
            for (int y = py + int(v[1]); y <= py + int(v[4]); ++y)
                for (int z = pz + int(v[2]); z <= pz + int(v[5]); ++z)
                    for (int x = px + int(v[0]); x <= px + int(v[3]); ++x)
                        sim_.setBlockBatch(x, y, z, uint8_t(v[6]),
                                           (uint8_t(v[6]) == B_WATER || uint8_t(v[6]) == B_LAVA)
                                               ? FLUID_SOURCE_META
                                               : (def(uint8_t(v[6])).fluid ? FLUID_FULL_META : 0));
    } else if (a.rfind("mine:", 0) == 0) {
        auto v = nums(arg("mine:"));
        int px = cellOf(player_.pos.x), py = cellOf(player_.pos.y + 0.01f), pz = cellOf(player_.pos.z);
        std::vector<ivec3> cells;
        if (v.size() >= 6)
            for (int y = py + int(v[1]); y <= py + int(v[4]); ++y)
                for (int z = pz + int(v[2]); z <= pz + int(v[5]); ++z)
                    for (int x = px + int(v[0]); x <= px + int(v[3]); ++x)
                        if (world_.get(x, y, z) != B_AIR) cells.push_back({x, y, z});
        sim_.removeCells(cells, false, &player_, player_.pos);
        sim_.structuralEdit(cells, true);
        std::printf("[quadaeria] mined %zu cells, %zu stressed\n", cells.size(), sim_.stressed().size());
    } else if (a.rfind("plant:", 0) == 0 && gen_) {
        auto v = nums(arg("plant:"));
        if (v.size() >= 3) {
            int x = cellOf(player_.pos.x) + int(v[1]), z = cellOf(player_.pos.z) + int(v[2]);
            int y = world_.heightAtTop(x, z);
            uint32_t hseed = v.size() > 3 ? uint32_t(v[3]) : uint32_t(x * 7919 + z * 104729);
            for (int cz = floorDiv(z - 40, CS); cz <= floorDiv(z + 40, CS); ++cz)
                for (int cx = floorDiv(x - 40, CS); cx <= floorDiv(x + 40, CS); ++cx)
                    if (Column* col = world_.column(cx, cz)) {
                        for (auto& sec : col->sec)
                            if (!sec.blocks) {
                                sec.blocks.reset(new uint8_t[CS3]);
                                std::memset(sec.blocks.get(), sec.fillBlock, CS3);
                            }
                        gen_->plantTree(*col, int(v[0]), x, y, z, hseed);
                        for (auto& sec : col->sec) {
                            sec.physicsBricks.reset();
                            sec.compact();
                        }
                        col->editStamp++;
                        col->lightStamp++;
                        for (int s2 = 0; s2 < NSEC; ++s2) col->meshDirty[s2] = true;
                        streamer_.requestRelight(colKey(cx, cz));
                    }
        }
    } else if (a.rfind("up:", 0) == 0) {
        player_.pos.y += float(std::atof(arg("up:").c_str()));
        player_.flying = true;
    } else if (a == "cave") {
        int px = cellOf(player_.pos.x), pz = cellOf(player_.pos.z);
        bool found = false;
        for (int r = 0; r < 200 && !found; r += 4)
            for (int k = 0; k < 16 && !found; ++k) {
                float ang = k * 6.2831853f / 16;
                int x = px + int(std::cos(ang) * r), z = pz + int(std::sin(ang) * r);
                if (!world_.loaded(x, z)) continue;
                int top = world_.heightAtTop(x, z);
                while (top > 1) {
                    uint8_t g = world_.get(x, top - 1, z);
                    if (g != B_AIR && def(g).render == Render::Opaque && def(g).sound != Sound::Wood) break;
                    --top;
                }
                for (int y = top - 24; y > 30 && !found; --y) {
                    bool open = true;
                    for (int dy = 0; dy < 8 && open; ++dy)
                        for (int d = 0; d < 9 && open; ++d)
                            if (world_.get(x + (d % 3 - 1) * 3, y + dy, z + (d / 3 - 1) * 3) != B_AIR) open = false;
                    if (open) {
                        player_.pos = vec3((x + 0.5f) * VOX, (y + 1) * VOX, (z + 0.5f) * VOX);
                        player_.flying = true;
                        player_.vel = {};
                        found = true;
                    }
                }
            }
        std::printf("[quadaeria] cave %s\n", found ? "found" : "not found");
    } else if (a.rfind("findblock:", 0) == 0) {
        int id = std::atoi(arg("findblock:").c_str());
        int px = cellOf(player_.pos.x), pz = cellOf(player_.pos.z);
        bool found = false;
        for (int r = 0; r < 160 && !found; r += 2)
            for (int dz = -r; dz <= r && !found; dz += 2)
                for (int dx = -r; dx <= r && !found; dx += 2) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    int x = px + dx, z = pz + dz;
                    if (!world_.loaded(x, z)) continue;
                    for (int y = 8; y < WH - 8 && !found; ++y) {
                        if (world_.get(x, y, z) != id) continue;
                        for (int k = 0; k < 8 && !found; ++k) {
                            float ang = k * 0.785398f;
                            for (int dist = 10; dist >= 4 && !found; dist -= 2) {
                                int ex = x + int(std::cos(ang) * dist), ez = z + int(std::sin(ang) * dist);
                                int ey = y + 2;
                                bool open = true;
                                for (int j = -1; j <= 4 && open; ++j) open = world_.get(ex, ey + j, ez) == B_AIR;
                                if (!open) continue;
                                player_.pos = vec3((ex + 0.5f) * VOX, ey * VOX, (ez + 0.5f) * VOX);
                                player_.flying = true;
                                player_.vel = {};
                                vec3 d = vec3((x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX) - player_.eye();
                                player_.yaw = std::atan2(d.x, -d.z);
                                player_.pitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
                                found = true;
                                std::printf("[quadaeria] found block %d at %d %d %d\n", id, x, y, z);
                            }
                        }
                    }
                }
        if (!found) std::printf("[quadaeria] block %d not found\n", id);
    } else if (a.rfind("torches:", 0) == 0) {
        int R = std::atoi(arg("torches:").c_str());
        int px = cellOf(player_.pos.x), py = cellOf(player_.pos.y), pz = cellOf(player_.pos.z), n = 0;
        for (int dz = -R; dz <= R; dz += 5)
            for (int dx = -R; dx <= R; dx += 5)
                for (int dy = -12; dy <= 12; ++dy) {
                    int x = px + dx, y = py + dy, z = pz + dz;
                    if (world_.get(x, y, z) == B_AIR && def(world_.get(x, y - 1, z)).solid &&
                        def(world_.get(x, y - 1, z)).render == Render::Opaque) {
                        world_.setBlock(x, y, z, B_TORCH);
                        ++n;
                        break;
                    }
                }
        std::printf("[quadaeria] placed %d torches\n", n);
    } else if (a == "mouth" && gen_) {
        int pcx = floorDiv(cellOf(player_.pos.x), 128), pcz = floorDiv(cellOf(player_.pos.z), 128);
        float bestD = 1e9f, bx = 0, by = 0, bz = 0, byaw = 0;
        for (int dz = -3; dz <= 3; ++dz)
            for (int dx = -3; dx <= 3; ++dx) {
                float ex, ey, ez, yaw;
                bool hill;
                if (!gen_->entranceAt(pcx + dx, pcz + dz, ex, ey, ez, yaw, hill) || !hill) continue;
                float d = float(dx * dx + dz * dz);
                if (d < bestD) {
                    bestD = d;
                    bx = ex;
                    by = ey;
                    bz = ez;
                    byaw = yaw;
                }
            }
        if (bestD < 1e8f) {
            float back = std::getenv("QC_MOUTH_BACK") ? float(std::atof(std::getenv("QC_MOUTH_BACK"))) : 26.0f;
            float ox = bx - std::cos(byaw) * back, oz = bz - std::sin(byaw) * back;
            int gt = world_.heightAtTop(int(std::floor(ox)), int(std::floor(oz)));
            float oy = std::max(by + 3.0f, float(gt) + 3.0f);
            player_.pos = vec3((ox + 0.5f) * VOX, oy * VOX, (oz + 0.5f) * VOX);
            player_.flying = true;
            player_.yaw = std::atan2(std::cos(byaw), -std::sin(byaw));
            player_.pitch = std::atan2((by - 1.0f) - (oy + 3.0f), back);
            int air = 0, rock = 0;
            for (int k = 0; k < 20; ++k) {
                int cx = int(std::floor(bx + std::cos(byaw) * k)), cz = int(std::floor(bz + std::sin(byaw) * k));
                for (int dy = -1; dy <= 1; ++dy) {
                    uint8_t b = world_.get(cx, int(by) + dy, cz);
                    if (b == B_AIR)
                        ++air;
                    else
                        ++rock;
                }
            }
            std::printf("[quadaeria] cave mouth at %.0f %.0f %.0f: %d open, %d solid along the way in\n", bx, by, bz, air, rock);
            for (int k = -2; k <= 14; k += 2) {
                int cx = int(std::floor(bx + std::cos(byaw) * k)), cz = int(std::floor(bz + std::sin(byaw) * k));
                std::string col;
                for (int dy = -4; dy <= 12; ++dy) col += world_.get(cx, int(by) + dy, cz) == B_AIR ? '.' : '#';
                std::printf("   k=%3d %s\n", k, col.c_str());
            }
        }
    } else if (a == "entrance") {
        int px = cellOf(player_.pos.x), pz = cellOf(player_.pos.z);
        bool found = false;
        auto groundTop = [&](int x, int z) {
            int t = world_.heightAtTop(x, z);
            while (t > 1) {
                uint8_t g = world_.get(x, t - 1, z);
                if (g != B_AIR && (def(g).render == Render::Opaque || def(g).fluid) && def(g).sound != Sound::Wood) break;
                --t;
            }
            return t;
        };
        for (int r = 0; r < 220 && !found; r += 3)
            for (int k = 0; k < 24 && !found; ++k) {
                float ang = k * 6.2831853f / 24;
                int x = px + int(std::cos(ang) * r), z = pz + int(std::sin(ang) * r);
                if (!world_.loaded(x, z) || !world_.loaded(x + 12, z + 12) || !world_.loaded(x - 12, z - 12)) continue;
                int t = groundTop(x, z);
                if (t < SEA + 4 || def(world_.get(x, t - 1, z)).fluid) continue;
                int higher = 0;
                for (int d = 0; d < 8; ++d) {
                    float a2 = d * 0.785398f;
                    int nt = groundTop(x + int(std::cos(a2) * 9), z + int(std::sin(a2) * 9));
                    if (nt >= t + 7) ++higher;
                }
                if (higher < 5) continue;
                for (int d = 0; d < 8 && !found; ++d) {
                    float a2 = d * 0.785398f;
                    int ex = x + int(std::cos(a2) * 16), ez = z + int(std::sin(a2) * 16);
                    int et = groundTop(ex, ez);
                    if (def(world_.get(ex, et - 1, ez)).fluid) continue;
                    player_.pos = vec3((ex + 0.5f) * VOX, (et + 6) * VOX, (ez + 0.5f) * VOX);
                    player_.flying = true;
                    player_.vel = {};
                    vec3 dv = vec3((x + 0.5f) * VOX, (t - 2) * VOX, (z + 0.5f) * VOX) - player_.eye();
                    player_.yaw = std::atan2(dv.x, -dv.z);
                    player_.pitch = std::atan2(dv.y, std::sqrt(dv.x * dv.x + dv.z * dv.z));
                    found = true;
                    std::printf("[quadaeria] entrance at %d %d %d\n", x, t, z);
                }
            }
        if (!found) std::printf("[quadaeria] no entrance found\n");
    } else if (a.rfind("y:", 0) == 0) {
        player_.pos.y = float(std::atof(arg("y:").c_str())) * VOX;
        player_.flying = true;
        player_.vel = {};
        player_.fallStart = player_.pos.y;
        int x = cellOf(player_.pos.x), z = cellOf(player_.pos.z);
        std::printf("[quadaeria] at %d %d: top %d, block at eye %d\n", x, z, world_.heightAtTop(x, z),
                    world_.get(x, cellOf(player_.eye().y), z));
    } else if (a.rfind("shot:", 0) == 0)
        screenshotPath = arg("shot:");
    else if (a == "debug")
        showDebug_ = !showDebug_;
    else if (a.rfind("countleaves:", 0) == 0 || a.rfind("leafpoke:", 0) == 0) {
        bool poke = a[0] == 'l';
        int R = std::atoi(a.substr(a.find(':') + 1).c_str()), n = 0;
        int px = cellOf(player_.pos.x), py = cellOf(player_.pos.y), pz = cellOf(player_.pos.z);
        for (int y = py - 8; y <= py + R * 2; ++y)
            for (int z = pz - R; z <= pz + R; ++z)
                for (int x = px - R; x <= px + R; ++x)
                    if (def(world_.get(x, y, z)).render == Render::Leaves) {
                        ++n;
                        if (poke) sim_.leafEvent(x, y, z);
                    }
        std::printf("[quadaeria] leaves within %d: %d\n", R, n);
    } else if (a.rfind("surface:", 0) == 0) {
        int top = world_.heightAtTop(cellOf(player_.pos.x), cellOf(player_.pos.z));
        player_.pos.y = top * VOX + float(std::atof(arg("surface:").c_str()));
        player_.vel = {};
    } else if ((a == "ruin" || a == "oldruin") && gen_) {
        int pcx = floorDiv(cellOf(player_.pos.x), RUIN_REGION), pcz = floorDiv(cellOf(player_.pos.z), RUIN_REGION);
        float best = 1e18f;
        int bx0 = 0, bz0 = 0, bx1 = 0, bz1 = 0;
        for (int dz = -6; dz <= 6; ++dz)
            for (int dx = -6; dx <= 6; ++dx) {
                int x0, z0, x1, z1;
                bool old;
                if (!gen_->ruinSite(pcx + dx, pcz + dz, x0, z0, x1, z1, old) || (a == "oldruin" && !old)) continue;
                Biome rb = gen_->surface((x0 + x1) / 2, (z0 + z1) / 2).biome;
                if (a == "oldruin" && (rb == Biome::Desert || rb == Biome::Badlands)) continue;
                float d = float(dx * dx + dz * dz);
                if (d < best) {
                    best = d;
                    bx0 = x0;
                    bz0 = z0;
                    bx1 = x1;
                    bz1 = z1;
                }
            }
        if (best < 1e17f) {
            float cx = (bx0 + bx1) * 0.5f, cz = (bz0 + bz1) * 0.5f;
            player_.pos = vec3((cx + 0.5f) * VOX, 0, (cz + 26.0f) * VOX);
            player_.pos.y = 110.0f;
            player_.flying = true;
            player_.yaw = 0;
            player_.pitch = -0.35f;
            std::printf("[quadaeria] ruin at %.0f %.0f\n", cx, cz);
        }
    } else if (a == "hud")
        hideHud_ = !hideHud_;
    else if (a.rfind("screen:", 0) == 0) {
        std::string s = arg("screen:");
        if (s == "inventory")
            setScreen(Screen::Inventory);
        else if (s == "pause")
            setScreen(Screen::Pause);
        else if (s == "options") {
            optionsReturn_ = worldLoaded_ ? Screen::Pause : Screen::Title;
            setScreen(Screen::Options);
        } else if (s == "title") {
            if (worldLoaded_) closeWorld(true);
            setScreen(Screen::Title);
        } else if (s == "worlds") {
            refreshWorldList();
            setScreen(Screen::Worlds);
        } else if (s == "create") {
            newName_ = "New World";
            newSeed_.clear();
            setScreen(Screen::Create);
        } else if (s == "worldgen")
            setScreen(Screen::CreateWorldGen);
        else if (s == "confirm")
            setScreen(Screen::Confirm);
        else if (s == "play")
            setScreen(Screen::Playing);
        else if (s == "controls") {
            optionsReturn_ = worldLoaded_ ? Screen::Pause : Screen::Title;
            setScreen(Screen::Controls);
        }
    } else if (a.rfind("dist:", 0) == 0) {
        settings_.renderDist = std::atoi(arg("dist:").c_str());
    } else if (a.rfind("optab:", 0) == 0) {
        optionsTab_ = std::atoi(arg("optab:").c_str());
    } else if (a == "potato") {
        settings_.potato();
        applySettings();
    } else if (a == "quit")
        quit = true;
    else if (a.rfind("biome:", 0) == 0 && gen_) {
        std::string want = arg("biome:");
        int px = cellOf(player_.pos.x), pz = cellOf(player_.pos.z);
        bool found = false;
        for (int r = 0; r < 8000 && !found; r += 24)
            for (int k = 0; k < std::max(8, r / 12) && !found; ++k) {
                float ang = k * 6.2831853f / std::max(8, r / 12);
                int x = px + int(std::cos(ang) * r), z = pz + int(std::sin(ang) * r);
                SurfaceInfo si = gen_->surface(x, z);
                std::string n = biomeName(si.biome);
                for (char& ch : n)
                    if (ch == ' ') ch = '_';
                if (n == want) {
                    player_.pos = vec3((x + 0.5f) * VOX, (std::max(si.height, SEA) + 4) * VOX, (z + 0.5f) * VOX);
                    player_.vel = {};
                    player_.flying = true;
                    player_.fallStart = player_.pos.y;
                    found = true;
                }
            }
        std::printf("[quadaeria] biome %s %s at %d %d (y %.1f)\n", want.c_str(), found ? "found" : "not found", cellOf(player_.pos.x),
                    cellOf(player_.pos.z), player_.pos.y / VOX);
    } else if (a == "cuttree") {
        int px = cellOf(player_.pos.x), pz = cellOf(player_.pos.z);
        bool done = false;
        for (int r = 4; r < 60 && !done; r += 2)
            for (int dz = -r; dz <= r && !done; dz += 2)
                for (int dx = -r; dx <= r && !done; dx += 2) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    int x = px + dx, z = pz + dz;
                    int top = world_.heightAtTop(x, z);
                    for (int y = top; y > top - 30 && !done; --y) {
                        uint8_t b = world_.get(x, y, z);
                        if (b != B_LOG && b != B_BIRCH_LOG && b != B_SPRUCE_LOG) continue;
                        int by = y;
                        while (world_.get(x, by - 1, z) == b) --by;
                        if (!world_.solidAt(x, by - 1, z)) continue;
                        int bx = x - floorMod(x, 2), bz = z - floorMod(z, 2);
                        vec3 base((bx + 1) * VOX, (by + 2) * VOX, (bz + 1) * VOX);
                        player_.pos = base + vec3(-7.5f, -1.0f, -4.5f);
                        int g = world_.heightAtTop(cellOf(player_.pos.x), cellOf(player_.pos.z));
                        player_.pos.y = std::max(player_.pos.y, g * VOX + 0.01f) + 1.5f;
                        player_.flying = true;
                        vec3 d = base + vec3(0, 3.0f, 0) - player_.eye();
                        player_.yaw = std::atan2(d.x, -d.z);
                        player_.pitch = std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z));
                        std::vector<ivec3> cut;
                        for (int cy = by + 2; cy <= by + 3; ++cy)
                            for (int cz = bz - 2; cz <= bz + 3; ++cz)
                                for (int cx = bx - 2; cx <= bx + 3; ++cx)
                                    if (world_.get(cx, cy, cz) == b) cut.push_back({cx, cy, cz});
                        sim_.removeCells(cut, false, &player_, base);
                        std::printf("[quadaeria] cut tree at %d %d %d (%zu cells)\n", bx, by, bz, cut.size());
                        done = true;
                    }
                }
    } else if (a.rfind("expect:", 0) == 0) {
        auto v = nums(arg("expect:"));
        if (v.size() >= 2) {
            int have = player_.inv.countOf(ItemId(v[0]));
            std::printf("[quadaeria] %s: item %d count %d (want >= %d)\n", have >= int(v[1]) ? "PASS" : "FAIL", int(v[0]), have, int(v[1]));
        }
    } else if (a == "stats") {
        std::printf(
            "[quadaeria] cpu: update %.2f ms, render submit %.2f ms, light volume %.2f ms (%d uploads), streamer %.2f ms, drain %.2f ms\n",
            msUpdate_, msRender_, msLightVol_, lightVol_.uploadsLastFrame, msStream_, msDrain_);
        {
            const auto& g = renderer_.gpu;
            std::printf("[quadaeria] gpu %.2f ms: shadow %.2f terrain %.2f entities %.2f water %.2f post %.2f ui %.2f | draws %d (%s) "
                        "shadow redraws %d\n",
                        g.total, g.shadow, g.terrain, g.entities, g.water, g.post, g.ui, renderer_.stats.drawCalls,
                        renderer_.multiDraw() ? "multi" : "single", renderer_.stats.shadowRenders);
        }
        std::printf("[quadaeria] sim cpu: checks %.2f ms, fluids %.2f ms, rigid bodies %.2f ms\n", sim_.stats.checksMs, sim_.stats.fluidMs,
                    sim_.stats.bodyMs);
        std::printf("[quadaeria] fluid cells changed last tick: %d\n", sim_.stats.fluidChanged);
        std::printf("[quadaeria] bodies created %d, settled %d, stressed %zu\n", sim_.stats.bodiesCreated, sim_.stats.settled,
                    sim_.stressed().size());
        for (const auto& bb : sim_.bodies())
            std::printf("[quadaeria]   body %d age %.1f count %d vel %.3f ang %.3f rest %.2f low %.2f pos %.2f %.2f %.2f\n", bb.id, bb.age,
                        bb.count, length(bb.vel), length(bb.angVel), bb.rest, bb.lowEnergy, bb.pos.x, bb.pos.y, bb.pos.z);
        std::printf("[quadaeria] wall frame %.2f ms\n", wallMs_);
        std::printf("[quadaeria] stats: fps %.1f ents %d bodies %d debris %d fluids %d particles %d sections %d/%d quads %d\n", fps_,
                    sim_.stats.entities, sim_.stats.bodies, sim_.stats.debris, sim_.stats.fluids, sim_.stats.particles,
                    renderer_.stats.drawn, renderer_.stats.sections, renderer_.stats.quads);
        std::printf("[quadaeria] sim tiers near/mid/far/dormant: entities %d/%d/%d/%d  bodies %d/%d/%d/%d | contacts %llu -> %llu solver, "
                    "pairs %llu/%llu\n",
                    sim_.stats.entityTier[0], sim_.stats.entityTier[1], sim_.stats.entityTier[2], sim_.stats.entityTier[3],
                    sim_.stats.bodyTier[0], sim_.stats.bodyTier[1], sim_.stats.bodyTier[2], sim_.stats.bodyTier[3],
                    (unsigned long long)sim_.stats.rawContacts, (unsigned long long)sim_.stats.solverContacts,
                    (unsigned long long)sim_.stats.bodyPairs, (unsigned long long)sim_.stats.broadphasePairs);
        std::printf("[quadaeria] player pos %.1f %.1f %.1f  vel %.2f %.2f %.2f ground %d fly %d\n", player_.pos.x, player_.pos.y,
                    player_.pos.z, player_.vel.x, player_.vel.y, player_.vel.z, int(player_.onGround), int(player_.flying));
    }
}

}
