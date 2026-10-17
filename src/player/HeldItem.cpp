#include "player/HeldItem.h"
#include "gfx/Textures.h"
#include <unordered_map>

namespace qc {
namespace {
const std::vector<uint32_t>& texels() {
    static std::vector<uint32_t> px;
    if (px.empty()) {
        int n = 0;
        px = loadTextures(n);
    }
    return px;
}

uint32_t texel(int layer, int x, int y) { return texels()[size_t(layer) * 256 + size_t((y & 15) * 16 + (x & 15))]; }

struct Part {
    int nx = 0, ny = 0, nz = 0;
    std::vector<uint32_t> c;
    uint32_t at(int x, int y, int z) const { return c[size_t((y * nz + z) * nx + x)]; }
};

const Part& blockPart(ItemId id) {
    static std::unordered_map<int, Part> cache;
    auto it = cache.find(int(id));
    if (it != cache.end()) return it->second;
    const BlockDef& d = def(BlockId(id));
    Part p;
    p.nx = p.ny = p.nz = 4;
    p.c.assign(64, 0);
    for (int y = 0; y < 4; ++y)
        for (int z = 0; z < 4; ++z)
            for (int x = 0; x < 4; ++x) {
                int tex = y == 3 ? d.texTop : d.texSide;
                uint32_t c = texel(tex, x * 4 + 1 + z, (3 - y) * 4 + 2);
                p.c[size_t((y * 4 + z) * 4 + x)] = ((c >> 24) > 100 ? c : texel(tex, 8, 8)) | 0xFF000000u;
            }
    return cache.emplace(int(id), std::move(p)).first->second;
}

void pushVoxel(std::vector<BoxInstance>& out, const mat3& A, vec3 t, vec3 q, uint32_t col, float sky, float blk) {
    vec3 p = t + A * q;
    BoxInstance b;
    b.r0 = {A.m[0][0], A.m[0][1], A.m[0][2], p.x};
    b.r1 = {A.m[1][0], A.m[1][1], A.m[1][2], p.y};
    b.r2 = {A.m[2][0], A.m[2][1], A.m[2][2], p.z};
    b.layers = vec4(-1);
    b.params = {sky, blk, 0, 0};
    b.tint = vec4(float(col & 255) / 255.0f, float((col >> 8) & 255) / 255.0f, float((col >> 16) & 255) / 255.0f, 1.0f);
    out.push_back(b);
}

}

void drawHeldItem(std::vector<BoxInstance>& out, ItemId item, const mat3& A, vec3 t, float sky, float blk) {
    // flat items get drawn as a thin slab
    bool sprite = !(isBlockItem(item) && def(BlockId(item)).render != Render::Cross);
    if (sprite) {
        int tex = isBlockItem(item) ? def(BlockId(item)).texSide : itemDef(item).tex;
        mat3 H = A;
        for (int r = 0; r < 3; ++r) {
            H.m[r][0] *= 0.62f;
            H.m[r][1] *= 0.62f;
        }
        auto solid = [&](int x, int y) { return x >= 0 && y >= 0 && x < 16 && y < 16 && (texel(tex, x, y) >> 24) > 100; };
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                if (!solid(x, y)) continue;
                uint32_t c = texel(tex, x, y);
                if (!solid(x + 1, y) || !solid(x, y + 1)) {
                    int r = int(c & 255) * 3 / 4, g = int((c >> 8) & 255) * 3 / 4, bb = int((c >> 16) & 255) * 3 / 4;
                    c = uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(bb) << 16);
                }
                pushVoxel(out, H, t, vec3(float(x), float(15 - y), 0.0f), c | 0xFF000000u, sky, blk);
            }
        return;
    }
    const Part& ip = blockPart(item);
    for (int y = 0; y < ip.ny; ++y)
        for (int z = 0; z < ip.nz; ++z)
            for (int x = 0; x < ip.nx; ++x)
                if (uint32_t c = ip.at(x, y, z)) pushVoxel(out, A, t, vec3(float(x), float(y), float(z)), c, sky, blk);
}

}
