#include "stream/Streamer.h"
#include "world/Light.h"
#include "world/Save.h"
#include <algorithm>
#include <cmath>

namespace qc {
void Streamer::init(World* w, Generator* g, Renderer* r, TaskPool* pool, const std::string& dir) {
    world_ = w;
    gen_ = g;
    renderer_ = r;
    pool_ = pool;
    saveDir = dir;
    requested_.clear();
    meshVersion_.clear();
    genInFlight_ = lightInFlight_ = meshInFlight_ = 0;
    ringRadius_ = -1;
    nextUnload_ = 0;
}

// all 8 around it have to be ready or the edges come out wrong
bool Streamer::neighboursAtLeast(int cx, int cz, ColState s) const {
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            Column* n = world_->column(cx + dx, cz + dz);
            if (!n || int(n->state) < int(s)) return false;
        }
    return true;
}

void Streamer::requestColumn(int cx, int cz, float pri) {
    int64_t key = colKey(cx, cz);
    if (requested_.count(key) || world_->column(cx, cz)) return;
    requested_.insert(key);
    genInFlight_++;
    Generator* gen = gen_;
    std::string dir = saveDir;
    TaskPool* pool = pool_;
    pool_->submit(pri, [=, this]() {
        std::unique_ptr<Column> col = loadColumn(dir, cx, cz);
        if (col)
            gen->fillClimate(*col);
        else
            col = gen->generate(cx, cz);
        Column* raw = col.release();
        pool->post([=, this]() {
            std::unique_ptr<Column> c(raw);
            genInFlight_--;
            requested_.erase(colKey(cx, cz));
            if (world_->column(cx, cz)) return;
            c->lastSeen = 0;
            world_->insert(std::move(c));
        });
    });
}

void Streamer::scheduleLight(Column& c, float pri) {
    const bool urgent = c.hasLight;
    c.lightPending = true;
    c.state = c.hasLight ? ColState::Lit : ColState::Lighting;
    lightInFlight_++;
    auto region = std::make_shared<Region>();
    world_->copyRegion(*region, c.cx * CS - LIGHT_PAD, 0, c.cz * CS - LIGHT_PAD, CS + 2 * LIGHT_PAD, WH, CS + 2 * LIGHT_PAD, false, false);
    uint32_t stamps[9];
    for (int i = 0; i < 9; ++i) {
        Column* n = world_->column(c.cx + i % 3 - 1, c.cz + i / 3 - 1);
        stamps[i] = n ? n->lightStamp : 0;
    }
    int cx = c.cx, cz = c.cz;
    TaskPool* pool = pool_;
    pool_->submit(urgent ? pri - 200.0f : pri, [=, this]() {
        uint8_t* out = new uint8_t[size_t(CS3) * NSEC];
        uint16_t* rgb = new uint16_t[size_t(CS3) * NSEC];
        computeColumnLight(*region, cx, cz, out, rgb);
        pool->post(
            [=, this]() {
                std::unique_ptr<uint8_t[]> light(out);
                std::unique_ptr<uint16_t[]> radiance(rgb);
                lightInFlight_--;
                Column* col = world_->column(cx, cz);
                if (!col) return;
                col->lightPending = false;
                bool stale = false;
                for (int i = 0; i < 9; ++i) {
                    Column* n = world_->column(cx + i % 3 - 1, cz + i / 3 - 1);
                    if (!n || n->lightStamp != stamps[i]) stale = true;
                }
                if (stale && col->hasLight && ++col->staleSkips <= 1) {
                    col->relightAt = now_ + 0.008;
                    col->state = col->hasLight ? ColState::Lit : ColState::Generated;
                    return;
                }
                const bool first = !col->hasLight;
                bool changed[NSEC] = {};
                col->hasLight = true;
                col->bakedLightStamp = stamps[4];
                col->staleSkips = 0;
                if (stale) col->relightAt = now_ + .008;
                for (int s = 0; s < NSEC; ++s) {
                    Section& sec = col->sec[s];
                    auto src = light.get() + size_t(s) * CS3;
                    auto rgb = radiance.get() + size_t(s) * CS3;
                    bool different = first;
                    for (int i = 0; !different && i < CS3; ++i) different = sec.lightAt(i) != src[i] || sec.radianceAt(i) != rgb[i];
                    if (!different) continue;
                    changed[s] = true;
                    if (!sec.light) sec.light.reset(new uint8_t[CS3]);
                    if (!sec.radiance) sec.radiance.reset(new uint16_t[CS3]);
                    std::memcpy(sec.light.get(), src, CS3);
                    std::memcpy(sec.radiance.get(), rgb, CS3 * sizeof(uint16_t));
                    sec.compact();
                    sec.lightDirty = true;
                }
                col->state = ColState::Lit;
                for (int s = 0; s < NSEC; ++s)
                    if (changed[s])
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx) {
                                Column* n = world_->column(cx + dx, cz + dz);
                                if (!n || !n->hasLight) continue;
                                for (int sy = std::max(0, s - 1); sy <= std::min(NSEC - 1, s + 1); ++sy)
                                    if (n == col || renderer_->hasSection(n->cx, n->cz, sy)) {
                                        n->meshLightDirty[sy] = true;
                                        if (first || !renderer_->liveLightColumn(n->cx, n->cz)) n->meshDirty[sy] = true;
                                    }
                            }
            },
            urgent);
    });
}

