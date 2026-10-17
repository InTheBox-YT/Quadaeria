#include "world/Gen.h"
#include "world/Orient.h"
#include "core/Base.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace qc {
const char* biomeName(Biome b) {
    static const char* n[] = {"Ocean",
                              "Deep Ocean",
                              "Frozen Ocean",
                              "Beach",
                              "Snowy Beach",
                              "Stony Shore",
                              "River",
                              "Frozen River",
                              "Plains",
                              "Meadow",
                              "Forest",
                              "Birch Forest",
                              "Dark Forest",
                              "Swamp",
                              "Taiga",
                              "Snowy Taiga",
                              "Snowy Plains",
                              "Ice Spikes",
                              "Jungle",
                              "Savanna",
                              "Desert",
                              "Badlands",
                              "Windswept Hills",
                              "Snowy Peaks",
                              "Stony Peaks",
                              "Chalk Downs",
                              "Rocky Moor",
                              "Autumn Woodland",
                              "Heather Highlands",
                              "Volcanic Coast"};
    return n[int(b)];
}

Generator::Generator(uint64_t seed, GenSettings settings)
    : seed_(seed), settings_(settings), cont_(seed * 7 + 1), hills_(seed * 7 + 2), detail_(seed * 7 + 3), mount_(seed * 7 + 4),
      temp_(seed * 7 + 5), humid_(seed * 7 + 6), cave1_(seed * 13 + 7), cave2_(seed * 13 + 8), cave3_(seed * 13 + 9),
      river_(seed * 13 + 10), eros_(seed * 17 + 11), weird_(seed * 17 + 12), warp_(seed * 17 + 13), dune_(seed * 17 + 14),
      mesa_(seed * 17 + 15), over_(seed * 19 + 16), noodle1_(seed * 19 + 17), noodle2_(seed * 19 + 18), ravine_(seed * 19 + 19),
      strata_(seed * 23 + 20), strata2_(seed * 23 + 21), entr_(seed * 23 + 22) {
    settings_.terrainScale = std::clamp(settings_.terrainScale, 0.50f, 2.00f);
    settings_.relief = std::clamp(settings_.relief, 0.50f, 1.60f);
    settings_.biomeScale = std::clamp(settings_.biomeScale, 0.50f, 2.50f);
    settings_.rivers = std::clamp(settings_.rivers, 0.0f, 2.0f);
    settings_.caves = std::clamp(settings_.caves, 0.0f, 2.0f);
    settings_.vegetation = std::clamp(settings_.vegetation, 0.0f, 2.0f);
    settings_.structures = std::clamp(settings_.structures, 0.0f, 2.0f);
}

