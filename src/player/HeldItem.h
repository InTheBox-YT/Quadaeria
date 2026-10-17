#pragma once
#include "core/Mat3.h"
#include "gfx/Renderer.h"
#include "world/Block.h"
#include <vector>

namespace qc {
void drawHeldItem(std::vector<BoxInstance>& out, ItemId item, const mat3& A, vec3 t, float sky, float blk);

}
