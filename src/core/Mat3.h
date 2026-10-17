#pragma once
#include "core/Math.h"

namespace qc {
struct mat3 {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static mat3 zero() {
        mat3 r;
        for (auto& row : r.m)
            for (float& v : row) v = 0;
        return r;
    }
    vec3 operator*(vec3 v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    mat3 operator*(const mat3& b) const {
        mat3 r = zero();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) r.m[i][j] += m[i][k] * b.m[k][j];
        return r;
    }
    mat3 transpose() const {
        mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i][j] = m[j][i];
        return r;
    }
    mat3 inverse() const {
        const auto& a = m;
        float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                    a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        mat3 r = zero();
        if (std::fabs(det) < 1e-20f) return r;
        float id = 1.0f / det;
        r.m[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * id;
        r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id;
        r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id;
        r.m[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * id;
        r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id;
        r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id;
        r.m[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * id;
        r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id;
        r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id;
        return r;
    }
};

inline mat3 toMat3(quat q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    mat3 r;
    r.m[0][0] = 1 - 2 * (y * y + z * z);
    r.m[0][1] = 2 * (x * y - z * w);
    r.m[0][2] = 2 * (x * z + y * w);
    r.m[1][0] = 2 * (x * y + z * w);
    r.m[1][1] = 1 - 2 * (x * x + z * z);
    r.m[1][2] = 2 * (y * z - x * w);
    r.m[2][0] = 2 * (x * z - y * w);
    r.m[2][1] = 2 * (y * z + x * w);
    r.m[2][2] = 1 - 2 * (x * x + y * y);
    return r;
}

}