namespace {
float sm(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

float continentHeight(float c) {
    static const float P[][2] = {{-1.0f, 20},    {-0.45f, 26}, {-0.2f, 40}, {-0.06f, 54}, {0.0f, 61},
                                 {0.04f, 63.6f}, {0.12f, 66},  {0.35f, 72}, {0.7f, 78},   {1.3f, 84}};
    const int N = sizeof(P) / sizeof(P[0]);
    if (c <= P[0][0]) return P[0][1];
    for (int i = 1; i < N; ++i)
        if (c < P[i][0]) {
            float t = (c - P[i - 1][0]) / (P[i][0] - P[i - 1][0]);
            t = t * t * (3 - 2 * t);
            return P[i - 1][1] + (P[i][1] - P[i - 1][1]) * t;
        }
    return P[N - 1][1];
}

bool isSnowy(Biome b) {
    return b == Biome::SnowyPlains || b == Biome::SnowyTaiga || b == Biome::IceSpikes || b == Biome::SnowyBeach || b == Biome::SnowyPeaks;
}
bool isWater(Biome b) {
    return b == Biome::Ocean || b == Biome::DeepOcean || b == Biome::FrozenOcean || b == Biome::River || b == Biome::FrozenRiver;
}

const uint8_t TC_BANDS[16] = {B_TERRACOTTA, B_TC_ORANGE,    B_TC_ORANGE,  B_TERRACOTTA, B_TC_YELLOW, B_TC_BROWN, B_TERRACOTTA, B_TC_RED,
                              B_TC_WHITE,   B_TC_LIGHTGRAY, B_TERRACOTTA, B_TC_ORANGE,  B_TC_RED,    B_TC_BROWN, B_TERRACOTTA, B_TC_YELLOW};

uint8_t surfaceLayer(Biome b, int y, int d, bool steep, float n, float n2, float band) {
    const int soil = 3 + int(n * 4);
    const bool under = y < SEA - 1;
    switch (b) {
    case Biome::Ocean:
    case Biome::FrozenOcean:
    case Biome::DeepOcean:
        if (d < 4 + int(n * 4)) return n2 > 0.72f ? B_GRAVEL : n2 < 0.12f ? B_CLAY : (b == Biome::DeepOcean ? B_GRAVEL : B_SAND);
        if (d < 9 && b != Biome::DeepOcean) return B_SANDSTONE;
        return 0;
    case Biome::River:
    case Biome::FrozenRiver:
        if (d < 4) return n2 > 0.7f ? B_GRAVEL : n2 < 0.2f ? B_CLAY : B_SAND;
        return 0;
    case Biome::Beach:
    case Biome::SnowyBeach:
        if (d < 6 + int(n * 3)) return B_SAND;
        if (d < 11) return B_SANDSTONE;
        return 0;
    case Biome::StonyShore: return (d < 2 && n2 > 0.6f) ? B_GRAVEL : 0;
    case Biome::Desert:
        if (d < 8 + int(n * 5)) return B_SAND;
        if (d < 20) return B_SANDSTONE;
        return 0;
    case Biome::Badlands:
        if (!steep && d < 2 && n2 > 0.2f) return B_RED_SAND;
        if (!steep && d == 0) return B_COARSE_DIRT;
        if (d < 50) return TC_BANDS[floorMod(floorDiv(y + int(std::floor(band)), 3), 16)];
        return B_RED_SANDSTONE;
    case Biome::Swamp:
        if (d == 0) return (under || y < SEA + 1) ? B_MUD : B_GRASS;
        if (d < soil) return n2 > 0.75f ? B_CLAY : B_DIRT;
        return 0;
    case Biome::SnowyPeaks:
        if (steep) return (d == 0 && n2 > 0.45f) ? B_SNOW : (d < 3 && n2 < 0.12f ? B_PACKED_ICE : 0);
        return d < 3 ? B_SNOW : 0;
    case Biome::StonyPeaks:
        if (d < 3 && n2 > 0.7f) return B_CALCITE;
        if (d < 3 && n2 < 0.25f) return B_ANDESITE;
        return 0;
    case Biome::ChalkDowns:
        if (steep) return B_CALCITE;
        if (d == 0) return B_GRASS;
        return d < 2 ? B_DIRT : d < 18 ? B_CALCITE : 0;
    case Biome::VolcanicCoast:
        if (d == 0 && !steep && n2 > .78f) return B_ASH;
        return d < 16 ? (n2 > .62f ? B_TUFF : B_SCORIA) : 0;
    case Biome::HeatherHighlands:
        if (steep) return d < 5 ? B_TUFF : 0;
        if (d == 0) return n2 > .86f ? B_COARSE_DIRT : B_GRASS;
        return d < 3 ? B_DIRT : d < 10 ? B_TUFF : 0;
    case Biome::RockyMoor:
        if (steep || n2 < .32f) return d < 4 ? B_ANDESITE : 0;
        if (d == 0) return n2 > .84f ? B_MOSS : n2 > .7f ? B_COARSE_DIRT : B_GRASS;
        return d < 3 ? B_DIRT : d < 8 ? B_TUFF : 0;
    case Biome::WindsweptHills:
        if (steep) return (d < 2 && n2 > 0.75f) ? B_GRAVEL : 0;
        if (d == 0) return n2 > 0.88f ? B_GRAVEL : B_GRASS;
        return d < 2 + int(n * 2) ? B_DIRT : 0;
    default: break;
    }
    if (steep && d < 3) return n2 > 0.8f ? B_GRAVEL : 0;
    if (d == 0) {
        if (under) return n2 > 0.65f ? B_GRAVEL : (n2 < 0.25f ? B_CLAY : B_DIRT);
        switch (b) {
        case Biome::AutumnWoodland: return n2 > .72f ? B_PODZOL : B_GRASS;
        case Biome::Taiga: return n2 > 0.62f ? B_PODZOL : (n2 < 0.1f ? B_COARSE_DIRT : B_GRASS);
        case Biome::SnowyTaiga:
        case Biome::SnowyPlains:
        case Biome::IceSpikes: return B_SNOWY_GRASS;
        case Biome::Savanna: return n2 > 0.8f ? B_COARSE_DIRT : B_GRASS;
        case Biome::Jungle: return n2 > 0.88f ? B_PODZOL : B_GRASS;
        case Biome::DarkForest: return n2 > 0.93f ? B_PODZOL : B_GRASS;
        default: return B_GRASS;
        }
    }
    return d < soil ? B_DIRT : 0;
}
}

Shape Generator::shape(int x, int z) const {
    Shape s;
    const float rawBx = x * 0.5f, rawBz = z * 0.5f;
    const float bx = rawBx / settings_.terrainScale, bz = rawBz / settings_.terrainScale;
    const float cbx = rawBx / settings_.biomeScale, cbz = rawBz / settings_.biomeScale;
    const float wx = bx + warp_.noise2(bx / 230.0f, bz / 230.0f) * 45.0f;
    const float wz = bz + warp_.noise2(bx / 230.0f + 31.7f, bz / 230.0f - 17.3f) * 45.0f;
    s.c = cont_.fbm2(wx / 620.0f, wz / 620.0f, 4) * 1.35f + 0.27f;
    const float e = eros_.fbm2(wx / 360.0f, wz / 360.0f, 3) * 1.3f;
    s.t = clamp01(0.5f + temp_.fbm2(cbx / 520.0f + 100.0f, cbz / 520.0f, 3) * 1.15f);
    s.hm = clamp01(0.5f + humid_.fbm2(cbx / 430.0f, cbz / 430.0f - 50.0f, 3) * 1.15f);
    s.pv = weird_.fbm2(bx / 260.0f, bz / 260.0f, 2);
    const float land = sm(0.0f, 0.12f, s.c), inland = sm(0.06f, 0.3f, s.c);
    float warm = .5f + temp_.noise2(cbx / 920.0f + 100, cbz / 920.0f) * 1.05f;
    float humid = .5f + humid_.noise2(cbx / 840.0f, cbz / 840.0f - 50) * 1.05f;
    float dryProvince = sm(.59f, .78f, warm) * (1 - sm(.22f, .4f, humid)) * land;
    s.t = std::max(s.t, .82f * dryProvince);
    s.hm += (std::min(s.hm, .15f) - s.hm) * dryProvince;
    s.mount = sm(-0.4f, -0.72f, e) * inland;
    float riverTx = 0, riverTz = 0, riverSlope = 0, riverCX = bx, riverCZ = bz, riverBase = continentHeight(s.c);
    float rDist = 1e9f, rDistV = 1e9f, rW = 0, rOk = 0, rFlood = 64, rShore = 0, rPlain = 0, rValley = 30, rV01 = 0;
    {
        auto field = [&](float px, float pz) {
            float qx = px + warp_.noise2(px / 230.0f, pz / 230.0f) * 45.0f,
                  qz = pz + warp_.noise2(px / 230.0f + 31.7f, pz / 230.0f - 17.3f) * 45.0f;
            qx += warp_.noise2(px / 150.0f - 40.0f, pz / 150.0f) * 22.0f + warp_.noise2(px / 41.0f + 3.0f, pz / 41.0f) * 3.0f;
            qz += warp_.noise2(px / 150.0f, pz / 150.0f + 40.0f) * 22.0f + warp_.noise2(px / 41.0f, pz / 41.0f - 9.0f) * 3.0f;
            return river_.fbm2(qx / 560.0f, qz / 560.0f, 3);
        };
        const float f0 = field(bx, bz);
        const float gx = field(bx + 1.0f, bz) - f0, gz = field(bx, bz + 1.0f) - f0;
        const float gl = std::sqrt(gx * gx + gz * gz);
        riverTx = -gz / std::max(gl, 1e-5f);
        riverTz = gx / std::max(gl, 1e-5f);
        auto potential = [&](float x, float z) {
            float wx = x + warp_.noise2(x / 230.0f, z / 230.0f) * 45.0f,
                  wz = z + warp_.noise2(x / 230.0f + 31.7f, z / 230.0f - 17.3f) * 45.0f;
            return continentHeight(cont_.fbm2(wx / 620.0f, wz / 620.0f, 4) * 1.35f + 0.27f);
        };
        if (std::fabs(f0) < 0.16f && gl > 1e-5f && std::fabs(f0) / gl < 65.0f) {
            riverCX = bx - f0 * gx / (gl * gl);
            riverCZ = bz - f0 * gz / (gl * gl);
            for (int k = 0; k < 2; ++k) {
                float f = field(riverCX, riverCZ);
                float nx = field(riverCX + .5f, riverCZ) - field(riverCX - .5f, riverCZ);
                float nz = field(riverCX, riverCZ + .5f) - field(riverCX, riverCZ - .5f);
                float g2 = nx * nx + nz * nz;
                if (g2 < 1e-10f) break;
                float step = std::clamp(f / g2, -8.0f / std::sqrt(g2), 8.0f / std::sqrt(g2));
                riverCX -= nx * step;
                riverCZ -= nz * step;
                riverTx = -nz / std::sqrt(g2);
                riverTz = nx / std::sqrt(g2);
            }
            riverBase = potential(riverCX, riverCZ);
            riverSlope =
                (potential(riverCX + riverTx * 3, riverCZ + riverTz * 3) - potential(riverCX - riverTx * 3, riverCZ - riverTz * 3)) / 6.0f;
            if (riverSlope > 0) {
                riverTx = -riverTx;
                riverTz = -riverTz;
            }
        }
        rDist = std::fabs(f0) / std::max(gl, 1e-5f);
        rDistV = std::fabs(f0) / std::max(gl, 0.004f);
        rW = 2.0f + 4.5f * sm(-0.45f, 0.55f, river_.noise2(bx / 260.0f + 90.0f, bz / 260.0f)) +
             river_.noise2(bx / 35.0f, bz / 35.0f + 7.0f) * 0.8f;
        float pool = sm(-.18f, .48f, river_.noise2(riverCX / 95 + 231, riverCZ / 95 - 112));
        rW *= 1.0f + .65f * pool;
        rW *= 1.0f + 0.8f * (1.0f - sm(0.0f, 0.15f, s.c));
        const float exists = sm(-0.45f, -0.2f, river_.noise2(bx / 700.0f - 77.0f, bz / 700.0f + 31.0f));
        rOk = std::clamp(sm(-0.02f, 0.06f, s.c) * exists * settings_.rivers, 0.0f, 1.0f);
        rW *= 0.70f + 0.30f * std::min(settings_.rivers, 2.0f);
        rV01 = river_.noise2(bx / 90.0f - 13.0f, bz / 90.0f + 57.0f) * 0.5f + 0.5f;
        rFlood = 63.55f + 0.9f * rV01;
        rShore = 2.5f + rW * 0.4f + 2.0f * rV01;
        rPlain = 2.0f + rW * 0.6f + 9.0f * (1.0f - rV01);
        rValley = std::clamp((continentHeight(s.c) + 5.0f - rFlood) * 5.5f + 34.0f, 36.0f, 150.0f);
        if (rOk > 0) {
            const float keep = sm(rW + rShore + rPlain, rW + rShore + rPlain + rValley * 1.3f, rDistV);
            s.mount *= 1.0f - rOk * (1.0f - keep);
        }
    }
    const float flat = sm(0.1f, 0.5f, e);
    const float detail = detail_.fbm2(bx / 26.0f, bz / 26.0f, 2);
    const float rough = 0.35f + 0.65f * sm(-0.35f, 0.45f, weird_.fbm2(bx / 340.0f + 51.0f, bz / 340.0f - 13.0f, 2));
    const float hx = wx + warp_.noise2(bx / 70.0f + 11.0f, bz / 70.0f) * 14.0f,
                hz = wz + warp_.noise2(bx / 70.0f, bz / 70.0f - 23.0f) * 14.0f;

    float h = continentHeight(s.c);
    float hills = hills_.fbm2(hx / 120.0f, hz / 120.0f, 5, 2.1f, 0.48f);
    float knolls = hills_.fbm2(hx / 38.0f + 70.0f, hz / 38.0f, 3) * 0.35f;
    h += (hills * (8.5f - 5.5f * flat) * rough + knolls * 3.0f * rough) * land + detail * 1.4f * land;
    if (s.mount > 0) {
        float r = 1.0f - std::fabs(mount_.fbm2(hx / 170.0f, hz / 170.0f, 4));
        float round = mount_.fbm2(hx / 260.0f + 7.0f, hz / 260.0f, 3) * 0.5f + 0.5f;
        float ridge = r * r * (3.0f - 2.0f * r);
        h += s.mount * (ridge * 18.0f + round * 14.0f + 2.0f);
    }
    {
        const float tmask =
            sm(0.2f, 0.5f, weird_.noise2(bx / 300.0f + 200.0f, bz / 300.0f - 40.0f)) * land * (1.0f - s.mount * 0.6f) * sm(64.5f, 68.0f, h);
        if (tmask > 0) {
            const float step = 2.5f + 1.5f * (weird_.noise2(bx / 90.0f, bz / 90.0f + 60.0f) * 0.5f + 0.5f);
            float f = (h - 63.0f) / step + detail * 0.15f, fl = std::floor(f);
            float terr = 63.0f + (fl + sm(0.62f, 1.0f, f - fl)) * step;
            h += (terr - h) * tmask * 0.8f;
        }
    }
    s.swamp = sm(0.8f, 0.9f, s.hm) * sm(0.38f, 0.48f, s.t) * (1.0f - sm(0.8f, 0.88f, s.t)) * inland * (1.0f - s.mount);
    h += (62.4f + detail * 1.3f - h) * s.swamp * 0.92f;
    s.desert = sm(0.56f, 0.71f, s.t) * (1.0f - sm(0.29f, 0.48f, s.hm)) * land * (1.0f - s.mount);
    if (s.desert > 0) {
        float wind = bx / 78.0f + bz / 190.0f + dune_.noise2(bx / 230.0f, bz / 230.0f) * .85f;
        float dune = .5f + .5f * std::sin(wind * 2 * PI);
        float basin = sm(-.35f, .30f, dune_.noise2(bx / 210.0f - 31, bz / 210.0f + 13));
        float open = continentHeight(s.c) + hills * 2.4f + dune * dune * (2.5f + basin * 4.0f);
        h += (open - h) * s.desert * .9f;
    }
    s.bad = sm(.58f, .73f, s.t) * (1 - sm(.36f, .56f, s.hm)) * sm(-.10f, .22f, mesa_.noise2(wx / 380.0f + 33, wz / 380.0f - 17)) * inland;
    if (s.bad > 0) {
        float m = sm(-.12f, .20f, mesa_.fbm2(hx / 180.0f, hz / 180.0f, 2));
        float wash = sm(.02f, .12f, std::fabs(mesa_.noise2(hx / 150.0f - 41, hz / 150.0f + 17)));
        float hb = continentHeight(s.c) + hills * 2.0f + m * (12.0f + 8.0f * wash) + 2.0f;
        const float step = 3.0f;
        float f = hb / step, fl = std::floor(f);
        hb = (fl + sm(0.65f, 1.0f, f - fl)) * step;
        h += (hb - h) * s.bad;
    }
    float temperate = sm(.24f, .37f, s.t) * (1 - sm(.62f, .76f, s.t));
    s.chalk = sm(.28f, .58f, weird_.noise2(bx / 340.0f - 211, bz / 340.0f + 87)) * temperate * land * (1 - s.mount) * (1 - s.swamp);
    s.moor = sm(.23f, .55f, weird_.noise2(bx / 285.0f + 319, bz / 285.0f - 121)) * sm(.16f, .28f, s.t) * (1 - sm(.45f, .60f, s.t)) *
             inland * (1 - s.chalk) * (1 - s.swamp);
    h += s.chalk * (3.0f + 5.0f * (hills_.noise2(bx / 80, bz / 80) * .5f + .5f));
    float ribs = 1 - std::fabs(hills_.noise2(bx / 32 + 170, bz / 50 - 80));
    h += s.moor * (ribs * ribs * 6.0f - 1.0f);
    s.autumn = sm(.10f, .44f, weird_.noise2(bx / 420 + 71, bz / 420 - 303)) * temperate * sm(.32f, .46f, s.hm) * (1 - sm(.76f, .9f, s.hm)) *
               land * (1 - s.swamp) * (1 - s.chalk);
    s.heath = sm(.12f, .48f, eros_.noise2(bx / 360 - 171, bz / 360 + 251)) * sm(.18f, .3f, s.t) * (1 - sm(.48f, .62f, s.t)) * inland *
              (1 - s.swamp) * (1 - s.chalk);
    s.volcanic = sm(.22f, .53f, mesa_.noise2(bx / 440 - 291, bz / 440 + 135)) * sm(.35f, .52f, s.t) * sm(-.15f, -.025f, s.c) *
                 (1 - sm(.14f, .28f, s.c));
    float heathRidge = 1 - std::fabs(hills_.noise2(bx / 130 + 83, bz / 180 - 91));
    h += s.heath * (heathRidge * heathRidge * 12 - 2) * (1 - s.mount * .6f);
    float shelf = 3.0f + 9.0f * sm(-.35f, .4f, mesa_.noise2(bx / 110, bz / 110 + 173));
    h += s.volcanic * shelf;
    float lift = std::max(0.0f, riverBase - 65.0f) * 0.38f;
    float falls = sm(0.40f, 0.64f, weird_.noise2(riverCX / 260.0f + 141.0f, riverCZ / 260.0f - 73.0f));
    float step = lift / 2.5f, whole = std::floor(step), part = step - whole;
    float ledge = (whole + sm(0.94f, 1.0f, part)) * 2.5f;
    float riverWater = 63.0f + (lift + (ledge - lift) * falls) * sm(0.10f, 0.95f, rOk);
    rFlood += riverWater - 63.0f;
    float valley = 0;
    if (rOk > 0 && std::min(rDist, rDistV) < rW + rShore + rPlain + rValley) {
        const float w = rW, dist = rDist, flood = rFlood, shoreW = rShore, plainW = rPlain, valleyW = rValley, riverOk = rOk;
        const float depth = std::min(5.5f, 1.2f + w * 0.48f + rV01 * 1.4f);
        float hv = h;
        if (h > flood) {
            float t = std::clamp((rDistV - w - shoreW - plainW) / valleyW, 0.0f, 1.0f);
            float g = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            hv = flood + (h - flood) * g;
            valley = 1.0f - g;
        } else
            valley = 1.0f - sm(w + shoreW, w + shoreW + plainW + valleyW * 0.3f, rDistV);
        float hr;
        if (dist < w) {
            float u = dist / w;
            hr = riverWater - 0.1f - (1.0f - u * u) * depth;
            float bar = sm(.30f, .62f, river_.noise2(riverCX / 63 + 91, riverCZ / 63 - 211));
            float inner = 1 - sm(.08f, .42f, u);
            hr += (riverWater + .35f - hr) * bar * inner * sm(5.0f, 8.0f, w) * (1 - falls);

        } else if (dist < w + shoreW) {
            float u = (dist - w) / shoreW;
            hr = riverWater + 0.05f + (std::max(flood, hv) - (riverWater + 0.05f)) * u * u * (3.0f - 2.0f * u);
        } else {
            hr = hv;
        }
        h += ((dist < w + shoreW ? hr : std::min(hr, h)) - h) * riverOk;
        valley *= riverOk;
        s.river = (1.0f - sm(0.75f, 1.0f, dist / w)) * riverOk;
        s.bank = (1.0f - sm(w + shoreW, w + shoreW + 2.5f + 2.0f * rV01, dist)) * riverOk;
        s.wetNear = (1.0f - sm(w + shoreW + 3.0f, w + shoreW + 9.0f, dist)) * riverOk;
        if (dist < w + shoreW) {
            s.riverLevel = std::max(float(SEA), riverWater * 2.0f);
            s.riverFall = falls;
            float speed = std::clamp(std::fabs(riverSlope) * 2.8f, 0.0f, 0.32f) * (1 - s.swamp) * s.river;
            if (std::fabs(riverSlope) > 0.002f) speed = std::max(speed, 0.055f * s.river * (1 - s.swamp));
            s.flowX = riverTx * speed;
            s.flowZ = riverTz * speed;
        }
    }
    const float seaBlocks = float(SEA) * 0.5f;
    h = seaBlocks + (h - seaBlocks) * settings_.relief;
    h = std::clamp(h, 6.0f, 115.0f);
    s.h = h * 2.0f;
    if (s.riverLevel > SEA) s.riverLevel = float(SEA) + (s.riverLevel - float(SEA)) * settings_.relief;
    const float crags = sm(0.42f, 0.7f, s.pv) * land * rough * (1.0f - s.swamp) * (1.0f - s.desert);
    const float coast = sm(0.4f, 0.6f, s.pv) * (1.0f - sm(0.03f, 0.12f, s.c)) * sm(-0.05f, 0.0f, s.c);
    s.amp = (s.mount * 12.0f + s.bad * 2.5f + crags * 6.0f * (1 - s.bad) + coast * 6.0f) *
            (1.0f - std::max(s.river, sm(0.15f, 0.7f, valley))) * settings_.relief;
    return s;
}

float Generator::overhang(int lx, int ly, int lz) const {
    const float x = lx * 2.0f, y = ly * 2.0f, z = lz * 2.0f;
    return over_.noise3(x / 13.0f, y / 8.0f, z / 13.0f) * 0.75f + over_.noise3(x / 5.0f + 40.0f, y / 4.0f, z / 5.0f) * 0.25f;
}

int Generator::topOf(const Shape& s, int x, int z) const {
    if (s.amp < 0.05f) return std::clamp(int(std::floor(s.h - 0.5f)) + 1, 1, WH - 2);
    const int ylo = std::max(1, int(std::floor(s.h - s.amp - 1))), yhi = std::min(WH - 2, int(std::ceil(s.h + s.amp + 1)));
    const int LX = floorDiv(x, 4), LZ = floorDiv(z, 4);
    const float fx = (x & 3) * 0.25f, fz = (z & 3) * 0.25f;
    int cachedLy = -100;
    float v0[4] = {}, v1[4] = {};
    for (int y = yhi; y >= ylo; --y) {
        int ly = y >> 2;
        if (ly != cachedLy) {
            for (int k = 0; k < 4; ++k) {
                v0[k] = overhang(LX + (k & 1), ly, LZ + (k >> 1));
                v1[k] = overhang(LX + (k & 1), ly + 1, LZ + (k >> 1));
            }
            cachedLy = ly;
        }
        float fy = (y & 3) * 0.25f;
        float a = v0[0] + (v0[1] - v0[0]) * fx, b = v0[2] + (v0[3] - v0[2]) * fx;
        float c = v1[0] + (v1[1] - v1[0]) * fx, d = v1[2] + (v1[3] - v1[2]) * fx;
        float lo = a + (b - a) * fz, hi = c + (d - c) * fz;
        float n = lo + (hi - lo) * fy;
        if ((s.h - (y + 0.5f)) + s.amp * n > 0) return y + 1;
    }
    return ylo;
}

Biome Generator::pickBiome(const Shape& s, int height, int x, int z) const {
    const float bx = x * 0.5f, bz = z * 0.5f;
    const float tj = s.t + detail_.noise2(bx / 7.0f + 300.0f, bz / 7.0f) * 0.03f + detail_.noise2(bx / 26.0f - 120.0f, bz / 26.0f) * 0.05f +
                     (hashf(x, 5, z, uint32_t(seed_)) - 0.5f) * 0.02f;
    const float hj = s.hm + detail_.noise2(bx / 7.0f, bz / 7.0f + 300.0f) * 0.03f +
                     detail_.noise2(bx / 26.0f, bz / 26.0f + 140.0f) * 0.05f + (hashf(x, 6, z, uint32_t(seed_)) - 0.5f) * 0.02f;
    if (s.river > 0.5f && s.c > 0.0f && height < s.riverLevel + 2) return tj < 0.2f ? Biome::FrozenRiver : Biome::River;
    if (height < SEA - 1) {
        if (tj < 0.16f) return Biome::FrozenOcean;
        return s.c < -0.3f ? Biome::DeepOcean : Biome::Ocean;
    }
    if (s.volcanic > .45f && s.river < .35f) return Biome::VolcanicCoast;
    if (height <= SEA + 4 && s.c < 0.1f && s.swamp < 0.5f) {
        if (s.amp > 5.0f) return Biome::StonyShore;
        return tj < 0.2f ? Biome::SnowyBeach : Biome::Beach;
    }
    if (s.mount > 0.4f && height > 160) {
        if (height > 210 || (tj < 0.3f && height > 182)) return Biome::SnowyPeaks;
        if (height > 186) return Biome::StonyPeaks;
        return Biome::WindsweptHills;
    }
    if (s.mount > 0.4f && height > 146) return Biome::WindsweptHills;
    if (s.swamp > 0.5f) return Biome::Swamp;
    if (s.chalk > .48f) return Biome::ChalkDowns;
    if (s.heath > .48f) return Biome::HeatherHighlands;
    if (s.autumn > .48f) return Biome::AutumnWoodland;
    if (s.moor > .46f) return Biome::RockyMoor;
    if (s.bad > 0.5f) return Biome::Badlands;
    if (s.desert > 0.5f) return Biome::Desert;
    if (tj < 0.2f) return hj < 0.45f ? (s.pv > 0.4f ? Biome::IceSpikes : Biome::SnowyPlains) : Biome::SnowyTaiga;
    if (tj < 0.36f) return hj < 0.3f ? Biome::Plains : Biome::Taiga;
    if (tj < 0.64f) {
        if (hj < 0.3f) return (height > 150 || s.pv > 0.3f) ? Biome::Meadow : Biome::Plains;
        if (hj < 0.4f) return s.pv < -0.25f ? Biome::Meadow : Biome::Plains;
        if (hj < 0.62f) return Biome::Forest;
        if (hj < 0.75f) return Biome::BirchForest;
        return Biome::DarkForest;
    }
    if (hj < 0.35f) return Biome::Savanna;
    if (hj < 0.55f) return s.pv > 0.15f ? Biome::Savanna : Biome::Plains;
    return Biome::Jungle;
}

SurfaceInfo Generator::surface(int x, int z) const {
    Shape s = shape(x, z);
    SurfaceInfo si;
    si.height = topOf(s, x, z);
    si.biome = pickBiome(s, si.height, x, z);
    si.temp = s.t;
    si.humid = s.hm;
    return si;
}

void Generator::fillClimate(Column& c) const {
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            auto s = shape(c.cx * CS + x, c.cz * CS + z);
            c.climT[z * CS + x] = uint8_t(s.t * 255.0f + 0.5f);
            c.climH[z * CS + x] = uint8_t(s.hm * 255.0f + 0.5f);
            c.riverLevel[z * CS + x] = uint8_t(std::ceil(s.riverLevel));
            c.riverX[z * CS + x] = int8_t(s.flowX * 100);
            c.riverZ[z * CS + x] = int8_t(s.flowZ * 100);
        }
}

