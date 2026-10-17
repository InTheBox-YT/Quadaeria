#include "sim/Sim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace qc {
namespace {
const int DX6[6] = {1, -1, 0, 0, 0, 0}, DY6[6] = {0, 0, 1, -1, 0, 0}, DZ6[6] = {0, 0, 0, 0, 1, -1};
inline vec3 cellCenter(int x, int y, int z) { return {(x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX}; }
}

void Sim::init(World* w, Renderer* r, uint64_t seed, TaskPool* pool) {
    bodyPool_ = pool;
    world_ = w;
    renderer_ = r;
    rng_ = Rng(seed ^ 0xABCDEF);
    world_->listener = this;
    clear();
}

void Sim::clear() {
    ++bodyEpoch_;
    bodyJobs_ = 0;
    for (Body& b : bodies_)
        if (b.meshId && renderer_) renderer_->freeMesh(b.meshId);
    for (Ghost& g : ghosts_)
        if (g.meshId && renderer_) renderer_->freeMesh(g.meshId);
    ghosts_.clear();
    bodies_.clear();
    {
        ImmediateWrites live(ecs_);
        ecs_.delete_with<Entity>();
    }
    pendingEnts_.clear();
    clearParticles();
    iterating_ = false;
    pickups.clear();
    stats = {};
    ecs_.set<SimulationFrame>({});
    fluidTick_ = 0;
    fluidTimer_ = lavaTimer_ = powerTimer_ = leafTimer_ = 0;
    spawnTimer_ = fireTimer_ = stressRecheck_ = 0;
    ambienceTimer_ = 3;
    dripTimer_ = 2;
    creakCooldown_ = fxTimer_ = 0;
    shake = 0;
    crank_.clear();
    knocked_.clear();
    crushed_.clear();
    bodyAccum_ = 0;
    simTierTimer_ = 0;
    itemMergeTimer_ = 0;
    fluidActive_.clear();
    lavaActive_.clear();
    displacedFluids_.clear();
    fluidPressure_.clear();
    fluidEditRevision_ = 1;
    internalFluidEdit_ = false;
    gravityChecks_.clear();
    supportChecks_.clear();
    collapseSeeds_.clear();
    stress_.clear();
    sites_.clear();
    dirtySites_.clear();
    nets_.clear();
    netOf_.clear();
    powerCells_.clear();
    loadedPlates_.clear();
    cellFrac_.clear();
    leafSeeds_.clear();
    withering_.clear();
    cracks_.clear();
    removedBlocks_.clear();
    relightSet_.clear();
    relight.clear();
    sounds.clear();
}

void Sim::markRelight(int x, int z) {
    int cx = floorDiv(x, CS), cz = floorDiv(z, CS);
    int lx = floorMod(x, CS), lz = floorMod(z, CS);
    for (int dz = (lz < 15 ? -1 : 0); dz <= (lz > CS - 16 ? 1 : 0); ++dz)
        for (int dx = (lx < 15 ? -1 : 0); dx <= (lx > CS - 16 ? 1 : 0); ++dx) relightSet_.insert(colKey(cx + dx, cz + dz));
}

void Sim::setBlockBatch(int x, int y, int z, uint8_t b, uint8_t meta) {
    uint8_t old = world_->get(x, y, z);
    if (old == b && world_->metaAt(x, y, z) == meta) return;
    if (!world_->rawSet(x, y, z, b, meta)) return;
    world_->markDirty(x, y, z);
    onBlockChanged(x, y, z, old, b);
}

void Sim::onBlockChanged(int x, int y, int z, uint8_t oldB, uint8_t newB) {
    if (!internalFluidEdit_) ++fluidEditRevision_;
    if (oldB != newB && (def(oldB).emit || def(newB).emit || lightCost(oldB) != lightCost(newB))) markRelight(x, z);
    if (oldB != newB && (def(oldB).structural || def(newB).structural))
        for (const auto& [id, box] : sites_) {
            if (x >= box.x0 && y >= box.y0 && z >= box.z0 && x < box.x0 + box.nx && y < box.y0 + box.ny && z < box.z0 + box.nz)
                dirtySites_.insert(id);
        }
    if (def(oldB).collides != def(newB).collides || def(oldB).fluid != def(newB).fluid) {
        vec3 edit((x + .5f) * VOX, (y + .5f) * VOX, (z + .5f) * VOX);
        for (auto& body : bodies_)
            if (body.sleeping && lengthSq(body.pos - edit) < (body.contactRadius + 1) * (body.contactRadius + 1)) {
                body.sleeping = false;
                body.rest = body.lowEnergy = 0;
                body.contactCount = 0;
                body.simTier = 0;
                body.simCarry = 0;
            }
    }
    if (isPowerBlock(oldB) || isPowerBlock(newB)) powerCellChanged(x, y, z, oldB, newB);
    if (oldB != newB && def(oldB).solid && (def(oldB).render == Render::Leaves || def(oldB).sound == Sound::Wood)) leafEvent(x, y, z);
    for (int d = -1; d < 6; ++d) {
        int nx = x + (d < 0 ? 0 : DX6[d]), ny = y + (d < 0 ? 0 : DY6[d]), nz = z + (d < 0 ? 0 : DZ6[d]);
        uint8_t b = world_->get(nx, ny, nz);
        if (b == B_WATER)
            fluidActive_.insert(packPos(nx, ny, nz));
        else if (b == B_LAVA || b == B_FIRE)
            lavaActive_.insert(packPos(nx, ny, nz));
    }
    const BlockDef& od = def(oldB);
    const BlockDef& nd = def(newB);
    if (od.solid && !nd.solid) {
        if (removedBlocks_.size() < 200000) removedBlocks_[packPos(x, y, z)] = oldB;
        if (y + 1 < WH) {
            gravityChecks_.insert(packPos(x, y + 1, z));
            supportChecks_.insert(packPos(x, y + 1, z));
        }
        for (int d = 0; d < 6; ++d) {
            int nx = x + DX6[d], ny = y + DY6[d], nz = z + DZ6[d];
            if (ny < 0 || ny >= WH) continue;
            if (def(world_->get(nx, ny, nz)).solid) collapseSeeds_.push_back({nx, ny, nz});
            if (def(world_->get(nx, ny, nz)).render == Render::Model) supportChecks_.insert(packPos(nx, ny, nz));
        }
    }
    if (oldB != newB || od.fluid || nd.fluid)
        for (int j = -1; j < 6; ++j) {
            int nx = x + (j < 0 ? 0 : DX6[j]), ny = y + (j < 0 ? 0 : DY6[j]), nz = z + (j < 0 ? 0 : DZ6[j]);
            if (ny < 0 || ny >= WH) continue;
            if (isPlantDetail(world_->get(nx, ny, nz))) supportChecks_.insert(packPos(nx, ny, nz));
        }
    if (nd.gravity) gravityChecks_.insert(packPos(x, y, z));
}

void Sim::processChecks() {
    int budget = 2048;
    while ((!gravityChecks_.empty() || !supportChecks_.empty()) && budget > 0) {
        auto g = gravityChecks_.take(std::min(256, budget));
        budget -= int(g.size());
        for (int64_t k : g) {
            ivec3 p = unpackPos(k);
            uint8_t b = world_->get(p.x, p.y, p.z);
            if (!def(b).gravity) continue;
            uint8_t below = world_->get(p.x, p.y - 1, p.z);
            if (def(below).collides || !world_->loaded(p.x, p.z)) continue;
            setBlockBatch(p.x, p.y, p.z, B_AIR);
            Entity e;
            e.type = EntType::Falling;
            e.block = b;
            e.pos = vec3((p.x + 0.5f) * VOX, p.y * VOX, (p.z + 0.5f) * VOX);
            e.w = e.h = VOX * 0.98f;
            addEntity(std::move(e));
        }
        auto s = supportChecks_.take(std::min(256, budget));
        budget -= int(s.size());
        for (int64_t k : s) {
            ivec3 p = unpackPos(k);
            uint8_t b = world_->get(p.x, p.y, p.z);
            const BlockDef& d = def(b);
            if (d.render == Render::Model) {
                bool supported = false;
                for (int j = 0; j < 6; ++j) {
                    uint8_t n = world_->get(p.x + DX6[j], p.y + DY6[j], p.z + DZ6[j]);
                    if (def(n).collides) supported = true;
                }
                if (supported) continue;
                setBlockBatch(p.x, p.y, p.z, B_AIR);
                spawnItem(cellCenter(p.x, p.y, p.z), d.drop ? d.drop : b, 1);
                continue;
            }
            // plants pop off if whatever they sit on is gone
            if (!isPlantDetail(b)) continue;
            bool cutTips = b == B_LONG_GRASS && world_->get(p.x, p.y + 1, p.z) != B_LONG_GRASS_TOP;
            if (!cutTips && world_->plantSupported(p.x, p.y, p.z, b)) continue;
            setBlockBatch(p.x, p.y, p.z, B_AIR);
            if (b == B_TORCH)
                spawnItem(cellCenter(p.x, p.y, p.z), B_TORCH, 1);
            else if (b == B_FLOWER_RED || b == B_FLOWER_YELLOW || b == B_GLOWSHROOM || (b >= B_VINE && b != B_LONG_GRASS_TOP))
                spawnItem(cellCenter(p.x, p.y, p.z), b, 1);
            blockParticles(cellCenter(p.x, p.y, p.z), b, 4, 0.2f, 1.5f);
        }
    }
    if (!collapseSeeds_.empty()) {
        std::vector<ivec3> seeds;
        seeds.swap(collapseSeeds_);
        collapseCheck(seeds);
    }
}

void Sim::collapseCheck(const std::vector<ivec3>& seeds) {
    std::unordered_set<int64_t> anchored;
    const int BUDGET = 20000;
    std::vector<std::vector<int64_t>> buckets(WH + 2);
    std::unordered_set<int64_t> comp;
    auto isLeaf = [](uint8_t b) { return def(b).render == Render::Leaves; };
    for (const ivec3& s : seeds) {
        if (s.y < 0 || s.y >= WH) continue;
        uint8_t sb = world_->get(s.x, s.y, s.z);
        if (!def(sb).solid || def(sb).fluid) continue;
        if (isLeaf(sb)) {
            leafEvent(s.x, s.y, s.z);
            continue;
        }
        int64_t sk = packPos(s.x, s.y, s.z);
        if (anchored.count(sk) || comp.count(sk)) continue;
        comp.clear();
        for (auto& b : buckets) b.clear();
        comp.insert(sk);
        buckets[size_t(s.y)].push_back(sk);
        int cur = s.y;
        bool isAnchored = false, usedLeaf = isLeaf(sb);
        const bool seedLeaf = isLeaf(sb);
        while (!isAnchored) {
            while (cur < WH && buckets[size_t(cur)].empty()) ++cur;
            if (cur >= WH) break;
            int64_t k = buckets[size_t(cur)].back();
            buckets[size_t(cur)].pop_back();
            ivec3 p = unpackPos(k);
            if (p.y <= 2 || int(comp.size()) > BUDGET) {
                isAnchored = true;
                break;
            }
            const bool fromLeaf = isLeaf(world_->get(p.x, p.y, p.z));
            for (int d = 0; d < 6 && !isAnchored; ++d) {
                int nx = p.x + DX6[d], ny = p.y + DY6[d], nz = p.z + DZ6[d];
                if (ny < 0) {
                    isAnchored = true;
                    break;
                }
                if (ny >= WH) continue;
                if (!world_->loaded(nx, nz)) {
                    isAnchored = true;
                    break;
                }
                uint8_t nb = world_->get(nx, ny, nz);
                const BlockDef& nd = def(nb);
                if (nd.fluid && nb == B_WATER && d != 2 && def(world_->get(p.x, p.y, p.z)).density < 1.0f) {
                    isAnchored = true;
                    break;
                }
                if (!nd.solid || nd.fluid) continue;
                if (!seedLeaf && isLeaf(nb)) continue;
                (void)fromLeaf;
                if (nb == B_BEDROCK) {
                    isAnchored = true;
                    break;
                }
                int64_t nk = packPos(nx, ny, nz);
                if (anchored.count(nk)) {
                    isAnchored = true;
                    break;
                }
                if (comp.insert(nk).second) {
                    buckets[size_t(ny)].push_back(nk);
                    if (ny < cur) cur = ny;
                    if (isLeaf(nb)) usedLeaf = true;
                }
            }
        }
        if (std::getenv("QC_DEBUG_COLLAPSE"))
            std::printf("[collapse] seed %d %d %d block %d anchored %d comp %zu leaf %d\n", s.x, s.y, s.z, sb, isAnchored, comp.size(),
                        usedLeaf);
        if (isAnchored) {
            if (usedLeaf)
                anchored.insert(sk);
            else
                for (int64_t k : comp) anchored.insert(k);
            continue;
        }
        {
            std::unordered_map<int64_t, int> dist;
            std::vector<int64_t> q;
            for (int64_t k : comp) {
                ivec3 p = unpackPos(k);
                if (isLeaf(world_->get(p.x, p.y, p.z))) continue;
                for (int d = 0; d < 6; ++d) {
                    int nx = p.x + DX6[d], ny = p.y + DY6[d], nz = p.z + DZ6[d];
                    int64_t nk = packPos(nx, ny, nz);
                    if (comp.count(nk) || dist.count(nk) || !isLeaf(world_->get(nx, ny, nz))) continue;
                    dist[nk] = 1;
                    q.push_back(nk);
                }
            }
            for (size_t i = 0; i < q.size() && dist.size() < 6000; ++i) {
                ivec3 p = unpackPos(q[i]);
                int dd = dist[q[i]];
                if (dd >= 12) continue;
                for (int d = 0; d < 6; ++d) {
                    int nx = p.x + DX6[d], ny = p.y + DY6[d], nz = p.z + DZ6[d];
                    int64_t nk = packPos(nx, ny, nz);
                    if (comp.count(nk) || dist.count(nk) || !isLeaf(world_->get(nx, ny, nz))) continue;
                    dist[nk] = dd + 1;
                    q.push_back(nk);
                }
            }
            if (!dist.empty()) {
                std::unordered_map<int64_t, int> foreign;
                std::vector<int64_t> fq;
                for (auto& [k, dd] : dist) {
                    ivec3 p = unpackPos(k);
                    for (int d = 0; d < 6; ++d) {
                        int nx = p.x + DX6[d], ny = p.y + DY6[d], nz = p.z + DZ6[d];
                        uint8_t nb = world_->get(nx, ny, nz);
                        if (!def(nb).solid || isLeaf(nb) || comp.count(packPos(nx, ny, nz))) continue;
                        if (!foreign.count(k)) {
                            foreign[k] = 1;
                            fq.push_back(k);
                        }
                    }
                }
                for (size_t i = 0; i < fq.size(); ++i) {
                    ivec3 p = unpackPos(fq[i]);
                    int dd = foreign[fq[i]];
                    for (int d = 0; d < 6; ++d) {
                        int64_t nk = packPos(p.x + DX6[d], p.y + DY6[d], p.z + DZ6[d]);
                        auto it = dist.find(nk);
                        if (it == dist.end() || foreign.count(nk)) continue;
                        foreign[nk] = dd + 1;
                        fq.push_back(nk);
                    }
                }
                size_t before = comp.size();
                for (auto& [k, dd] : dist) {
                    auto f = foreign.find(k);
                    if (f == foreign.end() || f->second > dd) comp.insert(k);
                }
                if (std::getenv("QC_DEBUG_COLLAPSE"))
                    std::printf("[collapse] leaves reachable %zu, foreign %zu, claimed %zu\n", dist.size(), foreign.size(),
                                comp.size() - before);
            }
        }
        std::vector<ivec3> cells;
        std::vector<uint8_t> blocks, metas;
        for (int64_t k : comp) {
            ivec3 p = unpackPos(k);
            uint8_t b = world_->get(p.x, p.y, p.z);
            if (def(b).gravity) {
                Entity e;
                e.type = EntType::Falling;
                e.block = b;
                e.pos = vec3((p.x + 0.5f) * VOX, p.y * VOX, (p.z + 0.5f) * VOX);
                e.w = e.h = VOX * 0.98f;
                addEntity(std::move(e));
            } else {
                cells.push_back(p);
                blocks.push_back(b);
                metas.push_back(world_->metaAt(p.x, p.y, p.z));
            }
        }
        for (int64_t k : comp) {
            ivec3 p = unpackPos(k);
            setBlockBatch(p.x, p.y, p.z, B_AIR);
        }
        if (cells.size() <= 2) {
            for (size_t i = 0; i < cells.size(); ++i) {
                Entity e;
                e.type = EntType::Debris;
                e.block = blocks[i];
                e.pos = vec3((cells[i].x + 0.5f) * VOX, cells[i].y * VOX, (cells[i].z + 0.5f) * VOX);
                e.w = e.h = VOX * 0.98f;
                e.spin = vec3(rng_.range(-3, 3), rng_.range(-3, 3), rng_.range(-3, 3));
                addEntity(std::move(e));
            }
        } else if (!cells.empty()) {
            createBodyFromCells(cells, blocks, vec3(0, 0, 0), vec3(rng_.range(-0.05f, 0.05f), 0, rng_.range(-0.05f, 0.05f)), &metas);
            sounds.push_back({Snd::Crack, cellCenter(cells[0].x, cells[0].y, cells[0].z), 1.0f, 0.8f});
        }
    }
}

void Sim::removeCells(const std::vector<ivec3>& cells, bool drops, Player* player, vec3 dropCenter) {
    (void)player;
    std::unordered_map<uint8_t, int> removed;
    for (const ivec3& c : cells) {
        uint8_t b = world_->get(c.x, c.y, c.z);
        if (b == B_AIR) continue;
        removed[b]++;
        uint8_t below = world_->get(c.x, c.y - 1, c.z);
        if (b == B_ICE && drops && (def(below).solid || def(below).fluid))
            world_->setBlock(c.x, c.y, c.z, B_WATER, FLUID_SOURCE_META);
        else
            world_->setBlock(c.x, c.y, c.z, B_AIR);
    }
    if (!drops) return;
    for (auto& [b, n] : removed) {
        const BlockDef& d = def(b);
        if (d.fluid) continue;
        ItemId it = d.drop ? d.drop : ItemId(b);
        int count = 0;
        if (isBlockItem(it)) {
            if (d.dropChance >= 1.0f)
                count = n;
            else
                count = int(std::round(n * d.dropChance));
        } else {
            float expected = (d.render == Render::Model ? float(n) : n / 8.0f) * d.dropChance;
            count = int(expected) + (rng_.uniform() < expected - int(expected) ? 1 : 0);
        }
        if (count > 0) spawnItem(dropCenter, it, count);
    }
}

void Sim::rustle(vec3 pos, uint8_t leaf, vec3 vel, float amount) {
    int n = amount >= 1.0f ? 2 : (rng_.uniform() < amount ? 1 : 0);
    for (int i = 0; i < n; ++i) {
        float g = rng_.range(0.8f, 1.1f);
        vec4 tint = leaf == B_LEAVES || leaf == B_JUNGLE_LEAVES || leaf == B_DARK_OAK_LEAVES ? vec4(0.36f * g, 0.5f * g, 0.22f * g, 1)
                                                                                             : vec4(0.42f * g, 0.52f * g, 0.3f * g, 1);
        vec3 pv = vel + vec3(rng_.range(-0.7f, 0.7f), rng_.range(0.0f, 0.9f), rng_.range(-0.7f, 0.7f));
        particle(PKind::Leaf, pos + vec3(rng_.range(-0.3f, 0.3f), rng_.range(-0.3f, 0.3f), rng_.range(-0.3f, 0.3f)), pv,
                 rng_.range(2.0f, 4.0f), rng_.range(0.09f, 0.15f), tint, def(leaf).texSide);
    }
    if (rng_.uniform() < 0.35f * amount) sounds.push_back({Snd::Step, pos, 0.35f, rng_.range(1.1f, 1.4f), Sound::Grass});
}

void Sim::igniteTNT(int x, int y, int z, float fuse) {
    if (world_->get(x, y, z) != B_TNT) return;
    const uint8_t group = world_->metaAt(x, y, z);
    ivec3 lo{x, y, z}, hi{x, y, z};
    if (group == 0) {
        lo = {x - floorMod(x, 2), y - floorMod(y, 2), z - floorMod(z, 2)};
        hi = {lo.x + 1, lo.y + 1, lo.z + 1};
    }
    std::vector<ivec3> cells{{x, y, z}};
    std::unordered_set<int64_t> seen{packPos(x, y, z)};
    for (size_t i = 0; i < cells.size() && cells.size() < 8; ++i)
        for (int d = 0; d < 6 && cells.size() < 8; ++d) {
            ivec3 n{cells[i].x + DX6[d], cells[i].y + DY6[d], cells[i].z + DZ6[d]};
            if (world_->get(n.x, n.y, n.z) != B_TNT || world_->metaAt(n.x, n.y, n.z) != group) continue;
            if (group == 0) {
                if (n.x < lo.x || n.y < lo.y || n.z < lo.z || n.x > hi.x || n.y > hi.y || n.z > hi.z) continue;
            } else {
                ivec3 l2{std::min(lo.x, n.x), std::min(lo.y, n.y), std::min(lo.z, n.z)},
                    h2{std::max(hi.x, n.x), std::max(hi.y, n.y), std::max(hi.z, n.z)};
                if (h2.x - l2.x > 1 || h2.y - l2.y > 1 || h2.z - l2.z > 1) continue;
                lo = l2;
                hi = h2;
            }
            if (seen.insert(packPos(n.x, n.y, n.z)).second) cells.push_back(n);
        }
    vec3 c;
    int miny = 1 << 30;
    for (auto& p : cells) {
        c += cellCenter(p.x, p.y, p.z);
        miny = std::min(miny, p.y);
    }
    c = c / float(cells.size());
    for (auto& p : cells) setBlockBatch(p.x, p.y, p.z, B_AIR);
    Entity e;
    e.type = EntType::TNT;
    float n = float(cells.size());
    e.w = e.h = std::cbrt(n) * VOX;
    e.pos = vec3(c.x, miny * VOX, c.z);
    e.vel = vec3(rng_.range(-0.5f, 0.5f), 2.0f, rng_.range(-0.5f, 0.5f));
    e.fuse = fuse;
    e.power = 1.6f + 2.6f * std::cbrt(n / 8.0f);
    addEntity(std::move(e));
    sounds.push_back({Snd::Fuse, c, 1.0f, 1.0f});
}

void Sim::throwDynamite(vec3 pos, vec3 vel) {
    Entity e;
    e.type = EntType::TNT;
    e.w = e.h = 0.3f;
    e.pos = pos - vec3(0, 0.15f, 0);
    e.vel = vel;
    e.fuse = 2.3f;
    e.power = 3.1f;
    addEntity(std::move(e));
    sounds.push_back({Snd::Fuse, pos, 0.8f, 1.3f});
}

// rays go out through every cell on the outside of a 12x12x12 cube
void Sim::explode(vec3 center, float power, Player& player) {
    std::unordered_set<int64_t> hit, splash, cracked;
    const int N = 12;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < N; ++k) {
                if (i != 0 && i != N - 1 && j != 0 && j != N - 1 && k != 0 && k != N - 1) continue;
                vec3 d = normalize(vec3(i / float(N - 1) * 2 - 1, j / float(N - 1) * 2 - 1, k / float(N - 1) * 2 - 1));
                float intensity = power * (0.7f + 0.6f * rng_.uniform());
                vec3 p = center;
                const float step = 0.25f;
                while (intensity > 0) {
                    int cx = cellOf(p.x), cy = cellOf(p.y), cz = cellOf(p.z);
                    uint8_t b = world_->get(cx, cy, cz);
                    if (b != B_AIR) {
                        const BlockDef& bd = def(b);
                        if (bd.fluid) {
                            intensity -= 0.55f;
                            if (intensity > 0 && cy >= 1 && world_->loaded(cx, cz)) splash.insert(packPos(cx, cy, cz));
                        } else {
                            float before = intensity;
                            intensity -= (bd.resistance * 0.3f + 0.3f) * 0.3f;
                            if (intensity > 0 && bd.hardness >= 0 && cy >= 1 && world_->loaded(cx, cz))
                                hit.insert(packPos(cx, cy, cz));
                            else if (before > 0.12f && bd.hardness >= 0 && bd.solid && cy >= 1 && world_->loaded(cx, cz))
                                cracked.insert(packPos(cx, cy, cz));
                        }
                    }
                    p += d * step;
                    intensity -= 0.1875f;
                }
            }
    std::vector<ivec3> cells, tnt;
    int debris = 0;
    for (const Entity& e : objects_)
        if (e.type == EntType::Debris) ++debris;
    for (int64_t k : hit) {
        ivec3 c = unpackPos(k);
        uint8_t b = world_->get(c.x, c.y, c.z);
        if (b == B_TNT) {
            tnt.push_back(c);
            continue;
        }
        cells.push_back(c);
        vec3 cc = cellCenter(c.x, c.y, c.z);
        vec3 away = cc - center;
        float dist = length(away);
        if (def(b).render == Render::Cross) continue;
        if (debris < settings.maxDebris && rng_.uniform() < 0.45f && def(b).render != Render::Glass) {
            Entity e;
            e.type = EntType::Debris;
            e.block = b;
            e.w = e.h = VOX * 0.98f;
            e.pos = cc - vec3(0, VOX * 0.49f, 0);
            float speed = std::clamp(power * 3.2f / (dist + 0.6f), 2.0f, 17.0f) * (0.6f + 0.6f * rng_.uniform());
            e.vel = normalize(away + vec3(0, 0.6f, 0), {0, 1, 0}) * speed + vec3(rng_.range(-1, 1), rng_.range(0, 2), rng_.range(-1, 1));
            e.spin = vec3(rng_.range(-12, 12), rng_.range(-12, 12), rng_.range(-12, 12));
            addEntity(std::move(e));
            ++debris;
        }
        if (rng_.uniform() < 0.25f) blockParticles(cc, b, 2, 0.25f, 6.0f);
    }
    for (const ivec3& c : cells) setBlockBatch(c.x, c.y, c.z, B_AIR);
    {
        std::vector<int64_t> cr(cracked.begin(), cracked.end());
        std::sort(cr.begin(), cr.end());
        int chunks = 0;
        const int maxChunks = std::clamp(int(power * 1.5f), 2, 7);
        std::unordered_set<int64_t> used;
        for (size_t t = 0; t < cr.size() * 3 && chunks < maxChunks && !cr.empty(); ++t) {
            int64_t seed = cr[size_t(rng_.uniform() * float(cr.size())) % cr.size()];
            if (used.count(seed) || hit.count(seed)) continue;
            ivec3 s0 = unpackPos(seed);
            if (!def(world_->get(s0.x, s0.y, s0.z)).solid) continue;
            std::vector<ivec3> chunk{s0};
            used.insert(seed);
            const int want = 3 + int(rng_.uniform() * 9.0f);
            for (size_t q = 0; q < chunk.size() && int(chunk.size()) < want; ++q)
                for (int d = 0; d < 6 && int(chunk.size()) < want; ++d) {
                    ivec3 n{chunk[q].x + DX6[d], chunk[q].y + DY6[d], chunk[q].z + DZ6[d]};
                    int64_t k = packPos(n.x, n.y, n.z);
                    if (used.count(k)) continue;
                    uint8_t nb = world_->get(n.x, n.y, n.z);
                    if (!def(nb).solid || def(nb).fluid || def(nb).hardness < 0) continue;
                    if (!cracked.count(k) && rng_.uniform() > 0.35f) continue;
                    used.insert(k);
                    chunk.push_back(n);
                }
            std::vector<uint8_t> cb, cm;
            vec3 cc;
            for (auto& c : chunk) {
                cb.push_back(world_->get(c.x, c.y, c.z));
                cm.push_back(world_->metaAt(c.x, c.y, c.z));
                cc += cellCenter(c.x, c.y, c.z);
            }
            cc = cc / float(chunk.size());
            for (auto& c : chunk) setBlockBatch(c.x, c.y, c.z, B_AIR);
            vec3 away = cc - center;
            float dist = std::max(length(away), 0.5f);
            float speed = std::clamp(power * 2.2f / dist, 1.5f, 11.0f) * (0.7f + 0.5f * rng_.uniform());
            vec3 v = normalize(away + vec3(0, 0.8f, 0), {0, 1, 0}) * speed;
            vec3 w(rng_.range(-4, 4), rng_.range(-4, 4), rng_.range(-4, 4));
            if (chunk.size() <= 2) {
                for (size_t j = 0; j < chunk.size(); ++j) {
                    Entity e;
                    e.type = EntType::Debris;
                    e.block = cb[j];
                    e.w = e.h = VOX * 0.98f;
                    e.pos = cellCenter(chunk[j].x, chunk[j].y, chunk[j].z) - vec3(0, VOX * 0.49f, 0);
                    e.vel = v;
                    e.spin = w * 3.0f;
                    addEntity(std::move(e));
                }
            } else
                createBodyFromCells(chunk, cb, v, w, &cm);
            for (auto& c : chunk) cells.push_back(c);
            ++chunks;
        }
    }
    for (int64_t k : cracked)
        if (cracks_.size() < 6000) cracks_.push_back({unpackPos(k), 25.0f});
    if (!cells.empty()) structuralEdit(cells, true, 1, 0.2f);
    {
        int thrown = 0;
        const int maxSplash = std::max(8, settings.maxDebris / 3);
        for (int64_t k : splash) {
            if (thrown >= maxSplash || debris >= settings.maxDebris + maxSplash) break;
            ivec3 c = unpackPos(k);
            vec3 away = cellCenter(c.x, c.y, c.z) - center;
            float dist = length(away);
            float speed = std::clamp(power * 2.6f / (dist + 0.8f), 2.0f, 13.0f) * (0.6f + 0.6f * rng_.uniform());
            vec3 v = normalize(away + vec3(0, 1.4f, 0), {0, 1, 0}) * speed + vec3(rng_.range(-1, 1), rng_.range(1, 3), rng_.range(-1, 1));
            splashFluid(c.x, c.y, c.z, v);
            ++thrown;
            ++debris;
        }
        if (!splash.empty()) sounds.push_back({Snd::Splash, center, 1.0f, 0.7f});
    }
    std::unordered_set<int64_t> groups;
    for (const ivec3& c : tnt)
        if (world_->get(c.x, c.y, c.z) == B_TNT) igniteTNT(c.x, c.y, c.z, 0.4f + rng_.uniform() * 0.8f);
    (void)groups;

