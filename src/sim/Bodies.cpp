#include "sim/Sim.h"
#include "world/Mesher.h"
#include "world/Light.h"
#include <algorithm>
#include <cmath>

namespace qc {
namespace {
constexpr float BODY_G = 10.5f;
inline vec3 crossv(vec3 a, vec3 b) { return cross(a, b); }

float strengthOf(const Body& b) {
    int counts[3] = {0, 0, 0};
    for (uint8_t v : b.vox) {
        if (!v) continue;
        const BlockDef& d = def(v);
        if (d.render == Render::Leaves || d.render == Render::Glass) continue;
        if (d.sound == Sound::Wood)
            counts[1]++;
        else if (d.sound == Sound::Stone)
            counts[2]++;
        else
            counts[0]++;
    }
    if (counts[1] >= counts[0] && counts[1] >= counts[2]) return counts[1] ? 11.0f : 4.0f;
    if (counts[2] >= counts[0]) return 9.0f;
    return 6.5f;
}
}

void Sim::buildBody(Body& b) {
    b.count = 0;
    b.contactCount = 0;
    b.sleeping = false;
    double mass = 0;
    vec3 m1;
    for (int y = 0; y < b.sy; ++y)
        for (int z = 0; z < b.sz; ++z)
            for (int x = 0; x < b.sx; ++x) {
                uint8_t v = b.at(x, y, z);
                if (!v) continue;
                float m = (def(v).render == Render::Leaves ? 0.01f : std::max(def(v).density, 0.1f)) * 1000.0f * 0.125f;
                mass += m;
                m1 += vec3((x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX) * m;
                b.count++;
            }
    if (b.count == 0) {
        b.dead = true;
        return;
    }
    b.mass = float(mass);
    b.invMass = 1.0f / b.mass;
    b.com = m1 / b.mass;
    mat3 I = mat3::zero();
    const float cube = VOX * VOX / 6.0f;
    b.samples.clear();
    b.emitters.clear();
    b.floatPts.clear();
    std::unordered_set<int64_t> corners;
    for (int y = 0; y < b.sy; ++y)
        for (int z = 0; z < b.sz; ++z)
            for (int x = 0; x < b.sx; ++x) {
                uint8_t v = b.at(x, y, z);
                if (!v) continue;
                float m = (def(v).render == Render::Leaves ? 0.01f : std::max(def(v).density, 0.1f)) * 1000.0f * 0.125f;
                vec3 r = vec3((x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX) - b.com;
                if (def(v).emit && b.emitters.size() < 3) b.emitters.push_back({r, v});
                float r2 = dot(r, r);
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) I.m[i][j] += m * ((i == j ? r2 + cube : 0.0f) - r[i] * r[j]);
                bool surface = !def(b.at(x + 1, y, z)).collides || !def(b.at(x - 1, y, z)).collides || !def(b.at(x, y + 1, z)).collides ||
                               !def(b.at(x, y - 1, z)).collides || !def(b.at(x, y, z + 1)).collides || !def(b.at(x, y, z - 1)).collides;
                if (surface && def(v).collides)
                    for (int c = 0; c < 8; ++c) {
                        int cx = x + (c & 1), cy = y + ((c >> 1) & 1), cz = z + (c >> 2);
                        corners.insert((int64_t(cx) << 40) | (int64_t(cy) << 20) | int64_t(cz));
                    }
                if (def(v).collides) b.floatPts.push_back(vec3((x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX) - b.com);
            }
    b.invInertiaLocal = I.inverse();
    b.impactStrength = strengthOf(b);
    b.leafCells.clear();
    for (int y = 0; y < b.sy; ++y)
        for (int z = 0; z < b.sz; ++z)
            for (int x = 0; x < b.sx; ++x) {
                uint8_t v = b.at(x, y, z);
                if (v && def(v).render == Render::Leaves &&
                    (!b.at(x, y + 1, z) || !b.at(x + 1, y, z) || !b.at(x - 1, y, z) || !b.at(x, y, z + 1) || !b.at(x, y, z - 1)))
                    b.leafCells.push_back({x, y, z});
            }
    if (b.leafCells.size() > 96) {
        std::vector<ivec3> sub;
        size_t st = b.leafCells.size() / 96 + 1;
        for (size_t k = 0; k < b.leafCells.size(); k += st) sub.push_back(b.leafCells[k]);
        b.leafCells.swap(sub);
    }
    std::vector<int64_t> ordered(corners.begin(), corners.end());
    std::sort(ordered.begin(), ordered.end());
    b.contactRadius = 0;
    b.contactMin = vec3(1e9f);
    b.contactMax = vec3(-1e9f);
    for (int64_t k : ordered) {
        vec3 c(float((k >> 40) & 0xFFFFF) * VOX, float((k >> 20) & 0xFFFFF) * VOX, float(k & 0xFFFFF) * VOX);
        b.contactRadius = std::max(b.contactRadius, lengthSq(c - b.com));
        for (int axis = 0; axis < 3; ++axis) {
            b.contactMin[axis] = std::min(b.contactMin[axis], c[axis] - b.com[axis]);
            b.contactMax[axis] = std::max(b.contactMax[axis], c[axis] - b.com[axis]);
        }
    }
    b.contactRadius = std::sqrt(b.contactRadius);
    if (ordered.empty()) b.contactMin = b.contactMax = {};
    const size_t n = std::min<size_t>(ordered.size(), 384);
    for (size_t i = 0; i < n; ++i) {
        int64_t k = ordered[i * ordered.size() / n];
        vec3 c(float((k >> 40) & 0xFFFFF) * VOX, float((k >> 20) & 0xFFFFF) * VOX, float(k & 0xFFFFF) * VOX);
        b.samples.push_back(c - b.com);
    }
    const size_t floatCount = b.floatPts.size();
    if (b.floatPts.size() > 96) {
        std::vector<vec3> sub;
        size_t st = b.floatPts.size() / 96 + 1;
        for (size_t k = 0; k < b.floatPts.size(); k += st) sub.push_back(b.floatPts[k]);
        b.floatPts.swap(sub);
    }
    b.floatVol = float(floatCount) * 0.125f / float(std::max<size_t>(1, b.floatPts.size()));
    b.meshDirty = true;
    ++b.meshVersion;
}

void Sim::createBodyFromCells(const std::vector<ivec3>& cells, const std::vector<uint8_t>& blocks, vec3 vel, vec3 angVel,
                              const std::vector<uint8_t>* metas) {
    if (cells.empty()) return;
    ivec3 mn = cells[0], mx = cells[0];
    for (auto& c : cells) {
        mn = {std::min(mn.x, c.x), std::min(mn.y, c.y), std::min(mn.z, c.z)};
        mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)};
    }
    const int MAXD = 60;
    if (mx.x - mn.x >= MAXD || mx.y - mn.y >= MAXD || mx.z - mn.z >= MAXD) {
        struct Tile {
            std::vector<ivec3> c;
            std::vector<uint8_t> b, m;
        };
        std::unordered_map<int64_t, Tile> tiles;
        for (size_t i = 0; i < cells.size(); ++i) {
            const ivec3& c = cells[i];
            int64_t k = packPos((c.x - mn.x) / MAXD, (c.y - mn.y) / MAXD, (c.z - mn.z) / MAXD);
            tiles[k].c.push_back(c);
            tiles[k].b.push_back(blocks[i]);
            tiles[k].m.push_back(metas ? (*metas)[i] : 0);
        }
        for (auto& [k, t] : tiles) createBodyFromCells(t.c, t.b, vel, angVel, &t.m);
        return;
    }
    while (int(bodies_.size()) >= settings.maxBodies) {
        auto still = [](const Body& a) { return length(a.vel) + length(a.angVel) * 0.5f - std::min(a.age, 30.0f) * 0.02f; };
        auto it = std::min_element(bodies_.begin(), bodies_.end(), [&](const Body& a, const Body& b) { return still(a) < still(b); });
        settleBody(*it, true);
        bodies_.erase(it);
    }
    Body b;
    b.id = nextBodyId_++;
    stats.bodiesCreated++;
    b.sx = mx.x - mn.x + 1;
    b.sy = mx.y - mn.y + 1;
    b.sz = mx.z - mn.z + 1;
    b.vox.assign(size_t(b.sx) * b.sy * b.sz, 0);
    b.vmeta.assign(b.vox.size(), 0);
    for (size_t i = 0; i < cells.size(); ++i) {
        const ivec3& c = cells[i];
        size_t idx = size_t(((c.y - mn.y) * b.sz + (c.z - mn.z)) * b.sx + (c.x - mn.x));
        b.vox[idx] = blocks[i];
        if (metas && orientable(blocks[i])) {
            uint8_t m = (*metas)[i];
            b.vmeta[idx] = uint8_t(metaOrient(m) | (metaPhase(m, c.x, c.y, c.z) << 5));
        } else if (metas)
            b.vmeta[idx] = (*metas)[i];
    }
    buildBody(b);
    if (b.dead) return;
    b.pos = vec3(mn.x * VOX, mn.y * VOX, mn.z * VOX) + b.com;
    b.vel = vel;
    b.angVel = angVel;
    int fx0 = 1 << 30, fx1 = -(1 << 30), fz0 = 1 << 30, fz1 = -(1 << 30);
    for (const ivec3& c : cells)
        if (c.y <= mn.y + 1) {
            fx0 = std::min(fx0, c.x);
            fx1 = std::max(fx1, c.x);
            fz0 = std::min(fz0, c.z);
            fz1 = std::max(fz1, c.z);
        }
    float footprint = float(std::max(fx1 - fx0, fz1 - fz0) + 1) * VOX;
    if (b.sy > std::max(b.sx, b.sz) * 1.3f || b.com.y > footprint * 1.2f) {
        float a = rng_.range(0, 6.2831853f);
        b.angVel += vec3(std::cos(a), 0, std::sin(a)) * rng_.range(0.25f, 0.5f);
        b.vel += vec3(std::sin(a), 0, -std::cos(a)) * 0.3f;
    }
    {
        int made = 0;
        for (size_t i = 0; i < cells.size() && made < 8; i += std::max<size_t>(1, cells.size() / 24)) {
            const ivec3& c = cells[i];
            if (c.y > mn.y + 1) continue;
            vec3 w((c.x + 0.5f) * VOX, c.y * VOX, (c.z + 0.5f) * VOX);
            blockParticles(w, blocks[i], 2, 0.2f, 1.0f);
            if ((made & 1) == 0) dustPuff(w, blocks[i], 2, 0.6f);
            ++made;
        }
    }
    if (Column* col = world_->columnAt(cells[0].x, cells[0].z)) {
        int ci = (cells[0].z & 31) * CS + (cells[0].x & 31);
        b.climT = col->climT[ci];
        b.climH = col->climH[ci];
    }
    bodies_.push_back(std::move(b));
}

bool Sim::bodyContactWorld(vec3 p, vec3& n, float& depth) const {
    int cx = cellOf(p.x), cy = cellOf(p.y), cz = cellOf(p.z);
    if (!world_->collidesAt(cx, cy, cz)) return false;
    float fx = p.x / VOX - cx, fy = p.y / VOX - cy, fz = p.z / VOX - cz;
    float best = 1e9f;
    vec3 bn(0, 1, 0);
    auto test = [&](int dx, int dy, int dz, float d) {
        if (d < best && !world_->collidesAt(cx + dx, cy + dy, cz + dz)) {
            best = d;
            bn = vec3(float(dx), float(dy), float(dz));
        }
    };
    test(0, 1, 0, (1 - fy) * VOX);
    test(1, 0, 0, (1 - fx) * VOX);
    test(-1, 0, 0, fx * VOX);
    test(0, 0, 1, (1 - fz) * VOX);
    test(0, 0, -1, fz * VOX);
    test(0, -1, 0, fy * VOX);
    if (best > 1e8f) {
        bn = vec3(0, 1, 0);
        best = (1 - fy) * VOX;
    }
    n = bn;
    depth = best;
    return true;
}

namespace {
bool pointInBodyFrame(const Body& b, const mat3& R, vec3 p, vec3* exitN, float* depth) {
    vec3 L = R.transpose() * (p - b.pos) + b.com;
    vec3 g = L / VOX;
    int x = int(std::floor(g.x)), y = int(std::floor(g.y)), z = int(std::floor(g.z));
    uint8_t v = b.at(x, y, z);
    if (!v || !def(v).collides) return false;
    if (exitN) {
        float fx = g.x - x, fy = g.y - y, fz = g.z - z;
        float best = 1e9f;
        vec3 bn(0, 1, 0);
        auto test = [&](int dx, int dy, int dz, float d) {
            if (d < best && !b.at(x + dx, y + dy, z + dz)) {
                best = d;
                bn = vec3(float(dx), float(dy), float(dz));
            }
        };
        test(0, 1, 0, 1 - fy);
        test(1, 0, 0, 1 - fx);
        test(-1, 0, 0, fx);
        test(0, 0, 1, 1 - fz);
        test(0, 0, -1, fz);
        test(0, -1, 0, fy);
        if (best > 1e8f) best = 1 - fy;
        *exitN = R * bn;
        *depth = best * VOX;
    }
    return true;
}

}
bool Sim::pointInBody(const Body& b, vec3 p, vec3* normal, float* depth) const {
    return pointInBodyFrame(b, toMat3(b.rot), p, normal, depth);
}

void Sim::stepBodies(float dt, Player& player) {
    float travel = 0;
    for (const auto& b : bodies_)
        if (!b.dead && !b.sleeping) {
            float dist = length(b.pos - player.pos);
            float precision = dist < 32.0f ? 1.0f : dist < 64.0f ? .82f : dist < 96.0f ? .70f : .60f;
            travel = std::max(travel, (length(b.vel) + length(b.angVel) * b.contactRadius) * dt * precision);
        }
    int steps = std::clamp(int(std::ceil(travel / (VOX * .35f))), 1, 8);
    for (int i = 0; i < steps; ++i) solveBodies(dt / steps, player);
}

void Sim::solveBodies(float dt, Player& player) {
    struct Contact {
        vec3 p, n, r;
        float depth = 0, jn = 0;
        int sample = -1;
        vec3 rn, an, t1, t2, rt1, rt2, at1, at2;
        float kn = 0, kt1 = 0, kt2 = 0, jt1 = 0, jt2 = 0, bounce = 0;
    };
    std::vector<Contact> cs;
    cs.reserve(384);
    std::vector<size_t> toFracture;
    std::vector<float> fractureSpeed;
    std::vector<vec3> fracturePoint;
    std::vector<size_t> chipList;
    std::vector<vec3> chipPoint;
    std::vector<float> chipSpeed;
    for (size_t bi = 0; bi < bodies_.size(); ++bi) {
        Body& b = bodies_[bi];
        if (b.dead) continue;
        if (b.sleeping) {
            b.simStepped = false;
            b.simCarry = 0;
            ++stats.sleepingBodies;
            if (!b.meshPending && !b.meshDirty) settleBody(b);
            continue;
        }
        float targetStep = b.simTier == 0 ? dt : b.simTier == 1 ? 1.0f / 60.0f : b.simTier == 2 ? 1.0f / 30.0f : 1.0f / 15.0f;
        const float edgeSpeed = length(b.vel) + length(b.angVel) * std::max(.5f, b.contactRadius);
        if (b.age < .75f || edgeSpeed > 8.0f)
            targetStep = dt;
        else if (edgeSpeed > 3.0f || b.inWater > .05f)
            targetStep = std::min(targetStep, 1.0f / 60.0f);
        b.simCarry = std::min(.10f, b.simCarry + dt);
        if (b.simCarry + 1e-7f < targetStep) {
            b.simStepped = false;
            continue;
        }
        const float bdt = b.simCarry;
        b.simCarry = 0;
        b.simStepped = true;
        b.age += bdt;
        const float playerDist = length(b.pos - player.pos);
        const size_t contactStride = playerDist > 112.0f ? 3 : playerDist > 64.0f ? 2 : 1;
        const bool nearEffects = playerDist < 72.0f;
        mat3 R = toMat3(b.rot);
        mat3 invI = R * b.invInertiaLocal * R.transpose();
        vec3 localHalf = (b.contactMax - b.contactMin) * .5f;
        vec3 center = b.pos + R * ((b.contactMax + b.contactMin) * .5f), extent;
        for (int a = 0; a < 3; ++a)
            extent[a] = std::abs(R.m[a][0]) * localHalf.x + std::abs(R.m[a][1]) * localHalf.y + std::abs(R.m[a][2]) * localHalf.z + .01f;
        uint8_t nearby = world_->regionPhysicsKinds(center - extent, center + extent);
        b.vel.y -= BODY_G * bdt;
        int wet = 0;
        if (nearby & 2)
            for (const vec3& f : b.floatPts) {
                vec3 p = b.pos + R * f;
                int cx = cellOf(p.x), cy = cellOf(p.y), cz = cellOf(p.z);
                if (world_->get(cx, cy, cz) != B_WATER) continue;
                float top = cy * VOX + fluidSurfaceAmountOf(B_WATER, world_->metaAt(cx, cy, cz)) / float(MAX_FLUID) * VOX;
                if (p.y > top) continue;
                ++wet;
                vec3 F(0, 1000.0f * BODY_G * b.floatVol, 0);
                b.vel += F * (b.invMass * bdt);
                b.angVel += invI * crossv(p - b.pos, F) * bdt;
            }
        float wetFrac = b.floatPts.empty() ? 0 : float(wet) / float(b.floatPts.size());
        if (wetFrac > 0) {
            if (nearEffects && b.inWater == 0 && length(b.vel) > 3.0f) {
                sounds.push_back({Snd::Splash, b.pos, std::min(1.0f, b.mass / 3000.0f + 0.3f), 0.8f});
                for (int i = 0; i < 12; ++i)
                    particle(PKind::Splash, b.pos + vec3(rng_.range(-1, 1), 0, rng_.range(-1, 1)),
                             vec3(rng_.range(-2, 2), rng_.range(3, 6), rng_.range(-2, 2)), 0.8f, 0.08f, vec4(0.6f, 0.75f, 1.0f, 0.8f));
            }
            b.vel *= std::exp(-bdt * 1.8f * wetFrac);
            b.angVel *= std::exp(-bdt * 2.5f * wetFrac);
        }
        b.inWater = wetFrac;
        b.angVel *= std::exp(-bdt * 0.05f);
        if (!b.leafCells.empty() && playerDist < 64.0f) {
            float sp = length(b.vel) + length(b.angVel) * 2.0f;
            if (sp > 1.2f) {
                b.shed += bdt * std::min(40.0f, float(b.leafCells.size()) * 0.3f) * std::min(1.0f, sp / 6.0f);
                while (b.shed >= 1.0f) {
                    b.shed -= 1.0f;
                    const ivec3& c = b.leafCells[rng_.next() % b.leafCells.size()];
                    uint8_t v = b.at(c.x, c.y, c.z);
                    if (!v) continue;
                    vec3 r = R * (vec3((c.x + rng_.uniform()) * VOX, (c.y + rng_.uniform()) * VOX, (c.z + rng_.uniform()) * VOX) - b.com);
                    vec3 pv = (b.vel + crossv(b.angVel, r)) * 0.35f +
                              vec3(rng_.range(-0.6f, 0.6f), rng_.range(0.0f, 0.8f), rng_.range(-0.6f, 0.6f));
                    float g = rng_.range(0.8f, 1.1f);
                    vec4 tint = v == B_LEAVES || v == B_JUNGLE_LEAVES || v == B_DARK_OAK_LEAVES ? vec4(0.36f * g, 0.5f * g, 0.22f * g, 1)
                                                                                                : vec4(0.42f * g, 0.52f * g, 0.3f * g, 1);
                    particle(PKind::Leaf, b.pos + r, pv, rng_.range(2.5f, 4.5f), rng_.range(0.1f, 0.16f), tint, def(v).texSide);
                }
            }
        }

        cs.clear();
        float maxApproach = 0;
        vec3 impactPt;
        bool supported = true;
        const bool crushCheck = playerDist < 72.0f && lengthSq(b.vel) > 0.25f && (++crushTick_ & 3) == 0;
        if ((nearby & 1) || (crushCheck && (nearby & 4)))
            for (size_t sample = 0; sample < b.samples.size(); sample += contactStride) {
                ++stats.contactSamples;
                const vec3& s = b.samples[sample];
                vec3 r = R * s;
                vec3 p = b.pos + r;
                if (crushCheck) {
                    int px = cellOf(p.x), py = cellOf(p.y), pz = cellOf(p.z);
                    uint8_t wb = world_->get(px, py, pz);
                    if ((def(wb).render == Render::Leaves || (isPlantDetail(wb) && wb != B_FIRE && wb != B_TORCH)) && crushed_.size() < 128)
                        crushed_.push_back({px, py, pz});
                }
                vec3 n;
                float depth;
                if (!(nearby & 1) || !bodyContactWorld(p, n, depth)) continue;
                cs.push_back({p, n, r, depth, 0, int(sample)});
                vec3 v = b.vel + crossv(b.angVel, r);
                float vn = dot(v, n);
                if (-vn > maxApproach) {
                    maxApproach = -vn;
                    impactPt = p;
                }
            }
        stats.rawContacts += cs.size();
        if (!cs.empty()) {
            if (maxApproach > 2.5f && b.age - b.lastImpact > 0.25f) {
                b.lastImpact = b.age;
                if (nearEffects) {
                    sounds.push_back({Snd::Thud, impactPt,
                                      std::clamp(maxApproach / 12.0f, 0.15f, 1.0f) * std::min(1.0f, 0.3f + b.mass / 4000.0f),
                                      0.7f + rng_.uniform() * 0.3f});
                    uint8_t ground = world_->get(cellOf(impactPt.x), cellOf(impactPt.y - 0.1f), cellOf(impactPt.z));
                    if (!ground || def(ground).fluid)
                        ground = world_->get(cellOf(impactPt.x), cellOf(impactPt.y - 0.6f), cellOf(impactPt.z));
                    if (ground && !def(ground).fluid) {
                        blockParticles(impactPt, ground, std::min(10, int(maxApproach)), 0.3f, 3.0f);
                        float heavy = std::min(1.0f, b.mass / 2500.0f + 0.25f);
                        dustPuff(impactPt, ground, int(std::min(18.0f, maxApproach * 1.6f) * heavy) + 2,
                                 std::min(3.5f, 0.6f + maxApproach * 0.3f));
                    }
                }
            }
            if (maxApproach > 5.0f && b.count > 12 && b.chips < 6 && b.age > 0.15f && maxApproach <= b.impactStrength) {
                chipList.push_back(bi);
                chipPoint.push_back(impactPt);
                chipSpeed.push_back(maxApproach);
            }
            if (maxApproach > 5.5f && b.age > 0.15f && b.age - b.lastTerrainBreak > 0.35f && b.terrainBreaks < 12) {
                int knocked = 0;
                for (Contact& c : cs) {
                    if (knocked >= 2 || b.terrainBreaks >= 12) break;
                    vec3 v = b.vel + crossv(b.angVel, c.r);
                    float vn = -dot(v, c.n);
                    if (vn < 5.5f) continue;
                    float energy = 0.5f * std::min(b.mass / float(cs.size()), 180.0f) * vn * vn;
                    int cx = cellOf(c.p.x), cy = cellOf(c.p.y), cz = cellOf(c.p.z);
                    uint8_t wb = world_->get(cx, cy, cz);
                    const BlockDef& d = def(wb);
                    if (!wb || d.hardness < 0 || d.fluid || wb == B_BEDROCK || cy < 2 || (d.hardness >= 1.5f && vn < 12.0f)) continue;
                    if (energy < 250.0f + d.hardness * 5000.0f) continue;
                    knocked_.push_back({{cx, cy, cz}, v * 0.45f + vec3(0, 1.5f, 0)});
                    ++knocked;
                    b.lastTerrainBreak = b.age;
                    ++b.terrainBreaks;
                    b.vel *= 0.80f;
                }
            }
            if (maxApproach > b.impactStrength && b.age > 0.2f && b.count >= 6) {
                toFracture.push_back(bi);
                fractureSpeed.push_back(maxApproach);
                fracturePoint.push_back(impactPt);
            }
            std::array<int, 24> selected;
            selected.fill(-1);
            std::array<float, 24> scores;
            scores.fill(-1e30f);
            for (size_t i = 0; i < cs.size(); ++i) {
                const auto& c = cs[i];
                int axis = std::abs(c.n.x) > .5f ? 0 : std::abs(c.n.y) > .5f ? 1 : 2;
                int face = axis * 2 + (c.n[axis] < 0 ? 1 : 0);
                int u = (axis + 1) % 3, v = (axis + 2) % 3;
                for (int corner = 0; corner < 4; ++corner) {
                    float score = c.r[u] * ((corner & 1) ? 1 : -1) + c.r[v] * ((corner & 2) ? 1 : -1) + c.depth * .25f;
                    int slot = face * 4 + corner;
                    if (score > scores[slot]) {
                        scores[slot] = score;
                        selected[slot] = int(i);
                    }
                }
            }
            std::vector<Contact> reduced;
            reduced.reserve(24);
            for (int index : selected)
                if (index >= 0 &&
                    std::none_of(reduced.begin(), reduced.end(), [&](const auto& c) { return c.sample == cs[size_t(index)].sample; }))
                    reduced.push_back(cs[size_t(index)]);
            cs.swap(reduced);
            stats.solverContacts += cs.size();
            for (auto& c : cs) {
                c.rn = crossv(c.r, c.n);
                c.an = invI * c.rn;
                c.kn = b.invMass + dot(c.rn, c.an);
                vec3 axis = std::abs(c.n.y) > .5f ? vec3(1, 0, 0) : vec3(0, 1, 0);
                c.t1 = normalize(cross(c.n, axis));
                c.t2 = cross(c.n, c.t1);
                c.rt1 = cross(c.r, c.t1);
                c.rt2 = cross(c.r, c.t2);
                c.at1 = invI * c.rt1;
                c.at2 = invI * c.rt2;
                c.kt1 = b.invMass + dot(c.rt1, c.at1);
                c.kt2 = b.invMass + dot(c.rt2, c.at2);
                float vn = dot(b.vel, c.n) + dot(b.angVel, c.rn);
                c.bounce = vn < -3 ? -vn * .12f : 0;
                if (maxApproach < 2.0f)
                    for (int k = 0; k < b.contactCount; ++k) {
                        const auto& old = b.contacts[k];
                        if (old.sample != c.sample || dot(old.normal, c.n) < .99f) continue;
                        c.jn = old.normalImpulse * .8f;
                        c.jt1 = old.friction1 * .8f;
                        c.jt2 = old.friction2 * .8f;
                        break;
                    }
                b.vel += (c.n * c.jn + c.t1 * c.jt1 + c.t2 * c.jt2) * b.invMass;
                b.angVel += c.an * c.jn + c.at1 * c.jt1 + c.at2 * c.jt2;
            }
            const int solverIterations = playerDist > 96.0f ? 3 : playerDist > 56.0f ? 4 : 6;
            for (int iteration = 0; iteration < solverIterations; ++iteration)
                for (auto& c : cs) {
                    float vn = dot(b.vel, c.n) + dot(b.angVel, c.rn), old = c.jn;
                    c.jn = std::max(0.0f, old + (c.bounce - vn) / c.kn);
                    float impulse = c.jn - old;
                    b.vel += c.n * (impulse * b.invMass);
                    b.angVel += c.an * impulse;
                    float a = c.jt1 - (dot(b.vel, c.t1) + dot(b.angVel, c.rt1)) / c.kt1;
                    float d = c.jt2 - (dot(b.vel, c.t2) + dot(b.angVel, c.rt2)) / c.kt2;
                    float limit = 1.1f * c.jn, magnitude = std::sqrt(a * a + d * d);
                    if (magnitude > limit && magnitude > 1e-6f) {
                        a *= limit / magnitude;
                        d *= limit / magnitude;
                    }
                    float da = a - c.jt1, dd = d - c.jt2;
                    c.jt1 = a;
                    c.jt2 = d;
                    b.vel += (c.t1 * da + c.t2 * dd) * b.invMass;
                    b.angVel += c.at1 * da + c.at2 * dd;
                }
            b.contactCount = int(cs.size());
            for (size_t i = 0; i < cs.size(); ++i) {
                const auto& c = cs[i];
                b.contacts[i] = {c.sample, c.n, c.jn, c.jt1, c.jt2};
            }
            vec3 corr;
            float maxDepth = 0;
            for (Contact& c : cs) {
                corr += c.n * std::max(c.depth - 0.01f, 0.0f);
                maxDepth = std::max(maxDepth, c.depth);
            }
            corr = corr / float(cs.size());
            float gx0 = 1e9f, gx1 = -1e9f, gz0 = 1e9f, gz1 = -1e9f;
            for (const Contact& c : cs)
                if (c.n.y > 0.5f) {
                    gx0 = std::min(gx0, c.p.x);
                    gx1 = std::max(gx1, c.p.x);
                    gz0 = std::min(gz0, c.p.z);
                    gz1 = std::max(gz1, c.p.z);
                }
            const float m = 0.04f;
            supported = gx0 <= gx1 && b.pos.x > gx0 + m && b.pos.x < gx1 - m && b.pos.z > gz0 + m && b.pos.z < gz1 - m;
            b.angVel *= std::exp(-bdt * (supported ? 2.5f : 0.4f));
            b.vel.x *= std::exp(-bdt * 1.5f);
            b.vel.z *= std::exp(-bdt * 1.5f);
            float cl = length(corr);
            if (cl > 1e-5f) b.pos += corr * (std::min(cl * 1.6f, maxDepth) / cl) * 0.5f;
        }
        if (cs.empty()) b.contactCount = 0;
        float angularSpeed = length(b.angVel);
        float maxAngular = std::clamp(6.0f / std::max(.5f, b.contactRadius), .45f, 4.0f);
        if (angularSpeed > maxAngular) b.angVel *= maxAngular / angularSpeed;
        b.pos += b.vel * bdt;
        b.rot = integrate(b.rot, b.angVel, bdt);
        bool slow = lengthSq(b.vel) < 0.16f && lengthSq(b.angVel) < 0.25f;
        bool still = lengthSq(b.vel) < 0.01f && lengthSq(b.angVel) < 0.004f;
        if (!cs.empty() && !supported && !still)
            b.rest = 0;
        else if (!cs.empty() && slow)
            b.rest += bdt;
        else if (!cs.empty() && lengthSq(b.vel) < 0.8f)
            b.rest += bdt * 0.4f;
        else
            b.rest = 0;
        if (!cs.empty() && (supported || still) && lengthSq(b.vel) < 0.6f && lengthSq(b.angVel) < 0.8f)
            b.lowEnergy += bdt;
        else
            b.lowEnergy = std::max(0.0f, b.lowEnergy - bdt * 2.0f);
        if (b.rest > 0.45f || b.lowEnergy > 2.5f || b.age > 25.0f) {
            settleBody(b);
            if (b.dead) continue;
            if ((supported || still) && slow && (b.meshPending || b.meshDirty)) {
                b.sleeping = true;
                b.vel = {};
                b.angVel = {};
            }
        }
        if (b.pos.y < -20) {
            b.dead = true;
            continue;
        }
    }
    for (auto& [c, v] : knocked_) {
        uint8_t wb = world_->get(c.x, c.y, c.z);
        if (!def(wb).solid || def(wb).hardness < 0) continue;
        vec3 cc((c.x + 0.5f) * VOX, (c.y + 0.5f) * VOX, (c.z + 0.5f) * VOX);
        setBlockBatch(c.x, c.y, c.z, B_AIR);
        Render rr = def(wb).render;
        if (rr == Render::Leaves || rr == Render::Glass) {
            blockParticles(cc, wb, 3, 0.25f, 2.5f);
            if (rr == Render::Glass) sounds.push_back({Snd::Glass, cc, 0.6f, 1.0f});
            continue;
        }
        Entity e;
        e.type = EntType::Debris;
        e.block = wb;
        e.w = e.h = VOX * 0.98f;
        e.pos = cc - vec3(0, VOX * 0.49f, 0);
        e.vel = v + vec3(rng_.range(-1, 1), rng_.range(0, 1.5f), rng_.range(-1, 1));
        addEntity(std::move(e));
        blockParticles(cc, wb, 2, 0.25f, 2.0f);
    }
    knocked_.clear();
    for (const ivec3& c : crushed_) {
        uint8_t wb = world_->get(c.x, c.y, c.z);
        if ((def(wb).render != Render::Cross && def(wb).render != Render::Leaves) || wb == B_FIRE || wb == B_TORCH) continue;
        setBlockBatch(c.x, c.y, c.z, B_AIR);
        vec3 at((c.x + 0.5f) * VOX, (c.y + 0.3f) * VOX, (c.z + 0.5f) * VOX);
        if (def(wb).render == Render::Leaves)
            rustle(at, wb, vec3(0, -0.3f, 0), 0.4f);
        else
            blockParticles(at, wb, 2, 0.2f, 1.5f);
    }
    crushed_.clear();
    struct BroadBody {
        size_t index;
        mat3 rotation;
        vec3 lo, hi;
    };
    std::vector<BroadBody> broad;
    broad.reserve(bodies_.size());
    for (size_t i = 0; i < bodies_.size(); ++i) {
        auto& b = bodies_[i];
        if (b.dead) continue;
        mat3 R = toMat3(b.rot);
        vec3 half = (b.contactMax - b.contactMin) * .5f;
        vec3 center = b.pos + R * ((b.contactMax + b.contactMin) * .5f), extent;
        for (int a = 0; a < 3; ++a)
            extent[a] = std::abs(R.m[a][0]) * half.x + std::abs(R.m[a][1]) * half.y + std::abs(R.m[a][2]) * half.z + VOX * .5f;
        b.aabbMin = center - extent;
        b.aabbMax = center + extent;
        broad.push_back({i, R, b.aabbMin, b.aabbMax});
    }
    std::sort(broad.begin(), broad.end(), [](const auto& a, const auto& b) { return a.lo.x < b.lo.x; });
    for (size_t i = 0; i < broad.size(); ++i)
        for (size_t j = i + 1; j < broad.size() && broad[j].lo.x <= broad[i].hi.x; ++j) {
            ++stats.broadphasePairs;
            const auto& ba = broad[i];
            const auto& bb = broad[j];
            if (ba.hi.y < bb.lo.y || bb.hi.y < ba.lo.y || ba.hi.z < bb.lo.z || bb.hi.z < ba.lo.z) continue;
            Body& A = bodies_[ba.index];
            Body& B = bodies_[bb.index];
            if ((A.sleeping && B.sleeping) || (!A.simStepped && !B.simStepped)) continue;
            ++stats.bodyPairs;
            mat3 RA = ba.rotation, RB = bb.rotation;
            mat3 IA = RA * A.invInertiaLocal * RA.transpose(), IB = RB * B.invInertiaLocal * RB.transpose();
            float pairDist = std::min(length(A.pos - player.pos), length(B.pos - player.pos));
            auto solvePair = [&](Body& P, Body& Q, const mat3& RP, const mat3& RQ, const mat3& IP, const mat3& IQ) {
                int n = 0;
                size_t stride = pairDist > 112.0f ? 3 : pairDist > 64.0f ? 2 : 1;
                int cap = pairDist > 112.0f ? 16 : pairDist > 64.0f ? 24 : 40;
                for (size_t si = 0; si < P.samples.size(); si += stride) {
                    const vec3& s = P.samples[si];
                    vec3 p = P.pos + RP * s;
                    vec3 nrm;
                    float depth;
                    if (!pointInBodyFrame(Q, RQ, p, &nrm, &depth)) continue;
                    P.sleeping = Q.sleeping = false;
                    P.simTier = Q.simTier = 0;
                    P.simCarry = Q.simCarry = 0;
                    if (++n > cap) break;
                    vec3 rp = p - P.pos, rq = p - Q.pos;
                    vec3 vrel = (P.vel + crossv(P.angVel, rp)) - (Q.vel + crossv(Q.angVel, rq));
                    float vn = dot(vrel, nrm);
                    if (vn < 0) {
                        float k =
                            P.invMass + Q.invMass + dot(nrm, crossv(IP * crossv(rp, nrm), rp)) + dot(nrm, crossv(IQ * crossv(rq, nrm), rq));
                        float jn = -1.05f * vn / k;
                        vec3 J = nrm * jn;
                        P.vel += J * P.invMass;
                        P.angVel += IP * crossv(rp, J);
                        Q.vel -= J * Q.invMass;
                        Q.angVel -= IQ * crossv(rq, J);
                        P.rest = Q.rest = 0;
                    }
                    float wsum = P.invMass + Q.invMass;
                    P.pos += nrm * (depth * 0.2f * P.invMass / wsum);
                    Q.pos -= nrm * (depth * 0.2f * Q.invMass / wsum);
                }
            };
            solvePair(A, B, RA, RB, IA, IB);
            solvePair(B, A, RB, RA, IB, IA);
        }
    for (size_t k = 0; k < chipList.size(); ++k)
        if (chipList[k] < bodies_.size() && !bodies_[chipList[k]].dead) chipBody(bodies_[chipList[k]], chipPoint[k], chipSpeed[k]);
    for (int k = int(toFracture.size()) - 1; k >= 0; --k) {
        size_t idx = toFracture[size_t(k)];
        if (idx < bodies_.size() && !bodies_[idx].dead) fractureBody(bodies_[idx], fracturePoint[size_t(k)], fractureSpeed[size_t(k)]);
    }
    for (Body& b : bodies_)
        if (b.dead && b.meshId) {
            if (renderer_) renderer_->freeMesh(b.meshId);
            b.meshId = 0;
        }
    bodies_.erase_if([](const Body& b) { return b.dead; });
}

ivec3 Sim::snapOrigin(const Body& b, vec3 T) const {
    vec3 f = T / VOX;
    if (b.dvalid && std::fabs(f.x - b.dTs.x) < 0.75f && std::fabs(f.y - b.dTs.y) < 0.75f && std::fabs(f.z - b.dTs.z) < 0.75f) return b.dTs;
    return {int(std::lround(f.x)), int(std::lround(f.y)), int(std::lround(f.z))};
}

void Sim::voxelizeBody(Body& b, const mat3& R, const Region* snapshot) const {
    vec3 T = b.pos - R * b.com;
    ivec3 Ts = snapOrigin(b, T);
    vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
    for (int c = 0; c < 8; ++c) {
        vec3 corner((c & 1) ? b.sx * VOX : 0.0f, (c & 2) ? b.sy * VOX : 0.0f, (c & 4) ? b.sz * VOX : 0.0f);
        vec3 w = R * corner;
        lo = vec3(std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z));
        hi = vec3(std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z));
    }
    int D[3], E[3];
    const int tsv[3] = {Ts.x, Ts.y, Ts.z};
    for (int a = 0; a < 3; ++a) {
        D[a] = tsv[a] + int(std::floor(lo[a] / VOX + 1e-3f));
        E[a] = tsv[a] + int(std::ceil(hi[a] / VOX - 1e-3f));
    }
    const int sx = std::max(1, E[0] - D[0]), sy = std::max(1, E[1] - D[1]), sz = std::max(1, E[2] - D[2]);
    b.dsx = sx;
    b.dsy = sy;
    b.dsz = sz;
    b.dorig = {D[0], D[1], D[2]};
    b.dTs = Ts;
    b.dvox.assign(size_t(sx) * sy * sz, 0);
    b.dmeta.assign(b.dvox.size(), 0);
    int snap = 0;
    {
        float best = -1e9f;
        for (int i = 0; i < 24; ++i) {
            const Orient& O = orient(i);
            float score = 0;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) score += float(O.m[r][c]) * R.m[r][c];
            if (score > best) {
                best = score;
                snap = i;
            }
        }
    }
    mat3 Rt = R.transpose();
    vec3 dx = Rt * vec3(VOX, 0, 0), dy = Rt * vec3(0, VOX, 0), dz = Rt * vec3(0, 0, VOX);
    vec3 base = Rt * vec3((D[0] - Ts.x + 0.5f) * VOX, (D[1] - Ts.y + 0.5f) * VOX, (D[2] - Ts.z + 0.5f) * VOX);
    const float inv = 1.0f / VOX;
    for (int y = 0; y < sy; ++y)
        for (int z = 0; z < sz; ++z) {
            vec3 L = base + dy * float(y) + dz * float(z);
            for (int x = 0; x < sx; ++x, L += dx) {
                int ix = int(std::floor(L.x * inv)), iy = int(std::floor(L.y * inv)), iz = int(std::floor(L.z * inv));
                uint8_t v = b.at(ix, iy, iz);
                if (!v) continue;
                size_t o = size_t((y * sz + z) * sx + x);
                b.dvox[o] = v;
                if (orientable(v)) {
                    uint8_t vm = b.metaAt(ix, iy, iz);
                    int oe = orientCompose(snap, metaOrient(vm));
                    b.dmeta[o] = makeOrientMeta(oe, vm >> 5, D[0] + x, D[1] + y, D[2] + z);
                } else
                    b.dmeta[o] = b.metaAt(ix, iy, iz);
            }
        }
    lightBody(b, snapshot);
}

