#include "app/Game.h"
#include "core/Noise.h"
#include <algorithm>
#include <cmath>

namespace qc {
void Game::updateClimate(float dt) {
    climateT_ -= dt;
    if (climateT_ > 0 || !gen_) return;
    climateT_ = 1.5f;
    Shape sh = gen_->shape(cellOf(player_.pos.x), cellOf(player_.pos.z));
    temp_ = sh.t;
    humid_ = sh.hm;
}

void Game::drawAmbient(std::vector<BoxInstance>& blended) {
    const vec3 cam = cam_.pos;
    const float t = float(clock_);
    auto boxAt = [&](vec3 p, vec3 a, vec3 b, vec3 c, vec4 tint, float sky) {
        BoxInstance bi;
        vec3 rel = p - cam;
        bi.r0 = {a.x, b.x, c.x, rel.x};
        bi.r1 = {a.y, b.y, c.y, rel.y};
        bi.r2 = {a.z, b.z, c.z, rel.z};
        bi.layers = vec4(-1);
        bi.params = {sky, 0, 0, 0};
        bi.tint = tint;
        return bi;
    };
    auto tx = [](float v) { return std::floor(v * 16.0f + 0.5f) / 16.0f; };
    auto pixBox = [&](vec3 corner, vec3 size, vec4 tint, float sky) {
        vec3 c(tx(corner.x), tx(corner.y), tx(corner.z));
        vec3 sz(std::max(1.0f, std::round(size.x * 16.0f)) / 16.0f, std::max(1.0f, std::round(size.y * 16.0f)) / 16.0f,
                std::max(1.0f, std::round(size.z * 16.0f)) / 16.0f);
        return boxAt(c, vec3(sz.x, 0, 0), vec3(0, sz.y, 0), vec3(0, 0, sz.z), tint, sky);
    };
    int vents = 0;
    // geyser steam
    for (const auto& [key, col] : world_.columns()) {
        if (col->state == ColState::Generating || std::abs(col->cx - floorDiv(cellOf(cam.x), CS)) > 2 ||
            std::abs(col->cz - floorDiv(cellOf(cam.z), CS)) > 2)
            continue;
        for (int index : col->features) {
            if (vents >= 12) break;
            int x = col->cx * CS + index % CS, y = index / CS2, z = col->cz * CS + (index / CS) % CS;
            if (world_.get(x, y, z) != B_GEYSER) continue;
            vec3 source((x + 0.5f) * VOX, (y + 1) * VOX, (z + 0.5f) * VOX);
            if (lengthSq(source - cam) > 40 * 40) continue;
            float ceiling = 8.0f;
            int surfaceY = y + 1;
            for (int dy = 1; dy <= 16; ++dy) {
                uint8_t b = world_.get(x, y + dy, z);
                if (def(b).collides) {
                    ceiling = (dy - 1) * VOX;
                    break;
                }
                if (b == B_WATER) surfaceY = y + dy + 1;
            }
            float waterLift = (surfaceY - y - 1) * VOX;
            if (ceiling <= waterLift + 0.1f) continue;
            source.y += waterLift;
            ceiling -= waterLift;
            ++vents;
            float jet = geyserJet(x, z, t), sky = float(world_.lightAt(x, surfaceY, z) >> 4);
            int count = settings_.graphics >= 2 ? (jet > 0 ? 16 : 4) : (jet > 0 ? 42 : 10);
            for (int k = 0; k < count; ++k) {
                float seed = hashf(x, k, z, 0x6E75u), age = std::fmod(t * (jet > 0 ? 0.55f : 0.3f) + seed, 1.0f);
                float rise = age * (jet > 0 ? 4.0f * jet : 1.5f);
                if (rise > ceiling - 0.1f) continue;
                float spread = 0.12f + age * (jet > 0 ? 0.52f : 0.65f), angle = seed * PI * 8 + t * 0.16f;
                vec3 p = source + vec3(std::cos(angle) * spread, rise, std::sin(angle) * spread);
                float size = 0.0625f + age * (jet > 0 ? 0.19f : 0.125f);
                blended.push_back(pixBox(p, vec3(size), vec4(0.80, 0.86, 0.77, (1 - age) * (jet > 0 ? 0.27f : 0.35f)), sky));
                if (jet > 0 && k < 10) {
                    float flight = age * 1.3f, height = (10.0f * flight - 7.0f * flight * flight) * jet;
                    if (height > 0 && height < ceiling - 0.1f) {
                        vec3 q = source + vec3(std::cos(angle) * flight * 0.30f, height, std::sin(angle) * flight * 0.30f);
                        auto drop =
                            pixBox(q, vec3(0.125f, 0.125f + 0.0625f * (1 - age), 0.125f), vec4(0.70, 0.84, 0.79, 0.55f * (1 - age)), sky);
                        drop.layers = vec4(T_WATER);
                        blended.push_back(drop);
                    }
                }
            }
        }
    }

    if (settings_.graphics < 2) {
        int gx = int(std::floor(cam.x / 3.0f)), gz = int(std::floor(cam.z / 3.0f));
        float night = 1.0f - std::clamp(std::sin(dayTime_ * 2 * PI) * 2.2f + 0.45f, 0.0f, 1.0f);
        for (int dz = -4; dz <= 4; ++dz)
            for (int dx = -4; dx <= 4; ++dx) {
                int cx = gx + dx, cz = gz + dz;
                float seed = hashf(cx, 0, cz, 0xA78u);
                if (seed > 0.36f) continue;
                float x = (cx + 0.25f + seed) * 3, z = (cz + hashf(cx, 1, cz, 0xA78u)) * 3;
                int mx = cellOf(x), mz = cellOf(z);
                if (!world_.loaded(mx, mz)) continue;
                int y = world_.heightAtTop(mx, mz);
                if (std::fabs(y * VOX - cam.y) > 8) continue;
                uint8_t ground = world_.get(mx, y - 1, mz);
                while (y > 1 && def(ground).render == Render::Cross) {
                    --y;
                    ground = world_.get(mx, y - 1, mz);
                }
                float phase = t * (0.14f + seed * 0.1f) + seed * 31, age = phase - std::floor(phase);
                vec3 p(x + std::sin(t * 0.6f + seed * 40) * 0.45f, y * VOX + 0.2f + age * 1.5f, z + std::cos(t * 0.5f + seed * 20) * 0.45f);
                float alpha = std::sin(age * PI) * std::clamp((15.0f - length(p - cam)) / 4.0f, 0.0f, 1.0f);
                uint8_t light = world_.lightAt(cellOf(p.x), cellOf(p.y), cellOf(p.z));
                vec4 color(0.78f, 0.84f, 0.47f, alpha * 0.45f);
                bool emissive = false;
                if (ground == B_WATER) {
                    if (temp_ < 0.28f || seed > 0.17f) continue;
                    p.y = y * VOX + 0.08f + age * 0.3f;
                    color = {0.58f, 0.82f, 0.79f, alpha * 0.35f};
                } else if (def(ground).render == Render::Leaves) {
                    p.y = y * VOX + 0.1f - age * 2.4f;
                    color = {0.39f, 0.64f, 0.24f, alpha * 0.8f};
                } else if (ground == B_GRASS || ground == B_MOSS) {
                    if ((light >> 4) < 5 || temp_ < 0.3f) continue;
                    if (night > .45f || seed > .18f) continue;
                } else
                    continue;
                if (world_.collidesAt(cellOf(p.x), cellOf(p.y), cellOf(p.z))) continue;
                auto mote = pixBox(p, vec3(1.0f / 16), color, float(light >> 4));
                if (emissive) mote.params.y = 16;
                blended.push_back(mote);
            }
        // fireflies, only at night
        if (night > .15f) {
            std::vector<vec3> bushes;
            int cx = floorDiv(cellOf(cam.x), CS), cz = floorDiv(cellOf(cam.z), CS);
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx) {
                    auto col = world_.column(cx + dx, cz + dz);
                    if (!col) continue;
                    for (int i : col->features) {
                        int x = col->cx * CS + i % CS, z = col->cz * CS + (i / CS) % CS, y = i / CS2;
                        if (col->get(x & 31, y, z & 31) != B_FIREFLY_BUSH) continue;
                        vec3 p((x + .5f) * VOX, (y + .7f) * VOX, (z + .5f) * VOX);
                        if (lengthSq(p - cam) < 26 * 26 && cam_.frustum.sphereVisible(p - cam, 2.5f)) bushes.push_back(p);
                    }
                }
            std::sort(bushes.begin(), bushes.end(), [&](vec3 a, vec3 b) { return lengthSq(a - cam) < lengthSq(b - cam); });
            if (bushes.size() > 16) bushes.resize(16);
            for (vec3 source : bushes)
                for (int k = 0; k < 4; ++k) {
                    float seed = hashf(cellOf(source.x), k, cellOf(source.z), 0xF17u), phase = seed * 2 * PI + t * (.35f + seed * .2f);
                    vec3 p = source + vec3(std::sin(phase) * (.45f + seed), .2f + (.5f + .5f * std::sin(phase * .71f + seed * 20)) * .95f,
                                           std::cos(phase * .83f) * (.4f + seed));
                    if (world_.collidesAt(cellOf(p.x), cellOf(p.y), cellOf(p.z))) continue;
                    float fade = std::clamp((26 - length(p - cam)) / 6, 0.0f, 1.0f) * std::clamp((night - .15f) * 1.5f, 0.0f, 1.0f);
                    float pulse = .28f + .72f * std::pow(.5f + .5f * std::sin(t * 2.4f + seed * 29), 3.0f);
                    auto fly = [&](vec3 q, vec3 size, vec4 color, bool glow) {
                        auto b = boxAt(q, size * vec3(1, 0, 0), size * vec3(0, 1, 0), size * vec3(0, 0, 1), color, 0);
                        if (glow) b.params.y = 16;
                        blended.push_back(b);
                    };
                    fly(p, vec3(.045f, .035f, .065f), {.12f, .18f, .065f, fade}, false);
                    fly(p + vec3(0, 0, .04f), vec3(.045f, .035f, .04f), {.65f + .3f * pulse, .8f + .2f * pulse, .22f, fade * pulse}, true);
                    float wing = .025f + .025f * std::fabs(std::sin(t * 28 + seed * 19));
                    for (int side : {-1, 1})
                        fly(p + vec3(side < 0 ? -wing : .045f, .024f, .008f), vec3(wing, .012f, .035f), {.63f, .72f, .40f, fade * .42f},
                            true);
                }
        }
        int count = 0;
        for (const auto& [key, column] : world_.columns()) {
            if (std::abs(column->cx - floorDiv(cellOf(cam.x), CS)) > 1 || std::abs(column->cz - floorDiv(cellOf(cam.z), CS)) > 1) continue;
            for (int i : column->emitters) {
                if (count >= 24) break;
                int x = column->cx * CS + i % CS, z = column->cz * CS + (i / CS) % CS, y = i / CS2;
                if (world_.get(x, y, z) != B_GLOWSHROOM) continue;
                vec3 source((x + 0.5f) * VOX, (y + 0.7f) * VOX, (z + 0.5f) * VOX);
                if (lengthSq(source - cam) > 18 * 18) continue;
                float age = std::fmod(t * 0.22f + hashf(x, y, z, 0x89u) * 9, 1.0f);
                vec3 p = source + vec3(std::sin(age * 5 + x) * 0.3f, age * 1.1f, std::cos(age * 4 + z) * 0.3f);
                if (world_.collidesAt(cellOf(p.x), cellOf(p.y), cellOf(p.z))) continue;
                auto spore = pixBox(p, vec3(1.0f / 16), vec4(0.25f, 0.84f, 0.74f, std::sin(age * PI) * 0.65f), 0);
                spore.params.y = 16;
                blended.push_back(spore);
                ++count;
            }
        }
    }
}

}
