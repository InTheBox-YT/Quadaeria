#pragma once
#include "world/Block.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace qc {
constexpr uint8_t LEAF_PLACED = 0x40;
constexpr int CS = 32;
constexpr int CS2 = CS * CS;
constexpr int CS3 = CS * CS * CS;
constexpr int NSEC = 8;
constexpr int WH = CS * NSEC;
// a mini is half a block
constexpr float VOX = 0.5f;
constexpr int SEA = 126;
constexpr int MAX_FLUID = 255;
constexpr uint8_t FLUID_FULL_META = 254;
constexpr uint8_t FLUID_SOURCE_META = 255;
inline int fluidAmountOf(uint8_t meta) { return meta >= FLUID_FULL_META ? MAX_FLUID : int(meta) + 1; }
inline bool fluidIsSource(uint8_t meta) { return meta == FLUID_SOURCE_META; }

// how far stuff spreads from a source
constexpr int WATER_FLOW_REACH = 14;
constexpr int LAVA_FLOW_REACH = 8;
constexpr int WATER_FLOW_MIN_AMOUNT = 24;
constexpr int LAVA_FLOW_MIN_AMOUNT = 40;
inline int fluidFlowReach(uint8_t type) { return type == B_LAVA ? LAVA_FLOW_REACH : WATER_FLOW_REACH; }
inline int fluidFlowMinAmount(uint8_t type) { return type == B_LAVA ? LAVA_FLOW_MIN_AMOUNT : WATER_FLOW_MIN_AMOUNT; }
inline int fluidFlowAmount(uint8_t type, int distance) {
    const int reach = fluidFlowReach(type);
    if (distance <= 0) return MAX_FLUID;
    if (distance > reach) return 0;
    const int minAmount = fluidFlowMinAmount(type);
    return MAX_FLUID - ((MAX_FLUID - minAmount) * distance + reach / 2) / reach;
}
inline int fluidFlowDistanceFromAmount(uint8_t type, int amount) {
    if (amount >= MAX_FLUID - 2) return 0;
    const int reach = fluidFlowReach(type);
    int best = 1, bestErr = 1 << 30;
    for (int d = 1; d <= reach; ++d) {
        int e = std::abs(fluidFlowAmount(type, d) - amount);
        if (e < bestErr) {
            bestErr = e;
            best = d;
        }
    }
    return best;
}
inline uint8_t fluidFlowMeta(uint8_t type, int distance, bool falling = false) {
    if (distance < 0) return FLUID_SOURCE_META;
    const int amount = fluidFlowAmount(type, distance);
    return uint8_t(std::clamp(amount - 1 - (falling ? 1 : 0), 0, 254));
}
inline bool fluidIsFalling(uint8_t type, uint8_t meta) {
    if (fluidIsSource(meta)) return false;
    const int reach = fluidFlowReach(type);
    for (int d = 0; d <= reach; ++d)
        if (meta == fluidFlowMeta(type, d, true)) return true;
    return false;
}
inline int fluidFlowDistanceFromMeta(uint8_t type, uint8_t meta) {
    if (fluidIsSource(meta)) return 0;
    return fluidFlowDistanceFromAmount(type, fluidAmountOf(meta));
}
inline int fluidSurfaceAmountOf(uint8_t type, uint8_t meta) {
    if (fluidIsSource(meta)) return MAX_FLUID;
    int raw = fluidAmountOf(meta);
    int d = fluidFlowDistanceFromAmount(type, raw);
    if (d <= 0) return MAX_FLUID;
    if (type == B_WATER) {
        int level = std::clamp((d + 1) / 2, 1, 7);
        return MAX_FLUID - ((MAX_FLUID - 32) * level + 3) / 7;
    }
    int level = std::clamp(d, 1, LAVA_FLOW_REACH);
    return MAX_FLUID - ((MAX_FLUID - 40) * level + LAVA_FLOW_REACH / 2) / LAVA_FLOW_REACH;
}

inline int floorDiv(int a, int b) { return (a >= 0 ? a : a - b + 1) / b; }
inline int floorMod(int a, int b) {
    int m = a % b;
    return m < 0 ? m + b : m;
}
inline int64_t colKey(int cx, int cz) { return (int64_t(uint32_t(cx)) << 32) | uint32_t(cz); }
inline int idx3(int x, int y, int z) { return (y * CS + z) * CS + x; }