void Sim::lightBody(Body& b, const Region* snapshot) const {
    const int RX = b.dsx + 2, RY = b.dsy + 2, RZ = b.dsz + 2, stride = RX * RZ;
    const size_t n = size_t(stride) * RY;
    static thread_local std::vector<uint8_t> cost;
    static thread_local std::vector<int> top, q;
    cost.assign(n, 1);
    b.dsky.assign(n, 0);
    top.assign(size_t(RX) * RZ, 0);
    auto I = [&](int x, int y, int z) { return (y * RZ + z) * RX + x; };
    for (int y = 0; y < b.dsy; ++y)
        for (int z = 0; z < b.dsz; ++z)
            for (int x = 0; x < b.dsx; ++x)
                cost[I(x + 1, y + 1, z + 1)] = uint8_t(std::min(lightCost(b.dvox[(y * b.dsz + z) * b.dsx + x]), 99));
    for (int z = 0; z < RZ; ++z)
        for (int x = 0; x < RX; ++x) {
            int y = RY - 1, i = I(x, y, z), level = 15;
            while (y >= 0 && cost[i] == 1) {
                b.dsky[i] = 15;
                --y;
                i -= stride;
            }
            top[z * RX + x] = y + 1;
            while (y >= 0) {
                level -= cost[i];
                if (level <= 0) break;
                b.dsky[i] = uint8_t(level);
                --y;
                i -= stride;
            }
        }
    q.clear();
    for (int z = 0; z < RZ; ++z)
        for (int x = 0; x < RX; ++x) {
            int t = top[z * RX + x], hi = t;
            if (x) hi = std::max(hi, top[z * RX + x - 1]);
            if (x + 1 < RX) hi = std::max(hi, top[z * RX + x + 1]);
            if (z) hi = std::max(hi, top[(z - 1) * RX + x]);
            if (z + 1 < RZ) hi = std::max(hi, top[(z + 1) * RX + x]);
            for (int y = std::max(0, t - 16); y <= std::min(hi, RY - 1); ++y) {
                int i = I(x, y, z);
                if (b.dsky[i] > 1) q.push_back(i);
            }
        }
    for (size_t h = 0; h < q.size(); ++h) {
        int i = q[h], level = b.dsky[i];
        if (level <= 1) continue;
        int y = i / stride, z = (i % stride) / RX, x = i % RX;
        auto visit = [&](int j, bool down) {
            int c = cost[j], next = down && level == 15 && c == 1 ? 15 : level - c;
            if (next > b.dsky[j]) {
                b.dsky[j] = uint8_t(next);
                q.push_back(j);
            }
        };
        if (x) visit(i - 1, false);
        if (x + 1 < RX) visit(i + 1, false);
        if (z) visit(i - RX, false);
        if (z + 1 < RZ) visit(i + RX, false);
        if (y) visit(i - stride, true);
        if (y + 1 < RY) visit(i + stride, false);
    }
    Region local;
    if (!snapshot) {
        world_->copyRegion(local, b.dorig.x - 1, b.dorig.y - 1, b.dorig.z - 1, RX, RY, RZ, true, false);
        snapshot = &local;
    }
    b.dlight.resize(n);
    if (snapshot->light.empty()) {
        for (size_t i = 0; i < n; ++i) b.dlight[i] = cost[i] >= 99 ? 0 : uint8_t(b.dsky[i] << 4);
        return;
    }
    for (int y = 0; y < RY; ++y)
        for (int z = 0; z < RZ; ++z)
            for (int x = 0; x < RX; ++x) {
                int i = I(x, y, z);
                uint8_t light = snapshot->light[snapshot->index(b.dorig.x - 1 + x, b.dorig.y - 1 + y, b.dorig.z - 1 + z)];
                b.dlight[i] = cost[i] >= 99 ? 0 : uint8_t((std::min<int>(light >> 4, b.dsky[i]) << 4) | (light & 15));
            }
}