std::unique_ptr<Column> Generator::generate(int cx, int cz) const {
    auto col = std::make_unique<Column>();
    Column& c = *col;
    c.cx = cx;
    c.cz = cz;
    for (auto& s : c.sec) {
        s.blocks.reset(new uint8_t[CS3]);
        std::memset(s.blocks.get(), B_AIR, CS3);
    }
    const uint32_t hs = uint32_t(seed_);
    auto blk = [&](int x, int y, int z) -> uint8_t& { return c.sec[y >> 5].blocks[idx3(x, y & 31, z)]; };

    constexpr int P = CS + 2;
    static thread_local std::vector<Shape> sh;
    sh.resize(size_t(P) * P);
    for (int z = -1; z <= CS; ++z)
        for (int x = -1; x <= CS; ++x) sh[size_t((z + 1) * P + x + 1)] = shape(cx * CS + x, cz * CS + z);
    auto SH = [&](int x, int z) -> const Shape& { return sh[size_t((z + 1) * P + x + 1)]; };

    constexpr int LX = CS / 4 + 1, LY = WH / 4 + 1;
    static thread_local std::vector<float> lat;
    static thread_local std::vector<uint8_t> have;
    lat.assign(size_t(LX) * LY * LX, 0.0f);
    have.assign(size_t(LX) * LY * LX, 0);
    auto latAt = [&](int lx, int ly, int lz) {
        size_t i = size_t((ly * LX + lz) * LX + lx);
        if (!have[i]) {
            lat[i] = overhang(cx * 8 + lx, ly, cz * 8 + lz);
            have[i] = 1;
        }
        return lat[i];
    };
    auto noiseAt = [&](int x, int y, int z) {
        int lx = x >> 2, ly = y >> 2, lz = z >> 2;
        float fx = (x & 3) * 0.25f, fy = (y & 3) * 0.25f, fz = (z & 3) * 0.25f;
        float a = latAt(lx, ly, lz) + (latAt(lx + 1, ly, lz) - latAt(lx, ly, lz)) * fx;
        float b = latAt(lx, ly, lz + 1) + (latAt(lx + 1, ly, lz + 1) - latAt(lx, ly, lz + 1)) * fx;
        float cc = latAt(lx, ly + 1, lz) + (latAt(lx + 1, ly + 1, lz) - latAt(lx, ly + 1, lz)) * fx;
        float d = latAt(lx, ly + 1, lz + 1) + (latAt(lx + 1, ly + 1, lz + 1) - latAt(lx, ly + 1, lz + 1)) * fx;
        float lo = a + (b - a) * fz, hi = cc + (d - cc) * fz;
        return lo + (hi - lo) * fy;
    };

    int tops[CS * CS];
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            const Shape& s = SH(x, z);
            int top = 1;
            if (s.amp < 0.05f) {
                int yfull = std::clamp(int(std::floor(s.h - 0.5f)) + 1, 1, WH - 2);
                for (int y = 1; y < yfull; ++y) blk(x, y, z) = B_STONE;
                top = yfull;
            } else {
                int ylo = std::max(1, int(std::floor(s.h - s.amp - 1))), yhi = std::min(WH - 2, int(std::ceil(s.h + s.amp + 1)));
                for (int y = 1; y < ylo; ++y) blk(x, y, z) = B_STONE;
                top = ylo;
                for (int y = ylo; y <= yhi; ++y)
                    if ((s.h - (y + 0.5f)) + s.amp * noiseAt(x, y, z) > 0) {
                        blk(x, y, z) = B_STONE;
                        top = y + 1;
                    }
            }
            tops[z * CS + x] = top;
        }
    int topsP[P * P];
    for (int z = -1; z <= CS; ++z)
        for (int x = -1; x <= CS; ++x) {
            int v = (x >= 0 && x < CS && z >= 0 && z < CS) ? tops[z * CS + x] : topOf(SH(x, z), cx * CS + x, cz * CS + z);
            topsP[(z + 1) * P + x + 1] = v;
        }

    Biome biomes[CS * CS];
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            const Shape& s = SH(x, z);
            int wx = cx * CS + x, wz = cz * CS + z;
            int top = tops[z * CS + x];
            Biome b = pickBiome(s, top, wx, wz);
            biomes[z * CS + x] = b;
            c.climT[z * CS + x] = uint8_t(s.t * 255.0f + 0.5f);
            c.climH[z * CS + x] = uint8_t(s.hm * 255.0f + 0.5f);
            c.riverLevel[z * CS + x] = uint8_t(std::ceil(s.riverLevel));
            c.riverX[z * CS + x] = int8_t(s.flowX * 100);
            c.riverZ[z * CS + x] = int8_t(s.flowZ * 100);
            auto T = [&](int dx, int dz) { return topsP[(z + 1 + dz) * P + x + 1 + dx]; };
            int slope = std::max(std::abs(T(1, 0) - T(-1, 0)), std::abs(T(0, 1) - T(0, -1)));
            bool steep = slope >= 4;
            float n = hashf(wx, 11, wz, hs);
            float n2 = detail_.noise2(wx * 0.5f / 6.0f + 50.0f, wz * 0.5f / 6.0f) * 0.5f + 0.5f;
            n2 = std::clamp(n2 + (hashf(wx, 12, wz, hs) - 0.5f) * 0.12f, 0.0f, 1.0f);
            const float band = b == Biome::Badlands ? mesa_.noise2(wx * 0.5f / 45.0f + 9.0f, wz * 0.5f / 45.0f) * 5.0f : 0.0f;
            int d = -1;
            for (int y = std::min(WH - 2, top + 1); y >= 1; --y) {
                uint8_t& v = blk(x, y, z);
                if (v == B_AIR) {
                    d = -1;
                    continue;
                }
                ++d;
                if (d > 60) {
                    if (y < top - 80) break;
                    continue;
                }
                uint8_t l = surfaceLayer(b, y, d, steep, n, n2, band);
                if (l) v = l;
            }
            {
                int ty = top - 1;
                uint8_t& tb = blk(x, ty, z);
                float hb = hashf(wx, 21, wz, hs), hn = n2;
                if (tb == B_GRASS || tb == B_SNOWY_GRASS) {
                    if (s.desert > 0.06f && hb < s.desert * 1.5f * (0.5f + hn)) {
                        tb = hn > 0.55f ? B_SAND : B_COARSE_DIRT;
                        if (tb == B_SAND && blk(x, ty - 1, z) == B_DIRT) blk(x, ty - 1, z) = B_SAND;
                    } else if (s.bad > 0.06f && hb < s.bad * 1.5f * (0.5f + hn)) {
                        tb = hn > 0.5f ? B_RED_SAND : B_COARSE_DIRT;
                    } else if (s.swamp > 0.08f && hb < s.swamp * 0.9f * hn) {
                        tb = B_MUD;
                    }
                } else if ((tb == B_SAND && b == Biome::Desert && s.desert < 0.8f && hb < (0.8f - s.desert) * 1.6f * hn) ||
                           (tb == B_MUD && b == Biome::Swamp && s.swamp < 0.75f && hb < (0.75f - s.swamp) * 1.5f)) {
                    tb = B_GRASS;
                } else if (b == Biome::Badlands && s.bad < 0.75f && !steep && hb < (0.75f - s.bad) * 1.4f * hn && def(tb).solid) {
                    tb = B_GRASS;
                }
                if (b == Biome::Desert && s.hm < 0.22f && top <= SEA + 14 && !steep && tb == B_SAND) {
                    float pan = detail_.noise2(wx * 0.5f / 40.0f + 300.0f, wz * 0.5f / 40.0f - 120.0f);
                    float crack = std::fabs(detail_.noise2(wx * 0.5f / 3.0f, wz * 0.5f / 3.0f + 40.0f));
                    if (pan > 0.25f && crack > 0.08f) tb = B_SALT;
                }
                if (s.bank > 0.05f && top <= s.riverLevel + 4 && s.swamp < 0.5f && hb < (s.bank - 0.1f) * 1.7f &&
                    (tb == B_GRASS || tb == B_SNOWY_GRASS || tb == B_DIRT || tb == B_PODZOL || tb == B_COARSE_DIRT || tb == B_MUD)) {
                    const bool cold = s.t < 0.22f;
                    uint8_t m = (cold || hn < 0.3f) ? uint8_t(B_GRAVEL) : (hn > 0.82f ? uint8_t(B_CLAY) : uint8_t(B_SAND));
                    tb = m;
                    uint8_t& below = blk(x, ty - 1, z);
                    if (below == B_DIRT || below == B_GRASS) below = m == B_CLAY ? uint8_t(B_SAND) : m;
                }
            }
            for (int y = int(std::ceil(s.riverLevel)) - 1; y >= 1 && blk(x, y, z) == B_AIR; --y) blk(x, y, z) = B_WATER;

            blk(x, 0, z) = B_BEDROCK;
            for (int y = 1; y < 4; ++y)
                if (hashf(wx, y, wz, hs) < 0.6f - y * 0.15f) blk(x, y, z) = B_BEDROCK;
        }

    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            const auto& s = SH(x, z);
            if (s.river < 0.35f || s.riverLevel <= SEA) continue;
            int level = int(std::ceil(s.riverLevel)), upper = level;
            for (auto d : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const auto& n = SH(x + d.first, z + d.second);
                if (n.river > 0.35f && n.riverLevel - s.riverLevel > 1.5f && n.h < n.riverLevel - 1)
                    upper = std::max(upper, int(std::ceil(n.riverLevel)));
            }
            for (int y = level; y < upper && y < WH - 1; ++y)
                if (blk(x, y, z) == B_AIR) blk(x, y, z) = B_WATER;
        }
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            Biome b = biomes[z * CS + x];
            if (b != Biome::FrozenOcean && b != Biome::FrozenRiver) continue;
            int level = int(std::ceil(SH(x, z).riverLevel)), upper = level, low = level;
            for (auto d : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                const auto& n = SH(x + d.first, z + d.second);
                if (n.river > .35f) {
                    upper = std::max(upper, int(std::ceil(n.riverLevel)));
                    low = std::min(low, int(std::ceil(n.riverLevel)));
                }
            }
            bool cap = false;
            for (int y = std::min(upper, WH - 2); y >= std::max(1, low - 1); --y)
                if (blk(x, y, z) == B_WATER) {
                    blk(x, y, z) = cap ? B_PACKED_ICE : B_ICE;
                    cap = true;
                }
        }
    carveCaves(c, tops, sh.data());
    carveWorms(c, tops, sh.data());
    placeStrata(c);
    placeOres(c);
    decorateCaves(c, topsP);
    placeLandmarks(c);
    placeRuins(c, tops, biomes);
    decorate(c, tops, biomes);

    for (int y = 1; y <= 40 && y < WH; ++y)
        for (int z = 0; z < CS; ++z)
            for (int x = 0; x < CS; ++x) {
                if (blk(x, y, z) != B_LAVA) continue;
                const int nx[6] = {x + 1, x - 1, x, x, x, x}, ny[6] = {y, y, y + 1, y - 1, y, y}, nz[6] = {z, z, z, z, z + 1, z - 1};
                for (int d = 0; d < 6; ++d) {
                    if (nx[d] < 0 || nx[d] >= CS || nz[d] < 0 || nz[d] >= CS || ny[d] < 1 || ny[d] >= WH) continue;
                    if (blk(nx[d], ny[d], nz[d]) == B_WATER) {
                        blk(x, y, z) = B_OBSIDIAN;
                        break;
                    }
                }
            }

    for (int sy = 0; sy < NSEC; ++sy) {
        auto& s = c.sec[sy];
        bool any = false;
        for (int i = 0; i < CS3 && !any; ++i) any = s.blocks[i] == B_WATER || s.blocks[i] == B_LAVA;
        if (!any) continue;
        if (!s.meta) {
            s.meta.reset(new uint8_t[CS3]);
            std::memset(s.meta.get(), 0, CS3);
        }
        for (int i = 0; i < CS3; ++i) {
            if (s.blocks[i] == B_WATER || s.blocks[i] == B_LAVA) {
                int x = i % CS, z = (i / CS) % CS, y = sy * CS + i / CS2;
                uint8_t fluid = s.blocks[i];
                s.meta[i] = (y + 1 >= WH || blk(x, y + 1, z) != fluid) ? FLUID_SOURCE_META : FLUID_FULL_META;
            }
        }
    }
    for (auto& s : c.sec) s.compact();
    c.indexFeatures();
    c.state = ColState::Generated;
    return col;
}

void Generator::carveCaves(Column& c, const int* tops, const Shape* shapes) const {
    const float caveAmt = settings_.caves;
    if (caveAmt <= 0.01f) return;
    constexpr int LX = CS / 4 + 1, LY = WH / 4 + 1;
    static thread_local std::vector<float> tun, big, noo;
    tun.resize(size_t(LX) * LY * LX);
    big.resize(tun.size());
    noo.resize(tun.size());
    for (int ly = 0; ly < LY; ++ly)
        for (int lz = 0; lz < LX; ++lz)
            for (int lx = 0; lx < LX; ++lx) {
                float bx = (c.cx * CS + lx * 4) * 0.5f, by = ly * 4 * 0.5f, bz = (c.cz * CS + lz * 4) * 0.5f;
                float a = cave1_.noise3(bx / 26.0f, by / 18.0f, bz / 26.0f);
                float b = cave2_.noise3(bx / 26.0f + 50.0f, by / 18.0f, bz / 26.0f);
                size_t i = size_t((ly * LX + lz) * LX + lx);
                tun[i] = a * a + b * b;
                big[i] = cave3_.fbm3(bx / 60.0f, by / 32.0f, bz / 60.0f, 2);
                float n1 = noodle1_.noise3(bx / 14.0f, by / 11.0f, bz / 14.0f),
                      n2 = noodle2_.noise3(bx / 14.0f, by / 11.0f, bz / 14.0f + 70.0f);
                noo[i] = n1 * n1 + n2 * n2;
            }
    struct Edges {
        float e0[LY], e1[LY];
    };
    static thread_local Edges eT, eN, eB;
    auto edges = [&](const std::vector<float>& f, Edges& e, int x, int z) {
        int lx = x >> 2, lz = z >> 2;
        float fx = (x & 3) * 0.25f;
        for (int ly = 0; ly < LY; ++ly) {
            const float* r0 = &f[size_t((ly * LX + lz) * LX + lx)];
            const float* r1 = r0 + LX;
            e.e0[ly] = r0[0] + (r0[1] - r0[0]) * fx;
            e.e1[ly] = r1[0] + (r1[1] - r1[0]) * fx;
        }
    };
    float fzCur = 0;
    auto sample = [&](const Edges& e, int y) {
        int ly = y >> 2;
        float fy = (y & 3) * 0.25f;
        float c0 = e.e0[ly] + (e.e0[ly + 1] - e.e0[ly]) * fy, c1 = e.e1[ly] + (e.e1[ly + 1] - e.e1[ly]) * fy;
        return c0 + (c1 - c0) * fzCur;
    };
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            const int wx = c.cx * CS + x, wz = c.cz * CS + z;
            const float bx = wx * 0.5f, bz = wz * 0.5f;
            const int top = tops[z * CS + x];
            edges(tun, eT, x, z);
            edges(noo, eN, x, z);
            edges(big, eB, x, z);
            fzCur = (z & 3) * 0.25f;
            const bool wet = top < SEA + 3;
            const bool riverSide = shapes[size_t((z + 1) * (CS + 2) + x + 1)].wetNear > 0.01f;
            int limit = wet ? top - 10 : top - 7;
            if (riverSide) limit = std::min(limit, SEA - 18);
            float ravD = 1e9f, ravW = 0;
            int ravFloor = WH, ravTopY = -1;
            if (!wet && !riverSide) {
                const float ravShift = (1.0f - caveAmt) * 0.10f;
                float mask = sm(0.64f + ravShift, 0.77f + ravShift, ravine_.noise2(bx / 900.0f + 5.0f, bz / 900.0f)) *
                             sm(0.25f + ravShift, 0.48f + ravShift, ravine_.noise2(bx / 300.0f - 41.0f, bz / 300.0f + 17.0f));
                if (mask > 0) {
                    auto rf = [&](float px, float pz) {
                        return ravine_.noise2(px / 170.0f + ravine_.noise2(px / 60.0f, pz / 60.0f) * 0.35f,
                                              pz / 170.0f + ravine_.noise2(px / 60.0f + 9.0f, pz / 60.0f) * 0.35f);
                    };
                    float r0 = rf(bx, bz);
                    float gx = rf(bx + 1.0f, bz) - r0, gz = rf(bx, bz + 1.0f) - r0;
                    ravD = std::fabs(r0) / std::max(std::sqrt(gx * gx + gz * gz), 1e-5f);
                    ravW = (5.5f + 6.5f * (ravine_.noise2(bx / 70.0f + 33.0f, bz / 70.0f) * 0.5f + 0.5f)) * mask;
                    if (ravD < ravW + 2.0f) {
                        ravFloor = 26 + int((ravine_.noise2(bx / 90.0f - 20.0f, bz / 90.0f) * 0.5f + 0.5f) * 44.0f);
                        ravTopY = top + 1;
                    }
                }
            }
            for (int y = 4; y < WH - 4; ++y) {
                uint8_t& b = c.sec[y >> 5].blocks[idx3(x, y & 31, z)];
                if (b == B_AIR || b == B_WATER || b == B_BEDROCK || b == B_ICE) continue;
                bool carve = false;
                if (y >= ravFloor && y <= ravTopY) {
                    float f = float(y - ravFloor) / float(std::max(1, top - ravFloor));
                    float half = ravW * (0.50f + 0.50f * std::sqrt(f)) + cave3_.noise3(bx / 5.0f, y * 0.5f / 4.0f, bz / 5.0f) * 1.1f;
                    carve = ravD < half;
                }
                if (!carve && y < limit) {
                    float depthFade = y > top - 8 ? 0.6f : 1.0f;
                    carve = y < top - 40 && sample(eT, y) < 0.0085f * caveAmt;
                    if (!carve) carve = sample(eN, y) < 0.0065f * caveAmt * depthFade;
                    if (!carve && y < top - 16)
                        carve = sample(eB, y) > 0.38f - (caveAmt - 1.0f) * 0.075f - 0.045f * sm(110.0f, 60.0f, float(y));
                }
                if (carve) b = y < 22 ? B_LAVA : B_AIR;
            }
        }
}

