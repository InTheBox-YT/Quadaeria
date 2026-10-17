#pragma once
#include "world/World.h"

namespace qc {
constexpr int LIGHT_PAD = 15;

void computeColumnLight(const Region& r, int cx, int cz, uint8_t* out, uint16_t* radiance = nullptr);

void computeRegionLight(Region& r, bool withRadiance = true);

}
