#include "world/Collide.h"
#include <algorithm>
#include <cmath>

namespace qc {
namespace {
constexpr float EPS = 1e-4f;

float clipAxis(const World& w, const AABB& b, int axis, float d) {
    if (d == 0) return 0;
    AABB sweep = b;
    if (d > 0)
        sweep.max[axis] += d;
    else
        sweep.min[axis] += d;
    int x0 = cellOf(sweep.min.x + EPS), x1 = cellOf(sweep.max.x - EPS);
    int y0 = cellOf(sweep.min.y + EPS), y1 = cellOf(sweep.max.y - EPS);
    int z0 = cellOf(sweep.min.z + EPS), z1 = cellOf(sweep.max.z - EPS);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                if (!w.collidesAt(x, y, z)) continue;
                vec3 cmin{x * VOX, y * VOX, z * VOX}, cmax = cmin + vec3(VOX);
                bool ok = true;
                for (int a = 0; a < 3 && ok; ++a)
                    if (a != axis && !(b.min[a] < cmax[a] - EPS && b.max[a] > cmin[a] + EPS)) ok = false;
                if (!ok) continue;
                if (d > 0 && cmin[axis] >= b.max[axis] - EPS)
                    d = std::min(d, cmin[axis] - b.max[axis]);
                else if (d < 0 && cmax[axis] <= b.min[axis] + EPS)
                    d = std::max(d, cmax[axis] - b.min[axis]);
            }
    return d;
}

MoveResult moveRaw(const World& w, AABB& box, vec3 delta) {
    MoveResult r;
    float dy = clipAxis(w, box, 1, delta.y);
    box = box.offset({0, dy, 0});
    float dx = clipAxis(w, box, 0, delta.x);
    box = box.offset({dx, 0, 0});
    float dz = clipAxis(w, box, 2, delta.z);
    box = box.offset({0, 0, dz});
    r.hitY = dy != delta.y;
    r.hitX = dx != delta.x;
    r.hitZ = dz != delta.z;
    r.onGround = r.hitY && delta.y < 0;
    r.actual = {dx, dy, dz};
    return r;
}
}

bool boxHitsWorld(const World& w, const AABB& b) {
    for (int y = cellOf(b.min.y + EPS); y <= cellOf(b.max.y - EPS); ++y)
        for (int z = cellOf(b.min.z + EPS); z <= cellOf(b.max.z - EPS); ++z)
            for (int x = cellOf(b.min.x + EPS); x <= cellOf(b.max.x - EPS); ++x)
                if (w.collidesAt(x, y, z)) return true;
    return false;
}

MoveResult moveBox(const World& w, AABB& box, vec3 delta, float step, bool wasOnGround) {
    AABB start = box;
    MoveResult r = moveRaw(w, box, delta);
    if (step > 0 && (wasOnGround || r.onGround) && (r.hitX || r.hitZ)) {
        AABB b2 = start;
        float up = clipAxis(w, b2, 1, step);
        b2 = b2.offset({0, up, 0});
        float dx = clipAxis(w, b2, 0, delta.x);
        b2 = b2.offset({dx, 0, 0});
        float dz = clipAxis(w, b2, 2, delta.z);
        b2 = b2.offset({0, 0, dz});
        float down = clipAxis(w, b2, 1, -up + std::min(delta.y, 0.0f));
        b2 = b2.offset({0, down, 0});
        float h1 = r.actual.x * r.actual.x + r.actual.z * r.actual.z;
        float h2 = dx * dx + dz * dz;
        if (h2 > h1 + 1e-6f) {
            MoveResult r2;
            r2.actual = b2.min - start.min;
            r2.hitX = dx != delta.x;
            r2.hitZ = dz != delta.z;
            r2.hitY = true;
            r2.onGround = true;
            box = b2;
            return r2;
        }
    }
    return r;
}

