#pragma once
#include "world/World.h"
#include <cstdint>
#include <array>
#include <vector>

namespace qc {
struct MeshOut {
    std::vector<uint32_t> solid;
    std::vector<uint32_t> cutout;
    std::vector<uint32_t> trans;
    std::vector<uint32_t> detail;
    std::array<uint32_t, 7> solidFaces{}, cutoutFaces{};
    std::vector<uint8_t> bodyLight;
    std::array<int, 3> bodyLightSize{}, bodyLightOrigin{};
    uint64_t visibility = ~0ull;
    void clear() {
        solid.clear();
        cutout.clear();
        trans.clear();
        detail.clear();
        solidFaces = {};
        cutoutFaces = {};
        bodyLight.clear();
        bodyLightSize = {};
        bodyLightOrigin = {};
        visibility = ~0ull;
    }
    bool empty() const { return solid.empty() && cutout.empty() && trans.empty() && detail.empty(); }
};

enum UvMode : uint32_t { UV_WORLD = 0, UV_TOPALIGNED = 1, UV_CORNER = 2, UV_FLUID = 3 };

inline void packVertex(std::vector<uint32_t>& o, int x, int y, int z, int face, int cu, int cv, uint32_t mode, bool sway, int yoff, int tex,
                       int sky, int blk, int ao, uint32_t clim = 0, uint32_t uvRot = 0, bool explicitUV = false, uint32_t uvWord = 0) {
    o.push_back((uint32_t(x) & 63u) | ((uint32_t(y) & 63u) << 6) | ((uint32_t(z) & 63u) << 12) | (uint32_t(face) << 18) |
                (uint32_t(cu) << 21) | (uint32_t(cv) << 22) | (mode << 23) | (uint32_t(sway) << 25) | (uint32_t(yoff) << 26) |
                (uvRot << 30) | (uint32_t(explicitUV) << 31));
    o.push_back(uint32_t(tex) | (uint32_t(sky) << 8) | (uint32_t(blk) << 12) | (uint32_t(ao) << 16) | (clim << 18));
    o.push_back(uvWord);
    o.push_back(((uint32_t(x) >> 6) & 1u) << 29 | ((uint32_t(y) >> 6) & 1u) << 30 | ((uint32_t(z) >> 6) & 1u) << 31);
}
constexpr int VERT_WORDS = 4;

void meshSection(const Region& r, MeshOut& out, bool fancyLeaves);

void meshRegion(const Region& r, int sx, int sy, int sz, MeshOut& out, bool fancyLeaves);

void meshRegionOwned(const Region& r, int sx, int sy, int sz, const uint8_t* owned, MeshOut& out, bool fancyLeaves);

void meshGrid(const uint8_t* blocks, int sx, int sy, int sz, MeshOut& out, int sky, int blk, uint8_t climT = 128, uint8_t climH = 128);

}
