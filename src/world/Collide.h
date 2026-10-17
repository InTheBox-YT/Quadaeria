#pragma once
#include "core/Math.h"
#include "world/World.h"

namespace qc {
struct AABB {
    vec3 min, max;
    AABB offset(vec3 d) const { return {min + d, max + d}; }
    bool overlaps(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z && max.z > o.min.z;
    }
    vec3 center() const { return (min + max) * 0.5f; }
};
inline AABB boxAt(vec3 feet, float w, float h) {
    return {{feet.x - w * 0.5f, feet.y, feet.z - w * 0.5f}, {feet.x + w * 0.5f, feet.y + h, feet.z + w * 0.5f}};
}
inline int cellOf(float m) { return int(std::floor(m / VOX)); }

struct MoveResult {
    bool hitX = false, hitY = false, hitZ = false, onGround = false;
    vec3 actual;
};

bool boxHitsWorld(const World& w, const AABB& b);
MoveResult moveBox(const World& w, AABB& box, vec3 delta, float stepHeight, bool wasOnGround);

struct FluidInfo {
    float waterFraction = 0;
    float lavaFraction = 0;
    vec3 flow;
    bool headInWater = false;
};
FluidInfo fluidAt(const World& w, const AABB& b, float eyeY);
float leafFraction(const World& w, const AABB& b, uint8_t* leaf = nullptr);

struct RayHit {
    bool hit = false;
    int x = 0, y = 0, z = 0;
    int nx = 0, ny = 0, nz = 0;
    float dist = 0;
    uint8_t block = 0;
};
RayHit raycast(const World& w, vec3 origin, vec3 dir, float maxDist, bool stopAtFluids = false);

}
