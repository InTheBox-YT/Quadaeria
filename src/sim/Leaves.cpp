#include "sim/Sim.h"
#include <algorithm>
#include <cmath>

namespace qc {
namespace {
const int LDX[6] = {1, -1, 0, 0, 0, 0}, LDY[6] = {0, 0, 1, -1, 0, 0}, LDZ[6] = {0, 0, 0, 0, 1, -1};
constexpr int LEAF_REACH = 10;
inline bool isLeafB(uint8_t b) { return def(b).render == Render::Leaves; }
inline bool isWoodB(uint8_t b) {
    const BlockDef& d = def(b);
    return d.solid && d.sound == Sound::Wood && d.render != Render::Leaves;
}
}

void Sim::leafEvent(int x, int y, int z) {
    if (leafSeeds_.size() < 20000) leafSeeds_.push_back(packPos(x, y, z));
}

void Sim::stepLeaves(float dt) {
    for (size_t i = 0; i < withering_.size();) {
        withering_[i].second -= dt;
        if (withering_[i].second > 0) {
            ++i;
            continue;
        }
        ivec3 c = unpackPos(withering_[i].first);
        uint8_t b = world_->get(c.x, c.y, c.z);
        withering_[i] = withering_.back();
        withering_.pop_back();
        if (!isLeafB(b) || (world_->metaAt(c.x, c.y, c.z) & LEAF_PLACED)) continue;
        setBlockBatch(c.x, c.y, c.z, B_AIR);
        if (rng_.uniform() < 0.3f) rustle(vec3((c.x + 0.5f) * VOX, (c.y + 0.5f) * VOX, (c.z + 0.5f) * VOX), b, vec3(0, -0.5f, 0), 0.6f);
    }
    leafTimer_ -= dt;
    if (leafTimer_ > 0 || leafSeeds_.empty()) return;
    leafTimer_ = 0.3f;
    std::vector<int64_t> seeds;
    seeds.swap(leafSeeds_);
    std::sort(seeds.begin(), seeds.end());
    seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
    std::unordered_set<int64_t> done;
    for (int64_t sk : seeds) {
        ivec3 s = unpackPos(sk);
        for (int d = -1; d < 6; ++d) {
            int x0 = s.x + (d < 0 ? 0 : LDX[d]), y0 = s.y + (d < 0 ? 0 : LDY[d]), z0 = s.z + (d < 0 ? 0 : LDZ[d]);
            int64_t k0 = packPos(x0, y0, z0);
            if (done.count(k0) || !isLeafB(world_->get(x0, y0, z0)) || !world_->loaded(x0, z0)) continue;
            std::vector<int64_t> patch{k0};
            std::unordered_map<int64_t, int> dist;
            dist[k0] = -1;
            std::vector<int64_t> front;
            bool bounded = true;
            for (size_t i = 0; i < patch.size(); ++i) {
                if (patch.size() > 6000) {
                    bounded = false;
                    break;
                }
                ivec3 p = unpackPos(patch[i]);
                bool wood = false;
                for (int e = 0; e < 6; ++e) {
                    int nx = p.x + LDX[e], ny = p.y + LDY[e], nz = p.z + LDZ[e];
                    uint8_t nb = world_->get(nx, ny, nz);
                    if (isWoodB(nb))
                        wood = true;
                    else if (isLeafB(nb)) {
                        int64_t nk = packPos(nx, ny, nz);
                        if (!dist.count(nk)) {
                            dist[nk] = -1;
                            patch.push_back(nk);
                        }
                    }
                }
                if (wood) {
                    front.push_back(patch[i]);
                    dist[patch[i]] = 1;
                }
            }
            for (int64_t k : patch) done.insert(k);
            if (!bounded) continue;
            for (size_t i = 0; i < front.size(); ++i) {
                ivec3 p = unpackPos(front[i]);
                int dd = dist[front[i]];
                if (dd >= LEAF_REACH) continue;
                for (int e = 0; e < 6; ++e) {
                    int64_t nk = packPos(p.x + LDX[e], p.y + LDY[e], p.z + LDZ[e]);
                    auto it = dist.find(nk);
                    if (it == dist.end() || it->second != -1) continue;
                    it->second = dd + 1;
                    front.push_back(nk);
                }
            }
            for (int64_t k : patch) {
                if (dist[k] != -1) continue;
                ivec3 p = unpackPos(k);
                if (world_->metaAt(p.x, p.y, p.z) & LEAF_PLACED) continue;
                withering_.push_back({k, 0.3f + rng_.uniform() * 3.5f});
            }
        }
    }
}

}
