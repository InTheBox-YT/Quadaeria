#include "sim/Sim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace qc {
namespace {
const int PDX[6] = {1, -1, 0, 0, 0, 0}, PDY[6] = {0, 0, 1, -1, 0, 0}, PDZ[6] = {0, 0, 0, 0, 1, -1};
inline vec3 cc(int x, int y, int z) { return {(x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX}; }
constexpr float TICK = 0.1f;
constexpr int RAM_FULL = 15, GAP_FULL = 31;
}

bool isPowerBlock(uint8_t b) { return b >= B_CABLE && b <= B_SPARKGAP; }

void Sim::crankDynamo(ivec3 c) {
    crank_[packPos(c.x, c.y, c.z)] += 30.0f;
    vec3 p = cc(c.x, c.y, c.z);
    sounds.push_back({Snd::Click, p, 0.6f, 0.9f + rng_.range(0.0f, 0.2f)});
    sounds.push_back({Snd::Creak, p, 0.25f, 1.6f});
}

void Sim::powerCellChanged(int x, int y, int z, uint8_t oldB, uint8_t newB) {
    int64_t k = packPos(x, y, z);
    if (isPowerBlock(newB))
        powerCells_.insert(k);
    else if (isPowerBlock(oldB))
        powerCells_.erase(k);
}

void Sim::scanPowerColumns() {
    for (auto& [key, col] : world_->columns()) {
        Column& c = *col;
        if (c.powerScanned || c.state == ColState::Generating) continue;
        c.powerScanned = true;
        if (!c.emittersScanned) c.indexFeatures();
        for (int i : c.powerDevices) {
            int lx = i % CS, lz = (i / CS) % CS, y = i / CS2;
            powerCells_.insert(packPos(c.cx * CS + lx, y, c.cz * CS + lz));
        }
    }
}

static ivec3 facingDir(uint8_t meta) {
    int f = meta & 7;
    if (f > 5) f = 0;
    return {PDX[f], PDY[f], PDZ[f]};
}

std::string Sim::powerInfo(ivec3 c) const {
    auto it = netOf_.find(packPos(c.x, c.y, c.z));
    uint8_t b = world_->get(c.x, c.y, c.z);
    uint8_t m = world_->metaAt(c.x, c.y, c.z);
    char buf[160];
    std::string extra;
    if (b == B_CELL) {
        std::snprintf(buf, sizeof(buf), "  charge %d%%", int(m * 100 / 255));
        extra = buf;
    }
    if (b == B_RAM) {
        std::snprintf(buf, sizeof(buf), "  capacitor %d%%%s", int(((m >> 4) & 15) * 100 / RAM_FULL), (m & 8) ? ", out" : "");
        extra = buf;
    }
    if (b == B_SPARKGAP) {
        std::snprintf(buf, sizeof(buf), "  gap %d%%", int((m >> 3) * 100 / GAP_FULL));
        extra = buf;
    }
    if (b == B_PLATE) extra = loadedPlates_.count(packPos(c.x, c.y, c.z)) ? "  loaded" : "  no load";
    if (it == netOf_.end() || size_t(it->second) >= nets_.size()) return extra.empty() ? "no current" : extra.substr(2);
    const PowerNet& n = nets_[size_t(it->second)];
    std::snprintf(buf, sizeof(buf), "supply %.1f  demand %.1f  (%d%%)", n.supply, n.demand, int(std::round(n.ratio * 100)));
    return std::string(buf) + extra;
}

void Sim::stepPower(float dt, Player& player) {
    powerTimer_ += dt;
    if (powerTimer_ < TICK) return;
    powerTimer_ = 0;
    scanPowerColumns();
    nets_.clear();
    netOf_.clear();
    if (powerCells_.empty()) return;

    loadedPlates_.clear();
    auto loadAt = [&](vec3 feet, float halfW) {
        int y = cellOf(feet.y - 0.06f);
        for (int z = cellOf(feet.z - halfW); z <= cellOf(feet.z + halfW); ++z)
            for (int x = cellOf(feet.x - halfW); x <= cellOf(feet.x + halfW); ++x)
                for (int py = y; py <= y + 1; ++py)
                    if (world_->get(x, py, z) == B_PLATE) loadedPlates_.insert(packPos(x, py, z));
    };
    if (player.onGround && !player.flying) loadAt(player.pos, 0.3f);
    for (const Entity& e : objects_) {
        if (!e.onGround) continue;
        bool heavy = e.type == EntType::TNT || (e.type == EntType::Item && e.count >= 4) || e.type == EntType::Falling;
        if (heavy) loadAt(e.pos, e.w * 0.5f);
    }
    for (const Body& b : bodies_)
        if (!b.dead)
            loadAt(vec3((b.aabbMin.x + b.aabbMax.x) * 0.5f, b.aabbMin.y, (b.aabbMin.z + b.aabbMax.z) * 0.5f),
                   (b.aabbMax.x - b.aabbMin.x) * 0.5f);
    {
        std::vector<int64_t> q(loadedPlates_.begin(), loadedPlates_.end());
        for (size_t i = 0; i < q.size() && q.size() < 512; ++i) {
            ivec3 p = unpackPos(q[i]);
            for (int d = 0; d < 6; ++d) {
                int64_t k = packPos(p.x + PDX[d], p.y + PDY[d], p.z + PDZ[d]);
                if (world_->get(p.x + PDX[d], p.y + PDY[d], p.z + PDZ[d]) == B_PLATE && loadedPlates_.insert(k).second) q.push_back(k);
            }
        }
    }
    auto conducts = [&](uint8_t b, int64_t k) {
        switch (b) {
        case B_CABLE:
        case B_CABLE_LIVE:
        case B_CELL:
        case B_DYNAMO:
        case B_SWITCH_ON:
        case B_LAMP:
        case B_LAMP_ON:
        case B_RAM:
        case B_DETONATOR: return true;
        case B_PLATE: return loadedPlates_.count(k) != 0;
        default: return false;
        }
    };

    std::vector<int64_t> stale;
    for (int64_t k : powerCells_) {
        if (netOf_.count(k)) continue;
        ivec3 p = unpackPos(k);
        uint8_t b = world_->get(p.x, p.y, p.z);
        if (!isPowerBlock(b)) {
            stale.push_back(k);
            continue;
        }
        if (!conducts(b, k)) continue;
        int id = int(nets_.size());
        nets_.push_back({});
        PowerNet& n = nets_.back();
        std::vector<int64_t> q{k};
        netOf_[k] = id;
        for (size_t i = 0; i < q.size() && q.size() < 20000; ++i) {
            ivec3 c = unpackPos(q[i]);
            n.cells.push_back(q[i]);
            for (int d = 0; d < 6; ++d) {
                int nx = c.x + PDX[d], ny = c.y + PDY[d], nz = c.z + PDZ[d];
                int64_t nk = packPos(nx, ny, nz);
                if (netOf_.count(nk)) continue;
                uint8_t nb = world_->get(nx, ny, nz);
                if (!isPowerBlock(nb) || !conducts(nb, nk)) continue;
                netOf_[nk] = id;
                q.push_back(nk);
            }
        }
    }
    for (int64_t k : stale) powerCells_.erase(k);

    struct Gap {
        ivec3 c;
        uint8_t meta;
        int in = -1, out = -1;
    };
    std::vector<Gap> gaps;
    for (int64_t k : powerCells_) {
        ivec3 p = unpackPos(k);
        if (world_->get(p.x, p.y, p.z) != B_SPARKGAP) continue;
        Gap g{p, world_->metaAt(p.x, p.y, p.z)};
        ivec3 f = facingDir(g.meta);
        auto in = netOf_.find(packPos(p.x - f.x, p.y - f.y, p.z - f.z)), out = netOf_.find(packPos(p.x + f.x, p.y + f.y, p.z + f.z));
        if (in != netOf_.end()) g.in = in->second;
        if (out != netOf_.end()) g.out = out->second;
        gaps.push_back(g);
    }

    for (size_t ni = 0; ni < nets_.size(); ++ni) {
        PowerNet& n = nets_[ni];
        float gen = 0, cellOffer = 0;
        int cables = 0;
        for (int64_t k : n.cells) {
            ivec3 c = unpackPos(k);
            uint8_t b = world_->get(c.x, c.y, c.z);
            uint8_t m = world_->metaAt(c.x, c.y, c.z);
            switch (b) {
            case B_CABLE:
            case B_CABLE_LIVE: ++cables; break;
            case B_CELL: cellOffer += std::min(3.0f, float(m)); break;
            case B_DYNAMO: {
                auto ck = crank_.find(k);
                if (ck != crank_.end()) {
                    float g = std::min(ck->second, 6.0f);
                    gen += g;
                    ck->second -= g;
                    if (ck->second <= 0.01f) crank_.erase(ck);
                }
                for (int d = 0; d < 6; ++d) {
                    int x = c.x + PDX[d], y = c.y + PDY[d], z = c.z + PDZ[d];
                    if (world_->get(x, y, z) != B_WATER) continue;
                    uint8_t below = world_->get(x, y - 1, z);
                    bool falling =
                        below == B_AIR || (below == B_WATER && fluidSurfaceAmountOf(B_WATER, world_->metaAt(x, y - 1, z)) < MAX_FLUID - 8);
                    if (fluidActive_.count(packPos(x, y, z)) || falling) gen += 3.0f;
                }
                break;
            }
            case B_LAMP:
            case B_LAMP_ON: n.demand += 0.15f; break;
            case B_RAM:
                if (((m >> 4) & 15) < RAM_FULL) n.demand += 3.0f;
                break;
            case B_DETONATOR: n.demand += 2.0f; break;
            default: break;
            }
        }
        for (const Gap& g : gaps)
            if (g.in == int(ni) && (g.meta >> 3) < GAP_FULL) n.demand += 2.0f;
        const float loss = std::clamp(1.0f - 0.004f * float(cables), 0.4f, 1.0f);
        n.gen = gen;
        n.supply = (gen + cellOffer) * loss;
        n.ratio = n.demand > 0 ? std::min(1.0f, n.supply / n.demand) : (n.supply > 0 ? 1.0f : 0.0f);
        float used = std::min(n.supply, n.demand) / loss;
        float fromCells = std::max(0.0f, used - gen);
        float spare = std::max(0.0f, gen - used);
        for (int64_t k : n.cells) {
            ivec3 c = unpackPos(k);
            if (world_->get(c.x, c.y, c.z) != B_CELL) continue;
            int m = world_->metaAt(c.x, c.y, c.z);
            float offer = std::min(3.0f, float(m));
            float take = cellOffer > 0 ? fromCells * offer / cellOffer : 0.0f;
            float give = spare > 0 ? std::min(2.0f, spare) : 0.0f;
            spare -= give;
            float nv = float(m) - take + give + cellFrac_[k];
            int iv = std::clamp(int(std::floor(nv)), 0, 255);
            cellFrac_[k] = std::clamp(nv - float(iv), 0.0f, 0.999f);
            if (iv != m) world_->rawSetMeta(c.x, c.y, c.z, uint8_t(iv));
        }
        n.flowing = used > 0.01f;
    }

    std::vector<int> pulsed;
    for (Gap& g : gaps) {
        int acc = g.meta >> 3;
        if (g.in >= 0 && acc < GAP_FULL) acc = std::min(GAP_FULL, acc + int(std::round(4.0f * nets_[size_t(g.in)].ratio)));
        if (acc >= GAP_FULL) {
            acc = 0;
            if (g.out >= 0) pulsed.push_back(g.out);
            vec3 p = cc(g.c.x, g.c.y, g.c.z);
            for (int i = 0; i < 6; ++i)
                particle(PKind::Spark, p, vec3(rng_.range(-2, 2), rng_.range(0, 2.5f), rng_.range(-2, 2)), rng_.range(0.15f, 0.35f), 0.04f,
                         vec4(0.85f, 0.9f, 1.0f, 1));
            sounds.push_back({Snd::Click, p, 0.7f, 1.8f + rng_.range(0.0f, 0.3f)});
            sounds.push_back({Snd::Crack, p, 0.25f, 2.2f});
        }
        world_->rawSetMeta(g.c.x, g.c.y, g.c.z, uint8_t((g.meta & 7) | (acc << 3)));
    }
    for (int ni : pulsed) {
        PowerNet& n = nets_[size_t(ni)];
        n.supply += 40.0f;
        n.ratio = 1.0f;
        n.pulse = true;
        n.flowing = true;
    }

    std::vector<ivec3> rams, dets;
    std::vector<std::pair<ivec3, uint8_t>> swaps;
    for (size_t ni = 0; ni < nets_.size(); ++ni) {
        const PowerNet& n = nets_[ni];
        const bool on = n.ratio >= 0.5f && (n.supply > 0.01f);
        for (int64_t k : n.cells) {
            ivec3 c = unpackPos(k);
            uint8_t b = world_->get(c.x, c.y, c.z);
            uint8_t m = world_->metaAt(c.x, c.y, c.z);
            switch (b) {
            case B_LAMP:
                if (on && n.ratio >= 0.6f) swaps.push_back({c, B_LAMP_ON});
                break;
            case B_LAMP_ON:
                if (!(on && n.ratio >= 0.6f)) swaps.push_back({c, B_LAMP});
                break;
            case B_CABLE:
                if (n.flowing) swaps.push_back({c, B_CABLE_LIVE});
                break;
            case B_CABLE_LIVE:
                if (!n.flowing) swaps.push_back({c, B_CABLE});
                break;
            case B_RAM: {
                int charge = (m >> 4) & 15;
                bool out = (m & 8) != 0;
                if (charge < RAM_FULL) charge = std::min(RAM_FULL, charge + int(std::round(3.0f * n.ratio * (n.pulse ? 5.0f : 1.0f))));
                if (on && !out && charge >= RAM_FULL) {
                    rams.push_back(c);
                    out = true;
                    charge = 0;
                } else if (!on)
                    out = false;
                world_->rawSetMeta(c.x, c.y, c.z, uint8_t((m & 7) | (out ? 8 : 0) | (charge << 4)));
                break;
            }
            case B_DETONATOR:
                if (on) dets.push_back(c);
                break;
            default: break;
            }
        }
    }
    for (int64_t k : powerCells_) {
        if (netOf_.count(k)) continue;
        ivec3 c = unpackPos(k);
        uint8_t b = world_->get(c.x, c.y, c.z);
        if (b == B_LAMP_ON)
            swaps.push_back({c, B_LAMP});
        else if (b == B_CABLE_LIVE)
            swaps.push_back({c, B_CABLE});
        else if (b == B_RAM && (world_->metaAt(c.x, c.y, c.z) & 8))
            world_->rawSetMeta(c.x, c.y, c.z, world_->metaAt(c.x, c.y, c.z) & ~8);
    }
    for (auto& [c, b] : swaps) world_->setBlock(c.x, c.y, c.z, b, world_->metaAt(c.x, c.y, c.z));

    for (const ivec3& c : dets) {
        if (world_->get(c.x, c.y, c.z) != B_DETONATOR) continue;
        for (int d = 0; d < 6; ++d)
            if (world_->get(c.x + PDX[d], c.y + PDY[d], c.z + PDZ[d]) == B_TNT) igniteTNT(c.x + PDX[d], c.y + PDY[d], c.z + PDZ[d], 0.15f);
        setBlockBatch(c.x, c.y, c.z, B_AIR);
        vec3 p = cc(c.x, c.y, c.z);
        for (int i = 0; i < 10; ++i)
            particle(PKind::Spark, p, vec3(rng_.range(-3, 3), rng_.range(0, 4), rng_.range(-3, 3)), rng_.range(0.2f, 0.5f), 0.05f,
                     vec4(1.0f, 0.8f, 0.4f, 1));
        sounds.push_back({Snd::Pop, p, 0.9f, 0.7f});
    }

    if (!rams.empty()) {
        std::unordered_set<int64_t> taken;
        std::vector<ivec3> cells, all;
        std::vector<uint8_t> blocks, metas;
        vec3 push, at;
        for (const ivec3& r : rams) {
            ivec3 f = facingDir(world_->metaAt(r.x, r.y, r.z));
            push = vec3(float(f.x), float(f.y), float(f.z));
            at = cc(r.x, r.y, r.z);
            for (int k = 1; k <= 2; ++k) {
                int x = r.x + f.x * k, y = r.y + f.y * k, z = r.z + f.z * k;
                uint8_t b = world_->get(x, y, z);
                const BlockDef& d = def(b);
                if (b == B_RAM) continue;
                if (!d.solid || d.fluid || d.hardness < 0 || d.hardness > 30) break;
                if (!taken.insert(packPos(x, y, z)).second) continue;
                cells.push_back({x, y, z});
                blocks.push_back(b);
                metas.push_back(world_->metaAt(x, y, z));
            }
            sounds.push_back({Snd::Thud, at, 0.9f, 0.55f});
            sounds.push_back({Snd::Click, at, 0.6f, 0.6f});
            for (int i = 0; i < 4; ++i)
                particle(PKind::Dust, at + push * 0.3f,
                         push * 2.0f + vec3(rng_.range(-0.5f, 0.5f), rng_.range(0, 0.6f), rng_.range(-0.5f, 0.5f)), 0.8f, 0.05f,
                         vec4(0.6f, 0.6f, 0.62f, 0.5f));
        }
        for (const ivec3& r : rams) {
            ivec3 f = facingDir(world_->metaAt(r.x, r.y, r.z));
            vec3 front = cc(r.x + f.x, r.y + f.y, r.z + f.z);
            vec3 dir(float(f.x), float(f.y), float(f.z));
            for (Entity& e : objects_)
                if (lengthSq(e.pos + vec3(0, e.h * 0.5f, 0) - front) < 0.6f) {
                    e.vel += dir * 9.0f + vec3(0, 2, 0);
                    e.onGround = false;
                }
            if (lengthSq(player.pos + vec3(0, 0.5f, 0) - front) < 0.8f) player.externalVel += dir * 9.0f + vec3(0, 2.5f, 0);
        }
        if (!cells.empty()) {
            for (auto& c : cells) {
                removedBlocks_[packPos(c.x, c.y, c.z)] = world_->get(c.x, c.y, c.z);
                setBlockBatch(c.x, c.y, c.z, B_AIR);
            }
            if (cells.size() <= 2) {
                for (size_t i = 0; i < cells.size(); ++i) {
                    Entity e;
                    e.type = EntType::Debris;
                    e.block = blocks[i];
                    e.w = e.h = VOX * 0.98f;
                    e.pos = cc(cells[i].x, cells[i].y, cells[i].z) - vec3(0, VOX * 0.49f, 0);
                    e.vel = push * 8.0f + vec3(0, 1.5f, 0);
                    e.spin = vec3(rng_.range(-6, 6), rng_.range(-6, 6), rng_.range(-6, 6));
                    addEntity(std::move(e));
                }
            } else {
                createBodyFromCells(cells, blocks, push * 7.0f + vec3(0, 1.2f, 0),
                                    vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)), &metas);
            }
            structuralEdit(cells, true, 0, 0.6f);
        }
    }
}

}
