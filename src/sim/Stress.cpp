#include "sim/Sim.h"
#include "core/Noise.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace qc {
namespace {
inline vec3 cellCenter(int x, int y, int z) { return {(x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX}; }
constexpr int SMAX = 1000;

float warnTime(uint8_t b) {
    const BlockDef& d = def(b);
    if (b == B_PROP || b == B_BEAM || d.sound == Sound::Wood) return 2.0f;
    if (d.sound == Sound::Stone) return 1.0f;
    return 1.5f;
}

bool structural(uint8_t b) {
    const BlockDef& d = def(b);
    return d.collides && !d.fluid && d.render != Render::Cross;
}
}

float Sim::spanLimit(ivec3 c) const {
    uint8_t b = world_->get(c.x, c.y, c.z);
    return reachOf(b);
}

float Sim::reachOf(uint8_t b) {
    const BlockDef& d = def(b);
    if (b == B_PROP || b == B_BEAM) return 10.0f;
    if (b == B_BEDROCK || b == B_OBSIDIAN) return 30.0f;
    if (d.gravity) return 0.6f;
    if (d.render == Render::Leaves) return 0.0f;
    if (d.render == Render::Glass) return 2.5f;
    if (b == B_REBAR) return 13.0f;
    if (b == B_CONCRETE) return 7.0f;
    if (b == B_RUST) return 8.0f;
    if (b == B_GRATE) return 5.0f;
    if (b == B_SCAFFOLD) return 4.0f;
    if (b == B_SCORIA || b == B_SALT) return 3.0f;
    if (b == B_BRICKS || b == B_STONE_BRICKS) return 7.0f;
    if (b == B_COBBLE || b == B_MOSSY) return 1.5f;
    if (b == B_DEEPSLATE || b == B_GRANITE || b == B_DIORITE || b == B_ANDESITE || b == B_TUFF) return 6.5f;
    if (b == B_ICE || b == B_PACKED_ICE) return 3.5f;
    if (d.sound == Sound::Wood) return 6.0f;
    if (d.sound == Sound::Stone) return 6.0f;
    return 2.6f;
}

void Sim::solveSupport(const SupportBox& box, const std::vector<std::pair<ivec3, uint8_t>>* toggle, std::vector<int16_t>& S,
                       const std::vector<ivec3>* cracked) const {
    const int nx = box.nx, ny = box.ny, nz = box.nz;
    const size_t N = size_t(nx) * ny * nz;
    static thread_local std::vector<uint8_t> blk;
    static thread_local std::vector<int16_t> dec;
    blk.resize(N);
    dec.resize(N);
    S.assign(N, -1);
    auto I = [&](int x, int y, int z) { return size_t((y * nz + z) * nx + x); };
    for (int y = 0; y < ny; ++y)
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x) blk[I(x, y, z)] = world_->get(box.x0 + x, box.y0 + y, box.z0 + z);
    if (toggle)
        for (auto& [c, b] : *toggle) {
            int x = c.x - box.x0, y = c.y - box.y0, z = c.z - box.z0;
            if (x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz) blk[I(x, y, z)] = b;
        }
    static thread_local std::vector<uint8_t> crack;
    crack.assign(N, 0);
    if (cracked && !cracked->empty()) {
        const int R = 2;
        for (const ivec3& c : *cracked) {
            int cx = c.x - box.x0, cy = c.y - box.y0, cz = c.z - box.z0;
            for (int y = std::max(0, cy - R); y <= std::min(ny - 1, cy + R); ++y)
                for (int z = std::max(0, cz - R); z <= std::min(nz - 1, cz + R); ++z)
                    for (int x = std::max(0, cx - R); x <= std::min(nx - 1, cx + R); ++x) {
                        int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz);
                        if (d2 <= R * R) crack[I(x, y, z)] = 1;
                    }
        }
    }
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            int run = 0;
            for (int y = ny - 1; y >= 0; --y) {
                uint8_t b = blk[I(x, y, z)];
                if (!structural(b)) {
                    run = 0;
                    continue;
                }
                float reach = reachOf(b) * (1.0f + 0.18f * float(std::min(run, 6))) * (crack[I(x, y, z)] ? 0.9f : 1.0f);
                dec[I(x, y, z)] = int16_t(std::clamp(int(float(SMAX) / reach + 0.5f), 1, SMAX + 1));
                if (!def(b).gravity) ++run;
            }
        }
    static thread_local std::vector<std::vector<int>> buckets;
    buckets.resize(SMAX + 1);
    for (auto& bucket : buckets) bucket.clear();
    for (int y = 0; y < ny; ++y)
        for (int z = 0; z < nz; ++z)
            for (int x = 0; x < nx; ++x) {
                size_t i = I(x, y, z);
                if (!structural(blk[i])) continue;
                bool edge = y == 0 || x == 0 || z == 0 || x == nx - 1 || z == nz - 1 || blk[i] == B_BEDROCK || box.y0 + y <= 2;
                if (edge) {
                    S[i] = SMAX;
                    buckets[SMAX].push_back(int(i));
                } else
                    S[i] = 0;
            }
    const int dx[10] = {1, -1, 0, 0, 0, 0, 1, 1, -1, -1}, dy[10] = {0, 0, 1, -1, 0, 0, 0, 0, 0, 0},
              dz[10] = {0, 0, 0, 0, 1, -1, 1, -1, 1, -1};
    for (int level = SMAX; level > 0; --level) {
        auto& q = buckets[size_t(level)];
        for (size_t qi = 0; qi < q.size(); ++qi) {
            int i = q[qi];
            if (S[size_t(i)] != level) continue;
            int x = i % nx, z = (i / nx) % nz, y = i / (nx * nz);
            const uint8_t from = blk[size_t(i)];
            const bool fromLoose = def(from).gravity, fromLeaf = def(from).render == Render::Leaves;
            for (int d = 0; d < 10; ++d) {
                int ax = x + dx[d], ay = y + dy[d], az = z + dz[d];
                if (ax < 0 || ay < 0 || az < 0 || ax >= nx || ay >= ny || az >= nz) continue;
                size_t j = I(ax, ay, az);
                if (S[j] < 0) continue;
                if (d >= 6 && S[I(ax, y, z)] < 0 && S[I(x, y, az)] < 0) continue;
                const uint8_t to = blk[j];
                int v;
                if (d == 2)
                    v = level;
                else {
                    if (fromLoose) continue;
                    if (fromLeaf && def(to).render != Render::Leaves) continue;
                    v = level - (d == 3 ? 2 * dec[j] : d >= 6 ? (dec[j] * 1414 + 500) / 1000 : dec[j]);
                }
                if (v > S[j]) {
                    S[j] = int16_t(v);
                    if (v > 0) buckets[size_t(v)].push_back(int(j));
                }
            }
        }
        q.clear();
    }
}