// drops a body back into the world as blocks once its stopped moving
void Sim::settleBody(Body& b, bool force) {
    if (b.dead) return;
    if (!force && b.meshId && b.age < 25.0f) {
        float same = std::fabs(b.drot.x * b.rot.x + b.drot.y * b.rot.y + b.drot.z * b.rot.z + b.drot.w * b.rot.w);
        if (b.meshPending || same < std::cos(.045f * .5f)) {
            b.meshDirty = true;
            return;
        }
    }
    if (!b.dvalid) {
        voxelizeBody(b, toMat3(b.rot));
        b.dvalid = true;
        b.drot = b.rot;
    }
    moveBodyDisplay(b);
    const ivec3 D = b.dorig;
    const int sx = b.dsx, sy = b.dsy, sz = b.dsz;
    bool forcedSeeds = false;
    {
        auto own = [&](int x, int y, int z) {
            return x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz && def(b.dvox[size_t((y * sz + z) * sx + x)]).collides;
        };
        bool onSomething = false;
        for (int y = 0; y < sy && !onSomething; ++y)
            for (int z = 0; z < sz && !onSomething; ++z)
                for (int x = 0; x < sx && !onSomething; ++x) {
                    if (!own(x, y, z) || !def(b.dvox[size_t((y * sz + z) * sx + x)]).collides) continue;
                    for (int k = 0; k < 27 && !onSomething; ++k) {
                        int dx = k % 3 - 1, dy = (k / 3) % 3 - 1, dz = k / 9 - 1;
                        if (dy > 0 || own(x + dx, y + dy, z + dz)) continue;
                        const BlockDef& d = def(world_->get(D.x + x + dx, D.y + y + dy, D.z + z + dz));
                        if (d.collides || d.fluid) onSomething = true;
                    }
                }
        if (!onSomething) {
            if (b.age < 25.0f && !force) {
                b.rest = 0;
                b.lowEnergy = 0;
                return;
            }
            forcedSeeds = true;
        }
    }
    std::vector<ivec3> placed;
    stats.settled++;
    std::unordered_set<int64_t> secs;
    for (int y = 0; y < sy; ++y)
        for (int z = 0; z < sz; ++z)
            for (int x = 0; x < sx; ++x) {
                size_t o = size_t((y * sz + z) * sx + x);
                uint8_t v = b.dvox[o];
                if (!v) continue;
                int cx = D.x + x, cy = D.y + y, cz = D.z + z;
                if (cy < 1 || cy >= WH || !world_->loaded(cx, cz)) continue;
                uint8_t meta = b.dmeta[o];
                uint8_t cur = world_->get(cx, cy, cz);
                if (def(v).render == Render::Leaves && def(cur).fluid) continue;
                if (!def(cur).replaceable) {
                    if (!def(world_->get(cx, cy + 1, cz)).replaceable) continue;
                    ++cy;
                    if (cy >= WH) continue;
                    if (def(v).render == Render::Leaves && def(world_->get(cx, cy, cz)).fluid) continue;
                    if (orientable(v)) meta = makeOrientMeta(metaOrient(meta), metaPhase(meta, cx, cy - 1, cz), cx, cy, cz);
                }
                displaceFluid(cx, cy, cz);
                setBlockBatch(cx, cy, cz, v, meta);
                placed.push_back({cx, cy, cz});
                for (int dy = -1; dy <= 1; ++dy)
                    secs.insert(sectionKey(floorDiv(cx, CS), floorDiv(cz, CS), std::clamp((cy + dy) >> 5, 0, NSEC - 1)));
            }
    resolveFluidDisplacements();
    b.dead = true;
    if (b.meshId) {
        Ghost g;
        g.meshId = b.meshId;
        g.pos = vec3(D.x * VOX, D.y * VOX, D.z * VOX);
        g.uvOrigin = vec3(float(b.meshOrigin.x), float(b.meshOrigin.y), float(b.meshOrigin.z));
        for (int64_t k : secs) g.secs.push_back({k, renderer_->sectionVersion(k)});
        ghosts_.push_back(std::move(g));
        b.meshId = 0;
    }
    if (forcedSeeds)
        for (const ivec3& c : placed) collapseSeeds_.push_back(c);
}