    float radius = power * 2.0f;
    auto exposure = [&](vec3 target) {
        RayHit h = raycast(*world_, center, target - center, length(target - center));
        return h.hit ? 0.35f : 1.0f;
    };
    for (Entity& e : objects_) {
        vec3 c = e.pos + vec3(0, e.h * 0.5f, 0);
        vec3 d = c - center;
        float dist = length(d);
        if (dist > radius) continue;
        float f = (1 - dist / radius) * exposure(c);
        vec3 dir = normalize(d + vec3(0, 0.3f, 0), {0, 1, 0});
        e.vel += dir * (power * 4.5f * f);
        e.onGround = false;
        if (e.type == EntType::Item) e.pickupDelay = std::max(e.pickupDelay, 0.5f);
    }
    {
        vec3 c = player.pos + vec3(0, 0.9f, 0);
        vec3 d = c - center;
        float dist = length(d);
        if (dist < radius) {
            float f = (1 - dist / radius) * exposure(c);
            player.externalVel += normalize(d + vec3(0, 0.4f, 0), {0, 1, 0}) * (power * 3.2f * f);
        }
        shake = std::max(shake, std::clamp(power * 0.35f * (1.0f - std::min(dist / (radius * 4), 1.0f)), 0.0f, 1.2f));
    }
    pushBodies(center, radius, power);
    for (int i = 0; i < 26; ++i) {
        vec3 v(rng_.range(-1, 1), rng_.range(-0.2f, 1), rng_.range(-1, 1));
        particle(PKind::Smoke, center + v * (power * 0.35f), v * rng_.range(1.0f, 4.0f), rng_.range(1.2f, 2.6f), rng_.range(0.3f, 0.7f),
                 vec4(0.35f, 0.35f, 0.35f, 0.85f));
        particle(PKind::Flame, center + v * (power * 0.2f), v * rng_.range(2.0f, 7.0f), rng_.range(0.25f, 0.55f), rng_.range(0.2f, 0.45f),
                 vec4(1.0f, 0.75f, 0.3f, 1.0f));
    }
    sounds.push_back({Snd::Explode, center, std::min(1.0f, 0.4f + power * 0.15f), 0.85f + rng_.uniform() * 0.3f});
}

