#pragma once
#include "core/Math.h"
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace qc {
struct PointLight {
    vec3 pos, color;
    float radius = 8;
    bool baked = false, priority = false;
    int emission = 14;
    bool stationary = false;
    uint64_t id = 0;
};

inline uint64_t lightIdentity(uint64_t kind, int x, int y, int z) {
    uint64_t h = kind + 0x9e3779b97f4a7c15ull;
    for (int v : {x, y, z}) { h ^= uint32_t(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ull;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebull;
    return (h ^ (h >> 31)) | 1;
}

struct LightClusters {
    static constexpr int X = 16, Y = 9, Z = 24, Count = X * Y * Z, MaxLights = 256;
    static constexpr float Near = .25f, Far = 80.0f;
    std::array<std::vector<uint16_t>, Count> cells;
    std::array<uint32_t, Count * 2> ranges{};
    std::vector<uint32_t> indices;
    int maxPerCell = 0;
    static int slice(float z) {
        return std::clamp(int(std::floor(std::log(std::max(z, Near) / Near) * (Z / std::log(Far / Near)))), 0, Z - 1);
    }
    static int cell(vec3 view, float tanY, float aspect) {
        float z = std::max(Near, view.z);
        int x = std::clamp(int((view.x / (z * tanY * aspect) * .5f + .5f) * X), 0, X - 1);
        int y = std::clamp(int((view.y / (z * tanY) * .5f + .5f) * Y), 0, Y - 1);
        return (slice(z) * Y + y) * X + x;
    }
    void build(const std::vector<PointLight>& lights, const mat4& view, vec3 camera, float tanY, float aspect) {
        for (auto& c : cells) c.clear();
        std::array<float, Z + 1> depths;
        for (int z = 0; z <= Z; ++z) depths[z] = Near * std::pow(Far / Near, float(z) / Z);
        float tanX = tanY * aspect;
        for (size_t i = 0; i < lights.size() && i < MaxLights; ++i) {
            const auto& l = lights[i];
            vec3 d = l.pos - camera;
            vec4 v = view * vec4(d.x, d.y, d.z, 1);
            vec3 p(v.x, v.y, -v.z);
            float r = l.radius;
            if (p.z + r < 0 || p.z - r > Far) continue;
            for (int z = slice(p.z - r); z <= slice(p.z + r); ++z) {
                float a = depths[z], b = depths[z + 1];
                auto bounds = [&](float center, float tangent, int size) {
                    float lo = std::min((center - r) / a, (center - r) / b) / tangent;
                    float hi = std::max((center + r) / a, (center + r) / b) / tangent;
                    return std::pair<int, int>{std::clamp(int(std::floor((lo * .5f + .5f) * size)), 0, size - 1),
                                               std::clamp(int(std::floor((hi * .5f + .5f) * size)), 0, size - 1)};
                };
                auto [x0, x1] = bounds(p.x, tanX, X);
                auto [y0, y1] = bounds(p.y, tanY, Y);
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        float lx = (float(x) / X * 2 - 1) * tanX, hx = (float(x + 1) / X * 2 - 1) * tanX;
                        float ly = (float(y) / Y * 2 - 1) * tanY, hy = (float(y + 1) / Y * 2 - 1) * tanY;
                        vec3 lo(std::min(lx * a, lx * b), std::min(ly * a, ly * b), z == 0 ? 0 : a);
                        vec3 hi(std::max(hx * a, hx * b), std::max(hy * a, hy * b), b);
                        vec3 nearest(std::clamp(p.x, lo.x, hi.x), std::clamp(p.y, lo.y, hi.y), std::clamp(p.z, lo.z, hi.z));
                        if (lengthSq(nearest - p) <= r * r) cells[(z * Y + y) * X + x].push_back(uint16_t(i));
                    }
            }
        }
        indices.clear();
        maxPerCell = 0;
        for (int i = 0; i < Count; ++i) {
            ranges[i * 2] = uint32_t(indices.size());
            ranges[i * 2 + 1] = uint32_t(cells[i].size());
            maxPerCell = std::max(maxPerCell, int(cells[i].size()));
            indices.insert(indices.end(), cells[i].begin(), cells[i].end());
        }
    }
};
}