Sim::SupportBox Sim::supportBoxAround(ivec3 mn, ivec3 mx) const {
    SupportBox b;
    const int H = 20;
    b.x0 = mn.x - H;
    b.z0 = mn.z - H;
    b.y0 = std::max(0, mn.y - 12);
    int y1 = std::min(WH - 1, mx.y + 18);
    b.nx = mx.x - mn.x + 1 + 2 * H;
    b.nz = mx.z - mn.z + 1 + 2 * H;
    b.ny = y1 - b.y0 + 1;
    return b;
}

void Sim::structuralEdit(const std::vector<ivec3>& cells, bool removed, int generation, float warnScale) {
    if (cells.empty()) return;
    ivec3 mn = cells[0], mx = cells[0];
    for (auto& c : cells) {
        mn = {std::min(mn.x, c.x), std::min(mn.y, c.y), std::min(mn.z, c.z)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)};
    }
    if (mx.x - mn.x > 64 || mx.z - mn.z > 64) {
        std::vector<ivec3> a, b;
        bool alongX = mx.x - mn.x >= mx.z - mn.z;
        int mid = alongX ? (mn.x + mx.x) / 2 : (mn.z + mx.z) / 2;
        for (auto& c : cells) ((alongX ? c.x : c.z) <= mid ? a : b).push_back(c);
        structuralEdit(a, removed, generation, warnScale);
        structuralEdit(b, removed, generation, warnScale);
        return;
    }
    SupportBox box = supportBoxAround(mn, mx);
    const int site = nextSite_++;
    bool usedSite = false;
    std::vector<std::pair<ivec3, uint8_t>> before;
    before.reserve(cells.size());
    for (auto& c : cells) {
        uint8_t was = B_AIR;
        if (removed) {
            auto it = removedBlocks_.find(packPos(c.x, c.y, c.z));
            was = it != removedBlocks_.end() ? it->second : uint8_t(B_STONE);
        }
        before.push_back({c, was});
    }
    static thread_local std::vector<int16_t> sNow, sWas;
    std::vector<ivec3> cracked;
    for (auto& [c, t] : cracks_)
        if (c.x >= box.x0 - 6 && c.z >= box.z0 - 6 && c.x < box.x0 + box.nx + 6 && c.z < box.z0 + box.nz + 6) cracked.push_back(c);
    solveSupport(box, nullptr, sNow, &cracked);
    solveSupport(box, &before, sWas, &cracked);
    std::unordered_set<int64_t> have;
    for (auto& s : stress_) have.insert(packPos(s.c.x, s.c.y, s.c.z));
    // QC_DEBUG_STRESS=1 dumps this to the console
    if (std::getenv("QC_DEBUG_STRESS")) {
        int a0 = 0, a1 = 0;
        for (size_t i = 0; i < sNow.size(); ++i) {
            if (sNow[i] == 0) ++a0;
            if (sWas[i] == 0) ++a1;
        }
        std::printf("[stress] edit gen %d: %zu cells, unsupported now %d, before %d\n", generation, cells.size(), a0, a1);
    }
    for (int y = 0; y < box.ny; ++y)
        for (int z = 0; z < box.nz; ++z)
            for (int x = 0; x < box.nx; ++x) {
                size_t i = size_t((y * box.nz + z) * box.nx + x);
                if (sNow[i] != 0 || sWas[i] <= 0) continue;
                ivec3 c{box.x0 + x, box.y0 + y, box.z0 + z};
                uint8_t b = world_->get(c.x, c.y, c.z);
                if (def(b).hardness < 0 || b == B_BEDROCK) continue;
                if (!have.insert(packPos(c.x, c.y, c.z)).second) continue;
                if (stress_.size() >= 5000) return;
                Stress s;
                s.c = c;
                s.block = b;
                s.ratio = 2.0f;
                s.timer = 0;
                s.warn = warnTime(b) * warnScale * (0.85f + 0.3f * hashf(c.x, c.y, c.z, 0x57E55u));
                s.nextCreak = rng_.range(0.0f, 0.4f);
                s.nextDust = 0;
                s.recheck = 0.4f;
                s.gen = generation;
                s.site = site;
                stress_.push_back(s);
                if (!usedSite) {
                    sites_[site] = box;
                    usedSite = true;
                }
            }
}