void Sim::chipBody(Body& b, vec3 impact, float speed) {
    mat3 R = toMat3(b.rot);
    vec3 L = (R.transpose() * (impact - b.pos) + b.com) / VOX;
    struct Cand {
        ivec3 c;
        float d;
    };
    std::vector<Cand> cand;
    int x0 = std::max(0, int(L.x) - 3), x1 = std::min(b.sx - 1, int(L.x) + 3);
    int y0 = std::max(0, int(L.y) - 3), y1 = std::min(b.sy - 1, int(L.y) + 3);
    int z0 = std::max(0, int(L.z) - 3), z1 = std::min(b.sz - 1, int(L.z) + 3);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                uint8_t v = b.at(x, y, z);
                if (!v) continue;
                int open = !b.at(x + 1, y, z) + !b.at(x - 1, y, z) + !b.at(x, y + 1, z) + !b.at(x, y - 1, z) + !b.at(x, y, z + 1) +
                           !b.at(x, y, z - 1);
                if (open < 3) continue;
                float d = lengthSq(vec3(x + 0.5f, y + 0.5f, z + 0.5f) - L);
                if (d < 12.0f) cand.push_back({{x, y, z}, d + rng_.uniform()});
            }
    if (cand.empty()) return;
    std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& c) { return a.d < c.d; });
    int n = std::min<int>(int(cand.size()), speed > 8.0f ? 2 : 1);
    vec3 T = b.pos - R * b.com;
    for (int i = 0; i < n; ++i) {
        ivec3 c = cand[size_t(i)].c;
        uint8_t& v = b.vox[size_t((c.y * b.sz + c.z) * b.sx + c.x)];
        vec3 w = T + R * vec3((c.x + 0.5f) * VOX, (c.y + 0.5f) * VOX, (c.z + 0.5f) * VOX);
        if (def(v).render == Render::Leaves || def(v).render == Render::Glass) {
            blockParticles(w, v, 3, 0.25f, 2.5f);
        } else {
            Entity e;
            e.type = EntType::Debris;
            e.block = v;
            e.w = e.h = VOX * 0.98f;
            e.pos = w - vec3(0, VOX * 0.49f, 0);
            vec3 out = normalize(w - b.pos, {0, 1, 0});
            e.vel = b.vel + crossv(b.angVel, w - b.pos) + out * rng_.range(1.0f, 2.5f) + vec3(0, rng_.range(1.5f, 3.0f), 0);
            addEntity(std::move(e));
            blockParticles(w, v, 3, 0.2f, 2.0f);
        }
        v = 0;
        b.chips++;
    }
    buildBody(b);
    if (b.dead) {
        if (b.meshId) {
            if (renderer_) renderer_->freeMesh(b.meshId);
            b.meshId = 0;
        }
        return;
    }
    b.pos = T + R * b.com;
}

