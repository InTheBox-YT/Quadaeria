#pragma once
#include "core/Assets.h"
#include "world/Block.h"
#include <array>
#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <vector>
namespace qc {
struct PlantFace {
    vec3 p[4];
    int face, tex;
};
inline const char* plantModelName(int id) {
    switch (id) {
    case B_TALL_GRASS: return "tall_grass";
    case B_FLOWER_RED: return "flower_red";
    case B_FLOWER_YELLOW: return "flower_yellow";
    case B_DEAD_BUSH: return "dead_bush";
    case B_FERN: return "fern";
    case B_BLUE_FLOWER: return "blue_flower";
    case B_SUGAR_CANE: return "sugar_cane";
    case B_GLOWSHROOM: return "glowshroom";
    case B_CRYSTAL: return "crystal";
    case B_VINE: return "vine";
    case B_WILD_WHEAT: return "wild_wheat";
    case B_LONG_GRASS: return "long_grass";
    case B_LONG_GRASS_TOP: return "long_grass_top";
    case B_CATTAIL: return "cattail";
    case B_CACTUS_FLOWER: return "cactus_flower";
    case B_FIREFLY_BUSH: return "firefly_bush";
    case B_HEATHER: return "heather";
    default: return nullptr;
    }
}
inline bool loadPlantGrid(const char* name, int* grid) {
    std::ifstream f(assetPath(std::string("models/plants/") + name + ".txt"));
    if (!f) return false;
    std::map<char, int> key;
    std::string line;
    while (std::getline(f, line) && !line.empty()) {
        if (line.size() < 3) continue;
        int t = textureByName(line.substr(2));
        if (t >= 0) key[line[0]] = t;
    }
    for (int y = 0; y < 8; ++y) {
        for (int z = 0; z < 8; ++z) {
            if (!std::getline(f, line)) return false;
            for (int x = 0; x < 8 && x < int(line.size()); ++x) {
                auto k = key.find(line[size_t(x)]);
                if (k != key.end()) grid[(y * 8 + z) * 8 + x] = k->second;
            }
        }
        std::getline(f, line);
    }
    return true;
}
inline const std::vector<PlantFace>& plantGeometry(BlockId id) {
    static const auto cache = [] {
        std::array<std::vector<PlantFace>, 256> result;
        for (int id = 1; id < B_COUNT; ++id) {
            const char* name = plantModelName(id);
            if (!name) continue;
            int grid[8 * 8 * 8];
            std::fill(std::begin(grid), std::end(grid), -1);
            auto at = [&](int x, int y, int z) -> int& { return grid[(y * 8 + z) * 8 + x]; };
            if (!loadPlantGrid(name, grid)) continue;
            const int D[] = {0, 0, 1, 1, 2, 2}, U[] = {2, 2, 0, 0, 0, 0}, V[] = {1, 1, 2, 2, 1, 1};
            for (int f = 0; f < 6; ++f)
                for (int slice = 0; slice < 8; ++slice) {
                    int mask[64];
                    bool pos = (f & 1) == 0;
                    int d = D[f], u = U[f], v = V[f];
                    for (int j = 0; j < 8; ++j)
                        for (int i = 0; i < 8; ++i) {
                            int p[3];
                            p[d] = slice;
                            p[u] = i;
                            p[v] = j;
                            int t = at(p[0], p[1], p[2]);
                            p[d] += pos ? 1 : -1;
                            mask[j * 8 + i] = t >= 0 && (p[d] < 0 || p[d] >= 8 || at(p[0], p[1], p[2]) < 0) ? t : -1;
                        }
                    for (int j = 0; j < 8; ++j)
                        for (int i = 0; i < 8;) {
                            int t = mask[j * 8 + i];
                            if (t < 0) {
                                ++i;
                                continue;
                            }
                            int w = 1, h = 1;
                            while (i + w < 8 && mask[j * 8 + i + w] == t) ++w;
                            bool more = true;
                            while (j + h < 8 && more) {
                                for (int k = 0; k < w; ++k)
                                    if (mask[(j + h) * 8 + i + k] != t) more = false;
                                if (more) ++h;
                            }
                            const int corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                            int order[4] = {0, 1, 2, 3};
                            if ((d == 2) != pos) {
                                order[1] = 3;
                                order[3] = 1;
                            }
                            PlantFace face;
                            face.face = f;
                            face.tex = t;
                            for (int k = 0; k < 4; ++k) {
                                int p[3], c = order[k];
                                p[d] = slice + int(pos);
                                p[u] = i + corners[c][0] * w;
                                p[v] = j + corners[c][1] * h;
                                face.p[k] = vec3(float(p[0]), float(p[1]), float(p[2])) / 8.0f;
                            }
                            result[id].push_back(face);
                            for (int y = 0; y < h; ++y)
                                for (int x = 0; x < w; ++x) mask[(j + y) * 8 + i + x] = -1;
                            i += w;
                        }
                }
        }
        return result;
    }();
    return cache[id];
}
}
