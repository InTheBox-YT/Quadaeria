#include "sim/Sim.h"
#include "sim/Models.h"
namespace qc {
void Sim::gatherDevices(std::vector<BoxInstance>& out, vec3 cam, float time) const {
    const ivec3 dirs[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const vec4 copper(0.72f, 0.39f, 0.16f, 1), ceramic(0.82f, 0.81f, 0.70f, 1), dark(0.085f, 0.115f, 0.13f, 1);
    for (int64_t k : powerCells_) {
        ivec3 c = unpackPos(k);
        uint8_t id = world_->get(c.x, c.y, c.z);
        if (!isPowerBlock(id)) continue;
        vec3 p(c.x * VOX, c.y * VOX, c.z * VOX);
        if (lengthSq(p - cam) > 96 * 96) continue;
        p -= cam;
        uint8_t L = world_->lightAt(c.x, c.y + 1, c.z), meta = world_->metaAt(c.x, c.y, c.z);
        float sky = L >> 4, blk = L & 15;
        auto box = [&](vec3 off, vec3 size, vec4 col, float glow = 0) { modelBox(out, p, mat3(), off, size, col, sky, glow ? glow : blk); };
        auto bar = [&](vec3 a, vec3 b, float w, vec4 col) { modelBar(out, p + a, p + b, w, col, sky, blk); };
        auto shape = [&](const char* name) { drawModel(out, loadModel(name), p, mat3(), sky, blk); };
        bool cable = id == B_CABLE || id == B_CABLE_LIVE;
        if (cable) {
            vec4 wire = id == B_CABLE_LIVE ? vec4(0.98f, 0.63f, 0.22f, 1) : copper;
            int n = 0;
            for (auto d : dirs)
                if (isPowerBlock(world_->get(c.x + d.x, c.y + d.y, c.z + d.z))) {
                    vec3 a(0.25f), b = a + vec3(d.x, d.y, d.z) * 0.25f;
                    bar(a, b, 0.078f, dark);
                    bar(a + vec3(0, 0.043f, 0), b + vec3(0, 0.043f, 0), 0.038f, wire);
                    ++n;
                }
            if (!n) { bar({0.04f, 0.25f, 0.25f}, {0.46f, 0.25f, 0.25f}, 0.055f, wire); }
            shape(id == B_CABLE_LIVE ? "cable_live" : "cable");
            continue;
        }
        shape("device_base");
        if (id == B_SWITCH || id == B_SWITCH_ON)
            shape(id == B_SWITCH_ON ? "switch_on" : "switch_off");
        else if (id == B_PLATE)
            shape(loadedPlates_.count(k) ? "plate_down" : "plate_up");
        else if (id == B_LAMP || id == B_LAMP_ON)
            shape(id == B_LAMP_ON ? "lamp_on" : "lamp_off");
        else if (id == B_CELL) {
            shape("cell");
            box({0.14f, 0.15f, 0.085f}, {0.23f * meta / 255.0f, 0.09f, 0.015f}, {0.50f, 0.82f, 0.50f, 1});
        } else if (id == B_DYNAMO) {
            shape("dynamo");
            float phase = time * (crank_.count(k) ? 5.0f : 1.0f);
            for (int j = 0; j < 6; ++j) {
                float a = phase + j * PI / 3;
                vec3 end(0.25f + cos(a) * 0.19f, 0.27f + sin(a) * 0.19f, 0.43f);
                bar({0.25f, 0.27f, 0.43f}, end, 0.033f, copper);
                box(end - vec3(0.04f), {0.08f, 0.08f, 0.06f}, ceramic);
            }
        } else if (id == B_RAM) {
            shape("ram");
            auto d = dirs[std::min(int(meta & 7), 5)];
            vec3 n(d.x, d.y, d.z), center(0.25f, 0.25f, 0.25f);
            vec3 end = center + n * ((meta & 8) ? 0.42f : 0.22f);
            bar(center, end, 0.085f, ceramic);
            box(end - vec3(0.08f), vec3(0.16f), copper);
        } else if (id == B_SPARKGAP) {
            shape("sparkgap");
            if (meta >> 3 > 24) bar({0.17f, 0.37f, 0.24f}, {0.33f, 0.37f, 0.24f}, 0.018f, {0.6f, 0.8f, 1, 1});
        } else
            shape("detonator");
        for (auto d : dirs)
            if (isPowerBlock(world_->get(c.x + d.x, c.y + d.y, c.z + d.z)))
                bar(vec3(0.25f), vec3(0.25f) + vec3(d.x, d.y, d.z) * 0.25f, 0.04f, copper);
    }
}
}
