#include "sim/Sim.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace qc {
namespace {
struct Xf {
    mat3 R;
    vec3 t;
    Xf operator*(const Xf& b) const { return {R * b.R, R * b.t + t}; }
};
Xf translate(vec3 v) { return {mat3(), v}; }
Xf scale(vec3 s) {
    mat3 m;
    m.m[0][0] = s.x;
    m.m[1][1] = s.y;
    m.m[2][2] = s.z;
    return {m, {}};
}
Xf rotY(float a) {
    mat3 m;
    m.m[0][0] = std::cos(a);
    m.m[0][2] = std::sin(a);
    m.m[2][0] = -std::sin(a);
    m.m[2][2] = std::cos(a);
    return {m, {}};
}
Xf rotX(float a) {
    mat3 m;
    m.m[1][1] = std::cos(a);
    m.m[1][2] = -std::sin(a);
    m.m[2][1] = std::sin(a);
    m.m[2][2] = std::cos(a);
    return {m, {}};
}
Xf rotZ(float a) {
    mat3 m;
    m.m[0][0] = std::cos(a);
    m.m[0][1] = -std::sin(a);
    m.m[1][0] = std::sin(a);
    m.m[1][1] = std::cos(a);
    return {m, {}};
}

void push(std::vector<BoxInstance>& out, const Xf& x, vec4 layers, float sky, float blk, vec4 tint = {1, 1, 1, 1}, float uvScale = 0,
          float uvOff = 0) {
    BoxInstance b;
    b.r0 = {x.R.m[0][0], x.R.m[0][1], x.R.m[0][2], x.t.x};
    b.r1 = {x.R.m[1][0], x.R.m[1][1], x.R.m[1][2], x.t.y};
    b.r2 = {x.R.m[2][0], x.R.m[2][1], x.R.m[2][2], x.t.z};
    b.layers = layers;
    b.params = {sky, blk, uvScale, uvOff};
    b.tint = tint;
    out.push_back(b);
}

inline float texel(float v) { return std::floor(v * 16.0f + 0.5f) / 16.0f; }

vec4 blockLayers(uint8_t b) {
    const BlockDef& d = def(b);
    return {float(d.texTop), float(d.texBottom), float(d.texSide), float(d.texSide)};
}
}

void Sim::blobShadow(std::vector<BoxInstance>& blended, vec3 feet, float radius, vec3 cam) const {
    int cx = cellOf(feet.x), cz = cellOf(feet.z), cy = cellOf(feet.y + 0.05f);
    for (int k = 0; k < 6; ++k, --cy) {
        if (!def(world_->get(cx, cy - 1, cz)).solid) continue;
        float gy = cy * VOX;
        float h = feet.y - gy;
        float a = std::clamp(0.42f * (1.0f - h / 2.5f), 0.0f, 0.42f);
        if (a <= 0.01f) return;
        float r = radius * (1.0f + h * 0.2f);
        float x0 = texel(feet.x - r), z0 = texel(feet.z - r), x1 = texel(feet.x + r), z1 = texel(feet.z + r);
        Xf x = translate(vec3(x0, gy + 0.006f, z0) - cam) * scale(vec3(x1 - x0, 0.002f, z1 - z0));
        push(blended, x, vec4(-1), 15, 0, {0.02f, 0.02f, 0.03f, std::floor(a * 8.0f + 0.5f) / 8.0f * 0.8f});
        return;
    }
}

