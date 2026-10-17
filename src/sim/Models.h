#pragma once
#include "gfx/Renderer.h"
#include "core/Mat3.h"
#include <string>
#include <vector>
namespace qc {
struct ModelPart {
    bool bar = false, stretch = false;
    vec3 a, b;
    float width = 0, glow = 0;
    int tex = -1;
    vec4 tint{1, 1, 1, 1};
};
struct Model {
    std::vector<ModelPart> parts;
};
const Model& loadModel(const std::string& name);
void drawModel(std::vector<BoxInstance>& out, const Model& m, vec3 origin, const mat3& R, float sky, float blk, float stretch = 0);
inline mat3 modelYaw(float a) {
    mat3 r;
    r.m[0][0] = cos(a);
    r.m[0][2] = sin(a);
    r.m[2][0] = -sin(a);
    r.m[2][2] = cos(a);
    return r;
}
inline void modelBox(std::vector<BoxInstance>& out, vec3 origin, const mat3& R, vec3 off, vec3 size, vec4 color, float sky = 15,
                     float blk = 0, int tex = -1) {
    vec3 p = origin + R * off;
    BoxInstance b;
    b.r0 = {R.m[0][0] * size.x, R.m[0][1] * size.y, R.m[0][2] * size.z, p.x};
    b.r1 = {R.m[1][0] * size.x, R.m[1][1] * size.y, R.m[1][2] * size.z, p.y};
    b.r2 = {R.m[2][0] * size.x, R.m[2][1] * size.y, R.m[2][2] * size.z, p.z};
    b.layers = vec4(float(tex));
    b.params = {sky, blk, tex >= 0 ? -1.0f : 0.0f, 0};
    b.tint = color;
    out.push_back(b);
}
inline void modelBar(std::vector<BoxInstance>& out, vec3 a, vec3 b, float width, vec4 color, float sky = 15, float blk = 0, int tex = -1) {
    vec3 d = b - a;
    float len = length(d);
    if (len < 0.0001f) return;
    vec3 y = d / len, x = normalize(cross(y, fabs(y.y) < 0.95f ? vec3(0, 1, 0) : vec3(1, 0, 0))), z = cross(x, y);
    mat3 R;
    for (int i = 0; i < 3; ++i) {
        R.m[i][0] = (&x.x)[i];
        R.m[i][1] = (&y.x)[i];
        R.m[i][2] = (&z.x)[i];
    }
    modelBox(out, a, R, {-width / 2, 0, -width / 2}, {width, len, width}, color, sky, blk, tex);
}
inline void modelRope(std::vector<BoxInstance>& out, vec3 a, vec3 b, float sag, float sky = 15, float blk = 0) {
    int n = std::clamp(int(length(b - a) * 3), 4, 80);
    vec3 prev = a;
    for (int i = 1; i <= n; ++i) {
        float t = float(i) / n;
        vec3 p = a + (b - a) * t - vec3(0, 4 * sag * t * (1 - t), 0);
        modelBar(out, prev, p, 0.0625f, {0.74f, 0.57f, 0.34f, 1}, sky, blk, T_WOOL_WHITE);
        prev = p;
    }
}
}