int Sim::depositFluid(int x, int y, int z, uint8_t type, int amount) {
    for (int k = 0; k < 16 && amount > 0; ++k) {
        int cy = y + k;
        if (cy < 1 || cy >= WH || !world_->loaded(x, z)) break;
        uint8_t b = world_->get(x, cy, z);
        if (b == type) {
            uint8_t m = world_->metaAt(x, cy, z);
            if (fluidIsSource(m)) return 0;
            int cur = fluidAmountOf(m);
            int add = std::min(MAX_FLUID - cur, amount);
            if (add > 0) {
                setBlockBatch(x, cy, z, type, uint8_t(cur + add - 1));
                amount -= add;
            }
        } else if (b == B_AIR || (def(b).replaceable && !def(b).solid && !def(b).fluid)) {
            int add = std::min(MAX_FLUID, amount);
            setBlockBatch(x, cy, z, type, uint8_t(add - 1));
            amount -= add;
        } else if (def(b).fluid) {
            int add = std::min(MAX_FLUID, amount);
            setBlockBatch(x, cy, z, add >= 190 && type == B_LAVA ? B_OBSIDIAN : B_COBBLE);
            sounds.push_back({Snd::Hiss, cellCenter(x, cy, z), 0.6f, 1.1f});
            amount -= add;
        } else if (def(b).solid) {
            break;
        }
        if (type == B_WATER)
            fluidActive_.insert(packPos(x, cy, z));
        else
            lavaActive_.insert(packPos(x, cy, z));
    }
    return amount;
}