void Sim::fractureBody(Body& b, vec3 impact, float speed) {
    mat3 R = toMat3(b.rot);
    bool glassy = false;
    const vec3 hitL = (R.transpose() * (impact - b.pos) + b.com) / VOX;
    for (int y = 0; y < b.sy; ++y)
        for (int z = 0; z < b.sz; ++z)
            for (int x = 0; x < b.sx; ++x) {
                uint8_t& v = b.vox[size_t((y * b.sz + z) * b.sx + x)];
                if (!v) continue;
                Render r = def(v).render;
                if (r == Render::Leaves || r == Render::Glass) {
                    if (r == Render::Leaves && lengthSq(vec3(x + 0.5f, y + 0.5f, z + 0.5f) - hitL) > 20.0f && rng_.uniform() > 0.12f)
                        continue;
                    if (r == Render::Glass) glassy = true;
                    vec3 w = b.pos + R * (vec3((x + 0.5f) * VOX, (y + 0.5f) * VOX, (z + 0.5f) * VOX) - b.com);
                    if (rng_.uniform() < 0.3f) blockParticles(w, v, 2, 0.25f, 3.0f);
                    if (v == B_LEAVES && rng_.uniform() < 0.01f) spawnItem(w, I_APPLE, 1);
                    v = 0;
                }
            }
    sounds.push_back({glassy ? Snd::Glass : Snd::Crack, impact, 1.0f, 0.7f + rng_.uniform() * 0.4f});
    std::vector<ivec3> cells;
    for (int y = 0; y < b.sy; ++y)
        for (int z = 0; z < b.sz; ++z)
            for (int x = 0; x < b.sx; ++x)
                if (b.at(x, y, z)) cells.push_back({x, y, z});
    b.dead = true;
    if (b.meshId) {
        if (renderer_) renderer_->freeMesh(b.meshId);
        b.meshId = 0;
    }
    if (cells.empty()) return;
    auto worldOf = [&](float x, float y, float z) { return b.pos + R * (vec3(x * VOX, y * VOX, z * VOX) - b.com); };
    int k = std::clamp(int(cells.size()) / 45 + 2, 2, 6);
    std::vector<vec3> seeds;
    vec3 impactLocal = (R.transpose() * (impact - b.pos) + b.com) / VOX;
    seeds.push_back(impactLocal);
    while (int(seeds.size()) < k) {
        const ivec3& c = cells[rng_.next() % cells.size()];
        seeds.push_back(vec3(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f));
    }
    std::vector<std::vector<ivec3>> pieces(seeds.size());
    for (const ivec3& c : cells) {
        vec3 p(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f);
        size_t best = 0;
        float bd = 1e30f;
        for (size_t s = 0; s < seeds.size(); ++s) {
            float d = lengthSq(p - seeds[s]) * (s == 0 ? 2.5f : 1.0f);
            if (d < bd) {
                bd = d;
                best = s;
            }
        }
        pieces[best].push_back(c);
    }
    std::vector<Body> fresh;
    for (auto& piece : pieces) {
        if (piece.empty()) continue;
        if (piece.size() <= 3) {
            for (const ivec3& c : piece) {
                Entity e;
                e.type = EntType::Debris;
                e.block = b.at(c.x, c.y, c.z);
                e.w = e.h = VOX * 0.98f;
                vec3 w = worldOf(c.x + 0.5f, c.y + 0.5f, c.z + 0.5f);
                e.pos = w - vec3(0, VOX * 0.49f, 0);
                e.vel = b.vel + crossv(b.angVel, w - b.pos) + vec3(rng_.range(-2, 2), rng_.range(1, 4), rng_.range(-2, 2));
                e.spin = vec3(rng_.range(-8, 8), rng_.range(-8, 8), rng_.range(-8, 8));
                addEntity(std::move(e));
            }
            continue;
        }
        ivec3 mn = piece[0], mx = piece[0];
        for (auto& c : piece) {
            mn = {std::min(mn.x, c.x), std::min(mn.y, c.y), std::min(mn.z, c.z)};
            mx = {std::max(mx.x, c.x), std::max(mx.y, c.y), std::max(mx.z, c.z)};
        }
        Body nb;
        nb.id = nextBodyId_++;
        nb.sx = mx.x - mn.x + 1;
        nb.sy = mx.y - mn.y + 1;
        nb.sz = mx.z - mn.z + 1;
        nb.vox.assign(size_t(nb.sx) * nb.sy * nb.sz, 0);
        nb.vmeta.assign(nb.vox.size(), 0);
        for (auto& c : piece) {
            size_t idx = size_t(((c.y - mn.y) * nb.sz + (c.z - mn.z)) * nb.sx + (c.x - mn.x));
            nb.vox[idx] = b.at(c.x, c.y, c.z);
            nb.vmeta[idx] = b.metaAt(c.x, c.y, c.z);
        }
        buildBody(nb);
        if (nb.dead) continue;
        vec3 localCom = vec3(mn.x * VOX, mn.y * VOX, mn.z * VOX) + nb.com;
        nb.pos = b.pos + R * (localCom - b.com);
        nb.rot = b.rot;
        vec3 out = normalize(nb.pos - impact, {0, 1, 0});
        nb.vel = b.vel * 0.6f + crossv(b.angVel, nb.pos - b.pos) + out * (speed * 0.12f) + vec3(0, speed * 0.05f, 0);
        nb.angVel = b.angVel * 0.7f + vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)) * (speed * 0.08f);
        nb.age = 0.1f;
        nb.lastImpact = nb.age;
        nb.climT = b.climT;
        nb.climH = b.climH;
        fresh.push_back(std::move(nb));
    }
    for (Body& f : fresh) bodies_.push_back(std::move(f));
}

