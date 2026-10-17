#include "world/Light.h"
#include <algorithm>

namespace qc {
void computeColumnLight(const Region& r, int cx, int cz, uint8_t* out, uint16_t* radiance) {
    const int sx = r.sx, sy = r.sy, sz = r.sz;
    const size_t n = size_t(sx) * sy * sz;
    const int strideY = sx * sz;
    static thread_local std::vector<uint8_t> sky, blk, cost;
    static thread_local std::vector<int> top;
    static thread_local std::vector<uint32_t> q;
    sky.assign(n, 0);
    blk.assign(n, 0);
    cost.resize(n);
    top.assign(size_t(sx) * sz, 0);
    for (size_t i = 0; i < n; ++i) cost[i] = uint8_t(std::min(lightCost(r.blocks[i]), 99));

    for (int z = 0; z < sz; ++z)
        for (int x = 0; x < sx; ++x) {
            int y = sy - 1;
            size_t i = size_t((y * sz + z) * sx + x);
            while (y >= 0 && cost[i] == 1) {
                sky[i] = 15;
                --y;
                i -= strideY;
            }
            top[z * sx + x] = y + 1;
            int v = 15;
            while (y >= 0) {
                int c = cost[i];
                if (c >= 99) break;
                v -= c;
                if (v <= 0) break;
                sky[i] = uint8_t(v);
                --y;
                i -= strideY;
            }
        }
    q.clear();
    for (int z = 0; z < sz; ++z)
        for (int x = 0; x < sx; ++x) {
            int t = top[z * sx + x];
            int hi = t;
            if (x > 0) hi = std::max(hi, top[z * sx + x - 1]);
            if (x < sx - 1) hi = std::max(hi, top[z * sx + x + 1]);
            if (z > 0) hi = std::max(hi, top[(z - 1) * sx + x]);
            if (z < sz - 1) hi = std::max(hi, top[(z + 1) * sx + x]);
            int lo = std::max(0, t - 16);
            hi = std::min(hi, sy - 1);
            for (int y = lo; y <= hi; ++y) {
                size_t i = size_t((y * sz + z) * sx + x);
                if (sky[i] > 1) q.push_back(uint32_t(i));
            }
        }
    auto bfs = [&](std::vector<uint8_t>& L, bool isSky) {
        for (size_t h = 0; h < q.size(); ++h) {
            uint32_t i = q[h];
            int lvl = L[i];
            if (lvl <= 1) continue;
            int y = int(i / strideY), rem = int(i % strideY), z = rem / sx, x = rem % sx;
            auto visit = [&](uint32_t j, bool down) {
                int c = cost[j];
                if (c >= 99) return;
                int nl = (isSky && down && lvl == 15 && c == 1) ? 15 : lvl - c;
                if (nl > L[j]) {
                    L[j] = uint8_t(nl);
                    q.push_back(j);
                }
            };
            if (x > 0) visit(i - 1, false);
            if (x < sx - 1) visit(i + 1, false);
            if (z > 0) visit(i - sx, false);
            if (z < sz - 1) visit(i + sx, false);
            if (y > 0) visit(i - strideY, true);
            if (y < sy - 1) visit(i + strideY, false);
        }
    };
    bfs(sky, true);
    static thread_local std::vector<uint32_t> sources;
    sources.clear();
    q.clear();
    for (size_t i = 0; i < n; ++i) {
        uint8_t e = def(r.blocks[i]).emit;
        if (e) {
            blk[i] = e;
            q.push_back(uint32_t(i));
            sources.push_back(uint32_t(i));
        }
    }
    bfs(blk, false);

    static thread_local std::vector<uint16_t> rgb;
    if (radiance) rgb.assign(n, 0);
    static thread_local std::vector<uint8_t> channel;
    if (radiance)
        for (int ch = 0; ch < 3; ++ch) {
            channel.assign(n, 0);
            q.clear();
            for (uint32_t i : sources) {
                uint8_t id = r.blocks[i], e = def(id).emit;
                if (!e) continue;
                vec3 color = emissionColor(id);
                float strength = ch == 0 ? color.x : ch == 1 ? color.y : color.z;
                int level = emissionChannelLevel(strength, e);
                if (level) {
                    channel[i] = uint8_t(level);
                    q.push_back(uint32_t(i));
                }
            }
            bfs(channel, false);
            for (uint32_t i : q) rgb[i] |= uint16_t(channel[i]) << (ch * 4);
        }
    const int ox = cx * CS - r.x0, oz = cz * CS - r.z0;
    for (int y = 0; y < WH; ++y)
        for (int z = 0; z < CS; ++z) {
            size_t src = size_t(((y - r.y0) * sz + (z + oz)) * sx + ox);
            uint8_t* dst = out + size_t(y >> 5) * CS3 + idx3(0, y & 31, z);
            for (int x = 0; x < CS; ++x) {
                dst[x] = uint8_t((sky[src + x] << 4) | blk[src + x]);
                if (radiance) radiance[size_t(y >> 5) * CS3 + idx3(x, y & 31, z)] = rgb[src + x];
            }
        }
}

void computeRegionLight(Region& r, bool withRadiance) {
    const int sx = r.sx, sy = r.sy, sz = r.sz;
    const size_t n = size_t(sx) * sy * sz;
    const int strideY = sx * sz;
    static thread_local std::vector<uint8_t> sky, blk, cost, channel;
    static thread_local std::vector<int> top;
    static thread_local std::vector<uint32_t> q, sources;
    static thread_local std::vector<uint16_t> rgb;

    sky.assign(n, 0);
    blk.assign(n, 0);
    cost.resize(n);
    top.assign(size_t(sx) * sz, 0);
    for (size_t i = 0; i < n; ++i) cost[i] = uint8_t(std::min(lightCost(r.blocks[i]), 99));

    for (int z = 0; z < sz; ++z)
        for (int x = 0; x < sx; ++x) {
            int y = sy - 1;
            size_t i = size_t((y * sz + z) * sx + x);
            while (y >= 0 && cost[i] == 1) {
                sky[i] = 15;
                --y;
                if (y >= 0) i -= strideY;
            }
            top[z * sx + x] = y + 1;
            int v = 15;
            while (y >= 0) {
                int c = cost[i];
                if (c >= 99) break;
                v -= c;
                if (v <= 0) break;
                sky[i] = uint8_t(v);
                --y;
                if (y >= 0) i -= strideY;
            }
        }

    q.clear();
    for (int z = 0; z < sz; ++z)
        for (int x = 0; x < sx; ++x) {
            int t = top[z * sx + x], hi = t;
            if (x > 0) hi = std::max(hi, top[z * sx + x - 1]);
            if (x < sx - 1) hi = std::max(hi, top[z * sx + x + 1]);
            if (z > 0) hi = std::max(hi, top[(z - 1) * sx + x]);
            if (z < sz - 1) hi = std::max(hi, top[(z + 1) * sx + x]);
            for (int y = std::max(0, t - 16); y <= std::min(hi, sy - 1); ++y) {
                size_t i = size_t((y * sz + z) * sx + x);
                if (sky[i] > 1) q.push_back(uint32_t(i));
            }
        }

    auto bfs = [&](std::vector<uint8_t>& L, bool isSky) {
        for (size_t h = 0; h < q.size(); ++h) {
            uint32_t i = q[h];
            int lvl = L[i];
            if (lvl <= 1) continue;
            int y = int(i / strideY), rem = int(i % strideY), z = rem / sx, x = rem % sx;
            auto visit = [&](uint32_t j, bool down) {
                int c = cost[j];
                if (c >= 99) return;
                int nl = (isSky && down && lvl == 15 && c == 1) ? 15 : lvl - c;
                if (nl > L[j]) {
                    L[j] = uint8_t(nl);
                    q.push_back(j);
                }
            };
            if (x > 0) visit(i - 1, false);
            if (x < sx - 1) visit(i + 1, false);
            if (z > 0) visit(i - sx, false);
            if (z < sz - 1) visit(i + sx, false);
            if (y > 0) visit(i - strideY, true);
            if (y < sy - 1) visit(i + strideY, false);
        }
    };
    bfs(sky, true);

    sources.clear();
    q.clear();
    for (size_t i = 0; i < n; ++i) {
        uint8_t e = def(r.blocks[i]).emit;
        if (e) {
            blk[i] = e;
            q.push_back(uint32_t(i));
            sources.push_back(uint32_t(i));
        }
    }
    bfs(blk, false);

    if (withRadiance) {
        rgb.assign(n, 0);
        for (int ch = 0; ch < 3; ++ch) {
            channel.assign(n, 0);
            q.clear();
            for (uint32_t i : sources) {
                uint8_t id = r.blocks[i], e = def(id).emit;
                if (!e) continue;
                vec3 color = emissionColor(id);
                float strength = ch == 0 ? color.x : ch == 1 ? color.y : color.z;
                int level = emissionChannelLevel(strength, e);
                if (level) {
                    channel[i] = uint8_t(level);
                    q.push_back(i);
                }
            }
            bfs(channel, false);
            for (uint32_t i : q) rgb[i] |= uint16_t(channel[i]) << (ch * 4);
        }
        r.radiance = rgb;
    } else
        r.radiance.clear();

    r.light.resize(n);
    for (size_t i = 0; i < n; ++i) r.light[i] = uint8_t((sky[i] << 4) | blk[i]);
}

}