namespace {
struct WormBall {
    float x, y, z, r;
    bool ent;
    float floorY;
    bool lining;
};
struct RegionWorms {
    std::vector<WormBall> balls;
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
};
}

bool Generator::entranceAt(int rx, int rz, float& px, float& py, float& pz, float& yaw, bool& hillside) const {
    if (settings_.caves <= 0.01f) return false;
    const int RS = 128;
    const uint32_t hs = uint32_t(seed_) ^ 0x3A11CEu;
    const int w = 0;
    if (hashf(rx * 7 + w, 3, rz * 13 + w, hs) >= std::min(0.98f, 0.8f * settings_.caves)) return false;
    float best = -1;
    int bxp = 0, bzp = 0, bh = 0;
    float upYaw = 0;
    for (int k = 0; k < 12; ++k) {
        int sx = rx * RS + 12 + int(hashf(rx * 7 + w, 40 + k, rz * 13 + w, hs) * (RS - 24));
        int sz = rz * RS + 12 + int(hashf(rx * 7 + w, 60 + k, rz * 13 + w, hs) * (RS - 24));
        Shape sh = shape(sx, sz);
        if (sh.h < SEA + 8 || sh.wetNear > 0.0f || sh.bank > 0.0f) continue;
        float hx = shape(sx + 8, sz).h - shape(sx - 8, sz).h, hz = shape(sx, sz + 8).h - shape(sx, sz - 8).h;
        float slope = std::sqrt(hx * hx + hz * hz);
        if (slope > best) {
            best = slope;
            bxp = sx;
            bzp = sz;
            bh = topOf(sh, sx, sz);
            upYaw = std::atan2(hz, hx);
        }
    }
    if (best < 0) return false;
    px = float(bxp);
    pz = float(bzp);
    hillside = best > 5.0f;
    yaw = hillside ? upYaw : hashf(rx * 7 + w, 5, rz * 13 + w, hs) * 6.2831853f;
    if (hillside) {
        px -= std::cos(upYaw) * 3.0f;
        pz -= std::sin(upYaw) * 3.0f;
        py = float(bh) + 2.2f;
    } else
        py = float(bh) + 2.0f;
    return true;
}

void Generator::carveWorms(Column& c, int* tops, const Shape* shapes) const {
    if (settings_.caves <= 0.01f) return;
    const int RS = 128, REACH = 3;
    const int x0 = c.cx * CS, z0 = c.cz * CS, x1 = x0 + CS, z1 = z0 + CS;
    const int rcx = floorDiv(x0, RS), rcz = floorDiv(z0, RS);
    const uint32_t hs = uint32_t(seed_) ^ 0x3A11CEu;
    static thread_local std::unordered_map<int64_t, RegionWorms> cache;
    static thread_local uint64_t cacheSeed = 0;
    const uint64_t cacheTag = seed_ ^ (uint64_t(std::lround(settings_.caves * 1000.0f)) << 32);
    if (cacheSeed != cacheTag || cache.size() > 600) {
        cache.clear();
        cacheSeed = cacheTag;
    }
    auto trace = [&](int rx, int rz) -> const RegionWorms& {
        int64_t key = (int64_t(rx) << 32) ^ int64_t(uint32_t(rz));
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        RegionWorms& out = cache[key];
        uint32_t rh = hash3i(rx, 0, rz, hs);
        int n = std::clamp(int(std::lround((3 + int(rh % 2u)) * settings_.caves)), 1, 7);
        for (int w = 0; w < n; ++w) {
            auto R = [&](int k) { return hashf(rx * 7 + w, k, rz * 13 + w, hs); };
            float px = rx * RS + R(1) * RS, pz = rz * RS + R(2) * RS;
            const bool entrance = w == 0 && R(3) < std::min(0.98f, 0.8f * settings_.caves);
            float py, yaw0 = R(5) * 6.2831853f, pitch0 = (R(6) - 0.5f) * 0.6f;
            bool hillside = false;
            if (entrance) {
                if (!entranceAt(rx, rz, px, py, pz, yaw0, hillside)) continue;
                pitch0 = hillside ? 0.0f : -0.55f - R(6) * 0.15f;
            } else {
                py = 24.0f + R(4) * 130.0f;
            }
            const int len = 130 + int(R(7) * 140);
            const float baseR = entrance ? 6.0f + R(8) * 1.5f : 8.0f + R(8) * 3.0f;
            int branchAt = R(9) < 0.7f ? int(len * (0.25f + R(10) * 0.5f)) : -1;
            struct St {
                float x, y, z, yaw, pitch;
                int left;
                float rs;
            };
            std::vector<St> stack;
            stack.push_back({px, py, pz, yaw0, pitch0, len, 1.0f});
            uint32_t salt = hash3i(rx, w, rz, hs ^ 0x1234u);
            while (!stack.empty()) {
                St st = stack.back();
                stack.pop_back();
                float x = st.x, y = st.y, z = st.z, yaw = st.yaw, pitch = st.pitch, dyaw = 0, dpitch = 0;
                int room = 0;
                float roomR = 1.0f;
                for (int i = 0; i < st.left; ++i, ++salt) {
                    float t = float(i) / float(st.left);
                    float r = baseR * st.rs * (0.85f + 0.45f * std::sin(i * 0.17f + R(11) * 6.0f) * std::sin(i * 0.071f + 1.3f));
                    if (room == 0 && hashf(int(salt), 7, 0, hs ^ 0x51u) < 0.016f) {
                        room = 8 + int(hashf(int(salt), 8, 0, hs) * 10);
                        roomR = 1.8f + hashf(int(salt), 9, 0, hs) * 1.0f;
                    }
                    if (room > 0) {
                        r *= 1.0f + (roomR - 1.0f) * std::sin(3.14159f * float(room) / 18.0f);
                        --room;
                    }
                    r *= 1.0f - 0.55f * sm(0.82f, 1.0f, t);
                    const bool mouthPart = entrance && st.rs > 0.95f && i < 22;
                    if (mouthPart)
                        r = std::max(r, hillside ? 3.8f + 0.8f * (1.0f - float(i) / 22.0f)
                                                 : 3.4f + 3.0f * std::max(0.0f, 1.0f - float(i) / 8.0f));
                    r = std::clamp(r, mouthPart ? 5.0f : (st.rs < 0.95f ? 5.5f : 6.5f), mouthPart ? 7.5f : 30.0f);
                    out.balls.push_back({x, y, z, r, entrance && i < 50, y - r * (mouthPart ? 0.36f : 0.60f), mouthPart});
                    out.x0 = std::min(out.x0, x - r);
                    out.x1 = std::max(out.x1, x + r);
                    out.z0 = std::min(out.z0, z - r);
                    out.z1 = std::max(out.z1, z + r);
                    float step = std::clamp(r * 0.35f, 1.0f, 2.4f);
                    x += std::cos(yaw) * std::cos(pitch) * step;
                    z += std::sin(yaw) * std::cos(pitch) * step;
                    y += std::sin(pitch) * step;
                    dyaw = dyaw * 0.85f + (hashf(int(salt), 1, 0, hs ^ 0x77u) - 0.5f) * 0.14f;
                    dpitch = dpitch * 0.8f + (hashf(int(salt), 2, 0, hs ^ 0x77u) - 0.5f) * 0.1f;
                    yaw += dyaw;
                    pitch = pitch * 0.94f + dpitch;
                    float homeX = (rx + .5f) * RS, homeZ = (rz + .5f) * RS;
                    if (std::abs(x - homeX) > 250 || std::abs(z - homeZ) > 250) {
                        yaw = std::atan2(homeZ - z, homeX - x);
                        dyaw = 0;
                    }
                    if (entrance && st.rs > 0.95f && i < 30) {
                        if (hillside) {
                            yaw = yaw0 + (yaw - yaw0) * 0.3f;
                            pitch = i < 14 ? 0.0f : std::clamp(pitch, -0.32f, -0.12f);
                        } else
                            pitch = i < 12 ? std::clamp(pitch, -0.75f, -0.5f) : std::clamp(pitch, -0.4f, -0.1f);
                    }
                    if (y < 14) pitch = std::fabs(pitch) + 0.05f;
                    if (stack.size() < 3 && i == branchAt) {
                        float side = hashf(int(salt), 3, 0, hs) < 0.5f ? 1.0f : -1.0f;
                        stack.push_back({x, y, z, yaw + side * (0.9f + hashf(int(salt), 5, 0, hs) * 0.8f),
                                         (hashf(int(salt), 4, 0, hs) - 0.5f) * 0.5f, (st.left - i) / 2 + 30, 0.8f});
                        branchAt = -1;
                    }
                }
            }
        }
        return out;
    };
    auto blk = [&](int x, int y, int z) -> uint8_t& { return c.sec[y >> 5].blocks[idx3(x, y & 31, z)]; };
    for (int rz = rcz - 1; rz <= rcz + 1; ++rz)
        for (int rx = rcx - 1; rx <= rcx + 1; ++rx) {
            float ex, ey, ez, yaw;
            bool hill;
            if (!entranceAt(rx, rz, ex, ey, ez, yaw, hill) || !hill) continue;
            const float fx = std::cos(yaw), fz = std::sin(yaw);
            if (ex + 26 < x0 || ex - 26 >= x1 || ez + 26 < z0 || ez - 26 >= z1) continue;
            const float crest = ey + 3.6f + 6.5f;
            for (int z = z0; z < z1; ++z)
                for (int x = x0; x < x1; ++x) {
                    const float dx = x + 0.5f - ex, dz = z + 0.5f - ez;
                    const float f = dx * fx + dz * fz, l = -dx * fz + dz * fx;
                    if (f < -0.5f || f > 24.0f || std::fabs(l) > 16.0f) continue;
                    float ht = crest + (hashf(x, 3, z, hs ^ 0x0C7u) - 0.5f) * 2.0f;
                    ht -= std::max(0.0f, std::fabs(l) - 6.0f) * 1.3f;
                    ht -= std::max(0.0f, f - 12.0f) * 0.9f;
                    const int lx = x - x0, lz = z - z0;
                    int& top = tops[lz * CS + lx];
                    const int ytop = std::min(WH - 3, int(ht));
                    if (ytop <= top || shapes[size_t((lz + 1) * (CS + 2) + lx + 1)].wetNear > 0.0f) continue;
                    for (int y = top; y < ytop; ++y) {
                        float h = hashf(x, y, z, hs ^ 0x7177u);
                        blk(x - x0, y, z - z0) = h < 0.55f ? uint8_t(B_STONE) : h < 0.8f ? uint8_t(B_COBBLE) : uint8_t(B_MOSSY);
                    }
                    if (ytop - 1 >= 0 && hashf(x, 9, z, hs) < 0.35f) blk(x - x0, ytop - 1, z - z0) = B_MOSS;
                    top = ytop;
                }
        }
    for (int rz = rcz - REACH; rz <= rcz + REACH; ++rz)
        for (int rx = rcx - REACH; rx <= rcx + REACH; ++rx) {
            const RegionWorms& rw = trace(rx, rz);
            if (rw.x1 < x0 || rw.x0 >= x1 || rw.z1 < z0 || rw.z0 >= z1) continue;
            for (const WormBall& bl : rw.balls) {
                const float px = bl.x, py = bl.y, pz = bl.z, r = bl.r;
                if (px + r < x0 || px - r >= x1 || pz + r < z0 || pz - r >= z1) continue;
                const float ry = r * 0.8f;
                const float ext = bl.lining ? 1.3f : 1.0f;
                int ax = std::max(x0, int(std::floor(px - r * ext))), bx = std::min(x1 - 1, int(std::ceil(px + r * ext)));
                int az = std::max(z0, int(std::floor(pz - r * ext))), bz = std::min(z1 - 1, int(std::ceil(pz + r * ext)));
                int ay = std::max(5, int(std::floor(std::max(py - ry * ext, bl.floorY - 2.0f)))),
                    by = std::min(WH - 3, int(std::ceil(py + ry * ext)));
                for (int z = az; z <= bz; ++z)
                    for (int x = ax; x <= bx; ++x) {
                        const int lx = x - x0, lz = z - z0;
                        const int top = tops[lz * CS + lx];
                        const bool wet = top < SEA + 3;
                        int lim = bl.ent && !wet ? top + 2 : (wet ? top - 10 : top - 6);
                        if (shapes[size_t((lz + 1) * (CS + 2) + lx + 1)].wetNear > 0.01f) lim = std::min(lim, SEA - 18);
                        float dx = (x + 0.5f - px) / r, dz = (z + 0.5f - pz) / r;
                        float dd = dx * dx + dz * dz;
                        if (dd >= ext * ext) continue;
                        for (int y = ay; y <= by && y < lim; ++y) {
                            float dy = (y + 0.5f - py) / ry;
                            float q = dd + dy * dy;
                            uint8_t& b = blk(lx, y, lz);
                            const bool floorCut = float(y) + 0.5f < bl.floorY;
                            if (q < 1.0f && !floorCut) {
                                if (b == B_AIR || b == B_WATER || b == B_BEDROCK || b == B_ICE || b == B_LAVA) continue;
                                b = y < 22 ? B_LAVA : B_AIR;
                            } else if (bl.lining && q < ext * ext &&
                                       (b == B_DIRT || b == B_GRASS || b == B_SAND || b == B_COARSE_DIRT || b == B_PODZOL ||
                                        b == B_SNOWY_GRASS || b == B_CLAY || b == B_MUD || b == B_RED_SAND)) {
                                float h = hashf(x, y, z, hs ^ 0x7177u);
                                b = h < 0.55f ? uint8_t(B_STONE) : h < 0.8f ? uint8_t(B_COBBLE) : uint8_t(B_MOSSY);
                            }
                        }
                    }
            }
        }
}

const char* caveThemeName(int t) {
    static const char* names[] = {"Dripstone Galleries", "Moss Grottoes", "Crystal Hollows", "Ember Tubes", "Frozen Vaults",
                                  "Mushroom Gardens",    "Fossil Beds",   "Root Hollows",    "Salt Vaults"};
    return names[std::clamp(t, 0, 8)];
}
int Generator::caveTheme(int wx, int wz, float temp) const {
    const float jx = entr_.noise2(wx * 0.5f / 60.0f + 7.0f, wz * 0.5f / 60.0f) * 40.0f,
                jz = entr_.noise2(wx * 0.5f / 60.0f, wz * 0.5f / 60.0f - 7.0f) * 40.0f;
    const int rx = floorDiv(wx + int(jx), 180), rz = floorDiv(wz + int(jz), 180);
    const float h = hashf(rx, 77, rz, uint32_t(seed_) ^ 0x7E3Eu);
    if (temp < 0.28f && h < 0.6f) return CT_FROZEN;
    if (h < 0.10f) return CT_ROOTS;
    if (h < 0.19f) return CT_SALT;
    if (h < 0.26f) return CT_PLAIN;
    if (h < 0.42f) return CT_MOSSY;
    if (h < 0.57f) return CT_CRYSTAL;
    if (h < 0.72f) return CT_LAVA;
    if (h < 0.86f) return CT_MUSHROOM;
    return CT_FOSSIL;
}