void Sim::stepStress(float dt) {
    for (size_t i = 0; i < cracks_.size();) {
        cracks_[i].second -= dt;
        if (cracks_[i].second <= 0) {
            const auto c = cracks_[i].first;
            for (const auto& [id, b] : sites_)
                if (c.x >= b.x0 - 2 && c.z >= b.z0 - 2 && c.x < b.x0 + b.nx + 2 && c.z < b.z0 + b.nz + 2) dirtySites_.insert(id);
            cracks_[i] = cracks_.back();
            cracks_.pop_back();
        } else
            ++i;
    }
    if (stress_.empty()) {
        sites_.clear();
        dirtySites_.clear();
        return;
    }
    if (!dirtySites_.empty()) {
        std::unordered_set<int64_t> held;
        static thread_local std::vector<int16_t> sNow;
        std::unordered_map<int, int> live;
        for (auto& st : stress_) live[st.site]++;
        int solveBudget = 2;
        for (auto it = sites_.begin(); it != sites_.end();) {
            if (!live.count(it->first)) {
                dirtySites_.erase(it->first);
                it = sites_.erase(it);
                continue;
            }
            if (!dirtySites_.count(it->first) || solveBudget <= 0) {
                ++it;
                continue;
            }
            dirtySites_.erase(it->first);
            --solveBudget;
            const SupportBox& box = it->second;
            std::vector<ivec3> cracked;
            for (auto& [c, t] : cracks_)
                if (c.x >= box.x0 - 6 && c.z >= box.z0 - 6 && c.x < box.x0 + box.nx + 6 && c.z < box.z0 + box.nz + 6) cracked.push_back(c);
            solveSupport(box, nullptr, sNow, &cracked);
            for (auto& st : stress_) {
                if (st.site != it->first) continue;
                int x = st.c.x - box.x0, y = st.c.y - box.y0, z = st.c.z - box.z0;
                if (sNow[size_t((y * box.nz + z) * box.nx + x)] > 0) held.insert(packPos(st.c.x, st.c.y, st.c.z));
            }
            ++it;
        }
        const size_t areas = sites_.size();
        if (std::getenv("QC_DEBUG_STRESS") && !stress_.empty()) {
            int ly = 1 << 30;
            ivec3 a{1 << 30, 0, 1 << 30}, b{-(1 << 30), 0, -(1 << 30)};
            for (auto& st : stress_) ly = std::min(ly, st.c.y);
            for (auto& st : stress_) {
                a.x = std::min(a.x, st.c.x);
                a.z = std::min(a.z, st.c.z);
                b.x = std::max(b.x, st.c.x);
                b.z = std::max(b.z, st.c.z);
            }
            SupportBox box = supportBoxAround({a.x, ly, a.z}, {b.x, ly, b.z});
            solveSupport(box, nullptr, sNow);
            std::printf("[stress] layer y=%d held %zu of %zu\n", ly, held.size(), stress_.size());
            for (int z = a.z - 2; z <= b.z + 2; ++z) {
                std::string row;
                for (int x = a.x - 2; x <= b.x + 2; ++x) {
                    int v = sNow[size_t(((ly - box.y0) * box.nz + (z - box.z0)) * box.nx + (x - box.x0))];
                    char c = v < 0 ? '.' : v == 0 ? '#' : char('0' + std::min(9, v / 100));
                    uint8_t bb = world_->get(x, ly - 1, z);
                    if (bb == B_PROP) c = 'P';
                    row += c;
                }
                std::printf("  %s\n", row.c_str());
            }
        }
        if (std::getenv("QC_DEBUG_STRESS")) {
            int mnx = 1 << 30, mxx = -(1 << 30), mny = 1 << 30, mxy = -(1 << 30);
            for (auto& st : stress_) {
                mnx = std::min(mnx, st.c.x);
                mxx = std::max(mxx, st.c.x);
                mny = std::min(mny, st.c.y);
                mxy = std::max(mxy, st.c.y);
            }
            std::printf("[stress] recheck: %zu stressed, %zu held again, %zu sites, x %d..%d y %d..%d\n", stress_.size(), held.size(),
                        areas, mnx, mxx, mny, mxy);
        }
        bool groaned = false;
        for (size_t i = 0; i < stress_.size();) {
            if (held.count(packPos(stress_[i].c.x, stress_[i].c.y, stress_[i].c.z))) {
                // creak once so you know its about to go
                if (!groaned) sounds.push_back({Snd::Creak, cellCenter(stress_[i].c.x, stress_[i].c.y, stress_[i].c.z), 0.35f, 1.3f});
                groaned = true;
                stress_[i] = stress_.back();
                stress_.pop_back();
            } else
                ++i;
        }
    }
    creakCooldown_ -= dt;
    const float dustShare = std::min(1.0f, 30.0f / float(std::max<size_t>(1, stress_.size())));
    for (size_t i = 0; i < stress_.size();) {
        Stress& s = stress_[i];
        if (world_->get(s.c.x, s.c.y, s.c.z) != s.block) {
            stress_[i] = stress_.back();
            stress_.pop_back();
            continue;
        }
        if (!dirtySites_.count(s.site)) s.timer += dt;
        float p = std::min(1.0f, s.timer / s.warn);
        vec3 bottom((s.c.x + 0.5f) * VOX, s.c.y * VOX, (s.c.z + 0.5f) * VOX);
        s.nextCreak -= dt;
        if (s.nextCreak <= 0 && creakCooldown_ <= 0) {
            creakCooldown_ = rng_.range(0.25f, 0.6f);
            bool wood = def(s.block).sound == Sound::Wood;
            sounds.push_back({Snd::Creak, bottom, 0.25f + 0.55f * p, (wood ? 1.0f : 0.6f) + rng_.range(-0.1f, 0.15f) + p * 0.2f});
            s.nextCreak = rng_.range(0.7f, 2.2f) * (1.1f - p * 0.8f) + 0.15f;
        }
        s.nextDust -= dt;
        if (s.nextDust <= 0 && rng_.uniform() < dustShare && !def(world_->get(s.c.x, s.c.y - 1, s.c.z)).solid) {
            uint32_t mc = def(s.block).mapColor;
            vec3 col(float(mc & 255) / 255.0f, float((mc >> 8) & 255) / 255.0f, float((mc >> 16) & 255) / 255.0f);
            float l = col.x * 0.3f + col.y * 0.59f + col.z * 0.11f;
            particle(PKind::Dust, bottom + vec3(rng_.range(-0.22f, 0.22f), -0.02f, rng_.range(-0.22f, 0.22f)), vec3(0, -0.5f, 0),
                     rng_.range(1.0f, 1.8f), rng_.range(0.015f, 0.04f), vec4(l * 1.2f, l * 1.2f, l * 1.22f, 0.45f));
            if (p > 0.55f && rng_.uniform() < 0.3f) blockParticles(bottom - vec3(0, 0.05f, 0), s.block, 1, 0.15f, 0.3f);
            s.nextDust = rng_.range(0.25f, 0.9f) * (1.0f - p * 0.85f);
        } else if (s.nextDust <= 0)
            s.nextDust = rng_.range(0.2f, 0.6f);
        if (s.timer >= s.warn && !dirtySites_.count(s.site)) {
            caveIn(i);
            break;
        }
        ++i;
    }
}

