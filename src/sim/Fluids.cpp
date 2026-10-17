#include "sim/Sim.h"
#include <algorithm>
#include <vector>

namespace qc {
namespace {
const int HX[4] = {1, -1, 0, 0}, HZ[4] = {0, 0, 1, -1};
const int DX6[6] = {1, -1, 0, 0, 0, 0}, DY6[6] = {0, 0, 1, -1, 0, 0}, DZ6[6] = {0, 0, 0, 0, 1, -1};
inline vec3 cellCenter(int x, int y, int z) { return {(x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX}; }
}

void Sim::resolveFluidDisplacements() {
    std::unordered_map<int64_t, DisplacedFluid> captured;
    for (const auto& c : displacedFluids_) captured.emplace(packPos(c.pos.x, c.pos.y, c.pos.z), c);
    displacedFluids_.clear();
    while (!captured.empty()) {
        FluidPressure p;
        p.type = captured.begin()->second.type;
        std::vector<int64_t> queue{captured.begin()->first};
        bool reservoir = false;
        for (size_t i = 0; i < queue.size(); ++i) {
            auto it = captured.find(queue[i]);
            if (it == captured.end() || it->second.type != p.type) continue;
            auto c = it->second;
            captured.erase(it);
            p.cells.push_back(c.pos);
            if (fluidIsSource(c.meta))
                reservoir = true;
            else
                p.amount += fluidAmountOf(c.meta);
            for (int d = 0; d < 6; ++d) {
                int64_t k = packPos(c.pos.x + DX6[d], c.pos.y + DY6[d], c.pos.z + DZ6[d]);
                if (captured.count(k)) queue.push_back(k);
            }
        }
        if (!reservoir && p.amount) fluidPressure_.push_back(std::move(p));
    }
    int budget = 8192;
    size_t jobs = fluidPressure_.size();
    while (jobs-- && budget > 0) {
        FluidPressure p = std::move(fluidPressure_.front());
        fluidPressure_.pop_front();
        if (p.checkedRevision == fluidEditRevision_) {
            fluidPressure_.push_back(std::move(p));
            continue;
        }
        std::unordered_set<int64_t> visited;
        std::vector<ivec3> queue, candidates;
        for (size_t i = 0; i < p.cells.size(); ++i) {
            auto c = p.cells[(p.start + i) % p.cells.size()];
            visited.insert(packPos(c.x, c.y, c.z));
            queue.push_back(c);
        }
        size_t head = 0;
        bool reservoir = false;
        int capacity = 0;
        for (; head < queue.size() && budget > 0 && !reservoir; ++head, --budget) {
            ivec3 c = queue[head];
            for (int d = 0; d < 6; ++d) {
                ivec3 n{c.x + DX6[d], c.y + DY6[d], c.z + DZ6[d]};
                if (n.y < 1 || n.y >= WH || !world_->loaded(n.x, n.z) || !visited.insert(packPos(n.x, n.y, n.z)).second) continue;
                uint8_t b = world_->get(n.x, n.y, n.z), m = world_->metaAt(n.x, n.y, n.z);
                if (b == p.type) {
                    if (fluidIsSource(m)) {
                        reservoir = true;
                        break;
                    }
                    queue.push_back(n);
                    if (fluidAmountOf(m) < MAX_FLUID) {
                        candidates.push_back(n);
                        capacity += MAX_FLUID - fluidAmountOf(m);
                    }
                } else if (b == B_AIR || (def(b).replaceable && !def(b).solid && !def(b).fluid)) {
                    candidates.push_back(n);
                    capacity += MAX_FLUID;
                }
            }
            if (capacity >= p.amount && head >= p.cells.size() + 64) {
                ++head;
                break;
            }
        }
        if (reservoir) continue;
        std::stable_sort(candidates.begin(), candidates.end(), [](ivec3 a, ivec3 b) { return a.y < b.y; });
        for (auto c : candidates) {
            if (!p.amount) break;
            int before = world_->get(c.x, c.y, c.z) == p.type ? world_->fluidAmount(c.x, c.y, c.z) : 0;
            int add = std::min(p.amount, MAX_FLUID - before);
            if (add > 0) {
                setBlockBatch(c.x, c.y, c.z, p.type, uint8_t(before + add - 1));
                p.amount -= add;
            }
        }
        if (p.amount) {
            p.checkedRevision = head == queue.size() ? fluidEditRevision_ : 0;
            p.start = (p.start + std::max<size_t>(1, head)) % p.cells.size();
            fluidPressure_.push_back(std::move(p));
        }
    }
}

void Sim::stepFluids() {
    resolveFluidDisplacements();
    const unsigned tick = ++fluidTick_;
    int changed = 0;

    auto openFor = [&](uint8_t b) {
        const BlockDef& d = def(b);
        return b == B_AIR || b == B_FIRE || (d.replaceable && !d.fluid && !d.solid);
    };
    auto quench = [&](int x, int y, int z, int lavaAmount) {
        setBlockBatch(x, y, z, lavaAmount >= 190 ? B_OBSIDIAN : B_COBBLE);
        sounds.push_back({Snd::Hiss, cellCenter(x, y, z), 0.6f, 1.0f + rng_.uniform() * 0.3f});
        for (int i = 0; i < 4; ++i)
            particle(PKind::Smoke, cellCenter(x, y, z) + vec3(rng_.range(-0.2f, 0.2f), 0.3f, rng_.range(-0.2f, 0.2f)), vec3(0, 1.5f, 0),
                     1.2f, 0.15f, vec4(0.85f, 0.85f, 0.9f, 0.7f));
    };

    auto processLiquid = [&](ActiveCells& active, uint8_t type, int budget, bool allowInfinitePool) {
        const int reach = fluidFlowReach(type);
        const int scan = std::min<int>(int(active.size()), std::max(budget, budget * 3));
        std::vector<int64_t> candidates = active.take(scan);
        std::vector<int64_t> list;
        list.reserve(std::min<int>(budget, int(candidates.size())));
        std::vector<int64_t> far;
        far.reserve(candidates.size());
        constexpr float NEAR_R2 = 96.0f * 96.0f;
        for (int64_t k : candidates) {
            ivec3 p = unpackPos(k);
            if (lengthSq(cellCenter(p.x, p.y, p.z) - listener) <= NEAR_R2 && int(list.size()) < budget)
                list.push_back(k);
            else
                far.push_back(k);
        }
        const int farAllowance = std::min<int>(std::max(24, budget / 10), std::max(0, budget - int(list.size())));
        int usedFar = 0;
        for (int64_t k : far) {
            if (usedFar < farAllowance && int(list.size()) < budget) {
                list.push_back(k);
                ++usedFar;
            } else
                active.insert(k);
        }
        std::sort(list.begin(), list.end(), [](int64_t a, int64_t b) {
            int ay = int(a & 511), by = int(b & 511);
            return ay == by ? a < b : ay < by;
        });

        auto distAt = [&](int x, int y, int z) {
            if (y < 1 || y >= WH || world_->get(x, y, z) != type) return reach + 1;
            return fluidFlowDistanceFromMeta(type, world_->metaAt(x, y, z));
        };
        auto fallingAt = [&](int x, int y, int z) {
            return y >= 1 && y < WH && world_->get(x, y, z) == type && fluidIsFalling(type, world_->metaAt(x, y, z));
        };
        auto canReceive = [&](int x, int y, int z, int d) {
            if (y < 1 || y >= WH || !world_->loaded(x, z)) return false;
            uint8_t b = world_->get(x, y, z);
            if (openFor(b)) return true;
            if (b != type) return false;
            uint8_t m = world_->metaAt(x, y, z);
            return !fluidIsSource(m) && distAt(x, y, z) > d;
        };
        auto writeFlow = [&](int x, int y, int z, int d, bool falling) {
            uint8_t oldB = world_->get(x, y, z), oldM = world_->metaAt(x, y, z);
            bool prev = internalFluidEdit_;
            internalFluidEdit_ = true;
            if (d > reach)
                setBlockBatch(x, y, z, B_AIR);
            else
                setBlockBatch(x, y, z, type, fluidFlowMeta(type, d, falling));
            internalFluidEdit_ = prev;
            return oldB != world_->get(x, y, z) || oldM != world_->metaAt(x, y, z);
        };
        auto writeSource = [&](int x, int y, int z) {
            uint8_t oldB = world_->get(x, y, z), oldM = world_->metaAt(x, y, z);
            bool prev = internalFluidEdit_;
            internalFluidEdit_ = true;
            setBlockBatch(x, y, z, type, FLUID_SOURCE_META);
            internalFluidEdit_ = prev;
            return oldB != type || oldM != FLUID_SOURCE_META;
        };

        auto dropCost = [&](int sx, int y, int sz) {
            const int maxDepth = type == B_WATER ? 6 : 3;
            const int W = maxDepth * 2 + 1;
            struct N {
                int x, z, d;
            };
            std::vector<N> q;
            q.reserve(size_t(W * W));
            std::vector<uint8_t> seen(size_t(W * W), 0);
            auto mark = [&](int x, int z) {
                int rx = x - sx + maxDepth, rz = z - sz + maxDepth;
                if (rx < 0 || rz < 0 || rx >= W || rz >= W) return false;
                size_t i = size_t(rz * W + rx);
                if (seen[i]) return false;
                seen[i] = 1;
                return true;
            };
            mark(sx, sz);
            q.push_back({sx, sz, 0});
            for (size_t h = 0; h < q.size(); ++h) {
                N n = q[h];
                if (openFor(world_->get(n.x, y - 1, n.z))) return n.d;
                if (n.d >= maxDepth) continue;
                for (int dir = 0; dir < 4; ++dir) {
                    int nx = n.x + HX[dir], nz = n.z + HZ[dir];
                    if (!world_->loaded(nx, nz) || !mark(nx, nz)) continue;
                    uint8_t b = world_->get(nx, y, nz);
                    if (openFor(b))
                        q.push_back({nx, nz, n.d + 1});
                    else if (b == type && !fluidIsSource(world_->metaAt(nx, y, nz)) && !fallingAt(nx, y, nz))
                        q.push_back({nx, nz, n.d + 1});
                }
            }
            return 999;
        };

        struct Desire {
            int d = 0;
            bool source = false;
            bool falling = false;
        };
        std::unordered_map<int64_t, Desire> want;
        std::unordered_set<int64_t> remove;
        auto proposeSource = [&](int x, int y, int z) {
            if (y < 1 || y >= WH || !world_->loaded(x, z)) return;
            want[packPos(x, y, z)] = {0, true, false};
        };
        auto proposeFlow = [&](int x, int y, int z, int d, bool falling) {
            if (y < 1 || y >= WH || !world_->loaded(x, z) || d > reach) return;
            int64_t key = packPos(x, y, z);
            auto it = want.find(key);
            if (it == want.end()) {
                want.emplace(key, Desire{d, false, falling});
                return;
            }
            Desire& w = it->second;
            if (w.source) return;
            if (d < w.d)
                w = {d, false, falling};
            else if (d == w.d && !falling)
                w.falling = false;
        };

        for (int64_t k : list) {
            ivec3 p = unpackPos(k);
            uint8_t b = world_->get(p.x, p.y, p.z);

            if (type == B_LAVA && b == B_FIRE) {
                int age = world_->metaAt(p.x, p.y, p.z) + 1;
                bool fuel = false;
                for (int d = 0; d < 6; ++d) {
                    int nx = p.x + DX6[d], ny = p.y + DY6[d], nz = p.z + DZ6[d];
                    uint8_t nb = world_->get(nx, ny, nz);
                    if (!def(nb).flammable) continue;
                    fuel = true;
                    float chance = def(nb).render == Render::Leaves ? 0.35f : 0.12f;
                    if (rng_.uniform() < chance) {
                        if (nb == B_TNT)
                            igniteTNT(nx, ny, nz, 0.5f + rng_.uniform());
                        else {
                            setBlockBatch(nx, ny, nz, B_FIRE, 0);
                            lavaActive_.insert(packPos(nx, ny, nz));
                        }
                    }
                }
                if (rng_.uniform() < 0.3f)
                    particle(PKind::Smoke, cellCenter(p.x, p.y, p.z) + vec3(0, .3f, 0), vec3(0, 1.4f, 0), 1.5f, .12f,
                             vec4(.3f, .3f, .3f, .6f));
                if (age > (fuel ? 14 : 5) || world_->get(p.x, p.y + 1, p.z) == B_WATER)
                    setBlockBatch(p.x, p.y, p.z, B_AIR);
                else {
                    world_->rawSetMeta(p.x, p.y, p.z, uint8_t(std::min(age, 255)));
                    lavaActive_.insert(k);
                }
                if (rng_.uniform() < 0.2f) sounds.push_back({Snd::Burn, cellCenter(p.x, p.y, p.z), .3f, .8f + rng_.uniform() * .4f});
                continue;
            }
            if (b != type) continue;

            uint8_t meta = world_->metaAt(p.x, p.y, p.z);
            bool source = fluidIsSource(meta);
            bool wasFalling = fluidIsFalling(type, meta);
            int dist = source ? 0 : fluidFlowDistanceFromMeta(type, meta);

            const uint8_t other = type == B_WATER ? B_LAVA : B_WATER;
            bool consumed = false;
            for (int d = 0; d < 6; ++d) {
                int nx = p.x + DX6[d], ny = p.y + DY6[d], nz = p.z + DZ6[d];
                if (world_->get(nx, ny, nz) != other) continue;
                if (type == B_LAVA) {
                    quench(p.x, p.y, p.z, fluidAmountOf(meta));
                    consumed = true;
                    ++changed;
                    break;
                }
                quench(nx, ny, nz, fluidAmountOf(world_->metaAt(nx, ny, nz)));
                ++changed;
            }
            if (consumed) continue;

            if (type == B_LAVA) {
                for (int d = 0; d < 6; ++d) {
                    int nx = p.x + DX6[d], ny = p.y + DY6[d], nz = p.z + DZ6[d];
                    uint8_t nb = world_->get(nx, ny, nz);
                    if (def(nb).flammable && rng_.uniform() < 0.08f) {
                        if (nb == B_TNT)
                            igniteTNT(nx, ny, nz, .5f);
                        else {
                            setBlockBatch(nx, ny, nz, B_FIRE, 0);
                            lavaActive_.insert(packPos(nx, ny, nz));
                        }
                    }
                }
            }

            if (!source && !wasFalling && allowInfinitePool) {
                int src = 0;
                for (int d = 0; d < 4; ++d)
                    if (world_->get(p.x + HX[d], p.y, p.z + HZ[d]) == type && fluidIsSource(world_->metaAt(p.x + HX[d], p.y, p.z + HZ[d])))
                        ++src;
                uint8_t below = world_->get(p.x, p.y - 1, p.z);
                bool supported = def(below).collides || (below == type && !fluidIsFalling(type, world_->metaAt(p.x, p.y - 1, p.z)));
                if (src >= 2 && supported) {
                    source = true;
                    dist = 0;
                    proposeSource(p.x, p.y, p.z);
                }
            }

            if (!source) {
                int best = reach + 1;
                if (p.y + 1 < WH && world_->get(p.x, p.y + 1, p.z) == type) best = std::min(best, distAt(p.x, p.y + 1, p.z));
                for (int d = 0; d < 4; ++d) {
                    int nx = p.x + HX[d], nz = p.z + HZ[d];
                    if (world_->get(nx, p.y, nz) != type) continue;
                    uint8_t nm = world_->metaAt(nx, p.y, nz);
                    if (fluidIsFalling(type, nm)) continue;
                    int nd = distAt(nx, p.y, nz);
                    if (nd < reach) best = std::min(best, nd + 1);
                }
                if (best > reach) {
                    remove.insert(k);
                    continue;
                }
                dist = best;
            }

            bool verticalOutlet = false;
            if (p.y > 1) {
                uint8_t below = world_->get(p.x, p.y - 1, p.z);
                if (openFor(below)) {
                    proposeFlow(p.x, p.y - 1, p.z, dist, true);
                    verticalOutlet = true;
                } else if (below == type) {
                    uint8_t bm = world_->metaAt(p.x, p.y - 1, p.z);
                    int bd = distAt(p.x, p.y - 1, p.z);
                    if (!fluidIsSource(bm) && bd > dist) {
                        proposeFlow(p.x, p.y - 1, p.z, dist, true);
                        verticalOutlet = true;
                    } else if (fluidIsFalling(type, bm) && bd == dist)
                        verticalOutlet = true;
                    else if (wasFalling)
                        verticalOutlet = true;
                } else if (type == B_WATER && def(below).render == Render::Leaves) {
                    for (int fall = 2; fall < 14 && p.y - fall > 0; ++fall) {
                        uint8_t bb = world_->get(p.x, p.y - fall, p.z);
                        if (def(bb).render == Render::Leaves) continue;
                        if (openFor(bb) ||
                            (bb == type && !fluidIsSource(world_->metaAt(p.x, p.y - fall, p.z)) && distAt(p.x, p.y - fall, p.z) > dist)) {
                            proposeFlow(p.x, p.y - fall, p.z, dist, true);
                            verticalOutlet = true;
                        }
                        break;
                    }
                }
            }

            if (!source) proposeFlow(p.x, p.y, p.z, dist, verticalOutlet);

            if (!verticalOutlet && dist < reach) {
                const int next = dist + 1;
                int dirs[4], cost[4], count = 0, bestCost = 999;
                for (int d = 0; d < 4; ++d) {
                    int nx = p.x + HX[d], nz = p.z + HZ[d];
                    if (!canReceive(nx, p.y, nz, next)) continue;
                    dirs[count] = d;
                    uint8_t under = world_->get(nx, p.y - 1, nz);
                    cost[count] = openFor(under) ? 0 : dropCost(nx, p.y, nz);
                    bestCost = std::min(bestCost, cost[count]);
                    ++count;
                }
                for (int i = 0; i < count; ++i) {
                    if (bestCost < 999 && cost[i] != bestCost) continue;
                    int d = dirs[i], nx = p.x + HX[d], nz = p.z + HZ[d];
                    uint8_t under = world_->get(nx, p.y - 1, nz);
                    bool fallsHere = openFor(under) || (under == type && fluidIsFalling(type, world_->metaAt(nx, p.y - 1, nz)));
                    proposeFlow(nx, p.y, nz, next, fallsHere);
                }
            }
        }

        for (int64_t k : remove)
            if (!want.count(k)) {
                ivec3 p = unpackPos(k);
                if (world_->get(p.x, p.y, p.z) == type && !fluidIsSource(world_->metaAt(p.x, p.y, p.z)))
                    if (writeFlow(p.x, p.y, p.z, reach + 1, false)) ++changed;
            }
        for (const auto& [k, w] : want) {
            ivec3 p = unpackPos(k);
            uint8_t b = world_->get(p.x, p.y, p.z);
            if (w.source) {
                if (writeSource(p.x, p.y, p.z)) ++changed;
                continue;
            }
            if (b == type && fluidIsSource(world_->metaAt(p.x, p.y, p.z))) continue;
            if (!openFor(b) && b != type) continue;
            uint8_t desired = fluidFlowMeta(type, w.d, w.falling);
            if (b != type || world_->metaAt(p.x, p.y, p.z) != desired)
                if (writeFlow(p.x, p.y, p.z, w.d, w.falling)) ++changed;
        }
    };

    processLiquid(fluidActive_, B_WATER, settings.fluidBudget, true);
    if (tick % 4 == 0) processLiquid(lavaActive_, B_LAVA, std::max(64, settings.fluidBudget / 3), false);
    stats.fluidChanged = changed;
}

}