void Sim::displaceFluid(int x, int y, int z) {
    uint8_t b = world_->get(x, y, z);
    if (!def(b).fluid) return;
    if (b == B_WATER || b == B_LAVA) return;
    displacedFluids_.push_back({{x, y, z}, b, world_->metaAt(x, y, z)});
}

void Sim::splashFluid(int x, int y, int z, vec3 vel) {
    uint8_t b = world_->get(x, y, z);
    if (!def(b).fluid) return;
    int amount = fluidAmountOf(world_->metaAt(x, y, z));
    setBlockBatch(x, y, z, B_AIR);
    Entity e;
    e.type = EntType::Debris;
    e.block = b;
    e.count = amount;
    e.w = e.h = VOX * 0.98f;
    e.pos = cellCenter(x, y, z) - vec3(0, VOX * 0.49f, 0);
    e.vel = vel;
    e.spin = vec3(rng_.range(-4, 4), rng_.range(-4, 4), rng_.range(-4, 4));
    addEntity(std::move(e));
}

void Sim::blockParticles(vec3 c, uint8_t b, int n, float spread, float speed) {
    const BlockDef& d = def(b);
    for (int i = 0; i < n; ++i) {
        vec3 o(rng_.range(-spread, spread), rng_.range(-spread, spread), rng_.range(-spread, spread));
        vec3 v = o * (speed / std::max(spread, 0.01f)) * 0.5f + vec3(0, rng_.range(0.5f, 2.5f), 0);
        particle(PKind::Block, c + o, v, rng_.range(0.4f, 1.0f), rng_.range(0.05f, 0.11f), vec4(1, 1, 1, 1), d.texSide);
    }
}

