#include "world/Gen.h"
#include "world/Orient.h"
#include "core/Math.h"
#include <algorithm>
#include <cmath>
namespace qc {
bool Generator::landmarkSite(int rx, int rz, LandmarkSite& site) const {
    if (settings_.structures <= 0.01f) return false;
    uint32_t h = hash3i(rx, 819, rz, uint32_t(seed_));
    int limit = std::clamp(int(std::lround(105.0f * settings_.structures)) - 1, 0, 255);
    if (int(h & 255) > limit) return false;
    int x = rx * 128 + 28 + int((h >> 8) % 70), z = rz * 128 + 28 + int((h >> 16) % 70);
    SurfaceInfo s = surface(x, z);
    if (s.height < SEA + 4 || s.height > WH - 32) return false;
    for (int d : {-12, 12})
        if (abs(surface(x + d, z).height - s.height) > 8 || abs(surface(x, z + d).height - s.height) > 8) return false;
    int kind = int((h >> 24) % 5);
    if (((h >> 7) & 3) == 0) kind = 5 + int((h >> 26) % 4);
    site = {x, z, kind, h, s};
    return true;
}

void Generator::placeLandmarks(Column& c) const {
    constexpr int R = 128;
    const int cx0 = c.cx * CS, cz0 = c.cz * CS;
    for (int rz = floorDiv(cz0 - 20, R); rz <= floorDiv(cz0 + CS + 20, R); ++rz)
        for (int rx = floorDiv(cx0 - 20, R); rx <= floorDiv(cx0 + CS + 20, R); ++rx) {
            LandmarkSite site;
            if (!landmarkSite(rx, rz, site)) continue;
            uint32_t h = site.hash;
            int x0 = site.x, z0 = site.z;
            if (x0 + 18 < cx0 || x0 - 18 >= cx0 + CS || z0 + 18 < cz0 || z0 - 18 >= cz0 + CS) continue;
            SurfaceInfo s = site.surface;
            int ground = s.height;
            bool dry = s.biome == Biome::Desert || s.biome == Biome::Badlands;
            int kind = site.kind;
            uint8_t rock = dry ? B_RED_SANDSTONE : B_MOSSY;
            auto put = [&](int x, int y, int z, uint8_t b, uint8_t meta = 0) {
                if (x < cx0 || x >= cx0 + CS || z < cz0 || z >= cz0 + CS || y < 1 || y >= WH) return;
                auto& sec = c.sec[y >> 5];
                int i = idx3(x & 31, y & 31, z & 31);
                sec.setBlock(i, b);
                sec.setMeta(i, meta);
            };
            if (kind == 0) {
                for (int x = -12; x <= 12; ++x)
                    for (int z = -2; z <= 2; ++z) {
                        int top = ground + 7 + int(6 * sqrt(std::max(0.0f, 1 - float(x * x) / 144)));
                        int low = abs(x) > 9 ? ground - 8 : top - 3;
                        for (int y = low; y <= top; ++y) put(x0 + x, y, z0 + z, (x + y + z) % 13 == 0 ? uint8_t(B_STONE) : rock);
                    }
            } else if (kind == 1) {
                for (int j = 0; j < 6; ++j) {
                    float a = j * PI / 3;
                    int x = x0 + int(cos(a) * 9), z = z0 + int(sin(a) * 9);
                    int y = surface(x, z).height;
                    int height = 3 + int(hash3i(x, j, z, h) % 8);
                    for (int dy = -2; dy < height; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (dy > height - 3 && abs(dx) + abs(dz) > 0) continue;
                                uint8_t b = kind == 1 && s.temp < 0.4f ? B_PACKED_ICE : (dy == height - 1 && !dry ? B_CRYSTAL : B_CALCITE);
                                put(x + dx, y + dy, z + dz, b);
                            }
                }
            } else if (kind == 2) {
                if (dry) continue;
                for (int x = -12; x <= 12; ++x) {
                    int y = ground + 2 + int(x > 4);
                    int z = z0 + int(x / 5);
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz) {
                            int wx = x0 + x, wy = y + dy, wz = z + dz;
                            const Orient& O = orient(1);
                            int ph = 0, q[3] = {wx & 1, wy & 1, wz & 1};
                            for (int ax = 0; ax < 3; ++ax) {
                                int v = 0;
                                for (int k = 0; k < 3; ++k) v += (2 * q[k] - 1) * O.m[k][ax];
                                ph |= ((v + 1) / 2) << ax;
                            }
                            put(wx, wy, wz, B_LOG, makeOrientMeta(1, ph, wx, wy, wz));
                        }
                    if (x % 5 == 0) put(x0 + x, ground, z0 - 3, B_GLOWSHROOM);
                }
            } else if (kind == 3) {
                bool hot = ((h >> 12) & 3) == 0;
                if (hot) rock = B_TRAVERTINE;
                for (int x = -8; x <= 8; ++x)
                    for (int z = -8; z <= 8; ++z) {
                        int rr = x * x + z * z;
                        if (rr > 64) continue;
                        for (int y = ground - 5; y <= ground + 2; ++y) {
                            uint8_t b;
                            if (rr > 44)
                                b = y < ground ? (hot ? uint8_t(B_TRAVERTINE) : uint8_t(B_CLAY)) : y == ground ? rock : uint8_t(B_AIR);
                            else if (y < ground - 2)
                                b = B_CLAY;
                            else
                                b = y < ground ? uint8_t(B_WATER) : uint8_t(B_AIR);
                            put(x0 + x, y, z0 + z, b, b == B_WATER ? FLUID_SOURCE_META : 0);
                        }
                    }
                if (hot) put(x0, ground - 3, z0, B_GEYSER);
            } else if (kind == 5) {
                for (int x = -4; x <= 4; ++x)
                    for (int z = -4; z <= 4; ++z) {
                        int edge = std::max(std::abs(x), std::abs(z));
                        for (int y = ground - 5; y <= ground + 2; ++y) {
                            uint8_t block =
                                edge <= 2 ? (y < ground ? uint8_t(B_WATER) : uint8_t(B_AIR)) : (y <= ground + 1 ? rock : uint8_t(B_AIR));
                            if (y == ground - 5) block = B_CLAY;
                            if (edge == 4 && y > ground - 1) continue;
                            put(x0 + x, y, z0 + z, block, block == B_WATER ? FLUID_SOURCE_META : 0);
                        }
                    }
                for (int x : {-4, 4})
                    for (int y = ground + 1; y < ground + 9; ++y) put(x0 + x, y, z0, B_LOG);
                for (int x = -5; x <= 5; ++x)
                    for (int z = -3; z <= 3; ++z)
                        if (hash3i(x, 7, z, h) % 9 != 0) put(x0 + x, ground + 9 - std::abs(z) / 2, z0 + z, B_PLANKS);
            } else if (kind == 6) {
                for (int x = -6; x <= 6; ++x)
                    for (int z = -6; z <= 6; ++z) {
                        int edge = std::max(std::abs(x), std::abs(z));
                        int height = 12 + int(hash3i(x / 2, 33, z / 2, h) % 9);
                        for (int y = ground - 4; y < ground + height; ++y) {
                            uint8_t block = B_AIR;
                            if (y < ground)
                                block = rock;
                            else if (edge >= 5)
                                block = (z == -6 && std::abs(x) < 2 && y < ground + 6) ? uint8_t(B_AIR)
                                        : dry                                          ? uint8_t(B_SANDSTONE)
                                                                                       : uint8_t(B_STONE_BRICKS);
                            else if (y == ground + 8 && z > 0)
                                block = B_PLANKS;
                            if (y >= ground && edge < 5 && block == B_AIR) continue;
                            put(x0 + x, y, z0 + z, block);
                        }
                    }
                for (int k = 0; k < 9; ++k)
                    for (int x = -4; x <= -2; ++x)
                        for (int y = ground; y <= ground + k; ++y) put(x0 + x, y, z0 - 4 + k, rock);
                put(x0 - 3, ground + 10, z0 + 3, B_LANTERN);
            } else if (kind == 7) {
                for (int x = -8; x <= 8; ++x)
                    for (int z = -5; z <= 5; ++z) {
                        for (int y = ground - 5; y < ground; ++y) put(x0 + x, y, z0 + z, rock);
                        if (z == 5 || x == -8)
                            for (int y = 0; y < 4 + int(hash3i(x, 71, z, h) % 5); ++y) put(x0 + x, ground + y, z0 + z, rock);
                        if (x < 2 && hash3i(x, 18, z, h) % 11) put(x0 + x, ground + 10 - std::abs(z) / 2, z0 + z, B_PLANKS);
                    }
                for (int x : {-8, 0})
                    for (int z : {-5, 5})
                        for (int y = 0; y < 10; ++y) put(x0 + x, ground + y, z0 + z, B_LOG);
                for (int x = 3; x < 7; ++x) put(x0 + x, ground, z0 + 3, B_PLANKS);
            } else if (kind == 8) {
                for (int j = 0; j < 5; ++j) {
                    uint32_t q = hash3i(x0, j, z0, h);
                    int x = x0 + int(q % 23) - 11, z = z0 + int((q >> 8) % 23) - 11;
                    int base = surface(x, z).height, rx = 3 + int((q >> 16) % 3), rz = 3 + int((q >> 20) % 3), ry = 3 + int((q >> 24) % 5);
                    for (int dy = -3; dy <= ry; ++dy)
                        for (int dz = -rz; dz <= rz; ++dz)
                            for (int dx = -rx; dx <= rx; ++dx)
                                if (float(dx * dx) / (rx * rx) + float(dz * dz) / (rz * rz) + float(dy * dy) / (ry * ry) < 1.15f)
                                    put(x + dx, base + dy, z + dz,
                                        dry                            ? uint8_t(B_RED_SANDSTONE)
                                        : s.biome == Biome::ChalkDowns ? uint8_t(B_CALCITE)
                                        : dy > ry - 2 && s.humid > .5f ? uint8_t(B_MOSS)
                                                                       : uint8_t(B_ANDESITE));
                }
            } else {
                for (int x : {-8, 0, 8}) {
                    int y = surface(x0 + x, z0).height, ht = x == 8 ? 5 : 12;
                    for (int dy = -3; dy < ht; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            for (int dz = -1; dz <= 1; ++dz)
                                put(x0 + x + dx, y + dy, z0 + dz, dry ? uint8_t(B_SANDSTONE) : uint8_t(B_STONE_BRICKS));
                }
                for (int x = -8; x <= 1; ++x)
                    for (int z = -1; z <= 1; ++z) put(x0 + x, ground + 11, z0 + z, rock);
            }
        }
}
}
