#pragma once
#include "gfx/Renderer.h"
#include "world/World.h"
#include <vector>

namespace qc {
class LightVolume {
public:
    void init(Renderer* r, int sizeMinis);
    void reset();
    void update(World& w, vec3 camMetres, int budget);
    int size() const { return n_; }
    int uploadsLastFrame = 0;

private:
    void upload(const Column& c, int s, int slotX, int slotZ);
    struct Slot {
        int64_t key = INT64_MIN;
        bool valid[NSEC] = {};
    };
    Renderer* r_ = nullptr;
    int n_ = 0, s_ = 0;
    std::vector<Slot> slots_;
    std::vector<std::pair<int, int>> order_;
    std::vector<uint8_t> buf_;
};

}
