#include "app/Game.h"
#include "sim/Models.h"
#include <fstream>
#include <cstdio>
#include <iomanip>

namespace qc {
namespace {
bool anchored(const World& w, ivec3 c) { return !w.loaded(c.x, c.z) || def(w.get(c.x, c.y, c.z)).collides; }
float segmentDistance(vec3 p, vec3 a, vec3 b, float& t) {
    vec3 d = b - a;
    t = std::clamp(dot(p - a, d) / std::max(0.001f, lengthSq(d)), 0.0f, 1.0f);
    return length(p - a - d * t);
}
AABB hull(vec3 p, float yaw, float height = 0.6f) {
    float c = fabs(cos(yaw)), s = fabs(sin(yaw));
    vec3 h(0.5625f * c + 0.9375f * s, 0, 0.9375f * c + 0.5625f * s);
    return {p - h, p + h + vec3(0, height, 0)};
}
}

void Game::loadAdventure() {
    skiffs_.clear();
    ropes_.clear();
    ridingSkiff_ = ridingRope_ = -1;
    hooked_ = ropePending_ = false;
    lanternOn_ = true;
    std::ifstream f(worldDir_ + "/expedition.txt");
    std::string tag;
    while (f >> tag) {
        if (tag == "boat") {
            Skiff b;
            if (!(f >> b.pos.x >> b.pos.y >> b.pos.z >> b.yaw)) break;
            if (skiffs_.size() < 64 && std::isfinite(lengthSq(b.pos)) && std::isfinite(b.yaw)) skiffs_.push_back(b);
        } else if (tag == "rope") {
            RopeSpan r;
            if (!(f >> r.a.x >> r.a.y >> r.a.z >> r.b.x >> r.b.y >> r.b.z >> r.ca.x >> r.ca.y >> r.ca.z >> r.cb.x >> r.cb.y >> r.cb.z))
                break;
            if (ropes_.size() < 128 && std::isfinite(lengthSq(r.a)) && std::isfinite(lengthSq(r.b)) && length(r.a - r.b) <= 32)
                ropes_.push_back(r);
        } else {
            std::string rest;
            std::getline(f, rest);
        }
    }
}
void Game::saveAdventure() const {
    std::string path = worldDir_ + "/expedition.txt", tmp = path + ".tmp";
    std::ofstream f(tmp);
    f << std::setprecision(9);
    for (const auto& b : skiffs_) f << "boat " << b.pos.x << ' ' << b.pos.y << ' ' << b.pos.z << ' ' << b.yaw << '\n';
    for (const auto& r : ropes_)
        f << "rope " << r.a.x << ' ' << r.a.y << ' ' << r.a.z << ' ' << r.b.x << ' ' << r.b.y << ' ' << r.b.z << ' ' << r.ca.x << ' '
          << r.ca.y << ' ' << r.ca.z << ' ' << r.cb.x << ' ' << r.cb.y << ' ' << r.cb.z << '\n';
    f.close();
    if (f) std::rename(tmp.c_str(), path.c_str());
}

bool Game::useAdventure() {
    ItemStack& held = player_.held();
    if (ridingSkiff_ < 0 && (input_->mousePressed(Mouse::Right) || input_->mousePressed(Mouse::Left))) {
        vec3 eye = player_.eye(), dir = player_.look();
        for (size_t i = 0; i < skiffs_.size(); ++i) {
            vec3 center = skiffs_[i].pos + vec3(0, 0.25f, 0), delta = center - eye;
            float along = dot(delta, dir);
            if (length(center - player_.eye()) > 3.8f || along < 0 || length(delta - dir * along) > 0.85f) continue;
            RayHit wall = raycast(world_, eye, dir, along);
            if (wall.hit && wall.dist < along - 0.85f && def(wall.block).collides) continue;
            if (input_->mousePressed(Mouse::Left)) {
                skiffs_.erase(skiffs_.begin() + i);
            } else {
                vec3 seat = skiffs_[i].pos + vec3(0, 0.25f, 0);
                if (!boxHitsWorld(world_, boxAt(seat, player_.width, player_.height))) {
                    ridingSkiff_ = int(i);
                    player_.pos = seat;
                    player_.vel = skiffs_[i].vel;
                    player_.flying = player_.gliding = false;
                }
            }
            return true;
        }
    }
    if (held.id < I_SKIFF || held.id > I_GLIDER) return false;
    if (!input_->mousePressed(Mouse::Right)) return false;
    swing_ = 1;
    if (held.id == I_LANTERN) {
        if (player_.sneaking || !target_.hit) {
            lanternOn_ = !lanternOn_;
            return true;
        }
        auto cells = placeCells(target_, true, B_LANTERN);
        if (!cells.empty()) {
            auto p = cells.front();
            uint8_t hanging = target_.ny < 0 ? 1 : 0;
            world_.setBlock(p.x, p.y, p.z, B_LANTERN, hanging);
            sim_.sounds.push_back({Snd::Place, vec3(p.x, p.y, p.z) * VOX, 0.6f, 1});
        }
        return true;
    }
    if (held.id == I_GLIDER) return true;
    if (held.id == I_GRAPPLE && hooked_) {
        hooked_ = false;
        return true;
    }
    if (held.id == I_ROPE && player_.sneaking) {
        ropePending_ = false;
        return true;
    }
    vec3 eye = player_.eye(), dir = player_.look();
    RayHit h = raycast(world_, eye, dir, held.id == I_SKIFF ? 7.0f : 30.0f, held.id == I_SKIFF);
    if (!h.hit || !world_.loaded(h.x, h.z)) return true;
    vec3 point = eye + dir * h.dist + vec3(h.nx, h.ny, h.nz) * 0.035f;
    if (held.id == I_SKIFF) {
        if (skiffs_.size() >= 64 || (h.block != B_WATER && !def(h.block).collides)) return true;
        float surface = h.block == B_WATER ? (h.y + float(fluidSurfaceAmountOf(B_WATER, world_.metaAt(h.x, h.y, h.z))) / MAX_FLUID) * VOX
                                           : (h.y + 1) * VOX;
        Skiff b;
        b.pos = {point.x, surface + 0.01f, point.z};
        b.yaw = player_.yaw;
        if (h.block != B_WATER && h.ny == 0) b.pos += vec3(h.nx, 0, h.nz) * 1.1f;
        if (boxHitsWorld(world_, hull(b.pos, b.yaw, 0.8f))) return true;
        for (const auto& other : skiffs_)
            if (length(other.pos - b.pos) < 2.0f) return true;
        skiffs_.push_back(b);
        sim_.sounds.push_back({Snd::Splash, b.pos, 0.8f, 1});
    } else if (held.id == I_GRAPPLE) {
        if (!def(h.block).collides || ridingSkiff_ >= 0 || ridingRope_ >= 0) return true;
        hooked_ = true;
        hookPoint_ = point;
        hookCell_ = {h.x, h.y, h.z};
        hookLength_ = std::clamp(length(point - (player_.pos + vec3(0, 1, 0))), 1.5f, 30.0f);
        player_.flying = false;
        sim_.sounds.push_back({Snd::Click, point, 0.9f, 1.5f});
    } else if (held.id == I_ROPE) {
        if (!def(h.block).collides) return true;
        if (!ropePending_) {
            ropePending_ = true;
            ropeStart_ = point;
            ropeStartCell_ = {h.x, h.y, h.z};
        } else {
            float d = length(point - ropeStart_);
            if (d < 2 || d > 30 || ropes_.size() >= 128 || !anchored(world_, ropeStartCell_)) return true;
            vec3 rd = (point - ropeStart_) / d;
            RayHit obstruction = raycast(world_, ropeStart_ + rd * 0.12f, rd, d - 0.24f);
            if (obstruction.hit) return true;
            ropes_.push_back({ropeStart_, point, ropeStartCell_, {h.x, h.y, h.z}});
            ropePending_ = false;
            sim_.sounds.push_back({Snd::Click, point, 0.7f, 0.8f});
        }
    }
    return true;
}

bool Game::toggleRide(bool retrieve) {
    if (ridingSkiff_ >= 0) {
        auto& b = skiffs_[size_t(ridingSkiff_)];
        for (int side : {1, -1})
            for (float up : {0.6f, 1.1f, 1.6f}) {
                vec3 p = b.pos + modelYaw(-b.yaw) * vec3(float(side) * 1.2f, up, 0);
                if (!boxHitsWorld(world_, boxAt(p, player_.width, player_.height))) {
                    player_.pos = p;
                    player_.vel = b.vel + vec3(0, 2, 0);
                    player_.fallStart = p.y;
                    ridingSkiff_ = -1;
                    return true;
                }
            }
        return false;
    }
    if (ridingRope_ >= 0) {
        ridingRope_ = -1;
        return true;
    }
    if (hooked_) {
        hooked_ = false;
        return true;
    }
    float best = 3.3f;
    int boat = -1;
    for (size_t i = 0; i < skiffs_.size(); ++i) {
        float d = length(skiffs_[i].pos - player_.pos);
        if (d < best) {
            best = d;
            boat = int(i);
        }
    }
    if (boat >= 0) {
        if (retrieve)
            skiffs_.erase(skiffs_.begin() + boat);
        else {
            auto& b = skiffs_[size_t(boat)];
            vec3 p = b.pos + vec3(0, 0.25f, 0);
            if (boxHitsWorld(world_, boxAt(p, player_.width, player_.height))) return false;
            ridingSkiff_ = boat;
            player_.flying = false;
            player_.gliding = false;
        }
        return true;
    }
    for (size_t i = 0; i < ropes_.size(); ++i) {
        float t;
        auto& r = ropes_[i];
        if (segmentDistance(player_.eye(), r.a, r.b, t) > 2.0f) continue;
        if (retrieve)
            ropes_.erase(ropes_.begin() + i);
        else {
            vec3 p = r.a + (r.b - r.a) * t - vec3(0, 1.90f, 0);
            if (boxHitsWorld(world_, boxAt(p, player_.width, player_.height))) continue;
            player_.pos = p;
            ridingRope_ = int(i);
            ropeT_ = t;
            ropeSpeed_ = 0;
            player_.flying = false;
        }
        return true;
    }
    return false;
}

void Game::adventureStep(float dt, const PlayerInput& in) {
    for (size_t i = 0; i < skiffs_.size(); ++i) {
        auto& b = skiffs_[i];
        if (!world_.loaded(cellOf(b.pos.x), cellOf(b.pos.z)) || lengthSq(b.pos - player_.pos) > 128 * 128) continue;
        bool riding = int(i) == ridingSkiff_;
        AABB box = hull(b.pos, b.yaw);
        FluidInfo water = fluidAt(world_, box.offset({0, -0.15f, 0}), b.pos.y + 0.4f);
        float surface = 0;
        int wet = 0;
        const vec3 probes[] = {{0, 0, 0}, {-0.38f, 0, -0.65f}, {0.38f, 0, -0.65f}, {-0.38f, 0, 0.65f}, {0.38f, 0, 0.65f}};
        for (auto off : probes) {
            vec3 p = b.pos + modelYaw(-b.yaw) * off;
            for (int y = cellOf(b.pos.y + 0.8f); y >= cellOf(b.pos.y - 0.9f); --y) {
                int x = cellOf(p.x), z = cellOf(p.z);
                if (world_.get(x, y, z) != B_WATER) continue;
                float waterline = (y + world_.fluidAmount(x, y, z) / float(MAX_FLUID)) * VOX;
                auto column = world_.column(floorDiv(x, CS), floorDiv(z, CS));
                int ci = floorMod(z, CS) * CS + floorMod(x, CS);
                if (y == SEA - 1 && fluidIsSource(world_.metaAt(x, y, z)) && column && column->riverX[ci] == 0 && column->riverZ[ci] == 0) {
                    int depth = 0;
                    while (depth < 8 && world_.get(x, y - depth, z) == B_WATER) ++depth;
                    float t = float(clock_);
                    float swell =
                        std::sin(p.x * .19f + p.z * .114f - t * .85f) * .36f + std::sin(p.x * .086f - p.z * .162f - t * .57f) * .10f;
                    waterline += std::floor(swell * (depth / 8.0f) * 32 + .5f) / 32 - .012f;
                }
                surface += waterline;
                ++wet;
                break;
            }
        }
        bool afloat = wet > 0;
        if (afloat) {
            float target = surface / wet - 0.12f;
            b.vel.y += (target - b.pos.y) * 30.0f * dt;
            b.vel.y *= exp(-dt * 8.0f);
        } else
            b.vel.y = std::max(-25.0f, b.vel.y - 18.0f * dt);
        b.vel += water.flow * (dt * 0.35f);
        if (riding) {
            if (in.jumpPressed) {
                toggleRide();
                riding = false;
            } else {
                b.yawVel += (in.move.x * 1.7f - b.yawVel) * (1.0f - exp(-dt * 8.0f));
                float turn = b.yawVel * dt, nextYaw = b.yaw + turn;
                if (!boxHitsWorld(world_, hull(b.pos, nextYaw))) {
                    b.yaw = nextYaw;
                    player_.yaw += turn;
                }
                vec3 forward(sin(b.yaw), 0, -cos(b.yaw));
                b.vel += forward * (in.move.z * dt * (afloat ? 7.0f : 1.5f));
                b.stroke += std::fabs(in.move.z) * dt * 5;
            }
        } else {
            b.yawVel *= exp(-dt * 6.0f);
            vec3 push = b.pos - player_.pos;
            push.y = 0;
            if (lengthSq(push) < 1.3f * 1.3f && std::fabs(player_.pos.y - b.pos.y) < 0.7f) b.vel += normalize(push, {1, 0, 0}) * dt * 2.0f;
        }
        vec3 forward(sin(b.yaw), 0, -cos(b.yaw)), lateral(cos(b.yaw), 0, sin(b.yaw));
        float fwd = dot(b.vel, forward) * exp(-dt * (afloat ? 1.25f : 7.0f));
        float side = dot(b.vel, lateral) * exp(-dt * (afloat ? 4.5f : 7.0f));
        vec3 planar = forward * std::clamp(fwd, -2.5f, 5.6f) + lateral * side;
        b.vel.x = planar.x;
        b.vel.z = planar.z;
        box = hull(b.pos, b.yaw);
        vec3 before = b.pos;
        MoveResult hit = moveBox(world_, box, b.vel * dt, 0, false);
        b.pos = {box.center().x, box.min.y, box.center().z};
        if (hit.hitX) b.vel.x = 0;
        if (hit.hitZ) b.vel.z = 0;
        if (hit.hitY) b.vel.y = 0;
        if (riding) {
            vec3 seat = b.pos + vec3(0, 0.25f, 0);
            if (boxHitsWorld(world_, boxAt(seat, player_.width, player_.height))) {
                b.pos = before;
                b.vel.x = b.vel.z = 0;
                seat = b.pos + vec3(0, 0.25f, 0);
            }
            player_.pos = seat;
            player_.vel = b.vel;
            player_.externalVel = {};
            player_.platformVel = {};
            player_.stepSmooth = player_.stepVelocity = 0;
            player_.fallStart = seat.y;
            player_.inWater = player_.headInWater = player_.onGround = false;
            player_.bobAmount *= exp(-dt * 12.0f);
        }
    }
    for (size_t i = 0; i < ropes_.size();) {
        if (anchored(world_, ropes_[i].ca) && anchored(world_, ropes_[i].cb)) {
            ++i;
            continue;
        }
        if (ridingRope_ == int(i))
            ridingRope_ = -1;
        else if (ridingRope_ > int(i))
            --ridingRope_;
        vec3 drop = (ropes_[i].a + ropes_[i].b) * 0.5f;
        sim_.spawnItem(drop, I_ROPE, 1);
        ropes_.erase(ropes_.begin() + i);
    }
    if (ridingRope_ >= 0) {
        if (in.jumpPressed) {
            ridingRope_ = -1;
            player_.vel.y += 3;
            return;
        }
        const auto& r = ropes_[size_t(ridingRope_)];
        vec3 d = r.b - r.a;
        float len = length(d);
        vec3 n = d / len;
        ropeSpeed_ += (-n.y * 14.0f + in.move.z * 5.0f) * dt;
        ropeSpeed_ *= exp(-dt * (in.sneak ? 5.0f : 0.2f));
        ropeSpeed_ = std::clamp(ropeSpeed_, -12.0f, 12.0f);
        float nt = std::clamp(ropeT_ + ropeSpeed_ * dt / len, 0.025f, 0.975f);
        vec3 want = r.a + d * nt - vec3(0, 1.90f, 0);
        AABB box = player_.box();
        auto hit = moveBox(world_, box, want - player_.pos, 0, false);
        player_.pos = {box.center().x, box.min.y, box.center().z};
        if (hit.hitX || hit.hitY || hit.hitZ) {
            ridingRope_ = -1;
            player_.vel = {};
            return;
        }
        ropeT_ = nt;
        player_.vel = n * ropeSpeed_;
        player_.fallStart = player_.pos.y;
        player_.inWater = player_.headInWater = false;
        if (nt <= 0.025f || nt >= 0.975f) ropeSpeed_ = 0;
    }
    if (!hooked_) return;
    if (player_.held().id != I_GRAPPLE || !anchored(world_, hookCell_) || in.jumpPressed || player_.flying) {
        hooked_ = false;
        return;
    }
    vec3 chest = player_.pos + vec3(0, 1, 0), delta = chest - hookPoint_;
    float d = length(delta);
    if (d > 34 || d < 0.01f) {
        hooked_ = false;
        return;
    }
    if (in.sprint) hookLength_ = std::max(1.5f, hookLength_ - dt * 5.0f);
    if (in.sneak) hookLength_ = std::min(30.0f, hookLength_ + dt * 4.0f);
    if (d > hookLength_) {
        vec3 n = delta / d;
        float outward = dot(player_.vel, n);
        if (outward > 0) player_.vel -= n * outward;
        AABB box = player_.box();
        moveBox(world_, box, n * (hookLength_ - d), 0, false);
        player_.pos = {box.center().x, box.min.y, box.center().z};
        player_.fallStart = player_.pos.y;
        player_.onGround = false;
        vec3 forward(sin(player_.yaw), 0, -cos(player_.yaw)), right(cos(player_.yaw), 0, sin(player_.yaw));
        vec3 pump = (forward * in.move.z + right * in.move.x) * 8.0f;
        player_.vel += (pump - n * dot(pump, n)) * dt;
    }
    vec3 rd = normalize(hookPoint_ - chest);
    RayHit block = raycast(world_, chest + rd * 0.15f, rd, std::max(0.0f, d - 0.3f));
    if (block.hit && !(block.x == hookCell_.x && block.y == hookCell_.y && block.z == hookCell_.z)) hooked_ = false;
}

void Game::gatherPointLights() {
    std::vector<PointLight> lights;
    ItemId held = player_.held().id;
    if ((held == I_LANTERN && lanternOn_) || (isBlockItem(held) && def(uint8_t(held)).emit))
        lights.push_back({player_.visualFeet() + vec3(0, 1.25f, 0) + player_.look() * 0.4f,
                          emissionColor(held == I_LANTERN ? uint8_t(B_LANTERN) : uint8_t(held)),
                          float(def(held == I_LANTERN ? uint8_t(B_LANTERN) : uint8_t(held)).emit) * 0.5f, false, true,
                          def(held == I_LANTERN ? uint8_t(B_LANTERN) : uint8_t(held)).emit});
    if (!lights.empty()) lights.back().id = lightIdentity(1, 0, 0, 0);
    int cx = floorDiv(cellOf(player_.pos.x), CS), cz = floorDiv(cellOf(player_.pos.z), CS);
    for (auto it = emitterCache_.begin(); it != emitterCache_.end();) {
        auto* col = world_.column(int(it->first >> 32), int(it->first));
        if (!col || std::abs(col->cx - cx) > 5 || std::abs(col->cz - cz) > 5)
            it = emitterCache_.erase(it);
        else
            ++it;
    }
    for (int dz = -5; dz <= 5; ++dz)
        for (int dx = -5; dx <= 5; ++dx) {
            auto* c = world_.column(cx + dx, cz + dz);
            if (!c || c->state == ColState::Generating) continue;
            if (!c->emittersScanned) c->indexFeatures();
            auto& cached = emitterCache_[colKey(c->cx, c->cz)];
            uint64_t stamp = c->lightStamp;
            for (int d = 0; d < 4; ++d) {
                constexpr int X[] = {1, -1, 0, 0}, Z[] = {0, 0, 1, -1};
                auto* n = world_.column(c->cx + X[d], c->cz + Z[d]);
                stamp ^= uint64_t(n ? n->lightStamp : 0xffffffffu) + 0x9e3779b97f4a7c15ull + (stamp << 6) + (stamp >> 2);
            }
            if (cached.stamp != stamp) {
                cached.stamp = stamp;
                cached.lights.clear();
                std::vector<int> emitters = c->emitters;
                std::sort(emitters.begin(), emitters.end());
                std::unordered_set<int> visited;
                auto natural = [](uint8_t id) {
                    return id == B_LAVA || id == B_GLOWSTONE || id == B_CRYSTAL || id == B_CRYSTAL_ROCK || id == B_GLOWSHROOM ||
                           id == B_MUSHROOM_CAP || id == B_EMBER;
                };
                for (int i : emitters) {
                    int x = c->cx * CS + i % CS, z = c->cz * CS + (i / CS) % CS, y = i / CS2;
                    uint8_t id = c->get(x & 31, y, z & 31);
                    if (!def(id).emit) continue;
                    int bx = x, by = y, bz = z;
                    if (natural(id)) {
                        if (!visited.insert(i).second) continue;
                        int root = i;
                        float best = -1e9f;
                        bool exposed = false;
                        int x0 = floorDiv(x, 4) * 4, y0 = floorDiv(y, 4) * 4, z0 = floorDiv(z, 4) * 4;
                        std::vector<ivec3> queue{{x, y, z}};
                        for (size_t h = 0; h < queue.size(); ++h) {
                            auto p = queue[h];
                            int open = 0;
                            constexpr int X[] = {1, -1, 0, 0, 0, 0}, Y[] = {0, 0, 1, -1, 0, 0}, Z[] = {0, 0, 0, 0, 1, -1};
                            for (int d = 0; d < 6; ++d) {
                                int nx = p.x + X[d], ny = p.y + Y[d], nz = p.z + Z[d];
                                auto n = world_.get(nx, ny, nz);
                                if (n != id && lightCost(n) < 99) ++open;
                                if (n != id || nx < x0 || ny < y0 || nz < z0 || nx >= x0 + 4 || ny >= y0 + 4 || nz >= z0 + 4) continue;
                                int at = idx3(nx & 31, ny, nz & 31);
                                root = std::min(root, at);
                                if (visited.insert(at).second) queue.push_back({nx, ny, nz});
                            }
                            float score = float(open) * 4 - lengthSq(vec3(p.x - x0 - 1.5f, p.y - y0 - 1.5f, p.z - z0 - 1.5f));
                            if (open && score > best) {
                                best = score;
                                bx = p.x;
                                by = p.y;
                                bz = p.z;
                                exposed = true;
                            }
                        }
                        if (!exposed) continue;
                        uint64_t key = lightIdentity(3, c->cx, root, c->cz);
                        cached.lights.push_back({{(bx + .5f) * VOX, (by + .65f) * VOX, (bz + .5f) * VOX},
                                                 emissionColor(id),
                                                 float(def(id).emit) * VOX,
                                                 false,
                                                 false,
                                                 def(id).emit,
                                                 true,
                                                 key});
                    } else
                        cached.lights.push_back({{(x + .5f) * VOX, (y + .65f) * VOX, (z + .5f) * VOX},
                                                 emissionColor(id),
                                                 float(def(id).emit) * VOX,
                                                 false,
                                                 false,
                                                 def(id).emit,
                                                 true,
                                                 lightIdentity(2, x, y, z)});
                }
            }
            for (auto l : cached.lights) {
                if (lengthSq(l.pos - player_.pos) > 72 * 72 || !cam_.frustum.sphereVisible(l.pos - cam_.pos, l.radius + .5f)) continue;
                l.baked = c->hasLight && c->bakedLightStamp == c->lightStamp;
                lights.push_back(l);
            }
        }
    for (const auto& e : sim_.entities()) {
        if (e.dead || lengthSq(e.pos - player_.pos) > 30 * 30) continue;
        int emit = (e.type == EntType::Debris || e.type == EntType::Falling) ? def(e.block).emit : 0;
        if (e.type == EntType::Item && e.item < 256) emit = def(uint8_t(e.item)).emit;
        if (e.type == EntType::Item && e.item == I_LANTERN) emit = 14;
        if (e.type == EntType::TNT && e.fuse < 1.5f) emit = 13;
        if (emit)
            lights.push_back({e.pos + vec3(0, 0.25f, 0),
                              emissionColor(e.type == EntType::Item  ? (e.item == I_LANTERN ? uint8_t(B_LANTERN) : uint8_t(e.item))
                                            : e.type == EntType::TNT ? uint8_t(B_TORCH)
                                                                     : e.block),
                              float(emit) * 0.5f, false, false, emit, false, lightIdentity(4, int(e.id), 0, 0)});
    }
    for (const auto& b : sim_.bodies()) {
        if (b.dead || lengthSq(b.pos - player_.pos) > 40 * 40) continue;
        int emitter = 0;
        for (const auto& [local, id] : b.emitters) {
            vec3 p = b.dvalid ? vec3(float(b.dTs.x), float(b.dTs.y), float(b.dTs.z)) * VOX + toMat3(b.drot) * (local + b.com)
                              : b.pos + toMat3(b.rot) * local;
            lights.push_back({p, emissionColor(id), float(def(id).emit) * 0.5f, false, false, def(id).emit, false,
                              lightIdentity(5, b.id, emitter++, 0)});
        }
    }
    std::stable_sort(lights.begin(), lights.end(), [&](const PointLight& a, const PointLight& b) {
        return lengthSq(a.pos - player_.pos) < lengthSq(b.pos - player_.pos);
    });
    renderer_.setPointLights(std::move(lights));
}

// skiffs, placed lanterns, ropes and the glider
void Game::drawAdventure(std::vector<BoxInstance>& out) const {
    for (size_t i = 0; i < skiffs_.size(); ++i) {
        const auto& b = skiffs_[i];
        if (lengthSq(b.pos - cam_.pos) > 160 * 160) continue;
        uint8_t L = world_.lightAt(cellOf(b.pos.x), cellOf(b.pos.y + 1), cellOf(b.pos.z));
        float sky = L >> 4, blk = L & 15;
        vec3 origin = b.pos - cam_.pos;
        mat3 R = modelYaw(-b.yaw);
        drawModel(out, loadModel("skiff"), origin, R, sky, blk);
        for (int side : {-1, 1}) {
            float sweep = sin(b.stroke) * 0.25f;
            vec3 a = origin + R * vec3(side * 0.4375f, 0.375f, 0), tip = origin + R * vec3(side * 1.0625f, 0.0625f, sweep);
            modelBar(out, a, tip, 0.0625f, {0.65f, 0.48f, 0.29f, 1}, sky, blk, T_LOG_SIDE);
            modelBox(out, origin, R, {side * 1.0625f - 0.0625f, 0, -0.125f + sweep}, {0.125f, 0.0625f, 0.3125f}, {1, 1, 1, 1}, sky, blk,
                     T_PLANKS);
        }
    }
    if (lanternCache_.size() > world_.columns().size() * 2 + 64) lanternCache_.clear();
    const float colReach = 80.0f + CS * VOX * 0.75f;
    for (const auto& [key, col] : world_.columns()) {
        float ccx = (col->cx * CS + CS * 0.5f) * VOX - cam_.pos.x, ccz = (col->cz * CS + CS * 0.5f) * VOX - cam_.pos.z;
        if (ccx * ccx + ccz * ccz > colReach * colReach) continue;
        auto& entry = lanternCache_[key];
        const uint64_t version = (uint64_t(col->lightStamp) << 32) | uint64_t(col->emitters.size());
        if (!entry.valid || entry.version != version) {
            entry.lanterns.clear();
            for (int i : col->emitters)
                if (col->get(i % CS, i / CS2, (i / CS) % CS) == B_LANTERN) entry.lanterns.push_back(i);
            entry.version = version;
            entry.valid = true;
        }
        for (int i : entry.lanterns) {
            int x = col->cx * CS + i % CS, z = col->cz * CS + (i / CS) % CS, y = i / CS2;
            if (world_.get(x, y, z) != B_LANTERN) continue;
            vec3 p(x * VOX, y * VOX, z * VOX);
            if (lengthSq(p - cam_.pos) > 80 * 80) continue;
            p -= cam_.pos;
            uint8_t L = world_.lightAt(x, y, z);
            float sk = L >> 4, bl = L & 15;
            drawModel(out, loadModel("lantern"), p, mat3(), sk, bl);
        }
    }
    for (const auto& r : ropes_) {
        if (lengthSq((r.a + r.b) * 0.5f - cam_.pos) > 180 * 180) continue;
        modelRope(out, r.a - cam_.pos, r.b - cam_.pos, 0);
        modelBox(out, r.a - cam_.pos, mat3(), vec3(-0.06f), vec3(0.12f), {0.3f, 0.35f, 0.38f, 1});
        modelBox(out, r.b - cam_.pos, mat3(), vec3(-0.06f), vec3(0.12f), {0.3f, 0.35f, 0.38f, 1});
    }
    if (hooked_) {
        vec3 hand = player_.pos + vec3(0, 1.3f, 0) - cam_.pos;
        modelRope(out, hand, hookPoint_ - cam_.pos, std::clamp(hookLength_ - length(hookPoint_ - cam_.pos - hand), 0.0f, 2.0f) * 0.25f);
        modelBar(out, hookPoint_ - cam_.pos - vec3(0, 0.1f, 0), hookPoint_ - cam_.pos + vec3(0, 0.15f, 0), 0.07f, {0.7f, 0.76f, 0.8f, 1});
    }
    if (ropePending_) modelRope(out, ropeStart_ - cam_.pos, player_.eye() - cam_.pos, 0.4f);
    if (player_.gliding) {
        mat3 R = modelYaw(-player_.yaw);
        vec3 p = player_.pos + vec3(0, 1.875f, 0) - cam_.pos;
        drawModel(out, loadModel("glider"), p, R, 15, 0);
    }
}
}