void Generator::decorateCaves(Column& c, const int* topsP) const {
    constexpr int P = CS + 2;
    const uint32_t hs = uint32_t(seed_) ^ 0xCA7Eu;
    auto blk = [&](int x, int y, int z) -> uint8_t& { return c.sec[y >> 5].blocks[idx3(x, y & 31, z)]; };
    auto stoneish = [](uint8_t b) {
        return b == B_STONE || b == B_DEEPSLATE || b == B_TUFF || b == B_GRANITE || b == B_DIORITE || b == B_ANDESITE || b == B_DIRT ||
               b == B_GRAVEL || b == B_DRIPSTONE || b == B_COBBLE;
    };
    auto rocky = [](uint8_t b) {
        return b == B_STONE || b == B_DEEPSLATE || b == B_TUFF || b == B_GRANITE || b == B_DIORITE || b == B_ANDESITE;
    };
    {
        const int G = 14;
        for (int z = 0; z < CS; ++z)
            for (int x = 0; x < CS; ++x) {
                const int wx = c.cx * CS + x, wz = c.cz * CS + z;
                const int gx = floorDiv(wx, G), gz = floorDiv(wz, G);
                if (hashf(gx, 91, gz, hs) > 0.12f) continue;
                const float cxp = gx * G + 1.5f + hashf(gx, 92, gz, hs) * 4.0f, czp = gz * G + 1.5f + hashf(gx, 93, gz, hs) * 4.0f;
                const float pr = 1.2f + hashf(gx, 94, gz, hs) * 1.3f;
                const float d = std::sqrt((wx + 0.5f - cxp) * (wx + 0.5f - cxp) + (wz + 0.5f - czp) * (wz + 0.5f - czp));
                if (d > pr + 0.8f) continue;
                const int top = topsP[(z + 1) * P + x + 1];
                const int theme = caveTheme(wx, wz, c.climT[z * CS + x] / 255.0f);
                const uint8_t mat = theme == CT_LAVA      ? B_SCORIA
                                    : theme == CT_FROZEN  ? B_PACKED_ICE
                                    : theme == CT_CRYSTAL ? B_CALCITE
                                                          : B_DRIPSTONE;
                int y = 8;
                while (y < top - 14) {
                    if (blk(x, y, z) != B_AIR || (!rocky(blk(x, y - 1, z)) && blk(x, y - 1, z) != B_DRIPSTONE)) {
                        ++y;
                        continue;
                    }
                    int y1 = y;
                    while (y1 < top - 10 && blk(x, y1, z) == B_AIR) ++y1;
                    const int len = y1 - y;
                    if (len >= 14 && len <= 42 && y1 < top - 10 && blk(x, y1, z) != B_WATER) {
                        for (int k = 0; k < len; ++k) {
                            float t = (k + 0.5f) / float(len);
                            float r = pr * (0.62f + 0.55f * std::fabs(t - 0.5f) * 2.0f);
                            if (d <= r) blk(x, y + k, z) = mat;
                        }
                    }
                    y = y1 + 1;
                }
            }
    }
    auto aqLevel = [&](int wx, int wz) {
        return 40.0f + (entr_.noise2(wx * 0.5f / 500.0f + 13.0f, wz * 0.5f / 500.0f) * 0.5f + 0.5f) * 50.0f;
    };
    auto aqZone = [&](int wx, int wz, int top) {
        return top > SEA + 2 && entr_.noise2(wx * 0.5f / 160.0f - 60.0f, wz * 0.5f / 160.0f + 25.0f) > 0.42f && aqLevel(wx, wz) < top - 14;
    };
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            const int wx = c.cx * CS + x, wz = c.cz * CS + z;
            const float bx = wx * 0.5f, bz = wz * 0.5f;
            const int top = topsP[(z + 1) * P + x + 1];
            const int theme = caveTheme(wx, wz, c.climT[z * CS + x] / 255.0f);
            const float drip = theme == CT_PLAIN || theme == CT_FOSSIL ? entr_.noise2(bx / 110.0f + 40.0f, bz / 110.0f) + 0.15f : -1.0f;
            const float shroom = theme == CT_MUSHROOM ? 1.0f : entr_.noise2(bx / 80.0f - 70.0f, bz / 80.0f + 20.0f) - 0.2f;
            const float damp = theme == CT_MOSSY || theme == CT_ROOTS ? 1.0f
                               : theme == CT_LAVA || theme == CT_FROZEN
                                   ? 0.0f
                                   : c.climH[z * CS + x] / 255.0f + entr_.noise2(bx / 40.0f, bz / 40.0f - 90.0f) * 0.25f;
            const bool zone = theme != CT_LAVA && aqZone(wx, wz, top);
            const int level = int(aqLevel(wx, wz));
            bool edge = false;
            int nbLevel[4] = {-1, -1, -1, -1};
            if (zone) {
                const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (int k = 0; k < 4; ++k) {
                    const auto& d = D4[k];
                    int nt = topsP[(z + 1 + d[1]) * P + x + 1 + d[0]];
                    if (!aqZone(wx + d[0], wz + d[1], nt))
                        edge = true;
                    else
                        nbLevel[k] = int(aqLevel(wx + d[0], wz + d[1]));
                }
            }
            const float pStalac = drip > 0.35f ? 0.09f : 0.006f, pStalag = drip > 0.35f ? 0.06f : 0.004f;
            for (int y = 6; y < std::min(top - 6, WH - 3); ++y) {
                uint8_t& b = blk(x, y, z);
                if (b != B_AIR) continue;
                if (zone && y > level && y > 24 && (nbLevel[0] >= y || nbLevel[1] >= y || nbLevel[2] >= y || nbLevel[3] >= y)) {
                    b = B_STONE;
                    continue;
                }
                if (zone && y <= level && y > 24) {
                    b = edge ? B_STONE : B_WATER;
                    if (!edge && y == 25 && blk(x, 24, z) == B_AIR) blk(x, 24, z) = B_STONE;
                    continue;
                }
                const float h1 = hashf(wx, y, wz, hs), h2 = hashf(wx, y, wz, hs ^ 0x99u), h3 = hashf(wx, y, wz, hs ^ 0x5Au);
                if (theme != CT_PLAIN) {
                    const int D6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    for (int k = 0; k < 6; ++k) {
                        int nx = x + D6[k][0], ny = y + D6[k][1], nz = z + D6[k][2];
                        if (nx < 0 || nz < 0 || nx >= CS || nz >= CS || ny < 5) continue;
                        uint8_t& w = blk(nx, ny, nz);
                        if (!rocky(w) && w != B_DIRT && w != B_GRAVEL) continue;
                        const float hw = hashf(wx + D6[k][0], ny, wz + D6[k][2], hs ^ 0x1D3u);
                        const bool isFloor = k == 3, isCeil = k == 2;
                        switch (theme) {
                        case CT_ROOTS:
                            if (isFloor)
                                w = hw < .6f ? B_MOSS : B_MUD;
                            else if (isCeil || hw < .4f)
                                w = hw < .25f ? B_PODZOL : B_MOSSY;
                            break;
                        case CT_SALT:
                            if (hw < .6f)
                                w = B_SALT;
                            else if (hw < .85f)
                                w = B_CALCITE;
                            break;
                        case CT_MOSSY:
                            if (isFloor)
                                w = hw < 0.8f ? uint8_t(B_MOSS) : uint8_t(B_MUD);
                            else if (hw < (isCeil ? 0.25f : 0.35f))
                                w = hw < 0.2f ? uint8_t(B_MOSSY) : uint8_t(B_MOSS);
                            break;
                        case CT_CRYSTAL:
                            if (hw < 0.12f)
                                w = B_CRYSTAL_ROCK;
                            else if (hw < 0.55f)
                                w = B_CALCITE;
                            break;
                        case CT_LAVA:
                            if (hw < 0.85f) w = B_SCORIA;
                            if (isFloor && hw < 0.14f) w = B_EMBER;
                            break;
                        case CT_FROZEN:
                            if (hw < (isFloor ? 0.7f : 0.45f)) w = B_PACKED_ICE;
                            break;
                        case CT_MUSHROOM:
                            if (isFloor) w = hw < 0.6f ? uint8_t(B_MOSS) : hw < 0.85f ? uint8_t(B_MUD) : w;
                            break;
                        case CT_FOSSIL:
                            if (hw < 0.08f)
                                w = B_FOSSIL;
                            else if (isFloor && hw < 0.4f)
                                w = B_GRAVEL;
                            break;
                        default: break;
                        }
                    }
                }
                const uint8_t below = blk(x, y - 1, z), above = blk(x, y + 1, z);
                const bool floorOk = stoneish(below) || below == B_MOSS || below == B_CALCITE || below == B_SCORIA ||
                                     below == B_PACKED_ICE || below == B_MUD || below == B_CRYSTAL_ROCK || below == B_FOSSIL ||
                                     below == B_SALT;
                const bool ceilOk =
                    stoneish(above) || above == B_CALCITE || above == B_SCORIA || above == B_PACKED_ICE || above == B_CRYSTAL_ROCK;
                int gapDown = 0, gapUp = 0;
                if (ceilOk || above == B_MOSSY || above == B_PODZOL)
                    while (gapDown < 10 && y - gapDown > 5 && blk(x, y - gapDown, z) == B_AIR) ++gapDown;
                if (floorOk)
                    while (gapUp < 10 && y + gapUp < WH - 3 && blk(x, y + gapUp, z) == B_AIR) ++gapUp;
                if (theme == CT_ROOTS && gapDown >= 7 && h1 < .06f) {
                    int n = std::min(gapDown - 4, 2 + int(h2 * 4));
                    for (int k = 0; k < n; ++k) blk(x, y - k, z) = B_VINE;
                    continue;
                }
                if (ceilOk && gapDown >= 7 && h1 < (theme == CT_FROZEN ? 0.05f : pStalac)) {
                    uint8_t m = theme == CT_FROZEN ? B_ICE : theme == CT_LAVA ? B_SCORIA : B_DRIPSTONE;
                    if (m == B_DRIPSTONE) blk(x, y + 1, z) = B_DRIPSTONE;
                    int len = std::min(gapDown - 4, 1 + int(h2 * 4.0f));
                    for (int k = 0; k < len && y - k > 5 && blk(x, y - k, z) == B_AIR && blk(x, y - k - 1, z) == B_AIR; ++k)
                        blk(x, y - k, z) = m;
                    continue;
                }
                if (!floorOk) continue;
                if (theme == CT_CRYSTAL && h3 < 0.07f) {
                    b = B_CRYSTAL;
                    continue;
                }
                if (theme == CT_LAVA && h3 < 0.05f && below == B_SCORIA) {
                    b = B_ASH;
                    continue;
                }
                if (gapUp >= 8 && h1 > 0.5f && h1 < 0.5f + pStalag) {
                    blk(x, y - 1, z) = B_DRIPSTONE;
                    int len = 1 + int(h2 * 3.0f);
                    for (int k = 0; k < len && y + k < top - 6 && blk(x, y + k, z) == B_AIR && blk(x, y + k + 1, z) == B_AIR; ++k)
                        blk(x, y + k, z) = B_DRIPSTONE;
                    continue;
                }
                if (theme == CT_ROOTS && h3 < .035f) {
                    b = B_ROOT;
                    continue;
                }
                if (theme == CT_MUSHROOM && h3 < .09f) {
                    b = B_GLOWSHROOM;
                    continue;
                }
                if (damp > 0.62f && y > 30) {
                    if (h2 < (damp - 0.55f) * 2.2f) {
                        if (below != B_MUD) blk(x, y - 1, z) = B_MOSS;
                        if (h1 > (theme == CT_MOSSY ? 0.965f : 0.985f)) b = B_FERN;
                        continue;
                    }
                }
                if (shroom > 0.15f && y < 110 && h1 > 0.9f - (theme == CT_MUSHROOM ? 0.12f : 0.0f) && h1 < 0.9f + 0.035f * (shroom + 0.5f))
                    b = B_GLOWSHROOM;
            }
            if (drip > 0.45f)
                for (int y = 6; y < std::min(top - 8, WH - 3); ++y) {
                    uint8_t& b = blk(x, y, z);
                    if (b != B_STONE && b != B_DEEPSLATE && b != B_TUFF) continue;
                    if ((blk(x, y + 1, z) == B_AIR || blk(x, y - 1, z) == B_AIR) && hashf(wx, y, wz, hs ^ 0x31u) < 0.55f) b = B_DRIPSTONE;
                }
        }
    for (int tz = 0; tz < CS; tz += 16)
        for (int tx = 0; tx < CS; tx += 16) {
            int wx = c.cx * CS + tx, wz = c.cz * CS + tz;
            uint32_t h = hash3i(wx / 16, 39, wz / 16, hs);
            if (h % 4 != 0) continue;
            int x = tx + 5 + (h >> 8) % 6, z = tz + 5 + (h >> 16) % 6;
            int theme = caveTheme(c.cx * CS + x, c.cz * CS + z, c.climT[z * CS + x] / 255.0f);
            if (theme != CT_MUSHROOM && theme != CT_CRYSTAL && theme != CT_SALT) continue;
            int top = topsP[(z + 1) * P + x + 1];
            for (int y = 26; y < top - 16; ++y) {
                if (!def(blk(x, y - 1, z)).collides || blk(x, y, z) != B_AIR) continue;
                int height = 5 + int((h >> 24) % 3);
                bool clear = true;
                for (int dy = 0; dy <= height + 2 && clear; ++dy)
                    for (int dz = -3; dz <= 3 && clear; ++dz)
                        for (int dx = -3; dx <= 3; ++dx) {
                            if (theme == CT_MUSHROOM && dy < height - 1 && (dx || dz)) continue;
                            if (std::abs(dx) + std::abs(dz) >= 5) continue;
                            auto block = blk(x + dx, y + dy, z + dz);
                            if (block != B_AIR && !isPlantDetail(block)) {
                                clear = false;
                                break;
                            }
                        }
                if (!clear) continue;
                if (theme == CT_MUSHROOM) {
                    for (int dy = 0; dy < height; ++dy) blk(x, y + dy, z) = B_MUSHROOM_STEM;
                    for (int dz = -3; dz <= 3; ++dz)
                        for (int dx = -3; dx <= 3; ++dx)
                            if (std::abs(dx) + std::abs(dz) < 5) {
                                blk(x + dx, y + height, z + dz) = B_MUSHROOM_CAP;
                                if (std::abs(dx) + std::abs(dz) < 3) blk(x + dx, y + height + 1, z + dz) = B_MUSHROOM_CAP;
                            }
                } else {
                    for (int dz = -2; dz <= 2; ++dz)
                        for (int dx = -2; dx <= 2; ++dx) {
                            int n = std::max(0, height - (std::abs(dx) + std::abs(dz)) * 2);
                            for (int dy = 0; dy < n; ++dy)
                                blk(x + dx, y + dy, z + dz) = theme == CT_SALT ? B_SALT : dy == n - 1 ? B_CRYSTAL_ROCK : B_CALCITE;
                        }
                }
                break;
            }
        }
}