void Streamer::scheduleMesh(Column& c, int sy, float pri, bool fancy) {
    c.meshDirty[sy] = false;
    c.meshLightDirty[sy] = false;
    const Section& sec = c.sec[sy];
    if (sec.empty()) {
        bool anyNeighbour = false;
        if (sy > 0 && !c.sec[sy - 1].empty()) anyNeighbour = true;
        if (sy < NSEC - 1 && !c.sec[sy + 1].empty()) anyNeighbour = true;
        if (!anyNeighbour) {
            renderer_->freeSection(c.cx, c.cz, sy);
            return;
        }
    }
    int64_t key = sectionKey(c.cx, c.cz, sy);
    uint32_t ver = ++meshVersion_[key];
    const bool urgent = renderer_->hasSection(c.cx, c.cz, sy);
    c.meshPending[sy] = true;
    meshInFlight_++;
    auto region = std::make_shared<Region>();
    world_->copyRegion(*region, c.cx * CS - 1, sy * CS - 1, c.cz * CS - 1, CS + 2, CS + 2, CS + 2, true, true);
    int cx = c.cx, cz = c.cz;
    TaskPool* pool = pool_;
    pool_->submit(urgent ? pri - 180.0f : pri, [=, this]() {
        auto mesh = std::make_shared<MeshOut>();
        meshSection(*region, *mesh, fancy);
        pool->post(
            [=, this]() {
                meshInFlight_--;
                auto it = meshVersion_.find(key);
                if (it == meshVersion_.end() || it->second != ver) return;
                Column* current = world_->column(cx, cz);
                if (!current) return;
                current->meshPending[sy] = false;
                renderer_->uploadSection(cx, cz, sy, *mesh);
            },
            urgent);
    });
}

void Streamer::requestRelight(int64_t key) {
    Column* c = world_->column(int(int32_t(uint32_t(key >> 32))), int(int32_t(uint32_t(key & 0xFFFFFFFF))));
    if (!c) return;
    if (c->state == ColState::Lit && c->relightAt <= 0)
        c->relightAt = now_ + 0.008;
    else if (c->lightPending)
        c->relightAt = now_ + 0.008;
}

