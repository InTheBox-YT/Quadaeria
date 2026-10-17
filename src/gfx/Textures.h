#pragma once
#include <cstdint>
#include <vector>

namespace qc {
constexpr int TEX_SIZE = 16;

std::vector<uint32_t> loadTextures(int& layerCount);

}