bool Sim::supportOnBodies(AABB& box, vec3& vel, bool& onGround, vec3* platformVel, float dt) {
    (void)dt;
    bool supported = false;
    for (Body& b : bodies_) {
        if (b.dead) continue;
        float r = b.contactRadius + 1.5f;
        vec3 c = box.center();
        if (lengthSq(c - b.pos) > r * r) continue;
        float w = box.max.x - box.min.x, h = box.max.y - box.min.y;
        float raise = 0;
        vec3 contactPt;
        bool any = false;
        for (int i = 0; i < 5; ++i) {
            vec3 p = i == 4 ? vec3(c.x, box.min.y + 0.02f, c.z)
                            : vec3(i & 1 ? box.max.x - 0.04f : box.min.x + 0.04f, box.min.y + 0.02f,
                                   i & 2 ? box.max.z - 0.04f : box.min.z + 0.04f);
            if (!pointInBody(b, p, nullptr, nullptr)) continue;
            float up = 0;
            while (up < 0.6f && pointInBody(b, p + vec3(0, up, 0), nullptr, nullptr)) up += 0.05f;
            if (up < 0.6f) {
                raise = std::max(raise, up);
                contactPt = p;
                any = true;
            }
        }
        if (any && vel.y <= 1.0f) {
            box = box.offset({0, raise, 0});
            if (vel.y < 0) vel.y = 0;
            onGround = true;
            supported = true;
            if (platformVel) *platformVel = b.vel + crossv(b.angVel, contactPt - b.pos);
        }
        for (int i = 0; i < 4; ++i) {
            vec3 p(i == 0 ? box.min.x : i == 1 ? box.max.x : c.x, box.min.y + h * 0.5f, i == 2 ? box.min.z : i == 3 ? box.max.z : c.z);
            vec3 n;
            float d;
            if (pointInBody(b, p, &n, &d)) {
                n.y = 0;
                if (lengthSq(n) < 0.01f) continue;
                n = normalize(n);
                box = box.offset(n * std::min(d, w * 0.5f));
            }
        }
    }
    return supported;
}