void Generator::placeStrata(Column& c) const {
    constexpr int L = CS / 8 + 1, LY = WH / 8 + 1;
    static thread_local std::vector<float> fa, fb;
    fa.resize(size_t(L) * LY * L);
    fb.resize(fa.size());
    for (int ly = 0; ly < LY; ++ly)
        for (int lz = 0; lz < L; ++lz)
            for (int lx = 0; lx < L; ++lx) {
                float x = (c.cx * CS + lx * 8) * 0.5f, y = ly * 8 * 0.5f, z = (c.cz * CS + lz * 8) * 0.5f;
                size_t i = size_t((ly * L + lz) * L + lx);
                fa[i] = strata_.noise3(x / 22.0f, y / 14.0f, z / 22.0f);
                fb[i] = strata2_.noise3(x / 22.0f, y / 14.0f, z / 22.0f);
            }
    static thread_local std::vector<float> pa, pb;
    pa.resize(size_t(LY) * CS2);
    pb.resize(pa.size());
    for (int ly = 0; ly < LY; ++ly)
        for (int z = 0; z < CS; ++z)
            for (int x = 0; x < CS; ++x) {
                int lx = x >> 3, lz = z >> 3;
                float fx = (x & 7) * 0.125f, fz = (z & 7) * 0.125f;
                for (int k = 0; k < 2; ++k) {
                    const std::vector<float>& f = k ? fb : fa;
                    auto at = [&](int ax, int az) { return f[size_t((ly * L + (lz + az)) * L + (lx + ax))]; };
                    float a = at(0, 0) + (at(1, 0) - at(0, 0)) * fx, b = at(0, 1) + (at(1, 1) - at(0, 1)) * fx;
                    (k ? pb : pa)[size_t(ly) * CS2 + z * CS + x] = a + (b - a) * fz;
                }
            }
    auto sample = [&](const std::vector<float>& plane, int x, int y, int z) {
        int ly = y >> 3;
        float fy = (y & 7) * 0.125f;
        float lo = plane[size_t(ly) * CS2 + z * CS + x], hi = plane[size_t(ly + 1) * CS2 + z * CS + x];
        return lo + (hi - lo) * fy;
    };
    const uint32_t hs = uint32_t(seed_) ^ 0x5717A;
    for (int y = 1; y < WH - 8; ++y)
        for (int z = 0; z < CS; ++z)
            for (int x = 0; x < CS; ++x) {
                uint8_t& b = c.sec[y >> 5].blocks[idx3(x, y & 31, z)];
                if (b != B_STONE) continue;
                int wx = c.cx * CS + x, wz = c.cz * CS + z;
                bool deep = y < 36 || (y < 48 && hashf(wx, y, wz, hs) < float(48 - y) / 12.0f);
                float a = sample(pa, x, y, z), bb = sample(pb, x, y, z);
                if (deep)
                    b = (bb < -0.55f) ? B_TUFF : B_DEEPSLATE;
                else if (a > 0.55f)
                    b = B_GRANITE;
                else if (a < -0.55f)
                    b = B_DIORITE;
                else if (bb > 0.55f)
                    b = B_ANDESITE;
                else if (bb < -0.6f && y < 70)
                    b = B_TUFF;
            }
}

void Generator::placeOres(Column& c) const {
    struct Ore {
        uint8_t b;
        int tries, minY, maxY, size;
    };
    static const Ore ores[] = {{B_COAL_ORE, 24, 40, 230, 14}, {B_IRON_ORE, 16, 4, 140, 9},    {B_GOLD_ORE, 5, 4, 64, 7},
                               {B_DIAMOND_ORE, 3, 4, 34, 6},  {B_GRAVEL, 5, 30, 200, 24},     {B_DIRT, 6, 60, 200, 24},
                               {B_CLAY, 2, 100, 140, 14},     {B_COPPER_ORE, 9, 70, 190, 11}, {B_BASALT, 7, 8, 90, 26},
                               {B_NITRE_ORE, 6, 20, 120, 9},  {B_SULFUR, 5, 4, 44, 8},        {B_RUNE_STONE, 1, 6, 56, 3},
                               {B_AMBER, 4, 70, 200, 6}};
    auto host = [](uint8_t b) {
        return b == B_STONE || b == B_DEEPSLATE || b == B_GRANITE || b == B_DIORITE || b == B_ANDESITE || b == B_TUFF;
    };
    auto hostFor = [&](const Ore& o, uint8_t b) {
        if (o.b == B_AMBER) return b == B_DIRT || b == B_PODZOL || b == B_COARSE_DIRT || b == B_CLAY;
        return host(b);
    };
    Rng rng(hash3i(c.cx, 99, c.cz, uint32_t(seed_)) | 1ull);
    for (const Ore& o : ores)
        for (int t = 0; t < o.tries; ++t) {
            int x = rng.rangeInt(0, CS - 1), z = rng.rangeInt(0, CS - 1), y = rng.rangeInt(o.minY, o.maxY);
            for (int i = 0; i < o.size; ++i) {
                for (int d = 0; d < 8; ++d) {
                    int px = x + (d & 1), py = y + ((d >> 1) & 1), pz = z + (d >> 2);
                    if (px < 0 || px >= CS || pz < 0 || pz >= CS || py < 1 || py >= WH) continue;
                    uint8_t& b = c.sec[py >> 5].blocks[idx3(px, py & 31, pz)];
                    if (hostFor(o, b)) b = o.b;
                }
                int dir = rng.rangeInt(0, 5);
                x += dir == 0 ? 1 : dir == 1 ? -1 : 0;
                y += dir == 2 ? 1 : dir == 3 ? -1 : 0;
                z += dir == 4 ? 1 : dir == 5 ? -1 : 0;
            }
        }
}

void Generator::tree(Column& c, int kind, int tx, int ty, int tz, uint32_t h) const {
    auto put = [&](int wx, int y, int wz, uint8_t b, bool force) {
        int lx = wx - c.cx * CS, lz = wz - c.cz * CS;
        if (lx < 0 || lx >= CS || lz < 0 || lz >= CS || y < 1 || y >= WH - 1) return;
        uint8_t& cur = c.sec[y >> 5].blocks[idx3(lx, y & 31, lz)];
        if (force ? (def(cur).replaceable || def(cur).render == Render::Leaves) : (cur == B_AIR || def(cur).render == Render::Cross))
            cur = b;
    };
    const uint32_t hs = uint32_t(seed_) ^ 0x51ED;
    Rng rng(uint64_t(h) * 2654435761ull + 17);
    struct P3 {
        int x, y, z;
    };
    std::vector<P3> logs;
    auto log = [&](int x, int y, int z, uint8_t b) {
        put(x, y, z, b, true);
        logs.push_back({x, y, z});
    };
    auto logAxis = [&](int x, int y, int z, uint8_t b, int axis) {
        int lx = x - c.cx * CS, lz = z - c.cz * CS;
        if (lx < 0 || lx >= CS || lz < 0 || lz >= CS || y < 1 || y >= WH - 1) {
            logs.push_back({x, y, z});
            return;
        }
        uint8_t& cur = c.sec[y >> 5].blocks[idx3(lx, y & 31, lz)];
        if (def(cur).replaceable || def(cur).render == Render::Leaves) {
            cur = b;
            c.sec[y >> 5].setMeta(idx3(lx, y & 31, lz), axisMeta(axis, x, y, z));
        }
        logs.push_back({x, y, z});
    };
    auto trunk = [&](int x, int z, int w, int y0, int height, uint8_t b) {
        for (int y = 0; y < height; ++y)
            for (int dz = 0; dz < w; ++dz)
                for (int dx = 0; dx < w; ++dx) log(x + dx, y0 + y, z + dz, b);
    };
    auto blob = [&](float cx, float cy, float cz, float r, float ry, uint8_t leaf, float holes) {
        int R = int(std::ceil(r)) + 1, RY = int(std::ceil(ry)) + 1;
        int x0 = int(std::floor(cx)) - R, y0 = int(std::floor(cy)) - RY, z0 = int(std::floor(cz)) - R;
        int GX = 2 * R + 2, GY = 2 * RY + 2;
        std::vector<uint8_t> g(size_t(GX) * GY * GX, 0);
        auto I = [&](int x, int y, int z) { return size_t((y * GX + z) * GX + x); };
        for (int y = 0; y < GY; ++y)
            for (int z = 0; z < GX; ++z)
                for (int x = 0; x < GX; ++x) {
                    float px = (x0 + x + 0.5f - cx) / r, py = (y0 + y + 0.5f - cy) / ry, pz = (z0 + z + 0.5f - cz) / r;
                    float d2 = px * px + py * py + pz * pz;
                    d2 += over_.noise3((x0 + x) * 0.35f, (y0 + y) * 0.35f, (z0 + z) * 0.35f) * 0.5f;
                    if (d2 > 1.0f) continue;
                    if (d2 > 0.6f && hashf(x0 + x, y0 + y, z0 + z, hs) < holes) continue;
                    g[I(x, y, z)] = 1;
                }
        std::vector<int> q;
        for (const P3& l : logs) {
            int x = l.x - x0, y = l.y - y0, z = l.z - z0;
            if (x < 0 || y < 0 || z < 0 || x >= GX || y >= GY || z >= GX) continue;
            g[I(x, y, z)] = 3;
            q.push_back(int(I(x, y, z)));
        }
        for (size_t k = 0; k < q.size(); ++k) {
            int i = q[k], x = i % GX, z = (i / GX) % GX, y = i / (GX * GX);
            const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& d : D) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= GX || ny >= GY || nz >= GX || g[I(nx, ny, nz)] != 1) continue;
                g[I(nx, ny, nz)] = 2;
                q.push_back(int(I(nx, ny, nz)));
            }
        }
        for (int y = 0; y < GY; ++y)
            for (int z = 0; z < GX; ++z)
                for (int x = 0; x < GX; ++x)
                    if (g[I(x, y, z)] == 2) put(x0 + x, y0 + y, z0 + z, leaf, false);
    };
    auto line = [&](int x0, int y0, int z0, int x1, int y1, int z1, uint8_t b) {
        int n = std::max({std::abs(x1 - x0), std::abs(y1 - y0), std::abs(z1 - z0)});
        for (int i = 0; i <= n; ++i) {
            float t = n ? float(i) / n : 0.0f;
            int x = int(std::lround(x0 + (x1 - x0) * t)), y = int(std::lround(y0 + (y1 - y0) * t)),
                z = int(std::lround(z0 + (z1 - z0) * t));
            for (int d = 0; d < 4; ++d) log(x + (d & 1), y, z + (d >> 1), b);
        }
    };
    const int maxTop = WH - 8;

    switch (kind) {
    case 3: {
        int variant = int((h >> 6) % 4), height = variant == 1 ? 5 + int(h % 2) : 2 + int(h % 3);
        if (variant >= 2) {
            put(tx, ty, tz, B_BARREL_CACTUS, false);
            if (h & 16) put(tx + 1, ty, tz + 1, B_BARREL_CACTUS, false);
            return;
        }
        for (int y = 0; y < height; ++y) put(tx, ty + y, tz, B_CACTUS, true);
        if (variant == 1)
            for (int side : {-1, 1}) {
                int level = side < 0 ? 2 : 3;
                put(tx + side, ty + level, tz, B_CACTUS, true);
                put(tx + side * 2, ty + level, tz, B_CACTUS, true);
                put(tx + side * 2, ty + level + 1, tz, B_CACTUS, true);
                if ((h & 1) || side < 0) put(tx + side * 2, ty + level + 2, tz, B_CACTUS_FLOWER, false);
            }
        if ((h & 3) != 0) put(tx, ty + height, tz, B_CACTUS_FLOWER, false);
        return;
    }
    case 12: {
        float r0 = 1.5f + rng.uniform() * 2.5f;
        int height = 12 + int(rng.uniform() * 26);
        if (rng.uniform() < 0.1f) {
            height += 25;
            r0 += 2;
        }
        height = std::min(height, maxTop - ty);
        for (int y = -3; y < height; ++y) {
            float r = r0 * std::pow(1.0f - float(std::max(y, 0)) / height, 0.75f);
            int ir = int(std::ceil(r));
            for (int dz = -ir; dz <= ir; ++dz)
                for (int dx = -ir; dx <= ir; ++dx)
                    if (dx * dx + dz * dz <= r * r + 0.3f) put(tx + dx, ty + y, tz + dz, B_PACKED_ICE, true);
        }
        return;
    }
    case 13: {
        float r = 2.2f + rng.uniform() * 2.3f;
        int ir = int(std::ceil(r));
        for (int y = -ir; y <= ir; ++y)
            for (int dz = -ir; dz <= ir; ++dz)
                for (int dx = -ir; dx <= ir; ++dx)
                    if (dx * dx + y * y * 1.3f + dz * dz <= r * r)
                        put(tx + dx, ty + y + 1, tz + dz, hashf(tx + dx, ty + y, tz + dz, hs) < 0.55f ? B_MOSSY : B_COBBLE, true);
        return;
    }
    case 2:
    case 14: {
        bool mega = kind == 14;
        int w = mega ? 4 : 2;
        int height = mega ? 26 + int(h % 10) : 12 + int(h % 9);
        height = std::min(height, maxTop - ty - 4);
        trunk(tx, tz, w, ty, height, B_SPRUCE_LOG);
        float cxp = tx + w * 0.5f, czp = tz + w * 0.5f;
        int topY = ty + height;
        int start = ty + (mega ? height / 3 : 4);
        float rMax = mega ? 8.0f : 5.0f;
        for (int y = start; y <= topY + 2; ++y) {
            float t = float(y - start) / float(topY + 2 - start);
            float r = 1.0f + (1.0f - t) * rMax;
            if (((y - ty) / 2) % 2 == 1) r *= 0.7f;
            r *= 0.8f + 0.4f * hashf(tx, (y - ty) / 2, tz, hs ^ 0x77u);
            r = std::max(r, w * 0.5f + 0.4f);
            int ir = int(std::ceil(r * 1.25f)) + w;
            for (int dz = -ir; dz <= ir; ++dz)
                for (int dx = -ir; dx <= ir; ++dx) {
                    float px = tx + dx + 0.5f - cxp, pz = tz + dz + 0.5f - czp;
                    float ang = std::atan2(pz, px);
                    float reach =
                        1.0f + 0.28f * std::sin(ang * 3.0f + float(h % 7) + y * 0.9f) + 0.25f * (hashf(tx + dx, y, tz + dz, hs) - 0.5f);
                    if (px * px + pz * pz <= r * r * reach * reach)
                        put(tx + dx, y, tz + dz, B_SPRUCE_LEAVES, false);
                    else if (px * px + pz * pz <= (r + 1.0f) * (r + 1.0f) && reach > 1.15f && hashf(tx + dx, y, tz + dz, hs ^ 3u) < 0.3f)
                        put(tx + dx, y - 1, tz + dz, B_SPRUCE_LEAVES, false);
                }
        }
        for (int dz = 0; dz < w; ++dz)
            for (int dx = 0; dx < w; ++dx) put(tx + dx, topY + 3, tz + dz, B_SPRUCE_LEAVES, false);
        return;
    }
    case 6: {
        int up = 6 + int(h % 4);
        trunk(tx, tz, 2, ty, up, B_ACACIA_LOG);
        int dir = int((h >> 4) & 3);
        int ddx = dir == 0 ? 1 : dir == 1 ? -1 : 0, ddz = dir == 2 ? 1 : dir == 3 ? -1 : 0;
        int x = tx, y = ty + up, z = tz;
        int lean = 3 + int((h >> 8) % 4);
        for (int i = 0; i < lean; ++i) {
            x += ddx;
            z += ddz;
            for (int d = 0; d < 4; ++d) log(x + (d & 1), y, z + (d >> 1), B_ACACIA_LOG);
            ++y;
            for (int d = 0; d < 4; ++d) log(x + (d & 1), y, z + (d >> 1), B_ACACIA_LOG);
        }
        blob(x + 1.0f, y + 1.0f, z + 1.0f, 5.5f, 1.6f, B_ACACIA_LEAVES, 0.25f);
        if (h & 0x10000) {
            int x2 = tx - ddx * 3, z2 = tz - ddz * 3, y2 = ty + up - 1;
            line(tx, ty + up - 2, tz, x2, y2, z2, B_ACACIA_LOG);
            blob(x2 + 1.0f, y2 + 1.0f, z2 + 1.0f, 3.6f, 1.3f, B_ACACIA_LEAVES, 0.25f);
        }
        return;
    }
    case 10: {
        for (int d = 0; d < 4; ++d) log(tx + (d & 1), ty, tz + (d >> 1), B_JUNGLE_LOG);
        blob(tx + 1.0f, ty + 1.0f, tz + 1.0f, 3.0f + rng.uniform(), 2.2f, B_JUNGLE_LEAVES, 0.2f);
        return;
    }
    default: break;
    }

    struct Sp {
        uint8_t logB, leaf;
        int w, hMin, hVar, branches;
        float branchLen, clumpR, clumpRy, holes, crook, upBias;
        bool roots;
    };
    Sp sp{B_LOG, B_LEAVES, 2, 9, 5, 3, 5.5f, 2.9f, 2.2f, 0.3f, 0.35f, 0.45f, true};
    switch (kind) {
    case 1: sp = {B_BIRCH_LOG, B_BIRCH_LEAVES, 2, 11, 5, 3, 3.5f, 2.4f, 2.5f, 0.3f, 0.12f, 0.8f, false}; break;
    case 15: sp = {B_LOG, B_AUTUMN_LEAVES, 2, 10, 5, 4, 5.0f, 2.8f, 2.2f, .24f, .3f, .45f, true}; break;
    case 9: sp = {B_BIRCH_LOG, B_BIRCH_LEAVES, 2, 16, 7, 4, 3.8f, 2.3f, 2.7f, 0.3f, 0.1f, 0.8f, false}; break;
    case 4: sp = {B_JUNGLE_LOG, B_JUNGLE_LEAVES, 2, 14, 9, 3, 5.0f, 3.2f, 2.0f, 0.2f, 0.22f, 0.35f, true}; break;
    case 5: sp = {B_JUNGLE_LOG, B_JUNGLE_LEAVES, 4, 30, 14, 6, 9.0f, 4.2f, 2.4f, 0.2f, 0.15f, 0.3f, true}; break;
    case 7: sp = {B_DARK_OAK_LOG, B_DARK_OAK_LEAVES, 4, 9, 4, 6, 6.5f, 3.6f, 2.2f, 0.15f, 0.3f, 0.25f, true}; break;
    case 8: sp = {B_LOG, B_LEAVES, 2, 8, 4, 4, 5.0f, 3.0f, 1.8f, 0.3f, 0.55f, 0.15f, true}; break;
    case 11: sp = {B_LOG, B_LEAVES, 2, 14, 5, 6, 7.0f, 3.2f, 2.5f, 0.3f, 0.45f, 0.4f, true}; break;
    default: break;
    }
    const uint8_t logB = sp.logB, leaf = sp.leaf;
    int height = sp.hMin + int(h % uint32_t(sp.hVar));
    height = std::min(height, maxTop - ty - 8);
    if (height < 4) return;
    std::vector<P3> spine;
    {
        float fx = float(tx), fz = float(tz);
        float a = rng.uniform() * 6.2831853f;
        float dx = std::cos(a) * sp.crook, dz = std::sin(a) * sp.crook;
        int ix = tx, iz = tz;
        for (int y = 0; y < height; ++y) {
            int w = sp.w;
            if (sp.w > 2 && y > height * 0.6f) w = 2;
            if (y > 2) {
                fx += dx * 0.35f;
                fz += dz * 0.35f;
                dx += (rng.uniform() - 0.5f) * sp.crook * 0.5f;
                dz += (rng.uniform() - 0.5f) * sp.crook * 0.5f;
            }
            int nx = int(std::lround(fx)), nz = int(std::lround(fz));
            if (nx != ix && nz != iz) nz = iz;
            ix = std::clamp(nx, ix - 1, ix + 1);
            iz = std::clamp(nz, iz - 1, iz + 1);
            int off = (sp.w - w) / 2;
            for (int k = 0; k < w * w; ++k) log(ix + off + k % w, ty + y, iz + off + k / w, logB);
            spine.push_back({ix + off, ty + y, iz + off});
        }
    }
    if (sp.roots) {
        for (int side = 0; side < 4; ++side) {
            if (hashf(tx, side, tz, hs ^ 0x700u) < 0.35f) continue;
            int sx = side == 0 ? -1 : side == 1 ? sp.w : 0, sz = side == 2 ? -1 : side == 3 ? sp.w : 0;
            int along = int(hashf(tx, side, tz, hs ^ 0x701u) * sp.w);
            int x = tx + (sx == 0 ? along : sx), z = tz + (sz == 0 ? along : sz);
            logAxis(x, ty, z, logB, sx != 0 ? 1 : 2);
            if (sp.w > 2 || hashf(tx, side, tz, hs ^ 0x702u) < 0.4f) log(x, ty + 1, z, logB);
            if (sp.w > 2) logAxis(x + (sx < 0 ? -1 : sx > 0 ? 1 : 0), ty, z + (sz < 0 ? -1 : sz > 0 ? 1 : 0), logB, sx != 0 ? 1 : 2);
        }
    }
    const P3 top = spine.back();
    const float cx = top.x + 1.0f, cz = top.z + 1.0f;
    const int topY = ty + height;
    struct Tip {
        float x, y, z, r;
    };
    std::vector<Tip> tips;
    auto limb = [&](float x, float y, float z, float ax, float ay, float az, float len, int depth) {
        auto go = [&](auto& self, float x0, float y0, float z0, float dx, float dy, float dz, float l, int d) -> void {
            int px = int(std::floor(x0)), py = int(std::floor(y0)), pz = int(std::floor(z0));
            float forkAt = d < 1 && l > 3.5f ? l * (0.45f + rng.uniform() * 0.25f) : -1.0f;
            for (float t = 0; t < l; t += 1.0f) {
                x0 += dx;
                y0 += dy;
                z0 += dz;
                dx += (rng.uniform() - 0.5f) * 0.25f;
                dz += (rng.uniform() - 0.5f) * 0.25f;
                dy += (rng.uniform() - 0.5f) * 0.15f - 0.02f;
                float m = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= m;
                dy /= m;
                dz /= m;
                int qx = int(std::floor(x0)), qy = int(std::floor(y0)), qz = int(std::floor(z0));
                while (px != qx || py != qy || pz != qz) {
                    int axis;
                    if (px != qx) {
                        px += qx > px ? 1 : -1;
                        axis = 1;
                    } else if (pz != qz) {
                        pz += qz > pz ? 1 : -1;
                        axis = 2;
                    } else {
                        py += qy > py ? 1 : -1;
                        axis = 0;
                    }
                    if (axis == 0)
                        log(px, py, pz, logB);
                    else
                        logAxis(px, py, pz, logB, axis);
                }
                if (forkAt > 0 && t >= forkAt) {
                    float fa = rng.uniform() < 0.5f ? 0.9f : -0.9f;
                    float ca = std::cos(fa), sa = std::sin(fa);
                    self(self, x0, y0, z0, dx * ca - dz * sa, dy + 0.15f, dx * sa + dz * ca, (l - t) * 0.7f, d + 1);
                    forkAt = -1;
                }
            }
            tips.push_back({x0, y0, z0, sp.clumpR * (d > 0 ? 0.85f : 1.0f) * (0.85f + rng.uniform() * 0.3f)});
        };
        go(go, x, y, z, ax, ay, az, len, depth);
    };
    const float golden = 2.39996f;
    float a0 = rng.uniform() * 6.2831853f;
    for (int i = 0; i < sp.branches; ++i) {
        float tt = 0.42f + 0.5f * (float(i) + rng.uniform() * 0.6f) / float(sp.branches);
        int lvl = std::clamp(int(tt * height), 2, height - 1);
        const P3& at = spine[size_t(lvl)];
        float a = a0 + i * golden + (rng.uniform() - 0.5f) * 0.6f;
        float up = sp.upBias + rng.uniform() * 0.3f;
        float dx = std::cos(a), dz = std::sin(a);
        float m = std::sqrt(1.0f + up * up);
        float len = sp.branchLen * (0.7f + rng.uniform() * 0.6f) * (1.1f - tt * 0.4f);
        limb(at.x + 1.0f, float(at.y) + 0.5f, at.z + 1.0f, dx / m, up / m, dz / m, len, 0);
    }
    limb(cx, float(topY) - 0.5f, cz, (rng.uniform() - 0.5f) * 0.4f, 1.0f, (rng.uniform() - 0.5f) * 0.4f, 2.0f + rng.uniform() * 2.0f, 1);
    for (const Tip& t : tips) blob(t.x, t.y + sp.clumpRy * 0.35f, t.z, t.r, sp.clumpRy, leaf, sp.holes);
    blob(cx, float(topY) + 1.0f, cz, sp.clumpR * 1.25f, sp.clumpRy * 1.15f, leaf, sp.holes);
    if (kind == 4 || kind == 5) {
        for (const Tip& t : tips) {
            int n = 2 + int(rng.uniform() * 3);
            for (int k = 0; k < n; ++k) {
                int vx = int(std::floor(t.x + (rng.uniform() - 0.5f) * t.r * 1.6f)),
                    vz = int(std::floor(t.z + (rng.uniform() - 0.5f) * t.r * 1.6f));
                int vy = int(std::floor(t.y)) - 1;
                int len = 3 + int(rng.uniform() * 8);
                for (int q = 0; q < len; ++q) put(vx, vy - q, vz, B_VINE, false);
            }
        }
    }
    if (kind == 8) {
        for (int i = 0; i < 12; ++i) {
            const Tip& t = tips[size_t(i) % tips.size()];
            float a = rng.uniform() * 6.2831853f;
            int hx = int(std::floor(t.x + std::cos(a) * (t.r - 0.5f))), hz = int(std::floor(t.z + std::sin(a) * (t.r - 0.5f)));
            int y0 = int(std::floor(t.y)) - 1;
            int len = 2 + int(rng.uniform() * 6);
            for (int k = 0; k < len; ++k) put(hx, y0 - k, hz, B_VINE, false);
        }
    }
}