void Sim::caveIn(size_t i) {
    const int gen = stress_[i].gen, site = stress_[i].site;
    std::unordered_map<int64_t, size_t> idx;
    for (size_t k = 0; k < stress_.size(); ++k) {
        const auto& s = stress_[k];
        if (s.site == site && s.gen == gen && s.timer >= s.warn * 0.85f) idx[packPos(s.c.x, s.c.y, s.c.z)] = k;
    }
    std::vector<ivec3> cells{stress_[i].c};
    std::unordered_set<int64_t> seen{packPos(stress_[i].c.x, stress_[i].c.y, stress_[i].c.z)};
    const int DX[6] = {1, -1, 0, 0, 0, 0}, DY[6] = {0, 0, 1, -1, 0, 0}, DZ[6] = {0, 0, 0, 0, 1, -1};
    for (size_t q = 0; q < cells.size() && cells.size() < 8000; ++q)
        for (int d = 0; d < 6; ++d) {
            ivec3 n{cells[q].x + DX[d], cells[q].y + DY[d], cells[q].z + DZ[d]};
            int64_t k = packPos(n.x, n.y, n.z);
            if (!idx.count(k) || !seen.insert(k).second) continue;
            cells.push_back(n);
        }
    for (size_t k = 0; k < stress_.size();) {
        if (seen.count(packPos(stress_[k].c.x, stress_[k].c.y, stress_[k].c.z))) {
            stress_[k] = stress_.back();
            stress_.pop_back();
        } else
            ++k;
    }
    std::vector<uint8_t> blocks, metas;
    std::vector<ivec3> real;
    for (auto& c : cells) {
        uint8_t b = world_->get(c.x, c.y, c.z);
        if (!structural(b)) continue;
        real.push_back(c);
        blocks.push_back(b);
        metas.push_back(world_->metaAt(c.x, c.y, c.z));
    }
    if (real.empty()) return;
    bool allLeaves = true;
    for (uint8_t b : blocks)
        if (def(b).render != Render::Leaves) {
            allLeaves = false;
            break;
        }
    if (allLeaves) {
        for (auto& c : real) leafEvent(c.x, c.y, c.z);
        return;
    }
    vec3 center;
    for (auto& c : real) center += cellCenter(c.x, c.y, c.z);
    center = center / float(real.size());
    for (auto& c : real) {
        removedBlocks_[packPos(c.x, c.y, c.z)] = world_->get(c.x, c.y, c.z);
        setBlockBatch(c.x, c.y, c.z, B_AIR);
    }
    int pieces = std::clamp(int(real.size()) / 45, 1, 7);
    int woody = 0;
    for (uint8_t b : blocks)
        if (def(b).sound == Sound::Wood || def(b).render == Render::Leaves) ++woody;
    if (woody * 10 >= int(blocks.size()) * 8) pieces = 1;
    std::vector<vec3> seeds;
    for (int k = 0; k < pieces; ++k) {
        const ivec3& c = real[size_t(rng_.uniform() * float(real.size())) % real.size()];
        seeds.push_back(cellCenter(c.x, c.y, c.z));
    }
    std::vector<std::vector<ivec3>> pc(static_cast<size_t>(pieces));
    std::vector<std::vector<uint8_t>> pb(static_cast<size_t>(pieces)), pm(static_cast<size_t>(pieces));
    for (size_t k = 0; k < real.size(); ++k) {
        vec3 p = cellCenter(real[k].x, real[k].y, real[k].z);
        p += vec3(hashf(real[k].x, real[k].y, real[k].z, 0xC4A1u) - 0.5f, 0, hashf(real[k].z, real[k].y, real[k].x, 0xC4A2u) - 0.5f) * 0.8f;
        size_t best = 0;
        float bd = 1e9f;
        for (size_t s = 0; s < seeds.size(); ++s) {
            float d = lengthSq(p - seeds[s]);
            if (d < bd) {
                bd = d;
                best = s;
            }
        }
        pc[best].push_back(real[k]);
        pb[best].push_back(blocks[k]);
        pm[best].push_back(metas[k]);
    }
    for (int k = 0; k < pieces; ++k) {
        if (pc[size_t(k)].empty()) continue;
        if (pc[size_t(k)].size() <= 2) {
            for (size_t j = 0; j < pc[size_t(k)].size(); ++j) {
                Entity e;
                e.type = EntType::Debris;
                e.block = pb[size_t(k)][j];
                e.pos = vec3((pc[size_t(k)][j].x + 0.5f) * VOX, pc[size_t(k)][j].y * VOX, (pc[size_t(k)][j].z + 0.5f) * VOX);
                e.w = e.h = VOX * 0.98f;
                e.spin = vec3(rng_.range(-3, 3), rng_.range(-3, 3), rng_.range(-3, 3));
                addEntity(std::move(e));
            }
            continue;
        }
        vec3 pcen;
        for (auto& c : pc[size_t(k)]) pcen += cellCenter(c.x, c.y, c.z);
        pcen = pcen / float(pc[size_t(k)].size());
        vec3 out = pcen - center;
        out.y = 0;
        vec3 tilt = cross(vec3(0, 1, 0), normalize(out, vec3(1, 0, 0))) * rng_.range(0.2f, 0.7f);
        createBodyFromCells(pc[size_t(k)], pb[size_t(k)], vec3(out.x * 0.15f, -0.4f - rng_.uniform() * 0.6f, out.z * 0.15f), tilt,
                            &pm[size_t(k)]);
    }
    sounds.push_back({Snd::Rumble, center, std::min(1.0f, 0.5f + real.size() / 80.0f), rng_.range(0.85f, 1.05f)});
    sounds.push_back({Snd::Crack, center, 0.9f, 0.6f});
    dustPuff(center - vec3(0, 0.5f, 0), blocks[0], std::clamp(int(real.size()) / 3, 12, 40), 2.2f);
    float d = length(center - listener);
    shake = std::max(shake, std::clamp(1.0f - d / 30.0f, 0.0f, 1.0f) * std::min(1.0f, 0.3f + real.size() / 100.0f));
    if (gen < 2) structuralEdit(real, true, gen + 1, 1.25f);
}

float Sim::spanAt(ivec3 c, const std::vector<ivec3>*, const std::vector<ivec3>*, bool) const {
    SupportBox box = supportBoxAround(c, c);
    std::vector<int16_t> S;
    solveSupport(box, nullptr, S);
    int x = c.x - box.x0, y = c.y - box.y0, z = c.z - box.z0;
    int s = S[size_t((y * box.nz + z) * box.nx + x)];
    return s > 0 ? 0.0f : 1e3f;
}

}