void Sim::dustPuff(vec3 c, uint8_t b, int n, float speed) {
    if (b == B_GRASS || def(b).tint == TINT_GRASS) b = B_DIRT;
    uint32_t mc = def(b).mapColor;
    vec3 col(float(mc & 255) / 255.0f, float((mc >> 8) & 255) / 255.0f, float((mc >> 16) & 255) / 255.0f);
    col = col * 0.75f + vec3(0.12f);
    for (int i = 0; i < n; ++i) {
        float a = rng_.range(0, 6.2831853f), r = rng_.range(0.3f, 1.0f) * speed;
        vec3 v(std::cos(a) * r, rng_.range(0.1f, 0.7f) * speed * 0.4f, std::sin(a) * r);
        float g = rng_.range(0.85f, 1.1f);
        particle(PKind::Dust, c + vec3(rng_.range(-0.3f, 0.3f), rng_.range(0.0f, 0.25f), rng_.range(-0.3f, 0.3f)), v,
                 rng_.range(0.9f, 2.0f), rng_.range(0.08f, 0.2f), vec4(col.x * g, col.y * g, col.z * g, 0.38f));
    }
}

void Sim::ambientParticles(float dt) {
    fxTimer_ -= dt;
    if (fxTimer_ > 0 || liveParticles_ > settings.maxParticles - 300) return;
    fxTimer_ = 0.12f;
    const int cx0 = cellOf(listener.x), cy0 = cellOf(listener.y), cz0 = cellOf(listener.z);
    for (int i = 0; i < 14; ++i) {
        int x = cx0 + int(rng_.next() % 29) - 14, y = cy0 + int(rng_.next() % 21) - 8, z = cz0 + int(rng_.next() % 29) - 14;
        if (!world_->loaded(x, z) || y < 1 || y >= WH - 2) continue;
        uint8_t b = world_->get(x, y, z);
        vec3 c((x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX);
        if (b == B_FIRE || b == B_TORCH || b == B_LANTERN || b == B_EMBER || b == B_LAMP_ON) {
            bool hot = b == B_FIRE || b == B_EMBER;
            if (rng_.uniform() < (hot ? 0.55f : 0.3f))
                particle(PKind::Spark, c + vec3(rng_.range(-0.1f, 0.1f), 0.2f, rng_.range(-0.1f, 0.1f)),
                         vec3(rng_.range(-0.35f, 0.35f), rng_.range(0.5f, 1.4f), rng_.range(-0.35f, 0.35f)), rng_.range(0.8f, 1.8f),
                         rng_.range(0.04f, 0.07f), vec4(1.0f, rng_.range(0.55f, 0.85f), 0.3f, 1));
        } else if (b == B_LAVA) {
            if (world_->get(x, y + 1, z) == B_AIR && rng_.uniform() < 0.5f)
                particle(PKind::Spark, c + vec3(rng_.range(-0.2f, 0.2f), 0.3f, rng_.range(-0.2f, 0.2f)),
                         vec3(rng_.range(-0.4f, 0.4f), rng_.range(1.0f, 3.0f), rng_.range(-0.4f, 0.4f)), rng_.range(0.6f, 1.6f),
                         rng_.range(0.045f, 0.09f), vec4(1.0f, 0.5f, 0.15f, 1));
        } else if (b == B_AIR) {
            int sky = world_->lightAt(x, y, z) >> 4;
            if (rng_.uniform() < (sky > 11 ? 0.025f : 0.05f)) {
                vec4 tint = sky > 11 ? vec4(1.0f, 0.95f, 0.7f, 0.55f) : vec4(0.75f, 0.82f, 0.9f, 0.45f);
                particle(PKind::Mote, c + vec3(rng_.range(-0.2f, 0.2f), rng_.range(-0.2f, 0.2f), rng_.range(-0.2f, 0.2f)), vec3(0, 0, 0),
                         rng_.range(5.0f, 9.0f), 0.035f, tint);
            }
            if (world_->get(x, y + 1, z) == B_DRIPSTONE && rng_.uniform() < 0.25f)
                particle(PKind::Drip, c + vec3(0, 0.2f, 0), vec3(0, 0, 0), 1.6f, 0.05f, vec4(0.7f, 0.85f, 1.0f, 0.9f));
        }
    }
}

void Sim::refreshSimulationTiers(vec3 focus) {
    for (int i = 0; i < 4; ++i) {
        stats.entityTier[i] = 0;
        stats.bodyTier[i] = 0;
    }
    const float lodScale = settings.maxBodies >= 80 ? 1.25f : settings.maxBodies <= 20 ? .80f : 1.0f;
    const float eNear = 36.0f * lodScale, eMid = 72.0f * lodScale, eFar = 120.0f * lodScale;
    const float bNear = 40.0f * lodScale, bMid = 72.0f * lodScale, bFar = 112.0f * lodScale;
    for (Entity& e : objects_) {
        const float d2 = lengthSq((e.pos + vec3(0, e.h * .5f, 0)) - focus);
        uint8_t tier = d2 < eNear * eNear ? 0 : d2 < eMid * eMid ? 1 : d2 < eFar * eFar ? 2 : 3;
        if (e.type == EntType::TNT || e.type == EntType::Falling || e.type == EntType::Debris) tier = std::min<uint8_t>(tier, 2);
        if (lengthSq(e.vel) > 64.0f)
            tier = 0;
        else if (lengthSq(e.vel) > 16.0f)
            tier = std::min<uint8_t>(tier, 1);
        e.simTier = tier;
        ++stats.entityTier[tier];
    }
    for (Body& b : bodies_) {
        if (b.dead) continue;
        const float d2 = lengthSq(b.pos - focus);
        uint8_t tier = d2 < bNear * bNear ? 0 : d2 < bMid * bMid ? 1 : d2 < bFar * bFar ? 2 : 3;
        const float edgeSpeed = length(b.vel) + length(b.angVel) * std::max(.5f, b.contactRadius);
        if (b.age < .75f || edgeSpeed > 8.0f)
            tier = 0;
        else if (edgeSpeed > 3.0f || b.inWater > .05f)
            tier = std::min<uint8_t>(tier, 1);
        if (b.sleeping) tier = 3;
        b.simTier = tier;
        ++stats.bodyTier[tier];
    }
}

void Sim::beginFrame(float dt, Player& player) {
    listener = player.pos + vec3(0, 1.6f, 0);
    stats.contactSamples = stats.rawContacts = stats.solverContacts = stats.bodyPairs = stats.broadphasePairs = stats.sleepingBodies = 0;
    processChecks();
    simTierTimer_ -= dt;
    if (simTierTimer_ <= 0.0f) {
        refreshSimulationTiers(player.pos + vec3(0, .8f, 0));
        simTierTimer_ = .12f;
    }
}

void Sim::tickFluids(float dt) {
    fluidTimer_ = std::min(fluidTimer_ + dt, 0.1f);
    lavaTimer_ += dt;
    for (int i = 0; i < 2 && fluidTimer_ >= 0.05f - 1e-6f; ++i) {
        fluidTimer_ -= 0.05f;
        stepFluids();
    }
}

void Sim::tickBodies(float dt, Player& player) {
    int bestBodyTier = 4;
    for (const Body& b : bodies_)
        if (!b.dead) {
            if (!b.sleeping)
                bestBodyTier = std::min(bestBodyTier, int(b.simTier));
            else if (!b.meshPending && !b.meshDirty)
                bestBodyTier = std::min(bestBodyTier, 3);
        }
    if (bestBodyTier == 4) {
        bodyAccum_ = 0;
    } else {
        const float h = bestBodyTier == 0   ? 1.0f / 120.0f
                        : bestBodyTier == 1 ? 1.0f / 60.0f
                        : bestBodyTier == 2 ? 1.0f / 30.0f
                                            : 1.0f / 15.0f;
        bodyAccum_ = std::min(bodyAccum_ + std::max(0.0f, dt), 0.12f + h);
        int guard = 0;
        while (bodyAccum_ >= h - 1e-7f && guard++ < 12) {
            stepBodies(h, player);
            bodyAccum_ = std::max(0.0f, bodyAccum_ - h);
        }
    }
}

void Sim::stepCanopy(Body& b, Player& player) {
    float sp = length(b.vel);
    if (b.dead || sp < 2.0f || b.samples.empty() || lengthSq(b.pos - player.pos) > 80.0f * 80.0f) return;
    mat3 R = toMat3(b.rot);
    for (int k = 0; k < 6; ++k) {
        const vec3& s = b.samples[rng_.next() % b.samples.size()];
        vec3 p = b.pos + R * s;
        int x = cellOf(p.x), y = cellOf(p.y), z = cellOf(p.z);
        uint8_t w = world_->get(x, y, z);
        if (def(w).render != Render::Leaves) continue;
        if (rng_.uniform() < std::min(0.9f, sp * 0.12f)) {
            setBlockBatch(x, y, z, B_AIR);
            rustle(cellCenter(x, y, z), w, b.vel * 0.3f, 1.0f);
            b.vel *= 0.985f;
        }
    }
}

void Sim::stepAmbience(float dt) {
    {
        int sky = world_->lightAt(cellOf(listener.x), cellOf(listener.y), cellOf(listener.z)) >> 4;
        bool under = sky < 6;
        dripTimer_ -= dt;
        if (dripTimer_ <= 0) {
            dripTimer_ = rng_.range(1.5f, 6.0f);
            if (under)
                sounds.push_back({Snd::Drip, listener + vec3(rng_.range(-7, 7), rng_.range(1, 4), rng_.range(-7, 7)),
                                  rng_.range(0.3f, 0.7f), rng_.range(0.8f, 1.25f)});
        }
        ambienceTimer_ -= dt;
        if (ambienceTimer_ <= 0) {
            ambienceTimer_ = 6.2f;
            if (under) sounds.push_back({Snd::Drone, listener + vec3(0, 1, 0), 0.9f, rng_.range(0.92f, 1.04f)});
        }
    }
    ambientParticles(dt);
    spawnTimer_ += dt;
}

void Sim::stepParticle(Particle& p, float dt) {
    auto solid = [&](vec3 q) { return world_->solidAt(cellOf(q.x), cellOf(q.y), cellOf(q.z)); };
    auto collide = [&](Particle& p, float pdt, float bounce) -> bool {
        bool landed = false;
        for (int axis = 0; axis < 3; ++axis) {
            vec3 np = p.pos;
            np[axis] += p.vel[axis] * pdt;
            if (!solid(np)) {
                p.pos = np;
                continue;
            }
            if (axis == 1 && p.vel.y < 0) {
                landed = true;
                p.vel.y = -p.vel.y * bounce;
                if (p.vel.y < 0.8f) p.vel.y = 0;
                p.vel.x *= 0.6f;
                p.vel.z *= 0.6f;
            } else
                p.vel[axis] *= -0.3f;
        }
        return landed;
    };
    const float far2 = 72.0f * 72.0f;
    p.life -= dt;
    if (p.life <= 0) return;
    const float pdt = dt;
    if (lengthSq(p.pos - listener) > far2) {
        if (p.kind == PKind::Block || p.kind == PKind::Splash || p.kind == PKind::Drip) p.vel.y -= 16.0f * pdt;
        p.pos += p.vel * pdt;
        return;
    }
    switch (p.kind) {
    case PKind::Block: {
        p.vel.y -= 18.0f * pdt;
        p.vel *= decay_.d04;
        bool landed = collide(p, pdt, 0.28f);
        if (landed && p.vel.y == 0) {
            p.vel.x *= decay_.d10;
            p.vel.z *= decay_.d10;
        }
        break;
    }
    case PKind::Spark: {
        if (p.maxLife > 0.7f) {
            float ph = p.uvOff * 23.0f + p.life * 3.0f;
            p.vel += (vec3(std::sin(ph) * 0.35f, 0.9f, std::cos(ph * 1.3f) * 0.35f) - p.vel) * std::min(1.0f, pdt * 1.6f);
            if (solid(p.pos + p.vel * pdt)) {
                p.vel = {};
                p.life = std::min(p.life, 0.2f);
            }
            p.pos += p.vel * pdt;
        } else {
            p.vel.y -= 14.0f * pdt;
            p.vel *= decay_.d12;
            collide(p, pdt, 0.45f);
        }
        break;
    }
    case PKind::Splash: {
        p.vel.y -= 16.0f * pdt;
        if (collide(p, pdt, 0.0f) || def(world_->get(cellOf(p.pos.x), cellOf(p.pos.y), cellOf(p.pos.z))).fluid)
            p.life = std::min(p.life, 0.06f);
        break;
    }
    case PKind::Drip: {
        p.vel.y -= 14.0f * pdt;
        vec3 np = p.pos + p.vel * pdt;
        if (solid(np) || def(world_->get(cellOf(np.x), cellOf(np.y), cellOf(np.z))).fluid) {
            for (int k = 0; k < 3; ++k) {
                Particle q;
                q.kind = PKind::Splash;
                q.pos = p.pos;
                q.vel = vec3(rng_.range(-0.6f, 0.6f), rng_.range(0.8f, 1.6f), rng_.range(-0.6f, 0.6f));
                q.life = q.maxLife = 0.35f;
                q.size = 0.04f;
                q.tint = vec4(0.7f, 0.85f, 1, 0);
                q.uvOff = rng_.uniform();
                spawnParticles_.push_back(q);
            }
            if (rng_.uniform() < 0.3f) sounds.push_back({Snd::Drip, p.pos, 0.25f, rng_.range(0.9f, 1.3f)});
            p.life = 0;
        } else
            p.pos = np;
        break;
    }
    case PKind::Bubble: {
        p.vel.y += 2.0f * pdt;
        p.vel.x = std::sin(p.life * 9.0f + p.uvOff * 20.0f) * 0.15f;
        p.pos += p.vel * pdt;
        if (!def(world_->get(cellOf(p.pos.x), cellOf(p.pos.y), cellOf(p.pos.z))).fluid) p.life = 0;
        break;
    }
    case PKind::Smoke:
    case PKind::Flame:
    case PKind::Dust: {
        if (p.kind == PKind::Smoke) {
            p.vel *= decay_.d14;
            p.vel.y += 1.1f * pdt;
            p.size += pdt * 0.22f;
        } else if (p.kind == PKind::Flame) {
            p.vel *= decay_.d30;
            p.vel.y += 1.0f * pdt;
        } else {
            p.vel *= decay_.d24;
            p.vel.y -= 0.25f * pdt;
            p.size += pdt * 0.3f;
        }
        for (int axis = 0; axis < 3; ++axis) {
            vec3 np = p.pos;
            np[axis] += p.vel[axis] * pdt;
            if (solid(np))
                p.vel[axis] = 0;
            else
                p.pos = np;
        }
        break;
    }
    case PKind::Mote: {
        float ph = p.uvOff * 31.0f + p.life * 0.7f;
        p.vel += (vec3(std::sin(ph) * 0.10f, std::sin(ph * 1.3f + 1.0f) * 0.05f, std::cos(ph * 0.8f) * 0.10f) - p.vel) *
                 std::min(1.0f, pdt * 1.5f);
        if (solid(p.pos + p.vel * pdt))
            p.vel = -p.vel;
        else
            p.pos += p.vel * pdt;
        break;
    }
    case PKind::Leaf: {
        float ph = p.uvOff * 40.0f + p.life * 5.0f;
        vec3 target(std::sin(ph) * 0.9f, -1.1f, std::cos(ph * 0.7f) * 0.9f);
        p.vel += (target - p.vel) * std::min(1.0f, pdt * 3.0f);
        vec3 np = p.pos + p.vel * pdt;
        if (solid(np)) {
            p.vel = {};
            p.life = std::min(p.life, 0.8f);
        } else
            p.pos = np;
        break;
    }
    }
}

void Sim::finishFrame(float dt) {
    commitParticles();
    shake = std::max(0.0f, shake - dt * 1.8f);

    for (int64_t k : relightSet_) relight.push_back(k);
    relightSet_.clear();
    removedBlocks_.clear();
    stats.entities = objects_.count();
    stats.bodies = int(bodies_.size());
    stats.fluids = int(fluidActive_.size() + lavaActive_.size());
    stats.particles = liveParticles_;
    stats.particleSlots = liveParticles_ + int(freeParticles_.size());
    int deb = 0;
    for (int i = 0; i < 4; ++i) {
        stats.entityTier[i] = 0;
        stats.bodyTier[i] = 0;
    }
    for (const Entity& e : objects_) {
        if (e.type == EntType::Debris || e.type == EntType::Falling) ++deb;
        ++stats.entityTier[std::min<int>(3, e.simTier)];
    }
    for (auto& b : bodies_)
        if (!b.dead) ++stats.bodyTier[std::min<int>(3, b.simTier)];
    stats.debris = deb;
}

}