bool Generator::ruinSite(int rx, int rz, int& x0, int& z0, int& x1, int& z1, bool& oldWorld) const {
    const int RS = RUIN_REGION;
    const uint32_t hs = uint32_t(seed_) ^ 0x2B1Eu;
    auto R = [&](int k) { return hashf(rx, k, rz, hs); };
    if (settings_.structures <= 0.01f || R(0) > std::min(1.0f, 0.62f * settings_.structures)) return false;
    const int w = 12 + int(R(1) * 14), d = 12 + int(R(2) * 14);
    x0 = rx * RS + 40 + int(R(3) * (RS - 80 - w));
    z0 = rz * RS + 40 + int(R(4) * (RS - 80 - d));
    x1 = x0 + w - 1;
    z1 = z0 + d - 1;
    oldWorld = R(12) < 0.4f;
    int hmin = 1 << 20, hmax = -(1 << 20);
    for (int k = 0; k < 5; ++k) {
        int sx = k == 4 ? (x0 + x1) / 2 : (k & 1 ? x1 : x0), sz = k == 4 ? (z0 + z1) / 2 : (k & 2 ? z1 : z0);
        Shape sh = shape(sx, sz);
        if (sh.river > 0.05f || sh.amp > 3.0f) return false;
        int t = topOf(sh, sx, sz);
        hmin = std::min(hmin, t);
        hmax = std::max(hmax, t);
    }
    return hmin >= SEA + 2 && hmax - hmin <= 20;
}

void Generator::placeRuins(Column& c, const int* tops, const Biome* biomes) const {
    const int RS = RUIN_REGION;
    const uint32_t hs = uint32_t(seed_) ^ 0x2B1Eu;
    const int x0c = c.cx * CS, z0c = c.cz * CS;
    auto blk = [&](int x, int y, int z) -> uint8_t& { return c.sec[y >> 5].blocks[idx3(x, y & 31, z)]; };
    for (int rz = floorDiv(z0c, RS) - 1; rz <= floorDiv(z0c + CS, RS); ++rz)
        for (int rx = floorDiv(x0c, RS) - 1; rx <= floorDiv(x0c + CS, RS); ++rx) {
            auto R = [&](int k) { return hashf(rx, k, rz, hs); };
            if (settings_.structures <= 0.01f || R(0) > std::min(1.0f, 0.62f * settings_.structures)) continue;
            const int w = 12 + int(R(1) * 14), d = 12 + int(R(2) * 14);
            const int bx0 = rx * RS + 40 + int(R(3) * (RS - 80 - w)), bz0 = rz * RS + 40 + int(R(4) * (RS - 80 - d));
            const int bx1 = bx0 + w - 1, bz1 = bz0 + d - 1;
            if (bx1 + 4 < x0c || bx0 - 4 >= x0c + CS || bz1 + 4 < z0c || bz0 - 4 >= z0c + CS) continue;
            int hmin = 1 << 20, hmax = -(1 << 20);
            for (int k = 0; k < 5; ++k) {
                int sx = k == 4 ? (bx0 + bx1) / 2 : (k & 1 ? bx1 : bx0), sz = k == 4 ? (bz0 + bz1) / 2 : (k & 2 ? bz1 : bz0);
                Shape sh = shape(sx, sz);
                if (sh.river > 0.05f || sh.amp > 3.0f) {
                    hmin = -1000;
                    break;
                }
                int t = topOf(sh, sx, sz);
                hmin = std::min(hmin, t);
                hmax = std::max(hmax, t);
            }
            if (hmin < SEA + 2 || hmax - hmin > 20) continue;
            const Biome cb = surface((bx0 + bx1) / 2, (bz0 + bz1) / 2).biome;
            const bool sandy = cb == Biome::Desert || cb == Biome::Badlands;
            const bool lush = cb == Biome::Jungle || cb == Biome::Swamp || cb == Biome::DarkForest;
            const bool oldWorld = R(12) < 0.4f && !sandy;
            const int style = int(R(13) * 4);
            const int wallH = (style == 1 ? 15 : 7) + int(R(5) * 9);
            const int doorSide = int(R(6) * 4), doorAt = 3 + int(R(7) * 6);
            const bool partition = R(8) < 0.5f && w > 16;
            const int px = bx0 + w / 2;
            auto material = [&](int x, int y, int z, int base) {
                float m = hashf(x, y, z, hs ^ 0x31u);
                float moss = (lush ? 0.45f : 0.18f) * (1.0f - std::clamp(float(y - base) / float(wallH), 0.0f, 1.0f));
                if (sandy) return m < 0.7f ? uint8_t(B_SANDSTONE) : uint8_t(B_COBBLE);
                if (oldWorld) {
                    if (m < moss * 0.5f) return uint8_t(B_MOSS);
                    return m < 0.55f ? uint8_t(B_CONCRETE) : m < 0.74f ? uint8_t(B_REBAR) : m < 0.86f ? uint8_t(B_RUST) : uint8_t(B_GRAVEL);
                }
                if (m < moss) return uint8_t(B_MOSSY);
                return m < 0.62f ? uint8_t(B_STONE_BRICKS) : uint8_t(B_COBBLE);
            };
            for (int z = std::max(bz0 - 4, z0c); z <= std::min(bz1 + 4, z0c + CS - 1); ++z)
                for (int x = std::max(bx0 - 4, x0c); x <= std::min(bx1 + 4, x0c + CS - 1); ++x) {
                    const int lx = x - x0c, lz = z - z0c;
                    const int base = tops[lz * CS + lx];
                    if (base < SEA || base > WH - 24) continue;
                    const bool inside = x > bx0 + 1 && x < bx1 - 1 && z > bz0 + 1 && z < bz1 - 1;
                    const bool inRect = x >= bx0 && x <= bx1 && z >= bz0 && z <= bz1;
                    bool wall = inRect && !inside;
                    if (style == 2 && wall && ((x - bx0 + z - bz0) % 7 > 2)) wall = false;
                    if (partition && x >= px && x <= px + 1 && inRect) wall = true;
                    int along = (doorSide < 2) ? x - bx0 : z - bz0;
                    bool onSide = (doorSide == 0 && z <= bz0 + 1) || (doorSide == 1 && z >= bz1 - 1) || (doorSide == 2 && x <= bx0 + 1) ||
                                  (doorSide == 3 && x >= bx1 - 1);
                    bool door = onSide && along >= doorAt && along < doorAt + 3;
                    if (wall) {
                        float n = over_.noise3(x * 0.12f, 5.0f, z * 0.12f) * 0.5f + 0.5f;
                        int hgt = int(float(wallH) * std::clamp(n * 1.25f - 0.05f, 0.0f, 1.0f)) + int(hashf(x, 1, z, hs) * 2.0f);
                        if (n < 0.28f) hgt = int(hashf(x, 2, z, hs) * 2.0f);
                        for (int y = base - 1; y > base - 4 && y > 1; --y)
                            if (!def(blk(lx, y, lz)).solid) blk(lx, y, lz) = material(x, y, z, base);
                        for (int y = base; y < base + hgt; ++y) {
                            if (door && y < base + 6) continue;
                            bool window = (y - base) >= 4 && (y - base) <= 5 && (along % 6) >= 2 && (along % 6) <= 3 && !onSide;
                            if (window && !(partition && x >= px && x <= px + 1)) {
                                if (oldWorld && hashf(x, y, z, hs ^ 0x61u) < 0.6f) blk(lx, y, lz) = B_GRATE;
                                continue;
                            }
                            if (hashf(x, y, z, hs ^ 0x55u) < 0.05f) continue;
                            blk(lx, y, lz) = material(x, y, z, base);
                        }
                        continue;
                    }
                    uint8_t& g = blk(lx, base - 1, lz);
                    if (inside && def(g).solid && hashf(x, 3, z, hs) < (oldWorld ? 0.8f : 0.55f))
                        g = oldWorld                    ? (hashf(x, 4, z, hs) < 0.75f ? uint8_t(B_CONCRETE) : uint8_t(B_GRAVEL))
                            : hashf(x, 4, z, hs) < 0.5f ? uint8_t(B_COBBLE)
                                                        : uint8_t(sandy ? B_SANDSTONE : B_STONE_BRICKS);
                    if (oldWorld && inside && blk(lx, base, lz) == B_AIR && hashf(x, 8, z, hs) < 0.025f)
                        blk(lx, base, lz) = hashf(x, 9, z, hs) < 0.6f ? uint8_t(B_RUST) : uint8_t(B_GRATE);
                    int dEdge = std::min({std::abs(x - bx0), std::abs(x - bx1), std::abs(z - bz0), std::abs(z - bz1)});
                    if (dEdge <= 3 && hashf(x, 5, z, hs) < 0.32f - dEdge * 0.06f && blk(lx, base, lz) == B_AIR) {
                        int hgt = 1 + (hashf(x, 6, z, hs) < 0.35f ? 1 : 0);
                        for (int y = base; y < base + hgt; ++y)
                            blk(lx, y, lz) = hashf(x, y, z, hs ^ 7u) < 0.25f ? uint8_t(B_GRAVEL) : material(x, y, z, base);
                    }
                }
            if (oldWorld) {
                int zz = bz0 + 4, yy = topOf(shape(bx0 + 4, zz), bx0 + 4, zz);
                for (int j = 0; j < 5; ++j) {
                    int xx = bx0 + 4 + j;
                    if (xx < x0c || xx >= x0c + CS || zz < z0c || zz >= z0c + CS || yy < 1 || yy > WH - 3) continue;
                    int lx = xx - x0c, lz = zz - z0c;
                    blk(lx, yy - 1, lz) = B_CONCRETE;
                    blk(lx, yy, lz) = j == 0 ? B_DYNAMO : j == 2 ? B_SWITCH_ON : j == 4 ? B_LAMP : B_CABLE;
                }
            }
            if (R(9) < 0.75f) {
                int bz = bz0 + 3 + int(R(10) * std::max(1, d - 6)), xs = bx0 + 3, xe = bx1 - 3 - int(R(11) * 4);
                for (int x = xs; x <= xe; ++x) {
                    if (x < x0c || x >= x0c + CS || bz < z0c || bz >= z0c + CS) continue;
                    int lx = x - x0c, lz = bz - z0c;
                    int y = tops[lz * CS + lx];
                    if (blk(lx, y, lz) != B_AIR) continue;
                    blk(lx, y, lz) = B_BEAM;
                    c.sec[y >> 5].setMeta(idx3(lx, y & 31, lz), axisMeta(1, x, y, bz));
                }
            }
        }
}