void Sim::pushBodies(vec3 center, float radius, float power) {
    for (Body& b : bodies_) {
        float ext = b.contactRadius;
        vec3 d = b.pos - center;
        float dist = length(d);
        float reach = radius + ext;
        if (dist > reach) continue;
        float f = 1 - dist / reach;
        float massScale = std::min(1.0f, std::sqrt(1500.0f / b.mass));
        b.vel += normalize(d + vec3(0, 0.5f, 0), {0, 1, 0}) * (power * 6.0f * f * massScale);
        b.angVel += vec3(rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)) * (power * 1.2f * f * massScale);
        b.rest = 0;
        b.sleeping = false;
        b.contactCount = 0;
        b.simTier = 0;
        b.simCarry = 0;
    }
}

namespace {
void compositeBodyLighting(Region& world, Body& b) {
    for (int y = 0; y < b.dsy; ++y)
        for (int z = 0; z < b.dsz; ++z)
            for (int x = 0; x < b.dsx; ++x) {
                size_t o = size_t((y * b.dsz + z) * b.dsx + x);
                uint8_t v = b.dvox[o];
                if (!v) continue;
                int wx = b.dorig.x + x, wy = b.dorig.y + y, wz = b.dorig.z + z;
                if (wx < world.x0 || wx >= world.x0 + world.sx || wy < world.y0 || wy >= world.y0 + world.sy || wz < world.z0 ||
                    wz >= world.z0 + world.sz)
                    continue;
                world.blocks[size_t(world.index(wx, wy, wz))] = v;
            }
    computeRegionLight(world, true);

    const int RX = b.dsx + 2, RY = b.dsy + 2, RZ = b.dsz + 2;
    const size_t n = size_t(RX) * RY * RZ;
    b.dlight.assign(n, 0);
    b.dsky.assign(n, 0);
    for (int y = 0; y < RY; ++y)
        for (int z = 0; z < RZ; ++z)
            for (int x = 0; x < RX; ++x) {
                int wx = b.dorig.x - 1 + x, wy = b.dorig.y - 1 + y, wz = b.dorig.z - 1 + z;
                size_t dst = size_t((y * RZ + z) * RX + x);
                if (wx < world.x0 || wx >= world.x0 + world.sx || wy < world.y0 || wy >= world.y0 + world.sy || wz < world.z0 ||
                    wz >= world.z0 + world.sz)
                    continue;
                uint8_t L = world.light[size_t(world.index(wx, wy, wz))];
                b.dlight[dst] = L;
                b.dsky[dst] = L >> 4;
            }
}

MeshOut displayMesh(const Body& b, const Region* composite = nullptr) {
    Region reg;
    reg.sx = b.dsx + 2;
    reg.sy = b.dsy + 2;
    reg.sz = b.dsz + 2;
    reg.x0 = b.dorig.x - 1;
    reg.y0 = b.dorig.y - 1;
    reg.z0 = b.dorig.z - 1;
    const size_t rn = size_t(reg.sx) * reg.sy * reg.sz;
    reg.blocks.assign(rn, B_AIR);
    reg.meta.assign(rn, 0);
    reg.light.assign(rn, 0xF0);
    if (composite) {
        reg.radiance.assign(rn, 0);
        for (int y = 0; y < reg.sy; ++y)
            for (int z = 0; z < reg.sz; ++z) {
                int wy = reg.y0 + y, wz = reg.z0 + z;
                if (wy < composite->y0 || wy >= composite->y0 + composite->sy || wz < composite->z0 || wz >= composite->z0 + composite->sz)
                    continue;
                int x0 = std::max(reg.x0, composite->x0), x1 = std::min(reg.x0 + reg.sx, composite->x0 + composite->sx);
                if (x0 >= x1) continue;
                size_t dst = size_t(reg.index(x0, wy, wz)), src = size_t(composite->index(x0, wy, wz)), count = size_t(x1 - x0);
                std::memcpy(reg.blocks.data() + dst, composite->blocks.data() + src, count);
                std::memcpy(reg.light.data() + dst, composite->light.data() + src, count);
                if (!composite->radiance.empty())
                    std::memcpy(reg.radiance.data() + dst, composite->radiance.data() + src, count * sizeof(uint16_t));
            }
    } else if (b.dlight.size() == rn)
        reg.light = b.dlight;

    std::vector<uint8_t> owned(size_t(b.dsx) * b.dsy * b.dsz, 0);
    for (int y = 0; y < b.dsy; ++y)
        for (int z = 0; z < b.dsz; ++z) {
            size_t src = size_t((y * b.dsz + z) * b.dsx), dst = size_t(((y + 1) * reg.sz + z + 1) * reg.sx + 1);
            for (int x = 0; x < b.dsx; ++x) {
                uint8_t v = b.dvox[src + x];
                if (!v) continue;
                reg.blocks[dst + x] = v;
                reg.meta[dst + x] = b.dmeta[src + x];
                owned[src + x] = 1;
            }
        }
    reg.climT.assign(size_t(reg.sx) * reg.sz, b.climT);
    reg.climH.assign(size_t(reg.sx) * reg.sz, b.climH);
    MeshOut mesh;
    meshRegionOwned(reg, b.dsx, b.dsy, b.dsz, owned.data(), mesh, true);
    if (composite) {
        const int pad = LIGHT_PAD;
        const int x0 = std::max(composite->x0, b.dorig.x - pad), x1 = std::min(composite->x0 + composite->sx, b.dorig.x + b.dsx + pad);
        const int z0 = std::max(composite->z0, b.dorig.z - pad), z1 = std::min(composite->z0 + composite->sz, b.dorig.z + b.dsz + pad);
        const int y0 = std::max(composite->y0, b.dorig.y - pad), y1 = std::min(composite->y0 + composite->sy, b.dorig.y + b.dsy + pad);
        if (x1 > x0 && y1 > y0 && z1 > z0) {
            mesh.bodyLightOrigin = {x0, y0, z0};
            mesh.bodyLightSize = {x1 - x0, y1 - y0, z1 - z0};
            mesh.bodyLight.resize(size_t(x1 - x0) * (y1 - y0) * (z1 - z0) * 4);
            for (int z = z0; z < z1; ++z)
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        size_t src = size_t(composite->index(x, y, z));
                        size_t dst = size_t(((z - z0) * (y1 - y0) + (y - y0)) * (x1 - x0) + (x - x0)) * 4;
                        uint8_t block = composite->blocks[src], L = composite->light[src];
                        bool open = def(block).light != LightKind::Opaque;
                        mesh.bodyLight[dst + 0] = open ? uint8_t((L >> 4) * 17) : 0;
                        mesh.bodyLight[dst + 1] = open ? uint8_t((L & 15) * 17) : 0;
                        mesh.bodyLight[dst + 2] = lightCost(block) >= 99 ? 255 : block == B_WATER ? 64 : 0;
                        mesh.bodyLight[dst + 3] = open ? 255 : 0;
                    }
        }
    }
    return mesh;
}
}

void Sim::moveBodyDisplay(Body& b) const {
    if (!b.dvalid) return;
    ivec3 next = snapOrigin(b, b.pos - toMat3(b.drot) * b.com);
    ivec3 delta{next.x - b.dTs.x, next.y - b.dTs.y, next.z - b.dTs.z};
    if (delta.x == 0 && delta.y == 0 && delta.z == 0) return;
    b.displayLightDirty = true;
    b.dorig = {b.dorig.x + delta.x, b.dorig.y + delta.y, b.dorig.z + delta.z};
    b.dTs = next;
    int parity = (delta.x & 1) | ((delta.y & 1) << 1) | ((delta.z & 1) << 2);
    if (parity)
        for (size_t i = 0; i < b.dmeta.size(); ++i)
            if (orientable(b.dvox[i])) b.dmeta[i] ^= uint8_t(parity << 5);
}

Region Sim::bodyLightSnapshot(const Body& b, const mat3& R) const {
    ivec3 T = snapOrigin(b, b.pos - R * b.com);
    vec3 lo(1e9f), hi(-1e9f);
    for (int k = 0; k < 8; ++k) {
        vec3 v = R * vec3((k & 1) ? b.sx * VOX : 0, (k & 2) ? b.sy * VOX : 0, (k & 4) ? b.sz * VOX : 0);
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], v[a]);
            hi[a] = std::max(hi[a], v[a]);
        }
    }
    int t[] = {T.x, T.y, T.z}, origin[3], size[3];
    for (int a = 0; a < 3; ++a) {
        int low = int(std::floor(lo[a] / VOX + 1e-3f)), high = int(std::ceil(hi[a] / VOX - 1e-3f));
        origin[a] = t[a] + low;
        size[a] = std::max(1, high - low);
    }
    constexpr int pad = LIGHT_PAD + 2;
    Region snapshot;
    world_->copyRegion(snapshot, origin[0] - pad, 0, origin[2] - pad, size[0] + pad * 2, WH, size[2] + pad * 2, false, false);
    return snapshot;
}

