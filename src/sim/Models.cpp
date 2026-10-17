#include "sim/Models.h"
#include "core/Assets.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace qc {
const Model& loadModel(const std::string& name) {
    static std::map<std::string, Model> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    Model m;
    std::ifstream f(assetPath("models/" + name + ".txt"));
    if (!f) std::fprintf(stderr, "[quadaeria] missing model %s\n", name.c_str());
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::string kind, tex, flag;
        ModelPart p;
        if (!(in >> kind)) continue;
        p.bar = kind == "bar";
        in >> p.a.x >> p.a.y >> p.a.z >> p.b.x >> p.b.y >> p.b.z;
        if (p.bar) in >> p.width;
        in >> tex >> p.tint.x >> p.tint.y >> p.tint.z;
        p.tex = tex == "-" ? -1 : textureByName(tex);
        while (in >> flag) {
            if (flag == "stretch")
                p.stretch = true;
            else
                p.glow = std::stof(flag);
        }
        m.parts.push_back(p);
    }
    return cache.emplace(name, std::move(m)).first->second;
}

void drawModel(std::vector<BoxInstance>& out, const Model& m, vec3 origin, const mat3& R, float sky, float blk, float stretch) {
    for (const ModelPart& p : m.parts) {
        float light = p.glow > 0 ? p.glow : blk;
        if (p.bar)
            modelBar(out, origin + R * p.a, origin + R * p.b, p.width, p.tint, sky, light, p.tex);
        else
            modelBox(out, origin, R, p.a, p.stretch ? vec3(p.b.x, stretch, p.b.z) : p.b, p.tint, sky, light, p.tex);
    }
}

}
