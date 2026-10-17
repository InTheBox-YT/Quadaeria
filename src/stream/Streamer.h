#pragma once
#include "core/Tasks.h"
#include "gfx/Renderer.h"
#include "world/Gen.h"
#include "world/World.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace qc {
class Streamer {
public:
    void init(World* w, Generator* g, Renderer* r, TaskPool* pool, const std::string& saveDir);
    void update(vec3 playerPos, int radius, bool fancyLeaves, double now);
    void saveAll();
    void unloadAll(bool save);
    int pendingJobs() const { return genInFlight_ + lightInFlight_ + meshInFlight_; }
    bool areaReady(vec3 pos, int radiusCols) const;
    int loadedColumns() const;
    void remeshAll();
    void requestRelight(int64_t key);
    std::string saveDir;

private:
    void requestColumn(int cx, int cz, float pri);
    void scheduleLight(Column& c, float pri);
    void scheduleMesh(Column& c, int sy, float pri, bool fancy);
    bool neighboursAtLeast(int cx, int cz, ColState s) const;
    World* world_ = nullptr;
    Generator* gen_ = nullptr;
    Renderer* renderer_ = nullptr;
    TaskPool* pool_ = nullptr;
    std::unordered_set<int64_t> requested_;
    std::unordered_map<int64_t, uint32_t> meshVersion_;
    int genInFlight_ = 0, lightInFlight_ = 0, meshInFlight_ = 0;
    int lastCx_ = 1 << 30, lastCz_ = 1 << 30;
    std::vector<std::pair<int, int>> ring_;
    int ringRadius_ = -1;
    double now_ = 0, nextUnload_ = 0;
};

}
