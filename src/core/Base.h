#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define QC_LOG(...)                                                                                                                        \
    do {                                                                                                                                   \
        std::fputs("[quadaeria] ", stdout);                                                                                                \
        std::fprintf(stdout, __VA_ARGS__);                                                                                                 \
        std::fputc('\n', stdout);                                                                                                          \
        std::fflush(stdout);                                                                                                               \
    } while (0)

#define QC_ERROR(...)                                                                                                                      \
    do {                                                                                                                                   \
        std::fputs("[quadaeria:error] ", stderr);                                                                                          \
        std::fprintf(stderr, __VA_ARGS__);                                                                                                 \
        std::fputc('\n', stderr);                                                                                                          \
    } while (0)

namespace qc {
inline double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return uint32_t((s * 0x2545F4914F6CDD1Dull) >> 32);
    }
    float uniform() { return float(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int rangeInt(int a, int b) { return a + int(next() % uint32_t(b - a + 1)); }
};

}