void Sim::prepareBodies(vec3 cam) {
    if (!renderer_) return;
    auto turnStep = [&](const Body& b) {
        float d = length(b.pos - cam);
        return d > 88.0f ? .18f : d > 56.0f ? .10f : .045f;
    };
    std::vector<Body*> ready;
    ready.reserve(bodies_.size());
    for (Body& b : bodies_) {
        if (b.dead) continue;
        moveBodyDisplay(b);
        float dq = std::fabs(b.drot.x * b.rot.x + b.drot.y * b.rot.y + b.drot.z * b.rot.z + b.drot.w * b.rot.w);
        bool turned = !b.dvalid || dq < std::cos(turnStep(b) * .5f);
        bool relightMove = b.displayLightDirty && lengthSq(b.pos - cam) < 72.0f * 72.0f;
        bool need = !b.dvalid || b.meshDirty || turned || relightMove;
        if (need && !b.meshPending) ready.push_back(&b);
    }
    std::stable_sort(ready.begin(), ready.end(), [&](const Body* a, const Body* b) {
        if (bool(a->meshId) != bool(b->meshId)) return !a->meshId;
        if (a->meshDirty != b->meshDirty) return a->meshDirty;
        float da = lengthSq(a->pos - cam), db = lengthSq(b->pos - cam);
        if (std::fabs(da - db) > 64.0f) return da < db;
        return a->age - a->meshAge > b->age - b->meshAge;
    });
    int budget = 260000;
    for (Body* body : ready) {
        Body& b = *body;
        if (budget <= 0 && b.meshId) continue;
        if (bodyPool_ && bodyJobs_ >= std::clamp(int(bodyPool_->workers() / 2), 2, 4)) continue;
        float dq = std::fabs(b.drot.x * b.rot.x + b.drot.y * b.rot.y + b.drot.z * b.rot.z + b.drot.w * b.rot.w);
        bool turned = !b.dvalid || dq < std::cos(turnStep(b) * .5f);
        mat3 R = toMat3(turned ? b.rot : b.drot);
        if (!bodyPool_) {
            Region exact = bodyLightSnapshot(b, R);
            voxelizeBody(b, R, &exact);
            b.drot = turned ? b.rot : b.drot;
            b.dvalid = true;
            compositeBodyLighting(exact, b);
            MeshOut mesh = displayMesh(b, &exact);
            if (b.meshId) renderer_->freeMesh(b.meshId);
            b.meshId = renderer_->uploadMesh(mesh);
            b.meshOrigin = b.dorig;
            b.meshDirty = false;
            b.displayLightDirty = false;
            b.meshAge = b.age;
        } else {
            struct Job {
                Body body;
                Region world;
                MeshOut mesh;
            };
            auto job = std::make_shared<Job>();
            Body& v = job->body;
            v.sx = b.sx;
            v.sy = b.sy;
            v.sz = b.sz;
            v.vox = b.vox;
            v.vmeta = b.vmeta;
            v.com = b.com;
            v.pos = b.pos;
            v.dTs = b.dTs;
            v.dvalid = b.dvalid;
            v.drot = turned ? b.rot : b.drot;
            v.climT = b.climT;
            v.climH = b.climH;
            v.age = b.age;
            if (!turned && !b.meshDirty) {
                v.dvox = b.dvox;
                v.dmeta = b.dmeta;
                v.dsx = b.dsx;
                v.dsy = b.dsy;
                v.dsz = b.dsz;
                v.dorig = b.dorig;
            }
            job->world = bodyLightSnapshot(b, R);
            const auto handle = bodies_.handleOf(b);
            const auto version = b.meshVersion;
            const auto epoch = bodyEpoch_;
            TaskPool* pool = bodyPool_;
            b.meshPending = true;
            ++bodyJobs_;
            pool->submit(-300.0f, [this, pool, job, R, handle, version, epoch] {
                if (job->body.dvox.empty()) voxelizeBody(job->body, R, &job->world);
                compositeBodyLighting(job->world, job->body);
                job->mesh = displayMesh(job->body, &job->world);
                pool->post(
                    [this, job, handle, version, epoch] {
                        if (epoch != bodyEpoch_) return;
                        --bodyJobs_;
                        if (!ecs_.is_alive(handle)) return;
                        if (auto* live = ecs_.entity(handle).try_get_mut<Body>()) {
                            Body& b = *live;
                            b.meshPending = false;
                            if (b.dead || b.meshVersion != version) return;
                            Body& v = job->body;
                            b.dvox = std::move(v.dvox);
                            b.dmeta = std::move(v.dmeta);
                            b.dlight = std::move(v.dlight);
                            b.dsky = std::move(v.dsky);
                            b.dsx = v.dsx;
                            b.dsy = v.dsy;
                            b.dsz = v.dsz;
                            b.dorig = v.dorig;
                            b.dTs = v.dTs;
                            b.drot = v.drot;
                            b.dvalid = true;
                            if (b.meshId) renderer_->freeMesh(b.meshId);
                            b.meshId = renderer_->uploadMesh(job->mesh);
                            b.meshOrigin = b.dorig;
                            b.meshDirty = false;
                            b.displayLightDirty = false;
                            b.meshAge = v.age;
                            moveBodyDisplay(b);
                            return;
                        }
                    },
                    true);
            });
        }
        budget -= (std::max(b.dsx, b.sx) + 12) * (std::max(b.dsy, b.sy) + 12) * (std::max(b.dsz, b.sz) + 12);
    }

    std::vector<std::pair<float, int>> lightMeshes;
    lightMeshes.reserve(bodies_.size() + ghosts_.size());
    for (const Body& b : bodies_)
        if (!b.dead && b.meshId) {
            vec3 half = vec3(float(b.dsx), float(b.dsy), float(b.dsz)) * (VOX * .5f);
            float r = length(half);
            // big stuff first. otherwise tiny chips take all 4 slots and the tree shade just vanishes while it falls
            lightMeshes.push_back({std::max(0.0f, length(b.displayOrigin() + half - cam) - r) / (r + 1.0f), b.meshId});
        }
    for (const Ghost& g : ghosts_)
        if (g.meshId) lightMeshes.push_back({length(g.pos - cam) / 4.0f, g.meshId});
    std::sort(lightMeshes.begin(), lightMeshes.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<int> ids;
    for (size_t i = 0; i < std::min<size_t>(4, lightMeshes.size()); ++i) ids.push_back(lightMeshes[i].second);
    renderer_->setBodyLightMeshes(ids);
}

uint64_t Sim::bodyShadowStamp(vec3 source, float radius) const {
    uint64_t stamp = 1469598103934665603ull;
    auto add = [&](int64_t v) {
        stamp ^= uint64_t(v);
        stamp *= 1099511628211ull;
    };
    for (const auto& b : bodies_) {
        vec3 half = vec3(b.dsx, b.dsy, b.dsz) * (VOX * .5f);
        float reach = radius + length(half);
        if (b.dead || !b.meshId || lengthSq(b.displayOrigin() + half - source) > reach * reach) continue;
        add(b.meshId);
        add(b.dorig.x);
        add(b.dorig.y);
        add(b.dorig.z);
    }
    for (const auto& g : ghosts_) {
        if (lengthSq(g.pos - source) > (radius + 52) * (radius + 52)) continue;
        add(g.meshId);
        add(std::llround(g.pos.x * 4096));
        add(std::llround(g.pos.y * 4096));
        add(std::llround(g.pos.z * 4096));
    }
    return stamp;
}

void Sim::drawBodies(Renderer& r, vec3 cam, bool shadowPass) {
    if (!shadowPass) {
        for (Ghost& g : ghosts_) {
            bool done = ++g.frames > 90;
            if (!done) {
                done = true;
                for (auto& [k, v] : g.secs)
                    if (r.sectionVersion(k) <= v) {
                        done = false;
                        break;
                    }
            }
            if (done) {
                r.freeMesh(g.meshId);
                g.meshId = 0;
            }
        }
        ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(), [](const Ghost& g) { return g.meshId == 0; }), ghosts_.end());
    }
    for (const Ghost& g : ghosts_) {
        mat4 model;
        model.m[12] = g.pos.x - cam.x;
        model.m[13] = g.pos.y - cam.y;
        model.m[14] = g.pos.z - cam.z;
        if (shadowPass)
            r.drawMeshShadow(g.meshId, model, g.uvOrigin);
        else
            r.drawMesh(g.meshId, model, g.uvOrigin, 1.0f);
    }
    for (const Body& b : bodies_) {
        if (b.dead || !b.meshId) continue;
        vec3 half = vec3(float(b.dsx), float(b.dsy), float(b.dsz)) * (VOX * .5f);
        vec3 display = b.displayOrigin();
        vec3 center = display + half - cam;
        if (!r.bodyVisible(center, length(half) + .08f, shadowPass)) continue;
        mat4 model;
        model.m[12] = display.x - cam.x;
        model.m[13] = display.y - cam.y;
        model.m[14] = display.z - cam.z;
        vec3 uv(float(b.meshOrigin.x), float(b.meshOrigin.y), float(b.meshOrigin.z));
        if (shadowPass)
            r.drawMeshShadow(b.meshId, model, uv);
        else
            r.drawMesh(b.meshId, model, uv, 1.0f);
    }
}

}
