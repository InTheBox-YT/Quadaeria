#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace qc {
constexpr float PI = 3.14159265358979323846f;
constexpr float DEG2RAD = PI / 180.0f;

struct vec2 {
    float x = 0, y = 0;
    constexpr vec2() = default;
    constexpr vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr vec2 operator+(vec2 b) const { return {x + b.x, y + b.y}; }
    constexpr vec2 operator-(vec2 b) const { return {x - b.x, y - b.y}; }
    constexpr vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr vec2 operator*(vec2 b) const { return {x * b.x, y * b.y}; }
    constexpr vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr vec2 operator-() const { return {-x, -y}; }
    vec2& operator+=(vec2 b) {
        x += b.x;
        y += b.y;
        return *this;
    }
    vec2& operator-=(vec2 b) {
        x -= b.x;
        y -= b.y;
        return *this;
    }
    vec2& operator*=(float s) {
        x *= s;
        y *= s;
        return *this;
    }
    bool operator==(vec2 b) const { return x == b.x && y == b.y; }
    bool operator!=(vec2 b) const { return !(*this == b); }
};
constexpr vec2 operator*(float s, vec2 v) { return v * s; }
inline float length(vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

struct vec3 {
    float x = 0, y = 0, z = 0;
    constexpr vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit constexpr vec3(float s) : x(s), y(s), z(s) {}
    constexpr vec3 operator+(vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    constexpr vec3 operator-(vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    constexpr vec3 operator*(vec3 b) const { return {x * b.x, y * b.y, z * b.z}; }
    constexpr vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr vec3 operator/(float s) const {
        float i = 1.0f / s;
        return {x * i, y * i, z * i};
    }
    constexpr vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(vec3 b) {
        x += b.x;
        y += b.y;
        z += b.z;
        return *this;
    }
    vec3& operator-=(vec3 b) {
        x -= b.x;
        y -= b.y;
        z -= b.z;
        return *this;
    }
    vec3& operator*=(float s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};
constexpr vec3 operator*(float s, vec3 v) { return v * s; }
constexpr float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr vec3 cross(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
constexpr float lengthSq(vec3 v) { return dot(v, v); }
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(vec3 v, vec3 fallback = {0, 1, 0}) {
    float l2 = dot(v, v);
    return l2 > 1e-20f ? v * (1.0f / std::sqrt(l2)) : fallback;
}
constexpr vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline vec3 vmin(vec3 a, vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline vec3 vmax(vec3 a, vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline vec3 clamp(vec3 v, vec3 lo, vec3 hi) { return vmin(vmax(v, lo), hi); }
inline float maxComp(vec3 v) { return std::max(v.x, std::max(v.y, v.z)); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0, 1);
    return t * t * (3 - 2 * t);
}

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr vec4() = default;
    constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    explicit constexpr vec4(float s) : x(s), y(s), z(s), w(s) {}
    constexpr vec4(vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr vec3 xyz() const { return {x, y, z}; }
};

struct quat {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr quat() = default;
    constexpr quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};
constexpr quat operator*(quat a, quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline quat normalize(quat q) {
    float l2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (l2 < 1e-20f) return {};
    float i = 1.0f / std::sqrt(l2);
    return {q.x * i, q.y * i, q.z * i, q.w * i};
}
inline quat angleAxis(float angle, vec3 axis) {
    float s = std::sin(angle * 0.5f);
    axis = normalize(axis);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}
constexpr vec3 rotate(quat q, vec3 v) {
    vec3 u{q.x, q.y, q.z};
    vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
inline quat nlerp(quat a, quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float s = d < 0 ? -1.0f : 1.0f;
    return normalize(quat{a.x + (b.x * s - a.x) * t, a.y + (b.y * s - a.y) * t, a.z + (b.z * s - a.z) * t, a.w + (b.w * s - a.w) * t});
}
inline quat integrate(quat q, vec3 w, float dt) {
    quat dq = quat{w.x, w.y, w.z, 0} * q;
    float h = 0.5f * dt;
    return normalize(quat{q.x + dq.x * h, q.y + dq.y * h, q.z + dq.z * h, q.w + dq.w * h});
}

struct mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static mat4 identity() { return {}; }
    float& operator()(int row, int col) { return m[col * 4 + row]; }
    float operator()(int row, int col) const { return m[col * 4 + row]; }
};
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            r.m[c * 4 + rr] = a.m[0 * 4 + rr] * b.m[c * 4 + 0] + a.m[1 * 4 + rr] * b.m[c * 4 + 1] + a.m[2 * 4 + rr] * b.m[c * 4 + 2] +
                              a.m[3 * 4 + rr] * b.m[c * 4 + 3];
    return r;
}
inline vec4 operator*(const mat4& a, vec4 v) {
    return {a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w, a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w,
            a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w, a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w};
}
inline mat4 perspective(float fovY, float aspect, float n, float f) {
    mat4 r;
    float t = 1.0f / std::tan(fovY * 0.5f);
    for (float& v : r.m) v = 0;
    r.m[0] = t / aspect;
    r.m[5] = t;
    r.m[10] = (f + n) / (n - f);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * f * n / (n - f);
    return r;
}
inline mat4 ortho(float l, float r_, float b, float t, float n, float f) {
    mat4 r;
    r.m[0] = 2.0f / (r_ - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (f - n);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(f + n) / (f - n);
    return r;
}
inline mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up), {1, 0, 0});
    vec3 u = cross(s, f);
    mat4 r;
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}
inline mat4 inverse(const mat4& mat) {
    const float* m = mat.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];
    inv[8] =
        m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];
    inv[13] =
        m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] =
        m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] =
        -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] =
        m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] =
        -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] =
        -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] =
        m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] =
        -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    mat4 r;
    if (std::fabs(det) < 1e-30f) return r;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * det;
    return r;
}

struct Frustum {
    vec4 planes[6];
    static Frustum fromMatrix(const mat4& vp) {
        Frustum f;
        auto row = [&](int i) { return vec4{vp.m[i], vp.m[4 + i], vp.m[8 + i], vp.m[12 + i]}; };
        vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto add = [](vec4 a, vec4 b) { return vec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; };
        auto sub = [](vec4 a, vec4 b) { return vec4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; };
        f.planes[0] = add(r3, r0);
        f.planes[1] = sub(r3, r0);
        f.planes[2] = add(r3, r1);
        f.planes[3] = sub(r3, r1);
        f.planes[4] = add(r3, r2);
        f.planes[5] = sub(r3, r2);
        for (vec4& p : f.planes) {
            float il = 1.0f / std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            p = {p.x * il, p.y * il, p.z * il, p.w * il};
        }
        return f;
    }
    bool sphereVisible(vec3 c, float r) const {
        for (const vec4& p : planes)
            if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
        return true;
    }
};

}