FluidInfo fluidAt(const World& w, const AABB& b, float eyeY) {
    FluidInfo f;
    float hgt = std::max(b.max.y - b.min.y, 0.01f);
    float waterH = 0, lavaH = 0;
    int x0 = cellOf(b.min.x), x1 = cellOf(b.max.x - EPS), z0 = cellOf(b.min.z), z1 = cellOf(b.max.z - EPS);
    int cells = 0;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            ++cells;
            float wcol = 0, lcol = 0;
            for (int y = cellOf(b.min.y); y <= cellOf(b.max.y - EPS); ++y) {
                uint8_t blk = w.get(x, y, z);
                if (!def(blk).fluid) continue;
                float top = y * VOX + fluidSurfaceAmountOf(blk, w.metaAt(x, y, z)) / float(MAX_FLUID) * VOX;
                if (def(w.get(x, y + 1, z)).fluid) top = (y + 1) * VOX;
                float lo = std::max(b.min.y, y * VOX), hi = std::min(b.max.y, top);
                if (hi > lo) (blk == B_WATER ? wcol : lcol) += hi - lo;
                if (blk == B_WATER && eyeY >= y * VOX && eyeY < top) f.headInWater = true;
                vec3 current = w.waterCurrent(x, y, z);
                f.flow += vec3(current.x, 0, current.z);
            }
            waterH += wcol;
            lavaH += lcol;
        }
    if (cells) {
        f.waterFraction = std::min(waterH / cells / hgt, 1.0f);
        f.lavaFraction = std::min(lavaH / cells / hgt, 1.0f);
        f.flow = f.flow / float(cells);
    }
    return f;
}

float leafFraction(const World& w, const AABB& b, uint8_t* leaf) {
    int n = 0, in = 0;
    for (int y = cellOf(b.min.y); y <= cellOf(b.max.y - 0.001f); ++y)
        for (int z = cellOf(b.min.z); z <= cellOf(b.max.z - 0.001f); ++z)
            for (int x = cellOf(b.min.x); x <= cellOf(b.max.x - 0.001f); ++x) {
                ++n;
                uint8_t c = w.get(x, y, z);
                if (def(c).render == Render::Leaves) {
                    ++in;
                    if (leaf) *leaf = c;
                }
            }
    return n ? float(in) / float(n) : 0.0f;
}

RayHit raycast(const World& w, vec3 o, vec3 d, float maxDist, bool stopAtFluids) {
    RayHit h;
    d = normalize(d);
    vec3 p = o / VOX;
    int x = int(std::floor(p.x)), y = int(std::floor(p.y)), z = int(std::floor(p.z));
    int sx = d.x > 0 ? 1 : -1, sy = d.y > 0 ? 1 : -1, sz = d.z > 0 ? 1 : -1;
    auto tdelta = [](float v) { return v != 0 ? std::fabs(1.0f / v) : 1e30f; };
    float tdx = tdelta(d.x), tdy = tdelta(d.y), tdz = tdelta(d.z);
    auto first = [](float pc, float v, int cell) {
        if (v > 0) return (float(cell + 1) - pc) / v;
        if (v < 0) return (pc - float(cell)) / -v;
        return 1e30f;
    };
    float tx = first(p.x, d.x, x), ty = first(p.y, d.y, y), tz = first(p.z, d.z, z);
    float maxT = maxDist / VOX, t = 0;
    int nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < 512 && t <= maxT; ++i) {
        uint8_t b = w.get(x, y, z);
        const BlockDef& bd = def(b);
        bool target = b != B_AIR && (!bd.fluid || stopAtFluids);
        if (target) {
            h.hit = true;
            h.x = x;
            h.y = y;
            h.z = z;
            h.nx = nx;
            h.ny = ny;
            h.nz = nz;
            h.dist = t * VOX;
            h.block = b;
            return h;
        }
        if (tx < ty && tx < tz) {
            x += sx;
            t = tx;
            tx += tdx;
            nx = -sx;
            ny = 0;
            nz = 0;
        } else if (ty < tz) {
            y += sy;
            t = ty;
            ty += tdy;
            nx = 0;
            ny = -sy;
            nz = 0;
        } else {
            z += sz;
            t = tz;
            tz += tdz;
            nx = 0;
            ny = 0;
            nz = -sz;
        }
        if (y < -1 || y > WH) break;
    }
    return h;
}

}