void Generator::decorate(Column& c, const int* tops, const Biome* biomes) const {
    const uint32_t hs = uint32_t(seed_);
    const float veg = settings_.vegetation;
    auto at = [&](int x, int y, int z) -> uint8_t& { return c.sec[y >> 5].blocks[idx3(x, y & 31, z)]; };
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            int level = std::max(SEA, int(c.riverLevel[z * CS + x]));
            if (biomes[z * CS + x] != Biome::Swamp || at(x, level - 1, z) != B_WATER || at(x, level, z) != B_AIR) continue;
            int wx = c.cx * CS + x, wz = c.cz * CS + z;
            if (tops[z * CS + x] >= level - 6 && detail_.noise2(wx * 0.08f, wz * 0.08f) > 0.1f && hashf(wx, 599, wz, hs) < 0.055f * veg)
                at(x, level, z) = B_LILY_PAD;
        }
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            int y = tops[z * CS + x];
            if (y >= WH - 8 || y < SEA) continue;
            uint8_t ground = at(x, y - 1, z);
            if (at(x, y, z) != B_AIR) continue;
            Biome b = biomes[z * CS + x];
            int wx = c.cx * CS + x, wz = c.cz * CS + z;
            float r = hashf(wx, 1234, wz, hs), r2 = hashf(wx, 77, wz, hs);
            bool soil = ground == B_GRASS || ground == B_PODZOL || ground == B_SNOWY_GRASS || ground == B_COARSE_DIRT;
            if ((ground == B_SAND || ground == B_GRASS || ground == B_DIRT || ground == B_MUD) && r < 0.12f * veg) {
                bool water = false;
                const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (auto& d : D) {
                    int nx = x + d[0], nz = z + d[1];
                    if (nx >= 0 && nx < CS && nz >= 0 && nz < CS && at(nx, y - 1, nz) == B_WATER) water = true;
                }
                if (water) {
                    if (r2 < 0.55f) {
                        at(x, y, z) = B_CATTAIL;
                        continue;
                    }
                    int hgt = 3 + int(r2 * 4);
                    for (int k = 0; k < hgt && at(x, y + k, z) == B_AIR; ++k) at(x, y + k, z) = B_SUGAR_CANE;
                    continue;
                }
            }
            bool fireflyHabitat =
                b == Biome::Swamp || b == Biome::Jungle || b == Biome::Forest || b == Biome::BirchForest || b == Biome::Meadow;
            if (soil && fireflyHabitat && hashf(wx, 923, wz, hs) < .0025f * veg &&
                detail_.noise2(wx * .016f + 83, wz * .016f - 51) > .12f) {
                at(x, y, z) = B_FIREFLY_BUSH;
                continue;
            }
            float grass = 0, flower = 0, fern = 0, dead = 0;
            switch (b) {
            case Biome::ChalkDowns:
                grass = .09f;
                flower = .025f;
                break;
            case Biome::RockyMoor:
                grass = .07f;
                fern = .01f;
                break;
            case Biome::AutumnWoodland:
                grass = .045f;
                fern = .014f;
                break;
            case Biome::HeatherHighlands:
                grass = .04f;
                flower = .04f;
                break;
            case Biome::Plains:
                grass = 0.18f;
                flower = 0.012f;
                break;
            case Biome::Meadow:
                grass = 0.28f;
                flower = 0.09f;
                break;
            case Biome::Forest:
            case Biome::BirchForest:
                grass = 0.07f;
                flower = 0.006f;
                break;
            case Biome::DarkForest:
                grass = 0.04f;
                fern = 0.02f;
                break;
            case Biome::Taiga:
                fern = 0.07f;
                grass = 0.03f;
                break;
            case Biome::SnowyTaiga: fern = 0.02f; break;
            case Biome::Jungle:
                fern = 0.16f;
                grass = 0.22f;
                break;
            case Biome::Savanna: grass = 0.25f; break;
            case Biome::Swamp:
                grass = 0.06f;
                flower = 0.008f;
                break;
            case Biome::WindsweptHills: grass = 0.05f; break;
            case Biome::Desert:
            case Biome::Badlands: dead = 0.006f; break;
            default: break;
            }
            grass *= veg;
            flower *= veg;
            fern *= veg;
            dead *= veg;
            float patch = detail_.noise2(wx * 0.07f, wz * 0.07f) * 0.5f + 0.5f;
            flower *= patch > 0.60f ? 3.2f : 0.18f;
            float clearing = sm(-.12f, .45f, detail_.noise2(wx * .025f + 71, wz * .025f - 39));
            grass *= (0.10f + 0.85f * patch) * (.25f + .75f * clearing);
            fern *= patch > 0.56f ? .90f : .14f;
            if (soil && r < flower) {
                at(x, y, z) = b == Biome::HeatherHighlands ? B_HEATHER
                              : b == Biome::Swamp          ? B_BLUE_FLOWER
                                                           : (r2 < 0.4f    ? B_FLOWER_RED
                                                              : r2 < 0.75f ? B_FLOWER_YELLOW
                                                                           : B_BLUE_FLOWER);
            } else if (soil && r < flower + fern) {
                at(x, y, z) = B_FERN;
            } else if (soil && r < flower + fern + grass) {
                bool field = b == Biome::Plains || b == Biome::Meadow || b == Biome::Savanna;
                if (field && patch > 0.65f && r2 < 0.16f)
                    at(x, y, z) = B_WILD_WHEAT;
                else if ((b == Biome::Meadow || b == Biome::Jungle || b == Biome::Swamp) && patch > .57f && r2 < 0.24f &&
                         at(x, y + 1, z) == B_AIR) {
                    at(x, y, z) = B_LONG_GRASS;
                    at(x, y + 1, z) = B_LONG_GRASS_TOP;
                } else
                    at(x, y, z) = B_TALL_GRASS;
            } else if ((ground == B_SAND || ground == B_RED_SAND || ground == B_COARSE_DIRT || ground == B_TERRACOTTA) && r < dead) {
                at(x, y, z) = r2 < 0.35f ? B_BARREL_CACTUS : B_DEAD_BUSH;
            }
            if (at(x, y, z) == B_AIR) {
                bool woods = b == Biome::AutumnWoodland || b == Biome::Forest || b == Biome::BirchForest || b == Biome::DarkForest ||
                             b == Biome::Taiga || b == Biome::Jungle;
                float debris = hashf(wx, 911, wz, hs);
                if (woods && soil && debris < 0.014f * (0.5f + patch) * veg)
                    at(x, y, z) = r2 < 0.32f ? B_ROOT : B_BRANCH;
                else if ((ground == B_GRAVEL || ground == B_STONE || ground == B_SAND || ground == B_COARSE_DIRT) && debris < 0.012f * veg)
                    at(x, y, z) = B_PEBBLES;
            }
        }

    constexpr int CELL = 10, MARGIN = 16;
    int x0 = c.cx * CS - MARGIN, x1 = c.cx * CS + CS + MARGIN, z0 = c.cz * CS - MARGIN, z1 = c.cz * CS + CS + MARGIN;
    std::vector<LandmarkSite> clearings;
    for (int rz = floorDiv(z0 - 30, 128); rz <= floorDiv(z1 + 30, 128); ++rz)
        for (int rx = floorDiv(x0 - 30, 128); rx <= floorDiv(x1 + 30, 128); ++rx) {
            LandmarkSite site;
            if (landmarkSite(rx, rz, site) && site.kind >= 5 && site.kind <= 7) clearings.push_back(site);
        }
    for (int gz = floorDiv(z0, CELL); gz <= floorDiv(z1, CELL); ++gz)
        for (int gx = floorDiv(x0, CELL); gx <= floorDiv(x1, CELL); ++gx) {
            uint32_t h = hash3i(gx, 7, gz, hs);
            int tx = (gx * CELL + int(h % 8)) & ~1, tz = (gz * CELL + int((h >> 8) % 8)) & ~1;
            bool reserved = false;
            for (const auto& site : clearings)
                if (std::abs(tx - site.x) < 26 && std::abs(tz - site.z) < 26) {
                    reserved = true;
                    break;
                }
            if (reserved) continue;
            float roll = float((h >> 16) & 1023) / 1024.0f, pick = float((h >> 26) & 63) / 64.0f;
            if (roll > 0.96f) continue;
            SurfaceInfo s = surface(tx, tz);
            float chance = 0;
            int kind = 0;
            switch (s.biome) {
            case Biome::ChalkDowns:
                chance = .015f;
                kind = 1;
                break;
            case Biome::RockyMoor:
                chance = .025f;
                kind = 13;
                break;
            case Biome::AutumnWoodland:
                chance = .35f;
                kind = pick < .82f ? 15 : 1;
                break;
            case Biome::HeatherHighlands:
                chance = .012f;
                kind = 2;
                break;
            case Biome::VolcanicCoast:
                chance = .008f;
                kind = 13;
                break;
            case Biome::Forest:
                chance = 0.55f;
                kind = pick < 0.72f ? 0 : pick < 0.94f ? 1 : 11;
                break;
            case Biome::BirchForest:
                chance = 0.55f;
                kind = pick < 0.7f ? 1 : 9;
                break;
            case Biome::DarkForest:
                chance = 0.9f;
                kind = pick < 0.68f ? 7 : pick < 0.9f ? 0 : 11;
                break;
            case Biome::Plains:
                chance = 0.035f;
                kind = pick < 0.85f ? 0 : 11;
                break;
            case Biome::Meadow:
                chance = 0.012f;
                kind = pick < 0.6f ? 0 : 1;
                break;
            case Biome::Taiga:
                chance = 0.5f;
                kind = pick < 0.84f ? 2 : pick < 0.94f ? 14 : 13;
                break;
            case Biome::SnowyTaiga:
                chance = 0.42f;
                kind = pick < 0.9f ? 2 : 14;
                break;
            case Biome::SnowyPlains:
                chance = 0.02f;
                kind = 2;
                break;
            case Biome::IceSpikes:
                chance = 0.08f;
                kind = 12;
                break;
            case Biome::Jungle:
                chance = 0.95f;
                kind = pick < 0.45f ? 4 : pick < 0.6f ? 5 : 10;
                break;
            case Biome::Savanna:
                chance = 0.09f;
                kind = pick < 0.88f ? 6 : 0;
                break;
            case Biome::Swamp:
                chance = 0.2f;
                kind = 8;
                break;
            case Biome::WindsweptHills:
                chance = 0.08f;
                kind = pick < 0.7f ? 2 : 0;
                break;
            case Biome::Desert:
                chance = 0.06f;
                kind = 3;
                break;
            case Biome::Badlands:
                chance = 0.015f;
                kind = 3;
                break;
            default: break;
            }
            chance = std::clamp(chance * veg, 0.0f, 1.0f);
            if (roll >= chance) continue;
            if (s.height <= SEA + 1 && s.biome != Biome::Swamp) continue;
            if (s.height <= SEA - 1) continue;
            int w = (kind == 5 || kind == 7 || kind == 14) ? 4 : 2;
            int hmin = s.height, hmax = s.height;
            for (int d = 1; d < 4; ++d) {
                int hh = surface(tx + (d & 1) * (w - 1), tz + (d >> 1) * (w - 1)).height;
                hmin = std::min(hmin, hh);
                hmax = std::max(hmax, hh);
            }
            if (hmax - hmin > (kind == 12 || kind == 13 ? 3 : 1)) continue;
            tree(c, kind, tx, hmin, tz, h * 2654435761u);
        }

    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x)
            for (int y = WH - 3; y > SEA - 8; --y) {
                if (at(x, y, z) == B_VINE && at(x, y + 1, z) != B_VINE && !def(at(x, y + 1, z)).solid) at(x, y, z) = B_AIR;
                if (at(x, y, z) == B_LONG_GRASS_TOP && at(x, y - 1, z) != B_LONG_GRASS) at(x, y, z) = B_AIR;
                if (at(x, y, z) == B_LONG_GRASS && at(x, y + 1, z) != B_LONG_GRASS_TOP) at(x, y, z) = B_AIR;
            }
    for (int z = 0; z < CS; ++z)
        for (int x = 0; x < CS; ++x) {
            Biome b = biomes[z * CS + x];
            if (!isSnowy(b)) continue;
            for (int y = WH - 3; y > SEA; --y) {
                uint8_t v = at(x, y, z);
                if (v == B_AIR) continue;
                const BlockDef& d = def(v);
                if ((d.render == Render::Opaque || d.render == Render::Leaves) && v != B_SNOW && v != B_PACKED_ICE && v != B_ICE)
                    at(x, y + 1, z) = B_SNOW;
                break;
            }
        }
    (void)isWater;
}

void Generator::findSpawn(int& x, int& y, int& z) const {
    for (int r = 0; r < 6000; r += 8)
        for (int a = 0; a < 16; ++a) {
            float ang = a * (2 * 3.14159265f / 16);
            int px = int(std::cos(ang) * r), pz = int(std::sin(ang) * r);
            SurfaceInfo s = surface(px, pz);
            Biome b = s.biome;
            if (s.height > SEA + 2 && (b == Biome::Plains || b == Biome::Forest || b == Biome::Meadow || b == Biome::BirchForest ||
                                       b == Biome::Savanna || b == Biome::Taiga)) {
                x = px;
                y = s.height + 1;
                z = pz;
                return;
            }
        }
    x = 0;
    z = 0;
    y = surface(0, 0).height + 2;
}

}
