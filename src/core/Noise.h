#pragma once
#include <cmath>
#include <cstdint>
#include "core/Base.h"

namespace qc {
class Noise {
public:
    explicit Noise(uint64_t seed = 1) { reseed(seed); }
    void reseed(uint64_t seed) {
        for (int i = 0; i < 256; ++i) p_[i] = uint8_t(i);
        uint64_t s = seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
        for (int i = 255; i > 0; --i) {
            s ^= s >> 31;
            s *= 0xBF58476D1CE4E5B9ull;
            s ^= s >> 27;
            int j = int(s % uint64_t(i + 1));
            uint8_t t = p_[i];
            p_[i] = p_[j];
            p_[j] = t;
        }
        for (int i = 0; i < 256; ++i) p_[256 + i] = p_[i];
    }

    float noise2(float x, float y) const {
        const float F2 = 0.366025403f, G2 = 0.211324865f;
        float s = (x + y) * F2;
        int i = fastfloor(x + s), j = fastfloor(y + s);
        float t = float(i + j) * G2;
        float x0 = x - (float(i) - t), y0 = y - (float(j) - t);
        int i1 = x0 > y0 ? 1 : 0, j1 = 1 - i1;
        float x1 = x0 - float(i1) + G2, y1 = y0 - float(j1) + G2;
        float x2 = x0 - 1.0f + 2.0f * G2, y2 = y0 - 1.0f + 2.0f * G2;
        int ii = i & 255, jj = j & 255;
        float n = 0;
        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 > 0) {
            t0 *= t0;
            n += t0 * t0 * grad2(p_[ii + p_[jj]], x0, y0);
        }
        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 > 0) {
            t1 *= t1;
            n += t1 * t1 * grad2(p_[ii + i1 + p_[jj + j1]], x1, y1);
        }
        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 > 0) {
            t2 *= t2;
            n += t2 * t2 * grad2(p_[ii + 1 + p_[jj + 1]], x2, y2);
        }
        return 70.0f * n;
    }

    float noise3(float x, float y, float z) const {
        const float F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
        float s = (x + y + z) * F3;
        int i = fastfloor(x + s), j = fastfloor(y + s), k = fastfloor(z + s);
        float t = float(i + j + k) * G3;
        float x0 = x - (float(i) - t), y0 = y - (float(j) - t), z0 = z - (float(k) - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0) {
                i1 = 1;
                j1 = 0;
                k1 = 0;
                i2 = 1;
                j2 = 1;
                k2 = 0;
            } else if (x0 >= z0) {
                i1 = 1;
                j1 = 0;
                k1 = 0;
                i2 = 1;
                j2 = 0;
                k2 = 1;
            } else {
                i1 = 0;
                j1 = 0;
                k1 = 1;
                i2 = 1;
                j2 = 0;
                k2 = 1;
            }
        } else {
            if (y0 < z0) {
                i1 = 0;
                j1 = 0;
                k1 = 1;
                i2 = 0;
                j2 = 1;
                k2 = 1;
            } else if (x0 < z0) {
                i1 = 0;
                j1 = 1;
                k1 = 0;
                i2 = 0;
                j2 = 1;
                k2 = 1;
            } else {
                i1 = 0;
                j1 = 1;
                k1 = 0;
                i2 = 1;
                j2 = 1;
                k2 = 0;
            }
        }
        float x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
        float x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3;
        float x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;
        int ii = i & 255, jj = j & 255, kk = k & 255;
        float n = 0;
        auto corner = [&](float cx, float cy, float cz, int h) {
            float tt = 0.6f - cx * cx - cy * cy - cz * cz;
            if (tt > 0) {
                tt *= tt;
                n += tt * tt * grad3(h, cx, cy, cz);
            }
        };
        corner(x0, y0, z0, p_[ii + p_[jj + p_[kk]]]);
        corner(x1, y1, z1, p_[ii + i1 + p_[jj + j1 + p_[kk + k1]]]);
        corner(x2, y2, z2, p_[ii + i2 + p_[jj + j2 + p_[kk + k2]]]);
        corner(x3, y3, z3, p_[ii + 1 + p_[jj + 1 + p_[kk + 1]]]);
        return 32.0f * n;
    }

    float fbm2(float x, float y, int oct, float lac = 2.0f, float gain = 0.5f) const {
        float a = 1, f = 1, sum = 0, norm = 0;
        for (int o = 0; o < oct; ++o) {
            sum += a * noise2(x * f, y * f);
            norm += a;
            a *= gain;
            f *= lac;
        }
        return sum / norm;
    }
    float fbm3(float x, float y, float z, int oct) const {
        float a = 1, f = 1, sum = 0, norm = 0;
        for (int o = 0; o < oct; ++o) {
            sum += a * noise3(x * f, y * f, z * f);
            norm += a;
            a *= 0.5f;
            f *= 2.0f;
        }
        return sum / norm;
    }

private:
    static int fastfloor(float v) {
        int i = int(v);
        return v < float(i) ? i - 1 : i;
    }
    static float grad2(int h, float x, float y) {
        switch (h & 7) {
        case 0: return x + y;
        case 1: return x - y;
        case 2: return -x + y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
        }
    }
    static float grad3(int h, float x, float y, float z) {
        switch (h & 15) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x + z;
        case 5: return -x + z;
        case 6: return x - z;
        case 7: return -x - z;
        case 8: return y + z;
        case 9: return -y + z;
        case 10: return y - z;
        case 11: return -y - z;
        case 12: return x + y;
        case 13: return -x + y;
        case 14: return -y + z;
        default: return -y - z;
        }
    }
    uint8_t p_[512];
};

inline uint32_t hash3i(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed ^ 0x9E3779B9u;
    h ^= uint32_t(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= uint32_t(y) * 0xC2B2AE35u;
    h = (h << 17) | (h >> 15);
    h ^= uint32_t(z) * 0x27D4EB2Fu;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}
inline float hashf(int x, int y, int z, uint32_t seed) { return float(hash3i(x, y, z, seed) >> 8) * (1.0f / 16777216.0f); }

}
