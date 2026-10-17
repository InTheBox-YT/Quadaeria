#pragma once
#include "core/Noise.h"
#include "world/World.h"
#include <memory>
#include <vector>

namespace qc {
enum class Biome : uint8_t {
    Ocean,
    DeepOcean,
    FrozenOcean,
    Beach,
    SnowyBeach,
    StonyShore,
    River,
    FrozenRiver,
    Plains,
    Meadow,
    Forest,
    BirchForest,
    DarkForest,
    Swamp,
    Taiga,
    SnowyTaiga,
    SnowyPlains,
    IceSpikes,
    Jungle,
    Savanna,
    Desert,
    Badlands,
    WindsweptHills,
    SnowyPeaks,
    StonyPeaks,
    ChalkDowns,
    RockyMoor,
    AutumnWoodland,
    HeatherHighlands,
    VolcanicCoast,
    Count
};
const char* biomeName(Biome b);

struct Shape {
    float h = 0;
    float amp = 0;
    float t = 0.5f, hm = 0.5f;
    float c = 0;
    float mount = 0, bad = 0, swamp = 0, river = 0, desert = 0, pv = 0;
    float riverLevel = SEA, flowX = 0, flowZ = 0, riverFall = 0;
    float chalk = 0, moor = 0, autumn = 0, heath = 0, volcanic = 0;
    float bank = 0;
    float wetNear = 0;
};

struct SurfaceInfo {
    int height = 0;
    Biome biome = Biome::Plains;
    float temp = 0.5f, humid = 0.5f;
};

constexpr int RUIN_REGION = 224;

enum CaveTheme { CT_PLAIN, CT_MOSSY, CT_CRYSTAL, CT_LAVA, CT_FROZEN, CT_MUSHROOM, CT_FOSSIL, CT_ROOTS, CT_SALT };
const char* caveThemeName(int theme);

struct GenSettings {
    float terrainScale = 1.0f;
    float relief = 1.0f;
    float biomeScale = 1.0f;
    float rivers = 1.0f;
    float caves = 1.0f;
    float vegetation = 1.0f;
    float structures = 1.0f;
};

class Generator {
public:
    explicit Generator(uint64_t seed, GenSettings settings = {});
    uint64_t seed() const { return seed_; }
    const GenSettings& settings() const { return settings_; }
    Shape shape(int x, int z) const;
    SurfaceInfo surface(int x, int z) const;
    Biome biomeAt(int x, int z) const { return surface(x, z).biome; }
    std::unique_ptr<Column> generate(int cx, int cz) const;
    void fillClimate(Column& c) const;
    void findSpawn(int& x, int& y, int& z) const;
    int caveTheme(int wx, int wz, float temp) const;
    bool ruinSite(int rx, int rz, int& x0, int& z0, int& x1, int& z1, bool& oldWorld) const;
    bool entranceAt(int rx, int rz, float& px, float& py, float& pz, float& yaw, bool& hillside) const;
    void plantTree(Column& c, int kind, int x, int y, int z, uint32_t h) const { tree(c, kind, x, y, z, h); }

private:
    struct LandmarkSite {
        int x, z, kind;
        uint32_t hash;
        SurfaceInfo surface;
    };
    bool landmarkSite(int rx, int rz, LandmarkSite& site) const;
    Biome pickBiome(const Shape& s, int height, int x, int z) const;
    float overhang(int lx, int ly, int lz) const;
    int topOf(const Shape& s, int x, int z) const;
    void carveCaves(Column& c, const int* tops, const Shape* shapes) const;
    void carveWorms(Column& c, int* tops, const Shape* shapes) const;
    void decorateCaves(Column& c, const int* topsP) const;
    void placeStrata(Column& c) const;
    void placeOres(Column& c) const;
    void decorate(Column& c, const int* tops, const Biome* biomes) const;
    void placeLandmarks(Column& c) const;
    void placeRuins(Column& c, const int* tops, const Biome* biomes) const;
    void tree(Column& c, int kind, int x, int y, int z, uint32_t h) const;
    uint64_t seed_;
    GenSettings settings_{};
    Noise cont_, hills_, detail_, mount_, temp_, humid_, cave1_, cave2_, cave3_, river_;
    Noise eros_, weird_, warp_, dune_, mesa_, over_, noodle1_, noodle2_, ravine_, strata_, strata2_, entr_;
};

}
