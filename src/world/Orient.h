#pragma once
#include "world/Block.h"
#include <cstdint>
#include <cstdlib>

namespace qc {
struct Orient {
    int8_t m[3][3];
};

namespace detail {
struct OrientTable {
    Orient o[24];
    int compose[24][24];
    OrientTable() {
        const int8_t fixed[3][3][3] = {
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
            {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}},
            {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        };
        int n = 0;
        for (int k = 0; k < 3; ++k) {
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) o[n].m[r][c] = fixed[k][r][c];
            ++n;
        }
        const int perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        for (auto& p : perms)
            for (int s = 0; s < 8; ++s) {
                Orient t{};
                for (int c = 0; c < 3; ++c) t.m[p[c]][c] = (s >> c) & 1 ? -1 : 1;
                int det = t.m[0][0] * (t.m[1][1] * t.m[2][2] - t.m[1][2] * t.m[2][1]) -
                          t.m[0][1] * (t.m[1][0] * t.m[2][2] - t.m[1][2] * t.m[2][0]) +
                          t.m[0][2] * (t.m[1][0] * t.m[2][1] - t.m[1][1] * t.m[2][0]);
                if (det != 1) continue;
                bool dup = false;
                for (int i = 0; i < n && !dup; ++i) {
                    bool same = true;
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c) same = same && o[i].m[r][c] == t.m[r][c];
                    dup = same;
                }
                if (!dup && n < 24) o[n++] = t;
            }
        for (int a = 0; a < 24; ++a)
            for (int b = 0; b < 24; ++b) {
                int8_t m[3][3];
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c) {
                        int v = 0;
                        for (int k = 0; k < 3; ++k) v += o[a].m[r][k] * o[b].m[k][c];
                        m[r][c] = int8_t(v);
                    }
                compose[a][b] = find(m);
            }
    }
    int find(const int8_t m[3][3]) const {
        for (int i = 0; i < 24; ++i) {
            bool same = true;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) same = same && o[i].m[r][c] == m[r][c];
            if (same) return i;
        }
        return 0;
    }
};
inline const OrientTable& orientTable() {
    static OrientTable t;
    return t;
}
}

inline const Orient& orient(int i) { return detail::orientTable().o[(i >= 0 && i < 24) ? i : 0]; }
inline int orientCompose(int a, int b) { return detail::orientTable().compose[a % 24][b % 24]; }
inline int orientFind(const int8_t m[3][3]) { return detail::orientTable().find(m); }

inline bool orientable(BlockId b) {
    const BlockDef& d = def(b);
    return (d.render == Render::Opaque || d.render == Render::Glass || d.render == Render::Cutout) && !d.fluid && b != B_TNT &&
           !(b >= B_CABLE && b <= B_SPARKGAP);
}
inline int metaOrient(uint8_t meta) {
    int o = meta & 31;
    return o < 24 ? o : 0;
}
inline int metaPhase(uint8_t meta, int wx, int wy, int wz) { return (meta >> 5) ^ ((wx & 1) | ((wy & 1) << 1) | ((wz & 1) << 2)); }
inline uint8_t makeOrientMeta(int o, int phase, int wx, int wy, int wz) {
    int p = phase ^ ((wx & 1) | ((wy & 1) << 1) | ((wz & 1) << 2));
    return uint8_t((o & 31) | (p << 5));
}

inline uint8_t axisMeta(int o, int wx, int wy, int wz) {
    const Orient& O = orient(o);
    int q[3] = {wx & 1, wy & 1, wz & 1}, phase = 0;
    for (int axis = 0; axis < 3; ++axis) {
        int v = 0;
        for (int k = 0; k < 3; ++k) v += (2 * q[k] - 1) * O.m[k][axis];
        phase |= ((v + 1) / 2) << axis;
    }
    return makeOrientMeta(o, phase, wx, wy, wz);
}

inline void textureTileOrigin(BlockId b, uint8_t m, int x, int y, int z, int out[3]) {
    int ph = metaPhase(m, x, y, z);
    const auto& O = orient(b == B_TNT ? 0 : metaOrient(m));
    int p[3] = {x, y, z};
    for (int w = 0; w < 3; ++w) {
        int bit = 0;
        for (int l = 0; l < 3; ++l)
            if (O.m[w][l]) bit = O.m[w][l] > 0 ? ((ph >> l) & 1) : 1 - ((ph >> l) & 1);
        out[w] = p[w] - bit;
    }
}

}