inline uint8_t physicsKinds(uint8_t block) {
    const auto& d = def(block);
    return uint8_t((d.collides ? 1 : 0) | (block == B_WATER ? 2 : 0) |
                   ((d.render == Render::Leaves || (isPlantDetail(block) && block != B_FIRE && block != B_TORCH)) ? 4 : 0));
}
struct Section {
    std::unique_ptr<uint8_t[]> blocks, light, meta;
    mutable std::unique_ptr<uint8_t[]> physicsBricks;
    uint8_t brickKinds(int x, int y, int z) const;
    std::unique_ptr<uint16_t[]> radiance;
    uint16_t fillRadiance = 0;
    uint16_t radianceAt(int i) const { return radiance ? radiance[i] : fillRadiance; }
    uint8_t fillBlock = B_AIR, fillLight = 0xF0, fillMeta = 0;
    bool lightDirty = true;

    uint8_t block(int i) const { return blocks ? blocks[i] : fillBlock; }
    uint8_t lightAt(int i) const { return light ? light[i] : fillLight; }
    uint8_t metaAt(int i) const { return meta ? meta[i] : fillMeta; }
    void setBlock(int i, uint8_t v) {
        if (physicsBricks) {
            uint8_t before = physicsKinds(block(i)), after = physicsKinds(v);
            int x = i % CS, y = i / CS2, z = (i / CS) % CS, brick = ((y / 4) * 8 + z / 4) * 8 + x / 4;
            for (int bit = 0; bit < 3; ++bit) physicsBricks[brick * 3 + bit] += int((after >> bit) & 1) - int((before >> bit) & 1);
        }
        if (!blocks) {
            if (v == fillBlock) return;
            blocks.reset(new uint8_t[CS3]);
            std::memset(blocks.get(), fillBlock, CS3);
        }
        blocks[i] = v;
    }
    void setLight(int i, uint8_t v) {
        if (!light) {
            if (v == fillLight) return;
            light.reset(new uint8_t[CS3]);
            std::memset(light.get(), fillLight, CS3);
        }
        if (light[i] != v) {
            light[i] = v;
            lightDirty = true;
        }
    }
    void setMeta(int i, uint8_t v) {
        if (!meta) {
            if (v == fillMeta) return;
            meta.reset(new uint8_t[CS3]);
            std::memset(meta.get(), fillMeta, CS3);
        }
        meta[i] = v;
    }
    bool empty() const { return !blocks && fillBlock == B_AIR; }
    void compact();
};

enum class ColState : uint8_t { Generating, Generated, Lighting, Lit };

struct Column {
    int cx = 0, cz = 0;
    Section sec[NSEC];
    ColState state = ColState::Generating;
    uint32_t lightStamp = 0;
    uint32_t bakedLightStamp = 0;
    bool hasLight = false;
    uint32_t editStamp = 0;
    bool lightPending = false;
    bool modified = false;
    std::vector<int> powerDevices;
    std::vector<int> features;
    bool emittersScanned = false;
    std::vector<int> emitters;
    bool powerScanned = false;
    bool meshDirty[NSEC] = {};
    bool meshLightDirty[NSEC] = {};
    bool meshPending[NSEC] = {};
    double lastSeen = 0;
    double relightAt = 0;
    uint8_t staleSkips = 0;
    uint8_t climT[CS * CS], climH[CS * CS];
    uint8_t riverLevel[CS2] = {};
    int8_t riverX[CS2] = {}, riverZ[CS2] = {};
    void indexFeatures();
    Column() {
        std::memset(climT, 128, sizeof(climT));
        std::memset(climH, 128, sizeof(climH));
    }

    uint8_t get(int x, int y, int z) const { return sec[y >> 5].block(idx3(x, y & 31, z)); }
    uint8_t lightAt(int x, int y, int z) const { return sec[y >> 5].lightAt(idx3(x, y & 31, z)); }
    uint8_t metaAt(int x, int y, int z) const { return sec[y >> 5].metaAt(idx3(x, y & 31, z)); }
};

struct Region {
    int x0 = 0, y0 = 0, z0 = 0, sx = 0, sy = 0, sz = 0;
    std::vector<uint8_t> blocks, light, meta;
    std::vector<uint16_t> radiance;
    std::vector<uint8_t> riverLevel;
    std::vector<int8_t> riverX, riverZ;
    std::vector<uint8_t> climT, climH;
    int index(int x, int y, int z) const { return ((y - y0) * sz + (z - z0)) * sx + (x - x0); }
};