void Streamer::update(vec3 p, int radius, bool fancy, double now) {
    now_ = now;
    int pcx = floorDiv(int(std::floor(p.x / VOX)), CS), pcz = floorDiv(int(std::floor(p.z / VOX)), CS);
    if (ringRadius_ != radius) {
        ring_.clear();
        const int gr = radius + 3;
        for (int dz = -gr; dz <= gr; ++dz)
            for (int dx = -gr; dx <= gr; ++dx)
                if (dx * dx + dz * dz <= gr * gr) ring_.push_back({dx, dz});
        std::sort(ring_.begin(), ring_.end(),
                  [](auto& a, auto& b) { return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second; });
        ringRadius_ = radius;
    }
    const int maxGen = int(pool_->workers()) * 2 + 2;
    for (auto& [dx, dz] : ring_) {
        if (genInFlight_ >= maxGen) break;
        int cx = pcx + dx, cz = pcz + dz;
        if (!world_->column(cx, cz)) requestColumn(cx, cz, float(dx * dx + dz * dz));
    }
    const int W = int(pool_->workers());
    int lightBudget = std::max(2, W * 2 - lightInFlight_), meshBudget = std::max(4, W * 6 - meshInFlight_);
    for (auto& [dx, dz] : ring_) {
        Column* c = world_->column(pcx + dx, pcz + dz);
        if (!c) continue;
        c->lastSeen = now;
        float pri = float(dx * dx + dz * dz);
        bool relight = c->relightAt > 0 && now >= c->relightAt;
        if ((c->state == ColState::Generated || relight) && !c->lightPending && lightBudget > 0 &&
            lightInFlight_ < int(pool_->workers()) * 2 && neighboursAtLeast(c->cx, c->cz, ColState::Generated)) {
            c->relightAt = 0;
            scheduleLight(*c, pri - 0.5f);
            --lightBudget;
        }
        if (c->state == ColState::Lit && dx * dx + dz * dz <= radius * radius && meshBudget > 0 &&
            neighboursAtLeast(c->cx, c->cz, ColState::Lit)) {
            const bool live = renderer_->liveLightColumn(c->cx, c->cz);
            bool any = false;
            for (int sy = 0; sy < NSEC; ++sy) {
                if (c->meshLightDirty[sy] && !live) c->meshDirty[sy] = true;
                if (c->meshDirty[sy] && !c->meshPending[sy]) any = true;
            }
            if (!any) continue;
            int eye = std::clamp(int(std::floor(p.y / VOX)) / CS, 0, NSEC - 1);
            for (int dist = 0; dist < NSEC && meshBudget > 0; ++dist) {
                int cand[2] = {eye - dist, eye + dist};
                int n = dist == 0 ? 1 : 2;
                for (int q = 0; q < n && meshBudget > 0; ++q) {
                    int sy = cand[q];
                    if (sy < 0 || sy >= NSEC || !c->meshDirty[sy] || c->meshPending[sy]) continue;
                    scheduleMesh(*c, sy, pri - 1.0f, fancy);
                    --meshBudget;
                }
            }
        }
    }
    if (pcx != lastCx_ || pcz != lastCz_ || now >= nextUnload_) {
        lastCx_ = pcx;
        lastCz_ = pcz;
        nextUnload_ = now + 2.0;
        std::vector<std::pair<int, int>> drop;
        int lim = radius + 5;
        for (auto& [k, c] : world_->columns()) {
            int dx = c->cx - pcx, dz = c->cz - pcz;
            if (dx * dx + dz * dz > lim * lim && !c->lightPending &&
                std::none_of(std::begin(c->meshPending), std::end(c->meshPending), [](bool busy) { return busy; }))
                drop.push_back({c->cx, c->cz});
        }
        for (auto& [cx, cz] : drop) {
            std::unique_ptr<Column> c = world_->remove(cx, cz);
            if (c && c->modified && !saveDir.empty()) saveColumn(saveDir, *c);
            renderer_->freeColumn(cx, cz);
            for (int s = 0; s < NSEC; ++s) meshVersion_.erase(sectionKey(cx, cz, s));
        }
    }
}

void Streamer::remeshAll() {
    for (auto& [k, c] : world_->columns())
        if (c->state == ColState::Lit)
            for (int s = 0; s < NSEC; ++s) c->meshDirty[s] = true;
}

void Streamer::saveAll() {
    if (saveDir.empty()) return;
    for (auto& [k, c] : world_->columns())
        // only save columns that got changed, everything else regens from the seed
        if (c->modified && c->state != ColState::Generating) saveColumn(saveDir, *c);
}

void Streamer::unloadAll(bool save) {
    if (save) saveAll();
    std::vector<std::pair<int, int>> all;
    for (auto& [k, c] : world_->columns()) all.push_back({c->cx, c->cz});
    for (auto& [cx, cz] : all) {
        world_->remove(cx, cz);
        renderer_->freeColumn(cx, cz);
    }
    meshVersion_.clear();
    requested_.clear();
}

bool Streamer::areaReady(vec3 p, int r) const {
    int pcx = floorDiv(int(std::floor(p.x / VOX)), CS), pcz = floorDiv(int(std::floor(p.z / VOX)), CS);
    for (int dz = -r; dz <= r; ++dz)
        for (int dx = -r; dx <= r; ++dx) {
            Column* c = world_->column(pcx + dx, pcz + dz);
            if (!c || c->state != ColState::Lit) return false;
            for (int s = 0; s < NSEC; ++s)
                if (c->meshDirty[s] || c->meshPending[s]) return false;
        }
    return true;
}

int Streamer::loadedColumns() const { return int(world_->columns().size()); }

}