void Sim::gatherBoxes(std::vector<BoxInstance>& out, std::vector<BoxInstance>& blended, vec3 cam, float time) const {
    gatherDevices(out, cam, time);
    auto lightAt = [&](vec3 p, float& sky, float& blk) {
        uint8_t L = world_->lightAt(cellOf(p.x), cellOf(p.y), cellOf(p.z));
        sky = float(L >> 4);
        blk = float(L & 15);
    };
    auto gridCorner = [&](vec3 p, float w) {
        vec3 c = p - vec3(w * 0.5f, 0, w * 0.5f);
        return vec3(std::round(c.x / VOX) * VOX, std::round(c.y / VOX) * VOX, std::round(c.z / VOX) * VOX) - cam;
    };
    for (const Entity& e : objects_) {
        if (e.dead) continue;
        vec3 c = e.pos + vec3(0, e.h * 0.5f, 0);
        float distSq = lengthSq(c - cam);
        if (distSq > 144.0f * 144.0f) continue;
        bool far = distSq > 56.0f * 56.0f;
        float sky, blk;
        lightAt(c, sky, blk);
        switch (e.type) {
        case EntType::Item: {
            float bob = far ? 0.0625f : std::floor((std::sin(e.age * 2.5f) * 0.06f + 0.12f) * 16.0f + 0.5f) / 16.0f;
            bool cube = false;
            if (isBlockItem(e.item)) {
                Render r = def(BlockId(e.item)).render;
                cube = r == Render::Opaque || r == Render::Leaves || r == Render::Glass || r == Render::Fluid;
            }
            const float turn = std::floor(e.yaw / (PI * 0.5f) + 0.5f) * (PI * 0.5f);
            int copies = far ? 1 : e.count > 64 ? 3 : e.count > 8 ? 2 : 1;
            for (int k = 0; k < copies; ++k) {
                vec3 o(k * 2.0f / 16.0f, k * 1.0f / 16.0f, -k * 1.0f / 16.0f);
                vec3 at = vec3(texel(e.pos.x), texel(e.pos.y), texel(e.pos.z)) - cam + vec3(0, bob, 0) + o;
                if (cube) {
                    Xf x = translate(at) * rotY(turn) * translate(vec3(-0.125f, 0, -0.125f)) * scale(vec3(0.25f));
                    push(out, x, blockLayers(uint8_t(e.item)), sky, blk);
                } else {
                    int tex = isBlockItem(e.item) ? def(BlockId(e.item)).texSide : itemDef(e.item).tex;
                    Xf x = translate(at) * rotY(turn) * translate(vec3(-0.25f, 0, -0.5f / 16.0f)) * scale(vec3(0.5f, 0.5f, 1.0f / 16.0f));
                    push(out, x, vec4(float(tex)), sky, blk);
                }
            }
            break;
        }
        case EntType::TNT: {
            bool flash = std::fmod(e.fuse, 0.5f) < 0.22f;
            Xf x = translate(gridCorner(e.pos, e.w)) * scale(vec3(e.w));
            push(out, x, blockLayers(B_TNT), sky, flash ? 16.0f : blk, flash ? vec4(1, 1, 1, 1) : vec4(1, 1, 1, 1));
            break;
        }
        case EntType::Falling: {
            Xf x = translate(gridCorner(e.pos, VOX)) * scale(vec3(VOX));
            push(out, x, blockLayers(e.block), sky, blk, {1, 1, 1, 1}, 0.5f, 0.25f);
            break;
        }
        case EntType::Debris: {
            Xf x = translate(gridCorner(e.pos, VOX)) * scale(vec3(VOX));
            if (e.block == B_WATER)
                push(blended, x, blockLayers(e.block), sky, blk, {0.55f, 0.75f, 1.0f, 0.8f}, 0.5f, 0.25f);
            else if (e.block == B_LAVA)
                push(out, x, blockLayers(e.block), sky, 16.0f, {1, 1, 1, 1}, 0.5f, 0.25f);
            else
                push(out, x, blockLayers(e.block), sky, blk, {1, 1, 1, 1}, 0.5f, 0.25f);
            break;
        }
        }
    }
    for (const Stress& st : stress_) {
        vec3 rel = vec3(st.c.x * VOX, st.c.y * VOX, st.c.z * VOX) - cam;
        if (lengthSq(rel) > 60.0f * 60.0f) continue;
        int stage = std::clamp(int(st.timer / st.warn * 8.0f) + 1, 0, 7);
        float sky, blk;
        lightAt(vec3((st.c.x + 0.5f) * VOX, (st.c.y - 0.5f) * VOX, (st.c.z + 0.5f) * VOX), sky, blk);
        const float e = 0.006f;
        Xf x = translate(rel - vec3(e)) * scale(vec3(VOX + 2 * e));
        push(blended, x, vec4(float(T_CRACK0 + stage)), sky, blk, {0.85f, 0.85f, 0.85f, 0.9f});
    }
    struct Sprite {
        float d2;
        BoxInstance b;
    };
    static thread_local std::vector<Sprite> sprites;
    sprites.clear();
    for (const Particle& p : particles_) {
        if (p.life <= 0) continue;
        vec3 rel = p.pos - cam;
        float d2 = lengthSq(rel);
        if (d2 > 80.0f * 80.0f) continue;
        float sky, blk;
        lightAt(p.pos, sky, blk);
        const float age01 = std::clamp(1.0f - p.life / std::max(p.maxLife, 0.001f), 0.0f, 1.0f);
        const float fadeOut = std::clamp(p.life / std::max(0.001f, std::min(0.45f, p.maxLife * 0.4f)), 0.0f, 1.0f);
        const float fadeIn = std::clamp(age01 * 8.0f, 0.0f, 1.0f);
        const float seed = p.uvOff * 37.0f;

        auto fragment = [&](float size, vec4 layers, vec4 tint, float b, float uvS, float uvO, bool blend) {
            float spin = (p.maxLife - p.life) * (3.0f + std::fmod(seed, 4.0f)) * std::min(1.0f, length(p.vel) * 0.6f + 0.15f);
            Xf x = translate(rel) * rotY(seed + spin) * rotX(seed * 0.7f + spin * 0.6f) * translate(vec3(-size * 0.5f)) * scale(vec3(size));
            push(blend ? blended : out, x, layers, sky, b, tint, uvS, uvO);
        };
        switch (p.kind) {
        case PKind::Block: {
            float s = p.size * std::min(1.0f, p.life / 0.35f);
            if (s < 0.01f) break;
            fragment(s, vec4(float(p.layer)), {1, 1, 1, 1}, blk, std::max(0.12f, s * 2.0f), std::floor(p.uvOff * 12.0f) / 16.0f, false);
            break;
        }
        case PKind::Leaf: {
            float flutter = std::sin((p.maxLife - p.life) * 5.0f + seed) * 0.8f;
            float s = p.size * std::min(1.0f, p.life / 0.5f);
            Xf x = translate(rel) * rotY(seed + (p.maxLife - p.life) * 1.5f) * rotX(flutter) * translate(vec3(-s * 0.5f, 0, -s * 0.5f)) *
                   scale(vec3(s, 1.0f / 32.0f, s));
            push(out, x, vec4(float(p.layer)), sky, blk, p.tint, 0.25f, std::floor(p.uvOff * 12.0f) / 16.0f);
            break;
        }
        case PKind::Spark: {
            float s = std::max(0.02f, p.size * (0.35f + 0.65f * fadeOut));
            vec4 hot(p.tint.x, p.tint.y * (0.55f + 0.45f * fadeOut), p.tint.z * fadeOut, 1);
            fragment(s, vec4(-1), hot, 16.0f, 0, 0, false);
            break;
        }
        case PKind::Splash:
        case PKind::Drip:
        case PKind::Bubble: {
            float s = std::max(0.025f, p.size * (p.kind == PKind::Bubble ? 0.8f : 0.7f));
            vec4 t(0.62f, 0.80f, 0.95f, (p.kind == PKind::Bubble ? 0.45f : 0.75f) * fadeOut);
            if (p.tint.w > 0) t = vec4(p.tint.x, p.tint.y, p.tint.z, t.w);
            Xf x = translate(rel) * translate(vec3(-s * 0.5f)) * scale(vec3(s));
            sprites.push_back({d2, BoxInstance{}});
            BoxInstance& bi = sprites.back().b;
            bi.r0 = {x.R.m[0][0], x.R.m[0][1], x.R.m[0][2], x.t.x};
            bi.r1 = {x.R.m[1][0], x.R.m[1][1], x.R.m[1][2], x.t.y};
            bi.r2 = {x.R.m[2][0], x.R.m[2][1], x.R.m[2][2], x.t.z};
            bi.layers = vec4(-1);
            bi.params = {sky, blk, 0, 0};
            bi.tint = t;
            break;
        }
        default: {
            float s = p.size;
            int tex;
            vec4 t = p.tint;
            float b = blk;
            if (p.kind == PKind::Flame) {
                tex = int(T_PFX_FIRE0) + (int(std::floor(time * 10.0f + seed)) & 3);
                b = 16.0f;
                t = vec4(1, 1, 1, std::min(1.0f, fadeOut * 1.4f) * fadeIn);
                s *= 0.8f + 0.4f * fadeOut;
            } else if (p.kind == PKind::Mote) {
                tex = int(T_PFX_MOTE);
                float tw = std::clamp(std::min(p.maxLife - p.life, p.life) * 0.8f, 0.0f, 1.0f);
                t.w *= tw;
                if (sky > 11.0f) b = std::max(b, 12.0f);
            } else {
                tex = int(T_PFX_SMOKE0) + (int(seed) & 3);
                t.w *= fadeOut * fadeIn;
                s *= 0.7f + 0.6f * age01;
            }
            if (t.w < 0.01f) break;
            vec3 toCam = -rel;
            float horiz = std::sqrt(toCam.x * toCam.x + toCam.z * toCam.z);
            float yaw = std::atan2(toCam.x, toCam.z), pitch = -std::atan2(toCam.y, std::max(horiz, 1e-4f));
            float roll = seed + (p.maxLife - p.life) * (p.kind == PKind::Flame ? 0.0f : 0.35f);
            const float thick = 1.0f / 256.0f;
            Xf bb = translate(rel) * rotY(yaw) * rotX(pitch) * rotZ(roll) * translate(vec3(-s * 0.5f, -s * 0.5f, -thick * 0.5f)) *
                    scale(vec3(s, s, thick));
            sprites.push_back({d2, BoxInstance{}});
            BoxInstance& bi = sprites.back().b;
            bi.r0 = {bb.R.m[0][0], bb.R.m[0][1], bb.R.m[0][2], bb.t.x};
            bi.r1 = {bb.R.m[1][0], bb.R.m[1][1], bb.R.m[1][2], bb.t.y};
            bi.r2 = {bb.R.m[2][0], bb.R.m[2][1], bb.R.m[2][2], bb.t.z};
            bi.layers = vec4(float(tex));
            bi.params = {sky, b, 0, 0};
            bi.tint = t;
            break;
        }
        }
    }
    std::sort(sprites.begin(), sprites.end(), [](const Sprite& a, const Sprite& b) { return a.d2 > b.d2; });
    for (const Sprite& sp : sprites) blended.push_back(sp.b);
    if (std::getenv("QC_PDEBUG"))
        std::printf("[pdebug] parts %zu sprites %zu out %zu blended %zu\n", size_t(liveParticles_), sprites.size(), out.size(),
                    blended.size());
}

}