template <class BlockAt, class MetaAt> vec3 waterCurrentAt(int x, int y, int z, vec3 hint, BlockAt block, MetaAt meta) {
    if (block(x, y, z) != B_WATER) return {};
    float own = fluidSurfaceAmountOf(B_WATER, meta(x, y, z)) / 255.0f;
    float head[4];
    const int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
        auto b = block(x + dx[k], y, z + dz[k]);
        head[k] = (def(b).collides || (def(b).fluid && b != B_WATER)) ? own
                  : b == B_WATER ? fluidSurfaceAmountOf(B_WATER, meta(x + dx[k], y, z + dz[k])) / 255.0f
                                 : 0.0f;
    }
    vec3 local((head[1] - head[0]) * 0.85f, 0, (head[3] - head[2]) * 0.85f);
    vec3 flow = lengthSq(local) > 0.01f ? local : hint;
    if ((flow.x > 0 && def(block(x + 1, y, z)).collides) || (flow.x < 0 && def(block(x - 1, y, z)).collides)) flow.x = 0;
    if ((flow.z > 0 && def(block(x, y, z + 1)).collides) || (flow.z < 0 && def(block(x, y, z - 1)).collides)) flow.z = 0;
    if (lengthSq(flow) < 0.0004f) flow = {};
    float len = length(flow);
    if (len > 1) flow = flow / len;
    bool openSide = false;
    for (int k = 0; k < 4; ++k) openSide |= block(x + dx[k], y, z + dz[k]) == B_AIR;
    bool falling = fluidIsFalling(B_WATER, meta(x, y, z)) || (openSide && block(x, y + 1, z) == B_WATER) || block(x, y - 1, z) == B_AIR;
    flow.y = falling ? -1.0f : 0.0f;
    return flow;
}

class World {
public:
    Column* column(int cx, int cz) const {
        if (cacheCol_ && cacheCol_->cx == cx && cacheCol_->cz == cz) return cacheCol_;
        auto it = cols_.find(colKey(cx, cz));
        if (it == cols_.end()) return nullptr;
        cacheCol_ = it->second.get();
        return cacheCol_;
    }
    Column* columnAt(int x, int z) const { return column(x >> 5, z >> 5); }
    Column* insert(std::unique_ptr<Column> c);
    std::unique_ptr<Column> remove(int cx, int cz);
    vec3 waterCurrent(int x, int y, int z) const;
    bool plantSupported(int x, int y, int z, BlockId b) const;
    uint8_t regionPhysicsKinds(vec3 minimum, vec3 maximum) const;
    const std::unordered_map<int64_t, std::unique_ptr<Column>>& columns() const { return cols_; }

    bool loaded(int x, int z) const {
        Column* c = columnAt(x, z);
        return c && c->state != ColState::Generating;
    }
    uint8_t get(int x, int y, int z) const {
        if (y < 0) return B_BEDROCK;
        if (y >= WH) return B_AIR;
        Column* c = columnAt(x, z);
        return c ? c->get(x & 31, y, z & 31) : uint8_t(B_STONE);
    }
    uint8_t lightAt(int x, int y, int z) const {
        if (y >= WH) return 0xF0;
        if (y < 0) return 0;
        Column* c = columnAt(x, z);
        return c ? c->lightAt(x & 31, y, z & 31) : 0xF0;
    }
    uint8_t metaAt(int x, int y, int z) const {
        if (y < 0 || y >= WH) return 0;
        Column* c = columnAt(x, z);
        return c ? c->metaAt(x & 31, y, z & 31) : 0;
    }
    int fluidAmount(int x, int y, int z) const {
        uint8_t b = get(x, y, z);
        return def(b).fluid ? fluidAmountOf(metaAt(x, y, z)) : 0;
    }
    bool solidAt(int x, int y, int z) const { return def(get(x, y, z)).solid; }
    bool collidesAt(int x, int y, int z) const { return def(get(x, y, z)).collides; }

    bool rawSet(int x, int y, int z, uint8_t b, uint8_t meta = 0);
    void rawSetLight(int x, int y, int z, uint8_t l);
    void rawSetMeta(int x, int y, int z, uint8_t m);

    bool setBlock(int x, int y, int z, uint8_t b, uint8_t meta = 0);

    void markDirty(int x, int y, int z);
    void markDirtyBox(int x0, int y0, int z0, int x1, int y1, int z1);

    void lightAfterEdit(int x, int y, int z, uint8_t oldBlock, uint8_t newBlock);
    int skyAt(int x, int y, int z) const { return lightAt(x, y, z) >> 4; }
    int blockLightAt(int x, int y, int z) const { return lightAt(x, y, z) & 15; }

    void copyRegion(Region& r, int x0, int y0, int z0, int sx, int sy, int sz, bool withLight, bool withMeta) const;

    struct Listener {
        virtual ~Listener() = default;
        virtual void onBlockChanged(int x, int y, int z, uint8_t oldB, uint8_t newB) = 0;
    };
    Listener* listener = nullptr;
    uint64_t seed = 0;
    int heightAtTop(int x, int z) const;

private:
    std::unordered_map<int64_t, std::unique_ptr<Column>> cols_;
    mutable Column* cacheCol_ = nullptr;
};

inline int lightCost(uint8_t b) {
    const BlockDef& d = def(b);
    return d.light == LightKind::Opaque ? 99 : d.light == LightKind::Filter ? 2 : 1;
}

}
