#include "world/World.h"
#include <algorithm>

namespace qc {
uint8_t Section::brickKinds(int x, int y, int z) const {
    if (!blocks) return physicsKinds(fillBlock);
    if (!physicsBricks) {
        physicsBricks.reset(new uint8_t[8 * 8 * 8 * 3]{});
        for (int y = 0; y < CS; ++y)
            for (int z = 0; z < CS; ++z)
                for (int x = 0; x < CS; ++x) {
                    uint8_t kinds = physicsKinds(block(idx3(x, y, z)));
                    int brick = ((y / 4) * 8 + z / 4) * 8 + x / 4;
                    for (int bit = 0; bit < 3; ++bit) physicsBricks[brick * 3 + bit] += uint8_t((kinds >> bit) & 1);
                }
    }
    int brick = (y * 8 + z) * 8 + x;
    uint8_t result = 0;
    for (int bit = 0; bit < 3; ++bit)
        if (physicsBricks[brick * 3 + bit]) result |= uint8_t(1 << bit);
    return result;
}

uint8_t World::regionPhysicsKinds(vec3 minimum, vec3 maximum) const {
    int x0 = int(std::floor(minimum.x / VOX)), y0 = int(std::floor(minimum.y / VOX)), z0 = int(std::floor(minimum.z / VOX));
    int x1 = int(std::floor(maximum.x / VOX)), y1 = int(std::floor(maximum.y / VOX)), z1 = int(std::floor(maximum.z / VOX));
    uint8_t result = y0 < 0 ? 1 : 0;
    y0 = std::max(0, y0);
    y1 = std::min(WH - 1, y1);
    if (y1 < y0) return result;
    for (int cz = floorDiv(z0, CS); cz <= floorDiv(z1, CS); ++cz)
        for (int cx = floorDiv(x0, CS); cx <= floorDiv(x1, CS); ++cx) {
            const auto* column = this->column(cx, cz);
            if (!column || column->state == ColState::Generating) {
                result |= 1;
                continue;
            }
            int bx0 = std::max(0, x0 - cx * CS) / 4, bx1 = std::min(CS - 1, x1 - cx * CS) / 4;
            int bz0 = std::max(0, z0 - cz * CS) / 4, bz1 = std::min(CS - 1, z1 - cz * CS) / 4;
            for (int sy = y0 / CS; sy <= y1 / CS; ++sy) {
                const auto& sec = column->sec[sy];
                if (!sec.blocks) {
                    result |= physicsKinds(sec.fillBlock);
                    if (result == 7) return result;
                    continue;
                }
                int by0 = std::max(0, y0 - sy * CS) / 4, by1 = std::min(CS - 1, y1 - sy * CS) / 4;
                for (int y = by0; y <= by1; ++y)
                    for (int z = bz0; z <= bz1; ++z)
                        for (int x = bx0; x <= bx1; ++x) {
                            result |= sec.brickKinds(x, y, z);
                            if (result == 7) return result;
                        }
            }
        }
    return result;
}

void Column::indexFeatures() {
    emitters.clear();
    features.clear();
    powerDevices.clear();
    for (int sy = 0; sy < NSEC; ++sy) {
        const auto& s = sec[sy];
        if (!s.blocks && !def(s.fillBlock).emit && s.fillBlock != B_GEYSER && s.fillBlock != B_FIREFLY_BUSH &&
            !(s.fillBlock >= B_CABLE && s.fillBlock <= B_SPARKGAP))
            continue;
        for (int i = 0; i < CS3; ++i) {
            uint8_t b = s.block(i);
            if (def(b).emit) emitters.push_back(i + sy * CS3);
            if (b == B_GEYSER || b == B_FIREFLY_BUSH) features.push_back(i + sy * CS3);
            if (b >= B_CABLE && b <= B_SPARKGAP) powerDevices.push_back(i + sy * CS3);
        }
    }
    emittersScanned = true;
}

void Section::compact() {
    auto uniform = [](std::unique_ptr<uint8_t[]>& a, uint8_t& fill) {
        if (!a) return;
        uint8_t v = a[0];
        for (int i = 1; i < CS3; ++i)
            if (a[i] != v) return;
        fill = v;
        a.reset();
    };
    uniform(blocks, fillBlock);
    uniform(light, fillLight);
    uniform(meta, fillMeta);
    if (radiance) {
        uint16_t v = radiance[0];
        bool same = true;
        for (int i = 1; i < CS3; ++i)
            if (radiance[i] != v) {
                same = false;
                break;
            }
        if (same) {
            fillRadiance = v;
            radiance.reset();
        }
    }
}

Column* World::insert(std::unique_ptr<Column> c) {
    Column* raw = c.get();
    cols_[colKey(c->cx, c->cz)] = std::move(c);
    cacheCol_ = nullptr;
    return raw;
}

std::unique_ptr<Column> World::remove(int cx, int cz) {
    auto it = cols_.find(colKey(cx, cz));
    if (it == cols_.end()) return nullptr;
    std::unique_ptr<Column> c = std::move(it->second);
    cols_.erase(it);
    cacheCol_ = nullptr;
    return c;
}

vec3 World::waterCurrent(int x, int y, int z) const {
    vec3 hint;
    auto c = columnAt(x, z);
    int i = (z & 31) * CS + (x & 31);
    if (c && c->riverLevel[i] > 0 && y < c->riverLevel[i] && y >= c->riverLevel[i] - 12)
        hint = vec3(float(c->riverX[i]) / 100.0f, 0, float(c->riverZ[i]) / 100.0f);
    return waterCurrentAt(
        x, y, z, hint, [&](int x, int y, int z) { return get(x, y, z); }, [&](int x, int y, int z) { return metaAt(x, y, z); });
}

bool World::plantSupported(int x, int y, int z, BlockId b) const {
    uint8_t below = get(x, y - 1, z), above = get(x, y + 1, z);
    if (b == B_VINE) {
        if (above == B_VINE || def(above).solid) return true;
        return collidesAt(x + 1, y, z) || collidesAt(x - 1, y, z) || collidesAt(x, y, z + 1) || collidesAt(x, y, z - 1);
    }
    if (b == B_LILY_PAD) return below == B_WATER && fluidSurfaceAmountOf(B_WATER, metaAt(x, y - 1, z)) >= 200;
    if (b == B_LONG_GRASS_TOP) return below == B_LONG_GRASS;
    if (b == B_CACTUS_FLOWER) return below == B_CACTUS || below == B_BARREL_CACTUS;
    if (b == B_SUGAR_CANE) return below == B_SUGAR_CANE || def(below).collides;
    return def(below).collides;
}

bool World::rawSet(int x, int y, int z, uint8_t b, uint8_t meta) {
    if (y < 0 || y >= WH) return false;
    Column* c = columnAt(x, z);
    if (!c || c->state == ColState::Generating) return false;
    Section& s = c->sec[y >> 5];
    int i = idx3(x & 31, y & 31, z & 31);
    uint8_t old = s.block(i);
    if (lightCost(old) != lightCost(b) || def(old).emit != def(b).emit || ((def(old).emit || def(b).emit) && old != b)) ++c->lightStamp;
    if (def(old).collides != def(b).collides || (lightCost(old) >= 99) != (lightCost(b) >= 99) || (old == B_WATER) != (b == B_WATER))
        s.lightDirty = true;
    if (old != b && (def(old).emit || def(b).emit)) {
        int e = idx3(x & 31, y, z & 31);
        if (!def(old).emit && def(b).emit)
            c->emitters.push_back(e);
        else if (!def(b).emit) {
            auto at = std::find(c->emitters.begin(), c->emitters.end(), e);
            if (at != c->emitters.end()) c->emitters.erase(at);
        }
    }
    if ((old >= B_CABLE && old <= B_SPARKGAP) || (b >= B_CABLE && b <= B_SPARKGAP)) {
        int e = idx3(x & 31, y, z & 31);
        auto it = std::find(c->powerDevices.begin(), c->powerDevices.end(), e);
        if (b >= B_CABLE && b <= B_SPARKGAP) {
            if (it == c->powerDevices.end()) c->powerDevices.push_back(e);
        } else if (it != c->powerDevices.end())
            c->powerDevices.erase(it);
    }
    if (old == B_GEYSER || b == B_GEYSER || old == B_FIREFLY_BUSH || b == B_FIREFLY_BUSH) {
        int e = idx3(x & 31, y, z & 31);
        auto it = std::find(c->features.begin(), c->features.end(), e);
        if ((b == B_GEYSER || b == B_FIREFLY_BUSH) && it == c->features.end()) c->features.push_back(e);
        if (b != B_GEYSER && b != B_FIREFLY_BUSH && it != c->features.end()) c->features.erase(it);
    }
    s.setBlock(i, b);
    s.setMeta(i, meta);
    c->editStamp++;
    c->modified = true;
    return true;
}

void World::rawSetLight(int x, int y, int z, uint8_t l) {
    if (y < 0 || y >= WH) return;
    Column* c = columnAt(x, z);
    if (!c) return;
    c->sec[y >> 5].setLight(idx3(x & 31, y & 31, z & 31), l);
}

void World::rawSetMeta(int x, int y, int z, uint8_t m) {
    if (y < 0 || y >= WH) return;
    Column* c = columnAt(x, z);
    if (!c) return;
    c->sec[y >> 5].setMeta(idx3(x & 31, y & 31, z & 31), m);
    c->modified = true;
}

bool World::setBlock(int x, int y, int z, uint8_t b, uint8_t meta) {
    uint8_t old = get(x, y, z);
    uint8_t oldMeta = metaAt(x, y, z);
    if (old == b && oldMeta == meta) return true;
    if (!rawSet(x, y, z, b, meta)) return false;
    if (old != b) lightAfterEdit(x, y, z, old, b);
    markDirty(x, y, z);
    if (listener) listener->onBlockChanged(x, y, z, old, b);
    return true;
}

void World::markDirtyBox(int x0, int y0, int z0, int x1, int y1, int z1) {
    y0 = std::max(y0, 0);
    y1 = std::min(y1, WH - 1);
    if (y0 > y1) return;
    for (int cz = floorDiv(z0, CS); cz <= floorDiv(z1, CS); ++cz)
        for (int cx = floorDiv(x0, CS); cx <= floorDiv(x1, CS); ++cx) {
            Column* c = column(cx, cz);
            if (!c) continue;
            for (int s = y0 >> 5; s <= (y1 >> 5); ++s) c->meshDirty[s] = true;
        }
}

void World::markDirty(int x, int y, int z) {
    int lx = x & 31, ly = y & 31, lz = z & 31;
    if (lx > 0 && lx < 31 && ly > 0 && ly < 31 && lz > 0 && lz < 31) {
        if (Column* c = columnAt(x, z)) c->meshDirty[y >> 5] = true;
        return;
    }
    markDirtyBox(x - 1, y - 1, z - 1, x + 1, y + 1, z + 1);
}

int World::heightAtTop(int x, int z) const {
    Column* c = columnAt(x, z);
    if (!c) return 0;
    for (int s = NSEC - 1; s >= 0; --s) {
        const Section& sec = c->sec[s];
        if (sec.empty()) continue;
        for (int y = CS - 1; y >= 0; --y)
            if (sec.block(idx3(x & 31, y, z & 31)) != B_AIR) return s * CS + y + 1;
    }
    return 0;
}

namespace {
struct LNode {
    int x, y, z;
    uint8_t lvl;
};
const int DX[6] = {1, -1, 0, 0, 0, 0}, DY[6] = {0, 0, 1, -1, 0, 0}, DZ[6] = {0, 0, 0, 0, 1, -1};
}

void World::lightAfterEdit(int x, int y, int z, uint8_t oldB, uint8_t newB) {
    const BlockDef& od = def(oldB);
    const BlockDef& nd = def(newB);
    int oldCost = lightCost(oldB), newCost = lightCost(newB);
    if (oldCost == newCost && od.emit == nd.emit) return;

    for (int pass = 0; pass < 2; ++pass) {
        bool sky = pass == 0;
        auto get = [&](int px, int py, int pz) -> int {
            uint8_t l = lightAt(px, py, pz);
            return sky ? (l >> 4) : (l & 15);
        };
        auto put = [&](int px, int py, int pz, int v) {
            uint8_t l = lightAt(px, py, pz);
            l = sky ? uint8_t((l & 15) | (v << 4)) : uint8_t((l & 0xF0) | v);
            rawSetLight(px, py, pz, l);
            markDirty(px, py, pz);
        };
        std::vector<LNode> rem, add;
        int cur = get(x, y, z);
        bool darker = newCost > oldCost || (!sky && nd.emit < od.emit);
        if (darker && cur > 0) {
            put(x, y, z, 0);
            rem.push_back({x, y, z, uint8_t(cur)});
        }
        for (size_t h = 0; h < rem.size(); ++h) {
            LNode n = rem[h];
            for (int d = 0; d < 6; ++d) {
                int nx = n.x + DX[d], ny = n.y + DY[d], nz = n.z + DZ[d];
                if (ny < 0 || ny >= WH || !loaded(nx, nz)) continue;
                int nl = get(nx, ny, nz);
                if (nl == 0) continue;
                bool fromAbove = sky && d == 3 && n.lvl == 15 && nl == 15;
                if (nl < n.lvl || fromAbove) {
                    put(nx, ny, nz, 0);
                    rem.push_back({nx, ny, nz, uint8_t(nl)});
                } else {
                    add.push_back({nx, ny, nz, uint8_t(nl)});
                }
            }
        }
        if (!sky && nd.emit > 0) {
            put(x, y, z, nd.emit);
            add.push_back({x, y, z, nd.emit});
        }
        if (newCost < 99) {
            for (int d = 0; d < 6; ++d) {
                int nx = x + DX[d], ny = y + DY[d], nz = z + DZ[d];
                if (ny >= WH && sky) {
                    add.push_back({x, WH, z, 15});
                    continue;
                }
                if (ny < 0 || ny >= WH || !loaded(nx, nz)) continue;
                int nl = get(nx, ny, nz);
                if (nl > 0) add.push_back({nx, ny, nz, uint8_t(nl)});
            }
        }
        for (size_t h = 0; h < add.size(); ++h) {
            LNode n = add[h];
            int lvl = n.y >= WH ? 15 : get(n.x, n.y, n.z);
            if (lvl <= 1) continue;
            for (int d = 0; d < 6; ++d) {
                int nx = n.x + DX[d], ny = n.y + DY[d], nz = n.z + DZ[d];
                if (ny < 0 || ny >= WH || !loaded(nx, nz)) continue;
                uint8_t blk = this->get(nx, ny, nz);
                int c = lightCost(blk);
                if (c >= 99) continue;
                int nlv = (sky && d == 3 && lvl == 15 && c == 1) ? 15 : lvl - c;
                if (nlv <= 0) continue;
                if (nlv > get(nx, ny, nz)) {
                    put(nx, ny, nz, nlv);
                    add.push_back({nx, ny, nz, uint8_t(nlv)});
                }
            }
        }
    }
}

void World::copyRegion(Region& r, int x0, int y0, int z0, int sx, int sy, int sz, bool withLight, bool withMeta) const {
    r.x0 = x0;
    r.y0 = y0;
    r.z0 = z0;
    r.sx = sx;
    r.sy = sy;
    r.sz = sz;
    size_t n = size_t(sx) * sy * sz;
    r.blocks.assign(n, B_STONE);
    if (withLight) {
        r.light.assign(n, 0xF0);
        r.radiance.assign(n, 0);
    } else {
        r.light.clear();
        r.radiance.clear();
    }
    if (withMeta)
        r.meta.assign(n, 0);
    else
        r.meta.clear();
    if (withMeta) {
        r.riverLevel.assign(size_t(sx) * sz, 0);
        r.riverX.assign(size_t(sx) * sz, 0);
        r.riverZ.assign(size_t(sx) * sz, 0);
        r.climT.assign(size_t(sx) * sz, 128);
        r.climH.assign(size_t(sx) * sz, 128);
        for (int z = z0; z < z0 + sz; ++z)
            for (int x = x0; x < x0 + sx; ++x) {
                Column* c = column(floorDiv(x, CS), floorDiv(z, CS));
                if (!c || c->state == ColState::Generating) continue;
                size_t o = size_t((z - z0) * sx + (x - x0));
                int ci = (z & 31) * CS + (x & 31);
                r.climT[o] = c->climT[ci];
                r.climH[o] = c->climH[ci];
                r.riverLevel[o] = c->riverLevel[ci];
                r.riverX[o] = c->riverX[ci];
                r.riverZ[o] = c->riverZ[ci];
            }
    } else {
        r.riverLevel.clear();
        r.riverX.clear();
        r.riverZ.clear();
        r.climT.clear();
        r.climH.clear();
    }
    for (int y = y0; y < y0 + sy; ++y) {
        if (y < 0 || y >= WH) {
            uint8_t fill = y < 0 ? B_BEDROCK : B_AIR;
            for (int z = z0; z < z0 + sz; ++z) {
                size_t o = size_t(r.index(x0, y, z));
                std::memset(&r.blocks[o], fill, sx);
                if (withLight) std::memset(&r.light[o], y < 0 ? 0 : 0xF0, sx);
            }
            continue;
        }
        int s = y >> 5, ly = y & 31;
        for (int z = z0; z < z0 + sz; ++z) {
            int x = x0;
            while (x < x0 + sx) {
                int cx = floorDiv(x, CS);
                int runEnd = std::min(x0 + sx, (cx + 1) * CS);
                int run = runEnd - x;
                size_t o = size_t(r.index(x, y, z));
                Column* c = column(cx, floorDiv(z, CS));
                if (c && c->state != ColState::Generating) {
                    const Section& sec = c->sec[s];
                    int si = idx3(x & 31, ly, z & 31);
                    if (sec.blocks)
                        std::memcpy(&r.blocks[o], &sec.blocks[si], run);
                    else
                        std::memset(&r.blocks[o], sec.fillBlock, run);
                    if (withLight) {
                        if (sec.light)
                            std::memcpy(&r.light[o], &sec.light[si], run);
                        else
                            std::memset(&r.light[o], sec.fillLight, run);
                        if (sec.radiance)
                            std::memcpy(r.radiance.data() + o, sec.radiance.get() + si, size_t(run) * sizeof(uint16_t));
                        else if (sec.fillRadiance)
                            std::fill_n(r.radiance.data() + o, run, sec.fillRadiance);
                    }
                    if (withMeta) {
                        if (sec.meta)
                            std::memcpy(&r.meta[o], &sec.meta[si], run);
                        else
                            std::memset(&r.meta[o], sec.fillMeta, run);
                    }
                }
                x = runEnd;
            }
        }
    }
    if (withLight && std::none_of(r.radiance.begin(), r.radiance.end(), [](uint16_t v) { return v != 0; })) r.radiance.clear();
}

}
